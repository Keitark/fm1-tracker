# Runtime audio, ADC and USB findings

Current checkpoint (2026-10-07): Doom `3cf7f681...` is installed and
full-readback verified. The user accepts PCM and master volume. The missing
cold-boot USB interfaces were traced to the programmer cable; switching to a
normal USB data cable restored healthy COM5 and UAC with stage4/fault0 and
zero LCD/key errors. No further firmware fix was proved necessary. Host music
PCM remains exact and unclipped; this is distinct from physical output timing.

## Live sound editor source contract

The NES controls are digital quadrature encoders; the master volume is a
separate ADC input. Reuse `firmware/usb-diag/peripheral_logic.c` and its
`fm1_encoders_capture` completed-snapshot decoder. Capture in the existing
1 ms scanner timer, under the input lock, before the next asynchronous kick.
Copy counts to the task, release the input lock, then take the audio lock to
apply DSP controls. Never nest scanner and audio locks. On scanner recovery,
invalidate the previous contact state but retain accumulated counts.

| Encoder ID | Panel label | A/B matrix contacts | Doom live editor |
| ---: | --- | --- | --- |
| 0 | SELECT | 0x10 / 0x20 | Unassigned |
| 1 | ALGORITHM | 0x30 / 0x40 | Classic / Triangle / Pulse / Mixed band-pass |
| 2 | KNOB1 | 0x91 / 0xa1 | VCO detune |
| 3 | KNOB2 | 0x90 / 0xa0 | VCF cutoff |
| 4 | KNOB3 | 0x70 / 0x80 | VCA attack/release contour |
| 5 | KNOB4 | 0x50 / 0x60 | Reverb wet mix |
| 6 | PRESETS | 0x15 / 0x25 | Original / Warm / Acid / Room |

Matrix high nibble is row, low nibble column, active-low. Selectors use four
net contact edges per detent; knobs use one bounded parameter step per edge.
The task consumes at most eight edges per control per poll and retains backlog.
Do not invent encoder push actions or claim clockwise polarity before bench
verification. Boot controls are preset0/algorithm0, knobs16/72/32/0. Original
OPL is the default. E4 toggles the synth; editing the same preset preserves
that explicit mode. Selecting a new preset changes the mode.

Reverb processes synth melody only, before the unchanged OPL percussion stem.
It uses 1,024 PCM16 samples partitioned from the existing Doom zone, bounded
feedback, a decimated delay and interpolated output. Control and waveform
changes smooth once per 64-frame audio block. `DOOM EDIT` reads the published
packed control snapshot without taking an audio lock in USB. All 13 native
contracts, including exact 4,233,600-frame OPL output, pass. Live knob polarity,
DSP timing and physical sound acceptance await flashing and bench checks.

## Verified shared board paths

Evidence: `fm1/firmware/nes/src/fm1_volume.c`, `fm1_audio_queue.c`, `fm1_wl82_keyscan.c`, boot `display_test.c`, and `docs/FM1_LCD_HANDOFF_FIX_20260929.md`.

- Physical volume input is **PB6, ADC channel 4** (`AD_CH_PB06`), derived from stock firmware and the SDK map. The working NES deployment recorded raw ADC122..971 and gain0..121 over about183 seconds. Doom used the same volume source. Do not guess another pin because the knob is stuck.
- Key scanner owns PB7; its bit0x80 GPIO changes preserve volume's PB6 bit0x40.
- IIS route is PORTC PC0..6, stereo24bit at44100 Hz; LCD owns PC7..10 with PA2 backlight.
- Stock LCD uses a 64-byte aligned3840-byte DMA strip. Wait for DMA completion and issue the required coherence barrier before reusing it. The shared strip avoids a duplicate allocation.

## Discriminate volume failures

Read `AUDIO` raw, sample count, valid/errors, target and applied gain while the user deliberately sweeps the knob. Sampling count advancing is not evidence that the voltage changes.

| Observation | Next investigation |
| --- | --- |
| Raw constant during confirmed sweep | ADC channel/configuration, analog gates and PB6 registers, physical input |
| Samples stop | Timer/ADC ownership and scanner recovery |
| Target changes, gain stays fixed | CPU control publication and output callback |
| Gain changes, loudness does not | Actual DMA samples and installed artifact |

