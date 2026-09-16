# NC-047 — Empires, goals, wars and truces

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | no | Done (PENDING) |

**Depends on:** NC-045
**Read first:** GDD §7 (*The world generates situations at a rate*: never quiet, the three guarantees; "A three-empire world at peace is a bug"), §8 (*Empires want things for years*; *Politics belong to the empires*), §15 (three empires; Milestone 2's politics wait); AGENTS.md R18 (an empire's decision routine takes belief), R23

## Goal

Empires that want things for years and fight about them: leaders with a small set of persistent goals, fleets assigned tasks from those goals, wars that start from conflicting goals and end in truces, grudges that resume them, instability that seeks a cheaper war, and the guarantee that at least one conflict is active at any time. The v0.1 subset of GDD §8's politics: war, truce, peace, grudge; no coalitions, elimination, cession or vassalage (Milestone 2).

## Deliverables

- `GameLogic/EmpireGoal.h`: `GoalKind { HoldSystem, TakeSystem, SupplySiege, BreakSiege, ProtectTrade, PunishRaider }`, target ids, priority in hundredths, `satisfied` (contracts dry up when a goal is met, GDD §8).
- `GameLogic/Relation.h`: per ordered empire pair: `RelationState { Peace, War, Truce }`, `truceExpiresAtTick`, `grudgeHundredths`, `warStartedAtTick`.
- `GameLogic/Politics.h` + `.cpp`: `ResolveDailyPolitics(World&, Tick, events)`: goal conflicts → war (with an event explaining which goals); losses raise grudge, days of peace lower it (`Tuning`); a war ends in a truce when exhaustion (losses over `Tuning::WAR_EXHAUSTION`) or a goal's satisfaction says so, lasting one to three weeks; a truce expiring with grudge above `GRUDGE_RESUME_THRESHOLD` resumes the war; an empire whose upkeep strain exceeds a threshold ends its costliest war and starts a cheaper one within `Tuning::INSTABILITY_DAYS`; if no war is active on a day, the pair with the highest grudge goes to war and the event says why ("A three-empire world at peace is a bug").
- Fleet tasking: each empire holds `Tuning::FLEETS_PER_EMPIRE` fleets with placeholder commanders (NC-060 gives them admirals); `AssignTasks` maps goals to fleet orders through NC-044 (move, engage intent, interdict); convoy escort size follows the relation state (NC-045 reads it).
- `EmpireDecision` inputs take a `BelievedSituation` built from the empire's reports (NC-050 fills it; until then a stub that contains only what the empire owns, never another fleet's true position), so the function signature is right from the start (R18: "A decision routine with a world-state parameter is a defect").
- `GameLogicTests/PoliticsTests.cpp`: never quiet over a year; a truce with high grudge resumes; instability switches wars; goals dry up; the decision function cannot be called with a `World` (compile-time test).

## Acceptance criteria

