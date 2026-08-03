# DUST — World Bible

The world is 10 × 10 km, 100 km², one contiguous streamed space with no loading
screens. Twelve regions, each authored as a `UDustRegionData` asset and blended
by `FDustWorldSampler` with noise-warped boundaries so no border reads as a circle.

Each region below states its **design intent** — the one sentence that answers
"why would anyone come here?". A region that cannot answer it does not ship.

---

## The rule every region obeys

> Each region must be identifiable from a single screenshot, at any time of day,
> by silhouette alone.

This is testable and it is tested. If a screenshot of the Badlands can be
mistaken for the Canyon, one of them is wrong.

---

## 1. The Long Erg — `DuneSea`

**Intent:** *The place that teaches you what distance costs.*

The largest region and deliberately the emptiest: 0.6 points of interest per km²
against the Buried City's 14. Crossing it is the game's first real commitment.

- Transverse and barchan dunes, 280 m to 1.05 km wavelength, migrating downwind
  at a rate you can watch over an afternoon.
- Interdune corridors — flat, gravel-floored, ~20% of the surface — are where
  anything interesting has been exposed by deflation. Learning to walk the
  corridors instead of the crests is the region's core skill.
- Slip faces sit at the angle of repose (34°), which means they are exactly at
  the boundary between "slide down" and "cannot climb".

**What you find:** artefacts exposed by the wind, half-buried structures that
were fully buried last week, the first landmark.

**Fauna:** Sandswimmer, Duneglider, Pale Fennec.

---

## 2. The Screaming Flat — `SaltFlat`

**Intent:** *Somewhere so flat and so silent it becomes uncomfortable.*

An evaporite pan. The flattest natural surface possible: polygonal salt crust
with 26 cm of relief across kilometres.

- Perfectly mirrored after rain, which happens rarely enough to be an event.
- Sound behaves strangely — near-zero enclosure means the audio director drops
  reverb to nothing, and the player's own footsteps are the only sound.
- Heat shimmer at its most extreme. Distant objects detach from the ground.

**What you find:** very little, deliberately (0.4/km²). What is here is *strange*
— the anomaly weighting is the highest in the game.

**Fauna:** almost none. That is the point.

---

## 3. The Cut — `Canyon`

**Intent:** *Shade, water, and the first place that feels like shelter.*

A dendritic drainage network incised into a plateau, generated from warped
cellular edge distance with sedimentary benching every 40 m of depth.

- The benches are what make the canyon read as *rock in layers* rather than as
  a smooth gouge, and they are also the climbing route.
- Deep shade for most of the day — thermally the safest crossing in the world.
- Flash-flood debris tells a story about weather the player has not yet seen.

**Fauna:** Ledge-nesting Kite, Canyon Grazer, Nightstalker (predatory).

---

## 4. The Standing Stones — `Mesa`

**Intent:** *Scale. The first time the player feels small.*

Isolated flat-topped remnants above debris aprons, ~45% of cells occupied,
irregular plan outlines.

- Caprock lips are climbable; the aprons are not, which forces route-finding.
- The best paraglider launches in the game. From a mesa top the player can
  cross the Erg in one flight.
- Vista weighting is high — these are composition points.

**Fauna:** Kite colonies on the caprock.

---

## 5. The Spine — `Mountains`

**Intent:** *The hard way, and the long view back over everything you crossed.*

Ridged multifractal peaks with domain warping so the range curves rather than
radiating. Rime above 2.4 km.

- The only region where stamina genuinely governs the route. Climbs need ledges
  to rest on; `UDustTraversalComponent`'s accelerating climb drain makes long
  faces a planning problem.
- Cold at altitude and at night — the other end of the thermal model.
- Above the snow line the surface changes to `Rime`, with different footstep
  audio and no sand deformation.

**Fauna:** Ridgewalker (territorial), high-altitude Kite.

---

## 6. The Sink — `Basin`

**Intent:** *Where everything that ever washed downhill has ended up.*

A broad bowl with a genuine radial drain, so fog pools at dawn and debris
concentrates at the low point.

- The dawn fog is one of the game's set-piece images and it is entirely
  emergent — cold air, low ground, high humidity after rain.
- Highest concentration of washed-in artefacts anywhere.

**Fauna:** transient; the migration route crosses here.

---

## 7. The Cinders — `Volcanic`

**Intent:** *A place that is actively hostile rather than passively indifferent.*

Fractured lava plates with pressure ridges along the fractures, plus sparse
cinder cones with craters punched into their summits.

