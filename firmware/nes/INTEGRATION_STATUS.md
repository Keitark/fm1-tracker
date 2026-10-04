# FM-1 standalone NES integration status

USB diagnostic (2026-09-28): a separate SDK CDC-only application has been
built and offline-tested in `firmware/usb-diag`; no NES/LCD/audio/scanner is
linked. It uses FreeRTOS and cannot log before SDK/USB startup. The Windows
terminal and transfer-verification client need no raw-disk access/elevation.
**Firmware installation over CDC is not implemented**: the stock directory
has no `app_dir_head2`, required by the SDK passive updater. COMMIT is blocked.
USB0 connector routing and physical enumeration remain unqualified. See
`firmware/usb-diag/README.md`. No device port was opened or image flashed.
Current work: user redirected to **startup investigation**, not another LCD
transport experiment. The parameter-polling assembly draft is paused and was
never packaged/flashed. Offline comparison of the accepted stock control and
failed pre-OS NES confirms unchanged boot-region bytes, cache-body parity,
consistent RAM-copy geometry and zero initial software-lock state. The first
unobserved startup boundary remains unlocalized; no root-cause fix is claimed.
The pre-OS hook still follows CRC-mutex initialization and its critical-section
code. Further review checks 36 ordered constant-argument P33 operations against
both working stocks, plus transport-helper parity and the three DC/DC delays.
The suspected `80AA` versus `80AB` difference was a disassembly-annotation
misreading: both target `80AB`. Eleven evidence tests pass normally and with
`-O`; no device operations or firmware changes followed the redirection. See
`docs/FM1_STARTUP_BOUNDARY_INVESTIGATION_20260928.md`.

Latest deployment (2026-09-28 07:07 JST): **stock V14 white frame using polled
pixel output flashed**, not NES. Only the accepted marker's 46-byte pixel loop
is replaced; stock startup/power/panel setup/UI remain. The user explicitly
requested retry after the earlier canceled UAC request. All 45 sectors and
two full 1 MiB readbacks match SHA-256
`444b5d8cea8a3ffa13d72e331e148a34e9f5af902965eb38f80d428dbad2b792`.
Existing jluboottool.py writer, no pre-write firmware reads, no automatic reset.
Receipt: `private-backups/stock-polled-write-20260928-070755/deployment.json`.
Physical test **passed by user observation**: user replied "it does" to the
expected brief white screen followed by normal stock UI. Polled white-pixel
output works under stock startup; the complete NES LCD driver/startup remains
unqualified. No further device operation was performed when recording this result.
See `docs/FM1_STOCK_POLLED_BENCH_20260928.md`.

Previous deployment (2026-09-28 02:51 JST): **pre-OS NES display checkpoint
flashed**. Power/display now run after main's clock/IRQ/latch setup but before
os_init, CPU1 release and filesystem startup. The original OS initialization,
CPU1 divider guard, integrity checks and SMB1 worker remain. All 45 sectors and
two complete readbacks match SHA-256
`8da9ba09455e10f9cd04666df10a32d7925aaf21efee9f6640631f39dba0d5ca`.
No pre-write firmware reads or automatic reset. Expected first picture is RGB
bands/yellow strip, followed by later blue checkpoint/SMB1 if startup proceeds.
User subsequently reports **still blank screen** and manual UBOOT re-entry.
Visible-output acceptance failed; execution reaching the checkpoint remains
unproved. No claim that the OS/CPU1 move fixed startup.
See `docs/FM1_PRE_OS_DISPLAY_BENCH_20260928.md` and
`private-backups/pre-os-write-20260928-025138/deployment.json`.

Accepted control: user confirmed stock V14 marker boots normally, displaying
white before the normal UI, then manually re-entered UBOOT and reconnected.

Previous deployment (2026-09-28 02:37 JST): **stock V14 white-frame diagnostic
flashed**, not NES. One two-byte fill instruction differs from working V14;
startup, power, panel setup and normal UI remain stock. All 45 sectors and two
complete readbacks match SHA-256
`fcc6ef41fc48f5e89904c184af6f5ffa798e67ad2bb3cadf33d1c7905b562a69`.
At the user's request, the already-completed matching source read was reused
and no further pre-write reads were made. Existing `jluboottool.py` write and
post-write verification remain in use. No reset sent by the writer; subsequent
user normal-boot test passed with the expected white marker and stock UI.
See `docs/FM1_STOCK_MARKER_BENCH_20260928.md` and
`private-backups/stock-marker-write-20260928-023712/deployment.json`.

