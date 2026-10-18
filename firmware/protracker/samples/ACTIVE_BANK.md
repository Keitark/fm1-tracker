# Original bank and tutorial

Run `python firmware/protracker/samples/default_bank.py` to reproduce
`FM1BREAK.FMB`, the self-contained four-channel `FM1BREAK.MOD`, and
`FM1BREAK.json`. There are 29 samples; the bank is approximately 80 KiB.
The MOD is an original 160 BPM tutorial with six patterns and ten orders:
INTRO / BREAK A / BREAK A / FILL / BREAK B / BREAK A / DROP / DROP / FILL / OUTRO.
Pattern reuse costs only one stored pattern. Inspect `09xx`, `E9x`, `Axx`,
`Dxx`, and `Fxx` in the song to see offset, retrigger, fade, break and tempo use.

The bank is independent of a song. NEW SONG has empty samples; bank import
copies only the chosen sound into a selected MOD slot. A bank may hold 256
entries, while a standard MOD has 31 sample slots. Direct bank audition reads
immutable PCM and does not materialize a sample or mark a song dirty.

## FMB version 1

All integers are little endian. The 32-byte header is `8sHHHHIIII`:
`FM1BANK\0`, version 1, directory entry size 48, count 1..256, reserved 0, image
length, directory offset 32, PCM offset, CRC32. The CRC is IEEE CRC32 over the
entire file with bytes 28..31 treated as zero.

The 48-byte directory format is `I20sIIIIIBbH`: nonzero stable ID, printable
ASCII name with NUL and zero padding, PCM offset, PCM length, loop start, loop
length, tags, volume 0..64, signed finetune -8..7, reserved 0. Names fit 19
characters. IDs strictly increase. Entries' PCM is contiguous in directory
order without gaps or overlap. PCM is signed 8-bit mono; sample length and loop
addresses are even, sample length is 2..131070, and a loop is absent or at least
four bytes within the sample. No-loop metadata is start 0 / length 0.
Tag bits: drum 1, break 2, bass 4, musical 8, FX 16, vocal 32. Unknown tags reject.

Validation checks the whole image, directory, CRC, bounds and loop consistency
before exposing any PCM view. The device parser does not allocate or write flash.

## PC tools

`python firmware/protracker/tools/fm1_bank.py list BANK.FMB`

`build manifest.json output.FMB` and `install-image manifest.json output.FMB`
both build a **PC file**. They do not contact the FM-1 or install device flash.
The JSON manifest contains an `entries` array of `id`, `name`, `file` and optional
volume, signed finetune, loop_start, loop_length and integer tags.
Files resolve relative to the manifest. Raw inputs contain signed 8-bit bytes.
Uncompressed mono 8/16-bit WAV is converted to signed 8-bit without resampling;
stereo/compressed/other-width WAV rejects. Odd sample lengths pad one zero byte.

`add BANK.FMB OUTPUT.FMB --id 601 --name NAME --pcm INPUT.wav` appends a new
stable ID. `replace` uses the same options and retains the requested ID.
`remove BANK.FMB OUTPUT.FMB --id 601` removes an existing ID. All operations
rebuild a validated canonical image. Metadata flags can be passed explicitly;
replace does not silently reuse old loop/tuning metadata for changed PCM.

The COME ON! pronunciation is intentionally synthetic and low fidelity.
Its provenance is recorded in the JSON rather than implying sampled speech.
The asset dedication is in `ASSET_LICENSE.md`.
