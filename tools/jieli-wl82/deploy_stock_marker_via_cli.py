"""V14 white-frame experiment; ALL USB operations use existing jluboottool.py."""
import argparse
from pathlib import Path
import sys

import deploy_v14_smb1_via_cli as deployment
from deploy_cpu1_guard_via_cli import IMAGE as PREVIOUS, IMAGE_SHA as PREVIOUS_SHA

sys.path.insert(0, str(deployment.ROOT/'firmware/nes'))
import build_stock_marker as marker

BUILD = deployment.ROOT/'private-backups/stock-marker-v14-white-20260928'
IMAGE = BUILD/'fm1-v14-white-marker.UNQUALIFIED.bin'
SECTORS = list(range(0x5000, 0x31000, 4096)) + [0x4000]


def validate_inputs(restore_v14=False):
    target, _, _ = marker.build()
    if IMAGE.read_bytes() != target:
        raise ValueError('Stored marker differs from regenerated reviewed patch')
    if restore_v14:
        source, target = target, marker.pinned(deployment.BASELINE, deployment.BASE_SHA)
        sectors = [0x25000, 0x4000]
    else:
        source = marker.pinned(PREVIOUS, PREVIOUS_SHA)
        sectors = SECTORS.copy()
    changed = [a for a in range(0, 1048576, 4096) if source[a:a+4096] != target[a:a+4096]]
    if changed != sorted(sectors):
        raise ValueError('Unexpected exact sector scope')
    return source, target, sectors


def call(device, commands, log):
    return deployment.cli_call(device, commands, log, stock_power=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--restore-v14', action='store_true')
    parser.add_argument('--verified-preflight', type=Path,
                        help='Reuse the just-completed matching read; skip further pre-write reads')
    args = parser.parse_args()
    if not args.directory.is_dir() or list(args.directory.iterdir()):
        # The elevation wrapper creates only console.log before entry.
        if not args.directory.is_dir() or any(p.name != 'console.log' for p in args.directory.iterdir()):
            raise ValueError('Use a fresh run directory')
    source, target, sectors = validate_inputs(args.restore_v14)
    deployment.deploy(args.device, args.directory, source, target, call, sectors, stock_power=True,
                      verified_preflight=args.verified_preflight)


if __name__ == '__main__':
    main()
