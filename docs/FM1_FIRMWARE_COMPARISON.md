# FM-1 stock firmware comparison

Compared offline on 2026-09-26 against the verified 1 MiB device backup.
No device commands or firmware writes were performed for this comparison.

## Result

The installed application contains **`FM-1_010`**. Its JLFS data CRC passes,
its size is 583,228 bytes, and its flash offset is `0x4120`.
Its SHA-256 is
`fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
This is an embedded version marker, not independently verified release labeling.

| Source label | Embedded application marker | Application bytes | Matches installed application? |
| --- | --- | ---: | --- |
| Community archive V13 | `FM-1_009` | 583,068 | No |
| Archived official V14 | `FM-1_014` | 584,956 | No |
| Current official V15 | `FM-1_015` | 581,564 | No |

All three images have an **identical first 16 KiB** to the device backup,
covering the flash header, bootloader, and configuration region. Their first
raw difference is at `0x4000`, the application-area header. Decoded application
hashes also differ, so this is not merely updater wrapping or extra user data
at the end of the full device backup. No exact online match was found among
the images checked. A targeted web search did not locate an `FM-1_010` package.

The V13 archive label and its `FM-1_009` marker disagree. Do not infer a precise
public release version or chronology from that marker alone.

## Sources and downloaded files

- [Official download page](https://www.m-vave.com/download) lists PC Firmware
  FM-1 V15, dated 2026-07-30. Its separate Windows/Mac bundle headings still
  say V14; these are different download entries.
- [Live official FM-1 package](https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc)
  saved to `references/downloads/FM-1-live-20260926.fwsc` (699,956 bytes).
  SHA-256: `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`.
  It exactly matches the previously saved V15 package.
- V14: `references/originals/FM-1-v14-original.fwsc` (704,052 bytes).
  SHA-256: `a1adca99b1f9823ff2873292be74877a7a14ca0846fca3d25a23aba2750162d3`.
- [Pinned community V13 archive](https://github.com/AL-255/FM-1-RE/blob/ec832f281ca165a7d5e0722985f3acb1465046d5/firmware-images/v13/raw_fw/FM-1.fwsc)
  saved to `references/downloads/FM-1-v13-community-20260926.fwsc` (704,084 bytes).
  SHA-256: `5b3ac73bdd5ec6c774ecf9be65f51b2575ad67b775f8fb175c7130ea8af687ae`.

## Community replacement firmware status

[AL-255/FM-1-RE](https://github.com/AL-255/FM-1-RE) provides stock images and
research. Its main branch explicitly excludes replacement firmware/image
builders; older experiments are retained on `with-custom-firmware`.
[ip2k/mvave-fm1-open-firmware](https://github.com/ip2k/mvave-fm1-open-firmware)
is research toward replacement firmware. Its GitHub releases API returned an
empty list when checked. No demonstrated ready-to-flash replacement release
was identified in these checked sources. This is not a claim that none exists
anywhere.

## Method and evidence

`tools/jieli-wl82/compare_stock_images.py` validates UFW header, entry-list,
and flash-payload CRCs before comparison. It removes the `.fwsc` wrapper and
compares the actual flash payload against the matching prefix of the backup.
Payload lengths are `0x94000` for V13/V14 and `0x93000` for V15; the device
backup is `0x100000` bytes and additionally includes persistent device data.

Application decoding uses the existing firmware metadata in memory only.
No key values or configuration blobs are printed or exported. All four
application data CRCs pass. V14/V15 decoded application hashes also match the
previous independent unpacked files. This decoding is separate from USB raw
readback, which did not query a chip key.

Detailed hashes and byte-comparison results:
`private-backups/readback-20260926-024637/stock-comparison.json`.
The raw backup remains unchanged; see [readback evidence](FM1_READBACK.md).
These comparisons do not establish safe flashing, restoration, or compatibility
of a modified image.

Reproduce locally:

```powershell
C:\Python311\python.exe tools/jieli-wl82/compare_stock_images.py
C:\Python311\python.exe -m unittest discover -s tools/jieli-wl82 -p test_compare_stock_images.py -v
```