Previous deployment (2026-09-28 02:17 JST): **CPU1 SYS_DIV guard version flashed**
through the existing `jluboottool.py` writer. All 19 changed application sectors
and two complete 1 MiB readbacks match SHA-256
`51dc35fdf0bcbe4e105c4f84e5c4ee52e827f4a88169c81b65541488d5998aa1`.
It adds stock's temporary divider guard around CPU1 release/ready wait while
retaining the two-core scheduler and screen-first SMB1 behavior. No reset was
sent. Subsequent user bench result: **still no screen**. Display acceptance
failed; gameplay and the point reached during normal boot remain unverified.
The divider guard alone was not sufficient. The latest follow-up is offline
diagnosis only; no USB serial implementation, new build or device operation.
See `docs/FM1_NO_SCREEN_GUARD_FOLLOWUP_20260928.md`.
See `docs/FM1_CPU1_GUARD_BENCH_20260928.md` and
`private-backups/cpu1-guard-write-20260928-021650/deployment.json`.

The entries below describe earlier images, not the currently installed image.

Previous bench result: user reports **"no screen yet"** for screen-first SMB1.
The successful flash/readback below remains valid, but visible-output acceptance
failed. Execution reaching the early display hook is not proven. Read-only
Windows enumeration shows UBOOT; the user confirmed deliberate re-entry after
testing normal boot. This is not evidence of automatic fallback. No further
image or device operation was made in this follow-up; see the updated bench report.

Earlier (2026-09-28 01:53 JST): **screen-first SMB1 built and flashed**, using the
existing `jluboottool.py` writer. All 45 application sectors and two full 1 MiB
final reads match SHA-256
`198140cbf6cb4d052ecc9fbb610ad0f9619acadb65888adee1e172827fc87e95`.
No reset was sent; normal power-cycle/display/gameplay acceptance is pending.
UBOOT, configuration and reserved bytes are preserved. Stock-first review
conclusions, remaining wiring uncertainty and intentional diagnostic differences
are documented; this is not a claim of complete stock semantic recovery.
See `docs/FM1_SCREEN_FIRST_BENCH_20260928.md` and receipt
`private-backups/screen-first-write-20260928-015134/deployment.json`.
Build: `build/boot-screen-first-reviewed-smb1/`, application 181,136 bytes.
Expected checkpoints: early RGB/yellow, later blue, then SMB1 if startup succeeds.

The following entries are historical checkpoints, superseded by the status above.

2026-09-28 expanded stock audit: full byte-checked boot/application/RAM listings,
initialization/device/nested callback registries and unresolved-path inventories
are now indexed. See `docs/FM1_STOCK_EXECUTION_AUDIT_20260928.md`.
Required-process completeness is **not established**. T2 early-display source
work is paused and its build profile fails closed before creating artifacts;
no new firmware was built or flashed in this audit. Last-written hardware state
remains the dark-screen stock-power candidate described below.

2026-09-28 bench update: the stock-power SMB1 candidate was flashed using the
existing `jluboottool.py` writer. All 45 application sectors and two full 1 MiB
readbacks matched exactly. Last-written device is now this candidate, **not T1**.
No reset was sent by the tool. The user subsequently reported **no screen,
completely dark**, so this candidate has failed the visible-output bench test;
the point at which execution stops remains unknown. See
`docs/FM1_STOCK_POWER_FLASH_20260928.md`. The offline build reports below remain
historical build-stage evidence, not hardware qualification.

2026-09-28 subsequent offline implementation: stock 010's recovered power
selectors now drive SDK `power_init` from `board_init`. The separate candidate
is `build/boot-stock-power-smb1/` (180,080-byte application). Host parameter/order
tests and compiled power-sequence auditing added. **Not packaged, flashed or
hardware-qualified**; no claim yet that this fixes the blank screen. See
`docs/FM1_STOCK_POWER_INTEGRATION_20260928.md`.

2026-09-28 offline refinement: actual NES-versus-working-010 startup comparison,
reported-clock guard correction, bounded stalled-clock/SPI waits, live-debugger
RAM breadcrumbs and expanded machine-code audits. Separately built in
`build/boot-refined-peripherals-smb1/`; **not packaged, flashed or hardware
qualified**. At that checkpoint stock board-power initialization was unresolved;
the subsequent implementation above addresses that gap. See
`docs/FM1_NES_BOOT_REFINEMENT_20260928.md`. Previous bench binaries are preserved.

