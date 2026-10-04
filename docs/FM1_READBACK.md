# FM-1 USB flash readback

Verified on 2026-09-26: the connected FM-1 accepted the community WL82 RAM
loader and returned flash data through its v2 `READ_FLASH` command (0xFD05).
This confirms readback on this particular device and loader combination.

Safety correction from offline analysis on 2026-09-27: the helper's online-device
command can internally write NOR protection/status registers. The host sent no
array-program or array-erase command, but the session cannot be described as
side-effect-free. Matching array reads do not establish unchanged status bits;
their pre-operation values were not captured. See
`tools/jieli-wl82/WL82_LOADER_AUDIT.md` for exact addresses and limitations.

## Saved backup

- Local file: `F:\dev\fm1\private-backups\readback-20260926-024637\fm1-flash-1MiB.bin`
- Size: 1,048,576 bytes, raw addresses `0x000000-0x0fffff`.
- SHA-256: `a45aa21afd8a659612427213ce80c7500a42fcbd8548262fb611f03df84e8b61`
- Verification: a second complete device read matched every byte; a separate
  host hash matched the readback manifest. Its first 256 bytes match the earlier
  successful probe (`3c0dee98aa08fb1a5b9ed59ca9a37bc9d149fff46ae7b282bb65d4b996ada0e5`).
- Transport: Windows USBSTOR/SCSI pass-through, elevated process.
- Device inquiry: `WL82 UBOOT1.00 1.00`; NOR ID: `0x856014`.

The backup is a private raw flash image, including settings and the reserved
identity/config sector. It is ignored by Git. Do not publish its contents.
It is not an updater `.fwsc` package, and restoration has not been tested.

## Tool and execution details

The source checkout is `tools/jl-uboot-tool`, upstream commit
`adb3f18889e88ac512ce0a3c4d8cc3d3cb30696a`. The FM-1 adapter is
`tools/jieli-wl82/read_fm1_firmware.py`.

The initial probe uploaded the 24,064-byte WL82 helper to RAM at `0x1c02000`.
The full backup reused that running helper with `--reuse-loader`; it did not
upload or jump again. Reads used 256-byte chunks. The adapter checked the
inquiry identity, NOR type/ID, and saved probe before reading the full image.
The transport also checks SCSI status and read-transfer lengths.

No chip-key query, flash-program, or flash-erase command was sent. This proves
raw readback without explicitly querying the key; it does not establish what
is needed to decrypt or restore the firmware. Flash data may remain encoded.

`readback.log`, `result.json`, and `fm1-flash-1MiB.json` next to the backup
record completion and integrity. The loader remains in RAM until reset or
power loss. After power cycling, re-enter UBOOT and load the helper again;
`--reuse-loader` applies only while that helper is still running.
