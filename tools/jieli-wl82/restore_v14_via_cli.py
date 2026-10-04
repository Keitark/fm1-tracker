"""Restore the pinned pre-NES V14 backup through the existing CLI orchestrator."""
import argparse
import hashlib
from pathlib import Path
import deploy_v14_smb1_via_cli as deployment


def validate_inputs():
    stock = deployment.BASELINE.read_bytes()
    nes = deployment.IMAGE.read_bytes()
    second = deployment.BASELINE.parent.parent/'pass-02/fm1-direct-1MiB.bin'
    if len(stock) != 1048576 or len(nes) != 1048576:
        raise ValueError('Wrong image length')
    if hashlib.sha256(stock).hexdigest() != deployment.BASE_SHA:
        raise ValueError('V14 backup hash mismatch')
    if hashlib.sha256(nes).hexdigest() != deployment.IMAGE_SHA:
        raise ValueError('NES source image hash mismatch')
    if second.read_bytes() != stock:
        raise ValueError('Independent V14 backup mismatch')
    changed = [a for a in range(0,1048576,4096)
               if stock[a:a+4096] != nes[a:a+4096]]
    if changed != sorted(deployment.SECTORS):
        raise ValueError('Restore scope differs from the 41 NES application sectors')
    return nes, stock


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args()
    if not args.directory.is_dir() or list(args.directory.glob('*.bin')):
        raise ValueError('Use a fresh existing restore directory')
    nes, stock = validate_inputs()
    deployment.deploy(args.device, args.directory, nes, stock)


if __name__ == '__main__':
    main()