Latest update (2026-09-27 23:41 JST): user confirmed restored V14 boots normally.
The authorized display-only diagnostic T1 is now written using `jluboottool.py`;
all 27 sectors and two complete 1 MiB readbacks match T1. No NES, ROM, audio or
key scanner is linked. The user subsequently reported no visible output on
normal boot. Last-written flash is T1, not V14/NES; hardware qualification has
not passed. See `docs/FM1_DISPLAY_T1_20260927.md` and the working-010 comparison
`docs/FM1_010_VS_T1_BOOT_TRACE.md`. Older unflashed
candidate and write-gate discussion below is retained as historical context.

2026-09-27. Target: a standalone NES application on the FM-1, using recovered
stock hardware behavior. This is not a replacement implementation of the stock
synthesizer or a claim of feature parity with its menus, patches or MIDI engine.
SMB1 supplied through the user's Google Drive is now the default cartridge.
See `SMB1_INTEGRATION.md` for its build and the latest stopped transfer attempt.

## Stock responsibilities versus the current replacement

| Responsibility | Implemented / retained | Missing evidence or functionality |
|---|---|---|
| Bootloader and private data | Original bootloader, configuration/reserved bytes and offsets retained in the raw package | Physical handoff, write/readback and recovery qualification |
| Startup ABI and memory | Stock-prefix bridge, SDK initialization, internal-RAM-only link, copy/clear bounds audited | Real CPU startup, stacks and peak runtime memory |
| SDK event dispatch | NES now has its own task; app entry returns to the SDK queue dispatcher | Physical timer/event delivery; live SDK integrity result |
| SDK integrity | Existing IRQ and deferred callback retained; 8,000 ms schedule checked | Hardware-dependent outcome cannot be established by package CRCs |
| LCD | Recovered SPI1 pins, initialization table, RGB565 renderer and 40-pixel Y origin | Visible crop/orientation, warm reset/backlight behavior and frame throughput |
| Audio | Recovered IIS/ALINK route, signed conversion, buffering, startup silence/ramp | Measured clocks, analog power/mute/transients; full NES DMC emulation absent |
| Keys | Recovered SPI2 scanner, 41-key decoder and F-first NES pad mapping | Physical debounce/scan timing/ghosting; knobs have no NES assignment |
| Emulator/cartridge | SMB1 default; ROM 50 optional; mapper 0/3 validation and host execution tests | Other mappers, saves, complete game-accuracy and physical execution qualification |
| Power/battery | Stock-derived P33/DC-DC/voltage initialization through the SDK; no guessed external reset/amplifier GPIO writes | Physical rail/timer qualification, battery monitoring and user-facing low-battery shutdown remain unverified/unimplemented |
| Persistence/update UI | Stock persistent bytes preserved; unsafe automatic VM/BTIF repair registrations excluded | No NES save menu, USB MIDI service, cartridge selection or in-app updater |

## Concrete integration correction in this build

The previous emulator loop occupied `app_main` indefinitely. The vendor event
loop comes after that call, so ordinary `os_time_dly` did not service its queued
callbacks. Both profiles now create `fm1_nes` (priority 10, 4,096 stack words)
and return to `app_core` (priority 15). Creation errors remain visible. See
`SDK_STARTUP_SAFETY.md` for source/disassembly evidence and test coverage.

The headless worker still runs 600 frames, but does not finish its diagnostic
observation interval before 20 seconds of worker uptime. Elapsed time and an
idle heartbeat aid later observation; they do not report an integrity-check
result or certify the hardware. The peripheral profile runs indefinitely and
exposes scanner/IRQ counters through `fm1_sdk_diag`.

## Current completion boundary

Both applications link and both private 1 MiB raw packages pass offline
round-trip, source/hash, directory CRC and preserved-byte checks. Nine host
CTest groups and fourteen boot-audit tests pass (the headless profile skips
only the peripheral ISR case); eight packaging tests pass. These scopes do
not include silicon execution.

