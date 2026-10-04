# SMB1 with the working stock-sequence LCD and USB recovery

## Per-channel effects prepared — 2026-09-29 JST

Latest candidate: `build/nes-smb1-channel-fx`, full image
`a1b6df96fc5c9d444f39a0e9f7ecd7213d6a49e4cfaac465b484d08ee7822272`.
[NES_CHANNEL_FX.md](NES_CHANNEL_FX.md) documents independent Pulse1/Pulse2/
Triangle/Noise/All controls and tests. SELECT now chooses the bank. Prepared
only, not flashed. IMPORTANT: this image needs sector0x37000, beyond the current
writer's0x36000 boundary; scope review is required before flashing.

## Live knob effects prepared — 2026-09-29 JST

Latest candidate: `build/nes-smb1-live-fx`, full image
`0b4153cb645aa6af340a9a48554d06d4192bb33001ecbada3222e9d8b294a9dd`.
See [NES_LIVE_FX.md](NES_LIVE_FX.md) for the seven-control mapping, implementation,
tests and bench limitations. Includes audio-priority and IRQ-input changes;
starts in bypass. Prepared only, no flash; installed baseline remains c60b423b... .

## Audio-priority scheduling prepared — 2026-09-29 JST

Profile `build/nes-smb1-audio-priority` adds `--nes-audio-priority` to the IRQ
input/display-optimized candidate. No flash or device operations. Gain remains4
for an isolated underrun comparison; the discussed gain128 is NOT implemented.
Sample rate44100, startup mute/fade, ROM/APU, display initialization and USB
recovery remain unchanged. No pitch change, interpolation or repeated samples.

The queue consumer waits for1470 mono samples (33.3ms) before consuming;
game scheduling aims for2205 samples (50ms). Rather than independently pacing
each video frame, frame boundaries inspect the locked DAC queue snapshot.
Below target they run without timing sleep and skip the next LCD presentation,
but still emulate CPU/PPU/APU and sample input. Both LCD halves share the same
skip decision. At most5 consecutive displays are skipped, then one is forced.
This mode is separate from `nes_auto_skip` (the old wall-clock option); its
startup banner explicitly reports AUTOSKIP=5 and CLOCK=DAC.

Only queue surplus above the target buys sleep: whole10ms RTOS ticks, maximum2
per boundary. A sub-tick surplus does not round up to10ms. The existing zero-tick
yield, STOP/UBOOT checks,100ms full-queue timeout and failure reporting remain.
The IRQ now expands mono16 to stereo24 and applies the stock envelope in one
pass: signed16 *2 *gain_q7. This is byte-exact with the prior two-stage converter
for queued samples, including negative values. It eliminates the normal-path
512-byte clear and separate128-word envelope pass. Shared lock ownership stays
unchanged; IRQ work is bounded and performs no waits/logging.

After a genuine shortage, output silence and retain new samples until the
prefill threshold is reached. Do not replay stale audio. Initial prefill silence
has a separate `prime_silence` count; post-start missing/rebuffering frames count
as `underrun`, and `rebuffers` reports each shortage episode. The one-second
startup envelope continues on DAC time, including prefill silence. This recovery
is not click concealment or a guarantee that stalls cannot be audible.

All20 NES and16 USB host suites pass, including fused-vs-original conversion,
startup gain, counter/ring wrap, partial shortage, prefill/rebuffer order,
bounded sleeps/skips, STOP, physical-input recovery and the actual ISR callback
integration. All32 linked-image tests and9 stock-layout checks pass. The new
global layout's boot-trace displacement312 was reviewed in target disassembly
(ota_status0x1c19a40 -> trace0x1c19b78, store `d1 ec 0b 13` at0x200336e);
the exact wrapper, marker and target checks remain enabled.

Deterministic MODEL ONLY:600 frames,2ms CPU/APU cost,40ms LCD cost, real44100Hz
consumer cadence and10ms wait rounding. With the same new consumer, old pacing
produces667862 missing output frames and299 rebuffer episodes over25.194s.
Audio-priority produces zero missing frames/episodes over9.998s, with199 shown
and401 skipped video frames; all441000 audio samples are produced. Injecting
a150ms stall records5637 missing frames and one rebuffer episode, then continues.
These are not measured FM-1 FPS, ISR time or audible results. The reserve adds
audio latency; a display/CPU stall exceeding it still causes gaps. A larger
buffer alone cannot fix sustained emulation slower than real time. Bench-test
`queued`, `underrun`, `rebuffers`, frame progression, held controls and USB.

Application206448 bytes, SHA-256
`0a2bfe69ba11558742fe2de57bccd7e690dfc3f05e69d2b096ab595f897f0357`.
Full1MiB candidate SHA-256
`b9f8c3f703e9e5c7f4a74ab58d96537578a20bdda675fc758e0cba564e30f1eb`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
Offline comparison against the verified installed c60b423b... backup finds51
changed sectors0x4000..0x36000; outside bytes are preserved. Existing writer
scope suffices; no writer changes. Source rollback:
`../../private-backups/nes-audio-priority-source-20260929-132449/`.

## Display-path optimization prepared — 2026-09-29 JST

Profile `build/nes-smb1-input-irq-video-opt` builds on the interrupt-input
candidate below. No device commands or flash were performed for this optimization.
The installed baseline remains c60b423b... . Gain4, Y0, frame sleep0, auto-skip
OFF, audio and input behavior are unchanged from the IRQ candidate.

Gameplay now sets one LCD window per 120-row callback instead of per 8-row
strip. Per displayed frame: window setups30->2, LCD commands90->6, all LCD
write callbacks180->40. The same30 bounded pixel DMAs still carry115200 bytes;
each completes before its buffer is reused. Startup/display-on is unchanged.
The exact256-to-240 mapping is now16 groups of15 sequential pixels, skipping
one source pixel between groups, instead of per-pixel scaling arithmetic.
Crop mode remains an exact240-pixel copy from source column8.

New `video_stream_equivalence` tests compare every pixel byte to the old
algorithm for stretch/crop, offsets, partial strips and half/full frames, check
window geometry and every write-failure boundary. All19 NES and15 USB host
suites,31 linked-image tests and9 package preservation checks pass. Target
disassembly confirms the sequential scaling loop and single window setup.

Host-only benchmark,7 alternating-order trials of10000 frames with the same
fake LCD sink: old median0.564s, new0.159s,3.547x faster for display conversion
and submission. Run `firmware/nes/build/host-smb1/Release/video_perf_tests.exe
--bench`. This excludes real SPI latency and NES CPU/PPU/APU work: it is NOT
a3.547x game-FPS claim. Hardware FPS and panel streaming still need testing.

