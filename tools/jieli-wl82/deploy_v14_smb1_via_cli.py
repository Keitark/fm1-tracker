"""Scoped orchestration of existing jluboottool.py commands, not a USB writer.

Experimental bench deployment, explicitly requested by the user. No automatic
reset/retry/rollback, helper upload, key operation, or whole-chip erase.
Readback is required but does not qualify boot, NOR timing, or recovery.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT/'tools/jl-uboot-tool/jluboottool.py'
BASELINE = ROOT/'private-backups/post-official-update-20260927-230806/pass-01/fm1-direct-1MiB.bin'
BASE_SHA = 'bcf61795e2ddb201f2f72180ec8f3dca830c84a7c64d23d08475412dbcfe2f9c'
IMAGE_SHA = '9873314f769c8b0670c6b03dab4ec2d84b29b473dd5ba16d23fd68586a8c59b8'
IMAGE = ROOT/'private-backups/nes-candidates/smb1/peripherals'/IMAGE_SHA/'fm1-nes-stock-layout.UNQUALIFIED.bin'
SECTOR = 4096
SECTORS = list(range(0x5000,0x2d000,SECTOR)) + [0x4000]


def validate_inputs():
    sys.path.insert(0,str(ROOT/'firmware/nes'))
    import build_stock_image as pack
    baseline, image = BASELINE.read_bytes(), IMAGE.read_bytes()
    if hashlib.sha256(baseline).hexdigest()!=BASE_SHA or hashlib.sha256(image).hexdigest()!=IMAGE_SHA:
        raise ValueError('Pinned baseline/candidate hash mismatch')
    elf, app = pack.load_built_profile('peripherals','smb1')
    regenerated, report = pack.build_candidate(baseline,elf,app,rom='smb1')
    if regenerated!=image or report['changed_sector_offsets']!=[hex(i) for i in sorted(SECTORS)]:
        raise ValueError('Rebuilt candidate or changed-sector scope mismatch')
    return baseline,image


def cli_call(device,commands,log,stock_power=False,nes_cdc=False):
    args=[sys.executable,'-u',str(CLI),'--device',device,'--chip','wl82',
          '--reuse-loader','--io-size','256','--fm1-bench',
          *(['--fm1-stock-power'] if stock_power else []),
          *(['--fm1-nes-cdc'] if nes_cdc else []),*commands]
    with log.open('x',encoding='utf-8') as stream:
        subprocess.run(args,stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=60)


def deploy(device,directory,baseline,image,call=cli_call,sectors=None,stock_power=False,
           verified_preflight=None,nes_cdc=False):
    sectors=list(SECTORS if sectors is None else sectors)
    if len(baseline)!=1048576 or len(image)!=1048576:
        raise ValueError('Wrong full image length')
    changed=[a for a in range(0,1048576,SECTOR) if baseline[a:a+SECTOR]!=image[a:a+SECTOR]]
    if stock_power and nes_cdc:raise ValueError('Conflicting deployment scopes')
    # Use the whole verified V14 app allocation, not a per-build cutoff.
    # The last erase sector shares bytes with config: require those unchanged.
    app_start,app_end=0x4120,0x4120+584956
    if nes_cdc:
        for start,end in ((0,0x4000),(0x4040,app_start),(app_end,1048576)):
            if baseline[start:end]!=image[start:end]:
                raise ValueError('Candidate changes boot/config/reserved bytes outside the stock application')
    allowed=list(range(0x4000,((app_end+SECTOR-1)//SECTOR)*SECTOR,SECTOR)) if nes_cdc else (list(range(0x4000,0x31000,SECTOR)) if stock_power else SECTORS)
    if sorted(sectors)!=changed or any(a not in allowed for a in sectors):
        raise ValueError('Sector list must exactly cover changes within the established application scope')
    if sectors[-1:]!=[0x4000]:
        raise ValueError('Application directory must be written last')
    if verified_preflight is not None:
        verified_preflight=Path(verified_preflight).resolve()
        if verified_preflight.read_bytes()!=baseline:
            raise ValueError('Provided completed read does not match expected source')
    report={'status':'preflight','writer':'existing jluboottool.py write command',
            'baseline_sha256':hashlib.sha256(baseline).hexdigest(),
            'candidate_sha256':hashlib.sha256(image).hexdigest(),
            'write_command_attempted':False,'verified_sectors':[],
            'hardware_boot_verified':False,'automatic_reset':False,
            'preflight_mode':'reuse_completed_read_no_further_prewrite_reads' if verified_preflight else 'two_fresh_full_reads_and_sector_preimages',
            'reused_preflight_file':str(verified_preflight) if verified_preflight else None}
    receipt=directory/'deployment.json'
    if receipt.exists():
        raise FileExistsError('Existing deployment receipt')
    def save():
        receipt.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        if verified_preflight is None:
            for i in (1,2):
                path=directory/f'preflight-{i}.bin'
                call(device,[f'read 0 1048576 {path}'],directory/f'preflight-{i}.log')
                if path.read_bytes()!=baseline:
                    raise ValueError('Fresh full read differs from the expected source image; no writes permitted')
            print('Two fresh full reads match expected source image.',flush=True)
        else:
            print('Using the completed matching read at user request; no further pre-write reads.',flush=True)
        for address in sectors:
            report['status']='sector_preparation';report['current_sector']=hex(address);save()
            source=directory/f'sector-{address:06x}.bin'
            restore=directory/f'restore-{address:06x}.bin'
            source.write_bytes(image[address:address+SECTOR])
            restore.write_bytes(baseline[address:address+SECTOR])
            before=directory/f'before-{address:06x}.bin'
            after=directory/f'after-{address:06x}.bin'
            if verified_preflight is None:
                call(device,[f'read {address} {SECTOR} {before}'],directory/f'before-{address:06x}.log')
                if before.read_bytes()!=baseline[address:address+SECTOR]:
                    raise ValueError(f'Sector preimage mismatch at {address:#x}')
            report['status']='sector_write_outcome_pending'
            report['write_command_attempted']=True;save()
            # Existing CLI write erases only this 4 KiB sector, checks erased
            # bytes, programs in 256-byte chunks, then checks sector readback.
            call(device,[f'write {address} {source}',f'read {address} {SECTOR} {after}'],
                 directory/f'write-{address:06x}.log')
            if after.read_bytes()!=image[address:address+SECTOR]:
                raise ValueError(f'Independent sector readback mismatch at {address:#x}')
            report['verified_sectors'].append(hex(address));report['status']='sector_verified';save()
            print(f'Verified sector {len(report["verified_sectors"])}/{len(sectors)}: {address:#x}',flush=True)
        report['status']='full_verification';save()
        path=directory/'final-1.bin'
        call(device,[f'read 0 1048576 {path}'],directory/'final-1.log')
        if path.read_bytes()!=image:
            raise ValueError('Final full read mismatch; do not reset or retry')
        report['status']='written_and_readback_verified'
        report['full_readback_count']=1
        report['full_readback_sha256']=hashlib.sha256(image).hexdigest()
        print('Full candidate verified once. Helper left running; boot not tested.',flush=True)
    except Exception as error:
        report['status']='failed'
        report['error']=f'{type(error).__name__}: {error}'
        raise
    finally:
        save()
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',required=True)
    parser.add_argument('--directory',required=True,type=Path)
    args=parser.parse_args()
    if not args.directory.is_dir() or list(args.directory.glob('*.bin')):
        raise ValueError('Use a fresh existing run directory')
    baseline,image=validate_inputs()
    deploy(args.device,args.directory,baseline,image)


if __name__=='__main__':
    main()
