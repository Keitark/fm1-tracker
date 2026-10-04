"""Offline address-window viewer for pinned FM-1 disassembly; no device I/O."""
import argparse
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
SOURCES={
    'nes':('firmware/nes/build/boot-peripherals-smb1',
           'fm1-nes-boot-candidate.app.bin','disassembly.asm',
           'a3e4f81914cceab7971d955832fa7b2821f3597a9dc27ac81a1cc2196e23216a'),
    'nes-refined':('firmware/nes/build/boot-refined-peripherals-smb1',
           'fm1-nes-boot-candidate.app.bin','disassembly.asm',
           '4e04dd69fc9cc8ded32f249b7d82733e9edca2bc1092c760010cf0f4093eda00'),
    'v14':('private-backups/post-official-update-20260927-230806/analysis',
           'app-FM-1_010.bin','app-linear-sweep.asm',
           '54a32371e8fc19e7492210e958023762f7335e354a4cab41cb26c35d6f0f0443'),
    't1':('firmware/nes/build/boot-display-smb1',
          'fm1-nes-boot-candidate.app.bin','disassembly.asm',
          'e42b44f2847bb731bf02de5aaf4a8801300273ce4d8ce60e7c1677a97c5877fa'),
}
def rows(profile):
    folder,app,listing,sha=SOURCES[profile]
    if hashlib.sha256((ROOT/folder/app).read_bytes()).hexdigest()!=sha:
        raise ValueError('Application hash mismatch')
    for line in (ROOT/folder/listing).read_text().splitlines():
        m=re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\t(.*)',line)
        if not m:continue
        address=int(m[1],16);size=len(bytes.fromhex(m[2]));instruction=m[3]
        branch=re.search(r'\b(call|goto) (-?(?:0x[0-9a-f]+|[0-9]+))(?:\s|$)',instruction)
        target=address+size+int(branch[2],0) if branch else None
        yield address,line,target,branch[1] if branch else None

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile',choices=SOURCES)
    parser.add_argument('--start',type=lambda s:int(s,0))
    parser.add_argument('--end',type=lambda s:int(s,0))
    parser.add_argument('--callers',type=lambda s:int(s,0))
    args=parser.parse_args()
    for address,line,target,kind in rows(args.profile):
        selected=(kind=='call' and target==args.callers) if args.callers is not None else (
            args.start is not None and args.start<=address<(args.end or args.start+256))
        if selected:print(line+(f' ; {kind} -> {target:#010x}' if target is not None else ''))

if __name__=='__main__':main()
