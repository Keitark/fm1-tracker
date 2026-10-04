# Asynchronous NES LCD transfer

Opt-in build: `--lcd-async` alongside the existing render-skip/profile flags.
Implemented, flashed and measured 2026-09-29. Physical image/audio acceptance
still requires the user's observation; serial is not a camera or microphone.

## Approach and ownership

Inspired by the queued DMA buffer ownership in
[Retro-Go's display driver](https://github.com/ducalex/retro-go/blob/master/components/retro-go/drivers/display/ili9341.h).
This is an FM-1 implementation, not an ESP32 driver transplant. Stock PA2/GPIO,
panel commands, startup black fill, RGB565 byte order, zero row origin and SPI
divider 4 remain unchanged. No PLL, flash, input, audio or USB protocol changes.

Two aligned 115200-byte buffers in internal SRAM hold complete scaled pictures.
The emulation task reserves FREE -> BUILD, copies all 240 rows, then publishes
PENDING. At most one immutable ACTIVE picture and one PENDING picture exist.
SPI1 IRQ16, priority2 on CPU0, completes 30 existing-size 3840-byte transfers;
only after the last completion does ACTIVE become FREE. The ISR has no peripheral
completion polling, pixel copies, logging or RTOS calls. Metadata uses the existing
SDK local-IRQ-save plus cross-core spinlock pattern; copying occurs unlocked.

When no buffer is free, the whole presentation is skipped before pixel
rendering. CPU/APU/input still execute. Audio's five-frame skip policy remains,
but display backpressure can extend a visual skip without waiting or overwriting
DMA storage. The pending picture starts from task context at frame boundaries.
Only the small window commands use the bounded synchronous driver. A 250ms
no-completion-progress deadline aborts DMA and ends emulation with a diagnostic.
STOP/failure masks IRQ16, disables SPI before releasing ownership, then unregisters
the interrupt. The usual CDC STOP/UBOOT recovery protocol is retained.

`shown` now means submitted snapshots; use `LCD ASYNC done` for complete wire
transfers. `busy` counts admission skips, `irq` completion events, and `bytes`
completed bytes. `err` counts progress timeouts. `a/p` show active/pending slots.
The profile's LCD bucket is now task-side submission cost, NOT elapsed wire time;
interrupt time remains attributed to whichever task bucket was interrupted.

## Dual-core boundary

The pinned SDK defines CPU_CORE_NUM=2, links cpu1_start/cpu1_main, and supports
task-name affinity prefixes (its sample uses `#C0usb_msd0`). Existing application
tasks are not explicitly pinned. This revision does not change their affinity
or create a second-core display worker: DMA already overlaps emulation without
occupying either CPU in a pixel-transfer wait. `cpu` reports the core executing
that NES status sample, not per-core utilization. A later core-affinity change
should be measured independently rather than assumed to double performance.

## Validation and rollback

New actual-driver host coverage: immutable active/pending snapshots, caller
buffer reuse, full two-buffer backpressure, concurrent build/completion, exact
wire pixels, 30 IRQs/frame, command isolation, missing-completion timeout, invalid
row order, and STOP during DMA. Board tests compare async RGB565 conversion
against the synchronous reference and exercise admission/skip/fault behavior.
ELF audit checks the ISR interrupt return and 230400 aligned BSS bytes, with at
least 128KiB heap reserve. Current initial link leaves 169644 heap bytes.

Source/build rollback: `private-backups/nes-async-source-20260929-191019/`.
Previously verified full image: `private-backups/nes-profile-write-20260929-190135/runs/5a775ab3eff748798f761379bdcee91a/final-1.bin`.
Use only the existing helper/session/writer; no new environment or writer.
One full post-write readback, no full pre-read, reset once, then serial observation.

## Measured outcome

107 local checks passed: 27 NES/core/board, 21 USB/peripheral/display, 30 linked
image/audit, and 29 existing broker/writer tests. No vendor SDK/core edits.

Application: 214096 bytes, SHA-256
`39d6600e9e5bf81824da7b00d15413ed5177e6edd34c75899300d2c60c5c86b7`.
Full verified image:
`2eed7c8f0296c1c78a04a52e2a5ae18388b873e3340492409e58e9c7d6c8d7b8`.
53 sectors 0x5000..0x38000 then directory0x4000 written/checked, one full 1MiB
readback matched. Existing reset log decoder reported its known UTF-8/CP932
error after sending reset; one serial observation confirmed reboot and advancing
NES frames, without resending reset. Session is idle/unblocked and left running.

Evidence: `private-backups/nes-async-write-20260929-192138/runs/`:

- `1b9db491ea5e43a08edf0ad13ab7a531/`: deployment receipt and final-1.bin.
- `4c2be54d13434aad857b9a71e60d14e7/serial.log`: initial capture, all reported
  audio underrun/rebuffer/LCD-error/fault counters zero.
- `d822dbdfe8bb4f1aadd43130c9c16e86/serial.log`: repeat capture. Ignore stale
  records queued during serial disconnect; use the final contiguous interval.

Fresh interval: frames3753->4355; LCD completed693->805;
profile wall124976->145067 (500us units) =10.0455s.

| Metric | Synchronous baseline | Async measured |
|---|---:|---:|
| Emulated frames/s | 35.94 | 59.93 |
| Completed LCD frames/s | 5.99 | 11.15 |
| Missing audio frames/s | 17707 | 0 |
| LCD task-side wall time | 51.66% | 0.319% |

Baseline is the prior profile capture documented in NES_PROFILE.md, not an
identical scripted scene A/B. Do not interpret task-side LCD time as wire time.
Async CPU/APU/PPU/video/FX/PCM/frame buckets are respectively 53.98/1.00/18.22/
2.63/5.55/0.27/18.02 percent. The frame bucket now includes normal DAC pacing.
At the final status (73.30s firmware time): zero total underruns, zero rebuffers,
zero LCD timeout errors, zero NES faults, 1949 queued audio samples. Initial
prefill silence1024 samples is separately counted, not hidden as an underrun.
Key scanner continues advancing with input errors/retries0.

All fresh sampled NES core IDs were1 except two0 samples, demonstrating task
migration. LCD IRQ is explicitly CPU0. Both cores are available already; this
revision does not claim a pinned-core partition. The remaining ~11fps visual
limit is consistent with full-screen SPI throughput, not slow emulated time.
CPU affinity alone cannot increase that wire bandwidth. Possible next separate
experiments: changed-region transfers, audited faster SPI, or a dedicated
CPU1 producer/CPU0 display partition, with current image retained as rollback.