The 2026-10-06 Doom build `8a36a90` recorded raw260..261 throughout a confirmed sweep, advancing samples, valid1/errors0 and gain=target32. Revision `60827f7` later recorded raw0 throughout 26 confirmed-sweep snapshots, samples30248→35552, valid1/errors0 and unchanged observed ADC/PB/analog gates. No wrong pin or missing gain operation was demonstrated. The later `909c0188...` startup/polling revision restores input and gain response as measured below; **the precise failing integration step remains unresolved**.

An actual firmware A/B test then installed the exact older CDC NES reference (app SHA-256 `d01690af...`, verified full image `1d9a5001...`) on the same FM-1. Live ADC readings traversed 0..1023 with zero errors at roughly 500 samples/s, and the user confirmed the knob works. This establishes a working hardware/driver reference; historical telemetry alone was insufficient to establish the current knob's health.

Doom's current split is CPU0 timer ADC polling, CPU1 ALINK renderer, aligned volatile Q7 control with `csync`. Reverting IRQ affinity without evidence can restore CPU0 starvation. Check emitted code and the real IRQ output path, not only an envelope unit test.

The real NES driver reads control `0x13100` and result `0x13104`. Its launch writes `0xf44e` then sets kick/enable, with ADC IRQ bit5 disabled; completion is bit7. PB6 analog input requires DIR bit6 set and DIE, PU, PD and DIEH bit6 cleared. The legacy CDC NES path is `firmware/usb-diag/peripherals.c`: LCD init, scanner start, volume start, scanner timer registration, then IIS open/rate/IRQ. Legacy MDX follows the same early-start ordering. The pinned 44100 Hz IIS path selects the ALINK clock and programs ALINK0_CON3; it does not overwrite the volume WLA_CON0 or PLL_CON1 gates.

The newer public `fm1-nes` UAC path differs: persistent DAC ownership starts IIS, installs its callback and 44100 Hz rate, starts ADC, and enables ALINK before LCD/scanner setup. It polls volume from CPU0 DAC IRQ cadence near 2 ms and excludes the volume poll from its scanner callback. Its UAC transport is 48000 Hz stereo PCM16 playback/capture. Do not describe legacy scanner-timer ADC polling as the universal NES integration.

Doom's early-ADC image (app `998677b7...`, full image `f328adbc...`) specifically follows legacy CDC NES/MDX: start ADC before registering the scanner timer and preserve its primed samples, accepted target and errors through sound initialization. An idempotent start uses bit30 of the existing control word; bit31 remains speaker mute, with explicit Q7 masks and no additional static RAM. ADC errors cannot cause an implicit restart; explicit shutdown permits a new lifecycle. Actual-driver regressions and all 12 native contracts pass.

That image was then flashed with 137 changed sectors and a full readback matching `f328adbc1c08cafd78d41547f5f6947878266da407ae49a7c985f3d1062d81a7`. Reset/observation reached stage4/fault0, zero LCD/key errors and frames129→163. At that checkpoint the user still reported failed knob control. Its 3.84-second initial capture showed raw221..222, accepted222, target=gain27, errors0, samples11030→11938, IIS ready1 and heap_free4244. Nonzero constant readings do not establish working physical control. Earlier startup did not resolve the reported failure; it did not establish startup ordering as the cause.

The earlier volume-control image is a 561,328 B application, SHA-256 `909c0188d2f0f68936c2a99812d76c756f29f0ef553773ff85332d6c79ac59dc`, packaged full image `ac0af0b6aa4238bd8ec2a6689a2228adeff800f622af09a0f924f1e9330475a7`. It matches newer UAC NES hardware initialization: IIS open/callback/rate → ADC → ALINK before LCD/scanner setup. Doom retains CPU0 timer ADC polling and CPU1 rendering. Its existing 1 ms timer starts before LCD initialization, polls ADC every 2 ms throughout it, and later handles scanner work only after setup enables that work. The timer is reused rather than allocated twice.

All 12 native contracts pass, including actual-driver event tracking of IIS/rate → first ADC MMIO → ALINK/channel, no ADC access on IIS failures, and preserved previously primed state. Static RAM remains 26,896 B data plus 451,512 B BSS and linker heap remains 45,132 B. Shared ADC machine-code MMIO sequences match NES; leaf versus inline structure does not itself establish a hardware-write difference. Seven hot OPL helpers are forced inline with zero remaining target helper calls and exact reference output retained.

