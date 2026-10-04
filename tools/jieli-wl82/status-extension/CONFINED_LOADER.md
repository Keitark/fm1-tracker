# Cold-loaded status helper in the original RAM footprint

This supersedes the *placement design* of the earlier `0x1c60000` prototype,
not its unverified flash-writer status. It is built by `build_confined.py`.

The blob remains exactly 24064 bytes at `0x1c02000`, and retains the original
entry, initialization, BSS, stack convention, read/program/erase handlers,
and key/negotiation paths. No live hook patch or additional RAM window is used.

Changes are limited to three regions:

- `0x1c04272`, four bytes: route FC subtypes outside 09..16 directly to the
  fallback. Optional FC40..48 are therefore no longer dispatched.
- `0x1c04318`, four bytes: route the fallback through the status dispatcher.
- `0x1c048e4`, 212 bytes: replace part of the now-unreachable optional FC41..48
  handlers with the status routine. The original handler region is 382 bytes.

No external direct branches or absolute pointer candidates into that retired
handler range were found in the pinned original. Its FC jump table becomes
unreachable through the route change. The 190-byte status/transfer body is
byte-identical to the earlier reviewed prototype; only the 22-byte dispatch
prefix has relocated external branch targets. Exact replacement and composed
blob hashes are pinned; all other original bytes are preserved.

Encoded blob SHA-256:
`288dcd73fed7eeb4fd144aa943edcbf4dfe410bcaf695e18f70ecd2f2b1a806d`.

## Hardware attempt, 2026-09-27 20:20 JST

With user-confirmed fresh UBOOT, the 47 RAM writes and loader jump completed.
The FC03 extension returned its `FMS1` signature and SR1 zero before the online
command and on four subsequent samples. NOR online identity matched `856014`.

The subsequent full array read failed with Windows error 31. No flash-array
erase/program command was sent. There is **no** successful full-array comparison
for this helper, and no established root cause for the read failure. SPI-state
interference is a hypothesis, not a diagnosis. The original backup is unchanged
on disk, but this failed read does not prove physical array identity.

Receipt: `private-backups/status-preflight-20260927-202013/result.json`.
The initial receipt did not record the exact failing read address; it failed
before the first 128 KiB progress milestone. Do not claim it was necessarily
the first read. No failed USB transfer was automatically retried or reset.

## Second hardware attempt, 2026-09-27 20:54 JST

After another user-confirmed cold boot, the baseline 256-byte read after online
initialization and **before any FC03** matched the original backup. Four status
samples returned SR1 zero; the same 256-byte probe after status sampling also
matched. The full read then matched bytes `[0, 0xc300)` (49,920 bytes), but the
256-byte FD05 read at `0xc300` failed with Windows error 31, command number 252
after INQUIRY. Neither complete 1 MiB comparison finished. This is not an
immediate read failure following status sampling, but does not rule out loader
interference or establish any specific cause.

Receipt: `private-backups/status-preflight-20260927-205455/result.json`.
Again, no array erase/program, chip-key query, automatic retry or reset occurred.

## Host transport diagnostic, 2026-09-27 21:03 JST

The separate `fm1_scsi.py` adapter queries Windows adapter requirements, aligns
buffers and collects SCSI sense data. One standard INQUIRY using it succeeded
in the uncertain session; no vendor command or loader operation was sent.
Windows reported alignment mask **0**, maximum transfer 65,536 bytes and 17
physical pages. Therefore the old adapter's lack of explicit alignment does
not explain the failure based on this adapter requirement. Successful INQUIRY
does not establish that NOR reads or the helper are healthy.

Receipt: `tmp/inquiry-aligned-20260927-210349/result.json`.
The preflight now uses this adapter and records the last transfer duration.

## Third hardware attempt, 2026-09-27 21:07 JST

After the user confirmed another fresh boot, the same confined loader was
tested through `fm1_scsi_aligned_sense_v1`. Baseline and post-status probes
matched, and all four status samples returned SR1 zero. The full read matched
`[0, 0x16900)` (92,416 bytes), then FD05 at `0x16900` failed with Windows error
31. The failed command was number 418 after INQUIRY and took 0.444 ms. The
packet contained SCSI status zero and 32 zero sense bytes; on a failed IOCTL
these are not proof of device-reported success or meaningful sense data.
No full comparison completed and no erase/program, key query, retry or reset
was sent. Receipt: `private-backups/status-preflight-20260927-210731/result.json`.

