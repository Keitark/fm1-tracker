"""Fail-closed compiled pre-OS diagnostic placement; not hardware qualification."""
from audit_power import require, call_target
import hashlib

MAIN_SHA='d0f828166eae275daf2bfbb13831f022ef3732a31c57eb034dae1f4d1e168c58'
WRAPPER=bytes.fromhex('10 04 bf ea b2 fe 51 81 80 ff 2e 81 02 00 00 04')


def audit_pre_os(symbols, code_at):
    if '__wrap_os_init' not in symbols:
        return {'enabled':False, 'hardware_verified':False}
    expected={'main':(0x0200171c,0x138),'__wrap_os_init':(0x02001686,16),
              'fm1_board_power_init':(0x020013f0,32),
              'fm1_display_test_init':(0x020015d0,0xb6),'os_init':(0x020297c2,22)}
    for name,pair in expected.items():
        require(symbols[name][:2]==pair,'Pre-OS reviewed symbol moved: '+name)
    main=symbols['main'][0];wrapper=symbols['__wrap_os_init'][0]
    require(call_target(code_at(main+0x114,4),main+0x114)==wrapper,
            'Main still bypasses pre-OS wrapper')
    require(code_at(wrapper,16)==WRAPPER,'Pre-OS power/display/original OS call sequence changed')
    require(hashlib.sha256(code_at(main,0x138)).hexdigest()==MAIN_SHA,
            'Pre-OS main prerequisite/order instructions changed')
    require(call_target(code_at(main+0x110,4),main+0x110)==symbols['p33_io_latch_init'][0],
            'GPIO latch release no longer immediately precedes checkpoint')
    require(call_target(code_at(main+0x128,6),main+0x128)==symbols['os_start'][0],
            'Scheduler continuation changed')
    require(call_target(code_at(wrapper+8,6),wrapper+8)==symbols['os_init'][0],
            'Original OS initialization bypassed')
    return {'enabled':True,'wrapper':hex(wrapper),'main_call':hex(main+0x114),
            'before_os_init_and_cpu1_launch':True,'power_config':0,
            'original_os_init_retained':True,'hardware_verified':False,
            'reviewed_main_bytes':0x138,'reviewed_wrapper_bytes':16}
