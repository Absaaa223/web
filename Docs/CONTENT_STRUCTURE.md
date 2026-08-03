# DUST — Content Structure

The layout of `/Game/Dust/`. Binary content is not in this repository (see
`.gitattributes`), but the structure is fixed and code references it by soft path.

```
/Game/Dust/
├── Maps/
│   ├── L_Dust_Persistent            Persistent level. World partition enabled.
│   ├── L_Dust_Lighting              Sky, sun, moon, atmosphere, cloud actors
│   └── Test/                        Never cooked. Feature test levels.
│       ├── L_Test_Traversal         Every ledge height and slope, labelled
│       ├── L_Test_Materials         Material ball array under real sun
│       ├── L_Test_Fauna             Flat arena for AI observation
│       └── L_Test_Sand              Deformation and slide tuning
│
├── World/
│   ├── DA_WorldComposition          The twelve regions, in order
│   ├── Regions/                     UDustRegionData, one per region
│   │   ├── DA_Region_LongErg
│   │   ├── DA_Region_ScreamingFlat
│   │   ├── DA_Region_TheCut
│   │   ├── DA_Region_StandingStones
│   │   ├── DA_Region_TheSpine
│   │   ├── DA_Region_TheSink
│   │   ├── DA_Region_TheCinders
│   │   ├── DA_Region_StandingDead
│   │   ├── DA_Region_Ashkarrid
│   │   ├── DA_Region_GreenEye
│   │   ├── DA_Region_TheVeil
│   │   └── DA_Region_FlayedGround
│   └── Landmarks/                   Hand-authored POI actors
│
├── Fauna/
│   ├── Species/                     UDustSpeciesData, one per species
│   ├── Blueprints/                  BP_Creature_* pawns
│   ├── Meshes/
│   ├── Animations/
│   └── Materials/
│
├── Weather/
│   ├── Presets/                     UDustWeatherPreset
│   │   ├── DA_Weather_Clear
│   │   ├── DA_Weather_Haze
│   │   ├── DA_Weather_Sandstorm
│   │   ├── DA_Weather_Rain
│   │   ├── DA_Weather_Fog
│   │   └── DA_Weather_Storm
│   └── Effects/                     Niagara systems per condition
│
├── Materials/
│   ├── Collections/
│   │   ├── MPC_DustSky              Contract in TECH_DESIGN.md §10
│   │   └── MPC_DustWeather
│   ├── Ground/                      Sand, salt, rock, clay, scoria, rime
│   ├── Functions/                   MF_SandRipples, MF_Deformation, MF_HeatShimmer
│   ├── Water/
│   ├── Foliage/
│   └── Structures/                  Worked stone, ceramic, corroded metal
│
├── Player/
│   ├── BP_DustCharacter             Derives ADustCharacter
│   ├── Meshes/
│   ├── Animations/
│   └── Equipment/                   Board, glider, rope, hoverbike
│
├── Input/
│   ├── IMC_DustDefault
│   └── Actions/                     IA_Move, IA_Look, IA_Traverse, ...
│
├── UI/
│   ├── HUD/                         Widgets bound to UDustHudDirector
│   ├── Menus/
│   ├── Journal/
│   ├── PhotoMode/
│   └── Fonts/
│
├── Audio/
│   ├── Music/Layers/
│   ├── Ambience/
│   ├── Footsteps/                   One set per EDustSurface
│   ├── Fauna/
│   ├── Weather/
│   └── Mix/                         Submixes, sound classes, HDR audio curves
│
├── Environment/
│   ├── Rocks/                       Nanite, the largest triangle budget
│   ├── Ruins/
│   ├── Ashkarrid/
│   ├── Flora/
│   ├── Debris/
│   └── Artefacts/                   Vessels, primarily
│
└── Developer/                       Never cooked. Per-artist scratch space.
```

---

## Naming

| Prefix | Type |
|---|---|
| `L_` | Level |
| `DA_` | Data asset |
| `BP_` | Blueprint |
| `M_` / `MI_` / `MF_` / `MPC_` | Material / instance / function / parameter collection |
| `SM_` / `SK_` | Static mesh / skeletal mesh |
| `T_` | Texture |
| `NS_` | Niagara system |
| `S_` / `SC_` | Sound wave / sound cue |
| `IA_` / `IMC_` | Input action / mapping context |
| `WBP_` | Widget blueprint |

Region-scoped content carries the region in its name: `SM_Rock_TheCut_Bench_01`.

---

## Rules

1. **Nothing in `/Game` root.** Everything lives under `/Game/Dust/`.
2. **`Developer/` and `Test/` are never cooked**, enforced in `DefaultGame.ini`.
3. **Primary data assets live in the scanned directories** listed in
   `DefaultGame.ini`, or the runtime will not discover them.
4. **Soft references from code, hard references within content.** No runtime C++
   module hard-references an asset.
5. **Source art does not live in `/Game`.** It lives in `RawContent/` and is
   excluded from git in favour of the art depot.

---

## Bootstrap

The commandlet generates the minimum content set that C++ expects, so a fresh
clone can be launched and played:

```bash
Tools/RunCommandlet.sh DustBootstrap
```

It creates the world composition, twelve region assets with tuned defaults, six
weather presets, both material parameter collections and the Enhanced Input
assets. Everything it makes is a starting point intended to be hand-tuned, not a
final artefact — but the game runs immediately afterwards, which is the point.
