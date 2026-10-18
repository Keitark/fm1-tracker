# Forge UVC integration — 2026-10-13

Optional Forge USB video reuses the ProTracker implementation at
`fb677ea095a28fba3b4b8b8fe91cc3deeb0278d5`. That earlier Windows test received
60 complete 256×240 frames while stereo audio was active. Its audio-starvation
finding remains relevant; it does not qualify the new Forge combination.

The fixed format is YUY2, 256×240, nominal 5 fps. A 240×240 firmware view has
eight black pixels at either side. The transport uses IN endpoint4 with a
642-byte packet (640 image bytes plus two header bytes), a bounded three-packet
queue and no full-frame allocation. CDC remains interfaces0/1; optional duplex
48 kHz UAC remains interfaces2–4/EP1. Video uses interfaces5/6 with UAC, or2/3
without it. All fit the reviewed SDK interface and setup-buffer limits.

The producer copies the published Forge frame, renders at most two RGB565 rows
in the foreground and pads/converts only the requested packet fragment. It
never renders from the USB IRQ. A custom module must render coherently from
that frame; the copy does not freeze arbitrary module globals. STOP, UBOOT and
USB teardown permanently disable capture until reboot. Video defaults off.

New schema3 files use `usb.uvc`; strict schema1/2 imports disable video. The
generator forwards `--forge-uvc`, and packaging retains all protected baseline
ranges and prior project versions. The shared EP1/EP3 packet writer is unchanged;
its compiler-layout audit includes reviewed video-specific call encodings while
retaining the old layouts and exact endpoint, DMA, epoch and IRQ-state checks.

| Fresh offline candidate | Application bytes | Application SHA256 | Remaining heap |
|---|---:|---|---:|
| `forge-uvc-cdc-20261013` |153104|`00c62b82643bd9b5fdc15f91b375c1008d8fb2f78d2aafa197d4f0bcee752b23`|252588|
| `forge-uvc-uac-20261013` |156784|`75f7b896bc969de617196cba92da94861b069523cacbd9f3d48ffd1e7dd79f0d`|243532|

Build manifests and static audits are under `firmware/usb-diag/build/` in those
directories. Source closure checked128 inputs for CDC and131 for UAC. The video
queue2068 bytes, controls64 bytes and producer1172 bytes reside in cache SRAM;
the separate1024-byte DMA buffer is64-byte aligned in ordinary SRAM.

Validation passed:23 Forge runtime tests,5 portable UVC tests,4 adapter/actual
EP0 policy tests,8 packaging/profile gates and11 project configuration tests.
Each linked image has16 negative audit tests; the CDC image skips the one UAC-only
case. Both packed in memory against the pinned stock fixture and preserved every
nonapplication range. These checks performed no device I/O or firmware write.

The new images retain `flashable:false` and require their own Windows capture,
simultaneous duplex UAC/LCD/input timing, reconnect and recovery acceptance.
