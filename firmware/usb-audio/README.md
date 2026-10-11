# Tracker USB audio capture

Capture-only UAC1 sends the tracker stereo mix to a connected PC while CDC
keeps serial commands/recovery and optional UVC sends the tracker screen.
This is a recording device: no PC-to-FM-1 USB playback endpoint is advertised.

The combined tracker passed target static audits, portable/fake-SDK tests and
protected deployment on 2026-10-11. A simultaneous Windows capture received
distinct stereo audio and complete padded video frames. Firmware FIFO
starvation/silent-fill telemetry exposed a buffering flaw. The current source's
buffered correction passes local regression and target static audits but is not
yet flashed; clean hardware UAC timing is not accepted yet. See hashes and results
in [the USB hardware evidence record](../../docs/FM1_USB_SPEED_AND_UVC.md).

## Fixed format and interface ownership

- Stereo PCM16, 44,100 Hz, full-speed asynchronous isochronous IN endpoint `0x81`.
  `bmAttributes=5` matches the independent native IIS clock. The inherited Doom
  descriptor used 13 (synchronous); this profile corrects that declaration.
- One fixed sampling frequency; no USB volume/mute/rate controls advertised.
- AudioControl/AudioStreaming interfaces are 4/5 with `FM1_UVC_SCREEN`, or 2/3
  with CDC alone. The 99-byte function is registered after CDC and optional UVC.
- Descriptor maximum is 180 bytes per 1 ms frame. Nominal packets contain
  44 frames, with 45 every tenth packet; bounded occupancy correction can use
  43..45 frames to track the independent IIS and USB clocks.
- UAC uses its own ordinary-RAM `fm1_uac_dma[256]`, aligned to 64 bytes. It
  never takes CDC/UVC endpoint DMA or the LCD strip.

## Producer and controller lifetime

`fm1_usb_audio_capture_push_pcm24()` accepts at most 64 stereo frames from
the existing native 44.1 kHz renderer, before speaker mono routing or physical
volume. It saturates/truncates PCM24 to PCM16 without resampling, allocation,
USB calls or waits. The input samples stay unchanged.

The tracker renders stereo once per audio block, then applies the selected
SPK/ST routing and master volume to the local output. The capture tap therefore
preserves the original stereo mix in either local mode. It includes song
playback and sample/note audition in edit mode; USB capture does not independently
advance the renderer or provide editing input from the PC.

For local output, hold **SAVE** and turn **KNOB3** until the top line shows
**SPK** or **ST**. Positive encoder steps select ST, negative steps select SPK;
physical clockwise polarity has not been confirmed in this session. USB capture
remains stereo whichever local mode is selected.

A short IRQ-safe cross-core spinlock protects the 256-frame FIFO and separate
180-byte persistent pending packet. On overflow the oldest FIFO frame is
dropped and counted. Host backpressure never blocks the audio renderer.
Endpoint arm/stop operations are outside this lock and belong to CPU0.
The CPU-only `capture_fifo[256][2]` occupies 1024 bytes of reclaimed cache SRAM
in `.memp_memory_x`; the image audit verifies its size, range and startup clear.
Endpoint DMA remains ordinary RAM. FIFO read/write reset excludes old samples
after close/reopen; priming waits for two 64-frame callbacks (128 frames).
The occupancy servo uses 108/172-frame boundaries to keep about one callback
in reserve, preventing submillisecond scheduling jitter from emptying the FIFO.

The adapter checks `TxPktRdy` before copying into DMA, rechecks the capture
epoch before committing, and preserves a pending packet on rejection.
Incomplete ISO retries existing DMA bytes and count without copying or
dequeueing another packet. One silent prime starts the stream; subsequent
packets are driven solely by TX completion IRQ. The SDK's default 1023-byte
ISO `Txmaxp` is overwritten with the advertised 180-byte maximum.

SET_CONFIGURATION 0/1, bus reset and AS alt0 stop DMA, disable its endpoint,
flush FIFO and invalidate capture epochs. AS alt1 can reopen after a valid
configuration. Explicit tracker/UBOOT shutdown uses the SDK's counted CPU0
critical section and permanently latches capture off until a fresh init.
Host reset/reconfiguration cannot restart audio after shutdown.

The global setup filter must run on every SETUP before ordinary SDK dispatch.
It handles only exact valid standard interface SET/GET_INTERFACE and GET_STATUS,
and endpoint GET_STATUS. Unsupported class/vendor requests, malformed lengths,
unadvertised controls and invalid alternate settings stall without reading OUT
data. Accepted device SET_CONFIGURATION falls through to the existing SDK.

Diagnostics are a noinline, at-most-384-byte status reply. Packet/frame counts
mean submitted to endpoint DMA; they cannot establish host receipt or audible
sound. FIFO/core/DMA state uses about 1.5 KiB and needs no heap allocation.

## Combined screen and audio profile

