import unittest
from inquiry_only import inspect, INQUIRY


class Device:
    def __init__(self,residue=0,wrong=False,fail=False):
        self.residue=residue;self.wrong=wrong;self.fail=fail
        self.calls=0;self.closed=False
    def execute(self,cdb,data_out,data_in):
        if cdb != INQUIRY or data_out is not None or len(data_in)!=36:
            raise AssertionError('Not the single permitted INQUIRY transaction')
        self.calls+=1
        if self.fail:raise IOError('transport error')
        data_in[8:16]=b'WL82    '
        data_in[16:32]=b'UBOOT1.00       '
        data_in[32:36]=b'1.00'
        if self.wrong:data_in[8]=ord('X')
        return self.residue
    def close(self):self.closed=True


class InquiryTests(unittest.TestCase):
    def test_identity_only(self):
        dev=Device()
        report=inspect('fake',lambda _:dev)
        self.assertEqual(report['status'],'complete')
        self.assertFalse(report['vendor_commands_sent'])
        self.assertEqual(dev.calls,1)
        self.assertTrue(dev.closed)
    def test_failures_close_without_retry(self):
        for dev in (Device(residue=1),Device(wrong=True),Device(fail=True)):
            with self.assertRaises((ValueError,IOError)):
                inspect('fake',lambda _:dev)
            self.assertTrue(dev.closed)
            self.assertEqual(dev.calls,1)

    def test_adapter_metadata_is_reported(self):
        dev=Device()
        dev.adapter_info={'alignment_mask':3,'buffer_alignment':4096}
        report=inspect('fake',lambda _:dev)
        self.assertEqual(report['adapter_info'],dev.adapter_info)
        self.assertEqual(dev.calls,1)


if __name__=='__main__':unittest.main()
