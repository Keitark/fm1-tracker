# PB6 digital volume implementation — 2026-09-29

Stock assignment: `docs/FM1_STOCK_VOLUME_INPUT_20260929.md`.
New opt-in `--nes-volume` requires paced key scanning and DAC audio priority.
Build-only profile: `nes-smb1-adc-volume`. No vendor checkout changes.

## Reader and gain

`fm1_volume.c` owns only SAR ADC channel4 /PB6. It configures PB6 as input
without digital-input buffers or pulls, preserving other GPIO bits. Analog
bias/test-gate setup follows pinned SDK adc_api.c, preserving unrelated bits.
Conversion uses divider96, delay15, channel4, with ADC interrupt disabled.
No SDK ADC owner may coexist; the ELF audit rejects adc_init/adc_scan/adc_isr.
Startup refuses an already enabled ADC/IRQ instead of stealing ownership.

Every second existing1ms scan-timer callback checks completion, consumes at
most one sample and starts the next. No new timer, ADC interrupt, busy wait,
allocation or logging in this path. Ten incomplete polls (nominal20ms) or
an out-of-range result shuts the reader down and sets target mute. A scanner
restart can restart sampling. Scanner stop clears reader validity/target.

Raw0..1023 is filtered with stock +/-6 deadband, ten consecutive excursions,
and quantization raw>>3. Initial target0 prevents full-volume startup before
acceptance. ADC polling differs from stock's shared battery/reference queue:
it is a dedicated non-blocking PB6 reader, without battery calibration loops,
voltage trim or SDK ADC interrupt.

Keep base NES gain128 and effects unchanged. The audio consumer snapshots
target0..127 under input_lock, then moves its existing startup envelope by
one step per64-sample callback in either direction. The existing gain/128
multiply is reused, with no extra per-sample volume pass. Lock order is
audio->input; input paths never acquire audio_lock. Invalid ADC sets target0
and fades down over at most127 callbacks (about184ms), not instantaneous mute.
Startup retains its original one-second silence. Maximum knob setting
retains the prior gain127/128 output, not an additional boost.

USB telemetry once per second:
`VOLUME adc=4 gpio=PB6 raw=... target=... gain=... valid=... run=... samples=... errors=...`
Existing USB observation commands capture it. No new updater or writer.

## Validation and artifacts

118 checks pass: NES29, USB/display27, linked image33, flash broker29.
Register model tests pin/bit preservation, channel and IRQ settings, no
conversion wait, endpoints, jitter/debounce, timeout/invalid fail-mute,
ownership refusal and stop/restart. Runtime tests exercise the actual paced
callback (20ticks ->10 samples ->512/64), lifecycle teardown and volume
gain reaching queued PCM. Envelope tests cover smooth descent to exact zero
and ascent. Compiled target reader paths were inspected: no polling loop.

Application214928 bytes (+576), SHA256:
`3240f8bd039d9f192362245e9066b017ee937442360693f28dddacc7a416d7f2`.
Full image SHA256:
`c954d3770680ab8443f62c9891441644ca4ff4aaca44f3fbe3cf95eb1e88bb5a`.
Source snapshot: `private-backups/nes-adc-volume-source-20260929/`.
Previous verified image: `private-backups/nes-volume-write-20260929/`.
LCD30MHz/RGB444/direct/tile settings remain unchanged, profiling off.

## Hardware observation (2026-09-29)

Flashed through the existing elevated helper; one full final readback matched
the image SHA above. Reset returned to COM10. First capture completed ADC
conversions at approximately 500/s without ADC, audio or input errors. All
captured raw values were initially zero. LCD throughput over the final 10.020s
was 31.54 fps; emulation was 60.08 fps. The user subsequently reported that
volume worked initially, then the display, keys and volume stopped responding.

The second capture preserved the actual first failure:
`NES FAILURE source=LCD_NATIVE_ROWS rc=-10 frame=1372 lcd=-10 audio=0 q=2179`.
USB heartbeats continued; TEST STATUS reported busy=0/mode=0. Input status
reported errors=0/retries=0. This is a stopped game after LCD submission
failure, not evidence of a CPU deadlock or ADC timeout. Game teardown stops
the input scanner and ADC, explaining why volume and keys then cease updating.

Code inspection found a possible matching buffer-handoff race: service() can
see an active transfer and return; its completion interrupt can then free a
buffer before begin() acquires the allocation lock, while a prior frame is
still pending. begin() can allocate the freed buffer; publishing the new frame
then fails with -10 because the older pending frame was never serviced. The
existing log does not distinguish this guard from row-validation guards, so
the exact branch is not yet confirmed by reproduction.

Failure evidence: `private-backups/nes-adc-volume-write-20260929/baa4a57f0a394655bfd1fbd31e3ad896/serial.log`
and `state-after-failure.json`. No additional firmware write was performed
during this diagnosis. Physical stability acceptance remains FAILED.
