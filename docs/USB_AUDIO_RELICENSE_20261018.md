# USB/audio licensing correction — 2026-10-18

The owner requested removal of the inherited GPL-3.0-only requirement from the
project-owned USB/audio transport. The exact 20-path current grant is
Apache-2.0 in [RELICENSE.md](../firmware/usb-audio/RELICENSE.md), with the full
[LICENSE](../firmware/usb-audio/LICENSE), [NOTICE](../firmware/usb-audio/NOTICE)
and preserved [origin history](../firmware/usb-audio/SOURCE_ORIGIN.json).

Two focused audits traced all scoped histories to Keitark and found no
RetroFM/mdxtools or official M-VAVE APP implementation in this transport
closure. The overlay does include Apache-licensed SDK/PeakRacing source context;
those notices remain applicable. This grant leaves separate GPL engines,
historical GPL releases, SDK binaries and unrelated firmware outside its scope.
It does not license the whole repository or a combined MDX image as Apache-only.

## Source and generated-file evidence

- All 19 source/test/audit files changed only at their initial license banners
  retain byte-identical implementation bodies relative to `cf4418f`.
- All 17 historical import hashes and all three pre-grant integration hashes
  remain unchanged. Current Apache terms are recorded separately from the old
  GPL release terms.
- Ten actual SDK/PeakRacing overlay cases produce the same transformed C text
  as before, apart from the one replaced license banner. Generated files now
  explicitly identify FM-1 modifications and retain original upstream text.
  `#line 1` preserves their logical source line numbering.
- Factory build provenance now records the active Apache LICENSE, NOTICE and
  grant instead of presenting the archived GPL license as its current USB grant.

Local proof receipts are `tmp/usb-audio-relicense-body-proof-20261018.json` and
`tmp/usb-audio-overlay-license-proof-20261018.json`. They are validation evidence,
not files to bundle with public firmware or device data.

## Existing validation

The existing USB/audio CMake build and all **33** CTest checks passed. The five
existing Python transport/overlay checks passed. A fresh offline target build:

```powershell
python -B firmware/usb-diag/build.py --controller 0 --factory-diag --usb-midi-update --out tmp/forge-midi-uac-apache-20261018-r8
```

compiled, linked and passed the existing boot/power/USB/packet/MIDI audits.
All eight linked-image regression checks passed with `FM1_MIDI_BUILD` explicitly
set to that r8 directory.

| Field | Post-grant r8 result |
| --- | --- |
| Application size | 161,328 bytes |
| Application SHA-256 | `cd16d9f4f000f1633fcee142d9e7d23a4e4ca5c41a77536063478a21f7684199` |
| Source-derived build digest | `a5eae551481664e75c62f85515a6ab6c8ec58f2ae75ad394d7a61b91965135f3` |
| Previous r7 application | `c412f2d45d7dd5edc887badae6465f298e208826a73bc79ad2340feb4e46267b` |
| Comparison with r7 | Exactly 32 bytes differ, all in the build digest at application offset 118,531. The rest of the application is byte-identical. |
| Installation gate / hardware acceptance | Still disabled / unverified. |

No device was opened, flashed, reset or clock-adjusted. The licensing correction
does not qualify the OTA loader, authorize installation or change physical
acceptance requirements. The separate public-source export remains limited to
the MIDI module; this grant does not silently add SDK-dependent USB/audio files
or private history to that export.
