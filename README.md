# FM-1 Tracker

ProTracker-style four-channel MOD tracker firmware for the M-VAVE FM-1.
Start with [tracker controls and status](firmware/protracker/README.md).

This workspace contains a read-only research baseline for the M-VAVE FM-1 pocket FM synthesizer, captured on 2026-08-12.

Start with [docs/FM1_RESEARCH.md](docs/FM1_RESEARCH.md). It records the verified hardware, AC7911B8 specifications, stock V14/V15 firmware layout, MIDI surface, updater behavior, confidence levels, and a conservative route toward replacement firmware.

The initial research capture did not flash the device. Later bench operations
are recorded under `docs/` and `private-backups/`. The unpacked metadata has
had device security material redacted.

The experimental [ProTracker-style MOD editor](firmware/protracker/README.md)
adds four-channel pattern editing, piano-key sample audition, REC-toggled WRITE,
and USB `.MOD` load/save. State of the Art is embedded as a read-only boot song;
3D Demo II and 9 Fingers remain in the PC library. **Installed v30** combines the
four-track view and refined edit workflow developed in v28 with the encoder
capture/backlog correction isolated in [v29](firmware/protracker/profiles/knob-fix-v29/README.md).
It was flashed on 2026-10-04: 96 sectors and one full 1 MiB readback verified,
and serial boot checks passed. [Deployment evidence](docs/MOD_VIEW_KNOBS_V30_DEPLOYMENT.md)
records the installation. Its [offline preview](firmware/protracker/preview/fm1-tracker-screens.png)
uses the installed firmware's renderer. Physical layout, knob response, audio,
frame rate and scrolling latency remain bench checks.

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
