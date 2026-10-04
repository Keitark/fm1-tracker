# v28 view and encoder correction: combined v30 deployment

Installed on 2026-10-04 after the user requested the layout shown in
[`fm1-tracker-screens.png`](../firmware/protracker/preview/fm1-tracker-screens.png)
with the knob correction. The combined build uses the canonical tracker source:
v28's view/edit workflow plus completed-scan encoder capture and retained
movement backlog. The isolated v29 deployment retained the old view and was
superseded by this combined image. Tracked by
[issue 1](https://github.com/Keitark/fm1-tracker/issues/1).

## Included behavior

- Four waveform scopes above all four pattern channels.
- Nine visible pattern rows, fixed highlighted current row, selected-track
  color, and WRITE note-field color.
- Selected-order playback, pattern playback, step-zero note entry, preserved
  effects when entering notes, and selective note/effect clearing.
- Paced encoder capture before scan replacement, four contact edges per click,
  bounded delivery that retains pending movement independently of screen work.
- The existing SPK mono speaker mix and ST stereo output selection.

The renderer SHA-256 matches the saved preview manifest exactly:
`0fef83044ae5cfbca25717f9dab35cf4e458928200d1294a08ab940fea5564aa`.
The preview is an offline firmware render, not a physical device capture.

## Build and verification

- Application: 392,048 bytes, SHA-256
  `e8c9dd354c2c2d7f0332f1addfd5fdf8ba6e9f0f6018106c3736d5a5831bc3de`.
- Full 1 MiB image SHA-256:
  `05f940970cbfe88284ada86deabb17ebc6e6f525a0123d2744f3392e1a1e823c`.
- Rebuilt host editor and starter-bank tests: 2/2 passed; encoder test: 1/1
  passed. Existing regression coverage includes paused UI polling and retained
  fast encoder movement.
- Firmware static audit and all nine image preservation/CRC checks passed.
  Heap reserve is 131,148 bytes, above the 131,072-byte minimum.
- All 96 changed sectors passed verification, then one complete image readback
  matched the candidate. Boot/configuration/reserved boundaries were preserved.
- Reset was sent once. The known host output-decoding error was resolved by
  serial observation; no second reset or flash retry was issued.
- COM6 identified `MOD-EDITOR/1`. Audio frames advanced from 419,072 to 551,424
  with zero underruns and no reported LCD, scanner, or audio errors.
- The protected session returned idle/unblocked with no reset or observation
  pending. Existing readback and receipts were retained locally; no additional
  backup read was performed.

Build output is `firmware/usb-diag/build/mod-editor-view-edit-knobs-v30`.
The build manifest still uses `flashable: false`; the protected session performs
installation with a checked baseline, plan, and full readback.

Physical layout, knob response, audible output, scrolling frame rate, and
audiovisual delay await user observation. A successful serial boot check does
not measure those properties.
