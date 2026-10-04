# v29 knob-only deployment

On 2026-10-04, the isolated v29 correction was installed through the existing
protected flash session. It retained v25's display, audio, and editing behavior.
The user then requested the v28 preview layout with this same encoder fix.
Current installation status is recorded in the [tracker README](../firmware/protracker/README.md).

## Verified result

- Application: 390,000 bytes, SHA-256
  `e9483172872c01cba925782d7acd3b219216a2efd5c7099188acf0368ebf8c24`.
- Full 1 MiB image SHA-256:
  `d6405cbe57e2f51505b9a56f01514997b9a823b5f4e6bbea9440081dcab23420`.
- All 96 changed sectors passed readback; one complete image readback matched.
- Reset was sent once. The known host log-decoding error was resolved by a
  successful serial observation; no second reset or flash retry was issued.
- COM6 identified `MOD-EDITOR/1`; audio frames advanced from 860,288 to 992,640,
  with zero audio underruns and no reported LCD, scanner, or audio errors.
- The protected session returned idle and unblocked, with no reset or
  observation pending. Existing verified readback and receipts were retained
  locally; no additional backup read was performed.

Physical knob response, display smoothness, and audible behavior require user
observation. The offline build manifest continues to use `flashable: false`;
installation is performed only through the checked protected-session workflow.
