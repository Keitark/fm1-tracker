# FM-1 screen-first SMB1 experiment, 2026-09-28

**Write completed:** all 45 application sectors verified; two complete
1,048,576-byte final readbacks match the candidate exactly. Receipt status is
`written_and_twice_readback_verified`. No reset was sent by the writer.
**Subsequent user result: "no screen yet". Visible-output acceptance failed.**
This report does not distinguish completely dark from lit-black, or establish
whether the early checkpoint executed. Last verified flash contents are the
screen-first candidate, not the preceding stock-power candidate.

Follow-up read-only Windows enumeration still reports WL82 UBOOT1.00. The user
confirmed: "I put it back into UBOOT after testing". Thus a normal-boot test was
attempted and the current USB enumeration is deliberate recovery-mode entry,
not evidence of automatic fallback. No new helper upload, flash operation or
reset was performed during this follow-up.

The actual early-display call is at `02001B7C`, after `os_start` dispatch,
system timer initialization, early/platform init and board `power_init`.
The first picture removes explicit timer waits and scanner/audio gates, **not
the dependency on successfully starting the OS and reaching board_init**.
The failure therefore does not identify LCD transport versus an earlier stop.
Live RAM breadcrumbs exist at `01C0287C` (boot trace), `01C03470` (power stage),
`01C03484` (display stage), and `01C03488` (display error). Re-entering UBOOT or
uploading its helper may destroy them; stale RAM must not be treated as a
live execution trace. Do not reflash this same failed image as a diagnosis.

Further offline investigation found a stock-versus-NES CPU1/scheduler startup
difference, including a stock SYS_DIV guard absent from our linked launch path.
It also verified cache-routine parity and the scheduler's interrupt-enable path.
See [the follow-up investigation](FM1_PRE_DISPLAY_STARTUP_DIFFERENCES_20260928.md).
The CPU1 difference is a hypothesis, not a diagnosed hardware failure.

## Purpose and limits

The user authorized completion of stock-first analysis, integration and flashing,
and confirmed power-cycle/UBOOT readiness. The stock review and its remaining
uncertainties are in `FM1_STOCK_EXECUTION_AUDIT_20260928.md`, final section.
This is a recoverable diagnostic, not a claim that the dark-screen cause is
proved or that the hardware boot/display test has passed.

The completed integration uses the preserved early display source: reviewed
stock-derived board power, then RGB bands with a yellow top strip, then normal
SDK initialization and the SMB1 worker. The later NES LCD checkpoint is blue.
The first picture has no clock-report guard, timer reads, RTOS delay, keyboard
scanner or audio dependency. Startup, board power and physical panel wiring
remain dependencies. No guessed panel reset/backlight pin or voltage is added.
SDK integrity initialization and its 8000 ms callback remain unchanged.

The linked disassembly was inspected through `fm1_early_delay_chunk`,
`panel_delay`, `send`, `command`, `fm1_display_test_frame`, and
`fm1_display_test_init`: 480,000 explicit nops per delay chunk; watchdog feed
between chunks; 1,000,000-iteration SPI polling bound; only PC7..PC10 and SPI1
configuration; setup before the initial wait; sleep-out followed by delay;
initial picture before display-on. This does not calibrate physical timing.

## Artifacts and checks

- Build: `firmware/nes/build/boot-screen-first-reviewed-smb1/`
- Application: 181,136 bytes, SHA-256
  `eda772a719fe94600023d8d709ebbe972aa165386131036b59310769201c4a88`.
- Full 1 MiB image:
  `private-backups/nes-candidates/smb1/screen-first/198140cbf6cb4d052ecc9fbb610ad0f9619acadb65888adee1e172827fc87e95/fm1-nes-stock-layout.UNQUALIFIED.bin`.
- Full image SHA-256:
  `198140cbf6cb4d052ecc9fbb610ad0f9619acadb65888adee1e172827fc87e95`.
- Build log: `tmp/screen-first-reviewed-build.log`.
- Package log: `tmp/screen-first-package.log`.
- 15 host CTest groups, 26 ELF mutation/audit tests, 3 stock-power tests passed.
- 5 screen-first package/deployment tests passed normally and with Python `-O`:
  exact forward/restore, preflight/sector/transport/final mismatch stops,
  existing CLI invocation, prohibited scopes, and artifact hash rejection.
- 2 existing CLI writer tests passed. Stock panel table transcription passes.
- Package CRCs/round-trip, stock UBOOT/configuration and reserved-byte
  preservation pass. The general build/package `flashable: false` flag remains
  because these are hardware-unqualified candidates. This explicitly authorized,
  hash-pinned bench write is recorded separately; it is not a release flag.

## Device operations

Fresh enumeration selected exactly one `WL82 UBOOT1.00 USB Device` at
`\\.\PHYSICALDRIVE3`. Disk numbering is not a permanent identity.

Cold read: `private-backups/pre-screen-first-20260928-015100/`.
The existing `jluboottool.py` uploaded the hash-pinned original WL82 helper to
RAM and read all 1,048,576 bytes successfully. SHA-256 exactly matches the last
written stock-power image:
`e1fc44a067d8bd97931ab216f7550b325f320ea1ee4f0fac86644b5b0b0b6ead`.
No erase/program command was used for that read. No chip-key query was made.

Write run: `private-backups/screen-first-write-20260928-015134/`.
The new `deploy_screen_first_via_cli.py` is an artifact-selection/pinning
wrapper. It delegates to the existing `deploy_v14_smb1_via_cli.py` orchestration
and existing `jluboottool.py` writer, without a new transport or flash algorithm.
The existing `--fm1-stock-power` scope is sufficient: 45 sectors, `0x5000`
through `0x30000`, followed by directory sector `0x4000`. No scope expansion.

Two full source reads are required before any write. Each sector has a preimage
check, erase check, program check, and independent readback; two complete 1 MiB
reads follow. The receipt `deployment.json` is the authoritative live status.
No automatic reset, retry, whole-chip erase, or KALLSUP image/helper is used.

## Rollback and physical acceptance

Working V14 rollback remains hash-pinned at
`private-backups/post-official-update-20260927-230806/pass-01/fm1-direct-1MiB.bin`,
SHA-256 `bcf61795e2ddb201f2f72180ec8f3dca830c84a7c64d23d08475412dbcfe2f9c`.
The wrapper's `--restore-v14` path has been tested with a fake device only;
actual restore requires a fresh full read matching this new candidate. Per-sector
preimages of the preceding stock-power image are also kept in the write folder.

After successful full readback, normal power-cycle testing is needed. Observe
whether RGB/yellow appears, whether blue follows, whether SMB1 appears, and
whether the result stays stable for at least 20 seconds. A very brief checkpoint
may be missed; absence by eye does not prove that checkpoint never executed.
Keep speakers/headphones disconnected for this first test. Completely dark,
lit-black, retained color bars, blue-only, and gameplay are distinct outcomes.