- [ ] On every day of a simulated year on a generated three-empire map, at least one relation is `War` (GDD §7); the test asserts it day by day.
- [ ] Wars last between one and three real weeks at the full-game clock (7–21 days of ticks) on average over the year, within a stated tolerance (GDD §7's starting values; R20 makes them tunable).
- [ ] Every war start, truce and resumption is an event with an explanation naming the goals or the grudge (R19).
- [ ] No empire decision reads `World` directly (R18; the compile-time test and the reviewer).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Admirals (NC-060), contracts (NC-056), covert raids (NC-055), coalitions and the rest of Milestone 2's politics, the hunt.

## Notes

- Leaders "never start wars or break treaties" applies to admirals (GDD §8); leaders do both, and a broken treaty is a public event with a record.

## Report

**Empires that want things for years and fight about them.** Goals that persist and dry up when met, relations per pair, wars from colliding goals, grudges that rise with war and fall with quiet, truces of one to three weeks, resumption when the grudge outlasts the truce, and a strained empire trading a costly war for a cheaper one. Eleven tests.

**Measured over a simulated year on a generated three-empire map:**

| | |
|---|---|
| Days with no war anywhere | **0 of 365** |
| Wars declared | 36 |
| Truces agreed | 35 |
| Truces that settled into peace | 21 |
| Mean war length | **9 days** (GDD §7 asks for 7–21) |

**The year-long run found the hole in "never quiet", and GDD §8 supplied the fix.** The first version refused to start a war out of a truce — a truce is a promise — and left **ten quiet days**, all of them days when all three pairs happened to be under truce at once. GDD §7 calls that a bug in as many words ("A three-empire world at peace is a bug"), and §8 has the escape hatch and its price: "treaties broken at the price of a public record." So when every pair is in truce, the empire with the most to hold against another breaks its truce, and the event says `ATreatyWasBroken` rather than `TheRegionWasTooQuiet`. **NC-051 is what makes that record cost something**; today it is a distinguishable reason and nothing more.

**The guarantee is explicit, not emergent.** The rules above make wars common, and then `ResolveDaily` checks at the end of the day whether the region is quiet and fixes it if so. A guarantee that emerges from tuning is a guarantee that stops holding the first time somebody tunes it, and this one is a design rule rather than a tendency.

**R18 is structural here, which was the point of the task.** `Politics::ChooseAnEnemy` takes a `BelievedSituation` and there is no overload that takes a `World`; the test says so with a `static_assert` on `is_invocable_v`, which a later task cannot quietly break. `Believe` builds the situation from what an empire genuinely knows — its own holdings, its own hulls, its own grudges — and **never another fleet's position or strength**. NC-050 fills it from reports; the signature is already right.

**Refined against the code as it is.**

- **Exhaustion is counted in days under arms, not hulls.** `Relation::lossesSinceWarStarted` is the field the task named and NC-062 is what will increment it per hull; until battles exist, a day of war costs a point, which gives wars the right length and leaves the field meaning what it will mean.
- **A grudge grows during a war and only decays in quiet.** That is what makes exhaustion and resumption two different things: a war can end without anybody forgiving anybody, which is exactly the loop GDD §7 describes.
- **`Relation` is a table row per unordered pair**, not a matrix on `Empire`. Three empires make three rows; the lookup is linear and the table is tiny.
- **The convoy escort reads the war state** (NC-045's `DispatchConvoy` now calls `Politics::EscortStrengthFor`), which was this task's deliverable and closes a loop NC-045 left open with a constant.
- **`Politics::Seed` runs in the generator beside `Economy::Seed`.** An empire without goals is not an empire, and a caller remembering to seed one is a bug nobody sees until a soak.
- **Fleet tasking was not built.** The task listed `AssignTasks` mapping goals to NC-044 orders. Empire fleets do not exist yet — nothing creates them, `FLEETS_PER_EMPIRE` is declared and unused — and **NC-060 is the task that gives an empire admirals to command them with**. Assigning orders to fleets that no admiral chooses would be a stub in the shape of the thing rather than the thing. Named here rather than quietly dropped; it is a small task once NC-060 lands.

**The acceptance criteria, checked.** At least one relation is `War` on every day of a simulated year, asserted day by day with the first quiet day named in the failure. Wars average 9 days against §7's one to three weeks, inside a stated tolerance of 4 days. Every war, truce, settlement and satisfied goal carries an explanation naming the goals or the grudge — and the test composes each one's sentence and requires it to read as a belief. No empire decision reads `World`, by `static_assert`.

**A defect in my own test:** `ATruceWithAHighGrudgeResumesTheWar` set the grudge *exactly* at the resume threshold, and a day of quiet decays it by one before the truce is checked — so it tested the decay rather than the resumption and failed. It sits well above the line now, with a comment saying why.

**Verified:** `CheckFormat.py` (172 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**75 translation units clean**). Debug builds with zero warnings. All four suites: **273 of 273 green**, 11 new here. Release not built; NC-048's soak is where that belongs.

**Assumed:** that a war must have a *pair* to be fought by, so the last-resort rule picks a pair rather than declaring a general emergency. With three empires that is always possible.

**Bent:** nothing.

**Noticed and left alone.**

- **Nothing takes a system yet.** A `TakeSystem` goal is satisfied only if ownership changes, and nothing changes ownership — NC-062's battles and NC-066's claims are what will. So goals in practice never dry up on their own today, and the test forces the ownership change to prove the rule works.
- **`Politics::Between` is a linear scan per call**, and `ResolveDaily` calls it inside a double loop over empires. Three empires make it nine scans of three rows. At Milestone 2's five empires it is ten rows scanned a hundred times a day, still nothing; past that it wants a lookup.
- **An empire at war with everyone still sends convoys to its enemies**, because NC-045's destination search is map-wide and knows nothing about relations. NC-045's report flagged this and it is now actionable: `Politics::Between` exists, and **the fix belongs to whichever task next touches the convoy planner**.
