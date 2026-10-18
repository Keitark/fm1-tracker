# USB/audio transport provenance

The current project-owned USB/audio transport scope is **Apache-2.0**, effective
2026-10-18 under the owner's explicit instruction. The exact 20-path scope,
authorship review and historical-release boundary are in
[RELICENSE.md](RELICENSE.md); retain [LICENSE](LICENSE) and [NOTICE](NOTICE).
This is a component grant, not a blanket license for the firmware repository.

## Historical source chain

The original 17 transport/audit/test files were imported from
`Keitark/fm1-public` at `d60e1b190e785ef83490ad6716e9c8ecf1e6013d`, whose USB
audio and bounded packet transport identify `Keitark/fm1-mdx` at
`41410578152eec195b534b3acfe2fa1b10e7476e`. Those imports carried
GPL-3.0-only notices based on the MDX project's root license. Earlier GPL grants
remain in force; the full historical license is retained at
`../../licenses/fm1-mdx-GPL-3.0.txt` for the history and other imported components.

`SOURCE_ORIGIN.json` preserves those original repository/commit fields and all
17 original hashes as immutable historical import evidence. Its `license`
records the current Apache grant; `historicalLicense` records the earlier GPL
grant. The separate relicense record adds the exact scope and pre-grant hashes
of `app_main.c`, `descriptors.c` and `vendor_overlay.py` at
`cf4418fcc947b5ac96f2c1320a05e84373469ae7`. Historical hashes intentionally do
not match the source after license-header changes.

The reviewed USB/audio implementations use the SDK/USB interfaces and
project-owned bridge code. No RetroFM, mdxtools or MDX engine body was identified
in this isolated closure. The earliest implementation revisions and sole-author
Git evidence are enumerated in RELICENSE.md. Unrelated GPL engine code and the
official M-VAVE APP remain outside the grant.

## External Apache components

The pinned JieLi AC79 SDK at
`e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d` has an Apache-2.0 root license;
individual SDK components retain their applicable notices and terms. The SDK
remains external and is not relicensed or bundled by this notice.

`vendor_overlay.py` includes SDK context and PeakRacing NES fragments under
their existing Apache-2.0 terms. Retain PeakRacing's `Copyright PeakRacing`
and applicable upstream notices. Generated SDK/core files keep their original
headers and identify the FM-1 overlay modifications. The current grant covers
the project-owned overlay contributions, not ownership of those fragments.

## Implementation and validation boundary

The FM-1 Forge profile reuses the fixed UAC1 descriptor and bounded
playback/capture queues; it captures the diagnostic synthesizer rather than
bundling any ROM, song, WAD or sound bank. The microphone-controller hook remains
unused. Host tests cover the descriptor/bridge/target and packet code; target
link audits additionally check RAM ownership, nonblocking packet instructions,
setup policy and confirmed UBOOT shutdown. This license-only change leaves
executable source bodies unchanged. Physical USB audio operation remains a
bench acceptance check.
