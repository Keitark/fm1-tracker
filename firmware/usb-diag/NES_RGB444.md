# Packed RGB444 LCD experiment

2026-09-29. Opt-in `--lcd-rgb444 --lcd-async --lcd-spi15` profile:
`build/nes-smb1-rgb444`. This changes wire packing only; same 240x240 fit,
two-buffer ownership, 30 interrupt-driven strips/frame and nominal 15 MHz.
CPU/PLL, NES emulation, audio and scanner settings are unchanged.

## Format and startup

Stock initialization, black fill and display-on remain RGB565 at BAUD4.
After the 60 MHz LSB guard, before enabling asynchronous DMA/IRQ, send
COLMOD 3Ah/53h, then select BAUD3. Format command failure stops the controller.
53h preserves the stock RGB-interface field 101 and selects serial/MCU 011.
The format returns to stock 55h on display reinitialization.

The board API still supplies big-endian RGB565. The LCD producer truncates
each component to its upper four bits and stores pairs as `R0G0 B0R1 G1B1`.
No dithering, no allocation, and no packing in the ISR. Every 240-pixel row
ends on a pixel pair. Persistent framebuffers and strips remain 64-byte aligned.

Reference: [Sitronix ST7789V v1.2](https://files.seeedstudio.com/wiki/Grove-1.2inch-ips-display/ST7789V_SPEC_V1.2.pdf),
printed page102, section8.8.41 (4-line serial RGB444), and page221 (COLMOD).
The actual FM-1 panel identity is not proven; this is a bench experiment using
the compatible stock protocol, not a claim of panel electrical qualification.

RGB565 -> RGB444: row480 -> 360 bytes; strip3840 -> 2880; frame115200 -> 86400;
two buffers230400 -> 172800. Wire payload decreases25%; the payload-only limit
at nominal15MHz increases from16.28 to21.70 complete240x240 frames/s. Actual
performance includes scheduling, preparation and transfer gaps; serial DMA
completion does not prove visible image/color correctness.

## Offline verification

109 checks pass: NES/core/board27, USB/peripheral/display23, linked-image30,
broker/writer29. Actual-driver MMIO tests cover all65536 RGB565 values in both
positions of a packed pair (six complete frames), independent RGB444 decode,
stock55 -> serial53, startup sequencing, mode-command failure, copied source
ownership, backpressure, 30-strip chaining, timeout and STOP. RGB565 profiles
still pass. Linked audit checks aligned172800-byte internal BSS buffers, ISR
interrupt return, stock boot/power sequence and >=128KiB heap reserve.

Application214256 bytes; SHA256
`0cd296462b6654296b8a16bc695e065f235b59a8bfdda2111da0ed5a97295af5`.
Heap227244 bytes. Candidate full-image SHA256
`56ffb10fad321165bdc0dfaa51408d830e655e7d43dba7cd87b0c0fb337f4137`.
Offline plan:51 changed sectors within0x5000..0x38000, excluding0xb000/0xc000,
and directory0x4000 written last. No change outside the allowed application
and directory fields. Existing writer and persistent elevated helper only.

## Rollback

Pre-change sources: `private-backups/nes-rgb444-source-20260929/`.
Verified15MHz RGB565 image:
`private-backups/nes-spi15-write-20260929-193657/runs/23fdf45adbb5492387f0fca02b2d9974/final-1.bin`.
SHA256 `b848335f165fb334fa79046745ea2be76eda315907662661e60f280f46786278`.
See NES_SPI15.md for its measured14.287 LCD FPS /60.026 emulated FPS baseline.

## Deployment and serial observation

Existing helper PID20664 remains active, idle/unblocked. Write succeeded on
the first attempt, run `e4bfbddb50744afc93f18d160e479aad`:51 sectors verified,
directory last, one full1MiB readback matched candidate SHA256 above, no full
pre-write read. Reset was sent once; its known UTF-8/CP932 decoder error
recurred, then COM10 HELLO and advancing frames resolved the uncertainty.
No second reset or new environment/writer/UAC was used.

Evidence: `private-backups/nes-rgb444-write-20260929-1950/`, including the
verified image and post-reset run `ff77a77204294ecba3e405861b6e4154/serial.log`.
Final10-second interval: NES frames203->804, LCD done59->243,
profile wall6699->26751 ticks of500us =10.026s. This is59.94 emulated FPS
and18.35 completed LCD FPS, versus14.29 for the earlier RGB565 capture
(about28.5% higher; not an identical scripted game-state benchmark).
Telemetry confirms configured `b=3 bpp=12`, advancing DMA/key counters,
zero underruns/rebuffers/LCD errors/NES faults through14.14s firmware time.
Audio queued2012 samples at the final snapshot;1152 initial priming samples
are separate from underruns. Physical image/color/audio correctness still
requires user observation; DMA completion is not a panel acknowledgement.
Build/audit manifests remain static evidence, not hardware certificates.

## Clock-limit clarification

The user's62.5MHz observation is supported by the newer
[ST7789V2 v1.0 datasheet, printed page44](https://files.waveshare.com/upload/c/c9/ST7789V2.pdf#page=44):
4-line serial write cycle minimum16ns, high/low minimum7ns. The older
ST7789V v1.2 document used for the initial experiment specifies66ns instead.
15.15MHz must not be treated as a family-wide limit. Exact FM-1 panel variant
and higher-speed board signal integrity remain unverified. This deployment
deliberately retains15MHz to isolate the RGB444 change; it does not test
62.5MHz or change the60MHz peripheral source clock.
