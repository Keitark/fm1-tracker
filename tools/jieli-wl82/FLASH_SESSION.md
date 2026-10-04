# FM-1 once-elevated flashing session

Status (2026-09-29): the explicit retry extension has now completed a physical
same-image retry with52 sector checks, one full readback, reset and successful
serial observation. No device operation was issued while implementing it;
the subsequent user-requested deployment is recorded below.
The original elevated attempt exited before readiness;
its runtime permission check also rejects the existing installation. Hardened
mode now checks this before asking for UAC.

## Runtime modes

The user explicitly approved **trusted-local mode** for this personal bench.
Start with `-TrustedLocal`: the same-account elevated helper reuses
`C:\Python311\python.exe` and normal installed/user-site packages in place.
It preserves the launching process's PATH and selected Python configuration
variables, omits `-I -S`, and does not clone dependencies, change Python ACLs,
install anything, or create a venv. PowerShell itself still uses `-NoProfile`.
The trust choice is fixed in the protected session descriptor at launch; IPC
clients cannot enable it per request. Writer code is still snapshotted, and
identity/sector/verification/failure guards are unchanged.

This deliberately trusts user-writable interpreter, Python environment,
site initialization hooks and dependencies to execute as administrator. It is
not a hardened security boundary against other software under your account.
Close the broker when finished. `environment` reports the active interpreter,
package locations, elevation and Python isolation flags without device access.

Without `-TrustedLocal`, the following stricter runtime gate remains:

`C:\Python311\python.exe`, `Lib`, and `DLLs` have write/modify ACEs for
Authenticated Users (`S-1-5-11`), even though the installation root is protected.
Running client-triggered elevated Python jobs from those locations would allow
replacement code to execute as administrator. The launcher refuses this.

Permission repair (an alternative to trusted-local mode) needs separate approval because it
changes the shared Python installation, and should preserve a rollback ACL
export and normal-user read/execute access. Recursive child ACLs/owners must
also be audited, not merely the root directory. This setup neither changes
those permissions nor installs/downloads Python or packages.

At the user's request the per-build cutoff was removed on2026-09-29. Writes
can cover the complete stock V14 application allocation, `0x4120..0x92e1b`,
plus its two directory records. The broker and deployment code require every
boot/config/reserved byte outside those spans to match the verified baseline,
including configuration bytes sharing the final erase sector `0x92000`.
Build size is still checked by the packager against the actual stock slot.
All20 broker/writer tests pass; future growth within this slot needs no cutoff
edit or helper replacement. Older protected snapshots need replacement once.

## Design

- Build/edit/package validation run normally, without elevation.
- `start-flash-session.ps1` freezes a hash manifest before UAC. The elevated
  bootstrap validates those exact bytes, creates a unique protected directory
  under `C:\Program Files\FM1FlashSession-<id>`, and copies the existing writer,
  deployment orchestrator, WL82 helper and broker code. Hardened mode also copies
  installed dependencies; trusted-local mode uses existing packages in place.
- Normal user has read/execute access to the session snapshot/logs; only
  Administrators and SYSTEM can alter them. In hardened mode, no privileged
  import is made from the workspace or user's site-packages: Python uses
  `-I -S` and explicit snapshot module paths. Trusted-local uses normal Python
  initialization and the original account's package environment instead.
  PowerShell uses system module paths in both modes.
- One UAC approval starts a session-only PowerShell server. Its child processes
  inherit elevation. There is no Windows service, scheduled task, autostart,
  network listener, UAC setting change, venv, pip operation, or new flash writer.
- A random local named pipe permits the launching Windows user, Administrators,
  and SYSTEM, denies network clients, and the client verifies its server PID
  against the protected descriptor. Do not treat this as isolation from other
  software running under the same user: that user is authorized to request
  scoped FM-1 firmware writes for the lifetime of the session.
- Requests are bounded JSON with exact schemas. They carry firmware bytes and
  hashes, never client-selected executable names, commands, device paths, output
  paths, or source-code paths. No arbitrary shell endpoint exists.
- Only one request runs at a time. At the user's request on 2026-09-29, there
  is no idle expiration: the helper remains until explicit `quit`, process
  termination, or Windows shutdown. Per-request I/O deadlines remain. There is
  still no autostart or persistence across reboot. Disconnecting the client does not cancel or retry a
  flash already in progress. Read protected `state.json`/logs if uncertain.
