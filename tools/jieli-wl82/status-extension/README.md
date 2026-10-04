# Experimental WL82 RAM status extension

A newer **cold-loaded, same-footprint variant** has now been built and bench
tested in part. See [CONFINED_LOADER.md](CONFINED_LOADER.md): its status command
responded, but full readback failed; **no flash-array write was performed**.
The text below describes the earlier, never-uploaded extra-RAM prototype.

Offline prototype only. **Not uploaded, not a firmware image, no installer.**
Bench power/recovery availability does not qualify this extension for execution.

The reviewed community WL82 helper does not implement generic FC03 status
polling. This prototype adds an experimental FC03 response after the original
CDB decode path, without removing negotiation or authentication handling.
It patches four bytes at `0x01c04318`, proposes a 212-byte routine at
`0x01c60000`, and preserves the displaced instruction for other FC subtypes.
Those addresses apply only to the exact community helper in the parent audit.
They must not be used with the SDK loader.

**Known installation obstacle:** FB06 in the running helper does not copy
payloads whose destination starts in `[0x01c02000, 0x01c21e00)`. The proposed
hook lies in this interval. That path writes the timer control register and returns
zero to the caller; subsequent ROM behavior is unqualified. The extension
address is outside this interval, but that does not prove it is unused or
safe. Do not attempt a live hook write, range-crossing write, or fallback
workaround. A separately reviewed loader construction/loading design would
be needed before execution. See `../WL82_LOADER_AUDIT.md` for addresses.

The routine sends NOR opcode 05, clocks one SR1 byte, and restores the saved
CS and SPI configuration (pending flags are acknowledged, not preserved).
Each controller wait is bounded to 100000 polls, not a calibrated time bound.
It contains no erase, program, status-write, write-enable, key or reset command.
It refuses an absent NOR handle, nonzero loader mode flag, input CS direction,
active CS, or disabled SPI controller. These guards and register semantics
still require hardware validation; they do not prove all possible bus states
are safe. On a transfer timeout the peripheral's physical state is uncertain.

Experimental 16-byte response: FC 03, result, SR1, ASCII `FMS1`, eight zero
bytes. Result 0 is a status sample; 1 is a rejected precondition; 2 is a
controller timeout. SR1 is meaningful only for result 0. A ready bit by itself
does not prove successful erase/program or correct flash contents.

## Offline verification

```powershell
C:/Python311/python.exe tools/jieli-wl82/status-extension/build.py
C:/Python311/python.exe tools/jieli-wl82/status-extension/test_audit.py
cmake -S tools/jieli-wl82/status-extension -B tmp/wl82-status-model -G "Visual Studio 17 2022" -A x64
cmake --build tmp/wl82-status-model --config Release
ctest --test-dir tmp/wl82-status-model -C Release --output-on-failure
```

The build binds the exact decoded loader and manually reviewed output bytes,
rejects unresolved symbols, and records source/artifact hashes. Changed machine
code fails closed until independently reviewed; do not just refresh its pins.
Artifact tests mutate every extension/hook byte. The host register model tests
all 256 status values, both transfer timeouts and five rejected preconditions.
Neither test executes the pi32v2 instructions or emulates the chip.

## Still required before any RAM upload

- Establish that the proposed address is unused by ROM, helper, USB buffers,
  heap, stacks and other cores throughout the transaction.
- Establish the memory-write path's instruction-cache coherency and ordering;
  `csync` alone is not evidence that freshly written code will execute.
- Resolve the excluded hook-write path using a supported, separately reviewed
  loading design; the current prototype has no demonstrated installation path.
- Review response handling, register liveness, command-transfer length and
  failure behavior, including stalled SPI transfers and existing bus modes.
- Define an explicit, separately authorized RAM-only experiment with fresh
  device identification and recovery steps. Do not invoke FC0A automatically:
  the original helper's online path can write flash status/protection bits.

The reader, original helper blob and flash images are not patched by this build.
No flash writer is enabled, and no physical completion or recovery test has
passed on the basis of this prototype.
