"""Offline stock V14 white-pixel polling diagnostic; no USB operations."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import build_stock_marker as marker
from build_stock_image import ROOT, V14_BACKUP, V14_SHA, _replace_application, verify_candidate, require

PATCH = 0x02021880
END = 0x020218ae
BEFORE = bytes.fromhex(
    '04 60 43 20 34 28 84 60 c0 ef 80 00 00 ef 80 1c 92 63 '
    '44 ea 28 10 14 60 54 e8 fd 79 00 ef 80 1c 00 ef 80 00 '
    '04 60 c3 21 21 ff 34 3e eb ff')
AFTER = bytes.fromhex(
    '00 ef 80 0f c0 ef 80 00 60 e0 e1 3b 00 ef 80 1c 42 ea ff 10 '
    '14 60 54 e8 fd 79 fb 3f f3 57 00 ef 80 1c 00 ef 80 00 '
    '04 60 00 00 00 00 00 00')
SOURCE = ROOT/'firmware/nes/boot/stock_polled_pixels.S'


def validate_application(white_app, candidate):
    require(len(BEFORE) == len(AFTER) == END-PATCH == 46, 'Patch length changed')
    offset = PATCH-marker.XIP
    require(white_app[offset:offset+46] == BEFORE, 'Original pixel DMA loop changed')
    require(candidate[offset:offset+46] == AFTER, 'Unreviewed polled machine code')
    require(len(candidate) == len(white_app) and candidate[:offset] == white_app[:offset]
            and candidate[offset+46:] == white_app[offset+46:], 'Out-of-scope application change')


def build():
    _, white_app, _ = marker.build()
    original = marker.pinned(V14_BACKUP, V14_SHA)
    offset = PATCH-marker.XIP
    candidate = white_app[:offset]+AFTER+white_app[offset+46:]
    validate_application(white_app, candidate)
    target = _replace_application(original, candidate)
    report = verify_candidate(original, target, candidate)
    require(report['changed_sector_offsets'] == ['0x4000', '0x25000'], 'Unexpected stock-relative scope')
    report.update(experiment='Stock V14 white frame with polled pixels, not NES',
                  patch_cpu_start=hex(PATCH), patch_cpu_end_exclusive=hex(END),
                  replaced_code_bytes=46, pixels=57600, pixel_bytes=115200,
                  parameters_still_use_stock_dma=True, pixel_spi_byte=255,
                  expected_followup='White first frame then ordinary synth UI',
                  polling_timeout='No software timeout, like the original stock loop',
                  interpretation='Tests pixel polling under stock startup, not complete NES LCD driver',
                  hardware_verified=False)
    return target, candidate, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    require(not args.out.exists(), 'Output already exists')
    target, app, report = build()
    args.out.mkdir(parents=True)
    tc = Path('C:/JL/pi32/bin')
    obj, binary = args.out/'pixels.o', args.out/'pixels.bin'
    subprocess.run([str(tc/'clang.exe'), '-target', 'pi32v2', '-integrated-as', '-mcpu=r3',
                    '-c', str(SOURCE), '-o', str(obj)], check=True)
    subprocess.run([str(tc/'llvm-objcopy.exe'), '-O', 'binary', '-j', '.text', str(obj), str(binary)], check=True)
    require(binary.read_bytes() == AFTER, 'Assembled loop does not match reviewed bytes')
    listing = subprocess.run([str(tc/'llvm-objdump.exe'), '-d', '-r', '-mcpu=r3', str(obj)],
                             capture_output=True, text=True, check=True).stdout
    (args.out/'pixels.asm').write_text(listing)
    (args.out/'fm1-v14-polled-white.UNQUALIFIED.bin').write_bytes(target)
    (args.out/'fm1-v14-polled-white.app.bin').write_bytes(app)
    report.update(application_sha256=hashlib.sha256(app).hexdigest(),
                  image_sha256=hashlib.sha256(target).hexdigest(),
                  assembly_source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest())
    (args.out/'manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
