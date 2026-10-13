# Forge graphical configuration: user HAL access, 2026-10-12

Related: Keitark/fm1-tracker issue12; native plugin/site work is tracked in
Keitark/fm1-marketplace-plugin issue8. This receipt covers firmware configuration,
runtime behavior and offline candidates. It does not establish device acceptance.

## Contract and runtime

Project schemaVersion2 adds exactly five boolean `hal` fields: `buttons`,
`encoders`, `volume`, `lcd`, `audio`. New projects default all to true; strict
legacy version1 input normalizes to version2 with all enabled. Existing module
ABI1, factory_schema1 and `FM1_FORGE_FACTORY_DIAG_V1` transfer identity are retained.
USB CDC and UBOOT recovery remain required and enabled by default. UAC is optional
and independent of user audio access.

Selections govern user-module access while fixed diagnostic/recovery drivers
continue running. A single sanitizer filters update, render-frame and public
snapshot input. Disabled buttons expose zero pressed/seen bits and last-key255;
disabled encoders expose zero counts/errors/seen; disabled volume exposes neutral,
invalid telemetry while the real PB6 ADC still owns final speaker gain. Disabled
LCD skips user render and uses the fixed dashboard with sanitized input. Disabled
audio skips user audio and supplies silence before optional fixed UAC playback
mixing and master gain. Diagnostics retain full inputs, screen and tones even if
all user access flags are false. This is a trusted native-C API contract rather
than a sandbox or driver power-gating mechanism.

The project builder translates every selection to ordered `--forge-hal-* 0|1`
arguments and the real builder emits `FM1_FORGE_HAL_*` defines. Manifests record
`forge_hal_access`; the ELF retains its five-byte selection table. Static audit
and protected package preparation compare exact table bytes with the manifest.
Legacy factory manifests remain accepted; flash sector scope is unchanged.
Declared project schema2 requires the complete five-boolean HAL object before
ELF auditing; missing, null, partial, extra and wrongly typed selections cannot
fall back to the legacy path. Direct image construction applies the same version
guard. Unknown/null/boolean project versions and explicit null HAL metadata reject.
Only a genuine version1/absent-version legacy manifest may omit the HAL object.

## Verification

- Python project/configuration tests: **8 passed**; strict version2, legacy
  normalization without caller mutation, invalid fields/types, each HAL flag,
  safe starter creation, clock/LCD compatibility and UAC independence.
- CMake Release/CTest: **16 passed**; actual `target.c` profiles cover default,
  SDK320, user, CDC-only, each HAL disabled, all user HALs disabled and diagnostics
  with all access flags disabled, plus input/tone/renderer and four LCD wire modes.
  Tests check callback suppression and filtering, scanner recovery, continuing
  physical master gain/UAC, clocks, DTR behavior and complete STOP teardown.
- Exact candidate ELF positive/negative audits: **12 passed per candidate**,
  including every mismatched HAL selection and corrupted selection-table bytes,
  USB descriptors/packet policy, IRQ returns, clocks/startup, LCD wire ownership,
  master gain and confirmed UBOOT teardown.
- Factory protected-profile regressions: **6 passed**, including version2 metadata
  rejection before the builder/audit and legacy acceptance with exact forwarding.
- All registered source/archive/overlay digests were rechecked after both builds:
  **106 / 107 source inputs and 10 archives per candidate remain fresh**.
- Offline protected package preparation accepted both candidates against the
  retained stock-image fixture and preserved its protected ranges. This fixture
  is not asserted to be the connected unit's current verified NOR baseline.
- `git diff --check` passed. No device was opened, reset, read, erased or flashed.

## Linked candidates

SDK pin: `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`; PI32v2 toolchain from
`C:/JL/pi32/bin`. Both candidates use `sdk-default`, `stock-dma`, CDC+UAC and
UBOOT recovery. Both report static audit passed, heap244620B, hardware boot false
and flashable false.

| Candidate | Local output | Application bytes | Application SHA-256 |
|---|---|---:|---|
| Complete diagnostics, all user access enabled | `firmware/usb-diag/build/forge-graph-default-guard-20261012` | 148176 | `a90f25a96b85da542d62fab653e1a940257d90eb142de2c07347925a878cd7f5` |
| User starter, all user access disabled | `firmware/usb-diag/build/forge-graph-user-no-hal-guard-20261012` | 148304 | `d07bfc36104cd18e9ede9e67083f932d81538f302fea958d9f0affcf9af85999` |

ELF SHA-256 respectively:
`c0726a38b9802242beb88296817af28d549d4ae7379f2ba456b5bdadf0eab3ac` and
`d605e079478be57d41cc33e246e74d12b9b7d44a7858a6aed03aa064288cf31b`.
Offline stock-fixture package SHA-256 respectively:
`07af4c97907016b23e4a2e492a9e37a81c47f6537fbf9635861295868249ec54` and
`d469991cbfe1d37b9f15a6ea8ba07f92aa99a9b66bb12c85944e995c97882e7f`.

Build/log/manifest/static-audit/symbol/disassembly artifacts are local ignored
outputs. The user candidate project is
`tmp/forge-graph-projects/hal-disabled-20261012`, generated from the tracked
starter with `module=user` and all `hal` fields false. It has no external assets.

## Remaining bench acceptance

The native graph UI's drag/drop/save behavior is validated separately by the
integration work. Actual transfer requires the exact current verified unit
baseline and reviewed protected writer plan. Device readback/startup, physical
LCD/input/encoder/volume behavior, audible output, UAC playback/capture and timing
remain bench checks. These candidate links establish no hardware acceptance.
