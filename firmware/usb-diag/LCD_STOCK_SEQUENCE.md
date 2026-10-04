# Recovered stock LCD transaction sequence — not a complete stock firmware

## Deployment — 2026-09-28 23:55 JST

Flashed at the user's explicit request via serial UBOOT, elevated PowerShell
and the existing `jluboottool.py`. All 34 changed sectors verified; directory
sector `0x4000` was last. Two complete 1 MiB readbacks both match
`57982447cb35c4b173afc513881bcc64c6ed865db4356a8c7def5feaf19c5ec9`.
Receipt: `private-backups/lcd-stock-sequence-write-20260928-235532/deployment.json`.
No pre-write hardware dump, new environment/writer or automatic reset.
At deployment the device was left in the loader awaiting normal power-cycle.
Boot and visible LCD output were subsequently confirmed below; serial UBOOT
re-entry from this particular image has not yet been tested.
The preparation notes below predate deployment.

Post-power-cycle bench run: COM10 returned exact `LCD=STOCK-SEQ` identity.
The 20-second LCD test logged init result=0/stage=4 and all frame results=0.
PA_DIR changed from `0000ffff` to `0000fffb` while PA_OUT stayed zero, confirming
the PA2 output-low register configuration (not a measured pin voltage).
Snapshots 0/1/2/4 were received, USB heartbeats continued, and STOP returned to
ready=1/busy=0/dropped=0/fault=0. No flash/reset commands were sent. Visible
output was confirmed by the user ("you did it !!!!"). Serial logs are `lcd-test-*.log` in the
deployment folder. The initial empty-line framing reset's protocol error was
before HELLO and unrelated to LCD/DMA completion.

Prepared after the CON-before-BAUD test also produced no screen.
**Flashed, readback-verified, boot-tested and user-confirmed visible output.**
The individual cause of success within the combined sequence changes has not
been isolated; dedicated backlight control remains unidentified.

## Subsequent input tests — 2026-09-29 JST

Existing firmware only; no rebuild, flash, reset or audio command was issued.
COM10 identified `LCD=STOCK-SEQ` throughout. Both tests ended with result=0,
ready=1/busy=0/dropped=0/fault=0, with USB heartbeats continuing.

- `keys-test-20260929-000022.log`: 30-second capture recorded DOWN/UP events
  for piano slots 14,16,18,19,20,21,23,25,26,28,32,34,36 and panel slots
  2,5,7,8. This verifies exercised contacts only, not all 41 physical controls
  or their complete physical mapping. Nominal slot14 is MIDI53/F.
- `knobs-test-20260929-000131.log`: 35-second capture recorded activity for
  encoder IDs 0,1,3,4,5,6; ID2 remained unchanged. The log filters unchanged
  per-ID reports. Final signed edges / invalid transitions were:

  | ID | Edges | Invalid |
  |---|---:|---:|
  | 0 | -4 | 3 |
  | 1 | -4 | 2 |
  | 2 | 0 | 0 |
  | 3 | -2 | 5 |
  | 4 | -2 | 3 |
  | 5 | -2 | 3 |
  | 6 | -6 | 9 |

Encoder decoding is **not qualified**: invalid transitions occurred, physical
direction/detent mapping is unverified, and ID2 was not demonstrated responding.
Polling can miss intermediate AB states; contact bounce or mapping also needs
checking. These logs do not establish a device fault. Analog volume is outside
this encoder test. Audio has not yet been tested in this run.
All logs reside in the deployment folder cited above.

Audio was subsequently tested with the user's low-volume speaker/amp setup:
`audio-test-20260929-001830.log` reports result=0, frames=132300, irqs=2074,
ready=1/busy=0/dropped=0/fault=0. The user confirmed hearing the two-second tone.
This qualifies audible test output, not NES audio quality or all analog paths.

The next integrated candidate and encoder correction are documented in
[NES_CDC_SMB1.md](NES_CDC_SMB1.md); this working diagnostic remains the rollback.

### One-at-a-time encoder mapping — 2026-09-29 JST

