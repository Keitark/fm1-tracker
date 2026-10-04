# ProTracker 3.15 workflow reference for FM-1

Reviewed on 2026-10-04 against the published Aminet source archive:
[ProTracker-3.15-Source](https://aminet.net/package/mus/edit/ProTracker-3.15-Source).
The local archive SHA-256 is
`84d7aab3c6923c1bec20801b1f2fc589efa094c602a71a9cf29b640f7a71c716`.
The extracted source is under
`tmp/protracker315-reference/PT-315-JUN93-Src`.

The source identifies itself as June 1993. The user's
[binary reference](https://aminet.net/package/mus/edit/ProTracker-3.15)
is the November 1993 bugfix release. `PT3-Keys.doc` identifies itself as
PT3.13, so `PT315.S` and `PT.help.txt` are the primary workflow references.
This comparison does not establish exact behavior of every later bugfix.

## Modes and note entry

| Behavior | ProTracker source/manual | FM-1 mapping |
| --- | --- | --- |
| Audition without modifying the pattern | `EditMode=0` bypasses note insertion, then still calls `PlayNote`; `PT315.S:8199,8263` | EDIT returns to audition; top line `E` |
| Write while stopped | EDIT stops playback and enables editing; `PT315.S:15981`; `PT.help.txt:112` | REC toggles WRITE; top line `W` |
| Write while playing | RECORD enables playback and editing; `PT315.S:5387`; `PT.help.txt:118` | PLAY starts playback, then REC enables WRITE |
| Enter a note | Replaces note/sample and retains effect data; `PT315.S:8232` | Piano key writes selected sample and pitch on the selected track, preserving effect/parameter |
| Entry step | Default 1, zero holds the row, wraps at 64; no extra advance during playback; `EditMoveDown`, `PT315.S:8884` | KNOB2 selects 0–16; header `T00` holds the row; stopped WRITE advances, live WRITE follows playback |
| Select track | Tab moves between four tracks; `PT315.S:6652` | SELECT moves one track per click and stays on that track after entry |
| Move row | Up/down wraps within 64 rows and is blocked during playback; `PT315.S:8815` | KNOB1 scrolls while stopped; playback owns the row while running |
| Select sample | Arrows clamp at slots 1–31; `PT315.S:13463` | PRESETS selects one sample per click and clamps at the ends |
| Release an audition key | Key-up events are discarded; `PT315.S:1868` | Candidate v27 lets a sample run to its end or loop; the next key replaces the preview, EDIT or PLAY/STOP stops it |
| Start song playback | Current song order, row 0; right mouse button optionally uses displayed row; `PT315.S:5268`; `PT.help.txt:103` | Candidate v27 starts at the selected order, row 0 |

The EDIT/WRITE names deliberately follow the controls the user accepted for
FM-1. They differ from the original Amiga names. One click on every encoder
uses four decoded contact edges; SELECT and PRESETS were physically confirmed
on installed v25.

PT's live RECORD can quantize a key to the current or next row using playback
tick position (`QuantizeCheck`, `PT315.S:8281`; `PT.help.txt:741`). FM-1 currently
writes the current triggered row and does not implement that quantization.
The preview is a separate single sample voice associated with the selected
track; it does not reproduce PT's direct replacement of that Paula channel.

## Playback position and scrolling

PT's music routine sets `ScrPattPos` to the row whose notes it has just
triggered, before advancing its next-row scheduler (`PT315.S:26731`).
The old FM-1 code exposed its upcoming row during the final tick of the
previous row. At speed 6 / BPM 125 this moved the highlight 20 ms early;
speed 1 exposed the next row immediately after every trigger.

Candidate v27 stores `current_order/current_row` when `row_start()` triggers
notes. Display, status, STOP cursor and live note entry use that current
position. Host regressions cover speed 6, speed 1, and a transition from row 63
into the next order. The PCM queue still renders ahead of the DAC, and the
LCD still has frame latency. These changes do not establish zero audiovisual
delay; the installed path measured about 20 fps and the candidate needs a
physical timing check.

PT renders all 64 pattern rows into a bitmap (`DoRedrawPattern`,
`PT315.S:22189`). `SetPatternPos` changes the Copper bitplane pointer to select
the visible rows (`PT315.S:13918`), avoiding a full pattern repaint for each
scroll step. The FM-1 LCD path currently submits a full packed RGB444 frame.
The candidate reduces repeated glyph and per-pixel work in the pattern area;
it has not yet implemented the original bitmap scrolling architecture or
measured a new hardware frame rate.

## v28 video view and editing

The supplied [tutorial](https://www.youtube.com/watch?v=t5Y5fCJVbCw)
was inspected at its layout section near 12:06, and the supplied
[PT3.15 playback short](https://www.youtube.com/shorts/UCznoemXTbE)
was inspected during playback. Both show four aligned tracks and a fixed
highlighted row below the controls. The short also shows level bars. The user's
four waveform scopes are retained above the FM-1 grid. Screenshots are under
`tmp/protracker-video-reference`.

The 240x240 adaptation uses 48 pixels of information, 32 pixels of scopes and
small level bars, a 16-pixel track header, and nine 16-pixel pattern rows.
The current row stays at y=160..175; out-of-pattern rows are blank. WRITE is
spelled out and its note field is red. FX/SMP label parameter knob mapping.
SAVE's reminder now occupies a visible information line. `view.c` renders both
device and previews. Font rows are ROM bytes; repeated rows are copied within
and across the LCD staging strips. MOD still uses one wire frame; no new
hardware FPS or audiovisual synchronization measurement has been made.

GLO starts the selected order's pattern at row 0 and loops it. Natural wrap and
Bxx/Dxx stay in that order: Bxx restarts row 0, Dxx supplies the break row, and
combined Bxx/Dxx uses Dxx's row. Song PLAY retains normal order transitions.
SCL clears note/sample, preserving effects. LFO clears effects, preserving
note/sample. Neither advances the cursor or marks unchanged data dirty.
SCL then LFO clears a full cell; the core also has a full-cell API.

Stopped audition primes any rendered audio instead of waiting for the song's
1470-frame reservoir. A new stopped audition flushes old queued samples;
invalid/empty sample selection cancels the old voice. EDIT stops preview from
every page and flushes stopped audition. Song playback retains its reservoir.
These target queue corrections await a physical short-sample/retrigger check.

## Remaining workflow work

Field navigation, block cut/copy/paste, undo and live record quantization remain
unimplemented. Preview remains a separate sample voice associated with the
selected channel. Unsupported effects and Paula-specific behavior are listed
in README.md. This adapts the accepted FM-1 control mapping.

## Candidate evidence

`firmware/usb-diag/build/mod-editor-view-edit-v28` contains the application,
source hashes, static audit, map and disassembly. Application: 391,792 bytes,
SHA-256 `b5b7737cb2cb7b46afddc6c51b6062251ec84700cbc656a154097e57428b31cd`.
The private 1 MiB image SHA-256 is
`cb15bbafc41fdc7a55bd8a8125c81d9dcaa803b1b05c93abda72dc68507be948`.
Static audit and all nine package preservation/CRC checks passed. Host tests
passed 2/2, including selected-position playback, row-trigger timing, effect
preservation, zero-step editing, pattern-loop Bxx/Dxx, selective deletion and
invalid/empty preview cancellation. Heap reserve is 131,148 bytes. The exact
C-rendered RGB444 screenshots are under `preview/`. FM-1 is disconnected;
candidate v28 is not flashed or hardware accepted.
