"""Fake-device tests only; no USB commands are issued."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import deploy_cpu1_guard_via_cli as mod


class DeploymentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.forward=mod.validate_inputs()
        cls.reverse=mod.validate_inputs(True)

    def simulate(self,reverse=False,fail=None):
        source,target,sectors=self.reverse if reverse else self.forward
        memory=bytearray(source); writes=[]; full=[]
        def cli(device,commands,log):
            for command in commands:
                verb,rest=command.split(' ',1)
                if verb=='read':
                    address,size,path=rest.split(' ',2); address,size=int(address),int(size)
                    if size==1048576:full.append(len(writes))
                    data=bytes(memory[address:address+size])
                    if ((fail=='preflight' and len(full)==2) or
                        (fail=='sector' and writes) or (fail=='final' and len(full)==3)):
                        data=bytes([data[0]^1])+data[1:]
                    Path(path).write_bytes(data)
                elif verb=='write':
                    address,path=rest.split(' ',1); address=int(address)
                    self.assertIn(address,sectors)
                    data=Path(path).read_bytes(); self.assertEqual(len(data),4096)
                    writes.append(address)
                    if fail=='transport':raise IOError('Unknown write result')
                    memory[address:address+4096]=data
                else:self.fail('Unexpected device command')
        with tempfile.TemporaryDirectory() as directory,contextlib.redirect_stdout(io.StringIO()):
            if fail:
                with self.assertRaises((ValueError,IOError)):
                    mod.deployment.deploy('fake',Path(directory),source,target,cli,sectors,stock_power=True)
            else:mod.deployment.deploy('fake',Path(directory),source,target,cli,sectors,stock_power=True)
            receipt=json.loads((Path(directory)/'deployment.json').read_text())
        return memory,target,sectors,writes,full,receipt

    def test_forward_and_stock_restore(self):
        for reverse in (False,True):
            memory,target,sectors,writes,full,receipt=self.simulate(reverse)
            self.assertEqual(memory,target); self.assertEqual(writes,sectors)
            self.assertEqual(full,[0,0,len(sectors)])
            self.assertEqual(receipt['status'],'written_and_readback_verified')
            self.assertFalse(receipt['hardware_boot_verified'])

    def test_failure_stops_without_retry(self):
        for mode,count in (('preflight',0),('sector',1),('transport',1),('final',19)):
            with self.subTest(mode=mode):
                *_,writes,full,receipt=self.simulate(fail=mode)
                self.assertEqual(len(writes),count)
                self.assertEqual(receipt['status'],'failed')

    def test_scope_rejected_before_io(self):
        source,target,sectors=self.forward
        for bad in (sectors[:-1],sectors+[0xee000],sectors+[0x31000],sorted(sectors)):
            with tempfile.TemporaryDirectory() as directory,self.assertRaises(ValueError):
                mod.deployment.deploy('fake',Path(directory),source,target,
                    lambda *a:self.fail('IO before validation'),bad,stock_power=True)

    def test_original_cli_used(self):
        with patch.object(mod.deployment,'cli_call') as cli:
            mod.call('fake',['read 0 256 x.bin'],Path('x.log'))
            cli.assert_called_once_with('fake',['read 0 256 x.bin'],Path('x.log'),stock_power=True)

    def test_wrong_app_rejected(self):
        with patch.object(mod,'APP_SHA','0'*64),self.assertRaisesRegex(ValueError,'Unreviewed'):
            mod.validate_inputs()


if __name__=='__main__':unittest.main(verbosity=2)
