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

The current `main` candidate (the v28 view code) already implements this model.

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

The repository README currently records **v25 as the installed firmware**.
The fixed-center pattern view belongs to the later **v28 candidate**, which is
built/packaged but not yet flashed or physically accepted. Therefore an FM-1
currently showing the play position moving from top to bottom is consistent
with the installed v25 behavior, not the current v28 source candidate.

When v28 or its successor is flashed, physically verify:

1. cursor remains fixed during PLAY,
2. rows visibly move bottom-to-top,
3. row 63 / next-pattern boundaries look correct,
4. audiovisual row indication is acceptably aligned with the heard trigger.