Application205904 bytes, SHA-256
`b4c7b0dcc697562da1c90fc92d4b7019d3c053f2cf27622c793cd6fc1152da7c`.
Full1MiB candidate SHA-256
`dc0fd36e77fc92f1a7e744f7f57a40d53cc09140ab108e6a1e34e2f2288ff993`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
All96 source/archive manifest hashes rechecked. Offline comparison against the
saved verified installed baseline finds51 changed sectors0x4000..0x36000,
with all bytes outside those sectors preserved. Source rollback:
`../../private-backups/nes-video-opt-source-20260929-130019/`.

Chronological update to the earlier writer warning below: during the preceding
IRQ-image flash request, sector0x36000 was reviewed as inside the stock app
slot; the existing writer/CLI scope was extended through that sector and all7
deployment tests passed. That attempt stopped at cancelled Windows UAC, before
helper upload or flash writes. Its empty attempt directory is
`../../private-backups/nes-input-irq-write-20260929-091208/`.
No new writer, environment, or flash script was introduced.

## Interrupt-driven NES input prepared — 2026-09-29 JST

Profile `build/nes-smb1-input-irq`, options `--keyscan-dma2
--nes-input-recovery --keyscan-irq`, retains SMB1, gain4, LCD Y0, frame sleep0
and auto-skip OFF. Physical controls are enabled. Not flashed/bench-qualified.
Source rollback: `../../private-backups/nes-input-irq-source-20260929-085034/`.

The new async API is separate from the existing synchronous diagnostic scanner.
Startup configures CON6020/BAUD29, arms ADR/CNT2 then sets only enable. The CPU
SPI2 vector remains masked until initialization is complete; registration then
enables IRQ37 at priority5 on CPU0, matching stock's vector/priority. Completion
already pending at registration is safe. Stock handler evidence is the saved
runtime-ram-indexed.asm at01c045ce..01c046e2: acknowledge, sample previous latch,
raise latch, select next word, ADR/CNT2, lower latch. This implementation follows
that row order but keeps LED multiplex outputs low, as before.

One ISR handles one completed word, samples six column inputs and starts the
next DMA. It discards the first11-row sweep and publishes only complete later
sweeps. No clock, logging, key decoding, RTOS sleep, or SPI wait occurs in the
ISR. A bounded11-byte copy occurs at a sweep boundary. A short shared spinlock
with local IRQ masking protects ISR/task state, including multiword snapshots.
The ISR and task can briefly contend on this lock; neither waits for DMA while
holding it. The DMA source stays64-byte-aligned in internal RAM at0x1c07fc0.
Task input reads copy a completed sweep, decode outside the lock, or immediately
return BUSY when no new snapshot exists. They never launch a synchronous scan.
Manual TEST KEYS/KNOBS retains the prior synchronous driver after NES teardown.

Held keys are levels, not press pulses. There is NO release-to-rearm gate.
The preceding recovery policy remains: preserve the last good input for100ms
without a fresh sample, then clear stale pad/debounce state at the next input
poll. Three total restart attempts per NES run, at least50ms apart, happen in
task context. A successful recovery accepts a still-held key. A scanner fault
does not set the fatal NES fault flag. Exhaustion leaves inputs OFFLINE, not
the emulator stopped. Ordinary10ms button debounce remains unchanged.

Stall detection is task-observed lack of IRQ progress, reason4: after observing
no new completion count for10ms, capture PND/CNT/GPIO/configuration and stop
SPI. This is NOT an exact per-transfer deadline; detection depends on frame
task scheduling and may take an additional observation interval. Pending DMA
completion with no ISR delivery is also diagnosed. STOP/UBOOT masks the vector,
disables the handler and SPI under the lock, then unregisters the vector.
An already-dispatched late handler is inert. First fault evidence survives
retries/event overflow. Periodic `KEYIRQ completions=... scans=...` is emitted
by the NES task for bench rate/liveness observation, never by the ISR.

All18 NES and15 USB host suites pass. New tests cover autonomous scans without
task calls, priming, partial-scan rejection, chord/held/release snapshots,
spurious/late interrupts, lost IRQ versus stalled DMA, timeout wrap, restart,
pending-before-enable, registration order, held-input recovery, bounded retries,
stale release, STOP and audio/video continuation. These are simulated checks,
not proof of physical input or improved FPS. Startup/static audit passes; the
reviewed boot-trace merged-global displacement316 is explicitly allowed while
all wrapper instructions/targets remain checked. Linked IRQ has interrupt
return; corrupted-return and misaligned DMA placement are rejected by tests.

The prior synchronous recovery-only candidate is retained at full-image hash
`efbc504ef07fa287c8f5813154739c2adc30d9ab629c62d1a2c662c02338774b`,
profile `build/nes-smb1-input-recovery`; it was NOT flashed. The neutral-input
candidate below also remains unflashed. Installed baseline is still c60b423b...
from `nes-keyspi-dma2-enable-write-20260929-080037/final-2.bin`.

Hardware qualification must check title/demo progression, sustained/chord/release
controls, USB responsiveness and STOP/UBOOT, IRQ rates and audio underruns.
Continuous IRQ scanning removes the frame's SPI busy wait but still consumes
CPU time per interrupt. ISR load and the original SPI stall are not yet proven
fixed. No writer changes, pre-write reads, device commands or flashing here.

Final package: application205904 bytes, SHA-256
`e44199da96f8ad6e1e0ff6e6ed9dfe8ea15268d9ce9e965d2a378f7a4527690d`.
Full1MiB image SHA-256
`e1c5633bbf89d660593dbb9ef5779cd3c3f4713bece82e49fc46b5e308334d7a`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
All31 linked-image tests and9 preservation checks pass; source/archive hashes
match the build manifest. Offline comparison to the installed saved baseline
finds51 changed sectors,0x4000..0x36000 inclusive. IMPORTANT: existing writer
allows only through0x35000; it will reject this candidate. The additional
0x36000 application sector needs scope review before flashing. Do not silently
bypass the writer guard or omit that sector. Writer is unmodified.

## Neutral-input isolation prepared — 2026-09-29 JST

Profile `build/nes-smb1-no-keys` adds `--nes-no-keys`. NES mode skips all
physical scanner start/poll/stop calls and supplies zero controller input.
No synthetic Start pulse is sent: title/attract mode only, not playable with
physical controls. Display, audio, USB recovery, frame sleep0 and auto-skip OFF
remain unchanged. DMA2 remains compiled for manual peripheral tests; do not
run TEST KEYS/KNOBS during this isolation. Startup and TEST STATUS identify it
with `NES INPUT=NEUTRAL SCANNER=DISABLED`.

All17 NES and13 USB host suites pass. The new isolation lifecycle test rejects
scanner accesses in NES mode, checks neutral inputs and continued video/audio
under scanner-fault injection, and preserves STOP and other fault coverage.
All29 linked-image tests and9 package preservation checks pass. The initial
image-test run found the old mandatory DMA banner assertion; it now checks
the manifest-selected neutral-input banner instead, with the DMA RAM placement
and corruption rejection checks retained. No production code changed for that.

