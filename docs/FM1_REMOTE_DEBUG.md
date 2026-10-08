# FM-1 remote debug and private app store

The PC builds and reviews packages. The Windows laptop owns the FM-1 USB
connection and a protected local flashing session. A small unelevated bridge
exposes fixed operations over authenticated HTTP bound to `127.0.0.1:9770`.
Tailscale Serve supplies private HTTPS for the browser store. The existing
reviewed WL82 tools remain the USB writer; the bridge adds no arbitrary shell,
device path, erase, signing bypass, or network-selected executable endpoint.

Implementation, automated tests, inventory-only laptop deployment and
authenticated private HTTPS checks are complete. Protected USB read/write,
LCD/audio/key behavior and physical acceptance still require the live checklist
below. No remote bench acceptance is claimed.

```mermaid
flowchart LR
  PC[PC: build and review] -->|HTTPS over Tailscale + bearer token| BR[Laptop: loopback bridge]
  BR -->|local authenticated named pipe| BROKER[Laptop: protected session]
  BROKER -->|existing reviewed USB tools| FM[FM-1]
```

## Files and boundaries

| File | Purpose |
|---|---|
| `tools/jieli-wl82/remote_bridge.py` | Fixed HTTP protocol, durable jobs, process lock, uncertainty latch |
| `remote_backend.py` | Laptop-local adapter for protected session, status inventory, catalog, vendor GUI |
| `remote_catalog.py` / `export_remote_catalog.py` | Private validated app variants and audited offline export |
| `remote_store.html` | Browser app cards, planning, explicit write confirmation, saved job polling |
| `remote_client.py` | PC CLI, no automatic job resubmission, verified binary downloads |
| `start-remote-bridge.ps1` | Private laptop state/token preparation and hidden unelevated launch |
| `make-remote-kit.ps1` | Versioned private ZIP without credentials, jobs, or session descriptors |

Default laptop state is `%LOCALAPPDATA%\FM1RemoteBridge`. Its token, catalog,
requests, receipts and logs remain private. The launcher grants access only to
the current Windows user, SYSTEM and Administrators within that directory. It
does not change the shared Python installation or any global Windows ACL.
Keep the state directory away from symlinks/junctions and use a dedicated local
folder. There is no service, scheduled task, automatic elevation, or autostart.

## Prepare a laptop kit on the PC

Run from `F:\dev\fm1`. Use a selected idle protected session whose verified
baseline still belongs to the same physical FM-1 that will move to the laptop.
An old baseline is not evidence that a different or partly written device
currently matches it. Blocked/reset/observation-pending sessions cannot be
exported as a clean bootstrap.

```powershell
# Source-only kit: inventory/store setup without local USB dependencies.
.\tools\jieli-wl82\make-remote-kit.ps1

# Full private deployment kit, when the selected baseline is appropriate.
$selectedSession = 'C:\Program Files\FM1FlashSession-<32-hex-id>'
.\tools\jieli-wl82\make-remote-kit.ps1 -IncludeUsbTools `
    -SessionRoot $selectedSession -CatalogDir .\tmp\remote-catalog
