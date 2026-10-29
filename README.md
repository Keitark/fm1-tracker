# FM-1 Tracker

ProTracker-style four-channel MOD tracker firmware for the M-VAVE FM-1.
Start with [tracker controls and status](firmware/protracker/README.md).

## Current development — 2026-10-30

This branch contains the latest editing workflow in
[PR #21](https://github.com/Keitark/fm1-tracker/pull/21): eight-level undo/redo,
block and arrangement tools, sample waveform/loop controls, mute/solo,
contextual help, PC saving and a combined UAC/UVC candidate. See the
[first-song tutorial](firmware/protracker/WORKFLOW_20.md) and
[build/installation record](docs/TRACKER_WORKFLOW20_20261029.md).

The revision passes 64 software checks and includes 25 rendered screen states.
It remains unflashed and awaits physical acceptance; the connected unit was
confirmed as stock V15. Publishing this source does not qualify a firmware image
for installation. Existing component notices apply; no blanket license is added.
The [repository publication review](docs/REPOSITORY_PUBLICATION_20261030.md)
records the source/history and private-material boundary.

The [FM1 Forge diagnostic firmware and module starter](firmware/forge/README.md)
adds a shared HAL for all mapped buttons/keys, encoders and master volume,
asynchronous LCD rendering, bounded test tones, optional USB audio, and
configuration-driven firmware projects. The
[Forge plugin integration](https://github.com/Keitark/fm1-marketplace-plugin)
hosts its native Codex panel and MCP service on ChatGPT Sites. Forge builds and
offline packages remain separate from device transfer and physical acceptance.
The [2026-10-11 Forge handoff](docs/FM1_FORGE_HANDOFF_20261011.md) records the
exact private candidate, validation and remaining first-transfer steps.

The [common USB-MIDI update candidate](firmware/usb-midi/README.md) adds an
opt-in native-plugin JavaScript route through the pinned SDK's single-bank updater.
It includes bounded MIDI framing, exact package checks and a current-device
baseline guard. Normal target builds keep installation disabled pending loader
compatibility and bench acceptance; no device has been flashed by this work.
The [linked candidate evidence](docs/COMMON_MIDI_IMPLEMENTATION_20261018.md)
records the exact build, tests and unqualified physical gates.
The existing manufacturer stock updater remains separate. The historical
[candidate exporter](tools/public-release/README.md) prepares a narrow source
snapshot from an explicit inventory and does not publish a repository or its
history. The owner's later repository-publication request is separate from that
tool. [Component licenses remain preserved](docs/LICENSING.md).

This workspace contains a read-only research baseline for the M-VAVE FM-1 pocket FM synthesizer, captured on 2026-08-12.

Start with [docs/FM1_RESEARCH.md](docs/FM1_RESEARCH.md). It records the verified hardware, AC7911B8 specifications, stock V14/V15 firmware layout, MIDI surface, updater behavior, confidence levels, and a conservative route toward replacement firmware.

The initial research capture did not flash the device. Later bench operations
are recorded under `docs/` and `private-backups/`. The unpacked metadata has
had device security material redacted.

The [tracker](firmware/protracker/README.md) now implements Issue #2: field
editing, eight quick samples, block copy/paste, an order arranger, a read-only
original 29-sample breakbeat bank and a self-contained tutorial MOD. Its
[offline preview](firmware/protracker/preview/fm1-tracker-screens.png) calls the
candidate renderer. [Milestone evidence](firmware/protracker/MILESTONE_2.md)
separates host/build verification from physical acceptance.

The **2026-10-05 v32 deployment** installed the v31 editor with
State of the Art as the boot default, including its samples. The separate
original 29-sound bank remains included. [Deployment evidence](docs/MOD_STOA_DEFAULT_V32_DEPLOYMENT.md)
records 119-sector/full-image verification and an exact boot-MOD CRC match.
[Prior v31 checks](docs/MOD_FAST_EDITOR_BANK_V31_DEPLOYMENT.md) recorded
zero underruns/errors and approximately 25 UI counter updates/second.
Physical layout, knob response, audible output and perceived scrolling remain
acceptance checks. The v30 encoder fix is retained.

## Repository contents

- `firmware/protracker/`: tracker source, host tests, original sample bank,
  controls, view previews, and the isolated v29 profile.
- `firmware/forge/`: diagnostic renderer, HAL ABI, safe project generator,
  configuration-driven builder and host behavior tests.
- `firmware/usb-audio/`: attributed shared UAC1 transport and bounded audio bridge.
- `firmware/usb-diag/`, `firmware/nes/`, and `tools/jieli-wl82/`: shared board
  support, build/audit code, USB tools, and protected flashing workflow.
- `docs/`: hardware research and bench notes.

Vendor SDK/tool checkouts, device dumps, packaged firmware, commercial ROMs,
and the classic MOD library are local dependencies excluded from Git. Their
paths and upstream references are documented in the component READMEs and
hardware research. The original starter bank can be regenerated with
`python firmware/protracker/samples/make_starter_bank.py`.

## Codex firmware skills

The local [FM-1 Mod Firmware plugin](plugins/fm1-mod-fw/plugin.json) preserves
verified build/flash, ADC/audio/USB and original Doom asset findings. Its skills
are [fm1-mod-fw](plugins/fm1-mod-fw/skills/fm1-mod-fw/SKILL.md) and
[fm1-port-doom](plugins/fm1-mod-fw/skills/fm1-port-doom/SKILL.md).

Register this checkout and install with `codex plugin marketplace add <repo-path>`
then `codex plugin add fm1-mod-fw@fm1-local`. Restart Codex after installation
when needed for skill discovery. The plugin contains source guidance; firmware,
game assets and device receipts remain private build inputs.

## Remote laptop debugging

[FM-1 remote debug and the private app store](docs/FM1_REMOTE_DEBUG.md) connects
this PC to a USB-attached Windows laptop through Tailscale HTTPS. It provides
authenticated status, durable firmware read/write jobs and reviewed packaged
app/ROM variants through the existing protected flashing workflow. Laptop setup
and physical acceptance remain separate from the offline implementation tests.
