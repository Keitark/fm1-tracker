"""Build experimental RAM status extension OFFLINE. Never opens a device."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from audit import verify

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent))
import audit_wl82_loader as loader
TC=Path('C:/JL/pi32/bin')
OUT=loader.ROOT/'tmp/wl82-status-extension'


def run(args):
    return subprocess.run(list(map(str,args)),check=True,capture_output=True,text=True)


def main():
    original=loader.decode(loader.SOURCE.read_bytes())
    loader_review=loader.inspect_protocol(original)
    if original[0x2318:0x231c]!=bytes.fromhex('31 e1 7b 0f'):
        raise ValueError('Original fallback instruction differs')
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'manifest.json').write_text('{"status":"incomplete","upload_qualified":false}\n')
    flags=['-target','pi32v2','-mcpu=r3','-mfprev1','-Oz','-ffreestanding',
           '-fno-builtin','-fno-common','-ffunction-sections','-fdata-sections']
    objects=[]
    for name in ('status.c','dispatch.S'):
        output=OUT/(name+'.o')
        run([TC/'clang.exe',*flags,'-c',HERE/name,'-o',output]);objects.append(output)
    elf=OUT/'status-extension.experimental.elf'
    run([TC/'pi32v2-lto-wrapper.exe','-o',elf,*objects,'-T'+str(HERE/'layout.ld'),
         '--plugin-opt=mcpu=r3','--plugin-opt=-mattr=+fprev1',
         '--plugin-opt=-pi32v2-large-program=true'])
    for section,name in (('.text','extension.bin'),('.patch','hook.bin')):
        run([TC/'llvm-objcopy.exe','-O','binary','-j',section,elf,OUT/name])
    asm=run([TC/'llvm-objdump.exe','-d','-mcpu=r3','-print-imm-hex',elf]).stdout
    (OUT/'disassembly.asm').write_text(asm)
    symbols=run([TC/'llvm-nm.exe','-n','-S',elf]).stdout
    (OUT/'symbols.txt').write_text(symbols)
    unresolved=run([TC/'llvm-nm.exe','-u',elf]).stdout
    review=verify((OUT/'extension.bin').read_bytes(),
                  (OUT/'hook.bin').read_bytes(),original,unresolved)
    report={'status':'OFFLINE_EXPERIMENTAL_ONLY','device_operations_performed':False,
            'upload_qualified':False,'physical_status_read_verified':False,
            'original_loader_sha256':loader.SHA,'hook_address':'0x01c04318',
            'proposed_extension_address':'0x01c60000','proposed_window_bytes':4096,
            'ram_overlap_and_cache_coherence_qualified':False,
            'machine_code_review':review,
            'installation_review':loader_review['memory_write_review'],
            'files':{name:{'bytes':(OUT/name).stat().st_size,
                          'sha256':hashlib.sha256((OUT/name).read_bytes()).hexdigest()}
                     for name in ('extension.bin','hook.bin')},
            'sources':{name:hashlib.sha256((HERE/name).read_bytes()).hexdigest()
                       for name in ('status.c','dispatch.S','layout.ld','build.py','audit.py')},
            'warning':'NOT an FM-1 firmware image. No installer or upload path supplied.'}
    (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    try:main()
    except subprocess.CalledProcessError as error:
        print(error.stdout);print(error.stderr,file=sys.stderr);raise
