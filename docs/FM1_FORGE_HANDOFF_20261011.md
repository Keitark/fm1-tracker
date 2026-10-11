# FM1 Forge implementation and first-transfer handoff

Date: 2026-10-11. Source implementation and offline acceptance are complete;
this diagnostic candidate has not been installed or physically accepted.

## Firmware and user module

The asset-free factory diagnostic exposes all 41 mapped buttons/keys, seven
digital encoders and PB6 master volume through HAL ABI 1. Immutable snapshots
include live/seen inputs, encoder edge/invalid counts, ADC range/gain and error
evidence. The LCD renders diagnostics and the USB/system -> HAL -> user module
graph; piano keys produce original bounded quiet tones. Startup is silent.

Fixed runtime owns CDC/UBOOT recovery, optional UAC1, coherent input scanning,
ADC, copied asynchronous LCD strips and final audio gain. User callbacks are
documented in [the starter guide](../firmware/forge/README.md). The generator
refuses existing/unsafe project paths; the builder consumes the actual module,
USB, clock and LCD configuration. Native C modules remain trusted code.

The packed RGB444 path uses `fm1_display_async_wire_rows` with an exact length
and owned copy. Legacy RGB565 callers retain their conversion API. Actual-driver
tests place exact 2880/3840-byte strips against inaccessible memory and reuse
them immediately. The corrected path passes; restoring the prior converter in
an isolated copy reproduces the overread failure.

Default clocks are preserved. Experimental `sdk-320` invokes the genuine SDK
`system_clock_set` once before USB/DMA, skips a redundant setter when already at
320 MHz and checks CPU/bus readback. It is not a claimed overclock: boot clocks
may be higher. Its 53 MHz LSB is incompatible with fast LCD presets requiring
60 MHz, so the schema, generator, builder and panel reject that combination.
SPI15/SPI30 RGB444 and all nondefault timing need device measurements.

## Exact offline candidate

| Artifact | Verified value |
|---|---|
| Catalog ID / profile | `factory-diag` / `diagnostics` |
| Live firmware identity | `FM1-FORGE/1` |
| Source profile | `forge-diag` |
| Default application | 148,144 bytes |
| Application SHA256 | `24d789a19e2961d4276a9a9fe976f77dcd161e1559c20fdab8d6333e2e79ebf1` |
| Full candidate | 1,048,576 bytes |
| Candidate SHA256 | `e52573f32d27a68e016d7aa36ac65ff254a6a8bada9c5f0fd70c4365f4f60a8c` |
| Exact verified rollback baseline SHA256 | `4d4da3ab34b643034ea91ddb670fd3556b737a05835ed77fced687eae6ad345c` |
| Changed sectors | 53, `0x4000` through `0x38000` inclusive |
| Directory sector | `0x4000`, written last |
| Protected boot/config/reserved bytes | Identical to verified baseline |

Private image, stock container, catalog bundle and receipts remain ignored local
artifacts. The full candidate is under
`private-backups/usb-diag-candidates/<candidate-sha256>/fm1-usb-diag.UNQUALIFIED.bin`.
The exported bundle is `tmp/remote-debug/catalog/factory-diag.json`; its exact
offline sector review is `tmp/fm1-forge-transfer-review-20261011.json`.
The pure protected-worker candidate validator independently checked that review
without opening a device. An authenticated loopback catalog installation copied
this same bundle into the existing private bridge catalog. `ready` means package
compatibility, not an installed or physically accepted diagnostic firmware.

## Validation

- Four target variants linked and passed full static boot/power/RAM/USB/audio/LCD
  audits: default CDC/UAC, CDC-only, SDK 320 + stock LCD, and a freshly generated
  ABI 1 user module + default clocks + SPI30 RGB444. Their source closures are fresh.
- 33 USB/legacy native groups and nine Forge/runtime/actual LCD driver groups pass.
- 57 producer/protected-worker tests, five project tests and the actual UAC
  descriptor check pass.
- Eleven negative image/audit tests pass for each target variant; CDC-only skips
  the UAC corruption case as expected.
- The immutable runtime/baseline suite ran 51 tests with five platform/elevation
  skips. The primary remote backend suite passed 35 tests.

## Hosted plugin and local service

The [FM1 Forge integration](https://github.com/Keitark/fm1-marketplace-plugin)
published private plugin version 3.0.1 in place and the Site's native MCP App
resource `ui://fm1/forge-panel-v2.html`. The panel defaults to opaque plain black,
provides the module graph and configuration controls, and sends reviewed project
requests through the host's supported message action. An exact successful
diagnostic write/full-readback/startup proof gates project onboarding.

The existing bridge and relay were reconnected with tokens/history preserved.
Both relay write permissions remain disabled. The old protected session retains
the exact verified rollback metadata, but its helper process is dead. Its
`stopped:false` status is only a missing marker and does not prove availability.
Windows elevation for the new immutable helper was canceled; no new helper or
device operation was performed. The current native Forge panel was inspected
in Codex: Forge branding/onboarding, exact diagnostics catalog and COM10 are
present, with HTML/body backgrounds verified as opaque black. The hosted service
title is now FM1 Forge as well. Physical hardware acceptance remains pending.

## Remaining physical work

Start the new immutable helper only after the user agrees to retry the canceled
Windows elevation. Attach it to the bridge and stage a review for the exact
candidate above; the user confirms that transfer in the plugin panel. Serial
entry uses the existing STOP/UBOOT handshake, and direct UBOOT uses the existing
protected application writer. CDC `COMMIT` stays blocked. UAC is audio transport.
Official `.fwsc`/SysEx updates remain the separately pinned vendor updater handoff;
this custom candidate is not an official `.fwsc` package.

After write/full readback and one reset, require the exact Forge identity,
advancing input/audio/LCD counters and zero retained faults. Then inspect the
LCD, every physical control, encoder direction, master-volume sweep, audible
tones and actual UAC playback/capture. Nondefault clock/LCD presets require their
own timing/stability acceptance. Compilation, readback and telemetry alone do
not establish those physical results.
