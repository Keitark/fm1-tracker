# RGB444 at nominal 30 MHz

2026-09-29. User requested a 30 MHz bench test. This profile changes only
the idle SPI1 streaming divider from3 to1. Stock initialization, black fill,
display-on and RGB444 format selection still run at BAUD4. The existing
60MHz LSB guard is shared by both fast profiles; no PLL/CPU/LSB clock changes.
Nominal SPI =60MHz/(1+1)=30MHz, not an oscilloscope measurement.

The `--lcd-spi30` flag requires asynchronous LCD and is mutually exclusive
with `--lcd-spi15`. Existing defaults and15MHz builds remain supported.
Two packed86400-byte RGB444 frames, 2880-byte strips, input and audio are
unchanged. At30MHz the payload-only limit is43.40 full240x240 frames/s;
actual throughput includes preparation, scheduling and transfer gaps.
The panel variant and higher-speed signal margin are still bench-unverified.

## Validation

110 checks pass: NES/core/board27, USB/peripheral/display24, linked image30,
broker/writer29. The new actual-driver host target checks stockBAUD4 then
idleBAUD1, unexpected80MHz source rejection, all RGB565 colors in both packed
RGB444 pixel positions, ownership, backpressure, timeout and stop. Existing
15MHz/RGB565/RGB444 targets still pass. Static boot/power/ISR/buffer audits pass.

Build: `firmware/usb-diag/build/nes-smb1-rgb444-spi30`.
Same preceding RGB444 build flags, replacing `--lcd-spi15` with `--lcd-spi30`.
Application214256 bytes, SHA256
`5150ff721930e5a3f823b9223df28fa2439f305ebf8418d272c3eb4b0318bdb4`.
Candidate full image SHA256
`29dabeb09415aaad7e7179ac8845d6ef09b00b052e48e81f01fa16f7add1b532`.
Existing helper/writer only; no full pre-read, one full final verification,
directory sector last, one reset followed by serial observation.

## Rollback

Source snapshot: `private-backups/nes-spi30-source-20260929-195357/`.
Verified15MHz RGB444 full image:
`private-backups/nes-rgb444-write-20260929-1950/runs/e4bfbddb50744afc93f18d160e479aad/final-1.bin`.
SHA256 `56ffb10fad321165bdc0dfaa51408d830e655e7d43dba7cd87b0c0fb337f4137`.
Its previous capture measured18.35 completed LCD FPS /59.94 emulated FPS.

Deployment and physical acceptance are not inferred from build success.

## Verified deployment and result

Run `dd423772de66409c93184dce5f10fa2d` succeeded first attempt:42 changed
sectors including directory0x4000 last, one full1MiB readback matching the
candidate hash above. Existing jluboottool writer and elevated helper used.
Reset sent once; the known reset-log UTF-8/CP932 decoder error recurred, then
COM10 HELLO and advancing NES frames resolved it without another reset.
Helper remains active, idle/unblocked, reset_pending=false, needs_observation=false.

Durable evidence: `private-backups/nes-spi30-write-20260929-1957/`.
Post-reset capture: `runs/4661d568e9eb4a8ebcce94b8e8397dd6/serial.log`.
Measured interval frames202->804, LCD completed71->269, profile wall6695->26750
ticks of500us =10.0275s. Thus60.035 emulated FPS and19.7457 LCD FPS,
about7.6% higher than the prior15MHz RGB444 capture18.3523 FPS. These are
separate captures, not a controlled identical-input benchmark. All captured
audio underrun/rebuffer/LCD-error/NES-fault counters remain0 through14.14s
firmware time; input reports errors0/retries0. Telemetry confirms configured
`b=1 bpp=12`; this is not a scope measurement or panel acknowledgement.
Audio queue2076 at final record;1152 priming samples are not underruns.

LCD busy skips reach14 during startup then remain14 throughout the measured
interval, while software-skipped frames rise130->534. LCD pending queue is0
at each snapshot in this interval. Doubling SPI no longer gives near-linear
visible-frame throughput: the producer/audio-priority policy is the next
likely limiter. `fm1_board.c:frame` requests a skipped picture whenever audio
queue<2205, bounded by max_frame_skip5, regardless of LCD buffer availability.
This experiment does not alter that policy. Physical pixels/colors and sound
still require user observation; this short capture is not long-term stability
or electrical-margin qualification.
