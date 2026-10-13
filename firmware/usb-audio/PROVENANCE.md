# Reused USB audio transport

These GPL-3.0-only components are copied from Keitark/fm1-public commit
`d60e1b190e785ef83490ad6716e9c8ecf1e6013d`, whose original USB audio and
single-packet transport identify Keitark/fm1-mdx commit `4141057`.
The license is retained at `../../licenses/fm1-mdx-GPL-3.0.txt`.

The FM1 FW Factory profile reuses the fixed UAC1 descriptor and bounded
playback/capture queues; it captures the diagnostic synthesizer rather than
bundling any ROM, song, WAD or sound bank. The microphone-controller hook
remains unused. Host tests cover the actual copied descriptor/bridge/target
and packet code; target link audits additionally check RAM ownership,
nonblocking packet instructions, setup policy and confirmed UBOOT shutdown.
Physical USB audio operation remains a bench acceptance check.