The failure at a different address with the new host adapter does not establish
a root cause. The next controlled test is **the original unmodified loader**,
using the same adapter and 256-byte comparisons without FC03. This isolates
loader/status-extension changes from common transport and read-path behavior.
`fm1_status_preflight.py --original-control` pins the original blob hash,
forbids FC03 and array writes, and compares two full passes against the backup.
Its successful result would qualify only that read control, not NOR status
polling, flashing or recovery. Nine preflight host tests pass in normal and
optimized Python. Its first hardware result is recorded below.

## Original-loader control, 2026-09-27 21:11 JST

After another user-confirmed fresh boot, the original encoded loader with
SHA-256 `d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376`
was uploaded and run through the same `fm1_scsi_aligned_sense_v1` adapter.
No FC03 was sent. The baseline probe matched, then the full comparison matched
`[0, 0xe300)` (58,112 bytes). FD05 at `0xe300` failed with Windows error 31,
command 278 after INQUIRY, taking 0.528 ms. Total preflight duration was
614.354 ms. Again the failed IOCTL provided no useful sense data, and neither
full pass completed. No flash-array erase/program, key query, retry or reset
was sent.

Receipt: `private-backups/original-control-20260927-211135/result.json`.
This reproduces failure without the custom extension; that extension is not
necessary for the observed failure. It does not prove a transport-only cause
or qualify the original loader. The proposed next controlled variable is
inter-read pacing with the original loader; no paced test has been run.
Windows disk metadata subsequently showed No Media, offline, RAW, size zero;
no disk configuration was changed. A narrow five-minute System event query
found no matching USB/disk/storage/Kernel-PnP warning/error entries, which
does not establish that the transport is healthy.

Another full-loader test requires a fresh user-confirmed power cycle; never
reload the helper into the uncertain session. Root cause remains unresolved.

## Paced original-loader control, 2026-09-27 21:22 JST

The launch requested at 21:20 initially waited at UAC. Approval subsequently
arrived and the diagnostic executed; its result was collected on the user's
next turn. Do not treat that pending launch as an unused cold-boot session.

With the original loader, no FC03 and a requested 10 ms pause before every
FD05 read, the baseline matched and pass 1 matched 42,496 bytes. FD05 at
`0xa600` failed with Windows error 31 after 217 commands following INQUIRY.
The failing IOCTL took 0.552 ms; total diagnostic duration was 2,179.317 ms.
No useful sense data was returned. No full pass completed, and no flash-array
erase/program, key query, automatic retry or reset was sent.

Receipt: `private-backups/original-paced-20260927-212027/result.json`.
Ten preflight host tests pass in normal and optimized Python, including pacing
order, failure termination, progress accounting and both full control passes.
The hardware result shows that this 10 ms pacing change did not resolve the
failure; it does not identify its cause. No second session was launched when
collecting this result. A subsequent RAM upload requires another explicit
fresh-power-cycle confirmation.

## Repeated paced control, 2026-09-27 21:25 JST

After another explicit fresh-boot confirmation, the same original-loader
10 ms-paced control was repeated once. The baseline matched and pass 1
matched 75,264 bytes before FD05 at `0x12600` failed with Windows error 31.
This was command 345 after INQUIRY; the failing IOCTL took 0.533 ms and the
whole preflight took 3,735.026 ms. Neither full pass completed. No flash-array
erase/program, key query, automatic retry or reset was sent.

Receipt: `private-backups/original-paced-20260927-212504/result.json`.
Unchanged-sequence retries are stopped. Read-only Windows PnP inspection shows
the FM-1 mass-storage interface (`4c4a:8057`, USBSTOR) at
`Port_#0022.Hub_#0001`, beneath a USB 3 root hub on the AMD USB 3.10 controller
(`1022:43d0`, USBXHCI). No intervening enumerated external hub appears in that
parent chain; this does not reveal a passive/inline downloader or cable.
All inspected nodes reported OK, which does not establish reliable transfers.
The narrow five-minute System event query again returned no matching entries.
No driver, disk, USB configuration or hardware state was changed by inspection.
The next investigation is the physical USB path; its cause is still unproven.

## Original reader with replacement cable, 2026-09-27 21:38 JST

The user changed the cable and explicitly confirmed a full power cycle and
return to UBOOT. This test used `read_fm1_firmware.py` with the original
upstream `scsiio` Windows transport, not `fm1_status_preflight.py` or
`fm1_scsi.py`. The wrapper first ran a cold original-loader probe, then opened
a second reader process with `--reuse-loader` for the two-pass raw backup,
reproducing the earlier successful probe/reuse workflow. The reader retains
its previously added strict upload validation; it is not a historical binary
snapshot of the September 26 script.

