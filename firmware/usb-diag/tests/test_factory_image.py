"""Negative static audits against this exact asset-free target candidate."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import unittest
HERE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE.parent/'nes'))
from audit_boot import audit,PARTS
from audit_power import call_target
import build_stock_image as pack

class FactoryImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=Path(os.environ.get('FM1_FACTORY_BUILD',str(HERE/'build/fm1-forge-diag-v1')))
        cls.manifest=json.loads((cls.folder/'build-manifest.json').read_text())
        cls.elf=(cls.folder/'fm1-usb-diag.elf').read_bytes()
        off=struct.unpack_from('<I',cls.elf,32)[0];n,strings=struct.unpack_from('<HH',cls.elf,48)
        raw=[struct.unpack_from('<10I',cls.elf,off+i*40) for i in range(n)];s=raw[strings];names=cls.elf[s[4]:s[4]+s[5]]
        cls.sections={names[s[0]:names.index(b'\0',s[0])].decode():s for s in raw}
        cls.symbols={}
        for line in (cls.folder/'symbols.txt').read_text().splitlines():
            fields=line.split()
            if len(fields)==4:cls.symbols[fields[3]]=(int(fields[0],16),int(fields[1],16))
    def check(self,elf=None,**flags):
        elf=elf or self.elf;app=b''.join(elf[self.sections[n][4]:self.sections[n][4]+self.sections[n][5]] for n in PARTS)
        opts=dict(usb_only=True,usb_peripheral_tests=True,factory_diag=True,factory_user_module=self.manifest.get('factory_user_module',False),factory_clock=self.manifest.get('forge_clock','sdk-default'),factory_hal_access=self.manifest.get('forge_hal_access'),usb_audio=self.manifest['usb_audio'],usb_video=self.manifest.get('usb_video',False),require_boot_trace=True,require_board_power=True)
        opts.update(flags);return audit(elf,app,**opts)
    def corrupt(self,symbol,delta=0):
        address=self.symbols[symbol][0]+delta
        s=next(s for s in self.sections.values() if s[1]!=8 and s[3]<=address<s[3]+s[5])
        elf=bytearray(self.elf);elf[s[4]+address-s[3]]^=4;return bytes(elf)
    def call_offset(self,caller,callee):
        start,size=self.symbols[caller]
        s=next(s for s in self.sections.values() if s[1]!=8 and s[3]<=start< s[3]+s[5])
        body=self.elf[s[4]+start-s[3]:s[4]+start-s[3]+size]
        for off in range(0,size-1,2):
            n=6 if body[off:off+2]==b'\x80\xff' else 4 if body[off]&0xc0==0x80 and body[off+1]==0xea else 2 if body[off]&0x8f==1 and body[off+1]&0xe0==0x80 else 0
            if n and off+n<=size and call_target(body[off:off+n],start+off)==self.symbols[callee][0]:return off
        self.fail('Call not found: '+caller+' -> '+callee)
    def corrupt_symbol(self,name,field,value):
        table=self.sections['.symtab'];off=struct.unpack_from('<I',self.elf,32)[0]
        strings=struct.unpack_from('<10I',self.elf,off+table[6]*40)
        text=self.elf[strings[4]:strings[4]+strings[5]];elf=bytearray(self.elf)
        for entry in range(table[4],table[4]+table[5],16):
            n=struct.unpack_from('<I',elf,entry)[0]
            if text[n:text.index(b'\0',n)].decode()==name:
                struct.pack_into('<I',elf,entry+(4 if field=='value' else 8),value);return bytes(elf)
        self.fail('Symbol not found: '+name)
    def test_exact_image_and_asset_free_profile(self):
        self.assertEqual(self.check()['static_audit'],'passed')
        self.assertTrue(self.manifest['factory_diag']);self.assertTrue(self.manifest['asset_free']);self.assertEqual(self.manifest['factory_schema'],1)
        self.assertFalse(any(self.manifest[n] for n in ('nes_smb1','mdx','mod')))
        app=(self.folder/'fm1-usb-diag.app.bin').read_bytes()
        self.assertEqual(hashlib.sha256(app).hexdigest(),self.manifest['application_sha256'])
        self.assertLess(len(app),512*1024)
        for name in ('cartridge','song','player','upload'):self.assertNotIn(name,self.symbols)
    def test_factory_requires_own_audit_profile(self):
        with self.assertRaises(ValueError):self.check(factory_diag=False)
    def test_project_hal_selection_matches_linked_table(self):
        selection=self.manifest.get('forge_hal_access')
        if selection is None:self.skipTest('Legacy candidate has no project HAL selections')
        for name in selection:
            wrong=dict(selection);wrong[name]=not wrong[name]
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,'HAL access table'):self.check(factory_hal_access=wrong)
        with self.assertRaisesRegex(ValueError,'HAL access table'):self.check(self.corrupt('fm1_forge_hal_access'))
        with self.assertRaisesRegex(ValueError,'HAL access selection'):self.check(factory_hal_access=dict(selection,audio=1))
    def test_task_affinity_is_pinned(self):
        with self.assertRaisesRegex(ValueError,'task registration'):self.check(self.corrupt('task_info_table',4*20))
    def test_usb_descriptor_is_pinned(self):
        with self.assertRaisesRegex(ValueError,'descriptor'):self.check(self.corrupt('fm1_usb_device_descriptor',4))
    def test_uac_descriptor_and_ep0_policy_are_pinned(self):
        if not self.manifest['usb_audio']:self.skipTest('CDC-only profile')
        with self.assertRaisesRegex(ValueError,'UAC1'):self.check(self.corrupt('fm1_uac_descriptor',90))
        with self.assertRaisesRegex(ValueError,'setup policy'):self.check(self.corrupt('filter',0x29))
    def test_uvc_descriptor_and_ep0_policy_are_pinned(self):
        if not self.manifest.get('usb_video'):self.skipTest('No UVC function')
        for delta in (2,10,28,59,80,106,145,155):
            with self.subTest(delta=delta),self.assertRaisesRegex(ValueError,'UVC descriptor'):self.check(self.corrupt('fm1_uvc_video_descriptor',delta))
        with self.assertRaisesRegex(ValueError,'setup policy'):self.check(self.corrupt('filter',0x18))
        with self.assertRaisesRegex(ValueError,'Non-video factory'):self.check(usb_video=False)
    def test_uvc_foreground_startup_and_teardown_are_pinned(self):
        if not self.manifest.get('usb_video'):self.skipTest('No UVC function')
        for caller,callee,reason in (
            ('app_task_handler','fm1_uvc_setup_init','initialize once'),
            ('fm1_forge_task','fm1_forge_capture_step','foreground LCD'),
            ('fm1_peripheral_usb_audio_quiesce','fm1_uvc_capture_stop','STOP/UBOOT'),
            ('usb_device_mode','fm1_uvc_capture_stop','USB device teardown'),
            ('filter','fm1_uvc_setup_filter','setup sieve')):
            off=self.call_offset(caller,callee)
            with self.subTest(caller=caller),self.assertRaisesRegex(ValueError,reason):self.check(self.corrupt(caller,off))
    def test_uvc_static_memory_and_unqualified_manifest(self):
        if not self.manifest.get('usb_video'):self.skipTest('No UVC function')
        contract=self.manifest['usb_video_contract'];self.assertEqual((contract['width'],contract['height'],contract['pixel_format'],contract['frame_rate']),(256,240,'YUY2',5))
        self.assertEqual((contract['payload_bytes'],contract['max_packet_bytes'],contract['endpoint']),(640,642,'0x84'))
        self.assertEqual(contract['vc_interface'],5 if self.manifest['usb_audio'] else 2)
        self.assertEqual(contract['static_memory']['fm1_uvc_state']['bytes'],2068)
        self.assertEqual(contract['static_memory']['fm1_uvc_dma']['bytes'],1024)
        self.assertFalse(contract['hardware_accepted']);self.assertFalse(contract['audio_under_load_accepted']);self.assertFalse(contract['full_framebuffer']);self.assertFalse(contract['heap_allocation']);self.assertFalse(self.manifest['flashable']);self.assertFalse(self.manifest['device_operations_performed'])
        for name,size,reason in (('fm1_uvc_dma',960,'UVC DMA'),('fm1_uvc_state',2072,'three-packet queue'),('fm1_forge_video_capture',1284,'two-row storage')):
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,reason):self.check(self.corrupt_symbol(name,'size',size))
    def test_uvc_offline_pack_preserves_exact_baseline_without_device_io(self):
        if not self.manifest.get('usb_video'):self.skipTest('No UVC function')
        baseline=pack.V14_BACKUP.read_bytes();app=(self.folder/'fm1-usb-diag.app.bin').read_bytes()
        image,report=pack.build_candidate(baseline,self.elf,app,require_peripherals=False,usb_only=True,usb_peripheral_tests=True,usb_audio=self.manifest['usb_audio'],usb_video=True,factory_diag=True,factory_user_module=self.manifest.get('factory_user_module',False),factory_clock=self.manifest.get('forge_clock','sdk-default'),factory_hal_access=self.manifest['forge_hal_access'],factory_project_schema=3)
        self.assertEqual(len(image),0x100000);self.assertEqual(report['static_boot_audit']['static_audit'],'passed')
        self.assertTrue(all(report['checks'].values()));self.assertFalse(report['flashable']);self.assertFalse(report['device_operations_performed'])
        for start,end in ((0,0x4000),(0x4040,0x4120),(0x4120+len(app),0x100000)):
            self.assertEqual(image[start:end],baseline[start:end])
    def test_packet_doorbell_and_irq_restore_are_pinned(self):
        for delta in (0x76,0x96 if self.manifest['usb_audio'] else 0x94):
            with self.subTest(delta=delta),self.assertRaisesRegex(ValueError,'USB packet'):self.check(self.corrupt('fm1_usb_packet_write',delta))
    def test_boot_trace_and_power_cannot_be_corrupted(self):
        with self.assertRaisesRegex(ValueError,'trace wrapper'):self.check(self.corrupt('__wrap_memory_init',14))
        with self.assertRaisesRegex(ValueError,'Power'):self.check(self.corrupt('power_init',0x3c2))
    def test_alink_and_scanner_interrupt_returns_are_pinned(self):
        for name in ('fm1_forge_alink_isr','fm1_forge_keyscan_isr'):
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,'interrupt return'):self.check(self.corrupt(name,self.symbols[name][1]-2))
    def test_clock_selection_and_startup_order_are_pinned(self):
        opposite='sdk-default' if self.manifest.get('forge_clock')=='sdk-320' else 'sdk-320'
        with self.assertRaisesRegex(ValueError,'clock setter/readback'):self.check(factory_clock=opposite)
        off=self.call_offset('app_task_handler','fm1_forge_clock_prepare')
        with self.assertRaisesRegex(ValueError,'clock preparation'):self.check(self.corrupt('app_task_handler',off))
    def test_lcd_uses_owned_packed_wire_rows(self):
        off=self.call_offset('fm1_forge_task','fm1_display_async_wire_rows')
        with self.assertRaisesRegex(ValueError,'packed-wire'):self.check(self.corrupt('fm1_forge_task',off))
    def test_audio_stage_and_confirmed_shutdown_are_pinned(self):
        off=self.call_offset('fm1_forge_audio_output','fm1_audio_startup_process24')
        with self.assertRaisesRegex(ValueError,'startup gain'):self.check(self.corrupt('fm1_forge_audio_output',off))
        off=self.call_offset('fm1_usb_rx_irq','fm1_peripheral_usb_audio_quiesce')
        with self.assertRaisesRegex(ValueError,'DMA teardown'):self.check(self.corrupt('fm1_usb_rx_irq',off))
        if self.manifest['usb_audio']:
            off=self.call_offset('fm1_forge_audio_output','fm1_usb_audio_dac')
            with self.assertRaisesRegex(ValueError,'capture/mix'):self.check(self.corrupt('fm1_forge_audio_output',off))

if __name__=='__main__':unittest.main()
