# NES paced input experiment, 2026-09-29

Implemented, flashed, reset, and observed. **Interrupt reduction succeeded;
FPS improvement did not. This build is currently installed, not rolled back.**

## Change

`--keyscan-paced` requires `--keyscan-irq`. It keeps the stock SPI ordering,
divider, eleven-row pipeline, initial discarded sweep and coherent snapshots.
After each sweep it clears pending, leaves the latch high and does not arm DMA.
An independent 1000us SDK timer resumes one sweep. Overlapping kicks are inert.
Held-key caching/debounce and the task-side 10ms no-progress watchdog remain.
The latter also detects a lost timer; normal 1ms idle is not treated as failure.

Encoder decoding runs once per new snapshot in the timer callback, outside the
SPI completion ISR. It is still interrupt context, not a worker task. Seven
bounded updates run under the existing input lock. Timer shutdown disables
access before deletion; late callbacks and failed allocation are tested.
There is no new delay in the NES frame loop. ISR/hot code remains in flash for
this pacing experiment; RAM relocation has NOT been implemented.

## Observed result

Effects were BYPASS for both captures. Nominal DAC time (audio-frame delta /
44100) provides the same time reference for both, because USB heartbeat lines
and NES records are asynchronous. Rates below are approximate; game scenes are
not deterministic/cycle-matched across the reset.

| Metric | Continuous baseline | Paced 1ms |
|---|---:|---:|
| Emulated FPS | 26.67 | 22.97 |
| LCD presentations/sec | 4.53 | 3.83 |
| SPI completion IRQ/sec | 111257 | 10995 |
| Full scans/sec | 10114 | 1000 |
| Missing audio frames/sec | 24498 | 27220 |

The first postboot captures were approximately 21 FPS; the later interval was
approximately 23 FPS. This is not a performance win: scan IRQs fell about 90%,
but the selected later FPS comparison fell about 14% and audio underruns rose.
Serial confirmed advancing frames through >140s uptime, fault=0, and input
ACTIVE/errors=0/retries=0. Physical LCD, key, knob, and audio quality were not
independently observed. A lower IRQ count is not proof of lower overall cost.

Disassembly confirms the SDK usec timer uses TIMER4, registers IRQ62 at priority5
on CPU1, and takes SDK locks before calling the callback. The original SPI ISR
uses IRQ37 on CPU0. This cross-core/timer/cache behavior is a profiling lead,
NOT an established explanation for the regression. Do not claim flash or the
timer is the cause without a controlled measurement. RAM hot-path relocation
and same-scene CPU timing are possible next experiments, not completed work.

## Build, verification and recovery evidence

Build profile: `build/nes-smb1-paced-1ms`.
App210192 bytes, SHA256:
`1180d28df76acb32ebc3d08b7a77e0eddedc82fbcfc263f30b8ce2c7a3b733dc`.
Full image SHA256:
`9097a0746af9a53135d047f8b5a21bc46b62e9d452ce82da4d31b3466eb8632e`.
Static boot audit passed. Four scanner model tests and all19 USB/peripheral
tests passed. All21 current broker/writer tests passed, including a final-read
mismatch stopping without reset and exactly one full post-write read.
Older-profile regression suites:15 of22 tests passed; seven are blocked by
their old build manifests rejecting changed shared sources. No stale-source
guard was disabled and no old image was rebuilt/flashed for these tests.

Existing jluboottool.py writer programmed52 changed sectors, directory last,
then **one** full1MiB read matched exactly. No pre-write full dump. Reset was
sent once. Its known USB disconnect/CP932 logging error was resolved by serial
HELLO and advancing NES frames, not by retrying reset. Session is idle/unblocked.

Evidence root: `../../private-backups/nes-paced-1ms-write-20260929-1729/`.
`comparison.json` includes raw endpoint counters, heartbeat-derived estimates,
and nominal-DAC-derived rates. Saved runs:

- `968e6acce41a42029fce879ab39c57d6/serial.log`: preflash continuous baseline.
- `9b16165222164b83b54953f93894a95c/`: write logs, receipt, `final-1.bin`.
- `4e2278887507456f992dbd7a1c6b97eb/serial.log`: first boot observation.
- `6aea096bc2574ff8b2e4de5441ae93ac/serial.log`: second observation.
- `160483d88f064290b6f8fbc66c86abf9/serial.log`: later comparison interval.

Source rollback: `../../private-backups/nes-paced-scan-source-20260929-171923/`.
Previous full image remains saved at
`../../private-backups/nes-channel-fx-write-20260929-161548/flash/final-2.bin`.
It is rollback evidence, not the currently installed baseline.

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --nes-smb1 --keyscan-dma2 --nes-input-recovery --keyscan-irq --keyscan-paced --nes-audio-priority --nes-live-fx --nes-channel-fx --out firmware/usb-diag/build/nes-smb1-paced-1ms
```
