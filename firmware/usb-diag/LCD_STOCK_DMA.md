# LCD stock-DMA diagnostic candidate

The CON-before-BAUD variant also remained blank, per the user's report.
The next prepared (not flashed) candidate is documented in
[LCD_STOCK_SEQUENCE.md](LCD_STOCK_SEQUENCE.md). It adds stock's established
PA2-low write, exact LCD GPIO helper order, and black-first 40-byte DMA fills;
it does not claim complete stock-firmware equivalence.

## Latest deployment — CON-before-BAUD correction

Flashed at the user's explicit request using serial UBOOT and the established
elevated PowerShell / existing `jluboottool.py` writer. Sectors `0x8000` and
`0x4000` (directory last) were written and individually verified. Both full
1 MiB readbacks match `a1ec12bd148a0dc3c55c507faf7c769143eda9260821cbf54daff4fe534c1c2a`.
Receipt: `private-backups/lcd-stock-dma-order-write-20260928-233447/deployment.json`.
No pre-write device dump or automatic reset. Normal boot and LCD result remain
untested for this correction. The preparation notes below predate deployment.

After the user's power-cycle, COM10 replied with the expected diagnostic
identity. The correction's 20-second LCD test reported init=0/stage=4 and
frame result=0 through elapsed=20000; USB heartbeats continued. Register phases
0/1/2/4 were captured and matched the preceding variant's snapshots. STOP
returned TEST END result=0. No flash or reset commands were sent during this
test. Visible output awaits the user's report. The initial empty-line framing
reset produced `ERR PROTOCOL_OR_CRC ABORTED` before HELLO; this was not an LCD
or flash failure. Serial evidence is in `lcd-test-*.log` under the latest
deployment folder.

Build: `firmware/usb-diag/build/peripherals-stock-dma-order`.
The shared display driver now writes `CON=0x4021` before `BAUD=4`, matching
stock010 `020230b6..020230b8`. Target disassembly confirms CON at `02004f10`
and BAUD at `02004f12`. Only nine application bytes differ from the installed
stock-DMA build, entirely inside `02004f0c..02004f15`; all other application
bytes and total length are unchanged. No GPIO, power, USB or recovery changes.

The host MMIO model now captures controller state at the BAUD access and checks
it when the write becomes visible. Register snapshot reads do not count as
writes. This regression failed against the old code and passes with the fix.
Seven native USB/peripheral suites and three display regression suites pass;
11 USB Python tests and 23 linked-image tests (normal and optimized Python)
pass. Static audits and all stock-layout packaging checks pass; SDK stays clean.

- Application: 136816 bytes; SHA-256
  `d3493306c3f2ee4b1b95926e6dfe73d022d20c91ea98877ff09084e6176284e6`.
- Full image: 1048576 bytes; SHA-256
  `a1ec12bd148a0dc3c55c507faf7c769143eda9260821cbf54daff4fe534c1c2a`.
- Candidate: `private-backups/usb-diag-candidates/a1ec12bd148a0dc3c55c507faf7c769143eda9260821cbf54daff4fe534c1c2a/fm1-usb-diag.UNQUALIFIED.bin`.
- Current installed rollback/saved baseline:
  `private-backups/lcd-stock-dma-write-20260928-232153/final-2.bin`,
  hash `76f81d5f02594707e21a8bffd46315a1e01ec983684efcba2181f22f56a628fa`.

No device commands or flashing occurred during this correction. The serial
identity remains `LCD=STOCK-DMA`; identity alone cannot distinguish these two
builds. Use deployment/readback hashes. Visible output is still unqualified:
correcting the confirmed ordering mismatch does not prove the blank-screen
cause. To reproduce, use the existing build/package commands below with output
directory `peripherals-stock-dma-order` instead of `peripherals-stock-dma`.

## Deployment — 2026-09-28 23:21 JST

