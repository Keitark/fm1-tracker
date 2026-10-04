# Exact WL82 helper: offline protocol review

2026-09-27. No device accessed, loader uploaded or flash operation performed
for this review. Static analysis is not physical write/restore qualification.

## Reproduce and scope

```powershell
C:\Python311\python.exe tools/jieli-wl82/audit_wl82_loader.py
C:\Python311\python.exe tools/jieli-wl82/test_audit_wl82_loader.py -v
```

Input: `tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin`, 24,064 bytes,
SHA-256 `d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376`.
The fixed MengLi transport decoding restarts every 512 bytes, matching the
existing uploader. No device key is queried or needed for this transport
decoding. This does not establish requirements for firmware authentication.

Decoded code SHA-256:
`db8676b9cca4e2c0dfee062715cdb41528a2a8ccc827520ba8c194e4a2e0356e`.
Disassembly, manifest and analysis-only ELF are in `tmp/wl82-loader-audit`.
Addresses below use the RAM load base `0x01c02000`. Never upload the synthetic
analysis ELF. Linear sweep disassembles data too: switch tables were decoded
as unsigned halfwords, with target = table base + 2 * halfword. Paired `#`
instructions read the old register inputs, not sequential results.

## Important correction to readback safety language

The Python adapter does not call array-program or array-erase commands. However,
the loader's **FC0A online-device operation is not side-effect-free**:

- FC dispatch table `0x1c042c6`, index 1, leads to `0x1c0462c`.
  The default FF-padded CDB follows `0x1c04ae0`, opens NOR, and reads its ID.
- For an ID other than zero or `0xffffff`, `0x1c04b26..0x1c04b2e` invokes
  device ioctl 13 with argument zero.
- NOR ioctl `0x1c0505a` dispatches that to `0x1c0519e`. It reads status with
  SPI 05/35, compares mask `0x41fc` with the requested value, and can issue
  write-enable 06 followed by status-write 01/31 (with a combined-write
  fallback) if they differ. This attempts to clear the masked protection bits.
- NOR open (`0x1c03aca`) also contains conditional status writes in its
  quad-mode setup path. Not every branch is reached for the current settings.

The existing backup contains matching array bytes from two reads; it does
**not** prove status registers were unchanged before/after loader initialization.
No pre-operation status capture exists, so do not claim the historical session
changed them or did not change them. A raw array backup does not capture those
registers. Reusing the loader still calls FC0A in the current adapter.

`read_fm1_firmware.py` now warns about this before opening the device and in
`--plan`. Its transfer behavior is unchanged. A future preservation-grade
read-only path must account for loader initialization and status preservation,
not merely remove host-side erase/program calls. Do not experimentally write
status values or patch security checks to resolve this uncertainty.

## Resolved NOR command paths

Registration `0x1c07c28` points to operations table `0x1c07964`:

| Operation | Callback | Relevant behavior |
|---|---|---|
| Open | `0x1c04e3c` | calls NOR initialization, returns device handle |
| Read | `0x1c04e1a` | calls data-read implementation `0x1c04cb4` |
| Ioctl | `0x1c0505a` | status, ID and erase dispatch |
| Write | `0x1c05430` | rearranges arguments, jumps to `0x1c04e8a` |

FB01 sector erase dispatches to `0x1c04458`. It decodes a big-endian address,
calls ioctl 200 and copies its return value into response byte 2 (after FB01).
The ioctl table at `0x1c050ba` maps 200 to `0x1c050c2`: selector 2 is passed
to `0x1c04ffa`, whose command constant `0x8120d8c7` selects SPI **20**.
It waits before issuing the erase, sends write-enable/address/command, then
deasserts CS. The ioctl returns its initialized zero result without a final
erase-completion check. Therefore response zero is not proof of completion.
The upstream `flash_erase_sector()` also discards this response byte.

FB04 program dispatches to `0x1c044be`. It decodes big-endian address/length,
receives data, then calls ops+16. The command handler **ignores the write
callback result**. The program callback splits data on configured page
boundaries (256 or 1024 bytes). The path specific to ID `0x856017` is not the
historically observed FM-1 ID `0x856014`. Lower programming code at
`0x1c04e50` waits before a page, sends write-enable, the configured program
opcode (initialized to 02), address and bytes; no final post-page wait occurs
in that routine. Callback return counts are not readback verification.

