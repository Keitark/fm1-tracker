# FM-1 Tracker implementation plan — Milestone #2

Prepared: 2026-10-05
Source of truth: [HANDOFF.md](HANDOFF.md)
Tracking issue: https://github.com/Keitark/fm1-tracker/issues/2

## Objective

Build the first genuinely fast hardware-tracker workflow on top of the installed
v30 baseline:

- step-oriented pattern editing,
- fixed-center playback/edit view,
- knob-driven field editing,
- quick sample access,
- block copy/paste,
- a usable song/order arranger,
- an original 90s breakbeat starter bank,
- a small original demo/tutorial song.

Do not add realtime record or production flash persistence in this milestone.

## Baseline rules

1. Preserve the current v30 playback engine and encoder backlog fix.
2. Keep one detent = one row for KNOB1 and one detent = one channel for SELECT.
3. Do not regress ordinary four-channel MOD import/export.
4. Keep exported songs ordinary self-contained M.K./M!K! MOD files.
5. Keep all new core editing behavior host-testable before hardware mapping.
6. Do not flash a candidate merely because it builds. Build/package/static-test
   first, then perform a deliberate physical acceptance pass.

## Phase 1 — Core editor model

Primary files:

- `mod.h`
- `mod.c`
- `tests/test_mod.c`

### Add field cursor

Replace the current binary sample/effect editing concept with an explicit field:

```c
FM1_FIELD_NOTE
FM1_FIELD_SAMPLE
FM1_FIELD_EFFECT
FM1_FIELD_PARAM
```

Extend `fm1_mod_editor` with the field cursor while retaining any compatibility
fields temporarily if needed during migration.

Add tested operations for:

- move field left/right with bounded or wrapped behavior,
- transpose current note by one semitone,
- set/increment sample slot,
- set/increment effect nibble,
- set PARAM high/low nibble,
- clear selected field,
- clear full cell.

Entering a piano note must continue to preserve FX/PARAM unless explicitly
cleared.

### Entry-step table

Change UI-facing step selection to the practical set:

```text
0, 1, 2, 3, 4, 6, 8, 16
```

Store either the actual step value or an index, but tests must cover the full
cycle and step 0 same-row entry.

## Phase 2 — Song/order arranger core

Primary files:

- `mod.h`
- `mod.c`
- `tests/test_mod.c`

Add explicit order-list APIs rather than manipulating raw header bytes from
`target.c`.

Required operations:

- get pattern assigned to order position,
- set pattern assigned to order position,
- append order entry,
- insert order entry,
- delete order entry,
- duplicate order entry,
- create blank pattern,
- duplicate current pattern into a new pattern.

Important rule:

> Reusing pattern 01 in five order positions still consumes only one 1024-byte
> pattern.

Tests must cover:

- order insert/delete shifting,
- repeated pattern references,
- song length/order count,
- pattern creation at capacity boundaries,
- M.K. -> M!K! transition where applicable,
- playback following the edited order table,
- PATTERN PLAY remaining isolated from song order traversal.

## Phase 3 — Block edit core

Primary files:

- `mod.h`
- `mod.c`
- `tests/test_mod.c`

Add a small fixed-size block clipboard. A maximum 64-row x 4-channel selection
is exactly one pattern (1024 bytes), so do not allocate dynamically.

Support:

- selection start/end row,
- selection start/end channel,
- copy,
- paste,
- overwrite semantics,
- clipping at pattern boundaries.

Do not implement complex merge modes yet.

## Phase 4 — Quick Sample model

Primary files:

- `mod.h`
- `target.c`
- optional dedicated `quick_slots.c/h` only if it keeps target.c simpler.

Add 6-8 quick-slot bindings. Each binding stores a **Current MOD sample slot
1-31**, never an Active Bank entry.

Required behavior:

- select quick slot -> immediately select its song sample,
- bind current song sample to quick slot via a simple hold gesture,
- display a transient confirmation,
- keep a data structure ready for future persistence.

Do not invent persistent Flash storage in this milestone.

## Phase 5 — Pattern UI

Primary files:

- `view.h`
- `view.c`
- preview generator/tests where useful.

Keep:

- nine visible pattern rows,
- fixed center active row,
- pattern scrolling bottom-to-top during PLAY,
- four scopes/meters.

Add:

- selected field sub-highlight inside selected channel,
- visible distinction between NOTE/SMP/FX/PARAM,
- transient status line for sample/step/quick-slot actions,
- visible block selection,
- no modal page for routine field editing.

The pattern page remains the main workspace.

## Phase 6 — Hardware control mapping

Primary file:

- `target.c`

Preferred mapping:

