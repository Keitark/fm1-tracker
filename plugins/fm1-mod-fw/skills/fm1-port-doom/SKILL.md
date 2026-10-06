---
name: fm1-port-doom
description: Maintain the original Doom engine port on FM-1, especially compressed WAD closure, scaled graphics, door rendering, bounded texture columns and engine validation under limited RAM.
---

# FM-1 Doom port

Locate the current `fm1-doom` checkout and review its branch/status and profile. Work from the user's original engine, menu, audio and resolution choices. Read [engine-assets.md](references/engine-assets.md) before altering WAD pruning, compact patches or renderer memory.

Use the real `stage_wad.py` and `pack_archive.py` path with a user-supplied original WAD. Generated banks, compact archives, rendered original art and firmware belong in ignored/private build output. Original Doom game data is not source code licensed for inclusion with the port.

Validate the engine contract implicated by the bug: archive pixel equality alone does not prove texture identity semantics, collision or door drawing. Reproduce with a host render/game-state capture, make a narrow fix and rebuild the current target with source freshness checks. Use [fm1-mod-fw](../fm1-mod-fw/SKILL.md) for board, audio/USB and deployment workflows. Do not claim a local render establishes physical acceptance.
