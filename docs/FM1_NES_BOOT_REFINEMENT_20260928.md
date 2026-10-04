# Stock 010 versus NES: boot refinement, 2026-09-28

Result: a separately linked NES/SMB1 candidate, **not flashed or hardware
qualified**. The known-working stock baseline remains FM-1_010. This follows
the T1 analysis, but compares the actual NES executable rather than treating
T1's implementation as identical to NES.

## Inputs and exact boot differences

Stock application SHA-256:
`fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
Original NES application SHA-256:
`a3e4f81914cceab7971d955832fa7b2821f3597a9dc27ac81a1cc2196e23216a`.
Both original NES and T1 build-folder application hashes were rechecked and
are unchanged. See `FM1_010_VS_T1_BOOT_TRACE.md` for boot-bank loading evidence.

The retained flash boot bank loads at RAM `0x01C02000`. It maps physical flash
`0x4000` to CPU `0x02000000`, then calls entry `0x02000120` (physical flash
`0x4120`) with r0 pointing to boot arguments at `0x01C7FE08`. The main application
executes from mapped flash, not as a whole-image RAM load. No bootloader changes
are part of this refinement.

| Operation | Stock 010 | Original NES | Refined NES |
|---|---|---|---|
| First branch target | `0x02000124` | `0x0200018A` | unchanged |
| Initial SP | `0x01C14B94` | `0x01C171B8` | `0x01C171C8` |
| Initial SSP | `0x01C15B94` | `0x01C181B8` | `0x01C181C8` |
| Boot-info initializer | `0x020001E6` | bridge `0x02001FE2` then SDK `0x02000292` | bridge relocated to `0x02002010`; same reviewed instructions |
| BSS clear start/length | `0x01C09E5C` / `0x17380` | `0x01C016A0` / `0x17EF0` | same start / `0x17F00` |
| RAM copy source | XIP `0x020848E0` | XIP `0x02027700` | XIP `0x020277A0` |
| RAM copy destination/length | `0x01C00000` / `0x9E5C` | `0x01C00000` / `0x16A0` | unchanged |
| Cache setup call | `0x01C00E88` | `0x01C00BAE` | unchanged |
| Memory hook | `0x02058C54`, **rts only** | `0x020245D0`, **rts only** | wrapper `0x02002046` calls original rts hook at `0x0202466C` |
| Main | `0x02001C24` | `0x02000C72` | unchanged |

The NES startup clears BSS, copies initialized RAM, performs zero-sized external
RAM operations, configures cache, calls the memory hook/update-result routine,
and enters main in the same broad order as stock. Stack/section relocation is
normal for this link. The extra 16 bytes of BSS are the diagnostic record.
The label `memory_init` must not be mistaken for a missing allocator setup:
its actual body is an empty return in stock and the original NES image too.

The existing 24-byte-prefix boot-info bridge remains: it preserves common
stock fields, zeroes SDK-only extensions, and calls the unchanged SDK initializer.
The SDK's internal boot-info address differs from stock; blindly relocating it
would not be a justified fix. Physical handoff compatibility is still unproven.

## Corrections/refinements made

1. **Reported-clock guard:** the previous NES and display guard rejected
   `sys > 396 MHz`. Both stock 010 at `0x02001D32` and original NES at
   `0x0200099E` have an early-clock branch loading `480000000`. A shared contract
   now permits reported sys values 24..480 MHz, retaining lsb `(0,80] MHz`.
   This changes validation only: no PLL, rail, divider or clock register is
   programmed. It does not prove the board took that branch or that every
   accepted value is electrically qualified.
2. **Finite pre-display waits:** the NES service delay now returns an error on
   a stopped millisecond counter. SPI polling also has an iteration bound when
   both the completion bit and time source stall. Clock/timer prepare failures
   remain in `fm1_sdk_diag.fault`. Successful timing/transport behavior is unchanged.
3. **Post-cache RAM trace:** a wrapper records cache-ready, calls the original
   memory hook, and records hook-returned. Board/app/worker markers follow.
   No pre-BSS globals, invented UART/GPIO, allocation, timer or flash access is
   introduced. SDK event dispatch and integrity callbacks are retained.
4. **Actual instruction audit:** all 154 bytes of the SDK's `chip_entry` block
   are checked against reviewed instructions with linker-resolved operands:
   stacks, BSS clear, RAM copy, zero external sizes, calls and main destination.
   Entry branch, stack bounds and cache-copy operands/call are checked too.
   Previously these relationships were checked mainly through symbols, which
   could miss the same instruction corruption in both ELF and flat image.
5. **Wrapper machine-code checks:** the trace wrapper and marker are checked
   instruction-for-instruction, including both marker calls and the original
   memory-hook call. The weak SDK hook needs an explicit linker undefined-symbol
   root to survive LTO wrapping; its original two-byte body is also checked.

## What this does NOT change

- Stock's board power/PMU sequence is still not reproduced. Board hooks now
  record progress only; they do not configure rails. The traced stock path
  includes oscillator, PMU, voltage and low-power state, not just an LCD pin.
  A partial raw register replay or the SDK demo board's voltage settings would
  be an unsupported replacement. This remains a substantive startup difference.
- NES **already** configures GPIO/SPI before the 100 ms wait; the reverse-order
  issue documented for T1 was not present in NES. NES still uses polled bytes
  rather than stock SPI DMA and uses configured display windows for its fill.
- No boot-ROM code, entry address, SDK security check, private configuration,
  bootloader, flash writer or recovery image is altered.
- No claim that the blank-screen cause is fixed. A live hardware checkpoint
  and stock-power dependency review are still needed.

## Refined candidate and trace interpretation

Folder: `firmware/nes/build/boot-refined-peripherals-smb1/`.
Application: **167,232 bytes** (160 bytes more than original NES), SHA-256
`4e04dd69fc9cc8ded32f249b7d82733e9edca2bc1092c760010cf0f4093eda00`.
The ELF, application, disassembly, symbols, static audit and build manifest are
in that folder. It is an application payload, **not a raw full-flash image**.
No new flash package was produced; existing deployment pins remain unchanged.

Trace address in this exact link: **`0x01C016AC`**, four 32-bit words:

| Offset | Meaning |
|---|---|
| +0 | magic `0x464D4254` |
| +4 | version 1 |
| +8 | visited mask, bit `1 << stage` |
| +12 | last stage |

Stages: 1 = cache setup returned; 2 = memory hook returned; 3 = app-task early
board hook (after OS/system-timer startup); 4 = board hook after early/platform
initcalls; 5 = app entry after remaining initcalls; 6 = NES worker entered.
All six gives mask `0x7E`. These are execution breadcrumbs, not integrity or
hardware-acceptance flags. Compiled marker calls were inspected in their actual
app-task/worker positions.

The record requires a **live debugger/halt/read path**, which has not been
established here. Power-cycling into UBOOT or uploading a RAM helper destroys
or repurposes application RAM; it cannot retrieve this as reliable post-mortem
evidence. No magic/zero record cannot distinguish reset, failure before the
wrapper, or RAM being overwritten. No checkpoint precedes the RAM/cache setup.

## Validation

- 13 host test targets passed, including clock boundaries, trace order,
  stopped timer/SPI failure paths, worker dispatch and existing NES contracts.
- 19 refined-image boot-audit tests passed normally and under Python `-O`.
  Negative tests mutate startup operands, entry, RAM-resident cache-copy
  operands, and the memory-hook call while keeping ELF/application consistent.
- Original NES passes the strengthened checks: 18 passed, one new-trace-only
  case skipped. This narrows suspicion; it does not establish physical execution.
- Stock board/table transcription checks pass; panel table remains identical.
- Original NES and T1 application hashes remain unchanged.

Build: `C:\Python311\python.exe firmware/nes/build_boot.py --profile peripherals
--rom smb1 --out firmware/nes/build/boot-refined-peripherals-smb1` (one line).
Logs: `tmp/build-boot-refined.log`, `tmp/refined-boot-audit-optimized.log`,
`tmp/original-nes-boot-audit.log`. Address viewer now accepts `nes` and
`nes-refined`, hash-pinned to these two applications.
