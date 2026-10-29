# Repository publication review — 2026-10-30 JST

Tracking: [Issue #22](https://github.com/Keitark/fm1-tracker/issues/22).
The owner requested updating and making the existing FM-1 tracker repository
public. This review is separate from the earlier narrow MIDI source-export tool.

## Reviewed scope

At tracker revision `b186d39527c9fa0ea7fe627954078fac659e39f8`:

- All 11 advertised remote branch tips matched the locally reviewed refs.
- No remote tags; 45 reachable commits and 927 unique blobs were examined.
- The current tree contained 585 tracked files. No private device captures,
  stock firmware packages, commercial game assets, toolchain binaries or
  external SDK/checkouts were tracked in the reviewed refs.
- Credential/private-key/unit-key scans found no actual credential or device
  security literal. Placeholder fixtures and shared file-format constants were
  distinguished from credentials.
- GitHub had no releases/assets, Actions runs/artifacts, review attachments,
  active wiki or discussions to publish. Issue/PR bodies and comments were
  reviewed; no actual secret or uploaded private-file reference was found.

The tracked binary media consists of generated UI previews and five small
music/bank fixtures. The fixtures byte-match their tracked generators:
MDX 242 bytes, PDX 928 bytes, FM1START.MOD 32,828 bytes, FM1BREAK.FMB 81,808 bytes,
and FM1BREAK.MOD 87,612 bytes. Classic MODs, ROMs, private recordings and unit
firmware images remain local inputs excluded from Git.

## License and provenance boundaries

Publishing this repository adds **no blanket license** and does not change
existing component terms. Preserve each applicable license, copyright and
NOTICE file. The original FM1BREAK assets have their own explicit CC0 grant;
that grant does not cover the complete repository or imported/generated voices.

The Buddha research sketch records a contour derived from an identified score
excerpt. Permission/public-domain status for that specific score is not recorded.
Its source/provenance notices remain; no TTS audio, generated chant MOD or voice
cache is tracked, and the sketch is not represented as CC0 starter content.

The older MIDI source-export inventory remains a deliberately narrow export
workflow, rather than a whole-repository license inventory. Source publication
does not establish redistribution rights for every possible linked binary.

## Current development and hardware status

The latest editing work remains in
[PR #21](https://github.com/Keitark/fm1-tracker/pull/21) on
[`codex/20-tracker-workflow`](https://github.com/Keitark/fm1-tracker/tree/codex/20-tracker-workflow).
The main README links that source and tutorial without merging unfinished
feature pull requests. Older deployment notes and PR statements describe their
dated bench snapshots, not the currently installed firmware.

The Workflow 20 candidate passes its documented software checks but remains
unflashed and physically unaccepted. A read-only query confirmed stock V15.
This publication task performs no device programming or firmware installation.
