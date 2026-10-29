# Tracker installation from stock V15

The stock-V15 installer uses the existing MIDI OTA transport, then verifies the
running tracker over CDC. Runtime MIDI is unnecessary. The tracker retains CDC,
duplex UAC audio, UVC screen output, the STOA boot default and original breakbeat
Active Bank. The four-track workflow is unchanged.

## Candidate and checks

The linked candidate is `firmware/usb-diag/build/tracker-workflow20-v15cdc-final`.
Application size is 511,760 bytes, SHA-256
`9ef399c21da34a71e83dfc99831b75c3b65beef5988500635ed42369c5f1e1fc`.
Its immutable source/build identity is
`251763e12335d417fc6b3a370a9600f268c9d26b928be26a74be57d3575eb931`.
The unchanged heap gate passes with 133,068 bytes available. The new identity
is 32 bytes of read-only XIP data; no writable firmware store is added.

The final emitted audit and corruption/profile cases pass 41/41; candidate
profile/layout cases pass 10/10; tracker CTest passes 13/13; build-identity seed
tests pass 21/21. The Windows installer/transport/panel suite passes 260 tests,
with six macOS-only checks skipped on Windows. The Windows alpha.10 package
passes passive checks for three MCP protocol revisions with an empty PATH and
all 128 installed files verified. These are software/package results.

The generated FWSC uses only the pinned public V15 template. It preserves the
template's boot/config/reserved layout and unused application tail and checks
the matching CRCs and source closure. This is not a live full-flash readback.
Generated firmware and private song content remain local and untracked.

## Install and verify

Use **FM-1 Forge Windows alpha.10**, **Custom firmware update · SysEx**. Select
the generated `firmware.json` and `firmware.fwsc`, review the stock V15 device,
profile and hashes, then explicitly choose **Install reviewed custom firmware**.
The stock V15 restoration selector is a separate route.

After transfer the plugin closes MIDI and selects exactly one present
USB3654:5155 interface0 CDC port. It sends `FWINFO` with a fresh 16-byte nonce.
Only the exact `FM1FW/1 MOD-EDITOR/1` response with the reviewed 32-byte build
identity and that nonce can complete the attempt. The query is bounded and
read-only. Its emitted helper, constants, pointers, copy/compare routines and
early return are checked; changing them fails the audit.

If transfer completes but the CDC check fails, **Verify tracker boot** observes
the identical original build again without any firmware write. A partial
transfer keeps the existing explicit original-attempt OTA recovery. Unknown
attempts remain blocked; stock/custom guards and old protected sessions are
not cleared. Subsequent protected serial UBOOT entry remains available in the
tracker. CDC COMMIT and common MIDI self-updates remain disabled.

The device was freshly identified as stock `FM-1_015`, loader=false before this
installation attempt. Hardware flash, running CDC proof, controls, playback and
concurrent UAC/UVC acceptance must be recorded separately after installation.
The UVC profile remains Full Speed, 256x240 YUY2 at 5 fps; this change does not
qualify High Speed or the iPad display workaround.

Installer implementation and policy:
[Forge tracker installer guide](https://github.com/Keitark/fm-1-forge/blob/codex/8-fm1-forge/docs/TRACKER_V15_INSTALLER_20261030.md).
