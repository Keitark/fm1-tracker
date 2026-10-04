"""Offline comparison: validate/extract UFW flash payloads without querying keys."""
import ast
import hashlib
import io
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'references/source/jl-misctools/firmware'))
from jltech.cipher import jl_enc_cipher, jl_sfc_cipher
from jltech.chipkeybin import chipkeybin_decode
from jltech.crc import jl_crc16
from jltech.utils import align_to, nulltermstr


def _decode_layout(payload):
    """Private in-memory layout access. Never serialize this return value.

    Load just the two audited directory classes, not the upstream CLI, whose
    normal execution prints keys and writes configuration files.
    """
    source = ROOT / 'references/source/jl-misctools/firmware/fwunpack_newfw.py'
    tree = ast.parse(source.read_text(encoding='utf-8'))
    classes = [n for n in tree.body if isinstance(n, ast.ClassDef)
               and n.name in ('JLFSEntry', 'JLFSIterator')]
    if len(classes) != 2:
        raise ValueError('Expected audited JLFS classes not found')
    namespace = dict(struct=struct, jl_crc16=jl_crc16, jl_enc_cipher=jl_enc_cipher,
                     jl_sfc_cipher=jl_sfc_cipher, align_to=align_to, nulltermstr=nulltermstr)
    exec(compile(ast.Module(body=classes, type_ignores=[]), str(source), 'exec'), namespace)
    iterator = namespace['JLFSIterator']
    fw = bytearray(payload)
    header = fw[:32]
    jl_enc_cipher(header, 0, 32, 0xffff)
    if jl_crc16(header[2:]) != int.from_bytes(header[:2], 'little'):
        raise ValueError('Flash header CRC mismatch')
    appbase = key = None
    top_entries = []
    def entry_info(ent):
        return {'name': ent.name, 'offset': hex(ent.data_offset), 'stored_offset': hex(ent.offset),
                'size': ent.data_size, 'flags': hex(ent.flags)}
    for ent in iterator(fw, 0, 32, key=0xffff):
        top_entries.append(entry_info(ent))
        if ent.name == 'app_dir_head' and ent.flags == 0x81:
            appbase = ent.data_offset
        elif ent.name == 'isd_config.ini':
            encoded, crc = struct.unpack_from('<32sH', fw, ent.data_offset)
            if jl_crc16(encoded) != crc:
                raise ValueError('Config metadata CRC mismatch')
            key = chipkeybin_decode(encoded)
    if appbase is None or key is None:
        raise ValueError('Required application metadata absent')
    area = next(iterator(fw, appbase, key=key, sfc=True))
    app_entries = list(iterator(fw, area.hdr_off, area.data_offset - area.hdr_off))
    return fw, area, app_entries, top_entries, appbase, key


def decode_application(payload):
    """Decode only the application in memory; never log keys or extract config."""
    fw, area, app_entries, top_entries, appbase, _ = _decode_layout(payload)
    def entry_info(ent):
        return {'name': ent.name, 'offset': hex(ent.data_offset), 'stored_offset': hex(ent.offset),
                'size': ent.data_size, 'flags': hex(ent.flags)}
    for ent in app_entries:
        if ent.name == 'app.bin' and ent.flags == 0x82:
            end = ent.data_offset + ent.data_size
            if end > len(fw):
                raise ValueError('Application extends beyond image')
            app = bytes(fw[ent.data_offset:end])
            info = {'bytes': len(app), 'flash_offset': hex(ent.data_offset),
                    'sha256': hashlib.sha256(app).hexdigest(),
                    'data_crc_valid': jl_crc16(app) == ent.data_crc,
                    'version_markers': sorted({m.decode('ascii') for m in re.findall(rb'FM-1_[0-9]{3}', app)})}
            return app, info, {'entry_point': hex(area.offset), 'app_area_base': hex(appbase),
                               'reserved_offset_caution': 'For reserved entries, offset is the generic parser base-added value, not a validated physical address. Preserve stored_offset separately.',
                               'top_entries': top_entries,
                               'app_entries': [entry_info(e) for e in app_entries]}
    raise ValueError('Application file absent')


def application_info(payload):
    return decode_application(payload)[1]


def extract_flash(path):
    data = path.read_bytes()
    for wrapped in (False, True):
        stream = io.BytesIO(data)
        if wrapped:
            header = bytearray()
            for _ in range(20):
                block = stream.read(48)
                header.extend(block[:47])
        else:
            header = bytearray(stream.read(64))
        skew = stream.tell() - len(header)
        jl_enc_cipher(header, 0, 64, key=0xffff)
        hcrc, listcrc, _, count, _, _, chip = struct.unpack_from('<HHIHHI48s', header)
        if jl_crc16(header[2:64]) != hcrc:
            continue
        header_size = 64 + count * 80
        if len(header) < header_size:
            header.extend(stream.read(header_size - len(header)))
        if jl_crc16(header[64:header_size]) != listcrc:
            raise ValueError('UFW entry list CRC mismatch')
        for off in range(64, header_size, 80):
            jl_enc_cipher(header, off, 80, key=0xffff)
            kind, _, crc, _, offset, size, _, _, name = struct.unpack_from('<HHHHIII44s16s', header, off)
            if kind == 0:
                payload = data[offset + skew:offset + skew + size]
                if len(payload) != size or jl_crc16(payload) != crc:
                    raise ValueError('Flash payload size/CRC mismatch')
                return payload, {'chip': chip.split(b'\0')[0].decode('ascii'), 'payload_crc_valid': True}
    raise ValueError('No valid flash payload found')


def compare(raw, payload):
    if len(payload) > len(raw):
        raise ValueError('Package payload exceeds device flash size')
    different = [i for i, (a,b) in enumerate(zip(raw, payload)) if a != b]
    regions = []
    for label, start, end in [('header', 0, 0xa0), ('boot_and_config', 0xa0, 0x4000), ('app_and_resources', 0x4000, len(payload))]:
        end = min(end, len(payload))
        if end <= start:
            continue
        changes = sum(a != b for a,b in zip(raw[start:end], payload[start:end]))
        regions.append({'region':label, 'start':hex(start), 'end_exclusive':hex(end), 'bytes':end-start, 'different_bytes':changes, 'exact_match':changes == 0})
    return {'compared_bytes':len(payload), 'different_bytes':len(different), 'exact_match':not different,
            'first_difference':hex(different[0]) if different else None,
            'last_difference':hex(different[-1]) if different else None, 'regions':regions}


def main():
    raw_path = ROOT / 'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin'
    raw = raw_path.read_bytes()
    candidates = [
        ('V13-community', ROOT / 'references/downloads/FM-1-v13-community-20260926.fwsc'),
        ('V14-official-archived', ROOT / 'references/originals/FM-1-v14-original.fwsc'),
        ('V15-official-live', ROOT / 'references/downloads/FM-1-live-20260926.fwsc'),
    ]
    installed = application_info(raw)
    report = {'backup_sha256':hashlib.sha256(raw).hexdigest(), 'backup_bytes':len(raw),
              'installed_application':installed, 'comparisons':[]}
    for label, path in candidates:
        payload, metadata = extract_flash(path)
        app = application_info(payload)
        report['comparisons'].append({'image':label, 'source_file':str(path),
            'package_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'flash_payload_sha256':hashlib.sha256(payload).hexdigest(),
            'application':app, 'application_matches_installed':app['sha256'] == installed['sha256'],
            **metadata, **compare(raw, payload)})
    output = ROOT / 'private-backups/readback-20260926-024637/stock-comparison.json'
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
