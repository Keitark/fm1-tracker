import contextlib
import hashlib
import io
import json
import sys
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch
import fm1_status_preflight as p


class Fake:
    def __init__(self):
        self.calls=[];self.sr1=[0];self.error=0;self.bad_magic=False
        self.short_at=None;self.fail_at=None;self.original=bytes(range(256))*4096
        self.closed=False
    def close(self):self.closed=True
    def execute(self,cdb,out,buffer):
        op=int.from_bytes(cdb[:2],'big');self.calls.append(op)
        if len(self.calls)==self.fail_at:raise IOError('transport failure')
        if len(self.calls)==self.short_at:return 1
        if op==0xfb06:
            if out is None or buffer is not None:raise AssertionError('Bad data direction')
            if p.jl_crc16(out)!=int.from_bytes(cdb[9:11],'little'):raise AssertionError('Bad CRC')
            return 0
        if out is not None:raise AssertionError('Unexpected outgoing data')
        if op==0xfd05:
            address=int.from_bytes(cdb[2:6],'big');size=int.from_bytes(cdb[6:8],'big')
            buffer[:]=self.original[address:address+size];return 0
        buffer[:]=op.to_bytes(2,'big')+bytes(14)
        if op==0xfc03:
            buffer[2]=self.error;buffer[3]=self.sr1.pop(0) if len(self.sr1)>1 else self.sr1[0]
            buffer[4:8]=b'BAD!' if self.bad_magic else b'FMS1'
        elif op==0xfc0a:
            buffer[2]=3;buffer[4:8]=(0x856014).to_bytes(4,'little')
        elif op!=0xfb08:raise AssertionError('Unexpected opcode')
        return 0


