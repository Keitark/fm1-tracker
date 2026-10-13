# FM1 Forge clock bench — 2026-10-12

The physical FM1 completed three deterministic CPU-workload runs at its preserved
SDK boot clock of **360 MHz**, then three at the opt-in **320 MHz** preset. Every
run completed 16,777,216 iterations with checksum `219bcd44`. The final run of
each group also exercised full-duplex USB audio. Both groups retained advancing
LCD, scanner/ADC and DAC counters with no new peripheral or USB errors.

This establishes a bounded running-device check of the 320 MHz candidate, not
long-term qualification, audible output quality or independently measured PLL
frequency. Firmware continues to report `qualified=0`. 320 MHz is a reduction from
this device's observed default, and took longer in this workload. The original
MOD firmware was restored and its serial boot/counters verified afterward.

## Configuration and measurement

Both tested candidates used the same source closure, asset-free diagnostics
module, all five user HALs enabled, `stock-dma` RGB565 LCD, CDC/UBOOT, stereo UAC1,
44.1 kHz DAC and the same bounded integer workload. Only the compile-time clock
preset changed. Pinned SDK commit:
`e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`.

`FORGE CLOCK` reported these fresh values before and after every run:

| Preset | SYS Hz | HSB Hz | LSB Hz | SFC Hz | Clock fault |
|---|---:|---:|---:|---:|---:|
| `sdk-default` |360,000,000|180,000,000|60,000,000|90,000,000|0|
| `sdk-320` |320,000,000|160,000,000|53,333,333|80,000,000|0|

`FORGE BENCH START` queues work on the foreground peripheral task. The workload
yields between bounded slices while IRQs and USB/scanner/LCD/DAC work continue.
`active_ticks` sums the sampled work intervals, including interrupt preemption
and timer-call overhead. `wall_ticks` includes task yield/scheduling gaps. Both
use the SDK's 500 µs timer ticks; the timer calibration was not checked against an
external reference. Active-time quantization uncertainty is up to 500 µs per batch,
shown as the aggregate bound below. These values are not pure CPU time or a
device CPU utilization percentage.

| Preset/run | Host UAC stream | Active ticks | Active seconds | Wall ticks | Wall seconds | Batches | Active quantization bound, seconds |
|---|---|---:|---:|---:|---:|---:|---:|
|360 MHz / 1|Closed|3,614|1.8070|6,554|3.2770|256|0.1280|
|360 MHz / 2|Closed|3,615|1.8075|6,588|3.2940|256|0.1280|
|360 MHz / 3|Duplex|4,027|2.0135|6,623|3.3115|259|0.1295|
|320 MHz / 1|Closed|4,244|2.1220|6,624|3.3120|266|0.1330|
|320 MHz / 2|Closed|4,242|2.1210|6,621|3.3105|266|0.1330|
|320 MHz / 3|Duplex|4,674|2.3370|7,304|3.6520|293|0.1465|

All six final states were `complete`, `fault=0`, `queued=0`, `cancel=0`,
`done=total=16777216`, x=`db418d78`, acc=`d1593f35`, and
checksum=expected=`219bcd44`. The two closed-stream runs are the direct baseline
comparison; the third deliberately adds a different USB workload. Do not combine
all three as repeated measurements under one identical load.

## Peripheral and USB observations

All snapshots reported `ready=1`, `running=1`, zero clock/audio/LCD/runtime
faults, and zero scanner/ADC/LCD error increments. LCD completions increased by 45
per snapshot interval at 360 MHz and 41 at 320 MHz. Published LCD FPS snapshots were
11–12 fps and 10 fps respectively; this is retained-driver telemetry, not an
external screen measurement. DAC audio frames, scanner scans and ADC samples
advanced in every run.

The 360 MHz baseline retained historical encoder invalid-transition counts
`0,3,11,12,5,1,0`; none increased during the benchmark. The 320 MHz boot counters
were zero and stayed zero. No claim that every physical key/knob was retested is
made by this workload.

The final run at each clock opened the FM1 FW Factory WASAPI input and output
together at 48 kHz, stereo float32 host samples. The host supplied precomputed
440/660 Hz tones with peak 0.005. Device UAC remains 48 kHz stereo PCM16. A two-second
warm-up preceded the telemetry snapshot; the streams stayed open through the
CPU benchmark and after-snapshot, then closed.

| Observation during final-run snapshot interval |360 MHz|320 MHz|
|---|---:|---:|
| USB RX packets advanced |4,069|4,069|
| USB TX packets advanced |4,069|4,069|
| Capture submissions advanced |4,069|4,069|
| Bad USB packets / short writes / transport busy increments |0 /0 /0|0 /0 /0|
| Playback/capture FIFO underrun increments |0 /0|0 /0|
| Playback/capture FIFO overrun increments |0 /0|0 /0|
| Host input/output underflow/overflow flags |All0|All0|
| Preallocated host capture capacity exceeded, frames |0|0|
| Total host-stream duration, seconds |6.313|6.312|
| Captured stereo frames |300,480|300,480|

