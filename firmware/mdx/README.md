# FM1 MDX development

The standalone repository and ongoing build entry point are now
[Keitark/fm1-mdx](https://github.com/Keitark/fm1-mdx), with its local checkout at
`F:\dev\fm1-mdx`. This workspace retains the initial player source and the
shared FM1-NES hardware integration work.

Implemented: bounded RetroFM MDX sequencing, YM2151 stereo synthesis, PDX
mixing, original MDX/PDX demo in read-only flash, track trigger muting, live
keyboard replacement, and USB CDC loading into192KiB volatile RAM. The
sequencer continues sending patch/volume/pan/LFO updates while muted.

Host player/USB/task tests passed. This MDX candidate has not been flashed or
accepted on hardware. Uploaded songs are lost on power-off; persistent song
storage and USB mass-storage support remain separate work.

The known-working NES images and stock/device backups remain in their existing
locations. No physical device operations were performed for this MDX work.
