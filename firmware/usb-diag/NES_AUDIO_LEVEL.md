# NES output level correction — 2026-09-29

User reports working audio but very low volume. Inspection found gain4 on
the DC-blocked unsigned8 mix feeding PCM16. Even arbitrary0..255 input was
limited to1020/32768 before the near-unity startup ramp; the four-voice mix
is only0..109. This was excessive digital attenuation, not evidence of a
DAC/analog fault. No DAC register, audio clock, IRQ, buffer or ramp changed.

Set game gain128 (+30.10dB nominal over4); extend board and channel-FX
validation to0..128. Default board mute remains0. The DC estimate is a
convex update within the input bounds. Worst-case absolute PCM is at most
255*128=32640, so arbitrary8-bit input cannot wrap signed16. For the real
four-voice mix, the bound is109*128=13952. Effects still saturate rather
than wrapping. Voice DC products and accumulators remain within int32.
Gain129 is rejected atomically. Startup mute/ramp is retained. The original
claim here that the physical volume knob was retained was incorrect: this
fixed-gain image did not read it. Subsequent stock analysis proves PB6/ADC4
controls digital volume; see `NES_VOLUME_INPUT.md` for the new implementation.

115 checks pass (NES29, USB25, image32, broker29). Extended tests cover all
gains0..128 in steps of4 against exact reference math, worst-case0/255
steps at128, and4096 blocks with all effect banks and high resonance/depth.
SMB1 test starts gameplay at frame120 and measures frames180..599:
308700 samples, peak212->6799, RMS30.82->995.35 PCM16 units, +30.18dB RMS,
zero clipped samples. Difference from exact32x old integer output is at
most31LSBs (old output rounding); CPU/PPU/audio sample sequence unchanged.
These are deterministic host measurements, not analog voltage readings.

Build `firmware/usb-diag/build/nes-smb1-tiles-volume128`,214352 bytes.
Application SHA256:
`8821ce5a39aad5e27559d727fd36498ee9da6cfca01bd1d1041e72e748b61903`.
Full image SHA256:
`28e2aefaf95514c21e8723e7f7bf0dfc03d86c48b9343363e395d2422cbc1536`.
Source rollback `private-backups/nes-volume-source-20260929/`.
Previous working tile image `private-backups/nes-tiles-write-20260929/`.
30MHz/RGB444/direct packing/tile optimization and profiling-off retained.

Deployment succeeded first attempt through the existing persistent helper:
run `3b563468042148eca2da89aeb05a4ebf`,25 changed sectors verified, directory
last, one full1MiB readback matching28e2aefa... above. No pre-read or new
environment. Reset sent once; known reset-log decoding exception resolved
by COM10 HELLO and advancing frames, without resending reset.

Observation `93cc1ba11dec451daff339112a90545e`: NES TIME4130->14140ms,
frames204->805, LCD done108->436, yielding32.77 LCD FPS and60.04 emulated
FPS. Zero recorded underruns/rebuffers, LCD errors and NES faults through
14.14s. Final queue2747 samples; initial1024 priming-silence samples are
not underruns. Physical volume and listening quality require user listening;
the measured +30.18dB is host PCM, not a calibrated analog measurement.

Durable write/readback/reset/observation evidence and state receipts:
`private-backups/nes-volume-write-20260929/`. Helper is idle/unblocked,
reset_pending=false, needs_observation=false; louder firmware left running.