Flashing changed 138 sectors and full readback matched the packaged image. Reset/observation reached stage4/fault0, zero LCD/key errors and frames19→53. A confirmed approximately 25.5-second physical sweep (`fm1-doom/build/adc-iis-first-knob-live.jsonl`) traversed raw/accepted0..1023 and target/gain0..127 with errors0; samples28174→37836, IIS ready1/error0 and heap_free4244. The grouped IIS/ADC-before-LCD plus early polling change restores ADC-to-gain response. The user confirms OPL music loudness follows the knob correctly. No single register/clock interaction has been isolated as the cause. The user still reports noise during music, especially at low master volume; its source has not been isolated to synthesis, PCM effects or output timing.

All 37 AUDIO snapshots stayed in default OPL mode0 over 25.485 seconds. IRQs50275→67370 and outputframes3217600→4311680 give 670.79 IRQ/s and 42,930.35 stereo frames/s, much improved but below 689.06 IRQ/s and 44,100 frames/s configured. The cumulative max_irq_us3500 includes earlier synth mode1 activity and is not an OPL-only maximum. At that checkpoint, a new recording attempt could not start because Windows no longer exposed serial or UAC. Windows subsequently detected a healthy UBOOT device; missing application interfaces do not establish a runtime USB hang or its cause. The earlier `60827f7` recording remains verified.

An earlier readback-verified image is a 561,040 B application, SHA-256 `8d7fc70db0535a1e5a116d9f527f686b5bfe6d6e0e2b60ba23f755c9d5e244c7`, packaged full image `50d6ee1037e4761667a9f4c881cbe1fc7f79234fbcc4b4aeab836b813c4be101`. It preserves the verified startup changes and adds an exact OPL silent-operator path (eg_out>=EG_MAX), skipping waveform, AM and feedback arithmetic while preserving operator history and state. All 12 native contracts pass, reference PCM remains identical, and static RAM is unchanged. Installation changed 137 sectors and full readback matched the packaged image. Reset/observation reached COM4 stage4/fault0, zero LCD/key errors and frames19→53.

Actual recording `fm1-doom/build/usb-audio-opl-silent-fast-capture.wav` is 19.997 seconds / 881,868 frames of 44,100 Hz stereo PCM16, PCM SHA-256 `a49e4e137c2a6ac3e4bac8df21ba60d10b96297890fc2739488bc32ad907a1ab`. Left/right peaks 7383/7385 round to -12.94 dBFS; RMS 1182.63 is -28.85 dBFS, with no fullscale endpoints. All captured AUDIO rows stayed OPL mode0, sfx_started3 fixed and sfx_voices0: this music-only capture excludes background PCM effects as the sustained noise source. Over 23.047 seconds, IRQs27201→42717 and outputframes1740864→2733888 give 673.23 IRQ/s and 43,086.91 stereo frames/s. IIS stayed ready1/error0, ADC errors0 and heap_free4244, while raw/gain203/24 fell to0/0. Final USB counters sent996947/silent26456/under2157/over1149 with starts2/stops7 include Windows stream reopening; a saved recording alone does not establish continuous transport acceptance.

The earlier paused-render comparison (`fm1-doom/build/opl-silent-fast-render-paused.jsonl`) kept engine entered/completed4951 fixed and capture inactive. Its 9.125-second interval, IRQs306745→312905 and outputframes19631680→20025920, gives 675.07 IRQ/s and 43,204.38 stereo frames/s. This suggested a fixed 43,200 Hz IIS-clock hypothesis. The later monitor PAUSE capture stops both engine and MUS and reaches approximately 44,100 frames/s, disfavoring that hypothesis. Producer counters are not validated missed-IRQ counters; stopping both loads does not isolate which work delayed production or prove physical timing acceptance.

`DOOM MUTE 1` was acknowledged and speaker_muted1/gain0 confirmed. Whether physical hiss persists awaits user observation. Source audit found no PCM-effect volume bypass, signedness/rate or stale-playback fault. Exact PCM8 at 11,025 Hz uses `(sample-128)*256`, four-frame hold at nominal 44,100 Hz, and exact end-count termination. Default engine effect volume64/center separation128 gives left31/right32, normalized gains31/256 and32/256 (-18.34/-18.06 dB before master); gain1 is 6.02 dB below the initial gain2 build. Music and effects share the final gain/128. Stopped effects contribute exactly zero; simple hold roughness can occur only while an effect plays. Remaining music noise is not yet assigned to a source.