```

Each invocation creates a new directory under `tmp\remote-kit` and leaves the
source copy, ZIP, SHA-256 sidecar and `remote-kit-manifest.json`. It never
overwrites or deletes previous kits. The manifest records every copied file's
size and hash. `-IncludeUsbTools` copies the explicitly selected local reviewed
dependency, its license and Python/YAML sources, plus only the reviewed
24,064-byte WL82 loader with SHA-256
`d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376`.
The upstream reference is [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool);
the FM-1 bench uses local reviewed extensions, so an arbitrary fresh upstream
checkout is not a substitute for the manifested tool copy.

`-SessionRoot` exports only the selected verified `baseline.bin` and matching
`baseline-deployment.json` into `private-bootstrap`. `-CatalogDir` exports only
validated package JSON into `private-catalog`. Neither option copies tokens,
pipe descriptors, PIDs, broker snapshots, job history or arbitrary backup
directories. Firmware/configuration bytes and packaged ROM/music assets make
this ZIP private. Keep kits and images out of the public source PR.

Copy the private ZIP through your existing trusted transfer mechanism, verify
its SHA-256 sidecar on the laptop, and extract to e.g. `C:\FM1\fm1-remote`.
The copied tooling is sufficient to operate already prepared packages. Build
and audited catalog export still run in the full PC producer checkout with its
normal private build inputs; the minimal laptop kit is not a complete SDK.

## Laptop setup

Use the existing `C:\Python311\python.exe` for protected device operations. The
protected broker hardcodes that interpreter even if the unelevated bridge uses
`-Python` explicitly. Inventory-only operation can use another Python 3.11 or
newer interpreter or dedicated virtual environment via `-Python`, with no
`-SessionRoot` configured.
The launcher only checks dependencies and never installs or repairs Python.
If packages are missing, deliberately install the supplied versions into the
intended existing interpreter:

```powershell
Set-Location C:\FM1\fm1-remote
C:\Python311\python.exe -m pip install -r .\tools\jieli-wl82\requirements-remote.txt
.\tools\jieli-wl82\start-remote-bridge.ps1 -PrepareOnly
```

`-PrepareOnly` creates the private state and a random token if absent, validates
dependencies and selected local paths, then exits. It starts no listener,
Tailscale publication, UAC prompt, helper, serial command, or firmware operation.
Existing tokens are retained and never printed. Use the laptop's token for this
laptop; the kit never imports a PC token.

Inventory and the empty store work without `-SessionRoot`. USB operations need
a fresh protected snapshot made locally on the laptop from this updated source.
Do not copy an old PC `session.json` or reuse its pipe descriptor. With the
correct physical device/baseline relationship established locally:

```powershell
$kitManifest = Get-Content .\remote-kit-manifest.json -Raw | ConvertFrom-Json
.\tools\jieli-wl82\start-flash-session.ps1 `
    -Baseline .\private-bootstrap\baseline.bin `
    -BaselineSha256 $kitManifest.baseline.sha256
```

That existing launcher freezes the worker/tool snapshot and asks for one local
UAC approval. It reports a new `C:\Program Files\FM1FlashSession-<id>` and
checks the local pipe without requesting a flash or reset. If hardened runtime
validation rejects the existing interpreter, no permissions are changed.
`-TrustedLocal` is an explicit personal-bench alternative: it permits existing
Python/site packages to run elevated and therefore trusts software writable by
your account. Decide that locally; do not change global Python ACLs merely to
make the remote bridge start. See [FLASH_SESSION.md](../tools/jieli-wl82/FLASH_SESSION.md).

Start the bridge as the same unelevated Windows user that owns the protected
session. Replace the placeholder with the new laptop session path:

```powershell
$laptopSession = 'C:\Program Files\FM1FlashSession-<new-laptop-id>'
.\tools\jieli-wl82\start-remote-bridge.ps1 -SessionRoot $laptopSession
```

The launcher records its PID, arguments and separate stdout/stderr log paths in
private state. It verifies an existing recorded process and authenticated
health response before reusing it. A port collision or unverifiable living PID
stops preparation of a duplicate; no existing process or listener is killed.
Omitting `-SessionRoot` on a later launch reuses the locally recorded selection.
Use an explicit empty `-SessionRoot ''` to choose inventory-only mode for a new
launch. Changing a running bridge's configuration requires local inspection
and a deliberate restart after any active job completes.

Both helpers stay running after the launching shell closes. A reboot stops
them; relaunch locally. The bridge OS lock prevents another service from owning
the same state directory. Preserve that state across restarts so uncertainty
and completed job receipts remain visible.

## Private HTTPS and the store

Use Tailscale Serve HTTPS for a remote browser. Browsers require a secure origin
for `crypto.randomUUID()` and verified SHA-256 downloads; raw HTTP on a tailnet
IP does not provide that browser context. Local `http://localhost:9770/` is
suitable for a loopback preview.

```powershell
.\tools\jieli-wl82\start-remote-bridge.ps1 `
    -SessionRoot $laptopSession -PublishTailscale