- The server's operation state is marked blocked on disk BEFORE hardware I/O.
  Process termination, timeout, failed write or verification leaves a failure
  latch. No automatic write retry, erase recovery, rollback, or reset follows.

## Workflow

**Latest status,2026-09-29 19:01: profiling firmware deployed successfully.**
Same active helper/PID20664 below, no new UAC.53 sectors verified and one full
readback matched `2c8f0dc94ceb72dd12f7e151766b06c8868c2b3242a937bc2c0a1181b1d4b763`.
Current protected baseline is `runs/5a775ab3eff748798f761379bdcee91a/final-1.bin`.
Single reset followed by two successful COM10 observations; state idle/unblocked,
reset_pending=false, needs_observation=false. Durable copy and receipt under
`private-backups/nes-profile-write-20260929-190135/`. Profiling results and exact
scope/limitations: `firmware/usb-diag/NES_PROFILE.md`. Use normal `-Flash` next,
not `-Retry`; older baseline hashes below are historical.

**Historical status,2026-09-29 18:25: retry succeeded.** Active helper:
`C:\Program Files\FM1FlashSession-3edce9be5079421c8d72bca4118957af`, PID20664,
trusted-local, no idle expiration. UAC accepted; the actual failed-write evidence
was migrated without clearing its latch. Explicit `-Flash -Retry` rewrote all52
sectors and a single full read matched4e84e7ef...; baseline promoted to
`runs/ad071d8b4d6045628356bfcc884067df/final-1.bin`.
Reset was sent once; the known Windows log-decoding error was resolved by COM10
HELLO/advancing frames/zero fault. State idle/unblocked, no reset or observation
pending. Future firmware changes use normal `-Flash`, not `-Retry`.
Durable receipt/readback/logs: `private-backups/nes-render-skip-retry-20260929-182551/`.
Physical LCD/audio acceptance remains separate; serial audio underruns persist.
The old helper was not stopped and must not be reused for device operations.

**Historical status,2026-09-29 17:44: helper alive but BLOCKED after a partial
first-sector write.** The subsequent render-skip candidate4e84e7ef... failed
with WinError31 while programming sector0x5000. Do not normally boot or reset.
The stored9097a074... image is the last fully verified baseline, NOT a verified
description of current NOR contents. No baseline promotion or reset followed.
See `firmware/usb-diag/NES_RENDER_SKIP.md` and
`private-backups/nes-render-skip-write-20260929-174442/` for recovery evidence.
The older `reset_resolved_by_observation` field describes the preceding reset;
it does not resolve this newer flash failure. Do not blindly restart the helper
against the old baseline or clear the failure latch.

### Explicit same-image retry (new)

`retry_flash` now permits one deliberate retry after a completed failed flash.
It requires the exact failed candidate hash, baseline hash, original sector
list, and recorded PNP identity. It rewrites the **whole original sector list**,
including previously verified and partially written sectors; directory remains
last. It uses the unchanged `jluboottool.py` writer, sector checks and **one**
complete final readback, without a preliminary device dump. The saved baseline
is a reconstruction source, not a claim that partially written NOR matches it.

Each attempt has a new log directory and `attempt.json`; `retry_of` links the
preceding attempt. Existing failure logs/receipts are retained. Another failure
stays blocked, with no automatic loop or reset. Only successful full verification
promotes the baseline and permits reset. Different candidate bytes, missing
target metadata, unresolved process termination (`pending`), and reset failures
cannot use this operation. A replug may change the physical-drive number, but
the recorded PNP identity must match; this is a Windows identity guard, not a
cryptographic unique-unit identity. Use the same FM-1 on this bench.

An already-running old protected snapshot cannot load edited workspace code.
For a legacy partial-write failure, load a new snapshot with one UAC approval
(the following migration was already completed; do not launch it again):

```powershell
.\tools\jieli-wl82\start-flash-session.ps1 -TrustedLocal -ResumeFailedSession 'C:\Program Files\FM1FlashSession-08b85861dc7d4f3aa8381adebe995a63'
```

This validates the old protected failure receipt/request and verified baseline,
copies the evidence into the hash-frozen snapshot, and starts **blocked**, bound
to candidate `4e84e7ef216f52f4f57395bbbf40f3d0063c7e0afa1a25cbda31e001612a11dc`.
It does not flash, clear the latch, stop the old helper, reset or touch the device.
Adding `-PrepareOnly` validates/prepares this migration without UAC or device I/O.
That offline migration was checked against the actual failed session.

