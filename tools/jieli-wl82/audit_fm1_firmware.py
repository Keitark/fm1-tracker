"""Offline structural audit and vendor pi32v2 disassembly of the private backup.

Creates analysis artifacts only. The synthetic ELF is NOT a flashable image;
its sections initially describe linear-sweep ranges, not recovered symbols.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

from compare_stock_images import ROOT, decode_application

BASE = 0x02000120


def make_elf(data, entry, base=BASE):
    """ELF32 little-endian pi32v2 (e_machine 0xf1, verified in AC79 SDK ELF)."""
    strings = b'\0.sweep\0.shstrtab\0'
    offset = 0x100
    names_offset = offset + len(data)
    shoff = (names_offset + len(strings) + 3) & ~3
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    header = struct.pack('<16sHHIIIIIHHHHHH', ident, 2, 0xf1, 1, entry, 52,
                         shoff, 0, 52, 32, 1, 40, 3, 2)
    program = struct.pack('<IIIIIIII', 1, offset, base, base, len(data), len(data), 5, 1)
    body = header + program
    body += bytes(offset - len(body)) + data + strings
    body += bytes(shoff - len(body)) + bytes(40)
    body += struct.pack('<IIIIIIIIII', 1, 1, 6, base, offset, len(data), 0, 0, 2, 0)
    body += struct.pack('<IIIIIIIIII', 8, 3, 0, 0, names_offset, len(strings), 0, 0, 1, 0)
    return body


def find_string_tables(app):
    """Find aligned runs of >=4 pointers to printable, NUL-terminated strings."""
    tables, run = [], []
    for off in range(0, len(app) - 3, 4):
        address = struct.unpack_from('<I', app, off)[0]
        pos = address - BASE
        end = app.find(b'\0', pos, pos + 81) if 0 <= pos < len(app) else -1
        value = app[pos:end] if end >= 0 else b''
        if 2 <= len(value) <= 80 and all(32 <= x < 127 for x in value) and re.search(rb'[A-Za-z]', value):
            run.append({'slot_address':hex(BASE + off), 'string_address':hex(address),
                        'text':value.decode('ascii')})
        else:
            if len(run) >= 4:
                tables.append({'address':run[0]['slot_address'], 'entries':run})
            run = []
    if len(run) >= 4:
        tables.append({'address':run[0]['slot_address'], 'entries':run})
    return tables


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backup', type=Path, default=ROOT / 'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin')
    parser.add_argument('--out', type=Path, default=ROOT / 'private-backups/readback-20260926-024637/analysis')
    parser.add_argument('--objdump', type=Path, default=Path('C:/JL/pi32/bin/llvm-objdump.exe'))
    args = parser.parse_args()
    raw = args.backup.read_bytes()
    app, info, layout = decode_application(raw)
    if not info['data_crc_valid']:
        raise ValueError('Application CRC mismatch; refusing disassembly')
    args.out.mkdir(parents=True, exist_ok=True)
    app_path = args.out / 'app-FM-1_010.bin'
    app_path.write_bytes(app)
    elf_path = args.out / 'app-analysis-only.elf'
    elf = make_elf(app, int(layout['entry_point'], 16))
    elf_path.write_bytes(elf)
    assert elf[0x100:0x100 + len(app)] == app
    strings = [{'offset': hex(m.start()), 'xip_address': hex(BASE + m.start()),
                'text': m.group().decode('ascii')}
               for m in re.finditer(rb'[\x20-\x7e]{6,}', app)]
    (args.out / 'strings.json').write_text(json.dumps(strings, indent=2), encoding='utf-8')
    command = [str(args.objdump), '-disassemble-all', '-arch-name=pi32v2',
               '-mcpu=r3', '-print-imm-hex', str(elf_path)]
    result = subprocess.run(command, capture_output=True, timeout=120, check=True)
    (args.out / 'app-linear-sweep.asm').write_bytes(result.stdout)
    (args.out / 'objdump-stderr.txt').write_bytes(result.stderr)
    listing = result.stdout.decode('utf-8', errors='replace')
    # Firmware-specific startup instructions, checked before using their literals.
    for offset, opcode in [(0x16, b'\xc3\xff'), (0x1e, b'\xc2\xff'),
                           (0x2c, b'\xc4\xff'), (0x32, b'\xc1\xff'), (0x38, b'\xc2\xff')]:
        if app[offset:offset + 2] != opcode:
            raise ValueError('Startup differs; memory-map interpretation needs review')
    literal = lambda off: struct.unpack_from('<I', app, off + 2)[0]
    ram_base, ram_source, ram_size = literal(0x2c), literal(0x32), literal(0x38)
    bss_base, bss_size = literal(0x16), literal(0x1e)
    ram_offset = ram_source - BASE
    if not 0 <= ram_offset < ram_offset + ram_size <= len(app):
        raise ValueError('Startup RAM copy outside app payload')
    ram = app[ram_offset:ram_offset + ram_size]
    ram_path = args.out / 'ram-copy-analysis-only.elf'
    ram_path.write_bytes(make_elf(ram, ram_base, ram_base))
    ram_command = command[:-1] + [str(ram_path)]
    ram_result = subprocess.run(ram_command, capture_output=True, timeout=120, check=True)
    (args.out / 'ram-copy-linear-sweep.asm').write_bytes(ram_result.stdout)
    memory = {'app_xip_base':hex(BASE), 'xip_code_and_const_bytes':ram_offset,
              'ram_copy_source':hex(ram_source), 'ram_copy_file_offset':hex(ram_offset),
              'ram_copy_destination':hex(ram_base), 'ram_copy_bytes':ram_size,
              'bss_base':hex(bss_base), 'bss_bytes':bss_size,
              'bss_end_exclusive':hex(bss_base + bss_size),
              'trailing_app_bytes':len(app) - ram_offset - ram_size}
    interesting = [s for s in strings if re.search(
        r'FM-1_|midi_route|usr_app_task|usr_audio_task|audio_server|usb_update_mode|'
        r'LOADER.BIN|Algorithm|Dx7|Sequencer|Arpeggio|mnt/sdfile/app|norflash', s['text'])]
    references = []
    for s in interesting:
        address = int(s['xip_address'], 16)
        hits = []
        for line in listing.splitlines():
            match = re.match(r'\s*([0-9a-f]+):.*\tr\d+ = 0x([0-9a-fA-F]+)\s*$', line)
            if match and int(match.group(2), 16) == address:
                hits.append(hex(int(match.group(1), 16)))
        packed = struct.pack('<I', address)
        pointer_hits = [hex(BASE + i) for i in range(0, len(app) - 3, 4)
                        if app[i:i + 4] == packed]
        runtime_hits = [hex(ram_base + int(h, 16) - ram_source)
                        if ram_source <= int(h, 16) < ram_source + ram_size else h for h in hits]
        references.append({**s, 'immediate_load_xrefs':hits, 'runtime_immediate_load_xrefs':runtime_hits,
                           'aligned_pointer_candidates':pointer_hits})
    (args.out / 'selected-string-xrefs.json').write_text(json.dumps(references, indent=2), encoding='utf-8')
    by_address = {int(s['xip_address'], 16):s['text'] for s in interesting}
    annotated = []
    for line in listing.splitlines():
        match = re.search(r'\tr\d+ = 0x([0-9a-fA-F]+)\s*$', line)
        if match and int(match.group(1), 16) in by_address:
            line += ' ; string: ' + by_address[int(match.group(1), 16)]
        annotated.append(line)
    (args.out / 'app-annotated.asm').write_text('\n'.join(annotated) + '\n', encoding='utf-8')
    tables = find_string_tables(app)
    (args.out / 'string-pointer-tables.json').write_text(json.dumps(tables, indent=2), encoding='utf-8')
    report = {'backup_sha256': hashlib.sha256(raw).hexdigest(), 'application': info,
              'layout': layout, 'startup_memory_map':memory, 'objdump_command': command,
              'objdump_sha256': hashlib.sha256(args.objdump.read_bytes()).hexdigest(),
              'synthetic_elf_sha256': hashlib.sha256(elf).hexdigest(),
              'synthetic_elf_payload_identical': True, 'string_count': len(strings),
              'selected_strings_with_immediate_xrefs': sum(bool(s['immediate_load_xrefs']) for s in references),
              'string_pointer_table_candidates':len(tables),
              'warning': 'Linear sweep includes data interpreted as instructions; no complete function recovery claimed.'}
    (args.out / 'audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    print('Selected application strings:')
    for item in strings:
        if re.search(r'FM-1_|dexed|msfa|midi|sysex|note.on|note.off|algorithm|oscillator|synth|task|malloc|assert|\.cpp|\.c\b', item['text'], re.I):
            print(item['offset'], item['text'][:160])


if __name__ == '__main__':
    main()
