# FM-1_010 stock-derived board profile

Offline analysis, 2026-09-27. This is evidence for implementation, **not hardware
qualification or a complete replacement BSP**. No flash, RAM upload, key query,
or device access was performed for this analysis.

Source: `private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin`,
SHA-256 `fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
All addresses below refer to this revision only. XIP base is `0x02000120`.
SDK reference pin: `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`.

## Recovered configuration

| Function | Stock evidence | Implementation status |
|---|---|---|
| Startup memory | Copy 40,540 bytes from `0x020848e0` to `0x01c00000`; zero 95,104 bytes at `0x01c09e5c` | Independent SDK startup/linker candidate already built; its memory layout is its own, not these hard-coded stock addresses |
| LCD transport | SPI1; PC7 CS, PC8 DC, SDK maps PC9 SCLK / PC10 MOSI | Existing adapter, corrected mux address this turn |
| LCD mux | `0x02022eae` loads `r8=0x51020`; `0x02023086` sets bit 4 there | **Fixed `0x51024` to `0x51020`**; SDK layout compile-time check added |
| LCD controller setup | 21 records at `0x0205012c`, 100 ms preceding wait and 120 ms after sleep-out | Already transcribed and byte-compared; still requires real display test |
| Audio pins | PC0 MCLK, PC1 LRCLK, PC2 BCLK, PC6 channel-3 data | Added no-I/O `fm1_wl82_stock_audio_route()` helper |
| Audio clock format | ALINK0 master, MCLK output, falling-edge data update, 64 SCLK/frame | Helper supplies these routing/clock values; no automatic install |
| Audio DMA format | CON1 bit 14 set: SDK interprets this as 24-bit mode on channel 3 | Adapter now emits signed 24-bit PCM in 32-bit words, 512-byte halves / 64 stereo frames |
| Audio interrupt | IRQ 11, priority 3, CPU 0; stock RAM ISR `0x01c047d4` | SDK ISR wrapper and cross-core teardown linked in peripheral profile; no hardware test |
| Button matrix | 41 cells in table `0x02046334`, 11 rows and 6 sampled columns | Polled SPI2 scanner and decoder linked to real timing in peripheral profile; hardware untested |
| Piano mapping | Stock slots 14..40, pitch `slot-14+53+octave*12+transpose` | Stock-derived NES note preset implemented; physical rollover still untested |

The peripheral addresses match the SDK, not guessed consecutively numbered
registers: `JL_IOMAP_BASE = 0x50000 + ((64*0x10 + 7)*4) = 0x5101c`.
Consequently CON1 is `0x51020`, CON2 `0x51024`, and CON5 `0x51030`.
The previous adapter used CON2's address for CON1; fake tests alone did not catch
that. The corrected code is checked against `offsetof(JL_IOMAP_TypeDef, CON1)`
when cross-compiling with the real SDK.

## Why channel 3 / PC6 is established

`0x02004758` establishes `r14=0x0204eb52`. Audio setup at
`0x020051f8..0x020052b6` stores two pointers:

- Channel records: `r14+0x103a = 0x0204fb8c`, into RAM `0x01c1c97c`.
- Pins: `r14+0x829 = 0x0204f37b`, into RAM `0x01c1c980`.

**Paired instructions matter:** the `#` pair at `0x0200520e/0x02005212`
stores the old r0 while computing the next r0. Reading the two printed lines
as sequential C incorrectly swaps the tables.

The four 16-byte channel records, as little-endian words, are:

```text
channel 0: 0, 0, 0, 0
channel 1: 0, 0, 0, 0
channel 2: 0, 0, 0, 0
channel 3: 0, 1, 0, 0x01c1c9f4
```

The pin list begins `20 21 22 23 24 25 26` hex (GPIO32..38, PC0..PC6).
The routine at `0x02004692` skips records with byte +4 zero, programs the DMA
address from word +12, selects `pins[channel+3]`, and uses word +8 to choose
input (1) or output (otherwise). Thus the one active channel is output 3 on PC6.
The address-register table at `0x02055b20` contains `0x12e10/14/18/1c`.
The RAM ISR only invokes the audio refill notification for channel-3 pending
bit 7, corroborating the table interpretation.

PC6 also goes low at `0x0200518c` and high at `0x020052dc`, separated by audio
configuration and busy waits. **Those writes are not evidence of a separate
amplifier enable/mute pin. Do not use PC6 as a mute GPIO during I2S operation.**
The busy-loop duration is not a proven millisecond delay.

