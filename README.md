# FM-1 Tracker

ProTracker-style four-channel MOD tracker firmware for the M-VAVE FM-1.
Start with [tracker controls and status](firmware/protracker/README.md).

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

**Installed hardware is v32**, flashed on 2026-10-05: the v31 editor with
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
