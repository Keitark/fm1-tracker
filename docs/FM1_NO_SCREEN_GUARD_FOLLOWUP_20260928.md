# No screen after CPU1 guard: offline follow-up, 2026-09-28

## Outcome and scope

The CPU1 SYS_DIV guard image also failed the user's screen test. There is
still **no proven root cause or proof that the LCD function executes**.
The guard is not sufficient; this does not establish whether CPU1 starts.
No firmware implementation, build, helper upload, device read/write, or reset
was performed in this investigation. USB serial was canceled by the user.
Only evidence tests and diagnostic documentation were added/updated.

Artifacts examined:

- Stock010 application SHA-256
  `fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543`.
- Last-flashed CPU1-guard application SHA-256
  `63476ce1f89cd6b83fe4a915396a07668bdfca49696d3917e91aff523508a6ea`.
- Saved complete readbacks in
  `private-backups/cpu1-guard-write-20260928-021650/final-{1,2}.bin`, SHA-256
  `51dc35fdf0bcbe4e105c4f84e5c4ee52e827f4a88169c81b65541488d5998aa1`.

Saved readbacks are evidence of the previous write, not a fresh read of the
currently connected device. Addresses below are CPU addresses, not NOR offsets.

## First-picture dependency closure

The linked image, not just the intended C source, puts the first picture here:

`boot -> RAM/cache/main -> CPU1/OS -> app task -> timers -> early registry
-> platform registry -> power_init -> LCD -> remaining init -> NES worker`.

| Before-picture dependency | Linked evidence |
|---|---|
| Four early callbacks | Table `02029B7C..02029B8C`: app_version_check, sys_event_init, thread_fork_init, sdfile_init |
| Filesystem -> NOR open | `02009720` calls `02009434` |
| NOR ID/UID read | `0200948C` calls RAM `01C00AEA` |
| Additional status-register snapshot | `020094A2` calls `01C00758` |
| Platform callback | Table `02029B90` -> syscfg_tools_init `02006462` |
| Configuration backend | `02029B98` -> syscfg_file_init `02006C08` |
| Board power | App task `02001B72` calls `0200162C` |
| First display attempt | App task `02001B7C` calls `0200180C` |

Thus "screen-first" means before the remaining initialization and emulator,
**not before the scheduler, filesystem or board power**. A no-screen outcome
still conflates all of those paths with LCD/power/reset failure.

`app_version_check` walks version strings; its name alone is not evidence of
a version rejection. Configuration-backend file-open failures have returning
error paths, and its dispatcher ignores the return value; there is no newly
identified unconditional halt at that dispatcher. Nested filesystem operations
can still wait or fault.

## NOR/SFC investigation

`enter_spi_code` (`01C00330`) takes a mutex and can call
`cpu_enter_sfc_critical` (`01C01D8A`). That routine contains unbounded waits:
previous CPU-wait flag at `01C01DBE`, cache/status bit at `01C01DE2`, and
other-core acknowledgement at `01C01DEC`. SFC busy also has an unbounded wait
at `01C00376`; restoration waits at `01C0043A`.

These are possible stops before any LCD traffic, **not captured hang PCs**.
Stock010 has the same broad cross-core handshake (`01C024BC`) and SFC switch
(`01C00000`). The stock single-core scheduler plus CPU1 worker and the NES SMP
scheduler remain different runtime environments. Merely finding a wait is
not evidence that it fails, and removing it would not be a justified fix.

The low-level ID/UID sequence is shared: command 9F/three ID bytes, command
4B/four dummy bytes/sixteen UID bytes, then restoration of continuous mode
when needed. No such commands were executed by this offline investigation.

NES adds `flash_write_protect_init` after NOR open. Its reviewed body calls
enter-SPI, exit-continuous, read-status, optionally enter-continuous, exit-SPI;
it stores masked status in RAM. Status commands are 05 and 35. Despite its
name, **this init routine does not call status programming or write-enable**.
It introduces another flash-mode/handshake transition, not evidence of a
flash rewrite. Do not remove it on the basis of its name.

A misleading disassembly annotation was also ruled out: NES `01C00392`
prints operand decimal 4137 but nearest-symbol text ends in `1028`.
The actual instruction bytes `41 e0 29 10` equal stock `01C0006A` and load
**0x1029**, not 0x1028. There is no missing SPI-enable bit there.

## Other checks and remaining limits

- The saved complete images still decode to the exact linked application;
  existing container checks validate directory/file/area CRCs, offsets,
  preserved stock boot/config/reserved bytes and unused application tail.
  The VM directory entry remains present; a missing VM entry is not supported
  by this image. This does not prove runtime flash reads work.
- Stock LCD setup uses PC7 CS, PC8 D/C, PC9/10 SPI1, mux bit 4, baud 4 and
  control 4021, followed by the recovered table, frame fill and display-on.
  The current first picture follows that reviewed command sequence but uses
  polled bytes rather than stock parameter/pixel DMA and finite loop delays.
  No new proven missing LCD command was found.
- No dedicated backlight/reset pin has been established. PA2 low elsewhere
  in stock remains unexplained, not permission to drive a guessed panel pin.
- The __tcnt_us helper is arithmetic tick conversion, not itself a polling
  wait. P33 transaction/lock waits are separate and remain runtime unknowns.
- Existing RAM breadcrumbs cannot be interpreted after reset/UBOOT/helper
  upload as reliable evidence of the failed normal boot.

## Next discriminating bench experiment (proposal only)

Preserve the working stock startup, scheduler, filesystem, power and panel
setup. Change only a reviewed initial screen-fill payload, using the same
stock-derived application container and existing writer. This would test
whether a minimally changed stock application survives repackaging and can
produce an identifiable display marker, without introducing a replacement
startup stack. Stock010's first black fill is `02021600..02021718`; its
zero-pixel buffer is prepared separately from its coordinates/loop counters.

Before implementing that experiment, finish the patch-site/encoding review,
ensure a distinguishable marker remains visible long enough to observe,
verify all other executable bytes remain unchanged, and validate the complete
candidate and rollback. No marker patch or image was created in this turn.
Do not patch checks, guess GPIOs or reuse addresses against V14 without
independently locating its corresponding routine. A successful marker would
validate only that narrow stock path; it would not qualify the NES firmware.

## Reproduction

`C:\Python311\python.exe tools/jieli-wl82/test_no_screen_guard_evidence.py`

Six offline checks cover saved full-image/container validity, registry and call
ordering, NOR call sites, the SPI operand, status-read call targets and wait
opcodes. They pin the exact binaries and are evidence regression tests,
**not hardware execution tests**. Also run with `-O`.
