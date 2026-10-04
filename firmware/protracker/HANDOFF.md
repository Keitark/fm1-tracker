# FM-1 Tracker handoff

Updated: 2026-10-04

This file records the current UX direction and the distinction between behavior
already present in the source candidate and behavior physically verified on the
installed FM-1.

## Edit workflow direction

The existing EDIT/WRITE control mapping is **not a compatibility constraint**.
The next editing UI should optimize for a hardware tracker that feels natural
to experienced ProTracker/tracker users while taking advantage of the FM-1's
piano keyboard and seven encoders.

Preferred mapping:

| Control | Edit action |
| --- | --- |
| KNOB1 | Move pattern row. Keep one detent = one row. |
| SELECT | Select channel 1-4. |
| KNOB2 | Select field: NOTE -> SMP -> FX -> PARAM. |
| KNOB3 | Change the value of the selected field. |
| KNOB4 | Entry step in normal note/sample/effect editing. Preferred values: 0, 1, 2, 3, 4, 6, 8, 16. |
| PRESETS | Select sample 01-1F. |
| ALGORITHM | Select song order/pattern position while stopped. |
| Piano keys | Audition; in WRITE enter note + selected sample, then advance by the entry step. |

The main editor state should therefore become conceptually:

```text
row + channel + field
field = NOTE | SAMPLE | EFFECT | PARAM
```

rather than treating sample/effect editing as separate editing modes.

### Field editing

KNOB3 should directly edit the selected field:

- NOTE: transpose the note by semitone when useful; piano keys remain the primary note-entry path.
- SMP: change sample number.
- FX: change effect 0-F.
- PARAM: change effect parameter 00-FF.

For PARAM editing, a useful tracker-oriented option is to temporarily use two
encoders as hexadecimal nibbles:

- KNOB3 = low nibble 0-F
- KNOB4 = high nibble 0-F

This makes common tracker edits such as 00 -> 10 -> 20 or 00 -> 01 -> 02 fast.
If this is used, the normal entry-step role of KNOB4 is suspended while PARAM
is the active field.

### Entry step

Keep step 0. It is especially useful on this hardware because the user can
enter several tracks on one row:

1. set STEP 00,
2. enter a note on CH1,
3. turn SELECT to CH2 and enter the next note,
4. repeat for CH3/CH4.

This gives the FM-1 a fast hardware chord-entry workflow.

Rather than requiring every integer 0-16, prefer the practical tracker steps:

```text
0, 1, 2, 3, 4, 6, 8, 16
```

### Delete and block operations

Deletion should follow the field cursor:

- deleting NOTE clears only the note,
- deleting SMP clears only the sample,
- deleting FX clears only the effect,
- deleting PARAM clears only the parameter.

Provide a second action for clearing the whole cell.

After field navigation is working, block copy/paste is a high-value next step.
A future block mode should support selecting a row/channel range and copying or
pasting repeated drum/melodic material. This is likely more useful in normal
tracker composition than adding many small edit modes.

## Pattern cursor and playback scrolling

Desired behavior for both stopped editing and playback:

- the active/current row stays at a **fixed vertical position** on screen;
- the pattern data moves underneath it;
- as playback advances, later rows enter from the bottom and move upward;
- the cursor itself must not travel from the top of the pattern window to the bottom.

### Current source state

The current `main` source (the v28 view incorporated into installed v30) implements this model.

In `view.c`, the view chooses:

```c
row = running ? p->current_row : e->row;
```

and renders nine rows centered around it:

```c
shown = row + i - 4;
```

The highlighted row is always the middle pattern row (`i == 4`). Therefore,
when `player.current_row` increments during PLAY, the highlight stays fixed
and the pattern rows scroll upward underneath it. Rows outside 00-63 are blank,
so pattern boundaries are visible instead of wrapping visually.

### Installed-device state

**v30 is installed as of 2026-10-04**. It combines the v28 view/edit workflow
with the encoder capture/backlog correction. Full image readback and serial
boot observation passed. The renderer hash matches the saved preview manifest;
physical display, knob response and audiovisual timing confirmation remain
pending. The earlier v29 knob-only deployment retained the old v25 display and
was superseded by v30. See the
[deployment record](../../docs/MOD_VIEW_KNOBS_V30_DEPLOYMENT.md).

For installed v30, physically verify:

1. cursor remains fixed during PLAY,
2. rows visibly move bottom-to-top,
3. row 63 / next-pattern boundaries look correct,
4. audiovisual row indication is acceptably aligned with the heard trigger.


## Standalone product direction

The FM-1 Tracker should be usable as a **standalone pocket tracker**. A PC is
optional for preparing banks, importing/exporting MOD files, and backups; it
must not be required for normal composition.

Target power-on experience:

1. power on,
2. restore the current work song from persistent storage,
3. if no work song exists, create a new song from the default content,
4. immediately audition samples, enter WRITE and compose,
5. every completed edit is persisted without requiring an explicit SAVE,
6. power may be removed without a shutdown ceremony,
7. the next boot resumes the last committed song and editing position.

