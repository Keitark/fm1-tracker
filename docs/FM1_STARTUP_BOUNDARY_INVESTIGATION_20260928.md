# Startup investigation after the accepted polled-pixel control

## Scope

The user redirected the next experiment to startup investigation. The parameter
transfer experiment is paused: `boot/stock_polled_params.S` and its temporary
object were assembled offline before that instruction, but were never packaged
or flashed. No firmware implementation or device operation followed the
redirection. The installed image remains the user-accepted stock pixel-polling
control. This report and the evidence tests do not establish a root cause.

Compared evidence:

- Accepted stock control, saved full readback:
  `private-backups/stock-polled-write-20260928-070755/final-1.bin`, SHA-256
  `444b5d8cea8a3ffa13d72e331e148a34e9f5af902965eb38f80d428dbad2b792`.
- Failed pre-OS NES, saved full readback:
  `private-backups/pre-os-write-20260928-025138/final-1.bin`, SHA-256
  `8da9ba09455e10f9cd04666df10a32d7925aaf21efee9f6640631f39dba0d5ca`.

These are local historical readbacks, not new reads of the connected unit.
Both decode to the expected application hash and pass application payload CRC.
All addresses below are CPU addresses, not physical NOR offsets.

## Boundary-by-boundary result

| Boundary | Working stock V14 control | Failed pre-OS NES | Conclusion |
|---|---|---|---|
| Boot region | Saved raw bytes 0000..4000 | Byte-identical | No replacement of this boot region; not proof the bootloader accepts/runs the entire new app |
| App entry | 02000120 -> 02000124 | 02000120 -> 0200018A | Both enter XIP; the whole app is not loaded into RAM |
| Initial SP / SSP | 01C15D14 / 01C16D14 | 01C18658 / 01C19658 | Different linked allocations, not a demonstrated mismatch |
| Boot info | 020001E6, destination 01C7FD50 | bridge 02002AC2 -> SDK 02000292, destination 01C7FD4C | Earliest substantial implementation difference; not hardware-qualified |
| BSS | 01C09E9C + 184C0 | 01C02870 + 181C0 | Both start exactly after initialized RAM; both end below boot-info/IRQ handoff storage |
| RAM copy | 02084F60 -> 01C00000, 9E9C bytes | 02029C20 -> 01C00000, 2870 bytes | Executed operands match saved payload geometry |
| Cache setup | 01C00E8C, 272 bytes | 01C00CC0, 272 bytes | Exact equality after three pointer/call relocations only |
| Memory hook | Original memory hook | Breadcrumb wrapper then original hook | Earlier review found both original hooks are returns; no missing heap setup proved |
| Update result | Stock CRC helper | SDK CRC16 then result/clear | Different CRC dispatch implementation, before main; no captured hang here |
| Main pre-display | Watchdog/clocks/IRQs/debug/CRC mutex/latch, then OS/app/UI | Watchdog/clocks/IRQs/debug/CRC mutex/latch, then power/LCD hook | Moving the hook did not remove all SDK library dependencies |

BSS includes the initial stack allocation in both images. Boot-info calls have
returned before the assembly clear/copy loops, which do not push another call
frame until clearing/copying completes. This overlap alone is not proof of
stack corruption. Hardware RAM writes/reads are still unobserved.

## Early state and waits checked more closely

The failed image's initialized-RAM payload explicitly contains zero at:

- P33 software lock `01C025EC`;
- Bluetooth lock and counters `01C025E0`, `01C025E4`;
- CPU lock counters `01C0280C`;
- IRQ nesting counters `01C02814`;
- scheduler-running state `01C0282C`.

Those are file bytes copied by startup, not live lock measurements. A pre-set
software lock in the supplied payload is not supported by this evidence.

P33 still has unbounded hardware-dependent waits: testset/retry at `01C00E92`
and the busy-bit loop at `01C00EAA`. Watchdog setup already calls P33 before
clock initialization, and main later performs reset-reason/latch operations.
These waits precede the display hook. Finding them is not proof of a hang;
do not remove the locks or invent a voltage/reset change.

The boot-info bridge preserves the common 24-byte input prefix and zeros SDK
extensions, but the SDK initializer also writes the halfword-derived value to
`01C7FF0C` at `020002DC..020002E6`. The stock initializer lacks this side
effect. It remains a compatibility difference to validate, not an established
fault and not a chip-key operation. RAM relocation alone is not grounds to
copy stock addresses into SDK state.