Set `$fm1Session` to the **newly printed** root. With the device already in UBOOT,
one explicitly requested retry/reset/observe cycle is:

```powershell
.\tools\jieli-wl82\invoke-flash-cycle.ps1 -SessionRoot $fm1Session -Build .\firmware\usb-diag\build\nes-smb1-render-skip -Flash -Retry
```

For a write-only retry, use `flash-session-client.ps1 -Operation retry_flash`
with `-SessionRoot` and the previously validated `-RequestFile`. Reset remains
a separate operation. Firmware changes require a normal cycle after successful
recovery/reset/observation, not a changed-image retry. Further retries in this
new session do not need additional UAC. There is still no idle expiration.
Use only the new session for subsequent operations; the old helper is unchanged.

Validation: **29 broker/writer tests passed**, including real non-elevated named
pipe exchanges, partial writes, re-failure, unchanged failure history, full-scope
rewrite, final-read mismatch, changed-image/identity rejection, and legacy failure
migration. All three modified PowerShell entrypoints parse. Source rollback:
`private-backups/flash-retry-source-20260929-175945/`.

Historical session after the paced-scanner test on2026-09-29 (superseded):

```powershell
$fm1Session = 'C:\Program Files\FM1FlashSession-08b85861dc7d4f3aa8381adebe995a63'
```

PID69316, UAC accepted, trusted-local mode, `idle_timeout_seconds=0`.
Leave this process running until the user requests stop. No idle expiry.
Single full-readback verification is active; all21 current broker/writer tests
passed. One physical flash/reset/observe cycle completed, followed by the partial
render-skip failure above; the session is now blocked.
Current protected baseline: `runs/9b16165222164b83b54953f93894a95c/final-1.bin`.
Current full SHA256:
`9097a0746af9a53135d047f8b5a21bc46b62e9d452ce82da4d31b3466eb8632e`.
Durable copy and successful one-read receipt are under
`private-backups/nes-paced-1ms-write-20260929-1729/runs/9b16165222164b83b54953f93894a95c/`.
Use that baseline/hash explicitly if restarting later, not either older default.
Details and the observed FPS regression: `firmware/usb-diag/NES_PACED_SCAN.md`.

### Historical session (superseded)

Previous verified session on 2026-09-29 (now stopped after its old idle timeout):

```powershell
$fm1Session = 'C:\Program Files\FM1FlashSession-dd4b8d8c469245d3ba86df4901280ba7'
```

Server PID `69584`, trusted-local mode. The former session `58d71e38...` was
closed to replace its per-build scope with the full stock app allocation.
This current session completed the first physical flash/reset/observe cycle:
52 sectors programmed, two full readbacks matched channel-FX `a1b6df96...`,
and COM10 returned with advancing NES frames and zero reported fault.
Its current baseline is `runs/5757c1f250d7478d8ae40e4f3ef37926/final-2.bin`.
State returned idle/unblocked. Check `status` before reuse; do not assume this
saved PID/session remains alive. New snapshots have no idle timeout.

The reset transfer reported WinError121 as the device restarted. The log also
contained CP932 text, which the worker attempted to read as UTF-8, causing a
host-side decoding error. No reset was repeated. The observation operation
resolved the ambiguous reset by verifying CDC HELLO plus advancing NES frames.
Known diagnostic defects remain in this snapshot: CLI `do_reset` shadows its
global args with command arguments, and the worker assumes UTF-8 CLI output.
Do not interpret the CLI's zero exit status as proof of reset/boot; the serial
observation gate is essential. These are not flash readback errors.

Local receipts/readbacks and both serial captures are preserved under
`private-backups/nes-channel-fx-write-20260929-161548/`. For a later new session,
use its `flash/final-2.bin` and full hash
`a1b6df96fc5c9d444f39a0e9f7ecd7213d6a49e4cfaac465b484d08ee7822272`.
The original `c60b423b...` default is now obsolete for this device; always supply
the actual current baseline when restarting.

Start once from the existing PowerShell environment:

```powershell
.\tools\jieli-wl82\start-flash-session.ps1 -TrustedLocal
```

It prints the protected session root. Set `$fm1Session` to that **actual** path.
By default it seeds the saved twice-verified `c60b423b...` image. If the device
has been flashed since, pass `-Baseline <final-2.bin> -BaselineSha256 <hash>`
from the actual most recent verified deployment. Receipt/hash/size are checked.
Do not restart against an obsolete baseline after an interrupted write.

