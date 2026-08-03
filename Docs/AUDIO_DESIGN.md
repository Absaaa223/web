# DUST — Audio Design

## The budget

> No more than **35%** of any twenty-minute window carries music.
> A minimum of **75 seconds** of silence between cues.

This is enforced in `UDustAudioDirector`, not by discipline. Cues are
*requested*, and a declined request results in silence — never a fallback cue.

### Why

The most common failure in an exploration game's audio is that it is always
playing something. Wall-to-wall ambient beds mean that when a moment deserves a
cue, there is no headroom left: the player has acclimatised and hears nothing new.

DUST is quiet a great deal of the time. That is what makes the moments that
aren't quiet work.

### The override

`ForceCue` exists for the one-time reveal of a major landmark. It bypasses the
budget. Every use spends credibility that the next unforced cue depends on, and
uses are counted in review.

Currently: 11 forced cues in the entire game.

---

## The audio states

| State | When | Character |
|---|---|---|
| `Silence` | Default, and in dead zones | Wind, sand, footsteps only |
| `Wander` | Curiosity level above 0.45 | Sparse. Two or three sustained tones |
| `Approach` | Significant target visible within 300 m | Adds a low pulse. Rising, unresolved |
| `Reveal` | Discovery of significance > 0.6 | The only place the score is allowed to resolve |
| `Threat` | Particulate above 0.55 | Low, physical, more felt than heard |

Note that `Silence` is the state in a dead zone. When the world has nothing to
offer, the game does **not** paper over it with music. Emptiness is allowed to be
empty — and it makes the dead-zone telemetry honest, because a designer walking
the region will actually experience it.

---

## The wind bed

Not an ambient loop. Driven directly from `UDustWeatherSubsystem::SampleWind` at
the player's ear position, which means:

- **Gusts are coherent with the visuals.** The player hears a gust arrive a
  moment before they see it cross the dune ahead, because both read the same
  spatially-varying wind field.
- **Shelter is audible.** In the lee of a dune the wind drops and dulls. A player
  who ducks behind a crest during a storm hears the difference immediately, which
  makes shelter a thing they look for rather than a thing they are told about.
- **Loudness tracks gust speed, not sustained speed.** What you hear is
  turbulence; a steady 8 m/s wind is much quieter than a gusting one.
- **Particulate dulls the high end.** A sandstorm sounds muffled and close rather
  than bright and open.

---

## Space

### Enclosure

`UDustAudioDirector::UpdateEnclosure` samples the terrain skyline in eight
directions at four distances and computes the mean elevation angle. Canyon floor
scores high; dune crest scores near zero.

That single number drives reverb send, wind bed damping and low-pass. **There is
not one hand-placed audio volume in 100 km².** A canyon reverberates because it
is canyon-shaped.

### Footsteps

Surface comes from `FDustGroundSample::Surface` — the same classification that
drives particles and traction, so audio can never disagree with what the player
is visibly standing on.

Ten surfaces, each with its own set. Salt crust is the distinctive one: a brittle
crunch that exists nowhere else in the world and instantly tells the player which
region they are in with their eyes closed.

Stride length scales with speed, so a sprint plays *fewer, longer* strides rather
than the same footsteps faster — which is what running actually sounds like.

---

## Silence as a place

Three locations in the world are designed around near-total silence:

1. **The centre of the Screaming Flat.** Near-zero enclosure, no fauna, and
   during clear weather almost no wind. The player's own footsteps and breathing
   are the entire mix. It is genuinely uncomfortable and it is meant to be.
2. **The deepest cave chamber.** Full enclosure, no wind path, no fauna. Total
   acoustic isolation.
3. **The observation ridge on the boundary massif.** Wind at altitude is
   constant and featureless; after a few minutes the ear stops registering it,
   which produces a kind of silence by adaptation.

---

## Music

### Instrumentation

Small. Bowed metal, a prepared piano, low strings, one breath instrument, and a
single female voice used without words, sparingly.

No percussion anywhere in the score. Percussion implies momentum and DUST is not
a game with momentum.

### Structure

Layers, not tracks. Each layer is a `FDustMusicLayer` with its own fade times —
8 s in, 12 s out by default. Long fades mean the score never announces itself:
the player notices that music is playing some seconds after it started.

### The rule for composers

> Write for the moment forty minutes into a walk when nothing has happened.
> Not for the arrival.

Arrival cues are easy and there are eleven of them. The hard, valuable work is
the sparse `Wander` material that has to survive being heard for the two hundredth
time without becoming furniture.

---

## Diegetic sound as information

DUST tells the player things through audio that it never says in text:

| Sound | What it tells the player |
|---|---|
| Wind pitch rising over minutes | A storm is coming, before it is visible |
| Footprints stop crunching | Surface changed — region boundary crossed |
| Reverb closing in | Terrain is rising around you |
| A Kite calling repeatedly | It has spotted something moving — often a predator |
| Breathing quickening | Distress rising, before the visual grade is obvious |
| Absolute silence in the Flat | You are as far from anything as you can get |

None of these are tutorialised. All of them are learnable.