Flashed at the user's explicit request. COM10 confirmed the installed stock-fill
identity and idle worker; serial UBOOT entry succeeded. The established elevated
PowerShell / existing `jluboottool.py` path then wrote all 34 changed sectors,
with directory sector `0x4000` last. Each sector verified, followed by two full
1 MiB readbacks matching candidate SHA-256
`76f81d5f02594707e21a8bffd46315a1e01ec983684efcba2181f22f56a628fa`.
Receipt: `private-backups/lcd-stock-dma-write-20260928-232153/deployment.json`.
No pre-write device dump, new environment, or automatic reset. The device is
left in the loader; normal boot, new serial identity, and LCD output are still
awaiting the next hardware test. Preparation notes below are historical.

After the user's power-cycle, COM10 returned the exact `LCD=STOCK-DMA`
identity. A 20-second LCD test completed with init result=0/stage=4 and every
reported frame result=0. Register phases 0, 1, 2 and 4 were captured; no failure
phase appeared. USB heartbeats continued, STOP succeeded, and final status was
ready=1/busy=0/dropped=0/fault=0. GPIOC DIR changed from `ffff` to `f87f`,
MUX1 from 0 to `10`, and SPI CON was `21` while enabled and 0 after STOP.
BAUD and DMA_ADR read as zero, but the SDK declares those registers write-only;
their read values do not demonstrate a failed write. No additional flash or
reset commands were sent. **Visible LCD output still needs the user's report.**
Serial evidence is saved as `lcd-test-*.log` in the deployment directory above.

The user subsequently reported **still completely blank**. Visible-output
acceptance failed. Follow-up instruction comparison found an overlooked setup
ordering difference: stock010 `020230b6` writes SPI CON=`4021`, then `020230b8`
writes BAUD=4. This candidate writes BAUD=4 at `02004f0c` while CON is disabled,
then writes CON=`4021` at `02004f14`. Whether enabling resets or ignores the
earlier divider write has not been established; write-only BAUD readback cannot
resolve that. The setup is therefore not instruction-order-equivalent to stock.

The stock GPIO helpers were followed for the actual PC7..10 arguments, including
the port-base table at `02055aa0`. PC7 uses `02003bbe` (OUT high, DIR output,
critical-section synchronization); PC8..10 use `02004098` (DIR output). No hidden
panel-reset or backlight write was found on those selected helper branches.
The replacement additionally changes pulls/DIE and performs mux setup later.
Stock's earlier UI setup drives PA1 and PA2 low (`0200445e`, `02004504`);
their electrical roles remain unresolved and these writes are not reproduced
by the isolated LCD test. Do not silently label them unnecessary or panel enables.

The live GPIOC input snapshot is `0000007f` even when OUT shows PC7 high, and
after STOP when OUT shows PC7/8 high. This is an observation, not proof of a
short or missing power: pin-input read semantics and a healthy-stock comparison
are still needed. Global P33 latch clearing is present in the linked startup;
absence of the routine is not the explanation. After first fill/display-on,
stock immediately enables SPI IRQ16 and prepares rendering state; this is not
evidence of a dedicated later backlight-enable operation.

This follow-up changed documentation only: no source change, build, flash,
GPIO probing, or new device command. The next bounded source correction is the
proven CON/BAUD ordering mismatch, with a test that checks write order rather
than just final register values. It must not be presented as a proven screen fix.

Prepared 2026-09-28 after the stock-window peripheral test remained blank.
**Initially verified offline; now flashed as recorded above. Visible output remains unproven.**

## Bounded changes

- Configure SPI1/GPIO before the initial 100 ms requested delay, matching stock
  setup order. RTOS tick rounding still applies to delays.
- Keep polled command bytes, but send initialization and address-window
  parameters through SPI1 DMA, as stock does. Initialization uses 19 DMA
  transfers totaling 62 parameter bytes, including the first fill's windows.
