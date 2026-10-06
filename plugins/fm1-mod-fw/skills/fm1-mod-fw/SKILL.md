---
name: fm1-mod-fw
description: Build, flash or debug modified firmware for the Jieli WL82 based FM-1, including its shared NES drivers, serial recovery, volume ADC and USB audio.
---

# FM-1 firmware

Use the user's selected checkout and existing deployment tools. Common local checkouts are `F:/dev/fm1`, `F:/dev/fm1-doom`, and `F:/dev/fm1-mdx`; locate them rather than assuming they exist. Read the applicable repo instructions and current branch/status first.

- For candidate builds, `app.bin` packaging or UBOOT deployment, read [build-flash.md](references/build-flash.md).
- For volume, distorted audio, input stalls or missing USB interfaces, read [audio-usb.md](references/audio-usb.md).
- For Doom asset/renderer bugs, use the companion [fm1-port-doom](../fm1-port-doom/SKILL.md) skill.

State the current evidence level: source review, host validation, linked candidate, written/readback verified, running telemetry, or physical acceptance. A successful earlier build or another port is a useful reference, not proof that the current image works.

Respect authorization already given in the conversation. A request to flash authorizes the necessary existing serial UBOOT transition and writer operation; do not repeat permission questions. If a device is absent or a failed-operation latch blocks progress, explain the concrete missing condition. One agent should own device I/O to avoid competing serial handles or writers.

Keep proprietary WADs, generated banks/images, private device receipts and chip keys out of public skill resources. Preserve reproducible source and evidence pointers. Update findings when a hypothesis is tested; label unresolved causes explicitly.
