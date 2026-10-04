# NES pre-OS display checkpoint, 2026-09-28

## Accepted control and purpose

The user confirmed that the stock V14 marker boots normally, with a white
screen before the normal UI, then manually re-entered UBOOT and reconnected.
That proves the minimally modified stock application/container and its first
LCD fill execute on this unit. It does not establish the NES startup path.

This experiment moves the existing NES power/display checkpoint before OS
initialization, CPU1 release, timer tasks and filesystem initialization. It
does not claim a root-cause fix. The previous checkpoint was only before the
NES worker, not before those startup dependencies.

## Reviewed prerequisite closure

The existing compiled power routine is still checked by `audit_power.py`:
1,082 bytes, all call/pointer relocations and remaining instructions validated.
The same stock-derived rail selectors and config=0 parameter block are used.

- Startup has already set stacks, copied RAM, cleared BSS, configured cache
  and run the SDK memory hook before main.
- Main has disabled interrupts and initialized watchdog, clocks, interrupt
  routing, debug, CRC mutex and P33 I/O latch release before the checkpoint.
- `power_init` zeroes its 492-byte state. Config at byte +93 remains zero in
  the reviewed configuration. Its bit-1 allocation/registration branch and
  bit-2/bit-4 OS timer branch are skipped. No new scheduler or timer dependency
  is introduced before OS initialization on this reviewed path.
- Executed helpers use memory/arithmetic, reported clocks, P33 transactions
  and interrupt-vector registration. `__hw_lrc_start` configures hardware;
  `__tcnt_us` converts units arithmetically. They do not wait for OS ticks.
- Main's outer interrupt-disable nesting remains in force through the
  balanced P33 lock/unlock calls. Interrupt handlers are registered but the
  wrapper does not explicitly enable interrupts or launch CPU1.
- P33 lock/transaction waits can still stall in hardware; this experiment
  does not prove that they complete. It also does not prove panel power/reset.
- The existing display routine uses bounded SPI polling, finite NOP delays
  and watchdog feeding, with no OS delay/tick dependency in this profile.

## Exact integration and linked verification

`boot/pre_os_display.c` calls, in order:
`fm1_board_power_init -> fm1_display_test_init -> original os_init`.
Display failure does not skip original OS initialization or subsequent checks.
The later board power call remains idempotent, so it does not repeat the rail
sequence. A later call to the already-enabled diagnostic display returns
without reinitializing it; the existing worker/peripheral path remains.

An initial link showed that `--wrap=os_init` alone did NOT redirect main under
LTO. That artifact was rejected before packaging/flashing. The build now makes
an isolated copy of the pinned SDK `apps/common/system/init.c`, adds one extern
declaration and replaces its single main `os_init()` call with the wrapper.
All other SDK source text is checked identical; the SDK checkout is unchanged.

Reviewed final machine code:

| Location | Meaning |
|---|---|
| `0200171C` | Main; full 312-byte body is pinned by the additional audit |
| `0200182C` | Existing P33 I/O latch release |
| `02001830` | Direct call to new checkpoint wrapper |
| `02001686..02001696` | Exact 16-byte power/display/original-OS wrapper |
| `020013F0` | Existing idempotent board power gateway |
| `020015D0` | Existing timer-independent display initialization |
| `020297C2` | Original SDK os_init, retained |
| `02001844` | Original os_start continuation, after the checkpoint |

The CPU1 SYS_DIV guard is retained. The adapter's two-core scheduler is not
changed. Boot ABI bridge, integrity checks, filesystem sequence, scanner,
audio and SMB1 remain; this is not a USB serial implementation.

## Artifacts and tests

Build: `firmware/nes/build/boot-pre-os-smb1/`.
Application 181,136 bytes, SHA-256
`0b08d4508208b971f332c951147b5ddaed028979908ddd83dce160992be48d24`.
Full image 1,048,576 bytes, SHA-256
`8da9ba09455e10f9cd04666df10a32d7925aaf21efee9f6640631f39dba0d5ca`.
Image under `private-backups/nes-candidates/smb1/screen-first/` in its hash folder.

- 16 host CTest groups pass, including pre-OS call order and continuation on
  display failure; 26 compiled boot-audit tests and 3 stock-power tests pass.
- Four pre-OS tests pass normally and with `-O`: exact placement, every byte
  mutation of main/wrapper rejected, isolated SDK copy scope, retained CPU1 guard.
- Five fake-device deployment tests pass normally and with `-O`: forward and
  marker restoration, no pre-write reads, fail-stop, wrong-image rejection,
  exact sector scope and existing writer delegation.
- SDK and peak-nes checkouts remain clean. Build/package logs are
  `tmp/pre-os-build.log` and `tmp/pre-os-package.log`.

These are static/host/mock results, not a successful physical NES boot.

## Device workflow and acceptance

User authorized proceeding and reported UBOOT after the successful stock test.
No fresh pre-write firmware read is requested or performed. The expected source
is the previously verified stock-marker image, using its saved `final-1.bin`.
This is an explicit baseline assumption after normal stock execution, not a
claim that mutable stock storage was re-read. Only the approved application
sectors are written; any final full-read mismatch must be investigated, not
silently repaired by overwriting unrelated storage.

`run-elevated-pre-os.ps1` validates the candidate offline, confirms the single
UBOOT disk and original WL82 helper hash, then invokes existing `jluboottool.py`
with `help` to upload/start the RAM helper and obtain its online-device identity.
This sends no flash-array read/erase/program command. No chip key is queried.
The selector then delegates all sector writes to the existing CLI with the
established 4000..30000 bounds. No new USB writer or expanded write scope.

45 sectors, directory last; erase/program verification, independent post-write
sector verification and two final full reads retained. No reset, retry or
automatic rollback. Deployment directory:
`private-backups/pre-os-write-20260928-025138/`.
**Complete:** `deployment.json` reports `written_and_twice_readback_verified`.
All 45 sectors verified; both full 1,048,576-byte readbacks match SHA-256
`8da9ba09455e10f9cd04666df10a32d7925aaf21efee9f6640631f39dba0d5ca`.
There are no preflight/before-sector read files for this run. No reset sent.
Subsequent physical result: user reports **still blank screen**, then manually
re-entered UBOOT and reconnected. Visible-output acceptance failed. The successful
readbacks do not prove this checkpoint executed; OS/CPU1 startup is not required
by its intended call path, but earlier startup, power and LCD transport remain
unlocalized. Manual UBOOT re-entry is not evidence of automatic boot fallback.

Normal boot observation:

- RGB bands with a yellow top strip: execution reached display initialization
  before OS startup. If it stops there, investigate the subsequent OS/CPU1/FS path.
- Later blue checkpoint and SMB1: progress into the original peripheral/worker
  path; gameplay/audio/control acceptance still requires bench observation.
- Still completely dark: scheduler/filesystem startup is no longer required
  for the first picture, so investigate earlier boot/clock/power and the LCD path.
  This does not by itself prove which earlier stage fails.

The successful stock-marker image is the rollback target; `--restore-marker`
in the selector accepts only the exact complete pre-OS candidate as its source.
Do not use it against an unknown or partial flash state.
