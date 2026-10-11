"""Offline broker tests. No real disk, serial port, loader or flash access."""
import ast
import base64
import contextlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import uuid
from types import SimpleNamespace

import flash_session_worker as worker


def request(base, image, op='plan'):
    return dict(op=op, image=base64.b64encode(image).decode(), sha256=worker.sha(image),
                baseline_sha256=worker.sha(base))


class PolicyTests(unittest.TestCase):
    def test_phy_probe_requires_exact_baseline_and_separate_profile(self):
        import prepare_flash_request as prepare
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); app=b'probe app'; elf=b'probe elf'; source=root/'input.h'
            source.write_bytes(b'header')
            manifest={'status':'LINKED_USB_DIAGNOSTIC_UNTESTED','usb_hs_phy_probe':True,
                'usb_controller_candidate':0,'probe_controller_candidate':1,
                'mod':False,'mdx':False,'nes_smb1':False,'peripheral_tests':False,
                'application_sha256':worker.sha(app),
                'source_sha256':{str(source):worker.sha(source.read_bytes())},
                'archive_sha256':{},'vendor_overlay_sources':{}}
            (root/'fm1-usb-diag.app.bin').write_bytes(app)
            (root/'fm1-usb-diag.elf').write_bytes(elf)
            def save():
                (root/'build-manifest.json').write_text(json.dumps(manifest))
            save(); baseline=bytes(worker.SIZE); image=bytearray(baseline)
            image[0x4000]=1;image[0x4120]=2; calls=[]
            def pack(base,actual_elf,actual_app,**flags):
                self.assertEqual((actual_elf,actual_app),(elf,app));calls.append(flags)
                return bytes(image),{'candidate_sha256':worker.sha(image)}
            fake=SimpleNamespace(V14_BACKUP=source,build_candidate=pack)
            with patch.dict(sys.modules,{'build_stock_image':fake}):
                req=prepare.prepare(root,worker.sha(baseline),baseline)
                self.assertEqual(base64.b64decode(req['image']),bytes(image))
                self.assertTrue(calls[0]['usb_hs_phy_probe'])
                self.assertFalse(calls[0]['usb_peripheral_tests'])
                self.assertFalse(calls[0]['usb_nes'])
                self.assertEqual(calls[0]['rom'],'rom50')
                with self.assertRaisesRegex(ValueError,'exact verified current baseline'):
                    prepare.prepare(root,worker.sha(baseline))
                for field,bad in (('usb_hs_phy_probe','true'),('usb_controller_candidate',1),
                    ('probe_controller_candidate',0),('peripheral_tests',True),
                    ('nes_smb1',True),('mod',True),('mdx',True)):
                    old=manifest[field];manifest[field]=bad;save()
                    with self.assertRaises(ValueError):
                        prepare.prepare(root,worker.sha(baseline),baseline)
                    manifest[field]=old
                save();image[0]=1
                with self.assertRaisesRegex(ValueError,'outside application scope'):
                    prepare.prepare(root,worker.sha(baseline),baseline)
                image[0]=0;source.write_bytes(b'stale')
                with self.assertRaisesRegex(ValueError,'Stale source'):
                    prepare.prepare(root,worker.sha(baseline),baseline)

    def test_mod_request_requires_verified_current_baseline_and_profile(self):
        import prepare_flash_request as prepare
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); app=b'mod app'; elf=b'mod elf'; source=root/'input.h'
            source.write_bytes(b'header')
            manifest={'status':'LINKED_USB_DIAGNOSTIC_UNTESTED','mod':True,
                      'peripheral_tests':True,'nes_smb1':False,'mdx':False,
                      'application_sha256':worker.sha(app),
                      'source_sha256':{str(source):worker.sha(source.read_bytes())},
                      'archive_sha256':{},'vendor_overlay_sources':{}}
            (root/'fm1-usb-diag.app.bin').write_bytes(app)
            (root/'fm1-usb-diag.elf').write_bytes(elf)
            (root/'build-manifest.json').write_text(json.dumps(manifest))
            baseline=bytes(worker.SIZE); calls=[]
            image=bytearray(baseline);image[0x4000]=1;image[0x4120]=2
            def pack(base,actual_elf,actual_app,**flags):
                self.assertEqual((base,actual_elf,actual_app),(source.read_bytes(),elf,app))
                calls.append(flags)
                return bytes(image),{'candidate_sha256':worker.sha(image)}
            fake=SimpleNamespace(V14_BACKUP=source,build_candidate=pack)
            with patch.dict(sys.modules,{'build_stock_image':fake}):
                req=prepare.prepare(root,worker.sha(baseline),baseline)
                self.assertEqual(base64.b64decode(req['image']),bytes(image))
                self.assertTrue(calls[0]['usb_mod'])
                self.assertFalse(calls[0]['usb_nes'])
                self.assertEqual(calls[0]['rom'],'rom50')
                self.assertFalse(calls[0]['usb_uvc_screen'])
                self.assertFalse(calls[0]['usb_uac_capture'])
                self.assertFalse(calls[0]['usb_uvc_ipad_pad'])
                manifest.update(usb_uvc_screen=True,usb_controller_candidate=0,lcd_async=True)
                def save_uvc():
                    (root/'build-manifest.json').write_text(json.dumps(manifest))
                save_uvc()
                prepare.prepare(root,worker.sha(baseline),baseline)
                self.assertTrue(calls[-1]['usb_uvc_screen'])
                manifest.update(usb_uac_capture=True,mod_source_dir='current')
                save_uvc()
                prepare.prepare(root,worker.sha(baseline),baseline)
                self.assertTrue(calls[-1]['usb_uac_capture'])
                manifest['usb_uvc_ipad_pad']=True;save_uvc()
                prepare.prepare(root,worker.sha(baseline),baseline)
                self.assertTrue(calls[-1]['usb_uvc_ipad_pad'])
                for field,bad in (('usb_uvc_ipad_pad',1),('usb_uvc_screen',False)):
                    previous=manifest.get(field,False);manifest[field]=bad;save_uvc()
                    with self.assertRaisesRegex(ValueError,'iPad video'):
                        prepare.prepare(root,worker.sha(baseline),baseline)
                    manifest[field]=previous
                manifest['usb_uvc_ipad_pad']=False;save_uvc()
                for field,bad in (('usb_uac_capture',1),('usb_controller_candidate',1),
                                  ('mod_source_dir',None),('usb_audio',True),('mod',False)):
                    previous=manifest.get(field,False);manifest[field]=bad;save_uvc()
                    with self.assertRaisesRegex(ValueError,'Tracker UAC'):
                        prepare.prepare(root,worker.sha(baseline),baseline)
                    manifest[field]=previous
                manifest['usb_uac_capture']=False;save_uvc()
                for field,bad in (('usb_uvc_screen',1),('usb_controller_candidate',1),
                                  ('lcd_async',False),('usb_hs_phy_probe',True),('mod',False)):
                    previous=manifest.get(field,False);manifest[field]=bad;save_uvc()
                    with self.assertRaisesRegex(ValueError,'Screen UVC'):
                        prepare.prepare(root,worker.sha(baseline),baseline)
                    manifest[field]=previous
                manifest['usb_uvc_screen']=False;save_uvc()
                with self.assertRaisesRegex(ValueError,'exact verified current baseline'):
                    prepare.prepare(root,'0'*64,baseline)
                changed=bytearray(baseline);changed[0]=1
                with self.assertRaisesRegex(ValueError,'outside application scope'):
                    prepare.prepare(root,worker.sha(changed),bytes(changed))
                manifest['peripheral_tests']=False
                (root/'build-manifest.json').write_text(json.dumps(manifest))
                with self.assertRaisesRegex(ValueError,'audited standalone MOD'):
                    prepare.prepare(root,worker.sha(baseline),baseline)

    def test_mod_observation_requires_clock_progress_and_zero_errors(self):
        def status(frames,lcd=0):
            return (f'MOD status ready=1 running=0 dirty=0 export=0 order=0 row=0 '
                    f'ch=0 sample=1 lcd={lcd} keys=0 audio=0 frames={frames} underruns=0')
        first,last=worker.mod_status_progress(status(100)+'\n'+status(200))
        self.assertEqual((first['frames'],last['frames']),(100,200))
        with self.assertRaisesRegex(ValueError,'clock progress'):
            worker.mod_status_progress(status(100)+'\n'+status(100))
        with self.assertRaisesRegex(ValueError,'peripheral error'):
            worker.mod_status_progress(status(100,lcd=1)+'\n'+status(200))

    def test_mdx_request_profile_and_stale_source_guard(self):
        import prepare_flash_request as prepare
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); app=b'mdx app'; elf=b'mdx elf'; source=root/'input.h'
            source.write_bytes(b'header')
            manifest={'status':'LINKED_MDX_KARAOKE_UNTESTED','application_sha256':worker.sha(app),
                      'source_sha256':{str(source):worker.sha(source.read_bytes())},
                      'archive_sha256':{},'vendor_overlay_sources':{}}
            (root/'fm1-mdx.app.bin').write_bytes(app);(root/'fm1-mdx.elf').write_bytes(elf)
            (root/'build-manifest.json').write_text(json.dumps(manifest))
            calls=[]
            def pack(base,actual_elf,actual_app,**flags):
                calls.append(flags);self.assertEqual((actual_elf,actual_app),(elf,app))
                return b'image',{'candidate_sha256':worker.sha(b'image')}
            fake=SimpleNamespace(V14_BACKUP=source,build_candidate=pack)
            with patch.dict(sys.modules,{'build_stock_image':fake}):
                req=prepare.prepare(root,'0'*64)
                self.assertEqual(req['op'],'plan')
                self.assertEqual(base64.b64decode(req['image']),b'image')
                self.assertTrue(calls[0]['usb_mdx']);self.assertFalse(calls[0]['usb_nes'])
                self.assertFalse(calls[0]['usb_audio'])
                manifest['usb_audio']=True
                (root/'build-manifest.json').write_text(json.dumps(manifest))
                prepare.prepare(root,'0'*64)
                self.assertTrue(calls[1]['usb_audio'])
                manifest['usb_audio']='true'
                (root/'build-manifest.json').write_text(json.dumps(manifest))
                with self.assertRaisesRegex(ValueError,'explicit MDX'):prepare.prepare(root,'0'*64)
                manifest['usb_audio']=True
                (root/'build-manifest.json').write_text(json.dumps(manifest))
                source.write_bytes(b'changed header')
                with self.assertRaisesRegex(ValueError,'Stale source'):prepare.prepare(root,'0'*64)
                self.assertEqual(len(calls),2)

    def test_trusted_mode_is_explicit_and_not_a_request_field(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            self.assertFalse(worker.trusted_local(root))
            for value in (False,True):
                (root/'session.json').write_text(json.dumps({'trusted_local':value}))
                self.assertEqual(worker.trusted_local(root),value)
                with patch.object(worker,'trusted_local',return_value=value):
                    self.assertEqual(worker.python_flags(),['-u'] if value else ['-I','-S','-u'])
            (root/'session.json').write_text(json.dumps({'trusted_local':'true'}))
            with self.assertRaises(ValueError):worker.trusted_local(root)
            with self.assertRaises(ValueError):worker.schema({'op':'status','trusted_local':True})

    def test_current_profile_regeneration_reviewed_extent(self):
        import prepare_flash_request
        base=(worker.ROOT/'private-backups/nes-keyspi-dma2-enable-write-20260929-080037/final-2.bin').read_bytes()
        build=worker.ROOT/'firmware/usb-diag/build/nes-smb1-famicom-controls'
        req=prepare_flash_request.prepare(build,worker.sha(base))
        self.assertEqual(req,prepare_flash_request.prepare(build,worker.sha(base)))
        image,sectors=worker.candidate(req,base)
        # ADC-volume tile/direct/no-profiler app remains below0x39000.
        self.assertEqual(sectors,list(range(0x5000,0x39000,4096))+[0x4000])
        metadata=json.loads((build/'build-manifest.json').read_text())
        self.assertTrue(metadata['nes_render_skip'])
        self.assertFalse(metadata['nes_profile'])
        self.assertTrue(metadata['lcd_async'])
        self.assertFalse(metadata['lcd_spi15'])
        self.assertTrue(metadata['lcd_spi30'])
        self.assertTrue(metadata['lcd_rgb444'])
        self.assertTrue(metadata['lcd_direct'])
        self.assertTrue(metadata['nes_tiles'])
        self.assertTrue(metadata['nes_volume'])
        end=worker.APP_START+metadata['application_bytes']
        self.assertEqual(image[end:],base[end:])
        self.assertEqual(image[:0x4000],base[:0x4000])

    def test_schema_forbids_commands_and_paths(self):
        for req in ({'op':'shell'}, {'op':'status','path':'C:\\'}, {'op':'reset','arg':0},
                    {'op':'flash','command':'erase'}, [], None):
            with self.assertRaises(ValueError): worker.schema(req)

    def test_scope_and_stale_baseline(self):
        base=bytes(worker.SIZE)
        image=bytearray(base); image[0x4000]=1; image[worker.APP_END-1]=1
        good=request(base,image)
        self.assertEqual(worker.candidate(good,base)[1],[0x92000,0x4000])
        for address in (0,0x3000,0x4040,0x411f,worker.APP_END,0x93000,0xee000,0xfffff):
            bad=bytearray(image); bad[address]=1
            with self.assertRaises(ValueError): worker.candidate(request(base,bad),base)
        for field,value in (('sha256','0'*64),('baseline_sha256','0'*64),('image','!')):
            bad=dict(good);bad[field]=value
            with self.assertRaises(ValueError): worker.candidate(bad,base)
        with self.assertRaises(ValueError): worker.candidate(request(base,base),base)

    def test_plan_no_device_io_and_failure_latch(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);base=bytes(worker.SIZE);image=bytearray(base);image[0x4000]=1
            (root/'baseline.bin').write_bytes(base)
            state=dict(baseline_file='baseline.bin',baseline_sha256=worker.sha(base),blocked=False,reset_pending=False)
            (root/'state.json').write_text(json.dumps(state))
            session=worker.Session(root)
            with patch.object(worker,'disk',side_effect=AssertionError('No hardware')):
                self.assertFalse(session.handle(request(base,image))['device_io'])
                with self.assertRaises(ValueError):session.handle({'op':'reset'})
                with self.assertRaises(AssertionError):session.handle(request(base,image,'flash'))
            self.assertTrue(worker.Session(root).state['blocked'])
            with patch.object(worker,'disk',side_effect=AssertionError('No retry')):
                with self.assertRaises(ValueError):worker.Session(root).handle(request(base,image,'flash'))

    def test_full_cycle_reuses_writer_and_updates_baseline(self):
        import deploy_v14_smb1_via_cli as deploy
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);base=bytes(worker.SIZE);image=bytearray(base)
            image[0x4000]=1;image[worker.APP_END-1]=2;image=bytes(image)
            (root/'baseline.bin').write_bytes(base)
            (root/'state.json').write_text(json.dumps(dict(baseline_file='baseline.bin',baseline_sha256=worker.sha(base),blocked=False,reset_pending=False)))
            memory=bytearray(base); writes=[]; modes=[]
            target={'DeviceID':'fake','PNPDeviceID':'fake'}
            def cli(device,commands,log,mode='write'):
                modes.append(mode)
                if mode!='write':return
                for command in commands:
                    bits=command.split(' ',3)
                    if bits[0]=='write':
                        addr=int(bits[1]);memory[addr:addr+4096]=Path(' '.join(bits[2:])).read_bytes();writes.append(addr)
                    else:
                        addr,size=int(bits[1]),int(bits[2]);Path(bits[3]).write_bytes(memory[addr:addr+size])
            with patch.object(worker,'disk',return_value=target),patch.object(worker,'execute_cli',side_effect=cli),contextlib.redirect_stdout(io.StringIO()):
                session=worker.Session(root)
                session.handle(request(base,image,'flash'))
                self.assertEqual(writes,[0x92000,0x4000])
                self.assertEqual(session.state['baseline_sha256'],worker.sha(image))
                self.assertTrue(session.state['baseline_file'].endswith('final-1.bin'))
                self.assertFalse((root/session.state['baseline_file']).with_name('final-2.bin').exists())
                self.assertTrue(session.state['reset_pending'])
                session.handle({'op':'reset'})
                self.assertEqual(modes.count('load'),1);self.assertEqual(modes.count('reset'),1)
                with self.assertRaises(ValueError):session.handle({'op':'reset'})
                with patch.object(worker,'serial_action',return_value={'serial_hello_verified':True}):
                    session.handle({'op':'observe'})

    def test_ambiguous_reset_latches_and_is_not_repeated(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);target={'DeviceID':'fake','PNPDeviceID':'fake'}
            (root/'state.json').write_text(json.dumps(dict(blocked=False,reset_pending=True,target=target)))
            with patch.object(worker,'disk',return_value=target),patch.object(worker,'execute_cli',side_effect=OSError('disconnect')) as cli:
                with self.assertRaises(OSError):worker.Session(root).handle({'op':'reset'})
                with self.assertRaises(ValueError):worker.Session(root).handle({'op':'reset'})
                self.assertEqual(cli.call_count,1)
            self.assertTrue(worker.Session(root).state['blocked'])

    def test_observation_can_resolve_reset_but_never_failed_flash(self):
        for previous,blocked in (('reset',False),('flash',True)):
            with self.subTest(previous=previous), tempfile.TemporaryDirectory() as folder:
                root=Path(folder)
                (root/'state.json').write_text(json.dumps(dict(blocked=True,failed_operation=previous,reset_pending=False)))
                with patch.object(worker,'serial_action',return_value={'serial_hello_verified':True,'frames_first':1,'frames_last':2}):
                    worker.Session(root).handle({'op':'observe'})
                self.assertEqual(worker.Session(root).state['blocked'],blocked)

    def test_serial_commands_and_frozen_frame_detection(self):
        sent=[]
        class Port:
            def __init__(self,**kwargs):pass
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def open(self):pass
            def write(self,data):sent.append(data)
            def flush(self):pass
        hello='FM1DIAG/1 NES=SMB1\n'
        with tempfile.TemporaryDirectory() as folder,patch.dict(sys.modules,{'serial':SimpleNamespace(Serial=Port)}),patch.object(worker,'serial_port',return_value='COM_FAKE'),patch.object(worker.time,'sleep'):
            root=Path(folder)
            for profile in ('SMB1','INES'):
                sent.clear()
                with patch.object(worker,'capture',side_effect=['',f'FM1DIAG/1 NES={profile}\n','NES frames=1 fault=0\nNES frames=2 fault=0\n']):
                    result=worker.serial_action('observe',root)
                    self.assertEqual(result['frames_last'],2)
                    self.assertEqual(sent,[b'HELLO\n',b'TEST STATUS\n'])
            sent.clear()
            with patch.object(worker,'capture',side_effect=['',hello,'NES frames=1 fault=0\nNES frames=1 fault=0\n']):
                with self.assertRaisesRegex(ValueError,'progress'):worker.serial_action('observe',root)
            sent.clear()
            for profile in ('SMB1','INES'):
                sent.clear()
                with patch.object(worker,'capture',side_effect=['',f'FM1DIAG/1 NES={profile}\n','TEST END\n','TEST STATUS busy=0\n','OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n']),patch.object(worker,'disk',return_value={'DeviceID':'fake'}):
                    worker.serial_action('enter_uboot',root)
                    self.assertEqual(sent,[b'HELLO\n',b'TEST STOP\n',b'TEST STATUS\n',b'UBOOT\n',b'UBOOT CONFIRM\n'])
            with patch.object(worker,'capture',side_effect=['']+['FM1DIAG/1 NES=UNKNOWN\n']*3):
                with self.assertRaisesRegex(ValueError,'HELLO not confirmed'):worker.serial_action('observe',root)

    def test_mdx_serial_observation_requires_progress_and_clean_peripherals(self):
        sent=[]
        class Port:
            def __init__(self,**kwargs):pass
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def open(self):pass
            def write(self,data):sent.append(data)
            def flush(self):pass
        hello='FM1DIAG/1 MDX-KARAOKE/1 RAM-UPLOAD UBOOT=SERIAL COMMIT=BLOCKED\n'
        def status(frame=1,error=0,lcd=0):
            return f'MDX running=1 pending=0 frames={frame} underruns=0 error={error} lcd={lcd} keys=0 audio=0\n'
        with tempfile.TemporaryDirectory() as folder,patch.dict(sys.modules,{'serial':SimpleNamespace(Serial=Port)}),patch.object(worker,'serial_port',return_value='COM_FAKE'):
            root=Path(folder)
            with patch.object(worker,'capture',side_effect=['',hello]+[status(n) for n in range(1,7)]):
                result=worker.serial_action('observe',root)
                self.assertEqual(result['profile'],'MDX-KARAOKE/1')
                self.assertEqual(result['frames_last'],6)
                self.assertFalse(result['physical_display_audio_verified'])
                self.assertEqual(sent,[b'HELLO\n']+[b'MDX STATUS\n']*6)
            for responses,message in (([status()]*6,'progress'),([status(n,error=1) for n in range(6)],'error'),([status(n,lcd=-1) for n in range(6)],'error'),(['MDX running=1 frames=1\n']*6,'Incomplete')):
                with patch.object(worker,'capture',side_effect=['',hello]+responses):
                    with self.assertRaisesRegex(ValueError,message):worker.serial_action('observe',root)
            sent.clear()
            with patch.object(worker,'capture',side_effect=['',hello,'OK MDX QUEUED\n','MDX running=0 pending=0\n','ERR UBOOT_RETRY_AFTER_TEST_END\n','','OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n']),patch.object(worker,'disk',return_value={'DeviceID':'fake'}),patch.object(worker.time,'sleep'):
                worker.serial_action('enter_uboot',root)
                self.assertEqual(sent,[b'HELLO\n',b'MDX STOP\n',b'MDX STATUS\n',b'UBOOT\n',b'UBOOT\n',b'UBOOT CONFIRM\n'])

    def test_doom_serial_observation_and_stop_before_uboot(self):
        sent=[]
        class Port:
            def __init__(self,**kwargs):pass
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def open(self):pass
            def write(self,data):sent.append(data)
            def flush(self):pass
        hello='FM1DIAG/1 DOOM-FM1/1 UBOOT=SERIAL COMMIT=BLOCKED\n'
        def status(frame=1,stage=4,fault=0,lcd=0,keys=0,stopped=0):
            return (f'DOOM STATUS stage={stage} fault={fault} frames={frame} stopped={stopped} '
                    f'lcd_stage=4 lcd_error={lcd} key_error={keys} sys_hz=240000000 '
                    'lsb_hz=60000000 error=\n')
        with tempfile.TemporaryDirectory() as folder,patch.dict(sys.modules,{'serial':SimpleNamespace(Serial=Port)}),patch.object(worker,'serial_port',return_value='COM_FAKE'),patch.object(worker.time,'sleep'):
            root=Path(folder)
            with patch.object(worker,'capture',side_effect=['',hello,status(0,stage=3)]+[status(n,keys=-3) for n in range(1,4)]):
                result=worker.serial_action('observe',root)
                self.assertEqual(result['profile'],'DOOM-FM1/1')
                self.assertEqual((result['frames_first'],result['frames_last']),(1,3))
                self.assertEqual(result['key_error_last'],-3)
                self.assertTrue(result['key_error_reported'])
                self.assertFalse(result['physical_display_keys_verified'])
                self.assertEqual(sent,[b'HELLO\n']+[b'DOOM STATUS\n']*4)
            for responses,message in (([status()]*4,'progress'),
                                      ([status(n,stage=3) for n in range(4)],'not running'),
                                      ([status(n,fault=-30) for n in range(4)],'error'),
                                      ([status(n,lcd=-20) for n in range(4)],'error'),
                                      ([status(n,stopped=1) for n in range(4)],'not running'),
                                      (['DOOM STATUS stage=4 frames=1\n']*4,'Incomplete')):
                with self.subTest(message=message),patch.object(worker,'capture',side_effect=['',hello]+responses):
                    with self.assertRaisesRegex(ValueError,message):worker.serial_action('observe',root)
            sent.clear()
            with patch.object(worker,'capture',side_effect=['',hello,'OK DOOM STOP REQUESTED\n',
                              status(40),status(40,stage=255,fault=-30,stopped=1),
                              'OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n']),patch.object(worker,'disk',return_value={'DeviceID':'fake'}):
                worker.serial_action('enter_uboot',root)
                self.assertEqual(sent,[b'HELLO\n',b'DOOM STOP\n',b'DOOM STATUS\n',b'DOOM STATUS\n',b'UBOOT\n',b'UBOOT CONFIRM\n'])
            sent.clear()
            with patch.object(worker,'capture',side_effect=['',hello,'OK DOOM STOP REQUESTED\n']+[status()]*6):
                with self.assertRaisesRegex(ValueError,'not confirmed stopped'):worker.serial_action('enter_uboot',root)
                self.assertNotIn(b'UBOOT\n',sent)
            with patch.object(worker,'capture',side_effect=['']+['FM1DIAG/1 DOOM-FM1/1 UBOOT=SERIAL COMMIT=ALLOWED\n']*3):
                with self.assertRaisesRegex(ValueError,'HELLO not confirmed'):worker.serial_action('observe',root)
            with patch.object(worker,'capture',side_effect=['']+[status().replace('error=\n','error='+hello)]*3):
                with self.assertRaisesRegex(ValueError,'HELLO not confirmed'):worker.serial_action('observe',root)

    def test_doom_status_does_not_parse_diagnostic_message_as_fields(self):
        line=('DOOM STATUS stage=4 fault=0 frames=10 stopped=0 lcd_stage=4 lcd_error=0 '
              'key_error=0 sys_hz=240000000 lsb_hz=60000000 error=panic stage=255 fault=-30 stopped=1\n')
        values=worker.doom_status_values(line)
        self.assertEqual((values[0]['stage'],values[0]['fault'],values[0]['stopped']),(4,0,0))
        self.assertEqual(worker.doom_status_values(line.rstrip('\n')),[])
        with self.assertRaisesRegex(ValueError,'Incomplete'):
            worker.doom_status_values(line.replace(' error=',' stage=255 error='))
        with self.assertRaisesRegex(ValueError,'Incomplete'):
            worker.doom_status_values(line.replace('stopped=0 lcd_stage=','stopped=1.5 lcd_stage='))

    def test_loader_reset_cli_grammar_before_device_open(self):
        tree=ast.parse(worker.CLI.read_text())
        # Only argparse definitions and preflight policy; no imports/USB access.
        start=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='ap' for t in n.targets))
        end=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='scriptroot' for t in n.targets))
        code=compile(ast.Module(body=tree.body[start:end],type_ignores=[]),'cli-arguments','exec')
        import argparse
        common=['tool','--device','fake','--chip','wl82','--io-size','256']
        cases=[(['--fm1-load','help'],True),(['--fm1-reset','--reuse-loader','reset 1'],True),
               (['--fm1-load','write 0 evil'],False),(['--fm1-reset','--reuse-loader','reset 0'],False),
               (['--fm1-reset','reset 1'],False),(['--fm1-load','--reuse-loader','help'],False),
               (['--fm1-load','--arg-target','0','help'],False)]
        for args,allowed in cases:
            with self.subTest(args=args),patch.object(sys,'argv',common+args),contextlib.redirect_stderr(io.StringIO()):
                if allowed:exec(code,{'argparse':argparse,'anyint':lambda x:int(x,0)})
                else:
                    with self.assertRaises(SystemExit):exec(code,{'argparse':argparse,'anyint':lambda x:int(x,0)})


