# FM-1 USB speed, tracker UVC and UAC capture

Status: **2026-10-11**. Work item: [issue #11](https://github.com/Keitark/fm1-tracker/issues/11).

## Evidence so far

| Item | Current evidence |
| --- | --- |
| Connected FM-1 USB0 CDC | Windows hub metadata reports **full-speed, 12 Mbit/s**. |
| USB1 high-speed path | Bounded PHY probe flashed, full-readback verified, and executed. **No fresh host reset or HSMODE was observed.** USB0 CDC returned. |
| Earlier native tracker screen UVC | First CDC/UVC composite flashed and full-readback verified. The user subsequently reported **clear video in OBS on Windows 11**. The iPad photo shows garbled tracker text. Earlier FFmpeg/DirectShow opening failed; that historical failure does not describe every host application. |
| Installed combined tracker CDC/UVC/UAC | Protected full readback and boot pass. Windows reports CDC, camera and UAC present without errors. A concurrent run received **60 complete 256 × 240 frames** and **12.31 s of 44.1 kHz PCM16 stereo audio**. UAC telemetry reports recurring FIFO starvation/silent-fill events; clean capture timing is **not accepted yet**. |
| Buffered combined correction | New source uses a 256-frame cache-SRAM FIFO, 128-frame priming and wider occupancy thresholds. The reproduced scheduling-jitter regression has zero settled loss. Target static audits and protected plan pass; this correction is **not yet flashed**. |
| iPad compatibility mode | Optional **256 × 240** video centers the native 240 × 240 screen with black side padding. A 480-to-512-byte row-stride mismatch reproduces the photographed geometry, but the app's internal handling and the fix on an iPad remain **unverified**. |

The linked USB1 probe application is **130,192 bytes**, SHA-256
`44b8c4e6bcbd3b8e030bcbd9188f3b0a6daa6e92b18cd725277aecba6eabd121`.
Its local manifest is `firmware/usb-diag/build/usb1-phy-probe-20261011/build-manifest.json`;
that build-time manifest records `LINKED_USB_DIAGNOSTIC_UNTESTED`. Subsequent
hardware receipts are retained privately, separately from the build manifest.

### Hardware probe result

The protected writer programmed **53 sectors** and verified the complete
readback against full-image SHA-256
`300f2ea8651ced9bd9a20c0c9305b01da7af87042cfa0a54741ab53a2f7972b3`.
After boot, the diagnostic reported:

```text
USB PHY status=2 ready=1 polls=1 reset=0 hs=0 power=60 samples=500 ms=5000 USB0-RESTORED
```

USB0 CDC liveness was observed with increasing uptime, **11,810 → 15,810 ms**.
The diagnostic's confirmed serial UBOOT handshake was subsequently used to
restore the prior NES firmware with the protected writer. Full-image readback
matched SHA-256 `4d4da3ab34b643034ea91ddb670fd3556b737a05835ed77fced687eae6ad345c`.
After one reset, its serial frame counters advanced **1,046 → 1,831**. Display
and audible output require physical inspection.

The result proves bounded probe execution and return to USB0 on this run. It
does not settle whether the FM-1 connector reaches HUSB or whether additional
controller/board configuration is needed. It does not prove a working high-speed
USB path.

The exact **AC7911B8** datasheet v1.1 describes FUSB1.1 and HUSB2.0; its pin table
identifies HUSBDM/HUSBDP on pins 40/41. The SDK's B0/B8 reference schematics also
describe B8's HUSB pins becoming available through its internal-flash arrangement.
These package-specific sources conflict with the broader `7911BXX` comment in
`apps/demo/demo_uvc/board/wl82/board_7911B_cfg.h`, which says no HUSB. That generic
comment is insufficient to rule out B8 HUSB. **Routing between the FM-1's USB
connector and HUSB remains unverified.**

Local reference evidence:

- `references/downloads/AC7911B8-datasheet-v1.1.pdf`: PDF pages 2 and 10.
- Pinned SDK `doc/datasheet/AC791N规格书/schematic/`: B0/B8 Wi-Fi speaker and aerial/endoscope reference schematics, revision V1.2, 2022-04-29.
- [Official SDK source](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK), pinned commit `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`.

## Read the operating speed on Windows

Run from the repository root:

```powershell
python tools/jieli-wl82/usb_speed_probe.py --out tmp/usb-speed.json
```

The [Windows speed reader](../tools/jieli-wl82/usb_speed_probe.py) queries USB hub
metadata. It does not open the device's serial interface or reset, flash, or change
drivers. Its result uses negotiated speed rather than the descriptor's `bcdUSB`
version or a port's speed capability. A USB 2.0 descriptor alone does not prove
480 Mbit/s operation.

## Bounded USB1 PHY diagnostic

The [probe](../firmware/usb-diag/hs_phy_probe.c) runs once before USB0 starts.
The [boot integration](../firmware/usb-diag/app_main.c) first holds USB0 to detach
any bootloader-inherited FUSB state. It then runs the USB1 probe, holds USB1,
waits **150 ms**, and starts the existing USB0 CDC implementation.

The diagnostic follows the pinned SDK PHY write sequence while replacing its
unbounded readiness wait with at most **20,000 reads**. After readiness, it samples
reset and `POWER.HSMODE` for a **5,000 ms deadline**, with at most **500 one-tick
sleeps**. The second bound also covers a stalled millisecond clock. A delayed task
wakeup can make the reported elapsed time exceed the deadline; it does not cause
another negotiation sample. The watchdog is serviced between sleeps.

USB1 has no descriptor/configuration context, class registration, endpoint DMA,
or installed ISR. CPU IRQs 53/54 remain disabled; controller interrupt masks are
cleared after initialization and every observed reset. Every normal exit clears
soft attachment, interrupt masks, SIE enable, PHY clock, and UTM enable.

After USB0 CDC returns, send uppercase, LF-terminated commands:

```text
HELLO
USB PHY STATUS
```

The response includes `status`, `ready`, readiness `polls`, fresh `reset`, `hs`,
last attached `power`, sample count, elapsed `ms`, and `USB0-RESTORED`.

| Status | Meaning |
| --- | --- |
| 0 `NOT_RUN` | Probe has not run. |
| 1 `READY_TIMEOUT` | USB1 SIE did not become ready within 20,000 reads. |
| 2 `NO_RESET` | No fresh host reset was observed after attachment. |
| 3 `RESET_NO_HS` | A fresh reset was observed without a later HSMODE observation. This does not prove full-speed enumeration. |
| 4 `HIGH_SPEED` | HSMODE was observed after a fresh reset: PHY negotiation evidence only. |

Stale reset status is drained before attachment. An HSMODE bit without a fresh
reset is insufficient. Even status 4 does **not** establish USB1 enumeration,
stable endpoint transfers, usable bandwidth, or UVC operation.

### Recovery and firmware restoration

`USB0-RESTORED` means the diagnostic returned to its USB0 CDC service. It does not
restore the previous application in NOR flash. Once CDC is available, the existing
two-command bootloader handshake remains available:

```text
UBOOT
UBOOT CONFIRM
```

Send confirmation only after `OK UBOOT ARMED CONFIRM-WITHIN-5000MS`, within the
five-second window. Verify the resulting UBOOT device before using the protected
host writer; disconnect alone is not proof of bootloader entry. See the
[USB diagnostic protocol](../firmware/usb-diag/README.md) and
[protected flash session](../tools/jieli-wl82/FLASH_SESSION.md).

If the diagnostic fails to return CDC, stop the automatic sequence and perform a
normal physical power-off/power-on. This reruns the installed application; it is
not firmware rollback. If CDC remains unavailable, physical bootloader recovery
is required before another write. Neither this probe nor a normal restart
guarantees recovery from a physically failed flash write.

A temporary diagnostic installation and later restoration of the old image use
the protected host writer with the exact verified baseline and readback checks.
There is **no automatic NOR rollback** in this firmware.

## Installed first CDC/UVC tracker: historical build and hardware evidence

The first UVC image used **240 × 240 YUY2 at 5 frames/s**, 115,200 bytes per
frame. It had a 3,012-byte stream state, three packet slots, and a separate
1,024-byte endpoint DMA buffer aligned to 64 bytes. CDC occupied interfaces 0/1,
VideoControl 2 and VideoStreaming 3, with ISO IN endpoint `0x84`. It did not
include UAC capture.

The linked application is **490,192 bytes**, SHA-256
`552b2f69218e3d1e09ee3de854e5eb0562056f66bf61e57d2b9d10b5d137d90b`.
The linker retained **132,364 bytes** of heap against the unchanged 128 KiB gate.
Conservative emitted bounds for the added paths were 160 bytes for TX IRQ and
512 bytes for capture work, including their caller frames. USB and peripheral
task stacks are 8,192 and 16,384 bytes. These are local code bounds, not measured
whole-program stack high-water marks.

The protected writer updated **120 application sectors**, application directory
last, and verified one complete readback against full-image SHA-256
`47093928be9de4410c464bc5e7f9927c9fce01699458b57876e8a3e5daa38c58`.
Boot observation confirmed `MOD-EDITOR/1` on COM4. Its audio clock advanced
**441,152 → 573,504 frames** with zero reported underruns and LCD/key/audio
initialization errors all zero. This replaced the previous NES application.

Windows reported CDC and **FM1 Tracker Screen**, using `usbvideo.inf`, with
PnP problem code 0. Hub metadata reported **full speed, 12 Mbit/s**. After
`MOD PLAY`, State of the Art's order/row counters advanced, display updates
were approximately 25/s, and the reported underrun count remained zero.
These are serial observations; they do not establish physical sound or control
response during a video capture.

### Historical DirectShow test and subsequent user observations

An earlier FFmpeg/DirectShow enumeration listed the FM-1 camera, but its
open/format-query attempts failed with `Unable to BindToObject`. That test
received no host frames and left video at `cfg=1 commit=0 alt=0 shutdown=0`
with all video counters zero. A serial check with DTR disabled also received no
output; enabling DTR restored replies, so that silence was not a firmware stall.
The device later disappeared from present-only Windows USB detection and was
subsequently reconnected. A local-only diagnostic under ignored
`tmp/uvc-camera-diag/` can report HRESULTs for a unique exact camera-name match
without running a capture graph.

The user subsequently confirmed **clear tracker video in OBS on Windows 11**.
Their iPad photograph shows the tracker layout and scopes arriving, with
corrupted text and a bright green footer; the camera app reports 240p/5 fps.
This demonstrates a receiving video path on the iPad. It does not identify
which copy, stride or format conversion operation caused the corruption.

A local comparison reconstructed the photographed geometry by interpreting
packed 480-byte rows as 512-byte rows. The native rendered image and the new
packetizer reconstruction compare byte-for-byte without that deliberate
mismatch. This supports a host row-stride hypothesis; it is **an inference,
not a verified diagnosis of the iPad app's internals**. The app and iPad model
have not yet been recorded.

### Historical control correction, never deployed

A separate correction added entity 0 / VideoControl interface 2 / selector 2
Request Error Code `GET_CUR` and `GET_INFO`, each bounded to one byte. `GET_INFO`
reports GET-only support; `GET_CUR` snapshots the preceding error before a
successful video control clears it. Malformed requests stall, and stale OUT
callbacks cannot change a newer request's error or pending transfer.

That linked application was **490,448 bytes**, SHA-256
`7ea8b19e2d15e91ac3a540e962a07ea1a71157c939af6103eb045bb8c89c4e0a`.
Its audit passed with **132,364 bytes** of heap. Its prepared full-image SHA-256
was `e1f7ff2a21c89304f21bb615777f94bd8459f0406c1c12722ded423aa15a1c94`.
It was **not flashed**. The correction is retained in the combined source below;
its relationship to the earlier DirectShow opening failure is unproven.

## Current combined tracker CDC/UVC/UAC source

The current profile keeps USB0 full speed and adds **capture-only UAC1** to the
tracker. UVC mirrors both playback and edit views. UAC records song playback and
sample/note audition from the existing audio engine. It does not add remote
editing controls or PC-to-FM-1 USB audio playback.

| Function | Interfaces | Endpoint and format |
| --- | --- | --- |
| CDC serial commands/recovery | 0/1 | Existing CDC endpoints |
| UVC tracker screen | 2/3 | ISO IN `0x84`, YUY2 at 5 fps, maximum 642 bytes per 1 ms interval |
| UAC tracker capture | 4/5 | Asynchronous ISO IN `0x81`, stereo PCM16 at 44,100 Hz, maximum 180 bytes per 1 ms interval |

Without UVC, the UAC-only build uses AudioControl/AudioStreaming interfaces 2/3.
The audio profile has one fixed rate and advertises no volume, mute or rate
controls. Its descriptor declares asynchronous capture to match the native IIS
clock. The combined ISO endpoint data reservation is **822 bytes per 1 ms USB
frame**; actual simultaneous host receipt remains a device acceptance check.

### Video packets and the optional iPad padded mode

The [portable stream core](../firmware/usb-video/uvc_stream.h) uses a **2,068-byte
stream state**, three bounded packet slots, no heap and reset epochs that reject
stale work. Each packet carries at most **640 consecutive YUY2 image bytes**
plus a two-byte UVC header. Packets may cross a row boundary. Rendering and
conversion run outside the transport/IRQ critical section, with at most two
RGB565 scratch rows per task iteration, after LCD work and with audio headroom.

| Video profile | Row bytes | Image bytes/frame | Image packets/frame | Advertised image bitrate |
| --- | --- | --- | --- | --- |
| Native 240 × 240 | 480 | 115,200 | 180 | 4,608,000 bit/s |
| `--uvc-ipad-pad`: 256 × 240 | 512 | 122,880 | 192 | 4,915,200 bit/s |

Both advertise a **200 ms frame interval (5 fps)** and use the same 642-byte
endpoint maximum. The padded profile renders eight black pixels on each side
of the native screen. The physical LCD remains 240 × 240. It is intended to
avoid the suspected 480/512-byte host stride mismatch, and remains available as
an explicit build option. **Its iPad image has not yet been checked.** The 192
image-bearing USB intervals leave less delivery margin than native mode's 180;
audio or display load can still cause dropped video frames.

One logical view is frozen per USB video frame. This frame rate is separate
from the LCD refresh rate. CPU-only state and scratch use reclaimed cache SRAM;
the 1,024-byte, 64-byte-aligned video endpoint DMA buffer stays in ordinary SRAM.
There is no extra full video framebuffer, allocation in the TX IRQ, IRQ rendering,
blocking endpoint write or stock camera-driver allocation path.

### Audio source and stream lifetime

The [audio capture module](../firmware/usb-audio/README.md) takes the renderer's
native stereo PCM24 output **before speaker mono folding and the local master
volume envelope**, then saturates/truncates it to PCM16. Speaker mode includes
all tracker parts locally while USB capture preserves the original stereo
mix. The local SPK/ST mode and physical volume do not select USB capture pan or
gain. The renderer advances once per audio block; capture does not render a
second copy of the song.

To select the local output, **hold SAVE and turn KNOB3** until the top line
shows **SPK** (speaker mono fold) or **ST** (stereo Audio OUT). Positive encoder
steps select ST and negative steps select SPK; physical clockwise polarity
has not been confirmed in this session. USB capture remains stereo in both modes.

A bounded 256-frame FIFO and persistent pending packet keep USB backpressure
from blocking the audio renderer. The current source places the 1,024-byte,
CPU-only FIFO in reclaimed cache SRAM, waits for 128 frames before priming and
uses occupancy thresholds of 108/172 frames. The installed first combined image
uses the older 128-frame FIFO; its correction is described below. The adapter
owns a separate ordinary-RAM
**256-byte, 64-byte-aligned audio DMA buffer**. It preserves DMA/pending bytes
on busy or incomplete ISO transactions. Nominal 44/45-frame packets and bounded
43..45-frame occupancy correction follow the independent IIS and USB clocks.

UAC alt0, configuration changes and bus reset stop and flush capture; valid
configuration and alt1 can start it again. Tracker/UBOOT shutdown permanently
latches both audio and video capture off for that boot. Host reset or later
reconfiguration cannot rearm them after quiescence. A normal firmware boot
clears the shutdown latch. The CDC `UBOOT` / `UBOOT CONFIRM` exchange and protected
writer remain required; there is no automatic NOR rollback or second image bank.

### First combined build and verified deployment

The requested combined candidate is built under local,
ignored `firmware/usb-diag/build/mod-stoa-uvc-uac-pad-20261011/`. It embeds the
SHA-256-locked State of the Art MOD and FM1BREAK bank through the existing private
input workflow, with LCD SPI 30 MHz and RGB444. Build flags include
`--mod --uvc-screen --uac-audio --uvc-ipad-pad --lcd-async`.

The installed combined linked application is **494,192 bytes**, SHA-256
`1c3f591e42aa523182c422c0604bf8104cd4db83f4aa0b397263e1010913cb0a`.
Its prepared 1 MiB full-image SHA-256 is
`7b4c8076c8a1fd03f296e821c6a164b5a01e21acfaacc643169f17a1c2e75693`.
Source/archive/overlay closure and static audits pass. Heap is **131,340 bytes**,
above the unchanged **128 KiB** gate, and cache BSS is **5,620 bytes**. The
protected application-sector request and plan were accepted against the
verified first-composite baseline.

The protected writer updated **121 sectors**, application directory last, and
verified the full readback against
`7b4c8076c8a1fd03f296e821c6a164b5a01e21acfaacc643169f17a1c2e75693`.
After exactly one reset, observation found the tracker on COM4 with zero audio,
LCD and key initialization errors and zero tracker playback underruns. Windows
reports CDC, camera and UAC all present with status OK. USB0 remains **full speed,
12 Mbit/s**. This is the currently installed image until a later verified
deployment is recorded.

### First simultaneous Windows audio/video capture

The local `tmp/check_tracker_av.py` check requested a 12-second capture. It
received **12.31 seconds of 44,100 Hz stereo PCM16 audio**: left and right differ
in **540,335 of 542,871 frames**. The host audio API reported no capture errors.
Concurrent DirectShow capture received **60 complete 256 × 240 video frames**.
These establish actual simultaneous host receipt for this image, including
distinct stereo channel data; they do not establish the padded image on an iPad.

Firmware UAC telemetry exposed a buffering flaw during that run: `under`
increased **2 → 94**, `silent` **51 → 542 frames** (approximately **0.1%** of
the recording), and startup/close `over` increased **121 → 202**. The tracker
playback underrun count stayed zero. Host receipt and a clean host API status do
not cancel these firmware starvation/silent-fill observations. **Clean UAC
timing is not accepted** for this image. The source correction and its pending
deployment have their own hashes and test evidence below.

### Buffered correction candidate, not yet flashed

The corrected build is under local, ignored
`firmware/usb-diag/build/mod-stoa-uvc-uac-pad-buffer-20261011/`. Its application is
**494,192 bytes**, SHA-256
`69fe7875e2d75ab4891359726a6e253f3520ff8f47928115034f92b1e8259838`.
The prepared full-image SHA-256 is
`ba75174ea861c54f608b940b18807070f0593b04ceabbfc59e90642ea5f3bedb`.
Static audits pass with **131,852 bytes of heap** and **6,644 bytes of cache
BSS**; the protected plan is accepted. Final source/archive/overlay closure and
emitted review pass, including the FIFO's complete startup clear. The device is
currently absent from Windows, so the protected write and clock readout await
reconnection. No second write or reset was attempted. The installed full-image
hash remains `7b4c8076c8a1fd03f296e821c6a164b5a01e21acfaacc643169f17a1c2e75693`.

The correction doubles the FIFO to **256 stereo frames**, primes after **128
frames**, and uses **108/172-frame** occupancy boundaries to preserve a callback
of reserve against submillisecond producer scheduling jitter. A separate
**1,024-byte CPU-only cache-SRAM FIFO** replaces the earlier 512-byte ordinary-RAM
FIFO member. Audio DMA remains the same 256-byte ordinary-RAM allocation. FIFO
reset/epoch handling and permanent shutdown remain bounded and tested.

An actual-core **12.311-second** regression delays one native callback by
**0.4 ms every 32 callbacks** at **+650 ppm**, while retaining the producer's
long-run sample rate. The earlier buffering produced **81 underruns and 579
silent frames** in this local regression. The correction preserves exact sample
order with **zero settled loss**, with **88 silent frames only during startup**.
All **4/4 UAC portable/adapter targets pass**. This is local simulation evidence;
clean timing on the FM-1 still requires a fresh capture after verified deployment.

The correction also adds read-only CDC command `USB CLOCK STATUS`, reporting
`clk_get` values for `sys` and `lsb`. It does not change either frequency. Hardware
clock values have not yet been collected for this candidate.

## Reproduce local verification

Run from the repository root; these tests do not contact the FM-1:

```powershell
C:\Python311\python.exe firmware/usb-audio/run_host_tests.py
C:\Python311\python.exe firmware/usb-audio/run_adapter_tests.py
C:\Python311\python.exe firmware/usb-video/run_host_tests.py
C:\Python311\python.exe firmware/usb-video/run_adapter_tests.py
cmake -S firmware/protracker -B firmware/protracker/build/host -G "Visual Studio 17 2022" -A x64
cmake --build firmware/protracker/build/host --config Debug --target capture_native_tests capture_padded_tests mod_tests
ctest --test-dir firmware/protracker/build/host -C Debug -R "tracker_capture_|mod_document_editor_player_usb|mod_starter_bank|breakbeat_demo_mod" --output-on-failure
```

Current local results:

| Test set | Result and scope |
| --- | --- |
| UAC portable + actual SDK adapter | **4/4 CTest targets pass**: two portable interface profiles and two fake-SDK adapter profiles, built with `/W4 /WX`. Includes PCM/source immutability, FIFO/epoch/backpressure bounds, six 60-second producer simulations with independent clocks at ±500 ppm, the 12.311-second scheduling-jitter regression and stale-data-free restart, descriptor/control routing, DMA ownership, reset/reopen and permanent shutdown. |
| UVC portable stream/control | **4/4 CTest targets pass**, native and padded modes, with `/W4 /WX`. Reconstructs the complete 115,200/122,880-byte frames and verifies authentic fragment metadata, boundary leases, EOF/FID, bounded controls and reset lifetime. |
| UVC actual SDK adapter | **2/2 CTest targets pass**, native and padded fake-SDK profiles, with `/W4 /WX`. Checks busy/incomplete DMA ownership, malformed OUT stall, configuration/reset, IRQ masks and permanent shutdown. |
| Tracker capture renderer | **2/2 focused CTest targets pass**, native and padded, Debug with `/W4 /WX`. Checks every row start across four views, exact pixels/black margins, buffer guards, invalid input and full-frame packet reconstruction. |
| Tracker audio renderer | **3/3 focused CTest targets pass** for document/player, starter and breakbeat MOD paths. Checks legacy SPK/ST behavior, original stereo tap, one renderer advance and song/sample/bank-preview panning. |
| Protected request policy | **1/1 focused unittest passes** for verified current MOD baseline/profile and UAC/padded-video flag constraints. |

The PHY host test remains independently reproducible:

```powershell
cmake -S firmware/usb-diag -B firmware/usb-diag/build/host -G "Visual Studio 17 2022" -A x64
cmake --build firmware/usb-diag/build/host --config Debug --target hs_phy_probe_tests
ctest --test-dir firmware/usb-diag/build/host -C Debug -R bounded_usb1_phy_probe --output-on-failure
```

Use Debug so assertions stay enabled. Local tests establish boundedness,
ownership, conversion and lifecycle contracts; they do not establish host
receipt, iPad compatibility, audible output or high-speed operation.

## Remaining combined-image hardware checks

The first combined run has verified deployment, boot, full-speed enumeration
and simultaneous stereo audio/video receipt. Remaining checks are:

- Review and deploy the buffered correction with protected readback, then repeat
  simultaneous recording with clean firmware capture telemetry over sustained
  playback and collect read-only USB clock values.
- Local SPK/ST playback, all tracker parts, controls and note audition while USB
  capture runs; confirm playback/edit screen changes and sustained received cadence.
- Video/audio close and reopen, USB reconnect/reset and fresh negotiation.
- CDC UBOOT entry while capture is active, permanent DMA quiescence and actual
  bootloader enumeration, followed by the protected boot/observation workflow.
- The padded image on the affected iPad app: clear text, correct layout and
  changing frames. Keep this pending until the user physically verifies it.

Raw 240 × 240 YUY2 at 30 fps needs **3,456,000 image bytes/s**, so that rate still
requires a working high-speed path or a different encoding/mode. The combined
full-speed implementation does not establish USB1 high-speed support.
