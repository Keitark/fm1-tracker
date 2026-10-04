"""Offline, private stock-layout image construction. NO device/downloader access.

Preserves this unit's boot/config/reserved bytes and all stock file offsets.
Output is an UNQUALIFIED raw flash candidate, not an official update container.
Never serialize decoded configuration or the in-memory encoding key.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'tools/jieli-wl82'))
from compare_stock_images import _decode_layout, decode_application, jl_crc16, jl_sfc_cipher
from audit_stock_bootloader import BACKUP_SHA, decode as decode_boot
from audit_boot import audit

BACKUP=ROOT/'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin'
AREA=0x4000
APP=0x4120
STOCK_APP_SIZE=583228
AREA_SIZE=583899
SECTOR=4096
CURRENT_BACKUP=ROOT/'private-backups/original-reader-repeat-20260927-220410/fm1-flash-1MiB.bin'
CURRENT_SHA='4d69ef5bbd662b0b762dc01956378f7c1776b317e463ccc15815cd5bc0b338a3'
V14_BACKUP=ROOT/'private-backups/post-official-update-20260927-230806/pass-01/fm1-direct-1MiB.bin'
V14_SHA='bcf61795e2ddb201f2f72180ec8f3dca830c84a7c64d23d08475412dbcfe2f9c'


def require(condition,message):
    if not condition:raise ValueError(message)


def stock_layout(raw):
    digest=hashlib.sha256(raw).hexdigest()
    require(len(raw)==1048576 and digest in (BACKUP_SHA,V14_SHA),
            'Only the reviewed, exact 1 MiB backup is accepted')
    app_size,area_size,cfg_offset=((584956,585627,585244) if digest==V14_SHA else
                                  (STOCK_APP_SIZE,AREA_SIZE,0x8e75c))
    fw,area,entries,_,base,key=_decode_layout(raw)
    require((base,area.hdr_off,area.offset,area.size,area.flags,area.index)==
            (AREA,AREA,0x02000120,area_size,0x83,0),'Stock application-area layout changed')
    require([(e.name,e.hdr_off,e.offset,e.size,e.flags) for e in entries]==[
        ('app.bin',0x4020,0x120,app_size,0x82),
        ('cfg_tool.bin',0x4040,cfg_offset,383,0x82),
        ('VM',0x4060,0x94000,348160,0x12),
        ('PRCT',0x4080,0,606208,0x92),
        ('BTIF',0x40a0,0xe9000,4096,0x92),
        ('USR',0x40c0,0xea000,73728,0x92)],'Stock directory layout changed')
    require(jl_crc16(fw[AREA+32:AREA+area.size])==area.data_crc,'Stock area CRC mismatch')
    for e in entries[:2]:
        require(jl_crc16(fw[e.data_offset:e.data_offset+e.data_size])==e.data_crc,
                'Stock file CRC mismatch')
    return fw,area,entries,key


def verify_candidate(raw,candidate,app):
    before,area,entries,_=stock_layout(raw)
    require(len(candidate)==len(raw),'Candidate must remain exactly 1 MiB')
    require(0<len(app)<=entries[0].size,'Application does not fit existing slot')
    # Nothing except two directory records and actual new application bytes
    # may change. This includes old unused application tail, cfg_tool, every
    # reserved entry, reserved payload, bootloader and flash header.
    for start,end in ((0,AREA),(AREA+64,APP),(APP+len(app),len(raw))):
        require(candidate[start:end]==raw[start:end],'Candidate altered preserved bytes')
    after,new_area,new_entries,_,base,_=_decode_layout(candidate)
    require(base==AREA and len(new_entries)==len(entries),'Candidate directory shape changed')
    require(after[AREA+4:AREA+32]==before[AREA+4:AREA+32],
            'Candidate changed area fields other than CRCs')
    old_app,new_app=entries[0],new_entries[0]
    require(after[AREA+32+4:AREA+32+8]==before[AREA+32+4:AREA+32+8] and
            after[AREA+32+12:AREA+64]==before[AREA+32+12:AREA+64],
            'Candidate changed app offset/flags/name')
    # Entry layout: hcrc:u16, dcrc:u16, offset:u32, size:u32, flags... .
    require(new_app.size==len(app) and new_app.data_offset==APP,'Candidate app size/placement mismatch')
    require(jl_crc16(after[AREA+32:AREA+new_area.size])==new_area.data_crc,
            'Candidate area payload CRC mismatch')
    decoded,info,layout=decode_application(candidate)
    require(decoded==app and info['data_crc_valid'],'Candidate application round-trip failed')
    require(layout['entry_point']=='0x2000120','Candidate entry changed')
    require(after[APP+len(app):APP+old_app.size]==before[APP+len(app):APP+old_app.size],
            'Unused stock application tail changed')
    for old,new in zip(entries[1:],new_entries[1:]):
        require(after[new.hdr_off:new.hdr_off+32]==before[old.hdr_off:old.hdr_off+32],
                'Non-application directory entry changed')
    cfg=new_entries[1]
    require(jl_crc16(after[cfg.data_offset:cfg.data_offset+cfg.data_size])==cfg.data_crc,
            'Preserved configuration-tool CRC mismatch')
    changed=[i for i,(a,b) in enumerate(zip(raw,candidate)) if a!=b]
    sectors=sorted({i//SECTOR*SECTOR for i in changed})
    return {'candidate_bytes':len(candidate),'candidate_sha256':hashlib.sha256(candidate).hexdigest(),
            'backup_sha256':hashlib.sha256(raw).hexdigest(),'application_bytes':len(app),
            'application_sha256':hashlib.sha256(app).hexdigest(),
            'application_flash_offset':hex(APP),'entry_point':layout['entry_point'],
            'stock_area_size_preserved':area.size,'unused_stock_app_tail_bytes':entries[0].size-len(app),
            'changed_bytes':len(changed),'changed_sector_count':len(sectors),
            'changed_sector_offsets':[hex(i) for i in sectors],
            'checks':{'application_round_trip':True,'all_directory_header_crcs':True,
                'area_payload_crc':True,'application_payload_crc':True,'configuration_tool_crc':True,
                'boot_and_config_byte_identical':True,'reserved_bytes_byte_identical':True,
                'all_file_and_reserved_offsets_preserved':True,'unused_app_tail_preserved':True},
            'flashable':False,'hardware_boot_verified':False,'device_operations_performed':False,
            'format':'raw flash; NOT a UFW/fwsc updater package',
            'privacy':'unit-specific image; retain privately, do not publish',
            'unverified':['SDK storage/initcall side effects','physical boot and peripherals',
                          'reviewed write/restore procedure; no writer provided']}


def _replace_application(raw,app):
    """Container transform only; caller must separately audit executable code."""
    fw,area,entries,key=stock_layout(raw)
    require(0<len(app)<=entries[0].size,'Application does not fit preserved stock slot')
    fw[APP:APP+len(app)]=app
    struct.pack_into('<H',fw,AREA+32+2,jl_crc16(app))
    struct.pack_into('<I',fw,AREA+32+8,len(app))
    struct.pack_into('<H',fw,AREA+32,jl_crc16(fw[AREA+34:AREA+64]))
    struct.pack_into('<H',fw,AREA+2,jl_crc16(fw[AREA+32:AREA+area.size]))
    struct.pack_into('<H',fw,AREA,jl_crc16(fw[AREA+2:AREA+32]))
    candidate=bytearray(raw)
    # Encoding is position dependent in 32-byte blocks. Re-encode only the
    # affected blocks, using the existing backup metadata in memory. Partial
    # final-block plaintext is retained, so surrounding ciphertext is exact.
    for start,end in ((AREA,AREA+64),(APP,(APP+len(app)+31)&~31)):
        jl_sfc_cipher(fw,start,end-start,AREA,key)
        candidate[start:end]=fw[start:end]
    candidate=bytes(candidate)
    return candidate


def build_candidate(raw,elf,app,require_peripherals=True,rom='rom50',display_only=False,usb_only=False,usb_peripheral_tests=False,usb_nes=False,usb_mdx=False,usb_audio=False,usb_mod=False):
    static=audit(elf,app,require_peripherals=require_peripherals,rom=rom,display_only=display_only,
                 usb_only=usb_only,usb_peripheral_tests=usb_peripheral_tests,usb_nes=usb_nes,usb_mdx=usb_mdx,usb_audio=usb_audio,usb_mod=usb_mod,require_boot_trace=usb_only,require_board_power=usb_only)
    original=BACKUP.read_bytes()
    require(raw[:AREA]==original[:AREA],'Boot/header/config differs from audited bootloader')
    _,boot_report=decode_boot(original)
    candidate=_replace_application(raw,app)
    report=verify_candidate(raw,candidate,app)
    report['static_boot_audit']=static
    report['stock_bootloader_handoff']=boot_report['handoff_evidence']
    return candidate,report


def load_built_profile(profile,rom='rom50',folder=None):
    """Read and validate current build inputs; no output files or device access."""
    require(profile in ('headless','peripherals','display','screen-first'),'Unknown build profile')
    require(rom in ('rom50','smb1'),'Unknown cartridge')
    prefixes={'headless':'boot-','peripherals':'boot-peripherals-','display':'boot-display-','screen-first':'boot-screen-first-'}
    folder=Path(folder) if folder is not None else HERE/'build'/(prefixes[profile]+rom)
    app=(folder/'fm1-nes-boot-candidate.app.bin').read_bytes()
    elf=(folder/'fm1-nes-boot-candidate.elf').read_bytes()
    manifest=json.loads((folder/'build-manifest.json').read_text())
    require(manifest.get('status')=='linked_'+profile+'_boot_candidate_UNTESTED',
            'Build manifest does not record a completed candidate')
    require(manifest.get('cartridge','rom50')==rom,'Wrong cartridge build')
    require(manifest['application_sha256']==hashlib.sha256(app).hexdigest(),'Stale build manifest')
    for path,expected in manifest['source_sha256'].items():
        require(hashlib.sha256(Path(path).read_bytes()).hexdigest()==expected,'Stale build source')
    for path,expected in manifest['linked_archive_sha256'].items():
        require(hashlib.sha256(Path(path).read_bytes()).hexdigest()==expected,'Stale linked archive')
    return elf,app


def preserve_current_baseline(original,candidate,current,app):
    """Preserve the exact two observed out-of-application baseline changes."""
    verify_candidate(original,candidate,app)
    require(len(current)==1048576 and hashlib.sha256(current).hexdigest()==CURRENT_SHA,
            'Unreviewed current backup')
    differences=[i for i,(a,b) in enumerate(zip(original,current)) if a!=b]
    require(differences==[0xee046,0xee069],'Current baseline difference changed')
    updated=bytearray(candidate)
    for offset in differences:updated[offset]=current[offset]
    # No scope expansion: only the same application sectors may differ from
    # the current baseline, and their entire contents must equal the audited image.
    allowed={i//SECTOR for i,(a,b) in enumerate(zip(original,candidate)) if a!=b}
    for start in range(0,len(current),SECTOR):
        expected=candidate if start//SECTOR in allowed else current
        require(updated[start:start+SECTOR]==expected[start:start+SECTOR],'Rebase changed sector scope')
    decoded,info,_=decode_application(bytes(updated))
    require(decoded==app and info['data_crc_valid'],'Rebased app differs')
    return bytes(updated)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile',choices=('headless','peripherals','display','screen-first'),default='peripherals')
    ap.add_argument('--rom',choices=('rom50','smb1'),default='smb1')
    ap.add_argument('--current-baseline',action='store_true')
    ap.add_argument('--baseline',choices=('v14','original'),default='v14')
    ap.add_argument('--build-folder',type=Path,help='Explicit separately audited build; default historical profile folder')
    args=ap.parse_args()
    elf,app=load_built_profile(args.profile,args.rom,args.build_folder)
    require(not(args.baseline=='v14' and args.current_baseline),
            '--current-baseline applies only to the historical original baseline')
    baseline=V14_BACKUP if args.baseline=='v14' else BACKUP
    if args.profile=='screen-first':
        audit(elf,app,require_peripherals=True,rom=args.rom,require_boot_trace=True,
              require_board_power=True,require_screen_first=True)
    candidate,report=build_candidate(baseline.read_bytes(),elf,app,args.profile in ('peripherals','screen-first'),args.rom,display_only=args.profile=='display')
    if args.baseline=='original' and (args.current_baseline or args.rom=='smb1'):
        candidate=preserve_current_baseline(BACKUP.read_bytes(),candidate,CURRENT_BACKUP.read_bytes(),app)
        report['source_original_backup_sha256']=report['backup_sha256']
        report['backup_sha256']=CURRENT_SHA
        report['candidate_sha256']=hashlib.sha256(candidate).hexdigest()
        report['preserved_current_delta_offsets']=['0xee046','0xee069']
    report['profile']=args.profile
    report['cartridge']=args.rom
    report['baseline_file']=str(baseline)
    report['baseline_version']=args.baseline
    report['packager_source_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    base=ROOT/'private-backups/nes-candidates'
    if args.rom!='rom50':base=base/args.rom
    out=base/args.profile/report['candidate_sha256']
    out.mkdir(parents=True,exist_ok=True)
    image=out/'fm1-nes-stock-layout.UNQUALIFIED.bin'
    if image.exists():require(image.read_bytes()==candidate,'Refuse to overwrite a different image')
    else:image.write_bytes(candidate)
    report['image']=str(image)
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
