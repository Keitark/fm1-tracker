"""Offline WL82 CDC diagnostics and optional composite UAC1 link; no device I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
NES=HERE.parent/'nes'
sys.path.insert(0,str(NES))
from build_boot import SDK,SDK_PIN,TC,MAKE,make_list,sdk_path
from audit_boot import audit
from verify_stock_power import STOCK,verify
import vendor_overlay
from embed_rom50 import generate,rom_path,ROMS

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--controller',type=int,choices=(0,),required=True,
                   help='Only USB0 has a reviewed link contract; connector routing is still unverified')
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--peripheral-tests',action='store_true')
    p.add_argument('--factory-diag',action='store_true',help='Asset-free FM1-FORGE diagnostics with persistent LCD/controls, key tones and composite CDC/UAC1')
    p.add_argument('--forge-no-uac',action='store_true',help='Factory diagnostics with CDC only; no audio USB class')
    p.add_argument('--forge-clock',choices=('sdk-default','sdk-320'),default='sdk-default')
    p.add_argument('--forge-lcd',choices=('stock-dma','spi15-rgb444','spi30-rgb444'),default='stock-dma')
    p.add_argument('--forge-user-module',type=Path,help='Explicit self-authored C module source, included in source-hash closure')
    hal_names=('buttons','encoders','volume','lcd','audio')
    for name in hal_names:p.add_argument('--forge-hal-'+name,type=int,choices=(0,1),help='Select user '+name+' HAL access; fixed diagnostics/recovery remain active (default 1)')
    p.add_argument('--mdx',action='store_true',help='Standalone flash-demo MDX/PDX karaoke player with volatile CDC uploads')
    p.add_argument('--mod',action='store_true',help='ProTracker-style four-channel MOD editor with volatile CDC file transfer')
    p.add_argument('--mod-source-dir',type=Path,
                   help='Build a preserved MOD source profile, such as a knob-only correction of an installed revision')
    p.add_argument('--mod-default-song',type=Path,help='4-channel MOD to embed as the read-only boot default')
    p.add_argument('--mod-default-song-sha256',help='Required SHA-256 lock for --mod-default-song')
    p.add_argument('--mod-bank',type=Path,help='Validated read-only Active Bank image to embed for standalone audition/import')
    p.add_argument('--mod-bank-sha256',help='Required SHA-256 lock for --mod-bank')
    p.add_argument('--nes-smb1',action='store_true',help='Autostart private hash-pinned SMB1 with CDC recovery and stock LCD sequence')
    p.add_argument('--nes-auto-skip',action='store_true',help='Opt-in bounded LCD-only automatic frame skipping; requires --nes-smb1')
    p.add_argument('--nes-audio-priority',action='store_true',help='DAC queue pacing, prefill and bounded LCD skipping; requires --nes-smb1')
    p.add_argument('--nes-live-fx',action='store_true',help='Live encoder VCF/LFO effects; requires --keyscan-irq and --nes-audio-priority')
    p.add_argument('--nes-channel-fx',action='store_true',help='Independent pulse1/pulse2/triangle/noise/master effects; requires --nes-live-fx')
    p.add_argument('--nes-render-skip',action='store_true',help='Skip hidden-frame pixels, retaining sprite-zero/CPU/APU; requires NES audio priority')
    p.add_argument('--nes-profile',action='store_true',help='Exclusive wall-time profiling; requires --nes-render-skip')
    p.add_argument('--nes-tiles',action='store_true',help='RAM bitplane lookup and eight-pixel tile rows; requires --nes-render-skip')
    p.add_argument('--nes-volume',action='store_true',help='PB6 ADC4 digital volume; requires paced scanner and audio priority')
    p.add_argument('--lcd-async',action='store_true',help='Double-buffered SPI1 IRQ display; requires --nes-render-skip')
    p.add_argument('--lcd-spi15',action='store_true',help='Bench test: idle SPI1 BAUD3 after stock init, requires async and 60MHz LSB')
    p.add_argument('--lcd-spi30',action='store_true',help='Bench test: idle SPI1 BAUD1 after stock init, requires async and 60MHz LSB')
    p.add_argument('--lcd-rgb444',action='store_true',help='Packed 12-bit LCD pixels after stock RGB565 startup; requires async')
    p.add_argument('--lcd-direct',action='store_true',help='Fuse native scaling and RGB444 packing directly into owned DMA frame; requires RGB444')
    p.add_argument('--keyscan-dma2',action='store_true',help='Opt-in stock-order two-byte SPI2 DMA with polled completion; requires --nes-smb1')
    p.add_argument('--keyscan-irq',action='store_true',help='NES-only interrupt DMA scanner; requires DMA2 and input recovery')
    p.add_argument('--keyscan-paced',action='store_true',help='One IRQ scan burst per 1ms; requires --keyscan-irq')
    p.add_argument('--nes-no-keys',action='store_true',help='NES isolation: skip physical scanner and supply neutral input; requires --nes-smb1')
    p.add_argument('--nes-input-recovery',action='store_true',help='Contain NES scanner faults; three spaced retries, held-key continuity and bounded stale input')
    p.add_argument('--lcd-stock-fill',action='store_true',help='Replay the stock initial-fill window and continuous white stream')
    p.add_argument('--lcd-stock-dma',action='store_true',help='Stock setup-before-delay, DMA parameters, fixed register snapshots; requires --lcd-stock-fill')
    p.add_argument('--lcd-stock-sequence',action='store_true',help='Stock PA2/GPIO setup, scheduler delays and 40-byte DMA black first fill; requires --lcd-stock-dma')
    a=p.parse_args(); out=a.out.resolve(); out.mkdir(parents=True,exist_ok=True)
    if a.factory_diag:
        if a.mdx or a.mod or a.nes_smb1:p.error('Factory diagnostic excludes player profiles and external media')
        if a.forge_clock=='sdk-320' and a.forge_lcd!='stock-dma':p.error('SDK 320 MHz uses a 53 MHz LSB bus; fast LCD presets require 60 MHz')
        a.peripheral_tests=True
        a.lcd_stock_fill=a.lcd_stock_dma=a.lcd_stock_sequence=True
        a.lcd_async=True
        a.lcd_spi15=a.forge_lcd=='spi15-rgb444';a.lcd_spi30=a.forge_lcd=='spi30-rgb444'
        a.lcd_rgb444=a.lcd_spi15 or a.lcd_spi30
    elif a.forge_no_uac or a.forge_user_module or a.forge_clock!='sdk-default' or a.forge_lcd!='stock-dma' or any(getattr(a,'forge_hal_'+name) is not None for name in hal_names):p.error('Forge options require --factory-diag')
    hal_access={name:True if getattr(a,'forge_hal_'+name) is None else bool(getattr(a,'forge_hal_'+name)) for name in hal_names}
    usb_audio=a.factory_diag and not a.forge_no_uac
    if (a.mdx or a.mod) and (a.nes_smb1 or not a.lcd_stock_sequence):p.error('Player profiles require stock LCD sequence and exclude NES')
    if a.mdx and a.mod:p.error('Select only one player profile')
    if a.mod and not a.peripheral_tests:p.error('--mod requires --peripheral-tests')
    if a.mod_source_dir and not a.mod:p.error('--mod-source-dir requires --mod')
    if bool(a.mod_default_song)!=bool(a.mod_default_song_sha256):p.error('The default MOD path and SHA-256 lock are both required')
    if a.mod_default_song and not a.mod:p.error('--mod-default-song requires --mod')
    if bool(a.mod_bank)!=bool(a.mod_bank_sha256):p.error('The Active Bank path and SHA-256 lock are both required')
    if a.mod_bank and not a.mod:p.error('--mod-bank requires --mod')
    if a.lcd_stock_fill and not a.peripheral_tests:p.error('--lcd-stock-fill requires --peripheral-tests')
    if a.lcd_stock_dma and not a.lcd_stock_fill:p.error('--lcd-stock-dma requires --lcd-stock-fill')
    if a.lcd_stock_sequence and not a.lcd_stock_dma:p.error('--lcd-stock-sequence requires --lcd-stock-dma')
    if a.nes_smb1 and not a.lcd_stock_sequence:p.error('--nes-smb1 requires --lcd-stock-sequence')
    if a.nes_auto_skip and not a.nes_smb1:p.error('--nes-auto-skip requires --nes-smb1')
    if a.nes_audio_priority and not a.nes_smb1:p.error('--nes-audio-priority requires --nes-smb1')
    if a.nes_live_fx and (not a.keyscan_irq or not a.nes_audio_priority):p.error('--nes-live-fx requires --keyscan-irq and --nes-audio-priority')
    if a.nes_channel_fx and not a.nes_live_fx:p.error('--nes-channel-fx requires --nes-live-fx')
    if a.nes_render_skip and not a.nes_audio_priority:p.error('--nes-render-skip requires --nes-audio-priority')
    if a.nes_profile and not a.nes_render_skip:p.error('--nes-profile requires --nes-render-skip')
    if a.nes_tiles and not a.nes_render_skip:p.error('--nes-tiles requires --nes-render-skip')
    if a.nes_volume and not (a.keyscan_paced and a.nes_audio_priority):p.error('--nes-volume requires paced scanner and audio priority')
    if a.lcd_async and not (a.nes_render_skip or a.mod or a.factory_diag):p.error('--lcd-async requires --nes-render-skip, --mod or --factory-diag')
    if a.lcd_spi15 and not a.lcd_async:p.error('--lcd-spi15 requires --lcd-async')
    if a.lcd_spi30 and not a.lcd_async:p.error('--lcd-spi30 requires --lcd-async')
    if a.lcd_spi30 and a.lcd_spi15:p.error('Select only one LCD SPI clock experiment')
    if a.lcd_rgb444 and not a.lcd_async:p.error('--lcd-rgb444 requires --lcd-async')
    if a.lcd_direct and (not a.lcd_rgb444 or not a.nes_smb1):p.error('--lcd-direct requires --lcd-rgb444 and --nes-smb1')
    if a.keyscan_dma2 and not a.nes_smb1:p.error('--keyscan-dma2 requires --nes-smb1')
    if a.keyscan_irq and (not a.keyscan_dma2 or not a.nes_input_recovery):p.error('--keyscan-irq requires --keyscan-dma2 and --nes-input-recovery')
    if a.keyscan_paced and not a.keyscan_irq:p.error('--keyscan-paced requires --keyscan-irq')
    if a.nes_no_keys and not a.nes_smb1:p.error('--nes-no-keys requires --nes-smb1')
    if a.nes_input_recovery and (not a.nes_smb1 or a.nes_no_keys):p.error('--nes-input-recovery requires --nes-smb1 and physical keys')
    manifest=out/'build-manifest.json'
    manifest.write_text(json.dumps({'status':'BUILDING_OR_FAILED','flashable':False})+'\n')
    log=(out/'build.log').open('w',encoding='utf-8')
    def run(args):
        result=subprocess.run(list(map(str,args)),cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        log.write(' '.join(map(str,args))+'\n'+result.stdout);log.flush()
        if result.returncode:
            print(result.stdout[-9000:]);raise RuntimeError('Build failed; see '+str(out/'build.log'))
        return result.stdout
    if run(['git','-C',SDK,'rev-parse','HEAD']).strip()!=SDK_PIN:raise ValueError('SDK pin changed')
    if run(['git','-C',SDK,'status','--porcelain']).strip():raise ValueError('SDK is dirty')
    power=verify(STOCK.read_bytes())
    text=MAKE.read_text(encoding='utf-8');flags=make_list(text,'CFLAGS')
    defines=make_list(text,'DEFINES')+['-DFM1_USB_CONTROLLER='+str(a.controller)]
    if a.peripheral_tests:defines+=['-DFM1_PERIPHERAL_TESTS=1']
    if a.factory_diag:defines+=['-DFM1_FORGE=1','-DFM1_FACTORY_DIAG=1','-DFM1_TARGET_PI32V2=1','-DFM1_KEYSCAN_DMA2=1','-DFM1_KEYSCAN_IRQ=1','-DFM1_KEYSCAN_PACED=1','-DFM1_FORGE_BOOT_CLOCK_HZ='+('320000000' if a.forge_clock=='sdk-320' else '0')]
    if a.factory_diag:defines+=['-DFM1_FORGE_HAL_'+name.upper()+'='+str(int(hal_access[name])) for name in hal_names]
    if usb_audio:defines+=['-DFM1_USB_AUDIO=1']
    if a.forge_user_module:defines+=['-DFM1_FORGE_USER_MODULE=1']
    if a.mdx:defines+=['-DFM1_MDX_PLAYER=1','-DFM1_TARGET_PI32V2=1','-DFM1_KEYSCAN_DMA2=1','-DFM1_KEYSCAN_IRQ=1','-DFM1_KEYSCAN_PACED=1']
    if a.mod:defines+=['-DFM1_MOD_PLAYER=1','-DFM1_TARGET_PI32V2=1','-DFM1_KEYSCAN_DMA2=1','-DFM1_KEYSCAN_IRQ=1','-DFM1_KEYSCAN_PACED=1']
    if a.mod_default_song:defines+=['-DFM1_MOD_EMBEDDED_DEFAULT=1']
    if a.mod_bank:defines+=['-DFM1_MOD_EMBEDDED_BANK=1']
    if a.lcd_stock_fill:defines+=['-DFM1_LCD_STOCK_FILL=1']
    if a.lcd_stock_dma:defines+=['-DFM1_LCD_STOCK_DMA=1']
    if a.lcd_stock_sequence:defines+=['-DFM1_LCD_STOCK_SEQUENCE=1']
    if a.nes_smb1:defines+=['-DFM1_NES_PLAYER=1','-DFM1_TARGET_PI32V2=1']
    if a.nes_auto_skip:defines+=['-DFM1_NES_AUTO_SKIP=1']
    if a.nes_audio_priority:defines+=['-DFM1_NES_AUDIO_PRIORITY=1']
    if a.nes_live_fx:defines+=['-DFM1_NES_LIVE_FX=1']
    if a.nes_channel_fx:defines+=['-DFM1_NES_CHANNEL_FX=1']
    if a.nes_profile:defines+=['-DFM1_NES_PROFILE=1']
    if a.nes_volume:defines+=['-DFM1_NES_VOLUME=1']
    if a.lcd_async:defines+=['-DFM1_LCD_ASYNC=1']
    if a.lcd_spi15:defines+=['-DFM1_LCD_SPI15=1']
    if a.lcd_spi30:defines+=['-DFM1_LCD_SPI30=1']
    if a.lcd_rgb444:defines+=['-DFM1_LCD_RGB444=1']
    if a.lcd_direct:defines+=['-DFM1_LCD_DIRECT=1']
    if a.keyscan_dma2:defines+=['-DFM1_KEYSCAN_DMA2=1']
    if a.keyscan_irq:defines+=['-DFM1_KEYSCAN_IRQ=1']
    if a.keyscan_paced:defines+=['-DFM1_KEYSCAN_PACED=1']
    if a.nes_no_keys:defines+=['-DFM1_NES_NO_KEYS=1']
    if a.nes_input_recovery:defines+=['-DFM1_NES_INPUT_RECOVERY=1']
    includes=['-I'+str(x) for x in (HERE,NES/'boot',NES/'include',SDK/'apps/common',SDK/'apps/common/usb',SDK/'apps/common/usb/device')]
    if a.factory_diag:includes=['-I'+str(HERE.parent/'forge'),'-I'+str(HERE.parent/'usb-audio')]+includes
    if a.forge_user_module:includes=['-I'+str(a.forge_user_module.resolve().parent)]+includes
    includes+=['-I'+str(sdk_path(x[2:])) for x in make_list(text,'INCLUDES')]
    sources=[sdk_path(x) for name in ('c_SRC_FILES','S_SRC_FILES') for x in make_list(text,name)]
    sources=[s for s in sources if s.name not in ('app_main.c','board.c','cpp_run_init.c')]
    sources += [NES/'boot'/n for n in ('board.c','boot_compat.c','boot_trace.c','board_power.c')]
    sources += [HERE/n for n in ('app_main.c','protocol.c','descriptors.c','usb_policy.c','dma.c','rx_channel.c','boot_entry.c')]
    if a.factory_diag:sources += [HERE/'packet.c']
    if a.peripheral_tests:
        sources += [HERE/n for n in (('peripheral_logic.c',) if (a.mdx or a.mod or a.factory_diag) else ('peripherals.c','peripheral_logic.c'))]
        sources += [NES/'boot/display_test.c',NES/'src/fm1_wl82_keyscan.c',NES/'src/fm1_stock_keys.c']
    speed_sources=set()
    if a.factory_diag:
        factory=[HERE.parent/'forge'/n for n in ('forge.c','target.c','user_module.c','render.c')]
        if a.forge_user_module:
            custom=a.forge_user_module.resolve()
            if not custom.is_file() or custom.suffix!='.c':raise ValueError('Forge user module requires an existing .c source')
            factory[factory.index(HERE.parent/'forge/user_module.c')]=custom
        if usb_audio:factory += [HERE.parent/'usb-audio'/n for n in ('bridge.c','profile.c','target.c')]
        factory += [NES/'src/fm1_volume.c',NES/'src/fm1_audio_queue.c']
        sources += factory;speed_sources.update(factory)
    if a.mdx:
        mdx=HERE.parent/'mdx'
        includes=['-I'+str(mdx/n) for n in ('include','vendor/retrofm','vendor/mdxtools')]+includes
        mdx_sources=list((mdx/'src').glob('*.c'))+list((mdx/'vendor/retrofm').glob('*.c'))+list((mdx/'vendor/mdxtools').glob('*.c'))+[mdx/'samples/demo.c',NES/'src/fm1_volume.c',NES/'src/fm1_audio_queue.c']
        sources+=mdx_sources;speed_sources.update(mdx_sources)
    if a.mod:
        mod_tools=HERE.parent/'protracker'
        mod=a.mod_source_dir.resolve() if a.mod_source_dir else mod_tools
        for name in ('mod.c','mod.h','usb.c','usb.h','library.c','library.h','target.c'):
            if not (mod/name).is_file():raise ValueError('Incomplete MOD source profile: '+str(mod/name))
        includes=['-I'+str(mod)]+includes
        mod_names=('mod.c','usb.c','library.c')+tuple(n for n in ('view.c','controls.c','bank.c') if (mod/n).is_file())+('target.c',)
        mod_sources=[mod/n for n in mod_names]+[NES/'src/fm1_volume.c',NES/'src/fm1_audio_queue.c']
        sources+=mod_sources;speed_sources.update(mod_sources)
        if a.mod_default_song:
            song_data=a.mod_default_song.read_bytes()
            song_sha=hashlib.sha256(song_data).hexdigest()
            if song_sha!=a.mod_default_song_sha256:
                raise ValueError('Default MOD SHA-256 does not match the requested song')
            sys.path.insert(0,str(mod_tools))
            import library_server
            library_server.validate_mod(song_data)
            embedded_source=out/'embedded_default_mod.c'
            with embedded_source.open('w',encoding='ascii',newline='\n') as stream:
                stream.write('#include <stdint.h>\nconst uint8_t fm1_mod_default_song[] __attribute__((aligned(4))) = {\n')
                for offset in range(0,len(song_data),16):
                    stream.write('  '+','.join(f'0x{value:02x}' for value in song_data[offset:offset+16])+',\n')
                stream.write('};\nconst uint32_t fm1_mod_default_song_size = sizeof(fm1_mod_default_song);\n')
            sources.append(embedded_source)
        if a.mod_bank:
            if not (mod/'bank.c').is_file() or not (mod/'bank.h').is_file():
                raise ValueError('This preserved MOD profile has no Active Bank support')
            bank_data=a.mod_bank.read_bytes()
            bank_sha=hashlib.sha256(bank_data).hexdigest()
            if bank_sha!=a.mod_bank_sha256:
                raise ValueError('Active Bank SHA-256 does not match the requested bank')
            sys.path.insert(0,str(mod_tools/'tools'))
            import fm1_bank
            fm1_bank.parse_bank(bank_data)
            bank_source=out/'embedded_active_bank.c'
            with bank_source.open('w',encoding='ascii',newline='\n') as stream:
                stream.write('#include <stdint.h>\nconst uint8_t fm1_mod_active_bank[] __attribute__((aligned(4))) = {\n')
                for offset in range(0,len(bank_data),16):
                    stream.write('  '+','.join(f'0x{value:02x}' for value in bank_data[offset:offset+16])+',\n')
                stream.write('};\nconst uint32_t fm1_mod_active_bank_size = sizeof(fm1_mod_active_bank);\n')
            sources.append(bank_source)
    if a.nes_smb1:
        core=ROOT/'references/source/peak-nes'
        if run(['git','-C',core,'rev-parse','HEAD']).strip()!='68bdfc8de570264c0e84f73766f1a1ed3591066f':raise ValueError('NES core pin changed')
        if run(['git','-C',core,'status','--porcelain']).strip():raise ValueError('NES core is dirty')
        includes=['-I'+str(NES/'port'),'-I'+str(core/'inc')]+includes
        generated=out/'generated/fm1_rom50.c';generate(rom_path('smb1'),generated,'smb1')
        sources += [NES/'src'/n for n in ('fm1_nes.c','fm1_nes_target.c','fm1_board.c','fm1_audio_queue.c')]
        sources += [core/'src'/n for n in ('nes.c','nes_cpu.c','nes_ppu.c','nes_apu.c')]+[generated]
        # Optimize emulation/conversion for speed, retaining SDK/boot flags.
        speed_sources={NES/'src'/n for n in ('fm1_nes.c','fm1_board.c','fm1_audio_queue.c')}
        speed_sources.update(core/'src'/n for n in ('nes.c','nes_cpu.c','nes_ppu.c','nes_apu.c'))
        if a.nes_volume:
            sources += [NES/'src/fm1_volume.c']
            speed_sources.add(NES/'src/fm1_volume.c')
        if a.lcd_direct:
            sources += [NES/'src/fm1_lcd_pack.c']
            speed_sources.add(NES/'src/fm1_lcd_pack.c')
        if a.nes_profile:
            sources += [NES/'src/fm1_profile.c']
            speed_sources.add(NES/'src/fm1_profile.c')
        if a.nes_live_fx:
            sources += [NES/'src/fm1_nes_fx.c']
            speed_sources.add(NES/'src/fm1_nes_fx.c')
        if a.nes_channel_fx:
            sources += [NES/'src/fm1_channel_fx.c']
            speed_sources.add(NES/'src/fm1_channel_fx.c')
    sources.append(SDK/'apps/common/usb/usb_config.c')
    overlays={}
    if a.nes_render_skip:
        original=core/'src/nes.c';target=out/'fm1-nes.c'
        transformed=vendor_overlay.nes_render_skip(original.read_text(encoding='utf-8'))
        if a.nes_tiles:transformed=vendor_overlay.nes_tiles(transformed)
        if a.nes_profile:transformed=vendor_overlay.nes_profile(transformed)
        target.write_text(transformed,encoding='utf-8')
        sources[sources.index(original)]=target
        speed_sources.remove(original);speed_sources.add(target)
        overlays[str(original)]=hashlib.sha256(original.read_bytes()).hexdigest()
    if a.nes_channel_fx:
        original=core/'src/nes_apu.c';target=out/'fm1-nes_apu.c'
        target.write_text(vendor_overlay.nes_apu(original.read_text(encoding='utf-8')),encoding='utf-8')
        sources[sources.index(original)]=target
        speed_sources.remove(original);speed_sources.add(target)
        overlays[str(original)]=hashlib.sha256(original.read_bytes()).hexdigest()
    for name,transform in (('cdc.c',vendor_overlay.cdc),('usb_device.c',vendor_overlay.device),('msd_upgrade.c',vendor_overlay.boot_entry)):
        original=SDK/'apps/common/usb/device'/name
        target=out/('fm1-'+name)
        target.write_text(transform(original.read_text(encoding='utf-8'),composite=usb_audio or name=='cdc.c') if a.factory_diag and name!='msd_upgrade.c' else transform(original.read_text(encoding='utf-8')),encoding='utf-8')
        sources.append(target);overlays[str(original)]=hashlib.sha256(original.read_bytes()).hexdigest()
    for source,name in ((SDK/'cpu/wl82/sdk_ld.c','sdk.ld'),(SDK/'cpu/wl82/sdk_used_list.c','sdk.used')):
        run([TC/'clang.exe',*flags,*defines,*includes,'-D__LD__','-E','-P',source,'-o',out/name])
    used=out/'sdk.used';used.write_text(used.read_text()+'\nmemory_init\nfm1_usb_task\ncdc_read_data\ncdc_write_data\nfm1_cdc_ready\nfm1_diag_feed\nfm1_usb_device_descriptor\nfm1_usb_config_descriptor\n')
    used.write_text(used.read_text()+'fm1_usb_rx_irq\ngo_mask_usb_updata\nnvram_set_boot_state\n')
    if a.factory_diag:used.write_text(used.read_text()+'fm1_forge_usb_command\nfm1_forge_render\nfm1_forge_clock_prepare\nfm1_forge_hal_access\nfm1_wl82_keyscan_async_raw\nfm1_usb_packet_write\nfm1_peripheral_usb_audio_quiesce\n')
    if a.forge_user_module:used.write_text(used.read_text()+'fm1_forge_user_module\n')
    if usb_audio:used.write_text(used.read_text()+'fm1_usb_audio_dac\nfm1_usb_audio_stop\nfm1_uac_desc_config\nfm1_uac_descriptor\n')
    if a.mdx:used.write_text(used.read_text()+'fm1_mdx_load\nfm1_mdx_render\nfm1_mdx_usb_command\nfm1_wl82_keyscan_async_raw\n')
    if a.mod:used.write_text(used.read_text()+'fm1_mod_open\nfm1_mod_render\nfm1_mod_usb_command\nfm1_wl82_keyscan_async_raw\n')
    if a.nes_smb1:used.write_text(used.read_text()+'fm1_board_run\nfm1_display_write\nfm1_audio_startup_process24\nfm1_nes_run\n')
    ld=(out/'sdk.ld').read_text()
    for old,new in (('*(.data)','*(.data .data.*)'),('*(.bss)','*(.bss .bss.*)')):
        ld=vendor_overlay.once(ld,old,new)
    for section in ('.syscfg.2.ops','.syscfg.1.ops'):
        ld=vendor_overlay.once(ld,'*('+section+')','/* no persistent cfg repair */')
    ld+='\nSECTIONS { /DISCARD/ : { *(.syscfg.2.ops) *(.syscfg.1.ops) } }\n'
    (out/'sdk.ld').write_text(ld)
    objects=[]
    for i,source in enumerate(sources):
        obj=out/(str(i)+'-'+source.name+'.o');objects.append(obj)
        source_flags=flags
        if source in speed_sources:
            source_flags=[f for f in flags if f not in ('-O0','-O1','-O2','-O3','-Os','-Oz','-Ofast')]+['-O2']
        run([TC/'clang.exe',*source_flags,*defines,*includes,'-c',source,'-o',obj])
    libs=[SDK/'include_lib/newlib/pi32v2-lib'/n for n in ('libm.a','libc.a','libcompiler_rt.a')]
    libs += [SDK/'cpu/wl82/liba'/n for n in ('cpu.a','event.a','system.a','cfg_tool.a','fs.a','common_lib.a','update.a')]
    elf=out/'fm1-usb-diag.elf'
    run([TC/'pi32v2-lto-wrapper.exe','-o',elf,*objects,'--start-group',*libs,'--end-group',
         '-T'+str(out/'sdk.ld'),'-M='+str(out/'fm1-usb-diag.map'),'--wrap=boot_info_init','--wrap=memory_init',
         '--undefined=memory_init','--plugin-opt=mcpu=r3','--plugin-opt=-mattr=+fprev1',
         '--plugin-opt=-pi32v2-large-program=true','--plugin-opt=-used-symbol-file='+str(used)])
    parts=[]
    for section in ('.text','.data','.dynamic_data','.ram0_data','.cache_ram_data'):
        file=out/(section[1:]+'.bin');run([TC/'llvm-objcopy.exe','-O','binary','-j',section,elf,file]);parts.append(file.read_bytes())
    app=out/'fm1-usb-diag.app.bin';app.write_bytes(b''.join(parts))
    (out/'symbols.txt').write_text(run([TC/'llvm-nm.exe','-n','-S',elf]))
    (out/'disassembly.asm').write_text(run([TC/'llvm-objdump.exe','-d','-mcpu=r3',elf]))
    report=audit(elf.read_bytes(),app.read_bytes(),usb_only=True,usb_peripheral_tests=a.peripheral_tests,usb_nes=a.nes_smb1,usb_mdx=a.mdx,usb_mod=a.mod,usb_audio=usb_audio,factory_diag=a.factory_diag,factory_user_module=bool(a.forge_user_module),factory_clock=a.forge_clock,factory_hal_access=hal_access if a.factory_diag else None,rom='smb1' if a.nes_smb1 else 'rom50',require_boot_trace=True,require_board_power=True)
    (out/'static-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    result={'status':'LINKED_USB_DIAGNOSTIC_UNTESTED','flashable':False,'usb_controller_candidate':a.controller,
            'controller_routing_verified':False,'sdk_commit':SDK_PIN,'application_bytes':app.stat().st_size,
            'application_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),
            'update_transport':'CDC verify-only; not stored or installed','flash_commit':'BLOCKED_STOCK_LAYOUT_UNQUALIFIED',
            'device_operations_performed':False,'static_audit':report,'power_evidence':power,
            'peripheral_tests':a.peripheral_tests,'serial_uboot':'implemented; transition not hardware-qualified',
            'factory_diag':a.factory_diag,'factory_schema':1 if a.factory_diag else None,'usb_audio':usb_audio,
            'factory_user_module':bool(a.forge_user_module),'forge_user_module':str(a.forge_user_module.resolve()) if a.forge_user_module else None,
            'forge_project_schema':2 if a.factory_diag else None,'forge_hal_access':hal_access if a.factory_diag else None,
            'forge_hal_access_scope':'user module callbacks and snapshot; fixed diagnostics/recovery drivers remain active' if a.factory_diag else None,
            'asset_free':a.factory_diag,'external_media_required':False if a.factory_diag else None,
            'nes_smb1':a.nes_smb1,'cartridge_sha256':ROMS['smb1'][2] if a.nes_smb1 else None,
            'mdx':a.mdx,'mdx_upload_storage':'192KiB RAM, volatile' if a.mdx else None,
            'mod':a.mod,'mod_song_storage':'256KiB editable RAM, plus read-only embedded boot song' if a.mod_default_song else ('256KiB RAM, volatile' if a.mod else None),
            'mod_source_dir':str(mod) if a.mod else None,
            'mod_default_song_sha256':song_sha if a.mod_default_song else None,
            'mod_default_song_bytes':len(song_data) if a.mod_default_song else None,
            'mod_bank_sha256':bank_sha if a.mod_bank else None,
            'mod_bank_bytes':len(bank_data) if a.mod_bank else None,
            'mod_bank_storage':'read-only embedded Active Bank; explicit copy to self-contained song sample slot' if a.mod_bank else None,
            'nes_auto_skip':a.nes_auto_skip,
            'nes_audio_priority':a.nes_audio_priority,
            'nes_live_fx':a.nes_live_fx,
            'nes_channel_fx':a.nes_channel_fx,
            'nes_render_skip':a.nes_render_skip,
            'nes_profile':a.nes_profile,
            'nes_tiles':a.nes_tiles,
            'nes_volume':a.nes_volume,
            'lcd_async':a.lcd_async,'forge_clock':a.forge_clock if a.factory_diag else None,'forge_lcd':a.forge_lcd if a.factory_diag else None,
            'lcd_spi15':a.lcd_spi15,
            'lcd_spi30':a.lcd_spi30,
            'lcd_rgb444':a.lcd_rgb444,
            'lcd_direct':a.lcd_direct,
            'keyscan_dma2':a.keyscan_dma2 or a.mdx or a.mod or a.factory_diag,
            'keyscan_irq':a.keyscan_irq or a.mdx or a.mod or a.factory_diag,
            'keyscan_paced':a.keyscan_paced or a.mdx or a.mod or a.factory_diag,
            'nes_no_keys':a.nes_no_keys,
            'nes_input_recovery':a.nes_input_recovery,
            'speed_optimization':{'level':'-O2','sources':sorted(str(s) for s in speed_sources)},
            'encoder_scanner':'1ms paced scan, completed-snapshot capture before replacement, lossless bounded backlog, four edges per detent; hardware retest pending' if a.mod or a.factory_diag else '250us semaphore-wakeup, stock stable-two filter; hardware retest pending',
            'lcd_test':'stock-pa2-gpio-dma40-sequence' if a.lcd_stock_sequence else ('stock-dma-params-registers' if a.lcd_stock_dma else ('stock-window-continuous-white' if a.lcd_stock_fill else 'row-addressed-rgb')),
            'vendor_overlay_sources':overlays,
            'source_sha256':{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources+list(HERE.glob('*.h'))+list(HERE.glob('*.py'))+
                [NES/n for n in ('audit_boot.py','audit_power.py','audit_pre_os.py','build_stock_image.py')]+list((NES/'include').glob('*.h'))+list((NES/'boot').glob('*.h'))},
            'archive_sha256':{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in libs}}
    if a.mdx:
        result['source_sha256'].update({str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in mdx.rglob('*.h')})
        result['source_sha256'].update({str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in (mdx/'samples').glob('*') if s.is_file()})
        result['status']='LINKED_USB_DIAGNOSTIC_UNTESTED'
        result['profile']='MDX_KARAOKE_CANDIDATE'
    if a.mod:
        result['source_sha256'].update({str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in mod.glob('*.h')})
        if a.mod_bank:
            inputs=[a.mod_bank.resolve(),mod_tools/'tools/fm1_bank.py']
            result['source_sha256'].update({str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in inputs})
        result['profile']='MOD_EDITOR_CANDIDATE'
    if a.factory_diag:
        result['profile']='FM1_FORGE_FACTORY_DIAG_V1'
        result['source_sha256'].update({str(s):hashlib.sha256(s.read_bytes()).hexdigest() for folder in (HERE.parent/'forge',HERE.parent/'usb-audio') for s in folder.glob('*.h')})
        result['source_sha256'][str(NES/'audit_usb_packet.py')]=hashlib.sha256((NES/'audit_usb_packet.py').read_bytes()).hexdigest()
        provenance=[HERE.parent/'usb-audio'/n for n in ('PROVENANCE.md','SOURCE_ORIGIN.json')]+[HERE.parents[1]/'licenses/fm1-mdx-GPL-3.0.txt',HERE.parent/'forge/config.schema.json']+list((HERE.parent/'forge').glob('*.py'))
        result['source_sha256'].update({str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in provenance})
        if a.forge_user_module:
            module_folder=a.forge_user_module.resolve().parent
            module_inputs=list(module_folder.glob('*.h'))
            if (module_folder/'forge.json').is_file():module_inputs.append(module_folder/'forge.json')
            result['source_sha256'].update({str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in module_inputs})
        result['usb_audio_contract']={'enabled':usb_audio,'class':'UAC1' if usb_audio else None,'sample_rate':48000 if usb_audio else None,'bits':16 if usb_audio else None,'channels':2 if usb_audio else None,'dac_rate':44100,'capture':'diagnostic synthesizer before analog master volume' if usb_audio else None,'microphone':False}
    manifest.write_text(json.dumps(result,indent=2)+'\n');log.close()
    print(json.dumps({k:v for k,v in result.items() if k not in ('static_audit','source_sha256','archive_sha256','power_evidence','vendor_overlay_sources')},indent=2))

if __name__=='__main__':main()