Application203376 bytes, SHA-256
`5edb974a1c0e192f6ed11a2334db117fd08d01da729b8d982c55f36e5c2394e5`.
Full1MiB candidate SHA-256
`87d3bc9f467af4f5a6ee55e8a64fda0435b105b35db603780d1a2de0c972ec0c`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
Offline comparison to the verified c60b423b... final-2.bin stays within the
existing writer scope0x4000..0x35000. Stock boot/config/reserved bytes preserved.
Source rollback: `../../private-backups/nes-no-keys-source-20260929-080557/`.
Prepared only: no device commands or flash performed, hardware unqualified.
After installation, observe beyond the prior30-frame stop and capture status;
continued operation would isolate the scanner path, not establish its root cause
or qualify audio quality. Current installed baseline remains c60b423b... .

## DMA enable-only correction — 2026-09-29 JST

Profile `build/nes-smb1-keyspi-dma2-enable` changes only the first DMA enable
operation from `wr(SPI,0x4021)` to `change(SPI,0,1)`, matching stock020044fe.
The prior pending-clear before ADR/CNT remains; no second pending-clear strobe
is issued after arming. No delay, IRQ, clock, timeout or recovery change.

Regression tests inject completion immediately after CNT and during the
enable read/modify/write window. The model now treats pending as read-only
(cleared only by its strobe/reset). The new early-completion test failed with
the old driver at startup, and both injected cases pass with the correction.
These are ordering scenarios, not proof of the device's actual event timing.
All17 NES and12 USB host suites pass, including genuine-stall timeouts and
restart behavior. Hardware confirmation is still required.
Source rollback: `../../private-backups/nes-keydma-enable-source-20260929-075703/`.
Build/static audit,28 linked-image tests and9 package checks pass. Linked
disassembly at0200531c is `00 ef 01 e0`, `[r14+0] |= 1`; no second clear strobe.
Application204272 bytes, SHA-256
`85448225f49b4b5e5bcc505078732cba617ff6917dd84330183b654916f057bb`.
Full1MiB image SHA-256
`c60b423b0e640c96ae939a138811a880eab1f674fe2e8c78ab585c0bfc254532`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
Offline comparison fits the existing0x4000..0x35000 writer scope.
Flashed at the user's request after serial UBOOT using the existing writer.
Receipt: `../../private-backups/nes-keyspi-dma2-enable-write-20260929-080037/deployment.json`.
Status written_and_twice_readback_verified, exit0;40 changed sectors verified,
directory0x4000 last, both full readbacks independently match c60b423b... .
No pre-write dump, writer/environment change or automatic reset. This run's
final-2.bin is the next saved baseline. Normal boot/hardware effect untested.

Bench after normal boot: user reports a blue screen before title, then freeze.
Blue is the existing fm1_board_lcd_init startup fill, not an exception display.
USB confirmed worker idle and:
```
NES FAILURE source=KEY_SCAN rc=-3 frame=30 lcd=0 audio=0 q=0
KEYSPI reason=1 row=8 phase=2 tx=feff elapsed_us=10000 polls=14
KEYSPI CON=00000021 BAUD_WRITE=29 MUX=00020010 PA_OUT=00000000 PA_DIR=0000f9e1
KEYSPI start_us=3850000 end_us=3860000 sys=360000000 lsb=60000000
KEYSPI INIT_CON=00008021 INIT_MUX=00020010 SCAN_CON=00000021 SCAN_MUX=00020010 scans=32
KEYSPI change_scan=0 change_CON=00000000 change_MUX=00000000
KEYSPI PRE_CON=00000021 CLEARED_CON=00000021 FIRST_CON=00000021 CNT=2
```
Audio frames140800/irqs2200; USB heartbeats continued. Enable-only permits
startup in this run, but DOES NOT fix the recurring scanner timeout. Both BUF
and DMA paths now have observed runtime failures. Proposed next isolation:
skip scanner initialization/polling entirely, supply neutral pad input and
observe NES/LCD/audio stability without SPI2. This is diagnostic isolation,
not a usable controls fix. No new build/flash/restart performed on this report.

## Two-byte SPI2 DMA experiment — 2026-09-29 JST

Profile `build/nes-smb1-keyspi-dma2` explicitly selects `--keyscan-dma2`;
the default scanner remains the preceding byte-polling implementation.
This changes each row selection from two BUF writes/two completion waits to
one ADR/CNT2 transfer and one bounded completion wait. First transfer is armed
before CON enable, as at stock020044f4..fe. Subsequent words start after
sampling columns and raising the latch, then lower the latch after CNT write,
matching the order at stock01c0469a..a0 and01c046d0..d6.

The two-byte source is high byte then low byte, in persistent64-byte-aligned
internal BSS `fm1_key_dma` (linked address0x01c07fc0, size64). Only2 bytes are
transferred. A csync precedes DMA arming; source memory is not reused until
completion or controller shutdown after timeout. No stack/SDRAM/flash source
or write-only register read is used. Static audit checks alignment, size and
placement, with misalignment/flash-placement rejection tests.

This is NOT a full stock interrupt-scanner port: SPI IRQ remains disabled and
completion is polled by the existing task. It isolates the transfer mechanism
without changing IRQ ownership, clocks, pins, row mapping or scheduling.
Timeout10ms and one-million-poll fallback stay fail-closed. Failure phase2
means the whole word (phase0/1 remain high/low BUF bytes in the default build).
CNT is captured on failure; its value alone does not establish DMA progress.
Banner: `KEYSPI MODE=DMA2-POLLED BYTES=2 IRQ=OFF`.
Frame sleep0, auto-skip OFF, gain4, LCD offset0, persistent SPI trace unchanged.

All17 NES and12 USB host suites pass. The additional DMA model checks all row
words/byte order, first-start disabled ordering, ADR-before-CNT, latch timing,
no BUF writes, no BAUD/ADR reads, stable source until completion, actual stuck
DMA shutdown, frozen-clock fallback, late completion, snapshot retention,
restart and coherent key publication. Hardware effectiveness is untested.
Source rollback: `../../private-backups/nes-keydma-source-20260929-074803/`.
Application204272 bytes, SHA-256
`b9769caec2090c500a3985beb970e17111a0ad0d1ea7eeec657de0c11d11d3c4`.
All28 linked-image tests and9 package checks pass. Full1MiB image SHA-256
`e28591d37b1fa948c2e332d7b5d349e632b5a1b138b3b015d7a93c384c39ff0f`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
Offline comparison stays within the existing0x4000..0x35000 writer scope.
Flashed at the user's request via the existing writer after serial UBOOT.
Receipt: `../../private-backups/nes-keyspi-dma2-write-20260929-075252/deployment.json`.
Status written_and_twice_readback_verified, exit0; all50 changed sectors
verified, directory0x4000 last. Both full readbacks independently match
e28591d3... . No pre-write dump, writer/environment changes or automatic reset.
This directory's final-2.bin is the next saved baseline. Normal boot and the
effect of DMA on freezes remain untested; capture TEST STATUS after failure.

