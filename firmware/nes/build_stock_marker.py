"""Offline V14 white-first-frame experiment; no replacement startup or USB IO."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from build_stock_image import ROOT, V14_BACKUP, V14_SHA, _replace_application, verify_candidate, require
from compare_stock_images import decode_application

STOCK_APP_SHA = '54a32371e8fc19e7492210e958023762f7335e354a4cab41cb26c35d6f0f0443'
STOCK010 = ROOT/'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin'
STOCK010_SHA = 'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543'
XIP = 0x02000120
FILL = 0x020217b8
PATCH = 0x020217ce
BEFORE = bytes.fromhex('40 20')  # r0 = 0
AFTER = bytes.fromhex('10 3f')   # r0 = -1 (0xffffffff), same length


def pinned(path, digest):
    data = path.read_bytes()
    require(hashlib.sha256(data).hexdigest() == digest, 'Unreviewed input: '+str(path))
    return data


def validate_application(original, candidate):
    require(hashlib.sha256(original).hexdigest() == STOCK_APP_SHA, 'Unreviewed V14 application')
    require(len(candidate) == len(original), 'Application length changed')
    offset = PATCH-XIP
    require(original[offset:offset+2] == BEFORE, 'Unexpected original instruction')
    require(candidate[offset:offset+2] == AFTER, 'Unexpected marker instruction')
    require(candidate[:offset] == original[:offset] and candidate[offset+2:] == original[offset+2:],
            'Changes outside the single fill instruction')


def build():
    stock = pinned(V14_BACKUP, V14_SHA)
    app, info, _ = decode_application(stock)
    require(info['data_crc_valid'], 'Invalid V14 application CRC')
    old = pinned(STOCK010, STOCK010_SHA)
    reviewed = old[0x02021600-XIP:0x0202171a-XIP]
    require(len(reviewed) == 282 and app[FILL-XIP:FILL-XIP+282] == reviewed,
            'V14 fill is not identical to reviewed stock010 routine')
    require(app.count(reviewed) == 1, 'Ambiguous fill function')
    # V14 UI table loop ends immediately before this unchanged direct call.
    require(app[0x02023316-XIP:0x0202331a-XIP] == bytes.fromhex('bf ea 4f f2'),
            'V14 UI-to-fill call changed')
    candidate_app = bytearray(app)
    candidate_app[PATCH-XIP:PATCH-XIP+2] = AFTER
    candidate_app = bytes(candidate_app)
    validate_application(app, candidate_app)
    target = _replace_application(stock, candidate_app)
    report = verify_candidate(stock, target, candidate_app)
    require(report['changed_sector_offsets'] == ['0x4000', '0x25000'], 'Unexpected stock-relative sectors')
    report.update(experiment='stock V14 white first frame', patch_cpu_address=hex(PATCH),
                  patch_flash_offset=hex(PATCH-0x02000000+0x4000),
                  original_instruction=BEFORE.hex(), marker_instruction=AFTER.hex(),
                  changed_application_bytes=2, stock_application_sha256=STOCK_APP_SHA,
                  visible_marker_duration='Not extended; stock UI may overwrite it immediately',
                  integrity_and_control_flow_unchanged=True,
                  expected_followup='Normal V14 synth UI, not NES',
                  unverified=['Physical boot and visible marker', 'Normal stock runtime behavior'])
    return target, candidate_app, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    require(not args.out.exists(), 'Output already exists')
    target, app, report = build()
    args.out.mkdir(parents=True)
    (args.out/'fm1-v14-white-marker.UNQUALIFIED.bin').write_bytes(target)
    (args.out/'fm1-v14-white-marker.app.bin').write_bytes(app)
    (args.out/'manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
