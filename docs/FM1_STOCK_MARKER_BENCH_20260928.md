# Stock V14 white-frame marker, 2026-09-28

## Experiment

User authorized writing the stock-preserving screen diagnostic and stated the
device was already in BOOTMODE. Windows detected exactly one WL82 UBOOT1.00
disk at PHYSICALDRIVE3. This is a **stock synth diagnostic, not NES firmware**.

The existing V14 backup is used, preserving its working startup, CPU arrangement,
filesystem, board power, panel initialization, SDK checks and normal UI flow.
V14's first-fill routine `020217B8..020218D2` is byte-identical to the reviewed
stock010 routine `02021600..0202171A`. Its actual UI call at `02023316` resolves
to `020217B8`; no stock010 addresses were blindly reused against V14.

Only one two-byte instruction changes:

- CPU `020217CE`, raw-flash offset `000257CE`: `40 20` (r0 = 0) becomes
  `10 3F` (r0 = -1 / FFFFFFFF).
- The following unchanged loop writes twenty words into the 80-byte stack
  pixel buffer. Coordinates occupy separate bytes at stack +80/+84; the
  loop counter and subsequent MMIO address are independently loaded.
- Stock's unchanged SPI DMA fill now sends FFFF pixels instead of 0000.
  Display-on and return to the original UI are unchanged.
- No pause, infinite loop, injected startup, new GPIO, clock change, integrity
  bypass, or USB serial is introduced. The marker may be overwritten quickly
  by the normal stock UI; an unobserved transient alone is inconclusive.

Compared with stock V14, exactly two application bytes change, and ten raw-image
bytes including regenerated CRCs. Only stock-relative sectors 4000 and 25000
differ. Replacing the previously installed NES image requires 45 sectors within
the established 4000..30000 scope, with the directory sector written last.

## Artifacts

Builder: `firmware/nes/build_stock_marker.py`.
Artifact directory: `private-backups/stock-marker-v14-white-20260928/`.
Full image: `fm1-v14-white-marker.UNQUALIFIED.bin`, 1,048,576 bytes, SHA-256
`fcc6ef41fc48f5e89904c184af6f5ffa798e67ad2bb3cadf33d1c7905b562a69`.
Application: 584,956 bytes, SHA-256
`d0230d6d02269469c92d0caa515c8ca79acc526dc8a6808e5e48975eea615f2c`.

Rollback remains the exact known-working V14 backup, SHA-256
`bcf61795e2ddb201f2f72180ec8f3dca830c84a7c64d23d08475412dbcfe2f9c`.
The marker selector supports `--restore-v14` from the complete verified marker,
changing only 25000 and 4000. Do not use it against an unknown or partial image.

## Validation and device operations

- Offline checks validate the exact original hashes, unique full fill-function
  match, unchanged call target, exact two-byte patch scope, application decoding,
  CRCs, stock file offsets and preserved boot/config/reserved bytes.
- Eight patch/fake-device tests pass normally and with Python `-O`, including
  forward deployment, stock restore, transport/verification fail-stop, source
  mismatch rejection, fixed-sector bounds and existing-writer selection.
- Five prior CPU1 deployment tests still pass after the orchestration change.
- Initial read `private-backups/pre-stock-marker-20260928-023520/` used the
  existing `jluboottool.py`, uploaded the original WL82 RAM helper, read the
  complete flash and matched the installed CPU1-guard image exactly. No flash
  erase/program was sent during that read.
- After that read completed, the user explicitly requested no pre-write reads.
  The deployment reused that completed matching file and **skipped all further
  full and sector pre-write reads**. This choice is explicit in the receipt;
  no fabricated preflight or sector-before read files are created.
- Existing `jluboottool.py` still performs aligned 4 KiB erase/program with
  erase/program verification, independent post-write sector readback and two
  final complete readbacks. No automatic retry, reset or rollback.

Deployment: `private-backups/stock-marker-write-20260928-023712/`.
**Complete:** `deployment.json` reports `written_and_twice_readback_verified`.
All 45 sectors verified. Both complete 1,048,576-byte final readbacks match the
candidate SHA-256 above. No automatic reset was sent; the helper was left running.
User bench result: **normal boot confirmed, white screen observed before the
normal stock UI**. The user then manually re-entered UBOOT and reconnected.
This validates this stock-marker path; it does not validate NES startup or
constitute a full audio/control regression test.

## Bench observation

After successful write verification, normal power-cycle **without entering
UBOOT**. Look for a brief white initial frame followed by the ordinary V14
synth screen. A phone recording may help catch the transient. A working stock
UI validates this minimally repackaged stock path, not NES startup. A blank
result requires preserving the receipt/image and comparing with exact V14
rollback; it does not by itself locate an LCD or CPU fault.
