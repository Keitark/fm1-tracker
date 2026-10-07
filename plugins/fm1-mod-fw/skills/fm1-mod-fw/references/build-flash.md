# Build and deploy

## Repository evidence

- Board and protected writer: `fm1/firmware/nes`, `fm1/firmware/usb-diag`, `fm1/tools/jieli-wl82`.
- Doom candidate: `fm1-doom/tools/build_target_candidate.py`, `tools/prepare_flash_request.py`, `build/target-candidate/build-manifest.json`.
- Boot mapping: `fm1/docs/FM1_010_VS_T1_BOOT_TRACE.md` and `FM1_DISASSEMBLY_AUDIT.md`.
- Known stable NES reference: `fm1/docs/FM1_LCD_HANDOFF_FIX_20260929.md`. Recheck artifact/source hashes before using its old build as a baseline.

The pinned WL82 SDK used during Doom bring-up was `references/source/fw-AC79_AIoT_SDK`, commit `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`; toolchain was `C:/JL/pi32/bin`. Verify these against current build metadata. SDK vendor edits are generated overlays so the upstream checkout can remain unchanged.

## Application identity and packaging

`app.bin` byte zero maps to CPU `0x02000120`, backed by physical flash `0x4120`. The application directory/header begins at flash `0x4000`. Therefore:

`CPU = flash offset - 0x4000 + 0x02000000`

Do not flash bare `app.bin` at an invented sector offset, including directly at `0x4120`: the payload requires stock SFC encoding. The existing package/writer handles that encoding, application headers, lengths, CRCs and directory entries. The writer already has an application update path; inspect its CLI and request schema before adding another full-image service. A package represents a complete 1 MiB image for validation while the writer programs only changed application sectors, directory sector last. The adapter needs the matching validated application, ELF and manifest; an arbitrary standalone binary is not automatically accepted.

Check source closure and object freshness, ELF entry and sections, physical application/reserved ranges, private bank hashes, static RAM/linker heap and emitted stack gates before preparing a request. Preserve the current accepted artifact separately before replacing build output when a rollback is useful; an explicit user choice to skip backup takes precedence.

## Existing protected session workflow

Use `tools/jieli-wl82/flash-session-client.ps1 -SessionRoot <actual-session> -Operation status` to inspect an existing session. Its baseline must be a verified readback of this device, not a similarly named file. Get plan/flash arguments from current tool help and generated request. Typical Doom request preparation:

```powershell
python F:/dev/fm1-doom/tools/prepare_flash_request.py --fm1-root F:/dev/fm1 --build F:/dev/fm1-doom/build/target-candidate --session '<actual-session>'
```

Use explicit script/build paths belonging to the chosen application. The board's separate `fm1/tools/prepare_flash_request.py` expects `--build --session --out`; do not run the Doom example from another checkout with a relative script name.

Use the generated request path for `plan`, inspect the exact changed-sector plan, then issue the authorized `flash`. Reuse an idle verified session when suitable. Do not edit protected state to clear a failed write/reset/observation latch. Supported `retry_flash` is limited to a completed failed flash with exactly the same candidate, baseline, sector list and recorded PnP identity; pending termination, reset failures and changed candidates are rejected. A new session seeded with an old image does not establish current NOR contents after a partial write: resolve the actual state through the supported recovery first. Never turn an ambiguous failure into a blind erase/write/reset retry.

### Recovery after a connection identity changes

For a completed failed flash whose USB port change produces a different PnP identity, use explicit `recover_flash` with the original generated request. Ordinary `retry_flash` still requires the recorded PnP identity. If the old protected writer snapshot lacks `recover_flash`, migrate its failure evidence into a current snapshot:

```powershell
& F:/dev/fm1/tools/jieli-wl82/start-flash-session.ps1 -TrustedLocal -ResumeFailedSession '<failed-session>'
& F:/dev/fm1/tools/jieli-wl82/flash-session-client.ps1 -SessionRoot '<new-session>' -Operation status
& F:/dev/fm1/tools/jieli-wl82/flash-session-client.ps1 -SessionRoot '<new-session>' -Operation recover_flash -RequestFile '<original-request>'
```

Migration preserves the original verified baseline, exact candidate and sector scope, failed-write evidence and blocked latch; it performs no device I/O. `-TrustedLocal` uses the existing Python environment under elevation. Inspect the new session before recovery; do not replace its baseline with a partial flash dump or edit protected state.

Before any erase/program operation, recovery loads the pinned RAM helper and reads the entire current 1 MiB flash. The read must match the failed receipt: verified sectors equal the candidate, only the next uncertain sector may have partially programmed application bytes, and every other sector equals the original baseline. Boot/config/reserved spans, including bytes sharing application erase sectors, must still match the baseline. The selected target's disk and PnP identity stay pinned throughout the operation.

After that check, the writer rewrites the original sector scope, application directory last, verifies every written sector and compares a complete final read with the exact candidate. A helper/read/check failure retains the original failed attempt and blocked latch, with the rejected recovery preflight recorded separately. There are no automatic write retries or resets. Do not boot a partially written application; reset once only after complete verification succeeds.

Reset once, then observe serial HELLO plus advancing engine/frame counters. A command return or successful readback does not prove boot, display, controls or sound. Power cycle when the known UBOOT transition requires it or the user reports failed startup; re-enumerate rather than resetting repeatedly.

## Windows enumeration

Known CDC identity during bring-up: VID `3654`, PID `5155`; COM10 was assigned on this PC. Known UBOOT disk: VID `4C4A`, PID `8057`, model `WL82 UBOOT1.00 USB Device`. COM numbers and physical disk numbers can change: resolve current identity before I/O.

Use JSON output for mixed serial/disk/PnP objects. PowerShell's first-object table layout can hide fields and falsely suggest the device is absent. Do not print a flash request JSON wholesale: it contains a base64 full image. Select metadata fields only.

The protected writer may need Windows User Account Control elevation. This is unrelated to USB Audio Class, also abbreviated UAC.
