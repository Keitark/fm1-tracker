"""Offline address-aware views of the hash-pinned FM-1_010 disassembly.

Direct pi32v2 PC-relative operands are relative to the end of the instruction.
Linear-sweep data and indirect calls are NOT resolved as a complete call graph.
"""
import argparse
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
ANALYSIS=ROOT/'private-backups/readback-20260926-024637/analysis'
APP_SHA='fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543'


def parse(line):
    match=re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\t(.*)',line)
    if not match:return None
    address=int(match[1],16);size=len(bytes.fromhex(match[2]));instruction=match[3]
    call=re.search(r'\b(call|goto) (-?0x[0-9a-f]+)(?:\s|$)',instruction)
    target=address+size+int(call[2],16) if call else None
    return address,instruction,target,call[1] if call else None


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--start',type=lambda x:int(x,0))
    parser.add_argument('--end',type=lambda x:int(x,0))
    parser.add_argument('--callers',type=lambda x:int(x,0))
    parser.add_argument('--context',type=int,default=6)
    area=parser.add_mutually_exclusive_group()
    area.add_argument('--ram',action='store_true')
    area.add_argument('--boot',action='store_true')
    args=parser.parse_args()
    if hashlib.sha256((ANALYSIS/'app-FM-1_010.bin').read_bytes()).hexdigest()!=APP_SHA:
        raise SystemExit('Unreviewed application; refuse fixed-address interpretation')
    file='ram-copy-linear-sweep.asm' if args.ram else 'app-linear-sweep.asm'
    if args.boot:
        boot=ANALYSIS/'bootloader/stock-boot-code.bin'
        if hashlib.sha256(boot.read_bytes()).hexdigest()!='fcaf033c5e10526353ecb11279a7556ac8645026d370c0267912cb4bfa7c7e4d':
            raise SystemExit('Unreviewed boot bank')
        file='bootloader/stock-boot-linear-sweep.asm'
    rows=[(line,parse(line)) for line in (ANALYSIS/file).read_text().splitlines()]
    selected=set()
    for index,(line,item) in enumerate(rows):
        if not item:continue
        address,_,target,kind=item
        if args.callers is not None:
            if kind=='call' and target==args.callers:
                selected.update(range(max(0,index-args.context),min(len(rows),index+2)))
        elif args.start is not None and args.start<=address<(args.end or args.start+256):
            selected.add(index)
    for index in sorted(selected):
        line,item=rows[index]
        suffix=f' ; direct {item[3]} target 0x{item[2]:08x}' if item and item[2] is not None else ''
        print(line+suffix)


if __name__=='__main__':main()
