<!-- SPDX-License-Identifier: Apache-2.0 -->
# Common FM-1 USB-MIDI update candidate

This implements an application update transport for the existing single-bank
SDK route. It is separate from Forge's exact manufacturer V15 stock updater.
No raw flash write API is exposed. A browser permission grant, transfer CRC,
SDK check pass or linked candidate is not proof of installation or preservation.

The initial native build keeps the install capability disabled. The pinned OTA
loader's compatibility with our preserved layout, physical acceptance of the
current-unit baseline check, first installation and reboot acceptance remain gates.
The existing firmware already on a device does not gain MIDI by loading this
plugin panel: a MIDI-enabled build must be installed first by a qualified method.

## Wire format

USB-MIDI 1.0, cable zero. Complete SysEx frames are `F0 packed F7`. The packed
body is a continuous least-significant-bit-first stream of 7-bit groups. Its
decoded bytes are:

```text
00 59 command payload_length_le24 payload checksum
```

The checksum is the one's complement of the payload byte sum modulo 256.
Payload length is at most 520 bytes. Noncanonical packing, missing terminators,
wrong cable/CIN, overflow, invalid checksum and inconsistent lengths reject the
frame. The exact separate vendor trigger is `F0 22 24 35 7F F7`.

| Command | Request | Reply |
| --- | --- | --- |
| `11` identity | empty | 27 ASCII/NUL bytes; common identity `FM1_FORGE_001` |
| `31` capabilities | empty | 47-byte record below |
| `32` arm | version 1, family 8, UFW length LE32, target build digest 32 bytes, baseline digest 32 bytes (70 total) | one status byte; zero accepts |
| `30` file service | device sends token, offset LE32, length LE24 | browser echoes all 8 request bytes, followed by exactly the requested data |

Capabilities are `FMC1`, version 1, family 8, state, loader compatibility flag,
chunk size 512 LE16, flash capacity 1,048,576 LE32, current build digest 32
bytes, error byte. Idle state is zero. An unsupported loader flag must remain
visible as unavailable; the browser must not arm or trigger it. An arm belongs
to the current USB epoch and is invalid after reset, disconnect or timeout.

File reads are bounded to 512 bytes and the reviewed UFW image. The loader
controls read offsets; coverage does not imply ordered or nonoverlapping reads.
At special offset `E0000000`, length 8, the first phase expects `success\0`.
At `F0000000`, length 8, the OTA phase uses the same acknowledgement. They are
phase-specific sentinels, not permitted ordinary image offsets. No timeout or
missing reply authorizes reset, a write retry or an automatic resume.

## Package and preservation boundary

`package.py` accepts complete, hash-pinned baseline and candidate images plus
the exact official OTA resource. It never contacts a device. Candidate changes
are limited to the application bytes and the first two application-directory
records. All other bytes must equal the supplied baseline. It wraps the encoded
flash prefix ending at the final application erase sector in a two-entry UFW:
plain flash type 0 and OTA loader type 100, with per-entry CRC16 and bounded
offsets. The outer `.fwsc` wrapper inserts 20 product markers. Public header
encoding is container formatting and does not provide authentication.

The browser independently checks the manifest, exact package/image SHA256,
container integrity, target identity, layout and pinned OTA hash. Manifest
`targetBuildSha256` is the exact 32-byte identity expected after reboot, not
the package hash. No stock V15 pin is relaxed and no force/migration path is
provided. The common code and manufacturer stock code remain separate.

**A manifest alone does not establish a current-device baseline.** After a
confirmed ARM, the firmware stops peripherals and computes SHA-256 over the
complete current 1 MiB flash in bounded foreground chunks. It accepts the arm
only if that digest matches the reviewed manifest's baseline. No raw flash or
device digest is sent to the web page. Disconnect, read failure, mismatch or
timeout prevents the update. This check uses the pinned SDK's original-flash
read API; its actual device behavior still requires bench acceptance. It does
not qualify the vendor OTA loader's write order or preservation behavior, so
the native compatibility gate remains off in normal builds.

Packages and baseline reports are unit-private and must stay outside a public
repository. No firmware, device readbacks, chip keys, commercial music or ROMs
are included in the source candidate.

## Browser execution

The FM-1 Forge native plugin panel bundles the JavaScript modules from `web/`.
ChatGPT Sites hosts its MCP endpoint and UI; users do not need a separate browser
tab or JavaScript installation. The standalone page in `web/` is a development
harness. There is no Python, Node, serial broker or native-helper process in the
device update path. Python is an offline build/package tool only. The native
host's MIDI API and SysEx permission are checked at the connection gesture.

The JavaScript design is portable across Windows, macOS and Linux where those
APIs and permissions are available. Mac/Linux hardware acceptance is unverified;
the current firmware compiler setup uses the Windows JieLi toolchain.

The browser requires a persistent journal and a shared exclusive device lock
within its origin; browser locks cannot arbitrate another origin or native app.
It records intent before sending the first update trigger. A failure after a
potentially destructive handoff retains an uncertain outcome; reload does not
clear it. Successful completion requires the expected application identity and
exact build digest after re-enumeration. It does not claim a full flash readback.
The initial implementation does not offer recovery by automatically repeating
an interrupted write. Use the existing separately reviewed recovery workflow.

## Provenance and release

New independently authored files here use Apache-2.0; see `LICENSE` and `NOTICE`.
The separately defined USB/audio scope has its own owner-authorized Apache-2.0
grant in [its scope/history notice](../usb-audio/RELICENSE.md); it is not included
in this bounded MIDI export. This does not grant rights in the vendor SDK or
change licenses of unrelated GPL engines or the official M-VAVE APP.
`docs/LICENSING.md` in the parent workspace records the component boundaries.
A public export must list exact source
hashes, retain these notices, exclude private outputs and remain a review
candidate until source and redistribution rights are checked.