```

The explicit switch inspects `tailscale serve status --json`. It creates the
following route only when no meaningful Serve configuration is present, or
preserves an already matching HTTPS root route. Conflicting routes or an
existing Funnel configuration are refused for local review; no reset is run.

```powershell
tailscale serve --bg --https=443 http://127.0.0.1:9770
tailscale serve status
```

HTTPS and Serve permissions may need tailnet owner configuration. Restrict the
tailnet policy to the intended PC/user and laptop. Serve is private to the
tailnet; this workflow does not enable public Funnel. See the official
[Serve command reference](https://tailscale.com/docs/reference/tailscale-cli/serve)
and [Serve setup](https://tailscale.com/docs/features/tailscale-serve).

Open the HTTPS URL reported by Tailscale on the PC. Read the token file locally
on the laptop and enter it in the store's password field using your trusted
access method. The browser retains it only in page memory, never in a URL or
localStorage. Reloading requires the token again. Only the static page is
unauthenticated; every `/v1/` data/operation endpoint requires the bearer token.

Cards cover NES, Doom, MDX Karaoke, Buddha Machine and ProTracker. A card becomes
writable only when its private variant matches the selected device's preserved
boot/config/reserved bytes. `View plan` submits `plan_app` for offline review.
`Write app` asks for confirmation with the package hash and UBOOT entry method,
then submits one `switch_app` job: plan, optional known serial UBOOT handshake,
existing scoped flash/readback, one reset, observation and profile check.
It never guesses a retry. The device still needs separate LCD/audio/key checks.

ROM/song variants are separately audited packaged firmware builds. The store
does not accept an arbitrary runtime ROM and does not claim a boot menu or an
OTA writer. Build/export another validated variant on the PC, assign a new
immutable catalog ID, and upload the resulting private JSON bundle using the
store or CLI. For example, with an audited ProTracker build and selected PC
baseline:

```powershell
C:\Python311\python.exe tools/jieli-wl82/export_remote_catalog.py `
  --profile protracker --build firmware/protracker/build/<audited-build> `
  --session $selectedSession --out tmp/remote-catalog/tracker-variant.json `
  --id tracker-variant --title 'ProTracker' --description 'Audited tracker variant'
```

Export checks source/archive/overlay freshness and the existing producer's
candidate validation. Private music/ROM/SDK inputs remain build requirements.
An absent or unaudited app appears as package-needed; a title alone never makes
it writable. Buddha requires a build with its validated chant embedded as the
boot song, not simply a renamed tracker card.

## PC command-line operations

Privately transfer the laptop token file to a user-only PC location through
your existing trusted access mechanism. Do not put token contents in command
arguments, screenshots, logs, query strings, the kit, or Git. The CLI accepts
loopback and Tailscale origins and rejects redirects/proxy forwarding of the
credential.

```powershell
$bridgeOrigin = 'https://laptop-name.your-tailnet.ts.net'
$remoteTokenFile = "$env:LOCALAPPDATA\FM1RemoteClients\laptop.token"
$remoteArgs = @('--url', $bridgeOrigin, '--token-file', $remoteTokenFile)
$client = 'F:\dev\fm1\tools\jieli-wl82\remote_client.py'
C:\Python311\python.exe $client @remoteArgs status
C:\Python311\python.exe $client @remoteArgs catalog
C:\Python311\python.exe $client @remoteArgs upload .\tmp\remote-catalog\tracker-variant.json
C:\Python311\python.exe $client @remoteArgs job plan_app --catalog-id tracker-variant --wait
C:\Python311\python.exe $client @remoteArgs job serial_status --wait
```

For a firmware read, establish the device's loader state explicitly on the
laptop. `cold` uploads the reviewed helper from cold UBOOT; `reuse` requires a
known already running helper. The existing loader's initialization can change
NOR status/protection registers even though the read path does not program
array bytes. See [WL82_LOADER_AUDIT.md](../tools/jieli-wl82/WL82_LOADER_AUDIT.md).
The read checks the captured device identity and two complete matching 1 MiB
passes. It leaves the helper running and never resets automatically. Establish
cold UBOOT locally before a later write; do not infer that state from a download.

```powershell
C:\Python311\python.exe $client @remoteArgs job read_firmware --loader-state cold --wait
# Save the job ID reported before submission, then download that completed job.
$readJob = '<saved-32-hex-job-id>'
C:\Python311\python.exe $client @remoteArgs download --id $readJob --out .\tmp\laptop-read.bin
# Download the already verified stored session baseline without device I/O.
C:\Python311\python.exe $client @remoteArgs download --out .\tmp\laptop-baseline.bin
```

Binary endpoints check the source artifact, fixed 1 MiB size and SHA-256 header;
the CLI/browser verifies the received bytes again and refuses an existing CLI
output file. Baseline download is refused while an operation is active.
Downloading an existing completed read artifact can proceed while another job
runs.

With explicit authorization to write the reviewed selected package:

```powershell
C:\Python311\python.exe $client @remoteArgs job switch_app `
  --catalog-id tracker-variant --entry-method serial --wait
