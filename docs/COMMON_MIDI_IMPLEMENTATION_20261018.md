# Common USB-MIDI implementation evidence — 2026-10-18

This is an opt-in linked firmware candidate, not a physically qualified update
release. No device was opened, flashed, reset or switched into a loader while
implementing or validating this change. Normal builds keep installation disabled.

## Linked candidate

Command, from the firmware workspace root:

```powershell
python -B firmware/usb-diag/build.py --controller 0 --factory-diag --usb-midi-update --out tmp/forge-midi-uac-candidate-20261018-r7
```

The existing pinned Windows JieLi compiler/linker required execution outside the
filesystem sandbox. This was an offline build only, with compiler temporary
files directed into the workspace. The final build and static audits passed:

| Field | Result |
| --- | --- |
| Manifest status | `LINKED_USB_DIAGNOSTIC_UNTESTED` |
| Application size | 161,328 bytes |
| Application SHA-256 | `c412f2d45d7dd5edc887badae6465f298e208826a73bc79ad2340feb4e46267b` |
| Reported firmware build identity | `e56db29b4c42815a0bd086ab17b66eb95ee47fe76b188f646755cbbe44a423df` |
| SDK revision | `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d` |
| USB MIDI | USB0 IN4 / OUT3, interfaces 5/6 with UAC |
| USB product name | `FM-1 Forge`, matching strict JavaScript discovery |
| Loader compatibility / installation enabled | `false` / `false` |
| Hardware accepted / flashable | `false` / `false` |

The build manifest and detailed audit are in the ignored output directory. Eight
linked-image tests passed with `FM1_MIDI_BUILD` explicitly pointing at r7. They
cover the disabled gate, exact MIDI descriptor/policy/DMA/task/power boundaries,
bounded shared packet implementation and the product name used by discovery.

Seven native host tests passed for the actual codec, SHA-256, SDK operations,
USB adapter and composite policies. The existing USB regression suite passed
all 33 tests. These deterministic checks and static audits do not exercise a
real USB controller, flash read, OTA loader or physical peripheral.

## Integration boundary

The separate FM-1 Forge integration repository bundles the independently
authored browser core into its native plugin UI; the private Site hosts that UI
and MCP server. The standalone page in `firmware/usb-midi/web/` is a development
harness. There is no Python/Node device helper in the direct MIDI path.

The project graph remains schema 3 and does not automatically add MIDI to
generated projects or existing app packages. The explicit candidate retains
CDC/UBOOT and optional UAC. MIDI plus UVC is rejected by the command-line
builder, overlay and native compilation guard because both need USB0 IN4.

The JavaScript design is portable across Windows, macOS and Linux when the
native host exposes Web MIDI/SysEx, WebCrypto, strict IndexedDB durability and
Web Locks. This is not verified hardware compatibility on those operating
systems. Firmware compilation currently uses the Windows toolchain.

## Remaining physical gates

Before enabling installation, qualify the exact OTA loader and container with
the target flash layout, current-unit baseline hashing and preservation of all
protected/calibration regions. Review the exact first-install image, baseline
and rollback path separately before any device write. Then verify the complete
transfer, reboot identity/build digest and independently checked preservation.
LCD, audio, controls and long-run stability require their own acceptance.

The common updater's capability gate cannot be enabled by selecting a manifest.
Do not interpret an advancing transfer counter or expected reboot identity as a
complete flash readback. An uncertain attempt retains its original recovery
evidence; no automatic retry or different-attempt resume is provided.

The source-only preparation and its final portable test receipt are recorded in
[the public source audit](PUBLIC_SOURCE_AUDIT_20261018.md). That candidate excludes
private history, firmware images and SDK binaries and remains unpublished.
