# Stock 010: whole-payload disassembly and startup audit

2026-09-28. Offline only. Baseline is the user's known-working **FM-1_010**,
not a different version. No device reads/writes, helper upload, reset, or flash.

**Result:** all bytes of both decoded executable payloads have byte-checked
linear listings, and startup callback coverage has been expanded. This is
**not a complete semantic reconstruction** and does not establish that the NES
firmware contains every required process. The last-written NES candidate still
has the user's reported completely dark display. T2 remains paused. A separate
`screen-first` implementation/link was prematurely attempted before this stock
review was finished; its build manifest remains incomplete/unqualified. Those
unfinished edits and outputs are preserved, not a flash-ready release.

**Current user-directed gate:** stock analysis first. No further NES source
changes, rebuilds, packaging, or device operations while this review is open.
Finding another difference does not by itself authorize implementing it. No
device writes were made during this audit or the interrupted build attempt.

## Files and reproducibility

Output directory:
`private-backups/readback-20260926-024637/analysis/execution-map-20260928/`.

- `application-indexed.asm`: complete application listing with direct targets.
- `runtime-ram-indexed.asm`: relocated RAM code/data at its execution addresses.
- `bootloader-indexed.asm`: separate normal-boot bank, not the USB RAM helper.
- `manifest.json`: hashes, coverage, exclusions, explicit incomplete status.
- `init-tables.json`, `device-init.json`, `nested-callback-tables.json`:
  reviewed initialization registries and resolved callsites.
- `cache-ram-data.json`: the additional 32-byte initialized cache-RAM region.
- `application-flow.json`, `bootloader-flow.json`, `boot-phase-frontiers.json`:
  potential static paths and unresolved boundaries.
- `unresolved-application-frontier.json`: each unresolved site with preceding
  instructions, so missing edges stay visible rather than disappearing.
- `interrupt-registration-sites.json`: 32 candidate direct calls to the stock
  interrupt-registration routine, including argument-load context.
- `application-code-pointer-candidates.json`: 1,621 aligned pointer candidates.
- `function-entry-candidates.json`: candidate entry addresses, not proven
  function boundaries. Data can resemble both pointers and instructions.
- Per-area direct-call, indirect-transfer, and MMIO-literal inventories.

Run from `F:\dev\fm1`:

```powershell
C:\Python311\python.exe tools/jieli-wl82/test_map_stock_execution.py -v
C:\Python311\python.exe -O tools/jieli-wl82/test_map_stock_execution.py -v
C:\Python311\python.exe tools/jieli-wl82/map_stock_execution.py
```

The mapper re-decodes the original pinned backup in memory, checks the payload
CRCs and equality with the analysis binaries, and checks every listed opcode
byte. It does not print/store decoded keys or extract configuration/identity
regions. Existing offline application-decoding code is reused, not a USB tool.

## Coverage and provenance

Original 1 MiB backup SHA-256:
`a45aa21afd8a659612427213ce80c7500a42fcbd8548262fb611f03df84e8b61`.

| View | Address / extent | Bytes accounted for | Meaning |
|---|---|---:|---|
| Normal boot bank | `0x01C02000..0x01C05820` | 14,368 / 14,368 | Decoded flash `0xB0..0x38D0` |
| Application | `0x02000120..0x0208E75C` | 583,228 / 583,228 | Code, constants, embedded RAM initializers |
| Main RAM copy | `0x01C00000..0x01C09E5C` | 40,540 / 40,540 | Duplicate view of app source `0x020848E0` |
| Cache-RAM data | `0x01F28000..0x01F28020` | 32 / 32 | Duplicate view of app source `0x0208E73C` |

