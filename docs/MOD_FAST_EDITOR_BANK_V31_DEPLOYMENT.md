# v31 fast editor and original bank deployment

Installed on **2026-10-05 JST** after the user requested transfer of the
Issue #2 implementation. Source commit: `5a936d22520037f43183d2fbb995e80cc21f6205`.
Tracked by [Issue #2](https://github.com/Keitark/fm1-tracker/issues/2) and
[PR #3](https://github.com/Keitark/fm1-tracker/pull/3).

## Verified installation

- Application: **340,912 bytes**, SHA-256
  `ee5df51f85595ddb4c53c357ad8dbd994d25a57b9595b1daef4adb27e9284db9`.
- Full 1 MiB image SHA-256:
  `8ffb01107fca32f5eb6c395be61cf0e86a623f4e3ceb955d15bedf8692583f63`.
- Current-source/hash verification, static audit, minimum 128 KiB heap gate and
  all nine package preservation/CRC checks passed before transfer.
- FM-1 CDC on COM6 reported stopped, dirty=0, export=0 before UBOOT entry.
- The existing protected session reused the verified v30 baseline. No extra
  pre-write firmware dump or backup read was performed.
- All **96 changed sectors** passed verification, followed by exactly one
  full **1,048,576-byte readback** matching the candidate.
- Reset was requested once. The known host output-decoding error occurred
  after the reset operation; reset was not repeated. Subsequent CDC observation
  confirmed `MOD-EDITOR/1` on COM6 and resolved the session's pending state.
- Boot audio frames advanced from 1,235,264 to 1,367,616 with zero underruns
  and no LCD/key/audio errors. Session returned idle/unblocked, with neither
  reset nor observation pending.

## Live demo check

The boot MOD reported **87,612 bytes**, CRC32 **`d034dd30`**, dirty=0; these
match `samples/FM1BREAK.MOD`. INFO export state was explicitly aborted before
playback. The read-only original 29-entry bank is included in the verified image.

During **20.125 seconds** of song playback:

- audio frames advanced from **4,665,408 to 5,547,776**;
- completed UI frame counter advanced from **2,647 to 3,150**, approximately
  **24.99 updates/second**;
- order/row progressed from 00/11 to 03/33;
- underruns remained **0**, and LCD/key/audio errors remained **0**;
- dirty/export remained **0**.

The software frame counter measures completed strip submission, not an optical
screen measurement. It does not establish perceived smoothness or input/audio
latency. The test ended stopped at order 03, row 34, preserving sample/channel
context as required by STOP synchronization.

## Remaining physical acceptance

The source/build/package, flash readback, boot and brief playback telemetry
checks passed. Physical knob detents/gestures, field/block readability,
audible speaker/stereo output, perceived scrolling and saved edited MOD
round-trip remain on the plan's acceptance checklist. PR #3 remains a draft.

Controls and offline previews: [tracker README](../firmware/protracker/README.md).
Detailed implementation/build evidence:
[Milestone #2](../firmware/protracker/MILESTONE_2.md).
RAM edits and shortcut bindings remain volatile; power cycling restores the
embedded original demo. Firmware build/package manifests keep `flashable: false`;
installation used the protected checked-plan workflow.

Local evidence is retained in the protected session's flash/reset/observation
runs and `firmware/usb-diag/build/mod-fast-editor-bank-v31/v31-live-check.json`.
Unit-specific binaries and logs remain excluded from Git.