class HsPhySerialTests(unittest.TestCase):
    @staticmethod
    def report(status=4, **changes):
        defaults = {
            1: dict(ready=0,polls=20000,reset=0,hs=0,power=0,samples=0,ms=0),
            2: dict(ready=1,polls=1,reset=0,hs=0,power=0x60,samples=500,ms=5010),
            3: dict(ready=1,polls=1,reset=1,hs=0,power=0x60,samples=500,ms=5010),
            4: dict(ready=1,polls=1,reset=1,hs=1,power=0x70,samples=500,ms=5010),
        }
        values = dict(defaults.get(status, defaults[4]), status=status)
        values.update(changes)
        return ('USB PHY status={status} ready={ready} polls={polls} reset={reset} '
                'hs={hs} power={power:02x} samples={samples} ms={ms} USB0-RESTORED\n').format(**values)

    @staticmethod
    def banner(uptime, stage=3):
        return (f'# FM1DIAG/1 profile=usb1-phy-probe/1 stage={stage} trace=000001ff '
                f'uptime={uptime} boot=serial-uboot update=verify-only commit=blocked\n')

    def test_completed_probe_statuses_and_tick_overshoot(self):
        for status in (1,2,3,4):
            values=worker.hs_phy_status_values(self.report(status))
            self.assertEqual(values[0]['status'],status)
        self.assertEqual(worker.hs_phy_status_values(self.report(ms=5100))[0]['ms'],5100)
        # A first time sample already beyond the negotiation deadline can
        # legitimately leave zero MMIO samples in the NO_RESET report.
        worker.hs_phy_status_values(self.report(2,samples=0,ms=5000,power=0))

    def test_malformed_truncated_duplicate_and_out_of_bound_reports(self):
        line=self.report()
        malformed=[line.rstrip('\n'),line.replace(' USB0-RESTORED',''),
            line.replace('ready=1','ready=1 ready=1'),line.replace('power=70','power=070'),
            line.replace('polls=1','polls=1.5'),line.replace('samples=500','samples=-1'),
            line.replace('ms=5010','ms=9999999999999999999999999')]
        for changes in ({'status':0},{'status':5},{'ready':2},{'polls':0},{'polls':20001},
                        {'reset':2},{'hs':2},{'samples':501},{'ms':5101}):
            malformed.append(self.report(**changes))
        for report in malformed:
            with self.subTest(report=report),self.assertRaises(ValueError):
                worker.hs_phy_status_values(report)
        with self.assertRaisesRegex(ValueError,'exceeded bound'):
            worker.hs_phy_status_values(line+'x'*65536)

    def test_contradictory_fields_and_immutable_reports_are_rejected(self):
        cases=[(1,{'ready':1}),(1,{'polls':19999}),(1,{'reset':1}),
               (2,{'ready':0}),(2,{'reset':1}),(2,{'hs':1}),
               (2,{'samples':0,'ms':0}),(3,{'reset':0}),(3,{'hs':1}),(3,{'power':0x70}),
               (4,{'reset':0}),(4,{'hs':0}),(4,{'power':0x60}),(4,{'samples':0})]
        for status,changes in cases:
            with self.subTest(status=status,changes=changes),self.assertRaisesRegex(ValueError,'Contradictory'):
                worker.hs_phy_status_values(self.report(status,**changes))
        with self.assertRaisesRegex(ValueError,'immutable'):
            worker.hs_phy_status_values(self.report()+self.report(polls=2))
        self.assertEqual(len(worker.hs_phy_status_values(self.report()*4)),4)

    def test_observation_requires_complete_stage3_and_advancing_uptime(self):
        good=self.report()+self.banner(6000)+self.banner(7000)
        report,first,last=worker.hs_phy_status_progress(good)
        self.assertEqual((report['status'],first['uptime'],last['uptime']),(4,6000,7000))
        cases=[self.report(),self.report()+self.banner(6000),
               self.report()+self.banner(6000)+self.banner(6000),
               self.report()+self.banner(6000)+self.banner(5000),
               self.report()+self.banner(6000,stage=2)+self.banner(7000),
               self.report()+self.banner(6000)+self.banner(7000).rstrip('\n'),
               self.report()+self.banner(6000)+self.banner(7000).replace('stage=3','stage=3 stage=3'),
               self.report()+self.banner(6000)+self.banner(0x100000000),
               good+'FAULT runtime\n',good+'fault runtime\n']
        for transcript in cases:
            with self.subTest(transcript=transcript),self.assertRaises(ValueError):
                worker.hs_phy_status_progress(transcript)

    def test_serial_ops_use_exact_profile_identity_and_phy_commands(self):
        sent=[]
        class Port:
            def __init__(self,**kwargs):pass
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def open(self):pass
            def write(self,data):sent.append(data)
            def flush(self):pass
        hello=worker.HS_PHY_HELLO+'\n'
        with tempfile.TemporaryDirectory() as folder,patch.dict(sys.modules,{'serial':SimpleNamespace(Serial=Port)}),patch.object(worker,'serial_port',return_value='COM_FAKE'),patch.object(worker.time,'sleep'):
            root=Path(folder)
            responses=[self.report()+self.banner(n) for n in (6000,7000,8000,9000)]
            with patch.object(worker,'capture',side_effect=['',hello]+responses):
                result=worker.serial_action('observe',root)
                self.assertEqual(result['profile'],'USB1-PHY-PROBE/1')
                self.assertTrue(result['usb0_restored_verified'])
                self.assertTrue(result['high_speed_phy_negotiated'])
                self.assertFalse(result['high_speed_enumeration_verified'])
                self.assertEqual(sent,[b'HELLO\n']+[b'USB PHY STATUS\n']*4)
            sent.clear()
            with patch.object(worker,'capture',side_effect=['',hello]+[self.report(2)]*4):
                result=worker.serial_action('serial_status',root)
                self.assertFalse(result['boot_verified'])
                self.assertFalse(result['high_speed_phy_negotiated'])
                self.assertFalse(result['high_speed_enumeration_verified'])
                self.assertFalse(result['usb0_restored_verified'])
                self.assertEqual(result['status_command'],'USB PHY STATUS')
                self.assertEqual(sent,[b'HELLO\n']+[b'USB PHY STATUS\n']*4)
            sent.clear()
            with patch.object(worker,'capture',side_effect=['',hello,'OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n']),patch.object(worker,'disk',return_value={'DeviceID':'fake'}):
                worker.serial_action('enter_uboot',root)
                self.assertEqual(sent,[b'HELLO\n',b'UBOOT\n',b'UBOOT CONFIRM\n'])
            bad_hellos=[hello.rstrip('\n'),hello.replace('COMMIT=BLOCKED','COMMIT=ALLOWED'),
                hello.replace('USB0=CDC','USB0=OTHER'),'error='+hello,' '+hello,
                hello.replace('\n',' NES=SMB1\n'),'FM1DIAG/1 USB-ONLY UBOOT=SERIAL COMMIT=BLOCKED\n',
                self.banner(6000)]
            for op in ('observe','serial_status','enter_uboot'):
                for bad in bad_hellos:
                    sent.clear()
                    with self.subTest(op=op,hello=bad),patch.object(worker,'capture',side_effect=['']+[bad]*3):
                        with self.assertRaisesRegex(ValueError,'HELLO not confirmed'):
                            worker.serial_action(op,root)
                        self.assertEqual(sent,[b'HELLO\n']*3)


