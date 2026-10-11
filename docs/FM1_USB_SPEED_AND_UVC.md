# FM-1 USB speed and tracker UVC

Status: **2026-10-11**. Work item: [issue #11](https://github.com/Keitark/fm1-tracker/issues/11).

## Evidence so far

| Item | Current evidence |
| --- | --- |
| Connected FM-1 USB0 CDC | Windows hub metadata reports **full-speed, 12 Mbit/s**. |
| USB1 high-speed path | Bounded PHY probe flashed, full-readback verified, and executed. **No fresh host reset or HSMODE was observed.** USB0 CDC returned. |
| Tracker screen UVC | Composite tracker flashed and full-readback verified. Windows camera/CDC enumeration and tracker playback telemetry pass. **Host video capture is not accepted:** DirectShow cannot open the camera. |

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

## Tracker UVC implementation

The [portable core](../firmware/usb-video/uvc_stream.h) targets **240 × 240 YUY2 at
5 frames/s**: 115,200 bytes per frame and 576,000 image bytes/s before UVC headers.
It uses a **3,012-byte stream state**, three bounded packet slots, no heap, stable
packet leases, retry on transport backpressure, and reset epochs to reject stale
work. RGB565-to-YUY2 conversion runs outside the transport/IRQ critical section.

The [conditional tracker producer](../firmware/protracker/target.c) freezes one
logical view per video frame and prepares at most two rows per task iteration.
Rendering/conversion occur outside IRQ guards, after LCD work and with audio
headroom. CPU-only RGB scratch and the view snapshot use the cache-RAM BSS
section; endpoint DMA remains separate. The adapter contract is in
[tracker_capture.h](../firmware/usb-video/tracker_capture.h).

The composite function retains CDC on interfaces 0/1, adds video control on 2
and video streaming on 3, and uses endpoint `0x84`. Fixed UVC 1.0 PROBE/COMMIT
controls and a dedicated 1,024-byte, 64-byte-aligned DMA buffer are implemented.
CPU-only capture/stream state occupies 6,496 bytes of cache BSS; emitted startup
code explicitly clears that section. ISO completion work has no allocation,
logging, rendering, task delay, or generic blocking endpoint write.

The linked application is **490,192 bytes**, SHA-256
`552b2f69218e3d1e09ee3de854e5eb0562056f66bf61e57d2b9d10b5d137d90b`.
The linker retains **132,364 bytes** of heap against the unchanged 128 KiB gate.
Conservative emitted bounds for the added paths are 160 bytes for TX IRQ and
512 bytes for capture work, including their caller frames. USB and peripheral
task stacks are 8,192 and 16,384 bytes. These are local code bounds, not a measured
whole-program stack high-water mark. Windows receiving actual video frames is
still a separate acceptance gate.

### First composite hardware run

The protected writer updated **120 application sectors**, application directory
last, and verified one complete readback against full-image SHA-256
`47093928be9de4410c464bc5e7f9927c9fce01699458b57876e8a3e5daa38c58`.
Boot observation confirmed `MOD-EDITOR/1` on COM4. Its audio clock advanced
**441,152 → 573,504 frames** with zero reported underruns and LCD/key/audio
initialization errors all zero. This replaces the previous NES application.

Windows reports both CDC and **FM1 Tracker Screen**, using `usbvideo.inf`, with
PnP problem code 0. Hub metadata still reports **full speed, 12 Mbit/s**.
After `MOD PLAY`, State of the Art's order/row counters advanced, display updates
were approximately 25/s, and the reported underrun count remained zero.
These are serial observations; physical sound, knob response and visual smoothness
were not confirmed during this run.

DirectShow device enumeration lists the FM-1 camera, but FFmpeg open/format-query
attempts fail with `Unable to BindToObject`. No host video frames were received.
CDC status remains `cfg=1 commit=0 alt=0 shutdown=0`, with all video counters zero:
the host never selected the streaming alternate. A temporary serial check used
DTR disabled and received no output; setting DTR enabled restored replies, so
that silence was not evidence of a firmware stall.

The device subsequently disappeared from Windows USB detection, with no host
flash or reset operation in progress. Reconnection is required to collect the
exact DirectShow HRESULT and compare Media Foundation activation. Do not infer
the cause from the FFmpeg message alone. A compiled, local-only diagnostic is
retained under ignored `tmp/uvc-camera-diag/`; it activates only a unique exact
`FM1 Tracker Screen` match and reports HRESULTs without running a capture graph.

Source review identified missing VideoControl Request Error Code queries in
that first image. The correction below resolves the source gap; its relationship
to the host bind failure is unproven. Complete moving-frame, close/reopen,
streaming playback and streaming UBOOT checks before qualifying the UVC function.
The separately tested USB1 diagnostic recovery does not qualify the composite
stream's active-DMA recovery path.

### Corrected candidate, not flashed

The current source supports entity 0 / VideoControl interface 2 / selector 2
`GET_CUR` and `GET_INFO`. Both return at most one byte; `GET_INFO` reports GET-only
support. `GET_CUR` snapshots the preceding request error before a successful
video control request clears it. Malformed requests still stall, and stale OUT
callbacks cannot overwrite the error or pending transfer of a newer request.

Portable control coverage now has **11 focused groups**, alongside the nine
stream groups. Actual adapter regression checks also pass for bounded VC replies,
malformed-query stalls and unchanged active-stream/COMMIT state.

The corrected linked application is **490,448 bytes**, SHA-256
`7ea8b19e2d15e91ac3a540e962a07ea1a71157c939af6103eb045bb8c89c4e0a`.
Its source/archive/overlay closure and static audit pass; heap remains
**132,364 bytes**. Control state grows from 60 to 64 bytes in cache BSS; the
descriptor bytes and ordinary-RAM DMA allocation are unchanged. The prepared
full image has SHA-256
`e1f7ff2a21c89304f21bb615777f94bd8459f0406c1c12722ded423aa15a1c94`.
Its application-only protected plan passes against the verified first composite
image. **It has not been flashed:** FM-1 remains absent from Windows detection.
The installed image remains the first composite candidate described above.

The actual adapter now routes class entity requests to the bounded core for
error classification and records malformed OUT counts without payload reads.
Adapter tests retrieve errors for known/unknown entities and OUT counts
0, 25, 27 and 65,535, while preserving newer errors after a stale OUT callback.
They check unchanged DMA/arm/COMMIT/alternate state with and without active video.

Raw 240 × 240 YUY2 at 30 frames/s requires 3,456,000 image bytes/s, so that target
needs a working high-speed path or a different video encoding/mode.

Reproduce host checks without contacting the FM-1:

```powershell
python firmware/usb-video/run_host_tests.py
python firmware/usb-video/run_adapter_tests.py
cmake -S firmware/usb-diag -B firmware/usb-diag/build/host -G "Visual Studio 17 2022" -A x64
cmake --build firmware/usb-diag/build/host --config Debug --target hs_phy_probe_tests
ctest --test-dir firmware/usb-diag/build/host -C Debug -R bounded_usb1_phy_probe --output-on-failure
```

Use Debug for the PHY test so assertions stay enabled. Host tests establish
boundedness, packet ownership, conversion, and cleanup contracts; they do not
establish hardware high-speed or UVC success.
