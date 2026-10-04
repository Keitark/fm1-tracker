# FM-1_010 versus T1: boot and first-display trace

Offline review, 2026-09-27. Baseline is the user's known-working **010**, not
a hypothesis about a firmware-version incompatibility. T1 was written and
twice fully read back, but the user reported no visible output on normal boot.
Storage verification does not prove CPU execution. No device commands or
firmware changes were performed for this review.

## Evidence and boundaries

- Original raw 1 MiB backup: `private-backups/readback-20260926-024637/fm1-flash-1MiB.bin`, SHA-256 `a45aa21afd8a659612427213ce80c7500a42fcbd8548262fb611f03df84e8b61`.
- Decoded 010 application: `private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin`, SHA-256 `fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
- T1 application: `firmware/nes/build/boot-display-smb1/fm1-nes-boot-candidate.app.bin`, SHA-256 `e42b44f2847bb731bf02de5aaf4a8801300273ce4d8ce60e7c1677a97c5877fa`.
- Packaged T1 raw image: SHA-256 `1f56085b9aa6c9c9e1b852141f264cc465b9b929607628cfdabe4d770f769c4d`.
- Rechecked: raw flash bytes `[0,0x4000)` are identical between original 010
  and packaged T1. Boot-bank header/data CRCs and handoff instruction checks pass.

Addresses below are CPU addresses unless explicitly called flash offsets.
Ranges are end-exclusive. Stock function names inferred by matching SDK
behavior are descriptive labels, not recovered vendor debug symbols.
This is a static trace, not an instruction trace captured from running hardware.
Mask-ROM instructions/reset PC and every indirect initialization callback have
not been recovered; the report does not claim a complete ROM-to-LCD call graph.

## 1. What is loaded first?

```text
Chip mask ROM (exact reset PC / internal instruction sequence not recovered)
  -> stock uboot.boot bank: flash 0x0000A0, 16-byte bank header
     decoded code from flash 0x0000B0, length 0x3820
     intended RAM load: 0x01C02000 .. 0x01C05820
  -> RAM bootloader sets up flash mapping and boot arguments
  -> CPU entry 0x02000120, backed by physical flash 0x00004120
  -> application startup clears BSS and copies selected RAM sections
  -> RAM cache setup, memory setup, main, OS/tasks, board/display setup
