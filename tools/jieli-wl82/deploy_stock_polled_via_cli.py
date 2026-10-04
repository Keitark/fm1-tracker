"""Select the bounded stock pixel-polling experiment; reuse existing writer."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import deploy_v14_smb1_via_cli as deployment

ROOT = deployment.ROOT
sys.path.insert(0, str(ROOT/'firmware/nes'))
import build_stock_polled as build

SOURCE_SHA = '8da9ba09455e10f9cd04666df10a32d7925aaf21efee9f6640631f39dba0d5ca'
SOURCE = ROOT/'private-backups/pre-os-write-20260928-025138/final-1.bin'
IMAGE_SHA = '444b5d8cea8a3ffa13d72e331e148a34e9f5af902965eb38f80d428dbad2b792'
APP_SHA = '1c1ecc0b62bc2ee922dcdc7fe48e1bb6f09a3f5fea330a34c9f536eff540cd82'
IMAGE = ROOT/'private-backups/stock-polled-white-20260928/fm1-v14-polled-white.UNQUALIFIED.bin'
SECTORS = list(range(0x5000, 0x31000, 4096))+[0x4000]


def validate_inputs():
    source, target = SOURCE.read_bytes(), IMAGE.read_bytes()
    for data, digest in ((source, SOURCE_SHA), (target, IMAGE_SHA)):
        if len(data) != 1048576 or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('Pinned complete image mismatch')
    regenerated, app, report = build.build()
    if (regenerated != target or hashlib.sha256(app).hexdigest() != APP_SHA
            or not all(report['checks'].values())):
        raise ValueError('Offline patch/package validation failed')
    changed = [a for a in range(0, 1048576, 4096) if source[a:a+4096] != target[a:a+4096]]
    if changed != sorted(SECTORS):
        raise ValueError('Unexpected exact sector scope')
    return source, target, SECTORS.copy()


def call(device, commands, log):
    return deployment.cli_call(device, commands, log, stock_power=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device')
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--verified-preflight', type=Path)
    parser.add_argument('--validate-only', action='store_true')
    args = parser.parse_args()
    source, target, sectors = validate_inputs()
    if args.validate_only:
        print(json.dumps(dict(source_sha256=SOURCE_SHA, target_sha256=IMAGE_SHA,
                              sectors=len(sectors), device_io=False)))
        return
    if not args.device or not args.directory or not args.verified_preflight:
        parser.error('Explicit device, directory and saved verified source are required')
    if not args.directory.is_dir() or any(p.name not in ('console.log', 'helper.log', 'offline-validation.log')
                                         for p in args.directory.iterdir()):
        raise ValueError('Use a fresh run directory')
    deployment.deploy(args.device, args.directory, source, target, call, sectors,
                      stock_power=True, verified_preflight=args.verified_preflight)


if __name__ == '__main__':
    main()
