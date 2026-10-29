# Workflow #20 — compose, revise, arrange and export

Updated: 2026-10-29 JST. Tracking: [Issue #20](https://github.com/Keitark/fm1-tracker/issues/20).

This guide describes the current portable controller and renderer. Host tests
exercise the editing, history, sample, arrangement and export flows. They do not
establish that this source is installed on an FM-1. The Workflow #20 candidate
has not been flashed or physically accepted. Candidate target builds, optional
USB capture and physical acceptance have separate evidence.

## Try the same controller on a PC

From the repository root, with CMake, Visual Studio 2022 C tools, Python 3.11+
and Pillow available:

```powershell
cmake -S firmware/protracker -B firmware/protracker/build/workflow20 -G "Visual Studio 17 2022" -A x64
cmake --build firmware/protracker/build/workflow20 --config Release
ctest --test-dir firmware/protracker/build/workflow20 -C Release --output-on-failure
C:\Python311\python.exe -m unittest discover -s firmware/protracker/tests -p "test_*.py" -v
C:\Python311\python.exe firmware/protracker/tools/render_ui_preview.py --build firmware/protracker/build/workflow20
C:\Python311\python.exe firmware/protracker/tools/serve_workshop.py --build firmware/protracker/build/workflow20
```

Open the URL printed by the last command, normally `http://127.0.0.1:8765`.
The illustrated panel serves [workshop.html](tools/workshop.html) and runs the
actual `session.c`, `mod.c` and `view.c`; it is not a separate JavaScript tracker.

- Each encoder `−`/`+` button sends one detent. Mouse wheel turns work too.
- **Latch SAVE/EDIT/SEQ** holds that modifier while clicking another control;
  release the latch after the chord. Physical controls use actual held buttons.
- **Enable preview audio** starts optional browser WebAudio. Browser scheduling
  and display polling are not measurements of the FM-1 audio/LCD timing.
- **Download this MOD** exports a self-contained copy of the current song.
  The workshop does not emulate the USB PC library acknowledgement, so a browser
  download does not necessarily clear its dirty indicator.
- The server never connects to an FM-1, flashes firmware or writes a song file
  automatically. Its session disappears when the process exits.

The default workshop opens `samples/FM1BREAK.MOD` and `samples/FM1BREAK.FMB`.
Use `--song <path>`, `--bank <path>` or `--port <number>` to choose alternatives.
The device's optional USB audio/video classes, enumeration, packet timing,
hardware scanner, DAC and SPI display transport are not simulated.

![Sample editor, help, confirmation and history feedback](preview/fm1-tracker-editing.png)

## Read the pattern screen

The ordinary workspace keeps four channels, four scopes and nine rows visible.
The current row stays at the center while pattern data scrolls underneath it.
Every fourth row is emphasized; a stronger highlight identifies the selected
field inside the selected channel. A shaded rectangle is a block selection.

The top line shows output mode, modified `*`, tempo/speed such as `B160/06`,
and available undo/redo counts `U04R02`. Channel headers use `M` for muted and
`S` for solo. Orders, patterns, rows and entry steps use decimal; order/row
indices start at **00**. Song sample numbers use hex **01–1F**.

`C-301E00` is C-3, sample 01, effect E with parameter 00. The input sample chosen
by PRESETS can differ from the sample stored in a cell. On the SMP field,
`SMP 03 IN07` explicitly means cell sample 03, current input sample 07.

## Controls that stay in the pattern

| Control | Meaning |
| --- | --- |
| SELECT | Channel 1–4, one detent per channel |
| ALGORITHM | Order position while stopped |
| KNOB1 | Row while stopped, one detent per row |
| KNOB2 | NOTE → SMP → FX → PARAM |
| KNOB3 | NOTE transpose, cell SMP, FX number; PARAM low hex nibble |
| KNOB4 | Entry step; PARAM high hex nibble while PARAM is selected |
| PRESETS | Input song sample 01–1F |
| Piano | Audition; in stopped WRITE also replace note/sample and advance |
| REC tap | Toggle WRITE while stopped |
| EDIT tap | Stop/return to audition; retain order, row, channel and input sample |
| PLAY/STOP | Song from row 00 of the selected order, or stop at the last triggered row |
| GLO | Loop selected pattern from row 00, or stop |
| FX | Next field |
| SCL / LFO | Clear selected field / full current cell without advancing |
| OCT− / OCT+ | Keyboard octave, bounded −2…+1 |
| HOME | Toggle the compact order arranger |
| ENV | Browse the Active Bank above the grid |
| ARP | Open/close the song sample inspector |

Explicit field edits and deletion work in either EDIT or WRITE, while stopped.
REC changes whether piano keys write notes. A piano note preserves the cell's
effect and parameter. During playback keys only audition; stop before editing.
One-shot previews finish naturally. EDIT stops a looping preview.

Entry steps cycle through **0, 1, 2, 3, 4, 6, 8, 16**. STEP 00 keeps the cursor
on the same row, useful for entering several channels. Return KNOB2 to NOTE,
SMP or FX before using KNOB4 for STEP; on PARAM it edits the high nibble instead.

### SAVE modifier and quick samples

| Gesture | Action |
| --- | --- |
| SAVE tap | Request a PC library export; playback stops |
| SAVE + PLAY/STOP | Open PC file browser |
| SAVE + GLO | Play selected pattern from the cursor row |
| SAVE + OCT− / OCT+ | Undo / redo while stopped |
| SAVE + HOME | Open contextual help; KNOB2 changes topic, EDIT closes |
| SAVE + SCL | Toggle selected channel mute |
| SAVE + LFO | Solo selected channel; repeat on that channel to remove solo |
| SAVE + KNOB3 | Turn right for ST stereo Audio OUT, left for SPK mono |

Solo temporarily overrides the manual mute mask; turning solo off restores it.
Mute/solo and cursor navigation do not rewrite MOD cells. SPK combines all four
channels for the speaker. ST uses channels 1/4 left and 2/3 right.

Hold **EDIT** and tap one of the following to select a quick song sample:

| Quick slot | Button with EDIT held |
| --- | --- |
| Q1 / Q2 | OCT− / OCT+ |
| Q3 / Q4 | FX / SCL |
| Q5 / Q6 | ENV / LFO |
| Q7 / Q8 | ARP / SEQ |

The short gesture selects silently on release. Hold it for 600 ms to bind the
song sample that was selected when the gesture began. Binding does not first
select the old shortcut or trigger a note, and an EDIT chord preserves WRITE.
Bindings are volatile and refer to song slots, never bank entries.

The original FM1BREAK demo uses `01,03,06,07,11,14,0D,1B` for Q1–Q8:
kick, snare, closed hat, open hat, bass, EP, break and COME ON!. NEW SONG and
other loaded MODs start with Q1–Q8 pointing to slots 01–08.

## Bank and sample editing

ENV opens the Active Bank. KNOB1 changes entries and auditions each one;
piano keys audition another pitch. PRESETS chooses the destination song slot.
FX explicitly copies the PCM and metadata into that slot and returns to the
pattern. ENV closes the bank. Browsing alone changes neither song bytes nor
the dirty state. The Active Bank can contain more than 31 entries; a MOD still
has only 31 self-contained song slots.

An occupied destination asks **FX to replace / EDIT to cancel**. Even an empty
destination asks when importing would clear existing undo/redo history. A
successful PCM copy establishes a history barrier: it cannot be undone, because
history does not keep old PCM payloads. Import the required kit before writing
notes, or export a copy before replacing an important sound. Every pattern cell
using a replaced slot then uses the new sound.

ARP opens the current song sample's waveform and metadata. PRESETS, SELECT or
KNOB1 choose another song slot. KNOB2 selects:

| Parameter | KNOB3 | KNOB4 |
| --- | --- | --- |
| Volume | One step, 0–64 | One step, 0–64 |
| Finetune | One unit, −8…+7 | One unit, −8…+7 |
| Loop start | 2 bytes | 256 bytes |
| Loop length | 2 bytes | 256 bytes |

Finetune units are one eighth of a semitone. Loop points use even MOD word
lengths, stay inside the sample, and a length of 0/2 means loop off. The min/max
waveform preserves narrow peaks; dotted markers show the loop boundaries.
Metadata changes are undoable; browsing and audition are not edits. Piano still
auditions, ENV goes to the bank, and ARP or HOME returns to the pattern.

## Blocks, variations and orders

Start at a row/channel, hold SEQ, then turn KNOB1 and SELECT to extend the
rectangle. Release SEQ to keep it. Ordinary navigation moves the paste cursor;
SEQ alone clears the visible selection without deleting notes.

| Gesture | Action |
| --- | --- |
| SEQ + SCL / LFO | Copy / overwrite-paste; clip at pattern/channel boundaries |
| SEQ + FX | Cut selected rectangle into the clipboard |
| SEQ + ENV | Fill with the cell at the original selection anchor, spaced by STEP |
| SEQ + OCT− / OCT+ | Transpose selected notes by a semitone |
| SEQ + ARP / GLO | Insert / delete a row on the selected channel |

Fill repeats the anchor's **whole cell**, including sample, effect and parameter,
across selected channels. It starts at the rectangle's top row and leaves rows
between entry-step positions unchanged. STEP 00 fills every row. Reverse
selection still uses the original anchor as the source. Clipboard capacity is
one pattern: 64 rows × 4 channels × 4 bytes = 1024 bytes.

HOME opens the order arranger above the grid. ALGORITHM chooses the order,
and KNOB3 assigns one of the already allocated patterns.

| Button in arranger | Action |
| --- | --- |
| FX | Append current pattern reference; select the appended order |
| ENV | Insert current pattern reference before the selected order |
| SCL | Delete selected order, keeping at least one order |
| LFO | Duplicate the order reference after it; select the duplicate |
| ARP | Create a blank pattern and assign it to this order |
| SEQ | Duplicate this pattern's contents into a new pattern; assign it here |
| HOME | Return to pattern fields |

Reusing P00 at O00 and O01 shares one pattern: edits affect both occurrences.
Duplicate the **pattern** before making an independent variation. Order/pattern
changes are stopped-only and covered by the bounded history model.

## First four-track pattern, variation and arrangement

This exercise works in the graphical workshop or on a deliberately accepted
device candidate. Use **Download this MOD** for the workshop's final save.

1. **Start blank.** Stop playback, hold SEQ+REC for 600 ms, then release both.
   If the current song is dirty, tap REC to create the new song; EDIT cancels.
   NEW gives O00/P00 and 31 empty sample slots. It resets history and is not
   itself undoable.
2. **Import four sounds before composing.** ENV opens the bank. Use KNOB1 to
   find each name, PRESETS for its destination, and FX to copy: TIGHT KICK→S01,
   DRY SNARE→S02, CLOSED HAT→S03, SUB BASS→S04. Confirm with FX if prompted.
3. **Kick on CH1.** SELECT chooses CH1, PRESETS S01, KNOB2 NOTE. Set T08 with
   KNOB4, row 00 with KNOB1, and tap REC for WRITE. Press a C key; check C-2
   on screen, using OCT−/OCT+ if needed. Return to row 00, hold SEQ and extend
   KNOB1 to row 63, release SEQ, then SEQ+ENV. This fills 00,08,…,56.
4. **Snare on CH2.** Select CH2/S02, keep NOTE/T08, move to row 04 and enter
   the same C pitch. Return to 04, select through row 63 with SEQ+KNOB1, and
   SEQ+ENV fills 04,12,…,60.
5. **Hats on CH3.** Choose CH3/S03, NOTE/T02, row 00. Enter C, return to 00,
   select through 63, then SEQ+ENV fills 00,02,…,62.
6. **Bass on CH4.** Choose CH4/S04, NOTE/T16, row 00. Enter a low C, return
   to 00, select through 63 and fill. Bass notes now fall at 00,16,32,48.
7. **Set 160 BPM without losing the bass note.** At CH4 row 00 choose FX
   with KNOB2, turn KNOB3 to F, then choose PARAM. KNOB4 sets the high nibble
   to A and KNOB3 the low nibble to 0: `FA0`. Return to NOTE. GLO plays this
   pattern; its top line should show `B160/06`. Stop before editing again.
8. **Try a reversible change.** Change a highlighted field, then SAVE+OCT− to
   undo and SAVE+OCT+ to redo. The header reports available U/R counts. There
   are eight history levels; repeated changing detents can consume several.
9. **Make A/A/B.** HOME opens the arranger. At O00 press LFO twice: O00,
   O01 and O02 now share P00. At O02 press SEQ to clone P00 into P01. HOME
   returns. Edits here now affect B, while the two A orders stay unchanged.
10. **Vary B.** Select CH4 row 32, hold SEQ and extend to 63, release SEQ,
    then SEQ+OCT+ raises the later bass notes by a semitone. Clear the block
    with SEQ alone when finished.
11. **Add an ending fill.** HOME, LFO duplicates B's reference into O03; SEQ
    makes its independent P02 copy. HOME returns. Select CH2/S02, NOTE/T02,
    row 56. If not in WRITE, tap REC. Enter four C notes at 56,58,60,62.
    The arrangement is now A/A/B/fill using four orders and three patterns.
12. **Play and export.** Stop, use ALGORITHM to select O00, then PLAY follows
    the order list. SAVE+SCL/LFO can compare tracks with mute/solo. Stop and
    SAVE through the PC service, or click **Download this MOD** in the workshop.

If fill repeats an unexpected effect, undo and inspect the source anchor cell:
fill copies all four bytes. If KNOB4 changes a parameter instead of Txx,
KNOB2 is still on PARAM. If only one part is heard, check `M`/`S` channel labels
and output mode before changing song data.

## Save and reopen on the physical device

This path needs an FM-1 running a qualified candidate, its actual CDC COM port,
Python with pyserial, and the PC library service. `COM6` is only an example:

```powershell
C:\Python311\python.exe firmware/protracker/client.py list
C:\Python311\python.exe firmware/protracker/library_server.py serve --port COM6 --library firmware/protracker/samples --autosave-dir private-library/MOD/edits
```

Tap SAVE on the FM-1 and wait for **SAVED TO PC**. The service writes a new
timestamped file under the configured autosave directory, verifies the MOD
transfer with CRC32 and acknowledges completion. A request, disconnected PC,
timeout or **SAVE FAILED / EDITS KEPT** message is not a successful save.

SAVE+PLAY opens the server's catalog; SELECT/KNOB1 browse, PLAY loads and HOME
returns. When replacing a dirty song, the service first exports its edits.
The catalog is assembled when the service starts; restart it with the edits
directory added as another `--library` to browse newly saved songs.

For a direct export, stop the library service so it releases the port, then run:

```powershell
C:\Python311\python.exe firmware/protracker/client.py save --port COM6 --file my-pattern.mod
```

The exported MOD contains its own samples and does not depend on the Active
Bank. Changing the bank later does not change saved songs. There is no writable
on-device file system or automatic flash work-song journal in this milestone.
Song edits, history, quick bindings and cursor state are lost on power removal;
power-on restores the configured embedded boot song.

## Verification boundaries

The CMake suite covers document/player/USB regressions, controls, eight-level
history, full compose/arrange/export/reopen sessions, PC save tickets/timeouts,
bank import and view geometry. View tests check min/max peaks, loop markers,
30/20-column budgets, read-only browsing and strip/full-frame consistency.
Python tests cover PC client/library and content tooling. Preview manifests
bind the images to the renderer and content hashes.

The eight-level history store is 10,328 bytes on the host (about 10.1 KiB), lives
in cache SRAM on target, and retains scoped header/pattern changes rather than
complete song or PCM copies. Exact target object size and combined cache,
heap and firmware sizes depend on the final target build and optional USB
features; use its recorded static-audit/package result. No build or simulator
pass substitutes for physical knob, scrolling, speaker/headphone, USB capture
and scanner/audio underrun checks.

The source retains the existing supported MOD effects. Tremolo, E6 pattern
loops, Amiga filtering and exact Paula behavior remain separate compatibility
limitations; this workflow does not claim complete ProTracker 3.15 emulation.
