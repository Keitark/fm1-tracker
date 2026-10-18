# Public-source preparation audit — 2026-10-18 JST

Status: **candidate preparation only; not approved or published**. This is a
source/provenance inventory, not an exhaustive copyright determination. No device
access or firmware transfer was performed for this audit. No existing license is
replaced, and the current private Git histories remain outside the export.

The current exact inventory classifies 39 scoped Apache-2.0 source files in
`firmware/usb-midi/`. The fresh r2 snapshot is at
`tmp/fm1-common-midi-public-source-candidate-20261018-r2/`: 39 sources plus
generated `PUBLIC_SOURCE_CANDIDATE.json`, with no Git history, dependency
binaries or private images. Every source/export hash was rechecked after tests
and matches the inventory. The original 38-file snapshot at
`tmp/fm1-common-midi-public-source-candidate-20261018/` is **superseded** and
retained unchanged as a private reference. The r2 snapshot remains
`candidate-unreviewed`; this is not a publication approval.

Standalone checks ran from r2 exported source: four portable C tests, 37
browser JavaScript tests and five synthetic package tests passed. Eight
linked-image Python checks skipped with their explicit integration-only reason;
three C integration targets were omitted because surrounding USB target/policy
sources are deliberately excluded. Build outputs are outside the source export
at `tmp/fm1-common-midi-public-host-20261018-r2/`. The exporter suite passed 17
checks, with four actual OS link-creation cases skipped due host permissions;
deterministic symlink/reparse/hardlink rejection checks passed. The local
validation receipt is `tmp/fm1-common-midi-public-source-validation-20261018-r2.json`.

The r2 correction adds a lock-held pre-write hook for integration guards,
read-only saved-attempt inspection without nested locking, and a bounded fresh
identity/capability probe. It also adds focused probe regressions. The updater's
SHA256 in original and exported source is
`9146b3d3f0ee6fc72fb93190712415d39f443c8cfce021908a96203f275b6e35`.

## Audited references

| Reference | Immutable revision / evidence |
| --- | --- |
| Official `m-vave/fm1_open` | `b178ac43c25f010c60a878056fb5331fe693fd62`; local read-only reference `references/source/m-vave-fm1-open-20261018`, upstream `LICENSE.md` and `THIRD_PARTY.md`. |
| Existing JieLi SDK used by the new backend | `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`; tracked root `LICENSE` is Apache-2.0 (blob `261eeb9e9f8b2b4b0d119366dda99c6fd7d35c64`), locally sparse-skipped. `README-en.md:496` corroborates the grant; separate component notices still apply. |
| Prior `Keitark/fm1-mdx` USB origin | `41410578152eec195b534b3acfe2fa1b10e7476e`; root `LICENSE`, `FM1_PROVENANCE.md`, `firmware/usb-audio/{bridge,profile,target}.c`, `firmware/usb-diag/packet.c`. |
| Prior `Keitark/fm1-public` transfer | `d60e1b190e785ef83490ad6716e9c8ecf1e6013d`; `PROVENANCE.md` records the MDX origin and preserved GPLv3 terms. |
| PeakRacing NES | `68bdfc8de570264c0e84f73766f1a1ed3591066f`; current `firmware/nes/THIRD_PARTY.md`. |
| Current UVC origin | `firmware/usb-video/SOURCE_ORIGIN.json`, `Keitark/fm1` revision `fb677ea095a28fba3b4b8b8fe91cc3deeb0278d5`; source origin exists, but this manifest states no license. |
| Official OpenAI SDK used by separate Forge integration repo | `site/vendor/openai-mcp-extensions.ORIGIN.md`: SDK 0.2.0, Apache-2.0, source `0d606217705e0e71eb21fff009619b67b80611dc`. |

MDX's root GPL license and GPL RetroFM/mdxtools vendors are present in its first
`92a2316` commit. UAC was introduced later in `b135802`, and bounded packet code
in `99bb92c`. The combined MDX player has GPL dependencies, but that alone does
not establish that its isolated USB/UAC implementation copied those engines.
Existing GPL declarations are preserved until an ownership/source review supports
any separate licensing decision.

The new backend uses the existing old JieLi SDK's `update.a` APIs, rather than
M-VAVE's proprietary `libfm1_engine.a`. The official APP/engine SDK exception is
recorded as a future integration boundary, not a dependency of this new source
candidate. A potential GPL-only/proprietary-engine combination must not be
misreported as the current backend's linkage.

The candidate protocol's baseline gate is an exact on-device SHA256 of the
current 1 MiB raw flash, computed after confirmed ARM and before its
acknowledgement/SDK trigger. It compares against the reviewed package's baseline;
neither raw flash bytes nor the observed digest are exposed through the host
protocol. This is a source-level safety boundary, not a claim of physical
qualification: normal target builds continue to report installation disabled,
and device read/loader/transfer/reboot acceptance remains unqualified.

## Component inventory and decision

