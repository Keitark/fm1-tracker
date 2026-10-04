# Private stock-layout NES image

Status: **offline image built and round-trip verified; NOT qualified to flash**.
No USB access, RAM upload, erase, flash write or device key query is performed.

## Reproduce

From `F:\dev\fm1`, after building the matching application profile:

```powershell
C:\Python311\python.exe firmware/nes/build_stock_image.py --profile peripherals
C:\Python311\python.exe firmware/nes/build_stock_image.py --profile headless
C:\Python311\python.exe firmware/nes/tests/test_stock_image.py -v
```

The script checks the completed build manifest, source/archive hashes and the
ELF/application audit. It accepts only the exact reviewed 1 MiB original backup.
Output is private and content-addressed:

`private-backups/nes-candidates/<profile>/<image SHA-256>/`

- `fm1-nes-stock-layout.UNQUALIFIED.bin`: exactly 1,048,576 bytes, raw flash layout.
- `manifest.json`: checksums, changed sectors, startup audit, limits and source identity.

This is **not a `.fwsc`/UFW update file** and must not be sent to an official
updater under a renamed extension. It includes unit-specific stock data; do not
publish it. Earlier candidates remain as unqualified historical artifacts;
use the current script output and manifest, not directory modification time.

## What is replaced and preserved

The decoded stock layout has an application area at `0x4000`, XIP entry
`0x02000120`, and `app.bin` at physical `0x4120`. The original application slot
is 583,228 bytes. The area length stays 583,899 bytes.

Only the new application prefix, `app.bin` size/CRC/header CRC, and enclosing
area data/header CRCs change. Position-dependent 32-byte SFC encoding is
reapplied in memory using metadata already in the private backup. The encoding
key is not queried from hardware, printed or saved as a separate value.

All file offsets, reserved records and their payloads remain unchanged.
The unused original application tail is deliberately retained: it is outside
the new `app.bin` length, but remains covered by the area CRC. Keeping it avoids
unnecessary sector changes; this image does not reclaim that space.

The verifier enforces byte identity everywhere outside the two changed
directory records and the actual new application bytes. Thus it checks more
than selected protected regions: it also covers gaps, partial-block neighbors,
the original tail, configuration tool data and all reserved bytes. It decodes
the result independently through the directory reader, checks all directory
header CRCs plus application/area/config-tool payload CRCs, and compares the
decoded application byte-for-byte with the linked payload.

Current peripheral candidate after separating the NES task from SDK event
dispatch (also retains the removal of persistent auto-repair registrations):

- Application: 175,264 bytes; SHA-256
  `54a0025ec77a800a42842dec3f94c9eaeffb72ebd8bcc127ccde5bb7df962af9`.
- Raw image: SHA-256
  `a6e81059ee918e970853d1ce4fd3878caea1e8a76bfc719075462d8b559b4a1d`.
- Changed data: 171,193 bytes across 43 sectors, `0x4000..0x2efff`.
- Original unused application tail retained: 407,964 bytes.

Current headless candidate: 173,824 application bytes, raw-image SHA-256
`69c018fad3c2b6e53b5e8322548cedd0eeba6bd0d3596e696b3404c3426e62aa`.
It uses the separate NES task, 20-second minimum observation interval and idle
heartbeat. Neither candidate has been executed on the device. Earlier hashes
in old logs/manifests identify historical candidates, not the current build.

Sector coverage is evidence for a future minimal update/restore plan, **not a
flash command**. A sector contains unchanged neighboring bytes too; a future
writer must preserve those bytes and must not write only the differing bytes
after an erase. No writer is supplied here.

`plan_flash_update.py` now generates checked, whole-sector offline plans and
models byte-preserving update/restore. It has no live transport; see
[FLASH_PLAN.md](FLASH_PLAN.md) for interruption coverage and remaining gates.

## Tests and remaining requirements

Eight packaging tests cover real-image decoding, determinism, original backup
immutability, protected-region edits, corrupt directory/application/area CRCs,
partial cipher blocks, size limits and invalid ELF rejection.

Before flashing: finish SDK hardware/startup review (including flash-driver
initialization and integrity-check behavior), review a device-specific
write/readback/restore procedure, and qualify the headless boot then the
peripheral profile. The backup proves recoverable bytes exist; it does not
prove a physical restore succeeds. See `SDK_STARTUP_SAFETY.md` and
`PERIPHERAL_BOOT.md`. Both application and package manifests retain
`flashable: false` and `hardware_boot_verified: false`.
