# NES controller using the FM-1 piano keys

## Current Famicom layout - 2026-09-29

Directions use the leftmost F-to-A group. B/A use the **far-right high F/G**,
two octaves above the leftmost F/G, not the middle F/G pair.

| Control | Physical key | Stock slot | Matrix row,column |
|---|---|---:|---|
| Left | Leftmost F | 14 | 4,4 |
| Down | Leftmost G | 16 | 6,4 |
| Right | Leftmost A | 18 | 8,4 |
| Up | Leftmost G# | 17 | 5,4 |
| Select | Leftmost A# | 19 | 7,4 |
| Start | Leftmost C# | 22 | 0,4 |
| B | Highest F | 38 | 5,2 |
| A | Highest G | 40 | 6,2 |

`fm1_stock_assign_note_keys()` installs this current hardware preset directly.
The older generic 12-note helper `fm1_board_assign_note_keys()` is retained
unchanged for existing callers; it is NOT the current standalone game mapping.
Only the preset changes: scan timing, held keys, debounce, opposite-direction
cancellation, volume, FX, audio and LCD paths are unchanged. Host tests cover
all41 decoded slots, unused keys, high F/G plus Right held for one second,
opposite-direction cancellation and release. Physical acceptance is pending.

Build prepared: `firmware/usb-diag/build/nes-smb1-famicom-controls` (214864 bytes).
29 NES host tests,33 linked-image checks and29 broker/writer tests passed.
Existing updater accepted the offline package plan; no flash/reset performed.
Application SHA256: `9055295d244d378cea82b72bb43ac5a2ff9ccc9961475d9c5182bdbbac7a1dcb`.
Full candidate SHA256: `c8d7f7017d4d02e87b4627ecd7986a4d36022c1aa082d04c35b54c937e41d438`.
Stable on-device rollback remains the LCD-guard image `1d9a5001...` documented
in `docs/FM1_LCD_HANDOFF_FIX_20260929.md`. Pre-remap sources are preserved in
`private-backups/famicom-controls-source-20260929/`.

Deployment update: flashed on2026-09-29 through the existing persistent updater.
Initial RAM-helper upload failed with WinError31 before any flash erase/write;
one explicit same-image retry succeeded. One full readback matched `c8d7f701...`.
Reset sent once; its known log-decoding exception was resolved by COM10 HELLO
and advancing frames23..806. At14150ms: LCDerr0, audio underrun0/rebuffers0,
ADCerrors0/raw312/gain39 and13389 key scans. No physical key acceptance claimed.
Evidence: `private-backups/nes-famicom-controls-write-20260929/`.
Current session remains `C:\Program Files\FM1FlashSession-dbb696e8a2864b628149e93a00d593aa`,
idle/unblocked; current baseline is `runs/60919125323b4f6f8cbe1ae7eeb2b4b3/final-1.bin`.
Previous LCD-guard image remains preserved for rollback.

## Historical first-octave preset (superseded for standalone firmware)

Assigned layout: use the **leftmost occurrence of each named piano key**.
The user confirmed that the far-left key is F (not C). The observed first twelve
keys run F through E. These names identify physical positions; MIDI pitches below
are observations under the current stock firmware's transpose/octave settings.

| Piano key | Observed MIDI note | NES control |
|---|---|---|
| C | 60 | Left |
| D | 62 | Down |
| E | 64 | Right |
| D# (black key between D and E) | 63 | Up |
| G | 55 | B |
| A | 57 | A |
| C# (black key between C and D) | 61 | Select |
| F# (black key between F and G) | 54 | Start |

Hold a note key to hold its NES button; release to release. Direction + action
combinations are supported by the software. Opposite directions cancel each
other. The existing 10 ms debounce applies, with changes observed when the
emulator polls inputs. Hardware multi-key rollover has not yet been tested.
Unused notes and knobs have no NES action. These are controller assignments,
not synthesizer note playback or a new MIDI input implementation.

