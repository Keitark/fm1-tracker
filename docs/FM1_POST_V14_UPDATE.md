# Official V14 update readback — 2026-09-27

After the user operated M-VAVE's updater and reported newly entering UBOOT,
the existing `jluboottool.py` CLI uploaded its pinned original WL82 RAM helper
and read the entire 1 MiB flash twice. Both CLI runs completed successfully.
No array erase/program command was sent by these readback operations. Loader
initialization retains the documented NOR status/protection-register caveat.

Private evidence directory:
`private-backups/post-official-update-20260927-230806/`

- Both `pass-01/fm1-direct-1MiB.bin` and `pass-02/fm1-direct-1MiB.bin`
  are 1,048,576 bytes and byte-identical.
- Full flash SHA-256:
  `bcf61795e2ddb201f2f72180ec8f3dca830c84a7c64d23d08475412dbcfe2f9c`.
- Decoded application marker: `FM-1_014`; size: 584,956 bytes.
- Application CRC passes; application SHA-256:
  `54a32371e8fc19e7492210e958023762f7335e354a4cab41cb26c35d6f0f0443`.
- Application bytes match the archived official V14 package exactly, not V15.
- All 606,208 bytes of the official V14 flash payload match physical flash
  at offsets `0x00000..0x93fff` exactly.
- Compared with the original September 26 raw backup, `0x00000..0x3fff`
  (boot/header/config) and `0xff000..0xfffff` (reserved identity/config) are
  unchanged. Application and user/state allocations have changed.
- The formerly varying bytes at `0xee046` and `0xee069` match between these
  two new reads, but differ from the old backup. Two reads do not establish
  that the earlier variability is permanently resolved.

This confirms installed flash contents, not physical boot/audio/UI behavior.
The old stock baseline and previously generated NES deployment candidates
must not be treated as matching the now-installed firmware or user state.
Preserve all old backups; do not overwrite them with this new snapshot.