## Installed music monitor and cold-boot check (2026-10-07)

The new application is 561,520 B, SHA-256
`6150df022dd98f525ea8fa59fa67020a4d0a9f62331be79a3d123ddd9cf15c7f`,
packaged full image
`71d2cc0d86965ac4ad146ba7493ec2eca2e43a1d82cf12f5236c441753bcf461`.
It was flashed and full readback matched `71d2cc0d...`. Warm-reset observation
on COM4 reached stage 4/fault 0, zero LCD/key errors and frames 20→54. Physical
noise and cold-boot USB acceptance remain open. All 12 native
contracts pass, including 37,784 original register writes and 4,233,600
byte-identical reference frames. Default FULL retains the original reference.
An OPL drum-history ownership mask fixes a one-LSB first-crossfade discrepancy
after lazy drum extraction resumes.

Temporary `DOOM MUSIC FULL/MELODY/DRUMS/PAUSE` commands select the full mix,
melody only, drums only or frozen music generation. Isolation restores FULL
after 15 seconds or protocol reset/unplug/fragment timeout. Selection retains
score events, operator state and normal voice allocation. The build adds 16 B
static storage: data 26,896 B + BSS 451,528 B = 478,424 B. Linker heap 45,132 B is
unchanged; diagnostic chain 1,472 B + SDK margin 1,024 B fits stack 2,560 B with 64 B
remaining. These build checks do not establish physical acceptance.

Captured PAUSE diagnostics keep engine entered/completed at 1515 and MUS
ticks unchanged. Device trace over 4.95 seconds measured 44,114.75 stereo frames/s
and 689.29 callbacks/s; host-counter regression measured 44,098.63 frames/s.
That window added no capture silent/under/over counts. FULL/MELODY/DRUMS still
accumulated transport corrections in different song intervals. Windows capture
has residual +/-1 samples during PAUSE and small stereo differences, so it is
not an exact device PCM fixture. This disfavors a fixed 43,200 Hz IIS clock;
it does not prove physical audio quality or separate engine and OPL load.

Drums are original OPL GENMIDI instruments: MUS channel15 maps through MIDI9
to 12 percussion patches, with 702 note-ons in E1M1. Synth oscillators skip that
channel and retain extracted OPL drums at gain2 versus gain4 in full OPL mode.
They obey music/operator attenuation and the shared master knob. They do not
come from the PCM/Rice effects bank. Zero `sfx_voices` excludes effect playback,
not OPL percussion. Do not use the emulator's fixed OPL_MASK_RHYTHM channels
to mute these dynamically allocated FM voices.

The phone-video analysis covered 6.25 seconds of decoded AAC at 48 kHz. Initial
zero duration was 0.491 seconds, with no interior exact-zero gaps or decoded
clipping. Bursts in 2–6 kHz changed by about 24.7 dB between louder and quieter
regions. This supports testing drum/transient content but cannot establish
PCM versus OPL or exclude analog noise, microphone processing or phone
automatic gain control. Physical drum isolation remains pending.

The user reports a normal cold boot with Doom visible but USB interfaces absent;
Windows saw no application serial/UAC device. Manual UBOOT subsequently
enumerated. This cold-boot enumeration problem remains unresolved. The pinned
SDK's USB PLL initialization is present and preserves the CLK_CON1 selector;
the clock hypothesis is not a proven cause of the missing application USB.

## Installed DMA output and full-track host check (2026-10-07)

Application 563,440 B, SHA-256
`ce8176d84946623efc296434973dbabc75c094edd3b7a8cb8ea29fbc39762ab3`,
was flashed and full readback matched image SHA-256
`2a432d0f469a21169a5cbe638fb2c5f95e9be493845eadd2eda0cd92951a6a1b`.
Warm-reset observation on COM4 reached stage 4/fault 0, zero LCD/key errors
and frames 109→142. The user reports speaker improvement and almost perfect
output; this does not establish complete noise acceptance.

A private 512 B scratch block completes rendering, the premaster USB tap and
physical master scaling before copying to the SDK DMA half and issuing target
`csync`. The actual-output regression fails before this change and passes after
it: DMA contents stay unchanged while samples are generated. Do not equate that
software property with a proved physical noise or deadline fix.