| Paths / material | Recorded terms and source boundary | Initial export |
| --- | --- | --- |
| New `firmware/usb-midi/` files, individually inventoried | Independently authored candidate currently under scoped Apache-2.0 via its own LICENSE/NOTICE. Exact per-file provenance/hash required; source/ownership review remains required. Owner may change independently owned scope before public release. | Eligible candidate after complete inventory; no binary. |
| `firmware/usb-audio/*`; `firmware/usb-diag/{packet.c,packet.h,packet_fake.h,test_packet.c,usb_policy.c,tests/usb_policy_tests.c}`; `firmware/nes/audit_usb_packet.py` | The exact copied closure is listed in `firmware/usb-audio/SOURCE_ORIGIN.json`; GPL-3.0-only and retained `licenses/fm1-mdx-GPL-3.0.txt`. Preserve current notices. | Excluded. |
| Official APP sources and build/workbench template | GPL3-only with exact binary-SDK exception. Exception applies to marked APP code, not automatically to our inherited GPL-only components. Current new backend does not import this APP. | External reference, excluded. |
| Official SDK `libfm1_engine.a` and other listed binary components | Separately licensed. Proprietary engine has FM1-only grant; other components retain their own terms. Root SDK licensing does not replace Newlib/compiler-rt/micro-ecc/other notices. | Excluded; separate binary redistribution review. |
| Official `uboot.boot`, `ota.bin`, `cfg_tool.bin`, `boot-config.bin`, `sdk/firmware/FM-1_015.fwsc` | Specific upstream Apache-2.0 resource grant, with required notices/modification markings. Does not relicense private readbacks or complete historical packages. | Excluded from source-only candidate. |
| `firmware/nes/`, `firmware/usb-diag/vendor_overlay.py` | NES Apache dependency; vendor/PeakRacing adaptation fragments and board findings are documented. SDK source/license notices and changes must be preserved if redistributed. | Excluded pending path-level review. |
| `firmware/forge/`, `firmware/protracker/`, `firmware/usb-video/`, `firmware/mdx/` | Much project-maintained code has no individual SPDX header/root license in this workspace. Origin statements do not certify sole authorship. MDX runtime and other external dependencies retain their own terms. | Excluded pending explicit ownership/license classification. |
| `firmware/protracker/samples/FM1BREAK.*` and original generation inputs expressly covered by `ASSET_LICENSE.md` | CC0-1.0 for identified original assets only. | Excluded from initial transport candidate. |
| Original MDX demo, starter MOD, Buddha prototype and other generated assets | Separate asset review; Buddha voice uses TTS-derived inputs and is expressly outside the CC0 breakbeat bank. | Excluded. |
| Separate Forge integration updater `tools/jieli-wl82/stock-sysex/` and `site/lib/browser-stock/` | README describes independent factual framing/timing implementation; consulted Felucca installer is GPL-3.0-only. Review expression/provenance before assigning permissive license; no current root license grants blanket clearance. | Excluded from this firmware export. |
| Separate Forge integration vendored packages | OpenAI SDK Apache-2.0; Sites Vite plugin and shadcn licenses MIT. Preserve their license files/package notices. Other dependencies require their own inventory if released. | Excluded. |

## Required exclusions

Never copy current private history, `private-backups/`, `private-library/`,
`private-media/`, `.codex-remote-attachments/`, credentials, session descriptors,
chip/device security material, unit readbacks, flash-sector plans, firmware
images or loader blobs into this source candidate. SDK/toolchain/dependency
checkouts under `references/`, vendor tools and generated `build/` outputs remain
outside its scope. Commercial ROMs, Doom WADs, classic MOD songs including
STATEART, imported samples and TTS caches are excluded. No Doom source directory
is tracked in the audited firmware tree; that does not clear other private Doom
work or WAD assets for release.

The current `.gitignore` excludes many of these paths, but an ignore file does
not remove previously committed content or prove source ownership. Therefore the
preparation exports a fresh source snapshot from an explicit allowlist rather
than publishing or rewriting the current history.

## Mechanical gate and remaining review

`tools/public-release/export_source.py` accepts only an explicit source inventory
with exact hashes, permitted source paths, a known license and independently
authored origin. It rejects unclassified additions, traversal, link/reparse/hard
links, private/generated paths and binary data. It writes only a fresh directory
under currently Git-ignored `tmp/`, plus a candidate receipt. It cannot prove
authorship or detect every secret, and it does not grant redistribution rights.

Before a public repository or firmware binary is prepared, review the exact
candidate, review its scoped Apache-2.0 license and include its license/copyright
material, preserve all relevant upstream notices and settle any copied-code or
SDK/GPL combination. In particular, an exception attached to official M-VAVE APP
code cannot simply be added to independently obtained GPL-only USB components.
The prepared source protocol and host tests also do not establish a working
flash/update path on hardware.

The standalone export omits the surrounding target firmware build, SDK archives
and USB/boot integration sources. Its CMake build reports and skips the target
adapter and composite-policy checks when those sources are absent; linked-image
Python checks likewise require the full checkout and an explicitly selected
image. Portable host/browser/package checks can run on the isolated source, but
their success and those integration skips do not qualify a full firmware image.

For any later package that includes official loader/recovery binaries, use the
[specific Apache resource grant](https://github.com/m-vave/fm1_open/blob/b178ac43c25f010c60a878056fb5331fe693fd62/THIRD_PARTY.md#bootupdate-resources-and-recovery-firmware),
retain the full Apache license and relevant notices, record exact bytes/pin and
identify modifications. The current source export includes no loader or recovery
image. Do not copy the entire official mixed GPL/SDK notice into new transport
source in place of a precise component inventory.