Fresh entry clears the BSS including state byte `0x1c09720`. The reviewed
default path uses that zero state; F5 negotiation can change it and select
data transformations. No negotiation, key query/write, chip erase, protection
override, or authentication bypass is authorized by this review.

The busy poll at `0x1c03a88` reads SPI 05 bit 0. Its counter exit/reporting
logic and callers' ignored results must not be trusted as a host timeout or
success guarantee. Device transport failures leave the physical outcome
unknown. Do not blindly retry, reset, or switch profiles after such a failure.

### Generic READ_STATUS is not a qualified completion path

The upstream Python `JL_LoaderV2.read_status()` emits FC03. In this decoded
helper's vendor dispatcher, FC commands first go through `0x1c0425c`:
unsigned ranges 09..16 and 40..48 use tables; the remaining branches at
`0x1c04318` test 83/84/85/86/9d/a0/a1. Subtype 03 matches none and reaches
`0x1c04b60` with the initial return flag zero. It does not reach a NOR-status
read/response handler. This helper installs that dispatcher through the
boot-supplied callback structure at `0x1c028a4..0x1c028ac`.

Thus the generic Python method's existence is not evidence of a usable WL82
status command. Any ROM-side fallback behavior remains unestablished; FC03
was not sent to a live device for this review.

FD05's NOR read callback `0x1c04e1a` does call the internal busy poll when its
state+50 flag is zero, but it ignores the return value and proceeds to the
read routine. Do not substitute this for independently qualified ready/error
reporting or conclude that a returned FF page proves an erase completed.
No assumed delay based solely on an unconfirmed flash part variant is used.

## Requirements for any later writer

No writer is enabled by this audit. At minimum it must:

1. Re-identify the current device and verify a fresh full image against the
   expected original; never reuse a remembered PHYSICALDRIVE number.
2. Verify the exact candidate, original backup and loader identities; scope
   every transaction to the reviewed changed sectors, preserving all neighbors.
3. Check SCSI status, full transfer length and command echo explicitly (not
   Python `assert`); account for the loader's insufficient completion reporting.
4. Establish bounded ready polling, check erase readback, program in bounded
   chunks, and compare full sector readback. A successful USB transfer alone
   is insufficient. Complete with a full-image hash and protected-byte check.
5. Treat writing the directory sector last only as risk reduction, **not**
   an atomic update: old metadata can reference partly replaced code already.
6. Validate recovery independently. Saved original bytes and an inverse offline
   plan do not prove USB recovery or restoration after an interrupted update.

The exact-blob tests bind these findings to this input and reject changed
instructions/tables. They are not tests of a live NOR chip or a complete
formal control-flow proof.

Offline sector plans and interruption models now exist in
`firmware/nes/plan_flash_update.py`; see `firmware/nes/FLASH_PLAN.md`.
They do not implement or qualify this missing live completion path.

## Offline alternative-loader investigation and status prototype

The SDK `cpu/wl82/tools/wl82loader.bin` is a different 31232-byte blob, encoded
SHA-256 `9920e66626fc86b2db536050a4d23dec10c8d1081575553539835fd812276c27`.
`audit_sdk_wl82_loader.py` creates an analysis-only decode/disassembly in
`tmp/sdk-wl82-loader-audit`; it does not upload or replace the community helper.
The inferred 512-byte-block decode has SHA-256
`f3c25d02f9574aca0af95169095dea866fac9796b541e1ce75a44fa0bf89a1e5`.
Its reviewed FC dispatcher at `0x1c04098` also lacks subtype 03; the unmatched
path reaches `0x1c04cc4`. Its erase routine at `0x1c068d2` likewise lacks a
final completion wait. Switching to that blob does not establish safe host
completion polling. No claim is made about unreviewed ROM fallback behavior.

An experimental community-helper-only RAM extension now exists under
`status-extension/`. Its bounded SPI status-read model passes offline tests;
the exact machine code is pinned after manual disassembly review. See its
README for response semantics and unresolved memory/cache/transport gates.
It has **not** been uploaded and supplies neither an installer nor a writer.

### FB06 memory writes do not establish a live patch route

