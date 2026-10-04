"""Pinned T1 display diagnostic (or V14 restore), using existing CLI writer."""
import argparse
import hashlib
from pathlib import Path
import sys
import deploy_v14_smb1_via_cli as deployment

T1_SHA='1f56085b9aa6c9c9e1b852141f264cc465b9b929607628cfdabe4d770f769c4d'
T1=deployment.ROOT/'private-backups/nes-candidates/smb1/display'/T1_SHA/'fm1-nes-stock-layout.UNQUALIFIED.bin'

def validate_inputs(restore=False):
    stock=deployment.BASELINE.read_bytes();test=T1.read_bytes()
    if len(stock)!=1048576 or len(test)!=1048576:
        raise ValueError('Wrong image length')
    if hashlib.sha256(stock).hexdigest()!=deployment.BASE_SHA or hashlib.sha256(test).hexdigest()!=T1_SHA:
        raise ValueError('Pinned T1/V14 hash mismatch')
    if not restore:
        sys.path.insert(0,str(deployment.ROOT/'firmware/nes'))
        import build_stock_image as pack
        elf,app=pack.load_built_profile('display','smb1')
        regenerated,report=pack.build_candidate(stock,elf,app,False,'smb1',display_only=True)
        if regenerated!=test or not report['static_boot_audit']['display_only']:
            raise ValueError('Current diagnostic build differs from pinned T1')
    sectors=[a for a in range(0,1048576,4096) if stock[a:a+4096]!=test[a:a+4096]]
    if sectors!=list(range(0x4000,0x1f000,4096)):
        raise ValueError('Unexpected T1 write scope')
    sectors=sectors[1:]+[0x4000]
    return (test,stock,sectors) if restore else (stock,test,sectors)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',required=True)
    parser.add_argument('--directory',required=True,type=Path)
    parser.add_argument('--restore-v14',action='store_true')
    args=parser.parse_args()
    if not args.directory.is_dir() or list(args.directory.glob('*.bin')):
        raise ValueError('Use a fresh existing directory')
    source,target,sectors=validate_inputs(args.restore_v14)
    deployment.deploy(args.device,args.directory,source,target,sectors=sectors)

if __name__=='__main__':main()
