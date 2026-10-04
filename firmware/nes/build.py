"""Build/test locally; no updater, packaging, USB, or device commands exist here."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
from embed_rom50 import generate, ROMS, rom_path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
CORE=ROOT/'references/source/peak-nes'
PIN='68bdfc8de570264c0e84f73766f1a1ed3591066f'


def run(cmd,**kwargs):
    print(' '.join(map(str,cmd)),flush=True)
    return subprocess.run(list(map(str,cmd)),check=True,**kwargs)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('target',choices=['host','pi32v2','all'])
    ap.add_argument('--toolchain',type=Path,default=Path('C:/JL/pi32/bin'))
    selection=ap.add_mutually_exclusive_group()
    selection.add_argument('--rom50',dest='cartridge',action='store_const',const='rom50',help='Embed the hash-pinned local ROM 50')
    selection.add_argument('--smb1',dest='cartridge',action='store_const',const='smb1',help='Embed the private hash-pinned SMB1 ROM (default)')
    selection.add_argument('--no-rom',dest='cartridge',action='store_const',const=None,help='Build the core without an embedded cartridge')
    ap.set_defaults(cartridge='smb1')
    args=ap.parse_args()
    cartridge=args.cartridge
    suffix='-'+cartridge if cartridge else ''
    commit=run(['git','-C',CORE,'rev-parse','HEAD'],capture_output=True,text=True).stdout.strip()
    if commit!=PIN: raise SystemExit('Dependency revision differs from reviewed pin')
    if run(['git','-C',CORE,'status','--porcelain'],capture_output=True,text=True).stdout.strip():
        raise SystemExit('Dependency has local changes; review before building')
    if args.target in ('host','all'):
        dest=HERE/('build/host'+suffix)
        run(['cmake','-S',HERE,'-B',dest,'-G','Visual Studio 17 2022','-A','x64',f'-DPython3_EXECUTABLE={sys.executable}',
             '-DFM1_EMBED_ROM50='+('ON' if cartridge else 'OFF'),
             '-DFM1_CARTRIDGE='+(cartridge or 'rom50')])
        run(['cmake','--build',dest,'--config','Release'])
        run(['ctest','--test-dir',dest,'-C','Release','--output-on-failure'])
    if args.target in ('pi32v2','all'):
        dest=HERE/('build/pi32v2'+suffix);dest.mkdir(parents=True,exist_ok=True)
        (dest/'build-manifest.json').write_text(json.dumps({'status':'build_in_progress_or_failed','flashable':False},indent=2)+'\n',encoding='utf-8')
        sources=[HERE/'src'/f for f in ('fm1_nes.c','fm1_nes_target.c','fm1_board.c','fm1_audio_queue.c','fm1_wl82.c','fm1_stock_keys.c','fm1_wl82_keyscan.c')]+[CORE/'src'/f for f in ('nes.c','nes_cpu.c','nes_ppu.c','nes_apu.c')]
        if cartridge:
            generated=dest/'generated/fm1_rom50.c'
            generate(rom_path(cartridge),generated,cartridge)
            sources.append(generated)
        flags=['-target','pi32v2','-mcpu=r3','-mfprev1','-std=gnu11','-Oz','-DFM1_TARGET_PI32V2=1',
               '-ffunction-sections','-fdata-sections','-fno-common','-fno-unwind-tables',
               '-I'+str(HERE/'include'),'-I'+str(HERE/'port'),'-I'+str(CORE/'inc'),
               '-I'+str(ROOT/'references/source/fw-AC79_AIoT_SDK/include_lib/newlib/include'),
               '-I'+str(ROOT/'references/source/fw-AC79_AIoT_SDK/include_lib/driver/cpu/wl82'),
               '-I'+str(ROOT/'references/source/fw-AC79_AIoT_SDK/include_lib'),
               '-I'+str(ROOT/'references/source/fw-AC79_AIoT_SDK/include_lib/system')]
        objects=[]
        for source in sources:
            obj=dest/(source.stem+'.o');objects.append(obj)
            warnings=['-Wall','-Wextra','-Werror'] if source.is_relative_to(HERE) else []
            run([args.toolchain/'clang.exe',*flags,*warnings,'-c',source,'-o',obj])
        archive=dest/'libfm1_nes.a'
        run([args.toolchain/'llvm-ar.exe','rcs',archive,*objects])
        symbols=run([args.toolchain/'llvm-nm.exe','-S',*objects],capture_output=True,text=True).stdout
        (dest/'symbols.txt').write_text(symbols,encoding='utf-8')
        state_match=re.search(r'^\S+\s+([0-9a-fA-F]+)\s+b\s+machine$',symbols,re.M)
        if not state_match: raise SystemExit('Could not confirm target state allocation')
        cartridge_match=re.search(r'^\S+\s+([0-9a-fA-F]+)\s+r\s+cartridge$',symbols,re.M)
        if cartridge and (not cartridge_match or int(cartridge_match.group(1),16)!=ROMS[cartridge][0]):
            raise SystemExit('Could not confirm read-only embedded ROM allocation')
        report={'status':'cross_compiled_library_only_NOT_BOOTABLE','core_commit':PIN,
                'architecture':'pi32v2','cpu':'r3','flashable':False,
                'cartridge':cartridge,
                'embedded_rom_sha256':ROMS[cartridge][2] if cartridge else None,
                'embedded_rom_readonly_object_bytes':ROMS[cartridge][0] if cartridge else 0,
                'build_scripts':{s.name:hashlib.sha256(s.read_bytes()).hexdigest() for s in (HERE/'build.py',HERE/'embed_rom50.py')},
                'entry_point':'fm1_rom50_start (legacy symbol; board-gated, NOT a reset vector)' if cartridge else 'fm1_nes_target_start',
                'state_bytes':int(state_match.group(1),16), 'compile_flags':flags,
                'sdk_commit':run(['git','-C',ROOT/'references/source/fw-AC79_AIoT_SDK','rev-parse','HEAD'],capture_output=True,text=True).stdout.strip(),
                'compiler_sha256':hashlib.sha256((args.toolchain/'clang.exe').read_bytes()).hexdigest(),
                'board_support_verified':False,'sources':{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources},
                'project_headers':{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for folder in ('include','port') for s in (HERE/folder).glob('*.h')},
                'object_bss_bytes_excluding_sdk_and_stacks':sum(int(x,16) for x in re.findall(r'^\S+\s+([0-9a-fA-F]+)\s+[bB]\s+\S+$',symbols,re.M)),
                'board_adapter':'SPI1 LCD + SDK ALINK0 + portable video/audio/input/timing; requires external board services',
                'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
                'archive_bytes':archive.stat().st_size}
        (dest/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,indent=2))


if __name__=='__main__':main()