Build with the existing firmware build command. Inspect an offline plan:

```powershell
.\tools\jieli-wl82\invoke-flash-cycle.ps1 -SessionRoot $fm1Session -Build .\firmware\usb-diag\build\<profile>
```

This verifies all manifest source/archive/overlay hashes, checks app hash,
regenerates the stock-layout package using the existing packager, and asks the
broker to check hashes and exact changed-sector scope. No device I/O in a plan.
`nes-smb1-channel-fx` is covered by the stock application allocation in new sessions.

For one explicitly requested physical iteration, add `-Flash`:

```powershell
.\tools\jieli-wl82\invoke-flash-cycle.ps1 -SessionRoot $fm1Session -Build .\firmware\usb-diag\build\<reviewed-profile> -Flash
```

Sequence:

1. Validate and plan before touching the device.
2. If no UBOOT disk exists, find the unique FM-1 CDC VID/PID, verify HELLO,
   send `TEST STOP`, confirm idle, then the existing armed UBOOT handshake.
3. Rediscover the unique WL82 disk (never assume PHYSICALDRIVE3). Upload the
   existing pinned WL82 RAM helper once using the existing CLI's strict mode.
4. Existing writer: application sectors only, directory sector last, erase
   check, 256-byte programming, sector readback and one complete final read.
   Reuse the saved verified baseline; no preliminary full device dump.
5. Promote `final-1.bin` as the protected baseline only after its full contents
   match the candidate. One full read replaces two at the user's request on
   2026-09-29; old two-readback receipts remain valid for baseline seeding.
6. Send existing CLI `reset 1` once, under strict identity and reset-only rules.
   USB disappearance is ambiguous, not proof of success; do not resend.
7. Observe CDC for a bounded interval: HELLO, at least two increasing NES frame
   counters, and zero reported faults are required. A successful observation
   may resolve an ambiguous reset disconnect, never a failed flash.
8. Inspect serial telemetry and physical LCD/audio/input before the next code
   change. No autonomous code generator or endless improvement loop is installed.

USB serial verifies liveness/frame progress, **not** correct LCD pixels, sound
quality, held-key behavior, or underrun-free real-time performance. Those remain
bench checks. If the loader reset cannot boot this hardware, stop and power-cycle;
the scheme cannot guarantee recovery without physical intervention.

Status, receive/status observation, and stop:

```powershell
.\tools\jieli-wl82\flash-session-client.ps1 -SessionRoot $fm1Session -Operation status
.\tools\jieli-wl82\flash-session-client.ps1 -SessionRoot $fm1Session -Operation environment
.\tools\jieli-wl82\flash-session-client.ps1 -SessionRoot $fm1Session -Operation observe
.\tools\jieli-wl82\flash-session-client.ps1 -SessionRoot $fm1Session -Operation quit
```

`observe` opens CDC and sends HELLO/TEST STATUS; it is not electrically passive.
Stopping retains the protected snapshot, request records, receipts and readbacks
for recovery/audit. Nothing is silently deleted. Changing writer/broker code
requires a new reviewed snapshot and one new UAC approval; changing firmware
within the established scope does not.

## Validation

```powershell
Push-Location .\tools\jieli-wl82
C:\Python311\python.exe -m unittest test_flash_session test_v14_cli_deployment -v
Pop-Location
.\tools\jieli-wl82\start-flash-session.ps1 -PrepareOnly
```

Includes a real Windows named-pipe status/rejection/quit test without elevation,
simulated existing-writer flash/reset cycle, stale hashes, reserved sectors,
unknown operations/fields, failure latching and strict loader/reset CLI grammar.
Tests never open actual USB, disk or serial devices. Existing writer scope and
its regression suite remain unchanged.

Historical initial result: **20 tests passed** (12 broker tests plus 8 existing-writer
regressions), all PowerShell entrypoints parsed, current channel-FX package
regenerated to its existing exact hash and accepted within the reviewed sector
boundary (with the unused tail preserved). Tests include real local named-pipe exchanges in both runtime modes;
trusted mode imports the existing user-site packages without Python isolation.
The hardened runtime check was exercised and rejected before UAC as expected.
Source rollback for the pre-existing CLI is in
`private-backups/flash-session-source-20260929-152837/`.
