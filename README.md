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

**Installed hardware remains v30**, flashed on 2026-10-04, with 96-sector and
full 1 MiB readback verification and serial boot checks. Its
[deployment note](docs/MOD_VIEW_KNOBS_V30_DEPLOYMENT.md) records installation.
The v31 candidate retains its encoder fix and needs deliberate installation
and physical layout, knob response, audio, frame rate and scrolling checks.

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
