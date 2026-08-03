# DUST — Technical Design

## 1. The central idea

**The world is a pure function.**

`FDustWorldSampler::SampleGround(FVector2D)` maps a world coordinate to height,
normal, slope, surface, region weights and loose-sand depth. No allocation, no
engine state, no RNG streams, no globals. Deterministic across platforms and
processes.

Four consumers need that answer, and they run in completely different contexts:

| Consumer | Context |
|---|---|
| Landscape heightmap bake | Cook commandlet, headless, no world |
| Runtime foot placement, prop settling, vehicle suspension | Game thread |
| Creature navigation and coarse ecology | Worker threads, unstreamed cells |
| POI generation | Anywhere, before the terrain has ever been loaded |

If any two disagree, the failure mode is a player falling through the floor.
Making the answer a pure function removes the entire class of bug: there is no
state to get out of sync.

Everything else in DUST hangs off that property. The save file stores a seed and
a delta rather than a world snapshot. Photo mode can autofocus on terrain 6 km
away that has never been streamed. The game mode can evaluate 200 candidate
waking positions with line-of-sight tests before any geometry exists. None of
that is possible if the world lives in loaded actors.

### Consequences we accept

- **`FDustNoise::AlgorithmVersion` is content-invalidating.** Change any noise
  function and baked landscapes, POI layouts and existing saves all become
  invalid. Saves record the version and refuse to load rather than dropping the
  player inside a mountain.
- **We do not use `FMath::PerlinNoise` or `FRandomStream`.** Both have changed
  behaviour across engine versions, which would silently reshape a shipped world
  on an engine upgrade.
- **Dune migration is the one piece of mutable state**, and it enters the maths
  as a scalar offset along the wind axis, so the world stays reproducible from
  `(seed, phase)` without storing a heightfield.

---

## 2. Module map

```
Plugins/
  DustCore        Noise, tags, settings, save, performance governor, user settings
  DustWorld       Sampler, regions, dunes, caves, POI generation, sand deformation
  DustWeather     Astronomy, weather state machine, wind field
  DustFauna       Utility AI, ecology, two-tier population simulation
  DustNarrative   Discovery registry, curiosity director, journal
  DustAudio       Adaptive music, silence budget, wind bed, terrain reverb
Source/
  Dust            Player, traversal, camera, HUD director, photo mode, game mode
  DustEditor      Bootstrap commandlet, validators
```

Dependencies flow strictly downward. `DustCore` depends on nothing but the
engine. `Dust` (the game module) depends on everything and is depended on by
nothing — which is what would let a second project reuse this foundation without
inheriting DUST's paraglider.

### Cross-module communication

Systems that must not depend on each other communicate through **gameplay tags**.
The weather subsystem raises `Dust.Weather.Sandstorm`; fauna, audio and the
curiosity director all react without any of them including a weather header.

---

## 3. Subsystem ownership

| Subsystem | Scope | Owns |
|---|---|---|
| `UDustSaveSubsystem` | GameInstance | Save/load, autosave rotation, participants |
| `UDustWorldSubsystem` | World | Sampler, region registry, sample cache, dune phase |
| `UDustSandDeformationSubsystem` | World | Footprint/track heightfield, wind healing |
| `UDustSkySubsystem` | World | Clock, solar and lunar position, temperature |
| `UDustWeatherSubsystem` | World | Weather state machine, wind field, dune transport |
| `UDustEcologySubsystem` | World | Creature records, promotion/demotion, migration |
| `UDustDiscoverySubsystem` | World | Discovery records, curiosity director |
| `UDustAudioDirector` | World | Audio state, silence budget, wind bed, enclosure |
| `UDustHudDirector` | World | Element reveal, attention budget, prompts |
| `UDustPhotoModeSubsystem` | World | Free camera, capture, gallery |
| `UDustPerformanceGovernor` | World | Frame timing, quality sacrifice ladder |

Initialisation order is expressed with `Collection.InitializeDependency<>()`
rather than relying on declaration order.

---

## 4. Save architecture

We save a **seed and a delta**, never a world snapshot.

Persisted: player position and vitals, discovery records, journal entries,
creature ledger, region first-visit times, world state flags, photo manifest,
dune migration phase, weather tag.

Not persisted: terrain (regenerated), POI layout (regenerated), creature
positions (regenerated then advanced by the coarse simulation), weather
transition state (reloading mid-blend is indistinguishable from reloading
settled).

A complete save is a few hundred kilobytes. Steam Cloud sync is instant and a
content patch cannot corrupt a save because there is almost nothing in it that a
content patch could invalidate.

### Participants

`IDustSaveParticipant` inverts the dependency: systems register with the save
subsystem rather than the save subsystem knowing about them. Restore runs in
ascending priority order:

```
  0  World subsystem   terrain must exist
 10  Sky               time must be set before anything reads it
 20  Weather
100  Player character  needs terrain to stand on
150  Discovery
200  Ecology           needs terrain and time
250  Photo gallery
```

### Async writes

