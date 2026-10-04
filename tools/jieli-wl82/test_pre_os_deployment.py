"""Fake-device pre-OS write/restore checks, never physical USB IO."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import deploy_pre_os_via_cli as mod


class PreOsDeployment(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.forward=mod.validate_inputs()
        cls.reverse=mod.validate_inputs(True)

    def simulate(self,reverse=False,fail=None):
        source,target,sectors=self.reverse if reverse else self.forward
        memory=bytearray(source);writes=[];full=[]
        def cli(device,commands,log):
            for command in commands:
                verb,rest=command.split(' ',1)
                if verb=='read':
                    self.assertTrue(writes,'No pre-write reads permitted')
                    self.assertNotIn('before-',rest);self.assertNotIn('preflight-',rest)
                    address,size,path=rest.split(' ',2);address,size=int(address),int(size)
                    if size==1048576:full.append(len(writes))
                    data=bytes(memory[address:address+size])
                    if fail=='sector' or (fail=='final' and full):data=bytes([data[0]^1])+data[1:]
                    Path(path).write_bytes(data)
                elif verb=='write':
                    address,path=rest.split(' ',1);address=int(address)
                    self.assertIn(address,sectors)
                    data=Path(path).read_bytes();self.assertEqual(len(data),4096)
                    writes.append(address)
                    if fail=='transport':raise IOError('Unknown write result')
                    memory[address:address+4096]=data
                else:self.fail('Unexpected command')
        with tempfile.TemporaryDirectory() as directory,contextlib.redirect_stdout(io.StringIO()):
            saved=Path(directory)/'saved.bin';saved.write_bytes(source)
            if fail:
                with self.assertRaises((ValueError,IOError)):
                    mod.deployment.deploy('fake',Path(directory),source,target,cli,sectors,
                        stock_power=True,verified_preflight=saved)
            else:mod.deployment.deploy('fake',Path(directory),source,target,cli,sectors,
                        stock_power=True,verified_preflight=saved)
            receipt=json.loads((Path(directory)/'deployment.json').read_text())
        return memory,target,writes,full,receipt

    def test_forward_and_marker_restore(self):
        for reverse in (False,True):
            memory,target,writes,full,receipt=self.simulate(reverse)
            self.assertEqual(memory,target);self.assertEqual(writes,mod.SECTORS)
            self.assertEqual(full,[45])
            self.assertEqual(receipt['status'],'written_and_readback_verified')
            self.assertFalse(receipt['automatic_reset'])

    def test_fail_stop(self):
        for failure,count in (('sector',1),('transport',1),('final',45)):
            with self.subTest(failure=failure):
                _,_,writes,_,receipt=self.simulate(fail=failure)
                self.assertEqual(len(writes),count);self.assertEqual(receipt['status'],'failed')

    def test_wrong_source_and_checkpoint_rejected(self):
        with patch.object(mod,'MARKER_SHA','0'*64),self.assertRaises(ValueError):mod.validate_inputs()
        with patch.object(mod,'APP_SHA','0'*64),self.assertRaises(ValueError):mod.validate_inputs()

    def test_scope_rejected_without_io(self):
        source,target,sectors=self.forward
        for bad in (sectors[:-1],sectors+[0xee000],sorted(sectors)):
            with tempfile.TemporaryDirectory() as directory,self.assertRaises(ValueError):
                mod.deployment.deploy('fake',Path(directory),source,target,
                    lambda *a:self.fail('IO before scope validation'),bad,stock_power=True)

    def test_existing_cli(self):
        with patch.object(mod.deployment,'cli_call') as cli:
            mod.call('fake',['write 20480 sector.bin'],Path('x.log'))
            cli.assert_called_once_with('fake',['write 20480 sector.bin'],Path('x.log'),stock_power=True)


if __name__=='__main__':unittest.main(verbosity=2)
