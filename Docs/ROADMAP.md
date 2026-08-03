# DUST — Production Roadmap

Where this repository sits, and what turns it into a shipped game.

---

## Where we are

**Phase 1 complete: the engineering foundation.**

Every runtime system exists in C++, documented and modular:

- Deterministic world sampling with twelve terrain profiles
- Migrating dunes, cave fields, POI generation with placement rules
- Persistent sand deformation readable by gameplay
- Astronomy, weather state machine, wind field
- Utility-AI fauna with a two-tier persistent ecology
- The curiosity director and its dead-zone validator
- The audio director and its silence budget
- Traversal, survival, camera, HUD director, photo mode
- Save system, performance governor, upscaler and platform support
- Offline bake, validation and census tooling

**What does not exist:** any binary content. No meshes, no textures, no
materials, no audio, no animation, no levels. The systems are complete and the
game is not.

---

## Phase 2 — First playable region (3 months)

Goal: **one region, end to end, at ship quality.** The Cut (canyon) is chosen
because it exercises climbing, shade, water and enclosure audio without needing
the dune simulation to be finished.

| Workstream | Deliverable |
|---|---|
| Content | Run `DustBootstrap`, author the 12 region assets, hand-tune The Cut |
| Environment art | Rock kit, canyon materials, one landmark, one ruin set |
| Character | Player mesh, first-person arms, full traversal animation set |
| Audio | Footsteps for 4 surfaces, wind bed, the `Wander` music layer |
| Tech art | Ground material with deformation, sand shader, heat shimmer |
| Tools | Landscape bake into the real level; verify sampler/landscape agreement |

**Exit criteria:** 20 minutes of play in The Cut at 60 FPS on target hardware,
with the curiosity validator reporting zero dead zones in the region.

---

## Phase 3 — The vertical slice (4 months)

Goal: **the first two hours of the real game.** Waking place → The Long Erg →
The Cut → first equipment find.

| Workstream | Deliverable |
|---|---|
| Content | Dune sea at quality; the first landmark chain; ~80 POIs |
| Fauna | 4 species fully implemented with animation and audio |
| Narrative | Vessel progression established; ~60 inscriptions |
| Traversal | Sand board and slide fully tuned |
| Audio | Full silence-budget pass; the first `Reveal` cue |
| UX | Settings, rebinding, save/load menus, accessibility pass |

**Exit criteria:** external playtest, 12 players, 2 hours each. Success metric is
not enjoyment — it is that **no player is ever observed walking without a
destination in mind** for more than 90 seconds.

---

## Phase 4 — Full world (9 months)

All twelve regions, all nineteen species, the complete narrative layer.

Milestones by region, in the order players are most likely to encounter them:
Erg → Cut → Standing Stones → Standing Dead → Green Eye → Sink → Flayed Ground →
Ashkarrid → Spine → Cinders → Screaming Flat → Veil → caves.

Running requirements throughout:

- The world validator runs on every commit; dead zones fail the build.
- The soak transect runs nightly; a 3% frame-time regression is flagged.
- Every region ships with its silhouette test passed.

---

## Phase 5 — Polish and platforms (4 months)

| Workstream | Deliverable |
|---|---|
| Performance | Steam Deck certification, minimum spec validation |
| Platform | DLSS / FSR / XeSS integration verified per vendor |
| Console | Platform ports, controller certification, TRC compliance |
| Features | Steam achievements, Cloud, photo gallery UI |
| Localisation | Full pass; note the game has ~900 inscriptions of ≤12 words |
| Accessibility | Motion comfort verification, colour vision, subtitle scaling |
| Audio | Final mix, HDR audio pass, spatial verification |

---

## Phase 6 — Ship and beyond

Post-launch is deliberately narrow. DUST is not a live-service game and will not
become one.

Planned:

- **Photo mode expansion.** More film stocks, a tripod, exposure bracketing.
- **The second seed.** A single alternative world seed, released once, as an
  option on a new game. Because the world is a pure function this is nearly free
  to produce and it is the only replayability feature that fits the design.
- **Bug fixes and platform maintenance.** Indefinitely.

Explicitly rejected: new regions as DLC, cosmetics, multiplayer, a survival
difficulty mode, quest markers as an accessibility option (the curiosity director
is the accessibility answer, and it is already always on).

---

## Team shape

Sized for the work above, not aspirationally.

| Discipline | Count |
|---|---|
| Engineering | 6 (2 gameplay, 2 graphics/tech art, 1 tools, 1 audio) |
| Environment art | 8 |
| Character/creature art | 3 |
| Animation | 3 |
| Design | 4 (2 world, 1 systems, 1 narrative) |
| Audio | 3 (1 designer, 1 composer, 1 implementer) |
| QA | 4 |
| Production | 2 |

~33 people, ~20 months from this foundation. This is a mid-size premium
production, not a AAA-scale one, and the architecture is built to suit that: the
procedural layer exists specifically so that eight environment artists can fill
100 km² instead of eighty.

---

## The risks we are tracking

**Emptiness is the design and also the failure mode.** The Long Erg is meant to
feel sparse. There is a version of it that feels *abandoned by the developers*
instead, and the line between them is thin. The curiosity validator is the early
warning; playtest observation is the real test.

**Determinism regressions.** A careless change to a noise function invalidates
every save in the wild. Mitigated by `AlgorithmVersion` and by a golden-hash test
in CI that samples 10,000 fixed points and compares against a checked-in digest.

**Ecology cost at scale.** 240 tracked creatures is comfortable. The migration
set-piece pushes toward 400. If that does not hold, migrations become a
lower-fidelity, longer-range simulation rather than being cut.

**Steam Deck thermals.** 60 FPS at 800p is achievable but the Deck throttles
under sustained load. The governor handles it; what needs verifying is that a
90-minute session does not visibly degrade over its length.
