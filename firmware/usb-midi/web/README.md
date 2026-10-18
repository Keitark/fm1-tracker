<!-- SPDX-License-Identifier: Apache-2.0 -->
# FM-1 common USB-MIDI web updater

The primary delivery is the native FM-1 Forge plugin panel, which bundles these
JavaScript modules. The separate Forge integration repository supplies the MCP
App adapter and its owner-private ChatGPT Site hosts the MCP server and UI.
Users do not need a separate browser tab or JavaScript installation.

The modules use Web MIDI SysEx, WebCrypto, IndexedDB and Web Locks. They do not
start a device helper, upload firmware, fetch packages or use WebUSB. The
standalone static page here is a development harness only; serve it on HTTPS
or localhost. Hosts without device APIs, SysEx permission or strict durable
storage cannot update. Build-time Python packaging is separate from device I/O.

The code is a physically unqualified candidate. The matching native source
defaults to reporting its install backend **unavailable**. Connect can display
that capability, but file selection cannot turn it on. Existing CFW without
the common USB-MIDI interface needs a separately qualified first installation.
The common updater does not change the exact stock V15 package pins or firmware
already installed on a device. See [the common protocol](../PROTOCOL.md).

The JavaScript design targets Windows, macOS and Linux where the native host
exposes MIDI/SysEx and storage APIs. Cross-platform hardware acceptance is
unverified; firmware compilation currently uses the Windows JieLi toolchain.

Select the offline builder's unit-private manifest and matching `.fwsc`, then
review their exact SHA-256, target build and MIDI pair. A second real user
confirmation starts a transfer only when fresh capabilities permit it. The
manifest's baseline digest is included in ARM. The matching firmware hashes
the complete current 1 MiB flash while quiesced and refuses to arm unless it
matches that exact baseline. This is a device-side digest comparison, not a
host flash readback. The install capability remains disabled pending separate
loader installation/preservation qualification on the bench.

The parser accepts only the specified two-entry UFW container, fixed loader
digest and `stock-single-v1` manifest. It checks marker bytes, package/image
SHA-256, all CRC16 fields and bounded nonoverlapping flash/loader entries. The
flash prefix excludes the reserved last sector. The target identity is
`FM1_FORGE_001`, with a separately declared 32-byte build digest. A matching
container alone does not establish current-device preservation; the native
baseline comparison and loader qualification are separate requirements.

Before either trigger, strict IndexedDB transactions commit the immutable
reviewed intent. A potentially destructive failure leaves the original attempt
unknown. A page reload keeps the latch; it cannot clear it or start a new
attempt. This version provides no automatic retry/resume or journal-reset UI.
Keep the original package and receipt for the separately reviewed recovery
workflow. Cancelling the review makes no device change. Closing a page during
transfer is not a safe cancellation method.

The lock name matches Forge's existing stock updater on the **same origin and
storage partition**. Web Locks do not coordinate different origins, profiles,
browser applications or native programs. Keep other device clients closed.
No browser claim here supersedes the native compatibility and bench gates.

Transfer responses echo the exact token/address/length. Duplicate and reordered
ordinary reads are bounded and served from an immutable image snapshot. Finish
windows acknowledge only bounded exact duplicate sentinels; unexpected requests
fail closed. Completion requires a fresh identity, idle capability state and
exact target build after USB re-enumeration. It reports no full flash readback
and no physical hardware qualification.

## Development preview

The static host must serve `.mjs` files with a JavaScript MIME type such as
`text/javascript`. Windows MIME settings can make a default Python HTTP server
return `text/plain`, which prevents the module from loading and leaves the
initial storage message visible.

For a development preview only, run this from the repository root:

```powershell
python -c "from functools import partial; from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer; SimpleHTTPRequestHandler.extensions_map['.mjs']='text/javascript'; ThreadingHTTPServer(('127.0.0.1', 8765), partial(SimpleHTTPRequestHandler, directory='firmware/usb-midi/web')).serve_forever()"
```

Open [the local preview](http://127.0.0.1:8765/) and stop the server with Ctrl+C
when finished. This loopback-only command serves static development files; it
is not a device driver or device helper. Production static hosting must provide
the same JavaScript MIME type and does not require Python.

## Offline checks

From the repository root, with Node 22 or later installed for development:

```powershell
node --test firmware/usb-midi/web/test/*.test.mjs
```

Host tests use synthetic packages, MIDI/IndexedDB facades and injected clocks.
The synthetic package suite explicitly substitutes the loader digest through
a test-only WebCrypto facade; production imports no fixtures or bypass flags.
No vendor binary, real unit image, device key or MIDI hardware is required.
Passing these tests does not establish host permission or physical update
acceptance. MIDI port open/close operations have bounded deadlines. A timed-out
port is quarantined for the page lifetime, including after late cleanup, so
that delayed host operations cannot close a newer transfer. There is no
automatic retry after a host lifecycle failure. New files in this directory
are Apache-2.0; inherited component licenses elsewhere remain in force.
