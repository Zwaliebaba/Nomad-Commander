# NC-062 — Battle resolution and the battle record

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | L | no | **yes** | Open |

**Depends on:** NC-060, NC-061, NC-044
**Read first:** GDD §4 (*Execution*: resolved against the admiral's plan; uncertainty mainly from what intelligence got wrong; a small spread), §8 (readability; every receipt names the template), §12 (interception; four classes' combat roles), §3 (19:00–25:00), §7 (fleet-against-fleet combat does not wait for the window); AGENTS.md R16, R18, R19, R24

## Goal

An encounter resolves into a battle fought between two plans: the player's (or an empire's standing doctrine) and the admiral's template, in rounds, with triggers recognized late and executed imperfectly, losses by integer strength with the pinned spread, withdrawal and pursuit by the rules, captures, and a record that is the replay and names the template. The model is the ADR this task writes, and it is owner-visible because it is the feel of combat.

## Deliverables

- `GameLogic/Battle.h` + `.cpp`: `Resolve(World&, const Encounter&, events) -> BattleRecord`: build each side's `BelievedSituation` from its reports (the admiral selects his template from it, NC-060; the company's plan assumptions come from its hypothesis, NC-063); `Tuning::BATTLE_ROUNDS` rounds; per round: evaluate each side's triggers against what it can see of the other (delayed by `TRIGGER_RECOGNITION_DELAY_ROUNDS`, failing with `TRIGGER_FAILURE_CHANCE`), apply the template's posture for the round (a table per template per round phase: opening, middle, closing, with modifiers to strike, defence and objective focus), compute losses per class from strength × posture × spread, check withdrawal thresholds and objective completion, handle the reserve (committed once, never uncommitted), pursuit per the rule; after the last round: outcome, losses, captured hulls (salvage, NC-046), officers recruited from broken fleets (NC-065 reads the record), admiral death chance, `BattleFought` log line.
- `GameLogic/BattleRecord.h`: `struct BattleRecord { tick; system; sides (fleet ids, commanders, template or plan); std::vector<BattleRound> rounds (strengths, triggers recognized and fired, losses); outcome; }` and `WireBattleRecord.h` (the replay; the company sees its own side in full and the other side as its reports showed it).
- Encounters from NC-044 now resolve; covert raids (NC-055) use the same path.
- `GameLogicTests/BattleTests.cpp`: property tests (a doubled force wins the objective more often than not over one hundred seeded battles; a withdrawal threshold of twenty-five percent is honoured within one round's losses; a plan with "heavies appear → withdraw" withdraws when the reserve appears, after the delay, most of the time; the reserve stays committed); the §3 doctrine against Varik's `Ambush` with a reserve produces the §4 receipt's facts when the courier's override arrived and a fight when it did not; determinism under the harness.

## Acceptance criteria

- [ ] The ADR states the model with the tables, and the report states the measured outcome distributions the property tests rely on.
- [ ] Neither side's plan evaluation reads the other side's true counts; the round loop reads `World` only to apply losses (R18; the reviewer checks the two code paths are distinct).
- [ ] Every battle emits its record with the template name and its events with explanations (R19; GDD §8: "every receipt names the template").
- [ ] A battle in open space resolves at the tick of the encounter, never deferred to a window (GDD §7).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

**ADR — the battle resolution model** (owner-visible). The Roadmap's recommendation: twelve rounds, template postures, integer losses with the pinned spread, delayed and imperfect triggers. What it forecloses: real-time tactics (by design, GDD §1), positional simulation, and any per-ship state.

## Out of scope

The replay view (NC-078), the receipt text (NC-064), sieges of outposts (NC-066 handles timers, not battles).

## Notes

- "A small random spread remains" is `Tuning::BATTLE_SPREAD_HUNDREDTHS`, applied by the pinned PRNG from the world's battle stream; the harness is what proves it is small and pinned.

## Report

_Filled in on hand-back._
