# FM-1 NES boot-link candidate — 2026-09-27

There is now also an explicitly selected **integrated peripheral profile**:
`build_boot.py --profile peripherals`. See `PERIPHERAL_BOOT.md` for its connected
LCD/audio/scanner services, test evidence and remaining flash gates. The rest
of this document describes the default headless profile.

## What now exists

The project now produces an **ELF executable and a linked application image**,
not just an archive. This is an **untested, headless bring-up candidate**, not a
playable standalone NES release or a firmware-update package.

Build offline from the workspace root:

```powershell
C:\Python311\python.exe firmware/nes/build_boot.py
```

Outputs under `firmware/nes/build/boot-rom50/`:

- `fm1-nes-boot-candidate.elf`: ELF32 little-endian pi32v2 executable.
- `fm1-nes-boot-candidate.app.bin`: SDK-layout application payload.
- `fm1-nes-boot-candidate.map`, `symbols.txt`, `disassembly.asm`: link/entry evidence.
- `static-audit.json`, `build-manifest.json`: checks, hashes, limitations.

The script only executes Git read checks, local tests, compiler/linker and
object inspection/extraction tools. It does not invoke `make`, `download.bat`,
`isd_download`, a packager, device handles, or UBOOT commands. SDK files are not
edited. Its sparse checkout was expanded to make the existing pinned common
runtime sources and version assembly files available.

## Startup and application behavior

The entry is **`0x02000120`**, matching this FM-1's recovered XIP application
entry. SDK `startup.S.o` supplies the entry branch, stacks, boot-info handling,
BSS clearing and initialized-RAM copying, then SDK runtime/FreeRTOS startup.
`boot/app_main.c` starts a dedicated `fm1_nes` RTOS task and returns to the
SDK's `app_core` event dispatcher. The worker runs mapper-3 ROM 50 for 600 frames with
no buttons and discards the rendered video/audio. It yields each frame to
the RTOS. That delay is not a qualified 60 Hz timing source. After completion
it waits until at least 20,000 milliseconds have elapsed since worker entry,
then idles with an updating heartbeat. Debugger-visible RAM records elapsed
time, result and statistics. A stalled clock cannot complete this interval.
The interval permits observation beyond the SDK's eight-second deferred
integrity callback; it does not prove that callback passed. Emulator errors
are retained without becoming a successful soak result.

`boot/board.c` deliberately registers **no demo-board UART or peripherals** and
does not choose power voltages. No physical display/audio/keyboard support or
MIDI/USB updater is installed by this profile. SDK runtime initialization still
touches chip hardware and includes storage/update infrastructure; it must not
be described as a read-only or hardware-safe program merely because its own
board hooks are empty. Physical boot compatibility and SDK initcall/storage
side effects remain to be checked before executing it.

The stock comparison identified SDK-only argument fields at +80 and +88.
Both profiles now use a pre-RAM stack-only bridge: preserve the confirmed
24-byte stock prefix and supply zero unused SDRAM/external-app extensions to
the original SDK initializer. The actual startup call and bridge instructions
are audited, with host memory-boundary tests and negative image tests.
See `STOCK_STARTUP_ABI.md` for the recovered bootloader handoff and limits.
Matching the XIP entry and passing these offline checks is not a hardware pass.

Debug mailbox addresses are emitted in `static-audit.json`; do not hardcode
them across builds. Stages: 1 = app entry/task creation, 2 = NES running,
4 = headless observation interval, 3 = NES returned successfully and (headless)
the interval elapsed, 255 = error. `fm1_boot_task_error` records a failed SDK
task-create result. `fm1_boot_elapsed_ms`/`fm1_boot_heartbeat` update per headless
frame, during its observation interval and while idling after either profile
returns. They are **not** a live peripheral-run monitor; that profile uses
`fm1_sdk_diag` scan/IRQ counts. No device-side stage has been observed.

## Static verification

- SDK revision: `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`.
- ROM SHA-256: `6cd4e6052a16ce014cbd0f175830b77ebe4b236615409f2156de00179cc8311d`.
- No external SDRAM sections contain data; all emulator state is internal BSS.
- Startup copy/clear symbols agree with the linked section addresses/sizes.
- No unresolved global/weak symbols or unexpected allocated sections.
- ROM bytes reside within XIP text/constants and match the original hash.
- The application is exactly the SDK's concatenation of `.text`, `.data`,
  `.dynamic_data`, `.ram0_data`, `.cache_ram_data`, not a flat ELF address dump.
- A conservative 512 KiB application budget is enforced. This is not proof of
  a physical flash offset or package compatibility.
- Tests reject changed entry/architecture, corrupted/truncated application,
  and a corrupted cartridge even when ELF/application copies agree.
- Startup must call the reviewed boot compatibility bridge after SP/SSP setup;
  tests also reject bridge bypass and a changed source-copy bound.

The generated linker script additionally collects `.data.*` and `.bss.*`,
because our port uses section-per-variable compilation. The original SDK's
non-C++ script only collects unsuffixed sections. This correction is confined
to the generated script, checked for template drift, and verified in the ELF.

## What it does not prove

**A successful link is not a successful hardware boot.** The manifest retains
`flashable: false` and `hardware_boot_verified: false`.

This application binary is neither a 1 MiB raw replacement nor a `.fwsc`/UFW
update file. Do not write it directly. The separate `build_stock_image.py`
now constructs and round-trip checks a private 1 MiB raw candidate using the
original stock layout; see `STOCK_IMAGE.md`. It preserves stock boot/config/
reserved data. No device operation or physical restore is established.

Remaining stages are SDK hardware/startup review and write/rollback validation,
headless boot observation, then LCD/power/audio/scanner bring-up. The existing
WL82 peripheral adapter remains independently gated; the headless profile does
not falsely mark that adapter as verified. See `BOARD_INTEGRATION.md`.
