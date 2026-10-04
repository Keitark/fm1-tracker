# FM-1 USB diagnostic firmware

Latest profile (2026-09-28): **peripheral-test v1, built/offline-tested, not flashed**.
See [PERIPHERAL_TEST.md](PERIPHERAL_TEST.md) for LCD, audio, key and knob commands,
build instructions, safety limits and remaining hardware checks.

The previous `build/usb0-uboot` binary was flashed and twice readback-verified;
USB0 serial operation was observed on COM10. Its serial-triggered UBOOT
transition is still not hardware-qualified. The current source restores that
IRQ/confirmation path after a partial source revert, and adds an optional
peripheral worker; the saved working binary and rollback images are unchanged.

The requested no-UAC firmware installation path is **not complete**. This
candidate can verify a byte stream over CDC but cannot store or install it.
Do not flash it on the assumption that subsequent updates will be UAC-free.
The new `build/usb0-uboot` candidate additionally implements serial-triggered
UBOOT entry. That transition is offline-tested, **not hardware-tested**.

## What is implemented

- Separate AC79/WL82 SDK application: USB CDC ACM, a diagnostic task, same-boot
  startup breadcrumbs and a repeating one-second status banner.
- Default profile: no NES, ROM, LCD, audio, key scanner, USB mass-storage or host application.
  The `--peripheral-tests` profile adds opt-in LCD/audio/keys/encoders, not NES.
  SDK FreeRTOS and its normal integrity checks remain. This is not a pre-OS
  console: failure before USB startup still produces no COM port.
- Windows client using an ordinary COM port, with no raw-disk access, elevation,
  driver replacement, registry edits, or privileged background service.
- `HELLO`, `STATUS`, `BEGIN`, ordered `DATA`, `END`, and `ABORT`. `COMMIT`
  always returns `ERR COMMIT_BLOCKED STOCK_LAYOUT_UNQUALIFIED`.
- `UBOOT`, followed by `UBOOT CONFIRM` within five seconds, requests the
  WL82 SDK bootloader transition from the USB OUT interrupt. No flash writes
  are performed by these commands. The existing Jieli writer is still used
  after Windows detects `WL82 UBOOT1.00`.
- Bounded lines/chunks, CRC32, size/offset checks, ten-second inactivity timeout,
  reset/DTR generation invalidation. No received data is executed or retained
  as a complete image. CRC matching is transport evidence, not authentication,
  package validation, or proof that an application will boot.

## Why firmware installation is blocked

The pinned SDK is `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`.
Its `update.a:flash_fs_api.c.o`, function `get_app_code_file_head`, searches for
both `app_dir_head` and `app_dir_head2`. It returns an error unless its two
found-header bits equal 3. `dual_bank_passive_update_init` calls
`update_target_update_info_get`, which depends on that pair and computes the
inactive-bank address/size from them.

The hash-verified saved stock V14 backup
`bcf61795e2ddb201f2f72180ec8f3dca830c84a7c64d23d08475412dbcfe2f9c`
has these top-level entries: `uboot.boot`, `isd_config.ini`, `app_dir_head`,
`key_mac`. There is **no `app_dir_head2`**. The existing packager preserves
that layout, so a smaller diagnostic does not automatically create a second
bank. The passive updater is therefore not a drop-in solution for this unit.

Evidence: `tmp/flash_fs_api.ll`, `tmp/dual_bank.ll`, and
`private-backups/post-official-update-20260927-230806/analysis/audit.json`.
Only saved files and SDK objects were examined; no chip key was queried.

Completing CDC-only firmware installation requires a stock-compatible RAM-resident updater
whose entire flash-critical execution path is reviewed, or an explicitly
approved and separately qualified bank-layout migration. This implementation
does neither. UBOOT entry is separate from installation and Windows permissions.
The current raw-physical-disk writer may require elevation; a COM command does
not change its access rights. Initial installation may also require UAC. UAC
is not intrinsically required by the Jieli chip or by the serial command.

## Serial-to-UBOOT entry

Reference: `F:\dev\KALLSUP_MOD\tools\apply_kallsup_cdc_console.ps1`,
`enter_download_mode`, and the 2026-06-12 CDC `download` acceptance record in
`REFDOC/MVP1_ACCEPTANCE_LOG.md`. KALLSUP calls the SDK entry from the USB
callback; its notes report task/timer dispatch returned to the application.
This is a BR28 reference, not proof of WL82 behavior. No KALLSUP image is used.

The FM-1 build uses the pinned WL82 SDK's `msd_upgrade.c:go_mask_usb_updata`.
Its empty weak `nvram_set_boot_state` fallback is removed **in the build copy**,
forcing the real `cpu.a:usb.c.o` definition to link. The reviewed linked path
copies `usb_update_mode\0` to retained RAM at `0x01c7fd80` and sets the SDK's
P33 reset control. The SDK's weak MMU/RAM-protection hooks remain their default
no-ops in this configuration. Static checks verify the actual entry, marker,
reset instructions and direct interrupt-callback call. No chip key is queried,
and no signing or integrity checks are bypassed.

Wire exchange (LF-only; uppercase; the client performs both steps):

```text
host: UBOOT\n
device: OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n
host: UBOOT CONFIRM\n
```

No final reply is expected: the IRQ immediately calls the SDK entry. The host
reports only that confirmation was sent, never that a disconnect proves UBOOT.
Check Windows enumeration before invoking the existing writer.

