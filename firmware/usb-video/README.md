# FM-1 tracker screen over USB

## Status

The optional `--uvc-screen` firmware profile implements a UVC1.0 screen function
alongside the tracker's USB CDC serial interface. Portable stream/control tests
and fake-SDK adapter tests pass. **Hardware acceptance is pending:** a linked
image, submitted packet counters or a camera entry in Windows does not establish
that the host received complete moving frames. The first composite image was
flashed and booted on 2026-10-11; Windows camera/CDC enumeration and tracker
telemetry passed, but DirectShow camera opening failed before COMMIT/alternate 1.
No host frames have been accepted. See the exact hashes and remaining work in
[the USB speed and hardware evidence record](../../docs/FM1_USB_SPEED_AND_UVC.md).

This directory contains independent screen transport code. No private MOD,
sample-bank or recorded-audio assets are included.

## Video mode

| Item | Fixed profile |
| --- | --- |
| USB path | USB0 full speed; this profile does not enable USB1 |
| UVC version | 1.0, 26-byte PROBE/COMMIT controls |
| Host camera name | `FM1 Tracker Screen` |
| Frame | 240×240, uncompressed YUY2, BT.601 limited range |
| Advertised frame rate | 5 fps; frame interval 200 ms |
| Interfaces | CDC 0/1; VideoControl 2; VideoStreaming 3 |
| Video endpoint | ISO IN `0x84`, maximum 962 bytes, 1 ms interval |
| Streaming switch | VS alternate 0 stops; alternate 1 starts after configuration and COMMIT |
| Audio | Existing FM-1 speaker/Audio OUT; no USB audio function |

The USB video frame rate is separate from the LCD refresh rate. A screen
snapshot is frozen for each video frame while the device display continues its
normal updates. The tracker produces video rows only after LCD work and when
the audio queue has sufficient headroom. Camera delivery may therefore drop
frames under load; the profile does not promise 5 received frames every second.

One image is 115,200 bytes; each full packet carries two rows plus a two-byte
UVC header. Transport uses three small CPU packet slots and a separate,
64-byte-aligned 1024-byte endpoint DMA buffer. CPU state/scratch is placed in
reclaimed cache SRAM; endpoint DMA stays in ordinary SRAM. There is no full
additional video framebuffer, IRQ rendering, blocking endpoint write or stock
camera-driver allocation path.

## Build the optional profile

From the repository root, with the pinned SDK and Jieli toolchain already set up:

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --mod --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --lcd-async --uvc-screen --out firmware/usb-diag/build/mod-uvc-screen
```

This uses the current tracker sources. `--uvc-screen` requires `--mod` and
`--lcd-async`; it excludes preserved `--mod-source-dir` profiles and the separate
`--hs-phy-probe` diagnostic. LCD SPI/RGB options and optional embedded MOD/bank
inputs remain explicit build choices. Private embedded inputs require their
existing SHA-256 locks and are not supplied by this guide.

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
   selection, request **240×240 / YUY2 / 5 fps**.
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

The camera stream carries the screen only. Any microphone audio a host capture
application adds is separate from FM-1's tracker audio output.

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

The first runner tests descriptors, exact-size controls, cancellation tokens,
PROBE/COMMIT/alternates, bounded packet storage, reset/lease lifetime and YUY2
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
