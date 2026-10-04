"""Offline FM-1_010 evidence checks; no device, keys, or flash-writing functions.

These check reviewed tables/opcodes and their C transcription, not hardware.
The instruction semantics are documented in STOCK_BOARD_PROFILE.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
APP = ROOT / 'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin'
SHA256 = 'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543'
BASE = 0x02000120


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(app):
    require(hashlib.sha256(app).hexdigest() == SHA256, 'Unreviewed application hash')

    def at(address, length):
        return app[address-BASE:address-BASE+length]

    pins = list(at(0x0204f37b, 7))
    require(pins == list(range(32, 39)), 'Unexpected ALINK port-C pin table')
    channels = [struct.unpack('<4I', at(0x0204fb8c+16*i, 16)) for i in range(4)]
    require(channels == [(0, 0, 0, 0)]*3+[(0, 1, 0, 0x01c1c9f4)], 'Unexpected channel records')
    require(struct.unpack('<4I', at(0x02055b20, 16)) ==
            (0x12e10, 0x12e14, 0x12e18, 0x12e1c), 'Unexpected DMA register table')
    # Reviewed instruction bytes, not patterns searched through arbitrary data.
    evidence = {
        0x02022eae: 'c8 ff 20 10 05 00',  # SPI mux base r8 = 0x51020
        0x02023086: '00 ef 10 80',        # [r8] |= bit 4
        0x02004758: 'ce ff 52 eb 04 02',  # audio rodata base r14
        0x02005208: 'c6 ff 7c c9 c1 01',
        0x02005202: '10 f1 3a e0',        # r0 = r14 + 0x103a (channel table)
        0x0200520e: '00 f1 29 e8 e0 60',  # paired: store OLD r0, form pin table
        0x0200521c: 'c0 ff 30 10 05 00 c0 ef c0 00',  # port C mux
        0x02005248: '30 28',             # MCLK output, CON0 bit 8
        0x0200525e: '30 27',             # master, CON0 bit 7
        0x02005272: 'b8 2a',             # falling-edge update, bit 10 clear
        0x02005278: 'b8 29',             # 64 clocks/frame, bit 9 clear
        0x020046c4: '41 22',             # bit ch*4+2 set: SDK 24-bit mode
        0x0202476e: 'c5 ff 34 63 04 02',  # button matrix table
        0x0202337e: '00 e1 f2 70',        # r0 = slot + 0xf2, then low byte = slot-14
        0x020233b2: 'e1 e1 0c 10',       # octave * 12
        0x020233bc: 'c9 35',             # add base MIDI note 53 (F)
        0x020051ce: 'c1 ff 56 ac 00 00', # startup countdown 44118
        0x020052b8: 'c0 ff 19 d2 c1 01 59 3f 89 40', # target digital gain = 127
        0x020001f2: 'c1 ff 50 fd c7 01', # stock's internal boot-info destination
        0x020001ee: '50 ec 04 20',       # boot argument +4 -> two words
        0x02000206: '0a 66',             # boot argument +12 -> u16
        0x0200020a: '0a 67',             # boot argument +14 -> u16
        0x0200020e: '50 ec 00 21',       # boot argument +16 -> two words
        0x02000216: '00 04',             # return; no +80/+88 extension reads here
    }
    for address, expected in evidence.items():
        value = bytes.fromhex(expected)
        require(at(address, len(value)) == value, f'Instruction mismatch at {address:#x}')
    # Relocated RAM code lives at app offset 0x847c0, destination 0x01c00000.
    ram_evidence = {
        0x01c04df2: 'c5 ff f4 c9 c1 01',  # audio DMA base
        0x01c04e18: '48 40 00 a9 50 18',  # selected half * 512 + base
        0x01c04e2e: '52 20',             # process 64 stereo frames
        0x01c04a7c: '35 60 55 a8 de a8 b6 60', # signed low-24 in 32-bit words
        0x01c045f0: '60 f1 01 00',       # column 0 PA0
        0x01c045fa: 'c4 a4 64 f1 1e 40', # columns 1..4 PA5..8
        0x01c04616: '65 e1 20 50',       # column 5 PB7 after >>2
        0x01c04b2c: 'c0 ff 14 d2 c1 01', # startup countdown pointer
        0x01c04b3e: 'f2 20 82 60 40 20', # subtract 64; hold gain zero
        0x01c04b46: 'c0 ff 19 d2 c1 01', # requested digital gain
        0x01c04b54: 'c0 21',            # increment current gain by one
        0x01c04b5e: '98 40',            # publish current gain
        0x01c04bfa: 'f8 e1 70 10 d0 e1 07 08', # signed product >>7
    }
    for address, expected in ram_evidence.items():
        value = bytes.fromhex(expected)
        offset = 0x847c0 + address - 0x01c00000
        require(app[offset:offset+len(value)] == value, f'RAM instruction mismatch at {address:#x}')
    # The stock nonlinear output table maps zero input to zero output.
    require(struct.unpack_from('<i', app, 0x847c0+0x7fbc+256*4)[0] == 0,
            'Stock output-table zero point changed')
    return {'status': 'PASS_static_evidence_only', 'app_sha256': SHA256,
            'spi1_mux_register': '0x51020', 'audio_channel': 3, 'data_gpio': 'PC6',
            'mclk_gpio': 'PC0', 'lrclk_gpio': 'PC1', 'bclk_gpio': 'PC2',
            'mclk_output': 1, 'update_edge': 0, 'sclk_per_frame': 64,
            'stock_dma_width_bits': 24, 'adapter_dma_width_bits': 24,
            'dma_half_bytes': 512, 'stereo_frames_per_half': 64,
            'startup_hold_counter_frames': 44118, 'startup_silent_blocks': 690,
            'startup_silent_frames': 44160, 'startup_ramp_blocks': 127,
            'startup_final_gain_q7': 127,
            'stock_boot_argument_direct_bytes_read': 24,
            'sdk_boot_argument_extensions_verified': False,
            'button_count': 41, 'piano_slots': [14, 40], 'first_piano_note': 'F',
            'panel_reset_gpio': None, 'amplifier_mute_gpio': None,
            'hardware_verified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=APP)
    args = parser.parse_args()
    result = inspect(args.app.read_bytes())
    source = (HERE/'src/fm1_wl82.c').read_text(encoding='utf-8')
    require('#define IOMAP_CON1 REG32(0x51020)' in source, 'Adapter SPI mux mismatch')
    for name, value in [('output_channel', 3), ('mclk_output', 1),
                        ('update_edge', 0), ('sclk_32_per_frame', 0)]:
        require(re.search(rf'a->{name}\s*=\s*{value}\s*;', source), f'Adapter {name} mismatch')
    require('pd.data_width=pd.channel_out;' in source and 'pd.sr_points=128;' in source,
            'Adapter DMA configuration mismatch')
    require('fm1_audio_startup_reset(&startup_audio);' in source and
            'fm1_audio_startup_process24(&startup_audio,stereo);' in source and
            'len!=512' in source, 'Missing exact-half startup envelope')
    keys = (HERE/'src/fm1_stock_keys.c').read_text(encoding='utf-8')
    declaration = re.search(r'stock_button_cells\[41\]\s*=\s*\{(.*?)\};', keys, re.S)
    require(declaration is not None, 'Missing button cell table')
    table = bytes(int(t.strip(), 0) for t in declaration[1].split(','))
    app = args.app.read_bytes()
    require(table == app[0x02046334-BASE:0x02046334-BASE+41], 'Button table transcription mismatch')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
