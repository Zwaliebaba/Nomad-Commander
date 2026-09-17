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

## Refinement (2026-09-17)

Three things the GDD leaves open and the ADR has to fix. All three were put to the owner and answered; they are written here
so the ADR states a decision rather than a preference, and so a later reader can see it was asked rather than assumed.

**How decisive a battle is: a bloody nose you withdraw from.** The loser normally gets out having lost a minority of
hulls; losing a whole fleet takes a failed withdrawal or a hopeless matchup. GDD §4 fixes only where the uncertainty
comes from ("mainly what the player's intelligence got wrong; a small random spread"), not how much a round costs, and
§12 says nothing about it either. This is what makes NC-061's default withdraw-at-25% a lever rather than decoration,
and it matches §11's "falling is a chapter" — a total loss should be an event, not a Tuesday.

**A consequence worth naming, because it costs a measured outcome.** GDD §15 measures "whether rebuilding after a loss
feels like a new chapter". If a lost battle is normally survivable, the floor is reached mainly through insolvency
(§5's own road down) and through failed withdrawals, so NC-065's rebuild path is exercised rarely and §15's outcome
will have few samples. That is the owner's choice made knowingly, not an oversight.

**Captures: some captured, the rest salvaged.** GDD §5 says "captured hulls from broken enemy fleets can be salvaged at
a fraction of their value", and `Tuning::SALVAGE_FRACTION` (30) already exists against that sentence. Read literally,
something is *captured* and salvage is what happens to what is not kept — so a fraction of a broken fleet's hulls join
the winner as hulls and the remainder pays out at 30% of value. The fraction is its own named constant citing §5, and
the report measures how often a capture actually happens over the soak rather than asserting it is rare. **Combined
with the decision above it should be rare by construction**: captures come from fleets that were broken, and most
fleets withdraw. Milestone 2 tests that "raiding stays viable without dominating"; if captures prove too generous, the
lever to reach for first is restricting them to fleets broken outright rather than lowering the fraction.

**Hull condition: in, but waiting on the GDD.** The owner decided hulls gain a condition and that NC-062 owns the
damage model, and chose to edit GDD §5 and §15 themselves rather than have it drafted here — a scope enlargement is the
owner's document to change (AGENTS.md §6), and until §5's sink list names repair and §15's scope list names hull
condition, R23 says a later reader deletes it. So this task **builds everything that does not depend on it** — the
round loop, postures, trigger recognition and failure, withdrawal, pursuit, the reserve, captures, the record and its
wire form — and folds the damage model into the loss computation, which is where it belongs, once the GDD carries it.
Three constraints it must respect whenever it lands:

- **Counts per class, never a per-hull state.** GDD §12: ships within a fleet "are counts per class and never
  individual hulls", and that is load-bearing for the store and the replay. The shape is a second `ShipCounts` beside
  `ships`.
- **A damaged hull still flies and still fights, just worse.** GDD §5 makes the deadlock state unreachable; a hull that
  needed credits before it could be used would put a broke player back in one. Repair buys back capability, never
  access to it.
- **A damaged hull still burns full upkeep.** GDD §5 charges for a hull "whether it moves or not", and NC-066 already
  had to close the same hole for docked hulls: anything that suspends upkeep is a mothball without the fee or the
  grace period.

## Report

_Filled in on hand-back._