The requested flash-ready result is **not yet achieved**. In particular, the
external WL82 loader can change status/protection registers during online
initialization and does not provide sufficient erase/program completion
assurance on its own. See `tools/jieli-wl82/WL82_LOADER_AUDIT.md`. There is no
qualified deployment/restore implementation, and no current candidate has
been flashed or boot-observed. Do not substitute an application payload for
the raw package, rename it to `.fwsc`, or use a KALLSUP image.

Whole-sector offline plans and eight additional model tests now cover exact
update/restore bytes and 731 completed-operation interruption boundaries.
See `FLASH_PLAN.md`. The generic FC03 status command is not handled by the
reviewed WL82 vendor dispatcher, so live completion polling remains an explicit
gate; the model does not resolve it or establish physical recovery.

The offline status-extension prototype is not a deployment solution yet:
the running helper routes writes at its proposed hook address away from its
own memory-copy path, and ROM fallback, RAM ownership and instruction-cache
coherency remain unqualified. The exact-loader audit now records this known
installation obstacle. No hook upload or flash change was attempted.

A later same-footprint cold-loader variant was uploaded on 2026-09-27. Its
status extension responded, but the full readback preflight failed with
Windows error 31, before any array erase/program command. Consequently it
does not yet qualify the flashing path. See
`tools/jieli-wl82/status-extension/CONFINED_LOADER.md` for the receipts.
The second cold-session test matched the baseline probe before and after status
sampling, then matched 49,920 bytes before error 31 at `0xc300`. A later standard
INQUIRY succeeded; Windows reports no special adapter alignment requirement.
The third cold-session test used the new sense-capturing transport and matched
92,416 bytes before error 31 at `0x16900`; it provided no useful sense data.
The original-unmodified-loader control also failed, after 58,112 matching
bytes at `0xe300`, without issuing FC03. Thus the custom status extension is
not necessary for this failure. The subsequent original-loader control with
10 ms pauses also failed, after 42,496 matching bytes at `0xa600`. Pacing did
not resolve the failure. Its initially pending UAC approval arrived later and
the completed failure receipt was collected on the next turn; do not assume
that cold-boot session is still unused. Root cause remains unresolved.
No NES image was flashed.

A repeated paced original-loader control at 21:25 matched 75,264 bytes before
error 31 at `0x12600`. Identical-sequence retries are stopped. Windows shows
USBSTOR below the AMD USB 3.10 root hub, but cannot establish cable/inline
downloader condition. Physical USB-path clarification is the next diagnostic
step, not an established diagnosis. No erase/program command was sent.

At 21:38, after a cable replacement and confirmed fresh boot, the original
`read_fm1_firmware.py` reader and upstream Windows transport were tested using
the original-loader probe/reuse workflow. Probe checks passed, but the full
read again failed with error 31. All 23,040 retained bytes match the original
backup prefix. Thus neither the custom status extension nor the newer host
adapter is necessary for the failure. Changing the cable did not resolve it.
Further identical retries are stopped; USB port/controller or another-host
testing is proposed. No flash-array erase/program command was sent.

One further user-requested original-reader retry at 21:43, after explicit cold
boot confirmation, also failed with error 31. All 29,184 retained bytes match
the original backup prefix; no full pass completed and no flash-array
erase/program command was sent. No additional port/controller change was
established for that retry.

At 21:55 Windows confirmed a port change from port 22 to port 11 on hub 1.
After a confirmed cold boot, the original reader still failed with error 31;
all 106,240 retained bytes match the original backup prefix. No full pass or
flash-array erase/program occurred. A different host controller was not
established, and the cause remains unresolved.

At 22:04 the original reader finally completed two full matching 1 MiB reads.
The latest verified dump is under
`private-backups/original-reader-repeat-20260927-220410/`, SHA-256
`4d69ef5bbd662b0b762dc01956378f7c1776b317e463ccc15815cd5bc0b338a3`.
It differs from the original at only `0xee046` and `0xee069`, outside the NES
update sectors. Preserve those current bytes; their cause is unknown. The
old full-image plan precondition no longer matches. This read success does
not qualify the custom status extension or supply a live erase/program/restore
implementation. No NES flash write has been attempted.

Current artifact identities and private output locations: `STOCK_IMAGE.md`.
Later bench acceptance must cover sustained headless execution beyond the
deferred-check window, then display, keys, low-level audio and sustained game
timing, with a validated recovery route. New ROM integration is independent
of the still-open hardware/deployment gates.
