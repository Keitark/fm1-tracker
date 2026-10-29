# FM-1 tracker Workflow 20 — implementation and validation

Date: 2026-10-29 JST. [Issue #20](https://github.com/Keitark/fm1-tracker/issues/20).

## Current result

The everyday editing workflow is implemented in the shared portable controller:
new song, sample-bank import, four-channel note entry, field editing, blocks,
pattern variations, order arrangement, playback, export and reopening the MOD.
The firmware and interactive PC workshop use the same controller and renderer.

This candidate is **not installed or physically accepted**. A read-only native
MIDI query on this date returned `FM-1_015`, `loader=false`, confirming the unit
is running stock V15. No firmware write, reset, loader transition or toolchain
update was performed for this work.

## Input and editing improvements

- Preserve one physical detent per row/channel/sample selection.
- Eight undo/redo levels for notes, fields, blocks, order changes, sample
  metadata, and blank/duplicate pattern creation. No full-song allocation.
- Block cut, copy, paste, anchor-based step fill and transpose; track row
  insertion/deletion. Note entry preserves effect data.
- Reachable ARP sample inspector: min/max waveform, volume, signed finetune,
  loop start/length, fine/coarse controls and piano audition.
- Track mute/solo; solo overrides an existing mute and toggles back correctly.
- Contextual SAVE shortcuts and five help topics; accurate playback hints,
  tempo/speed, dirty indicator, history counts and beat/field highlights.
- Dirty NEW and occupied sample replacement require confirmation. Importing
  PCM also warns before clearing existing undo/redo. Cancel preserves the song.
- SAVE requests an actual PC export; `SAVED TO PC` follows CRC-verified disk
  saving and acknowledgement. Failed or disconnected requests report failure.
  Long MOD transfer traffic keeps the pending library request alive.
- Quick-sample/modifier releases and busy USB input cannot trigger a stray
  EDIT, WRITE, SAVE or NEW action. Original demo quick mappings are retained.

See [the operating guide](../firmware/protracker/WORKFLOW_20.md) for every
button/knob and the complete first-song tutorial.

## Combined firmware candidate

Local build: `firmware/usb-diag/build/tracker-workflow20-av`.

| Item | Audited result |
| --- | --- |
| Application | 511,344 bytes |
| Application SHA-256 | `e0af54b3b29db717b681d9332d2b35154c5021c69961c475fbea4172fddc9d65` |
| Heap | 133,068 bytes; 1,996 above the unchanged 128 KiB gate |
| Cache SRAM | 26,288 / 28,672 bytes, including initialized data |
| Undo storage | 10,320 target bytes; 10,328 on the Windows host |
| USB audio | UAC1, 48 kHz stereo PCM16; DAC remains 44.1 kHz |
| USB video | UVC1.0, 256×240 YUY2, 5 fps, native screen centered with black margins |
| Local display | Existing asynchronous RGB444 path, SPI 30 MHz |
| Embedded boot song | Private hash-locked State of the Art, 230,652 bytes |
| Separate Active Bank | Original FM1BREAK.FMB, 81,808 bytes |

USB capture taps original stereo before local SPK downmix and master volume.
All four tracker voices contribute to SPK; ST retains stereo Audio OUT. The
foreground tasks are pinned to CPU0 for the shared USB packet ownership model.
Rendering/conversion stays outside IRQ guards, with bounded packet reservations.

The emitted audit verifies exact USB descriptors, ordinary-SRAM DMA placement,
CPU-only cache scratch, complete cache startup clearing, bounded packet commit,
capture → SPK fold → analog gain, LCD submission → USB screen production, and
STOP/UBOOT teardown. Reviewed SDK power initialization remains unchanged.

Direct emitted paths include 1,504 bytes for the visible peripheral task chain,
1,876 for the USB task, a 284-byte capture chain, and approximately
216 bytes for audio callback plus ALINK/IIS wrappers. Task budgets remain
16 KiB peripheral and 8 KiB USB; CPU0 system IRQ stack is 4 KiB. Callback and
SDK/assert dispatch prevent claiming a whole-program worst-case stack bound.
Physical stack-canary/deadline checks remain pending.

## Reproduce validation

From the repository root:

The private bench candidate uses the following explicit SDK link options. The
STOA file is supplied locally and is not distributed with this repository:

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --mod --mod-usb-audio --mod-uvc --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --lcd-async --lcd-spi30 --lcd-rgb444 --mod-default-song F:/dev/fm1/private-library/MOD/STATEART.MOD --mod-default-song-sha256 f74f635926482845decf2ecdf214e1f53f7100a41871609f5c6789940fc1ef14 --mod-bank firmware/protracker/samples/FM1BREAK.FMB --mod-bank-sha256 0696b3a3e75732abf27edbfdd3eb68f9fe689d0cbe13a14c556e853b9abf89f8 --out firmware/usb-diag/build/tracker-workflow20-av
```

Host verification:

```powershell
cmake -S firmware/protracker -B firmware/protracker/build/workflow20 -G "Visual Studio 17 2022" -A x64
cmake --build firmware/protracker/build/workflow20 --config Release
ctest --test-dir firmware/protracker/build/workflow20 -C Release --output-on-failure
C:\Python311\python.exe -m unittest discover -s firmware/protracker/tests -p "test_*.py" -v
C:\Python311\python.exe tools/jieli-wl82/test_tracker_profile.py --build firmware/usb-diag/build/tracker-workflow20-av --require-actual -v
C:\Python311\python.exe tools/jieli-wl82/test_tracker_audit.py --build firmware/usb-diag/build/tracker-workflow20-av --require-actual -v
C:\Python311\python.exe firmware/protracker/tools/render_ui_preview.py --build firmware/protracker/build/workflow20
```

The tracker CTest suite passes 12/12; the PC/content tests pass 15/15; the actual
candidate/profile packaging tests pass 10/10. Four shared UAC bridge/control/
nonblocking-stress and packet-ownership tests also pass. The final emitted
candidate audit passes 23 positive/corruption/profile checks. Its manifest
closure matches 130 source inputs, 10 archives and three vendor overlay bases.
Native/padded capture comparisons
check complete raster payloads, chroma alignment, black margins, final EOF,
canaries and reset/stale leases. The compose scenario verifies four-track entry,
row-63 wrap, independent variation, order reuse, cursor playback, full USB
export CRC/bytes, MOD reopening and undo savepoints.

The UI preview contains 25 exact firmware-rendered states. Browser checks
exercised PLAY/STOP, the held NEW gesture, bank import, piano WRITE, sample
metadata and undo, confirmation feedback, and physical control placement.
The workshop additionally supports mouse/keyboard buttons, held modifiers,
optional WebAudio and downloading the current self-contained MOD. It never
connects to the FM-1.

## Installation boundary

The private raw reference package is SHA-256
`57a1ac2e16929a15ecd9a6bd902f0b8f8ab4fd9f34b8f16963cc319718d2d2a6`.
All nine layout/CRC/preservation checks pass against the retained **V14 layout**.
It is not a stock V15 installer and must not be relabelled as one.

The current unit has stock audio/MIDI interfaces and no custom tracker CDC
entry. Existing protected raw installation requires an established UBOOT
connection and a matching verified current baseline/layout. The installed
Forge native custom FWSC route currently admits factory diagnostic builds,
not this tracker profile. The old stopped stock-restore session and its
observation latch remain intact. No guessed button sequence, arbitrary MIDI
command or raw write is used to work around these conditions.

On-device testing is pending that installation route: detent counts at slow
and fast rotation, held-chord release order, speaker/stereo output, short-sample
latency, sustained scrolling/audio, concurrent UAC/UVC close/reopen and recovery.
The iPad stride workaround and High Speed routing remain unverified on hardware.

Edits remain volatile; save to the PC before power-off. Flash journaling,
quantized live recording, destructive PCM waveform editing and complete Paula/
unsupported-effect emulation are not delivered by this editing workflow.