Unique decoded payload bytes: **597,596**. RAM views must not be added again.
Application SHA-256:
`fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
Boot-bank SHA-256:
`fcaf033c5e10526353ecb11279a7556ac8645026d370c0267912cb4bfa7c7e4d`.

This is not a claim that all 1 MiB is executable. Flash directories, configuration,
persistent/private storage, and padding are not made into fictional functions.
The application listing has 9,118 unknown-instruction rows; the RAM view has
1,302 (overlapping application bytes), and the boot view 59. Linear sweeping
constants/data produces invalid opcodes and meaningless blocks. These rows are
retained. Global counts are not CPU failures, function counts, or executed code.

## Recovered normal-boot order

```text
chip mask ROM                 instructions/reset PC not in this flash backup
  -> stock RAM boot bank      flash 0xB0 -> RAM 0x01C02000, 14,368 bytes
  -> XIP application          flash 0x4120 -> CPU 0x02000120
     -> stacks / boot-info prefix / BSS clear / initialized RAM copies
     -> cache setup / memory hook / update result
     -> main 0x02001C24       watchdog, clocks, IRQs, latch release, OS
     -> app task 0x02004734
        -> devices -> early registry -> platform registry
        -> board power -> ADC/calibration -> normal/module registries
        -> application-core setup -> late registry
        -> stock synth/configuration allocation and setup
        -> UI setup 0x0200429E -> register UI task 0x02022E90
        -> later ALINK/audio setup

UI task (scheduled separately):
  UART1/PH8 input and UI state -> SPI1/GPIO -> 100 ms wait
  -> panel table / sleep-out wait -> black fill -> display-on -> UI loop
