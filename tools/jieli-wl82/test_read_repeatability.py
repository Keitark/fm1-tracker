import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import read_repeatability as repeat


class RepeatTests(unittest.TestCase):
    def test_ten_full_reads_capture_all_differences(self):
        calls = []
        def read(dev, protocol, address, length):
            number = len(calls)//4
            calls.append(address)
            data = bytearray(length)
            if address == 512:
                data[7] = number
            if address == 768 and number == 9:
                data[30] = 1
            return bytes(data)
        with tempfile.TemporaryDirectory() as folder, \
             patch.object(repeat.reader, 'FM1_FLASH_SIZE', 1024), \
             patch.object(repeat.reader, 'read_flash_exact', side_effect=read), \
             contextlib.redirect_stdout(io.StringIO()):
            report = repeat.capture(None, None, Path(folder))
            self.assertEqual(report['status'], 'complete')
            self.assertEqual(report['variable_offsets'], ['0x207', '0x31e'])
            self.assertEqual(calls, [0,256,512,768]*10)
            self.assertEqual(len(list(Path(folder).glob('*.bin'))), 10)

    def test_transport_failure_stops_without_retry_and_preserves_partial(self):
        with tempfile.TemporaryDirectory() as folder, \
             patch.object(repeat.reader, 'FM1_FLASH_SIZE', 1024), \
             patch.object(repeat.reader, 'read_flash_exact', side_effect=[bytes(256), IOError('transport')]) as read:
            with self.assertRaises(IOError):
                repeat.capture(None, None, Path(folder))
            self.assertEqual(read.call_count, 2)
            self.assertEqual((Path(folder)/'pass-01.bin.partial').stat().st_size, 256)
            self.assertEqual(json.loads((Path(folder)/'comparison.json').read_text())['status'], 'failed')


if __name__ == '__main__':
    unittest.main()
