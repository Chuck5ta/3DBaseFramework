# Wild West Frontier in Game3D — what's missing

> Written 2026-11-18; updated 2026-11-19 (the move is decided). Framework state compared against: `GAME3D_AI_BRIEFING.md` of
> 2026-11-17. If the framework has moved on since, check the briefing's *Known limitations*
> section before trusting this list.

## Why this document exists

Wild West Frontier (WWF) is the owner's game, on Steam and still in development, currently
built in Unreal Engine 5 with characters and props bought on Fab. **Decided 2026-11-19: WWF
will be made in Game3D**, starting as soon as the owner can; the Unreal version is paused.
This document lists what WWF needs that Game3D doesn't have yet, so each gap can be built
as its own stage.

## What Game3D already gives WWF

- Code the owner owns and understands, with no engine bugs that can't be fixed — the
  problem that ended Outlaws of the Old West.
- A small, readable codebase, which suits AI-assisted development well.
- Good performance on modest hardware such as the Steam Deck; no royalties.
- Already in place: Vulkan forward renderer, glTF models with skinning and animation
  cross-fades, normal maps, instancing, frustum culling, sun shadows, third-person
  camera, 3D audio with effects, input actions with rebinding and gamepad, game states,
  crash-safe JSON saves, Dear ImGui debug UI.
- CharacterCreator: modular characters on a shared skeleton, with detail levels, usable
  as an in-game addon.

## The gaps

Rough size: **S** = a stage or two, **M** = several stages, **L** = a large project of its own.

| # | Feature | Why WWF needs it | Game3D today | Suggested approach | Size |
|---|---|---|---|---|---|
| 1 | **Physically based materials (PBR)** | Fab characters and props are authored for PBR; they look wrong under Blinn-Phong | Blinn-Phong with roughness/metallic shaping the highlight; no metallic-roughness or occlusion textures | Metallic-roughness glTF materials, image-based lighting from a sky probe, tone mapping | M |
| 2 | **Large outdoor terrain with streaming** | Open frontier land, towns, trails | No terrain; every object is tested every pass (no spatial structure) | Heightmap terrain in chunks with LOD, texture splatting, a spatial grid, streaming chunks in and out around the player | L |
| 3 | **Foliage** | Grass, scrub, cacti, trees across the land | Instancing works (800-tree demo) but copies share material and pose | Builds on instancing: per-chunk scatter, distance fade, wind sway in the shader, per-copy tint | M |
| 4 | **Physics** | Collisions, falling, ragdolls, thrown objects, horses | None; the demo pushes the player out of circles | Integrate Jolt Physics (the briefing's recommendation) | M |
| 5 | **Character controller** | Walking on slopes and stairs, through doors, not through walls | None | Jolt's CharacterVirtual once physics is in | S |
| 6 | **AI navigation** | NPCs, outlaws and lawmen finding paths through town and terrain | None | Recast/Detour navmeshes, baked per chunk; steering on top | M |
| 7 | **Layered animation** | Walk and shoot at once, aim while riding | One animation per animator plus cross-fades; no per-body-part layering | Upper/lower-body masks, additive layers, aim offsets; animation events | M |
| 8 | **Level editor** | Placing buildings, props, roads and NPCs | Levels are made in code; no scene/entity system | A scene/entity format first; then either an in-game ImGui editor or Blender as the editor with an export script | L |
| 9 | **Networking** *(only if WWF is multiplayer)* | Co-op or PvP | None | A client-server model with a library such as GameNetworkingSockets (Steam) or ENet; affects how gameplay code is written, so decide early | L |

### Smaller gaps that would also show

- **Shadow cascades** — one shadow map stops at its distance; open landscapes need cascades.
- **Skybox / sky and time of day** — the fog-coloured clear colour stands in today.
- **Anti-aliasing** — none yet; noticeable on fences, wires and distant buildings.
- **More than one light** — only the sun lights the scene; lamps, fires and muzzle
  flashes need point lights.
- **Scene / entity system** — needed by the level editor, saves and networking alike.
- **Save games** — `core::SaveData` writes JSON safely, but saving the world state is game code.
- **Asset pipeline from Fab** — many packs ship only as Unreal assets. Each needs its
  source FBX (or an export from UE), conversion to glTF, and its materials rebuilt. Check
  each pack's licence: the Fab Standard License allows any engine, except packs marked
  *UE-Only Content* and packs still under the legacy UE Marketplace licence.

## A suggested order

1. **PBR materials, sky and anti-aliasing** — makes the existing Fab assets look right; quick visual payoff.
2. **Scene/entity system** — the base the editor, saves and networking all build on.
3. **Physics (Jolt) and the character controller.**
4. **Terrain, then foliage, then streaming.**
5. **Layered animation.**
6. **Navigation.**
7. **Level editor.**
8. **Networking** — but if WWF is multiplayer, design for it at step 2, since it changes
   how gameplay state is held.

## A low-risk way to decide

Keep WWF in Unreal for now and build one small vertical slice in Game3D: for example a
single street of the town, a few Fab props converted to glTF, and a couple of
CharacterCreator characters walking about. It needs only steps 1 and 3 above (plus a
flat ground in place of terrain). That will show:

- whether the look holds up against the Unreal version,
- how much work converting Fab assets really is,
- how it runs on the Steam Deck,
- and whether working this way feels right,

without betting the game on it.
