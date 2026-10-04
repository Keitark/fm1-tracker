"""Scoped pre-OS checkpoint selector; device IO stays in existing jluboottool.py."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import deploy_v14_smb1_via_cli as deployment

ROOT=deployment.ROOT
sys.path.insert(0,str(ROOT/'firmware/nes'))
import build_stock_image as pack
from audit_boot import audit
from cpu1_guard import audit_application

BUILD=ROOT/'firmware/nes/build/boot-pre-os-smb1'
IMAGE_SHA='8da9ba09455e10f9cd04666df10a32d7925aaf21efee9f6640631f39dba0d5ca'
APP_SHA='0b08d4508208b971f332c951147b5ddaed028979908ddd83dce160992be48d24'
IMAGE=ROOT/'private-backups/nes-candidates/smb1/screen-first'/IMAGE_SHA/'fm1-nes-stock-layout.UNQUALIFIED.bin'
MARKER=ROOT/'private-backups/stock-marker-v14-white-20260928/fm1-v14-white-marker.UNQUALIFIED.bin'
MARKER_SHA='fcc6ef41fc48f5e89904c184af6f5ffa798e67ad2bb3cadf33d1c7905b562a69'
VERIFIED_MARKER=ROOT/'private-backups/stock-marker-write-20260928-023712/final-1.bin'
SECTORS=list(range(0x5000,0x31000,4096))+[0x4000]


def validate_inputs(restore_marker=False):
    marker,target=MARKER.read_bytes(),IMAGE.read_bytes()
    for data,digest in ((marker,MARKER_SHA),(target,IMAGE_SHA)):
        if len(data)!=1048576 or hashlib.sha256(data).hexdigest()!=digest:
            raise ValueError('Pinned complete image mismatch')
    if restore_marker:
        source,target=target,marker
    else:
        elf,app=pack.load_built_profile('screen-first','smb1',BUILD)
        if hashlib.sha256(app).hexdigest()!=APP_SHA:
            raise ValueError('Unreviewed pre-OS application')
        report=audit(elf,app,require_peripherals=True,rom='smb1',require_boot_trace=True,
                     require_board_power=True,require_screen_first=True)
        if not report['pre_os_display']['enabled']:
            raise ValueError('Pre-OS checkpoint missing')
        audit_application(app)
        regenerated,report=pack.build_candidate(deployment.BASELINE.read_bytes(),elf,app,rom='smb1')
        if regenerated!=target or not all(report['checks'].values()):
            raise ValueError('Pre-OS package mismatch')
        source=marker
    changed=[a for a in range(0,1048576,4096) if source[a:a+4096]!=target[a:a+4096]]
    if changed!=sorted(SECTORS):
        raise ValueError('Unexpected exact sector scope')
    return source,target,SECTORS.copy()


def call(device,commands,log):
    return deployment.cli_call(device,commands,log,stock_power=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device')
    parser.add_argument('--directory',type=Path)
    parser.add_argument('--verified-preflight',type=Path)
    parser.add_argument('--validate-only',action='store_true')
    parser.add_argument('--restore-marker',action='store_true')
    args=parser.parse_args()
    source,target,sectors=validate_inputs(args.restore_marker)
    if args.validate_only:
        print(json.dumps({'source_sha256':hashlib.sha256(source).hexdigest(),
                          'target_sha256':hashlib.sha256(target).hexdigest(),
                          'sectors':len(sectors),'device_io':False}))
        return
    if not args.device or not args.directory or not args.verified_preflight:
        parser.error('Explicit device, directory and saved verified source are required')
    if not args.directory.is_dir() or any(p.name not in ('console.log','helper.log','offline-validation.log')
                                         for p in args.directory.iterdir()):
        raise ValueError('Use a fresh run directory')
    deployment.deploy(args.device,args.directory,source,target,call,sectors,stock_power=True,
                      verified_preflight=args.verified_preflight)


if __name__=='__main__':main()
