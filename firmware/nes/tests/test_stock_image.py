"""Private-backup packaging tests: offline; never print decoded private data."""
from pathlib import Path
import struct
import sys
import unittest

HERE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
import build_stock_image as pack


class StockImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder=HERE/'build/boot-peripherals-rom50'
        if not pack.BACKUP.is_file() or not (folder/'fm1-nes-boot-candidate.elf').is_file():
            raise unittest.SkipTest('Private stock backup and integrated ELF required')
        cls.raw=pack.BACKUP.read_bytes()
        cls.elf=(folder/'fm1-nes-boot-candidate.elf').read_bytes()
        cls.app=(folder/'fm1-nes-boot-candidate.app.bin').read_bytes()
        cls.candidate,cls.report=pack.build_candidate(cls.raw,cls.elf,cls.app)

    def test_real_integrated_round_trip(self):
        self.assertEqual(len(self.candidate),1048576)
        self.assertTrue(all(self.report['checks'].values()))
        self.assertFalse(self.report['flashable'])
        self.assertFalse(self.report['device_operations_performed'])
        self.assertEqual(pack.decode_application(self.candidate)[0],self.app)

    def test_current_baseline_preserves_two_outside_bytes(self):
        current=pack.CURRENT_BACKUP.read_bytes()
        rebased=pack.preserve_current_baseline(self.raw,self.candidate,current,self.app)
        self.assertEqual([i for i,(a,b) in enumerate(zip(rebased,self.candidate)) if a!=b],
                         [0xee046,0xee069])
        self.assertEqual(rebased[0xee000:0xef000],current[0xee000:0xef000])
        self.assertEqual(pack.decode_application(rebased)[0],self.app)

    def test_current_baseline_rejects_unreviewed_changes(self):
        current=bytearray(pack.CURRENT_BACKUP.read_bytes())
        for offset in (0,0xee046,0xee047,0xfffff):
            modified=current.copy();modified[offset]^=1
            with self.assertRaisesRegex(ValueError,'current backup'):
                pack.preserve_current_baseline(self.raw,self.candidate,modified,self.app)

    def test_optional_smb1_integrated_package(self):
        folder=HERE/'build/boot-peripherals-smb1'
        if not (folder/'build-manifest.json').exists():self.skipTest('SMB1 build absent')
        elf,app=pack.load_built_profile('peripherals','smb1')
        candidate,report=pack.build_candidate(self.raw,elf,app,rom='smb1')
        self.assertEqual(report['static_boot_audit']['embedded_rom_sha256'],
                         'f61548fdf1670cffefcc4f0b7bdcdd9eaba0c226e3b74f8666071496988248de')
        current=pack.CURRENT_BACKUP.read_bytes()
        updated=pack.preserve_current_baseline(self.raw,candidate,current,app)
        self.assertEqual(pack.hashlib.sha256(updated).hexdigest(),
                         'b61723ffa318aea3c372f29d4f0e058cf00843cd0d66302468b936c9bdf0eae5')
        self.assertEqual(pack.decode_application(updated)[0],app)
        with self.assertRaises(ValueError):pack.build_candidate(self.raw,elf,app,rom='rom50')

    def test_deterministic_and_does_not_mutate_input(self):
        self.assertEqual(pack._replace_application(self.raw,self.app),self.candidate)
        self.assertEqual(pack.hashlib.sha256(self.raw).hexdigest(),pack.BACKUP_SHA)

    def test_v14_smb1_preserves_every_non_application_byte(self):
        raw=pack.V14_BACKUP.read_bytes()
        elf,app=pack.load_built_profile('peripherals','smb1')
        image,report=pack.build_candidate(raw,elf,app,rom='smb1')
        self.assertEqual(report['backup_sha256'],pack.V14_SHA)
        self.assertEqual(report['stock_area_size_preserved'],585627)
        self.assertEqual(pack.decode_application(image)[0],app)
        self.assertEqual(image[:0x4000],raw[:0x4000])
        self.assertEqual(image[0x4040:pack.APP],raw[0x4040:pack.APP])
        self.assertEqual(image[pack.APP+len(app):],raw[pack.APP+len(app):])
        self.assertEqual(report['changed_sector_offsets'],[hex(i) for i in range(0x4000,0x2d000,0x1000)])
        bad=bytearray(raw);bad[0xee046]^=1
        with self.assertRaisesRegex(ValueError,'exact 1 MiB'):
            pack.build_candidate(bytes(bad),elf,app,rom='smb1')

    def test_preserved_regions_reject_changes(self):
        for offset in (0,0xa0,0x38d0,0x4040,0x40e0,pack.APP+len(self.app),
                       0x9275c,0x94000,0xed000,0xff000,0xfffff):
            with self.subTest(offset=offset):
                candidate=bytearray(self.candidate);candidate[offset]^=1
                with self.assertRaisesRegex(ValueError,'preserved bytes'):
                    pack.verify_candidate(self.raw,candidate,self.app)

    def test_corrupt_payload_and_directory_rejected(self):
        for offset in (pack.AREA,pack.AREA+32,pack.APP,pack.APP+len(self.app)-1):
            with self.subTest(offset=offset):
                candidate=bytearray(self.candidate);candidate[offset]^=1
                with self.assertRaises(ValueError):
                    pack.verify_candidate(self.raw,candidate,self.app)

    def test_matching_header_crc_cannot_hide_wrong_area_crc(self):
        fw,_,_,_,_,key=pack._decode_layout(self.candidate)
        fw[pack.AREA+2]^=1
        struct.pack_into('<H',fw,pack.AREA,pack.jl_crc16(fw[pack.AREA+2:pack.AREA+32]))
        pack.jl_sfc_cipher(fw,pack.AREA,32,pack.AREA,key)
        candidate=bytearray(self.candidate)
        candidate[pack.AREA:pack.AREA+32]=fw[pack.AREA:pack.AREA+32]
        with self.assertRaisesRegex(ValueError,'area payload CRC'):
            pack.verify_candidate(self.raw,candidate,self.app)

    def test_partial_cipher_blocks_preserve_neighbors(self):
        # Container-only cases, intentionally not executable images. The CLI
        # never uses this transform without the full ELF/static audit first.
        for length in (1,31,32,33,65):
            with self.subTest(length=length):
                app=bytes((i*17)&255 for i in range(length))
                image=pack._replace_application(self.raw,app)
                result=pack.verify_candidate(self.raw,image,app)
                self.assertTrue(result['checks']['application_round_trip'])
                self.assertEqual(image[pack.APP+length:],self.raw[pack.APP+length:])

    def test_reject_wrong_backup_and_application_size(self):
        with self.assertRaisesRegex(ValueError,'exact 1 MiB'):
            pack._replace_application(self.raw[:-1],self.app)
        for app in (b'',bytes(pack.STOCK_APP_SIZE+1)):
            with self.assertRaisesRegex(ValueError,'does not fit'):
                pack._replace_application(self.raw,app)

    def test_reject_invalid_executable_before_packaging(self):
        with self.assertRaises(ValueError):pack.build_candidate(self.raw,b'not ELF',self.app)
        with self.assertRaises(ValueError):pack.build_candidate(self.raw,self.elf,self.app[:-1])


if __name__=='__main__':unittest.main()
