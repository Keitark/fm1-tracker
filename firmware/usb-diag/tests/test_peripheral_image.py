"""Offline peripheral-image negative audits and stock encoder evidence."""
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_usb_diag import LinkTests, HERE, ROOT, audit

class PeripheralImageTests(LinkTests):
    @classmethod
    def setUpClass(cls):
        folder=Path(os.environ.get('FM1_PERIPHERAL_BUILD',str(HERE/'build/peripherals-v1')))
        cls.elf=(folder/'fm1-usb-diag.elf').read_bytes()
        cls.app=(folder/'fm1-usb-diag.app.bin').read_bytes()
        cls.no_keys=json.loads((folder/'build-manifest.json').read_text()).get('nes_no_keys',False)
        cls.input_recovery=json.loads((folder/'build-manifest.json').read_text()).get('nes_input_recovery',False)
        cls.key_irq=json.loads((folder/'build-manifest.json').read_text()).get('keyscan_irq',False)
        cls.audio_priority=json.loads((folder/'build-manifest.json').read_text()).get('nes_audio_priority',False)
        cls.live_fx=json.loads((folder/'build-manifest.json').read_text()).get('nes_live_fx',False)
        cls.channel_fx=json.loads((folder/'build-manifest.json').read_text()).get('nes_channel_fx',False)
        cls.rgb444=json.loads((folder/'build-manifest.json').read_text()).get('lcd_rgb444',False)
        cls.direct=json.loads((folder/'build-manifest.json').read_text()).get('lcd_direct',False)
        cls.tiles=json.loads((folder/'build-manifest.json').read_text()).get('nes_tiles',False)
        cls.volume=json.loads((folder/'build-manifest.json').read_text()).get('nes_volume',False)
        cls.symbols={}
        for line in (folder/'symbols.txt').read_text().splitlines():
            parts=line.split()
            if len(parts)==4:cls.symbols[parts[3]]=(int(parts[0],16),int(parts[1],16))
        cls.nes='fm1_nes_player_run' in cls.symbols
    def check(self,elf=None,app=None):
        return audit(self.elf if elf is None else elf,self.app if app is None else app,
                     usb_only=True,usb_peripheral_tests=True,usb_nes=self.nes,rom='smb1' if self.nes else 'rom50',require_boot_trace=True,require_board_power=True)
    def test_nes_profile_requires_explicit_audit(self):
        if not self.nes:self.skipTest('Not NES profile')
        with self.assertRaises(ValueError):
            audit(self.elf,self.app,usb_only=True,usb_peripheral_tests=True,require_boot_trace=True,require_board_power=True)
    def test_nes_cartridge_corruption_rejected(self):
        if not self.nes:self.skipTest('Not NES profile')
        address,_=self.symbols['cartridge']
        with self.assertRaisesRegex(ValueError,'cartridge hash'):self.check(*self.patch(address,b'\x00'))
    def test_peripheral_link(self):
        report=self.check()
        self.assertTrue(report['usb_peripheral_tests'])
        self.assertTrue(report['usb_bootloader_entry'])
    def test_real_link(self):
        result=self.check();self.assertTrue(result['usb_only'])
        self.assertEqual(result['cartridge'],'smb1' if self.nes else None)
        self.assertFalse(result['flashable']);self.assertEqual(result['sdk_delayed_integrity']['scheduled_delay_ms'],8000)
    def test_alink_return_corruption_rejected(self):
        address,size=self.symbols['fm1_test_alink_isr']
        with self.assertRaisesRegex(ValueError,'ALINK ISR'):self.check(*self.patch(address+size-2,b'\x80\x00'))
    def test_async_lcd_return_corruption_rejected(self):
        if 'fm1_lcd_spi1_isr' not in self.symbols:self.skipTest('Not async LCD profile')
        address,size=self.symbols['fm1_lcd_spi1_isr']
        with self.assertRaisesRegex(ValueError,'LCD SPI1 ISR'):self.check(*self.patch(address+size-2,b'\x80\x00'))
    def test_async_lcd_storage(self):
        if 'fm1_lcd_frames' not in self.symbols:self.skipTest('Not async LCD profile')
        address,size=self.symbols['fm1_lcd_frames']
        self.assertEqual(size,172800 if self.rgb444 else 230400);self.assertEqual(address%64,0)
        self.assertEqual('fm1_lcd_pack_native_rgb444' in self.symbols,self.direct)
        if not self.direct:self.assertEqual('fm1_lcd_pack_rgb444' in self.symbols,self.rgb444)
        self.assertGreaterEqual(self.check()['heap_bytes'],128*1024)
    def test_encoder_table_corruption_rejected(self):
        address,_=self.symbols['fm1_encoder_contacts']
        with self.assertRaisesRegex(ValueError,'encoder'):self.check(*self.patch(address,b'\x11'))
    def test_tile_profile_and_ram_table(self):
        self.assertEqual('fm1_tile_expand' in self.symbols,self.tiles)
        if self.tiles:
            self.assertEqual(self.symbols['fm1_tile_expand'][1],512)
            self.check()
        self.assertNotIn('fm1_tile_test_reference',self.symbols)
    def test_tile_table_corruption_rejected(self):
        if not self.tiles:self.skipTest('Not tile profile')
        address,_=self.symbols['fm1_tile_expand']
        with self.assertRaisesRegex(ValueError,'Tile table data'):
            self.check(*self.patch(address,b'\xff'))
    def test_volume_reader_profile(self):
        self.assertEqual('fm1_volume_start' in self.symbols,self.volume)
        if self.volume:
            self.assertIn(b'VOLUME adc=4 gpio=PB6 raw=',self.app)
            self.assertNotIn('adc_init',self.symbols)
            self.assertNotIn('adc_isr',self.symbols)
            self.check()
    def test_task_priority_corruption_rejected(self):
        address,_=self.symbols['task_info_table']
        with self.assertRaisesRegex(ValueError,'task registration'):self.check(*self.patch(address+20*5+4,b'\x1e'))
    def test_boot_reset_corruption_rejected(self):
        address,_=self.symbols['nvram_set_boot_state']
        with self.assertRaisesRegex(ValueError,'UBOOT'):self.check(*self.patch(address+26,b'\x43\x20'))
    def test_boot_irq_disable_corruption_rejected(self):
        address,_=self.symbols['go_mask_usb_updata']
        with self.assertRaises(ValueError):self.check(*self.patch(address,b'\0\0'))
    def test_stock_encoder_mapping(self):
        stock=(ROOT/'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin').read_bytes()
        self.assertEqual(hashlib.sha256(stock).hexdigest(),'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543')
        contacts=stock[0x020462fc-0x02000120:0x020462fc-0x02000120+14]
        source=(HERE/'peripheral_logic.c').read_text()
        table=re.search(r'fm1_encoder_contacts\[14\]=\{([^}]+)\}',source).group(1)
        self.assertEqual(bytes(int(n.strip(),16) for n in table.split(',')),contacts)
        ram=lambda address,n:stock[0x847c0+address-0x1c00000:0x847c0+address-0x1c00000+n]
        self.assertEqual(ram(0x1c04700,6),bytes.fromhex('c3 ff fc 62 04 02'))
        self.assertEqual(ram(0x1c04706,8),bytes.fromhex('49 e0 14 28 48 e0 82 41'))
        # Equal-candidate branch then accepted-state comparison; first differing
        # sample stores candidate and returns without applying direction masks.
        self.assertEqual(ram(0x1c04750,4),bytes.fromhex('85 e8 1a 40'))
        self.assertEqual(ram(0x1c04788,6),bytes.fromhex('e0 f1 14 70 9d 40'))
    def test_identity_and_flash_fit(self):
        self.assertIn(b'PERIPHERAL-TEST/1',self.app)
        self.assertIn(b'profile=peripheral-test/1',self.app)
        self.assertLess(len(self.app),512*1024)
    def test_dma_identity_and_register_logs(self):
        if 'fm1_lcd_dma_params' not in self.symbols:self.skipTest('Not the DMA variant')
        self.assertTrue(b'LCD=STOCK-DMA' in self.app or b'LCD=STOCK-SEQ' in self.app)
        self.assertIn('fm1_display_snapshot',self.symbols)
        for label in (b'LCDREG phase=',b'OUT=',b'DIEH=',b'MUX5=',b'DMA_ADR=',b'DMA_CNT='):
            self.assertIn(label,self.app)
        self.assertEqual(self.symbols['fm1_lcd_dma_params'][1],64)
        self.assertEqual(self.symbols['fm1_lcd_dma_params'][0]%64,0)
    def test_stock_pa2_setup_evidence(self):
        stock=(ROOT/'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin').read_bytes()
        self.assertEqual(hashlib.sha256(stock).hexdigest(),'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543')
        at=lambda address,n:stock[address-0x2000120:address-0x2000120+n]
        # r0=2; SPI2 count write and enable preserve r0; r1=0; GPIO output call.
        self.assertEqual(at(0x20044fa,14),bytes.fromhex('40 22 c0 64 00 ef 01 40 41 20 bf ea 5b fb'))
        # PC7 high, then PC8/9/10 direction=output; SPI enable precedes divider.
        self.assertEqual(at(0x202308a,8),bytes.fromhex('48 27 41 21 bf ea 96 05'))
        self.assertEqual(at(0x20230b6,6),bytes.fromhex('e0 60 41 ea 04 60'))
    def test_nes_pixel_dma_placement(self):
        if 'fm1_lcd_dma_pixels' not in self.symbols:self.skipTest('Not the large DMA variant')
        self.assertTrue(self.nes)
        self.assertEqual(self.symbols['fm1_lcd_dma_pixels'][1],3840)
        self.assertEqual(self.symbols['fm1_lcd_dma_pixels'][0]%64,0)
        self.check()
    def test_dma_invalid_placement_rejected(self):
        if 'fm1_lcd_dma_params' not in self.symbols:self.skipTest('Not the DMA variant')
        shoff=struct.unpack_from('<I',self.elf,32)[0]
        count=struct.unpack_from('<H',self.elf,48)[0]
        sections=[struct.unpack_from('<10I',self.elf,shoff+i*40) for i in range(count)]
        symtab=next(s for s in sections if s[1]==2)
        strings=sections[symtab[6]]
        strings=self.elf[strings[4]:strings[4]+strings[5]]
        for symbol in ('fm1_lcd_dma_params','fm1_lcd_dma_pixels','fm1_key_dma'):
            if symbol not in self.symbols:continue
            for offset in range(symtab[4],symtab[4]+symtab[5],16):
                name=struct.unpack_from('<I',self.elf,offset)[0]
                if strings[name:strings.find(b'\0',name)]==symbol.encode():break
            else:self.fail('Missing DMA symbol table entry')
            for address in (self.symbols[symbol][0]+1,0x02000140):
                elf=bytearray(self.elf);struct.pack_into('<I',elf,offset+4,address)
                with self.assertRaisesRegex(ValueError,'aligned internal RAM BSS'):self.check(elf=elf)
    def test_key_dma_profile(self):
        if 'fm1_key_dma' not in self.symbols:self.skipTest('Not the key DMA variant')
        self.assertEqual(self.symbols['fm1_key_dma'][1],64)
        self.assertEqual(self.symbols['fm1_key_dma'][0]%64,0)
        if not self.no_keys and not self.key_irq:
            self.assertTrue(b'KEYSPI MODE=DMA2-POLLED BYTES=2 IRQ=OFF' in self.app)
        self.check()
    def test_nes_input_profile(self):
        neutral=b'NES INPUT=NEUTRAL SCANNER=DISABLED'
        self.assertEqual(neutral in self.app,self.no_keys)
        if self.no_keys:
            self.assertTrue(self.nes)
            self.assertFalse(b'KEYSPI MODE=DMA2-POLLED BYTES=2 IRQ=OFF' in self.app)
    def test_nes_input_recovery_profile(self):
        banner=b'NES INPUT RECOVERY=BOUNDED RETRIES=3 BACKOFF_MS=50 HOLD_MS=100'
        self.assertEqual(banner in self.app,self.input_recovery)
        if self.input_recovery:
            self.assertTrue(self.nes)
            self.assertFalse(self.no_keys)
            self.assertTrue(b'GAME=CONTINUES' in self.app)
    def test_nes_key_irq_profile(self):
        self.assertEqual(b'KEYSPI MODE=DMA2-IRQ BYTES=2 IRQ=37 PRIORITY=5' in self.app,self.key_irq)
        if self.key_irq:
            self.assertTrue(self.input_recovery)
            self.assertFalse(b'KEYSPI MODE=DMA2-POLLED BYTES=2 IRQ=OFF' in self.app)
            address,size=self.symbols['fm1_nes_keyscan_isr']
            with self.assertRaisesRegex(ValueError,'Keyscan ISR'):
                self.check(*self.patch(address+size-2,b'\x80\x00'))
    def test_nes_audio_priority_profile(self):
        banner=b'NES AUDIO CLOCK=DAC PRIME=1470 TARGET=2205 REBUFFER=ON'
        self.assertEqual(banner in self.app,self.audio_priority)
        if self.audio_priority:
            self.assertTrue(self.nes)
            self.assertIn(b'AUTOSKIP=5',self.app)
            self.assertIn(b'prime_silence=',self.app)
            self.assertIn(b'rebuffers=',self.app)
            self.assertIn('nes_buffered',self.symbols)
    def test_nes_live_fx_profile(self):
        banner=b'FX KNOB1=CUTOFF KNOB2=RES KNOB3=LFO-RATE KNOB4=LFO-DEPTH'
        self.assertEqual(banner in self.app,self.live_fx)
        if self.live_fx:
            self.assertTrue(self.audio_priority and self.key_irq)
            if self.channel_fx:
                self.assertIn(b'FX channel=%s mode=%s cutoff=',self.app)
                self.assertIn(b'SELECT=CHANNEL ALGORITHM=MODE PRESETS=PATCH',self.app)
            else:
                self.assertIn(b'FX mode=%s cutoff=',self.app)
                self.assertIn(b'SELECT=MIX ALGORITHM=MODE PRESETS=PATCH',self.app)
            self.assertIn('nes_fx_encoders',self.symbols)
    def test_nes_channel_capture_profile(self):
        banner=b'FX CHANNELS=PULSE1,PULSE2,TRIANGLE,NOISE,ALL'
        self.assertEqual(banner in self.app,self.channel_fx)
        if self.channel_fx:
            self.assertTrue(self.live_fx)
            self.assertEqual(self.symbols['fm1_voice_samples'][1],4*735)
            self.assertIn(b'BANKS=INDEPENDENT',self.app)
    def test_stock_initial_fill_overrides_table_window(self):
        stock=(ROOT/'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin').read_bytes()
        v14=(ROOT/'private-backups/post-official-update-20260927-230806/analysis/app-FM-1_010.bin').read_bytes()
        self.assertEqual(hashlib.sha256(stock).hexdigest(),'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543')
        self.assertEqual(hashlib.sha256(v14).hexdigest(),'54a32371e8fc19e7492210e958023762f7335e354a4cab41cb26c35d6f0f0443')
        at=lambda address,n:stock[address-0x2000120:address-0x2000120+n]
        # r0=r1=0xf0; rev8 both; d[sp+80]=r1_r0.
        self.assertEqual(at(0x2021604,16),bytes.fromhex('78 30 79 30 70 e0 00 11 70 e0 00 00 d0 e9 51 00'))
        # DMA four bytes from sp+80 then sp+84 to CASET/RASET parameters.
        self.assertEqual(at(0x2021654,8),bytes.fromhex('cb 90 93 63 44 ea 04 10'))
        self.assertEqual(at(0x2021694,8),bytes.fromhex('cb 94 93 63 44 ea 04 10'))
        # 40 bytes per block, 0xb40 blocks = 115200, unchanged in V14.
        self.assertEqual(at(0x20216da,4),bytes.fromhex('44 ea 28 10'))
        self.assertEqual(at(0x20216f0,6),bytes.fromhex('21 ff 34 3e eb ff'))
        self.assertEqual(at(0x2021600,282),v14[0x20217b8-0x2000120:0x20217b8-0x2000120+282])
        source=(HERE.parent/'nes/boot/display_test.c').read_text()
        self.assertIn('stock_window[4]={0,0,0,0xf0}',source)
        if b'LCD=STOCK-FILL' in self.app:
            self.assertIn(b'LCD STOCK-FILL x=000000f0 y=000000f0 bytes=115200 value=ff continuous-CS',self.app)

if __name__=='__main__':unittest.main()