The current volatile-RAM implementation does **not** yet provide this guarantee.
Persistent writes must be implemented only after the FM-1 flash/XIP API,
erase/write granularity, endurance assumptions, and recovery behavior have been
bench-verified.

### Power-loss-safe editing target

Do not rewrite a complete MOD for every note. Keep the current MOD in RAM and
persist small completed edits to an append-only flash journal.

Typical journal records should cover:

- one 4-byte pattern cell change,
- sample volume/finetune/loop/name metadata change,
- order-list change,
- pattern creation/copy/clear,
- editor position/state when useful for resume.

Each record needs a sequence number, target, payload, integrity check and a
final commit marker. On boot, replay only complete valid records. An incomplete
last record caused by sudden power loss is ignored.

Periodically compact the journal into a new checkpoint of the current song.
Checkpoint/compaction itself must be recoverable after power loss; do not erase
the last known-good state before the replacement has been verified.

The user-visible goal is therefore **no normal SAVE operation**. SAVE may later
be repurposed for snapshot/export. Editing should feel like a hardware
instrument: make a change, switch the power off, and continue next time.

## Active sample bank model

Keep one **Active Bank** on the FM-1. Do not expose separate Factory/User bank
concepts in normal use.

The PC workflow is:

- install a complete bank set, for example BREAKS, HOUSE, TECHNO or CHIP,
- add individual samples to the installed Active Bank,
- remove or replace samples,
- then disconnect the PC and compose standalone.

The Active Bank is not limited to the MOD format's 31 sample slots. It is a
device-side sample library. When a bank sample is used in a song, copy its
metadata and PCM into one of the Current MOD's 31 sample slots. The song must
never depend on an external bank reference. Changing the Active Bank later must
not alter old songs, and exported MOD files must remain self-contained.

The Active Bank format should therefore be a compact directory plus PCM data,
with at least:

- sample ID,
- display name,
- PCM length and offset,
- default volume,
- finetune,
- loop start/length,
- optional tags/category.

Bank replacement and sample add/remove/replace are PC-side management
operations. Normal composition is device-only.

## Default content: 90s breakbeat starter

The first-run/default Active Bank should be a compact **original 90s-style
breakbeat kit**: enough to power on an FM-1 with no PC and immediately make a
complete track. Do not redistribute copyrighted classic break samples or famous
vocal snippets. Create original recordings/synthesis that evoke the era.

Target size: approximately **64-96 KiB** for the initial bank. Keep enough
headroom that songs can still copy only the samples they actually use.

Suggested first bank:

| Group | Samples |
| --- | --- |
| Core drums | tight kick, deep kick, dry snare, crunchy snare, clap |
| Hats/cymbals | closed hat, open hat, ride, crash |
| Percussion | rim/click, low tom, high tom, shaker |
| Break material | 3-4 original chopped break hits/slices with different textures |
| Bass | sine/sub bass, saw bass, dirty/reese-style loop |
| Musical | organ stab, rave/chord stab, pluck, short orchestral-style hit |
| Chip/wave | saw, square/pulse or one compact waveform utility slot |
| FX | reverse hit, noise/sweep or scratch-like one-shot |
| Vocals | original **COME ON!**, plus one or two short calls such as HEY!/YEAH! |

The exact count does not need to stop at 31 because this is the Active Bank,
not a MOD sample table. Prefer roughly 24-32 useful sounds over filling space
for its own sake.

### Default demo song

Replace the large embedded classic demo as the long-term default with a small,
original FM-1 demo/tutorial song built entirely from the default breakbeat bank.

The demo should function as both a showcase and a template:

- clearly 90s breakbeat/rave flavored but entirely original,
- approximately 155-165 BPM,
- use all four MOD channels,
- demonstrate chopped drums rather than merely playing a full loop,
- show bass, stab and vocal use,
- use a few representative tracker effects such as volume slide, sample offset,
  retrigger, pattern break and tempo/speed where musically appropriate,
- keep the pattern count modest so the sequence remains easy to study,
- leave enough reusable patterns that the user can immediately modify it.

Suggested arrangement:

```text
INTRO     2-4 patterns
BREAK A   4 patterns
FILL      1 pattern
BREAK B   4 patterns
DROP      4 patterns
OUTRO     2 patterns
```

Reuse patterns through the order list where possible. The goal is not a long
finished composition; it is a compact song that makes the machine sound good
within seconds and teaches the editing model by example.

### New Song behavior

There are two distinct concepts:

- **Demo Song**: the small original example loaded on first boot/reset or
  explicitly selected as a demo/template.
- **New Song**: a fresh blank sequence with no copied song samples initially.

Creating a New Song should not copy the entire Active Bank into the MOD.
Instead, samples are copied into the MOD's 31 slots only when the user chooses
them from the Active Bank. This keeps new songs tiny and lets one large Active
Bank serve many different songs.

The default first-run flow may offer the demo immediately, but after the
standalone persistence work exists, normal subsequent boots should resume the
current work song rather than replacing it with the demo.
