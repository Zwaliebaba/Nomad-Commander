# ADR-022 — The battle resolution model

**Status:** Accepted
**Date:** 2026-09-17
**Task:** NC-062 (*owner-visible*)
**Cites:** GDD §4 (*Execution*: resolved against the admiral's plan; uncertainty mainly from what intelligence got wrong; a small spread), §3 (the 19:00 plan and its thresholds), §7 (combat does not wait for the window), §8 (eight readable templates; readability in three to four engagements; every receipt names the template), §11 (*falling is a chapter*), §12 (ships are counts per class, never individual hulls), §5 (captured hulls salvaged at a fraction; the floor), §15 (measured outcomes), §16 (*battle plans become programming*); AGENTS.md R16, R18, R19, R20, R23, R24; ADR-002 (the pinned PRNG), ADR-004 (byte encoding and versioning), ADR-018 (the wire schema), ADR-021 (reality and knowledge)

## Context
GDD §4 says what a battle *is* — "Execution is resolved against the enemy admiral's own plan, chosen by the rule in section 8 from his repertoire, his traits and his circumstances. Uncertainty comes mainly from what the player's intelligence got wrong; a small random spread remains" — and §8 says what it has to *read like*: eight named templates, readable "within three to four engagements, not ten", with every receipt naming the one the admiral used.

What neither says is what a round costs. §12 lists the mobility verbs and stops at interception. So the feel of combat was undecided, and GDD §8 calls the admirals "the equivalent of a conventional game's enemy classes" — which makes this the most consequential undecided thing in v0.1. It is recorded here because it is the owner's to judge, not the implementer's to default.

`Plan/Roadmap.md` recommended twelve rounds, template postures, integer losses with the pinned spread, and delayed, imperfect triggers. That recommendation is taken. The numbers under it are not in the Roadmap and are decided here.

## Decision
### The shape

Twelve rounds in three phases of four — opening, middle, closing. Both sides act simultaneously each round, so neither gets a first-strike advantage the model never chose to give it. A battle resolves **at the encounter's own tick** (GDD §7: combat "does not wait for the window"); only outpost timers wait, and those are NC-066's.

Each template carries a `Posture` per phase — strike, defence, objective focus, as modifiers around unity — and its own withdrawal threshold. The posture table is what turns eight names into eight ways to fight: an ambush spends itself in the opening and fades, a feint and withdrawal never commits, an escort refuses to be drawn off the convoy, a pincer is weak until it closes. The player's side flies a `Plan` instead, and its withdrawal threshold is the plan's.

### Losses

A side's strength is its hulls' combat strengths with veterancy on top. Each round it inflicts a share of the other's hulls equal to `BATTLE_ROUND_LETHALITY_HUNDREDTHS` scaled by its strike against the sum of both sides' — so an even fight costs each side about four and a half percent a round, and a three-to-one costs three quarters of the lethality.

**Losses accrue in hundredths of a hull and materialise as whole ones.** A round that rounded to zero would make a long fight free; a forced minimum of one hull a round would annihilate a four-hull scout group by arithmetic. Carrying the remainder is exact, integer, and replays identically (R16).

The spread is ±15%, drawn from the world's pinned Battle stream. **Small on purpose**: §4 puts the uncertainty in what the intelligence got wrong, and a spread large enough to overturn a plan would make the outcome luck.

Where the hulls come off is `objectiveFocus`: a template pushing for the cargo takes haulers, one fighting the escort takes whatever is thickest in front of it.

### Base rules are free and reliable; overrides are bought and fallible

GDD §4 lists the base rules as free — objective, priority, engagement threshold, withdrawal threshold, pursuit, reserve — so they apply directly, immediately and without a die roll. An override costs a point of branch budget, is recognised after `TRIGGER_RECOGNITION_DELAY_ROUNDS` and is not executed at all with `TRIGGER_FAILURE_CHANCE_HUNDREDTHS`.

That split is the whole answer to §16's "battle plans become programming": the reliable basics are free, and what you pay for is the clever, fallible part. A reserve committed cannot be uncommitted (§4).

**Mid-battle observation is truthful, and that is not a hole in the fog.** §6 gives identity "when marked or in the same system", and a fight is the same system by definition. The fog is about what the player knew when they *committed* — the hypothesis and the plan's assumptions. A trigger that could not read the enemy in front of it would be a conditional about nothing.

### Three things the GDD left open, decided by the owner on 2026-09-17

**A lost battle is a bloody nose the loser withdraws from.** Annihilation takes a failed withdrawal or a hopeless matchup. This is what makes GDD §3's twenty-five percent withdrawal threshold a lever rather than decoration, and it matches §11's "falling is a chapter" — a total loss should be an event, not a Tuesday.

*Its cost is named rather than hidden*: GDD §15 measures "whether rebuilding after a loss feels like a new chapter", and a survivable defeat means the floor is reached mainly through insolvency (§5's own road down). NC-065's rebuild path will be exercised rarely and that outcome will have few samples.

**Captures: a share of a broken fleet's remaining hulls change hands, the rest is salvaged** at the existing `SALVAGE_FRACTION`. GDD §5 says "captured hulls from broken enemy fleets can be salvaged at a fraction of their value" — read literally, something is captured and salvage is what happens to the rest. Credits only pay a company; an empire has no treasury in this model and inventing one would be an economy nobody asked for (R23).

**Hull condition is in, and is not built here.** The owner decided hulls gain a condition and that NC-062 owns the damage model, and chose to edit GDD §5 and §15 themselves. Until §5's sink list names repair and §15's scope list names hull condition, R23 says a later reader deletes it, so this ADR's model computes losses as destroyed hulls only. When the GDD carries it, damage folds into the loss computation — as **counts per class** (§12: ships are counts and never individual hulls), with a damaged hull that still flies and still fights (§5's deadlock state stays unreachable) and still burns full upkeep.

### Two rules the model needed that nothing had asked for

**A battle happens once: both fleets go on a cooldown.** Two fleets sharing a system with intent would otherwise be re-intercepted on every tick and ground to annihilation in minutes of game time, making the withdrawal above unreachable by arithmetic rather than by decision. `Fleet::reorganisingUntilTick` is six hours. **It is a cooldown and not a loss of intent**: a raider that has just fought still wants to, so it still takes couriers crossing its system and still runs an outpost's clock (NC-066). What it cannot do is re-enter the same battle at once.

**A side that withdrew leaves.** It takes the first lane out of the system it can fuel, and `ResolveMovement` departs fleets before it scans for encounters, so by the next scan it is in a lane and out of reach. A fleet that cannot leave — no fuel, no lane — is caught again, which is exactly the situation GDD §7 calls "a fleet the player failed to plan for".

**The break threshold is calibrated so that pursuit is what breaks a fleet.** A side that withdraws and is not chased escapes at around half its hulls; chased, it takes two more rounds at full exposure and goes over. That is `Plan.h`'s note made arithmetic — pursuit "turns a won fight into a lost fleet" — and it gives GDD §3's "never pursue" a cost on both sides. At the first value tried, 70%, *nothing broke in a hundred hopeless fights* and captures were unreachable code (R23); it is 55%.

## What this forecloses
Real-time tactics, which GDD §1 rules out by design. Positional simulation — there is no space inside a battle, only rounds and postures. Any per-ship state: GDD §12 makes ships counts per class, and the damage model when it arrives must respect that or reopen this decision and §12 with it.

It also forecloses a battle the player watches happen. A fight resolves inside one tick, so the light panel's "take a branch point in a live battle" (GDD §3) is not reachable against this model; that is consistent with A4, which puts the light panel outside v0.1, but a later task that wants it is reopening this ADR rather than extending it.

## Consequences
**For the code.** `Battle` is the only thing besides the resolver that mutates a `World`, and it does so in one place — applying losses. `Fleet` gained two fields: a `Plan`, because a battle cannot go looking for an operation record that does not exist yet and the fleet is what is standing in the system when the shooting starts, and `reorganisingUntilTick`. Phase 5 of the tick reads this tick's `EncounterBegan` events rather than walking the fleet table, which is the pattern NC-055 established after measuring what a per-tick walk costs over a simulated year.

**For the store.** `World::SCHEMA_VERSION` is 15. The fleet's plan and its cooldown are on the wire, so an older store cannot be read — which is the rule ADR-004 sets and not an exception to it.

**For the wire.** `WireBattleRecord` carries the replay to a participant and a news item to everybody else: GDD §6 grants identity "in the same system", and a fight is the same system by definition, so a company that owned one of the fleets is told both sides' complements and losses. A company that was not in it gets when, where, who won and the template — with every count zero and no rounds at all. The template crosses either way, because §8 makes readability the point of an admiral.

**For the tests.** Five existing tests had staged worlds that quietly implied combat and did not get it; each was corrected rather than the phase weakened. That is the expected cost of adding a phase to a resolver that other systems had been assuming was empty, and it is worth expecting again for NC-064 and NC-067.

**If this is reversed.** The posture tables and the thresholds are data in `Tuning.h` (R20) and can be retuned without touching the loop. What cannot be retuned away is the shape: rounds rather than a single roll, both sides deciding from belief, and the record being the replay. Reversing those means rewriting `Battle` and everything that reads a `BattleRecord` — which at the time of writing is NC-064's receipt and NC-065's officer recruitment.

## Measurements
Every figure below is from `GameLogicTests::BattleTests` at the values in `Tuning.h`, over a hundred seeded battles each. **Machine and configuration:** clang 18.1.3 at `-O1 -D_DEBUG` on Linux x86-64, through the local harness that compiles `NeuronCore` and `GameLogic` against a stand-in CppUnitTest; **input:** seeds 1 to 100, one battle per seed, fleets as each test names them; **command:** the suite binary, whose `[NC-062]` lines are quoted verbatim. They are what the property tests assert against, and they are measurements rather than estimates. None has yet been reproduced on MSVC — CI is the build this agent does not have, and the report says so.

| What | Measured |
|---|---|
| A doubled force wins | **86 of 100** — strength tells, and does not always decide |
| An even fight (6 v 6 warships, 25% threshold) | **100 withdrawals, 0 broken, 0 stalemates** |
| What an even fight costs the company side | **31% of its hulls on average** |
| A hopeless matchup (4 scouts v 16 warships, no withdrawal) | **broke the weaker side 79 of 100** |
| A heavies-appear override | **fired 93, was fluffed 7**, first firing on round 1 |
| A pursued lopsided fight | **broke the loser 14 of 100**, and every break gave the winner hulls |

**The outcome *class* of an even fight is deterministic and only the cost varies.** A hundred even battles produced a hundred withdrawals: the ±15% spread moves who wins and how much it hurt, never whether somebody was annihilated. That is the design's own ordering — "uncertainty comes mainly from what the intelligence got wrong" — working as stated, and it is worth knowing before anybody reads variety into the spread.
