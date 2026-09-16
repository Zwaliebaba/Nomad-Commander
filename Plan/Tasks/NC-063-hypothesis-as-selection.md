# NC-063 — Hypothesis as selection

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 4 | GameLogic | M | no | no | Open |

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

- [ ] `DeriveReadings` takes reports and a dossier and no `World` (R18; compile-time test).
- [ ] The §4 receipt's two sentences about the reading are derivable from a `Hypothesis` with outcomes.
- [ ] Both log lines carry the operation id so NC-101 can pair them (GDD §15).

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

_Filled in on hand-back._
