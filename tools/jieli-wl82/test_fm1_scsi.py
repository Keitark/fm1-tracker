import ctypes as ct
import unittest
from unittest.mock import patch
import fm1_scsi as s


class AdapterTests(unittest.TestCase):
    def test_buffer_alignment_and_bounds(self):
        for size in (1,16,36,256,512):
            for mask in (0,1,3,15,511,4095,65535):
                owner,pointer=s.aligned_buffer(size,mask)
                self.assertEqual(pointer&max(mask,4095),0)
                self.assertGreaterEqual(pointer,ct.addressof(owner))
                self.assertLessEqual(pointer+size,ct.addressof(owner)+ct.sizeof(owner))
        for size,mask in ((0,0),(513,0),(16,5),(16,65536)):
            with self.assertRaises(ValueError):s.aligned_buffer(size,mask)

    def device(self,callback):
        dev=object.__new__(s.SCSIDev)
        dev.is_open=True;dev.fhandle=None
        dev.adapter_info={'alignment_mask':511}
        dev._ioctl=callback
        return dev

    def test_inbound_alignment_sense_and_residue(self):
        def driver(code,src,dst):
            self.assertEqual(code,s.PASS_THROUGH)
            self.assertIs(src,dst)
            self.assertEqual(src.spt.sense_off,s.Packet.sense.offset)
            self.assertEqual(src.spt.dir,1)
            self.assertEqual(ct.cast(src.spt.data_ptr,ct.c_void_p).value&4095,0)
            ct.memmove(src.spt.data_ptr,b'abc',3)
            src.spt.data_len=3;src.spt.sense_len=0
            return ct.sizeof(s.Packet)
        dev=self.device(driver);result=bytearray(16)
        self.assertEqual(dev.execute(b'\x12\0\0\0\x10\0',None,result),13)
        self.assertEqual(result[:3],b'abc')

    def test_outgoing_bytes_unchanged(self):
        def driver(code,src,dst):
            self.assertEqual(src.spt.dir,0)
            self.assertEqual(ct.string_at(src.spt.data_ptr,src.spt.data_len),bytes(range(256)))
            return ct.sizeof(s.Packet)
        self.assertEqual(self.device(driver).execute(bytes(16),bytes(range(256)),None),0)

    def test_errors_and_invalid_parameters(self):
        for failure in ('short','overrun','sense','status','os'):
            def driver(code,src,dst):
                if failure=='short':return 1
                if failure=='overrun':src.spt.data_len=513
                if failure=='sense':src.spt.sense_len=33
                if failure=='status':src.spt.status=2;src.sense[0]=0x70
                if failure=='os':raise OSError('test failure')
                return ct.sizeof(s.Packet)
            with self.subTest(failure=failure),self.assertRaises(s.SCSIException):
                self.device(driver).execute(bytes(16),None,bytearray(256))
        dev=self.device(lambda *args:self.fail('Should not issue ioctl'))
        for cdb,out,inp in ((bytes(17),None,bytearray(16)),(b'\x12',b'a',bytearray(1)),
                            (b'\x12',None,None),(b'\x12',None,bytearray(513))):
            with self.assertRaises(ValueError):dev.execute(cdb,out,inp)

    def test_descriptor_validated_and_closed_on_error(self):
        def fake_open(dev,path):dev.is_open=True;dev.fhandle=None
        def valid_ioctl(dev,code,source,dest):
            self.assertEqual(code,s.QUERY_PROPERTY)
            raw=s.struct.pack('<5I',32,32,65536,17,511)
            ct.memmove(dest,raw,len(raw));return 32
        with patch.object(s.BaseDevice,'__init__',fake_open),patch.object(s.SCSIDev,'_ioctl',valid_ioctl):
            dev=s.SCSIDev('fake')
            self.assertEqual(dev.adapter_info['buffer_alignment'],4096)
        with patch.object(s.BaseDevice,'__init__',fake_open),patch.object(s.SCSIDev,'_ioctl',return_value=8):
            with self.assertRaises(IOError):s.SCSIDev('fake')


if __name__=='__main__':unittest.main()
