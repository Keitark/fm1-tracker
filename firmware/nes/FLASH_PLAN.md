# Offline sector update and restore plan

Status: **modelled byte preservation, not a device writer or recovery proof**.
No USB/SCSI transport is imported or opened by `plan_flash_update.py` and it
has no `--device`, upload, reset or live-execution mode.

## Generate from the current verified build

```powershell
C:\Python311\python.exe firmware/nes/build_stock_image.py --profile headless
C:\Python311\python.exe firmware/nes/plan_flash_update.py --profile headless
C:\Python311\python.exe firmware/nes/build_stock_image.py --profile peripherals
C:\Python311\python.exe firmware/nes/plan_flash_update.py --profile peripherals
C:\Python311\python.exe firmware/nes/tests/test_flash_plan.py -v
```

The planner checks the current build manifest/source/archive identities,
audits ELF/application agreement, rebuilds the candidate in memory, verifies
the already saved raw image matches it, and checks the exact original backup.
It emits a deterministic, content-addressed `offline-sector-plan-<hash>.json`
beside each private raw image. Plans contain offsets, per-sector before/after
hashes, page offsets and counts, not raw private payload or encoding keys.

## Current transaction scope

Both current profiles change 43 sectors. Order: `0x5000..0x2e000`, then
`0x4000` (directory **and some application code**, not metadata alone).
Every sector is 4,096 bytes, reconstructed from sixteen full 256-byte pages.
Unchanged neighboring bytes must be programmed back too after erase.
The plan records how many neighboring bytes this entails for every sector.

The directory sector is last only to reduce an exposure window. This is
**not an atomic update**: old metadata can already reference partly replaced
code. Power loss can leave the application unbootable even before the last
sector is touched. Keeping the stock boot region intact does not establish
that the board will always be recoverable through USB.

The prospective procedure requires an exact full-image preflight match,
strict transport checks, qualified ready polling, complete erase verification,
page and sector readback, then a final full 1 MiB hash. A failed/ambiguous
command requires stopping without an automatic retry, reset or rollback.
A separately authorized recovery must reject unexpected differences outside
the planned sectors, reconstruct whole sectors from the original, and finish
with the original full-image hash. No such live procedure is implemented here.

## What the model proves

An immediate-completion NOR byte-array model rejects programming any zero bit
back to one without an erase. On both real private candidates, 43 erase plus
688 page operations (731 total) reproduce the candidate exactly; the inverse
model restores the exact original. Eight tests cover:

- full-sector/page coverage, unique aligned offsets and directory-last order;
- exact forward and inverse model results;
- independent original-sector reconstruction at all 731 completed-operation
  interruption boundaries of the peripheral candidate;
- representative partial-sector erase and partial-page programming states;
- refusal to broaden recovery when bytes outside the plan differ;
- protected-region candidate edits, zero-to-one programming and malformed
  page/sector geometry.

The model excludes device timing, voltage, wear, status bits, USB availability,
boot recovery, and damage outside an addressed sector. It is not a physical
power-cut test. Original-sector reconstruction establishes that the needed
bytes exist, not that the silicon will accept them after a failure.

## Remaining live-tool gate

The reviewed WL82 vendor dispatcher does not implement generic FC03 status
polling, and its read callback ignores the internal busy-wait result. Loader
initialization can also change status/protection registers. These must be
resolved before enabling a writer; see `tools/jieli-wl82/WL82_LOADER_AUDIT.md`.
There is deliberately no automatic path from a passing model to a device write.

`STOCK_IMAGE.md` records current image identities. The original backup and all
previous candidate files are retained. No KALLSUP image is involved.