Bench result: user reports blank screen. USB remains responsive; status
busy0/mode0, frames0/irqs0, and persistent failure identifies initial scanner
priming (not a global firmware hang):
```
NES FAILURE source=KEY_START rc=-3 frame=0 lcd=0 audio=0 q=0
KEYSPI reason=1 row=0 phase=2 tx=fffe elapsed_us=10000 polls=39364
KEYSPI CON=00000021 BAUD_WRITE=29 MUX=00020010 PA_OUT=00000000 PA_DIR=0000f9e1
KEYSPI start_us=770000 end_us=780000 sys=360000000 lsb=60000000
KEYSPI INIT_CON=00000021 INIT_MUX=00020010 SCAN_CON=00000000 SCAN_MUX=00000000 scans=0
KEYSPI change_scan=0 change_CON=00000000 change_MUX=00000000
KEYSPI PRE_CON=00000020 CLEARED_CON=00000020 FIRST_CON=00000021 CNT=0
```
Repeated TEST STATUS confirmed the same record. No restart/write was sent.
Review found a concrete startup mismatch: after CNT2, stock020044fe sets only
CON enable bit0, whereas this experiment writes4021, asserting pending-clear
bit14 again. This can discard a completion; CNT0 alone cannot distinguish
completion from an unaccepted start. Correct that enable operation and add a
model case with completion between CNT and enable before another bench test.
This is a candidate explanation, not yet a hardware-proven cause.

## Corrected SPI trace / stock comparison — 2026-09-29 JST

Correction: the SDK's `include_lib/driver/cpu/wl82/asm/WL82.h` declares SPI
BAUD and ADR `___WO` (write-only). The preceding diagnostic's BAUD=0 read
does NOT establish lost configuration, a changed divider, or a clock change.
Do not implement a divider rewrite or clock workaround based on that value.

Reviewed stock010 evidence is in the saved `execution-map-dispatch-20260928/`
application-indexed.asm and runtime-ram-indexed.asm, under
`private-backups/readback-20260926-024637/analysis/`.

| Operation | Stock010 disassembly | Current polling scanner |
| --- | --- | --- |
| SPI2 route | 02004462..68 sets MUX bit17 | sets bits16..17 to binary10 |
| Control/divider | 020044a4..aa writes6020 then BAUD29 | writes4020 then BAUD29; SPI IRQ deliberately disabled |
| Enable/first transfer | 020044f4..fe writes DMA ADR, CNT2, enables CON bit0 | enables CON then writes high/low bytes to BUF |
| Completion | ISR01c045ce clears pending at01c045d8 | polls bit15, clears via bit14 |
| Row/latch | samples columns before latch-high at01c0462c | same logical sampling/latch order |
| Next word | ADR/CNT2 at01c0469a..9c or01c046d0..d2; latch-low afterward | two BUF transfers; latch-low after first launch |

Thus stock's IRQ/DMA two-byte path differs materially from our task-polled
byte path. This is a candidate area to investigate, NOT proof that DMA is
required or that the difference causes the timeout. No speculative DMA/IRQ
conversion or timeout masking was made.

New profile `build/nes-smb1-keyspi-trace` eliminates BAUD reads. BAUD_WRITE=29
is explicitly software intent, not hardware confirmation. It samples readable
CON/MUX after enabling SPI and before each complete scan, records the first
control/routing mismatch (ignoring CON bits14/15 and unrelated mux bits), and
captures CON before pending-clear, after clear, at first completion poll, and
at timeout. Scan count saturates rather than wrapping its change marker.
Six KEYSPI lines persist through teardown/queue overflow and TEST STATUS;
starting a new NES run clears the published record. Extra reads can perturb
timing, so even a successful run will not alone prove the underlying fix.

Frame sleep remains0, auto-skip OFF, gain4, row offset0 and all transfer/timeout
policies unchanged. The host SPI model now fails on ANY BAUD/ADR read instead
of incorrectly modeling these registers as readable RAM. All16 NES and12 USB
host suites pass, including readable-register mutation/retention/ignore masks,
counter saturation, timeout evidence and six-line persistent status recovery.
Source rollback: `../../private-backups/nes-keytrace-source-20260929-073528/`.
Build/static audit, all27 linked-image tests and9 package checks pass. App
204208 bytes; SHA-256
`4429c1fb3f01ecae86fed8b52aee136386e06a5c72c82b7c2e8625a6e932343c`.
Full1MiB image SHA-256
`3ed606fb33b8efd285416043a19a025e14c99e7881cd75e336d3bc6408a7f43f`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
Offline comparison fits the existing0x4000..0x35000 writer scope. No audit
allowlist changes were needed for this revision.
Flashed at the user's request using the existing writer. Receipt:
`../../private-backups/nes-keyspi-trace-write-20260929-074145/deployment.json`.
Status written_and_twice_readback_verified, exit0;50 changed sectors verified,
directory0x4000 last; both full readbacks independently match3ed606fb... .
No pre-write dump or automatic reset. This directory's final-2.bin is the next
saved baseline. Serial UBOOT initially returned UBOOT_RETRY while diagnostic
lines were queued; after draining, the normal arm/confirm transition succeeded.
Normal boot of this revision is untested. Capture TEST STATUS after failure
before restarting NES; no underlying freeze fix is claimed.

After the user power-cycled, COM10 confirmed KEYSPI=TRACE and a stopped worker:
```
NES FAILURE source=KEY_SCAN rc=-3 frame=64 lcd=0 audio=0 q=0
KEYSPI reason=1 row=6 phase=1 tx=bf elapsed_us=10000 polls=14
KEYSPI CON=00000021 BAUD_WRITE=29 MUX=00020010 PA_OUT=00000000 PA_DIR=0000f9e1
KEYSPI start_us=7650000 end_us=7660000 sys=360000000 lsb=60000000
KEYSPI INIT_CON=00000021 INIT_MUX=00020010 SCAN_CON=00000021 SCAN_MUX=00020010 scans=66
KEYSPI change_scan=0 change_CON=00000000 change_MUX=00000000
KEYSPI PRE_CON=00000021 CLEARED_CON=00000021 FIRST_CON=00000021
```
USB heartbeats remained live; status busy0/mode0, audio frames308864/irqs4826,
audio fault0. No control/routing drift was observed at the sampled boundaries;
this does not exclude unsampled transients. The low byte of row6 did not show
completion within the10ms deadline. BAUD_WRITE29 is intent only. The remaining
concrete stock difference is DMA word transfer versus our polled BUF bytes;
an isolated two-byte DMA experiment is the next candidate, not a proven fix.
No restart, UBOOT command or flash was sent during this observation.

## Scanner timeout snapshot diagnostic — 2026-09-29 JST

