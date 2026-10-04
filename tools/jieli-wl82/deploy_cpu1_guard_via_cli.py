"""Pinned CPU1-divider experiment; delegates all device IO to existing writer."""
import argparse
import hashlib
from pathlib import Path
import sys
import deploy_v14_smb1_via_cli as deployment
from deploy_screen_first_via_cli import IMAGE as PREVIOUS, IMAGE_SHA as PREVIOUS_SHA

APP_SHA = '63476ce1f89cd6b83fe4a915396a07668bdfca49696d3917e91aff523508a6ea'
IMAGE_SHA = '51dc35fdf0bcbe4e105c4f84e5c4ee52e827f4a88169c81b65541488d5998aa1'
IMAGE = deployment.ROOT/'private-backups/nes-candidates/smb1/screen-first'/IMAGE_SHA/'fm1-nes-stock-layout.UNQUALIFIED.bin'
BUILD = deployment.ROOT/'firmware/nes/build/boot-cpu1-guard-smb1'
SECTORS = [0x5000,0x6000,0x7000,0x8000,0x9000,0xa000,0xb000,0xc000,
           0xd000,0xe000,0x17000,0x1c000,0x2a000,0x2b000,0x2c000,
           0x2d000,0x2e000,0x30000,0x4000]
RESTORE_SECTORS = list(range(0x5000,0x31000,4096)) + [0x4000]


def validate_inputs(restore_v14=False):
    stock, previous, target = (p.read_bytes() for p in (deployment.BASELINE, PREVIOUS, IMAGE))
    for data, digest in ((stock,deployment.BASE_SHA),(previous,PREVIOUS_SHA),(target,IMAGE_SHA)):
        if len(data)!=1048576 or hashlib.sha256(data).hexdigest()!=digest:
            raise ValueError('Pinned source/target/rollback mismatch')
    if restore_v14:
        source, target, sectors = target, stock, RESTORE_SECTORS
    else:
        sys.path.insert(0,str(deployment.ROOT/'firmware/nes'))
        import build_stock_image as pack
        from audit_boot import audit
        from cpu1_guard import audit_application
        elf, app = pack.load_built_profile('screen-first','smb1',BUILD)
        if hashlib.sha256(app).hexdigest()!=APP_SHA:
            raise ValueError('Unreviewed CPU1 guard application')
        audit(elf,app,require_peripherals=True,rom='smb1',require_boot_trace=True,
              require_board_power=True,require_screen_first=True)
        audit_application(app)
        regenerated, report = pack.build_candidate(stock,elf,app,rom='smb1')
        if regenerated!=target or not all(report['checks'].values()):
            raise ValueError('CPU1 guard package mismatch')
        source, sectors = previous, SECTORS
    changed = [a for a in range(0,1048576,4096) if source[a:a+4096]!=target[a:a+4096]]
    if changed!=sorted(sectors):
        raise ValueError('Unexpected exact deployment scope')
    return source,target,sectors.copy()


def call(device,commands,log):
    return deployment.cli_call(device,commands,log,stock_power=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',required=True)
    parser.add_argument('--directory',required=True,type=Path)
    parser.add_argument('--restore-v14',action='store_true')
    args=parser.parse_args()
    if not args.directory.is_dir() or list(args.directory.glob('*.bin')):
        raise ValueError('Use a fresh existing run directory')
    source,target,sectors=validate_inputs(args.restore_v14)
    deployment.deploy(args.device,args.directory,source,target,call,sectors,stock_power=True)


if __name__=='__main__':
    main()