Clock interpretation is corroborated by the pinned SDK `iis.c.o` LLVM IR:
CON0 bit 8 is MCLK enable, bit 7 master, bit 10 update edge, bit 9 32-clock
frames. Stock sets bits 7/8 and clears 9/10. CON5 bits 6/7 clear select port C.
Stock LEN is 128; its channel buffer is 1,024 bytes. The adapter now matches this:
`data_width=BIT(3)` and `sr_points=128`, so the SDK callback's `sr_points*4` bytes
represent 64 stereo frames, not 128. `iis_channel_on` clears the full 1,024 bytes.
The stock refill routine at `0x01c04dc8` selects half-buffer by `index<<9`, uses
base `0x01c1c9f4`, and passes 64 frames to `0x01c04a3a`. That processing routine
sign-extends the low 24 bits of 32-bit samples at `0x01c04a7c..0x01c04a82`.
Our signed-16 NES mono samples are scaled by 256 into signed-24 range, duplicated
to L/R, and copied safely even to an unaligned callback pointer. This does not
increase the emulator's source fidelity; it supplies the stock DMA representation.
Clock-divider settings were observed, but no physical sample rate was measured.

### Startup audio hold and ramp

Stock setup at `0x020051ce` initializes RAM `0x01c1d214` to 44,118. The refill
path at `0x01c04b2c..0x01c04b5e` subtracts 64 per half while positive and holds
the current digital gain at zero. This rounds the silence period up to 44,160
frames (690 halves). It then increments gain by one per half toward 127;
`0x01c04bfa..0x01c04bfe` applies a signed product shifted right by seven.
The stock output table's zero-input entry is also zero.

`fm1_audio_startup_process24()` now reproduces that startup hold and linear
Q7 envelope in the WL82 callback. It is reset before enabling audio IRQs,
owned by the consumer, and only advances for an exact 512-byte channel-3
callback. It does not reproduce the synth's nonlinear DSP, replace hardware
mute, or prove pop-free operation. The default application gain remains zero.
Host tests cover 690 silent blocks, all 127 rising gain levels, bounded signed
arithmetic, final-gain saturation and restart. See `STOCK_STARTUP_ABI.md` for
the startup/bootloader comparison and remaining compatibility questions.

## Button scanning and note identity

The SPI2 ISR at `0x01c045ce` packs PA0, PA5, PA6, PA7, PA8 and PB7 into bits
0..5 of each sample byte. There are 11 row slots (0..10); the completed scan is
published through RAM `0x01c0e650+0x148`. SPI2 uses PA3/PA4 via SDK port mapping,
CON `0x6020`, BAUD 29, and PA1 toggles around the two-byte row-select DMA.
The ISR also multiplexes PH6/PH9/PA9/PA10 from a separate table; these are not
therefore justified as dedicated LCD reset or amplifier pins.

Important scan-phase detail for the physical driver: `0x01c045ee..0x01c04628`
samples and stores columns **before** raising PA1 at `0x01c0462c`. It then
increments the row index, forms the next two-byte transfer, and lowers PA1
after programming DMA (`0x01c0469a..0x01c046a0` or `0x01c046d0..0x01c046d6`).
The wire bytes are the big-endian 16-bit word `0xffff ^ (1<<next_row)`, additionally
clearing bit 11 for next_row=0 or bit 12 for next_row=1. Initial setup differs
from the steady-state special-row sequence. Do not implement a naive
"select row, latch, read same row number" scanner and silently shift the map;
preserve the recovered pipeline and discard unprimed data.

`0x0202476e` loads the 41-byte button map at `0x02046334`. High nibble is row,
low nibble is column. The handler tests active-low input and iterates 41 entries;
its debounced events encode the slot in bits 16..23. The separate ISR at
`0x01c046e4` handles seven quadrature encoders with a different table at
`0x020462fc`; those entries must not be mistaken for piano-key slots.

The UI event consumer at `0x02023a02` dispatches slots 0..13 as panel controls;
`0x02023374` accepts the remaining slots through 40. At `0x0202337e` it forms
the low byte of `slot+0xf2` (equivalent to `slot-14` for 14..40), then
`0x020233aa..0x020233bc` adds octave*12, transpose and base MIDI note 53.
This establishes slot 14 as F without assuming the matrix is chromatically
ordered. It agrees with the user's far-left-F identification and MIDI capture.

`fm1_stock_decode_keys()` implements only coherent-row decoding.
Historical first-octave preset (superseded by the Famicom layout in
`CONTROLS.md`): `fm1_stock_assign_note_keys()` previously supplied C..B slots
`21,22,23,24,25,14,15,16,17,18,19,20`. Neither accesses peripherals or pretends
that a real scan has occurred. The missing physical scanner must reproduce row
selection, latch timing, sampling phase and coherent publication before using
these helpers. The scanner described below now supplies this path; existing
portable 10 ms NES debounce remains in place.

