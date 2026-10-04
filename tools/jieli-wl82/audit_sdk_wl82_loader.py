"""Offline exploratory decode of the pinned SDK WL82 loader; NEVER upload it.

Transport decoding is inferred from the community WL82 profile and checked
by disassembly, not a statement of device compatibility or authorization.
"""
import hashlib
import json
import subprocess

from audit_wl82_loader import BASE,ROOT,make_elf,jl_crc_cipher

SOURCE=ROOT/'references/source/fw-AC79_AIoT_SDK/cpu/wl82/tools/wl82loader.bin'
SHA='9920e66626fc86b2db536050a4d23dec10c8d1081575553539835fd812276c27'


def decode(raw):
    if len(raw)!=31232 or hashlib.sha256(raw).hexdigest()!=SHA:
        raise ValueError('Unreviewed SDK helper blob')
    code=bytearray(raw)
    for offset in range(0,len(code),512):
        jl_crc_cipher(code,offset,min(512,len(code)-offset))
    return bytes(code)


def main():
    code=decode(SOURCE.read_bytes())
    out=ROOT/'tmp/sdk-wl82-loader-audit'
    out.mkdir(parents=True,exist_ok=True)
    elf=out/'sdk-wl82-loader-analysis-only.elf'
    elf.write_bytes(make_elf(code,BASE,BASE))
    (out/'sdk-wl82-loader-code.bin').write_bytes(code)
    asm=subprocess.run(['C:/JL/pi32/bin/llvm-objdump.exe','-disassemble-all',
        '-arch-name=pi32v2','-mcpu=r3','-print-imm-hex',str(elf)],
        capture_output=True,check=True).stdout
    (out/'linear-sweep.asm').write_bytes(asm)
    report={'encoded_blob_sha256':SHA,'decoded_code_sha256':hashlib.sha256(code).hexdigest(),
            'assumed_load_address':hex(BASE),'bytes':len(code),'transport_block_bytes':512,
            'device_operations_performed':False,'compatible_with_fm1_verified':False,
            'warning':'analysis-only; no loader replacement or upload authorized'}
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
