# Integrated ROM 50 peripheral boot candidate

Status: **linked and statically audited; not flashed or hardware-qualified**.
This is now a real application link using LCD/audio/key services, not only a
static library and not a headless loop. A stock-input boot compatibility bridge
is now linked and audited; a [private raw image](STOCK_IMAGE.md) also round-trips.
Physical boot, write/recovery qualification and remaining SDK review are open, so the
manifest intentionally retains `flashable: false`.

## Reproduce without device access

```powershell
C:\Python311\python.exe firmware/nes/build_boot.py --profile peripherals
```

Artifacts: `firmware/nes/build/boot-peripherals-rom50/`

- `fm1-nes-boot-candidate.elf` and `.app.bin`
- `fm1-nes-boot-candidate.map`, `symbols.txt`, `disassembly.asm`
- `static-audit.json` and `build-manifest.json`

The default command still builds the separate headless profile in
`build/boot-rom50/`. Neither command invokes a downloader, packager, USB/device
handle, RAM upload, flash read/write/erase or chip-key query. SDK and core source
checkouts are unchanged. The original backup is unchanged.

Current link with separate NES task and persistent auto-repair registrations
removed: 175,264 application bytes, including the 49,168-byte ROM 50.
Internal initialized RAM is 5,792 bytes and BSS 98,032 bytes; the linker reports
419,724 bytes between heap boundaries. This fits the
conservative 512 KiB application budget and does not use SDRAM. Heap figures
are link-time capacity, not measured peak runtime usage. Actual flash placement
is covered by the private stock-layout packager's offline checks; this does
not qualify a write operation. Do not flash this application payload directly.

## Connected execution path

Stock-input bridge -> SDK startup/RTOS -> app_main creates NES worker and returns
to app_core event dispatch -> NES worker -> stock note-key preset -> SDK services ->
WL82 adapter -> LCD setup -> IIS/ALINK0 setup -> unbounded ROM 50 execution.

- LCD: SPI1 PC7..PC10; recovered panel command table, black clear and display-on.
  Profile origin is x=0/y=40 from the setup table, **not a bench-confirmed crop**.
  Polled transfers remain a bring-up implementation; 60 fps is not established.
- Audio: SDK port C, channel 3 on PC6; 24-bit signed PCM in 32-bit stereo words;
  44.1 kHz request, 512-byte halves. Digital gain 4/16 maximum policy, about one
  second of startup silence, then the stock-derived 127-step Q7 ramp.
- Keys: physical polled SPI2 scanner, coherent 11-row publication, stock-derived
  41-button decoder, F-first piano mapping, portable 10 ms debounce.
- Time: SDK `timer_get_ms`, exposed in wrapping microsecond units with **1 ms
  resolution**, not a true microsecond timer. Waits add a quantization interval,
  yield to the RTOS and feed the watchdog; RTOS ticks can add further delay.
- IRQ: stock ALINK0 IRQ 11 / priority 3 / CPU0, with a compiler-attributed ISR.
  Local ICFG masking plus `testset`/`csync` locks serialize queue access across
  cores. A separate dispatcher lock makes teardown wait for a running handler
  before closing/freeing SDK IIS state. No assumed app-task CPU affinity.

`prepare()` checks SDK-reported clock sanity, observes the timer advance and
starts the scanner. It does **not** program rail voltages, guess a reset or
backlight pin, or establish battery/analog safety. Power/reset handoff from
the stock bootloader remains a qualification requirement, not a hidden pass.

The mute policy is explicitly **digital-only** with no hardware-mute callback.
No PC6 GPIO toggle pretends to control the amplifier. Zero PCM and a startup
ramp do not eliminate analog turn-on/clock-start/turn-off transients. A future
bench sequence must account for that before attaching speakers/headphones.

## Evidence and diagnostics

Nine host CTest groups pass, including app-task return/creation failure and
headless observation-window tests, boot-prefix/extension memory-boundary
tests and SDK-service tests for clock bounds and
wrap, watchdog waits, scanner failure/restart, preserved interrupt flags and
two-host-thread IRQ teardown with an in-flight callback. These are host models,
not silicon tests. The normal pi32v2 library and the SDK boot sources compile
and link against the pinned real SDK.

Boot audits verify XIP entry, section packing, no SDRAM, startup copy/clear
bounds, ROM hash, required integrated symbols and the ISR's actual `rti`
epilogue, plus the real pre-RAM boot compatibility call and bounded copy/zero
instructions. Fourteen boot-audit tests reject bridge bypass/source overread,
along with an ordinary-return ISR replacement even when
both ELF and application bytes agree, and reject configuration-write registry
changes, removal of SDK integrity initialization, alteration of its delayed
callback/timeout, or a changed NES task priority. See `SDK_STARTUP_SAFETY.md`.
Generated code also shows local ICFG
masking and `testset`/`csync` locks, and the app task calls the actual install
and run functions. A symbol table alone would not prove that execution path.

`fm1_boot_stage`, `fm1_boot_result`, `fm1_boot_stats` and `fm1_sdk_diag` are
debugger-visible; consult the generated audit for their addresses. Stage 2
means the adapter was entered, **not** that preparation or hardware boot passed.
Stats are copied on return, not continuously. SDK diagnostics count successful
scans/IRQ callbacks and retain reported clocks and scanner faults. A scanner
fault stops the run and is reported as an I/O error by the boot application.

## Still required for the requested flash-ready result

1. Validate the physical boot handoff for the input bridge described in
   `STOCK_STARTUP_ABI.md` and finish SDK initialization/storage side-effect review.
2. Confirm power/reset handoff and the display origin; inspect/qualify actual
   display throughput, scan phasing, audio clocking and analog transients.
3. Qualify the generated stock-layout raw image's actual write/readback/restore
   procedure. Offline round-trip and protected-byte preservation checks pass;
   a physical restore is not yet validated.
4. Perform staged hardware bring-up before claiming playable FM-1 operation.

ROM 50 remains the embedded cartridge. A future supplied `mario.nes` must have
its header validated and be tested separately; no commercial ROM was fetched.