The installed zero-frame-sleep image4e3189d6... stopped. Live COM10 status
reported `NES FAILURE source=KEY_SCAN rc=-3 frame=72 lcd=0 audio=0 q=0`
twice, with worker idle, audio frames348160/irqs5440 and USB heartbeats still
running. This localizes the stop to the scanner timeout path; it does not
establish why SPI completion was missing. No restart was sent while reading.

Preparation: `build/nes-smb1-keyspi-diag`. The scanner now snapshots CON,
BAUD, MUX, PA_OUT/PA_DIR, row, high/low byte phase, transmitted byte, start/end
time and poll count BEFORE stopping/resetting SPI2. Reason bits distinguish
elapsed-time limit(1) from the poll limit(2), or both(3). BUF is not read.
Snapshots survive stop and are cleared on an accepted scanner start. NES
copies three formatted KEYSPI lines into lock-protected persistent storage;
TEST STATUS republishes these after queue overflow or other peripheral tests.
NES diagnostic records clear only on a new NES run. Reported sys/lsb clocks
are sampled after the scanner returns, not atomically with the SPI snapshot.
The supplied timer has millisecond resolution despite microsecond units.

This is instrumentation, not a timeout fix. Frame sleep remains0, auto-skip
OFF, gain4, LCD offset0, and the10ms/one-million-poll limits and fail-closed
behavior stay unchanged. No retry or automatic recovery was added. Only
failure paths read extra peripheral registers or format diagnostic lines.
Source rollback: `../../private-backups/nes-keydiag-source-20260929-014657/`.
All16 NES and12 USB/peripheral host suites pass, including pre-reset snapshot,
both byte phases, clock wrap/frozen clock, completion winning a timeout race,
unchanged key output on failure, retention, event overflow and fresh-run reset.
The linked-image audit initially rejected a moved boot-trace offset. Reviewed
disassembly confirms ota_status0x1c196f0 +284 = fm1_boot_trace0x1c1980c using
`d1 ec 0f 11`; that exact NES-only encoding was added to the existing audit.
All other instruction/call/address checks remain enforced.
Build/static audit,27 linked-image tests and all9 package-preservation checks
pass. Application203824 bytes, SHA-256
`6865a7aa1bf1c4674c8914ccec0112430db6a4501bd28776646d2025602ce7a4`.
Full1MiB image SHA-256
`b4ccb9b1f7ab41701e13f454510f2698d49152c8e488c69b989807ed353a7b1e`,
under `../../private-backups/usb-diag-candidates/<sha>/fm1-usb-diag.UNQUALIFIED.bin`.
Offline comparison with the installed4e3189d6... saved readback remains inside
the existing0x4000..0x35000 writer scope; no writer changes needed.
Flashed at the user's request via existing jluboottool.py after serial UBOOT.
Receipt: `../../private-backups/nes-keyspi-diag-write-20260929-071330/deployment.json`.
Status written_and_twice_readback_verified, exit0; all50 changed sectors
verified, directory0x4000 last. Both full1MiB readbacks independently match
b4ccb9b1... . No pre-write dump, writer/environment change or automatic reset.
This deployment's final-2.bin is the next saved baseline. Normal boot remains
untested; collect TEST STATUS after failure before any NES restart.

Bench observation after normal boot: initially advancing around9 FPS; observed
frame223 through584. At uptime about66 seconds it stopped with:
```
NES FAILURE source=KEY_SCAN rc=-3 frame=584 lcd=0 audio=0 q=0
KEYSPI reason=1 row=7 phase=1 tx=7f elapsed_us=10000 polls=11
KEYSPI CON=00000021 BAUD=00000000 MUX=00020010 PA_OUT=00000000 PA_DIR=0000f9e1
KEYSPI start_us=66060000 end_us=66070000 sys=360000000 lsb=60000000
TEST END mode=7 result=-3 frames=2885824 irqs=45091
```
USB heartbeats continued afterward. No restart or UBOOT command was sent.
BAUD reads0 at failure despite scanner initialization writing29. Correction:
this register is write-only; this read is not evidence that it became0. Likewise
the clock values are failure-time reports, not proof of a clock transition.
The linked clock-critical table contains clock_timer1 and clock_port, not an
identified SPI clock callback. Investigate readable control/status and
transfer/clock interactions before claiming the cause or applying a repair.

## Remove unconditional frame sleep — 2026-09-29 JST

The user confirmed the sleep1 diagnostic runs, but music remains broken/quiet.
Live serial observation reached uptime333420ms, NES frames2779, fault0,
shown2779/skipped0; TEST STOP completed result0. The earlier measurement in
`nes-failurediag-write-20260929-012711/running-baseline-20260929-013127.log`
was approximately8.33 emulated FPS with86.1% audio underruns. This is evidence
of sustained running and severe starvation, not correct audio or full speed.

At the user's request, change only the unconditional frame-boundary sleep
from `os_time_dly(1)` to `os_time_dly(0)` and update its diagnostic banner.
Gain4, offset0,3840-byte LCD DMA, O2, scanner repair and persistent failure
reporting remain unchanged; automatic frame skip stays OFF. Conditional
ahead-of-deadline pacing, queue-full waits and LCD initialization delays remain.
Previous zero-delay builds stopped; collect TEST STATUS before restarting a
failed game to preserve the first-failure record. No stability claim yet.

Build: `build/nes-smb1-failurediag-sleep0`, application203184 bytes, SHA-256
`c630cdef0926ceb7e17ac0f87d92f1b38386d30ea2dbfb22a5687bcd57057b3e`.
Full1MiB candidate SHA-256
`4e3189d6ebb057dc5bf50f67e557ce7f7126810b9aa3e90806add7fda1e10bd1`.
All12 USB/peripheral suites,27 linked-image tests and9 package checks pass.
Deployment directory: `../../private-backups/nes-sleep0-write-20260929-013806/`.
Saved baseline is `nes-failurediag-write-20260929-012711/final-2.bin` (e33fb72a...).
Deployment completed: written_and_twice_readback_verified, exit0,48 changed
sectors verified, application directory0x4000 last. Both full readbacks match
4e3189d6... independently. Existing jluboottool.py, no pre-write device dump,
no automatic reset. This deployment's final-2.bin is the next saved baseline.
Normal boot, speed, audio and stability remain untested.

## Persistent failure diagnostics / sleep A-B — 2026-09-29 JST

The scanner-repair image still stops. Receive-only/status capture and a serial
TEST NES reproduction are saved under `nes-keyfix-write-20260929-011851/` as
`freeze-status-20260929-012120.log` and `freeze-reproduce-20260929-012149.log`.
The latter reaches frame34 then TEST END mode7 result=-3 after roughly5 seconds;
LCD teardown reports error0 and USB remains responsive. That older final log
cannot distinguish every possible I/O source. The timeout-race patch is NOT a
hardware-qualified explanation or repair for this continuing failure.

