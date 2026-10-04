from pathlib import Path
import struct
import sys
import unittest
import argparse

HERE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
from audit_boot import audit as audit_impl
from embed_rom50 import ROMS
FOLDER=HERE/'build/boot-rom50'
PERIPHERALS=False
ROM='rom50'
DISPLAY=False


def audit(*args,**kwargs):
    return audit_impl(*args,rom=ROM,display_only=DISPLAY,**kwargs)


class BootAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder=FOLDER
        if not (folder/'fm1-nes-boot-candidate.elf').exists():
            raise unittest.SkipTest('Offline boot candidate has not been built')
        cls.elf=(folder/'fm1-nes-boot-candidate.elf').read_bytes()
        cls.app=(folder/'fm1-nes-boot-candidate.app.bin').read_bytes()

    def test_real_link_closure(self):
        result=audit(self.elf,self.app,require_peripherals=PERIPHERALS)
        self.assertEqual(result['static_audit'],'passed')
        self.assertFalse(result['hardware_boot_verified'])
        self.assertFalse(result['flashable'])
        self.assertEqual(result['startup_instructions']['bytes_verified'],154)

    def test_corrupt_application_rejected(self):
        for app in (b'',self.app[:-1],b'bad!'+self.app[4:]):
            with self.assertRaises(ValueError):audit(self.elf,app)

    def test_wrong_entry_and_architecture_rejected(self):
        for offset,format,value in ((24,'<I',0x4000120),(18,'<H',0x28)):
            elf=bytearray(self.elf);struct.pack_into(format,elf,offset,value)
            with self.assertRaises(ValueError):audit(elf,self.app)

    def test_corrupt_embedded_rom_even_with_matching_app_rejected(self):
        if DISPLAY:self.skipTest('No cartridge in display-only image')
        # ELF and app can agree while both contain the wrong cartridge.
        magic=ROMS[ROM][1]
        offset=self.elf.index(magic)
        app_offset=self.app.index(magic)
        elf=bytearray(self.elf);app=bytearray(self.app)
        elf[offset+17]^=1;app[app_offset+17]^=1
        with self.assertRaisesRegex(ValueError,'cartridge hash'):audit(elf,app)

    def patch_xip(self,address,new):
        shoff=struct.unpack_from('<I',self.elf,32)[0]
        shcount=struct.unpack_from('<H',self.elf,48)[0]
        text=next(struct.unpack_from('<10I',self.elf,shoff+i*40)
                  for i in range(shcount)
                  if struct.unpack_from('<I',self.elf,shoff+i*40+12)[0]==0x02000120)
        elf=bytearray(self.elf);app=bytearray(self.app)
        off=address-0x02000120
        elf[text[4]+off:text[4]+off+len(new)]=new
        app[off:off+len(new)]=new
        return elf,app

    def test_boot_bridge_source_overread_rejected(self):
        info=audit(self.elf,self.app)['boot_compatibility']
        # Change source-loop bound from six words to seven, preserving sizes
        # and ELF/application agreement. The exact opcode audit must reject it.
        elf,app=self.patch_xip(int(info['bridge'],16)+23,b'\x0f')
        with self.assertRaisesRegex(ValueError,'prefix/zero-fill'):
            audit(elf,app)

    def test_boot_bridge_bypass_rejected(self):
        info=audit(self.elf,self.app)['boot_compatibility']
        call=int(info['startup'],16)+12
        destination=int(info['sdk_initializer'],16)
        elf,app=self.patch_xip(call+2,struct.pack('<i',destination-call-6))
        with self.assertRaisesRegex(ValueError,'bypasses boot compatibility'):
            audit(elf,app)

    def test_actual_startup_operands_and_loops_rejected(self):
        info=audit(self.elf,self.app)['boot_compatibility']
        start=int(info['startup'],16)
        # Mutate both ELF and app: symbol-only checks would miss these.
        for offset in (2,8,20,28,34,42,48,54,60,90,116,128,134,140,148):
            with self.subTest(offset=offset):
                old=self.app[start-0x02000120+offset]
                elf,app=self.patch_xip(start+offset,bytes([old^4]))
                with self.assertRaisesRegex(ValueError,'Startup instruction'):
                    audit(elf,app)

    def test_actual_entry_branch_rejected(self):
        elf,app=self.patch_xip(0x02000120,bytes.fromhex('04 81'))
        with self.assertRaisesRegex(ValueError,'entry branch'):audit(elf,app)

    def test_refined_trace_required_when_requested(self):
        if audit(self.elf,self.app)['early_boot_trace']['enabled']:
            self.assertTrue(audit_impl(self.elf,self.app,rom=ROM,display_only=DISPLAY,
                                     require_boot_trace=True)['early_boot_trace']['enabled'])
        else:
            with self.assertRaisesRegex(ValueError,'Missing refined boot trace'):
                audit_impl(self.elf,self.app,rom=ROM,display_only=DISPLAY,require_boot_trace=True)

    def test_cache_copy_operand_rejected(self):
        # RAM code is stored after XIP in the packed application. Mutate both
        # copies while retaining valid section sizes and linker symbols.
        shoff=struct.unpack_from('<I',self.elf,32)[0]
        shcount=struct.unpack_from('<H',self.elf,48)[0]
        rows=[struct.unpack_from('<10I',self.elf,shoff+i*40) for i in range(shcount)]
        text=next(s for s in rows if s[3]==0x02000120 and s[1]==1)
        ram=next(s for s in rows if s[3]==0x01c00000 and s[1]==1)
        # Find the reviewed cache destination instruction within initialized RAM.
        code=self.elf[ram[4]:ram[4]+ram[5]]
        offset=code.index(bytes.fromhex('c0 ff 00 80 f2 01 c1 ff'))
        for delta in (2,8,14):
            with self.subTest(delta=delta):
                elf=bytearray(self.elf);app=bytearray(self.app)
                elf[ram[4]+offset+delta]^=4;app[text[5]+offset+delta]^=4
                with self.assertRaisesRegex(ValueError,'Cache RAM copy operands'):
                    audit(elf,app)

    def test_memory_trace_cannot_skip_original_hook(self):
        info=audit(self.elf,self.app)
        if not info['early_boot_trace']['enabled']:self.skipTest('Historical image has no trace')
        start=int(info['boot_compatibility']['startup'],16)
        off=start-0x02000120+134
        wrapper=start+140+struct.unpack_from('<i',self.app,off+2)[0]
        elf,app=self.patch_xip(wrapper+30,bytes(6))
        with self.assertRaisesRegex(ValueError,'Boot trace wrapper instructions'):
            audit(elf,app)

    def test_configuration_initializer_change_rejected(self):
        info=audit(self.elf,self.app)['configuration_registry']
        elf,app=self.patch_xip(int(info['address'],16),bytes(4))
        with self.assertRaisesRegex(ValueError,'not the file reader'):audit(elf,app)

    def test_configuration_write_callback_rejected(self):
        info=audit(self.elf,self.app)['configuration_registry']
        elf,app=self.patch_xip(int(info['address'],16)+12,struct.pack('<I',0x02000120))
        with self.assertRaisesRegex(ValueError,'write callbacks'):audit(elf,app)

    def test_configuration_read_callback_change_rejected(self):
        info=audit(self.elf,self.app)['configuration_registry']
        elf,app=self.patch_xip(int(info['address'],16)+8,struct.pack('<I',0x02000120))
        with self.assertRaisesRegex(ValueError,'read callbacks'):audit(elf,app)

    def test_sdk_integrity_init_removal_rejected(self):
        info=audit(self.elf,self.app)['configuration_registry']
        elf,app=self.patch_xip(int(info['integrity_init_slot'],16),bytes(4))
        with self.assertRaisesRegex(ValueError,'initcall targets'):audit(elf,app)

    def test_delayed_integrity_timer_change_rejected(self):
        info=audit(self.elf,self.app)['sdk_delayed_integrity']
        elf,app=self.patch_xip(int(info['timer_wrapper'],16)+14,bytes(4))
        with self.assertRaisesRegex(ValueError,'8000 ms timeout'):audit(elf,app)

    def test_delayed_integrity_callback_change_rejected(self):
        info=audit(self.elf,self.app)['sdk_delayed_integrity']
        elf,app=self.patch_xip(int(info['timer_wrapper'],16)+10,struct.pack('<I',0x02000120))
        with self.assertRaisesRegex(ValueError,'delayed integrity callback'):audit(elf,app)

    def test_nes_task_priority_change_rejected(self):
        info=audit(self.elf,self.app)['nes_task']
        elf,app=self.patch_xip(int(info['table'],16)+4*20+4,b'\x1e')
        with self.assertRaisesRegex(ValueError,'task registration changed'):audit(elf,app)

    def test_integrated_irq_return_rejected(self):
        if not PERIPHERALS:self.skipTest('Peripheral profile only')
        result=audit(self.elf,self.app,require_peripherals=True)
        address=int(result['integrated_symbols']['fm1_alink_isr'],16)
        last=address+result['irq_wrapper_bytes']-2
        shoff=struct.unpack_from('<I',self.elf,32)[0]
        shcount=struct.unpack_from('<H',self.elf,48)[0]
        text=next(struct.unpack_from('<10I',self.elf,shoff+i*40)
                  for i in range(shcount)
                  if struct.unpack_from('<I',self.elf,shoff+i*40+12)[0]==0x02000120)
        elf=bytearray(self.elf);app=bytearray(self.app)
        elf_offset=text[4]+last-text[3];app_offset=last-0x02000120
        self.assertEqual(elf[elf_offset:elf_offset+2],b'\x81\x00')
        elf[elf_offset:elf_offset+2]=b'\x80\x00' # ordinary rts is not an ISR return
        app[app_offset:app_offset+2]=b'\x80\x00'
        with self.assertRaisesRegex(ValueError,'interrupt return'):
            audit(elf,app,require_peripherals=True)

    def power_info(self):
        info=audit(self.elf,self.app)['board_power']
        if not info['enabled']:self.skipTest('Historical image has no board power integration')
        return info

    def test_board_power_required_when_requested(self):
        if audit(self.elf,self.app)['board_power']['enabled']:
            self.assertTrue(audit_impl(self.elf,self.app,rom=ROM,display_only=DISPLAY,
                                     require_board_power=True)['board_power']['enabled'])
        else:
            with self.assertRaisesRegex(ValueError,'Missing stock board power'):
                audit_impl(self.elf,self.app,rom=ROM,display_only=DISPLAY,require_board_power=True)

    def test_power_parameter_corruption_rejected(self):
        info=self.power_info()
        elf,app=self.patch_xip(int(info['parameter_address'],16)+14,b'\x03')
        with self.assertRaisesRegex(ValueError,'parameter block changed'):audit(elf,app)

    def test_compiled_power_voltage_corruption_rejected(self):
        info=self.power_info()
        # The parameter block is retained but LTO folds values into instructions.
        # Corrupting only the executed VDC14 selector must also fail.
        elf,app=self.patch_xip(int(info['initializer'],16)+0x2a9,b'\x23')
        with self.assertRaisesRegex(ValueError,'Power initializer instructions changed'):audit(elf,app)

    def test_power_helper_redirection_rejected(self):
        info=self.power_info()
        elf,app=self.patch_xip(int(info['initializer'],16)+0x2ac,bytes(4))
        with self.assertRaisesRegex(ValueError,'Power call target changed'):audit(elf,app)

    def test_power_gateway_bypass_rejected(self):
        info=self.power_info()
        elf,app=self.patch_xip(int(info['gateway'],16)+18,bytes(4))
        with self.assertRaisesRegex(ValueError,'Power short call opcode changed'):audit(elf,app)

    def test_board_power_call_removal_rejected(self):
        info=self.power_info()
        elf,app=self.patch_xip(int(info['app_task_call'],16),bytes(4))
        with self.assertRaisesRegex(ValueError,'Board power call missing'):audit(elf,app)

    def test_board_power_init_order_corruption_rejected(self):
        info=self.power_info()
        elf,app=self.patch_xip(int(info['app_task_call'],16)+12,bytes(4))
        with self.assertRaisesRegex(ValueError,'Board power startup ordering changed'):audit(elf,app)


if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False)
    parser.add_argument('--folder',type=Path,default=FOLDER)
    parser.add_argument('--peripherals',action='store_true')
    parser.add_argument('--display',action='store_true')
    parser.add_argument('--rom',choices=('rom50','smb1'),default='rom50')
    options,extra=parser.parse_known_args()
    FOLDER=options.folder;PERIPHERALS=options.peripherals;ROM=options.rom;DISPLAY=options.display
    unittest.main(argv=[sys.argv[0],*extra])
