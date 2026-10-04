import contextlib
import copy
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import read_fm1_firmware as reader


class FakeFlash:
    def __init__(self, corrupt_second_pass=False):
        self.data = bytes(range(256)) * 4
        self.calls = 0
        self.corrupt_second_pass = corrupt_second_pass
        self.is_open = True

    def execute(self, cdb, data_out, data_in):
        assert data_out is None
        assert int.from_bytes(cdb[:2], 'big') == reader.JL_LoaderV2.Cmd.READ_FLASH
        address = int.from_bytes(cdb[2:6], 'big')
        length = int.from_bytes(cdb[6:8], 'big')
        assert length == len(data_in)
        assert address + length <= len(self.data)
        data_in[:] = self.data[address:address + length]
        self.calls += 1
        if self.corrupt_second_pass and self.calls > 4:
            data_in[0] ^= 1
        return 0

    def close(self):
        self.is_open = False


class FakeUpload:
    """Record transport only. Does not model ROM execution or SPI effects."""
    def __init__(self, fail_at=None, failure='short', jump_failure=None):
        self.calls=[]
        self.fail_at=fail_at
        self.failure=failure
        self.jump_failure=jump_failure

    def execute(self,cdb,data_out,data_in):
        self.calls.append((bytes(cdb),data_out))
        if cdb[:2] == b'\xfb\x06':
            if len(self.calls)==self.fail_at:
                if self.failure=='exception':raise IOError('transport failure')
                return 1
            return 0
        if cdb[:2] != b'\xfb\x08':
            raise AssertionError('Unexpected command')
        if self.jump_failure=='exception':raise IOError('jump transport failure')
        data_in[:]=b'\xfb\x08'+bytes(14)
        if self.jump_failure=='echo':data_in[1]=0
        if self.jump_failure=='length':del data_in[-1:]
        return 1 if self.jump_failure=='short' else 0


class UploadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.chip,cls.spec,cls.path=reader.load_wl82_profile()
        cls.raw=cls.path.read_bytes()

    def upload(self,dev,chip=None,spec=None,path=None):
        reader.upload_wl82_ram_loader(dev,chip or self.chip,spec or self.spec,path or self.path)

    def test_exact_original_upload_and_jump(self):
        dev=FakeUpload()
        self.upload(dev)
        self.assertEqual(len(dev.calls),48)
        for index,(cdb,payload) in enumerate(dev.calls[:-1]):
            self.assertEqual(cdb[:2],b'\xfb\x06')
            self.assertEqual(int.from_bytes(cdb[2:6],'big'),reader.WL82_LOADER_ADDRESS+512*index)
            self.assertEqual(cdb[6:9],b'\x02\x00\x00')
            self.assertEqual(payload,self.raw[index*512:(index+1)*512])
            self.assertEqual(cdb[9:11],reader.jl_crc16(payload).to_bytes(2,'little'))
            self.assertEqual(cdb[11:],b'\xff'*5)
        self.assertEqual(dev.calls[-1][0],b'\xfb\x08\x01\xc0\x20\x00\x00\x01'+b'\xff'*8)
        self.assertIsNone(dev.calls[-1][1])

    def test_each_failed_write_stops_without_jump_or_retry(self):
        for fail_at in range(1,48):
            for failure in ('short','exception'):
                with self.subTest(fail_at=fail_at,failure=failure):
                    dev=FakeUpload(fail_at,failure)
                    with self.assertRaises(IOError):self.upload(dev)
                    self.assertEqual(len(dev.calls),fail_at)
                    self.assertTrue(all(cdb[:2]==b'\xfb\x06' for cdb,_ in dev.calls))

    def test_failed_jump_does_not_retry(self):
        for failure in ('short','echo','length','exception'):
            with self.subTest(failure=failure):
                dev=FakeUpload(jump_failure=failure)
                with self.assertRaises(IOError):self.upload(dev)
                self.assertEqual(len(dev.calls),48)

    def test_changed_loader_rejected_before_any_command(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'unreviewed.bin'
            for data in (self.raw[:-1],bytes([self.raw[0]^1])+self.raw[1:]):
                path.write_bytes(data)
                dev=FakeUpload()
                with self.assertRaises(ValueError):self.upload(dev,path=path)
                self.assertEqual(dev.calls,[])

    def test_changed_profiles_rejected_before_any_command(self):
        for field,value in (('address',0x1c60000),('block-size',256),
                            ('blocksize',256),('encryption','none'),
                            ('info',{'protocol-version':'v1'})):
            spec=copy.deepcopy(self.spec)
            spec[field]=value
            dev=FakeUpload()
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.upload(dev,spec=spec)
            self.assertEqual(dev.calls,[])
        chip=copy.deepcopy(self.chip)
        chip['uboot1.00']['quirks']['memory-rw-mengli-crypt']=False
        dev=FakeUpload()
        with self.assertRaises(ValueError):self.upload(dev,chip=chip)
        self.assertEqual(dev.calls,[])

    def test_incomplete_inquiry_rejected(self):
        class Inquiry:
            def execute(self,cdb,data_out,data_in):return 1
        with self.assertRaisesRegex(IOError,'identity not established'):
            reader.read_inquiry(Inquiry())


class ReadbackTests(unittest.TestCase):
    def test_offline_plan_warns_without_opening_device(self):
        output=io.StringIO()
        with patch.object(reader,'SCSIDev',side_effect=AssertionError('Device access')), \
             patch('sys.argv',['reader','--plan']), contextlib.redirect_stdout(output):
            self.assertEqual(reader.main(),0)
        self.assertIn(reader.LOADER_SIDE_EFFECT_WARNING,output.getvalue())

    def test_two_reads_match_and_only_read_commands_are_used(self):
        dev = FakeFlash()
        with tempfile.TemporaryDirectory() as folder, patch.object(reader, 'FM1_FLASH_SIZE', 1024):
            output = Path(folder) / 'backup.bin'
            with contextlib.redirect_stdout(io.StringIO()):
                reader.read_raw_backup(dev, reader.JL_LoaderV2(dev), output)
            self.assertEqual(output.read_bytes(), dev.data)
            self.assertEqual(dev.calls, 8)
            self.assertFalse(output.with_suffix('.bin.partial').exists())
            self.assertFalse(json.loads(output.with_suffix('.json').read_text())['restore_tested'])

    def test_second_read_mismatch_keeps_partial_and_no_final_backup(self):
        dev = FakeFlash(corrupt_second_pass=True)
        with tempfile.TemporaryDirectory() as folder, patch.object(reader, 'FM1_FLASH_SIZE', 1024):
            output = Path(folder) / 'backup.bin'
            with self.assertRaisesRegex(IOError, 'Second read differs'):
                reader.read_raw_backup(dev, reader.JL_LoaderV2(dev), output)
            self.assertFalse(output.exists())
            self.assertTrue(output.with_suffix('.bin.partial').exists())
            self.assertEqual(dev.calls, 5)
            self.assertEqual(output.with_suffix('.mismatch.bin').read_bytes(),
                             bytes([1]) + dev.data[1:256])
            detail = json.loads(output.with_suffix('.mismatch.json').read_text())
            self.assertEqual(detail['changed_offsets'], ['0x0'])
            self.assertEqual(detail['chunk_address'], '0x0')

    def test_reuse_mode_never_uploads_or_jumps(self):
        dev = FakeFlash()
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'probe.bin'
            with patch.object(reader, 'SCSIDev', return_value=dev), \
                 patch.object(reader, 'read_inquiry', return_value=('WL82', 'UBOOT1.00', '1.00')), \
                 patch.object(reader.JL_LoaderV2, 'online_device', return_value={'type': 3, 'id': reader.FM1_FLASH_ID}), \
                 patch.object(reader, 'upload_wl82_ram_loader', side_effect=AssertionError('Unexpected upload')), \
                 patch('sys.argv', ['reader', '--device', 'fake', '--reuse-loader', '--probe-output', str(output)]), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(reader.main(), 0)
            self.assertEqual(output.read_bytes(), dev.data[:256])
            self.assertFalse(dev.is_open)


if __name__ == '__main__':
    unittest.main()
