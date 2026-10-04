# FM-1 standalone NES firmware project

Status (2026-09-27): **host-tested emulator and board pipeline, experimental WL82
driver, pi32v2 library, and linked headless/peripheral boot candidates; hardware boot untested**.
The new offline [boot-image build](BOOT_IMAGE.md) produces an ELF/application
payload with SDK startup. It is not yet a playable or deployable FM-1 release.
See [BOARD_INTEGRATION.md](BOARD_INTEGRATION.md) for evidence and remaining startup work.
The new [peripheral profile](PERIPHERAL_BOOT.md) connects LCD, audio, keys and
real SDK timing/interrupt services using `build_boot.py --profile peripherals`.
A stock-input [boot compatibility bridge](STOCK_STARTUP_ABI.md) now normalizes
the SDK's unused argument extensions before RAM initialization.
An offline [private stock-layout raw image](STOCK_IMAGE.md) now exists, with
round-trip and protected-byte checks. It is not deployment-qualified; no
`.fwsc` updater package, erase command or writer is provided.
Two SDK persistent auto-repair registrations are omitted; see
[startup side-effect review](SDK_STARTUP_SAFETY.md).
Without explicitly installed board services, `fm1_nes_target_start()`
returns `FM1_NES_BSP_UNVERIFIED` without touching any peripherals.

This is a standalone replacement project, not an extension of the stock synth.
The original readback and research are unchanged.

## Implemented

