# NES live knob effects — prepared 2026-09-29 JST

Built and packaged, NOT flashed or listening/latency-qualified. Applies to the
NES mono mix, not an external audio input. Starts in exact digital bypass;
gain remains4, as in the audio-priority candidate. The previously discussed
gain128 change is separate and is not implemented here.

## Controls

| Physical control | Function |
|---|---|
| KNOB1 | Logarithmic filter cutoff, approximately80Hz–5kHz |
| KNOB2 | Resonance (damping1.8 down to approximately0.25; no self-oscillation mode) |
| KNOB3 | Triangle LFO rate,0.1–12.8Hz in0.1Hz steps |
| KNOB4 | LFO cutoff modulation depth,0 = off |
| SELECT | Dry/wet mix,0 = dry,127 = wet |
| ALGORITHM | Bypass -> low-pass -> band-pass -> high-pass |
| PRESETS | Dry -> warm low-pass -> slow wah -> resonant band-pass sweep |

ALGORITHM/PRESETS advance per four net decoded contact edges; all other
parameters advance one step per edge. Do not equate this with a verified
physical detent or clockwise direction: those still need bench testing.
Choices clamp at the ends, not wrap. After boot, turn ALGORITHM toward LP
(or PRESETS toward warm/wah); the startup bypass otherwise leaves audio dry.
Serial `FX mode=... cutoff=... res=... lfo_mHz=... depth=... mix=... limited=...`
shows the current task-side controls approximately once per second while
emulation progresses. Cutoff/resonance/depth/mix are0..127 indices, not Hz/dB.
No on-screen overlay is added to Mario.

Modes fade through dry before changing filter topology. Mix moves by1/256 per
sample (~5.8ms for a full fade); mode changes can take a fade-out plus fade-in.
Cutoff and resonance are smoothed per sample. LFO modulates cutoff, not pitch
or game speed. PRESETS recalls controls in RAM only; no flash/save writes.

## Implementation and boundaries

The filter is a digital Chamberlin state-variable design, not an emulation
of a particular analog synthesizer. Its useful synth-style outputs and
high-frequency stability limitation are described in
[Nigel Redmon's design reference](https://www.earlevel.com/main/2003/03/02/the-digital-state-variable-filter/).
The frequency coefficient is tabled as Q12 `2*sin(pi*Fc/44100)` over80..5000Hz,
capped at2857/4096. Resonance controls damping1023..7373 /4096. Q8 audio states
retain detail at the current quiet NES level. Signed64 products avoid overflow;
states are limited to +/-32767*256 and output is bounded PCM16. `limited` counts
internal state limiting operations, not measured DAC overloads. Resonance can
raise level and engage this limiter; analog volume cannot remove digital clipping.

The SPI scan IRQ runs the existing seven-encoder stable-two decoder only when
a COMPLETE11-row sweep is published. No frame-rate subsampling of quadrature
contacts. Decoder work is bounded, under the existing input lock, without DSP,
waits, logging, or acquiring the audio lock. Scanner recovery re-primes contacts
without resetting accumulated counts or effect parameters. The task snapshots
counts, applies clamped deltas, and performs DSP before queue insertion, outside
all locks/IRQs. It processes at most64 samples per call, exactly once even if
the audio queue temporarily rejects the push. DSP has no heap, I/O, or waits.

Audio-priority scheduling, prefill/rebuffer, stereo24 consumer, stock startup
envelope, keyboard-held input, STOP and serial UBOOT are preserved. Effects
are applied before the audio reserve, so that reserve adds approximately33–50ms
of nominal latency, plus task/frame scheduling delay. LFO advances on generated
audio samples; it is not a separate wall-clock oscillator during underruns.
New DSP and scan-decoder CPU costs still need target measurement. Neither the
bounded algorithm nor host tests guarantee uninterrupted real-device playback.

## Validation and artifact

- 21 NES host suites and 17 USB/peripheral host suites pass.
- Exact bypass, block-versus-single-sample equivalence, control limits/presets,
  LFO rate/depth, extreme-input bounds, queue-retry LFO advancement and fast
  quadrature capture without task polling are covered.
- For a480Hz cutoff and2000-peak sine input: LP RMS at200Hz1257.48 vs4kHz21.62;
  HP RMS at50Hz14.83 vs4kHz1492.01. At480Hz, raising resonance produces RMS
  2439.12 vs784.70. These are host numerical tests, not analog measurements.
- 33 linked-image checks and 9 stock-layout preservation checks pass.
- Reviewed startup trace relocation: ota_status0x1c19a40 +324 = trace0x1c19b84,
  writeback instruction `d1 ec 07 14` at0x2003370. Exact wrapper/marker and
  interrupt-return checks remain enabled; no boot-check bypass.

Application208464 bytes, SHA-256
`e8d41399dddf7511d03df8541b5001a1f71caecf1c8edba9be6c227ae2af9f86`.
Full1MiB candidate SHA-256
`0b4153cb645aa6af340a9a48554d06d4192bb33001ecbada3222e9d8b294a9dd`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
Application ends at0x36f70, within the existing writer's last sector0x36000.
No writer/environment changes or hardware reads/writes this turn. Source rollback:
`../../private-backups/nes-live-fx-source-20260929-133350/`.

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --nes-smb1 --keyscan-dma2 --nes-input-recovery --keyscan-irq --nes-audio-priority --nes-live-fx --out firmware/usb-diag/build/nes-smb1-live-fx
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/nes-smb1-live-fx
```

Bench qualification: first confirm unchanged bypass sound and frame/input/USB
liveness. Then select LP/warm, turn each KNOB1..4 individually, try mix and mode
changes, and confirm parameter direction/edge sensitivity. Compare underrun and
rebuffer counters with bypass; listen for control clicks/limiting. Exercise STOP,
restart and UBOOT. Hardware acceptance remains pending.