Seven hot operator helpers plus `update_noise` are forced inline with exact
reference PCM retained. Static RAM 479,000 B, linker heap 44,556 B and reviewed
startup reserve 4,544 B pass build budgets. Optional F4-at-boot LCD diagnostics
use 60 B USB-register snapshot storage and the existing LCD strip, read registers
without reconfiguration and time out after 30 seconds. The user subsequently
reports cold-boot USB still disappears. Windows found neither application nor
UBOOT devices before the next UAC capture opened, so no recording was produced.
Exact transition timing is unconfirmed; do not declare a spontaneous firmware
hang from that absence. New runtime audio/capture and F4 LCD evidence are pending.

Repository-source host evidence lives in ignored `fm1-doom/build/host-bgm-pcm-check/`:
97 seconds at 44,100 Hz/stereo PCM16, OPL mode 0/FULL, music/effects 64 and no active
PCM effect voices. It covers the 96-second E1M1 score plus one second after looping,
with a separate WAV through the actual board startup/master-127 envelope. Source
hashes were verified unchanged after rendering; no normalization or filtering.

Premaster range -7516..2976, peak -12.7893 dBFS, RMS -29.0226 dBFS and DC
-757.4211 PCM16. There are zero quarter-limit 8191/-8192 endpoint hits, zero
strictly clipped samples and zero fullscale endpoints. The first 4,233,600 frames
and 37,778 ordered register writes at constant 64 match the independently compiled
unchanged original driver/core exactly. The 37,784-write regression additionally
changes volume 127/64. The loudest peak is 1.678 ms after percussion note 57;
the largest sample step also matches the reference and is not a 64-frame block
boundary. No added clipping, overflow or sample discontinuity was identified
in that compared music. Offline synchronous generation cannot rule out device
DMA/USB underruns, analog noise or distortion; physical noise remains unresolved.

The proposed internal-reference/VBG probe is **not implemented**: SDK `p33_lock` and SPI-transfer waits are unbounded, with no verified exported try-lock, cancellation or restoration path. No P33 gates or reference settings are changed. A bounded VBG probe requires a verified bounded transaction path.

## Audio timing and fidelity

64 stereo frames at44100Hz give an IRQ rendering budget of about1.451ms. Historical current telemetry showed a4500us maximum and roughly350..400 callbacks/sec against about689 expected. These counters indicate a timing fault; they are not a dedicated DAC underrun counter. The timer used for that maximum is quantized in500us steps and excludes time waiting for the mixer lock.

OPL music means Doom DMX register timing plus the original GENMIDI operator data rendered through9-voice OPL2. It is separate from original PCM effects. An analog synth mode intentionally changes the timbre. E4 toggles that mode; confirm `AUDIO synth_mode=0` before diagnosing OPL fidelity. A host PCM comparison cannot establish physical IIS playback timing or audible output quality.

The 2026-10-06 audit found unnecessary synth voices, drum stem extraction and blending still computed in settled OPL mode. Revision `60827f7` boots in OPL mode and skips that unused work; exact noise LFSR chunks preserve state across rhythm changes. Host validation passed 37784 ordered register writes, 4233600 identical PCM samples and 65536 rhythm-toggle samples. Its actual UAC capture still showed substantial underflows and about33454 produced stereo frames/s against44100 configured. Later images reached about43087 during capture; monitor PAUSE with engine and MUS stopped subsequently measured approximately44100, disfavoring a fixed43200 Hz clock explanation. These counters do not establish the physical noise source or validated deadline misses. AM-table behavior was suspect in one portable branch, but all 15 active E1M1 patches had AM disabled, so it did not explain this song.

## USB and stack

### Cold-boot USB investigation, 2026-10-07

**Latest result: programmer cable identified by the user.** Switching from
the programmer cable to a normal USB data cable restored application USB.
Windows exposes healthy
COM5 and a UAC recording endpoint. Read-only HELLO/live observation confirms
DOOM-FM1/1, stage4/fault0, frames1375->1409 and LCD/key errors0 without another
flash. The following failures are historical; they do not establish a Doom
cold-startup defect. Reopen firmware diagnosis only if the problem reproduces
with the working cable. Audio streaming was not repeated for this check.
Use a normal USB data cable for runtime CDC/UAC. The programmer cable's
electrical/protocol mechanism was not measured; do not assume it is broken
or assert specific boot-trigger wiring.
The connection moved from root-hub port22 to11: resolve the current PnP
parent/location before querying a cached port. An empty old port is not proof
that the device is absent elsewhere.

