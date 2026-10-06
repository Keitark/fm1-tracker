import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "samples"))
import fm1_bank as bank
import default_bank


class BankTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = default_bank.entries()
        cls.image = bank.build(cls.entries)

    def test_canonical_capacity_and_ids(self):
        entries = [bank.Entry(300 + i, f"TEST{i}", b"\x40\x20\0\0") for i in range(256)]
        self.assertEqual(bank.parse_bank(bank.build(entries)), entries)
        self.assertEqual(bank.build(list(reversed(entries))), bank.build(entries))
        for invalid in ([], entries + [bank.Entry(999, "EXTRA", b"\0\0")], [entries[0], entries[0]]):
            with self.assertRaises(ValueError):
                bank.build(invalid)

    def test_validation_rejects_corruption_and_bounds(self):
        data = bank.build([bank.Entry(1, "FIRST", b"\x01\x02\x03\x04"), bank.Entry(2, "SECOND", bytes(4))])
        for offset, value in ((8, 2), (10, 47), (12, 0), (14, 1), (32, 0), (36, 0), (36, 31),
                              (55, 65), (60, 3), (64, 2), (68, 2), (72, 64), (76, 65), (77, 8),
                              (78, 1), (80, 1), (104, 128)):
            bad = bytearray(data)
            bad[offset] = value
            struct.pack_into("<I", bad, 28, bank.image_crc(bad))
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                bank.parse_bank(bad)
        for bad in (data[:-1], data + b"\0\0", data[:40], bytes(32)):
            with self.assertRaises(ValueError):
                bank.parse_bank(bad)
        bad = bytearray(data)
        bad[-1] ^= 1
        with self.assertRaises(ValueError):
            bank.parse_bank(bad)

    def test_metadata_loops(self):
        for e in (bank.Entry(0, "BAD", bytes(4)), bank.Entry(1, "X" * 20, bytes(4)),
                  bank.Entry(1, "BAD", bytes(3)), bank.Entry(1, "BAD", bytes(4), loop_length=6),
                  bank.Entry(1, "BAD", bytes(4), finetune=-9), bank.Entry(1, "BAD", bytes(4), tags=64)):
            with self.assertRaises(ValueError):
                bank.build([e])
        e = bank.Entry(7, "LOOP", bytes(12), finetune=-8, loop_start=4, loop_length=8)
        self.assertEqual(bank.parse(bank.build([e])), [e])

    def test_default_assets_and_determinism(self):
        self.assertEqual(self.image, (ROOT / "samples/FM1BREAK.FMB").read_bytes())
        self.assertEqual(default_bank.entries(), self.entries)
        self.assertTrue(64 * 1024 <= len(self.image) <= 96 * 1024)
        self.assertTrue(24 <= len(self.entries) <= 32)
        self.assertEqual({e.tags for e in self.entries}, {1, 2, 4, 8, 16, 32})
        self.assertTrue(all(any(e.pcm) for e in self.entries))
        metadata = json.loads((ROOT / "samples/FM1BREAK.json").read_text())
        self.assertEqual(metadata["bank_sha256"], hashlib.sha256(self.image).hexdigest())
        self.assertEqual(metadata["asset_license"], "CC0-1.0")
        self.assertEqual(next(e for e in self.entries if e.id == 601).name, "COME ON!")

    def test_demo_structure_and_effects(self):
        data = default_bank.build_demo(self.entries)
        self.assertEqual(data, (ROOT / "samples/FM1BREAK.MOD").read_bytes())
        self.assertLessEqual(len(data), 256 * 1024)
        self.assertIn(data[1080:1084], (b"M.K.", b"M!K!"))
        count = data[950]
        order = list(data[952:952 + count])
        self.assertEqual(order, default_bank.ORDER)
        self.assertLess(len(set(order)), len(order))
        patterns = max(order) + 1
        self.assertEqual(patterns, 6)
        pcm_offset = 1084 + patterns * 1024
        channels, effects, bpm, vocal = set(), set(), False, False
        for offset in range(1084, pcm_offset, 4):
            a, b, c, d = data[offset:offset + 4]
            slot, effect = (a & 0xF0) | (c >> 4), c & 15
            if (a & 15) or b:
                channels.add(((offset - 1084) // 4) % 4)
            effects.add(effect)
            if effect == 0xF and d == 160:
                bpm = True
            if slot == 27:
                vocal = True
            if effect == 0xE:
                self.assertEqual(d >> 4, 9)
        self.assertEqual(channels, {0, 1, 2, 3})
        self.assertTrue({9, 0xE, 0xA, 0xD, 0xF} <= effects)
        self.assertTrue(bpm and vocal)
        for slot, e in enumerate(self.entries):
            offset = 20 + slot * 30
            size = struct.unpack_from(">H", data, offset + 22)[0] * 2
            start, loop = (value * 2 for value in struct.unpack_from(">HH", data, offset + 26))
            self.assertEqual(size, len(e.pcm))
            self.assertLessEqual(start + loop, size)
            self.assertEqual(data[pcm_offset:pcm_offset + size], e.pcm)
            pcm_offset += size
        self.assertEqual(pcm_offset, len(data))
        self.assertEqual(data[20 + 29 * 30:950], bytes(60))

    def test_pc_crud_manifest_and_wav_conversion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "sample.raw").write_bytes(b"\x40\xc0\x20")
            (root / "manifest.json").write_text(json.dumps({"entries": [{"id": 9, "name": "NINE", "file": "sample.raw"}]}))
            def cli(args):
                with contextlib.redirect_stdout(io.StringIO()):
                    bank.main(args)
            cli(["install-image", str(root / "manifest.json"), str(root / "a.FMB")])
            self.assertEqual(bank.parse((root / "a.FMB").read_bytes())[0].pcm, b"\x40\xc0\x20\0")
            cli(["add", str(root / "a.FMB"), str(root / "b.FMB"), "--id", "10", "--name", "TEN", "--pcm", str(root / "sample.raw")])
            cli(["replace", str(root / "b.FMB"), str(root / "c.FMB"), "--id", "9", "--name", "NEW", "--pcm", str(root / "sample.raw")])
            cli(["remove", str(root / "c.FMB"), str(root / "d.FMB"), "--id", "10"])
            self.assertEqual(bank.parse((root / "d.FMB").read_bytes())[0].name, "NEW")
            cli(["list", str(root / "d.FMB")])
            for width, raw in ((1, b"\0\x80\xff\x80"), (2, struct.pack("<hhhh", -32768, 0, 32767, 0))):
                path = root / f"sample{width}.wav"
                with wave.open(str(path), "wb") as wav:
                    wav.setnchannels(1); wav.setsampwidth(width); wav.setframerate(8287); wav.writeframes(raw)
                self.assertEqual(bank.read_pcm(path), b"\x80\0\x7f\0")
            path = root / "stereo.wav"
            with wave.open(str(path), "wb") as wav:
                wav.setnchannels(2); wav.setsampwidth(1); wav.setframerate(8287); wav.writeframes(bytes(4))
            with self.assertRaises(ValueError):
                bank.read_pcm(path)


if __name__ == "__main__":
    unittest.main()