Both capture RMS channels were 0.0 with the device note idle (`note=255`). This
confirms active transport counters and silence under that source state; it does
not verify an audible key tone, analog speaker output or audio fidelity. Host
callback flags and device FIFO/transport counters are separate observations.

## Guard correction and protected deployment

The first default-clock candidate reported the preserved 360/180/60/90 MHz tuple,
but the previous guard incorrectly applied the 320 MHz application-preset limits
(HSB<=160MHz, SFC<=80MHz) to it. It reported `fault=-82` and prevented audio/input
startup. The failed observation and its protected session latch were retained;
no latch was edited or cleared.

The correction keeps `sdk-default` free of clock writes and validates positive,
ordered reports plus the LSB bound. The opt-in 320 MHz path retains the stricter
application-preset bounds and exact SYS readback check. The corrected candidates
were built again and independently staged in a new immutable protected session
seeded from the first candidate's exact verified full readback. Both corrected
boots passed serial observation before the workload was run.

| Tested candidate | Application bytes | Application SHA256 | Full 1 MiB readback SHA256 |
|---|---:|---|---|
| `forge-benchmark-default-guardfix-20261012` |149,904|`7bd4adb57ae347ca8a85751642c9a8bb84cb3b8d415998cdca105aadf26df63d`|`c239d59da41e187e5f04c579fe72551a8996c2c867f203d9a4acbd83ce545bde`|
| `forge-benchmark-sdk320-guardfix-20261012` |151,696|`72ccfdade052871d95177b5a785572a8a860da7714c7f79cb62e351acc5cd5c5`|`7b44f2d3dbae28fc11b75c057c9e857dbffa5f7dd18cc8774266aafd6911aa53`|

Both build directories under `firmware/usb-diag/build/` contain the unchanged
offline `build-manifest.json` and passing `static-audit.json`. Their original
`flashable=false` / `hardware_boot_verified=false` build labels are not rewritten
to imply acceptance; separate protected receipts and live telemetry establish
the later write/readback and bounded running-device evidence.

Each protected deployment used the owner's existing policy of reusing a
completed verified baseline, without further full or sector pre-write reads.
Receipts explicitly record `reuse_completed_read_no_further_prewrite_reads`.
Each deployment completed sector write verification and one exact full 1 MiB final
readback. No new double-read preflight was performed or claimed here.

Local protected receipt root:
`C:\Program Files\FM1FlashSession-ed00f7de39b6492993ff56ebd2191f08\runs\`.

| Operation | Flash/deployment run | Reset run | Serial-observation run |
|---|---|---|---|
| Corrected default |`41cca4f8ebc9491bb16756e7229a988a`|`5e58959265c4464e852b7939fb1ff1b3`|`b9f4b62512e747ffb4ba9cb277ee4ff1`|
|320 MHz |`8bdbb3e916b64d6bbedcdd1ff683afee`|`2c43446cb23a406fb6da0dca0e7e67b5`|`7d57edfe7c8c4334a15bbf06218831b0`|
| Restore original MOD |`c2ba35e0313945b8b62ff6ba17b2ef55`|`bda90f6bccd143a180f053aeceb691dd`|`2cf94275a766458cb55c279634b2c8e9`|

The original MOD firmware was restored with exact full-image/readback SHA256
`7b4c8076c8a1fd03f296e821c6a164b5a01e21acfaacc643169f17a1c2e75693`.
Post-restore observation confirmed `MOD-EDITOR/1` on COM4, audio frames advancing
from 1,113,920 to 1,246,272 and underruns remaining 0. Physical sound/display
acceptance was not claimed. The device was not left running the benchmark image.

## Reproduction and evidence

The sole device-I/O owner ran:

```powershell
python tools/jieli-wl82/benchmark_forge_clock.py --port COM4 --out tmp/forge-default-benchmark-20261012 --runs 3 --uac
python tools/jieli-wl82/benchmark_forge_clock.py --port COM4 --out tmp/forge-sdk320-benchmark-20261012 --runs 3 --uac
```

Those commands require the corresponding reviewed candidate already running;
they do not build, flash, reset, enter UBOOT, change clocks or stop peripherals.
Fresh repetitions require new output directories. Raw `serial.log` and
`report.json` remain local in those directories. JSON report SHA256 values:

- Default: `899ad4d0360a86672e8834eed8261612cec74c1e6ae5078cfe5c086d99c3bd9f`.
- 320 MHz: `0812020c63cda1eb0df24a7c0632d219ef8819c5e1075907e29b9f29d107567f`.

The host runner's six offline tests cover full-count/checksum/fault/timer-contract
rejection, exact CDC VID/PID selection, unambiguous WASAPI endpoint selection,
FIFO pairs and uint32 counter wrap. Both runner files passed `py_compile`.
The target host suites cover default-clock preservation and 320 MHz guard
readback, bounded foreground execution, queued cancellation, checksum/timer-wrap
behavior and IRQ/lock availability. Project tests validate config-to-build
translation. Linked static audits, protected receipts and live results above
remain distinct evidence stages.

Long-duration thermal/voltage/timing stability, external clock calibration,
audible output, LCD appearance and a full physical 41-key/seven-encoder acceptance
run remain outside this bounded benchmark. 320 MHz stays opt-in and experimental.
