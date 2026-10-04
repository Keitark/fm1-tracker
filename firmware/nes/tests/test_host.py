import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from make_diagnostic_rom import make_rom, make_cnrom_rom

EXE = Path(sys.argv.pop(1)).resolve()


class HostTests(unittest.TestCase):
    def run_rom(self, rom, pad=0, fail=False, frames=12):
        with tempfile.TemporaryDirectory(prefix='fm1-nes-test-') as d:
            source, picture = Path(d)/'diagnostic.nes', Path(d)/'frame.ppm'
            source.write_bytes(rom)
            cmd = [str(EXE),str(source),str(frames),str(pad),str(picture)]
            if fail: cmd.append('fail-video')
            result = subprocess.run(cmd,capture_output=True,text=True,timeout=20)
            return result.returncode,json.loads(result.stdout),picture.read_bytes() if picture.exists() else b''

    def test_cpu_video_and_audio(self):
        rc,s,picture=self.run_rom(make_rom())
        self.assertEqual(rc,0)
        self.assertEqual(s['ram0'],0x5a)
        self.assertEqual(s['frames'],12)
        self.assertEqual(s['video_blocks'],24)
        self.assertEqual(s['audio_samples'],12*735)
        self.assertLess(s['state_bytes'],100*1024)
        self.assertGreater(s['audio_max'],s['audio_min'])
        self.assertTrue(picture.startswith(b'P6\n256 240\n255\n'))
        pixels=picture.split(b'\n',3)[3]
        self.assertEqual(len(pixels),256*240*3)
        self.assertGreater(len(set(pixels[i:i+3] for i in range(0,len(pixels),3))),1)

    def test_every_controller_bit(self):
        # ROM publishes a complete sample, never a partially shifted pad byte.
        for pad in (0,1,2,4,8,16,32,64,128,255):
            with self.subTest(pad=pad):
                rc,s,_=self.run_rom(make_rom(),pad)
                self.assertEqual(rc,0)
                self.assertEqual(s['ram1'],pad)

    def test_nrom256(self):
        rc,s,_=self.run_rom(make_rom(2))
        self.assertEqual(rc,0);self.assertEqual(s['ram0'],0x5a)

    def test_nrom_writes_are_ignored(self):
        rc,s,_=self.run_rom(make_rom(test_rom_write=True))
        self.assertEqual(rc,0);self.assertEqual(s['ram0'],0x5a)

    def test_cnrom_banks_and_read_only_rom(self):
        for prg in (1,2):
            for chr_count in (1,2,4):
                for mirror in (0,1):
                    with self.subTest(prg=prg,chr=chr_count,mirror=mirror):
                        rc,s,_=self.run_rom(make_cnrom_rom(prg,chr_count,mirroring=mirror),pad=0xa5)
                        self.assertEqual(rc,0);self.assertEqual(s['ram0'],0x5a)
                        self.assertEqual(s['ram1'],0xa5)

    def test_cnrom_rendered_bank_switch(self):
        hashes=set()
        for bank in range(4):
            rc,s,_=self.run_rom(make_cnrom_rom(chr_banks=4,final_bank=bank))
            self.assertEqual(rc,0);self.assertEqual(s['ram0'],0x5a)
            hashes.add(s['video_hash'])
        self.assertEqual(len(hashes),4)

    def test_cnrom_truncation_and_unsupported_variants(self):
        rom=make_cnrom_rom()
        for invalid in (rom[:-1],rom+b'\0'):
            rc,s,_=self.run_rom(invalid)
            self.assertNotEqual(rc,0);self.assertEqual(s['result'],-1)
            self.assertEqual(s['frames'],0)
        for off,value in ((4,0),(4,3),(5,0),(5,3),(5,8),(6,0x32),(6,0x34),
                          (6,0x38),(6,0x40),(7,8),(7,0x10),(8,1)):
            invalid=bytearray(rom);invalid[off]=value
            with self.subTest(offset=off,value=value):
                rc,s,_=self.run_rom(invalid)
                self.assertNotEqual(rc,0);self.assertEqual(s['result'],-2)
                self.assertEqual(s['frames'],0)

    def test_optional_rom50_smoke(self):
        path=Path(__file__).resolve().parents[1]/'roms/50-coin-shine.nes'
        if not path.exists(): self.skipTest('User-selected ROM is not redistributed')
        rom=path.read_bytes()
        self.assertEqual(hashlib.sha256(rom).hexdigest(),
                         '6cd4e6052a16ce014cbd0f175830b77ebe4b236615409f2156de00179cc8311d')
        hashes=[]
        for pad in (0,8):
            rc,s,_=self.run_rom(rom,pad=pad,frames=600)
            self.assertEqual(rc,0);self.assertEqual(s['frames'],600)
            self.assertEqual(s['video_blocks'],1200)
            self.assertEqual(s['audio_samples'],441000)
            self.assertGreater(s['audio_max'],s['audio_min'])
            hashes.append(s['video_hash'])
        self.assertNotEqual(*hashes)

    def test_invalid_and_truncated_rejected(self):
        rom=make_rom()
        for invalid in (b'',rom[:15],b'bad!'+rom[4:],rom[:-1],rom+b'\0'):
            with self.subTest(size=len(invalid)):
                rc,s,_=self.run_rom(invalid)
                self.assertNotEqual(rc,0);self.assertEqual(s['result'],-1)
                self.assertEqual(s['frames'],0)

    def test_unsupported_formats_rejected(self):
        for off,value in ((4,0),(4,3),(5,0),(6,0x10),(6,2),(6,4),(6,8),(7,8),(9,1)):
            rom=bytearray(make_rom());rom[off]=value
            with self.subTest(offset=off,value=value):
                rc,s,_=self.run_rom(rom)
                self.assertNotEqual(rc,0);self.assertEqual(s['result'],-2)
                self.assertEqual(s['frames'],0)

    def test_video_failure_exits(self):
        rc,s,_=self.run_rom(make_rom(),fail=True)
        self.assertNotEqual(rc,0);self.assertEqual(s['result'],-3)
        self.assertEqual(s['frames'],1)

    def test_deterministic(self):
        self.assertEqual(self.run_rom(make_rom())[1],self.run_rom(make_rom())[1])

    def test_target_gate(self):
        subprocess.run([str(EXE),'--target-gate'],check=True,timeout=5)


if __name__ == '__main__': unittest.main()