```

`already_uboot` is available only when the local operator has established cold
UBOOT. Low-level `plan`, `flash`, `retry_flash`, `recover_flash`, `enter_uboot`,
`reset`, `observe` and `environment` retain their existing protected guards;
image operations require the exact offline `op:plan` image/hash/baseline request.
Network timeouts never authorize retry or recovery.

For a legacy CLI-only TCP route, manually review current Serve configuration
first and select an unused port, e.g.
`tailscale serve --bg --tcp=9771 tcp://127.0.0.1:9770` followed by a CLI origin
`http://laptop-name.your-tailnet.ts.net:9771`. The launcher does not create this
route. Use HTTPS for the store. A configured SSH tunnel is another loopback
CLI option; no SSH service/account is installed by this feature.

## Official firmware updater

Configure only the real local `M-UPGRADE-FM1.exe` or `M-UPGRADE.exe` with its
explicit SHA-256 when starting the laptop bridge:

```powershell
$vendorUpdater = 'C:\FM1\official\M-UPGRADE-FM1.exe'
$vendorHash = (Get-FileHash -LiteralPath $vendorUpdater -Algorithm SHA256).Hash.ToLowerInvariant()
.\tools\jieli-wl82\start-remote-bridge.ps1 -SessionRoot $laptopSession `
  -OfficialUpdater $vendorUpdater -OfficialUpdaterSha256 $vendorHash