The bounded IRQ RX queue feeds the ordinary task parser. Only the exact
confirmation is interpreted in the IRQ while armed; split USB packets are
supported. Arming is rejected during a transfer, with pending RX/TX, or across
a USB session change. Malformed confirmation, five-second expiry, RX overflow,
USB reset or DTR change cancels entry. Other bytes while armed abort it; send
`UBOOT` again after the error. There is no IRQ-side logging or mutex wait.

## USB integration and outstanding hardware gate

The linked candidates select **USB0 only**, which enumerated successfully with
the previously flashed serial image. Each new image still needs its own boot
test. USB1 is not initialized speculatively. USB1 needs a separate
high-speed descriptor/endpoint and startup audit before a candidate is built.

SDK device-mode setup supplies `usb_config`, SIE/PHY setup, DMA addresses,
control transfers and the CPU0 USB interrupt. CDC and EP0 allocations are
checked before installing the interrupt. Four disjoint 256-byte, 64-byte-aligned
RAM slots supply endpoint buffers; SDK heap state remains separate. There is
one IRQ owner for raw RX and one task owner for protocol/TX; no serial
transmission from an ISR. TX is limited to
63-byte short packets and checks endpoint readiness before calling the SDK.
The lower-level SDK write also has a jiffies-based timeout; this is not a
guarantee of progress if interrupts/the scheduler themselves fail.

`vendor_overlay.py` applies exact-source checked changes to build copies only:

- Reject malformed CDC direction/interface/value/length before EP0 reception;
  validate actual received length, then read exactly seven line-coding bytes.
- Honour DTR/RTS, make ISR-shared state volatile, invalidate on USB reset.
- Bound data-mutex waits and remove multi-packet/ZLP loops from the task path.
- Route raw RX through the bounded IRQ queue and guarded boot confirmation.
- Require the real WL82 boot-state function instead of its weak empty fallback.
- Remove the unused unsafe generic echo and irrelevant class includes.

CDC endpoints are IN 0x83, OUT 0x02, notification IN 0x82, all within WL82's
0..4 endpoint indices. The SDK default notification endpoint 6 is not used.
The generic private jump-to-updater setup hook is not linked.

Descriptors use CDC device class/subclass 02/02 for Windows' inbox
[Usbser driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids).
The local bench VID/PID 3654:5155 is inherited from the SDK CDC identity, not
an allocated public product identity. Do not distribute it as one. Product is
`FM1 USB Diagnostic`; no chip/flash serial is published. Actual Windows binding
is untested. USB logs report the RTOS-stage trace, not an SDK integrity verdict.

## Build and validate (offline)

Run from `F:\dev\fm1`:

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --out firmware/usb-diag/build/usb0-uboot-rebuild
cmake -S firmware/usb-diag -B firmware/usb-diag/build/host -G "Visual Studio 17 2022" -A x64
cmake --build firmware/usb-diag/build/host --config Release
ctest --test-dir firmware/usb-diag/build/host -C Release --output-on-failure
C:\Python311\python.exe firmware/usb-diag/tests/test_usb_diag.py -v
C:\Python311\python.exe -O firmware/usb-diag/tests/test_usb_diag.py -v
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/usb0-uboot-rebuild
```

Previously flashed application (preserved): `build/usb0-uboot/fm1-usb-diag.app.bin`, **129,360 bytes**,
SHA-256 `c453282d223adeccc8596cfcca3729dab454804375b76057cf1754b29c7c2120`.
The original `build/usb0` binaries remain unchanged.
The build writes ELF, disassembly, source/archive hashes, and a static audit.
The common audit retains exact startup, boot bridge, power-init and delayed
integrity checks, with separately reviewed USB-link instruction encodings.
USB descriptors, DMA bounds and interrupt return are checked too.

The existing stock-layout packager creates a private **1 MiB unqualified** image
under `private-backups/usb-diag-candidates/<SHA256>/fm1-usb-diag.UNQUALIFIED.bin`.
Its command output and adjacent `package-report.json` identify the exact image.
Boot/config/reserved bytes, stock file offsets and unused application tail are
verified unchanged; all relevant CRCs and application round-trip pass.
This is a unit-specific raw image, **not a UFW/fwsc update file**. Do not publish.
The working stock rollback and previous NES images have not been overwritten.

Current validation covers native parser/control cases, IRQ queue/boot gates,
the Python-to-real-C parser transfer, linked-image corruption checks and the
new peripheral logic/lifecycle tests. See PERIPHERAL_TEST.md for the exact
commands. Offline tests do not establish peripheral or flash-update success.

## Host tools (only after a qualified installation enumerates)

`pyserial` 3.5 is available in `C:\Python311`. Listing opens no ports:

```powershell
C:\Python311\python.exe tools/jieli-wl82/usb_diag_client.py list
# Example COM7 only; not a detected FM-1 port:
C:\Python311\python.exe tools/jieli-wl82/usb_diag_client.py info --port COM7
C:\Python311\python.exe tools/jieli-wl82/usb_diag_client.py verify --port COM7 --file firmware/usb-diag/build/usb0-uboot/fm1-usb-diag.app.bin
```

The client verifies VID/PID before opening and the protocol identity before
transferring. It has **no install command**. LF-only ASCII framing is deliberate.
Keep the receive-only `usb-serial-terminal.ps1` for continuous logs; close it
before using the protocol client so only one process owns the COM port.

Use an interactive terminal for the two-command UBOOT exchange above; the
current Python client only supports info/verify, not UBOOT or peripheral tests.
The receive-only monitor must be closed before an interactive terminal opens
the same port. Preparing the peripheral-test profile performed no device I/O.
