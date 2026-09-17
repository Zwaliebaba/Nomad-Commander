# NC-063 — Hypothesis as selection

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | M | no | no | Done (PR #7) |

**Depends on:** NC-050, NC-061
**Read first:** GDD §4 (*Hypothesis* whole: a selection, not a journal; binds the plan's default assumptions; the receipt says whether it held), §3 (11:00: the three readings), §15 ("whether players can state their hypothesis and whether it held"); AGENTS.md R24

## Goal

The interface derives the readings the current evidence supports; the player picks one; the pick binds the plan's default assumptions about escort strength, enemy commander and timing; and the receipt afterwards says whether the reading held. The derivation is a rule over the company's reports, so a reading is never offered that the evidence does not support.

## Deliverables

- `GameLogic/Hypothesis.h` + `.cpp`: `struct Reading { ReadingKind kind (ConvoyRealAndUnguarded, ConvoyIsBaitWithReserve, ConvoyAlreadyPassed, EscortAsReported, EscortHeavier, CommanderIs(admiral)); assumptions { ShipCounts expectedEscort; CharacterId expectedCommander; Tick expectedTiming; }; supportingReports; }`, `DeriveReadings(company's reports about a target, dossier) -> std::vector<Reading>` (a reading appears when its conditions hold: bait needs a dossier entry showing the habit and an admiral in the sector; passed needs a sighting older than the route time; and so on), `struct Hypothesis { OperationId; Reading chosen; Tick chosenAtTick; outcome { Pending, Held, Failed } per assumption; }`.
- Binding: choosing a reading fills `Plan`'s assumption slots (NC-061) and defaults `engageIfEscortAtOrBelow` to the expected escort.
- Evaluation: after the operation's battle or its window's end, each assumption is checked against the `BattleRecord` (what was actually met) and the outcome written for the receipt (NC-064).
- Log lines `HypothesisChosen`, `HypothesisResolved` (per assumption).
- `WireReading.h`, `WireHypothesis.h`.
- `GameLogicTests/HypothesisTests.cpp`: the §3 evidence yields exactly the three readings; without the dossier entry the bait reading is absent; choosing binds the plan; evaluation marks "the convoy was real: held; Varik had no reserve: failed" from a record with a reserve.

## Acceptance criteria

- [x] `DeriveReadings` takes reports and a dossier and no `World` (R18; compile-time test). *`TheDerivationCannotBeMadeFromTheWorld` asserts the signature with `static_assert`; both the reports and the dossier are `Knowledge`, so there is no path to a fleet's true escort.*
- [x] The §4 receipt's two sentences about the reading are derivable from a `Hypothesis` with outcomes. *`Compose` takes a hypothesis and an `AssumptionKind`; `TheReceiptSaysWhichReadingHeldAndWhichDidNot` asserts §4's exact wording for a failed reading and `AReadingNothingTestedIsUntestedAndNotWrong` the untested one.*
- [x] Both log lines carry the operation id so NC-101 can pair them (GDD §15). *`BothLogLinesCarryTheOperationSoTheyCanBePaired` reads the id out of each and asserts they match — and that `HypothesisResolved` is written once **per assumption**, because one reading can hold and another fail in the same fight.*

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

The picker's screen (NC-076), a free-text hypothesis (GDD §4: not a journal).

## Notes

- Readings are few by design; the test that they are exactly the GDD's three for the Kessel evidence is the guard against a menu of twenty.

## Report

**A hypothesis is a selection, and the whole of what makes it one is that every reading has a condition.** GDD §3's evidence yields exactly the three readings §3 offers — real and unguarded, bait with a reserve, already passed — and the test that says so is the guard the task's own Notes ask for against a menu of twenty. Each absence is earned: with no reports at all there are no readings; with no dossier, or a dossier built on one engagement, or an admiral last seen elsewhere, the bait reading is simply not there.

**The pick binds the plan, and the binding is where a wrong reading becomes a wrong plan.** `Bind` fills the assumption slots NC-061 left and defaults `engageIfEscortAtOrBelow` to the expected escort — so a player who read the convoy as unguarded engages an escort they would have refused had they read it as bait. That is the distinction GDD §4's receipt rests on, and without the default it would be a wrong *number* rather than a wrong decision.

### Two dependencies the task file did not list, and what I did about them

The deliverables reference `BattleRecord` (NC-062) and `OperationId` (NC-064). **NC-062 is owner-visible and unmerged; NC-064 has not started.** Neither is listed under *Depends on*, so this is a gap in the task file rather than a decision I could defer to.

- **`OperationId` is declared in `EntityIds.h` and NC-064 will build the table.** That is the pattern the file already documents for `EventId` — declared before `Event` existed because the entities that refer to it needed a handle. §15 pairs the two hypothesis log lines by the operation, and a metric that could not name what it was about would answer nothing (R24).
- **Evaluation takes `ObservedOutcome` and not a `BattleRecord`.** This is not a stand-in for the record: an assumption is about an escort strength, a commander and a time, and those three are what the check needs whatever shape NC-062's account of the fight ends up being. NC-062 fills one of these from its record and calls `Resolve`, which is a smaller seam than the record would have been and does not constrain what NC-062 decides.

### The dossier had to exist, and it belongs in `Knowledge`

A bait reading needs "a dossier entry showing the habit and an admiral in the sector", and no dossier type existed. `DossierEntry` is **the belief counterpart of NC-060's `AdmiralRecord`**: the record is what the admiral did and lives in `World`; the dossier is what a company watched him do and lives in `Knowledge` (ADR-021). They differ exactly where the player was not looking, which is the whole of why GDD §8's readability takes three or four engagements rather than one — and a reading derived from `AdmiralRecord` would be a player who can see through the fog.

Nothing writes to the dossier yet. NC-062 is what will, when a battle names the template an admiral used; the table, the accessor and the store round-trip are here so that it has somewhere to write.

### Refined against the code as it is

- **`Outcome::Untested` is distinct from `Failed`.** GDD §4's other receipt ends "The Oren have paid nothing, because nothing happened", and a reading scored as *wrong* when the fleet never made contact would teach the player something false about their own judgement. §15 asks "whether the hypothesis held"; "it was never tested" is a third answer and the log carries it.
- **Outcomes are per assumption, and so is the log line.** §4's example is two clauses — "Your reading that the convoy was real was correct; your reading that Varik had no reserve was not" — which is one hypothesis with a held escort reading and a failed commander reading. A single verdict per operation could not produce that sentence.
- **The escort assumption holds at *or below* what was expected**, not on equality, because §3's rule is "engage only if the escort is at or below the assumed strength": the assumption the player acted on is a ceiling, and meeting less than they feared is the reading holding.
- **`WireReading` carries how much a reading rests on and not how likely it is to be true.** GDD §4 makes the hypothesis a bet; a client told which bet was right would not be a client the player has to think in front of (R18). So the wire record has a count of supporting reports and no confidence, and the two `WireReading.h`/`WireHypothesis.h` files the deliverables name are one header, because a reading outside a hypothesis is not a thing anybody sends.
- **`ReadingKind` carries six enumerators and three are unreachable today.** `EscortAsReported`, `EscortHeavier` and `CommanderIs` are in the task's own list and have no derivation rule yet; they are declared so the schema is stable and NC-076's picker has the vocabulary, and nothing offers them. Named here rather than quietly dropped.

### What was verified, and what was not

**Verified here:** **All 216 `GameLogicTests` methods** compiled and run at `-O1 -D_DEBUG` with clang 18.1.3 — the 206 after NC-061 and 10 new. `CheckFormat.py` (229 files) and `CheckProjectFiles.py` (9 projects) clean. clang-tidy 22.1.8 clean on the new and changed files.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent. NC-060 in this PR is confirmed green on CI; NC-061 and this task have not been through it at the time of writing.

**Assumed:** that a bait reading expects the seen escort plus `BAIT_READING_EXTRA_WARSHIPS` rather than a separately-derived force, because the point of bait is that what you saw is not what is there and the design gives no number. That "already passed" is read from the last sighting's age against the route time, which is what a report's age is for. Both are R20 levers citing §3 and §4.

**Bent:** nothing. `Knowledge` schema 3 → 4 for the dossier table.

### For the owner

**Nothing calls `Choose` or `Resolve` yet**, and nothing writes a dossier. Both wait on the same task: NC-062 resolves a fight, which is what produces the observed outcome a hypothesis is scored against and the template sighting a dossier is built from. This task is the rule and the record; NC-062 supplies the event.

**NC-062 is the next task by number and it is *Owner-visible***. `Plan/README.md` says such a task "lands alone as a small PR of its own before anything is built on it", and PR #7 already carries NC-060, NC-061 and this. I have not started it, and NC-066 is the next task that can proceed without it.