- Ground temperature offset makes this the worst thermal region in the game.
- Scoria is climbable but loose. Footing is genuinely poor.
- Fumarole vents are the only light source at night besides the sky.

**Fauna:** Ashcrawler, and nothing else.

---

## 8. The Standing Dead — `DryForest`

**Intent:** *Relief. And the realisation that this was not always desert.*

Fossil woodland on stabilised sand sheets. Low rounded hummocks, gentle walking
after the Erg — and that contrast is the entire point of its placement.

- Every tree is dead and has been for a very long time. Nothing regrows.
- After the rare rain event, ephemeral flowers bloom across the whole region
  within hours and are gone within a day. Most players will never see it.

**Fauna:** the richest in the game — Pale Fennec, Grazer herds, Kite.

---

## 9. Ashkarrid — `BuriedCity`

**Intent:** *The moment the world stops being a landscape and becomes a place someone lived.*

Terrain follows the buried street grid: blocks stand proud where rubble
accumulated over structures, streets sit scoured lower where wind has run along
them. The grid is rotated per-region and warped by differential settling.

- **The player reads a city plan from the ground itself, long before they find a
  standing wall.** That realisation is the single most important designed moment
  in the game, and it is produced by the terrain function rather than by art.
- 14 POI/km² — by far the densest region.
- Priority 3 in the composition, so it cuts through the Erg cleanly rather than
  averaging into it.

**Fauna:** Pale Fennec (denning in the ruins), Nightstalker.

---

## 10. The Green Eye — `Oasis`

**Intent:** *Water. Safety. The place you plan routes around.*

A spring-fed depression with a vegetation-stabilised rim holding the water in.
Irregular outline, dense flora, standing water.

- The only reliable water in the world. Every migration route touches it.
- Thermally and hydrologically the safest place in the game, which makes it the
  natural base of operations without a single line of UI saying so.
- Validation enforces that the rim is everywhere above the water surface, or the
  lake would spill into the desert.

**Fauna:** everything, at dawn and dusk. The best wildlife watching in the game.

---

## 11. The Veil — `StormBelt`

**Intent:** *A region you can only cross, never inhabit.*

Yardangs — streamlined ridges carved parallel to a wind that never stops,
sampled with 12:1 anisotropy along the prevailing wind axis.

- Weather bias sets sandstorm to 6.0× and clear to 0.05×. Clear sky here is a
  rare event a player will remember.
- The yardangs re-orient automatically if the world's prevailing wind is
  retuned, because they are derived from it rather than authored against it.
- Visibility collapse makes silhouette navigation impossible; the player must
  navigate by wind direction, which they can feel and hear.

**Fauna:** none resident. Migrators pass through fast.

---

## 12. The Flayed Ground — `Badlands`

**Intent:** *Legible geological time. Depth you can read.*

Chaotic rilling in soft banded sediment, quantised into strata bands every 11 m
and blended 42% back toward smooth.

- The banding is what makes badlands legible as *layers*, and gives the player a
  readable sense of how deep they have descended.
- Dense inscription weighting: the exposed rock faces are where the vanished
  civilisation carved.

**Fauna:** Nightstalker, Canyon Grazer.

---

## Subterranean layer

Caves are carved by `FDustWorldSampler::SampleCaveDensity` as intersecting tube
fields rather than thresholded blobs, which matters: tube networks are naturally
navigable, passages connect, dead ends are rare, and the player can always get
back out.

- Depth band: 12 m to 900 m below the surface.
- Vertical scale compressed 0.45× so passages run more horizontally than
  vertically — vertical shafts are dramatic but unnavigable in bulk.
- Entrances taper closed near the surface so caves never slice open mid-dune.

Cave systems are the primary hiding place for the game's most significant
finds, and the only places genuinely dark enough that the player needs a light.

---

## Region layout constraints

Enforced by `UDustWorldComposition::IsDataValid`:

1. No two regions of equal priority may have overlapping cores.
2. `BoundaryWarp` may not exceed `FalloffRadius` — otherwise gaps open between
   regions and fallback terrain shows through.
3. Every region needs at least one landmark-weighted POI category, or it cannot
   be navigated to deliberately.
4. Region ids are save-file keys and may never be renamed after ship.

## The boundary

There is no invisible wall. Terrain rises into an encircling massif starting
2.5 km *inside* the nominal boundary, so the player sees mountains ahead long
before they reach the edge and reads them as geography.

A determined player can climb it. The design rewards them with the observation
ridge and the long view back across everything they have crossed. That is the
correct reward for treating a boundary as a puzzle.