The user exercised only the named encoder in each separate 60-second capture,
clockwise first and then counterclockwise. Only the corresponding ID changed
in each run; the other six remained at their initial zero counts/states.

| Physical label | ID | Final signed edges | Invalid transitions | Log |
|---|---:|---:|---:|---|
| SELECT | 0 | -4 | 8 | map-SELECT-20260929-000742.log |
| PRESETS | 6 | 12 | 8 | map-PRESETS-20260929-000913.log |
| ALGORITHM | 1 | 2 | 11 | map-ALGORITHM-20260929-001041.log |
| KNOB1 | 2 | 2 | 9 | map-KNOB1-20260929-001151.log |
| KNOB2 | 3 | -2 | 9 | map-KNOB2-20260929-001300.log |
| KNOB3 | 4 | 4 | 10 | map-KNOB3-20260929-001410.log |
| KNOB4 | 5 | 2 | 9 | map-KNOB4-20260929-001520.log |

This establishes the physical name-to-ID mapping, including previously
unexercised ID2. Counts generally increased during the requested clockwise
phase and decreased on return. All seven still recorded invalid transitions;
edge counts are not reliable detent counts, and occasional reversals occurred.
The intended five-click movements were user-guided, not independently measured.
Scanner timing/contact-state handling remains to be investigated; no hardware
fault is established. This supersedes the earlier "physical mapping unverified"
limitation for these seven labels only.

Each run ended with result=0 and ready=1/busy=0/dropped=0/fault=0. Logs retain
changed per-ID reports and USB heartbeats. The framing-reset empty line generated
the expected protocol error before successful HELLO; it was not a test failure.
No firmware rebuild, flash, audio test or reset was performed. The final test
stopped and the COM port was closed. Logs are in the same deployment folder.

## Reproduced operations

The new opt-in `FM1_LCD_STOCK_SEQUENCE` variant retains USB diagnostics and serial
UBOOT. It changes the LCD bring-up as one bounded sequence, using hash-pinned
stock010 instructions rather than assigning a guessed backlight pin.

| Order | Stock evidence (CPU address) | Implementation |
|---|---|---|
| Before LCD setup | `020044fa..02004504`: r0=2, r1=0, gpio_direction_output | Drive PA2 low with the SDK GPIO helper |
| LCD pin routing | `02023086` | IOMAP CON1 bit4 set before PC setup |
| CS output | `0202308a..0202308e` | gpio_direction_output(PC7,1) |
| Other LCD outputs | `02023092..020230a8` | gpio_set_direction(PC8/9/10,0), in that order |
| SPI setup | `020230b6..020230b8` | CON=4021 then BAUD=4; no extra initial CON=0 |
| First delay | `020230be`, helper `0205b9d2` | os_time_dly(10) |
| Panel table | `020230e2..0202313c`, table `0205012c` | Unchanged commands; DMA parameters; sleep-out delay os_time_dly(12) |
| Initial window | `02021604..020216bc` | 2A/2B parameters 00 00 00 F0, followed by 2C |
| Initial fill | `020216cc..020216f0` | 2880 DMA transfers x40 bytes of zero; CS deasserted after each block |
| Display-on | `02021704` | 29 only after the entire black fill completes |

The GPIO helpers' stock branches were reviewed: ordinary PA/PC output writes
OUT and clears DIR under synchronization; PC direction changes clear DIR only.
The new variant does not add pull-up/down or DIE modifications absent from
those LCD setup calls. PA2 remains low on STOP; an opposite "backlight off"
level would be an unsupported electrical assumption. The older profiles remain
available without these new pin operations.

The dedicated 64-byte aligned internal-RAM buffer is reused for parameter and
40-byte pixel DMA. Each transfer finishes before reuse. Timeout disables SPI;
no received-code execution or flash writer is introduced into the firmware.

## What is intentionally NOT claimed

- PA2's electrical connection is unknown. Its low output is proven stock
  behavior, not proof of a backlight, reset or shift-register-enable connection.
