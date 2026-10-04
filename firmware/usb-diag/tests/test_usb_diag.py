from pathlib import Path
import importlib.util
import struct
import subprocess
import sys
import unittest

HERE=Path(__file__).resolve().parents[1]
ROOT=HERE.parents[1]
sys.path[:0]=[str(HERE),str(HERE.parent/'nes'),str(ROOT/'tools/jieli-wl82')]
from audit_boot import audit, PARTS
import vendor_overlay
import usb_diag_client as client
from build_boot import SDK

class PipePort:
    def __enter__(self):
        self.proc=subprocess.Popen([str(HERE/'build/host/Release/protocol_bridge.exe')],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
        return self
    def write(self,data):
        result=self.proc.stdin.write(data);self.proc.stdin.flush();return result
    def read(self,n):return self.proc.stdout.read(n)
    def __exit__(self,*args):
        self.proc.stdin.close();self.proc.wait(timeout=5);self.proc.stdout.close()

class TransportTests(unittest.TestCase):
    def test_real_c_parser_with_python_sender(self):
        with PipePort() as port:
            for data in (b'1',b'123456789',bytes(range(256))*2048):
                result=client.verify_transfer(port,data)
                self.assertIn('NOT stored or installed',result)
    def test_commit_is_rejected_after_verified_transfer(self):
        with PipePort() as port:
            client.verify_transfer(port,b'123456789')
            with self.assertRaisesRegex(RuntimeError,'COMMIT_BLOCKED'):client.exchange(port,'COMMIT')
    def test_bad_size_rejected_before_io(self):
        for data in (b'',bytes(client.LIMIT+1)):
            with self.assertRaises(ValueError):client.verify_transfer(None,data)
    def test_partial_control_transfer_checks_precede_read(self):
        source=(SDK/'apps/common/usb/device/cdc.c').read_text(encoding='utf-8')
        result=vendor_overlay.cdc(source)
        start=result.index('static u32 cdc_setup_rx(',result.index('static u32 cdc_setup_rx(')+1)
        body=result[start:result.index('static void cdc_reset',start)]
        self.assertLess(body.index('ctrl_req->wLength'),body.index('usb_read_ep0'))
        self.assertLess(body.index('usb_read_count0(usb_id)!=7'),body.index('usb_read_ep0'))
        self.assertIn('usb_read_ep0(usb_id,data,sizeof(data))',body)
        self.assertNotIn('read_ep[8]',body)
        self.assertIn('volatile u8 bmTransceiver',result)
        self.assertNotIn('buf = %s',result)
    def test_overlay_refuses_source_drift(self):
        source=(SDK/'apps/common/usb/device/cdc.c').read_text(encoding='utf-8')
        with self.assertRaises(ValueError):vendor_overlay.cdc(source.replace('    u8 bmTransceiver;','    u32 bmTransceiver;'))

class LinkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder=HERE/'build/usb0'
        cls.elf=(folder/'fm1-usb-diag.elf').read_bytes()
        cls.app=(folder/'fm1-usb-diag.app.bin').read_bytes()
        cls.symbols={}
        for line in (folder/'symbols.txt').read_text().splitlines():
            parts=line.split()
            if len(parts)==4:cls.symbols[parts[3]]=(int(parts[0],16),int(parts[1],16))
    def check(self,elf=None,app=None):
        return audit(self.elf if elf is None else elf,self.app if app is None else app,
                     usb_only=True,require_boot_trace=True,require_board_power=True)
    def patch(self,address,data):
        shoff=struct.unpack_from('<I',self.elf,32)[0];n=struct.unpack_from('<H',self.elf,48)[0]
        sections=[struct.unpack_from('<10I',self.elf,shoff+i*40) for i in range(n)]
        names=sections[struct.unpack_from('<H',self.elf,50)[0]]
        names=self.elf[names[4]:names[4]+names[5]]
        named={names[s[0]:names.index(b'\0',s[0])].decode():s for s in sections}
        section=next(s for s in sections if s[2]&2 and s[1]!=8 and s[3]<=address< s[3]+s[5])
        app_start=0
        for part in PARTS:
            if named[part]==section:break
            app_start+=named[part][5]
        off=address-section[3];elf=bytearray(self.elf);app=bytearray(self.app)
        elf[section[4]+off:section[4]+off+len(data)]=data
        app[app_start+off:app_start+off+len(data)]=data
        return elf,app
    def test_real_link(self):
        result=self.check();self.assertTrue(result['usb_only']);self.assertIsNone(result['cartridge'])
        self.assertFalse(result['flashable']);self.assertEqual(result['sdk_delayed_integrity']['scheduled_delay_ms'],8000)
    def test_device_descriptor_corruption_rejected(self):
        address,_=self.symbols['fm1_usb_device_descriptor']
        with self.assertRaisesRegex(ValueError,'descriptor'):self.check(*self.patch(address+4,b'\xff'))
    def test_usb_isr_return_corruption_rejected(self):
        address,size=self.symbols['usb0_g_isr']
        with self.assertRaisesRegex(ValueError,'interrupt return'):self.check(*self.patch(address+size-2,b'\x80\x00'))
    def test_trace_store_destination_corruption_rejected(self):
        address,_=self.symbols['__wrap_memory_init']
        with self.assertRaisesRegex(ValueError,'trace wrapper'):self.check(*self.patch(address+17,b'\x1c'))
    def test_power_stage_store_corruption_rejected(self):
        address,_=self.symbols['fm1_board_power_init']
        with self.assertRaisesRegex(ValueError,'gateway instructions'):self.check(*self.patch(address+17,b'\x67'))
    def test_startup_corruption_rejected(self):
        address,_=self.symbols['chip_entry']
        with self.assertRaises(ValueError):self.check(*self.patch(address+28,b'\x00'))

if __name__=='__main__':unittest.main()
