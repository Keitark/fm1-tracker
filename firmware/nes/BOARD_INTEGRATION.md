# FM-1 NES board integration — 2026-09-27

## Actual status

The emulator is now connected to a tested board pipeline and an experimental
WL82 hardware adapter. All project/core units cross-compile for pi32v2/r3.
The [integrated peripheral boot candidate](PERIPHERAL_BOOT.md) now connects
the real SDK services and produces an ELF/application payload. A separate
[headless SDK profile](BOOT_IMAGE.md) remains available. **Neither is a
hardware-validated, playable or flash-ready firmware.**
No device RAM payload, flash write, erase, driver replacement or key query was
performed during this integration. The private stock backup was not modified.

Latest stock-ROM follow-up: [STOCK_BOARD_PROFILE.md](STOCK_BOARD_PROFILE.md).
It corrects the SPI mux address, establishes the channel-3/PC6 audio route and
stock clock format, and now implements the stock 24-bit DMA representation.
The 41-button matrix decoder and F-first piano slot mapping are also recovered.
The physical SPI2 scanner is wired into SDK timing/services. Reset/backlight and
analog startup behavior remain unqualified; the explicit digital-only mute
policy does not invent amplifier control.

## Implemented paths

| Path | Implementation | Verification |
|---|---|---|
| Video | 256x240 RGB565 -> 240x240, nearest-neighbor fit or center crop; 8-row big-endian wire buffers | Host pixel/coordinate/byte-order tests |
| Panel | Recovered 21-record init, sleep-out delay, black screen before display-on; 2A/2B/2C windows | Exact table comparison to this device's application; fake transport |
| WL82 SPI | SPI1, PC7 CS, PC8 DC, PC9 clock, PC10 MOSI; bounded polled writes | Cross-compiled and fake-register lifecycle tests; no hardware test |
| Audio | Q8 DC removal, bounded gain, default digital mute, 4096-frame mono queue, signed-24-in-32 stereo conversion and silence on underrun | Host signal/ring tests, including counter wrap and full-scale extrema |
| WL82 audio | SDK ALINK0/IIS, 44.1 kHz request, 64-frame callback, explicit channel/clock configuration and IRQ service binding | Real SDK header cross-compile; ABI inspected in SDK bitcode; fake SDK lifecycle |
| Inputs | Polled SPI2 scanner, stock 41-button decoding, F-first piano NES preset, 10 ms debounce, opposite-direction neutralization | Stock bytes + shift-register model; physical timing/rollover unverified; see CONTROLS.md |
| Timing | Rational 60 Hz deadlines, wrap-safe microsecond clock, late-frame resynchronization and wait-error propagation | Fake-clock tests; not measured hardware frame rate |

LCD transfers are synchronous: the upstream half-frame buffer is not retained.
The SPI implementation deliberately uses byte transfers; it is a bring-up driver,
**not a validated 60-fps transport**. Full-screen 60-fps pixels alone require
6,912,000 bytes/s (~55.3 Mbit/s), before commands and software overhead. DMA,
frame skipping and measured scheduling may be needed. The frame scheduler does
not make an overloaded target run at 60 fps. Audio and timer drift may also need
measured clock correction.

## Evidence from this device (not addresses copied from another revision)

The decoded application is `private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin`:

