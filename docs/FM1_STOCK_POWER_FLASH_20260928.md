# Stock-power NES candidate: bench flash, 2026-09-28

Status: **written and twice full-readback verified, but failed visible-output
bench test**. The user reported "no screen shown" and clarified that the display
is completely dark (not a glowing backlight with a black picture). The power
integration did not produce visible output; the execution failure point is
unknown. The user requested flashing and confirmed a power cycle.
No automatic reset was sent; the original RAM helper was left running.

## Inputs and result

- Source: last-written display-only T1, SHA-256
  `1f56085b9aa6c9c9e1b852141f264cc465b9b929607628cfdabe4d770f769c4d`.
- New application: stock-power SMB1 build, 180,080 bytes, SHA-256
  `8e1a218eaba2abc7330abaf9c2da30c7f7e75fc729d4c8788bee5c1ed5356445`.
- New private 1 MiB package and both final readbacks: SHA-256
  `e1fc44a067d8bd97931ab216f7550b325f320ea1ee4f0fac86644b5b0b0b6ead`.
- Retained known-working V14 rollback: SHA-256
  `bcf61795e2ddb201f2f72180ec8f3dca830c84a7c64d23d08475412dbcfe2f9c`.
- Evidence: `private-backups/stock-power-write-20260928-003646/`, including
  `deployment.json`, `console.log`, sector preimages/readbacks and two final
  complete 1 MiB images. Independent byte comparisons and file hashes matched.
- Cold-helper read: `private-backups/pre-stock-power-20260928-003402/`; exactly
  matched T1. No read/transport errors reported during the completed run.

## Procedure and scope

Windows identified exactly one `WL82 UBOOT1.00`, physical drive 3. The original
hash-pinned WL82 helper was uploaded to RAM via existing `jluboottool.py` for
the cold read. The existing writer then reused it, with strict device/flash
identity checks and 256-byte I/O. No replacement USB protocol was introduced.

Two new full preflight reads matched T1 before any erase. Then 45 sectors were
written: `0x5000..0x30000`, followed by application-directory sector `0x4000`
last. Each sector's old contents, erased contents, programming and additional
readback were checked. Two complete final reads matched the new target.

Bootloader/header bytes below `0x4000`, settings/reserved data, non-app directory
entries and unused stock application tail were preserved. The target was built
from the retained V14 container, and its decoded application/CRCs were checked.
No whole-chip erase, chip-key request, automatic retry or rollback was issued.

## Tool changes and validation

- Stock packager accepts an explicit separate build folder; old build artifacts
  were not overwritten and historical profile defaults remain unchanged.
- New pinned `deploy_stock_power_via_cli.py` validates source/target/rollback
  hashes, current build/source/library closure, compiled power audit and exact
  changed-sector list before invoking the existing orchestrator.
- Existing CLI has an explicit `--fm1-stock-power` opt-in, requiring
  `--fm1-bench`, for the four extra application sectors `0x2d000..0x30000`.
  The old default limit remains `0x2c000`; unaligned/oversized/outside-range
  writes are rejected. Erase/program/read algorithms are unchanged.
- Five stock-power/shell tests, five prior CLI/rollback tests and four protocol
  tests passed normally and under Python `-O`, using fake devices only.
- Pinned wrapper supports explicit V14 restoration from this exact candidate,
  with the same preflight and verification requirements; it was not invoked.

Readback proves the programmed bytes, not successful execution, panel output,
rail voltages, audio behavior or recovery under power interruption. Original
helper status-register effects and lack of independent NOR-status polling
remain known limits. See `FM1_STOCK_POWER_INTEGRATION_20260928.md` for the
implementation evidence. Subsequent offline triage is recorded below.

## Blank-screen triage after the bench result

No additional device commands, code changes or flash attempts were made during
this triage. Review used the exact flashed build and working stock 010.

Confirmed code paths, not a diagnosis of the observed hardware failure:

- `src/fm1_wl82.c:154` runs `services.prepare` before any LCD setup.
  `boot/wl82_services.c:113..120` gates on reported clocks, checks timer progress,
  then starts SPI2 key scanning. Any failure returns before the LCD is touched.
- `src/fm1_wl82_keyscan.c:46..51` bounds a stalled SPI transfer only by elapsed
  timer time. Unlike the LCD wait, it has no independent iteration bound. A
  stopped clock together with stalled SPI can prevent reaching display setup.
  Existing scanner tests advance their fake clock and do not test that combined
  failure. This cannot alone explain old T1, which had no scanner.
- `src/fm1_board.c:154..160` fills black, then sends display-on. Audio setup
  (`iis_open`, sample rate and IRQ setup) must succeed before NES renders the
  first game image. There is no visible pre-audio success marker.
- Stock 010 sets SPI1 up before its 100 ms wait at `0x020230be`; the current
  full NES path also does setup before its `fm1_board_lcd_init` delay. The
  historical T1's different delay placement must not be misreported as a
  difference in the current full NES image.
- Rechecked stock `0x02022e90..0x0202315a`: PC7/8/9/10 SPI controls and the
  command sequence are established. PH8 is configured as input with pull-up,
  not an identified panel reset/backlight output. No evidence justifies
  driving an arbitrary GPIO to light the panel.
- Compiled `timer_get_ms` at `0x02000754` is `jiffies * 10`; it is dependent
  on running SDK tick machinery. Post-reset UBOOT/helper RAM is not a valid
  way to recover the prior execution breadcrumbs.

Next diagnostic should make a visible milestone before scanner/audio/NES and
minimize scheduler/timer dependencies while keeping SDK integrity behavior
intact. A dark outcome would still require an independent execution signal
(verified UART/debugger or a measured known SPI output), not another claim
that panel power or the CPU is necessarily at fault. No such new diagnostic
has been built or flashed in response to this report.
