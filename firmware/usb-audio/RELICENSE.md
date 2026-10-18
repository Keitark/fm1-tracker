# Scoped Apache-2.0 license grant

Effective **2026-10-18**, Keitark, the copyright owner of the project-owned
portions listed below, authorizes their use under the Apache License, Version
2.0. The owner's instruction was to remove the inherited GPL-3.0-only licensing
from this USB/audio scope. The complete current license is [LICENSE](LICENSE);
attribution and third-party boundaries are in [NOTICE](NOTICE).

This grant applies only to the project-owned portions of these exact paths,
relative to the repository root:

- `firmware/nes/audit_usb_packet.py`
- `firmware/usb-audio/bridge.c`
- `firmware/usb-audio/bridge.h`
- `firmware/usb-audio/profile.c`
- `firmware/usb-audio/target.c`
- `firmware/usb-audio/target.h`
- `firmware/usb-audio/target_fake.h`
- `firmware/usb-audio/test_bridge.c`
- `firmware/usb-audio/test_profile.py`
- `firmware/usb-audio/test_stall.c`
- `firmware/usb-audio/test_target.c`
- `firmware/usb-diag/packet.c`
- `firmware/usb-diag/packet.h`
- `firmware/usb-diag/packet_fake.h`
- `firmware/usb-diag/test_packet.c`
- `firmware/usb-diag/tests/usb_policy_tests.c`
- `firmware/usb-diag/usb_policy.c`
- `firmware/usb-diag/app_main.c`
- `firmware/usb-diag/descriptors.c`
- `firmware/usb-diag/vendor_overlay.py`

The license/provenance documents in this directory record this grant; the list
does not expand to the repository, all firmware, SDK archives or imported assets.
The native USB-MIDI module has its separate Apache-2.0 grant and is not changed
by this notice.

## Authorship and source history

The scoped history was reviewed in the non-shallow `Keitark/fm1-mdx` history
(35 commits through `41410578152eec195b534b3acfe2fa1b10e7476e`) and
`Keitark/fm1-public` history (20 commits through
`d60e1b190e785ef83490ad6716e9c8ecf1e6013d`), together with the current
integration at `cf4418fcc947b5ac96f2c1320a05e84373469ae7`. All commits touching
the 20-path closure identify Keitark as the sole author; no `Co-authored-by`
trailers were found. The source review found no copied RetroFM/mdxtools/MDX
engine implementation in this transport closure. These Git facts support the
owner-authorized grant; Git authorship alone is not a substitute for ownership
of third-party material.

Recorded origins:

| Material | Earliest reviewed origin |
| --- | --- |
| Original CDC policy | `fm1-mdx` `92a23169cea0f9cf0b5fe5bb80c697885e6b4d94`; the corresponding original `fm1-public` CDC source was Apache-licensed at its initial `e6d810a` revision. |
| UAC bridge, profile, target and initial tests | `fm1-mdx` `b13580246ac87a7154fcc32b6777f912aae69928`. |
| Bounded packet implementation and instruction audit | `fm1-mdx` `99bb92cde544a1a37454457d51616dbd2efe5851`. |
| Composite stall test and fixture extension | `fm1-mdx` `c4215344997af2f2ccaa384a1f88b56d0b86ac83`. |
| Composite policy and tests | `fm1-public` `7223075cffc2c663dd15cf0ee108e1da5b66c5fe`. |
| Explicit GPL import and USB integration | `fm1-public` `ca5b24973516bd789ae8cc2df4dfefd1b3a28fb5`; the GPL notices followed the MDX repository's root license. |
| Import into this workspace | `375b255739390c689c1999b0b2ffe12a40ba2f7a`. |

The integration review includes the project-owned CPU0 task/UAC status and
generation-packet changes in `app_main.c`, descriptor class/product changes in
`descriptors.c`, and packet-writer/UAC-registration changes in `vendor_overlay.py`.
The latter also contains SDK context and PeakRacing NES fragments under their
existing Apache-2.0 terms. Their original notices, including PeakRacing's
`Copyright PeakRacing`, remain applicable; generated SDK/core files retain
their upstream headers and identify the overlay modifications.

## Historical releases and other components

Earlier copies and releases were offered under GPL-3.0-only. Those historical
GPL grants remain available and are not withdrawn by this additional Apache
grant. `SOURCE_ORIGIN.json` deliberately preserves the original upstream
repository, commit and all 17 historical import hashes, and separately records
the three integration files at the pre-grant workspace commit. Its historical
hashes must not be regenerated from the current headed source.

The complete historical GPL license remains at
[`../../licenses/fm1-mdx-GPL-3.0.txt`](../../licenses/fm1-mdx-GPL-3.0.txt), because
other imported material still uses it. RetroFM, mdxtools, other GPL MDX engine
code and M-VAVE's official APP are outside this grant and retain their own
licenses and permissions. A combined binary containing GPL components remains
subject to the applicable GPL obligations; this USB/audio grant does not
automatically make that binary MIT- or Apache-licensed.