Build the current tracker with `--mod --uac-audio`; add `--uvc-screen --lcd-async`
for its screen. UVC advertises YUY2 at 5 fps with a 642-byte maximum packet;
UAC reserves 180 bytes, giving **822 bytes per 1 ms USB frame** for both ISO
endpoint data maxima. The code uses USB0 full speed and does not enable USB1
high speed. The first combined image received simultaneous audio/video on
Windows; the buffered correction still requires verified deployment and a
repeated capture to establish clean firmware timing.

Optional `--uvc-ipad-pad` advertises 256×240 video, centering the native 240×240
screen between eight black pixels per side. This makes the YUY2 row 512 bytes
instead of 480. Clear original video was reported in OBS on Windows 11, while
an iPad photograph showed garbled text. A deliberate 480/512-byte row-stride
mismatch reproduced that geometry; the suspected host stride handling is an
inference. The padded image has not yet been verified on the iPad. UAC's format,
endpoint and DMA allocation are the same in both video modes.

After the exact image is flashed and enumerated, select the FM-1 USB **recording
device** in the host audio or capture program at stereo/44,100 Hz. Select
**FM1 Tracker Screen** separately for video. The USB stream reflects playback
and note/sample audition while the edit screen remains visible. Confirm stereo
in a received recording; successful enumeration alone does not establish sound.

## Read capture status over CDC

With the actual tracker COM port open at 115200 baud, DTR enabled, RTS disabled
and LF line endings, send:

```text
HELLO
USB AUDIO STATUS
USB VIDEO STATUS
USB CLOCK STATUS
MOD STATUS
```

The video query requires the optional UVC profile. Audio status reports the fixed
`rate=44100 bits=16 ch=2` plus capture state, FIFO fill, pending bytes, submitted
packets/frames, silent frames, under/overruns, busy/short-write counts, incomplete
ISO retries and epoch. Compare these with the tracker underruns and a host
recording. Submitted counters do not prove received audio. Close the serial
monitor before another tool opens the same port.

The buffered correction adds read-only `USB CLOCK STATUS`, reporting `clk_get`
values for `sys` and `lsb`. It changes no clock frequencies; values on this
candidate remain to be collected after deployment.

## Local validation

```powershell
C:/Python311/python.exe firmware/usb-audio/run_host_tests.py
C:/Python311/python.exe firmware/usb-audio/run_adapter_tests.py
```

Portable capture tests run for both interface profiles and cover PCM conversion,
FIFO bounds and source immutability, pending retry/stale epoch rejection,
starvation, overflow, backpressure and six 60-second native producer simulations
including independent clocks at +/-500 ppm. A separate 12.311-second regression
delays one native callback by 0.4 ms every 32 callbacks at +650 ppm, without
changing its long-run sample rate. This test failed against the prior 128-frame
FIFO/64-frame priming (**81 underruns/579 silent frames**) and now keeps exact
sample order with **zero settled loss**, with only **88 startup silent frames**.
A restart test verifies no old FIFO sample leaks before the new priming reserve.

The actual SDK adapter compiles against fake callbacks with `/W4 /WX` and runs
for both profiles. It covers descriptor boundaries/registration, interface and
endpoint routing, invalid/class/vendor requests, 180-byte `Txmaxp`, stereo DMA
ownership, busy/incomplete ISO, configuration/reset/close/reopen, permanent
counted-IRQ shutdown, bounded diagnostics and preservation of CDC/UVC handlers
and DMA. **All four CTest targets pass locally:** two portable and two actual
adapter targets, for the CDC/UAC and CDC/UVC/UAC interface profiles. Tracker
renderer integration also passes its three focused MOD tests, including legacy
SPK/ST routing, original stereo capture, one advance per block, and song/sample/
bank-preview panning. The first protected combined deployment received **12.31 s
of 44.1 kHz PCM16 stereo audio**, with L/R distinct in **540,335/542,871 frames**,
alongside **60 complete 256×240 video frames**. The host audio API reported no
errors, but UAC `under` increased **2 → 94** and `silent` **51 → 542 frames**
(approximately 0.1%); startup/close `over` increased **121 → 202**. The buffered
correction's application SHA-256 is
`69fe7875e2d75ab4891359726a6e253f3520ff8f47928115034f92b1e8259838`;
its prepared image is
`ba75174ea861c54f608b940b18807070f0593b04ceabbfc59e90642ea5f3bedb`.
Final source and emitted review pass; it remains unflashed because FM-1 is absent
from Windows. New sustained hardware capture
evidence is required before clean timing can be accepted. Serial recovery with
active streams, physical local controls
and the padded iPad image remain device acceptance checks.

## Provenance

The capture core, header, 99-byte profile and native capture tests derive from
`F:/dev/fm1-doom` commit `3d8a093ddadd7e93a210c3847db6d57ce99202c0`:
`src/fm1_usb_audio_capture.c`, `src/fm1_usb_audio_profile.c`,
`include/fm1_usb_audio.h`, and `tests/usb_audio_capture_tests.c`.
The target adapter starts from that project's `src/fm1_usb_audio_target.c`,
then adopts bounded direct endpoint handling and counted shutdown from this
repository's `firmware/usb-video/fm1_uvc.c`. This adaptation is GPL-2.0-only.
