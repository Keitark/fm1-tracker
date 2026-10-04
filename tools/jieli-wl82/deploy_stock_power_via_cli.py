"""Pinned stock-power NES bench candidate via existing jluboottool.py writer.

No new transport/flash algorithm. Requires the original RAM helper already
running. No automatic reset, retry, key command or whole-chip erase.
"""
import argparse
import hashlib
from pathlib import Path
import sys
import deploy_v14_smb1_via_cli as deployment
from deploy_display_t1_via_cli import T1, T1_SHA

APP_SHA = '8e1a218eaba2abc7330abaf9c2da30c7f7e75fc729d4c8788bee5c1ed5356445'
IMAGE_SHA = 'e1fc44a067d8bd97931ab216f7550b325f320ea1ee4f0fac86644b5b0b0b6ead'
IMAGE = deployment.ROOT/'private-backups/nes-candidates/smb1/peripherals'/IMAGE_SHA/'fm1-nes-stock-layout.UNQUALIFIED.bin'
BUILD = deployment.ROOT/'firmware/nes/build/boot-stock-power-smb1'
SECTORS = list(range(0x5000,0x31000,4096))+[0x4000]


def validate_inputs(restore_v14=False):
    stock, source, target = (p.read_bytes() for p in (deployment.BASELINE, T1, IMAGE))
    for blob, expected in ((stock,deployment.BASE_SHA),(source,T1_SHA),(target,IMAGE_SHA)):
        if len(blob)!=1048576 or hashlib.sha256(blob).hexdigest()!=expected:
            raise ValueError('Pinned source/target/rollback image mismatch')
    if not restore_v14:
        sys.path.insert(0,str(deployment.ROOT/'firmware/nes'))
        import build_stock_image as pack
        from audit_boot import audit
        elf,app=pack.load_built_profile('peripherals','smb1',BUILD)
        if hashlib.sha256(app).hexdigest()!=APP_SHA:
            raise ValueError('Unreviewed stock-power application')
        audit(elf,app,require_peripherals=True,rom='smb1',require_boot_trace=True,require_board_power=True)
        regenerated,report=pack.build_candidate(stock,elf,app,rom='smb1')
        if regenerated!=target or not all(report['checks'].values()):
            raise ValueError('Current package differs from pinned stock-power image')
    else:
        source,target=target,stock
    changed=[a for a in range(0,1048576,4096) if source[a:a+4096]!=target[a:a+4096]]
    if changed!=sorted(SECTORS):
        raise ValueError('Unexpected exact 45-sector deployment scope')
    return source,target,SECTORS.copy()


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


if __name__=='__main__':main()
