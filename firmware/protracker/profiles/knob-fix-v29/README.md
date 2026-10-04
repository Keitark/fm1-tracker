# v29 knob-only correction from the v25 baseline

Built, packaged and flashed on 2026-10-04 JST. Flash readback and serial boot
checks passed. This historical checkpoint was then superseded by installed
[combined v30](../../../../docs/MOD_VIEW_KNOBS_V30_DEPLOYMENT.md), which adds
the refined view/edit workflow while retaining this input correction.
Physical new-layout, knob and audio acceptance for v30 remain pending.
See [deployment evidence](../../../../docs/MOD_KNOB_V29_DEPLOYMENT.md).

This profile starts from all seven exact v25 source files, recovered from this
task's source patch history and checked against the v25 build manifest hashes.
`baseline-source.json` records those hashes. Six core/header files remain byte
identical; `target.c` has the isolated input delta in `knob-only.patch`.
The historical v25 application SHA-256 is
`edf482ccd9caf0b1cb9ce68cbce92aff74a2534138219cb28ac13265e9636b67`.

## Correction

Encoder decoding now runs in the paced scan callback, before the next scan
replaces the completed snapshot. Screen rendering cannot discard those
intermediate phases. The task takes a locked snapshot of accumulated counts
and delivers four decoded contact edges per click on all seven encoders.
It processes at most two clicks per knob per iteration and retains additional
movement for later iterations. It no longer clips and discards fast movements.
The stock two-scan stability filter, physical mappings and knob actions remain.
Scanner recovery resets decoder state while holding the input lock.

This is the isolated knob-only deployment checkpoint. Its
screen, audio and edit behavior retain the v25 baseline. The portable
view/workflow developed in v28 is now installed through combined v30.

## Evidence

- Encoder host test passed, including six new regressions for all seven knobs,
  display-polling pauses, duplicate snapshots, sequence wrap, preserved 80-edge
  backlog, direction reversal, and signed count extremes.
- Static firmware audit passed. A reviewed four-byte merged-global relocation
  was added to the audit's exact expected instruction encodings.
- All nine package preservation/CRC checks passed.
- Baseline audio output, key handling, knob mappings and renderer function
  text were compared and are unchanged.
- 96 sectors were verified, and one full 1 MiB device readback was identical
  to the packaged v29 image.
- After one reset, protected serial observation identified `MOD-EDITOR/1` on
  COM6. Audio frames advanced from 860,288 to 992,640 with zero underruns and
  zero peripheral errors.
- The reset command hit the known host `UnicodeDecodeError` after reset was
  sent. It was never resent; the follow-up observation resolved the reset and
  left the protected session idle and unblocked, with `reset_pending: false`
  and `needs_observation: false`.
- Physical acceptance was not completed for this isolated profile before it
  was superseded by combined v30.

Application: 390,000 bytes, SHA-256
`e9483172872c01cba925782d7acd3b219216a2efd5c7099188acf0368ebf8c24`.
Private 1 MiB image SHA-256:
`d6405cbe57e2f51505b9a56f01514997b9a823b5f4e6bbea9440081dcab23420`.
Build artifacts: `firmware/usb-diag/build/mod-editor-knob-only-v29`.

## Rebuild

From the workspace root:

```powershell
C:\Python311\python.exe firmware/usb-diag/build.py --controller 0 --peripheral-tests --lcd-stock-fill --lcd-stock-dma --lcd-stock-sequence --mod --mod-source-dir firmware/protracker/profiles/knob-fix-v29 --lcd-async --lcd-spi30 --lcd-rgb444 --mod-default-song private-library/MOD/STATEART.MOD --mod-default-song-sha256 f74f635926482845decf2ecdf214e1f53f7100a41871609f5c6789940fc1ef14 --out firmware/usb-diag/build/mod-editor-knob-only-v29
C:\Python311\python.exe firmware/usb-diag/package.py --build firmware/usb-diag/build/mod-editor-knob-only-v29
```

Installation uses the existing protected flash session workflow with checked
baseline, plan and full image readback. The build manifest retains
`flashable: false`; it does not authorize direct flashing outside that workflow.
