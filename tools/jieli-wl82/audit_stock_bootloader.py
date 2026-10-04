"""Decode/disassemble only this backup's uncompressed public-format boot bank.

Offline: no device access, config/reserved extraction or chip-key handling.
Generated ELF is analysis-only, never a replacement/download payload.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from compare_stock_images import ROOT, jl_enc_cipher, jl_crc16
from audit_fm1_firmware import make_elf

BACKUP_SHA='a45aa21afd8a659612427213ce80c7500a42fcbd8548262fb611f03df84e8b61'

def inspect_handoff(code):
    # Reviewed reachable instructions, not a search through linear-sweep data.
    evidence={
        0x1c04dc0:'ca ff 80 fd c7 01',           # r10 = 1c7fd80
        0x1c04f9e:'d0 ec c4 01 00 55',         # conditional skip of extension init
        0x1c04fa4:'42 20 43 20 50 ec a9 2d',   # zero pair at r10+d8 = argument+80
        0x1c051d0:'00 e1 88 a0',               # argument = r10+88
        0x1c051d8:'01 16 10 85 52 05 92 05',   # copy six words
        0x1c051e0:'09 98 81 60',               # pointer = argument+24
        0x1c051e8:'10 9f 02 07 92 07',         # copy 32-byte pointee
        0x1c05284:'d0 ec c0 10 00 e1 88 a0 c1 00', # r0 argument; indirect app call
    }
    for address,expected in evidence.items():
        expected=bytes.fromhex(expected);offset=address-0x1c02000
        if code[offset:offset+len(expected)]!=expected:
            raise ValueError(f'Boot handoff instruction mismatch at {address:#x}')
    return {'argument_address':'0x01c7fe08','prefix_bytes_copied':24,
            'pointee_bytes_copied':32,'application_call':'0x01c0528c',
            'sdk_plus80_initialization':'conditional in reviewed branch',
            'sdk_plus88_initialization':'not established',
            'hardware_verified':False}

def decode(raw):
    if len(raw)!=1048576 or hashlib.sha256(raw).hexdigest()!=BACKUP_SHA:
        raise ValueError('Unreviewed stock backup; refuse fixed layout')
    # The first directory entry is ROM-loaded uboot.boot, flags=0 (not LZ4).
    ent=bytearray(raw[32:64]);jl_enc_cipher(ent,0,32,0xffff)
    if jl_crc16(ent[2:])!=int.from_bytes(ent[:2],'little'):
        raise ValueError('Boot directory CRC mismatch')
    _,offset,size,flags,_,_,name=struct.unpack('<HIIBBH16s',ent[2:])
    if (offset,size,flags,name.split(b'\0')[0])!=(0xa0,14384,0,b'uboot.boot'):
        raise ValueError('Unexpected boot directory entry')
    bank=bytearray(raw[offset:offset+size]);jl_enc_cipher(bank,0,16,0xffff)
    count,length,load,start,crc,hcrc=struct.unpack_from('<HHIIHH',bank)
    if jl_crc16(bank[:14])!=hcrc or (count,length,load,start)!=(1,14368,0x1c02000,16):
        raise ValueError('Unexpected bank header or CRC')
    jl_enc_cipher(bank,start,length,0xffff)
    code=bytes(bank[start:start+length])
    if jl_crc16(code)!=crc:raise ValueError('Boot bank data CRC mismatch')
    return code,{'backup_sha256':BACKUP_SHA,'bank_count':count,'compressed':False,
                 'load_address':hex(load),'code_bytes':length,
                 'code_sha256':hashlib.sha256(code).hexdigest(),
                 'header_and_data_crc_valid':True,'device_operations_performed':False,
                 'handoff_evidence':inspect_handoff(code),
                 'config_or_reserved_extracted':False,'flashable':False}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--backup',type=Path,default=ROOT/'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin')
    ap.add_argument('--out',type=Path,default=ROOT/'private-backups/readback-20260926-024637/analysis/bootloader')
    args=ap.parse_args();code,report=decode(args.backup.read_bytes())
    args.out.mkdir(parents=True,exist_ok=True)
    elf=make_elf(code,0x1c02000,0x1c02000)
    path=args.out/'stock-boot-analysis-only.elf';path.write_bytes(elf)
    (args.out/'stock-boot-code.bin').write_bytes(code)
    result=subprocess.run(['C:/JL/pi32/bin/llvm-objdump.exe','-disassemble-all',
        '-arch-name=pi32v2','-mcpu=r3','-print-imm-hex',str(path)],capture_output=True,check=True)
    (args.out/'stock-boot-linear-sweep.asm').write_bytes(result.stdout)
    (args.out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
