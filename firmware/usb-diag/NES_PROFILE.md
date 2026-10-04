# NES wall-time profile — 2026-09-29

Status: built, tested, flashed through the existing elevated helper, single
full-readback verified, reset once, and observed twice on COM10. Fault=0 and
NES frames advance. Physical LCD/audio/input acceptance remains separate.

## Result

The synchronous LCD write path is the largest elapsed-time cost. During the
second serial capture, frames1348..1708 and shown225..285 span10.0175 seconds
on the profiler clock (NES TIME38220..48240ms gives10.02 seconds).

| Exclusive section | Time in capture | Wall share | ms per emulated frame |
|---|---:|---:|---:|
| LCD write: DMA-buffer copy, commands, transfer/completion polling | 5.1750s | 51.66% | 14.375 |
| NES CPU opcode execution | 3.2375s | 32.32% | 8.993 |
| PPU/rendering and remaining core-loop work | 1.0045s | 10.03% | 2.790 |
| Audio effects/stem processing, excluding queue push | 0.3300s | 3.29% | 0.917 |
| Video conversion/callback, excluding LCD writes | 0.1535s | 1.53% | 0.426 |
| APU generation, excluding downstream effects/queue | 0.0870s | 0.87% | 0.242 |
| Frame callback/input/pacing/telemetry | 0.0160s | 0.16% | 0.044 |
| PCM queue/push/wait | 0.0140s | 0.14% | 0.039 |
| Other | 0 | 0 | 0 |

The sections sum exactly to10.0175 seconds. Emulation35.94FPS, LCD5.99 updates/s.
LCD time per presented frame is86.25ms total, spread over the synchronous
half-frame/strip writes; this is not a measured single uninterrupted86ms pause.
Audio missing-frame counter still increases by17707.4/s. Mode at observation
is channelALL/effectBYPASS; active filter patches may cost more.

Preflash capture's final10.12 seconds:35.87 emulated FPS,6.03 LCD updates/s,
17655.1 missing audio frames/s. No large throughput regression is apparent from
instrumentation, but scene/input histories were not controlled, so this is not
an exact profiler-overhead benchmark. No Start/key presses were injected.

The next optimization target is LCD scheduling/transfer overlap: the existing
DMA routine still spins synchronously until each strip completes. Reducing that
blocking time can let CPU/APU execution continue. Higher SPI speed, asynchronous
DMA buffers, and presentation-rate changes need separate validation; none were
implemented in this profiling build. Audio DSP itself is not the dominant cost
in this measured BYPASS case.

## Measurement contract

- Opt-in `--nes-profile`, requiring the existing render-skip profile.
- Clock: SDK `jiffies_half_msec()`,500us units. The linked SDK `timer_get_ms()`
  reads jiffies*10, so the existing `now_us()` has10ms granularity and was not
  suitable for sub-frame attribution. No hardware timer or clock was reconfigured.
- Single NES-task exclusive scope accounting. Nested LCD/PCM/FX costs are
  subtracted from parent scopes, not double-counted. Snapshot includes current
  scope elapsed time; the reported stage sum must equal wall time.
- These are **wall-time buckets**, including interrupts, scheduler preemption,
  waits and instrumentation overhead occurring inside that scope. They are not
  per-function CPU-cycle measurements or a separate ISR-utilization audit.
- PPU bucket includes scroll/sprite-state work and the rest of the core loop,
  not just RGB pixel generation. Hidden-frame rendering behavior is unchanged.
- Timing wrappers execute around opcode batches/APU calls, not every emulated
  instruction. Cumulative PROF A/B/C records are emitted once per existing
  telemetry interval. Match records by `f`; discard incomplete records and old
  buffered output. Differences of complete snapshots provide each interval.
- Input, audio queue/gain/effects, stock LCD setup/divider, frame skipping and
  RTOS scheduling are unchanged. Profiler never runs from an ISR or USB task.

## Build and validation

Application212816 bytes, SHA256:
`4f09b89c3fe791081675990f260b5c6645b775c833afa35a12d7e9b281926bec`.
Full1MiB image SHA256:
`2c8f0dc94ceb72dd12f7e151766b06c8868c2b3242a937bc2c0a1181b1d4b763`.

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --nes-smb1 --keyscan-dma2 --nes-input-recovery --keyscan-irq --keyscan-paced --nes-audio-priority --nes-live-fx --nes-channel-fx --nes-render-skip --nes-profile --out firmware/usb-diag/build/nes-smb1-profile
```

104 tests passed:27 instrumented NES host tests,20 USB/peripheral lifecycle
tests,28 ELF/image positive/negative audits,29 broker/writer tests. The900-frame
SMB1 equivalence run compares all CPU/PPU/APU state and audio, plus every
presented pixel, with timing disabled/enabled and full/hidden rendering.
Profile unit tests cover exclusive nesting/sums, snapshots, reset, disabled
clock, invalid scope, and unsigned counter rollover.

Static boot audit first rejected a merged-global relocation caused by added
state. Reviewed disassembly: `ota_status=0x1c1a5f0`, trace=`0x1c1a744`, delta340;
store at0x2003370 is `d1 ec 07 15` (`[++r0=340]=r1`). Added that exact encoding
to the allowlist. Full46-byte wrapper,22-byte marker, call targets and negative
corruption tests still pass. No boot sequence was altered or audit bypassed.

## Deployment and recovery evidence

Active helper: `C:\Program Files\FM1FlashSession-3edce9be5079421c8d72bca4118957af`,
PID20664, trusted-local existing Python, no idle expiry. No new UAC/environment
or writer. Ordinary `-Flash` cycle used because prior recovery had succeeded.

53 sectors (0x5000..0x38000, then directory0x4000) were programmed/sector-checked;
one complete1MiB read matched. No preliminary device dump. Existing CLI reset
was sent once; its Windows UTF-8 log-decoding error recurred. COM10 identity,
advancing frames and zero faults resolved the reset outcome without resending.
Session state is idle/unblocked, reset_pending=false, needs_observation=false.

Durable copy: `../../private-backups/nes-profile-write-20260929-190135/`.

- `runs/f8d71cd284da431ebcd00a65c8169f82/serial.log`: preflash capture (old
  buffered startup records precede fresh data; use final contiguous interval).
- `runs/5a775ab3eff748798f761379bdcee91a/`: verified deployment receipt,
  sector logs and `final-1.bin` (the new verified baseline).
- `runs/2bd6010ca8e14ec68ebdf3930b2d2247/serial.log`: initial post-reset profile.
- `runs/17aa27ae41db49be8b839ae481305ce3/serial.log`: second capture used above.
- `state.json`, `session.json`, snapshot and all run logs retained.

Source rollback: `../../private-backups/nes-profile-source-20260929-185029/`.
Previous verified render-skip image4e84e7ef... remains preserved in
`../../private-backups/nes-render-skip-retry-20260929-182551/runs/ad071d8b4d6045628356bfcc884067df/final-1.bin`.
