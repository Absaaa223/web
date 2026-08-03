# DUST — Coding Standards

Enforced in review. Where these differ from Epic's conventions, DUST wins.

---

## 1. Comments explain *why*

The single rule that matters most.

```cpp
// BAD — restates the code
// Clamp the value to 0..1
Value = FMath::Clamp(Value, 0.0f, 1.0f);

// GOOD — explains the decision
// Hydration bottoms out above zero rather than at it: the design promise is
// that thirst shapes routes and never kills, so the worst case has to remain
// survivable indefinitely.
Hydration = FMath::Max(Hydration, 0.04f);
```

Every magic number gets a sentence explaining where it came from. A tuning
constant with no rationale is a constant nobody will ever dare change.

## 2. Determinism is a contract

Anything that participates in world generation:

- Derives sub-seeds via `FDustNoise::DeriveSeed(Master, TEXT("Channel"))`.
  Never share a seed between systems; never use magic offsets.
- Never calls `FMath::Rand`, `FRandomStream`, or reads engine time.
- Converts world coordinates to noise space by dividing in **double** before
  casting to float.
- Uses hashes of position and kind for stable identities, never sequential ids.

Changing any noise function requires bumping `FDustNoise::AlgorithmVersion`.

## 3. No allocation in hot paths

The sampler, the POI generator and the creature brain all run in tight loops.
Use stack arrays with a compile-time cap (`DustSampler::MaxRegions`) or
`TInlineAllocator`. `TArray` with default allocation in an inner loop is a
review rejection.

## 4. Subsystems own state; components own behaviour

- A subsystem is the single owner of a piece of world state.
- A component reads subsystems and acts on its owner.
- A component never owns state another component needs. If two need it, it
  belongs in a subsystem.

## 5. Cross-plugin communication is by tag

If `DustFauna` needs to know about weather, it reacts to `Dust.Weather.Sandstorm`.
It does not include a weather header. Adding a plugin dependency to make a
feature work is a design smell — check whether a tag would do.

## 6. Thread safety is documented, not assumed

Every class states its threading contract in its header comment. Functions that
must be game-thread-only `check(IsInGameThread())` rather than relying on
callers reading the docs.

## 7. Const correctness is load-bearing

`FDustWorldSampler`'s methods are const because that is what makes it safe to
share across threads. `mutable` is permitted only for caches that are not a
semantic mutation, and each use carries a comment saying so.

## 8. Failure is visible

- Missing content logs an error naming the asset and the tool that generates it.
- A system that cannot function degrades to something obviously wrong rather
  than something subtly wrong. A missing world composition produces a flat plane
  and a loud error, not a crash and not a plausible-looking world.
- Never `check()` on content. Content is authored by people who are not
  programmers, and a crash is not a validation message.

## 9. Naming

| | |
|---|---|
| Classes | `UDustThing`, `ADustThing`, `FDustThing` — always the `Dust` prefix |
| Booleans | `bIsSomething`, `bHasSomething`, `bWantsToSomething` |
| Units | Always in the name or a `meta = (Units="cm")`. `Radius` is ambiguous; `RadiusCm` is not |
| Console variables | `dust.<system>.<Thing>` for gameplay, `dust.debug.<system>` for overlays |
| Log categories | One per major system, `LogDust<System>` |

**Everything is in centimetres.** Every distance in the codebase, every data
asset field, every comment. Mixing units is how a player ends up 100× too far
from a landmark.

## 10. Data assets validate themselves

Every `UPrimaryDataAsset` implements `IsDataValid`. Validation catches design-rule
violations, not just null pointers:

- A species without an evolutionary rationale is **invalid**, not warned.
- A region whose boundary warp exceeds its falloff is **invalid** — it produces
  gaps in the world.
- A creature whose reaction radius exceeds its awareness radius is **invalid** —
  it would react to a player it has not noticed.

These rules exist because they are the ones that get quietly skipped near a
deadline.

## 11. Blueprint exposure

C++ implements; Blueprint composes. Every gameplay system exposes a
`BlueprintPure` read API and `BlueprintCallable` verbs, and no gameplay logic
lives in a Blueprint graph.

Rationale: a designer should be able to build a new interaction without touching
C++, and a programmer should be able to refactor a system without opening thirty
Blueprints.

## 12. Includes

- Headers include the minimum; forward declare wherever possible.
- `.cpp` includes its own header first, then engine, then DUST, each block
  alphabetised.
- Never include an editor header from a runtime module.

## 13. Review checklist

- [ ] Every magic number has a sentence explaining it
- [ ] No allocation added to a hot path
- [ ] Determinism contract respected if generation is touched
- [ ] `AlgorithmVersion` bumped if any noise output changed
- [ ] Threading contract documented if a new class was added
- [ ] Data asset validation covers the new field's design rules
- [ ] Debug console variable added if the system has state worth inspecting
- [ ] No new plugin dependency that a gameplay tag could have avoided
