# DUST // Beyond the Horizon

> You wake in the sand. Nobody explains why. The horizon is the only instruction you get.

An open-desert exploration experience for Unreal Engine 5.5. No quest markers, no minimap,
no objective log. Progress is made of the things you notice.

---

## What this repository is

This is the **engineering foundation** of the game: a complete, modular C++ codebase, the
render/scalability configuration, the developer tooling, and the design bibles that the
content teams build against.

Binary content (`.uasset`, `.umap`, meshes, textures, audio) is **not** in this repository —
it lives in the art depot and is pulled via Git LFS / the studio asset server. Everything
here is text: systems, contracts, data schemas, and documentation. Every gameplay system is
authored in C++ and exposed to Blueprint so designers can compose without touching engine code.

## Layout

```
Dust.uproject
Config/                     Engine, render, input, scalability + per-platform configuration
Source/
  Dust/                     Game runtime module: pawn, traversal, vehicles, camera, UI, survival
  DustEditor/               Editor-only tooling: bootstrap commandlet, validators, world tools
Plugins/
  DustCore/                 Shared foundation: subsystems, save, settings, data assets, dev tools
  DustWorld/                Deterministic world sampling, biomes, dunes, streaming, POI placement
  DustWeather/              Time of day, sky/astronomy, weather state machine, global wind field
  DustFauna/                Utility-AI creatures, ecology, migration, spawn budgeting
  DustNarrative/            Discovery registry, curiosity director, journal, environmental story
  DustAudio/                Adaptive music, wind bed, silence budget, spatial acoustics
Docs/                       Design bibles: GDD, world, art, tech, audio, roadmap
Tools/                      Build, cook and validation scripts
```

## Architecture in one paragraph

The world is a **pure function**. `FDustWorldSampler` maps a world-space XY coordinate to
height, slope, biome weights and surface material — deterministically, from a single seed,
with no allocation and no engine state. Runtime foot placement, creature navigation, POI
placement, the editor's landscape baker and the offline validator all call the *same*
function, so procedural terrain and handcrafted set-pieces can never disagree about where the
ground is. Handcrafted regions are stamped into that function as **region cards** — data
assets that blend an authored heightfield over the procedural base with a falloff, so an
artist can carve a temple valley without forking the terrain pipeline.

Everything else hangs off subsystems that read from that sampler: weather drives a wind field,
the wind field drives dune migration and particle behaviour, dune migration feeds back into the
sampler as a time-varying offset, creatures path over the sampler, and the curiosity director
guarantees that the player's forward horizon always contains something worth walking toward.

See [`Docs/TECH_DESIGN.md`](Docs/TECH_DESIGN.md) for the full system breakdown.

## Design pillars

1. **Curiosity over instruction.** The world pulls; it never pushes. If the player is walking
   without a reason, that is a bug in the curiosity director, not player error.
2. **Legibility at distance.** Silhouette is the primary navigation tool. Every region has a
   landmark readable from 4 km.
3. **Forgiving survival.** Thirst and stamina exist to shape routes, not to punish. Failure
   costs time and comfort, never progress.
4. **Every frame a wallpaper.** Composition, contrast and scale are gameplay-visible budgets,
   not post-process afterthoughts.
5. **Silence is content.** The audio director spends silence deliberately.

## Getting started

```bash
# Generate project files (Windows)
Tools\GenerateProjectFiles.bat

# Generate project files (Linux/Mac)
./Tools/GenerateProjectFiles.sh

# One-time: create the Enhanced Input assets, material parameter collections
# and default data assets the C++ modules expect.
Tools/RunCommandlet.sh DustBootstrap
```

Then open `Dust.uproject`, load `/Game/Dust/Maps/L_Dust_Persistent`, and press Play.
Console command `dust.debug.world 1` overlays the live sampler readout.

## Requirements

| | |
|---|---|
| Engine | Unreal Engine 5.5 |
| Compiler | MSVC 19.38+ / Clang 16+ |
| Target | 60 FPS at 1440p on RTX 3070-class hardware with TSR Quality |
| Minimum | 60 FPS at 800p on Steam Deck with the `SteamDeck` scalability profile |

## Documentation

| Document | Contents |
|---|---|
| [`Docs/GDD.md`](Docs/GDD.md) | Game design: pillars, loops, traversal, survival, progression |
| [`Docs/WORLD_BIBLE.md`](Docs/WORLD_BIBLE.md) | The twelve regions, their rules, secrets and fauna |
| [`Docs/ART_BIBLE.md`](Docs/ART_BIBLE.md) | Look targets, lighting, material standards, composition rules |
| [`Docs/TECH_DESIGN.md`](Docs/TECH_DESIGN.md) | Systems, subsystem ownership, threading, performance budgets |
| [`Docs/AUDIO_DESIGN.md`](Docs/AUDIO_DESIGN.md) | Adaptive music, silence budget, spatial model |
| [`Docs/NARRATIVE.md`](Docs/NARRATIVE.md) | The Vessel Age, environmental storytelling rules, the open ending |
| [`Docs/PERFORMANCE.md`](Docs/PERFORMANCE.md) | Frame budgets per platform, governor behaviour, profiling workflow |
| [`Docs/ROADMAP.md`](Docs/ROADMAP.md) | Production phases from this foundation to ship |
| [`Docs/CODING_STANDARDS.md`](Docs/CODING_STANDARDS.md) | C++ conventions enforced in review |

## Licence

Proprietary. All rights reserved.
