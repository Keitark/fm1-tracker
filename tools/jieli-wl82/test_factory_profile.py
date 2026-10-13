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
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'firmware/nes'))
import build_stock_image as pack


HELLO='FM1DIAG/1 FM1-FORGE/1 UBOOT=SERIAL COMMIT=BLOCKED\n'
def status(n=1,**changes):
    values=dict(ready=1,running=1,pending=0,frames=n,audio_frames=64*n,scans=100*n,keys='0000000000000000',lcd=0,fault=0,input_errors=0,adc_errors=0)
    values.update(changes)
    return 'FORGE STATUS '+' '.join(f'{k}={v}' for k,v in values.items())+'\n'


class FactoryTests(unittest.TestCase):
    def test_v2_metadata_cannot_bypass_linked_hal_audit(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);base=bytes(0x100000);app=b'factory application'
            (root/'fm1-usb-diag.app.bin').write_bytes(app);(root/'fm1-usb-diag.elf').write_bytes(b'elf')
            manifest=dict(status='LINKED_USB_DIAGNOSTIC_UNTESTED',factory_diag=True,factory_schema=1,profile='FM1_FORGE_FACTORY_DIAG_V1',asset_free=True,external_media_required=False,peripheral_tests=True,usb_audio=True,nes_smb1=False,mdx=False,mod=False,application_sha256=worker.sha(app),source_sha256={},archive_sha256={},vendor_overlay_sources={})
            path=root/'build-manifest.json';selection=dict.fromkeys(('buttons','encoders','volume','lcd','audio'),True)
            rejected=[{'forge_project_schema':2},{'forge_project_schema':2,'forge_hal_access':None},{'forge_project_schema':2,'forge_hal_access':{}},{'forge_project_schema':2,'forge_hal_access':{'buttons':True}},{'forge_project_schema':2,'forge_hal_access':dict(selection,audio=1)},{'forge_project_schema':2,'forge_hal_access':dict(selection,gpio=True)},{'forge_project_schema':1,'forge_hal_access':None},{'forge_project_schema':None},{'forge_project_schema':True},{'forge_project_schema':3}]
            for fields in rejected:
                path.write_text(json.dumps(dict(manifest,**fields)))
                with self.subTest(fields=fields),patch.object(pack,'build_candidate') as build,self.assertRaisesRegex(ValueError,'factory.*(schema|HAL)|schema[23]'):
                    producer.prepare(root,worker.sha(base),base)
                build.assert_not_called()
            for fields in ({},{'forge_project_schema':1},{'forge_project_schema':2,'forge_hal_access':selection},{'forge_project_schema':3,'forge_hal_access':selection},{'forge_project_schema':3,'forge_hal_access':selection,'usb_video':True}):
                path.write_text(json.dumps(dict(manifest,**fields)))
                with self.subTest(fields=fields),patch.object(pack,'build_candidate',return_value=(base,{'candidate_sha256':worker.sha(base)})) as build:
                    result=producer.prepare(root,worker.sha(base),base)
                    self.assertEqual(result['sha256'],worker.sha(base))
                    self.assertEqual(build.call_args.kwargs['factory_project_schema'],fields.get('forge_project_schema',1))
                    self.assertEqual(build.call_args.kwargs['factory_hal_access'],fields.get('forge_hal_access'))
                    self.assertEqual(build.call_args.kwargs['usb_video'],fields.get('usb_video',False))
    def test_direct_packaging_v2_requires_selection_before_elf_audit(self):
        selection=dict.fromkeys(('buttons','encoders','volume','lcd','audio'),True)
        for hal in (None,{},dict(selection,volume='false')):
            with self.subTest(hal=hal),patch.object(pack,'audit') as audit,self.assertRaisesRegex(ValueError,'HAL access'):
                pack.build_candidate(b'',b'',b'',factory_diag=True,factory_project_schema=2,factory_hal_access=hal)
            audit.assert_not_called()
        for version in (None,True,4):
            with self.subTest(version=version),self.assertRaisesRegex(ValueError,'project schema'):
                pack.build_candidate(b'',b'',b'',factory_diag=True,factory_project_schema=version,factory_hal_access=selection)
        for version,hal in ((1,None),(2,selection),(3,selection)):
            with self.subTest(version=version),patch.object(pack,'audit',side_effect=RuntimeError('reached ELF audit')) as audit,self.assertRaisesRegex(RuntimeError,'reached ELF audit'):
                pack.build_candidate(b'',b'',b'',factory_diag=True,factory_project_schema=version,factory_hal_access=hal)
            self.assertEqual(audit.call_args.kwargs['factory_hal_access'],hal)
    def test_uvc_requires_boolean_flag_and_schema3_before_packaging(self):
        selection=dict.fromkeys(('buttons','encoders','volume','lcd','audio'),True)
        for schema,video in ((1,True),(2,True),(3,1),(3,'true')):
            with self.subTest(schema=schema,video=video),patch.object(pack,'audit') as audit,self.assertRaisesRegex(ValueError,'UVC'):
                pack.build_candidate(b'',b'',b'',factory_diag=True,factory_project_schema=schema,factory_hal_access=selection,usb_video=video)
            audit.assert_not_called()
        with patch.object(pack,'audit',side_effect=RuntimeError('reached ELF audit')) as audit,self.assertRaisesRegex(RuntimeError,'reached ELF audit'):
            pack.build_candidate(b'',b'',b'',factory_diag=True,factory_project_schema=3,factory_hal_access=selection,usb_video=True)
        self.assertTrue(audit.call_args.kwargs['usb_video'])
    def test_request_uvc_rejects_invalid_profile_flag_and_keeps_baseline_gate(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);selection=dict.fromkeys(('buttons','encoders','volume','lcd','audio'),True)
            manifest=dict(status='LINKED_USB_DIAGNOSTIC_UNTESTED',factory_diag=True,factory_schema=1,profile='FM1_FORGE_FACTORY_DIAG_V1',asset_free=True,external_media_required=False,peripheral_tests=True,usb_audio=True,nes_smb1=False,mdx=False,mod=False,forge_project_schema=3,forge_hal_access=selection,usb_video=True)
            path=root/'build-manifest.json'
            for changes in ({'usb_video':1},{'usb_video':'true'},{'forge_project_schema':2},{'forge_project_schema':1},{'factory_diag':False,'usb_audio':False}):
                path.write_text(json.dumps(dict(manifest,**changes)))
                with self.subTest(changes=changes),patch.object(pack,'build_candidate') as build,self.assertRaisesRegex(ValueError,'USB video'):
                    producer.prepare(root,'0'*64)
                build.assert_not_called()
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'exact verified'):producer.prepare(root,'0'*64)
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
