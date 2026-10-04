# FM-1 four-channel MOD tracker

An independent ProTracker-style editor for the M-VAVE FM-1, using its 240×240
LCD, piano keys, seven encoders, stereo DAC and USB CDC file exchange.
It edits ordinary self-contained 31-sample, four-channel `M.K.` / `M!K!` MODs
in a 256 KiB RAM song buffer. The ProTracker 3.15 comparison is recorded in
[PROTRACKER315_WORKFLOW.md](PROTRACKER315_WORKFLOW.md).

## Status

The source implements [Issue #2](https://github.com/Keitark/fm1-tracker/issues/2)
and [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md): explicit field editing,
quick samples, rectangular block copy/paste, order arranging, an Active Bank,
an original tutorial and empty NEW SONG. Build/test evidence and the physical
acceptance checklist are in [MILESTONE_2.md](MILESTONE_2.md).

**Installed hardware is v32**, flashed on 2026-10-05 JST: the v31 editor with
State of the Art as its embedded boot song, retaining the original 29-sound bank.
Its 119-sector/full-image verification, CDC boot and exact loaded-MOD CRC passed.
[Deployment evidence](../../docs/MOD_STOA_DEFAULT_V32_DEPLOYMENT.md) records these checks.
The [v31 live check](../../docs/MOD_FAST_EDITOR_BANK_V31_DEPLOYMENT.md) measured
approximately 25 UI counter updates/second. Physical knobs, visual response and audible outputs remain on
the acceptance checklist. The encoder capture/backlog correction is retained.
Historical evidence remains in the [v30 deployment](../../docs/MOD_VIEW_KNOBS_V30_DEPLOYMENT.md)
and [v29 profile](profiles/knob-fix-v29/README.md).

## Pattern workflow

All four tracks, four scopes and nine pattern rows stay visible. The active row
is fixed at the center; rows move upwards during playback. The selected field
has its own highlight, blocks are shaded, and short notices report actions.
HOME and ENV add compact contextual controls above the grid.
The preview uses `view.c` and LCD RGB444 truncation; it is not a device photo.

![Offline tracker workflow preview](preview/fm1-tracker-screens.png)

Each cell is note (3 characters), sample (2 hex digits), effect (1 hex digit)
and parameter (2 hex digits): `C-301E00` means C-3, sample 01, effect E00.
Orders, patterns, rows and steps are decimal; sample slots and effects are hex.

| Control | Action |
| --- | --- |
| SELECT | Channel 1–4; one detent = one channel |
| ALGORITHM | Order position while stopped |
| KNOB1 | Row while stopped; one detent = one row |
| KNOB2 | Select NOTE / SMP / FX / PARAM |
| KNOB3 | Transpose note, change sample/effect, or PARAM low hex nibble |
| KNOB4 | Entry step; on PARAM, high hex nibble |
| PRESETS | Current song sample 1–31 |
| Piano | Audition; WRITE also enters note/sample and advances |
| REC | Toggle WRITE while stopped |
| EDIT | Stop playback/preview and return to audition, retaining cursor context |
| PLAY/STOP | Song from row 0 of selected order; STOP resumes at last triggered row |
| GLO | Selected-pattern loop from row 0; stop running playback |
| FX | Next field |
| SCL / LFO | Clear highlighted field / entire cell |
| OCT− / OCT+ | Keyboard octave, bounded −2..+1 |
| HOME | Toggle compact order arranger |
| ENV | Open/close Active Bank |
| SAVE | Open/close USB PC file chooser |
| SAVE held + KNOB3 | Right: ST stereo Audio OUT; left: SPK all-track mono speaker mix |

Field edits and deletion work while stopped in EDIT or WRITE. REC controls
piano note entry. Notes preserve effect/parameter data. Entry steps cycle
through `0,1,2,3,4,6,8,16`; zero keeps the row for multiple tracks.
Playback permits audition but does not record or modify patterns. Key release
lets a sample finish; EDIT stops looping previews.

### Quick samples and blocks

Hold EDIT and briefly press **OCT−, OCT+, FX, SCL, ENV, LFO, ARP, SEQ** to select
Q1–Q8. Hold that pair for 600 ms to bind the captured PRESETS sample. An EDIT
chord preserves WRITE. Shortcuts refer only to current MOD sample slots and are
volatile. The demo binds kick/snare/closed hat/open hat/bass/EP/break/COME ON! to
`01,03,06,07,11,14,0D,1B` (hex). NEW SONG/upload reset to sample slots 1–8.

Hold SEQ and move KNOB1/SELECT to extend a shaded row/channel rectangle.
Release SEQ to retain it; ordinary navigation moves the paste destination.
SEQ+SCL copies, SEQ+LFO pastes with overwrite and clips at row 63/channel 4.
SEQ alone clears the selection. The fixed clipboard is at most 1024 bytes.

### Order arranger

HOME opens it. ALGORITHM selects an order; KNOB3 changes its pattern reference.
FX appends the reference, ENV inserts before the selected order, SCL deletes
an order, and LFO duplicates its reference after it. ARP creates a blank
pattern; SEQ duplicates the current pattern. These assign the new pattern to
the selected order. HOME returns to fields. All mutations require stopped
playback. Reusing a pattern in several orders does not copy its data.

## Bank, demo and NEW SONG

ENV opens the bank. KNOB1 browses and auditions immutable PCM; piano auditions
another pitch. PRESETS chooses a destination MOD slot. FX explicitly copies
the sound there while stopped and returns to the pattern. Browsing leaves song
bytes and dirty state unchanged.

Hold SEQ+REC for 600 ms for **NEW SONG**: one blank pattern/order and 31 empty
sample slots. Import only the bank sounds needed; the bank is not copied wholesale.

- `samples/FM1BREAK.FMB`: 29 original signed 8-bit mono sounds, 81,808 bytes.
- `samples/FM1BREAK.MOD`: 160 BPM, 87,612 bytes, six patterns across ten orders.
- Drums, original chops, three basses, EP MAJ7/MIN7, rave/organ/orchestral stabs,
  reverse/noise/scratch and synthetic COME ON!/HEY!/YEAH! are generated locally.
- [Format/tools](samples/ACTIVE_BANK.md), [asset-only CC0](samples/ASSET_LICENSE.md),
  [provenance and hashes](samples/FM1BREAK.json).

Regenerate with `python firmware/protracker/samples/default_bank.py`.
PC tools provide build/install-image representation, list, add, remove and
replace. `install-image` creates a PC file; it does not write device flash.
Banks support 256 entries independently of MOD's 31 slots. The legacy
[FM1START.MOD](samples/FM1START.MOD) remains available.

## Audio and storage

The existing player renders four signed 8-bit sample voices at 44.1 kHz.
SPK folds all voices into both DAC outputs for the mono speaker; ST uses
channels 1/4 left and 2/3 right. Volume ramp and effects remain:
`0,1,2,3,4,5,6,8,9,A,B,C,D,F` and `E1/2/9/A/B/C/D/E`.
Unsupported effects survive export. Tremolo, E6 loops, Amiga filtering and exact
Paula output are not implemented.

The standard v31 build embeds the original demo and read-only bank; this unit's
v32 build instead boots STOA with its own samples selectable through PRESETS.
The separate ENV bank still contains the original 29 sounds. Edits are volatile;
power cycling restores the embedded song. Save to the PC before switching off. Writable flash
persistence, undo, waveform editing, device WAV conversion and realtime recording
are outside this milestone.

## Build and verify offline

Run from the repository root:

```powershell
cmake -S firmware/protracker -B firmware/protracker/build -G "Visual Studio 17 2022" -A x64
cmake --build firmware/protracker/build --config Release
ctest --test-dir firmware/protracker/build -C Release --output-on-failure
C:\Python311\python.exe -m unittest discover -s firmware/protracker/tests -p "test_*.py" -v
C:\Python311\python.exe firmware/protracker/tools/render_ui_preview.py --build firmware/protracker/build
& firmware/protracker/build/Release/mod_demo_preview.exe firmware/protracker/samples/FM1BREAK.MOD firmware/protracker/build/preview/FM1BREAK-demo.wav
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --mod --lcd-async --lcd-spi30 --lcd-rgb444 --mod-default-song firmware/protracker/samples/FM1BREAK.MOD --mod-default-song-sha256 b7a4a0d04762e895e732c5a78a1f8fc0f40fe96054cad78291db0c96253bae9a --mod-bank firmware/protracker/samples/FM1BREAK.FMB --mod-bank-sha256 0696b3a3e75732abf27edbfdd3eb68f9fe689d0cbe13a14c556e853b9abf89f8 --out firmware/usb-diag/build/mod-fast-editor-bank-v31
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/mod-fast-editor-bank-v31
```

Target building requires the pinned clean AC79 SDK and WL82 tools documented
in [usb-diag](../usb-diag/README.md). Static gates retain minimum 128 KiB heap.
Packaging produces a private unit-specific **UNQUALIFIED** 1 MiB image, not a
vendor `.fwsc`/`.ufw`. Build success does not constitute hardware acceptance.

## USB load/save and file chooser

Use the observed COM port (COM6 on the v30 bench; it can change):

```powershell
C:\Python311\python.exe firmware/protracker/client.py list
C:\Python311\python.exe firmware/protracker/client.py upload --port COM6 --file song.mod
C:\Python311\python.exe firmware/protracker/client.py play --port COM6
C:\Python311\python.exe firmware/protracker/client.py stop --port COM6
C:\Python311\python.exe firmware/protracker/client.py save --port COM6 --file edited.mod
C:\Python311\python.exe firmware/protracker/client.py key --port COM6
C:\Python311\python.exe firmware/protracker/client.py vol --port COM6
C:\Python311\python.exe firmware/protracker/library_server.py serve --port COM6
```

The client checks identity, ordered chunks and CRC32 and saves to a new file.
Export freezes editing/playback until acknowledgement, abort, disconnect or
timeout. Unsaved edits require explicit discard for upload; the library server
saves dirty songs before replacement. SAVE opens its catalog, SELECT/KNOB1
choose a file and PLAY requests it from the running PC server. Classic modules
remain privately staged on the PC. There is no mass-storage filesystem or CDC
firmware writer.
