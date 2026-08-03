# DUST — Performance

## Targets

| Platform | Resolution | Upscaler | Target |
|---|---|---|---|
| RTX 3070 / RX 6700 XT | 1440p | TSR Quality (66%) | 60 FPS |
| RTX 4080+ | 4K | TSR Quality | 60 FPS, RT reflections on |
| Steam Deck | 800p | FSR Balanced (58%) | 60 FPS locked |
| Minimum spec (GTX 1660) | 1080p | TSR Performance (50%) | 60 FPS, tier 1 |

60 is the floor, not the aspiration. A game whose subject is looking at things
cannot afford to stutter while the player pans across a ridge.

---

## The worst frame

Every budget in this document is measured against one scene, not against an
average:

> **Standing on a mesa top at golden hour during a sandstorm, facing the Green
> Eye 3 km away, with a migration crossing the middle distance.**

That frame has: maximum view distance, maximum shadow-casting geometry,
volumetric fog and cloud at full sample count, peak particulate, ~24 promoted
creatures, and Lumen resolving a valley of bounce light. If it holds 60, the game
holds 60.

---

## Frame budget at 60 FPS (16.6 ms)

| Bucket | Budget | Notes |
|---|---|---|
| Game thread total | 8.0 ms | |
| — Ecology (promotion + coarse sim) | 0.9 ms | 24 promoted, 240 tracked |
| — Curiosity director | 0.3 ms | Hard budget in developer settings |
| — Sampler queries (all consumers) | 0.6 ms | Cache hit rate >92% in normal play |
| — Sand deformation | 0.5 ms | Healing at 10 Hz, dirty-row upload only |
| — Traversal + vitals + camera | 0.4 ms | |
| — Weather + sky | 0.2 ms | Sky recomputes at 15 Hz |
| — Audio director | 0.2 ms | Enclosure at 2 Hz |
| — Engine, streaming, animation, physics | 4.9 ms | |
| Render thread | 7.0 ms | |
| GPU | 15.0 ms | Leaves headroom for the frame-to-frame variance |

---

## The sacrifice ladder

`UDustPerformanceGovernor` gives up quality in this order. **The ordering is a
design decision, not an engineering one.**

| Stage | What goes | Why here |
|---|---|---|
| 1 | Distant particle counts, dust density | Least perceptible at range |
| 2 | Grass and small-detail foliage draw distance | Sparse already; barely noticed |
| 3 | Volumetric cloud and fog sample counts | Degrades smoothly |
| 4 | Shadow map resolution, VSM page pressure | Now it starts to hurt |
| 5 | Lumen final gather quality | Bounce light is the lighting; late in the list |
| 6 | Internal resolution within upscaler comfort range | |
| 7 | Reflection quality; RT reflections off | Rare surfaces, so cheapest to lose last |

**View distance is not on this list.** At 10 km sightlines the silhouette read is
the game's only navigation channel. Softening the horizon costs the player their
way home, so we would rather run at 50% resolution than pull the draw distance
in. This is the single most important performance decision in the project.

### Hysteresis

- Escalate: 1.5 s of sustained overrun, or immediately if below half the target
  frame rate.
- Recover: 8 s of sustained headroom.

Asymmetric on purpose. Quality that visibly pumps up and down is more
objectionable than quality that settles slightly conservative and stays there.

### CPU vs GPU

The governor reads game, render and GPU timings separately. CPU-bound frames
reduce `SimulationScale` (which fauna, curiosity and sand simulation multiply
their budgets by) and foliage density. GPU-bound frames reduce resolution and
lighting.

Engine dynamic resolution alone only solves the GPU case — which, in an open
world with a live ecology, is the *less* common failure.

---

## Memory

| | Desktop | Steam Deck |
|---|---|---|
| Texture streaming pool | 2200 MB (4600 high-end) | 1100 MB |
| Sand deformation field | 2 MB | 2 MB |
| Sampler cache | 0.5 MB | 0.5 MB |
| Creature records (240) | ~30 KB | ~30 KB |
| Save file | ~250 KB | ~250 KB |

The Deck's streaming pool is measured, not guessed: 1100 MB is what remains
after world partition cells, the ecology and the sand field have taken their
share of unified memory.

Sand deformation is the **last thing we cut**. It is 2 MB, it is one of the most
photographed details in the game, and it stays on at every scalability tier
including the Deck.

---

## Profiling workflow

```bash
# Live overlays
dust.debug.world 1        # sampler readout, cache hit rate, dune phase
dust.debug.ecology 1      # tracked vs promoted, regions populated
dust.debug.curiosity 1    # candidate count, score, dead-zone timer
dust.debug.audio 1        # state, music density, enclosure
dust.debug.traversal 1    # probes drawn, state transitions

# Standard engine
stat unit
stat game
trace.start cpu,gpu,frame
```

### Repeatable soak

```bash
Tools/RunWorldSoak.sh
```

Runs the headless server target across a fixed 12 km transect touching every
region, recording frame timings and governor stage changes. Any commit that
raises the mean frame time by more than 3% on the transect is flagged in CI.

---

## Things that have bitten us

Recorded so they are not rediscovered.

**Sampler cache key clustering.** The first implementation used the raw packed
coordinate as the bucket index. Landscape bakes scan in row order, which mapped
every sample in a row onto a handful of buckets and gave a 4% hit rate. Hashing
the key fixed it.

**Dune migration cache invalidation.** Advancing the dune phase invalidates the
entire sample cache. Applying it every frame made the cache useless. It is now
batched and applied once accumulated migration exceeds 1 cm — roughly once a
second — which is far below anything visible.

**Float precision at the world edge.** Casting world coordinates to float before
dividing by the feature scale quantised positions 5 km from origin and produced
visible terrain banding in the far corners of the map. The division now happens
in double precision first. This bug only manifests in the corners and took a
week to find.

**Herd decision lockstep.** Creatures spawned in the same frame all evaluated on
the same frame forever, producing a periodic spike and a herd that moved as a
single organism. Fixed with a per-creature phase offset derived from the actor
name hash.

**Texture region upload use-after-free.** The sand deformation upload originally
staged into a `TArray` whose storage died at the end of scope while the render
thread was still reading it. Now a raw allocation handed to the cleanup lambda.
