# Rendering generation: profiling A/B and fused RGB444 conversion

2026-09-29. Two separately flashed experiments, both nominal30MHz SPI,
RGB444, same SMB1, input scanner, audio effects, audio-priority policy and
two-buffer asynchronous LCD. No PPU/CPU emulation or audio-threshold changes.

## A: remove detailed profiling only

Build `nes-smb1-spi30-noprof` removes `--nes-profile` from the preceding
`nes-smb1-rgb444-spi30` options. Frame/time/LCD/error counters remain, but
per-section accounting and PROF records are disabled. The linked image audit
and30 linked-image negative tests pass before flashing. Existing helper used:
run `01811cfbaf334b8299555afeeaea93b6`,53 sectors, one full1MiB verification,
one reset. Known reset-log decoder exception resolved by COM10 observation.

Application212144 bytes, SHA256
`3e67c3a8b5f2b5c793b019aabc2c5b60b49592ff84286038de62a33a5f4dec36`.
Full image SHA256
`d47c5434a224ee39a4fa0212be15a7f1543b5483a6b08c0a4781dcf99ce3f444`.
Evidence/rollback: `private-backups/nes-noprof-write-20260929-2011/`.
Serial run `a250a6581b334fc5b40c8901d23cb3e8`: last10.040s interval
NES TIME4120->14160, frames203->805, LCD done78->301. Thus59.96 emulated FPS,
22.21 completed LCD FPS. Previous profiled30MHz capture was19.75 LCD FPS,
about12.5% throughput improvement when removing profiling. Captured
underrun/rebuffer/LCD-error/NES-fault counters0 through14.16s firmware time;
input errors/retries0. Initial1152 priming samples are separate from underruns.
LCD busy stays13 throughout measured interval. Separate captures, not an
identical-input benchmark; timer records have10ms granularity.

## B: fuse scaling and wire packing

New opt-in `--lcd-direct` requires RGB444 (which requires async). A paired
native-row callback consumes256-wide native-endian RGB565 rows while writing
packed RGB444 directly into the existing task-owned BUILD buffer. The old
wire callback remains supported but cannot be configured simultaneously.
Native callback never retains the core pointer or touches the active/pending
DMA buffer. Publication still occurs only after240 complete rows; IRQ behavior,
two-buffer backpressure, 250ms timeout and error/STOP handling are unchanged.

Packing module `fm1_lcd_pack.c` builds with-O2. Crop uses x+8. Fit processes
32 input pixels into30 outputs, omitting input15 and31, including the packed
pixel pair crossing that first omission. This is exactly floor(x*256/240),
with no divisions or intermediate RGB565 byte buffer. Each RGB component is
truncated to its upper4 bits, matching the prior RGB444 output.

Source rollback: `private-backups/nes-render-source-20260929-201122/`.
111 checks pass: NES/core/board27, USB/peripheral/display25, linked-image30,
broker/writer29. Native board tests cover fit/crop/partial strips and confirm
board.wire stays untouched, plus skip/errors and conflicting callback rejection.
Actual-driver tests compare six full native frames (fit/crop, sequential and
random pixels), destroy input before DMA completion, and test bad crop/row
ordering. Existing exhaustive RGB444 wire packing/ownership tests still pass.
Static audit recognizes native RGB444 marker and172800-byte aligned BSS buffers.

Build `nes-smb1-spi30-direct`: application212592 bytes, heap227308 bytes.
App SHA256 `b29b443144c657124155712b37bfef30a155a3beefeb81953286d0e953100fe0`.
Candidate SHA256 `cdb04c27c72deacc88a172fb59ad681416bde7f8c1103a5efbfe6831324f7250`.
Initial broker regression expectation still included0x38000 from the larger
profiled image; reviewed APP_START0x4120 plus212592 bytes ends at0x37f90, so
updated the exact expected extent to exclude0x38000. Safety scope unchanged.

Hardware results follow below. Serial counters cannot establish physical color,
screen integrity or audible quality, and short runs are not long-term tests.

Initial B attempt `e97f583a71704e8098e3eb8ab91167a1` failed in the existing
jluboottool loader.mem_write while uploading the RAM helper: WinError31.
No deployment/sector writer started, no erase or flash write occurred and no
reset was sent. Same WL82 UBOOT PNP identity remains. After inspecting helper.log,
one explicit same-candidate `-Flash -Retry` was requested, without changing
the writer/environment or clearing its safety latch manually.

## B verified deployment and measured outcome

Retry `60564a3176e747a3b4403113a2e15856` programmed/verified all50 requested
sectors, directory last. Single full1MiB readback matches cdb04c27... above.
Reset sent once and resolved by COM10 HELLO/advancing frames after the known
log-decoding exception. Helper PID20664 remains running, idle/unblocked with
reset_pending=false and needs_observation=false. No new UAC/environment/writer.

Durable evidence: `private-backups/nes-direct-write-20260929-2019/`, including
initial helper-upload failure, successful retry/full image, reset and capture
`runs/02a9179be9994c4188204c544b2d1d84/serial.log`.
Last10.070s: NES TIME4140->14210, frames204->808, LCD done89->354.
Result:59.98 emulated FPS,26.32 completed LCD FPS. Relative to A22.21 FPS,
direct conversion improves throughput18.5%; versus original profiled30MHz
19.75 FPS, the two changes together improve33.3% in these separate captures.
No recorded underruns/rebuffers/LCD errors/NES faults through14.21s; input
errors/retries0, LCD busy remains13. Final audio queue2008 samples, initial
1024 priming samples reported separately. No PROF accounting is linked.

The new image is left running for physical testing. This completes the first
two optimizations; tile-renderer changes, audio-threshold redesign and core
offload have not been implemented.30 displayed FPS remains a target, not an
achieved or guaranteed result. Visual/crop/color equivalence is host-tested;
the physical display/audio still needs user observation.

Subsequent tile-renderer implementation and measured32.5-32.6 LCD FPS are
recorded in `NES_TILE_RENDERER.md`; the paragraph above describes the earlier
direct-packing milestone, before that optimization.
