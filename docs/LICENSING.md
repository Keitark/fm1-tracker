# Licensing boundaries

This workspace is a collection of components, **not one MIT- or Apache-licensed
firmware tree**. Existing upstream licenses remain unchanged. No blanket root
license is added by the public-source preparation. Source availability, build
success and successful device operation do not establish redistribution rights.

The new independently authored USB-MIDI transport and browser-updater candidate
currently uses **Apache-2.0**, scoped by `firmware/usb-midi/LICENSE` and `NOTICE`.
The owner may change independently owned scope before public release, after
authorship/provenance review. A candidate manifest records assertions; it grants
no license beyond the applicable source/license notices. This scoped choice
cannot replace licenses on copied or derived third-party source.

## Current USB/audio owner grant

On 2026-10-18 the owner explicitly requested removal of the inherited GPL-only
licensing from the project-owned USB/audio transport scope. Those portions of
the **exact 20 paths** in [RELICENSE.md](../firmware/usb-audio/RELICENSE.md) are
now offered under Apache-2.0. Retain the component's
[LICENSE](../firmware/usb-audio/LICENSE) and [NOTICE](../firmware/usb-audio/NOTICE).
This includes the bounded packet/UAC code and specified integration contributions,
not every file in their directories or the complete firmware.

The recorded history/source review found sole-author Keitark commits, no
co-author trailers, and no copied RetroFM/mdxtools/MDX or M-VAVE APP engine
implementation in that closure. These are supporting source records, not a legal
certification of every contribution. The SDK and PeakRacing context in
`vendor_overlay.py` already has Apache-2.0 terms; original notices, including
`Copyright PeakRacing`, remain applicable. Generated copies retain their upstream
headers and identify the project modifications.

Earlier GPL grants/releases remain available. The historical pins and 17 import
hashes in `firmware/usb-audio/SOURCE_ORIGIN.json` remain immutable; the current
Apache grant and three integration origins are recorded separately. Do not
rehash historical entries or remove the retained GPL license used by unrelated
components. The official M-VAVE APP, RetroFM/mdxtools and other GPL engine code
are outside this owner's USB/audio grant.

## Current update backend and external SDK

The new USB-MIDI backend targets the existing JieLi AC79 SDK at
`e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d` and its `update.a` APIs. It does
**not** link M-VAVE's proprietary `sdk/lib/libfm1_engine.a`. The official M-VAVE
APP/engine exception discussed below is therefore a boundary for a possible
future import, not a present dependency of this new source candidate.

The old SDK's root `LICENSE` is tracked Apache-2.0 at that exact revision
(Git blob `261eeb9e9f8b2b4b0d119366dda99c6fd7d35c64`), although this local
sparse checkout marks it skip-worktree and omits it on disk. Its `README-en.md`
also states Apache-2.0. Individual components retain their own notices: for
example `include_lib/system/os/POSIX/LICENSE` is MIT/FreeRTOS. The source-only
candidate does not copy SDK archives; distributing a linked image still requires
identifying all inputs and preserving their applicable component notices.

## Preserve inherited terms

| Material | Required boundary |
| --- | --- |
| M-VAVE `fm1_open` APP, pinned `b178ac43c25f010c60a878056fb5331fe693fd62` | GPL-3.0-only with its **exact** FM1 binary-SDK linking permission. Retain upstream `LICENSE.md`, `THIRD_PARTY.md`, notices and required corresponding APP source/build/install scripts. |
| M-VAVE proprietary engine SDK | Separate FM1-device permission in upstream `THIRD_PARTY.md`. It allows specified binary uses/redistribution with notices; it is not an MIT/Apache open-source engine. |
| M-VAVE's listed boot/update/recovery resources | Upstream explicitly licenses specified resources under Apache-2.0. Use the identified upstream bytes and notices; this is not permission to publish private device readbacks. |
| Project-owned portions of the exact 20 USB/audio paths listed in `firmware/usb-audio/RELICENSE.md` | Current Apache-2.0 owner grant; retain component LICENSE/NOTICE, upstream Apache notices and immutable historical GPL import records. No blanket repository grant. |
| PeakRacing NES core | Apache-2.0. Retain the complete license and relevant upstream notices, and identify modifications. The current core is an external dependency. |
| MIT libraries/tools and Apache OpenAI SDK | Preserve each component's own license/copyright and applicable NOTICE material. They do not set the license of every application file. |
| Original FM1BREAK assets explicitly named by `firmware/protracker/samples/ASSET_LICENSE.md` | CC0-1.0; that dedication does not cover software, imported MODs, ROMs or unrelated generated voices. |

