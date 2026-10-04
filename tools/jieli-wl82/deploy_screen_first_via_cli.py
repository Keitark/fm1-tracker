"""Pinned screen-first SMB1 bench deployment using the existing CLI writer.

This selects/validates artifacts only; USB, erase, program and readback remain
in jluboottool.py and deploy_v14_smb1_via_cli.py. No reset/retry/key operation.
The original RAM helper must already be running after a cold-UBOOT read.
"""
import argparse
import hashlib
from pathlib import Path
import sys
import deploy_v14_smb1_via_cli as deployment
from deploy_stock_power_via_cli import IMAGE as PREVIOUS, IMAGE_SHA as PREVIOUS_SHA

APP_SHA = 'eda772a719fe94600023d8d709ebbe972aa165386131036b59310769201c4a88'
IMAGE_SHA = '198140cbf6cb4d052ecc9fbb610ad0f9619acadb65888adee1e172827fc87e95'
IMAGE = deployment.ROOT/'private-backups/nes-candidates/smb1/screen-first'/IMAGE_SHA/'fm1-nes-stock-layout.UNQUALIFIED.bin'
BUILD = deployment.ROOT/'firmware/nes/build/boot-screen-first-reviewed-smb1'
SECTORS = list(range(0x5000, 0x31000, 4096)) + [0x4000]


def validate_inputs(restore_v14=False):
    stock, previous, target = (p.read_bytes() for p in (deployment.BASELINE, PREVIOUS, IMAGE))
    for blob, expected in ((stock, deployment.BASE_SHA), (previous, PREVIOUS_SHA), (target, IMAGE_SHA)):
        if len(blob) != 1048576 or hashlib.sha256(blob).hexdigest() != expected:
            raise ValueError('Pinned source/target/rollback image mismatch')
    if restore_v14:
        source, target = target, stock
    else:
        sys.path.insert(0, str(deployment.ROOT/'firmware/nes'))
        import build_stock_image as pack
        from audit_boot import audit
        elf, app = pack.load_built_profile('screen-first', 'smb1', BUILD)
        if hashlib.sha256(app).hexdigest() != APP_SHA:
            raise ValueError('Unreviewed screen-first application')
        audit(elf, app, require_peripherals=True, rom='smb1', require_boot_trace=True,
              require_board_power=True, require_screen_first=True)
        regenerated, report = pack.build_candidate(stock, elf, app, rom='smb1')
        if regenerated != target or not all(report['checks'].values()):
            raise ValueError('Current package differs from pinned screen-first image')
        source = previous
    changed = [a for a in range(0, 1048576, 4096) if source[a:a+4096] != target[a:a+4096]]
    if changed != sorted(SECTORS):
        raise ValueError('Unexpected exact 45-sector deployment scope')
    return source, target, SECTORS.copy()


def call(device, commands, log):
    return deployment.cli_call(device, commands, log, stock_power=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--restore-v14', action='store_true')
    args = parser.parse_args()
    if not args.directory.is_dir() or list(args.directory.glob('*.bin')):
        raise ValueError('Use a fresh existing run directory')
    source, target, sectors = validate_inputs(args.restore_v14)
    deployment.deploy(args.device, args.directory, source, target, call, sectors, stock_power=True)


if __name__ == '__main__':
    main()
