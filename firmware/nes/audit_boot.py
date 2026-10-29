"""Static ELF/application closure checks; does not certify physical bootability."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from embed_rom50 import ROMS
from audit_power import audit_power, call_target
from audit_pre_os import audit_pre_os
from audit_usb_packet import audit_usb_packet

XIP=0x02000120
RAM=0x01c00000
RAM_LIMIT=0x01c7fd4c
APP_BUDGET=512*1024  # Deliberately conservative code budget, not a flash offset.
PARTS=('.text','.data','.dynamic_data','.ram0_data','.cache_ram_data')


def require(condition,message):
    if not condition: raise ValueError(message)


def audit(elf,app,require_peripherals=False,rom='rom50',display_only=False,require_boot_trace=False,require_board_power=False,require_screen_first=False,usb_only=False,usb_peripheral_tests=False,usb_nes=False,usb_mdx=False,usb_audio=False,usb_mod=False,factory_diag=False,factory_user_module=False,factory_clock='sdk-default',factory_hal_access=None,usb_video=False,tracker_build_id=None):
    require(type(usb_audio) is bool and (not usb_audio or usb_mdx or usb_mod or factory_diag),'Composite audio requires an explicit MDX, MOD or factory profile')
    require(not factory_diag or (usb_only and usb_peripheral_tests and not (usb_nes or usb_mdx or usb_mod)),'Factory profile must be asset-free CDC peripheral diagnostics')
    require(not factory_user_module or factory_diag,'User module requires explicit factory profile')
    require(type(usb_video) is bool and (not usb_video or factory_diag or usb_mod),'USB video requires an explicit MOD or Forge factory profile')
    if factory_hal_access is not None:
        require(factory_diag and isinstance(factory_hal_access,dict) and set(factory_hal_access)=={'buttons','encoders','volume','lcd','audio'} and all(type(v) is bool for v in factory_hal_access.values()),'Invalid factory HAL access selection')
    require(not usb_nes or (usb_only and usb_peripheral_tests and rom=='smb1'),'NES CDC requires explicit SMB1 peripheral profile')
    require(not usb_mdx or (usb_only and usb_peripheral_tests and not usb_nes),'MDX requires a separate CDC peripheral profile')
    require(not usb_mod or (usb_only and usb_peripheral_tests and not usb_nes and not usb_mdx),'MOD requires a separate CDC peripheral profile')
    tracker_composite=usb_mod and (usb_audio or usb_video)
    require(tracker_build_id is None or (usb_mod and isinstance(tracker_build_id,str) and len(tracker_build_id)==64 and all(c in '0123456789abcdef' for c in tracker_build_id)),'Invalid expected tracker CDC build identity')
    require(not usb_peripheral_tests or usb_only,'Peripheral diagnostic requires USB transport audit')
    require(not usb_only or not (require_peripherals or display_only or require_screen_first),'Conflicting USB-only audit')
    require(not(display_only and require_peripherals),'Conflicting diagnostic/peripheral audit')
    require(rom in ROMS,'Unknown cartridge')
    rom_size,_,rom_sha=ROMS[rom]
    require(len(elf)>=52 and elf[:7]==b'\x7fELF\x01\x01\x01','Expected ELF32 little endian')
    header=struct.unpack_from('<HHIIIIIHHHHHH',elf,16)
    kind,machine,version,entry,_,shoff,_,_,_,_,shsize,shcount,shstr=header
    require(kind==2 and machine==0xf1 and version==1,'Expected pi32v2 executable')
    require(entry==XIP,'Wrong FM-1 XIP entry')
    require(shsize==40 and shcount>0 and shstr<shcount and shoff+shcount*40<=len(elf),'Invalid section table')
    raw=[struct.unpack_from('<10I',elf,shoff+i*40) for i in range(shcount)]
    def data(s):
        require(s[4]+s[5]<=len(elf),'Section extends beyond ELF')
        return elf[s[4]:s[4]+s[5]]
    names=data(raw[shstr])
    def string(table,offset):
        require(offset<len(table),'String offset out of range')
        end=table.find(b'\0',offset)
        require(end>=0,'Unterminated string')
        return table[offset:end].decode('utf-8')
    sections={string(names,s[0]):s for s in raw}
    for name in PARTS+('.ram0_bss','.boot_info','.symtab'):
        require(name in sections,'Missing section '+name)
    require(sections['.text'][3]==XIP and not sections['.text'][2]&1,'Bad XIP text mapping')
    for name in ('.data','.bss','.dynamic_data','.dynamic_bss'):
        require(name in sections and sections[name][5]==0,'External SDRAM is not present: '+name)
    allowed=set(PARTS)|{'.ram0_bss','.boot_info','.cache_ram_bss'}
    for name,s in sections.items():
        if s[2]&2 and s[5]: require(name in allowed,'Unexpected allocated section '+name)
    for name in ('.ram0_data','.ram0_bss'):
        s=sections[name]
        require(RAM<=s[3] and s[3]+s[5]<=RAM_LIMIT,'RAM overflow: '+name)
    require(sections['.ram0_bss'][3]>=sections['.ram0_data'][3]+sections['.ram0_data'][5],'RAM overlap')
    for name in ('.cache_ram_data','.cache_ram_bss'):
        s=sections[name]
        require(0x1f28000<=s[3] and s[3]+s[5]<=0x1f2f000,'Cache RAM overflow')
    boot=sections['.boot_info']
    require(boot[3]==RAM_LIMIT and boot[5]==52,'Boot-info ABI differs from SDK')
    packed=b''.join(data(sections[name]) for name in PARTS)
    require(app==packed,'Application differs from exact vendor section concatenation')
    require(0<len(app)<=APP_BUDGET,'Application exceeds conservative code budget')
    symtab=sections['.symtab'];strings=data(raw[symtab[6]])
    require(symtab[9]==16 and symtab[5]%16==0,'Invalid symbol table')
    symbols={}
    for off in range(0,symtab[5],16):
        name,value,size,info,other,index=struct.unpack_from('<IIIBBH',data(symtab),off)
        name=string(strings,name)
        require(not(name and index==0 and info>>4 in (1,2)),'Unresolved symbol '+name)
        if name: symbols[name]=(value,size,index)
    usb_midi='fm1_usb_midi_poll' in symbols
    require(not usb_midi or (usb_only and not usb_video),'USB MIDI requires USB0 and excludes conflicting UVC')
    def value(name):
        require(name in symbols,'Missing startup symbol '+name)
        return symbols[name][0]
    def code_at(address,size):
        owner=next((s for s in sections.values() if s[2]&2 and s[1]!=8 and
                    s[3]<=address and address+size<=s[3]+s[5]),None)
        require(owner is not None,'Startup code outside image')
        return data(owner)[address-owner[3]:address-owner[3]+size]
    def long_call_target(address):
        code=code_at(address,6)
        require(code[:2]==b'\x80\xff','Expected reviewed startup long call')
        return address+6+struct.unpack_from('<i',code,2)[0]
    def calls_to(caller,callee):
        start,size,_=symbols[caller];body=code_at(start,size);target=value(callee);hits=[]
        for off in range(0,size-1,2):
            n=6 if body[off:off+2]==b'\x80\xff' else 4 if body[off]&0xc0==0x80 and body[off+1]==0xea else 2 if body[off]&0x8f==1 and body[off+1]&0xe0==0x80 else 0
            if n and off+n<=size and call_target(body[off:off+n],start+off)==target:hits.append(off)
        return hits
    tracker_identity=None
    if tracker_build_id is not None or 'fm1_tracker_build_id' in symbols:
        require(usb_mod and not usb_midi,'Tracker CDC identity requires MOD without runtime MIDI')
        require('fm1_tracker_build_id' in symbols,'Missing tracker CDC build identity')
        address,size,index=symbols['fm1_tracker_build_id'];owner=sections['.text']
        require(size==32 and index==raw.index(owner) and owner[3]<=address and address+size<=owner[3]+owner[5] and not owner[2]&1,'Tracker CDC build identity must be immutable 32-byte XIP data')
        build_id=code_at(address,32).hex()
        require(build_id!='00'*32 and (tracker_build_id is None or build_id==tracker_build_id),'Tracker CDC build identity differs from source closure')
        require('fm1_tracker_fwinfo' in symbols and symbols['fm1_tracker_fwinfo'][1]>0,'Missing tracker CDC read-only query helper')
        require(len(calls_to('fm1_mod_usb_command','fm1_tracker_fwinfo'))==1,'Tracker CDC query must dispatch exactly once')
        require(b'FM1FW/1 MOD-EDITOR/1 ' in app,'Missing tracker CDC wire identity')
        # Reviewed O2 helper: validate all 39 command bytes before any output;
        # copy a constant prefix, read 32 volatile identity bytes, hex encode,
        # echo 32 nonce bytes, then LF/NUL. Only memmove is called. Its complete
        # SDK assembly body and strncmp entry dispatch are pinned below.
        require(symbols['fm1_tracker_fwinfo'][1]==200,'Tracker CDC query helper size changed')
        query=bytearray(code_at(value('fm1_tracker_fwinfo'),200))
        for name,size,expected in (('fm1_tracker_fwinfo.prefix',22,b'FM1FW/1 MOD-EDITOR/1 \0'),
                                   ('fm1_tracker_fwinfo.hex',17,b'0123456789abcdef\0')):
            require(name in symbols,'Missing tracker CDC query constant '+name)
            address,length,index=symbols[name]
            require(length==size and index==raw.index(owner) and owner[3]<=address and address+size<=owner[3]+owner[5] and code_at(address,size)==expected,'Tracker CDC query constant changed or writable')
        for off,opcode,name in ((0x60,b'\xc1\xff','fm1_tracker_fwinfo.prefix'),
                                (0x74,b'\xc2\xff','fm1_tracker_build_id'),
                                (0x7a,b'\xc3\xff','fm1_tracker_fwinfo.hex')):
            require(query[off:off+2]==opcode and struct.unpack_from('<I',query,off+2)[0]==value(name),'Tracker CDC query constant pointer changed')
            query[off+2:off+6]=b'\0'*4
        require(calls_to('fm1_tracker_fwinfo','memmove')==[0x6a] and query[0x6a:0x6c]==b'\x80\xff','Tracker CDC query copy call changed')
        query[0x6a:0x70]=b'\0'*6
        require(hashlib.sha256(query).hexdigest()=='598141332615df1c6e3e1310b53ed4d175d1de27203ab95d704bc16bb6ce5214','Tracker CDC query read-only instructions changed')
        require(hashlib.sha256(code_at(value('memmove'),194)).hexdigest()=='7c3346543954e137b7503cc91cd5238d5e6fc5e91861bca749e85e6f9c2ededc','Tracker CDC query memory-copy instructions changed')
        require(symbols['strncmp'][1]==26 and hashlib.sha256(code_at(value('strncmp'),26)).hexdigest()=='531ed2c20edd5a3e52391f34119b5c3a8f7ec23908ae9e5e387bc2e9ee5184d3','Tracker CDC query prefix-compare instructions changed')
        # These three connected regions execute before application locks/state:
        # save arguments/compare, FWINFO helper/error selection, reply/return.
        command=code_at(value('fm1_mod_usb_command'),0x1dc)
        require(calls_to('fm1_mod_usb_command','fm1_tracker_fwinfo')==[0xac] and calls_to('fm1_mod_usb_command','strncmp')[0]==0x16,'Tracker CDC query first dispatch changed')
        dispatch=bytearray(command[:0x1e]+command[0xa4:0xc2]+command[0x1ce:0x1dc])
        for off,expected in ((0x0e,b'FWINFO\0'),(0x1e+0x10,b'ERR FWINFO\n\0')):
            pointer=struct.unpack_from('<I',dispatch,off+2)[0]
            require(dispatch[off:off+2]==b'\xc1\xff' and owner[3]<=pointer and pointer+len(expected)<=owner[3]+owner[5] and code_at(pointer,len(expected))==expected,'Tracker CDC dispatch constant pointer changed')
            dispatch[off+2:off+6]=b'\0'*4
        dispatch[0x16:0x1c]=b'\0'*6;dispatch[0x1e+8:0x1e+12]=b'\0'*4
        require(hashlib.sha256(dispatch).hexdigest()=='4baa40e22955994eeffa126af496204b6722ccb91c8d6eaff56ce48c20d32778','Tracker CDC query first-dispatch instructions changed')
        tracker_identity={'symbol':'fm1_tracker_build_id','build_id':build_id,'nonce_bytes':16,'read_only':True}
    require(not any(name in symbols for name in ('fm1_tile_test_reference',
            'fm1_reference_background','fm1_reference_sprite')),'Host tile oracle linked into target')
    if 'fm1_volume_start' in symbols:
        require(factory_diag or (usb_nes and 'fm1_nes_scan_tick' in symbols) or (usb_mdx and 'fm1_mdx_scan_tick' in symbols) or (usb_mod and 'fm1_mod_scan_tick' in symbols),'Volume requires paced player or factory profile')
        require(all(name in symbols for name in (('fm1_volume_tick','fm1_volume_stop') if factory_diag else ('fm1_volume_tick','fm1_volume_stop','mdx_volume' if usb_mdx else 'mod_volume' if usb_mod else 'nes_volume'))),
                'Missing volume reader lifecycle')
        require(not any(name in symbols for name in ('adc_init','adc_isr','adc_scan')),
                'Volume ADC has competing SDK owner')
        if not (usb_mdx or usb_mod or factory_diag):require(b'VOLUME adc=4 gpio=PB6 raw=' in app,'Missing volume telemetry')
    if 'fm1_tile_expand' in symbols:
        address,size,_=symbols['fm1_tile_expand']
        ram=sections['.ram0_data']
        require(size==512 and address%4==0 and ram[3]<=address and
                address+size<=ram[3]+ram[5],'Tile table must occupy aligned initialized internal RAM')
        expected=struct.pack('<256H',*(sum(((x>>bit)&1)<<(2*bit)
                for bit in range(8)) for x in range(256)))
        require(code_at(address,size)==expected,'Tile table data differs from bitplane expansion')
    # Pinned compiler output: six volatile source words -> 92-byte stack
    # buffer, zero words 6..22, one call to the original SDK initializer.
    # This is deliberately fail-closed on compiler/codegen changes; review
    # the disassembly before changing this whitelist.
    bridge=value('__wrap_boot_info_init')
    require(symbols['__wrap_boot_info_init'][1]==54,'Boot bridge code size changed')
    bridge_code=code_at(bridge,54)
    bridge_prefix=bytes.fromhex(
        '10 04 e2 89 41 20 04 86 d8 ec 0a 21 8b 80 d8 ec 3b 21 c1 21 '
        '81 f8 f8 0d 40 26 41 20 04 84 8a 80 d8 ec 2b 10 c0 21 '
        '80 f8 fa 2f 88 80')
    require(bridge_code[:44]==bridge_prefix and bridge_code[50:]==bytes.fromhex('02 97 00 04'),
            'Boot bridge prefix/zero-fill instructions changed')
    require(long_call_target(bridge+44)==value('boot_info_init'),'Boot bridge bypasses SDK initializer')
    startup=value('chip_entry')
    require(code_at(startup,2)==b'\xee\xff' and code_at(startup+6,2)==b'\xed\xff',
            'Startup must set SP/SSP before boot bridge')
    require(long_call_target(startup+12)==bridge,'Startup bypasses boot compatibility bridge')
    # Check executed operands/loops, not just linker symbols. The old checks
    # could accept a corrupted SP, RAM-copy immediate or main jump when both
    # ELF and flat application contained the same corruption.
    traced='__wrap_memory_init' in symbols
    require(not require_boot_trace or traced,'Missing refined boot trace')
    memory_target=value('__wrap_memory_init') if traced else value('memory_init')
    startup_code=bytearray.fromhex(
        'ee ff b8 71 c1 01 ed ff b8 81 c1 01 80 ff 46 1e 00 00 '
        'c3 ff a0 16 c0 01 41 20 c2 ff f0 7e 01 00 a2 a2 02 03 b1 05 f2 5d '
        'c4 ff 00 00 c0 01 c1 ff 00 77 02 02 c2 ff a0 16 00 00 a2 a2 '
        '12 03 13 05 c3 05 f2 5c 80 ff 14 01 00 00 03 16 '
        'c0 ff 20 01 00 04 c1 ff 00 77 02 02 c2 ff 00 00 00 00 '
        '01 e8 04 00 80 ff f8 00 00 00 30 16 c3 ff 20 01 00 04 '
        '41 20 c2 ff 00 00 00 00 a2 a2 02 03 b1 05 f2 5d '
        '80 ff 9e 09 c0 ff 80 ff ba 43 02 00 80 ff 20 11 00 00 '
        'c6 ff 72 0c 00 02 d6 00')
    for offset,name in ((2,'_cpu0_ustack'),(8,'_cpu0_sstack'),
                        (20,'_ram0_bss_vma'),(28,'_ram0_bss_size'),
                        (42,'_ram0_data_vma'),(48,'_ram0_data_lma'),(54,'_ram0_data_size'),
                        (84,'_ram0_data_lma'),(148,'main')):
        struct.pack_into('<I',startup_code,offset,value(name))
    calls={12:bridge,68:value('boot_init_after_ram_ready'),98:value('boot_memmove'),
           128:value('cache_way_config'),134:memory_target,140:value('update_result_get')}
    for offset,target in calls.items():struct.pack_into('<i',startup_code,offset+2,target-(startup+offset+6))
    require(startup==XIP+0x6a and code_at(XIP,2)==bytes.fromhex('14 94'),
            'Startup entry branch changed')
    require(code_at(startup,len(startup_code))==startup_code,'Startup instruction/operand sequence changed')
    bss=sections['.ram0_bss']
    require(bss[3]<=value('_stack_info_begin')<=value('_cpu0_ustack')-96 and
            value('_cpu0_ustack')+4096==value('_cpu0_sstack')<=value('_cpu1_ustack')-96 and
            value('_cpu1_ustack')+4096==value('_cpu1_sstack')<=bss[3]+bss[5],
            'Startup stack bounds changed')
    require(value('_ram0_data_size')%4==0 and value('_ram0_bss_size')%4==0,
            'Startup word loops need aligned lengths')
    cache=value('cache_way_config')
    require(code_at(cache+2,12)==bytes.fromhex('c1 ff 00 00 00 00 c2 ff 07 00 00 00'),
            'Cache way configuration changed')
    require(code_at(cache+0xe2,18)==b'\xc0\xff'+struct.pack('<I',0x1f28000)+
            b'\xc1\xff'+struct.pack('<I',value('cache_ram_data_lma'))+
            b'\xc2\xff'+struct.pack('<I',sections['.cache_ram_data'][5]),
            'Cache RAM copy operands changed')
    require(long_call_target(cache+0xf4)==value('memmove'),'Cache RAM copy call changed')
    if tracker_composite:
        # CPU-only tracker/UAC/UVC state now occupies cache BSS. Verify the
        # actual startup zero, including its full current section length.
        cache_bss=sections['.cache_ram_bss']
        require(symbols['cache_way_config'][1]==0x110 and
                code_at(cache+0xfa,14)==b'\xc0\xff'+struct.pack('<I',cache_bss[3])+
                b'\xc2\xff'+struct.pack('<I',cache_bss[5])+b'\x41\x20' and
                long_call_target(cache+0x108)==value('memset') and
                code_at(cache+0x10e,2)==b'\x57\x04',
                'Tracker cache BSS startup clear changed')
    require(symbols['memory_init'][1]==2 and code_at(value('memory_init'),2)==b'\x80\x00',
            'Reviewed SDK memory hook changed')
    if traced:
        address,size,_=symbols['fm1_boot_trace']
        require(size==16 and bss[3]<=address and address+size<=bss[3]+bss[5],
                'Boot trace outside initialized RAM')
        trace_size=46 if usb_only else 44
        require(symbols['__wrap_memory_init'][1]==trace_size,'Boot trace wrapper layout changed')
        wrapper=code_at(memory_target,trace_size)
        expected_wrapper=bytearray.fromhex(
            '10 04 c0 ff 00 00 00 00 c1 ff 54 42 4d 46 81 60 '
            '41 21 81 61 42 ea 00 00 40 21 00 00 00 00 '
            '80 ff 00 00 00 00 40 22 00 00 00 00 00 04')
        struct.pack_into('<I',expected_wrapper,4,address)
        if usb_only:
            # Reviewed USB links merge the trace at ota_status+176 and use
            # a writeback store instead of the NES direct-base store.
            delta=address-value('ota_status')
            encodings={176:'d0 ec 03 1b',180:'d0 ec 07 1b'}
            if usb_peripheral_tests:encodings.update({228:'d0 ec 07 1e',244:'d0 ec 07 1f'})
            # Reviewed NES CDC links: same writeback store. KEYSPI diagnostic
            # disassembly stores at ota_status 0x1c196f0 + 284 = trace 0x1c1980c.
            # Input-recovery link: ota_status 0x1c19a30 + 308 = trace
            # 0x1c19b64; reviewed __wrap_memory_init store at 0x2003362.
            # IRQ link: ota_status 0x1c19a30 +316 = trace 0x1c19b6c,
            # store at0x200336e; same 46-byte wrapper and22-byte marker.
            # Audio-priority link: ota_status0x1c19a40 +312 = trace0x1c19b78;
            # reviewed store0x200336e is d1 ec 0b 13. Wrapper/marker unchanged.
            # Live-FX link: ota_status0x1c19a40 +324 = trace0x1c19b84;
            # reviewed store0x2003370 is d1 ec 07 14. Same wrapper/marker.
            if usb_nes:encodings.update({280:'d1 ec 0b 11',284:'d1 ec 0f 11',288:'d1 ec 03 12',308:'d1 ec 07 13',312:'d1 ec 0b 13',316:'d1 ec 0f 13',324:'d1 ec 07 14'})
            # Profiling link: ota_status0x1c1a5f0 +340 = trace0x1c1a744;
            # reviewed __wrap_memory_init0x2003362, store0x2003370:
            # d1 ec 07 15 = [++r0=340] = r1. Same46-byte wrapper/22-byte marker.
            if usb_nes:encodings.update({340:'d1 ec 07 15'})
            # MDX standalone profile: reviewed store at ota_status+300,
            # d1 ec 0f 12 is [++r0=300]=r1; same startup wrapper contract.
            # Panel/octave link: ota_status0x1c4b910 +336 = trace0x1c4ba60;
            # reviewed store0x200276c: d1 ec 03 15 = [++r0=336]=r1.
            if usb_mdx:encodings.update({300:'d1 ec 0f 12',312:'d1 ec 0b 13',316:'d1 ec 0f 13',336:'d1 ec 03 15'})
            # Screenshot CDC link: ota_status0x1c4b910 +340 = trace0x1c4ba64;
            # reviewed store0x200276c: d1 ec 07 15. Same wrapper/marker.
            if usb_mdx:encodings.update({340:'d1 ec 07 15'})
            # MOD editor links: the PC catalog moves the merged trace to
            # ota_status+340; disassembly at 0x2002760 has the same reviewed
            # [++r0] store (d1 ec 07 15), with the 46-byte wrapper unchanged.
            # Four-channel grid link: ota_status0x1c47630 +344 =
            # trace0x1c47788; reviewed store0x2002760 is d1 ec 0b 15.
            # Status-counter link: ota_status0x1c47630 +352 = trace0x1c47790;
            # reviewed store0x2002766 is d1 ec 03 16. Wrapper unchanged.
            # RGB444 async MOD link: ota_status 0x1c5c7b0 +364 = trace
            # 0x1c5c91c; the reviewed wrapper store is [++r0=364]=r1.
            # Scope/recovery MOD link: ota_status 0x1c5be40 +368 = trace
            # 0x1c5bfb0; reviewed store at 0x200276c is [++r0=368]=r1.
            if usb_mod:encodings.update({328:'d1 ec 0b 14',332:'d1 ec 0f 14',336:'d1 ec 03 15',340:'d1 ec 07 15',344:'d1 ec 0b 15',352:'d1 ec 03 16',364:'d1 ec 0f 16',368:'d1 ec 03 17'})
            # Knob-only v29 adds the four-byte captured sequence. Reviewed
            # ota_status 0x1c5be40 +372 = trace 0x1c5bfb4; at 0x200276c
            # d1 ec 07 17 is [++r0=372]=r1. Wrapper/marker otherwise unchanged.
            if usb_mod:encodings.update({372:'d1 ec 07 17'})
            # Milestone #2 MOD link adds separate ring slots. Reviewed
            # ota_status 0x1c5be50 +380 = trace 0x1c5bfcc; store at
            # 0x200276c is d1 ec 0f 17. Same 46-byte wrapper/22-byte marker.
            if usb_mod:encodings.update({380:'d1 ec 0f 17'})
            # Composite tracker: the same complete 46-byte wrapper stores its
            # trace at ota_status+388; only this reviewed writeback is added.
            if tracker_composite:encodings.update({388:'d1 ec 07 18'})
            # Composite link: ota_status0x1c4db50 +344 = trace0x1c4dca8;
            # reviewed store0x200276c: d1 ec 0b 15. Wrapper otherwise unchanged.
            if usb_audio:encodings.update({344:'d1 ec 0b 15'})
            # Screenshot composite link: ota_status0x1c4db50 +348 = trace0x1c4dcac;
            # reviewed store0x200276c: d1 ec 0f 15. Same wrapper/marker.
            if usb_audio:encodings.update({348:'d1 ec 0f 15'})
            # Audio diagnostics link: ota_status0x1c4db90 +380 = trace0x1c4dd0c;
            # reviewed store0x2002774: d1 ec 0f 17. Same46-byte wrapper/marker.
            if usb_audio:encodings.update({380:'d1 ec 0f 17'})
            if factory_diag:
                # Symbol-resolved small/extended writeback store; retain the
                # complete instruction/call hash and exact BSS destination.
                require(0<=delta<512 and delta%4==0,'Factory trace offset outside reviewed writeback encoding')
                encodings[delta]=bytes((0xd0|(delta>>8),0xec,(delta&15)|3,((delta>>4)&15)|0x10)).hex(' ')
            require(delta in encodings,'USB trace merged-global offset changed: '+str(delta))
            struct.pack_into('<I',expected_wrapper,4,value('ota_status'))
            expected_wrapper[14:16]=bytes.fromhex(encodings[delta])
        for off in ((28,40) if usb_only else (26,38)):
            call=wrapper[off:off+4]
            require(call[0]&0xc0==0x80 and call[1]==0xea,'Boot trace mark call changed')
            relative=((call[0]&0x3f)<<16)|int.from_bytes(call[2:],'little')
            if relative & (1<<21):relative-=1<<22
            require(memory_target+off+4+relative*2==value('fm1_boot_trace_mark'),
                    'Boot trace mark target changed')
            expected_wrapper[off:off+4]=call
        struct.pack_into('<i',expected_wrapper,34 if usb_only else 32,value('memory_init')-(memory_target+(38 if usb_only else 36)))
        require(wrapper==expected_wrapper,'Boot trace wrapper instructions changed')
        marker=(bytes.fromhex('31 e1 ff 0f b1 ec 05 80 c1 ff')+struct.pack('<I',address)+
                bytes.fromhex('66 e8 08 10 90 63 80 00'))
        if usb_only:
            marker=(bytes.fromhex('31 e1 ff 0f b1 ec 05 80 c1 ff')+struct.pack('<I',address+8)+
                    bytes.fromhex('66 e8 00 10 90 61 80 00'))
        require(symbols['fm1_boot_trace_mark'][1]==len(marker) and
                code_at(value('fm1_boot_trace_mark'),len(marker))==marker,'Boot trace marker changed')
    cfg_start=value('syscfg_ops_begin');cfg_end=value('syscfg_ops_end')
    require(cfg_end-cfg_start==28 and value('cfg_bin')==cfg_start and symbols['cfg_bin'][1]==28,
            'Unexpected persistent configuration registry')
    require('cfg_vm' not in symbols and 'cfg_btif' not in symbols,
            'Persistent VM/BTIF auto-repair registrations present')
    cfg_words=struct.unpack('<7I',code_at(cfg_start,28))
    require(cfg_words[0]==value('syscfg_file_init'),'Configuration init is not the file reader')
    require(cfg_words[3]==0 and cfg_words[4]==0,'Configuration registry exposes write callbacks')
    require(tuple(cfg_words[i] for i in (1,2,5,6))==tuple(value(n) for n in
            ('syscfg_bin_check_id','syscfg_bin_read','syscfg_bin_group_read','syscfg_bin_ptr_read')),
            'Configuration read callbacks changed')
    # Retain and verify all startup registration groups. This change must not
    # remove the SDK integrity-check initcall, nor skip its runtime machinery.
    init_groups={
        'early_initcall':('app_version_check','sys_event_init','thread_fork_init','sdfile_init'),
        'platform_initcall':('syscfg_tools_init',),
        'initcall':('app_update_init',),
        'module_initcall':('wait_completion_init',),
        'late_initcall':('sdk_meky_check',),
    }
    init_report={}
    for group,expected in init_groups.items():
        begin=value(group+'_begin');end=value(group+'_end')
        require(end-begin==4*len(expected),'SDK initcall group changed: '+group)
        pointers=struct.unpack('<'+'I'*len(expected),code_at(begin,end-begin))
        require(pointers==tuple(value(name) for name in expected),'SDK initcall targets changed: '+group)
        init_report[group]=list(expected)
    # In this LTO link, sys_timeout_add is specialized for the SDK's delayed
    # integrity callback. Verify scheduling, not a hardware-dependent outcome.
    integrity=value('sdk_meky_check');timeout=value('sys_timeout_add')
    require(symbols['sdk_meky_check'][1]==62,'SDK integrity initializer layout changed')
    require(code_at(integrity+2,6)==b'\xc4\xff'+struct.pack('<I',value('isr_check_key')),
            'SDK integrity ISR registration changed')
    require(long_call_target(integrity+16)==value('request_irq') and
            long_call_target(integrity+30)==value('request_irq') and
            long_call_target(integrity+54)==timeout,'SDK integrity scheduling calls changed')
    require(symbols['sys_timeout_add'][1]==26 and
            long_call_target(timeout+2)==value('os_current_task') and
            code_at(timeout+8,6)==b'\xc1\xff'+struct.pack('<I',value('_mkey_check')) and
            code_at(timeout+14,4)==bytes.fromhex('42 e0 40 1f'),
            'SDK delayed integrity callback or 8000 ms timeout changed')
    task_table=value('task_info_table')
    expected_tasks=(('app_core',15,4096,1024),('sys_event',29,512,0),
                    ('systimer',14,256,0),('sys_timer',9,512,128),
                    (('#C0usb_diag' if factory_diag or tracker_composite else 'usb_diag',10,2048,0) if usb_only else ('fm1_nes',10,4096,0)))
    if usb_midi:expected_tasks=expected_tasks[:4]+(('#C0update',9,2048,0),)+expected_tasks[4:]
    if usb_peripheral_tests:expected_tasks+=(('#C0peripheral' if factory_diag or tracker_composite else 'peripheral',8,4096 if (usb_nes or usb_mdx or usb_mod or factory_diag) else 2048,0),)
    require(symbols['task_info_table'][1]==20*(len(expected_tasks)+1),
            'SDK/NES task table size changed')
    for i,(name,priority,stack,queue) in enumerate(expected_tasks):
        ptr,prio,stk,q,static_mem=struct.unpack('<IB3xIH2xI',code_at(task_table+20*i,20))
        require(code_at(ptr,len(name)+1)==name.encode()+b'\0' and
                (prio,stk,q,static_mem)==(priority,stack,queue,0),
                'SDK/NES task registration changed: '+name)
    require(code_at(task_table+20*len(expected_tasks),20)==bytes(20),'Task table terminator changed')
    worker='fm1_usb_task' if usb_only else 'fm1_nes_task'
    require(symbols.get(worker,(0,0,0))[1]>0,'Missing separate worker task')
    peripheral_symbols=('fm1_alink_isr','fm1_wl82_run_with_stats','fm1_wl82_keyscan_poll',
                        'fm1_audio_startup_process24','fm1_sdk_diag','iis_open','iis_irq_handler')
    if require_peripherals:
        for name in peripheral_symbols:require(name in symbols,'Missing integrated peripheral '+name)
        diag=symbols['fm1_sdk_diag'];s=sections['.ram0_bss']
        require(s[3]<=diag[0] and diag[0]+diag[1]<=s[3]+s[5],'SDK diagnostics outside BSS')
        # The SDK interrupt attribute must generate rti (81 00), not a normal
        # function epilogue. Symbol bounds stop us matching another function.
        address,size,_=symbols['fm1_alink_isr']
        owner=next((s for s in sections.values() if s[2]&2 and s[1]!=8 and
                    s[3]<=address and address+size<=s[3]+s[5]),None)
        require(owner is not None and size>=2,'ISR has no executable bytes')
        code=data(owner)[address-owner[3]:address-owner[3]+size]
        require(code[-2:]==b'\x81\x00','ALINK wrapper lacks interrupt return')
    require(value('_start')==XIP,'_start is not at image entry')
    runtime_states=('fm1_usb_stage','fm1_usb_heartbeat','fm1_usb_tx_dropped','fm1_usb_error') if usb_only else (
        (() if display_only else ('machine',))+('fm1_boot_stage','fm1_boot_result','fm1_boot_stats',
                 'fm1_boot_elapsed_ms','fm1_boot_heartbeat','fm1_boot_task_error'))
    for name in runtime_states:
        require(name in symbols,'Missing runtime state '+name)
        addr,size,_=symbols[name];s=sections['.ram0_bss']
        require(s[3]<=addr and addr+size<=s[3]+s[5],'State outside initialized BSS: '+name)
    if usb_only:
        forbidden=('msd_register','hid_register','uac_register')
        if not usb_nes:forbidden+=('machine','cartridge','fm1_nes_run','fm1_nes_player_run')
        else:
            for name in ('fm1_nes_player_run','fm1_board_run','fm1_display_write','fm1_audio_startup_process24'):
                require(name in symbols,'Missing NES CDC component '+name)
            require(symbols['machine'][1]==69240,'Unexpected NES state layout')
            addr,size,_=symbols['machine']
            require(bss[3]<=addr and addr+size<=bss[3]+bss[5],'NES state outside initialized internal RAM')
            require(b'NES=SMB1' in app,'Missing NES serial identity')
        if not usb_peripheral_tests:forbidden+=('fm1_display_test_init','iis_open','fm1_wl82_keyscan_start','fm1_alink_isr')
        for name in forbidden:
            require(name not in symbols,'USB-only image contains forbidden component '+name)
        for name in ('cdc_read_data','cdc_write_data','fm1_cdc_ready','fm1_usb_dma','fm1_diag_feed'):
            require(name in symbols,'Missing USB diagnostic '+name)
        descriptor=bytes((18,1,0,2,2,2,1,64,0x54,0x36,0x55,0x51,0,2,1,2,0,1))
        if usb_audio or usb_video or usb_midi:descriptor=bytes((18,1,0,2,0xef,2,1,64,0x54,0x36,0x55,0x51,3,2,1,2,0,1))
        if usb_midi:
            for name in ('fm1_usb_midi_init','fm1_midi_desc_config','fm1_midi_descriptor','fm1_midi_update_message','fm1_midi_update_poll'):
                require(name in symbols,'Missing common MIDI integration '+name)
            require(symbols['fm1_midi_build_id'][1]==32,'Common MIDI build identity must be 32 bytes')
            require(symbols['fm1_midi_install_enabled'][1]==1 and code_at(value('fm1_midi_install_enabled'),1)==b'\0','Unqualified MIDI installation must remain blocked')
            require('usb1_g_isr' not in symbols,'MIDI must retain USB0-only ownership')
            for name in ('fm1_midi_dma_in','fm1_midi_dma_out'):
                address,size,_=symbols[name]
                require(address%64==0 and size==256 and bss[3]<=address and address+size<=bss[3]+bss[5],'MIDI DMA alignment/internal RAM placement changed')
            midi_descriptor=bytes((8,11,0,2,1,0,0,0,9,4,0,0,0,1,1,0,0,9,36,1,0,1,9,0,1,0,
                9,4,0,0,2,1,3,0,0,7,36,1,0,1,65,0,6,36,2,1,1,0,6,36,2,2,2,0,
                9,36,3,1,3,1,2,1,0,9,36,3,2,4,1,1,1,0,9,5,3,2,64,0,0,0,0,5,37,1,1,1,
                9,5,132,2,64,0,0,0,0,5,37,1,1,3))
            require(symbols['fm1_midi_descriptor.d'][1]==100 and code_at(value('fm1_midi_descriptor.d'),100)==midi_descriptor,'Common MIDI descriptor/jacks/endpoint addresses changed')
        if usb_audio:
            for name in ('fm1_uac_desc_config','fm1_usb_audio_dac','fm1_usb_audio_stop','fm1_uac_descriptor','fm1_uac_dma'):
                require(name in symbols,'Missing composite audio component '+name)
            audio_dma,audio_size,_=symbols['fm1_uac_dma']
            require(audio_size==512 and audio_dma%64==0 and bss[3]<=audio_dma and audio_dma+audio_size<=bss[3]+bss[5],
                    'Audio DMA size/alignment/internal-RAM placement changed')
            require(symbols['fm1_uac_descriptor'][1]==173 and hashlib.sha256(code_at(value('fm1_uac_descriptor'),173)).hexdigest()==
                    '820961a648e4bf17faa19a3bdd788f7a70205ff0600b5fbb35c069deb7219d8d',
                    'UAC1 interface/terminal/format/endpoint descriptor changed')
        if factory_diag:
            if not usb_video:
                for name in ('fm1_uvc_state','fm1_uvc_dma','fm1_uvc_desc_config'):
                    require(name not in symbols,'Non-video factory links UVC '+name)
            audit_usb_packet(symbols,code_at,composite=usb_audio,video=usb_video,midi=usb_midi)
            for name in ('cartridge','player','song','upload','fm1_demo_mdx','fm1_demo_pdx','fm1_mod_open','fm1_mdx_load'):
                require(name not in symbols,'Factory contains media/player component '+name)
            for name in ('fm1_forge_usb_command','fm1_forge_render','fm1_forge_clock_prepare','fm1_peripheral_usb_audio_quiesce'):
                require(name in symbols,'Missing factory integration '+name)
            if factory_hal_access is not None:
                require('fm1_forge_hal_access' in symbols and symbols['fm1_forge_hal_access'][1]==5,'Missing factory HAL access table')
                expected=bytes(int(factory_hal_access[name]) for name in ('buttons','encoders','volume','lcd','audio'))
                require(code_at(value('fm1_forge_hal_access'),5)==expected,'Factory HAL access table does not match selected project')
            require(factory_clock in ('sdk-default','sdk-320'),'Invalid factory clock profile')
            clock_calls=calls_to('app_task_handler','fm1_forge_clock_prepare')
            starts=calls_to('app_task_handler','task_create')
            require(len(clock_calls)==1 and len([off for off in starts if off>clock_calls[0]])>=2,
                    'Factory clock preparation must precede diagnostic and USB task creation')
            sets=calls_to('fm1_forge_clock_prepare','system_clock_set') if 'system_clock_set' in symbols else []
            require(not ('clk_set' in symbols and calls_to('fm1_forge_clock_prepare','clk_set')),
                    'Factory clock must not use the SDK no-op clk_set API')
            gets=calls_to('fm1_forge_clock_prepare','clk_get')
            require(len(sets)==(1 if factory_clock=='sdk-320' else 0) and len(gets)==(6 if factory_clock=='sdk-320' else 4),
                    'Factory clock setter/readback does not match selected profile')
            if sets:
                setter=value('system_clock_set')
                # LTO specializes the SDK application setter to its only
                # caller. The requested clock is materialized in its body,
                # then stored in sys_cfg_info before the RAM critical path.
                require(symbols['system_clock_set'][1]==0x116,'Factory SDK clock setter body changed or is a no-op stub')
                require(code_at(setter+10,6)==b'\xc1\xff'+struct.pack('<I',320000000) and code_at(setter+30,2)==b'\xc1\x63',
                        'Factory SDK clock request must equal 320 MHz')
                for callee in ('cpu_enter_sfc_critical','clock_update_wait','cpu_exit_sfc_critical'):
                    require(len(calls_to('system_clock_set',callee))==1,'Factory SDK clock critical path changed: '+callee)
            require(b'FM1DIAG/1 FM1-FORGE/1 UBOOT=SERIAL COMMIT=BLOCKED\n' in app,'Factory serial identity changed')
            for name in ('fm1_forge_audio_output','fm1_audio_startup_process24'):
                require(name in symbols,'Missing factory audio pipeline '+name)
            gain=calls_to('fm1_forge_audio_output','fm1_audio_startup_process24')
            require(len(gain)==1,'Factory analog startup gain must have one owner')
            render_limit=calls_to('fm1_forge_audio_output','fm1_forge_audio_limit' if factory_user_module else 'fm1_forge_voice_render')
            require(len(render_limit)==1 and render_limit[0]<gain[0],
                    'Factory render/clamp must precede analog startup gain')
            quiet=calls_to('fm1_usb_rx_irq','fm1_peripheral_usb_audio_quiesce')
            reboot=calls_to('fm1_usb_rx_irq','go_mask_usb_updata')
            require(len(quiet)==len(reboot)==1 and quiet[0]<reboot[0],'Confirmed factory UBOOT DMA teardown changed')
            require(not calls_to('fm1_usb_boot_arm','fm1_peripheral_usb_audio_quiesce'),'UBOOT arm must not prematurely stop factory audio')
            if usb_video:
                for name in ('fm1_uvc_setup_init','fm1_uvc_desc_config','fm1_uvc_setup_filter','fm1_uvc_capture_stop','fm1_uvc_get_status','fm1_uvc_video_descriptor','fm1_uvc_state','fm1_uvc_dma','fm1_uvc_control_state','fm1_forge_video_capture','fm1_forge_capture_step','fm1_forge_capture_packet'):
                    require(name in symbols,'Missing Forge UVC component '+name)
                for name in ('uvc_register','uvc_open','uvc_alloc','uvc_malloc'):
                    require(name not in symbols,'Forge UVC must not link stock camera allocation '+name)
                vc=5 if usb_audio else 2;vs=vc+1
                def le16(n):return struct.pack('<H',n)
                def le32(n):return struct.pack('<I',n)
                expected_video=(bytes((8,11,vc,2,14,3,0,0,9,4,vc,0,0,14,1,0,0,13,36,1))+
                    le16(0x100)+le16(40)+le32(1000)+bytes((1,vs,18,36,2,1))+le16(0x201)+
                    bytes((0,0))+le16(0)*3+bytes((3,0,0,0,9,36,3,2))+le16(0x101)+
                    bytes((0,1,0,9,4,vs,0,0,14,2,0,0,14,36,1,1))+le16(77)+
                    bytes((0x84,0,2,0,0,0,1,0,27,36,4,1,1))+bytes.fromhex('5955593200001000800000aa00389b71')+
                    bytes((16,1,0,0,0,0,30,36,5,1,0))+le16(256)+le16(240)+
                    le32(4915200)*2+le32(122880)+le32(2000000)+bytes((1,))+le32(2000000)+
                    bytes((6,36,13,4,4,4,9,4,vs,1,1,14,2,0,0,7,5,0x84,1))+le16(642)+bytes((1,)))
                require(len(expected_video)==159 and symbols['fm1_uvc_video_descriptor'][1]==159 and
                    code_at(value('fm1_uvc_video_descriptor'),159)==expected_video,'Forge UVC descriptor/numbering/mode changed')
                dma,dma_bytes,_=symbols['fm1_uvc_dma']
                require(dma_bytes==1024 and dma%64==0 and bss[3]<=dma and dma+dma_bytes<=bss[3]+bss[5],
                    'Forge UVC DMA size/alignment/SRAM placement changed')
                require(symbols['fm1_uvc_state'][1]==2068,'Forge UVC three-packet queue layout changed')
                require(symbols['fm1_uvc_control_state'][1]==64,'Forge UVC control storage changed')
                require(1024<symbols['fm1_forge_video_capture'][1]<=1280,'Forge UVC snapshot/two-row storage changed')
                for name in ('fm1_uvc_state','fm1_uvc_control_state','fm1_forge_video_capture'):
                    address,size,_=symbols[name];cache=sections['.cache_ram_bss']
                    require(cache[3]<=address and address+size<=cache[3]+cache[5], 'Forge UVC CPU scratch outside cache SRAM: '+name)
                init=calls_to('app_task_handler','fm1_uvc_setup_init')
                require(len(init)==1 and all(off>init[0] for off in starts),'Forge UVC must initialize once before worker/USB attachment')
                steps=calls_to('fm1_forge_task','fm1_forge_capture_step')
                lcd=calls_to('fm1_forge_task','fm1_display_async_wire_rows')
                require(len(steps)==1 and len(lcd)==1 and steps[0]>lcd[0],'Forge UVC producer must follow foreground LCD packed-wire work')
                require(len(calls_to('fm1_peripheral_usb_audio_quiesce','fm1_uvc_capture_stop'))==1,
                    'Forge UVC STOP/UBOOT permanent teardown changed')
                require(len(calls_to('usb_device_mode','fm1_uvc_capture_stop'))==1,'Forge UVC USB device teardown changed')
                require(len(calls_to('filter','fm1_uvc_setup_filter'))==1,'Forge UVC setup sieve missing')
                # Filled only after reviewing the linked policy for each unchanged
                # compiler/profile; the legacy audio-only hash stays pinned below.
                # Normalize only verified relocatable calls; all whitelist/
                # stall operands remain pinned, including endpoint direction.
                policy=bytearray(code_at(value('filter'),symbols['filter'][1]))
                for target in ('fm1_uvc_setup_filter','usb_set_setup_phase'):
                    hits=calls_to('filter',target)
                    require(len(hits)==1 and policy[hits[0]]&0xc0==0x80 and policy[hits[0]+1]==0xea,
                        'Forge UVC setup policy call changed: '+target)
                    policy[hits[0]:hits[0]+4]=bytes(4)
                expected_policy='fd4085e3642bd61688785eae4c92842d384d8f9dbc6d4632d630b4fb449d2ebc' if usb_audio else '07dfa722bdbd843a14c017450425eef7a76fd7537b0e60c54ab38074e200e816'
                require(hashlib.sha256(policy).hexdigest()==expected_policy,'Forge UVC setup policy instructions changed')
            else:
                for name in ('fm1_uvc_state','fm1_uvc_dma','fm1_uvc_desc_config'):
                    require(name not in symbols,'Non-video factory links UVC '+name)
            if usb_audio:
                if usb_midi:
                    # Reviewed r4: vendor/other requests stall; full 16-bit
                    # interface indices allow 0..6, endpoint addresses only
                    # 0..3 and 0x80..0x84. The short stall call is checked
                    # independently before the remaining 72 bytes are hashed.
                    require(symbols['filter'][1]==74,'MIDI composite setup policy size changed')
                    policy=bytearray(code_at(value('filter'),74))
                    require(calls_to('filter','usb_set_setup_phase')==[0x2c] and policy[0x2c:0x2e]==bytes.fromhex('71 87'),'MIDI setup stall call changed')
                    policy[0x2c:0x2e]=bytes(2)
                    require(hashlib.sha256(policy).hexdigest()=='64efa042da63015bb3abcddaf1a50b96c4a34689703db833a89007747775ea79','MIDI composite USB setup policy instructions changed')
                elif not usb_video:require('filter' in symbols and symbols['filter'][1]==74 and hashlib.sha256(code_at(value('filter'),74)).hexdigest()=='8aefdb7c31f5e456688d6564c9d5cf5829ba370ab62d40375a21fa020ecd8078','Composite USB setup policy instructions changed')
                mix=calls_to('fm1_forge_audio_output','fm1_usb_audio_dac')
                require(len(mix)==1 and len(gain)==1 and mix[0]<gain[0],'Factory USB capture/mix must precede analog gain')
                if factory_user_module:
                    require('fm1_forge_user_module' in symbols and symbols['fm1_forge_user_module'][1]==28,'User module ABI layout changed')
                    require(struct.unpack('<I',code_at(value('fm1_forge_user_module'),4))[0]==1,'User module ABI must equal one')
                else:
                    raw=calls_to('fm1_forge_audio_output','fm1_forge_voice_render')
                    require(len(raw)==1 and raw[0]<mix[0],'Factory tone rendering must precede USB capture/mix')
                require(render_limit[0]<mix[0],'Factory render/clamp must precede USB capture/mix')
                stop=calls_to('fm1_usb_rx_irq','fm1_usb_audio_stop')
                require(len(stop)==len(quiet)==len(reboot)==1 and stop[0]<quiet[0]<reboot[0],'Confirmed factory UBOOT audio teardown changed')
                require(not calls_to('fm1_usb_boot_arm','fm1_usb_audio_stop') and not calls_to('fm1_usb_boot_arm','fm1_peripheral_usb_audio_quiesce'),'UBOOT arm must not prematurely stop factory audio')
            else:
                for name in ('fm1_uac_desc_config','fm1_usb_audio_dac','fm1_uac_descriptor'):
                    require(name not in symbols,'CDC-only factory links audio class '+name)
        if tracker_composite:
            # The tracker uses the same bounded packet/endpoint adapters as the
            # factory profile, with its own foreground producer and RAM closure.
            audit_usb_packet(symbols,code_at,composite=usb_audio,video=usb_video,midi=usb_midi)
            for name in ('tracker_session','tracker_history','block','fm1_peripheral_usb_audio_quiesce'):
                require(name in symbols,'Missing composite tracker component '+name)
            require('fm1_tracker_lcd_submit' in symbols and 'fm1_lcd_frames' in symbols,
                    'Composite tracker requires the reviewed async LCD copy/DMA boundary')
            require(symbols['fm1_tracker_lcd_submit'][1]==248 and
                    symbols.get('lcd_async',(0,0,0))[1]==72 and
                    value('lcd_async')==value('_sdfile_handl')+252,
                    'Tracker LCD copy/conversion state layout changed')
            submission=bytearray(code_at(value('fm1_tracker_lcd_submit'),248))
            for off,name,opcode in ((4,'_sdfile_handl',b'\xc3\xff'),
                                    (58,'fm1_lcd_frames',b'\xc1\xff'),
                                    (66,'row_pixels',b'\xc1\xff'),
                                    (90,'ota_status',b'\xc5\xff'),
                                    (202,'snapshot.addresses',b'\xc0\xff')):
                require(submission[off:off+2]==opcode and
                        struct.unpack_from('<I',submission,off+2)[0]==value(name),
                        'Tracker LCD copy/conversion pointer changed: '+name)
                submission[off+2:off+6]=bytes(4)
            for off,name in ((76,'fm1_lcd_pack_rgb444'),(82,'async_publish_rows'),
                              (104,'bit_clr_ie'),(192,'unrequest_irq')):
                require(calls_to('fm1_tracker_lcd_submit',name)==[off] and
                        call_target(submission[off:off+4],value('fm1_tracker_lcd_submit')+off)==value(name),
                        'Tracker LCD copy/conversion call changed: '+name)
                submission[off:off+4]=bytes(4)
            require(hashlib.sha256(submission).hexdigest()==
                    '71c2d3c58b2e1ccd113764804a34999abf31c896fa55c581e600dc20ead29e01',
                    'Tracker LCD copy/conversion instructions changed')
            for name in ('fm1_forge_task','fm1_forge_video_capture','fm1_forge_audio_output'):
                require(name not in symbols,'Composite tracker links factory owner '+name)
            scratch={'block':512,'view':1262,'row_pixels':1440}
            if usb_audio:scratch['bridge']=8288
            if usb_video:scratch.update(tracker_capture=2288,fm1_uvc_state=2068,fm1_uvc_control_state=64)
            for name,expected_size in scratch.items():
                require(name in symbols,'Missing tracker CPU scratch '+name)
                address,size,_=symbols[name];cache=sections['.cache_ram_bss']
                require(size==expected_size and address%4==0 and cache[3]<=address and
                        address+size<=cache[3]+cache[5],
                        'Tracker CPU scratch size/cache placement changed: '+name)
            quiet=calls_to('fm1_usb_rx_irq','fm1_peripheral_usb_audio_quiesce')
            reboot=calls_to('fm1_usb_rx_irq','go_mask_usb_updata')
            require(len(quiet)==len(reboot)==1 and quiet[0]<reboot[0],
                    'Confirmed tracker UBOOT DMA teardown changed')
            require(not calls_to('fm1_usb_boot_arm','fm1_peripheral_usb_audio_quiesce'),
                    'UBOOT arm must not prematurely stop tracker peripherals')
            if usb_video:
                for name in ('fm1_uvc_setup_init','fm1_uvc_desc_config','fm1_uvc_setup_filter',
                             'fm1_uvc_capture_stop','fm1_uvc_get_status','fm1_uvc_video_descriptor',
                             'fm1_uvc_dma','fm1_tracker_capture_step','fm1_tracker_capture_packet'):
                    require(name in symbols,'Missing tracker UVC component '+name)
                for name in ('uvc_register','uvc_open','uvc_alloc','uvc_malloc'):
                    require(name not in symbols,'Tracker UVC must not link stock camera allocation '+name)
                vc=5 if usb_audio else 2;vs=vc+1
                def le16(n):return struct.pack('<H',n)
                def le32(n):return struct.pack('<I',n)
                expected_video=(bytes((8,11,vc,2,14,3,0,0,9,4,vc,0,0,14,1,0,0,13,36,1))+
                    le16(0x100)+le16(40)+le32(1000)+bytes((1,vs,18,36,2,1))+le16(0x201)+
                    bytes((0,0))+le16(0)*3+bytes((3,0,0,0,9,36,3,2))+le16(0x101)+
                    bytes((0,1,0,9,4,vs,0,0,14,2,0,0,14,36,1,1))+le16(77)+
                    bytes((0x84,0,2,0,0,0,1,0,27,36,4,1,1))+bytes.fromhex('5955593200001000800000aa00389b71')+
                    bytes((16,1,0,0,0,0,30,36,5,1,0))+le16(256)+le16(240)+
                    le32(4915200)*2+le32(122880)+le32(2000000)+bytes((1,))+le32(2000000)+
                    bytes((6,36,13,4,4,4,9,4,vs,1,1,14,2,0,0,7,5,0x84,1))+le16(642)+bytes((1,)))
                require(len(expected_video)==159 and symbols['fm1_uvc_video_descriptor'][1]==159 and
                        code_at(value('fm1_uvc_video_descriptor'),159)==expected_video,
                        'Tracker UVC descriptor/numbering/mode changed')
                dma,size,_=symbols['fm1_uvc_dma']
                require(size==1024 and dma%64==0 and bss[3]<=dma and dma+size<=bss[3]+bss[5],
                        'Tracker UVC DMA size/alignment/internal-RAM placement changed')
                init=calls_to('app_task_handler','fm1_uvc_setup_init')
                starts=calls_to('app_task_handler','task_create')
                require(len(init)==1 and starts and all(off>init[0] for off in starts),
                        'Tracker UVC must initialize once before worker/USB attachment')
                steps=calls_to('fm1_peripheral_task','fm1_tracker_capture_step')
                lcd=calls_to('fm1_peripheral_task','fm1_tracker_lcd_submit')
                require(len(steps)==len(lcd)==1 and steps[0]>lcd[0],
                        'Tracker UVC producer must follow foreground LCD RGB565 copy/conversion')
                require(len(calls_to('fm1_peripheral_usb_audio_quiesce','fm1_uvc_capture_stop'))==1,
                        'Tracker UVC confirmed UBOOT permanent teardown changed')
                require(len(calls_to('usb_device_mode','fm1_uvc_capture_stop'))==1,
                        'Tracker UVC USB device teardown changed')
                policy=bytearray(code_at(value('filter'),symbols['filter'][1]))
                for target in ('fm1_uvc_setup_filter','usb_set_setup_phase'):
                    hits=calls_to('filter',target)
                    require(len(hits)==1 and policy[hits[0]]&0xc0==0x80 and policy[hits[0]+1]==0xea,
                            'Tracker UVC setup policy call changed: '+target)
                    policy[hits[0]:hits[0]+4]=bytes(4)
                expected_policy='fd4085e3642bd61688785eae4c92842d384d8f9dbc6d4632d630b4fb449d2ebc' if usb_audio else '07dfa722bdbd843a14c017450425eef7a76fd7537b0e60c54ab38074e200e816'
                require(hashlib.sha256(policy).hexdigest()==expected_policy,
                        'Tracker UVC setup policy instructions changed')
            else:
                for name in ('fm1_uvc_state','fm1_uvc_dma','fm1_uvc_desc_config','tracker_capture'):
                    require(name not in symbols,'Non-video tracker links UVC '+name)
            if usb_audio:
                for name in ('audio_output','fm1_tracker_speaker_fold','fm1_tracker_analog_gain'):
                    require(name in symbols,'Missing tracker audio pipeline '+name)
                mix=calls_to('audio_output','fm1_usb_audio_dac')
                fold=calls_to('audio_output','fm1_tracker_speaker_fold')
                gain=calls_to('audio_output','fm1_tracker_analog_gain')
                require(len(mix)==len(fold)==len(gain)==1 and mix[0]<fold[0]<gain[0],
                        'Tracker USB stereo capture must precede speaker fold and analog gain')
                require(symbols['fm1_tracker_speaker_fold'][1]==64 and
                        hashlib.sha256(code_at(value('fm1_tracker_speaker_fold'),64)).hexdigest()==
                        '5f7d1de08604d44f70d22c18af525340664289a369ff329aa8e3f8357ca79b6b',
                        'Tracker bounded 64-bit speaker average instructions changed')
                # Pin the existing 64-frame startup mute/ramp and all 128
                # signed Q7 sample operations. Normalize only three pointers
                # after checking their exact split-envelope symbol ownership.
                require(symbols['fm1_tracker_analog_gain'][1]==116,
                        'Tracker analog startup gain size changed')
                analog=bytearray(code_at(value('fm1_tracker_analog_gain'),116))
                for name,size in (('envelope.0',4),('envelope.1',1),('envelope.2',1)):
                    require(name in symbols and symbols[name][1]==size and
                            bss[3]<=value(name) and value(name)+size<=bss[3]+bss[5],
                            'Tracker analog startup state layout changed: '+name)
                require(value('envelope.0')==value('ota_status')+336,
                        'Tracker analog startup mute state offset changed')
                for off,name,opcode in ((2,'ota_status',b'\xc3\xff'),
                                        (12,'envelope.1',b'\xc1\xff'),
                                        (38,'envelope.2',b'\xc2\xff')):
                    require(analog[off:off+2]==opcode and
                            struct.unpack_from('<I',analog,off+2)[0]==value(name),
                            'Tracker analog startup state pointer changed: '+name)
                    analog[off+2:off+6]=bytes(4)
                require(hashlib.sha256(analog).hexdigest()==
                        'fafed90b01353580f1d0b84a2c818c850d672da2a8cf6afbd61afbbe4a938c58',
                        'Tracker analog startup gain instructions changed')
                stop=calls_to('fm1_usb_rx_irq','fm1_usb_audio_stop')
                require(len(stop)==1 and stop[0]<quiet[0]<reboot[0],
                        'Confirmed tracker UBOOT audio teardown changed')
                require(not calls_to('fm1_usb_boot_arm','fm1_usb_audio_stop'),
                        'UBOOT arm must not prematurely stop tracker USB audio')
                if usb_midi:
                    require(symbols['filter'][1]==74,'Tracker MIDI composite setup policy size changed')
                    policy=bytearray(code_at(value('filter'),74))
                    require(calls_to('filter','usb_set_setup_phase')==[0x2c] and policy[0x2c:0x2e]==bytes.fromhex('71 87'),
                            'Tracker MIDI setup stall call changed')
                    policy[0x2c:0x2e]=bytes(2)
                    require(hashlib.sha256(policy).hexdigest()=='64efa042da63015bb3abcddaf1a50b96c4a34689703db833a89007747775ea79',
                            'Tracker MIDI composite setup policy instructions changed')
                elif not usb_video:
                    require(symbols['filter'][1]==74 and hashlib.sha256(code_at(value('filter'),74)).hexdigest()==
                            '8aefdb7c31f5e456688d6564c9d5cf5829ba370ab62d40375a21fa020ecd8078',
                            'Tracker composite USB setup policy instructions changed')
            else:
                for name in ('fm1_uac_desc_config','fm1_usb_audio_dac','fm1_uac_descriptor','bridge'):
                    require(name not in symbols,'Video-only tracker links audio class '+name)
        require(code_at(value('fm1_usb_device_descriptor'),18)==descriptor,
                'USB CDC device descriptor changed')
        require(code_at(value('fm1_usb_config_descriptor'),9)==bytes((9,2,0,0,0,1,0,0x80,50)),
                'USB configuration descriptor changed')
        dma,dma_size,_=symbols['fm1_usb_dma']
        require(dma%64==0 and dma_size==1024 and bss[3]<=dma and dma+dma_size<=bss[3]+bss[5],
                'USB DMA alignment/size/placement changed')
        require('usb0_g_isr' in symbols and 'usb1_g_isr' not in symbols,
                'Only the reviewed USB0 candidate is currently audited')
        irq,irq_size,_=symbols['usb0_g_isr']
        require(code_at(irq+irq_size-2,2)==b'\x81\x00','USB ISR lacks interrupt return')
        boot_enabled='go_mask_usb_updata' in symbols
        if boot_enabled:
            boot=value('go_mask_usb_updata');nvram=value('nvram_set_boot_state')
            require(symbols['go_mask_usb_updata'][1]==40 and
                    sections['.ram0_data'][3]<=boot<sections['.ram0_data'][3]+sections['.ram0_data'][5],
                    'UBOOT entry not in RAM or changed size')
            expected=bytearray.fromhex('00 00 00 00 c0 ff 00 00 00 00 64 e0 00 16 d8 ec 0a 21 2b 81 d8 ec 0b 31 82 41 41 00 20 00 40 22 80 ff 00 00 00 00 f7 9f')
            require(call_target(code_at(boot,4),boot)==value('__local_irq_disable'),'UBOOT IRQ disable changed')
            expected[:4]=code_at(boot,4)
            struct.pack_into('<I',expected,6,value('cpu_lock_cnt'))
            struct.pack_into('<i',expected,34,nvram-(boot+38))
            require(code_at(boot,40)==expected,'UBOOT entry instructions changed')
            require(symbols['nvram_set_boot_state'][1]==36,'UBOOT retained marker writer size changed')
            marker=struct.unpack('<I',code_at(nvram+8,4))[0]
            require(code_at(marker,16)==b'usb_update_mode\0','UBOOT retained marker changed')
            expected=bytearray.fromhex('c0 ff 80 fd c7 01 c1 ff 00 00 00 00 42 30 80 ff 00 00 00 00 68 20 41 24 42 21 43 21 80 ff 00 00 00 00 f7 9f')
            struct.pack_into('<I',expected,8,marker)
            struct.pack_into('<i',expected,16,value('memmove')-(nvram+20))
            struct.pack_into('<i',expected,30,value('P33_CON_SET')-(nvram+34))
            require(code_at(nvram,36)==expected,'UBOOT retained marker/reset instructions changed')
            callback=value('cdc_wakeup_handler')
            require(symbols['cdc_wakeup_handler'][1]==8 and code_at(callback,2)==b'\x10\x04' and
                    code_at(callback+6,2)==b'\x00\x04' and
                    call_target(code_at(callback+2,4),callback+2)==value('fm1_usb_rx_irq'),'UBOOT direct RX interrupt path changed')
            irq_entry,irq_length,_=symbols['fm1_usb_rx_irq']
            require(long_call_target(irq_entry+irq_length-8)==boot and code_at(irq_entry+irq_length-2,2)==b'\xf7\x9f',
                    'UBOOT entry no longer dispatched from RX IRQ')
        if usb_peripheral_tests:
            require(boot_enabled,'Peripheral diagnostic must preserve UBOOT')
            alink_name='fm1_forge_alink_isr' if factory_diag else 'fm1_test_alink_isr'
            for name in ('fm1_forge_task' if factory_diag else 'fm1_peripheral_task','fm1_display_test_init','fm1_wl82_keyscan_async_raw' if (usb_mdx or usb_mod or factory_diag) else 'fm1_wl82_keyscan_raw','iis_open','iis_irq_handler',alink_name):
                require(name in symbols,'Missing peripheral test '+name)
            address,size,_=symbols[alink_name]
            require(code_at(address+size-2,2)==b'\x81\x00','Test ALINK ISR lacks interrupt return')
            if factory_diag:
                address,size,_=symbols['fm1_forge_keyscan_isr']
                require(code_at(address+size-2,2)==b'\x81\x00','Factory scanner lacks interrupt return')
                require('fm1_display_async_wire_rows' in symbols and
                        len(calls_to('fm1_forge_task','fm1_display_async_wire_rows'))==1,
                        'Factory LCD must copy the exact packed-wire row span')
                require('fm1_display_async_rows' not in symbols or
                        not calls_to('fm1_forge_task','fm1_display_async_rows'),
                        'Factory LCD must not reinterpret wire rows as RGB565')
            if usb_mdx:
                for name in ('fm1_mdx_load','fm1_mdx_render','fm1_mdx_usb_command','fm1_mdx_keyscan_isr','fm1_demo_mdx','fm1_demo_pdx','upload','player'):
                    require(name in symbols,'Missing MDX integration '+name)
                address,size,_=symbols['fm1_mdx_keyscan_isr']
                require(code_at(address+size-2,2)==b'\x81\x00','MDX scanner lacks interrupt return')
                for name in ('fm1_demo_mdx','fm1_demo_pdx'):
                    address,size,index=symbols[name]
                    owner=next(s for s in sections.values() if s[3]<=address and address+size<=s[3]+s[5])
                    require(owner[1]!=8 and not (owner[2]&1),'Demo must be read-only flash')
            if usb_mod:
                for name in ('fm1_mod_open','fm1_mod_render','fm1_mod_usb_command','fm1_mod_keyscan_isr','song'):
                    require(name in symbols,'Missing MOD editor '+name)
                if 'tracker_session' in symbols:
                    require('tracker_history' in symbols,'Task controller lacks edit history')
                    address,size,_=symbols['tracker_history'];cache=sections['.cache_ram_bss']
                    require(size==10320 and cache[3]<=address and
                            address+size<=cache[3]+cache[5],
                            'Tracker edit history must fit reviewed CPU-only cache SRAM')
                    address,size,_=symbols['tracker_session']
                    require(0<size<=3072 and bss[3]<=address and address+size<=bss[3]+bss[5],
                            'Tracker controller exceeds reviewed internal-RAM budget')
                else:require('player' in symbols,'Missing legacy MOD player state')
                require(any(n=='transfer' or n.startswith('transfer.') for n in symbols),'Missing MOD transfer state')
                address,size,_=symbols['fm1_mod_keyscan_isr']
                require(code_at(address+size-2,2)==b'\x81\x00','MOD scanner lacks interrupt return')
            if 'fm1_nes_keyscan_isr' in symbols:
                address,size,_=symbols['fm1_nes_keyscan_isr']
                require(code_at(address+size-2,2)==b'\x81\x00','Keyscan ISR lacks interrupt return')
            require(code_at(value('fm1_encoder_contacts'),14)==bytes.fromhex('10 20 30 40 91 a1 90 a0 70 80 50 60 15 25'),
                    'Stock encoder contact mapping changed')
            if 'fm1_key_dma' in symbols:
                address,size,_=symbols['fm1_key_dma']
                require(address%64==0 and size==64 and bss[3]<=address and address+size<=bss[3]+bss[5],
                        'Key scanner DMA must occupy aligned internal RAM BSS')
            if 'fm1_lcd_dma_params' in symbols:
                address,size,_=symbols['fm1_lcd_dma_params']
                require(size==64 and address%64==0 and bss[3]<=address and address+size<=bss[3]+bss[5],
                        'LCD DMA parameters must occupy aligned internal RAM BSS')
                require('fm1_display_snapshot' in symbols,'LCD DMA variant lacks fixed-register diagnostics')
            if 'fm1_lcd_dma_pixels' in symbols:
                require(usb_nes or usb_mdx or usb_mod or factory_diag,'Large LCD DMA buffer only allowed in explicit peripheral profile')
                address,size,_=symbols['fm1_lcd_dma_pixels']
                require(size==3840 and address%64==0 and bss[3]<=address and address+size<=bss[3]+bss[5],
                        'LCD pixel DMA must occupy aligned internal RAM BSS')
            if 'fm1_lcd_frames' in symbols:
                require(usb_nes or usb_mod or factory_diag,'Async LCD only allowed in explicit NES, MOD or factory profile')
                address,size,_=symbols['fm1_lcd_frames']
                frame_bytes=(86400 if usb_mod else 172800) if ('fm1_lcd_pack_rgb444' in symbols or 'fm1_lcd_pack_native_rgb444' in symbols) else (115200 if usb_mod else 230400)
                require(size==frame_bytes and address%64==0 and bss[3]<=address and address+size<=bss[3]+bss[5],
                        'Async LCD frames must occupy aligned internal RAM BSS')
                address,size,_=symbols['fm1_lcd_spi1_isr']
                require(code_at(address+size-2,2)==b'\x81\x00','LCD SPI1 ISR lacks interrupt return')
                require(value('HEAP_END')-value('HEAP_BEGIN')>=128*1024,'Async LCD needs 128 KiB heap reserve')
        for name in ('dual_bank_passive_update_init','dual_bank_update_write',
                     'dual_bank_update_burn_boot_info','fm1_nes_task'):
            require(name not in symbols,'USB diagnostic unexpectedly exposes updater/component '+name)
    elif not display_only:require(symbols['machine'][1]==69240,'Unexpected NES state layout')
    else:
        for name in ('machine','cartridge','fm1_nes_run','fm1_wl82_keyscan_start','iis_open',
                     'fm1_alink_isr','iis_channel_on','fm1_wl82_run_with_stats'):
            require(name not in symbols,'Display-only image contains forbidden component '+name)
        for name in ('fm1_display_test_init','fm1_display_test_frame','fm1_display_stage'):
            require(name in symbols,'Missing display diagnostic '+name)
    require(value('_ram0_data_lma')==XIP+len(data(sections['.text'])),'Wrong startup RAM copy source')
    require(value('_ram0_data_vma')==sections['.ram0_data'][3],'Wrong startup RAM destination')
    require(value('_ram0_data_size')==sections['.ram0_data'][5],'Wrong startup RAM copy length')
    require(value('_ram0_bss_vma')==sections['.ram0_bss'][3] and
            value('_ram0_bss_size')==sections['.ram0_bss'][5],'Wrong startup BSS clearing range')
    require(value('cache_ram_data_lma')==XIP+len(packed)-sections['.cache_ram_data'][5],'Wrong cache RAM copy source')
    require(sections['.ram0_bss'][3]+sections['.ram0_bss'][5]<=value('HEAP_BEGIN')<value('HEAP_END')<RAM_LIMIT,'Bad heap boundary')
    if not display_only and (not usb_only or usb_nes):
        require('cartridge' in symbols,'Missing embedded cartridge')
        addr,size,_=symbols['cartridge'];start=addr-XIP
        require(size==rom_size and 0<=start and start+size<=sections['.text'][5],'Cartridge is not wholly in XIP')
        require(hashlib.sha256(data(sections['.text'])[start:start+size]).hexdigest()==rom_sha,'Embedded cartridge hash mismatch')
    power_report=audit_power(symbols,sections,code_at,required=require_board_power,usb_only=usb_only,usb_audio=usb_audio,factory_diag=factory_diag,usb_midi=usb_midi)
    pre_os_report=audit_pre_os(symbols,code_at)
    require(not require_screen_first or power_report.get('screen_first',False),'Missing screen-first board hook')
    return {'static_audit':'passed','entry':hex(entry),'application_bytes':len(app),
            'board_power':power_report,
            'pre_os_display':pre_os_report,
            'application_sha256':hashlib.sha256(app).hexdigest(),
            'sections':{name:{'address':hex(s[3]),'bytes':s[5]} for name,s in sections.items() if name in allowed},
            'heap_bytes':value('HEAP_END')-value('HEAP_BEGIN'),
            'startup_instructions':{'bytes_verified':len(startup_code),'entry_branch_verified':True,
                'sp':hex(value('_cpu0_ustack')),'ssp':hex(value('_cpu0_sstack')),
                'ram_source':hex(value('_ram0_data_lma')),'ram_destination':hex(value('_ram0_data_vma')),
                'ram_bytes':value('_ram0_data_size'),'bss_start':hex(value('_ram0_bss_vma')),
                'bss_bytes':value('_ram0_bss_size'),'cache_copy_operands_verified':True,
                'tracker_cache_bss_clear_verified':bool(tracker_composite),
                'main':hex(value('main'))},
            'early_boot_trace':{'enabled':traced,'address':hex(value('fm1_boot_trace')) if traced else None,
                'live_debugger_required':True,'survives_reset':False},
            'boot_compatibility':{'bridge':hex(bridge),'startup':hex(startup),
                'sdk_initializer':hex(value('boot_info_init')),
                'source_prefix_bytes':24,'normalized_bytes':92,
                'unused_extensions_zeroed':True,'machine_code_verified':True},
            'configuration_registry':{'address':hex(cfg_start),'bytes':28,
                'initializer':'syscfg_file_init','write_callbacks':False,
                'vm_btif_auto_repair_registered':False,
                'integrity_init_slot':hex(value('__initcall_sdk_meky_check')),
                'low_level_flash_write_code_still_linked':True},
            'sdk_initcall_inventory':init_report,
            'sdk_delayed_integrity':{'initializer':hex(integrity),
                'timer_wrapper':hex(timeout),'callback':hex(value('_mkey_check')),
                'scheduled_delay_ms':8000,'runtime_result_verified':False},
            ('usb_task' if usb_only else 'nes_task'):{'entry':hex(value(worker)),'table':hex(task_table),
                'priority':10,'stack_words':2048 if usb_only else 4096,'queue_words':0,
                'app_core_priority':15,'physical_dispatch_verified':False},
            'debug_mailboxes':{n:hex(value(n)) for n in runtime_states if n!='machine'},
            'peripherals_required':require_peripherals,
            'irq_wrapper_bytes':len(code) if require_peripherals else 0,
            'integrated_symbols':{n:hex(value(n)) for n in peripheral_symbols} if require_peripherals else {},
            'display_only':display_only,
            'usb_only':usb_only,
            'usb_bootloader_entry':usb_only and 'go_mask_usb_updata' in symbols,
            'usb_peripheral_tests':usb_peripheral_tests,
            'usb_nes':usb_nes,'usb_mod':usb_mod,'factory_diag':factory_diag,'factory_user_module':factory_user_module,
            'usb_midi_update':usb_midi,'usb_midi_installation_enabled':False,
            'tracker_cdc_identity':tracker_identity,
            'factory_clock':factory_clock if factory_diag else None,'factory_hal_access':factory_hal_access if factory_diag else None,'usb_audio':usb_audio,'usb_video':usb_video,
            'usb_video_memory':{n:{'address':hex(value(n)),'bytes':symbols[n][1]} for n in ('fm1_uvc_state','fm1_uvc_control_state','tracker_capture' if usb_mod else 'fm1_forge_video_capture','fm1_uvc_dma')} if usb_video else None,
            'cartridge':None if display_only or (usb_only and not usb_nes) else rom,'embedded_rom_sha256':None if display_only or (usb_only and not usb_nes) else rom_sha,'hardware_boot_verified':False,'flashable':False}


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('elf',type=Path);ap.add_argument('application',type=Path)
    ap.add_argument('--peripherals',action='store_true')
    ap.add_argument('--rom',choices=tuple(ROMS),default='rom50')
    args=ap.parse_args()
    print(json.dumps(audit(args.elf.read_bytes(),args.application.read_bytes(),args.peripherals,args.rom),indent=2))