class PreflightTests(unittest.TestCase):
    def test_upload_and_fail_at_every_write(self):
        raw=p.checked_file(p.LOADER,24064,p.LOADER_SHA)
        fake=Fake();p.Transport(fake).upload(raw)
        self.assertEqual(fake.calls,[0xfb06]*47+[0xfb08])
        for number in range(1,48):
            for kind in ('short_at','fail_at'):
                fake=Fake();setattr(fake,kind,number)
                with self.assertRaises(IOError):p.Transport(fake).upload(raw)
                self.assertEqual(fake.calls,[0xfb06]*number)
    def test_status_values_errors_signature(self):
        for value in range(256):
            fake=Fake();fake.sr1=[value]
            self.assertEqual(p.Transport(fake).status(),value)
        for error in (1,2,255):
            fake=Fake();fake.error=error
            with self.assertRaises(IOError):p.Transport(fake).status()
        fake=Fake();fake.error=1
        self.assertIsNone(p.Transport(fake).status(True))
        fake=Fake();fake.bad_magic=True
        with self.assertRaises(IOError):p.Transport(fake).status()
    def test_original_control_pins_blob_and_forbids_status(self):
        fake=Fake();transport=p.Transport(fake,original_control=True)
        with self.assertRaises(ValueError):transport.upload(p.LOADER.read_bytes())
        with self.assertRaises(ValueError):transport.status()
        self.assertEqual(fake.calls,[])
        transport.upload(p.checked_file(p.ORIGINAL_LOADER,24064,p.ORIGINAL_LOADER_SHA))
        self.assertEqual(fake.calls,[0xfb06]*47+[0xfb08])
        for failure in ('short_at','fail_at'):
            for number in range(1,48):
                fake=Fake();setattr(fake,failure,number)
                with self.assertRaises(IOError):
                    p.Transport(fake,original_control=True).upload(p.ORIGINAL_LOADER.read_bytes())
                self.assertEqual(fake.calls,[0xfb06]*number)

    def test_original_control_main_two_reads_without_status(self):
        fake=Fake();fake.original=p.BACKUP.read_bytes()
        with tempfile.TemporaryDirectory() as folder:
            result=Path(folder)/'result.json'
            argv=['preflight','--device','fake','--confirm-cold-uboot',
                  '--original-control','--read-delay-ms','10','--result',str(result)]
            with patch.object(sys,'argv',argv),patch.object(p,'SCSIDev',return_value=fake), \
                 patch.object(p,'read_inquiry',return_value=('WL82','UBOOT1.00','1.00')), \
                 patch.object(p.time,'sleep') as sleep, \
                 contextlib.redirect_stdout(io.StringIO()):
                p.main()
            report=json.loads(result.read_text())
        self.assertEqual(report['status'],'complete')
        self.assertTrue(report['original_control'])
        self.assertFalse(report['flash_path_qualified'])
        self.assertEqual(report['loader_sha256'],p.ORIGINAL_LOADER_SHA)
        self.assertEqual(fake.calls.count(0xfd05),8193)
        self.assertNotIn(0xfc03,fake.calls)
        self.assertEqual(set(fake.calls),{0xfb06,0xfb08,0xfc0a,0xfd05})
        self.assertEqual(len(report['readbacks']),2)
        self.assertEqual(report['read_delay_ms'],10)
        self.assertEqual(sleep.call_count,8193)
        self.assertTrue(all(call.args==(0.01,) for call in sleep.call_args_list))
        self.assertEqual(report['compare_progress'],{'pass':2,'matched_bytes':p.FLASH_SIZE})
        self.assertIsNone(report['readbacks'][0]['status_before'])
        self.assertTrue(fake.closed)
    def test_pacing_order_failure_and_bounds(self):
        fake=Fake();transport=p.Transport(fake,original_control=True,read_delay_ms=10)
        with patch.object(p.time,'sleep',side_effect=lambda delay:self.assertEqual(fake.calls,[])) as sleep:
            self.assertEqual(transport.read(0,256),fake.original[:256])
        sleep.assert_called_once_with(0.01)
        for delay in (-1,1,11,float('nan')):
            with self.assertRaises(ValueError):p.Transport(fake,read_delay_ms=delay)
        fake=Fake();fake.fail_at=2
        with patch.object(p.time,'sleep') as sleep:
            transport=p.Transport(fake,original_control=True,read_delay_ms=10)
            with self.assertRaises(IOError):transport.compare(fake.original,1)
        self.assertEqual(fake.calls,[0xfd05,0xfd05])
        self.assertEqual(sleep.call_count,2)
        self.assertEqual(transport.compare_progress,{'pass':1,'matched_bytes':256})
    def test_ready_waits_and_times_out(self):
        fake=Fake();fake.sr1=[1,1,0]
        with patch.object(p.time,'sleep'):
            self.assertEqual(p.Transport(fake).ready(),{'sr1':0,'polls':3,'busy_seen':True})
        fake=Fake();fake.sr1=[1]
        with patch.object(p.time,'monotonic',side_effect=[0,31]):
            with self.assertRaises(TimeoutError):p.Transport(fake).ready()
        self.assertEqual(fake.calls,[0xfc03])
    def test_two_full_reads_and_no_array_writes(self):
        fake=Fake();transport=p.Transport(fake);transport.online()
        with contextlib.redirect_stdout(io.StringIO()):
            reports=[transport.compare(fake.original,n) for n in (1,2)]
        self.assertEqual(fake.calls.count(0xfd05),8192)
        self.assertTrue(set(fake.calls)<={0xfc0a,0xfc03,0xfd05})
        self.assertEqual(reports[0]['sha256'],hashlib.sha256(fake.original).hexdigest())
    def test_invalid_bounds_and_forbidden_commands(self):
        fake=Fake();transport=p.Transport(fake)
        for addr,length in ((-1,1),(0,0),(0,257),(p.FLASH_SIZE,1)):
            with self.assertRaises(ValueError):transport.read(addr,length)
        for op in (0xfb00,0xfb01,0xfb02,0xfb04,0xfc09,0xfc12,0xfc0c):
            with self.assertRaises(ValueError):transport.transfer(op)
        self.assertEqual(fake.calls,[])
    def test_short_commands_and_read_mismatch(self):
        for op in (0xfc03,0xfc0a,0xfb08,0xfd05):
            fake=Fake();fake.short_at=1
            with self.assertRaises(IOError):p.Transport(fake).transfer(op)
            self.assertEqual(len(fake.calls),1)
        fake=Fake()
        with self.assertRaises(IOError):p.Transport(fake).compare(bytes(p.FLASH_SIZE),1)

    def test_main_baseline_precedes_status_and_failure_is_recorded(self):
        for fail in (False,True):
            fake=Fake();fake.original=p.BACKUP.read_bytes()
            if fail:fake.short_at=50  # first read, after 47 writes/jump/online
            with tempfile.TemporaryDirectory() as folder:
                result=Path(folder)/'result.json'
                argv=['preflight','--device','fake','--confirm-cold-uboot','--result',str(result)]
                with patch.object(sys,'argv',argv),patch.object(p,'SCSIDev',return_value=fake), \
                     patch.object(p,'read_inquiry',return_value=('WL82','UBOOT1.00','1.00')), \
                     contextlib.redirect_stdout(io.StringIO()):
                    if fail:
                        with self.assertRaises(IOError):p.main()
                    else:p.main()
                report=json.loads(result.read_text())
                self.assertEqual(fake.calls[48:50],[0xfc0a,0xfd05])
                self.assertTrue(fake.closed)
                self.assertFalse(report['array_program_or_erase_commands_sent'])
                if fail:
                    self.assertEqual(report['status'],'failed')
                    self.assertEqual(report['last_command']['address'],'0x0')
                    self.assertNotIn(0xfc03,fake.calls)
                else:
                    self.assertEqual(report['status'],'complete')
                    self.assertEqual(len(report['readbacks']),2)


if __name__=='__main__':unittest.main()
