# FM-1: stock-derived board power initialization

Date: 2026-09-28. Offline implementation; **not flashed or hardware-qualified**.

Subsequent bench update: this candidate was packaged, written and twice
full-readback verified at the user's request. See
`FM1_STOCK_POWER_FLASH_20260928.md`. The text below records the earlier offline
checkpoint; normal boot/display qualification remains pending.

## Why it was missing, and what is now implemented

The replacement's board hooks had deliberately omitted the SDK demo board's
power parameters because those values were not established for the FM-1. That
left a real initialization gap, not a completed port. The working stock 010
image provides enough instruction-level evidence to implement this stage now.

`firmware/nes/boot/board.c` now calls `fm1_board_power_init()` once from
`board_init`, before recording the board-complete breadcrumb. The new
`boot/board_power.c` passes recovered selectors to the pinned SDK's actual
`power_init` API. It does not copy stock RAM addresses, private state structures,
or absolute stock function calls into the replacement.

The linked order is:

1. Existing startup, RAM copy/clear, cache initialization and memory hook.
2. SDK main/task setup, early-board marker, early/platform initcall groups.
3. New stock-derived power initialization; then board-complete marker.
4. Later SDK initialization, application entry and separate NES worker.

This closes one known boot difference. It does **not** establish that this was
the sole cause of the blank screen, or that execution reaches this point on
hardware. LCD reset/backlight behavior and analog audio remain separate issues.

## Evidence and parameter mapping

Reference: decoded `app-FM-1_010.bin` from the preserved original 1 MiB backup.
SHA-256: `fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
All addresses below are CPU XIP addresses, not raw NOR file offsets.

The stock power sequence is inlined into the application task, approximately
`0x020047e0..0x02004c2a`. The values below are **effective parameters recovered
from instructions**, not a recovered original C initializer.

| Setting | Selector | SDK nominal interpretation | Stock evidence |
|---|---:|---|---|
| `config` | 0 | No optional system-sleep/RF-idle flags | State clear at `0x020047e0`; flags remain zero |
| `btosc_disable` | 0 | Disable flag clear | Store at `0x02004830` |
| `vddiom_lev` | 4 | 3.2 V | P33 reg 5, bits 0..2; arguments at `0x02004a66` |
| `vddiow_lev` | 3 | 3.2 V | P33 reg 5, bits 4..5; arguments at `0x02004a82` |
| `vdc14_lev` | 7 | 1.60 V | P33 reg 6, bits 0..2; arguments at `0x02004a74` |
| `vdc14_dcdc` | 1 | DC/DC enabled | State at `0x020049c8`; transitions at `0x020049d2/49ec/4a06` |
| `sysvdd_lev` | 15 | 1.38 V | P33 reg 9, bits 0..3; arguments at `0x02004ad4` |
| `vlvd_value` | 7 | 2.6 V threshold | P33 reg `0x11`, bits 3..5; arguments at `0x02004996` |
| `vlvd_enable` | 1 | Detector enabled | P33 reg `0x11` bit 0 at `0x020049b4` |

Voltages are SDK enum labels, **not measured rail voltages**. The demo's VDC14
selector 3 (1.4 V), SYSVDD selector 11 (1.26 V), and VLVD selector 6 (2.5 V)
are not the values used by this working stock image.

Stock's field-set calls above resolve to `P33_CON_SET` at `0x01c0108c`.
The pi32 paired `#` instructions store the old operand while the paired
operation computes the new value; treating those as ordinary sequential
instructions gives incorrect values in several parts of this trace.

The pinned SDK ignores input fields `osc_type`, `osc_hz`, `delay_us`, and
`pd_wdvdd_lev` in this particular `power_init` implementation. We set them to
zero; we do not claim their stock source values were recovered. The SDK selects
LRC type 2 and 32,000 Hz internally, matching the reviewed stock path, and
derives its calibration from the reported system clock.

`verify_stock_power.py` checks the reference image hash, 12 instruction points,
and the relevant stock field-set call targets. This is a build prerequisite:
missing/private reference data or an unreviewed stock revision fails closed.

## SDK integration and side effects

SDK commit: `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`.
Actual ABI: `include_lib/driver/cpu/wl82/asm/power_interface.h`;
voltage selectors: `asm/p33.h`. Compile-time checks enforce the 20-byte
parameter layout, key field offsets, and numeric selectors.

