from pathlib import Path
import subprocess
import sys
import unittest
HERE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
import usb_client as client
class Pipe:
    def __enter__(self):
        self.proc=subprocess.Popen([str(HERE/'build/host/Release/mdx_usb_pipe.exe')],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
        return self
    def write(self,b):
        # Deliberate fragmentation through the real CDC line parser.
        for i in range(0,len(b),7):self.proc.stdin.write(b[i:i+7]);self.proc.stdin.flush()
        return len(b)
    def read(self,n):return self.proc.stdout.read(n)
    def __exit__(self,*args):
        self.proc.stdin.close();self.proc.wait(timeout=5);self.proc.stdout.close()
class ClientTests(unittest.TestCase):
    def test_real_parser_upload_and_karaoke(self):
        data=client.bundle((HERE/'samples/FM1DEMO.MDX').read_bytes(),(HERE/'samples/FM1DEMO.PDX').read_bytes())
        with Pipe() as port:
            self.assertIn('RAM',client.send(port,data))
            self.assertEqual(client.exchange(port,'MDX PLAY'),'OK MDX QUEUED')
            self.assertIn('running=1',client.exchange(port,'MDX STATUS'))
            for s in ('MDX SELECT 0','MDX MUTE 00 1','MDX NOTE 3c 1','MDX NOTE 3c 0','MDX STOP'):
                self.assertEqual(client.exchange(port,s),'OK MDX QUEUED')
            self.assertIn('running=0',client.exchange(port,'MDX STATUS'))
    def test_missing_pdx_rejected(self):
        data=client.bundle((HERE/'samples/FM1DEMO.MDX').read_bytes())
        with Pipe() as port:
            with self.assertRaisesRegex(RuntimeError,'FILE_OR_CRC'):client.send(port,data)
    def test_crc_and_malformed_line_reset(self):
        with Pipe() as port:
            client.exchange(port,'MDX BEGIN 0000000c 00000000')
            with self.assertRaises(RuntimeError):client.exchange(port,'x'*301)
            with self.assertRaises(RuntimeError):client.exchange(port,'MDX END')
    def test_size_before_io(self):
        with self.assertRaises(ValueError):client.bundle(b'')
        with self.assertRaises(ValueError):client.bundle(bytes(client.LIMIT))
if __name__=='__main__':unittest.main()