### Physical scanner implementation update

`src/fm1_wl82_keyscan.c` now implements the MMIO acquisition path, not just the
decoder. It uses SPI2 port C (PA3 clock / PA4 output), PA1 latch, BAUD 29, and
CON `0x4021`: stock's master/write configuration with IRQ disabled for polling.
Each transfer has a wrap-safe 10 ms bound using a caller-supplied monotonic
clock. There is no DMA/cache ownership or additional ISR in this implementation.
It follows the stock sample-before-latch pipeline and special row words,
discards the unprimed first sweep, and only publishes complete scans. Errors
leave the caller's key mask unchanged, disable SPI2, and require restart.

The six input pull-ups and four multiplex outputs follow the stock configuration;
the latter remain low (no stock UI LED animation). Other GPIO bits and SPI1's
mux bit are preserved. PA2, power rails, USB pins, DAC pins and flash are not
touched. Stop disables SPI2 and lowers the latch; it does not pretend to reset
the external shift registers or restore another owner's peripheral configuration.

`keyscan_tests.c` provides an independent MMIO/shift-register model that checks
row words, latch-before-transfer and latch-low timing, coherent publication,
initial discarded data, multi-key chords/release, invalid/inactive entry gates,
wrap-around timeout, unchanged output on failure, and restart while external
latched state is retained. This validates the software protocol, **not** the
board's electrical settling time, actual SPI mode/polarity, or physical rollover.
The explicit peripheral boot profile now calls this scanner through real SDK
timing/services. The default headless profile does not. See `PERIPHERAL_BOOT.md`.

## Display reset, backlight, and board power: still unresolved

The traced display branch sends sleep-out (11h), setup, a black fill, and
display-on (29h). Sleep-out is **not** hardware reset. No separately identified
panel-reset/backlight GPIO has been established from this branch. They may be
handled elsewhere or electrically tied; absence here proves neither.

PH8 is **not a justified reset/backlight output candidate**: `0x02022eec..0x02022f1c`
configures it as a digital input, clears pull-down, and enables pull-up. The
helper at `0x02003e80` writes the port pull-up register (+0x10), whereas
`0x02003fa0` writes pull-down (+0x14).

Other early GPIO writes are tracked but are not assigned rail/reset roles just
because they occur at startup. Their consumers and board connections need to
be traced. The remaining full BSP also needs physical key scanning, power/
watchdog policy, monotonic timing, and properly registered interrupt wrappers.
An independent firmware must reproduce behavior, not call addresses in stock
code that its own application image would replace.

## Reproduce offline checks

```powershell
C:\Python311\python.exe firmware/nes/tests/verify_stock_panel.py
C:\Python311\python.exe firmware/nes/tests/verify_stock_board.py
C:\Python311\python.exe tools/jieli-wl82/test_trace_stock_calls.py
C:\Python311\python.exe firmware/nes/build.py all --rom50
```

The checks reject unknown application hashes, verify exact tables/reviewed
instruction bytes and C route transcription, then exercise fake-register
lifecycle tests and the real SDK cross-compile. They never prove that a panel
lights or audio is safe/audible. The default boot profile remains headless;
an explicit `--profile peripherals` now links/uses the adapters. Neither is
flash-ready or physically qualified.

Latest local checks pass: all seven Windows ROM-integrated CTest groups and twelve
pi32v2 translation units. Added coverage includes signed-24 extrema, negative
samples, ring-counter wrap, underrun silence, callback bounds/unaligned storage,
all 66 matrix cells, all 64 column combinations, and the derived piano preset.

## Remaining work toward the requested flashing image

1. Qualify physical row/sample phasing and multi-key behavior; the scanner is
   now connected to SDK timing in the integrated peripheral image.
2. Finish tracing board power/display-reset/backlight and audio mute behavior;
   do not manufacture GPIO roles. Reproduce stock-required initialization.
3. SDK timing/watchdog/IRQ services and actual LCD/audio/input integration now
   link. Resolve bootloader argument extensions and SDK initcall side effects.
4. Static ELF/ROM/ISR checks now pass. Measure runtime stack/heap/throughput and
   validate actual image placement within the 1 MiB flash layout.
5. Complete reviewed packaging and recovery checks, then staged hardware tests.
   A passing host test or headless ELF does not complete this objective.
