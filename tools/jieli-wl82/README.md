# FM1 Jieli WL82 tools

The WL82/AC791N SDK utilities are already present in the checked-out SDK at:

`references/source/fw-AC79_AIoT_SDK/cpu/wl82/tools`

This folder contains a read-only PnP checker and a read-focused adapter for the
FM-1's WL82/AC791N bootloader. The FM1 research notes identify the SoC as
AC7911B8 / AC791N, and Windows reports the attached device as `WL82 UBOOT1.00`
(`VID:PID 4C4A:8057`). The upstream community tool lists WL82 support as
unverified. On this FM-1, however, a 1 MiB raw backup completed on 2026-09-26
and a second complete read matched every byte. See `docs/FM1_READBACK.md`.

## Read-only check

Run from the workspace root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\jieli-wl82\check-device.ps1
```

It reads Windows Plug-and-Play descriptors and port enumeration only. It does
not open a device handle, send USB commands, change boot mode, or write files.

## Tool inventory and safety

- `isd_download.exe`, `SM01-DFU.exe`, `SM01-DFU_1.6.exe`: device-facing
  firmware/update tools. The SDK batch files invoke these to program WL82
  devices; do not run them for an FM1 operation until the exact input image,
  target, and effect have been reviewed.
- `download.bat`, `write_file_to_flash.bat`, and the scripts under
  `loader_tools`: build or program firmware/update images. Some explicitly
  write to NOR flash. They are not run by this setup.
- `fw_add.exe`, `ufw_maker.exe`, `uboot_lz4.exe`, `packres.exe`, and
  `fat_image_tool.exe`: offline image/resource packaging tools; they can create
  or alter local output files, but do not identify the connected device.
- `report_elf_sizes.py` and `llvm-symbolizer.exe`: offline build-analysis
  utilities.

## Verified FM-1 readback

`read_fm1_firmware.py` adapts the local `tools/jl-uboot-tool` WL82 loader and
v2 protocol classes. Its only explicit array-data operation is `READ_FLASH`. The initial
loader upload goes to volatile RAM at `0x1c02000`; `--reuse-loader` skips that
upload when the loader is already running. It does not query the chip key or
call flash-write, erase, or reset methods.

**Safety correction (2026-09-27):** the helper's `GET_ONLINE_DEVICE` operation
can write NOR status/protection registers internally, including when reusing
the loader. Thus this is flash-array readback, not a side-effect-free inspection.
The Python adapter now warns before opening the device. The matched backup
reads do not prove status registers stayed unchanged. See
[the exact-loader audit](WL82_LOADER_AUDIT.md) before any further device access.

The cold uploader is now restricted to the exact reviewed 24,064-byte helper,
its address, encoding and 512-byte transaction size. All 47 RAM writes must
report a complete transfer before a jump is issued. A short write or transport
error stops immediately without retry/jump; a short or mismatched jump response
stops with execution state unknown. Inquiry must also be complete. These
changes have host fault-injection coverage, **not a new hardware test**.
This adapter cannot upload the experimental status extension or another image.

Cold UBOOT versus an already-running helper must be established from the
session history: both can report `UBOOT1.00`. Do not infer a cold session from
that string or overwrite a running helper. The updater has no general live
memory-ownership or cache-coherency proof. Automatic RAM readback is deliberately
absent: upstream documents in-place temporary transformation during memory
reads on some UBOOT implementations, so arbitrary reads are not harmless.

`--raw-output` is the verified backup route: it reads the complete 1 MiB flash
twice and checks every byte before finalizing the file. Such backups include
device settings and the reserved identity/config sector; keep them private.
The backup and manifests are in `private-backups/readback-20260926-024637`.
Restoring the image has not been tested.

Run from an elevated PowerShell because Windows denied the current
non-administrator process access to `\\.\PHYSICALDRIVE3`:

```powershell
# A 256-byte array read; loader setup can change NOR protection/status bits:
.\tools\jieli-wl82\read-fm1-firmware.ps1

# The probe leaves a RAM loader running. Reuse it for subsequent reads.
# The agent's run-elevated-backup.ps1 handles the verified raw backup flow.
```

The older `-Full` / `--full-output` path assumes plaintext V14/V15 markers at
specific offsets. That path remains unverified on raw device flash; use raw
backup mode for preservation. After reset/power loss, re-enter UBOOT and load
the helper again before using `--reuse-loader`.

The helper checks the device identity and refuses to overwrite existing
output files. `--plan` prints the address plan without opening USB:

```powershell
python .\tools\jieli-wl82\read_fm1_firmware.py --plan
```

Offline uploader/readback tests (including all 47 write-failure positions):

```powershell
C:/Python311/python.exe tools/jieli-wl82/test_read_fm1_firmware.py
C:/Python311/python.exe -O tools/jieli-wl82/test_read_fm1_firmware.py
```