The user accepts PCM playback and volume after the private DMA-block fix.
The reported issue was normal cold boot losing both CDC and UAC while Doom runs;
unplugging/reconnecting USB with device power on also failed to enumerate.
Check Windows before requesting manual UBOOT: it may already be present.

The F4 photo from the `ce8176d8` application shows USB stage2/error0 and a
running heartbeat. Startup snapshots show USB0_CON0 `0x8001 -> 0x2883D`,
USB0_CON1 `0 -> 0x8000`, IO_CON0 `0xEAC -> 0x164C`; CLK_CON1 `0x400`,
USBPLL_CON0/1/2 `0x07796CB3/0x008881C3/4` are unchanged. The photo's last
digit is D, not 0: PHY/reset are enabled. These are startup snapshots, not
live endpoint-state readings, and do not prove enumeration or clock frequency.

Public NES `9fa8d235` creates its CPU0 peripheral owner before USB. Doom had
created USB first. Candidate app `3cf7f681` creates the game/peripheral owner
first and makes USB wait until IIS/ADC startup completes, including failure
paths that preserve recovery. It retains the NES SDK controller setup; an
unflashed experimental `usb_g_hold`/delay was removed. This candidate was
flashed and readback verified as image `af92a30b`; COM4/UAC worked after
UBOOT reset, but the user's next cold power cycle FAILED again (Windows also
found no FM-1 interfaces). Ordering alone is insufficient. For this ongoing
Doom investigation the user explicitly forbids further flashing until the
cause is identified; investigate offline and preserve that constraint.
The maintained source report is `fm1-doom/USB_COLD_BOOT_AUDIT.md`.

Do not repeat the stale-build hypothesis without new evidence: compiling all
19 baseline SDK C sources with boot versus USB app_config produced identical
functional LLVM IR (only source-location metadata differed). The shared
board.c and USB app_config.h also match public NES. No post-start controller
detach path was found in the linked application.

The subsequent audit compared all34 actual baseline objects with retained
working NES:33 functional IR modules match, one differs only in date/time
print strings, and all12 assembly objects are byte-identical. Linked startup
copy/clear ranges and packaged application/area CRCs are correct; the image
contains one executable application and no secondary application directory.
USB IRQ9 remains registered/enabled on CPU0. Descriptors use174 of768 bytes
and are constructed before attachment, without late allocation. IIS platform
parameters and the44100-Hz clock branch match NES/MDX, while LCD transfers
leave interrupts enabled. The F4 failure photo precedes Doom/music
initialization, excluding expensive OPL rendering for that specific failure.
These exclusions do not identify the cold-boot cause. Collect the failed
application's host-port/attach/reset state; an enumerated UBOOT port does not
provide that evidence. Preserve the user's no-flash-until-cause constraint.

The protected writer's `reset1` uses loader `RUN_APP(1)` (0xFC0C). The exact
retained RAM helper disables USB and sets CLOCK.PWR_CON bit4 for software reset;
it does not directly jump to Doom. Argument1 selects zero delay, not a boot
argument. It does not power-cycle the device. Retained peripheral differences
were not measured, so do not claim a missing cold initialization from warm
enumeration alone. Also correct the earlier clk_early_init reads-only premise:
it conditionally initializes USBPLL when disabled, and the cold photo shows
USBPLL enabled/PLL48 selected. USB0_CON0 snapshots
also contain pending status; do not label them fully nonintrusive or infer
WL82 read-clear semantics from documentation for other Jieli families.

The WL82 SDK task stack field is in32-bit words. LTO can inline separate handlers into one larger task frame. Doom once emitted a3192-byte reply/task frame plus formatting, exceeding a4096-byte stack. Out-of-line protocol handlers and an emitted call-chain gate fixed that cause. Inspect the actual ELF and stack call chain after adding diagnostics; source buffer sizes alone are insufficient. ISR stack and task stacks are separate budgets.

Scalar telemetry must avoid taking the long CPU1 mixer lock. USB task/CPU0 service can otherwise wait behind audio and appear hung. USB queues and writes must remain bounded; never add a busy-poll packet writer to an IRQ. The frame capture protocol intentionally pauses engine ticks during a bounded snapshot; do not misdiagnose that pause as the old input freeze.