FB dispatch table entry 6 reaches `0x1c045aa`. After decoding destination and
length, `0x1c045d6..0x1c045ea` tests the **starting address** against the
half-open range `[0x1c02000, 0x1c21e00)`. A destination within that interval
branches to a store of `0x4000` at `0x10500`, then returns through `0x1c04b60`
with the initial zero flag. It does not receive or copy the payload along
this reviewed helper path. The SDK WL82 register header maps `0x10500` to
`JL_TIMER1->CON`; this is not instruction-cache invalidation. ROM handling
after the zero flag is not established. Do not describe it as either a
successful write or a proven unconditional rejection by the whole device.

The prototype's hook address `0x1c04318` falls inside this interval. Therefore
its build and instruction review do **not** establish an installation method.
No live write, cross-boundary workaround or ROM-fallback experiment was made.

For starting addresses outside that range, the reviewed path receives into
`r8` (`0x1c09600`), compares payload CRC16 at `0x1c04616..0x1c0461a`, and calls
the copy routine `0x1c0551c` at `0x1c04626`. The local path and copy routine
contain no explicit instruction-cache maintenance. This is a limited static
finding, not proof of the complete CPU/ROM/cache state or the USB callback's
implementation. Length safety is not established by this start-address test.
Do not use upstream generic memory-write defaults as verified WL82 bounds.

Known helper storage includes the decoded image `[0x1c02000, 0x1c07e00)`,
initialization-managed blocks beginning at `0x1c07e00` and `0x1c08e00`, and
BSS cleared at entry over `[0x1c09600, 0x1c09dd0)`. Absence of the proposed
`0x1c60000` address from those objects is **not** an ownership proof: ROM
stacks, heap, buffers and other-core state remain unqualified.

The SDK application linker declares RAM from `0x1c00000` toward `0x1c7fe00`
(with TLB/boot-info reservations). This establishes an application allocation
convention, not the cold-ROM downloader's live memory ownership. There is no
saved ROM dump or complete live-memory ownership map in the reviewed workspace
that closes this gap. The next loading-design review should consider the
existing cold-UBOOT whole-loader upload sequence, not hot-patching a running
helper or treating its excluded-copy interval as a free-memory guarantee.

The normal response path at `0x1c04b32` inserts the request opcode and invokes
the existing response transform at `0x1c03478`; state byte `0x1c09720` controls
that transform. The prototype retains this path. Its plaintext `FMS1` layout
must not be assumed to be the wire layout in every negotiation state.

### Cold-upload adapter hardening

The original upload sequence uses 47 encoded 512-byte FB06 payloads followed
by FB08 at `0x1c02000`, argument 1. The local adapter formerly called upstream
wrappers that discarded write-transfer residue and used `assert` for jump
echo validation. It now validates the complete original blob/profile before
its first upload command, checks every transfer residue, and checks the full
16-byte jump response and FB08 echo using runtime checks (also under Python
`-O`). Failure does not trigger a retry, jump, reset or flash operation.
Fault injection covers short transfers and exceptions at every write position,
and short/wrong-length/wrong-echo/exception jump responses.

This improves transport failure handling, not physical RAM verification.
Upstream `docs/usb-loader-v2.md` documents that some cold UBOOT memory reads
temporarily encode the addressed memory in place and then restore it. Thus
FD07 cannot be assumed side-effect-free for arbitrary ROM-runtime or MMIO
addresses. No automatic memory-read probing was added. Cold-session identity,
RAM ownership, instruction coherency and custom-loader deployment remain open.

### Flash-request preflight: command-remapping alternative

The generic upstream `set_flash_cmds()` API (FC0D) does not supply a supported
alternative status-read mechanism for this helper. FC09..16 table entry 4
points directly to `0x1c04b60`, the unmatched-command return, rather than a
flash-command configuration handler. This was checked against the exact
decoded blob, not tested on hardware. No FC0D transaction was sent.

FC46 is not a generic NOR-opcode or arbitrary-ioctl interface: its handler at
`0x1c04946` updates mode/configuration, may exit a special read mode, and calls
the hardware initialization routine. FC84 invokes the 0x4b flash-identity
read routine, not SR1. Neither was used as a speculative status probe.
These findings do not qualify a new erase/program path. The successful
elevated INQUIRY on 2026-09-27 was only one standard identity transaction;
it did not upload a helper or validate write/completion/recovery behavior.
