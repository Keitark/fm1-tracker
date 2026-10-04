# FM-1 peripheral diagnostic v1

## Prepared artifact and validation receipt — 2026-09-28

- Application: **136,016 bytes**, SHA-256
  `0fa13d836f15ff40d0dec49d2f412d9e01839467ffdd85f6e4d4a7f8442e68bf`.
- Full private raw image: **1,048,576 bytes**, SHA-256
  `fccd4a9ad8f312647f713fcf1649e15e0fc47124db9ec03d5109773e7bd6da97`.
- Image path:
  `F:\dev\fm1\private-backups\usb-diag-candidates\fccd4a9ad8f312647f713fcf1649e15e0fc47124db9ec03d5109773e7bd6da97\fm1-usb-diag.UNQUALIFIED.bin`.
- Passed: 5 native protocol/logic/lifecycle suites; 11 USB transport/legacy-link
  tests; 20 peripheral/legacy-link tests in both normal and optimized Python;
  existing LCD and keyscan host-model suites. Static boot audit and all 9
  package preservation/CRC checks passed. SDK checkout remains unmodified.
- Preserved USB rollback image hash rechecked:
  `1d07e35b73e87ff6557b5da00190ed4d91f1dbdcc45bf2dc5512a67dc6c640fe`.
- Flashed on explicit request at 22:48 JST using the established elevated
  PowerShell → existing `jluboottool.py` flow. All 34 changed sectors verified,
  followed by two complete 1 MiB readbacks matching the image hash above.
  Receipt: `private-backups/peripheral-test-write-20260928-224840/deployment.json`.
  No automatic reset. New-image boot and peripheral operation are not yet tested.
- Serial-to-UBOOT entry succeeded from the previous USB diagnostic before this
  write; Windows detected WL82 UBOOT1.00. Re-entry from the new image remains
  a separate bench check. The preparation-time notes below predate deployment.

Built from the USB0 CDC diagnostic with the stock-derived LCD, SPI2 matrix and
ALINK0 drivers. This is a separate opt-in test profile, not the NES emulator.
No device was accessed or flashed while preparing this build. Physical
peripheral operation and serial-to-UBOOT transition remain bench tests.

## Operation after installation

Use the FM-1's enumerated COM port (previously COM10), 115200, 8N1, no flow
control, DTR/RTS asserted. Send uppercase commands with **LF only**, not CRLF.
Use an interactive terminal; `usb-serial-terminal.ps1` is receive-only.
Only one program may own the COM port at a time.

`HELLO` identifies `FM1DIAG/1 PERIPHERAL-TEST/1`. The repeating banner includes
`profile=peripheral-test/1`. `HELP` lists commands. Peripherals are off at boot.

| Command | Result |
| --- | --- |
| `TEST LCD` | Initializes the recovered stock panel sequence, then RGB bands, blinking top strip and a white bottom strip after 20 seconds. Logs init result, stage and frame progress. |
| `TEST AUDIO` | ALINK0/channel3, 44.1 kHz stereo, 24-bit samples; one second of silence then two seconds of 441 Hz triangle tone, peak approximately -42 dBFS with 10 ms ramps. Stops automatically. |
| `TEST KEYS` | Debounced DOWN/UP reports for all 41 stock slots: 14 panel buttons, 27 piano keys. Leftmost piano key is F, nominal MIDI 53. |
| `TEST KNOBS` | Reports seven encoders as IDs 0–6, raw AB contact state, signed edge count and invalid-transition count every 250 ms. |
| `TEST STATUS` | Worker availability, active mode, dropped log count, audio frame/IRQ counts and audio fault. |
| `TEST STOP` | Requests shutdown of the current test. Wait for `TEST END` before starting another test or requesting UBOOT. |

Mode numbers are LCD=2, AUDIO=3, KEYS=4, KNOBS=5. Only one test runs at a time.
`OK TEST REQUEST` acknowledges a request, not hardware success. A successful
driver result or IRQ count does not prove a visible picture or audible sound.
LCD STOP stops SPI traffic but leaves the last displayed picture; it does not
invent a backlight power-off pin. Key/knob tests use a one-RTOS-tick poll loop;
turn slowly initially. Fast edges may be missed, even without invalid counts.
Encoder names, physical direction and edges per detent need bench mapping.
The separate analog volume control is **not** included in encoder reports;
no ADC channel is guessed for it. Turn physical volume down before AUDIO.

USB task priority is 10; the separate peripheral worker uses priority 8.
Event logs are bounded (16 lines), with dropped-line count visible in STATUS.
USB session loss or DTR change requests shutdown; audio DMA emits zeros as soon
as it observes the session change. Audio also has a sample-count cap and a
four-second worker timeout. Failed peripheral-task creation leaves USB recovery
running and reports `ready=0`. No test writes persistent device storage.

## Bootloader and update path

After `TEST END`, send `UBOOT`, wait for
`OK UBOOT ARMED CONFIRM-WITHIN-5000MS`, then send `UBOOT CONFIRM` within five
seconds. UBOOT while a test is active requests its shutdown and returns
`ERR UBOOT_RETRY_AFTER_TEST_END`; repeat UBOOT after shutdown. This transition
is implemented but not yet verified on the device. If serial fails, retain
the physical Jieli downloader recovery path.

Actual flashing still uses the established PowerShell →
`C:\Python311\python.exe` → existing `jluboottool.py` workflow. No new writer,
Python environment or package installation was introduced. CDC `COMMIT`
remains blocked: serial UBOOT entry is not direct serial firmware installation.

## Offline build and checks

From `F:\dev\fm1`:

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --out firmware/usb-diag/build/peripherals-v1
cmake -S firmware/usb-diag -B firmware/usb-diag/build/host
cmake --build firmware/usb-diag/build/host --config Release
ctest --test-dir firmware/usb-diag/build/host -C Release --output-on-failure
C:\Python311\python.exe firmware/usb-diag/tests/test_usb_diag.py
C:\Python311\python.exe firmware/usb-diag/tests/test_peripheral_image.py
C:\Python311\python.exe -O firmware/usb-diag/tests/test_peripheral_image.py
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/peripherals-v1
```

Build artifacts, disassembly, source/archive hashes and static audit:
`build/peripherals-v1/`. The packager reports the full 1 MiB image path in
`private-backups/usb-diag-candidates/<SHA256>/` and checks stock boot/config,
reserved data and unused tail preservation plus CRCs and application round-trip.
These are private, unit-specific raw images, not vendor UFW update packages.
Existing stock and working USB rollback images are retained unchanged.

## Evidence and remaining limits

Encoder table: hash-pinned stock FM-1_010 app, XIP `0x020462fc`, seven pairs
`10/20 30/40 91/a1 90/a0 70/80 50/60 15/25`. Stock RAM ISR `0x01c046e4`
assembles active-low AB state and uses direction masks `0x2814`/`0x4182`.
The offline test verifies these bytes against the saved stock image.
Key/panel/audio pin routes are documented in `../nes/STOCK_BOARD_PROFILE.md`.

Validation covers parser framing, UBOOT confirmation and expiry, tone limits,
encoder transitions, mock lifecycle/error/timeout/disconnect handling, existing
LCD/keyscan models, linked boot/power/IRQ/descriptor checks and corrupted-image
rejection. Hardware timing, audio analog path, LCD reset/backlight behavior,
physical inputs and recovery remain unqualified until this candidate is tested.
