# LCD divider-only 15 MHz experiment

2026-09-29. Follow-up to NES_ASYNC_LCD.md. Explicit build option `--lcd-spi15`
requires `--lcd-async`; default and rollback remain divider4/RGB565.

## Scope

Stock initialization, black fill and display-on remain at BAUD4. Before any
asynchronous DMA or IRQ is enabled, require `clk_get("lsb")==60000000`, then
write SPI1 BAUD3. Unexpected LSB reports fail with LCD error-11 without changing
the divider. No PLL, CPU, LSB, audio, scanner or pixel-format changes. NES uses
the same two RGB565 buffers and 3840-byte DMA descriptors. Periodic LCD telemetry
adds `b=3`, the configured divider (not a write-only register readback).

Nominal 60MHz/(3+1)=15MHz, compared with 12MHz at BAUD4. These are source-clock
calculations, not oscilloscope measurements. Exact LCD controller remains
unidentified: stock protocol is ST7789-like. The ST7789V v1.2 datasheet's 66ns
minimum serial write period corresponds to ~15.15MHz, but does not establish
this particular FM-1 panel's identity or electrical qualification. No RGB444
or higher-frequency experiment is included in this image.

## Validation and rollback

108 checks pass: NES/core/board27, USB/peripheral/display22, linked image30,
broker/writer29. Added actual-driver host test verifies initial stock BAUD4,
the idle switch to BAUD3, unchanged pixels/ownership/timeout/STOP behavior,
and rejection of an unexpected80MHz source. Existing boot/power/ISR/DMA audits
pass. Source changes do not touch vendor repositories or flashing tools.

Source rollback: `private-backups/nes-spi15-source-20260929-193200/`.
Verified 12MHz rollback image:
`private-backups/nes-async-write-20260929-192138/runs/1b9db491ea5e43a08edf0ad13ab7a531/final-1.bin`.
Rollback SHA256: `2eed7c8f0296c1c78a04a52e2a5ae18388b873e3340492409e58e9c7d6c8d7b8`.

Candidate application214160 bytes, SHA256
`c57e4e2f2b9fcc06595ff4a2862bb2acfd4b487059502f2e02a47135cf66a2ef`.
Packaged image SHA256
`b848335f165fb334fa79046745ea2be76eda315907662661e60f280f46786278`.

## Baseline capture

Existing helper, `runs/0a66ec7f5e1e410fa9f6ff78fb903676/serial.log`:
fresh final interval frames45872->46473, LCD done8483->8595,
profile wall1528397->1548447 ticks of500us: 10.025s. This is60.00 emulated
frames/s and11.17 LCD updates/s. Total underruns/rebuffers/LCD-errors/faults0
through774.99s firmware time. Initial1024-sample priming silence unchanged.
Stale buffered records at the start of the capture are excluded.

Image correctness and electrical margin cannot be proven from SPI completion
counters alone; physical screen/audio acceptance still needs user observation.

Initial attempt `runs/62965638786246f294c6196166ce0a1a/` wrote/verified sector
0x5000, then encountered WinError31 during the erased-sector read check at
0x6000, before programming that sector. The coordinator stopped without reset.
The matching WL82 PNP identity remained present. After inspecting the log, one
explicit same-image `-Flash -Retry` was issued through the existing helper;
this reloads the existing RAM helper and rewrites the entire original scope.
This failure happened in UBOOT before the new LCD clock code could execute.
Failed-attempt evidence is retained; no writer/environment changes were made.

## Verified deployment and measured result

Retry `runs/23fdf45adbb5492387f0fca02b2d9974/` successfully programmed and
verified all51 requested sectors, directory0x4000 last. The single full1MiB
readback matches `b848335f...` above. No preliminary full device dump. The
existing reset was issued once; its known UTF-8/CP932 log decoder error recurred,
then COM10 HELLO and advancing NES frames resolved reset without a second reset.
Helper PID20664 remains running; state idle/unblocked, reset_pending=false,
needs_observation=false. No new UAC/environment/writer was created.

Durable evidence: `private-backups/nes-spi15-write-20260929-193657/`, containing
the baseline capture, failed attempt, successful retry/final-1.bin and the
post-reset capture `runs/dcac13087299432aa0332a2e36a2f2e0/serial.log`.

Measured interval: frames202->807; LCD done47->191; profile wall6689->26847
ticks of500us, or10.079s. This gives60.026 emulated frames/s and14.287 completed
LCD frames/s, versus the fresh baseline60.00 and11.17. LCD throughput improves
27.9% in this interval; not an identical scripted game-state benchmark.
All captured underrun/rebuffer/LCD-timeout/NES-fault counters remain0 through
14.19s firmware time. Audio queued2041 samples at the last record; initial
1088-sample prefill silence is reported separately. Input errors/retries0,
key scan and DMA completion counters advance. Telemetry reports `b=3`; the
successful guarded startup also confirms the SDK reported60MHz LSB then.
No physical SCLK measurement or visual pixel-integrity claim is made.

RGB444, higher SPI clocks and changes to CPU affinity are not included.
