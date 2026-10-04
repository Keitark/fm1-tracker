"""Offline tests. Fake CLI calls never open a device."""
import ast
import cmd
import contextlib
import io
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
import deploy_v14_smb1_via_cli as deploy
import restore_v14_via_cli as restore
import hashlib


class Progress:
    def __init__(self,**kwargs):pass
    def update(self,*args):pass
    def close(self):pass


class ShellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tree=ast.parse(deploy.CLI.read_text())
        shell=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='DasShell')
        namespace={'cmd':cmd,'args':SimpleNamespace(io_size=256,fm1_bench=True),
                   'tqdm':Progress,'SCSIException':IOError}
        exec(compile(ast.Module(body=[shell],type_ignores=[]),'upstream-shell','exec'),namespace)
        cls.shell=namespace['DasShell']

    def test_sector_write_erase_and_program_verified(self):
        for fail in (None,'erase','program'):
            class Device:
                def __init__(self):self.data=bytearray(4096);self.writes=0;self.erases=0
                def flash_erase_sector(self,addr):
                    self.erases+=1;self.data[:]=b'\xff'*4096
                    if fail=='erase':self.data[0]=0
                def flash_read(self,addr,size):return bytes(self.data[addr-0x5000:addr-0x5000+size])
                def flash_write(self,addr,data):
                    self.writes+=1;self.data[addr-0x5000:addr-0x5000+len(data)]=data
                    if fail=='program':self.data[0]^=1
            with tempfile.TemporaryDirectory() as folder:
                source=Path(folder)/'sector.bin';source.write_bytes(bytes(range(256))*16)
                dev=Device();shell=self.shell(dev)
                with contextlib.redirect_stdout(io.StringIO()):
                    if fail:
                        # Make a persistent final corruption for program test.
                        if fail=='program':
                            original=dev.flash_read
                            dev.flash_read=lambda addr,size: (b'\xff'+original(addr,size)[1:]
                                if dev.writes and addr==0x5000 else original(addr,size))
                        with self.assertRaises(IOError):shell.do_write(f'0x5000 {source}')
                    else:shell.do_write(f'0x5000 {source}')
                self.assertEqual(dev.erases,1)
                self.assertEqual(dev.writes,0 if fail=='erase' else 16)

    def test_scope_rejected_before_erase(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'sector.bin';source.write_bytes(bytes(4096))
            for addr in (0,0x3000,0x4001,0x2d000,0xee000):
                with self.assertRaises(ValueError):self.shell(object()).do_write(f'{addr} {source}')

    def test_nes_cdc_boundary_is_explicit(self):
        options=self.shell.__init__.__globals__['args']
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'sector.bin';source.write_bytes(bytes(4096))
            with self.assertRaises(ValueError):self.shell(object()).do_write(f'0x35000 {source}')
            options.fm1_nes_cdc=True
            try:
                shell=self.shell(object());self.assertEqual(shell.fm1_write_last,0x92000)
                for address in (0,0x3000,0x92001,0x93000,0xee000):
                    with self.assertRaises(ValueError):shell.do_write(f'{address} {source}')
            finally:options.fm1_nes_cdc=False


class NESCDCDeploymentTests(unittest.TestCase):
    def test_new_extent_saved_baseline_and_directory_last(self):
        baseline=bytes(1048576);image=bytearray(baseline)
        sectors=[0x5000,0x31000,0x32000,0x33000,0x34000,0x35000,0x36000,0x37000,0x92000,0x4000]
        for address in sectors:image[address]=1
        image=bytes(image);memory=bytearray(baseline);writes=[];full=[]
        def call(device,commands,log):
            for command in commands:
                parts=command.split(' ',3)
                if parts[0]=='write':
                    address=int(parts[1]);payload=Path(' '.join(parts[2:])).read_bytes()
                    self.assertEqual(len(payload),4096);writes.append(address);memory[address:address+4096]=payload
                else:
                    self.assertEqual(parts[0],'read');address,size=int(parts[1]),int(parts[2])
                    if size==1048576:full.append(len(writes))
                    Path(parts[3]).write_bytes(memory[address:address+size])
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()):
            path=Path(folder);saved=path/'saved.bin';saved.write_bytes(baseline)
            for opt,bad in ((False,sectors),(True,sorted(sectors)),(True,sectors+[0x93000])):
                with self.assertRaises(ValueError):
                    deploy.deploy('fake',path,baseline,image,lambda *args:self.fail('Unexpected I/O'),bad,
                                  verified_preflight=saved,nes_cdc=opt)
            report=deploy.deploy('fake',path,baseline,image,call,sectors,verified_preflight=saved,nes_cdc=True)
            self.assertEqual(writes,sectors);self.assertEqual(full,[10]);self.assertEqual(memory,image)
            self.assertEqual(report['status'],'written_and_readback_verified')
            self.assertEqual(report['full_readback_count'],1)
            self.assertFalse((path/'final-2.bin').exists())

    def test_shared_final_sector_config_bytes_are_protected(self):
        baseline=bytes(1048576)
        for address in (0x4040,0x411f,0x92e1c,0x92fff,0x93000,0xee000):
            image=bytearray(baseline);image[0x4000]=1;image[address]=1
            sectors=sorted({0x4000,address&~0xfff})
            order=[a for a in sectors if a!=0x4000]+[0x4000]
            with self.subTest(address=address),tempfile.TemporaryDirectory() as folder:
                with self.assertRaisesRegex(ValueError,'boot/config/reserved'):
                    deploy.deploy('fake',Path(folder),baseline,bytes(image),lambda *args:self.fail('Unexpected I/O'),order,nes_cdc=True)


class DeploymentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.image,cls.baseline=restore.validate_inputs()

    def run_fake(self,folder,fail=None,reverse=False):
        baseline,image=restore.validate_inputs() if reverse else (self.baseline,self.image)
        memory=bytearray(baseline);writes=[];calls=[]
        def cli(device,commands,log):
            calls.append(commands)
            for command in commands:
                verb,rest=command.split(' ',1)
                if verb=='read':
                    address,size,path=rest.split(' ',2);address,size=int(address),int(size)
                    data=bytes(memory[address:address+size])
                    if fail=='preflight' and len(calls)==2:data=bytes([data[0]^1])+data[1:]
                    if fail=='readback' and writes:data=bytes([data[0]^1])+data[1:]
                    if fail=='final' and writes and size==1048576:data=bytes([data[0]^1])+data[1:]
                    Path(path).write_bytes(data)
                elif verb=='write':
                    address,path=rest.split(' ',1);address=int(address)
                    payload=Path(path).read_bytes()
                    self.assertEqual(len(payload),4096);self.assertIn(address,deploy.SECTORS)
                    writes.append(address)
                    if fail=='transport':raise IOError('transport outcome unknown')
                    memory[address:address+4096]=payload
                else:raise AssertionError('Forbidden CLI command')
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                result=deploy.deploy('fake',Path(folder),baseline,image,cli)
        except (ValueError,IOError):
            if fail is None:raise
            result=None
        return result,writes,memory

    def test_all_sectors_then_one_full_read(self):
        with tempfile.TemporaryDirectory() as folder:
            report,writes,memory=self.run_fake(folder)
            self.assertEqual(writes,deploy.SECTORS)
            self.assertEqual(memory,self.image)
            self.assertEqual(report['status'],'written_and_readback_verified')
            self.assertEqual(report['full_readback_count'],1)
            self.assertTrue((Path(folder)/'final-1.bin').exists())
            self.assertFalse((Path(folder)/'final-2.bin').exists())

    def test_failures_stop_without_retry(self):
        for fail,count in (('preflight',0),('readback',1),('transport',1),('final',len(deploy.SECTORS))):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as folder:
                result,writes,_=self.run_fake(folder,fail)
                self.assertIsNone(result);self.assertEqual(len(writes),count)

    def test_restore_v14(self):
        with tempfile.TemporaryDirectory() as folder:
            report,writes,memory=self.run_fake(folder,reverse=True)
            self.assertEqual(writes,deploy.SECTORS)
            self.assertEqual(memory,self.baseline)
            self.assertEqual(report['baseline_sha256'],deploy.IMAGE_SHA)
            self.assertEqual(report['candidate_sha256'],deploy.BASE_SHA)
            self.assertEqual(report['full_readback_sha256'],hashlib.sha256(self.baseline).hexdigest())
            self.assertEqual(report['status'],'written_and_readback_verified')


if __name__=='__main__':unittest.main()
