"""Fake CLI/transport only: exact stock-power deployment and restore scope."""
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
import deploy_stock_power_via_cli as power
from test_v14_cli_deployment import ShellTests


class StockPowerDeploymentTests(unittest.TestCase):
    def test_write_and_restore_with_exact_reads(self):
        for reverse in (False,True):
            source,target,sectors=power.validate_inputs(reverse)
            memory=bytearray(source);writes=[];full=[]
            def cli(device,commands,log):
                for command in commands:
                    verb,rest=command.split(' ',1)
                    if verb=='read':
                        address,size,path=rest.split(' ',2);address,size=int(address),int(size)
                        if size==1048576:full.append(len(writes))
                        Path(path).write_bytes(memory[address:address+size])
                    elif verb=='write':
                        address,path=rest.split(' ',1);address=int(address)
                        payload=Path(path).read_bytes()
                        self.assertEqual(len(payload),4096)
                        self.assertIn(address,power.SECTORS)
                        writes.append(address);memory[address:address+4096]=payload
                    else:self.fail('Forbidden command')
            with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()):
                report=power.deployment.deploy('fake',Path(folder),source,target,cli,sectors,stock_power=True)
            self.assertEqual(writes,power.SECTORS)
            self.assertEqual(full,[0,0,45])
            self.assertEqual(memory,target)
            self.assertEqual(report['status'],'written_and_readback_verified')

    def test_bad_scope_or_missing_opt_in_rejected_before_io(self):
        source,target,sectors=power.validate_inputs()
        bads=[(sectors,False),(sectors[:-1],True),(sectors+[0x31000],True),
              (sectors+[0xee000],True),(sectors+sectors[:1],True),(sorted(sectors),True)]
        for bad,enabled in bads:
            with tempfile.TemporaryDirectory() as folder:
                with self.assertRaises(ValueError):
                    power.deployment.deploy('fake',Path(folder),source,target,
                        lambda *a:self.fail('Device access before rejection'),bad,stock_power=enabled)

    def test_new_cli_boundary_erases_only_permitted_sector(self):
        ShellTests.setUpClass()
        class Device:
            def __init__(self):self.erases=[];self.memory=bytearray(4096)
            def flash_erase_sector(self,addr):self.erases.append(addr);self.memory[:]=b'\xff'*4096
            def flash_read(self,addr,size):return bytes(self.memory[addr-0x30000:addr-0x30000+size])
            def flash_write(self,addr,data):self.memory[addr-0x30000:addr-0x30000+len(data)]=data
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'sector.bin';source.write_bytes(bytes(4096))
            dev=Device();shell=ShellTests.shell(dev)
            with self.assertRaises(ValueError):shell.do_write(f'0x30000 {source}')
            shell.fm1_write_last=0x30000
            for address in (0,0x3000,0x30001,0x31000,0xee000):
                with self.assertRaises(ValueError):shell.do_write(f'{address} {source}')
            self.assertEqual(dev.erases,[])
            with contextlib.redirect_stdout(io.StringIO()):shell.do_write(f'0x30000 {source}')
            self.assertEqual(dev.erases,[0x30000])


if __name__=='__main__':unittest.main()
