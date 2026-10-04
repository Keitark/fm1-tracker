# Stock pixel-polling isolation test, 2026-09-28

## Evidence and question

The user reports the pre-OS NES candidate still has a blank display, then
manually re-entered UBOOT. Its flash readbacks passed, but no normal-boot PC
or trace establishes execution of the checkpoint. No further flash read is
performed before writing, as requested. OS startup is not required by the
checkpoint's intended call path, but earlier startup/power/transport remain
unlocalized.

The previous stock V14 white-frame control was physically confirmed: white
before the normal synth UI. Stock uses DMA for pixel data; the replacement
checkpoint uses per-byte polling. This diagnostic tests polling under the
known-working stock startup, not another speculative SDK startup change.

## Exact change

Starting with the accepted white-marker application, replace only the 46 bytes
at CPU `02021880..020218AE` (end exclusive), inside the first-fill function.
The existing marker instruction remains. Compared with unmodified V14 there
are just these two application patch ranges plus automatically recalculated
container CRC bytes; all other application, boot, configuration and reserved
bytes are preserved. Original stock and candidate full images differ in 52
bytes, contained in sectors `4000` and `25000`.

The old loop sends 2,880 DMA blocks of 40 white bytes. The new loop sends
115,200 bytes of FF through SPI1 BUF using the same pending-clear and bit-15
completion test used by stock commands and the NES byte transport. CS stays
low throughout the pixels, with DC high; this also differs from stock's
40-byte CS grouping. No clock, GPIO setup, rail or IRQ setup changes.

At entry, r0 is PC OUT (`50080`), r1 SPI1 CON (`11D00`), and stock has already
sent RAMWR (`2C`). r3 counts down from `1C200`; r4 polls SPI1, then is restored
to the current OUT value for the original display-on sequence. The loop makes
no calls and changes no stack, RAM globals, r0/r1/r2 or other registers. The
original prologue/epilogue and display-on command `29` remain. Like stock's
original loop, completion polling has no software timeout. This is not a
claim of equivalence to the full NES driver, which also changes parameter
transfers, window geometry, delay implementation and initialization placement.

## Build and validation

Source: `firmware/nes/boot/stock_polled_pixels.S`.
Builder: `firmware/nes/build_stock_polled.py`.
Artifacts: `private-backups/stock-polled-white-20260928/`.

Application: 584,956 bytes, SHA-256
`1c1ecc0b62bc2ee922dcdc7fe48e1bb6f09a3f5fea330a34c9f536eff540cd82`.
Full image: 1,048,576 bytes, SHA-256
`444b5d8cea8a3ffa13d72e331e148a34e9f5af902965eb38f80d428dbad2b792`.

Assembler output is exactly the pinned 46-byte replacement, no relocations or
external dependencies. Three trailing NOPs fill the original loop allocation.
All container CRC, application round-trip, boot/config/reserved preservation
checks pass. The SDK checkout remains unchanged.

`tools/jieli-wl82/test_stock_polled.py`: six tests pass normally and with `-O`.
They check every replacement-byte mutation is rejected, unchanged application
outside the patch, a narrow instruction model's byte count/DC/CS/fallthrough,
exact 45-sector fake deployment, directory-last order, no pre-write reads,
two full final reads, fail-stop, invalid input/scope rejection and delegation
to the existing CLI. The instruction model assumes successful SPI completion;
these tests do not measure hardware timing or prove physical boot.

## Device workflow and interpretation

`deploy_stock_polled_via_cli.py` selects only the exact saved pre-OS source and
this target. It regenerates the target offline and calls the existing common
deployment orchestration. `run-elevated-stock-polled.ps1` requires exactly one
WL82 UBOOT disk and the original helper hash, uploads that RAM helper using
the existing CLI's `help`, and requires online NOR identity `856014` before
the write. It performs no chip-key query and uses no KALLSUP material.

The expected source is the saved successful readback:
`private-backups/pre-os-write-20260928-025138/final-1.bin`.
No claim is made that current flash was freshly read. Transitioning from NES
back to this stock diagnostic changes 45 application sectors, within the
previously established `4000..30000` bounds, directory last. Post-erase,
post-program, independent sector and two full final checks are retained.
No automatic reset, retry, rollback or unrelated-sector repair is allowed.

Deployment request issued through hidden elevated PowerShell, run directory
`private-backups/stock-polled-write-20260928-030455/`. Windows returned
"This operation was canceled by the user" from Start-Process before returning
a process ID. The session has ended; the directory remains empty and no helper
upload or write started. No automatic retry was made at that time.

The user then explicitly requested another flash attempt. The retry at 07:07
JST succeeded, using `private-backups/stock-polled-write-20260928-070755/`.
The original RAM helper loaded, NOR identity matched `856014`, and existing
jluboottool.py wrote and verified all 45 sectors, directory last. Receipt status
is `written_and_twice_readback_verified`. Both `final-1.bin` and `final-2.bin`
are 1,048,576 bytes and independently hash to
`444b5d8cea8a3ffa13d72e331e148a34e9f5af902965eb38f80d428dbad2b792`.
No before-sector/preflight read files exist; no reset was sent. The installed
image is now this stock polling diagnostic, not the failed NES candidate.
Physical result: **passed by user observation**. In response to the requested
normal-boot check (brief white screen followed by stock UI), the user replied
"it does". This confirms this stock-based pixel-polling diagnostic boots and
produces the expected picture; it does not qualify NES execution or the full
replacement LCD driver. No new helper upload, read, write or reset accompanied
recording this observation.

This rules out the broad hypothesis that the panel cannot accept polled pixel
bytes at all. It does not establish the NES SPI clock, register state, timing,
parameter transfers, window geometry, power sequence or arrival at the display
call. Those remain candidates. Preserve this working control while narrowing
the remaining startup/initialization differences; do not call it a root-cause fix.

Expected normal boot: **white first frame, then stock synth UI, not NES**.
If it appears, polled white-pixel output works under stock prerequisites; that
does not prove the complete NES display driver or startup. If blank, this
changed transfer sequence/timing becomes a narrower suspect. If normal UI
appears without a noticed white flash, marker visibility/timing is inconclusive.
The previously accepted white-marker image remains preserved for recovery.
