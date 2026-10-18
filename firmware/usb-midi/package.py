# SPDX-License-Identifier: Apache-2.0
"""Offline, unit-private SysEx container preparation. Never opens a device.

The input images must already contain correctly encoded application bytes.
This does not convert an app.bin, migrate a layout, authorize an installation,
or establish that the baseline still matches a connected device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

FLASH_BYTES = 0x100000
APP_START = 0x4120
APP_END = APP_START + 584956
FLASH_PREFIX = (APP_END + 4095) & ~4095
LOADER_SHA256 = 'ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541'
TARGET_IDENTITY = 'FM1_FORGE_001'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def crc16(data):
    value = 0
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x1021 if value & 0x8000 else 0)) & 65535
    return value


def header_codec(data):
    """Public container header encoding, not a device key or authentication."""
    output = bytearray()
    state = 65535
    for byte in data:
        output.append(byte ^ (state & 255))
        state = ((state << 1) ^ (0x1021 if state & 0x8000 else 0)) & 65535
    return bytes(output)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def make_package(baseline, candidate, loader, *, baseline_sha256,
                 candidate_sha256, target_build_sha256, target_identity=TARGET_IDENTITY):
    require(isinstance(baseline, bytes) and isinstance(candidate, bytes), 'Immutable image bytes required')
    require(len(baseline) == len(candidate) == FLASH_BYTES, 'Expected two complete 1 MiB images')
    require(sha256(baseline) == baseline_sha256, 'Baseline hash mismatch')
    require(sha256(candidate) == candidate_sha256, 'Candidate hash mismatch')
    require(isinstance(target_build_sha256, str) and
            re.fullmatch('[0-9a-f]{64}', target_build_sha256) is not None and
            target_build_sha256 != '0' * 64, 'Invalid target build digest')
    require(target_identity == TARGET_IDENTITY, 'Unsupported common firmware identity')
    require(isinstance(loader, bytes) and 0 < len(loader) < 32768 and
            sha256(loader) == LOADER_SHA256, 'Only the pinned official OTA resource is accepted')
    require(baseline != candidate and baseline[0x4000:0x4040] != candidate[0x4000:0x4040],
            'An audited application and directory change is required')
    for begin, end in ((0, 0x4000), (0x4040, APP_START), (APP_END, FLASH_BYTES)):
        require(baseline[begin:end] == candidate[begin:end],
                'Candidate changes boot/config/reserved data outside the application scope')

    # Include the complete preserved application slot and its final erase sector.
    # The existing unit layout, boot/config and partial-sector bytes stay exact.
    # The OTA resource's actual write/preservation behavior needs separate audit.
    flash = candidate[:FLASH_PREFIX]
    loader_at = 1024 + len(flash)
    total = loader_at + ((len(loader) + 31) & ~31)
    image = bytearray(b'\xff' * total)
    directory = bytearray()
    for number, (kind, name, offset, data) in enumerate((
        (0, b'flash.bin', 1024, flash), (100, b'ota.bin', loader_at, loader)
    )):
        entry = bytearray(80)
        struct.pack_into('<HHH', entry, 0, kind, number, crc16(data))
        struct.pack_into('<III', entry, 8, offset, len(data), (len(data) + 31) & ~31)
        entry[64:64 + len(name)] = name
        directory.extend(header_codec(entry))
        image[offset:offset + len(data)] = data
    header = bytearray(64)
    struct.pack_into('<HIHHI', header, 2, crc16(directory), total, 2, 4, 512)
    header[16:23] = b'AC791N\0'
    struct.pack_into('<H', header, 0, crc16(header[2:]))
    image[:64] = header_codec(header)
    image[64:224] = directory
    wrapped = bytearray()
    identity = target_identity.encode('ascii')
    for index in range(20):
        wrapped.extend(image[index * 47:(index + 1) * 47])
        wrapped.append(identity[index] + index + 1 if index < len(identity) else 125)
    wrapped.extend(image[940:])
    manifest = {
        'schemaVersion': 1, 'model': 'FM-1', 'family': 8, 'layout': 'stock-single-v1',
        'packageBytes': len(wrapped), 'packageSha256': sha256(wrapped),
        'imageBytes': len(image), 'imageSha256': sha256(image),
        'targetBuildSha256': target_build_sha256, 'targetIdentity': target_identity,
        'loaderSha256': LOADER_SHA256, 'baselineSha256': baseline_sha256,
        'preservation': 'unit-baseline-app-only', 'hardwareQualified': False,
    }
    return bytes(wrapped), manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'candidate', 'loader', 'out'):
        parser.add_argument('--' + name, required=True, type=Path)
    for name in ('baseline-sha256', 'candidate-sha256', 'target-build-sha256'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    package, manifest = make_package(
        args.baseline.read_bytes(), args.candidate.read_bytes(), args.loader.read_bytes(),
        baseline_sha256=args.baseline_sha256, candidate_sha256=args.candidate_sha256,
        target_build_sha256=args.target_build_sha256)
    # A fresh, local output folder prevents accidentally replacing a reviewed
    # package. Unit-private outputs must never enter a public source release.
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / 'firmware.fwsc').write_bytes(package)
    (args.out / 'firmware.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (args.out / 'PRIVATE.txt').write_text(
        'UNIT-PRIVATE CANDIDATE. Contains baseline boot/config/application bytes.\n'
        'Do not publish. Baseline freshness and loader compatibility are not qualified.\n', encoding='utf-8')
    print(json.dumps({'packageBytes': len(package), 'sha256': manifest['packageSha256'],
                      'deviceIO': False, 'hardwareQualified': False, 'privacy': 'unit-private'}))


if __name__ == '__main__':
    main()
