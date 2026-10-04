"""Generate a private SDK archive derivative with stock's CPU1 divider guard.

Only EnableOtherCpu's IR is changed. Original SDK remains untouched. This is
an experimental timing-parity change, NOT a single-core scheduler conversion.
"""
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess

SYSTEM_SHA = 'a71bdc75e5afbcffaf7d79bc2b27f2b65fec63d94d2261b584744d3e9b9098ea'
SAVE = ('  %fm1_saved_div = load volatile i32, i32* inttoptr (i32 65544 to i32*), align 4\n'
        '  %fm1_guard_div = or i32 %fm1_saved_div, 8\n'
        '  store volatile i32 %fm1_guard_div, i32* inttoptr (i32 65544 to i32*), align 4\n')
RESTORE = '  store volatile i32 %fm1_saved_div, i32* inttoptr (i32 65544 to i32*), align 4\n'
GUARD_ADDRESS = 0x02027000
# Reviewed linked CPU0 gate, vector, save/OR, launch, wait, restore, lockset.
GUARD_CODE = bytes.fromhex(
    '80 5d c0 ff f8 ff c7 01 c1 ff 24 02 00 02 81 60 '
    'c0 ff 08 00 01 00 01 60 42 e1 08 10 82 60 '
    'c2 ff 2d aa c1 01 43 20 ab 40 '
    'c3 ff 04 e0 ee 01 00 ef 08 30 c0 ef 02 30 '
    '2b 40 73 5e 81 60 41 00')


def audit_application(app, address=GUARD_ADDRESS, cpu1_start=0x02000224, ready=0x01c1aa2d):
    offset = address - 0x02000120
    expected = bytearray(GUARD_CODE)
    struct.pack_into('<I', expected, 10, cpu1_start)
    struct.pack_into('<I', expected, 32, ready)
    if offset < 0 or app[offset:offset+len(expected)] != expected:
        raise ValueError('Linked CPU1 guard differs from reviewed machine code')
    return {'guard_address': hex(address), 'bytes_verified': len(GUARD_CODE),
            'sys_div': '0x10008', 'temporary_or_mask': 8,
            'restored_after_cpu1_ready': True, 'hardware_verified': False}


def archive_members(blob):
    if not blob.startswith(b'!<arch>\n'):
        raise ValueError('Expected regular archive')
    entries, names, offset = [], b'', 8
    while offset < len(blob):
        header = blob[offset:offset+60]
        if len(header) != 60 or header[58:] != b'`\n':
            raise ValueError('Bad archive header')
        size = int(header[48:58])
        name = header[:16].decode().strip()
        payload = blob[offset+60:offset+60+size]
        if len(payload) != size:
            raise ValueError('Truncated archive')
        if name == '//':
            names = payload
        elif name not in ('/', '/SYM64/'):
            if name.startswith('/') and name[1:].isdigit():
                start = int(name[1:]); end = names.find(b'/\n', start)
                if end < 0:
                    raise ValueError('Invalid archive long name')
                name = names[start:end].decode()
            else:
                name = name.rstrip('/')
            entries.append((name, payload))
        offset += 60 + size + (size & 1)
    return entries


def transform(text):
    matches = list(re.finditer(r'^define void @EnableOtherCpu\(\)[^\n]*\{\n.*?^}', text, re.M | re.S))
    if len(matches) != 1:
        raise ValueError('Expected exactly one vendor EnableOtherCpu')
    match = matches[0]
    body = match[0]
    if ('65544' in body or 'fm1_' in body or body.count('  ret void,') != 1 or
        body.count('  store volatile i8 0, i8* @cpu1_run_flag,') != 1 or
        body.count('  store i32 ptrtoint (void (...)* @cpu1_start to i32),') != 1 or
        body.count('while.end:') != 1):
        raise ValueError('Vendor CPU1 launch contract changed')
    body = body.replace('  store volatile i8 0, i8* @cpu1_run_flag,',
                        SAVE + '  store volatile i8 0, i8* @cpu1_run_flag,')
    body = body.replace('  ret void,', RESTORE + '  ret void,')
    changed = text[:match.start()] + body + text[match.end():]
    if changed.replace(SAVE, '').replace(RESTORE, '') != text:
        raise ValueError('Unexpected non-guard change')
    return changed


def build_archive(source, out, tc):
    source, out, tc = Path(source), Path(out), Path(tc)
    if hashlib.sha256(source.read_bytes()).hexdigest() != SYSTEM_SHA:
        raise ValueError('Unreviewed vendor system.a')
    out.mkdir(parents=True, exist_ok=True)
    def run(*args):
        subprocess.run([str(a) for a in args], cwd=out, check=True)
    run(tc/'llvm-ar.exe', 'x', source, 'port.c.o')
    run(tc/'clang.exe', '-target', 'pi32v2', '-S', '-emit-llvm', '-x', 'ir',
        out/'port.c.o', '-o', out/'port.original.ll')
    original = (out/'port.original.ll').read_text()
    (out/'port.guarded.ll').write_text(transform(original))
    run(tc/'clang.exe', '-target', 'pi32v2', '-c', '-emit-llvm', '-x', 'ir',
        out/'port.guarded.ll', '-o', out/'port.c.o')
    target = out/'system-cpu1-guard.a'
    shutil.copy2(source, target)
    run(tc/'llvm-ar.exe', 'r', target, out/'port.c.o')
    run(tc/'llvm-ar.exe', 's', target)
    before, after = archive_members(source.read_bytes()), archive_members(target.read_bytes())
    if [name for name, _ in before] != [name for name, _ in after]:
        raise ValueError('Archive member order/names changed')
    changed = [a[0] for a, b in zip(before, after) if a != b]
    if changed != ['port.c.o']:
        raise ValueError('Archive changes exceed port.c.o')
    return target