The implementation from `cpu.a(power_hw.c.o)` was inspected through LLVM IR and
the linked disassembly. It performs PMU/P33 setup, the three DC/DC transitions
with intervening 1,000-iteration waits, selected voltage writes, LRC bookkeeping,
and low-power timer initialization. LRC IRQ 44 uses priority 2; timer IRQs 49/50
use priority 3, CPU 0. These are SDK-owned handlers and state.

Stock clears 496 bytes of power state; this SDK clears 492. They are different
library revisions, not binary-compatible internal state layouts. Using the
SDK API lets each helper operate on its own compiled layout. This is a semantic
port of the recovered setup, not byte-for-byte replacement of stock execution.

The reviewed `config=0` leaves optional sleep and RF-idle paths inactive. The
compiled library still contains their conditional branches and other firmware
services; this is not a claim that the entire SDK is free of flash-write code.

Safety constraints retained:

- No SDRAM; the board source rejects an SDRAM-enabled configuration.
- `CONFIG_EXCEPTION_AUTO_FIX_ENABLE`, `CONFIG_RF_TEST_ENABLE`, and
  `CONFIG_READ_RF_PARAM_FROM_CFGTOOL_ENABLE` are rejected in this board source.
  These could introduce flash repair or override recovered power parameters.
- The linked power path has neither the auto-fix nor cfg-voltage-override hook.
- Existing VM/BTIF auto-repair registrations remain discarded; read-only cfg
  registry and SDK integrity machinery remain intact.
- No guessed external reset, amplifier-enable, or backlight GPIO writes added.
- No chip keys queried, stored, or changed. No USB/device operation performed.

## Audit and tests

`audit_power.py` checks both the retained parameter blob and the **executed**
code: LTO constant-folds those parameters, so checking the blob alone is not
sufficient. It checks all reviewed direct-call relocations and address loads,
then fingerprints the remaining 1,082-byte initializer with only those checked
relocations normalized. Register values, branches, delays and MMIO operands
remain part of the fingerprint. The gateway and its surrounding platform/normal
initcall loops are also checked. Compiler changes require a fresh review.

Normalized initializer SHA-256:
`9fd637a2334e7ad01ec3bc5c048833f21d1fffe12cc7eae5ee2c359c534b4d05`.

Host power tests use the actual SDK struct/enums with a mocked SDK call. The
vendor header's redundant `*lpres;;` is normalized only in a generated host
test header for MSVC compatibility; no SDK source was edited.

Validation includes:

- Host checks for parameter bytes, board-stage order, and one-time execution.
- Stock-reference tests including altered hash and instruction rejection.
- Boot-audit negative tests for changed compiled voltage, parameter corruption,
  redirected helper, skipped gateway/call, and altered initcall order.
- Existing startup, configuration, delayed integrity, task and IRQ checks.

These tests do not execute the P33 hardware writes on the PC.

Completed validation: all 14 host CTest targets, all 26 boot-audit tests, and
all 3 stock-reference tests passed. Python suites also passed with `-O` (checks
do not depend on Python assertions). The build manifest's source and linked
archive hashes matched current files; the pinned SDK checkout stayed clean.
The original NES candidate still passes its historical static audit without
claiming that the newly added power stage existed in that image.

## Candidate and bench boundary

Separate output: `firmware/nes/build/boot-stock-power-smb1/`.
Application: `fm1-nes-boot-candidate.app.bin`, 180,080 bytes.
SHA-256: `8e1a218eaba2abc7330abaf9c2da30c7f7e75fc729d4c8788bee5c1ed5356445`.
XIP entry remains `0x02000120`; startup remains `0x0200018a`.
The initialized RAM copy is now 10,352 bytes at `0x01c00000`, sourced from
`0x02029800`; BSS starts `0x01c02870`, length 98,240. Link/audit checks derive
these operands from the new image rather than reusing older addresses.

This is an application payload, **not a packaged 1 MiB flash image**. Original
NES, T1, and previous refined application binaries are retained separately.
The manifest remains `flashable: false` and `device_operations_performed: false`.

Live RAM diagnostics: `fm1_power_stage` at `0x01c03470`: 0 not entered, 1 SDK
call in progress, 2 returned. The ordinary board breadcrumb is recorded only
after return. This does not report a measured rail or successful LCD startup;
it requires a live debugger and does not survive reset/re-entry into UBOOT.

Next bench work is to package a separately identified diagnostic/new candidate
using the preserved stock container, then qualify actual startup and display.
No claim of a fixed screen or successful hardware boot follows from this build.
