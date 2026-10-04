# Per-channel NES effects — prepared 2026-09-29 JST

Implemented, built and packaged; NOT flashed or hardware-qualified.
Profile: `build/nes-smb1-channel-fx`. Gain remains4; audio-priority scheduling,
IRQ input and the previous live filter/LFO implementation are retained.

## Controls and routing

SELECT now chooses **Pulse1 / Pulse2 / Triangle / Noise / All** instead of
dry/wet mix. Selection starts at All, clamps at either end, and moves one bank
per four net encoder edges. Turn the opposite direction if already at the end.
Physical clockwise direction and detent sensitivity remain bench-unverified.

- KNOB1: cutoff80Hz–5kHz for the selected bank.
- KNOB2: resonance for that bank.
- KNOB3: triangle LFO rate0.1–12.8Hz for that bank.
- KNOB4: cutoff LFO depth for that bank.
- ALGORITHM: bypass / LP / BP / HP for that bank.
- PRESETS: dry / warm / wah / resonant sweep for that bank only.

All five banks retain their settings and independent filter/LFO state while
switching selection. All is a separate post-mix master effect, NOT a command to
overwrite every voice's settings. Selection alone does not reset or change the
sound. Every bank starts in bypass; select a mode/preset to hear an effect.
Settings are RAM-only and reset on a new NES run. Wet mix remains part of each
preset but has no dedicated physical knob in this layout; bypass gives dry.
Simultaneous SELECT/parameter deltas in one task snapshot apply selection first.

Routing: four independent raw NES voices -> per-voice DC/weighting and effects
-> sum -> All/master effect -> existing audio queue -> DAC. Stereo output still
duplicates mono; these are NES synthesis voices, not left/right channels.
The core does not generate DMC/sample audio, so no DMC bank is advertised.
The combined BGM and sound-effect material using any voice shares its settings.

Serial status adds `FX channel=PULSE1|PULSE2|TRIANGLE|NOISE|ALL mode=...` with
the selected bank's parameter values. No game-screen overlay is added.

## Integration and verification

The pinned `references/source/peak-nes` checkout remains clean/unmodified.
The existing `vendor_overlay.py` generates an exact-anchor build copy of
`nes_apu.c`, capturing four0..15 voices before the existing mixer. The target
manifest hashes both original and generated sources. No APU oscillator/timer,
envelope, noise or mixer weights are changed. Capture buffers add2940 B static
RAM and the five-bank processor is256 B. Host builds use the same overlay.

An optional synchronous channel callback replaces the mono callback only in
the channel profile. Otherwise it falls back to the original mono path. Audio
statistics increment once, and callback failures retain the normal stop path.
For exact bypass, the processor retains original mixed-byte DC/gain rounding
and adds each voice's wet-minus-dry correction before the master stage. Voice
filters receive the correctly weighted/DC-removed contribution, not the master
mix. The final sum saturates to PCM16 before the master filter. Processing is
task-side in blocks of at most64 samples, once before queue retries; no DSP is
added to either IRQ. Fully dry FX use a block-copy/phase-advance fast path.

Passed:23 NES host suites,18 USB/peripheral host suites,34 linked-image checks
and9 stock-layout preservation checks. Tests cover exact dry PCM, independent
selection/settings, unaffected other-voice contributions, master ordering,
block partition equivalence, real APU mixer reconstruction and fallback,
single callback delivery, failure propagation, retries, STOP and USB recovery.

Real SMB1 host run,600 frames: original mixed hash4096699697 is unchanged;
raw hashes are2419496683 /3379829425 /2611785330 /3788494092. These validate
capture routing, not physical sound quality. Host-only6.4-million-sample bank
microbenchmark: all dry0.080s, one active0.130s, all five active0.386s. Target
CPU cost/FPS and audible latency are unmeasured; enabling multiple filters costs
more CPU and still needs an underrun/rebuffer bench check. Buffer latency and
prior real-time limitations in NES_LIVE_FX.md/NES_CDC_SMB1.md still apply.

## Artifact and flash boundary

### Live deployment2026-09-29

Flashed through the once-elevated existing `jluboottool.py` workflow:52 changed
sectors, application directory last, no preliminary device dump, and two full
1MiB readbacks matched the candidate hash below. One loader reset attempt was
followed by successful COM10 HELLO and advancing NES frames; no power-cycle or
reset retry was needed. Reset USB timeout/CP932 log-decoding warnings were
resolved by observation, not treated as successful boot on their own.

Saved evidence: `../../private-backups/nes-channel-fx-write-20260929-161548/`.
`flash/final-2.bin` is the new verified baseline. Active helper:
`C:\Program Files\FM1FlashSession-dd4b8d8c469245d3ba86df4901280ba7`, PID69584,
idle/unblocked after monitoring (session lifetime must be rechecked).

Monitoring reached uptime79s without a reported fault or input scan error.
At66.830s: frames1878/shown313/underrun1561461/rebuffers377.
At78.830s: frames2202/shown367/underrun1858809/rebuffers472.
That interval is27.0 emulated FPS,4.5 displayed FPS,297348 additional missing
audio frames, and95 rebuffer events. Effects remained in bypass. IRQ input
completions advanced7422649->8773180 (~112544/s); cause of low throughput has
not yet been established. This is a successful flash/boot/liveness test, NOT
acceptable real-time audio/display performance. Physical screen/audio/key/knob
behavior still requires user observation.

Application209968 bytes, SHA-256
`11ddbe796ce034124d5ca51bb349b9811bc0c39868274e0caa502c080f2cfec7`.
Full1MiB candidate SHA-256
`a1b6df96fc5c9d444f39a0e9f7ecd7213d6a49e4cfaac465b484d08ee7822272`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
Offline comparison with the saved verified installed c60b423b... baseline finds
52 changed sectors0x4000..0x37000. Bytes outside those sectors are unchanged.
Application ends0x37550, still inside the stock application slot.

Writer scope reviewed on2026-09-29: final sector0x37000 is inside the original
V14 app slot ending0x92e1c. Repackaging passed all9 preservation checks and
reproduced the exact candidate hash. Comparison with c60b423b... confirms52
sectors0x4000..0x37000; every byte at/after0x37550 and before0x4000 is unchanged.
At the user's request, the per-build cutoff was then removed: the complete
stock app allocation is permitted, with byte-level preservation outside the
application and its two directory records. Config bytes sharing the final
stock app sector are protected too. All20 broker/writer tests pass, including
directory-last and two full readbacks. Writer rollback:
`../../private-backups/channel-fx-writer-scope-20260929-160402/`.
This scope review is offline evidence, not proof of successful hardware boot.
Source rollback: `../../private-backups/nes-channel-fx-source-20260929-145654/`.

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --nes-smb1 --keyscan-dma2 --nes-input-recovery --keyscan-irq --nes-audio-priority --nes-live-fx --nes-channel-fx --out firmware/usb-diag/build/nes-smb1-channel-fx
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/nes-smb1-channel-fx
```
