"""Fail-closed audit of the reviewed SDK power-init machine code, offline only.

Relocations are checked against ELF symbols before normalizing. All remaining
instructions (including register writes, values, branches and waits) are pinned.
This is code review evidence, not a hardware/voltage qualification.
"""
import hashlib
import struct

PARAMETERS = bytes.fromhex('00 00 00 00 00 00 00 00 00 00 00 00 04 03 07 01 0f 00 07 01')
BODY_SHA256 = '9fd637a2334e7ad01ec3bc5c048833f21d1fffe12cc7eae5ee2c359c534b4d05'
# offset, encoded instruction length, destination symbol
CALLS = '''
012 6 memset
02c 4 __hw_set_osc_hz
042 4 clk_get
05c 6 p33_tx_1byte
066 6 p33_tx_1byte
094 6 p33_tx_1byte
0ae 6 p33_tx_1byte
0b8 6 p33_tx_1byte
0c2 6 p33_tx_1byte
0cc 6 p33_tx_1byte
0ea 4 __hw_lrc_start
0fa 4 request_irq
118 6 p33_tx_1byte
122 6 p33_tx_1byte
12c 6 p33_tx_1byte
136 6 p33_tx_1byte
13e 4 __tcnt_us
146 4 __tcnt_us
152 6 p33_tx_1byte
15e 6 p33_tx_1byte
16e 4 __tcnt_us
17a 4 __tcnt_us
18a 6 p33_rx_1byte
194 6 p33_or_1byte
19e 6 p33_tx_1byte
1a8 6 p33_tx_1byte
1b2 6 p33_tx_1byte
1bc 6 p33_and_1byte
1c6 6 p33_or_1byte
1d4 6 P33_CON_SET
1ea 6 p33_or_1byte
1f4 6 p33_or_1byte
208 6 P33_CON_SET
222 6 P33_CON_SET
23c 6 P33_CON_SET
252 6 p33_tx_1byte
25c 6 p33_tx_1byte
266 6 p33_tx_1byte
270 6 p33_tx_1byte
27a 6 p33_tx_1byte
282 6 p33_rx_1byte
28e 6 p33_tx_1byte
29c 6 P33_CON_SET
2aa 6 P33_CON_SET
2b8 6 P33_CON_SET
2c2 6 p33_or_1byte
2d0 6 P33_CON_SET
2da 6 p33_or_1byte
2e4 6 p33_and_1byte
2f2 6 P33_CON_SET
300 6 p33_tx_1byte
30a 6 p33_tx_1byte
31c 4 request_irq
32a 6 p33_tx_1byte
334 6 p33_tx_1byte
346 4 request_irq
354 6 malloc
36a 6 __local_irq_disable
3b6 6 __local_irq_enable
3dc 4 sys_timer_add_to_task
40a 4 clk_get
42e 6 os_current_task_rom
434 4 cpu_assert_debug
'''
POINTERS = {
    0x002: 'low_power_hdl', 0x03c: b'sys\0', 0x076: 'ota_status',
    0x0ee: 'lrc_irq_handler', 0x0fe: '__lrc_trace_oschz', 0x1da: 'lp_dat',
    0x310: 'lp_timer0_irq_handler', 0x33a: 'lp_timer1_irq_handler',
    0x35e: 'sys_power_ops', 0x370: 'bt_lock_cnt', 0x384: 'bt_lock',
    0x3cc: b'sys_timer\0', 0x3d2: 'check_rf_power_idle_timer',
    0x3e0: 'sys_low_power_enable', 0x404: b'sdram\0',
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def call_target(code, address):
    if len(code) == 2:
        require(code[0]&0x8f==1 and code[1]&0xe0==0x80,'Power compact call opcode changed')
        relative=((code[0]>>4)&7)*32+(code[1]&31)
        if relative&128:relative-=256
        return address+2+relative*2
    if len(code) == 6:
        require(code[:2] == b'\x80\xff', 'Power long call opcode changed')
        return address + 6 + struct.unpack_from('<i', code, 2)[0]
    require(len(code) == 4 and code[0] & 0xc0 == 0x80 and code[1] == 0xea,
            'Power short call opcode changed')
    relative = ((code[0] & 0x3f) << 16) | int.from_bytes(code[2:], 'little')
    if relative & (1 << 21):
        relative -= 1 << 22
    return address + 4 + relative * 2


def audit_power(symbols, sections, code_at, required=False, usb_only=False, usb_audio=False,factory_diag=False,usb_midi=False):
    def value(name):
        require(name in symbols, 'Missing board power symbol ' + name)
        return symbols[name][0]

    enabled = 'fm1_board_power_init' in symbols
    require(enabled or not required, 'Missing stock board power integration')
    if not enabled:
        return {'enabled': False, 'hardware_verified': False}
    for name in ('exception_auto_fix_config', 'get_power_config_from_cfg_tools_enalbe'):
        require(name not in symbols, 'Unexpected power override/auto-fix hook: ' + name)
    param = value('fm1_stock_power_param')
    require(symbols['fm1_stock_power_param'][1] == 20 and code_at(param, 20) == PARAMETERS,
            'Stock power parameter block changed')
    phase = value('fm1_power_stage')
    bss = sections['.ram0_bss']
    require(symbols['fm1_power_stage'][1] == 4 and bss[3] <= phase < bss[3]+bss[5]-3,
            'Power stage outside initialized BSS')
    start = value('power_init')
    require(symbols['power_init'][1] == 0x43a, 'Power initializer size changed')
    normalized = bytearray(code_at(start, 0x43a))
    for line in CALLS.splitlines():
        if not line.strip():
            continue
        off, length, target = line.split()
        off, length = int(off, 16), int(length)
        require(call_target(code_at(start+off, length), start+off) == value(target),
                'Power call target changed at ' + hex(off))
        normalized[off:off+length] = bytes(length)
    for off, target in POINTERS.items():
        insn = code_at(start+off, 6)
        require(insn[0] & 0xf0 == 0xc0 and insn[1] == 0xff, 'Power pointer opcode changed')
        pointer = int.from_bytes(insn[2:], 'little')
        require(code_at(pointer, len(target)) == target if isinstance(target, bytes)
                else pointer == value(target), 'Power pointer target changed at ' + hex(off))
        normalized[off+2:off+6] = bytes(4)
    # LTO merges globals under the ota_status base. Adding display state moves
    # these three RAM operands, not the power sequence. Validate destinations
    # against their symbols before restoring historical encodings for hashing.
    base=value('ota_status')
    layout=tuple(value(n)-base for n in ('lrc.0','lrc.6','sys_low_power'))
    if usb_audio and not factory_diag:
        # Reviewed original composite and composite-with-screenshot layouts.
        require(layout in ((224,236,260),(228,240,264),(260,272,296)),
                'Composite audio power merged-global layout changed')
    for off,target,prefix,regbits,historical in (
        (0x0d6,'lrc.0',b'\x5a\xee',0x10,bytes.fromhex('5a ee 14 04')),
        (0x104,'lrc.6',b'\xd0\xec',0x80,bytes.fromhex('d0 ec 81 05')),
        (0x3c0,'sys_low_power',b'\xd0\xec',0x80,bytes.fromhex('d0 ec 81 36')),
    ):
        delta=value(target)-base
        if usb_audio or factory_diag:
            require(0<=delta<512 and delta%4==0,'Composite audio power state offset/alignment changed')
        elif usb_only and delta==256:
            # Screenshot CDC link: reviewed sys_low_power store d1 ec 81 30
            # at power_init+0x3c0, with LRC globals at+220/+232.
            require(off==0x3c0 and layout==(220,232,256),
                    'Screenshot CDC power merged-global layout changed')
        else:
            require(0<=delta<256 and delta%4==0,'Power merged-global offset outside reviewed encoding')
        require(symbols[target][1]==4,'Power merged-global object size changed')
        if off==0x0d6:
            # Diagnostics link:0x20012da, r1=r8+260 (01 e1 04 81).
            # The extended add-immediate replaces the prior small-offset
            # encoding; exact operands still resolve to lrc.0, not a whitelist
            # bypass for arbitrary power instructions.
            require(value('lrc.1')==value('lrc.0')+4 and value('lrc.2')==value('lrc.0')+8,
                    'Power LRC state layout changed')
            if (usb_audio and layout==(260,272,296)) or (factory_diag and delta>=256):
                # The former mov+preincrement-store (6 bytes) is replaced by
                # add-immediate+store (6 bytes), with identical destination.
                expected_add=bytes((1,0xe1,delta&255,0x80|(delta>>8),0x98,0x40))
                require(code_at(start+0x0d4,6)==expected_add,
                        'Power merged-global target changed: lrc.0')
                normalized[0x0d4:0x0da]=bytes.fromhex('81 16 5a ee 14 04')
                continue
            expected=prefix+bytes([regbits|(delta&15),delta>>4])
        else:
            # The reviewed profiles put sys_low_power at +256/+260/+264:
            # its ninth offset bit is encoded in the first opcode byte.
            expected=bytes([prefix[0]|(delta>>8),prefix[1]])+bytes([regbits|1|(delta&15),((delta&255)>>4)|(0x30 if off==0x3c0 else 0)])
        require(code_at(start+off,4)==expected,'Power merged-global target changed: '+target)
        normalized[off:off+4]=historical
    digest = hashlib.sha256(normalized).hexdigest()
    require(digest == BODY_SHA256, 'Power initializer instructions changed: ' + digest)

    gateway = value('fm1_board_power_init')
    require(symbols['fm1_board_power_init'][1] == 32, 'Board power gateway size changed')
    expected = bytearray.fromhex('75 04 c4 ff 00 00 00 00 50 ee 4c 02 80 48 45 21 c5 6c '
                                  '00 00 00 00 52 ee 4c 52 4c ea 02 40 55 04')
    struct.pack_into('<I', expected, 4, phase-48)
    if usb_audio and usb_midi:
        # MIDI/UAC r4 has the already reviewed +28 guard/+32 stage layout,
        # shifted from the legacy audio-only +24/+28 layout. The complete
        # same 32-byte guarded gateway remains pinned below, including its
        # exact call target and ordered stage 1/2 writes.
        require(usb_only and phase==value('ota_status')+32 and
                value('initialized')==value('ota_status')+28 and symbols['initialized'][1]==4,
                'MIDI composite power gateway state layout changed')
        expected=bytearray.fromhex('75 04 c4 ff 00 00 00 00 50 ee 4c 01 80 48 45 21 c5 68 '
                                  '00 00 00 00 52 ee 4c 51 48 ea 02 40 55 04')
        struct.pack_into('<I',expected,4,value('ota_status'))
    elif usb_audio:
        require(usb_only and phase==value('ota_status')+28 and
                value('initialized')==value('ota_status')+24 and symbols['initialized'][1]==4,
                'Composite audio power gateway state layout changed')
        expected=bytearray.fromhex('75 04 c4 ff 00 00 00 00 50 ee 48 01 80 48 45 21 c5 67 '
                                  '00 00 00 00 52 ee 48 51 47 ea 02 40 55 04')
        struct.pack_into('<I',expected,4,value('ota_status'))
    elif usb_only:
        if phase==value('ota_status')+24:
            expected=bytearray.fromhex('75 04 c4 ff 00 00 00 00 50 ee 44 01 80 48 45 21 c5 66 '
                                          '00 00 00 00 52 ee 44 51 46 ea 02 40 55 04')
            struct.pack_into('<I',expected,4,phase-24)
        elif phase==value('ota_status')+28:
            # MOD PC-library link: LTO merged the guard byte at +24 and
            # fm1_power_stage at +28. Disassembly at 0x200162c confirms the
            # same guarded gateway and call to the reviewed power initializer.
            expected=bytearray.fromhex('75 04 c4 ff 00 00 00 00 50 ee 48 01 80 48 45 21 c5 67 '
                                          '00 00 00 00 52 ee 48 51 47 ea 02 40 55 04')
            struct.pack_into('<I',expected,4,value('ota_status'))
        else:
            # Four-channel grid link: ota_status0x1c47630, initialized+28,
            # fm1_power_stage+32. Disassembly at 0x200162c keeps the same
            # one-time guard, stage 1/2 stores and call to power_init.
            require(phase==value('ota_status')+32 and
                    value('initialized')==value('ota_status')+28 and
                    symbols['initialized'][1]==4,
                    'USB power stage merged-global offset changed')
            expected=bytearray.fromhex('75 04 c4 ff 00 00 00 00 50 ee 4c 01 80 48 45 21 c5 68 '
                                          '00 00 00 00 52 ee 4c 51 48 ea 02 40 55 04')
            struct.pack_into('<I',expected,4,value('ota_status'))
    require(call_target(code_at(gateway+18, 4), gateway+18) == start,
            'Board power gateway bypasses SDK power_init')
    expected[18:22] = code_at(gateway+18, 4)
    require(code_at(gateway, 32) == expected, 'Board power gateway instructions changed')

    task = value('app_task_handler')
    size = symbols['app_task_handler'][1]
    body = code_at(task, size)
    calls = [off for off in range(0, size-3, 2)
             if body[off] & 0xc0 == 0x80 and body[off+1] == 0xea
             and call_target(body[off:off+4], task+off) == gateway]
    require(len(calls) == 1, 'Board power call missing or repeated in app_task')
    off = calls[0]
    require(body[off+4:off+6] == b'\x40\x24' and
            call_target(body[off+6:off+10], task+off+6) == value('fm1_boot_trace_mark'),
            'Board power does not precede board-complete trace')
    def init_loop(group):
        return (b'\xc4\xff'+struct.pack('<I', value(group+'_begin'))+
                b'\xc5\xff'+struct.pack('<I', value(group+'_end'))+
                bytes.fromhex('04 82 40 05 c0 00 85 e9 fc 41'))
    screen_first='fm1_early_delay_chunk' in symbols
    next_init=off+10
    if screen_first:
        require(call_target(body[next_init:next_init+4],task+next_init)==value('fm1_display_test_init'),
                'Board power startup ordering changed: missing first display')
        next_init+=4
    require(off >= 22 and body[off-22:off] == init_loop('platform_initcall') and
            body[next_init:next_init+22] == init_loop('initcall'),
            'Board power startup ordering changed')
    return {'enabled': True, 'initializer': hex(start), 'gateway': hex(gateway),
            'parameter_address': hex(param), 'parameter_hex': PARAMETERS.hex(),
            'stage_address': hex(phase), 'app_task_call': hex(task+off),
            'normalized_body_sha256': digest, 'machine_code_bytes_verified': 1082,
            'cfg_voltage_override_or_auto_fix_hook_present': False,'screen_first':screen_first,
            'hardware_verified': False}
