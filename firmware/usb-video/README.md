# Optional FM1 Forge USB video

The transport, UVC1.0 controls and USB0 EP4 adapter are selectively copied from
the confirmed ProTracker source at `fb677ea095a28fba3b4b8b8fe91cc3deeb0278d5`.
The Forge adaptation retains CDC0/1 and existing UAC2/3/4. Video follows as
VC5/VS6 when UAC is enabled, otherwise VC2/VS3. ISO IN84 has no status endpoint.

`firmware/usb-diag/build.py --controller 0 --factory-diag --forge-uvc --out PATH`
builds a 256x240 YUY2 screen at an advertised 5 fps; add `--forge-no-uac` for
CDC/video without the USB audio class. The native 240x240 LCD is centered with
eight black pixels on each side. Each of the 192 image packets carries at most
640 raster bytes and a two-byte UVC header, for a 642-byte endpoint maximum.
The existing Forge UAC1 function remains 48 kHz PCM16 stereo with EP1 playback
and capture. Simultaneous host delivery and audio headroom require a Forge test.

The stream queue is 2068 bytes of CPU cache SRAM; endpoint DMA uses a distinct
64-byte-aligned 1024-byte ordinary SRAM buffer. A frozen Forge ABI frame and two
RGB565 scratch rows are CPU-only. There is no extra full video framebuffer,
heap allocation, rendering in USB IRQ, or blocking endpoint write. The producer
does bounded foreground work after LCD service and yields like ProTracker's
producer. Deadline/backpressure and USB reset discard incomplete frames.

Diagnostics render from one copied frame throughout each accepted USB frame.
User render callbacks are used only with the LCD HAL enabled, receive that same
immutable frame, and run with IRQs enabled. Their hidden mutable globals cannot
be frozen by the existing module ABI; user renderers should derive a frame from
the supplied snapshot for coherent video. HAL-off user render falls back to the
diagnostics renderer with the selected input access restrictions.

Alternate 0, configuration changes and reset stop video. Forge STOP, confirmed
UBOOT and USB device teardown latch capture off until the next firmware boot.
`FORGE VIDEO` or `USB VIDEO STATUS` reports bounded transport counters; submitted
packets and frames do not prove host reception.

`run_host_tests.py` and `run_adapter_tests.py` validate native and padded modes,
including controls and endpoint lifecycle. Forge capture/runtime tests live in
`firmware/forge`. All runners are offline and perform no device operations.

The ProTracker 256x240 reference was received on Windows as complete frames with
stereo USB audio, but audio starvation was observed in that session. Forge is a
new linked candidate. Its enumeration, complete moving frames, simultaneous
audio quality, close/reopen, reset and recovery acceptance remain unverified.
Build manifests deliberately retain `flashable: false` and hardware gates.
