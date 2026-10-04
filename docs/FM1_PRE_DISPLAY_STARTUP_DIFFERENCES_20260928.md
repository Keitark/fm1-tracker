# Further investigation after the failed screen-first test

## Result and scope

The stock010 and last-flashed NES images have a concrete **CPU1/scheduler
startup difference before any picture is attempted**. It is a credible place
to investigate, not a proved cause of the blank display. There is no captured
normal-boot PC, execution trace, or surviving crash record.

The user manually returned to UBOOT after testing. UBOOT enumeration therefore
does not establish that the firmware reset or fell back there by itself.
This investigation changed no firmware source or installed image, uploaded no
helper, and sent no device command. Only offline analysis/tests and notes were
added. The last bench result remains **no screen**.

## Binary evidence

Stock application: `app-FM-1_010.bin`, SHA-256
`fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
NES application: `build/boot-screen-first-reviewed-smb1/fm1-nes-boot-candidate.app.bin`,
SHA-256 `eda772a719fe94600023d8d709ebbe972aa165386131036b59310769201c4a88`.
Addresses below are CPU virtual addresses, **not flash offsets**.

| Step | Stock010 | Last-flashed NES |
|---|---|---|
| CPU1 launch location | Main calls `0205A2B8` at `02002040`, before `os_init` | Inside `os_start`, `02027002..0202702A` |
| CPU1 vector slot | `01C7FFF8` points to `020001B8` | Same slot points to `02000224` |
| CPU1 C entry | `01C023AE`, internal RAM; signals ready, initializes interrupts/debug, then a polling worker | `0200086C`, XIP; signals ready, initializes interrupts/debug, then calls `os_start` |
| Clock-divider guard | Saves `[00010008]`, ORs `8`, starts CPU1, waits for ready, restores saved value | No corresponding save/OR/restore in launch sequence |
| Ready wait | Byte `01C1FEE8`, unbounded loop at `0205A2EE` | Byte `01C1AA2D`, unbounded loop at `02027028` |
| OS arrangement | CPU0 scheduler; CPU1 worker does not call `os_start` | Both cores enter scheduler |
| First NES picture | Not applicable | App-task call at `02001B7C`, **after scheduler dispatch**, board power and early/platform init |

Stock CPU1's worker checks `01C16EA0/01C16EA1` and calls `01C010FE` or
`01C01BFA`; it is not merely an unused core. Stock's CPU1 entry still calls
XIP interrupt/debug functions, so it is also wrong to describe its entire
CPU1 path as RAM-only.

### Correct register identity

The SDK `WL82.h` maps the clock block at `00010000` as PWR_CON, RESERVED0,
SYS_DIV, CLK_CON0. Thus **00010008 is SYS_DIV**, while CLK_CON0 is 0001000C.
`clock_hw.h` defines HSB_CLK_DIV in SYS_DIV bits 0..7. The stock OR-with-8
modifies that divider field; it must not be relabeled a CPU1-enable bit.
Its actual timing effect depends on the inherited divider value, which has
not been measured during normal boot. The guard may be a compatibility or
timing measure; disassembly alone does not prove its purpose.

The SDK archive corroborates that the missing guard is not an accidental
link omission: `system.a:port.c.o` contains `EnableOtherCpu` with vector,
ready-byte, core-control writes and wait, but no SYS_DIV access.
`vPortStartFirstTask` calls it on core0. LTO inlined this into the observed
`os_start` body. Read-only archive extraction and target-preserving LLVM IR
rendering produced `tmp/port.c.o` and `tmp/port.ll`.

## Two suspected missing steps ruled out

1. Cache setup: stock `01C00E88..01C00F98` and NES
   `01C00CC0..01C00DD0` have identical 272-byte bodies after normalizing
   exactly three relocation operands: cache-data source, memmove call and
   memset call. MMIO operands, masks, waits and loop instructions match.
   This proves this routine's static equivalence, not identical runtime state.
2. Interrupt enable: NES main's enable call occurs after `os_start`, but
   `prvIdleTask` begins at `02028E8A` with `__local_irq_enable`, which can
   execute `sti` at `01C00E5E`. Therefore the main-function order alone is
   **not evidence of forgotten interrupt enable**.

## Why not simply change CPU_CORE_NUM?

The pinned SDK defines it as 2. Its source and Jieli's
[AC79 single/dual-core documentation](https://doc.zh-jieli.com/AC79/zh-cn/master/module_example/system/操作系统单双核使用方法.html)
say the one-core-OS arrangement requires a **single-core-specific system.a**
and appropriate interrupt routing. The documentation directs developers to
Jieli for that library. CPU1 cannot use OS-dependent calls in that arrangement.
Changing only the macro against our existing SMP archive is not a sound fix.
This is AC79 documentation, not the similar but different AC792 page.

## Remaining uncertainty and next controlled test design

The last image's RGB checkpoint removed timer delays, scanner and audio from
the first-picture path, but did **not** remove OS startup. A CPU1-ready wait,
scheduler failure, board-power wait, or LCD problem can all still give the
same visible result. Successful flash readback cannot distinguish them.

The next diagnostic should establish execution **before and after CPU1/OS
startup**, rather than adjust panel commands again. Two constraints matter:

- Existing RAM breadcrumbs at `01C0287C`, `01C03470`, `01C03484/88` are not
  live evidence after manually re-entering UBOOT. A helper upload/reset can
  overwrite them. Do not read them now and assign a failed-boot stage.
- A genuinely pre-OS picture needs a separately reviewed power/display
  dependency closure. Moving board_init wholesale is not yet justified:
  power_init contains IRQ registration, TCNT polling, and configuration-gated
  allocation/timer paths. The current config is zero, but those branches and
  all preconditions must be accounted for before moving the call.

A direct execution trace would discriminate the CPU1-ready loop from later
failures. The bundled debugger is not ready to use: its configuration names
WL80, includes reset/download actions and stale paths; there is no established
FM-1 debugger pin mapping or verified connected debug adapter. Do not run that
download script as a harmless status query.

Do not claim the SYS_DIV difference is the root cause, blindly substitute a
single-core library, move power init without its dependency audit, or reflash
the unchanged failed candidate. No new hardware test was performed here.

## Reproduction

Run `C:\Python311\python.exe tools/jieli-wl82/test_stock_startup_comparison.py`.
Six checks pass against the hash-pinned application binaries, including exact
launch bytes, core entry targets, relocation-normalized cache parity, idle
interrupt enable and app-task display placement. These are **evidence
regression checks**, not six successful hardware tests.

For readable instruction views use `trace_stock_calls.py` with the ranges
above (`--ram` for 01C addresses); compare the existing NES `disassembly.asm`.
The SDK checkout was verified unchanged during this investigation.