```

The bank's load address/length are explicit, CRC-checked metadata. Its first
instructions are decoded at `0x01C02000`; the ROM's actual bank-loading routine
is inferred, not disassembled. At that entry the bootloader initially uses an
inherited stack, clears `[0x01C05840,0x01C065A0)`, then sets
`SSP=0x01C05C40`, `SP=0x01C06040` and jumps to `0x01C037DC`.

The **whole application is not loaded into RAM at 0x01C02000**. Main application
text executes through the serial-flash mapping (XIP). For these images:

`CPU mapped address = physical flash offset - 0x4000 + 0x02000000`

Thus the application area/header at flash `0x4000` maps to `0x02000000`, and
payload at flash `0x4120` maps to entry `0x02000120`. These are mapped/decoded
CPU contents; raw flash storage bytes are not necessarily plain instructions.

Bootloader evidence:

- `0x01C0519A..1C051AE`: physical application base is installed in the SFC
  register at `r14+0xC` (`r14=0x40200`), and mapped base `0x02000000` is recorded.
- `0x01C051D0..1C051EC`: copies a 24-byte argument prefix to `0x01C7FE08`
  and its pointed-to 32-byte flash header to `0x01C7FE20`.
- `0x01C05284..1C0528C`: loads entry from application metadata, sets
  `r0=0x01C7FE08`, executes `call r1`. Entry here is `0x02000120`.
- A separate branch for entries with bit `0x04000000` set exists; it is not
  taken for this `0x02000120` XIP entry.

The USB `wl82loader.bin` helper uploaded by `jluboottool.py` also uses RAM
`0x01C02000`, but it is a **different executable used during USB servicing**.
It is not the normal-boot flash bank and does not survive a power cycle.

## 2. First application instructions and memory initialization

| Step | Working stock 010 | T1 display diagnostic |
|---|---|---|
| Entry branch | `0x02000120 -> 0x02000124` | `0x02000120 -> 0x0200018A` |
| Initial SP | `0x01C14B94` | `0x01C03404` |
| Initial SSP | `0x01C15B94` | `0x01C04404` |
| Boot-info initialization | call `0x020001E6` at `0x02000130` | call bridge `0x020021DA` at `0x02000196`, then SDK `0x02000292` |
| BSS clear | `[0x01C09E5C,0x01C211DC)`, `0x17380` bytes | `[0x01C016A0,0x01C057DC)`, `0x413C` bytes |
| Main RAM copy source (XIP) | `0x020848E0` | `0x02018AA0` |
| Same source as physical flash offset | `0x888E0` | `0x1CAA0` |
| Main RAM copy destination/length | `0x01C00000`, `0x9E5C` bytes | `0x01C00000`, `0x16A0` bytes |
| Post-copy hook | no corresponding call | `0x020002E8`, just `rts` |
| External RAM data/BSS | zero sizes | zero sizes |
| Cache setup call | `0x01C00E88` at `0x0200019E` | `0x01C00BAE` at `0x0200020A` |
| Cache-RAM data copy | `0x0208E73C -> 0x01F28000`, 32 bytes | `0x0201A140 -> 0x01F28000`, 32 bytes |
| Memory setup call | `0x02058C54` | `0x02015984` |
| Update-result call | `0x020054F6` | `0x0200133C` |
| Main destination | `0x02001C24` | `0x02000C72` |

Both run BSS clearing before copying initialized RAM data/code. Both cache
setup routines use `r1=0, r2=7` and the same reviewed cache-register/clear
sequence, with relocated sources/call targets. Different stack and section
addresses are expected for a differently linked application, not proof of a bug.
Application initialization reuses old bootloader RAM; stock itself overwrites
that bank with its RAM copy, so overlap alone is not evidence of failure.

The first *functional* implementation difference after stack assignment is
boot-info processing. Stock consumes the common prefix and writes internal
state at `0x01C7FD50`. T1 makes a 92-byte stack copy, preserves the first 24
bytes/pointer, zeros the rest, and calls SDK initialization using internal state
at `0x01C7FD4C`. The SDK also consumes extension offsets +80/+88 and writes a
32-bit value derived from the chip-ID halfword to `0x01C7FF0C`; stock's
initializer has no corresponding write. This is chip identification, not a
chip-key query. The bridge returns before BSS clearing. Its hardware compatibility
has not been established by a visible successful T1 boot.

## 3. Main, OS and board initialization

T1 main calls IRQ disable, watchdog, early clocks, interrupt setup, reset-reason
handling, movable-code setup, debug/CRC mutex setup, P33 I/O-latch release,
OS initialization, app-task creation and OS start. App-core initialization
retains SDK callbacks/integrity handling. It creates the display worker at
`0x02002154`, then returns to its event queue. The worker calls display init
`0x0200203E`. This is no longer the earlier event-loop-starvation implementation.

010 main installs app-task entry `0x02004734` at `0x02002050`, calls its task
creation wrapper at `0x02002058`, and reaches the OS-start path. That app task
walks device/init tables and falls through into board power setup at
`0x020047E0`. Examples of observed writes in that path:

| Instruction | Destination | Value |
|---|---|---|
| `0x02004836` call to P33 writer `0x01C0104E` | P33 `0x30`, SDK `P3_PMU_CON0` | `0x08` |
| `0x02004840` same writer | P33 `0x72` | `0xBF` |
| `0x02004878` | MMIO `0x13E00`, PMU control | `0x200C` |
| `0x02004882` | P33 `0x31`, `P3_PMU_CON1` | `0x44` |
| `0x0200488C` | P33 `0x54`, `P3_LP_TMR0_CLK` | `0x44` |
| `0x02004896`, `0x020048A0` | P33 `0x13/0x14`, `P3_LRC_CON0/1` | `0xD1` each |

The last four belong to the branch selected by the preceding initialization.
This is an excerpt, not a complete or safe-to-replay power recipe. Our
`boot/board.c` has empty early/normal board hooks and no linked `power_init`.
Consequently the custom image does **not** reproduce this stock power path.
The SDK demo itself calls `power_init` from its board hook, but its board-specific
voltage/rail parameters must not be blindly copied to FM-1.

Do not confuse this omission with I/O-latch release: stock writes zero to
P33 `0xD0..0xDF,0xAF,0xAE,0xAD,0x94,0x11`, and T1's
`p33_io_latch_init` already does so. Likewise the watchdog-side `0x13E04`
bit-4 write is present in T1. Neither is a demonstrated missing step.

## 4. LCD initialization differences

010 table at `0x0205012C` is 378 bytes and exactly matches our table. Rechecked
with `verify_stock_panel.py` plus `test_display_table.py`; both pass. Table hash:
`280cb746088bea4a2fce2819c1415da0597a286120e3a8229449fbafdc1280c7`.

| Operation | Stock 010 | T1 |
|---|---|---|
| Clock acceptance gate | no corresponding gate in recovered LCD path | refuses sys outside 24..396 MHz or lsb outside (0,80] MHz, before LCD writes |
| GPIO/SPI | mux `0x51020` bit 4, PC7 high, PC8/9/10 outputs; SPI1 CON `0x4021`, BAUD 4 | same pins/mux/CON/BAUD, plus explicit pull and digital-input changes |
| First 100 ms delay | **after** GPIO/SPI setup, call `0x020230BE` | **before** GPIO/SPI setup |
| Command bytes | SPI1 BUF at `0x11D08` | same |
| Parameters and pixels | DMA ADDR/CNT at `0x11D0C/0x11D10` | per-byte BUF polling, no DMA |
| Sleep-out delay | 120 ms after `0x11` | same table interpretation |
| Initial fill | helper `0x02021600`: windows ending in `0x00F0`, 2,880 x 40-byte zero DMA transfers | RGB image, 240 row transactions, Y=40..279, task yields |
| Display-on `0x29` | at `0x02021704`, after zero fill | after entire first RGB frame succeeds |
| Subsequent transfer servicing | enables SPI IRQ and installs RAM handler `0x01C034BC` | polling worker |

The first-fill stock window differs from its own initial table (which sets
Y=40..279); report the actual bytes, do not silently normalize them.
T1's timer/delay, clock guard or SPI/frame failure can prevent `0x29` from
ever being sent. The static early-clock code contains a possible 480 MHz
reporting branch, but the board's actual branch/clocks were not measured;
there is no evidence yet that the 396 MHz guard is the observed failure.

No dedicated LCD reset/backlight output has been established from this trace.
PH8 setup in the surrounding stock code is input/pull-up, not proof of an LCD
reset output. PMU register differences are not proof of a particular LCD rail.

## Conclusion and remaining localization

The stored bootloader, intended load address, XIP mapping and application entry
are retained. The application is **not a stock-equivalent startup**: boot-info
handling and board-power initialization differ, as do LCD timing/transport.
These are concrete inspection targets, not a proven root cause.

Next diagnostic design should distinguish reaching application entry, finishing
RAM/cache setup, reaching the app task, and reaching first SPI/display-on, while
keeping integrity handling intact. No further flash is justified by readback
alone. Finish dependency tracing for stock board power and an observable
milestone mechanism before choosing a narrowly scoped next test.

Reproduce address windows with `tools/jieli-wl82/trace_stock_calls.py` (010;
`--boot` or `--ram` as appropriate) and `inspect_fm1_listing.py t1` (T1).
These viewers pin application hashes; their linear sweep does not establish
reachability of arbitrary data or indirect targets.