The official APP exception is a GPLv3 section 7 additional permission for code
**marked with that exception**, linked with the identified binary SDK components
in its manifest. APP modifications remain GPL-covered; SDK components keep their
own terms. It does not grant an exception for unrelated GPL components, nor
permission in arbitrary third-party code. A future combination of that proprietary
engine with other GPL-covered engine code needs its own compatibility/rights
review. The current USB/audio scope has an owner-authorized Apache grant, and the
current new backend uses the old JieLi SDK rather than M-VAVE's engine. Do not
copy the official exception onto unrelated files without authority.

## Loader and recovery resource notice

The audited official resource grant is specifically in
[M-VAVE's boot/update resource terms at the pinned revision](https://github.com/m-vave/fm1_open/blob/b178ac43c25f010c60a878056fb5331fe693fd62/THIRD_PARTY.md#bootupdate-resources-and-recovery-firmware).
It identifies `sdk/tools/pack/{uboot.boot,ota.bin,cfg_tool.bin,boot-config.bin}`
and `sdk/firmware/FM-1_015.fwsc` as Apache-2.0. The initial source export includes
none of those binaries. A later package containing any of them needs their
exact source pin/hash, the full Apache license, applicable attribution/NOTICE
material and modification identification; the APP GPL license does not replace
these resource-specific terms. Do not copy the entire official mixed-component
notice into independent transport source as though every listed component were
included. A license for an official resource also does not clear a private dump.

## Historical USB/audio GPL notices

The protocol standards do not require GPL. The recorded source chain is:

1. `Keitark/fm1-mdx` at `41410578152eec195b534b3acfe2fa1b10e7476e` carries a
   root GPLv3 license. Its USB bridge/profile/packet implementations have no
   separate external copyright or alternative license header at that revision.
2. `Keitark/fm1-public` at `d60e1b190e785ef83490ad6716e9c8ecf1e6013d` explicitly
   records reusing those MDX USB sources under GPLv3 and adds GPL-3.0-only SPDX
   headers.
3. This workspace initially retained those notices, provenance hashes and the
   full license in `licenses/fm1-mdx-GPL-3.0.txt`. The current 20-path owner grant
   is recorded separately in `firmware/usb-audio/RELICENSE.md`.

The MDX repository's initial `92a2316` commit also contains GPLv3-licensed
`firmware/mdx/vendor/retrofm/COPYING` and `vendor/mdxtools/LICENSE`; the combined
MDX player therefore has genuine GPL dependencies. UAC source was added later in
`b135802`, and bounded packet code in `99bb92c`. Those USB implementations use
SDK/USB APIs and project bridge headers; this focused review did not identify a
copy from those GPL MDX engines in the isolated transport files.

That was inherited project licensing, not a requirement of USB/UAC. The subsequent
20-path review and explicit owner instruction support the current scoped Apache
grant above. Historical GPL releases are not withdrawn, and outside contributions
keep their own terms; the grant is not a claim to own upstream SDK/core code.

## Public preparation

Use the narrow [candidate exporter](../tools/public-release/README.md), which
requires an exact path/SHA256/license/origin inventory and exports no Git history.
The initial scope is new `firmware/usb-midi/` source only. All exports remain
**candidate-unreviewed** until a person reviews the exact content, license files,
provenance and sensitive-data boundary. No public repository is created by this
step. See the [dated audit](PUBLIC_SOURCE_AUDIT_20261018.md) for exclusions and
remaining review work.
The separately relicensed USB/audio closure is still excluded because the export
is a bounded MIDI snapshot; its wider build/dependency closure has not been added.

Primary terms: [MIT](https://opensource.org/license/mit),
[Apache-2.0 section 4](https://www.apache.org/licenses/LICENSE-2.0),
[GNU FAQ on the original copyright holder's license choices](https://www.gnu.org/licenses/gpl-faq.html#ReleaseUnderGPL),
[official APP license at the audited pin](https://github.com/m-vave/fm1_open/blob/b178ac43c25f010c60a878056fb5331fe693fd62/LICENSE.md),
[official component terms at that pin](https://github.com/m-vave/fm1_open/blob/b178ac43c25f010c60a878056fb5331fe693fd62/THIRD_PARTY.md).