At the user's request, the next diagnostic restores ONLY the scheduling policy
from os_time_dly(0) to the original os_time_dly(1) on each frame boundary and
adds first-failure reporting. Offset0, gain4,3840-byte DMA, -O2 and the scanner
race repair remain; automatic frame skip is disabled.

`NES FAILURE source=... rc=... frame=... lcd=... audio=... q=...` records the
first failing operation. Sources distinguish KEY_SCAN/KEY_START, LCD_WRITE/
LCD_INIT, PCM_PUSH/PCM_TIMEOUT/PCM_IRQ/PCM_CANCEL, AUDIO_IRQ, IIS_OPEN/IIS_RATE,
ROM, KEY_MAP, BOARD and a CORE fallback. The record persists across teardown,
serial disconnect and event queue overflow. TEST STATUS republishes it; only
starting a new NES run clears it. Capture TEST STATUS before restarting a
failed game. Queue depth is captured under the audio lock; the failure record
is copied under the control lock, never holding both simultaneously.

Build: `build/nes-smb1-failurediag-sleep1`, application203152 bytes, SHA-256
`2658ce4e66cfe9efaf8fab846cda8b4d37601526ab90972947117e4259b2ac9b`.
Full1MiB candidate SHA-256
`e33fb72a1aa12faf119dd603f49281c6188287a75779c3842b21299b6de6b6bc`.
All12 USB/peripheral suites,27 linked-image tests and9 package preservation
checks pass. New tests check named failure sources, first-error retention,
publication after overflow/teardown, full-queue100ms timeout, clearing on a new
run, and the restored10ms frame sleep. No clock, pin or core source changes.
Source rollback: `../../private-backups/nes-failurediag-source-20260929-012427/`.
Deployment directory: `../../private-backups/nes-failurediag-write-20260929-012711/`.
The saved baseline for that operation is the preceding2ac4f714... readback.
Completed with written_and_twice_readback_verified, exit0,50 sectors verified,
directory0x4000 last, and both full readbacks matching e33fb72a... independently.
No pre-write dump or automatic reset. This directory's final-2.bin is now the
baseline for subsequent writes. Normal boot/behavior testing remains pending;
the diagnostic is not yet evidence that the freeze has been fixed.

## Bench regression and follow-up — 2026-09-29 JST

The speed/audio candidate c435bf0... was installed with the existing writer;
`../../private-backups/nes-speed-audio-write-20260929-010740/deployment.json`
reports written_and_twice_readback_verified, exit0, all50 sectors verified,
directory0x4000 last. Both1MiB readbacks independently hash to c435bf0...
The writer's opt-in NES bound was extended by exactly one sector to0x35000,
with boundary/deployment tests passing; default/stock-power limits unchanged.
No pre-write device dump and no automatic reset were used.

After normal boot, the user reports unresponsive keys and a frozen picture.
Live logs caught NES frames52 fault=-3 then TEST END mode7 result=-3;
TEST STATUS confirms worker idle and audio fault0 while USB heartbeats continue.
The NES fault is set by the key scanner's failed SPI2 sweep, not an LCD timeout.
A four-second standalone TEST KEYS completed result0. TEST NES restarts the
game without reboot; its next eight-second observation reached61 frames with
fault0. This is intermittent, not a successful stability qualification.
Speed during that run was only about9–10 FPS; no60 FPS claim.

A new host regression reproduces a scanner timeout race: status initially busy,
then preemption advances the wall clock20ms while hardware finishes. The old
code stops the scanner without checking the newly asserted completion flag.
The regression failed before the fix and passes after it. The fix starts the
timeout after launching the byte and rechecks completion before declaring a
timeout. Genuine stuck-clock/stuck-controller tests still pass and fail closed.
This establishes a real software defect consistent with the failure, but not
yet proof of the hardware root cause or a bench-qualified repair.

Automatic LCD-only frame skipping is implemented and host-tested as an explicit
`--nes-auto-skip` build option, OFF by default to isolate the scanner repair.
It retains a60Hz deadline, drops whole presentations only when at least one
frame late, caps catch-up debt at100ms, and forces a visible frame after at
most5 consecutive skips. CPU/PPU/APU and key callbacks still run on every frame;
both half-frame LCD blocks and pixel conversion are omitted on skipped frames.
Tests cover normal60Hz/no skips, catch-up, both halves, continued audio/input,
skip limit, timeout/STOP, wraparound and full-core execution with slow fake LCD.
Serial telemetry adds shown/skipped counts. Physical effectiveness is untested.
The earlier auto-skip candidates have NOT been flashed; do not deploy them
without rebuilding against the current scanner fix and explicit option.

Scanner-repair-only candidate: `build/nes-smb1-keyfix-only-v1`,
`nes_auto_skip=false`,202512 application bytes, application SHA-256
`e0462735f04bacdff67db5c6f52e14b1c992228e2912239af7a26bee8c789f64`.
Full1MiB candidate SHA-256
`2ac4f714688f6fe372da84754497d758a8807330e851b2729e5db3c9eddf6d7e`.
Validation:12 USB/peripheral suites (auto-skip both off/on),16 NES suites,
27 linked-image tests and9 preservation checks pass. Timing diagnostics add
shown/skipped counters; auto-skip remains disabled for this bench repair.
Deployment receipt: `../../private-backups/nes-keyfix-write-20260929-011851/`.
Completed with written_and_twice_readback_verified and exit0; all50 changed
sectors verified, directory last, both1MiB readbacks match2ac4f714... .
Device left in the helper without automatic reset; normal boot test pending.
Its saved preflight baseline is the preceding c435bf0... verified readback,
not the original stock or first NES image. Hardware stability and a causal
explanation still require retesting. Its verified final-2.bin is now the saved
baseline for subsequent writes, not the previous c435bf0... image.

## Speed/audio candidate — 2026-09-29 JST (built, not flashed)

Supersedes the offset-only candidate below for the next bench test:

- NES row origin remains0; full picture addresses0..239.
- Pixel DMA uses a separate64-byte-aligned3840-byte internal-RAM buffer,
  completing each transfer before reuse. Full-frame pixel transactions fall
  from2880 to30 (96x fewer transactions, not a96x FPS claim). Parameters and
  the stock startup40-byte black-fill sequence retain the original64-byte
  buffer. GPIOs, BAUD and display-on sequence are unchanged.
- `nes_stop` uses `os_time_dly(0)` instead of an unconditional one-tick sleep.
  The pinned linked SDK implementation branches directly to `vPortYield` for
  zero, without adding the task to the delayed list. USB/system workers have
  higher priorities. Watchdog/cancellation checks remain. Existing ahead-of-
  deadline frame waits are unchanged and still have10ms tick granularity;
  lower-priority tasks are not guaranteed execution merely by a zero yield.