- Portable emulator integration using the [PeakRacing NES core](https://github.com/PeakRacing/nes),
  pinned to `68bdfc8de570264c0e84f73766f1a1ed3591066f`.
- Strict length-checked iNES 1.0 loader for **NROM/mapper 0** (8 KiB CHR-ROM)
  and **CNROM/mapper 3** (8, 16 or 32 KiB CHR-ROM), both with 16 or 32 KiB PRG.
  CNROM switches the full 8 KiB graphics window on writes to `$8000-$FFFF`.
  No automatic header guessing or cartridge database overrides.
- 256x240 RGB565 rendering in two reusable 256x120 blocks (61,440-byte buffer).
- Fixed-point APU sample callback: 44,100 samples/second at nominal 60 frames/s.
- All eight logical controller buttons and a [piano-key layout](CONTROLS.md).
  Stock-derived physical key slots are implemented; hardware behavior is untested.
- Static emulator state; no emulator heap or filesystem required.
- Frame limit/stop callback, error handling, reproducible build scripts, and an
  original diagnostic ROM generator with no commercial game content.
- Cross-compilation with the installed JieLi compiler: pi32v2, CPU r3.
- FM-1 board pipeline: 240x240 display fit/crop, recovered panel script, DC-filtered
  muted-by-default audio, queue/underrun handling, configurable debounced keys,
  and 60 Hz scheduling. Experimental SPI1 and SDK ALINK0 adapter included.

## Verification actually performed

`build.py all --rom50` builds the host runner and eight Windows CTest groups:
emulator contract, board pipeline, fake-WL82 lifecycle, ROM asset generation,
embedded game, physical-scanner model, SDK-service/IRQ model and the boot
compatibility bridge contract. It then
compiles twelve port/core/driver/generated translation units for pi32v2 and creates
`libfm1_nes.a`. No target link or boot image is produced.
The separate `build_boot.py` command supplies the SDK startup and full link.

Tests exercise CPU mailbox execution, non-flat rendered pixels, changing audio
samples, every controller bit through the emulated `$4016` serial interface,
both NROM PRG sizes, writes to read-only cartridge ROM, malformed/truncated
inputs, unsupported cartridge formats, deterministic output, callback failure,
and the blocked hardware entry point.

CNROM diagnostics also check initial bank zero, all eight 1 KiB PPU pages,
bank selection/address aliases, ignored high bits, read-only CHR/PRG, both PRG
sizes and both mirroring flags, and four distinct rendered banks. The optional
ROM 50 smoke test checks its pinned hash and runs 600 frames each with no buttons
and Start held; it skips when the user-selected, non-redistributed ROM is absent.
Both runs passed locally; the resulting title and gameplay frames were visually
inspected. This is a smoke test, not full-game correctness or hardware acceptance.

A separate 120-frame diagnostic run completed with:

- 240 half-frame video callbacks and 88,200 audio samples;
- CPU mailbox `$00 = 0x5a`, controller mailbox `$01 = 1` with A held;
- video FNV-1a accumulation `3173721285`, audio accumulation `821716565`;
- audio minimum 0 and maximum 28 (confirms variation, not listening quality).

Target `nes_t` allocation: **69,240 bytes**, including the 61,440-byte video
buffer. The x64 host allocation is 69,424 bytes because pointers/layout differ.
These numbers exclude stacks, platform/DMA buffers, SDK/RTOS allocations, and
any copied ROM. They are not whole-firmware memory usage. The target archive
size is recorded in the current build manifest; archive size is not linked flash footprint.

No pi32v2 instructions have been executed on the FM-1 for this project. No
frame-rate, screen, sound, controls, boot, or restore test has passed on hardware.

## Build and run

From `F:\dev\fm1`, with Python, CMake, Visual Studio 2022 C tools, and the
existing JieLi SDK/toolchain installed:

```powershell
C:\Python311\python.exe firmware/nes/build.py all
C:\Python311\python.exe firmware/nes/tests/make_diagnostic_rom.py firmware/nes/build/diagnostic.nes
.\firmware\nes\build\host\Release\nes_host.exe firmware/nes/build/diagnostic.nes 120 1 firmware/nes/build/diagnostic-frame.ppm
```

`nes_host` is a headless diagnostic runner, not a desktop game UI. It writes a
PPM screenshot and prints JSON statistics. Its pad mask is held for the run;
the embedded adapter polls a callback every frame. Button bits:
`A=1, B=2, Select=4, Start=8, Up=16, Down=32, Left=64, Right=128`.

`build.py host` runs just the host build/tests; `build.py pi32v2` compiles the
target library. The script checks the dependency commit and rejects a dirty
dependency checkout. It never invokes the SDK's default Makefile: that
Makefile can invoke a download script after building.

### Optional compiled-in ROM 50

```powershell
C:\Python311\python.exe firmware/nes/build.py all --rom50
```

This checks the exact local cartridge SHA-256, generates a `static const` byte
array under the ignored build directory, and supplies `fm1_rom50_start()` from
[include/fm1_rom50.h](include/fm1_rom50.h). That function calls the existing
board-gated target entry; it does not install a BSP or act as a reset vector.
Host tests run the compiled-in bytes for 600 frames and confirm the hardware
entry remains blocked. The target symbol audit verifies 49,168 read-only bytes;
final XIP placement has **not** been linked or verified.

Outputs are separate from the emulator-only build: `build/host-rom50/` and
`build/pi32v2-rom50/libfm1_nes.a`. The manifest still says `flashable: false`.
Do not pass this archive to any updater. It contains the user-selected game,
whose redistribution rights must be checked independently.

Dependencies remain under `references/source/` outside this project. To
reproduce on another checkout, obtain PeakRacing/nes at the exact pin above
and the AC79 SDK at `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`.
Keep the upstream license with the source. No SDK binaries are bundled here.

## Platform integration contract

See [include/fm1_nes.h](include/fm1_nes.h). `fm1_nes_run()` expects a ROM buffer
that stays readable until return and four callbacks:

- `video`: synchronously consume/copy RGB565 blocks. DMA must not retain the
  reused source buffer without copying or waiting for transfer completion.
- `audio`: consume unipolar unsigned sample amplitudes. They are not signed PCM
  and must not be sent directly to the CS4344. Add DC removal, sample-format
  conversion, bounded gain, buffering and the confirmed I2S clock format.
- `buttons`: provide logical pad bits; no physical GPIO/knob assumptions.
- `frame`: pace at the selected emulation rate and/or request stop. Host tests
  intentionally run unpaced. Target timing and audio synchronization are pending.

The API supports one instance and is not thread-safe. Calls from callbacks are
rejected as busy. Only this adapter's loader should be used; the upstream
unbounded memory-ROM loader and generic allocator API are not exposed here.

## Known limits

- Only mappers 0 and 3 are accepted. CHR-RAM, other mappers (including 4/MMC3),
  NES 2.0, PAL flags, trainers, save RAM,
  four-screen cartridges, and nonzero extended-header bytes are rejected.
- CNROM uses the pinned core's legacy-iNES **no bus-conflict** policy. Real CNROM
  boards can AND a write with the PRG bus value; this electrical behavior is not
  emulated. NES 2.0 conflict submappers, oversized CNROM, mapper 185 protection,
  speech hardware and CNROM PRG-RAM variants are not supported. See the
  [CNROM reference](https://www.nesdev.org/wiki/CNROM). Mapper numbers identify
  different hardware designs; supporting 3 does not imply support for higher ones.
- Upstream CPU/PPU/APU accuracy is not comprehensively qualified. This is not
  cycle-accurate certification or an all-games compatibility claim.
- The inspected APU mixer covers pulse, triangle and noise; **complete DMC
  playback is not implemented in the inspected path**. DMC register storage is
  not equivalent to a working DMC channel. More audio work is needed.
- Header validation is not a sandbox for hostile ROM programs. In particular,
  upstream DMA handling for unusual source ranges needs hardening before
  accepting arbitrary untrusted ROMs.
- Half-frame rendering now has 240x240 scaling/cropping, but display bandwidth,
  visible offsets and orientation still need hardware qualification.
- Experimental peripheral startup/link closure and embedded ROM storage exist;
  private raw-image packaging also passes offline checks. Physical boot,
  write/restore qualification and battery management remain open. A menu and
  persistent saves are not implemented in this fixed-ROM mapper-0/3 build.

## Remaining hardware gates

See [INTEGRATION_STATUS.md](INTEGRATION_STATUS.md) for the current stock-function
comparison, the corrected SDK event-dispatch integration and exact completion
boundary. Both profiles run NES in a separate task; `app_main` returns to the
SDK dispatcher. The headless diagnostic includes a minimum 20-second observation
window, not an assertion that the delayed SDK integrity check passed.

1. Bench-confirm the recovered display script/pins, origin, reset/backlight and throughput.
2. Bench-confirm the recovered button scanner and F-first NES pad layout;
   knobs/encoders are not mapped to NES functions.
3. Qualify recovered DAC/I2S routing, actual clocks, amplifier power/mute and gain.
4. Establish standalone clocks, pin multiplexing, power/battery handling,
   watchdog, interrupts, and SDK/RTOS memory/stack requirements.
5. Integrated startup now links and passes static memory/ROM checks; validate
   bootloader compatibility and actual placement without disturbing reserved data.
6. Validate a recoverable deployment plan. A verified readback does not prove
   restoration. Any RAM payload execution or flashing needs separate approval.
7. Hardware tests: muted boot, display pattern, controls, low-level audio,
   diagnostic ROM, then sustained playback/frame timing and thermal/power checks.

The next work is the remaining board startup/physical mapping and muted bench
validation described in BOARD_INTEGRATION.md, not writing this archive to flash.
