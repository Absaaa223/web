# DUST — Art Bible

## The look in one line

Photographic realism, composed like large-format landscape photography, lit by
one enormous light and the ground it bounces off.

---

## 1. Lighting

### One light

There is a sun. There is a moon. There is sky. There is nothing else, anywhere,
except fumarole glow in the Cinders and whatever the player carries.

No fill lights. No rim lights placed to flatter a silhouette. No hidden
practicals. If a shot reads badly it is because the *composition* or the *time of
day* is wrong, and both are things the player controls.

### Bounce is the subject

Sunlight hitting pale sand and bouncing up into everything above it is most of
what the lighting *is*. Undersides of rocks, the player's chin, the belly of an
animal — all lit warm from below. Lumen does this correctly and it is the main
reason the project uses it rather than a baked solution.

Practical consequence: **albedo discipline matters enormously**. Sand at 0.32
albedo bounces plausibly. Sand at 0.55 makes the world glow like a cartoon. Every
ground material is measured against a reference chart, and the validator flags
any base colour above 0.42 luminance.

### The three hours that matter

| Window | Character | Notes |
|---|---|---|
| **Dawn, 05:30–07:00** | Long shadows, warm-cool split, ground fog in basins | The game opens here, always |
| **Mid-afternoon, 14:00–16:00** | Near-vertical sun, shadows collapse, extreme shimmer | Deliberately ugly. It is what makes shade valuable |
| **Golden hour, 18:00–19:30** | Rim-lit dunes, 4 km of visible atmospheric depth | Most screenshots will be from here and that is fine |

Midday is not a failure of the lighting. It is the game's antagonist.

### Night

Genuinely dark. Ambient floor is 0.012 — starlight and airglow only.

A new-moon night in DUST is dark enough to change how the player moves. A
full-moon night on the salt flat is one of the best images in the game. Both
exist because moon phase is modelled on a 29.5-day cycle rather than faked.

The Milky Way is a real band that rotates on a sidereal day and is usable as a
clock. It is suppressed by moonlight and by suspended dust, so it is not always
available — which is what makes a clear moonless night feel like an event.

---

## 2. Composition

### Rules the level art is held to

1. **Foreground, midground, background — always all three.** A dune face at 3 m,
   a rock at 200 m, a mesa at 3 km. Any vista with only two layers reads flat.
2. **One subject.** If a composition has two things competing for the eye, one of
   them is dressed down.
3. **Scale anchor mandatory.** Every large object has something human-sized near
   it. A 400 m mesa with nothing recognisable beside it reads as a 40 m mesa.
4. **Negative space is content.** The salt flat is 90% empty frame and that is
   the design, not an unfinished area.
5. **Silhouette test.** Every landmark must be identifiable as a pure black
   shape against sky. Print it at 5% size; if you cannot name it, redesign it.

### Vista points

Authored composition points, tagged `Dust.Discovery.Vista`, placed where the
three-layer rule resolves particularly well. ~90 across the world.

They are not marked, gated or announced. The player finds them by walking to the
high ground, which is what they were going to do anyway.

---

## 3. Materials

### PBR discipline

Everything is measured. No artist-invented values.

| Surface | Base colour luminance | Roughness | Notes |
|---|---|---|---|
| Loose sand, dry | 0.30–0.36 | 0.72–0.82 | Slight forward scatter at grazing angles |
| Loose sand, wet | 0.17–0.22 | 0.28–0.40 | Darkens ~45%, roughness drops hard |
| Packed sand | 0.28–0.33 | 0.80 | |
| Bedrock, sandstone | 0.22–0.30 | 0.65–0.78 | |
| Salt crust | 0.62–0.70 | 0.55 | The brightest surface in the game |
| Volcanic scoria | 0.05–0.09 | 0.88 | The darkest |
| Worked stone | 0.25–0.32 | 0.42–0.58 | Polish where hands touched it for centuries |

### Sand specifically

Sand is the hardest material in the game and gets the most attention.

- **Grain-scale detail** via a high-frequency normal that fades out by 8 m —
  beyond that it aliases and TSR fights it.
- **Ripples** are geometry from the sampler, not a texture. They exist on the
  windward faces only, at ~2.2 m wavelength, and they fade on slip faces where
  the sand is avalanching.
- **Sparkle** from a low-density specular glint layer, visible only within ~4 m
  and only at high sun angles. It is the single detail that most sells sand as
  a material made of individual grains.
- **Wetness** darkens and smooths, driven by the `SurfaceWetness` parameter
  which lags rain by minutes on the way up and tens of minutes on the way down.

### Deformation

The ground material samples the sand deformation heightfield with world-space
UVs, offsets vertices downward by the stored depth and perturbs the normal from
the gradient. Footprints have a raised rim because the displaced sand has to go
somewhere — without it they read as holes punched in the ground.

---

## 4. Atmosphere

### Heat shimmer

Driven by ground temperature, not sun elevation. This is why air over the salt
flat still boils an hour before sunset.

Applied as a refraction offset in the ground and distant-rock materials, driven
from `MPC_DustSky.HeatShimmer`. Strength ramps from 28 °C to 46 °C.

At full strength, distant objects visibly detach from the ground. That is real,
it is one of the most evocative things about a desert, and we do not tone it down.

### Suspended dust

`Particulate` from the weather MPC drives aerial perspective density, sun disc
softening and a warm shift in the scattering. A hazy day in DUST is not fog —
it is *warm*, because the particles are sand.

### Volumetric clouds

Sparse by default (8% coverage in clear weather). Their real job is not to fill
the sky, it is to **cast moving shadows across the dunes**, which is what gives
the ground its sense of enormous scale.

---

## 5. Scale references

| Object | Height |
|---|---|
| Player | 180 cm |
| Pale Fennec | 30 cm |
| Canyon Grazer | 210 cm |
| Ridgewalker | 340 cm |
| Typical dune | 25–90 m |
| Mesa | 180–400 m |
| Ashkarrid tower stub | 40 m |
| Spine peaks | 2400–3200 m |
| Boundary massif | up to 3200 m |

---

## 6. Colour

The palette is narrow by design. A desert is not colourful, and pretending
otherwise would cost the moments when colour *does* appear.

- **Ground:** ochre through bone, 15–40° hue. Almost the entire game.
- **Sky:** the only source of blue, and it is the primary colour contrast.
- **Salt:** near-white, the brightest thing in the world.
- **Volcanic:** near-black, the darkest.
- **Verdant:** the oasis and the bloom event. Deep, saturated green — used in
  exactly two places in 100 km², which is what makes arriving at the Green Eye
  physically startling.

### The bloom event

After the rare rain, ephemeral flowers cover the Dry Forest within hours and are
gone within a day. It is the only large-scale saturated colour in the game and
most players will never see it.

We are keeping it that way. A thing that 8% of players ever witness, and that
they cannot make happen, is worth more than a thing everyone sees.

---

## 7. Rendering budget

| | |
|---|---|
| Nanite | All rock, ruins, terrain detail meshes |
| Virtual shadow maps | Everything. 6144 page pool at tier 3 |
| Lumen | GI and reflections; hardware RT reflections on capable GPUs |
| Distance fields | Required by Lumen tracing and by fauna nav in unstreamed cells |
| TSR | Default upscaler at 66% |

Triangle budget is effectively unbounded thanks to Nanite; the real constraints
are the VSM page pool and Lumen's card capacity. Both are checked in the mesa
vista test case, which is the worst frame in the game.