## USB Audio Class

CDC presence does not imply audio interfaces exist. The deployed Doom8a36a90 was CDC-only. The sibling `fm1-mdx` commit93c6820 has custom UAC1 audio interfaces2..4, EP1 audio and CDC OUT2/notifyIN2/dataIN3. Its full duplex FIFO/state costs about9KiB; Doom had only4244 bytes of live free heap and about1024 bytes above the reviewed startup gate, so blindly copying it cannot fit.

The newer public NES profile at `fm1-public/firmware/usb-audio/profile.c` (`9fa8d235`) supports UAC1 playback/capture at 48000 Hz stereo PCM16, interfaces2..4 and EP1 IN/OUT. The retained 2026-10-03 SMB1 deployment verified 51 changed sectors, full readback `462751778c8b00ab6117f5752c59961eeb10046924fd72d64f06f27d6b5385d0`, advancing frames and both Windows audio endpoints. It is separate from the older CDC-only image used for the ADC A/B test; enumeration alone does not prove audible duplex acceptance.

`fm1-mdx/USB_AUDIO.md` records an older packet writer stalling after75.62 seconds. Its bounded replacement was an unflashed candidate at this checkpoint. Reuse source patterns with current tests and actual target validation, not a claim that every MDX revision is qualified.

For capture-only native44100Hz stereoPCM16, EP1IN is free alongside Doom CDC. A small bounded producer FIFO, persistent pending packet and aligned endpoint DMA can fit with a measured static/linker budget. Preserve packet data on endpoint busy, bound IRQ work, handle alternate-setting/reset epochs and report drops/underruns. Keep the4096-byte reviewed runtime heap reserve; do not weaken the gate to force a build through.

## Allocator and capture bring-up checkpoint (2026-10-06)

The linked SDK allocator is `system.a(malloc.c.o)` dlmalloc, not the SDK source
`apps/common/system/mem_heap.c`. Its chunks round to32B: IIS's1024B DMA request
needs1056B. Heap growth uses4096B increments, and the pinned `sbrk` rejects
an end at or beyond HEAP_END. `get_malloc_remain_heap_size` combines free and
uncommitted space; it is neither a largest-block measurement nor proof that
another allocation can succeed. Keep this distinction in startup gates.

The first Doom capture image was readback-verified (`c510e024...`) and exposed
CDC plus a Windows recording endpoint after UBOOT reset. IIS then reported
ready0/error-1/irqs0 and stopped its ADC/capture producer during cleanup.
After the user's reset Doom remained visible but no USB interface appeared.
This establishes neither a USB recording nor the exact allocation failure.

In the earlier USB-first startup, running Doom proved `task_create(doom_usb)` succeeded: app_main returned before
creating Doom when that call fails. USB initialization can separately fail
on CDC allocations (-3), configuration allocation (-1), or EP0 DMA (-2), then
sleep indefinitely. Capture initialization itself is static; its descriptor
order matches MDX and its endpoint DMA does not use the CDC pool.

Revision `60827f7` restored1024B headroom using USB stack2560B and a512B CDC
reply ring. Every reply is bounded511B; production-task tests cover full
replies, busy retries, burst rejection and generation cancellation. Emitted
diagnostic chain1472B plus1024B SDK margin fits the stack. Linker heap45132B
and reviewed reserve5120B pass the build gates. The application
`71cea5cef5aa3f8beeb175e99a0d618a6291438d37422ea340daed79f7fc9d52`
was flashed and full-readback verified as image
`ae08519409aa26f81af53ab83ed03ddf7813ccf6dc4096af6cf879f204a2b020`.
After UBOOT reset, IIS reported ready1/error0 with running callbacks and
aggregate heap_free4244. Actual UAC recording produced 19.779 seconds of
44100 Hz stereo PCM16, peak -13.70 dBFS and RMS -31.08 dBFS, without fullscale
endpoints. That image still had streaming underflows and failed knob input;
the later image's responsive ADC/gain sweep is documented above. Recording
does not establish timely speaker playback. A later missing serial
port was clarified as UBOOT state, so that absence alone did not establish a
cold-boot USB failure. The later normal-cold-boot report is separately recorded
above and remains unresolved.
Boot gain follows the ADC. `DOOM MUTE 1/0` is an optional speaker override;
capture taps the mixed audio before that gain/override.