| Control | Action |
| --- | --- |
| SELECT | channel |
| ALGORITHM | order position |
| KNOB1 | row |
| KNOB2 | NOTE/SMP/FX/PARAM field |
| KNOB3 | selected-field value |
| KNOB4 | entry step; PARAM may use high nibble |
| PRESETS | current song sample |
| Piano keys | audition + WRITE note entry |

Rules:

- KNOB1 never accelerates.
- SELECT never accelerates.
- ALGORITHM/bank browsing may later accelerate.
- EDIT -> PLAY -> STOP -> EDIT stays on the pattern page.
- STOP returns the cursor to the last/current triggered playback row.
- Song PLAY follows the order list.
- Pattern PLAY loops only the selected pattern.

Button assignments for quick slots and block modifiers may be adjusted after the
first physical test. Do not let temporary button naming dictate the core data
model.

## Phase 7 — Active Bank format and PC tooling scaffold

New suggested files:

- `bank.h` / `bank.c` for parser/model if device-side code is needed now,
- `tools/fm1_bank.py` for PC creation/inspection,
- `samples/default_bank.py` or equivalent deterministic generator.

The Active Bank is a sample library, not a MOD.

Minimum per-entry metadata:

- stable sample ID,
- display name,
- PCM offset/length,
- volume,
- finetune,
- loop start/length,
- category/tag flags.

Provide PC-side operations or at least format support for:

- build/install image representation,
- list,
- add,
- remove,
- replace.

Actual writable Flash installation on the FM-1 is **out of scope** here. Keep the
format deterministic and independently verifiable so storage can be added later.

## Phase 8 — Default breakbeat bank

Create only original, redistributable material.

Target:

- 64-96 KiB,
- roughly 24-32 useful entries,
- signed 8-bit mono material appropriate for MOD use.

Minimum palette:

### Drums
- tight kick
- deep kick
- dry snare
- crunchy snare
- clap
- closed hat
- open hat
- ride or crash
- rim/click
- low tom
- high tom
- shaker

### Break/chop
- 3-4 original breakbeat slices/hits designed to recombine well

### Bass
- sub/sine bass
- saw bass
- dirty/reese-style bass

### Musical
- EP MAJ7 stab
- EP MIN7 stab
- rave chord stab
- organ or pluck
- short orchestral-style hit

### FX/vocal
- reverse/noise/scratch-like hit
- original COME ON!
- optional original HEY!/YEAH!

Avoid reproducing famous break loops or recognizable copyrighted vocal samples.

## Phase 9 — Demo/tutorial song

Build a small original MOD from the default bank.

Target:

- 155-165 BPM,
- all four channels used,
- modest pattern count,
- order-list reuse,
- chopped drums,
- bass,
- EP/rave stab,
- COME ON!,
- representative effects such as sample offset, retrigger, volume slide and
  pattern break.

Suggested arrangement:

```text
INTRO
BREAK A
BREAK A
FILL
BREAK B
BREAK A
DROP
DROP
FILL
OUTRO
```

The demo is both a song and a tutorial: inspecting its order list and patterns
should teach how to build a complete track.

## Phase 10 — New Song

Keep two concepts separate:

- Demo/template song.
- NEW SONG = blank sequence and empty song sample slots.

NEW SONG must not copy the Active Bank wholesale. A bank sound enters the song
only when explicitly copied into one of the MOD's 31 sample slots.

## Required host regressions before any device candidate

At minimum add tests for:

- field cursor/edit semantics,
- note entry preserving effect data,
- practical step table including step 0,
- field-sensitive clear,
- order insert/delete/reuse,
- duplicate/blank pattern creation,
- block copy/paste,
- quick-slot binding model,
- current-row synchronization after PLAY/STOP,
- selected-pattern loop vs song playback,
- default bank structural validation,
- demo MOD structural validation and max-size checks.

Run the existing test suite unchanged as well.

## Physical acceptance checklist

After a reviewed build is installed deliberately:

1. one SELECT detent = one channel,
2. one KNOB1 detent = one row,
3. KNOB2 field cursor is obvious,
4. KNOB3/4 edit the highlighted value without lag or dropped movement,
5. PRESETS + quick slots make drum programming fast,
6. piano audition and WRITE entry still work,
7. fixed center row behaves correctly in song and pattern playback,
8. STOP resumes editing at the heard position,
9. block copy/paste is understandable without a manual,
10. song arranger can create a short A/A/B/fill structure quickly,
11. speaker and headphone playback remain correct,
12. no new audio underruns or scanner errors.

## Follow-up milestone — deliberately not part of #2

After the editing UX is physically accepted:

- validate safe writable Flash regions and XIP behavior,
- implement power-loss-safe work-song checkpoint + append-only edit journal,
- persist quick slots/editor position,
- implement Active Bank install/add/remove/replace on device Flash,
- then test arbitrary power removal and resume.

Realtime/quantized recording remains optional and should be considered only
after the step workflow is proven fast.