class FlashFreshPreflightTests(unittest.TestCase):
    def fresh_attempt(self,current,changed_identity=False):
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
        root=Path(folder.name);base=bytes(worker.SIZE);image=bytearray(base);image[0x4000]=1
        (root/'baseline.bin').write_bytes(base)
        (root/'state.json').write_text(json.dumps(dict(baseline_file='baseline.bin',
            baseline_sha256=worker.sha(base),blocked=False,reset_pending=False)))
        helper=root/'tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin'
        helper.parent.mkdir(parents=True);helper.write_bytes(b'test pinned helper')
        target={'DeviceID':'fake-drive','PNPDeviceID':'fake-usb'};commands=[]
        def cli(device,actual,log,mode='write'):
            commands.append((mode,actual))
            if mode=='load':return
            self.assertEqual(len(actual),1)
            parts=actual[0].split(' ',3)
            self.assertEqual(parts[:3],['read','0',str(worker.SIZE)])
            Path(parts[3]).write_bytes(current)
        writer=SimpleNamespace(deploy=unittest.mock.Mock())
        def deploy(device,run,actual_base,actual_image,**options):
            path=options['verified_preflight']
            self.assertEqual(path,run/'preflight-current.bin')
            self.assertEqual(path.read_bytes(),actual_base)
            self.assertEqual(actual_base,base)
            self.assertTrue(options['nes_cdc'])
            (run/'final-1.bin').write_bytes(actual_image)
            return {'status':'written_and_readback_verified','full_readback_count':1}
        writer.deploy.side_effect=deploy
        identities=[target,target,dict(target,DeviceID='different-drive')] if changed_identity else [target]*3
        with patch.object(worker,'ROOT',root),patch.object(worker,'HELPER_SHA',worker.sha(helper.read_bytes())),patch.object(worker,'disk',side_effect=identities),patch.object(worker,'execute_cli',side_effect=cli),patch.dict(sys.modules,{'deploy_v14_smb1_via_cli':writer}):
            if current!=base or changed_identity:
                with self.assertRaisesRegex(ValueError,'differs from protected baseline|identity changed'):
                    worker.Session(root).handle(request(base,image,'flash'))
                writer.deploy.assert_not_called()
                self.assertTrue(worker.Session(root).state['blocked'])
            else:
                session=worker.Session(root)
                session.handle(request(base,image,'flash'))
                writer.deploy.assert_called_once()
                self.assertEqual(session.state['flash_attempt']['fresh_preflight']['sha256'],worker.sha(base))
                self.assertTrue(session.state['reset_pending'])
        self.assertEqual(commands[0],('load',['help']))
        self.assertEqual(len(commands),2)
        self.assertTrue(commands[1][1][0].startswith(f'read 0 {worker.SIZE} '))
        self.assertFalse(any(command.startswith(('write ','erase ')) for _,group in commands for command in group))

    def test_exact_fresh_baseline_proceeds_with_checked_read_path(self):
        self.fresh_attempt(bytes(worker.SIZE))

    def test_mismatch_short_read_and_changed_identity_never_reach_writer(self):
        changed=bytearray(worker.SIZE);changed[0xee000]=1
        for current in (bytes(changed),bytes(worker.SIZE-1)):
            with self.subTest(size=len(current)):
                self.fresh_attempt(current)
        self.fresh_attempt(bytes(worker.SIZE),changed_identity=True)


