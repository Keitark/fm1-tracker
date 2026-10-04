"""Verify pinned FM-1_010 evidence for the implemented power parameters.

Reads a local decoded stock image only. No USB, keys, writes or extraction.
Instruction addresses are XIP addresses, not raw NOR offsets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
STOCK = ROOT/'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin'
SHA256 = 'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543'
XIP = 0x02000120
# Each tuple: XIP instruction start, bytes, interpretation. These are not a
# source-level stock struct: parameters were constant-folded into instructions.
EVIDENCE = (
    (0x020047e0, 'c6 ff 28 95 c0 01 41 20 42 e0 f0 01 44 20 60 16 80 ff 0c e9 03 00 40 22',
     'clear 496-byte stock power state; config flags remain zero'),
    (0x02004830, 'dc 45', 'btosc_disable is zero in cleared/initialized state'),
    (0x02004996, '40 31 41 23 42 23 43 27 80 ff e8 c6 bf ff', 'VLVD selector 7'),
    (0x020049b4, '40 31 41 21 80 ff f6 c6 bf ff', 'VLVD enabled'),
    (0x020049c8, '50 ed 90 00 30 25 50 ed 91 00', 'DCDC enabled in lp_dat'),
    (0x020049d2, '40 20 41 22 42 21 43 21 80 ff ac c6 bf ff', 'DCDC transition 1'),
    (0x020049ec, '40 20 41 23 42 21 43 21 80 ff 92 c6 bf ff', 'DCDC transition 2'),
    (0x02004a06, '40 20 41 22 42 21 43 20 80 ff 78 c6 bf ff', 'DCDC transition 3'),
    (0x02004a66, '40 25 41 20 42 23 43 24 80 ff 18 c6 bf ff', 'VDDIOM selector 4'),
    (0x02004a74, '40 26 41 20 42 23 43 27 80 ff 0a c6 bf ff', 'VDC14 selector 7'),
    (0x02004a82, '40 25 41 24 42 22 43 23 80 ff fc c5 bf ff', 'VDDIOW selector 3'),
    (0x02004ad4, '40 29 41 20 42 24 43 2f 80 ff aa c5 bf ff', 'SYSVDD selector 15'),
)


def verify(blob):
    digest = hashlib.sha256(blob).hexdigest()
    if digest != SHA256:
        raise ValueError('Unreviewed stock 010 application hash')
    for address, expected, label in EVIDENCE:
        expected = bytes.fromhex(expected)
        actual = blob[address-XIP:address-XIP+len(expected)]
        if actual != expected:
            raise ValueError('Stock power instruction mismatch: ' + label)
        if len(expected) == 14:  # Four parameter registers + P33_CON_SET call.
            target = address+14+struct.unpack_from('<i', actual, 10)[0]
            if target != 0x01c0108c:
                raise ValueError('Stock P33 setter target mismatch: ' + label)
    return {'stock_application_sha256': digest, 'instruction_points_verified': len(EVIDENCE),
            'effective_parameters': {'config': 0, 'btosc_disable': 0,
                'vddiom_lev': 4, 'vddiow_lev': 3, 'vdc14_lev': 7,
                'vdc14_dcdc': 1, 'sysvdd_lev': 15, 'vlvd_value': 7, 'vlvd_enable': 1},
            'sdk_ignored_inputs_zeroed_not_recovered': ['osc_type', 'osc_hz', 'delay_us', 'pd_wdvdd_lev'],
            'hardware_voltage_measurement': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock', type=Path, default=STOCK)
    args = parser.parse_args()
    print(json.dumps(verify(args.stock.read_bytes()), indent=2))