```

The store's official-update action opens that pinned GUI on the laptop. The
operator completes the vendor MIDI/SysEx workflow there with the correct
official package. The bridge does not emulate SysEx, assert a direct stock
UBOOT command, or report a completed official firmware write. A successful
handoff persistently blocks protected device jobs because the old baseline may
no longer describe the device. After local official completion and verified
readback/receipt, start a fresh protected session and a fresh dedicated bridge
state directory. Preserve the previous state and logs for audit. There is no
remote clear-latch endpoint.

## Disconnects and recovery

Jobs are committed to disk before acknowledgment and marked running before
backend execution. An HTTP disconnect does not cancel them. Save the job ID,
then poll it; the browser saves only that ID in localStorage:

```powershell
C:\Python311\python.exe $client @remoteArgs get <saved-job-id>
C:\Python311\python.exe $client @remoteArgs wait <saved-job-id>
```

The same ID plus the same normalized request returns the saved job; a changed
request with that ID is HTTP 409. The client never resubmits automatically.
Only one operation can run at once. Known backend refusals are `failed`.
Unexpected backend exceptions and unfinished device jobs discovered after a
restart become `unknown`, persist across restarts, and block device operations.
Inventory, catalog, `plan`, `plan_app` and `environment` remain available for
review. Restarted unfinished offline plan/environment jobs can fail safely.

For an unknown outcome, inspect laptop bridge job metadata plus the protected
session's `state.json`, run receipts and local logs before any device action.
Do not delete history, invent a new clean baseline, power-cycle, reset, repeat
the write, or clear the latch to make the UI green. Follow the protected
same-image retry/recovery evidence workflow only after a local audit establishes
the actual failure and physical state. Preserve old snapshots. A fresh state
directory is appropriate only after a verified new baseline resolves the old
uncertainty, never as a substitute for that evidence.

## Live acceptance checklist

1. Verify the kit manifest/ZIP, laptop Python dependencies, fresh protected
   worker snapshot and same physical FM-1 baseline relationship.
2. Run preparation twice: token unchanged, private ACLs retained, no device I/O
   or automatic elevation. Verify loopback-only bind and duplicate-process/port
   refusal without stopping an unrelated listener.
3. Publish the reviewed private HTTPS route. On the PC, confirm unauthenticated
   API rejection, authenticated status/catalog and saved job polling. Preserve
   existing unrelated Serve configuration.
4. On the laptop establish cold UBOOT, capture the expected device identity,
   request one explicit two-pass read and download it. Verify 1 MiB size/hash and
   acknowledge the retained helper/status-register behavior.
5. Review an audited package's `plan_app`; confirm preserved bytes, changed
   sectors and the correct profile/variant. Establish appropriate cold device
   state locally before authorizing one app switch.
6. Confirm scoped write/readback, single reset, advancing serial status and
   correct app identifier. Separately observe LCD, keys/knobs and audible output.
7. During a simulated network disconnect, recover using the saved ID without
   issuing a second write. Exercise uncertainty/restart behavior with offline
   fake backends rather than deliberately interrupting a physical flash.
8. If using M-UPGRADE, verify the GUI handoff separately, complete the official
   workflow locally, then obtain a new verified baseline before protected
   switching resumes. Record results as remote/bench acceptance only when done.

## Implementation validation, 2026-10-08

`python -m unittest discover -s tools/jieli-wl82 -p 'test_remote_*.py'`
passed all 112 tests together. They cover protected reads/status, package export,
artifact integrity, fixed requests, authentication, job persistence/idempotency,
disconnects, recovery blocks, app switching, and the PC transport. Thirty
existing flash-session regression tests also passed. The existing
`test_current_profile_regeneration_reviewed_extent` remains blocked by a stale
older NES build's `app_main.c` source hash; it was not bypassed or relabeled.

The Windows launcher passed real unelevated startup, authenticated device
enumeration with no attached FM-1, and duplicate-start checks. Repeated
`-PrepareOnly` retained the token and private ACLs. The five real bundles were
uploaded through the authenticated HTTP API and validated against the selected
verified baseline without opening USB or a protected pipe.

| Private catalog ID | Application bytes | Candidate SHA256 prefix | Evidence |
|---|---:|---|---|
| `nes-smb1` | 214896 | `4d4da3ab` | Fresh isolated audited build |
| `doom-original` | 567344 | `d6884aae` | Existing fresh candidate and audited producer |
| `mdx-demo` | 197040 | `e29d2aae` | Exact external MDX static audit and stock-container checks |
| `mdx-rayforce-penetration` | 390416 | `c719cad5` | Fresh private-song build; exact MDX/PDX bytes in decoded image |
| `buddha-namo` | 405072 | `0c4c8e07` | Fresh build; exact NAMO.MOD embedded boot array |
| `protracker-stoa` | 483920 | `978f19b7` | Fresh isolated audited build |

These are validated packages, not physically written or accepted images.
Full hashes, build/source freshness and the excluded local MDX fallback
failure are retained privately in
`tmp/remote-catalog-20261008-a38c7d1f/receipts/build-evidence.json`. Old accepted
build outputs were preserved. No flash, reset or UBOOT transition was requested.

The private kit from source commit `54c9520` was subsequently deployed on the
Windows laptop. The receiving session verified the ZIP SHA256 and all 126
manifested file sizes/hashes, then prepared a dedicated Python 3.12 environment
for inventory-only operation. It reported private state preparation, successful
local authenticated status/catalog, duplicate-start reuse and a private HTTPS
route without a protected session.

At 19:41 JST on 2026-10-08 the PC independently verified the laptop's HTTPS
certificate, static store HTTP 200, unauthenticated status HTTP 401, authenticated
status/catalog and exact hashes for all five variants. Every variant remained
unwritable without a protected session. Safe enumeration detected the FM-1 CDC
interface; no serial or USB operation was submitted. Sanitized evidence remains
in `tmp/laptop-https-evidence-20261008.json`; credentials are outside the repo.
At that first inventory verification, the laptop still needed `C:\Python311`
and a fresh protected snapshot with the locally established same-device baseline.

The later private RayForce variant contains one song, `PENETRATION (ROUND 1)`,
with its exact matching PDX bank. It passed fresh target linking, the complete
static audit, stock-container gates and exact generated/ELF/packaged asset
comparison. All 123 source, 10 archive and 3 overlay hashes matched. The
source-only [private boot-song builder change](https://github.com/Keitark/fm1-mdx/pull/25)
requires explicit MDX/PDX files and their SHA256 locks, preserves the original
demo, and excludes that demo's metronome mute from the private music variant.
Eighteen input/generator tests passed normally and with Python `-O`; the host
workflow passed 19 CTest cases and 14 Python checks. A 65-second real-asset host
replay passed without missing samples or player/LCD/USB errors. The private JSON
and sanitized evidence were transferred over Taildrop for local catalog import.
No hardware operation was performed during this package work. The stock demo
and RayForce are distinct immutable catalog variants; select the desired one.

The receiving session subsequently prepared the protected runtime and a fresh
laptop snapshot using the same verified baseline, with the user's confirmation
that it is the same physical FM-1 with no later firmware changes. The PC's
authenticated HTTPS metadata check confirmed the imported RayForce ID/hash,
an idle unblocked session and all six variants matching the preserved regions.
This confirms catalog preparation and session configuration, not physical
playback or a new full-flash hash. No device operation was submitted by the PC.