- This is **not perfectly identical to stock startup**. The replacement still
  uses its linked SDK/RTOS, stock-derived power API, USB worker and opt-in test.
  It does not reproduce all concurrent stock audio/scanner/UART/UI tasks. PA1's
  scanner latch and the scanner's asynchronous output stream are not added to
  this isolated LCD test. Stock IRQ16-driven later rendering is not reproduced.
- Stock's power and cache setup have prior static audits, but there is no live
  healthy-stock register trace proving the same inherited hardware state.
- Bounded transfer waits, error checks and register logging remain. The DMA
  pixel loop yields every64 blocks with CS high to keep recovery responsive;
  this changes timing relative to stock. Initial requested sleeps use stock's
  10ms tick conversion (10/12 ticks), with a stopped-time check afterward.
- The first fill is black as in stock. Later one-second diagnostic frames are
  white, not stock synth graphics. Observe after TEST LCD has run for >1 second.
- The actual unmodified-stock-startup white-marker control remains preserved
  in `private-backups/stock-marker-v14-white-20260928/`; its prior successful
  bench test is not evidence that this replacement will work.

## Artifact and verification

Build: `firmware/usb-diag/build/peripherals-stock-sequence`.

- Application: 137008 bytes, SHA-256
  `463daa90a6d339fd18816943c0ca88b5f6f668d710e90f3c89f99d7838b4f47a`.
- Raw full image: 1048576 bytes, SHA-256
  `57982447cb35c4b173afc513881bcc64c6ed865db4356a8c7def5feaf19c5ec9`.
- Image: `private-backups/usb-diag-candidates/57982447cb35c4b173afc513881bcc64c6ed865db4356a8c7def5feaf19c5ec9/fm1-usb-diag.UNQUALIFIED.bin`.
- Current installed, twice-readback-verified baseline:
  `private-backups/lcd-stock-sequence-write-20260928-235532/final-2.bin`,
  SHA-256 `57982447cb35c4b173afc513881bcc64c6ed865db4356a8c7def5feaf19c5ec9`.
- Previous installed/saved baseline:
  `private-backups/lcd-stock-dma-order-write-20260928-233447/final-2.bin`,
  SHA-256 `a1ec12bd148a0dc3c55c507faf7c769143eda9260821cbf54daff4fe534c1c2a`.

Passed: 8 native suites; 3 prior RGB/early-display/table regressions; 11 USB
Python checks; 24 linked-image/evidence tests, both normally and with Python -O.
Host tests check GPIO call order, preservation of unrelated bits/pulls/DIE,
enable-before-divider, exact 10/12-tick requests, 2899 initial DMA transfers
(19 parameters +2880 pixels), all first-frame bytes black before29, next frame
white, and parameter/pixel DMA timeout cleanup. Hash-pinned opcode tests check
PA2 evidence and prior fill/table/encoder evidence. Static startup, power,
USB/UBOOT, IRQ and internal-RAM DMA audits pass. SDK checkout is clean.
All stock-layout package CRC/offset/boot/config/reserved preservation checks pass.

Target disassembly was also inspected: PA2 arguments at `02004f72/74`, helper
call at `02004f76`, PC7 call at `02004f8a`, PC8/9/10 calls at `02004f92/9a/fa2`,
SPI CON/BAUD at `02004fc4/c6`. Pixel DMA loop passes40 and iterates2880 times.

Serial identity is `LCD=STOCK-SEQ`. Logs include `PA_OUT`/`PA_DIR` in phases
0/1/2/3/4 alongside prior GPIOC/SPI snapshots. TEST LCD remains opt-in; audio
does not start automatically. No new writer, downloader or Python environment.
No device command or flash operation occurred while preparing this build.

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --out firmware/usb-diag/build/peripherals-stock-sequence
cmake --build firmware/usb-diag/build/host --config Release
ctest --test-dir firmware/usb-diag/build/host -C Release --output-on-failure
$env:FM1_PERIPHERAL_BUILD='F:\dev\fm1\firmware\usb-diag\build\peripherals-stock-sequence'
C:\Python311\python.exe firmware/usb-diag/tests/test_peripheral_image.py
C:\Python311\python.exe -O firmware/usb-diag/tests/test_peripheral_image.py
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/peripherals-stock-sequence
```
