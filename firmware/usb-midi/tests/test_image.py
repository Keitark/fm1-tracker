"""SPDX-License-Identifier: Apache-2.0

Negative static checks on an explicitly selected linked MIDI candidate.
No compiler, device, helper or firmware transfer is invoked.
"""
import os
from pathlib import Path
import struct
import sys
import unittest

_audit_root=Path(__file__).resolve().parents[2]/'nes'
_linked_test_available=bool(os.environ.get('FM1_MIDI_BUILD')) and (_audit_root/'audit_boot.py').is_file()
if _linked_test_available:
    sys.path.insert(0,str(_audit_root))
    from audit_boot import audit, PARTS


@unittest.skipUnless(_linked_test_available, 'Integration-only: needs full firmware workspace audits and explicit FM1_MIDI_BUILD')
class MidiImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder=Path(os.environ['FM1_MIDI_BUILD'])
        cls.elf=(folder/'fm1-usb-diag.elf').read_bytes()
        offset=struct.unpack_from('<I',cls.elf,32)[0]
        count,names_index=struct.unpack_from('<HH',cls.elf,48)
        raw=[struct.unpack_from('<10I',cls.elf,offset+i*40) for i in range(count)]
        names=raw[names_index];strings=cls.elf[names[4]:names[4]+names[5]]
        cls.sections={strings[s[0]:strings.index(b'\0',s[0])].decode():s for s in raw}
        sym=cls.sections['.symtab'];st=raw[sym[6]];strings=cls.elf[st[4]:st[4]+st[5]]
        cls.symbols={}
        for p in range(sym[4],sym[4]+sym[5],16):
            name,value,size,_,_,_=struct.unpack_from('<IIIBBH',cls.elf,p)
            cls.symbols[strings[name:strings.index(b'\0',name)].decode()]=(value,size,p)

    def check(self,elf=None):
        elf=elf or self.elf
        app=b''.join(elf[self.sections[n][4]:self.sections[n][4]+self.sections[n][5]] for n in PARTS)
        return audit(elf,app,usb_only=True,usb_peripheral_tests=True,usb_audio=True,
                     factory_diag=True,require_boot_trace=True,require_board_power=True)

    def corrupt(self,name,delta=0):
        address=self.symbols[name][0]+delta
        section=next(s for s in self.sections.values() if s[1]!=8 and s[3]<=address<s[3]+s[5])
        elf=bytearray(self.elf);elf[section[4]+address-section[3]]^=4
        return bytes(elf)

    def symbol(self,name,field,value):
        elf=bytearray(self.elf)
        struct.pack_into('<I',elf,self.symbols[name][2]+field,value)
        return bytes(elf)

    def test_exact_candidate_remains_unqualified(self):
        result=self.check()
        self.assertTrue(result['usb_midi_update'])
        self.assertFalse(result['usb_midi_installation_enabled'])
        self.assertFalse(result['hardware_boot_verified'])
        self.assertFalse(result['flashable'])

    def test_install_gate_cannot_be_enabled_by_byte_patch(self):
        with self.assertRaisesRegex(ValueError,'installation must remain blocked'):
            self.check(self.corrupt('fm1_midi_install_enabled'))

    def test_midi_descriptor_is_exact(self):
        with self.assertRaisesRegex(ValueError,'MIDI descriptor'):
            self.check(self.corrupt('fm1_midi_descriptor.d',88))

    def test_midi_product_matches_panel_discovery(self):
        self.assertIn(b'FM-1 Forge\0',self.elf)
        self.assertNotIn(b'FM1 FW Factory\0',self.elf)

    def test_midi_endpoint_policy_is_exact(self):
        with self.assertRaisesRegex(ValueError,'MIDI composite USB setup policy'):
            self.check(self.corrupt('filter',0x26))

    def test_shared_packet_bounds_remain_exact(self):
        with self.assertRaisesRegex(ValueError,'USB packet commit instructions'):
            self.check(self.corrupt('fm1_usb_packet_write',0x20))

    def test_midi_dma_and_identity_layout(self):
        with self.assertRaisesRegex(ValueError,'MIDI DMA'):
            self.check(self.symbol('fm1_midi_dma_in',4,self.symbols['fm1_midi_dma_in'][0]+4))
        with self.assertRaisesRegex(ValueError,'build identity'):
            self.check(self.symbol('fm1_midi_build_id',8,31))

    def test_sdk_task_registration_and_power_guard(self):
        with self.assertRaisesRegex(ValueError,'task registration'):
            self.check(self.corrupt('task_info_table',4*20+4))
        with self.assertRaisesRegex(ValueError,'power gateway instructions'):
            self.check(self.corrupt('fm1_board_power_init',10))


if __name__=='__main__':
    unittest.main()