The cold probe matched the saved 256-byte SHA-256, and the second process's
online/probe checks also passed. The raw read failed with Windows error 31.
The retained partial is 23,040 bytes (`0x5a00`), and an offline byte comparison
found every retained byte identical to the original backup prefix. No full
pass completed. The original reader did not log the failing command address;
`0x5a00` is the next sequential address inferred from the flushed partial.
No flash-array erase/program, key query, automatic retry or reset was sent.

Evidence: `private-backups/original-reader-new-cable-20260927-213757/`
contains `result.json`, `cold-probe.log`, `readback.log`, the matched probe and
the retained private partial. The original full backup was not overwritten.
The new cable did not resolve the failure, and the new transport adapter is
not necessary to reproduce it. USB port/controller or another-host testing
is the proposed next change; no driver/configuration mutation was performed.

## User-requested original-reader retry, 2026-09-27 21:43 JST

Following another explicit fresh-power-cycle confirmation, the original
reader/upstream transport probe-and-reuse flow was repeated once at the user's
request. Both probe checks passed, but the raw read again failed with Windows
error 31. All 29,184 bytes in the retained partial matched the original backup
prefix in an offline comparison; `0x7200` is the inferred next read address,
not a command address recorded by this reader. No full pass completed and no
flash-array erase/program, chip-key query, reset or automatic retry was sent.
Receipt and partial: `private-backups/original-reader-repeat-20260927-214333/`.
No additional USB port/controller change was established for this run.

## User-requested original-reader retry, 2026-09-27 21:53 JST

After another fresh-boot confirmation, the original probe/reuse reader flow
again passed both header checks but failed during raw read with Windows error
31. All 8,448 retained bytes match the original backup prefix; `0x2100` is the
inferred next sequential address, not a logged failing command address. No
full pass completed and no flash-array erase/program, chip-key query, reset
or automatic retry was sent. Evidence is retained under
`private-backups/original-reader-repeat-20260927-215324/`. Windows still reports
the same USB interface instance `6&26B09227&0&22`; a different controller path
has not been established. Root cause remains unknown.

## Confirmed port change, 2026-09-27 21:55 JST

The user confirmed another cold boot. Windows now enumerated the FM-1 at
`Port_#0011.Hub_#0001`, interface instance suffix `&0&11`, rather than port 22.
The original reader/upstream transport probe-and-reuse flow passed both
header checks, then failed during raw read with Windows error 31. All 106,240
retained bytes match the original backup prefix; `0x19f00` is the inferred
next sequential address, not a recorded failing command address. No full
pass completed. No flash-array erase/program, key query, reset or automatic
retry was sent. Evidence: `private-backups/original-reader-port11-20260927-215552/`.
This establishes a changed port, not a changed controller or a diagnosed cause.

## User-requested original-reader retry, 2026-09-27 21:57 JST

With another confirmed fresh boot, the original probe/reuse flow again passed
both header checks but failed in raw read with Windows error 31. All 114,176
retained bytes match the original backup prefix; `0x1be00` is the inferred
next sequential address. No complete pass, array erase/program, key query,
reset or automatic retry occurred. Evidence:
`private-backups/original-reader-repeat-20260927-215730/`.

## Successful original-reader run, 2026-09-27 22:04 JST

Following the user's fresh-boot confirmation, the original probe/reuse flow
completed **two full, mutually matching 1 MiB reads**. The result is retained
under `private-backups/original-reader-repeat-20260927-220410/`, with SHA-256
`4d69ef5bbd662b0b762dc01956378f7c1776b317e463ccc15815cd5bc0b338a3`.
No flash-array erase/program or key query was sent. No reset was requested;
the original RAM helper remains the last known running helper.

Offline comparison against the September 26 backup found exactly two differing
bytes, at `0xee046` and `0xee069`, outside all current planned NES sectors.
Their cause is not established; do not claim the whole array is unchanged or
overwrite these new bytes with the older backup. `baseline-comparison.json`
records offsets and hashes without exposing private byte values.

This establishes a successful original-reader session, not the cause of prior
failures or qualification of the custom status helper, erase/program or
physical recovery. Existing offline plans still require the old exact full
preimage and therefore cannot be executed against this new baseline unchanged.
The user authorized proceeding toward NES writing and asked not to repeat
confirmation questions. No write was attempted: a qualified completion path
and live writer are still missing. Do not hot-reload the status helper into
the running original helper.

Until these checks pass, the NES flash update remains blocked. Successful
status responses alone are not successful flashing or recovery qualification.
