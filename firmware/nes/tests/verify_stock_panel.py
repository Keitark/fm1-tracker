"""Read-only comparison of the recovered panel table with the private FM-1 app.

Refuses other firmware revisions. Prints hashes/offsets only, never device keys.
This verifies transcription, not physical panel operation or board startup.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

APP_SHA256='fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543'
HERE=Path(__file__).resolve().parents[1]
ROOT=HERE.parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,default=ROOT/'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin')
    args=parser.parse_args()
    app=args.app.read_bytes()
    digest=hashlib.sha256(app).hexdigest()
    if digest!=APP_SHA256:
        raise SystemExit('Unrecognized application hash: refuse to use these offsets')
    source=(HERE/'src/fm1_board.c').read_text(encoding='utf-8')
    declaration=re.search(r'panel_init\[21\]\[18\]\s*=\s*\{(.*?)\n\};',source,re.S)
    if not declaration: raise SystemExit('Panel declaration not found')
    table=bytearray()
    for record in re.findall(r'\{([^{}]*)\}',declaration.group(1)):
        values=[int(token.strip(),0) for token in record.split(',')]
        if len(values)>18: raise SystemExit('Oversized record')
        table.extend(bytes(values)+bytes(18-len(values)))
    # r12 = 0x0204eae0; table displacement = 0x164c; runtime base = 0x02000120.
    offset=0x0204eae0+0x164c-0x02000120
    if len(table)!=378 or bytes(table)!=app[offset:offset+378]:
        raise SystemExit('Panel table differs from the verified stock application')
    print(json.dumps({'status':'PASS_table_transcription_only','app_sha256':digest,
        'app_offset':hex(offset),'runtime_address':hex(offset+0x02000120),
        'records':21,'bytes':len(table),'table_sha256':hashlib.sha256(table).hexdigest(),
        'hardware_tested':False},indent=2))

if __name__=='__main__':main()
