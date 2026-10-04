# Hidden-frame rendering optimization, 2026-09-29

Latest device image is now the instrumentation-only successor documented in
`NES_PROFILE.md` (full hash2c8f0dc9...). The verified4e84e7ef... image below is
the preserved pre-profile baseline.

Status: **retry flashed and fully verified; reset/serial liveness confirmed**.
The earlier first-sector failure below was recovered on 2026-09-29. Physical
LCD/audio/input acceptance remains unconfirmed; audio underruns persist.

## Successful explicit retry, 2026-09-29

Activated updated helper with UAC, PID20664, protected root
`C:\Program Files\FM1FlashSession-3edce9be5079421c8d72bca4118957af`.
Used existing `invoke-flash-cycle.ps1 -Flash -Retry` and existing Jieli writer.
All52 original sectors were rewritten (directory last), sector-verified, then
one complete1MiB readback matched candidate4e84e7ef... exactly. No prewrite dump.
The verified image is now the helper's baseline, not the older9097a074... image.

Reset was sent once. The known UTF-8 decoding error occurred while reading the
Windows CLI reset log; reset was not resent. Subsequent COM10 HELLO and advancing
NES frames13->490 confirmed serial boot/liveness; reported fault=0. Helper state
is idle, blocked=false, reset_pending=false, needs_observation=false; its reset
error is marked resolved_by_observation. No physical display/audio claim.

From counters53->490 over NES TIME2170->14280ms, emulation averaged36.09FPS;
shown9->82 gives6.03 LCD presentations/sec. Input reported ACTIVE/errors=0,
scanning approximately1kHz/11k completions/sec. Audio underrun20144->233062 and
rebuffer8->81 still grow. This is an initial postboot capture, not a controlled
same-scene benchmark against the previous firmware.

Durable evidence: `../../private-backups/nes-render-skip-retry-20260929-182551/`.
Verified baseline and deployment receipt:
`runs/ad071d8b4d6045628356bfcc884067df/final-1.bin` and `deployment.json`.
Full SHA256: `4e84e7ef216f52f4f57395bbbf40f3d0063c7e0afa1a25cbda31e001612a11dc`.
Serial capture: `runs/69658c6f98d547258f7b17c22225e169/serial.log`.
All retry/reset/observe run logs plus final session state are retained there.

## Why this experiment

Pacing reduced scan interrupts, but did not improve FPS. Its exact regression
cause remains unresolved; TIMER4 runs on CPU1 whereas SPI2 runs on CPU0, so IRQ
count alone cannot establish saved CPU time. No claim that the timer/cache is
the proven cause. Fresh preflash paced baseline measured approximately21.54
emulated FPS,3.59 LCD presentations/sec,10995 scan IRQ/sec and28197 missing audio
frames/sec. These use audio-counter delta /44100 as nominal elapsed time.

The existing audio-priority frame skipping suppresses LCD transfer/conversion
AFTER the core has fully rendered the frame. Five of six presentations were
being discarded while still paying full background/sprite pixel-render cost.

## Implementation

- Optional `--nes-render-skip` build overlay; pinned vendor checkout untouched.
- Board's existing bounded skip decision is consulted once at frame start.
- Hidden NROM/CNROM frames avoid RGB rendering and draw callbacks. They retain
  all CPU/APU execution, mapper callbacks, scroll updates and sprite evaluation.
- Sprite-zero hit remains the upstream test. A fast hidden-background path
  calculates opacity for the at-most-eight pixels beneath sprite zero on its
  eligible scanlines; it does not replace hit semantics with a coarse guess.
- Sprite overflow remains upstream evaluation. Unknown mappers force full
  rendering; this port still only accepts mapper0/3 as before.
- Kept 1ms scanning, audio parameters/effects, clocks and display policy unchanged.
- Added `NES TIME ms=` tied to each NES counter report for future timing checks.

Offline validation:26 NES tests,19 USB/peripheral tests,21 broker/writer tests
all passed (66 total). New tests compare900 scripted SMB1 frames (Start, running,
jumping): CPU/PPU/APU/mapper state every frame (excluding only draw/opacity
caches), every audio sample hash, and every displayed pixel hash. All match full
rendering.6000 additional cases match sprite-zero hit/overflow, with scrolling,
clipping, flips, 8x16 sprites, banks, overlap and screen boundaries.
Static boot audit and all9 image-layout preservation checks passed.

## Candidate and historical first-attempt failure (superseded by retry above)

Profile: `build/nes-smb1-render-skip`. Application210608 bytes, SHA256:
`43190fa26542b471de886943002002eed46d3ddefc6cfe03542a403920f36e9b`.
Full1MiB candidate SHA256:
`4e84e7ef216f52f4f57395bbbf40f3d0063c7e0afa1a25cbda31e001612a11dc`.
Image is under `../../private-backups/usb-diag-candidates/<full-sha>/`.

Existing elevated helper/CLI entered UBOOT and uploaded its pinned WL82 RAM
helper successfully. In the first sector (`0x5000`), erase completed; programming
progress reached2.25KiB/4KiB before `SCSIException: Transfer failed: [WinError 31]`.
No sector verification completed. No other sector or directory write was
attempted. No reset, final full read, retry, or baseline promotion followed.
The current NOR contents are **not** the last verified baseline: sector5000 may
be partial/erased. WL82 UBOOT remains enumerated; helper process69316 remains
alive without an idle timeout, but its flash failure latch blocks further writes.

Recovery requires a deliberate reviewed write after restoring a stable UBOOT
connection. Do not clear the failure latch or pretend baseline9097a074... is
the current physical image. It is the last complete verified rollback image.
The candidate's52 changed sectors include the damaged sector; a controlled
complete candidate write can cover that known damage, then one full matching
readback is required before reset. No recovery was attempted in this turn.

Evidence: `../../private-backups/nes-render-skip-write-20260929-174442/`:

- `b6bc5fa6cb1b4021b67a707512f342bc/serial.log`: preflash capture.
- `3e2a1541a341455baedd3a8c0436a196/`: UBOOT transition.
- `cdeffa93bfe640a7a5cdf8364ca6364d/`: helper/write logs, failed receipt,
  candidate sector5000 and saved old-sector restore bytes.
- `state.json`, `session.json`, `preflash-metrics.json`.

Source rollback: `../../private-backups/nes-render-skip-source-20260929-173814/`.
Last fully verified image remains
`../../private-backups/nes-paced-1ms-write-20260929-1729/runs/9b16165222164b83b54953f93894a95c/final-1.bin`.

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --nes-smb1 --keyscan-dma2 --nes-input-recovery --keyscan-irq --keyscan-paced --nes-audio-priority --nes-live-fx --nes-channel-fx --nes-render-skip --out firmware/usb-diag/build/nes-smb1-render-skip
```
