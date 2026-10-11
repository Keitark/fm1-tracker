# FM-1 tracker screen over USB

## Status

The optional `--uvc-screen` firmware profile implements a UVC1.0 screen function
alongside the tracker's USB CDC serial interface. Add `--uac-audio` to expose
tracker audio as a separate UAC1 capture function. Portable stream/control tests
and fake-SDK adapter tests pass for both native and optional padded video modes.
**Hardware acceptance is host and build specific:** a linked
image, submitted packet counters or a camera entry in Windows does not establish
that the host received complete moving frames. The first composite image was
flashed and booted on 2026-10-11; Windows camera/CDC enumeration and tracker
telemetry passed, but DirectShow camera opening failed before COMMIT/alternate 1.
See the exact hashes, subsequent host results and remaining work in
[the USB speed and hardware evidence record](../../docs/FM1_USB_SPEED_AND_UVC.md).

This directory contains independent screen transport code. No private MOD,
sample-bank or recorded-audio assets are included.

## Video mode

| Item | Fixed profile |
| --- | --- |
| USB path | USB0 full speed; this profile does not enable USB1 |
| UVC version | 1.0, 26-byte PROBE/COMMIT controls |
| VC error status | Entity 0 Request Error Code GET_CUR/GET_INFO, bounded to one byte |
| Host camera name | `FM1 Tracker Screen` |
| Frame | Default 240×240; optional 256×240 with centered native screen and black side padding; uncompressed YUY2, BT.601 limited range |
| Advertised frame rate | 5 fps; frame interval 200 ms |
| Interfaces | CDC 0/1; VideoControl 2; VideoStreaming 3; optional AudioControl 4/AudioStreaming 5 |
| Video endpoint | ISO IN `0x84`, maximum 642 bytes, 1 ms interval |
| Streaming switch | VS alternate 0 stops; alternate 1 starts after configuration and COMMIT |
| Audio | Existing FM-1 speaker/Audio OUT; optional UAC1 capture, 44,100 Hz stereo PCM16, ISO IN `0x81` maximum 180 bytes |

The USB video frame rate is separate from the LCD refresh rate. A screen
snapshot is frozen for each video frame while the device display continues its
normal updates. Both playback and edit screens use the same screen snapshot.
The tracker produces video raster fragments only after LCD work and when
the audio queue has sufficient headroom. Camera delivery may therefore drop
frames under load; the profile does not promise 5 received frames every second.

One native image is 115,200 bytes. Each packet carries up to 640 consecutive YUY2
raster bytes plus a two-byte UVC header, crossing row boundaries as needed;
180 full packets carry one image within the 200 ms frame period. A fragment
needs at most two RGB565 scratch rows. The 642-byte video maximum leaves room
for the optional audio endpoint's 180-byte maximum; their combined ISO data
reservation is 822 bytes per 1 ms USB frame. Actual simultaneous host delivery
remains a hardware acceptance check. Transport uses three small CPU packet slots and a separate,
64-byte-aligned 1024-byte endpoint DMA buffer. CPU state/scratch is placed in
reclaimed cache SRAM; endpoint DMA stays in ordinary SRAM. There is no full
additional video framebuffer, IRQ rendering, blocking endpoint write or stock
camera-driver allocation path.

### Optional iPad row-stride compatibility mode

`--uvc-ipad-pad` requires `--uvc-screen` and builds a 256×240 mode instead of
the default 240×240 mode. The LCD screen remains 240×240; the video producer
adds eight black pixels to each side, making each transmitted YUY2 row exactly
512 bytes. The frame becomes 122,880 bytes, with 192 packets within the same
200 ms period, a 4,915,200 bit/s image bitrate and the same 642-byte endpoint
maximum. Packet slots, endpoint DMA and stream-state allocation are unchanged.

The observed iPad image skew is consistent with reading packed 480-byte native
rows using a 512-byte row stride. That is an inference from reproducing the
image geometry, not a verified diagnosis of the iPad application's internals.
The padded mode addresses that suspected stride mismatch; its iPad display
still needs a hardware check. Native mode remains available for hosts such as
OBS that displayed the original image correctly. The padded mode has 192
image-bearing USB intervals per frame, so its delivery margin is narrower than
the native mode's 180 intervals.

## Build the optional profile

