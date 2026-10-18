<!-- SPDX-License-Identifier: Apache-2.0 -->
# Common FM-1 USB-MIDI update candidate

This independently authored module adds USB-MIDI 1.0 framing, discovery and a
bounded adapter to the pinned JieLi single-bank active updater. It is an opt-in
source candidate. Installation remains disabled in every normal target build;
the capability reply reports that restriction and ARM rejects it. No device was
accessed while implementing or testing this module.

The [protocol](PROTOCOL.md) describes the browser/file-service contract and the
remaining loader, package, current-unit baseline and physical acceptance gates.
The manufacturer V15 update flow remains separate. Loading a browser panel does
not add a MIDI interface to firmware already installed on an FM-1.

## Native integration

`fm1_midi.c` parses cable-zero USB-MIDI event packets and continuous 7-bit SysEx
framing, validates exact length/checksum/padding and generates the two-interface
Audio/MIDI descriptor. `fm1_midi_target.c` owns separate aligned USB0 EP4 IN and
EP3 OUT DMA buffers. The USB OUT callback only places whole packets in a bounded
queue. Foreground processing owns parsing and TX. TX checks endpoint ownership
once and commits one packet without the SDK's blocking bulk-write loop.

`fm1_midi_update.c` provides `update_op_api_t` file operations to
`app_active_update_task_init()` using `USB_HID_UPDATA`. It does not expose raw
flash commands or use the incompatible passive dual-bank updater. The SDK is
responsible for loading and checking the vendor OTA engine. No proprietary
M-VAVE engine archive is linked.

ARM first requests foreground peripheral shutdown and waits at most two
seconds for idle. It then reads and hashes the full 1 MiB raw flash, at most
1024 bytes per foreground turn, with a 15-second hash deadline. Its one-byte
success acknowledgement requires an exact match to the reviewed baseline SHA
supplied in the ARM request. No raw flash bytes or live digest are returned to
the browser. A mismatch, short read, resumed peripheral or reset rejects ARM.
An accepted arm expires
after five seconds and belongs to the current USB epoch. A trigger without a
valid arm cannot start the SDK. The separate CPU0 `update` task has 2048 stack
words. SDK startup must report its channel initialization within one second.
Reads use exact token/address/length matching, at most 512 bytes per request,
and bounded waits. There are no automatic write retries. Reset/disconnect,
queue overflow or a pending-read failure invalidates the session. A failure
after starting the SDK stays latched until the application is restarted.

The final `success\0` host receipt is checked **before** asking the SDK to write
retained update parameters. The subsequent reset callback checks the USB epoch
again; a concurrent cancellation clears those parameters and does not reset.

USB0 supports endpoint indices 0 through 4. Existing CDC notification/data uses
IN2/IN3 and OUT2; optional UAC uses IN1/OUT1. MIDI uses IN4/OUT3. UVC also needs
IN4, so the current builder rejects MIDI plus UVC. CDC/UBOOT stay present.

## Offline build and tests

From the repository root:

```powershell
cmake -S firmware/usb-midi -B tmp/usb-midi-host
cmake --build tmp/usb-midi-host --config Release
ctest --test-dir tmp/usb-midi-host -C Release --output-on-failure
python firmware/usb-diag/build.py --controller 0 --factory-diag --usb-midi-update --out tmp/usb-midi-forge-candidate
```

The target command only builds. It cannot install firmware and does not enable
the compatibility gate. A manifest records the source-derived 32-byte build
identity and explicit `installation_enabled:false`. Existing linked-image boot,
power, RAM, CDC, UBOOT and composite audits remain enabled; MIDI adds its task,
internal DMA buffers, identity and disabled-installation assertions.

Host tests execute the actual codec, descriptor builder, SDK file-operation
adapter and USB target code against deterministic models. The qualification
flag is enabled only inside one host test executable to exercise otherwise
blocked state transitions. That test does not qualify the vendor loader or
grant a target build permission to write flash.

`tests/test_image.py` is an additional integration suite requiring the complete
firmware workspace's existing boot/power audits and an explicit
`FM1_MIDI_BUILD` directory. It skips cleanly in the standalone public source
candidate, which does not redistribute those shared components.

## Provenance

New native files in this directory are Apache-2.0; see [LICENSE](LICENSE),
[NOTICE](NOTICE) and [SOURCE_ORIGIN.json](SOURCE_ORIGIN.json). They implement
documented USB-MIDI layouts and factual vendor framing independently. External
SDK sources/archives retain their own licenses and remain outside the public
export. The project-owned portions of the separately defined USB/audio scope now
have an owner-authorized Apache-2.0 grant; see
[its exact scope and history](../usb-audio/RELICENSE.md). That wider component
remains outside this bounded MIDI source export. Other GPL engines and the
official M-VAVE APP retain their original terms.