## Important qualification of the previous pre-OS experiment

Before the display hook, NES main calls:

`02001828 -> __crc16_mutex_init 02000B06 -> os_mutex_create 02027D84`
`-> xQueueGenericCreateStatic 020270AC`.

The create path includes `lockset` at `020270F6` and `lockclr` at `0202713E`.
It initializes a static queue/mutex, then the initial send path uses queue/list
and critical-section code. With the initialized empty queue, the reviewed
success path does not require a running tick/scheduler. Nonetheless, the
checkpoint is **before os_init/os_start, not before all OS-library code**.

Stock010's corresponding mutex constructor `0205A28C` also creates a static
queue/mutex before OS initialization. Therefore the mere presence of this
call is not a newly proven missing step or evidence that it must fail.
Both the static state and hardware lock behavior matter. Do not remove CRC
initialization or integrity checks to make a marker appear.

The NES checkpoint's verified direct chain remains:

`main 0200171C -> latch 02000B16 -> wrapper 02001686`
`-> power gateway 020013F0 -> display 020015D0 -> original os_init`.

CPU1 launch and scheduler dispatch are later. If the first picture is never
attempted, moving/patching that later CPU1-ready wait again cannot localize the
earlier stop. The visible blank result alone does not prove non-arrival.

## What is established and what is next

The stock test proves polled white pixels can reach the panel under working
stock prerequisites. It does not prove the replacement reaches main, completes
power initialization, or has the same hardware state at its first SPI write.

Priority for further startup investigation is the first unobserved boundary:
boot-info/initialized RAM -> main entry -> P33/clock/debug/mutex/latch -> power
return. Any proposed diagnostic should expose an execution boundary while
preserving the original checks, rather than adjust more LCD parameters. No
such firmware modification is implemented by this report.

There is no captured normal-boot PC or live register trace. Manually entering
UBOOT and uploading its helper would overwrite/reset relevant state; existing
RAM breadcrumbs cannot honestly be assigned to the failed normal boot then.
The debugger connection/pin mapping is not established, and USB serial was
canceled. Do not present offline opcode equality as a successful hardware trace.

## Continuation: ordered power operations and the pre-main CRC branch

Further offline investigation, at the user's request. No firmware source,
candidate binary or device state was changed. The comparison uses the failed
pre-OS application's pinned bytes, the accepted stock V14 readback above, and
the previously known-working stock010 payload. This does not switch the
diagnosis to a version-difference hypothesis.

### Power comparison result

The earlier parameter checks have been extended to **36 ordered
constant-argument P33 operations** on the reviewed LRC/config=0 path. All 36
have matching argument-load bytes and resolve to the corresponding helpers in
both working stocks and NES. Stock010's reviewed power block begins at
`020047E0`; V14's begins at `02004A40`; the selected sites move by `260` hex.
NES's linked `power_init` begins at `02000FB6`.

The comparison covers PMU setup, LRC controls, wake/latch-related operations,
low-voltage detector settings, the three DC/DC transitions, voltage fields,
and both low-power-timer control sequences. It is a **selected-path static
comparison**, not an exhaustive list of all hardware accesses: variable timing
values, helper-internal writes and hardware-dependent branches are outside
that 36-operation count.

Additional findings:

- All three intervening DC/DC delay loops are identical 12-byte sequences:
  counter -1000, NOP, increment, branch until zero. They are finite CPU loops,
  not waits for scheduler ticks. This does not measure their physical duration.
- P33 byte transfer (`stock010 01C00FC8`, NES `01C00E9C`) is byte-identical,
  including the busy-bit polling loop. P33 read, write and field-set bodies
  match after normalizing only calls to checked corresponding helper targets.
  Their software-lock implementations are not included in that equality claim.
- Both implementations OR `40` hex into **`80AB`**, named `R3_RST_CON` in the
  local pinned WL82 header. A transient suspicion of `80AA` was a misreading of
  a nearest-symbol annotation and is ruled out by the operand bytes
  `AB 80 00 00`. Do not patch this address.
- NES adds a read of `80AB` before that OR, at `02001140`; its result is
  discarded in this linked build. The archive IR shows the source operation
  calling `rtc_recode_pwr_flag` with bit 7, but that bookkeeping has no retained
  call here. Stock010/V14 do not have this preceding read at this site.
- Stock performs three extra reads of P33 registers 6, 5 and 5 after the voltage
  setters (`02004A90..02004AA6`, V14 +`260`). Their results are overwritten,
  and there are no intervening extra writes. NES omits these. This is a real
  transaction-count/timing difference, **not proof that these reads are required
  for LCD startup** or harmless on all hardware.
