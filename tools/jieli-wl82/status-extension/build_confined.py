"""Offline cold-loader variant; original footprint and BSS, no device I/O."""
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from build import HERE, TC, run, loader
from jltech.cipher import jl_crc_cipher

OUT=loader.ROOT/'tmp/wl82-confined-status-loader'
REGIONS=((0x1c04272,4,'route.bin'),(0x1c04318,4,'hook.bin'),
         (0x1c048e4,382,'extension.bin'))
REVIEWED_PARTS={
    'route.bin':'b9d8f9f6e7368f5853ecccdc730abacee7acb94bdc7c0837fb774bc4441a8463',
    'hook.bin':'323c170fb9ae0e00d230428479abedde4697f644b47cea489177096c92293216',
    'extension.bin':'569ef99d500e7de0fd9217af3269e970ce040b90e93f8302386a019c20581c27'}
ENCODED_SHA='288dcd73fed7eeb4fd144aa943edcbf4dfe410bcaf695e18f70ecd2f2b1a806d'


def combine(original,parts):
    loader.inspect_protocol(original)
    result=bytearray(original)
    for address,limit,name in REGIONS:
        data=parts[name]
        if hashlib.sha256(data).hexdigest()!=REVIEWED_PARTS[name]:
            raise ValueError('Machine code changed; review required before use')
        if not data or len(data)>limit or (name!='extension.bin' and len(data)!=limit):
            raise ValueError('Invalid replacement size')
        result[address-loader.BASE:address-loader.BASE+len(data)]=data
    if len(result)!=len(original):raise ValueError('Loader length changed')
    for index,(before,after) in enumerate(zip(original,result)):
        if before!=after and not any(a-loader.BASE<=index<a-loader.BASE+len(parts[n])
                                   for a,_,n in REGIONS):
            raise ValueError('Out-of-scope modification')
    return bytes(result)


def transport_encode(code):
    result=bytearray(code)
    for offset in range(0,len(result),512):
        jl_crc_cipher(result,offset,min(512,len(result)-offset))
    return bytes(result)


def main():
    original=loader.decode(loader.SOURCE.read_bytes())
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'manifest.json').write_text('{"status":"incomplete","flash_writer_enabled":false}\n')
    flags=['-target','pi32v2','-mcpu=r3','-mfprev1','-Oz','-ffreestanding',
           '-fno-builtin','-fno-common','-ffunction-sections','-fdata-sections']
    objects=[]
    for name in ('status.c','dispatch.S','confined-route.S'):
        obj=OUT/(name+'.o')
        run([TC/'clang.exe',*flags,'-c',HERE/name,'-o',obj]);objects.append(obj)
    elf=OUT/'confined-extension.elf'
    run([TC/'pi32v2-lto-wrapper.exe','-o',elf,*objects,'-T'+str(HERE/'confined-layout.ld'),
         '--plugin-opt=mcpu=r3','--plugin-opt=-mattr=+fprev1',
         '--plugin-opt=-pi32v2-large-program=true'])
    if run([TC/'llvm-nm.exe','-u',elf]).stdout.strip():
        raise ValueError('Unresolved extension symbols')
    for section,name in (('.route','route.bin'),('.patch','hook.bin'),('.text','extension.bin')):
        run([TC/'llvm-objcopy.exe','-O','binary','-j',section,elf,OUT/name])
    (OUT/'disassembly.asm').write_text(run([TC/'llvm-objdump.exe','-d','-mcpu=r3','-print-imm-hex',elf]).stdout)
    parts={name:(OUT/name).read_bytes() for _,_,name in REGIONS}
    code=combine(original,parts)
    raw=transport_encode(code)
    if hashlib.sha256(raw).hexdigest()!=ENCODED_SHA:
        raise ValueError('Unexpected composed loader identity')
    if transport_encode(raw)!=code:raise ValueError('Transport round trip failed')
    (OUT/'loader.decoded.bin').write_bytes(code)
    (OUT/'loader.mengli.bin').write_bytes(raw)
    report={'status':'OFFLINE_COLD_LOADER_CANDIDATE','bytes':len(raw),
            'original_encoded_sha256':loader.SHA,'original_decoded_sha256':loader.DECODED_SHA,
            'load_address':hex(loader.BASE),'entry_address':hex(loader.BASE),
            'original_footprint_preserved':True,'original_bss_preserved':True,
            'retired_optional_commands':'FC40..FC48','new_command':'FC03 NOR SR1',
            'key_and_negotiation_paths_preserved':True,'device_operations_performed':False,
            'hardware_verified':False,'flash_writer_enabled':False,
            'files':{name:{'bytes':(OUT/name).stat().st_size,
                           'sha256':hashlib.sha256((OUT/name).read_bytes()).hexdigest()}
                     for name in ('route.bin','hook.bin','extension.bin','loader.decoded.bin','loader.mengli.bin')},
            'sources':{name:hashlib.sha256((HERE/name).read_bytes()).hexdigest()
                       for name in ('status.c','dispatch.S','confined-route.S','confined-layout.ld','build_confined.py')},
            'warning':'Cold UBOOT only. Not an FM-1 flash image. Never hot-patch the running helper.'}
    (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