Serialisation runs on the game thread (it touches UObject reflection). The byte
array write goes to a background task. An autosave therefore costs well under a
millisecond of frame time, which is why it never hitches.

---

## 5. Threading

| Work | Thread |
|---|---|
| Sampler queries | Any. Const, allocation-free, no engine state |
| POI sector generation | Any |
| Landscape bake | `ParallelFor` over rows, scales linearly with cores |
| Sand deformation impacts | Queued from any thread, applied on game thread |
| Dune phase mutation | Game thread only, behind a fence |
| Creature decisions | Game thread, phase-staggered per creature |

The only mutation of sampler state is `SetDuneMigrationPhase` / `SetPrevailingWind`,
both game-thread-only and both asserting.

---

## 6. Performance architecture

### Sample cache

Direct-mapped, 2048 entries, 25 cm quantisation, hashed bucket index (a raw key
would cluster badly for the axis-aligned scan pattern of a landscape bake). The
hot query pattern — player, shadow, four foot IK traces, vehicle suspension, a
dozen emitters all asking about the same square metre — hits this almost always.

### Two-tier ecology

Most of the desert's animals exist only as `FDustCreatureRecord`: a position, a
species, and needs that keep evolving at 1 Hz. Promoted to real pawns within
240 m, demoted at 1.2× that (hysteresis prevents thrashing). The promoted
creature *continues from* the record's accumulated state, so a herd that walked
to the oasis parched is still parched when it becomes visible.

### The governor

`UDustPerformanceGovernor` watches game, render and GPU timings and trades
quality in a designer-defined order. Escalation is fast (1.5 s hold, immediate
if below half the target rate); recovery is slow (8 s hold). The asymmetry is
deliberate — quality oscillation is more objectionable than a conservative setting.

CPU-bound frames reduce simulation scale and foliage; GPU-bound frames reduce
resolution and lighting. Engine dynamic resolution alone solves only the GPU
case, which in an open world with a live ecology is the less common failure.

---

## 7. Sand deformation

A 1024² R16F heightfield covering 80 m, addressed **toroidally**: when the
player moves east, the column falling off the west edge is cleared and reused as
the new east edge. Nothing is copied, so cost is proportional to distance
travelled rather than texture size, and a footprint stays where it was left in
world space.

CPU-side rather than a GPU ping-pong, deliberately: gameplay needs to **read**
it. Predators follow the player's tracks; the hoverbike loses grip in churned
sand; the wind erases evidence an AI was using. A GPU readback would cost a
frame of latency and a pipeline stall.

Healing is exponential decay with a wind-driven half-life: ~180 s in still air,
~4 s in a sandstorm. Watching your own tracks vanish behind you is the clearest
possible communication of wind strength.

---

## 8. Determinism checklist

Anything that must be reproducible follows these rules:

1. Derive sub-seeds with `FDustNoise::DeriveSeed(Master, "ChannelName")` — never
   share a seed between systems, never use magic offsets.
2. Convert world coordinates to noise space by dividing in **double** precision
   before casting to float. Casting first quantises at the world edge and
   produces banding 5 km from origin.
3. Never use `FMath::Rand`, `FRandomStream` or engine time in generation.
4. Stable identities are hashes of position and kind, never sequential ids.

---

## 9. Editor tooling

`UnrealEditor-Cmd Dust.uproject -run=DustWorldBake -mode=<mode>`

| Mode | Output |
|---|---|
| `heightmap` | 16-bit PNG landscape import tiles, one per 2 km, plus the Z scale |
| `validate` | Impassable pockets, interest deserts, water above rim. Non-zero exit fails CI |
| `stats` | Region coverage, elevation histogram, surface split, POI census |

The validator is how the curiosity promise stops being a matter of discipline:
if more than 2% of the world has nothing within the guaranteed interest radius,
the build fails.

---

## 10. Content that C++ expects

Generated by `-run=DustBootstrap`, then hand-authored from there:

```
/Game/Dust/World/DA_WorldComposition
/Game/Dust/World/Regions/*                 12 UDustRegionData
/Game/Dust/Fauna/Species/*                 19 UDustSpeciesData
/Game/Dust/Weather/Presets/*                6 UDustWeatherPreset
/Game/Dust/Materials/Collections/MPC_DustSky
/Game/Dust/Materials/Collections/MPC_DustWeather
/Game/Dust/Input/IMC_DustDefault + IA_*
```

Paths are referenced by soft path from code so no runtime module hard-references
content.

### Material parameter collection contract

| MPC | Parameters |
|---|---|
| `MPC_DustSky` | `SunDirection`, `MoonDirection`, `SunElevation`, `MoonIllumination`, `MoonPhase`, `StarFieldRotation`, `StarVisibility`, `TimeOfDay`, `Temperature`, `HeatShimmer` |
| `MPC_DustWeather` | `WindDirection`, `WindSpeed`, `Particulate`, `Visibility`, `CloudCoverage`, `RainIntensity`, `SurfaceWetness` |

Materials read these rather than querying subsystems, so sky, ground, flora,
water and particle materials all agree without any of them ticking a blueprint.
