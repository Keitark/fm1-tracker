# Tile renderer experiment — 2026-09-29

Continuation of `NES_RENDER_GENERATION.md`. Baseline is the direct native
RGB444 conversion image, 30 MHz SPI, detailed profiling off, measured at
26.32 completed LCD FPS and 59.98 emulated FPS in its prior short capture.
This changes tile pixel generation only; clocks, transfer, audio, input,
frame skipping, startup and USB-to-UBOOT behavior are unchanged.

## Implementation and correctness

Opt-in `--nes-tiles` requires `--nes-render-skip`. The build-only overlay
leaves the pinned vendor checkout unchanged. A 256-entry uint16 table
expands one pattern byte into alternating bits; two reads combine both
bitplanes into eight 2-bit pixel indices. Full background tile rows use
eight unrolled palette/opacity stores. Partial/clipped rows retain the
original pixel helper. Sprites reuse the packed indices only on visible
frames, retaining flip, priority and sprite-zero logic.

This is not a CHR/palette cache. Fetch order, mapper hooks, pattern/nametable
bank changes, ExRAM, live palette updates and CHR RAM writes stay live.
The 512-byte table is at internal RAM address0x01c020ac and is included in
the existing startup copy. The ELF audit checks exact table contents,
size/alignment/placement and rejects host-only reference functions.

115 checks passed: NES29, USB/display25, linked image32, flash broker29.
New tests exhaust all65,536 bitplane pairs with partial rows and clipping,
then compare20,000 randomized scanlines against the exact pre-change
renderer, including complete NES state and mapper callback sequence.
A900-frame SMB1 run compares all displayed pixels, NES state and cumulative
audio hashes against that same reference renderer. Existing skipped-frame,
sprite-zero, display ownership and audio/input tests also pass.

## Build and rollback

Source snapshot: `private-backups/nes-tile-source-20260929-203120/`.
Previous verified image: `private-backups/nes-direct-write-20260929-2019/`.
New build: `firmware/usb-diag/build/nes-smb1-spi30-tiles`.

Application214352 bytes (+1760), SHA256:
`5977753126b645751aecdcfef649a3b67d48ae566805136f470a77570fd0379b`.
Full1MiB candidate SHA256:
`4f4fdbf8239c0c363bb2cf76640a78f0cfa698718fc89fc6e2c1c86fc40b8bed`.
App begins0x4120 and ends0x38670. Reviewed changed sectors are
0x5000..0x38000 inclusive, then directory sector0x4000 last. Bytes beyond
the new app extent and before0x4000 remain identical to the saved baseline.
Existing elevated helper, writer, Python and single full final readback
are used; no pre-read, new environment, or additional UAC process.

## Verified deployment and measurements

Existing writer run `289c7300889243e290286d2f49ec680d` succeeded first attempt:
all53 requested sectors verified, directory last, one full1MiB readback
matching4f4fdbf8... above. Reset sent once. The known reset log UTF8/CP932
decoding exception occurred after sending reset; COM10 HELLO and advancing
frames resolved the ambiguity without another reset or write.

Capture `9419226433264a51816afca752228cea`: NES TIME4120->14150ms,
frames203->805, LCD done107->434. Over10.030s this gives32.60 completed
LCD FPS and60.02 emulated FPS, about23.9% above baseline26.32 LCD FPS.
Independent follow-up capture `38f0d74dfc6643f2b869cc1b07ad1341` ends at
NES TIME67340ms: its last10.030s (57310->67340) advances frames3396->3998,
LCD done1861->2187, giving32.50 LCD FPS and60.02 emulated FPS.
Separate short captures are not identical-input benchmarks; clock records
have10ms granularity. Scene/user input can change the measured throughput.

No recorded audio underruns/rebuffers, NES faults, LCD errors or input
errors/retries through67.34s. Initial1024 priming-silence samples are separate
from underruns. Final audio queue2274 samples. LCD busy/backpressure grows
(1366 by67.34s); that counter is not an error and the two-buffer pipeline is
still throughput-limited. This is not60 displayed FPS.

Durable evidence: `private-backups/nes-tiles-write-20260929/` contains write,
reset and both observation runs, final readback and session-state receipts.
Free heap226796 bytes. Helper PID20664 remains alive, idle/unblocked,
reset_pending=false, needs_observation=false. New firmware left running.

Host equivalence and serial counters do not certify physical screen/audio
quality or long-term stability. Physical visual/audio acceptance remains
with the user; no claim that a camera or LCD readback was inspected.