- Copy parameters into a persistent 64-byte, 64-byte-aligned internal-RAM
  buffer; issue `csync` before starting DMA. The linked buffer is at
  `0x01c03d80` inside `.ram0_bss`, not flash, SDRAM, or cache RAM.
- Retain the prior stock-window `00 00 00 F0` / 115200-byte continuous-CS
  polled white pixel stream, panel table, SPI baud, and clock/power setup.
- Queue fixed read-only GPIOC/IOMAP/SPI1 register snapshots. No speculative
  reset, backlight or power GPIO writes were introduced.

DMA waits are bounded by 100 ms or 1,000,000 polls. Failure records error -6,
captures registers, deasserts CS and disables SPI1. The parameter buffer remains
alive throughout. Existing USB serial, STOP, serial UBOOT, audio, key and encoder
paths are retained. Pixel output still yields to the RTOS; this is not a claim
of cycle-identical stock behavior or a proven diagnosis of the blank display.

## Serial evidence expected after a separately authorized flash

115200, 8N1, no flow control; DTR/RTS enabled; uppercase commands with LF only.
`HELLO` must contain `LCD=STOCK-DMA`. `TEST LCD` remains opt-in.

`LCDREG phase=N` produces five lines per snapshot:

- 0: before peripheral setup
- 1: configured, before initial delay
- 2: after first white fill and display-on
- 3: failure, before cleanup
- 4: stopped, SPI disabled

Fields cover GPIOC OUT/IN/DIR/DIE/PU/PD/HD0/HD/DIEH; IOMAP CON0/1/2/3/5;
SPI1 CON/BAUD/DMA_ADR/DMA_CNT; and the software error. The bounded event queue
can report dropped lines; these are not arbitrary MMIO reads or panel readback.
Controller completion and a successful init do not prove LCD acknowledgement.
Use `TEST STOP`, wait for TEST END and idle status before serial UBOOT.

## Artifact and validation

Build directory: `firmware/usb-diag/build/peripherals-stock-dma`.

- Application: 136816 bytes; SHA-256
  `7fe3509c06258824ea3c05454db616c428ac2fb262045f10b03ee371a075f568`.
- Full image: 1048576 bytes; SHA-256
  `76f81d5f02594707e21a8bffd46315a1e01ec983684efcba2181f22f56a628fa`.
- Image: `private-backups/usb-diag-candidates/76f81d5f02594707e21a8bffd46315a1e01ec983684efcba2181f22f56a628fa/fm1-usb-diag.UNQUALIFIED.bin`.

Passed: 7 native USB/peripheral/LCD suites; 3 original RGB/early-display/table
regressions; 11 USB Python tests; 23 linked-image/evidence tests in both normal
and optimized Python. Negative tests reject DMA buffer misalignment and flash
placement. Static startup, boot trace, power, USB/UBOOT and IRQ audits pass.
The SDK checkout remains clean. Packaging passes all application/CRC checks
and preserves stock boot/config/reserved bytes and offsets. Package remains
marked unqualified: no hardware I/O or flash occurred during this preparation.

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --out firmware/usb-diag/build/peripherals-stock-dma
cmake --build firmware/usb-diag/build/host --config Release
ctest --test-dir firmware/usb-diag/build/host -C Release --output-on-failure
$env:FM1_PERIPHERAL_BUILD='F:\dev\fm1\firmware\usb-diag\build\peripherals-stock-dma'
C:\Python311\python.exe firmware/usb-diag/tests/test_peripheral_image.py
C:\Python311\python.exe -O firmware/usb-diag/tests/test_peripheral_image.py
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/peripherals-stock-dma
```

The currently installed stock-fill rollback and verified saved baseline remain
`private-backups/lcd-stock-fill-write-20260928-230301/final-2.bin`, hash
`a2d149bde0227909b0ee5714d5f127b9a49e7f3c55acd18315fc5459365f45d7`.
Any next deployment must use the established existing PowerShell/Jieli writer,
without a new environment or pre-write device dump, and retain post-write
verification. This document does not authorize deployment.
