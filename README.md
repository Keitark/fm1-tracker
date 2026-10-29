# FM-1 Tracker

ProTracker-style four-channel MOD tracker firmware for the M-VAVE FM-1.
Start with the [latest controls and first-song tutorial](https://github.com/Keitark/fm1-tracker/blob/codex/20-tracker-workflow/firmware/protracker/WORKFLOW_20.md).

## Latest development — 2026-10-30

The newest tracker is on
[`codex/20-tracker-workflow`](https://github.com/Keitark/fm1-tracker/tree/codex/20-tracker-workflow),
reviewed in [PR #21](https://github.com/Keitark/fm1-tracker/pull/21):

- Eight-level undo/redo, block cut/fill/transpose and pattern/order arranging.
- Sample waveform and loop/volume/finetune controls, track mute/solo, contextual
  help and safer NEW/SAVE workflows.
- An interactive graphical PC workshop using the actual firmware controller
  and renderer, plus a combined USB stereo audio/screen-capture candidate.

The revision passes 64 software checks and includes 25 firmware-rendered UI
states. It is **not hardware accepted or installed**: the latest read-only device
query confirmed stock V15. See the
[build and installation record](https://github.com/Keitark/fm1-tracker/blob/codex/20-tracker-workflow/docs/TRACKER_WORKFLOW20_20261029.md).

```sh
git clone --branch codex/20-tracker-workflow https://github.com/Keitark/fm1-tracker.git
```

This `main` branch retains the earlier source baseline. Unfinished feature
pull requests remain separate; the deployment notes below are historical.

## Public source and licenses

Private device captures, keys, vendor/toolchain checkouts, generated firmware,
commercial game assets and classic MOD libraries are excluded from Git. The
included starter content is generated original material; its own notices apply.
Source publication is not a firmware binary release or an installation approval.

This is a component-based workspace with **no blanket repository license**.
Retain each component's notices and consult the
[licensing boundaries](https://github.com/Keitark/fm1-tracker/blob/codex/20-tracker-workflow/docs/LICENSING.md)
before reusing or redistributing code or linked firmware.
The [publication review](docs/REPOSITORY_PUBLICATION_20261030.md) records the
reviewed history, excluded private material and remaining provenance boundaries.

## Historical research and deployment

This workspace contains a read-only research baseline for the M-VAVE FM-1 pocket FM synthesizer, captured on 2026-08-12.

Start with [docs/FM1_RESEARCH.md](docs/FM1_RESEARCH.md). It records the verified hardware, AC7911B8 specifications, stock V14/V15 firmware layout, MIDI surface, updater behavior, confidence levels, and a conservative route toward replacement firmware.

The initial research capture did not flash the device. Later bench operations
are recorded under `docs/` and `private-backups/`. The unpacked metadata has
had device security material redacted.

The experimental [ProTracker-style MOD editor](firmware/protracker/README.md)
adds four-channel pattern editing, piano-key sample audition, REC-toggled WRITE,
and USB `.MOD` load/save. In the historical private bench image, State of the Art
was embedded as a read-only boot song;
3D Demo II and 9 Fingers remained in the private PC library. **The v30 deployment** combined the
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
