# Selected game ROM

Downloaded at the user's request on 2026-09-27. Staged locally only; **not
transferred to the FM-1 or executed on hardware**. It is now optionally embedded
in a cross-compiled static library, not a bootable firmware image.

- File: `50-coin-shine.nes` (highest numbered ROM in the checked repository listing).
- Source: https://github.com/GOROman/calude-famicom-game/blob/576a0c249d017e4d339410a2c19739d31778ec6b/roms/50-coin-shine.nes
- Repository revision: `576a0c249d017e4d339410a2c19739d31778ec6b`.
- Size: 49,168 bytes.
- SHA-256: `6cd4e6052a16ce014cbd0f175830b77ebe4b236615409f2156de00179cc8311d`.
- Git blob hash: `9e17e0a355d0c3f245a5d5ba673b001b40db3bca`, matching the GitHub listing.
- Header: `4e 45 53 1a 02 02 31 00 00 00 00 00 00 00 00 00`.
- Format: iNES 1.0, mapper 3 (CNROM), 32 KiB PRG-ROM, 16 KiB CHR-ROM,
  vertical mirroring, no trainer or battery flag.

Compatibility update (2026-09-27): the previous mapper-3 rejection is resolved.
The bounded CNROM implementation supports this ROM's header unchanged.
Two host smoke runs each completed 600 frames with `FM1_NES_OK` (0), 1,200 video
callbacks and 441,000 audio samples:

| Held buttons | Video FNV-1a | Audio FNV-1a | Audio range | Inspected final frame |
| --- | --- | --- | --- | --- |
| None | 699761033 | 921054454 | 0..77 | Title/menu |
| Start (8) | 2787571347 | 747886188 | 0..75 | Gameplay |

Screenshots are in `../build/rom50-idle.png` and `../build/rom50-start.png`;
the host runner's original PPMs are beside them. The optional hash-pinned
`test_optional_rom50_smoke` regression repeats both runs when this ROM is present.
Synthetic diagnostics separately validate graphics bank changes and ROM write
protection. Neither test establishes complete game accuracy or sound quality.
CNROM uses a documented no-bus-conflict legacy-iNES policy (see the project README).

A complete verified FM-1 startup/link/image/recovery path is still required
before transfer to the device. A `.nes` cartridge image is not a JieLi firmware
image. No device write or payload execution was performed for these tests.

`build.py all --rom50` embeds the exact hash-pinned cartridge into the local
host/target libraries and tests that embedded copy. Generated code and artifacts
stay under ignored `build/` directories; the source ROM remains unmodified.

The downloaded binary is ignored by Git. No redistribution license is asserted;
refer to the upstream project and asset terms.