- SHA-256: `fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
- Runtime/XIP base: `0x02000120`; see the existing disassembly audit.
- `0x02022f3a` loads `r12 = 0x0204eae0`.
- Init loop `0x020230c8..0x0202313c` uses table displacement `0x164c`, stride
  `0x12`, and count `0x15`. Therefore the table is at runtime `0x0205012c`,
  decoded-app offset **`0x5000c`**, 378 bytes.
- Record 1 (`45 78 ...`) is a **120 ms delay pseudo-record**, not command 45h.
  The branch at `0x020230e2` skips transmitting it. There is a preceding 100 ms delay.
- `0x020230ac..0x020230b8` sets SPI1 CON=`0x4021`, BAUD=`4`.
- Window routine `0x0202161e..0x02021712` uses SFR `0x50080` bit 7 as active-low
  CS and bit 8 low for commands/high for data. The SDK names that SFR PORTC OUT.
- `0x0202308a..0x020230a8` configures GPIO39 (PC7) output-high, then GPIO40/41/42
  direction-output. This was checked against implementations `0x02003bbe`
  (set OUT then clear DIR bit) and `0x02004098` (direction bit setter).
- IOMAP CON1 **at `0x51020`** bit 4 selects SPI1 port C in the SDK: PC9 SCLK,
  PC10 MOSI. The prior adapter's `0x51024` address was corrected from stock evidence.
- The panel uses a ST7789-like command set and `3A 55` for RGB565. **Exact
  controller marking, reset/backlight wiring and orientation are not proven.**
- The init table contains page range 40..279. Later window writes replace that
  range. The physical visible offset must be verified; configuration makes it
  explicit instead of assuming the init range proves the final screen origin.

[Community display research](https://github.com/AL-255/FM-1-RE/blob/main/docs/io/08-display.md)
helped locate the subsystem, but its version-specific addresses and a GPIO39
input claim were not used as authoritative pin definitions. Our own application's
addresses/operands above take precedence.

Read-only table check:

```powershell
C:\Python311\python.exe firmware/nes/tests/verify_stock_panel.py
```

This check rejects other application hashes and compares all 378 bytes against
the C table. It does not inspect boot configuration, chip keys or reserved flash.

## SDK audio audit

SDK pin: `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`.
The `iis.c.o` member of `cpu/wl82/liba/cpu.a` was extracted to the **build directory**
and decoded to LLVM IR using the installed vendor compiler. This established:

- `iis_open` and `iis_set_sample_rate` return 0 on success, -1 on failure.
- The callback's final argument is the **channel index**, not a bit mask.
- Callback length is `sr_points * 4` bytes regardless of sample width. With
  24-bit samples in 32-bit words, `sr_points=128` gives 64 stereo frames.
- The SDK keeps the platform-data pointer; ours stays alive until `iis_close`.
- `iis_channel_on` clears its output buffers before enabling the channel.
- This module does **not** register a CPU interrupt handler. Board services must
  route ALINK0 IRQ through a correctly attributed ISR wrapper to the supplied C
  dispatch function. Shutdown must quiesce that IRQ before closing/freeing IIS.
- The SDK allocates its own audio buffers. The emulator's static-allocation claim
  does **not** mean the whole eventual firmware has no heap.

The SDK port-C data table maps channels 0..3 to PC3..PC6 and clocks to PC0..PC2.
The stock channel records and RAM ISR now establish channel 3 output on PC6.
`fm1_wl82_stock_audio_route()` supplies that route and the recovered clock format
without I/O. PC6 must **not** be treated as an amplifier-enable pin. The current
adapter now matches stock's 24-bit-in-32 DMA layout; see the profile above.

## Startup contract and remaining blocker

`fm1_nes_target_start()` calls the WL82 adapter on pi32v2. Without an explicit
`fm1_wl82_install()` it returns `FM1_NES_BSP_UNVERIFIED`, touching no peripherals.
Installation itself only copies/validates the configuration; it is **not evidence
that the board is safe to run** and is not performed by the default build.

The peripheral profile now supplies timing/watchdog, scanning, cross-core queue
locks and ABI interrupt registration/teardown in `boot/wl82_services.c`. The
complete qualification requirements remain:

1. Board power/clock/reset/watchdog setup, initialized SDK allocator/runtime,
   panel reset/backlight, and exclusive ownership of the LCD/audio pins.
2. Bench validation of the recovered DAC route/clock/24-bit DMA output and
   analog transients; current mute policy is explicitly digital-only.
3. Monotonic microseconds, watchdog-servicing waits, a physical key scanner,
   stop input, and IRQ-safe queue locks. Producer/consumer must be on one CPU or
   use real cross-core serialization; disabling one CPU's IRQs alone is not enough.
4. ALINK0 interrupt registration with the pi32v2 ISR ABI and synchronous teardown.
5. A boot/linker closure, retained read-only diagnostic ROM, complete memory map,
   and reviewed image packaging that preserves the stock boot/reserved data.
6. Proven recovery and staged muted bench tests before any flash deployment.

No default dev-board power settings, stock function-address calls, physical
key-to-scanner IDs or amplifier GPIO have been invented; the DAC channel and
piano slot mapping are now stock-derived, not guessed. The intended
piano-key layout is specified in CONTROLS.md. An installation
flag cannot replace the above work. The test services/fake registers exist only
in host test targets, never in the cross-compiled library.

## Validation

On Windows, `build.py all --rom50` now runs eight CTest groups, including the
boot compatibility prefix/zero-extension contract. In addition to
the original groups below, it includes physical-scanner modeling and SDK-service
clock/IRQ lifecycle tests, including concurrent teardown:

- Existing emulator host contract (thirteen Python methods, including CNROM).
- ROM asset generator: exact bytes, reproducibility, and rejection of wrong
  size/hash/header without replacing an existing generated file.
- With `--rom50`: the compiled-in cartridge runs 600 frames on the host, while
  the target entry remains gated without a board implementation.
- Board contract: configuration, init script, scaling/crop/endianness, muted and
  DC-filtered audio, key debounce, clock/counter wrap, queue overflow/underrun,
  and an original diagnostic ROM through the complete portable board pipeline.
- WL82 lifecycle on **fake hardware**: uninstalled/invalid-ROM gates, rejected
  prepare, SPI timeout, IIS open/rate errors, IRQ registration failure, unmute
  failure, ordered cleanup, normal run and restart.

All five groups passed in the ROM-integrated build. Ten translation units
cross-compiled (nine without ROM integration); project-owned
units use `-Wall -Wextra -Werror`. The manifest explicitly says `flashable: false`.
Host success is not an LCD, audible-output, physical-input, boot or recovery test.

Latest integration/flash preflight (2026-09-27): Windows reports **WL82 UBOOT1.00
USB Device** at `\\.\PHYSICALDRIVE3`, status OK. This is an enumeration observation,
not proof that erase/write or recovery works; the drive index must be rediscovered
before any future device access. The saved 1 MiB backup's SHA-256 was rechecked
and still matches `a45aa21afd8a659612427213ce80c7500a42fcbd8548262fb611f03df84e8b61`.

ROM 50 is now optionally compiled into `build/pi32v2-rom50/libfm1_nes.a`, with
49,168 read-only cartridge bytes and `fm1_rom50_start()` wired to the gated target
entry. No startup/linker or physical-service gaps above have been bypassed.
The artifact remains **not bootable**. No device handle, RAM upload, key query,
erase or flash write was performed during this preflight/integration request.
The paragraph above describes the earlier library-only preflight. The newer
peripheral application is linked; see `PERIPHERAL_BOOT.md`. Flashing still needs
startup compatibility/side-effect review, correct packaging, validated recovery
and physical qualification, not merely another permission to write.
