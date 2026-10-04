# SDK startup side-effect review

Offline review of the pinned SDK-linked FM-1 candidates, 2026-09-27. This is
not a claim that all SDK execution is read-only or hardware-qualified.

## Concrete hazard found and removed from registration

The original integrated link's `syscfg_tools_init` walks 28-byte records from
`syscfg_ops_begin` to `syscfg_ops_end` and calls each nonzero first word.
Those records were `cfg_btif`, `cfg_vm`, `cfg_bin`.

The corresponding `syscfg_vm_init` contains conditional calls to `sfc_erase`,
`sfc_write`, recovery/copy code and `vm_reset`. `vm_reset` erases VM areas and
writes a new header. `syscfg_btif_init` contains conditional calls to
`__btif_buf_erase` when configuration-item reads fail. These initializers run
before `app_main`, so an otherwise idle/headless program could still modify
stock persistent data. No claim is made that a particular branch would run on
this unit: no private VM/BTIF contents were exposed to decide that.

The NES build has no Bluetooth service, persistent configuration or save-RAM
support. `build_boot.py` now excludes only the two registration sections
`.syscfg.2.ops` and `.syscfg.1.ops` in the generated linker script, explicitly
placing them in `/DISCARD/`. The SDK checkout and libraries are unchanged.
This removes the automatic VM/BTIF initialization route, not their contents
from flash and not the SDK's integrity checks.

The resulting configuration registry is exactly one 28-byte `cfg_bin` record,
whose initializer is `syscfg_file_init`. Its write callback slots are zero.
The current disassembly still contains low-level erase/write routines and
unregistered VM/BTIF code: **not registered is not the same as absent**.
Other call paths and flash-driver setup still need review before execution.

## Startup groups retained and checked

The static audit reads actual function pointers from the ELF, not merely names:

| Group, in SDK app-task order | Retained callbacks |
|---|---|
| early | app_version_check, sys_event_init, thread_fork_init, sdfile_init |
| platform | syscfg_tools_init |
| normal | app_update_init |
| module | wait_completion_init |
| late | sdk_meky_check |

`app_update_init` calls `update_module_init`; the currently reviewed body
allocates/zeros RAM and stores the callback. This is not proof that subsequent
update callbacks are harmless. `app_version_check` walks SDK version strings;
printing is optimized out in this configuration.

`sdfile_init` loads mapping/VM metadata, opens the NOR driver, passes boot-info
to it, reads flash data and mounts the filesystem. Driver initialization and
all indirect operations are not yet qualified. `syscfg_file_init` remains a
file/configuration reader; preserving its registration also retains read-only
configuration access. The existing `sdk_meky_check` late initcall remains
intact. No authorization/signature check was removed or patched; actual
integrity-check compatibility remains unverified.

Fourteen boot-audit tests now include negative cases for a changed configuration
initializer/read callback, a nonzero write callback, and removal of the SDK integrity
initcall. They also retain boot-bridge, ROM, entry, section and ISR checks.
The headless run skips only the peripheral ISR-specific case. Passing these
tests establishes the intended registration layout, not every possible
transitive side effect or hardware behavior.

## Event-dispatch integration defect fixed

The pinned SDK's `apps/common/system/init.c:app_task_handler` runs late
initializers, calls `app_main`, **then** loops through `os_task_pend` and
`app_core_msg_handler`. The previous NES `app_main` never returned. Its sleeps
yielded processor time but did not process the app-core queue.

This matters to SDK timers: `include_lib/system/timer.h` documents delivery to
the task that registered the timer. The linked `sdk_meky_check` registers IRQ
123 on both cores and schedules `_mkey_check` with an 8,000 ms timeout through
`sys_timeout_add`; the optimized wrapper obtains `os_current_task`. The linked
timer task posts queue messages with `os_taskq_post_type`. Keeping a timer
symbol/initcall linked did not ensure its callback could actually be dispatched.

The NES now runs in its own registered task, priority 10, 4,096 stack words,
with no queue. `app_main` creates it and returns on success or failure.
`app_core` remains priority 15 with its SDK queue. Its existing event loop is
unchanged. The linked disassembly was checked to follow worker creation with
`os_task_pend` and `app_core_msg_handler`, rather than an emulator loop. This
restores the intended dispatch path, not a guarantee of physical timing.

The SDK integrity callback remains untouched. Its hardware-register and
boot-provided state dependencies mean that preserving package bytes cannot
prove success offline. Do not replace it, force its result, or treat a brief
boot as acceptance. The headless worker now has a minimum 20-second observation
interval and ongoing idle heartbeat. At a later authorized bench test, observe
continued progress beyond that interval, not merely stage 1/2. A stage-3 value
alone is still not proof of SDK integrity success or stable hardware operation.

Host tests execute the real headless application against fake task creation,
clock and emulator services: app entry must return immediately, creation
failure is retained, short runs wait, long runs do not wait an extra 20 seconds,
clock wrap works, and a stopped clock cannot finish the interval. Boot audits
also check the linked task table, integrity callback pointer and 8,000 ms delay;
negative tests reject a changed task priority, callback or delay.

## Next execution gates

Additional static review of the pre-task-split peripheral ELF (application
SHA-256 `d3d174cbe06a4af34fa60c83ce99a7233c80f107f7e6a5ca5cdbbb2c773b303a`;
addresses relocate in later links):
`norflash_open` at `0x0200717a` invokes `read_flash_id` (`0x01c00ae6`) and
`flash_write_protect_init` (`0x01c00754`). The latter enters SPI mode, exits
continuous-read mode, calls `norflash_read_sr` (`0x01c005ca`, SPI 05/35), masks
`0x41fc`, and records the value in RAM; its reviewed body contains no direct
status-write, program or erase call. It can restore continuous-read mode.
`read_flash_id` issues JEDEC 9f and UID 4b reads; this review inspected code,
not live device values. This narrows the open-path concern, but does not prove
every transitive helper/MMIO effect is harmless or the whole SDK read-only.

This SDK routine is distinct from the external USB RAM loader. The latter's
FC0A online-device path **can write status/protection bits**, as documented in
`tools/jieli-wl82/WL82_LOADER_AUDIT.md`. Do not transfer the SDK routine's
limited read-only finding to that tool.

- Finish transitive NOR helpers and remaining startup MMIO review against stock.
- Confirm SDK integrity-check behavior with the legitimate boot/package path;
  do not bypass a failed check.
- Validate a scoped write/verify/restore method and retain the original backup.
- Observe headless boot before display/audio/scanner qualification.
