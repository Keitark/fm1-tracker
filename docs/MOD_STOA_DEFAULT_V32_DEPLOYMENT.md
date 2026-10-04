# v32: State of the Art boot default

Installed on **2026-10-05 JST**, at the user's request. This is the v31 tracker
with a different embedded boot song; its UI, encoder fixes and Active Bank
source are unchanged. The canonical original tutorial remains in the repository.

## Content and verification

- Private boot song: State of the Art, **230,652 bytes**, SHA-256
  `f74f635926482845decf2ecdf214e1f53f7100a41871609f5c6789940fc1ef14`.
- STOA has **30 nonempty sample slots**, **189,632 PCM bytes**, 39 patterns and
  45 orders. Samples remain inside its self-contained MOD and are selectable
  through PRESETS. They are not extra entries in the ENV Active Bank.
- The separate original **29-entry / 81,808-byte** bank remains embedded,
  SHA-256 `0696b3a3e75732abf27edbfdd3eb68f9fe689d0cbe13a14c556e853b9abf89f8`.
- Application: **483,920 bytes**, SHA-256
  `383d21a205bfe0b4323cf883e279dc4809083a6892f38fb14caff5750d7d1557`.
- Full 1 MiB image SHA-256:
  `f806e1042525f8bf546d938f3d3caac1a528db29cf035a34df4bb82b382140ed`.
- All source/archive/overlay input hashes matched. All 14 tracker C/header
  hashes matched v31. Host parsing and five-second rendering of STOA passed.
- Target static audit and all nine package CRC/preservation checks passed.
  Heap remains **131,948 bytes**, above the unchanged 128 KiB minimum.
- The protected session reused the verified v31 baseline without an additional
  pre-write firmware dump. **119 sectors** passed verification, followed by
  exactly one complete **1,048,576-byte readback** matching the candidate.
- Reset was requested once. The known host output-decoding error was resolved
  by CDC observation without repeating reset or flash.
- COM6 identified `MOD-EDITOR/1`; boot frames advanced **838,208 → 970,560**,
  with zero underruns and no LCD/key/audio errors.
- The loaded RAM MOD reported **230,652 bytes / CRC32 `d3b29503` / dirty=0**, an
  exact match to STOA. INFO export state was explicitly aborted after checking.
- Playback was resumed after this check. The live status reported running=1,
  dirty/export=0 and zero underruns/LCD/key/audio errors.
- Session returned idle/unblocked with no reset or observation pending.

## Operation

Power-on now restores STOA. Edits remain volatile; save them to the PC when
needed. PRESETS selects STOA's current-song instruments. ENV opens the separate
original bank for audition and explicit copy into the current MOD.
STOA receives ordinary Q1–Q8 bindings to song slots 1–8; the original tutorial's
semantic shortcut assignments apply only to that tutorial.

Build output: `firmware/usb-diag/build/mod-fast-editor-stoa-v32` (ignored).
The classic MOD and unit-specific binaries remain private and excluded from Git.
Build/package manifests retain `flashable: false`; installation used the checked
protected-session workflow. Audible output, physical knob gestures and visual
latency remain user-observed acceptance items.

Related: [v31 deployment](MOD_FAST_EDITOR_BANK_V31_DEPLOYMENT.md),
[tracker controls](../firmware/protracker/README.md),
[PR #3](https://github.com/Keitark/fm1-tracker/pull/3).
