# DUST // Beyond the Horizon — Game Design

> You wake in the sand. Nobody explains why. The horizon is the only instruction you get.

---

## 1. The one-sentence version

An exploration game in a 100 km² desert where there are no markers, no minimap
and no quests, and where every system exists to make the next horizon worth
walking to.

## 2. Pillars

These are not aspirations. Each one is enforced by a system, and each system
names the pillar it serves.

### 2.1 Curiosity over instruction

The world pulls. It never pushes.

If a player is walking without a reason, that is a **defect in the curiosity
director**, not a player failure. `UDustDiscoverySubsystem` scores everything in
the player's horizon by significance, novelty, bearing and real terrain line of
sight. Below the floor, the player is in a *dead zone* — a logged, measurable,
build-failing event that the offline validator sweeps the entire map for.

The intervention is deliberately weak: raise the silhouette of the nearest
unresolved thing by at most 2.5× so the player might notice it. We never spawn
content under the player, never move anything, and never draw a marker. The
world does not change; the player's chance of noticing what is already there does.

### 2.2 Legibility at distance

Silhouette is the only navigation tool in the game.

Every region has a landmark readable from 4 km. Every landmark is placed by
topographic prominence, not by random scatter — `FDustPoiGenerator` requires
90 m of prominence measured over a 1.2 km ring before it will place one. This
is why DUST's landmarks end up on ridges you can actually see, and why the
player can navigate by saying "the tilted one, to the left of the notch".

The scalability ordering follows from this: we give up resolution and particles
before we give up view distance and shadow definition, because a soft silhouette
costs the player their way home.

### 2.3 Forgiving survival

Thirst and stamina shape routes. They do not punish.

- You cannot die of thirst. `bForgivingSurvival` floors hydration above zero.
- Exhaustion locks out sprinting, never walking.
- Every vital recovers on its own, given time and shade. Nothing requires an
  item you might not have found.
- The worst case is a 35% stamina ceiling and a desaturated image — deeply
  inconvenient, entirely survivable.

The design test: a player who makes every bad decision available to them should
arrive somewhere slowly and uncomfortably, never be sent back.

### 2.4 Every frame a wallpaper

Composition, contrast and scale are budgets, not afterthoughts.

The camera is built as a lens — focal length, sensor width, aperture — rather
than as a FOV slider, so depth of field and perspective compression relate the
way a photographer expects. Photo mode is a camera, not a screenshot button.

### 2.5 Silence is content

The audio director spends silence deliberately: no more than 35% of any
twenty-minute window may carry music, with a hard 75-second floor between cues.
A declined cue results in silence, never a fallback. This is why, when something
does play, it lands.

---

## 3. The loop

There is one loop, and it is very short:

```
      notice something on the horizon
                  ↓
        decide whether to go
                  ↓
      the walk there costs something
      (time, water, stamina, daylight)
                  ↓
         arrive, and find out
                  ↓
    what you find reframes something
    you already saw, or shows you
    somewhere new on the horizon
                  ↓
              (repeat)
```

Everything in the game is in service of one of those five steps. If a proposed
feature does not strengthen one of them, it does not go in.

### What "find out" can mean

| Kind | Example | Frequency |
|---|---|---|
| Landmark | The tilted spire; a wrecked vessel on a ridge | ~120 in the world |
| Ruin | A wind-scoured foundation, half-buried | ~700 |
| Inscription | A carved panel, readable, adding one piece | ~900 |
| Artefact | A carryable object with narrative weight | ~450 |
| Cache | Concealed supplies or equipment | ~300 |
| Creature | A first sighting, or a behaviour witnessed | 19 species |
| Vista | An authored composition point | ~90 |
| Anomaly | Rare, unexplained, deliberately unresolved | ~30 |

Counts are produced by `UnrealEditor-Cmd Dust.uproject -run=DustWorldBake -mode=stats`
and are a build artefact, not a guess.

---

## 4. Traversal

Movement is the entire interaction vocabulary. Two rules govern all of it.

**Never refuse silently.** If the player presses traverse at a ledge they nearly
reached, they get the mantle. Probes are generous and tolerances wide, because a
move that *should* have worked and did not is far more corrosive than one that
arguably should not have.

**Never take control away.** No cinematic traversal animations. Mantles are
instant and interruptible. The player can always let go, always bail, always
change their mind mid-move.

### The move set

| Move | Notes |
|---|---|
| Walk / sprint | Sprint drains stamina; 22 s from full |
| Crouch | Halves fauna detection radius — a real stealth tool |
| Jump | Fallback when no context action applies |
| Mantle | Up to 220 cm, cost scales with height |
| Vault | Up to 95 cm, preserves momentum through the move |
| Free climb | Rock, worked stone, clay, scoria — **never sand** |
| Dune slide | Auto-triggers cresting a dune at speed; steerable |
| Swim | Rehydrates passively; no drowning |
| Rope | Deployed, persistent, re-usable |
| Paraglider | ~4:1 glide, gains altitude on real ridge lift |
| Sand board | Requires loose depth; faster than sliding, less steerable |
| Hoverbike | Ground-effect vehicle; loses grip in churned sand |