- For zero config, the allocation branch at NES `02001304` and timer branch at
  `0200137A` are skipped. The failure/assert loop `020013EE` belongs to the
  skipped allocation-failure path. The final SDRAM-status branch either skips
  or calculates a clock-derived number; it does not wait for SDRAM readiness.
- `__hw_lrc_start` (`02000CDC..02000D0A`) configures MMIO and returns;
  `__tcnt_us` (`02000D0C..02000D48`) calculates a rounded tick count. Neither
  is a timer polling loop. P33 transport/lock waits remain real possibilities.

These results argue against another guessed rail-value change. They do not
prove identical power timing or that the failed firmware reaches/returns from
`power_init`.

### The early CRC call is not automatically an uninitialized-mutex deadlock

`update_result_get` is before main and therefore before `__crc16_mutex_init`.
That ordering initially looks suspicious, but the actual CRC function chooses
between two paths:

1. `02001D98..02001DAC` tests interrupt context and `cpu_irq_disabled`.
2. In interrupt context or with IRQs disabled, `02001DAE..02001DE4` computes
   the CRC in software. For this call the length is 78 bytes, loaded at
   `02001E20`; the byte loop is finite and does not pend a mutex.
3. Only the other branch reaches `os_mutex_pend` at `02001DEE`.

The bootloader issues CLI at `01C051C2`. On the reviewed direct-XIP handoff
with entry `02000120`, bit `04000000` is clear; branch `01C051F2` goes directly
to `01C05238`, bypassing `01C051F6..01C05234`. Thus the call at `01C05230`
must not be counted as an unconditional part of this handoff. The remaining
tail copies arguments/metadata and calls the entry at `01C0528C` without STI.
The reviewed application's pre-CRC bridge, RAM/cache setup and memory-marker
path do not deliberately enable IRQs. This supports the software-CRC path on
ordinary entry, rather than a claim that the uninitialized mutex must hang.
It is static reasoning about the reviewed entry path, not a captured ICFG.

### Remaining stopping points, in execution order

| Boundary in failed pre-OS image | What can still prevent reaching the display |
|---|---|
| Boot acceptance/handoff and application RAM setup | No live entry PC or RAM-copy verification; saved flash CRC is not execution evidence |
| Cache ready, `01C00CDE..01C00CE4` | Unbounded wait for bit `4000` at MMIO `01EEE008`; same routine as stock |
| Watchdog, reset reason, clock/P33 setup | Software testset wait `01C00E92`; P33 busy wait `01C00EAA`; physical completion unobserved |
| Debug/CRC mutex/latch initialization | Hardware critical-section lock and exception handling still execute before the checkpoint |
| Power initializer | Same reviewed constant writes, but repeated P33 accesses and inherited hardware state remain unmeasured |
| LCD setup and transfer | First-picture code can return a transfer error; a blank screen alone cannot identify which earlier boundary completed |

The next discriminating observation should be **normal-boot execution or the
LCD bus**, not another speculative voltage/reset edit. For example, observing
whether the failed image sends its first `11h` sleep-out command would separate
successful arrival at SPI output from an earlier failure. That requires a
physical bus capture with verified probe points, or a verified live debugger;
neither is established here. A lack of bus activity alone also cannot distinguish
early code failure from wrong peripheral routing. No serial feature is being
reintroduced, and reading RAM after manually entering UBOOT is not a substitute.

There is still **no proven root cause and no new flash-ready fix**.

### Additional reproduction

`C:\Python311\python.exe tools/jieli-wl82/test_power_sequence_evidence.py`
and the same command with `-O`: five tests pass each. Together with the six
startup-boundary tests, eleven offline evidence tests pass in each mode.
The new tests pin the exact evidence hashes and compare the 36 selected
operations, RTC address, three delay loops, discarded readbacks and selected
transport-helper bodies. They are not hardware acceptance tests.

## Reproduction

`C:\Python311\python.exe tools/jieli-wl82/test_startup_boundary_evidence.py`
and the same command with `-O`: six tests pass each. They pin both saved images,
application CRC/hashes, boot-region equality, executed RAM/stack operands,
cache routine parity, initial software lock state, pre-display mutex call
chain, P33 wait opcodes and the extra boot-info write. These are static evidence
regressions, not a CPU emulator or a hardware boot test.
