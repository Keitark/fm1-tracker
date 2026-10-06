# Original engine and asset contracts

## Source and private build paths

`fm1-doom/tools/stage_wad.py` prunes and pixelates the original IWAD; `pack_archive.py` builds FMD1. DirectE1M1 uses4096-byte archive blocks/cache and a296KiB fixed Doom zone (`src/i_zone_fm1.c`). Current source framebuffer is160x100 with a160x93 gameplay view, scaled to the240x240 LCD. Smooth8x8 and detailed views are switchable with D-sharp4. Menus use scaled original patch images.

Use the matching profile and generated engine objects; `build_target_candidate.py` checks stale source/header/configuration dependencies and target entry, RAM/heap and emitted stack requirements. Host tests and a linked ELF are separate from actual flash/readback and device acceptance.

## Texture zero is not an ordinary prunable entry

Confirmed bug and fix: `fm1-doom` commit8a36a90,2026-10-06.

Original Doom1.8 TEXTURE1 entry0 is unused AASTINKY, with WALL00_3 patches. Pruning it made BIGDOOR2 become texture index0. The engine treats0 as no texture (`r_segs.c` tests `if(toptexture)`), while closed-door clipping still runs. Door pixels were intact, but drawing was skipped, leaving stale pixels/Hall-of-Mirrors through a blocking door. This was a persistent asset identity bug, not merely low resolution or transient LCD tearing.

Keep the original first TEXTURE1 entry and its patch dependency closure even if no map side names it. Reject an empty table. Regenerate from the original WAD: an archive already pruned cannot recover the missing entry. Ensure the real used door resolves to index>0 after pruning, compaction and PNAMES remapping.

Regression: `tests/test_stage_quality.py` builds an unused first texture with multiple dependencies and a used door. The old pruner fails it. The correction adds only WALL00_3; the ten geometry lumps stay byte identical and existing decoded patch pixels and texture meaning stay unchanged. Private host before/after door captures plus open-door routing established the fix. Later live GAME counters recorded exploration past the spawn door and one kill; full-level completion remains a separate check.

## Column and memory ownership

Review the maintained low-resolution engine transform (`make_lowres_engine.py`) before changing composite texture generation. Keep column work bounded and use the reviewed cache/zone ownership; don't resurrect per-column allocations or attach temporary buffers to persistent engine pointers. Doom zones can fail from fragmentation even when aggregate free memory looks adequate.

`heap_free` in USB telemetry is the SDK aggregate remaining heap, not its largest free block or minimum-ever value. Account static objects, task stacks/queues/control blocks, endpoint state and IIS DMA allocations separately. Private WAD compression reduces flash, not all engine live allocations.

## Gameplay discriminators

EasiestE1M1 still has live enemies (two zombies, two imps in the observed profile); dead marines/gibs near spawn are original map objects. The first door requires F5/use (second white key from right), fire is G5 (rightmost white key). GAME position/sector, total/killed, health/ammo and actual open-door host navigation distinguish a collision/map bug from missing drawing or distant enemies. Do not conclude the map was deliberately restricted because only the spawn room is visible.

The LCD has no verified TE/vblank synchronization, so a transient old/new row split can occur during updates. Persistent missing-door pixels require an engine/asset investigation. CPU/frame progress and zero driver errors do not prove what the user sees.