- The seven emulator/board/audio-queue sources listed in the build manifest
  use `-O2` with LTO; SDK, startup and peripheral code retain their original
  `-Oz` flags. No pinned dependency sources were edited.
- The user reports no BGM while playing World1-1. A timed Start press after
  frame120 produces nonzero SMB1 audio in the host core (600 frames,
  441000 samples, raw sample range0..78). Holding Start through reset leaves
  the title silent, so the embedded-ROM test now uses a real press/release.
  This verifies signal generation, not audible music fidelity on hardware.
- Game gain increases1 to4 (+12dB); the previous observed raw range and gain1
  implied a peak no greater than about-52dBFS after DC removal, versus the
  audible diagnostic tone's-42dBFS. Gain4 still bounds arbitrary8-bit input
  to1020/32768 PCM full scale. Startup mute/ramp and underrun silence remain.
  This does not by itself cure the previously measured audio starvation.
- **No automatic frame skip.** Every emulated frame is still rendered and
  presented. Physical FPS, sound and offset correction await bench testing.

Build: `build/nes-smb1-speed-audio-v1`; application202384 bytes, SHA-256
`e10c199c76999545a01d4c208a9523f3924e2980a1487b23c77f7ce5586a8cf3`.
Packaged1MiB image:
`../../private-backups/usb-diag-candidates/c435bf0fea5d7356f23390935a81598162e6f473f6e1dc359dafda589a5254a5/fm1-usb-diag.UNQUALIFIED.bin`;
SHA-256 equals the containing directory name. Source rollback for the initial
speed edits: `../../private-backups/nes-speed-source-20260929-005759/`.

Validation:11 USB/peripheral suites,16 NES host suites,27 linked-image/negative
tests, static boot audit and9 package-preservation checks pass. New tests cover
3840+2-byte splitting, all115200 pixel bytes in30 DMAs, unchanged startup,
large-DMA timeout/shutdown, aligned internal-RAM placement and corrupted
placement rejection, zero-delay STOP hook, gain4 bounds and timed-Start SMB1
audio. No device write/reboot or new writer/environment was used. The installed
rollback remains `nes-smb1-stock-sequence-write-20260929-003836/final-2.bin`.
Offline comparison finds50 changed sectors,0x4000 through0x35000. The existing
NES writer guard currently ends at0x34000: installation must first explicitly
review and test the one-sector scope extension in the existing writer, without
bypassing its guard, and retain directory-last/two-readback verification.

## Offset correction — 2026-09-29 JST (built, not flashed)

The user confirmed that the installed first integration displays Mario, but is
slow and cuts off the lower picture. Receive-only serial observation showed
approximately 7.7–8 emulated frames/second, fault0 and sustained audio underruns.
These observations supersede the initial awaiting-boot status below; they do
not qualify controls, audio quality or recovery on the game build.

The NES runtime previously overrode the default row origin to40, drawing rows
40..279. The working stock fill overrides the panel-init table to a zero-origin
window. The correction sets the NES runtime origin to0, addressing all 240
picture rows at0..239. Stock initialization, DMA chunk size, clocks, scheduler,
audio, input and USB recovery code are unchanged. Physical clipping correction
still needs observation after installation; a framebuffer alone cannot verify
the visible panel aperture.

- Source rollback: `../../private-backups/nes-offset-source-20260929-005140/`.
- Build: `build/nes-smb1-y0`.
- Application: 194864 bytes, SHA-256
  `0fd42ced71fc5fbe575a1d15e69e77c21a60f7ff89afaeb38eca3e53e48eab74`.
- Packaged 1MiB image: SHA-256
  `b067b2eab4b88f1f2058e87993d67ef3aaf99e8e5b7a84f4a7f8b102af89a7f5`;
  `../../private-backups/usb-diag-candidates/b067b2eab4b88f1f2058e87993d67ef3aaf99e8e5b7a84f4a7f8b102af89a7f5/fm1-usb-diag.UNQUALIFIED.bin`.
- Validation: all11 USB/peripheral native suites and26 linked-image/negative
  tests pass; static boot audit and all9 packaging preservation checks pass.
  The lifecycle test checks actual CASET/RASET bytes for both row0 and row239,
  in addition to checking the runtime configuration.
- No device write or reboot performed for this correction. Installed-image
  rollback/baseline remains the first NES deployment's verified `final-2.bin`.

## FPS investigation — no speed changes yet

The installed telemetry measures whole-frame throughput, not individual stage
time. The following are code-derived costs, not a measured stage profile:

- Each picture is240x240x2 =115200 pixel bytes, split into30 eight-row strips.
  Each strip supplies3 LCD commands and8 window parameter bytes:115530 total
  SPI bytes/frame, of which pixels alone need55.296 Mbit/s at60 pictures/s.
- `fm1_display_write` splits the pixels into2880 blocking40-byte DMA transfers.
  There are also60 parameter DMA transfers and90 byte-polled commands/frame.
  Each DMA copies into persistent RAM, toggles CS, reads the timer and polls
  completion; CPU emulation cannot overlap these transfers.
- `nes_stop` sleeps one10ms RTOS tick every emulated frame. At roughly130ms
  observed per frame, eliminating10ms alone would only yield roughly8.3 FPS,
  assuming everything else unchanged. Preserve USB/task scheduling when fixing
  this; do not simply remove all yielding or watchdog service.
- Board pacing resets its deadline when behind; it does not intentionally
  impose another full16.7ms sleep on already-late frames. Its microsecond API
  actually uses a millisecond timer and rounded10ms sleeps on this port.
- The core and board adapter are compiled with SDK `-Oz -flto`. Speed-oriented
  optimization of hot CPU/PPU/APU and conversion code is another candidate,
  subject to ROM-result, RAM and image-size regression tests.
- `NES_FRAME_SKIP=0`: every picture is rendered and sent. The audio queue is
  underfed at this speed; queue backpressure is not established as the cause.

Next experiment: accumulate elapsed time for LCD calls, key scanning, audio
queueing and frame sleeps over many frames, then compare with total time.
Residual time includes emulator/conversion work and preemption, not pure CPU
execution. Keep logging outside the transfer loop. Change one variable at a
time: larger persistent-RAM pixel DMA chunks (480/3840 bytes, leaving stock
startup unchanged), deadline-aware yielding, then hot-path optimization.
If display bandwidth remains limiting, decouple LCD presentation rate from
60Hz emulation/audio; dropping LCD presentations is not skipping CPU frames.
Do not assume the core's compile-time frame-skip mode preserves every PPU
side effect without dedicated regression checks.

The local generic Jieli SPI notes describe BAUD+1 division, but do not establish
the WL82 SPI1 source clock in this running configuration. BAUD=4 is confirmed
in our code; actual SCLK and panel timing tolerance are not measured here.
Therefore no definite wire-time ceiling, clock increase or60 FPS promise is
claimed. Larger DMA chunks reduce transaction overhead, not bytes on the wire.

