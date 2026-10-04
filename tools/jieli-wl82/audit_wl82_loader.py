"""Offline disassembly of the exact WL82 helper used for FM-1 readback.

Fixed MengLi transport encoding only; no device key or device I/O. The output
ELF is analysis-only, never an upload/flash artifact.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from audit_fm1_firmware import make_elf
from compare_stock_images import ROOT
from jltech.cipher import jl_crc_cipher

SOURCE=ROOT/'tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin'
SHA='d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376'
DECODED_SHA='db8676b9cca4e2c0dfee062715cdb41528a2a8ccc827520ba8c194e4a2e0356e'
BASE=0x1c02000


def decode(raw):
    if len(raw)!=24064 or hashlib.sha256(raw).hexdigest()!=SHA:
        raise ValueError('Unreviewed WL82 helper blob')
    code=bytearray(raw)
    # Matches the verified uploader's 512-byte memory-write transactions.
    # UBOOT decodes each transaction independently, resetting the format state.
    for offset in range(0,len(code),512):
        jl_crc_cipher(code,offset,min(512,len(code)-offset))
    return bytes(code)


def inspect_protocol(code):
    """Report manually traced paths in this exact blob, not hardware acceptance.

    Table words are data, despite objdump's linear-sweep instruction labels.
    A pi32v2 tbh destination is table_base + 2 * unsigned_halfword.
    """
    if len(code)!=24064 or hashlib.sha256(code).hexdigest()!=DECODED_SHA:
        raise ValueError('Unreviewed decoded WL82 helper')

    def words(address, count):
        return struct.unpack_from('<'+'I'*count,code,address-BASE)

    def jump_table(address, count):
        return [address+2*v for v in
                struct.unpack_from('<'+'H'*count,code,address-BASE)]

    registration=words(0x1c07c28,3)
    ops=words(registration[1],6)
    fb=jump_table(0x1c041c0,7)
    fc=jump_table(0x1c042c6,14)
    ioctl=jump_table(0x1c050ba,4)
    # These assertions make table interpretation explicit; the full hash above
    # binds every instruction discussed in WL82_LOADER_AUDIT.md as well.
    if registration!=(0x1c0560c,0x1c07964,0x1c0974c):
        raise ValueError('Unexpected NOR registration')
    if ops!=(0x1c04e3c,0x1c04e1a,0x1c0505a,0,0x1c05430,0):
        raise ValueError('Unexpected NOR callbacks')
    if (fb[1],fb[4],fb[6],fc[1],ioctl[0])!=(0x1c04458,0x1c044be,0x1c045aa,0x1c0462c,0x1c050c2):
        raise ValueError('Unexpected command dispatch')
    return {
        'scope':'exact-loader static review; no device execution',
        'nor_callbacks':dict(zip(('open','read','ioctl','unused_12','write','unused_20'),
                                 [hex(v) for v in ops])),
        'handlers':{'FB01_sector_erase':hex(fb[1]),'FB04_program':hex(fb[4]),
                    'FB06_memory_write':hex(fb[6]),
                    'FC0A_online':hex(fc[1]),'ioctl_200':hex(ioctl[0])},
        'memory_write_review':{
            'vendor_copy_excluded_start':'0x1c02000',
            'vendor_copy_excluded_end_exclusive':'0x1c21e00',
            'range_test_checks_start_address_only':True,
            'excluded_path_register_write':{'address':'0x10500','value':'0x4000'},
            'excluded_path_return_flag':0,
            'ROM_fallback_behavior_qualified':False,
            'proposed_status_hook_uses_excluded_path':True,
            'outside_range_copy_routine':'0x1c0551c',
            'explicit_cache_maintenance_in_reviewed_copy_path':False,
            'end_to_end_cache_coherence_qualified':False,
            'live_status_patch_installation_qualified':False,
        },
        'online_device_may_write_status_registers':True,
        'sector_erase_spi_opcode':'0x20',
        'erase_reply_proves_completion':False,
        'program_callback_result_checked_by_handler':False,
        'FC03_handled_by_reviewed_vendor_dispatch':False,
        'FC0D_set_flash_commands_handled_by_reviewed_vendor_dispatch':False,
        'FC0D_dispatch_target':hex(fc[4]),
        'host_ready_poll_qualified':False,
        'read_callback_checks_busy_wait_result':False,
        'physical_write_verified':False,
        'physical_restore_verified':False,
    }


def main():
    code=decode(SOURCE.read_bytes())
    protocol=inspect_protocol(code)
    out=ROOT/'tmp/wl82-loader-audit'
    out.mkdir(parents=True,exist_ok=True)
    elf=out/'wl82-loader-analysis-only.elf'
    elf.write_bytes(make_elf(code,BASE,BASE))
    (out/'wl82-loader-code.bin').write_bytes(code)
    asm=subprocess.run(['C:/JL/pi32/bin/llvm-objdump.exe','-disassemble-all',
        '-arch-name=pi32v2','-mcpu=r3','-print-imm-hex',str(elf)],
        capture_output=True,check=True).stdout
    (out/'linear-sweep.asm').write_bytes(asm)
    report={'encoded_blob_sha256':SHA,'decoded_code_sha256':hashlib.sha256(code).hexdigest(),
            'load_address':hex(BASE),'bytes':len(code),'transport_block_bytes':512,
            'device_operations_performed':False,'physical_write_verified':False,
            'protocol_review':protocol,
            'warning':'linear sweep includes data; not a complete recovered call graph'}
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
