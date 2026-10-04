# SMB1 local integration

## Latest bench result: September 27, 23:41 JST

SMB1 NES was written and twice readback verified, but normal boot produced a
blank screen. At the user's request, V14 was then restored through the existing
`jluboottool.py` CLI. All 41 restored sectors and two complete 1 MiB readbacks
matched V14, and the user confirmed normal stock boot. Subsequently authorized
display-only T1 was written with 27 sectors and two full readbacks verified.
Last-written flash is T1, with no embedded SMB1/NES/audio/scanner. The user
subsequently reported no visible output on normal boot; hardware qualification
has not passed. See `docs/FM1_DISPLAY_T1_20260927.md` for exact hashes,
evidence and limitations. The sections below retain earlier preparation and
stopped-attempt history; their "not flashed" statements are historical.

The user's Google Drive root file `smb1.nes` was fetched through the Drive
connector and saved privately at `private-backups/roms/smb1.nes`. No other
Drive ROM was downloaded. The file is unchanged and is not for redistribution.
Its source receipt is beside it as `smb1-source.json`.

- 40,976 bytes; iNES mapper 0; 32 KiB PRG, 8 KiB CHR; vertical mirroring.
- SHA-256: `f61548fdf1670cffefcc4f0b7bdcdd9eaba0c226e3b74f8666071496988248de`.
- Existing host emulator completed two 600-frame runs (no buttons and held
  Start), with result zero and a visible SMB1 title screen. Both final frames
  were identical and audio silent; this is not gameplay or sound acceptance.

## Existing build pipeline

The existing generator/build/audit/packager now accept the pinned `smb1`
selection. SMB1 is now the default in the build wrappers and CMake cartridge
selection; ROM 50 remains an explicit option and its outputs are not overwritten.
The `fm1_rom50_*` C symbols are retained as a legacy cartridge ABI, not a claim
that ROM 50 is embedded in the SMB1 image.

```powershell
C:\Python311\python.exe firmware/nes/build_boot.py --profile peripherals --rom smb1
C:\Python311\python.exe firmware/nes/build_stock_image.py --profile peripherals --rom smb1 --baseline v14
```

The peripheral application is 167,072 bytes, SHA-256
`a3e4f81914cceab7971d955832fa7b2821f3597a9dc27ac81a1cc2196e23216a`.
Its 1 MiB private raw candidate is:

`private-backups/nes-candidates/smb1/peripherals/b61723ffa318aea3c372f29d4f0e058cf00843cd0d66302468b936c9bdf0eae5/fm1-nes-stock-layout.UNQUALIFIED.bin`

The current-baseline option admits only the exact twice-read September 27
backup and its exact two-byte delta from the original. It preserves `0xee046`
and `0xee069` unchanged and keeps all other non-application data intact. This
candidate changes 41 application sectors, not the bootloader or a whole-chip
image replacement. It is not a `.fwsc` updater container or a bare `.nes` file.

## Transfer status

**Downloaded and integrated locally; not flashed or executed on FM-1.**
The latest transfer request reached a live read-only preflight on September 27
at 22:27 JST, using `read_fm1_firmware.py` with the existing upstream transport
and the already running original RAM helper. The first full 1 MiB read exactly
matched the latest backup (`4d69ef5b...338a3`), but verification stopped when
the second pass differed in the 256-byte chunk starting at `0xee000`. The
second-pass bytes were not saved, so neither the exact differing byte nor the
cause is established. No erase/program command, reset, helper upload, or
automatic retry was sent. Evidence is retained privately under
`private-backups/smb1-pretransfer-20260927-222751/`.

User requested using the
existing Jieli tools, not development of a replacement writer. Custom draft
modules `fm1_deployment.py`, `fm1_flash_engine.py`, and `prepare_deployment.py`
were left unused and unqualified when that request arrived. They are not a
deployment path for this SMB1 build. No existing writer was invoked either.

The existing `jluboottool.py` CLI now accepts `--reuse-loader` to avoid uploading
again to a running helper, and `--io-size 256` to avoid the optional buffer-size
query and constrain I/O. Without the reuse flag it still reloads on UBOOT1.00;
do not launch it blindly. Its documented `write` operation
also erases the addressed range first; a bare SMB1 ROM must never be used as
the FM-1 firmware image. Existing completion/readback and recovery limitations
remain described in `FLASH_PLAN.md` and `WL82_LOADER_AUDIT.md`.
