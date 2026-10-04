# FM-1 ProTracker-style MOD editor

This is a new, independent FM-1 firmware profile for composing and editing
four-channel Amiga `.MOD` files on the device. It uses the existing stock-derived
LCD, 41-key matrix, seven encoders, ALINK0 stereo DAC, and USB CDC recovery path.
The 3.15 reference is [ProTracker 3.15 on Aminet](https://aminet.net/package/mus/edit/ProTracker-3.15);
the file layout and effect meanings follow the [ProTracker module format](https://www.aes.id.au/modformat.html).
This is an original small-screen implementation, not a port of the Amiga UI or
its source code.
The source/manual comparison and control adaptation are recorded in
[PROTRACKER315_WORKFLOW.md](PROTRACKER315_WORKFLOW.md).
Current UX decisions and implementation handoff notes are in
[HANDOFF.md](HANDOFF.md).

## Current status

**Installed firmware is v30**, flashed on 2026-10-04 JST. It combines the portable
view and edit workflow developed in v28 with the encoder capture/backlog fix
isolated in [v29](profiles/knob-fix-v29/README.md). Encoder decoding captures
each completed scan independently of screen work and retains fast movements
while delivering one step per click. Host tests, static audit, package checks,
flash readback and serial boot checks passed.
[Deployment evidence](../../docs/MOD_VIEW_KNOBS_V30_DEPLOYMENT.md) records the
installation. Physical new layout, knob response, audio and frame rate
confirmation remain pending. On historical v25, the user confirmed one physical
click of SELECT or PRESETS moves one track or sample. Prior revisions confirmed
all four parts on the speaker in SPK mode and stereo headphone output in ST mode.

The installed ProTracker-inspired blue/slate view follows the supplied videos: four waveform
scopes and narrow level indicators, nine pattern rows, a fixed center cursor,
selected-track color, and a red WRITE note field. The header shows song,
EDIT/PLAY/PAT/WRITE, order, pattern, sample, step, octave, parameter mode and
sample volume/finetune. Blank margins mark the pattern boundaries. Font rows
are precomputed in ROM; repeated pixel rows are reused. The LCD remains 30 MHz,
RGB444, with one wire frame to retain the song and heap reserve. New hardware
frame rate and audiovisual delay have not been measured.

v30 includes v27's selected-order PLAY, zero entry step, effect-preserving note
entry, preview continuing after key release, and triggered-row highlighting.
GLO adds pattern playback; SCL and LFO clear note/sample or effect/parameter.
Stopped audition flushes old queued audio and primes short samples immediately.
EDIT stops preview from every page. RAM edits require USB save before power-off.

Installed v30 application: **392,048 bytes**, SHA-256
`e8c9dd354c2c2d7f0332f1addfd5fdf8ba6e9f0f6018106c3736d5a5831bc3de`.
Installed v30 1 MiB image: SHA-256
`05f940970cbfe88284ada86deabb17ebc6e6f525a0123d2744f3392e1a1e823c`.
The application static audit, nine package preservation/CRC checks, rebuilt
editor/starter host tests (2) and encoder test (1),
96-sector verification and one full 1 MiB flash readback passed. After the single
reset, the protected CDC observation identified `MOD-EDITOR/1` on COM6, with
audio frames advancing from 419,072 to 551,424 and zero underruns or peripheral
errors. The reset tool reported the known host `UnicodeDecodeError` after
sending reset; reset and flashing were never repeated. The follow-up observation
resolved the reset; the protected session is idle and unblocked, with device
`ready: 1`. The build manifest keeps `flashable: false` so
installation uses the protected flash session with a checked plan and readback.
Heap reserve is 131,148 bytes (minimum 131,072).

Historical v29 was successfully flashed before v30 and retained v25's screen,
audio and edit behavior. [Its deployment note](../../docs/MOD_KNOB_V29_DEPLOYMENT.md)
and [profile provenance](profiles/knob-fix-v29/README.md) preserve that checkpoint.

Historical v25 source baseline: application **389,744 bytes**, SHA-256
`edf482ccd9caf0b1cb9ce68cbce92aff74a2534138219cb28ac13265e9636b67`;
1 MiB image SHA-256
`cd47143087957128c837514b6f81aa2ccead9ae050458adb0e8aa50392961440`.
The v29 profile preserves the recovered v25 source hashes and isolated input
delta in [its provenance record](profiles/knob-fix-v29/README.md).

Historical v28 build (its original packaged image was never flashed):
`firmware/usb-diag/build/mod-editor-view-edit-v28`, application **391,792 bytes**,
SHA-256 `b5b7737cb2cb7b46afddc6c51b6062251ec84700cbc656a154097e57428b31cd`.
Its private 1 MiB image SHA-256 is
`cb15bbafc41fdc7a55bd8a8125c81d9dcaa803b1b05c93abda72dc68507be948`.
Static audit and nine package preservation/CRC checks passed. The same portable
view/edit workflow is now installed as part of v30.

[Exact offline UI preview](preview/fm1-tracker-screens.png) calls `view.c`, the
firmware's text and pixel renderer, then applies LCD RGB444 truncation. It is
not a device photograph. Regenerate it with
`python firmware/protracker/tools/render_ui_preview.py`;
`preview/preview-manifest.json` records source/song/frame hashes.
Its renderer SHA-256 matches installed v30:
`0fef83044ae5cfbca25717f9dab35cf4e458928200d1294a08ab940fea5564aa`.

Each channel cell shows note (3 characters), sample (2 hex digits), and
effect/parameter (3 hex digits): `C-301E00` means C-3, sample 01, effect E00.
Rows, orders, patterns and step are decimal; samples and effects are hex.
File and sample lists use `>` and a highlighted background for selection.

The firmware embeds State of the Art as a read-only boot song and copies it into
the editable **256 KiB** internal RAM song buffer. A power cycle restores that
original song, not unsaved edits. The other two classic modules remain in the
USB-connected PC library.
The firmware keeps one module of at most **256 KiB** in volatile internal RAM.
It accepts standard 31-sample, four-channel `M.K.` and `M!K!` files. It rejects
15-sample Soundtracker, `4CHN` and multichannel variants. The original file
bytes, sample data and unknown effect cells are preserved during pattern edits.
Adding a pattern inserts exactly 1024 zero bytes before sample data and switches
the magic to `M!K!` once the song exceeds 64 patterns.

There is no device flash song writer, SD filesystem, or drag-and-drop mass
storage. The on-device file chooser receives a catalog from the connected PC,
and the PC serves the selected `.MOD` into device RAM. The chooser lists the
original State of the Art, 3D Demo II and 9 Fingers modules staged privately
under `private-library/MOD`, plus the original FM-1 starter sample bank.
Use the USB client to load and save `.MOD` files on the computer.
Uploads replace the current RAM song; the client refuses to discard unsaved
device edits unless `--discard-edits` is supplied. A failed upload can leave
the RAM song unavailable, so save edits first.

## Editing controls

The 240×240 view shows the selected order/pattern/row, four waveform scopes,
and all four channel cells across nine pattern rows. The current row has a full
width highlight and `>` marker. Physical panel
labels use the recovered stock slot order; exact encoder direction and detents
for this profile still need a bench check.
The table below describes installed v30: the v28 view/edit workflow plus the
encoder capture/backlog correction.

| Control | Action |
| --- | --- |
| Piano keys | Audition the selected sample at key pitch on the selected channel; in WRITE, also replace the note at the current row and advance by `T` rows |
| SELECT encoder | Select channel 1–4 |
| ALGORITHM encoder | Select song order |
| KNOB1 | Move row when playback is stopped |
| KNOB2 | Set note-entry row step 0–16; `T00` keeps the row for entering several tracks |
| KNOB3 | In WRITE change the selected effect (`FX`) or sample volume (`SMP`); outside WRITE select `SPK` mono mix or `ST` stereo headphone mix |
| KNOB4 | Change selected cell's effect parameter (`FX`), or sample finetune (`SMP`) in WRITE |
| PRESETS encoder | Select sample 1–31, clamped at the ends |
| EDIT | Return to audition mode without changing notes |
| FX | In WRITE select cell effect/parameter controls (`FX`) |
| ENV | In WRITE select sample volume/finetune controls (`SMP`); outside WRITE open the current song's sample list |
| SCL | In WRITE clear note/sample, keeping effect/parameter and cursor |
| LFO | In WRITE clear effect/parameter, keeping note/sample and cursor; use SCL then LFO to clear the full cell |
| GLO | Stop playback/preview, or start a loop of the selected pattern from row 0 |
| OCT− / OCT+ | Shift keyboard entry octave, bounded −2..+1 |
| HOME | In WRITE return to order 0, row 0; outside WRITE open the PC file list; in either list return to pattern view |
| ARP | Append a blank pattern to a new song order (stopped only) |
| SEQ | Copy the current pattern into a new song order (stopped only) |
| PLAY/STOP | Stop a running song or sample preview; otherwise start the song at row 0 of the selected order; in the file list request the selected song from the PC |
| REC | Toggle WRITE mode |
| SAVE | Show the reminder to export with the USB client |

Releasing a piano key does not cut its sample in installed v30. A one-shot
sample finishes naturally; a loop continues until the next audition key,
EDIT or PLAY/STOP. Live WRITE follows the current triggered row and does not
apply the stopped-entry row step. The header shows the step as `T00`–`T16`
(decimal). ALGORITHM and KNOB1 change the cursor only while stopped.

The new blank module contains no sample audio, so entered notes are silent until
a `.MOD` with samples is uploaded. A sample's volume and finetune can be edited
on the FM-1. Waveform editing, importing a standalone sample, sample trimming,
loop-point editing, title/name entry, cut/paste, and undo are not implemented.
In the sample list, SELECT, KNOB1, or PRESETS selects one of the loaded song's 31 sample
slots; HOME returns to the pattern. In the file list, SELECT or KNOB1 scrolls;
PLAY/STOP loads the highlighted song. The sample list does not copy instruments
between songs; load `FM1 STARTER` to compose with its 17 original samples.

`samples/FM1START.MOD` is a reproducible **32,828-byte** starter song containing
17 original signed 8-bit mono samples: eleven looping waveforms, a pluck,
kick, snare, two hi-hats, and clap. Its single pattern is blank for editing.
At note C-2 (MOD period 428), the sample playback clock is about 8.29 kHz;
the one-shot percussion length is therefore about 0.2–1 second. Generate it
again with `python firmware/protracker/samples/make_starter_bank.py`. Once the
MOD editor profile is installed and identified on a live COM port, upload this
file using the command below. The song exists only in volatile FM-1 RAM, so
save edits back to the computer before switching off the device.

## Audition engine

Playback renders four signed 8-bit sample channels at 44.1 kHz. `SPK` is the
power-on default: it folds all four voices into both DAC outputs so the built-in
mono speaker receives every track. `ST` restores channels 1/4 left and 2/3
right for headphones. Turn KNOB3 with WRITE off to select the mode shown in
the top line. The existing volume ramp applies in either mode. Playback handles
note triggers, sample loops and finetune plus these effects: `0` arpeggio,
`1/2` period slides, `3/5` tone portamento, `4/6` vibrato, `8` panning,
`9` sample offset, `A` volume slide, `B` position jump, `C` set volume,
`D` pattern break, `F` speed/tempo, and `E1/2/9/A/B/C/D/E` fine slides,
retrigger, note cut/delay and pattern delay.
Unsupported effects remain editable and survive export, but this audition
engine does not reproduce all ProTracker 3.15 playback behavior. In particular,
`7` tremolo, `E6` pattern loops, filter behavior, and precise Amiga Paula output
are outstanding. The revised speaker mix has user-confirmed audible playback;
the scope and current-row view await physical confirmation. Electrical output levels and detailed audio behavior have not
been measured.

## Build and verify offline

Run from `F:\dev\fm1`:

```powershell
cmake -S firmware/protracker -B firmware/protracker/build/host -G "Visual Studio 17 2022" -A x64
cmake --build firmware/protracker/build/host --config Release
ctest --test-dir firmware/protracker/build/host -C Release --output-on-failure
C:\Python311\python.exe -m unittest discover -s firmware/protracker/tests -p "test_*.py" -v
C:\Python311\python.exe firmware/protracker/library_server.py list
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --mod --lcd-async --lcd-spi30 --lcd-rgb444 --mod-default-song private-library/MOD/STATEART.MOD --mod-default-song-sha256 f74f635926482845decf2ecdf214e1f53f7100a41871609f5c6789940fc1ef14 --out firmware/usb-diag/build/mod-editor-view-edit-knobs-v30
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/mod-editor-view-edit-knobs-v30
```

The build verifies SDK revision `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`
and a clean SDK checkout. `build/mod-editor-view-edit-knobs-v30` contains the ELF, application
payload, source hashes, map, disassembly and static audit. The stock-layout
packager makes a private unit-specific **UNQUALIFIED** 1 MiB image, retaining
the saved boot/config/reserved regions and verifying CRCs. This is not a vendor
`.fwsc`/`.ufw` package. The protected flash session used an independently
reviewed plan and verified the full device readback.

## USB load and save

With the installed profile and observed CDC enumeration, use the actual COM
port (COM6 at the 2026-10-04 bench; it can change after reconnection):

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

The client checks firmware identity, uses 64-byte ordered chunks and CRC32,
and saves to a new file through a same-directory temporary file. It does not
overwrite an existing file. During export the firmware freezes editing and
playback until the client verifies the entire file and sends `MOD SAVED`, sends
`MOD ABORT`, disconnects, or times out. `MOD SAVE` is a song-file exchange;
firmware installation remains blocked over CDC.
The library server is the PC-side file service. It uploads only validated
four-channel files at most 256 KiB, and saves a dirty FM-1 song under
`private-library/MOD/edits` before loading a different title. Keep it running
while using the on-device file chooser. State of the Art is also embedded as the
read-only boot default; 3D Demo II and 9 Fingers stay on the PC.

## Hardware acceptance gate

The v30 protected flash readback, CDC identity, advancing audio frame counter,
zero reported underruns and zero peripheral errors have been observed.
The boot song reports 230,652 bytes, CRC32
`d3b29503`, and no edits before USB upload. The user confirmed fast scrolling
and the complete `SPK` mix on prior revisions. Bench acceptance still needs
keyboard audition and WRITE, sample and channel encoders, scrolling latency,
four scope appearance, current-row highlight, output-mode knob, headphone
stereo, volume mute/ramp behavior, electrical audio measurements, CRC-exact
save after physical edits, and power-cycle behavior.
