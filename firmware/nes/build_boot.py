"""Offline-only SDK boot link. NEVER invokes make, download scripts or USB tools.

Produces headless or peripheral FM-1 candidates, not hardware-qualified releases.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from audit_boot import audit
from verify_stock_power import STOCK,verify as verify_stock_power

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SDK=ROOT/'references/source/fw-AC79_AIoT_SDK'
SDK_PIN='e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d'
TC=Path('C:/JL/pi32/bin')
MAKE=SDK/'apps/demo/demo_hello/board/wl82/Makefile'


def run(args, **kwargs):
    print(' '.join(map(str,args)),flush=True)
    return subprocess.run(list(map(str,args)),check=True,**kwargs)


def make_list(text,name):
    match=re.search(r'^'+re.escape(name)+r'\s*:=\s*\\\n((?:.*\\\n)*)',text,re.M)
    if not match: raise ValueError('Missing SDK make list: '+name)
    return match.group(1).replace('\\\n',' ').split()


def sdk_path(value):
    return (MAKE.parent/value).resolve()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',choices=('headless','peripherals','display','early-display','screen-first'),default='headless')
    parser.add_argument('--rom',choices=('rom50','smb1'),default='smb1')
    parser.add_argument('--out',type=Path,help='Separate offline build folder; preserves previous bench artifacts')
    parser.add_argument('--cpu1-divider-guard',action='store_true',help='Experimental stock SYS_DIV guard inside vendor CPU1 launch')
    parser.add_argument('--pre-os-display',action='store_true',help='Diagnostic power/display checkpoint before os_init')
    args=parser.parse_args()
    if args.cpu1_divider_guard and (args.profile!='screen-first' or args.out is None):
        parser.error('CPU1 guard requires screen-first and an explicit separate output folder')
    if args.pre_os_display and (args.profile!='screen-first' or args.out is None or not args.cpu1_divider_guard):
        parser.error('Pre-OS diagnostic requires screen-first, the retained CPU1 guard and a separate output folder')
    if args.profile=='early-display':
        parser.error('T2 early-display is paused for stock execution audit; validation is not implemented. No build or flash performed.')
    peripheral=args.profile in ('peripherals','screen-first')
    display=args.profile in ('display','early-display')
    early=args.profile in ('early-display','screen-first')
    # Keep the separately built headless candidate intact when integrating I/O.
    OUT=args.out.resolve() if args.out else HERE/(('build/boot-screen-first-' if args.profile=='screen-first' else ('build/boot-early-display-' if early else ('build/boot-display-' if display else ('build/boot-peripherals-' if peripheral else 'build/boot-'))))+args.rom)
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=OUT/'build-manifest.json'
    manifest.write_text(json.dumps({'status':'build_in_progress_or_failed','flashable':False})+'\n')
    if run(['git','-C',SDK,'rev-parse','HEAD'],capture_output=True,text=True).stdout.strip()!=SDK_PIN:
        raise SystemExit('Unreviewed SDK revision')
    if run(['git','-C',SDK,'status','--porcelain'],capture_output=True,text=True).stdout.strip():
        raise SystemExit('SDK has local changes')
    stock_power_evidence=verify_stock_power(STOCK.read_bytes())
    run([sys.executable,HERE/'tests/test_stock_power.py','-v'])
    run([sys.executable,HERE/'build.py','all','--'+args.rom])
    text=MAKE.read_text(encoding='utf-8')
    flags=make_list(text,'CFLAGS')
    defines=make_list(text,'DEFINES')
    if peripheral:defines+=['-DFM1_BOOT_PERIPHERALS=1']
    if display:defines+=['-DFM1_BOOT_DISPLAY_TEST=1']
    if early:defines+=['-DFM1_BOOT_EARLY_DISPLAY=1']
    includes=['-I'+str(HERE/'boot'),'-I'+str(HERE/'include')]
    includes+=['-I'+str(sdk_path(x[2:])) for x in make_list(text,'INCLUDES')]
    sources=[sdk_path(x) for name in ('c_SRC_FILES','S_SRC_FILES') for x in make_list(text,name)]
    sources=[s for s in sources if s.name not in ('app_main.c','board.c','cpp_run_init.c')]
    sources += [HERE/'boot/board.c',HERE/'boot/app_main.c',HERE/'boot/boot_compat.c',HERE/'boot/boot_trace.c',HERE/'boot/board_power.c']
    if peripheral:sources += [HERE/'boot/wl82_services.c']
    if display or early:sources += [HERE/'boot/display_test.c']
    if args.pre_os_display:sources += [HERE/'boot/pre_os_display.c']
    if args.pre_os_display:
        # LTO resolves main's os_init call before --wrap can redirect it.
        # Use an exact, isolated build copy; never edit the pinned SDK source.
        original_init=SDK/'apps/common/system/init.c'
        init_text=original_init.read_text(encoding='utf-8')
        if init_text.count('    os_init();')!=1:
            raise ValueError('Unexpected SDK main OS initialization call count')
        copied_init=OUT/'init-pre-os.c'
        copied_init.write_text('extern void __wrap_os_init(void);\n'+
            init_text.replace('    os_init();','    __wrap_os_init();'),encoding='utf-8')
        sources[sources.index(original_init)]=copied_init
    for source in sources:
        if not source.is_file(): raise SystemExit('Missing SDK source (check sparse checkout): '+str(source))
    # Preprocess linker metadata ONLY; never generate/run the SDK downloader.
    for source,name in ((SDK/'cpu/wl82/sdk_ld.c','sdk.ld'),(SDK/'cpu/wl82/sdk_used_list.c','sdk.used')):
        run([TC/'clang.exe',*flags,*defines,*includes,'-D__LD__','-E','-P',source,'-o',OUT/name])
    # memory_init is weak in the SDK bitcode. Preserve it through LTO so
    # __real_memory_init resolves to the original, not an internalized symbol.
    used=OUT/'sdk.used'
    used.write_text(used.read_text()+'\nmemory_init\n')
    if args.pre_os_display:used.write_text(used.read_text()+'\nos_init\n')
    ld=(OUT/'sdk.ld').read_text()
    # The SDK's non-C++ script only collects unsuffixed data/bss; the port uses
    # -fdata-sections. Collect these explicitly, never leave RAM as ROM orphans.
    for old,new in (('*(.data)','*(.data .data.*)'),('*(.bss)','*(.bss .bss.*)')):
        if ld.count(old)!=1: raise ValueError('SDK linker template changed: '+old)
        ld=ld.replace(old,new)
    # NES mapper 0/3 build has no persistent save/config or Bluetooth service.
    # Vendor cfg_vm/cfg_btif init can repair/erase stock data before app_main.
    # Omit those two registration records, not the SDK integrity initcall or
    # read-only cfg_bin reader. Fail closed if the pinned linker layout changes.
    for section in ('.syscfg.2.ops','.syscfg.1.ops'):
        selector='*('+section+')'
        if ld.count(selector)!=1:raise ValueError('SDK configuration registry changed: '+section)
        ld=ld.replace(selector,'/* FM-1: no persistent configuration registration */')
    ld+='\nSECTIONS { /DISCARD/ : { *(.syscfg.2.ops) *(.syscfg.1.ops) } }\n'
    (OUT/'sdk.ld').write_text(ld)
    objects=[]
    for index,source in enumerate(sources):
        obj=OUT/(f'{index:02d}-'+source.name+'.o');objects.append(obj)
        run([TC/'clang.exe',*flags,*defines,*includes,'-c',source,'-o',obj])
    libs=[SDK/'include_lib/newlib/pi32v2-lib'/name for name in ('libm.a','libc.a','libcompiler_rt.a')]
    libs += [SDK/'cpu/wl82/liba'/name for name in ('cpu.a','event.a','system.a','cfg_tool.a','fs.a','common_lib.a','update.a')]
    if args.cpu1_divider_guard:
        from cpu1_guard import build_archive
        system=SDK/'cpu/wl82/liba/system.a'
        libs[libs.index(system)]=build_archive(system,OUT/'cpu1-guard',TC)
    archive=HERE/('build/pi32v2-'+args.rom)/'libfm1_nes.a'
    elf=OUT/'fm1-nes-boot-candidate.elf'
    run([TC/'pi32v2-lto-wrapper.exe','-o',elf,*objects,'--start-group',archive,*libs,'--end-group',
         '-T'+str(OUT/'sdk.ld'),'-M='+str(OUT/'fm1-nes-boot-candidate.map'),
         '--wrap=boot_info_init',
         '--wrap=memory_init',
         '--undefined=memory_init',
         *(['--wrap=os_init','--undefined=os_init'] if args.pre_os_display else []),
         '--plugin-opt=mcpu=r3','--plugin-opt=-mattr=+fprev1',
         '--plugin-opt=-pi32v2-large-program=true',
         '--plugin-opt=-used-symbol-file='+str(OUT/'sdk.used')],cwd=OUT)
    # The vendor format concatenates these sections, not an ELF-wide objcopy
    # (RAM and XIP VMAs are far apart). No UBOOT/reserved sector is included.
    parts=[]
    for section in ('.text','.data','.dynamic_data','.ram0_data','.cache_ram_data'):
        path=OUT/(section[1:]+'.bin')
        run([TC/'llvm-objcopy.exe','-O','binary','-j',section,elf,path])
        parts.append(path.read_bytes())
    app=OUT/'fm1-nes-boot-candidate.app.bin'
    app.write_bytes(b''.join(parts))
    symbols=run([TC/'llvm-nm.exe','-n','-S',elf],capture_output=True,text=True).stdout
    (OUT/'symbols.txt').write_text(symbols)
    disasm=run([TC/'llvm-objdump.exe','-d','-mcpu=r3',elf],capture_output=True,text=True).stdout
    (OUT/'disassembly.asm').write_text(disasm)
    audit_result=audit(elf.read_bytes(),app.read_bytes(),require_peripherals=peripheral,rom=args.rom,display_only=display,require_boot_trace=True,require_board_power=True,require_screen_first=early)
    if args.pre_os_display != audit_result['pre_os_display']['enabled']:
        raise ValueError('Pre-OS build selection and linked checkpoint disagree')
    if args.cpu1_divider_guard:
        from cpu1_guard import audit_application
        symbol_values={}
        for line in symbols.splitlines():
            fields=line.split()
            if len(fields)==4:
                symbol_values[fields[3]]=int(fields[0],16)
        audit_result['cpu1_divider_guard']=audit_application(app.read_bytes(),
            address=symbol_values['os_start']+0xea,
            cpu1_start=symbol_values['cpu1_start'],ready=symbol_values['cpu1_run_flag'])
    (OUT/'static-audit.json').write_text(json.dumps(audit_result,indent=2)+'\n')
    run([sys.executable,HERE/'tests/test_boot_audit.py','--folder',OUT,
         '--rom',args.rom,
         *(['--peripherals'] if peripheral else []),*(['--display'] if display else []),'-v'])
    report={'status':'linked_'+args.profile+'_boot_candidate_UNTESTED','flashable':False,
            'cpu1_divider_guard':args.cpu1_divider_guard,
            'pre_os_display':args.pre_os_display,
            'sdk_commit':SDK_PIN,'profile':args.profile,'cartridge':args.rom,
            'behavior':(('Pre-OS' if args.pre_os_display else 'Board-stage')+' RGB/yellow strip independent of RTOS delays, then blue LCD checkpoint and unbounded '+args.rom+' NES with SPI1/ALINK0/SPI2') if early else (('T1 SPI1-only RGB bands, blinking top, white bottom after 20 s; no cartridge/core/audio/scanner') if display else (('unbounded '+args.rom+' run; SPI1 LCD, ALINK0 audio, SPI2 keys') if peripheral else ('600-frame headless '+args.rom+' run; minimum 20 s observation window; idle heartbeat'))),
            'audio_mute_policy':'digital-only; physical mute/transients unqualified' if peripheral else 'no audio',
            'panel_origin':{'x':0,'y':40,'source':'stock setup table; not bench-confirmed'} if peripheral else None,
            'elf':str(elf),'application':str(app),'application_bytes':app.stat().st_size,
            'application_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),
            'static_audit':audit_result,
            'source_sha256':{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources+[HERE/'boot/app_config.h',HERE/'boot/wl82_services.h',HERE/'boot/display_test.h',HERE/'boot/boot_trace.h',HERE/'boot/clock_contract.h',HERE/'boot/board_power.h',HERE/'build_boot.py',HERE/'audit_boot.py',HERE/'audit_power.py',HERE/'audit_pre_os.py',HERE/'verify_stock_power.py']+([HERE/'cpu1_guard.py'] if args.cpu1_divider_guard else [])},
            'linked_archive_sha256':{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in libs+[archive]},
            'not_included':([] if peripheral else ['FM-1 peripheral BSP'])+['UBOOT/update packaging','validated restore path','physical qualification'],
            'boot_argument_policy':'stock 24-byte prefix; zero SDK SDRAM/external-app extensions',
            'persistent_configuration_policy':'cfg_vm/cfg_btif registrations discarded; cfg_bin and SDK integrity checks retained',
            'stock_power_evidence':stock_power_evidence,
            'unverified':['physical boot handoff','power/reset handoff','SDK initcall side effects',
                          'display origin/throughput','physical keyboard sampling','audio sample rate/transients'],
            'device_operations_performed':False}
    manifest.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