## Candidate — 2026-09-29 JST

This integrates the existing pinned NES emulator and private user-supplied
`smb1.nes` into the USB peripheral diagnostic's working SDK/RTOS startup.
It is not the earlier blank-screen NES boot profile. Physical gameplay,
orientation, frame rate and sustained audio are still bench tests.

- Build: `build/nes-smb1-stock-sequence`.
- Application: 194864 bytes; SHA-256
  `5e9798c3d23d1b1998598f8163bb408bbe7765e83f123ac81534c63154f34ac3`.
- Full private image: 1048576 bytes; SHA-256
  `1e1ba2fe9152ab75f28f7b20a8351ce5d74b35f6691153f3d04dfb8d2a541028`.
- Image: `../../private-backups/usb-diag-candidates/1e1ba2fe9152ab75f28f7b20a8351ce5d74b35f6691153f3d04dfb8d2a541028/fm1-usb-diag.UNQUALIFIED.bin`.
- ROM: private `smb1.nes`, 40976 bytes, mapper0; SHA-256
  `f61548fdf1670cffefcc4f0b7bdcdd9eaba0c226e3b74f8666071496988248de`.
- Retained working diagnostic/rollback:
  `../../private-backups/lcd-stock-sequence-write-20260928-235532/final-2.bin`,
  SHA-256 `57982447cb35c4b173afc513881bcc64c6ed865db4356a8c7def5feaf19c5ec9`.
- Deployment: `../../private-backups/nes-smb1-stock-sequence-write-20260929-003836/`.
  Completed with `written_and_twice_readback_verified`, process exit0. All 48
  changed sectors verified (application through0x33000, directory0x4000 last).
  Both full 1MiB readbacks match the image SHA-256 above. No pre-write hardware
  dump or automatic reset. The existing diagnostic's serial UBOOT command
  succeeded before flashing. The new game image awaits a normal power-cycle
  and visible/audible/control/recovery observations.

## Runtime

Normal power-on queues SMB1 automatically on the peripheral worker. The worker
has a 4096-word stack; the separate USB worker and app dispatcher remain intact.
The game continues without a serial terminal. Explicit TEST STOP or UBOOT
cancels it; a UBOOT request while busy first stops it and requires retry after
TEST END. RX faults also cancel. Other peripheral tests cannot run concurrently.

Commands are LF-only, 115200/8N1, no flow control:

- `HELLO`: contains `LCD=STOCK-SEQ NES=SMB1`.
- `TEST STATUS`: mode7 while NES is running; audio counters and worker state.
- `TEST STOP`: stop game, close audio/scanner/SPI; retain panel's last picture.
- `TEST NES`: restart SMB1 after stopping.
- `UBOOT`, then `UBOOT CONFIRM` within five seconds after the armed reply.
- Existing LCD/audio/key/knob tests remain available when idle.

The recovered PA2/GPIO/table/black-fill/display-on sequence is unchanged.
NES RGB565 blocks use the existing board adapter (256-to-240 fit, y-origin40)
and synchronous 40-byte chunks through the same internal-RAM DMA buffer.
No DMA retains the emulator's reusable pixel buffer. The diagnostic's slow
yield-every-64-block black initialization remains; gameplay does not repeat it.
USB and watchdog are serviced on frame boundaries; this is not a 60fps claim.

ALINK portC/channel3, 44100Hz/24-bit geometry and IRQ wrapper match the audible
diagnostic. The existing DC-removal/queue conversion feeds it with gain1 and
the stock-derived silence/ramp envelope. Underruns output silence and are logged.
`NES frames=... audio=... underrun=... fault=...` appears approximately once per
second with a terminal open. Queue and ISR ownership are protected by the same
IRQ-safe lock. Stream queue waits are bounded, and teardown disables the IRQ
before closing the channel. Listening quality remains untested for this image.

Use the leftmost occurrence of each piano note:

| Key | NES action |
|---|---|
| F# | Start |
| C# | Select |
| C / D / E | Left / Down / Right |
| D# | Up |
| A | A / jump |
| G | B / run |

## Encoder correction included for TEST KNOBS

Stock RAM `01c045ce..01c046e2` continuously chains two-byte row DMA and publishes
completed scans; `01c04750..01c047a6` accepts an AB change only after an identical
repeat sample. The old test sampled once per RTOS tick and decoded immediately.
The new test requests a 250us SDK microtimer wakeup, coalesced by a semaphore;
the IRQ does not poll SPI or log. The worker performs the entire scan and applies
the stock two-sample filter. Timer allocation/dead-timer failures stop the test.
Cancellation, timer deletion, late callbacks and semaphore cleanup are tested.
`INPUT END` reports scans, maximum millisecond gap, wakes and coalesced ticks.
This is not an exact reproduction of stock IRQ/DMA cadence and is not yet
hardware-qualified. Invalid transitions remain visible; no missing edges are
invented. Logs add the physically confirmed SELECT/ALGORITHM/KNOB1-4/PRESETS names.

SPI2 setup now writes configuration CON4020 before BAUD29, then enables CON4021,
following stock `020044a4..020044fe` with IRQ disabled. The preceding code wrote
BAUD while CON was zero. This is an identified ordering difference, not proof
that the divider caused every observed invalid transition.

Knobs are not needed to start/play SMB1 and are not decoded in NES mode; its
piano keys use the existing per-frame scanner and board debounce.

## Validation

- 11 USB/peripheral native suites, including NES protocol/lifecycle/STOP,
  disconnect behavior, failure cleanup, stock LCD stream chunking and timeouts.
- 16 NES native/host suites, including embedded SMB1, emulator, board, scanner,
  startup bridge, audio and services models.
- 26 linked-image/evidence/negative tests, normal and optimized Python.
- 11 existing USB Python tests.
- 7 existing-writer/deployment tests and 4 transport guard tests; fake device only.
- Exact ROM hash/XIP placement, 69240-byte NES state in internal BSS, startup
  instructions, RAM copies, task registrations, USB/ALINK interrupt returns,
  power, serial UBOOT and aligned persistent LCD DMA checked by the ELF audit.
- Link reports 408844 heap bytes after static allocation; this does not measure
  runtime peak heap/stack use. No external SDRAM is required.
- All nine stock-layout CRC/offset/boot/config/reserved/tail checks pass.

The audit retains exact reviewed boot-trace instructions at the new merged
global offset288 (encoder-only offset244). It does not bypass startup checks.
The existing writer adds explicit `--fm1-nes-cdc` scope through sector0x34000;
default/stock-power bounds are unchanged. The existing deployment function still
requires the exact changed-sector set, directory-last order, per-sector verify
and two full readbacks. No new writer, environment or flash script was created.

Build using existing Python/toolchain:

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --nes-smb1 --out firmware/usb-diag/build/nes-smb1-stock-sequence
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/nes-smb1-stock-sequence
```