## Wiring the preset into the board

`fm1_board_assign_note_keys(&config, note_slots)` applies this preset without
changing gain, display settings, or any hardware. Pass 12 measured scanner IDs
in **pitch-class array order**: C, C#, D, D#, E, F, F#, G, G#, A, A#, B.
These are the leftmost occurrence of each note; the array wraps across the
physical F-through-E group and is NOT physical left-to-right order. Scanner IDs are 0..40;
they are **not** GPIO numbers, MIDI note numbers, or chromatic note indices.

The latest stock-ROM trace establishes the logical slot IDs below. Their physical
scan timing and rollover remain untested. The default config stays unassigned
unless a caller explicitly applies the recovered preset. Pass
`FM1_KEY_UNASSIGNED` for unmeasured notes: if a required note is missing, the
function returns `FM1_NES_BSP_UNVERIFIED` and leaves the old mapping unchanged.
Duplicate or invalid IDs are rejected. The four unused notes may remain unknown.
Apply the mapping before constructing/installing the board adapter, which copies
its configuration. This function does not implement the physical key scanner.

## Stock-derived mapping update

`fm1_stock_assign_note_keys()` now supplies the recovered mapping without I/O:

| Control | Key | Stock slot | Matrix row,column |
|---|---|---:|---|
| A | A | 18 | 8,4 |
| B | G | 16 | 6,4 |
| Select | C# | 22 | 0,4 |
| Start | F# | 15 | 3,4 |
| Up | D# | 24 | 2,3 |
| Down | D | 23 | 1,3 |
| Left | C | 21 | 10,4 |
| Right | E | 25 | 3,3 |

This is established by the stock button table and MIDI note-generation
instructions, not assumed from a sequential matrix. The stock starts piano
slots at 14 and computes note `slot-14+53+octave*12+transpose`.
See [STOCK_BOARD_PROFILE.md](STOCK_BOARD_PROFILE.md) for addresses and checks.
`fm1_stock_decode_keys()` decodes coherent 11-row active-low samples to these
slot bits; it does not yet acquire physical samples. Next work is the actual
SPI2 scanner integration and bench verification of all eight controls together.
The MMIO scanner is now implemented in `src/fm1_wl82_keyscan.c` and tested with a
shift-register model, but is not yet enabled in the boot application.

## Live USB MIDI observation — 2026-09-27

Normal-mode device: USB `4c4a:c755`, input name `FM-1 Midi`. A receive-only WinMM
capture recorded an isolated ascending sequence on channel 1, each with its own
release: F=53, F#=54, G=55, G#=56, A=57, A#=58, B=59, C=60, C#=61, D=62,
D#=63, E=64. The user explicitly identified the starting key as **far-left F**.

Evidence: `tmp/fm1-notes-20260927-081813-249246.jsonl`, device timestamps
24530..29930 ms (relative to capture start). Note-on velocity was 90; note-off
velocity was 100. This establishes note identity under the current settings,
not GPIO numbering, scanner slot IDs, velocity sensitivity, or a complete
multi-key-rollover guarantee. Do not put MIDI numbers 53..64 into `note_slots`.

Capture utility: `tools/jieli-wl82/listen_fm1_notes.py`. It opens only the exact
named MIDI input, sends no MIDI/SysEx, and does not access flash. The first capture
contained exploratory overlapping notes; the isolated twelve-key sequence above
is the mapping evidence. No device firmware/configuration was changed.

## Validation

Host tests use deliberately scrambled fake scanner IDs to check all eight note
assignments, press/release, Right+A, opposite directions, slot 40, invalid/missing
IDs, unchanged configuration on failure, and a diagnostic ROM's controller
mailbox through the assigned A key. These do not prove the physical wiring.

ROM 50 now passes CNROM/mapper-3 host smoke tests, including Start held to enter
gameplay. This controller assignment still does not establish physical scanner
IDs or make the firmware bootable.
