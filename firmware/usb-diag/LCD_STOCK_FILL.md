# LCD stock-window test

Follow-up: [stock-DMA diagnostic candidate](LCD_STOCK_DMA.md) implements the
setup-order and DMA-parameter comparison below, with fixed-register logging.
It passed offline validation and was flashed with two matching full readbacks;
its normal boot and LCD result are still awaiting the next hardware test.

## Deployment — 2026-09-28 23:03 JST

Flashed at the user's explicit request using the established elevated
PowerShell → existing `jluboottool.py` flow. Serial UBOOT entry first succeeded
from peripheral v1; Windows enumerated WL82 UBOOT1.00. All 34 changed sectors
verified, followed by two full 1 MiB readbacks matching SHA-256
`a2d149bde0227909b0ee5714d5f127b9a49e7f3c55acd18315fc5459365f45d7`.
Receipt: `private-backups/lcd-stock-fill-write-20260928-230301/deployment.json`.
No pre-write hardware reads or automatic reset. Preparation notes below predate
this deployment.

After power-cycle, COM10 replied with the exact `LCD=STOCK-FILL` identity.
The 20-second test logged the expected window, 115200 FF bytes, init result=0,
stage=4 and repeated frame result=0. USB stayed responsive; STOP ended with
ready=1, busy=0, mode=0 and no reported fault. **The user reported the display
remained blank. Visible-output acceptance failed.** No further flash occurred
while reviewing that result.

Follow-up comparison confirms PC7 CS/PC8 DC, output direction for PC7..10,
SPI1 mux bit4, CON=4021 and BAUD=4 agree with stock's explicit LCD setup.
This is source/static agreement, not live pin or register measurements.
Two concrete differences remain: stock configures SPI/GPIO before the 100 ms
wait (the current RTOS diagnostic waits before configuration), and stock uses
DMA for initialization parameters and window bytes (the diagnostic polls each
byte). The accepted stock polled-white experiment changed only pixel output;
it retained stock DMA parameter transfers and all stock setup. Thus its success
does not qualify our entire command/parameter transport. Neither difference is
yet a demonstrated root cause. A bounded stock-equivalent transfer test with
fixed-register diagnostics is the next software isolation step; SPI signals
and panel power/reset/backlight remain physically unobserved.

## Observed result and reason for this test

Peripheral v1 booted, enumerated on COM10 and remained responsive during the
25-second LCD test. Reported clocks were sys=360 MHz and lsb=60 MHz. LCD init
and frame writes returned result=0, stage=4; STOP returned the worker to idle.
The user reported the display remained blank. These results confirm program
execution and SPI-controller completion, **not panel acknowledgement**.

Two complete flash readbacks had matched v1, so missing flash sectors do not
explain that run. The user's concern about the addressed LCD region prompted
comparison with stock's actual initial fill, beyond the initialization table.

## Concrete difference

The stock initialization table contains X=0..239 and Y=40..279. However, stock
010's initial fill at `0x02021600` **overwrites both windows**. Instructions
`0x02021604..0x02021613` load 240 into two registers, reverse each word's byte
order, and store them at stack offsets 80 and 84. Each is sent as four parameter
bytes: **`00 00 00 F0`**, once after `2A` and once after `2B`.
Stock then sends `2C`, 2,880 blocks of 40 pixel bytes (115,200 total), and `29`.
The complete 282-byte routine is identical in V14 at `0x020217b8`.

This test preserves those exact observed window bytes and pixel count; it does
not reinterpret the endpoint as 239 or claim a proven native controller size.
It differs from v1's per-row writes using Y+40. Since these regions overlap,
this discrepancy alone is **not a proven explanation for a completely blank
screen**.

## New variant

Build `peripherals-stock-fill` retains USB, UBOOT, audio, keys and encoders.
It is opt-in at build time; the ordinary RGB profile is unchanged.

`TEST LCD` initializes the same panel table, sets the stock fill window once,
sends one `RAMWR (2C)`, and streams 115,200 bytes of `FF` from immediate SPI
register writes. No pixel framebuffer, per-row addressing or DMA pixel source.
CS remains low/DC high throughout the pixel stream. Watchdog servicing and
bounded waits remain; RTOS yields introduce pauses, not new transactions.
First fill precedes display-on (`29`) as before. Subsequent frames repeat white.
`TEST STOP` stops traffic, leaving the last picture in panel memory.

Identity includes `LCD=STOCK-FILL`. Serial prints exact window/count before
initialization. Success remains a transport result, not visible-output proof.
No reset/backlight pins, power settings, pixel format, SPI baud, or clock setup
were changed. No speculative 320-row controller-memory range was introduced.

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --out firmware/usb-diag/build/peripherals-stock-fill
cmake --build firmware/usb-diag/build/host --config Release
ctest --test-dir firmware/usb-diag/build/host -C Release --output-on-failure
$env:FM1_PERIPHERAL_BUILD='F:\dev\fm1\firmware\usb-diag\build\peripherals-stock-fill'
C:\Python311\python.exe firmware/usb-diag/tests/test_peripheral_image.py
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/peripherals-stock-fill
```

Offline tests check the stock opcode evidence in hash-pinned 010 and V14,
window parameters, command counts, all transmitted pixel bytes, continuous
CS/DC state, timeout cleanup, and unchanged original RGB/early-display tests.
The existing boot/power/USB/UBOOT/IRQ audits and package-preservation checks
remain enabled. **Not flashed during preparation; no device commands sent.**
