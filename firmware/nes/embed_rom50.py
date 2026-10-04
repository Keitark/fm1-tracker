"""Generate a read-only C asset from the exact user-selected ROM; no device I/O."""
import argparse
import hashlib
from pathlib import Path

SHA256 = '6cd4e6052a16ce014cbd0f175830b77ebe4b236615409f2156de00179cc8311d'
HEADER = bytes.fromhex('4e45531a020231000000000000000000')
ROMS = {
    'rom50': (49168, HEADER, SHA256),
    'smb1': (40976, bytes.fromhex('4e45531a020101000000000000000000'),
             'f61548fdf1670cffefcc4f0b7bdcdd9eaba0c226e3b74f8666071496988248de'),
}


def rom_path(name):
    here=Path(__file__).resolve().parent
    if name=='rom50':return here/'roms/50-coin-shine.nes'
    if name=='smb1':return here.parents[1]/'private-backups/roms/smb1.nes'
    raise ValueError('Unknown cartridge')


def generate(source: Path, output: Path, cartridge='rom50'):
    size,header,digest=ROMS[cartridge]
    rom = source.read_bytes()
    if len(rom) != size or rom[:16] != header or hashlib.sha256(rom).hexdigest() != digest:
        raise ValueError('Selected ROM does not match the reviewed size/header/SHA-256; refusing integration')
    rows = [','.join(f'0x{x:02x}' for x in rom[i:i+16]) + ',' for i in range(0, len(rom), 16)]
    code = ('/* Generated local game asset. Do not redistribute without checking its license. */\n'
            '#include "fm1_rom50.h"\n'
            'static const uint8_t cartridge[] = {\n' + '\n'.join(rows) + '\n};\n'
            'const uint8_t *fm1_rom50_data(size_t *size) {\n'
            '    if(size) *size=sizeof(cartridge);\n'
            '    return cartridge;\n}\n'
            'int fm1_rom50_start(void) {\n'
            '    return fm1_nes_target_start(cartridge,sizeof(cartridge));\n}\n')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(code, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--cartridge',choices=tuple(ROMS),default='rom50')
    args = parser.parse_args()
    generate(args.source, args.output, args.cartridge)
