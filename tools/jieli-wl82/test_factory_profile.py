"""Factory source/profile/serial contracts. No real USB/flash operations."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import flash_session_worker as worker
import prepare_flash_request as producer
import export_remote_catalog as exporter
import remote_catalog


HELLO='FM1DIAG/1 FM1-FORGE/1 UBOOT=SERIAL COMMIT=BLOCKED\n'
def status(n=1,**changes):
    values=dict(ready=1,running=1,pending=0,frames=n,audio_frames=64*n,scans=100*n,keys='0000000000000000',lcd=0,fault=0,input_errors=0,adc_errors=0)
    values.update(changes)
    return 'FORGE STATUS '+' '.join(f'{k}={v}' for k,v in values.items())+'\n'


class FactoryTests(unittest.TestCase):
    def test_fresh_baseline_profile_and_no_media_are_required(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'user.c';source.write_text('/* source */')
            app=b'factory application';(root/'fm1-usb-diag.app.bin').write_bytes(app);(root/'fm1-usb-diag.elf').write_bytes(b'elf')
            manifest=dict(status='LINKED_USB_DIAGNOSTIC_UNTESTED',factory_diag=True,factory_schema=1,profile='FM1_FORGE_FACTORY_DIAG_V1',asset_free=True,external_media_required=False,peripheral_tests=True,usb_audio=True,nes_smb1=False,mdx=False,mod=False,application_sha256=worker.sha(app),source_sha256={str(source):worker.sha(source.read_bytes())},archive_sha256={},vendor_overlay_sources={})
            path=root/'build-manifest.json';path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'exact verified'):producer.prepare(root,'0'*64)
            for changes in ({'factory_schema':2},{'nes_smb1':True},{'mdx':True},{'mod':True},{'asset_free':False},{'external_media_required':True},{'profile':'MDX_KARAOKE_CANDIDATE'}):
                path.write_text(json.dumps(dict(manifest,**changes)))
                with self.subTest(changes=changes),self.assertRaisesRegex(ValueError,'asset-free'):producer.prepare(root,'0'*64)
            path.write_text(json.dumps(manifest));base=bytes(0x100000)
            source.write_text('changed')
            with self.assertRaisesRegex(ValueError,'Stale source'):producer.prepare(root,worker.sha(base),base)
    def test_catalog_requires_explicit_diagnostics_schema(self):
        m=dict(factory_diag=True,factory_schema=1,profile='FM1_FORGE_FACTORY_DIAG_V1',asset_free=True,external_media_required=False)
        exporter.check_profile('diagnostics',m)
        self.assertIn('diagnostics',remote_catalog.PROFILES)
        with self.assertRaisesRegex(ValueError,'asset-free'):exporter.check_profile('diagnostics',dict(m,mdx=True))
    def test_status_numeric_format_and_progress_rejects_fault_stall_and_duplicates(self):
        first,last=worker.forge_status_progress(status(1)+status(2))
        self.assertEqual((first['frames'],last['frames']),(1,2))
        for text in (status(1)+status(1),status(1)+status(2,lcd=-1),status(1)+status(2,fault=1),status(1)+status(2,input_errors=1),status(1)+status(2,adc_errors=1),status(1)+status(2,running=0),status(1)+status(2,scans=100),status(1).rstrip()+' ready=1\n',status(1,keys='a'),status(1,frames=-1)):
            with self.subTest(text=text),self.assertRaises(ValueError):worker.forge_status_progress(text)
    def test_read_only_status_and_stop_before_uboot(self):
        sent=[]
        class Port:
            def __init__(self,**kwargs):pass
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def open(self):pass
            def write(self,data):sent.append(data)
            def flush(self):pass
        with tempfile.TemporaryDirectory() as folder,patch.dict(sys.modules,{'serial':SimpleNamespace(Serial=Port)}),patch.object(worker,'serial_port',return_value='COM_FAKE'),patch.object(worker.time,'sleep'):
            root=Path(folder)
            with patch.object(worker,'capture',side_effect=['',HELLO]+[status(n) for n in range(1,5)]):
                result=worker.serial_action('serial_status',root)
                self.assertTrue(result['diagnostic_only']);self.assertFalse(result['boot_verified'])
                self.assertEqual(sent,[b'HELLO\n']+[b'FORGE STATUS\n']*4)
            sent.clear()
            with patch.object(worker,'capture',side_effect=['',HELLO]+[status(n) for n in range(1,5)]):
                result=worker.serial_action('observe',root)
                self.assertFalse(result['usb_audio_verified']);self.assertFalse(result['physical_display_audio_input_verified'])
            sent.clear()
            with patch.object(worker,'capture',side_effect=['',HELLO,'OK STOP\n',status(8,running=0),'OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n']),patch.object(worker,'disk',return_value={'DeviceID':'FAKE'}):
                worker.serial_action('enter_uboot',root)
                self.assertEqual(sent,[b'HELLO\n',b'FORGE STOP\n',b'FORGE STATUS\n',b'UBOOT\n',b'UBOOT CONFIRM\n'])

if __name__=='__main__':unittest.main()
