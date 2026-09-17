# NC-062 — Battle resolution and the battle record

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | L | no | **yes** | Done (PR #8) |

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

- [x] The ADR states the model with the tables, and the report states the measured outcome distributions the property tests rely on.
- [x] Neither side's plan evaluation reads the other side's true counts; the round loop reads `World` only to apply losses (R18; the reviewer checks the two code paths are distinct).
- [x] Every battle emits its record with the template name and its events with explanations (R19; GDD §8: "every receipt names the template").
- [x] A battle in open space resolves at the tick of the encounter, never deferred to a window (GDD §7).

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

**What is built.** `Battle.h`/`.cpp` resolves an encounter at its own tick; `BattleRecord.h` is the replay and
`WireBattleRecord.h` is what crosses to a client. `Tuning.h` gained the model's numbers — twelve rounds in three
phases, the per-template posture table, the per-template withdrawal thresholds, lethality, spread, break threshold,
capture fraction, the reorganising cooldown and the admiral death chance. `ADR-022` states the model and its tables.
Phase 5 of the resolver reads this tick's `EncounterBegan` events, which is the NC-055 pattern rather than a new
per-tick walk of the fleet table.

**Measured, and the figures the property tests assert against** — all from `BattleTests` on clang 18.1.3, a hundred
seeded battles each:

| What | Measured |
|---|---|
| A doubled force wins | **86 of 100** |
| An even fight, 25% threshold | **100 withdrawals, 0 broken, 0 stalemates**; 31% of hulls lost on average |
| A hopeless matchup, no withdrawal | **broke the weaker side 79 of 100** |
| A heavies-appear override | **fired 93, fluffed 7**, first firing on round 1 |
| A pursued lopsided fight | **broke the loser 14 of 100**; every break gave the winner hulls |

The owner's "bloody nose" decision is therefore built and measured rather than asserted. **Worth knowing: the outcome
*class* of an even fight is deterministic** — a hundred even battles gave a hundred withdrawals, and the ±15% spread
moves who wins and what it cost, never whether somebody was annihilated. That is GDD §4's own ordering working as
written, and it means nobody should read variety into the spread.

**Three things the model needed that no task had asked for, each recorded in ADR-022 rather than slipped in.**

1. **A battle happens once: both fleets go on a six-hour cooldown** (`Fleet::reorganisingUntilTick`). Without it two
   fleets sharing a system with intent are re-intercepted every tick and ground to annihilation in minutes of game
   time, which would make the withdrawal decision unreachable by arithmetic rather than by decision. It is a cooldown
   and **not** a loss of intent, which is the distinction that matters: a raider that has just fought still takes
   couriers crossing its system and still runs an outpost's clock.
2. **A side that withdrew leaves**, down the first lane out it can fuel. `ResolveMovement` departs fleets before it
   scans for encounters, so by the next scan it is gone. A fleet that cannot leave is caught again, which is GDD §7's
   "a fleet the player failed to plan for".
3. **`Fleet` gained a `Plan`.** GDD §4: "The offline doctrine is the same plan read as standing orders. Every
   operation has one." A battle cannot go looking for an operation record that does not exist yet, and the fleet is
   what is standing in the system when the shooting starts. NC-064's operation points at this rather than holding a
   second copy.

**One calibration found by measuring rather than reasoning.** `BATTLE_BREAK_LOSSES` started at 70% and **nothing
broke in a hundred hopeless fights** — a withdrawing side escapes at around half its hulls, so captures were
unreachable code (R23) and pursuit bought nothing. At 55% a chased fleet takes two more rounds at full exposure and
goes over, which makes pursuit exactly what turns a won fight into a lost fleet (`Plan.h`'s own note) and gives GDD
§3's "never pursue" a cost on both sides.

**Hull condition is not built, and that is the owner's sequencing rather than an omission.** The owner decided hulls
gain a condition and that this task owns the damage model, and chose to edit GDD §5 and §15 themselves. At the time of
writing `Design/GameDesign.md` has not moved: §5's sink list still omits repair and §15's scope list has no hull
condition, so R23 says a later reader deletes it. Everything else here is independent of it, and it folds into the
loss computation when the GDD carries it — as counts per class (§12), with a damaged hull that still flies (§5's
floor) and still burns full upkeep.

**Five existing tests staged worlds that now imply combat**, and each was corrected rather than the phase weakened.
NC-066's evacuation test had a hostile fleet standing *in* the outpost's system; the governor's rule is about contacts
in its **reports**, so the contact moved one jump away and the test now exercises the policy rather than a battle. Its
defence test relied on four haulers holding off three warships, which they no longer do, so it has warships. Its siege
test asserted the clock restarts — which is still true, because the cooldown does not remove intent. `CourierTests`'
twenty engaging raiders were being disengaged one per tick by a lone scout that survived to a stalemate every time;
the cooldown fixed that by keeping intent. `WorldTests`' round-trip fixture default-initialised its `Fleet`, which
left the new plan's scalars indeterminate — braced now, and carrying a real plan so every branch of `ReadPlan` is
reached.

**Verified.** `python Build\CheckFormat.py` (239 files) and `python Build\CheckProjectFiles.py` (9 projects) pass.
**245 test methods across the four suites pass on clang-18 locally**, 11 of them new in `BattleTests.cpp`.
clang-tidy-18 is clean over every file this task touched (see the CI paragraph below — it was not, at first). `World` schema 14 → 15 (the fleet's plan and its cooldown);
the NC-048 soak hash moved with it and every other measured figure — NC-045's, NC-047's, NC-055's, NC-060's,
NC-066's — is unchanged.

**Noticed and left alone.** The year-long soak produces no battles: its empire fleets are convoys without engage
intent, and a covert raid is resolved by `CovertRaid` rather than by an encounter. So this model is exercised by
`BattleTests` and not by the soak, which is worth knowing before anybody reads the soak's stability as evidence about
combat. Giving the soak a fighting player is NC-090's scenario work, not this task's.

**Not done, and not claimable:** no `msbuild` and no `vstest.console.exe` **run by me** — there is no Windows
toolchain here, so the MSVC build and the real CppUnitTest framework are CI's word and not mine. The task needs no
desktop run.

**What CI then confirmed, and the one thing it caught.** On `bb5867b` the Windows job built Debug|x64 and ran all
four suites under the real CppUnitTest framework: **420 of 420 tests pass**. Every measured figure agrees with
clang-18 on Linux to the digit — the NC-048 soak hash `6152001022014570065`, all five `[NC-062]` lines, and
NC-043's, NC-045's, NC-047's, NC-055's, NC-060's and NC-066's besides. **That is the cross-compiler determinism R16
rests on, measured across two compilers, two standard libraries and two operating systems rather than assumed.**

The same run was red, on clang-tidy 22.1.8 rather than on the model: `bugprone-inc-dec-in-conditions` on the two
lines of the round loop that decremented `withdrawRoundsLeft` inside the condition that read it. The short-circuit
made it correct and the check is still right — nobody should have to reason about sequencing to know what a round
does. Decrementing before the test fixes it with identical behaviour (a left break still pre-empts the right side's
tick, and the counter is never read after the loop), and every figure above is unchanged by it.

**That was the second CI failure on this task from a check the local harness did not run**, after MSVC's C4244 on the
same two lines' ancestors. Both gaps are now closed in the session harness: `-Wunused-parameter` and
`-Wshorten-64-to-32` on the compile, and a clang-tidy-18 pre-filter over `GameLogic` and `NeuronCore` using the
repository's own `.clang-tidy`. The pre-filter is not a substitute for CI — CI pins 22.1.8 and drives it against the
real Windows SDK, where this runs 18 against a shim — and it carries a measured false-positive floor of three
findings in files CI passes. It reproduced both of Battle.cpp's findings exactly, which is what it was built for.