### Why sand cannot be climbed

Excluding loose sand from climbable surfaces is the single most important
traversal decision in the game. The whole point of a dune is that you cannot
simply go up it. If the player could climb dune faces, the dune sea would stop
being terrain and become scenery, and the traversal design of the largest region
in the world would collapse.

### The context button

One button does jump, mantle, vault, start climb, grab rope. `UDustTraversalComponent`
decides from the probe. The HUD prompt names the *move*, never the button — the
player knows which button they are holding; what they need is to know what will
happen.

---

## 5. Survival

Three vitals, all in `UDustVitalsComponent`.

### Stamina

Spent on sprinting, climbing, mantling. Recovers after a 1.1 s delay, and
recovers *faster the longer you rest* — which rewards stopping to look at
something, exactly the behaviour the whole game encourages.

Exhaustion uses hysteresis (enter at 2%, exit at 25%) so the player cannot
stutter-tap sprint at zero.

### Hydration

Reaches "parched" in ~3.5 in-game hours of ordinary daytime walking. Heat and
exertion multiply the rate up to ~3×. Swimming and standing in water rehydrate
passively — no prompt required to survive next to a lake.

Hydration's only mechanical effect is the **stamina ceiling**. That is the entire
consequence, and it is enough: a parched player is slow, cannot sprint far, and
will start planning routes around water without ever being told to.

### Heat

The interesting one, because it is a *place* rather than a clock.

Thermal load depends on ambient temperature, sun angle, whether terrain is
casting shade on you (real occlusion, marched against the height field), wind
speed, and whether you are in water. Shade is worth about 8 °C.

The diurnal curve peaks two hours after solar noon because ground keeps
absorbing after the sun starts descending. This thermal lag is why **mid-afternoon**
is the punishing part of a DUST day, not noon, and why late-afternoon shade is
worth walking to.

Body temperature has inertia — minutes, not seconds — so stepping in and out of
shade does not flicker every dependent system.

---

## 6. Progression

There are no levels, no skills, no upgrades. Progression is made of three things:

1. **Knowledge.** Which region has water. Where the migration crosses. That
   approaching downwind works. None of this is written down by the game.
2. **Equipment.** Board, glider, rope, hoverbike — each found, each opening
   routes rather than gating them. Everywhere reachable with the glider is
   reachable without it, slower.
3. **Understanding.** The story is assembled, not delivered. See `NARRATIVE.md`.

The design rule: **nothing is ever locked**. Equipment is a shortcut, never a
key. A player who finds nothing can still reach everything.

---

## 7. Failure

DUST has no failure states.

- No fall damage. The world is meant to be climbed on and jumped off.
- No drowning.
- No death from thirst, heat or cold.
- No combat, therefore no combat death.
- Territorial animals warn before they escalate, and a player who reads the
  warning is never hurt.

What replaces failure is **cost**: time, daylight, water, and the distance back.
Losing a climb because you ran out of stamina means falling, standing up, and
finding another way — which is a better beat than a reload.

---

## 8. Session shape

A session is expected to be 40–90 minutes. The systems are tuned around it:

- 24-minute day cycle: noon is a state you live in, not a moment you pass through.
- 180-second autosave, deferred until the player is somewhere reasonable to
  reload from — never mid-glide, never mid-climb.
- Weather typically holds 4 in-game hours (~4 real minutes) with wide variance.
- The lunar cycle is 29.5 days, so moon phase changes noticeably *across*
  sessions rather than within one.

---

## 9. What DUST is not

Stated explicitly, because each of these has been proposed and rejected:

- **Not a survival crafting game.** No inventory management, no building, no
  hunger bar to feed. Thirst is the only consumable pressure and it is forgiving.
- **Not a combat game.** No weapons. Predators hunt other fauna; the player can
  watch.
- **Not procedurally infinite.** The world is a fixed 100 km², authored where it
  matters, generated where it does not, and identical for every player. Two
  players can talk about the tilted spire and mean the same rock.
- **Not a walking simulator.** There is friction: heat, water, terrain, wind,
  stamina, darkness. The walking has weight.

---

## 10. Related documents

| | |
|---|---|
| [`WORLD_BIBLE.md`](WORLD_BIBLE.md) | The twelve regions and what each is for |
| [`NARRATIVE.md`](NARRATIVE.md) | The Vessel Age and the rules of environmental story |
| [`ART_BIBLE.md`](ART_BIBLE.md) | Look targets, lighting, composition |
| [`TECH_DESIGN.md`](TECH_DESIGN.md) | System architecture and ownership |
| [`AUDIO_DESIGN.md`](AUDIO_DESIGN.md) | The silence budget |
| [`PERFORMANCE.md`](PERFORMANCE.md) | Frame budgets and what we sacrifice, in order |
