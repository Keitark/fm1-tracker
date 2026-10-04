"""Ten full reads using the existing Jieli reader/transport; no array writes.

Uses an already running original RAM helper. Online initialization has the
status/protection-register caveat documented by read_fm1_firmware.py.
Binary outputs contain private device data and must not be published.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

import read_fm1_firmware as reader


def capture(dev, protocol, directory, count=10):
    report = {'status': 'running', 'requested_passes': count, 'passes': [],
              'array_program_or_erase_commands_sent': False,
              'chip_key_queried': False, 'loader_uploaded': False,
              'comparison': 'every byte compared with pass 1; differences do not abort',
              'variable_offsets': []}
    baseline = None
    previous = None
    variable = set()

    def save():
        (directory/'comparison.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')

    save()
    try:
        for number in range(1, count+1):
            output = directory/f'pass-{number:02d}.bin'
            partial = output.with_suffix('.bin.partial')
            with partial.open('xb') as stream:
                for address in range(0, reader.FM1_FLASH_SIZE, reader.FLASH_READ_CHUNK):
                    block = reader.read_flash_exact(dev, protocol, address, reader.FLASH_READ_CHUNK)
                    if len(block) != reader.FLASH_READ_CHUNK:
                        raise IOError(f'Short read at 0x{address:x}')
                    stream.write(block)
                stream.flush()
                os.fsync(stream.fileno())
            partial.rename(output)
            data = output.read_bytes()
            if baseline is None:
                baseline = data
                previous = data
            differences = [i for i, (a,b) in enumerate(zip(baseline,data)) if a != b]
            adjacent = [i for i, (a,b) in enumerate(zip(previous,data)) if a != b]
            variable.update(differences)
            report['passes'].append({'pass': number, 'file': output.name, 'bytes': len(data),
                                     'sha256': hashlib.sha256(data).hexdigest(),
                                     'changed_vs_first': [hex(i) for i in differences],
                                     'changed_vs_previous': [hex(i) for i in adjacent]})
            report['variable_offsets'] = [hex(i) for i in sorted(variable)]
            save()
            print(f'Pass {number}/{count} complete: {len(data)} bytes; '
                  f'{len(differences)} differences vs first, {len(adjacent)} vs previous', flush=True)
            previous = data
        report['status'] = 'complete'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        save()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    if not args.directory.is_dir() or any(args.directory.glob('pass-*')) or (args.directory/'comparison.json').exists():
        raise ValueError('Use a fresh existing run directory')
    probe = Path(__file__).resolve().parents[2]/'tmp/fm1-read-probe-20260926-024212.bin'
    reference = probe.read_bytes()
    if hashlib.sha256(reference).hexdigest() != '3c0dee98aa08fb1a5b9ed59ca9a37bc9d149fff46ae7b282bb65d4b996ada0e5':
        raise ValueError('Reference probe changed')
    print(reader.LOADER_SIDE_EFFECT_WARNING, flush=True)
    with reader.SCSIDev(args.device) as dev:
        if reader.read_inquiry(dev) != ('WL82', 'UBOOT1.00', '1.00'):
            raise ValueError('Unexpected target')
        protocol = reader.JL_LoaderV2(dev)
        online = protocol.online_device()
        if online != {'type': reader.SPI_NOR_DEVICE_TYPE, 'id': reader.FM1_FLASH_ID}:
            raise ValueError('Unexpected flash identity')
        if reader.read_flash_exact(dev, protocol, 0, 256) != reference:
            raise ValueError('Reference header mismatch')
        capture(dev, protocol, args.directory)


if __name__ == '__main__':
    main()
