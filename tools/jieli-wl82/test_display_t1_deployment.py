"""No device access: exercise the actual orchestrator with a fake CLI."""
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
import deploy_display_t1_via_cli as t1

class DisplayDeploymentTests(unittest.TestCase):
    def test_both_directions(self):
        for restore in (False,True):
            source,target,sectors=t1.validate_inputs(restore)
            memory=bytearray(source);writes=[];full_reads=[]
            def cli(device,commands,log):
                for command in commands:
                    verb,rest=command.split(' ',1)
                    if verb=='read':
                        addr,size,path=rest.split(' ',2);addr,size=int(addr),int(size)
                        if size==1048576:full_reads.append(len(writes))
                        Path(path).write_bytes(memory[addr:addr+size])
                    elif verb=='write':
                        addr,path=rest.split(' ',1);addr=int(addr)
                        data=Path(path).read_bytes()
                        self.assertEqual(len(data),4096);self.assertIn(addr,sectors)
                        memory[addr:addr+4096]=data;writes.append(addr)
                    else:self.fail('Forbidden command')
            with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
                report=t1.deployment.deploy('fake',Path(directory),source,target,cli,sectors)
            self.assertEqual(memory,target);self.assertEqual(writes,sectors)
            self.assertEqual(full_reads,[0,0,27]);self.assertEqual(writes[-1],0x4000)
            self.assertEqual(report['status'],'written_and_readback_verified')

    def test_bad_scope_rejected_before_cli(self):
        source,target,sectors=t1.validate_inputs()
        for bad in (sectors[:-1],sectors+[0xee000],sectors+sectors[:1],sorted(sectors)):
            with tempfile.TemporaryDirectory() as directory:
                with self.assertRaises(ValueError):
                    t1.deployment.deploy('fake',Path(directory),source,target,
                        lambda *args:self.fail('CLI accessed before rejection'),bad)

if __name__=='__main__':unittest.main()