From the repository root, with the pinned SDK and Jieli toolchain already set up:

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --mod --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --lcd-async --uvc-screen --uac-audio --out firmware/usb-diag/build/mod-uvc-audio
```

This uses the current tracker sources. `--uvc-screen` requires `--mod` and
`--lcd-async`; it excludes preserved `--mod-source-dir` profiles and the separate
`--hs-phy-probe` diagnostic. LCD SPI/RGB options and optional embedded MOD/bank
inputs remain explicit build choices. Private embedded inputs require their
existing SHA-256 locks and are not supplied by this guide.
Add `--uvc-ipad-pad` to the command for the optional iPad compatibility mode.

The command builds and audits an application; it does not flash the device.
Use the protected, verified application-sector deployment workflow for the
specific unit. Its baseline, image hash, readback and reset checks still apply.

## View and record on Windows

Use these steps after the exact UVC image has been installed and enumerates:

1. Connect FM-1 with a USB data cable and let Windows enumerate both functions.
   In **Settings → Bluetooth & devices → Cameras**, look for
   **FM1 Tracker Screen**. Keep the actual COM-port number for serial status;
   it may change after composite-device installation.
2. Open **Camera** from Start. If another camera opens, use **Change camera**
   until FM-1's screen is selected. Where a capture program offers manual mode
   selection, request **240×240 / YUY2 / 5 fps**, or **256×240 / YUY2 / 5 fps**
   for an image built with `--uvc-ipad-pad`.
3. Confirm the image changes when you move the tracker cursor and during song
   playback. A camera name or static first frame alone is insufficient.
4. To record, select the Camera app's **Video** button, start recording, then
   use **Stop**. Open the resulting thumbnail to inspect the saved clip.

These Windows controls are described in Microsoft's
[Camera app guide](https://support.microsoft.com/en-us/windows/hardware/camera/how-to-use-the-windows-camera-app)
and [camera settings guide](https://support.microsoft.com/en-us/windows/hardware/camera/manage-cameras-with-camera-settings-in-windows-11).
If Windows blocks camera access, check the existing camera permission setting
for the application you chose. Close other camera capture programs while
performing the acceptance check.

The camera stream carries the screen only. With `--uac-audio`, choose FM-1's
USB audio recording device separately in the host capture program to record
the tracker stereo mix while viewing or editing its screen. UAC sends audio
from FM-1 to the PC; it does not implement PC audio playback into FM-1.

## Read video status over CDC

Find the FM-1 COM port without opening it:

```powershell
C:\Python311\python.exe tools/jieli-wl82/usb_diag_client.py list
```

Close any serial monitor or PC library server that already owns that COM port.
Replace `COMx` below with the detected port, then open a terminal with LF-only
line endings, DTR enabled and RTS disabled:

```powershell
C:\Python311\python.exe -m serial.tools.miniterm COMx 115200 --dtr 1 --rts 0 --eol LF --encoding ascii --echo
```

Enter each command and press Enter:

```text
HELLO
USB VIDEO STATUS
MOD STATUS
```

`HELLO` retains the `MOD-EDITOR/1` identity. The video query is available only in
the UVC profile; its response has this form (values here are illustrative):

```text
USB VIDEO active=1 cfg=1 commit=1 alt=1 shutdown=0 started=12 sent=10 dropped=1 packets=1240 retries=0 backpressure=3
```

| Field | Meaning |
| --- | --- |
| `active` | Video capture is enabled, configured, committed and on alternate 1 |
| `cfg`, `commit`, `alt` | Host configuration, completed UVC COMMIT and current streaming alternate |
| `shutdown` | Permanent capture-quiescence latch; cleared on next firmware boot |
| `started`, `sent`, `dropped` | Core frame start, endpoint-submission completion and discard counters |
| `packets` | Prepared packets copied/submitted into the endpoint DMA buffer |
| `retries` | Core write retries and recovery of an incomplete ISO transaction |
| `backpressure` | Producer queue/storage was unavailable |

`sent` and `packets` are firmware submission counters. Isochronous transport
does not acknowledge delivery, so they do not prove host receipt or decoding.
Compare status before and during capture, then inspect the host image/recording.
Use `MOD STATUS` alongside it to check audio underruns and display activity.
Exit miniterm with **Ctrl+]** before reopening the same port with another tool.

The existing USB-only diagnostic client's `info` command expects a different
identity, and the tracker client does not currently expose `USB VIDEO STATUS`
as a CLI operation. Use the terminal above for this query.

## Closing video and entering recovery

Closing the camera normally selects alternate 0, disables the video endpoint
and stops capture. USB reset or a configuration change also stops the endpoint
and clears COMMIT; reopening the camera can negotiate a fresh stream.

Tracker shutdown for UBOOT or USB device teardown is stronger: it permanently
latches video off for that boot. Later host resets, reconfiguration, PROBE/COMMIT
or alternate 1 cannot restart video DMA after the tracker reports quiescence.
A normal new firmware boot clears that latch. Stopping song playback with the
ordinary tracker STOP control does not permanently disable screen capture.

The CDC recovery exchange remains:

```text
UBOOT
UBOOT CONFIRM
```

Send confirmation within the firmware's five-second window. UBOOT arming first
quiesces the tracker; USB video cannot resume if confirmation subsequently
expires or is canceled. Restart normally to resume tracker/camera operation.
Close the camera before the transition, and verify Windows actually enumerates
the bootloader before using the protected writer.

This is serial entry into the existing bootloader. It is not an automatic
rollback, a second firmware bank, a watchdog recovery from every crash, or a
complete update installer over CDC. Firmware `COMMIT` remains blocked. If the
application cannot reach CDC, the established physical bootloader/recovery
procedure remains necessary; screen streaming cannot repair an earlier boot
failure.

## Offline tests and physical acceptance

```powershell
C:\Python311\python.exe firmware/usb-video/run_host_tests.py
C:\Python311\python.exe firmware/usb-video/run_adapter_tests.py
```

Both runners build and test native 240×240 and optional padded 256×240 profiles.
The first runner tests descriptors, exact-size controls, cancellation tokens,
PROBE/COMMIT/alternates, Request Error Code queries/lifetime, bounded packet
storage, byte-for-byte full-frame reconstruction across row fragments,
producer metadata authentication, reset/lease lifetime and YUY2
conversion. The adapter runner uses fake SDK callbacks to exercise the actual
adapter: busy-DMA ownership, empty ISO transactions, incomplete-transaction
recovery, hardware EP0 stall on malformed OUT, configuration/reset handling,
IRQ-mask restoration and the permanent shutdown latch. Neither opens a device.

Before marking this firmware hardware-qualified, record:

- Exact application/full-image hashes and verified readback, followed by boot.
- USB0 speed, Windows camera and CDC enumeration on the same installed image.
- Complete changing host frames at the advertised mode, including a saved clip
  or measured capture; check frame corruption and actual received cadence.
- Tracker playback, LCD response, all four tracks, knobs and both output modes
  while the camera is streaming; compare underruns and response with capture off.
- Camera close/reopen, USB reset/reconnect and repeated streaming negotiation.
- CDC UBOOT entry with capture active, video quiescence and bootloader enumeration.

Hardware results belong to the exact tested build; source and host-test success
alone leave these checks pending.