class CliLogTests(unittest.TestCase):
    def run_cli(self, output, log, mode='reset', returncode=0):
        # Run a real, harmless child process so redirected output is written as
        # bytes, just as it is by the CLI; never import its USB implementation.
        actual_run = subprocess.run
        script = f'import os, sys; os.write(1, {output!r}); sys.exit({returncode})'
        def child(args, **kwargs):
            self.assertEqual(kwargs['check'], True)
            return actual_run([sys.executable, '-I', '-S', '-c', script], **kwargs)
        with patch.object(worker, 'python_flags', return_value=['-I', '-S', '-u']), \
             patch.object(worker.subprocess, 'run', side_effect=child):
            worker.execute_cli('fake-device', ['reset 1'], log, mode)

    def test_reset_accepts_non_utf8_output_and_preserves_raw_log(self):
        output = b'Resetting device\r\n\x83\x65\x83\x58\x83\x67\r\n'
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'reset.log'
            self.run_cli(output, log)
            self.assertEqual(log.read_bytes(), output)

    def test_reset_failed_exit_is_rejected_with_raw_log_preserved(self):
        output = b'Resetting device\r\n\x83\x65\x83\x58\x83\x67\r\n'
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'reset.log'
            with self.assertRaises(subprocess.CalledProcessError) as caught:
                self.run_cli(output, log, returncode=7)
            self.assertEqual(caught.exception.returncode, 7)
            self.assertEqual(log.read_bytes(), output)

    def test_reset_error_markers_are_rejected_with_non_utf8_output(self):
        for marker in (b'Key mismatch', b'ERROR'):
            with self.subTest(marker=marker), tempfile.TemporaryDirectory() as folder:
                log = Path(folder) / 'reset.log'
                with self.assertRaisesRegex(ValueError, 'CLI reported an error'):
                    self.run_cli(b'\x83\x65 ' + marker + b'\r\n', log)

    def test_nonreset_decoding_remains_strict(self):
        for mode in ('write', 'load'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as folder:
                with self.assertRaises(UnicodeDecodeError):
                    self.run_cli(b'\x83\x65\r\n', Path(folder) / 'cli.log', mode)


class RetryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base = bytes(worker.SIZE)
        image = bytearray(self.base)
        image[0x4000] = 1; image[0x5000] = 2; image[0x6000] = 3
        self.image = bytes(image)
        (self.root/'baseline.bin').write_bytes(self.base)
        (self.root/'state.json').write_text(json.dumps(dict(baseline_file='baseline.bin',
            baseline_sha256=worker.sha(self.base),blocked=False,reset_pending=False,
            reset_resolved_by_observation=True)))
        self.target = {'DeviceID':'fake-drive-3','PNPDeviceID':'same-usb-target'}
        self.memory = bytearray(self.base)
        self.writes = []; self.reads = []; self.modes = []; self.fail = True
        self.fail_address = 0x5000

    def cli(self, device, commands, log, mode='write'):
        self.modes.append(mode)
        if mode != 'write': return
        for command in commands:
            bits = command.split(' ',3)
            addr = int(bits[1])
            if bits[0] == 'write':
                self.writes.append(addr)
                self.memory[addr:addr+4096] = b'\xff'*4096
                if self.fail and addr == self.fail_address:
                    self.memory[addr:addr+256] = Path(' '.join(bits[2:])).read_bytes()[:256]
                    raise OSError('simulated partial write / USB disconnect')
                self.memory[addr:addr+4096] = Path(' '.join(bits[2:])).read_bytes()
            else:
                size = int(bits[2]); self.reads.append((addr,size))
                Path(bits[3]).write_bytes(self.memory[addr:addr+size])

    def run_request(self, op, image=None):
        with patch.object(worker,'disk',return_value=self.target), \
             patch.object(worker,'execute_cli',side_effect=self.cli), \
             contextlib.redirect_stdout(io.StringIO()):
            return worker.Session(self.root).handle(request(self.base,self.image if image is None else image,op))

    def failed_flash(self):
        with self.assertRaisesRegex(OSError,'partial write'): self.run_request('flash')
        state = worker.Session(self.root).state
        self.assertTrue(state['blocked'])
        self.assertEqual(state['baseline_sha256'],worker.sha(self.base))
        self.assertFalse(state['reset_pending'])
        self.assertNotIn('reset_resolved_by_observation',state)
        return self.root/state['flash_attempt']['run']

    def test_explicit_retry_rewrites_full_scope_preserves_failure_and_verifies_once(self):
        failed = self.failed_flash()
        evidence = {p.name:p.read_bytes() for p in failed.iterdir()}
        self.fail = False
        # Replug may renumber the disk, but PNP identity must remain unchanged.
        self.target['DeviceID'] = 'fake-drive-4'
        self.run_request('retry_flash')
        state = worker.Session(self.root).state
        self.assertEqual(self.writes,[0x5000,0x5000,0x6000,0x4000])
        self.assertEqual(self.reads,[(0,worker.SIZE),(0x5000,4096),(0x6000,4096),(0x4000,4096),(0,worker.SIZE)])
        self.assertEqual(self.modes.count('load'),2)
        self.assertNotIn('reset',self.modes)
        self.assertEqual(bytes(self.memory),self.image)
        self.assertFalse(state['blocked']); self.assertTrue(state['reset_pending'])
        self.assertTrue(state['needs_observation'])
        self.assertNotIn('failed_operation',state); self.assertNotIn('error',state)
        self.assertEqual(state['flash_attempt']['retry_of'],str(failed.relative_to(self.root)))
        self.assertEqual(state['baseline_sha256'],worker.sha(self.image))
        for name,data in evidence.items(): self.assertEqual((failed/name).read_bytes(),data)
        receipt = json.loads((self.root/state['flash_attempt']['run']/'deployment.json').read_text())
        self.assertEqual(receipt['full_readback_count'],1)

    def test_retry_again_failure_remains_latched_without_automatic_loop(self):
        failed = self.failed_flash()
        with self.assertRaises(OSError): self.run_request('retry_flash')
        state = worker.Session(self.root).state
        self.assertTrue(state['blocked']); self.assertFalse(state['reset_pending'])
        self.assertEqual(state['failed_operation'],'flash')
        self.assertEqual(state['flash_attempt']['status'],'failed')
        self.assertEqual(state['flash_attempt']['retry_of'],str(failed.relative_to(self.root)))
        self.assertEqual(self.writes,[0x5000,0x5000])
        self.assertEqual(state['baseline_sha256'],worker.sha(self.base))
        with self.assertRaises(ValueError): worker.Session(self.root).handle({'op':'reset'})
        self.fail = False
        self.run_request('retry_flash')
        self.assertFalse(worker.Session(self.root).state['blocked'])

    def test_retry_rewrites_previously_verified_sectors_too(self):
        self.fail_address = 0x6000
        failed = self.failed_flash()
        receipt = json.loads((failed/'deployment.json').read_text())
        self.assertEqual(receipt['verified_sectors'],['0x5000'])
        self.fail = False
        self.run_request('retry_flash')
        self.assertEqual(self.writes,[0x5000,0x6000,0x5000,0x6000,0x4000])
        self.assertEqual(self.reads.count((0,worker.SIZE)),2)  # Initial flash preflight + retry final verification.

    def test_retry_rejects_changed_candidate_regular_flash_and_changed_identity(self):
        self.failed_flash()
        modified = bytearray(self.image); modified[0x5001] = 42
        with self.assertRaisesRegex(ValueError,'exact failed candidate'):
            self.run_request('retry_flash',modified)
        with self.assertRaisesRegex(ValueError,'latched'): self.run_request('flash')
        self.target['PNPDeviceID'] = 'different-usb-device'
        with self.assertRaisesRegex(ValueError,'identity changed'): self.run_request('retry_flash')
        self.assertEqual(self.writes,[0x5000])
        self.assertEqual(self.modes,['load','write','write'])  # One helper, fresh read, failed write.
        self.assertTrue(worker.Session(self.root).state['blocked'])

    def test_retry_requires_completed_failure_and_rejects_pending_or_reset_failure(self):
        with self.assertRaisesRegex(ValueError,'completed failed flash'): self.run_request('retry_flash')
        self.failed_flash()
        original = worker.Session(self.root).state
        for field,value in [('status','pending'),('status','verified'),('target',None)]:
            state = json.loads(json.dumps(original)); state['flash_attempt'][field] = value
            (self.root/'state.json').write_text(json.dumps(state))
            with self.assertRaisesRegex(ValueError,'completed failed flash'): self.run_request('retry_flash')
        original['failed_operation'] = 'reset'
        (self.root/'state.json').write_text(json.dumps(original))
        with self.assertRaisesRegex(ValueError,'completed failed flash'): self.run_request('retry_flash')
        self.assertEqual(self.writes,[0x5000])

    def test_failed_final_readback_keeps_baseline_and_reset_closed(self):
        self.failed_flash(); self.fail = False
        # Outside write scope, emulating a final read mismatch.
        self.memory[0xee000] = 9
        with self.assertRaisesRegex(ValueError,'Final full read mismatch'): self.run_request('retry_flash')
        state = worker.Session(self.root).state
        self.assertTrue(state['blocked']); self.assertFalse(state['reset_pending'])
        self.assertEqual(state['baseline_sha256'],worker.sha(self.base))
        self.assertEqual(self.reads.count((0,worker.SIZE)),2)  # Initial flash preflight + rejected final read.

    def legacy_evidence(self):
        failed = self.failed_flash()
        state = worker.Session(self.root).state
        state.pop('flash_attempt')
        (self.root/'state.json').write_text(json.dumps(state))
        (self.root/'ipc').mkdir()
        (self.root/'ipc/failed.json').write_text(json.dumps(request(self.base,self.image,'flash')))
        return failed

    def test_legacy_migration_preserves_latch_and_can_retry_exact_image(self):
        failed = self.legacy_evidence()
        with patch.object(worker,'disk',side_effect=AssertionError('Offline only')):
            plan = worker.resume_failed_session(self.root)
        self.assertTrue(plan['initial_state']['blocked'])
        self.assertFalse(plan['initial_state']['reset_pending'])
        self.assertEqual(plan['initial_state']['flash_attempt']['sectors'],[0x5000,0x6000,0x4000])
        self.assertIn(str(failed/'deployment.json'),[f['source'] for f in plan['files']])
        with tempfile.TemporaryDirectory() as new:
            new = Path(new)
            shutil.copy2(plan['baseline'],new/'baseline.bin')
            for file in plan['files']:
                dest = new/file['relative']; dest.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(file['source'],dest)
            (new/'state.json').write_text(json.dumps(plan['initial_state']))
            self.root = new; self.fail = False
            self.run_request('retry_flash')
            self.assertFalse(worker.Session(new).state['blocked'])

    def test_legacy_migration_rejects_unfinished_receipt_missing_request_and_wrong_scope(self):
        failed = self.legacy_evidence()
        receipt_path = failed/'deployment.json'
        original = json.loads(receipt_path.read_text())
        for edits in ({'status':'sector_write_outcome_pending'}, {'current_sector':'0xee000'},
                      {'verified_sectors':['0x6000']}, {'baseline_sha256':'0'*64}):
            receipt_path.write_text(json.dumps(dict(original,**edits)))
            with self.assertRaises(ValueError): worker.resume_failed_session(self.root)
        receipt_path.write_text(json.dumps(original))
        (self.root/'ipc/failed.json').unlink()
        with self.assertRaisesRegex(ValueError,'candidate request missing'): worker.resume_failed_session(self.root)


@unittest.skipUnless(sys.platform=='win32','Windows named-pipe smoke test')
class PipeTests(unittest.TestCase):
    def test_no_idle_expiration(self):
        source=(Path(__file__).resolve().parent/'flash-session-server.ps1').read_text(encoding='utf-8')
        self.assertIn('$pipe.WaitForConnectionAsync().GetAwaiter().GetResult()',source)
        self.assertNotIn('WaitForConnectionAsync().Wait(',source)

    def test_real_pipe_status_rejection_quit_no_elevation(self):
        self.run_pipe(False)

    def test_trusted_pipe_uses_existing_environment(self):
        self.run_pipe(True)

    def run_pipe(self,trusted):
        here=Path(__file__).resolve().parent
        sid=subprocess.check_output([worker.PS,'-NoProfile','-Command','[Security.Principal.WindowsIdentity]::GetCurrent().User.Value'],text=True).strip()
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'ipc').mkdir();(root/'tools/jieli-wl82').mkdir(parents=True)
            shutil.copy2(here/'flash_session_worker.py',root/'tools/jieli-wl82')
            (root/'session.json').write_text(json.dumps(dict(pipe='fm1-test-'+uuid.uuid4().hex,owner_sid=sid,trusted_local=trusted)))
            (root/'state.json').write_text(json.dumps(dict(blocked=False,operation='offline-test')))
            server=subprocess.Popen([worker.PS,'-NoProfile','-File',str(here/'flash-session-server.ps1'),'-SessionRoot',str(root)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                deadline=time.monotonic()+10
                while 'server_pid' not in json.loads((root/'session.json').read_text(encoding='utf-8-sig')):
                    if server.poll() is not None: self.fail(str(server.communicate()))
                    if time.monotonic()>deadline:self.fail('Server startup timeout')
                    time.sleep(.1)
                def client(op,extra=()):
                    return subprocess.run([worker.PS,'-NoProfile','-File',str(here/'flash-session-client.ps1'),'-SessionRoot',str(root),'-Operation',op,*extra],capture_output=True,text=True,timeout=25)
                status=client('status');self.assertEqual(status.returncode,0,status.stdout+status.stderr)
                self.assertTrue(json.loads(status.stdout)['ok'])
                self.assertEqual(json.loads((root/'session.json').read_text(encoding='utf-8-sig'))['idle_timeout_seconds'],0)
                self.assertIsNone(server.poll())
                if trusted:
                    env=client('environment');self.assertEqual(env.returncode,0,env.stdout+env.stderr)
                    data=json.loads(env.stdout)['data']
                    self.assertTrue(data['trusted_local']);self.assertFalse(data['isolated']);self.assertFalse(data['no_site'])
                    self.assertEqual(Path(data['python']),Path(sys.executable))
                    self.assertIn('site-packages',data['packages']['serial'])
                bad=root/'bad.json';bad.write_text(json.dumps(dict(op='plan',command='malicious')))
                rejected=client('plan',['-RequestFile',str(bad)])
                self.assertNotEqual(rejected.returncode,0)
                self.assertIn('Unexpected or missing',rejected.stdout)
                rejected=client('retry_flash',['-RequestFile',str(bad)])
                self.assertNotEqual(rejected.returncode,0)
                self.assertIn('Unexpected or missing',rejected.stdout)
                quit_result=client('quit');self.assertEqual(quit_result.returncode,0,quit_result.stderr)
                server.wait(timeout=5)
            finally:
                if server.poll() is None:server.terminate();server.wait(timeout=5)
                server.communicate()


if __name__=='__main__':unittest.main()