```

Task creation before the later audio setup is proven; which task actually runs
first depends on scheduling. It is not a claim that the LCD is fully initialized
before ALINK starts in every execution.

Bootloader and application RAM overlap physically but belong to different
phases. Never merge their functions by address. The USB downloader's RAM helper
is a third, different executable and is not part of this normal-boot trace.
Detailed load/stack/ABI evidence remains in `FM1_010_VS_T1_BOOT_TRACE.md` and
`firmware/nes/STOCK_STARTUP_ABI.md`; older statements about missing power there
describe T1, not the subsequently flashed stock-power NES candidate.

## Indirect initialization calls now included

All addresses are CPU addresses; table ends are exclusive.

| Group | Table | Indirect callsite | Recovered targets |
|---|---|---|---|
| Early | `02083964..02083974` | `020047C2` | `0200546C`, `020290BA`, `0205C4CA`, `0200335C` |
| Platform | `02083978..0208397C` | `020047DA` | `02032ACE` |
| Normal | `0208395C..02083964` | `02004E4E` | `020054A2`, `02038E54` |
| Module | `0208397C..02083980` | `02004E66` | `02032004` |
| Late | `02083974..02083978` | `02004FBC` | `020345C0` |

Additional registries, previously absent from the simple direct-call trace:

- Device table `020839FC..02083A14`, stride 12, call `020047A0`:
  `audio -> 0203B4DA`, `otg -> 0203452E`.
- Nested audio table `02083AD4..02083B1C`, stride 12, call `0203B4F6`:
  `0203F040`, `0203ECC0`, `02040210`, `0203DCC8`, `0203D3BE`, `0203CA60`.
- Nested configuration-backend table `02083998..020839EC`, stride 28,
  call `02032AE4`: `02032E68`, `02032358`, `02032B06`.

The configuration backend family also has indirect read/write dispatch.
Blindly reproducing its initialization can modify persistent flash; the NES
build's deliberate exclusion of VM/BTIF repair registrations stays in force.
Retain SDK integrity handling; an apparent lack of display does not authorize
removing it. The late stock function at `020345C0` schedules `020345D6` with
`0x1F40` (8,000) delay and is not safely classified as optional USB detection.

### Audio callbacks: active versus inactive branches

Stock audio platform data `0204E030` points to `0204E040`, whose eight initial
words are zero. Review of the six initializer bodies establishes:

- `0203F040` uses fallback descriptor `0204EA84` when its descriptor pointer is
  null. The fallback pin byte at +2 is `FF`, so the call to GPIO output at
  `0203F06A` is skipped. This possible early GPIO is **not a missing LCD enable**.
- `0203ECC0` initializes RAM list heads.
- `02040210` still installs interrupt 13, handler `02041672`, priority 4; it
  does not guard that registration on a non-null channel descriptor.
- `0203DCC8`, `0203D3BE`, `0203CA60` clear their RAM state and skip the subsequent
  channel/IRQ paths when their configuration pointers are null.

These conclusions apply to these initializer branches in this pinned image,
not all later audio setup. The stock application separately configures ALINK.
There is no justification to add all six SDK audio modules just to light the LCD.

## Boot differences that still matter

| Area | Stock evidence | NES status / decision |
|---|---|---|
| Boot handoff | 24-byte common prefix and pointed 32-byte header | Existing bridge retained; real execution remains unobserved |
| Power | Inlined stock P33 sequence between platform and normal callbacks | Recovered selectors implemented in last flashed candidate; dark result persists |
| Device/config registries | Two device entries, six nested audio entries, three config backends | Not blanket-copied; distinguish active hardware setup from persistence/services |
| Display scheduling | UI task pointer at `02004676`, stored at `02004680`, registered at `02004688` | NES visible game output is downstream of preparation/keyscan and audio startup |
| LCD transport | SPI1 setup before initial delay; byte commands, DMA data; black fill before `29h` | NES uses its ported transfer path; T2 polling diagnostic is not a stock-equivalent driver |
| Timing | Stock SDK delay and interrupt-driven services | No physical tick/clock measurements; host tests do not establish running hardware |
| Reset/backlight | No dedicated output identified in reviewed LCD path | Still unresolved; do not guess a pin |

PH8 is configured as input/pull-up alongside UART1 (`0x12100`). It is not a
demonstrated panel-reset output. PC6 is later ALINK channel-3 data, not an
established amplifier/backlight-enable GPIO. Existing pin evidence is in
`firmware/nes/STOCK_BOARD_PROFILE.md`.

## Remaining boundaries, not a completion claim

The broad potential application trace currently contains 24,438 instruction
rows and **61 unresolved indirect sites**. The boot-bank trace contains 4,294
rows and four unresolved indirect calls. Calls are assumed able to return;
conditions and iteration counts are not simulated, and asynchronous IRQ/timer/
task callbacks are not automatically proven reachable. Missing indirect edges
mean this is neither an exhaustive execution graph nor a runtime trace.

Focused slices improve the next review order:

| Slice (stopping before the named call/setup) | Unresolved indirect sites |
|---|---:|
| App entry to OS start at `02002070` | 0 |
| App task to UI setup call at `02005184` | 17 |
| UI setup to task registration at `02004688` | 6 |
| UI task to SPI setup at `02023086` | 0 |

These slices reach no unknown instructions or malformed/unclosed printed
blocks. Zero here does **not** prove correct electrical behavior, callback
delivery, completion of waits, or equivalence with the replacement firmware.

Next work: resolve the pre-UI configuration/flash dispatch and table branches,
trace the remaining IRQ/timer registrations, and classify each hardware side
effect and its prerequisites. Finish and present the stock reconstruction and
its unresolved boundaries before proposing replacement-firmware changes. No new
ROM should be called bootable merely because its link, CRCs, or flash readback pass.

The 16 mapper regression tests pass normally and under `python -O`. They cover
byte mismatches/overlap/gaps, relative displacement sizes/signs, predicated
if/else and hardware repeats, unknown instructions, indirect jumps, explicit
phase handoffs, milestones, pointer bounds and registry validation. They test
the analysis tooling, not the FM-1 hardware.

## Stock-only continuation after the implementation stop

The pinned application SHA-256 above was rechecked. The following are manual
disassembly/dataflow findings, not a regenerated closure count or a claim that
all newly reached callees have been reviewed. The 17/6 frontier counts above
still describe the saved mapper output.

### Configuration dispatch

The 28-byte records at `02083998..020839EC` contain an initializer at +0,
an ID predicate at +4, a read callback at +8, and a write callback at +12.
`020027A4` and `020033F8` invoke the predicate; `020027B4` invokes the selected
read callback; `02003410` invokes the selected write callback. The writer checks
that +12 and +4 are nonzero before calling the predicate.

| Record | Predicate | Read | Write |
|---|---|---|---|
| `02083998` | `02032ED4` | `02032EEC` | `02032F40` |
| `020839B4` | `0203267C` | `02032696` | `02032786` |
| `020839D0` | `02032BD2` | `02032D0E` | null |

This resolves the registry sources of these four indirect sites, not every
internal path within the selected backend. Null callbacks must not become
invented edges in an execution graph.

### File and device dispatch: distinct registries

The mount routine `02028F84` compares the requested filesystem name with
records at `0208377C..0208395C`, stride `0x78`. At `0202903C` it calls record +4;
on success `0202905C` stores that record pointer into mount +8. The records are
`nor_sdfile`, `sdfile`, `sdfile_fat`, and `fat`, with mount targets `02028B46`,
`0202842C`, `02029778`, and `02029778`, respectively.

The early initializer's call at `020291AA` specifically supplies:
`device = null`, mount point `"mnt/sdfile"` (`0204EF70`), filesystem
`"sdfile"` (`0204ED31`), and zero remaining options. On that successful path
the selected operations record is `020837F4`, not an arbitrary FAT backend.

For a file on that mount:

| Indirect callsite | Pointer provenance | Selected target / condition |
|---|---|---|
| `0200367E` | mount +8 -> operations +18h | `02028940`, open |
| `02003700` | file +0 -> mount +8 -> operations +68h | `02028B1E`, attributes/address |
| `02003744` | file +0 -> mount +8 -> operations +48h | `02028AB2`, free per-file state |
| `0200381E` | operations +8 on final mount teardown | null for this backend; guarded/skipped |
| `0200370C` | device +8 -> device operations +18h | reached by close only if mount +Ch device is non-null; this mount supplied null |

`02028B1E` returns file length/address into attributes +4/+8. This explains
the UI setup's use of its result, without treating a file address as a GPIO.

The device-open routine `02028E76` uses a **different** registry,
`020839FC..02083A14`, stride 12. Its `02028EAA` call is device operations +8
(audio `0203B502`, otg null); `02028F80` is device operations +1Ch (audio
`0203C2B6`, otg null). These are not filesystem mount callbacks. Their presence
in an unpruned static closure does not prove the null-device sdfile mount runs
them. Other callers still need path-specific review.

### UI setup has persistent side effects before task creation

At `020042AE`, the UI setup opens `"mnt/sdfile/app/usr"` (`0204F276`) with
`"r+w"` (`0204EB22`). It obtains the file attributes, saves the mapped address,
and closes the file. At `020042FC..02004306` it copies `0x76` bytes from the
derived user-data location and tests the first byte against 4. On mismatch it
constructs defaults and calls the persistent write path `020038D4` at
`020043BA`, before registering the UI task at `02004688`.

This is stock behavior, **not** a new requirement to reproduce its settings
writes in NES. The prerequisite/read/write distinction must survive the audit.

### Initial LCD wait depends on scheduler infrastructure

The stock wait routine `0205B9D2` calculates
`max(1, floor(ms/10) + (ms % 10 > 5))` ticks and calls `02059C52`, which forwards
to `02059C34`. Thus the LCD's 100 ms and 120 ms requests become 10 and 12 ticks.
`02059C34` calls `02058D72`, the task-delay routine `02059912`, then `020591FC`
and conditionally the scheduling routine `02059116`. It is not a CPU-spin wait.

OS startup `0205A8A6` installs the CPU-0 IRQ-3 handler `0205BF62` at
`0205A960`, obtains a named clock at `0205A96E`, and writes clock/100 to
`01EEF0F4` at `0205A97A`, before setting control byte `01EEF0EC` to 1. The
handler acknowledges via that control byte, increments `01C08FD4`, calls
`02058EB2`, and conditionally schedules via `02059116` before `rti`.

These linked instructions establish a pre-display timing dependency. Full
tick-list/context-switch semantics and hardware delivery remain open; this is
not evidence that the replacement's scheduler is the actual dark-screen cause.

### Review still open

- Resolve the five pre-UI table branches with verified table-branch semantics,
  then follow every selected destination; do not disassemble table bytes as code.
- Follow configuration-backend internals and remaining file operations, pruning
  branches only when actual arguments/state justify it.
- Trace IRQ/timer/task callbacks separately from synchronous call flow, including
  tick delivery, allocation failures, fatal loops, and waits before display-on.
- Finish register-by-register clock/power/cache/GPIO ordering, including branches
  dependent on inherited boot information. Mask-ROM internals are not in this dump.
- Explain what establishes panel power/reset/backlight, or explicitly identify
  what cannot be determined from firmware alone. No pin has been proved for a
  separate panel reset/backlight control.

Only documentation was changed in this continuation. No NES implementation,
compiler/build invocation, USB helper transfer, firmware write, or reset occurred
after the user's stock-first correction.

## UBOOT-ready checkpoint: branch-dispatch audit, still no flash

The user has power-cycled, entered UBOOT, and authorized flashing **when ready**.
Windows enumeration confirms `WL82 UBOOT1.00 USB Device`,
`\\.\PHYSICALDRIVE3`, status OK. This was enumeration only; no helper was
uploaded and no flash read/write/reset command was sent. Re-enumerate the exact
device before any eventual operation; a disk number is not a permanent identity.

The interrupted `boot-screen-first-smb1/build-manifest.json` still says
`build_in_progress_or_failed`, `flashable: false`. Authorization does not make
that incomplete candidate ready, and does not cancel the user's stock-first
instruction. No NES source, library, image, or build manifest changed here.

### Six bounded table branches are now decoded

Two isolated C switch examples were passed on stdin to the installed vendor
compiler (`C:/JL/pi32/bin/clang.exe`, `-target pi32v2 -mcpu=r3 -mfprev1 -Oz -S
-x c - -o -`). Assembly was printed to stdout only: no firmware compile/link or
candidate build was performed. The compiler demonstrates:

```text
tbb [r0]    -> .byte  (case_label - immediately_following_table)/2
r1 = r0 << 1
tbh [r1]    -> .short (case_label - immediately_following_table)/2
```

This verifies both the table base and halfword-scaled destination offsets;
TBH uses a byte-scaled index register. Bounds were separately read from each
stock callsite. Raw entries and every destination are now in
`analysis/execution-map-dispatch-20260928/reviewed-table-branches.json`.

| Branch site | Bounded entries | Classification from surrounding instructions |
|---|---:|---|
| `01C0035A` | 8 byte entries | Flash read-mode selector from SFC config bits 8..11; modes with sign bit set are rejected |
| `02002E28` | 22 halfword entries | Formatting character dispatch, index character minus `63h` |
| `02003040` | 21 halfword entries | Formatting integer-type dispatch |
| `0201D4BC` | 7 byte entries | Flash-control requests 200..206 |
| `0201D4D4` | 4 byte entries | Flash-control requests 100..103 |
| `01C00666` | 5 byte entries | Nested flash-erase request dispatch 200..204 |

The first five were the earlier frontier. Resolving them exposed the nested
erase switch and two more filesystem callbacks, illustrating why merely
resolving one frontier is not completion. The nested erase routine selects
`20h`, `D8h`, `C7h`, or `81h` command paths and uses the existing flash-transfer
helpers; it is not an LCD power-enable routine. **No such command was executed.**

The four reviewed configuration predicate/read/write sites from the preceding
section are also included in the updated mapper. All non-null registered
targets remain potential paths; this does not assert every backend executes.

### Reproducible output and remaining work

```powershell
C:\Python311\python.exe tools/jieli-wl82/test_map_stock_execution.py -v
C:\Python311\python.exe -O tools/jieli-wl82/test_map_stock_execution.py -v
C:\Python311\python.exe tools/jieli-wl82/map_stock_execution.py --out private-backups/readback-20260926-024637/analysis/execution-map-dispatch-20260928
```

19 tests pass both normally and under `-O`, including table-width/base/offset,
out-of-range entries, wrong caller/register, and targets inside table data.
The previous output directory remains as the earlier checkpoint.

Updated potential application closure: 25,562 instruction rows, **54 unresolved
indirect sites**. Pre-UI app-task slice: 13,076 rows, **10 unresolved sites**.
UI setup through task registration: 7,462 rows, **5 unresolved sites**.
App entry through OS start and UI task through SPI setup still have zero
unresolved indirect sites in these synchronous slices. These slices reach no
unknown instructions, malformed/unclosed printed blocks, or missing boundaries.

The two newly exposed filesystem sites are `0202835A` (operations +28h, seek)
and `020283B0` (operations +24h, write/program). For the previously established sdfile
mount these point to `02028A4A` and `020289F4`; other mount contexts must not be
silently assigned the same target. The remaining ten sites all belong to the
file/device dispatch families, but backend bodies, asynchronous callbacks, and
hardware initialization still require the review described above.

**Readiness remains false.** The dark-screen cause has not been established,
and this analysis update is not a reason to flash the old or unfinished image.

## Stock-first review conclusion for the next controlled experiment

This section supersedes the progress-only readiness checkpoints above. The
bounded review now covers the dumped bootloader, application startup, main
clock/latch setup, init registries, board power/ADC, UI GPIO setup, timer/tick
dependency, and the first LCD display-on path. It is **not** a claim that every
instruction in stock firmware or every asynchronous RTOS path has been recovered.
The 54 potential unresolved indirect sites in the broad graph remain reported;
they must not be replaced with an invented zero. No mask-ROM internals or PCB
reset/backlight connections can be recovered from these listings alone.

### Final cross-checks before implementation resumes

- Stock main `02001C24..02002078` and the linked SDK `clk_early_init` were
  compared through source selection, divisors, USB PLL writes/delays and clock
  reports. Both accept `clk_get("lsb")`; that name is not a discovered bug.
  SDK setup also retains interrupt setup, cache/memory setup, CRC mutex and P33
  latch release. This is static parity evidence, not measured runtime clocks.
- Stock ADC block `02004C2A..02004E38` matches the pinned SDK
  `cpu/wl82/key/adc_api.c`: optional VDDIOM trim, analog ADC gates, battery and
  bandgap sampling, IRQ24 and a 2 ms scan callback. The voltage trim runs only
  for selector 6; our recovered power parameters request selector 4. Do not
  introduce a guessed 3.5 V trim as a display fix. ADC sampling is not reproduced
  in this LCD experiment; it has no identified panel GPIO operation.
- Both observed startup mounts have a null device argument. Early sdfile mount
  selects `0202842C`; `/tmp/audio` requests `ramfs`, absent from the four-entry
  filesystem registry, and returns failure without a mount callback. For the
  **sdfile context only**, open/attributes/close/seek/write are `02028940`,
  `02028B1E`, `02028AB2`, `02028A4A`, `020289F4`; unmount is null and device
  ioctl is not selected. The +24h callback programs flash, not reads it. The
  replacement continues to omit VM/BTIF persistent repair registrations while
  retaining read-only cfg_bin and the SDK's delayed integrity handling.
- UI setup includes PA2 low at `02004504`. Its electrical purpose is still
  unproved. Reviewed 2 ms callbacks `0202110E` and `020213B0` manipulate musical
  sequencer state; `02024552` samples ADC/power state and queues events. None
  establishes PA2 as a panel reset/backlight output. PA2 therefore remains
  untouched, not relabeled or guessed. PH8 is input; PC6 belongs to ALINK;
  PH6/PH9/PA9/PA10 are scanner multiplex outputs.
- Stock tick setup/IRQ and task-delay code explain why 100/120 ms LCD waits
  require a working scheduler. SPI1's IRQ is enabled **after** the initial
  fill and command 29h. Initial display-on itself is polled. The existing
  replacement instead gates all LCD traffic behind reported-clock validation,
  a tick-progress check and SPI2 scanner startup. No execution trace proves
  which gate, if any, has failed on the physical device.

### One coherent test change, not a claimed root-cause fix

Finish the preserved `screen-first` integration: after reviewed stock-derived
board power, before normal/module/late init and the NES worker, issue the exact
stock panel table and visible RGB/yellow checkpoint using bounded polling and
finite watchdog-fed delays. Do not add unproved GPIO, PLL or voltage changes.
Then continue all SDK init (including integrity checks) and the existing SMB1
worker; its later LCD checkpoint is blue. This removes the clock-report,
scheduler-wait, keyboard and audio dependencies from the **first** picture.
It does not remove startup, board-power or physical panel dependencies.

The diagnostic intentionally differs from stock in first-picture placement,
byte polling instead of parameter/pixel DMA, visible pixels, and conservative
uncalibrated delays. These are declared experimental differences, not claims of
stock equivalence. Host tests check protocol/order, no timer/clock reads during
the first picture, and bounded stuck-SPI behavior; ELF audits check startup,
power, init ordering, ROM and unchanged integrity scheduling. Packaging must
preserve every byte outside the application replacement and its two directory
records. Use only the existing jluboottool.py sector writer after two matching
full preflight reads, and require sector plus two full final readbacks.

The user has authorized this recoverable bench experiment and already supplied
cold-UBOOT readiness. A verified write is not a successful boot. Remaining
hardware questions must be reported with the visible test result rather than
misrepresented as completed reverse engineering.
