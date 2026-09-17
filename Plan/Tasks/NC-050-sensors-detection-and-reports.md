# NC-050 — Sensors, detection and reports

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Done (PENDING) |

**Depends on:** NC-044
**Read first:** GDD §4 (*Intelligence*: source, age, reliability; "the AI sees the player through the same fog"), §9 (*Reality, belief and evidence*), §12 (sensor range per class; scouting), §3 (the nine-hour-old sighting); AGENTS.md R18 whole

## Goal

The boundary between reality and everything anyone knows: detection by sensor range produces reports, a report says who saw what and when with the observer's noise, its reliability is the source's track record and never the game's knowledge of the truth, and both companies and empires receive reports and nothing else. Everything the AI and the client will ever reason from is created here.

## Deliverables

- `GameLogic/Report.h`: `ReportId`, `struct Report { Tick observedAtTick; Tick deliveredAtTick; ReportSource source; observer (company or empire); subject (a `SightedFleet { hull classes seen with counts, marked?, identity if known, position, heading if in lane }` or an `Incident` reference or a market observation); }`, `ReportSource { OwnSensors, Picket, Scout, CapturedCourier, PurchasedTip, EmployerBriefing, News }`, `struct SourceRecord { std::uint32_t confirmed; std::uint32_t contradicted; Hundredths Reliability() const; }` per (observer, source, and for tips the seller).
- `GameLogic/Sensor.h` + `.cpp`: `ResolveDetection(World&, Tick, events)`: each fleet sees fleets in its system and, for scouts and pickets, at `SENSOR_RANGE_JUMPS[class]`; what is seen is a `SightedFleet` with counts spread by `Random` scaled by distance (`Tuning::SIGHTING_NOISE_HUNDREDTHS_PER_JUMP`), identity only when the target is marked or shares the system; delivery is immediate to an observer in the mothership's system, otherwise by courier delay (NC-053's speed; until NC-053 lands, `deliveredAtTick = observedAtTick + Tuning::COURIER_TICKS_PER_JUMP × jumps`); `News`: public events (battles, marked raids) reach every observer after `Tuning::NEWS_DELAY_TICKS`.
- `BelievedSituation` (NC-047's stub) becomes real: built from an empire's reports only; NC-060 reads it.
- Track record: when a later fact confirms or contradicts a report (a battle contact reveals counts; an arrival reveals a position), the source's record updates and every future report from it carries the new reliability.
- `WireReport.h`: what the client sees of a report, with `Reliability` and `Age(now)`, never a truth field.
- `GameLogicTests/SensorTests.cpp`: a fleet two jumps from a picket is reported with noise and a nine-hour-old sighting has age nine hours; an unmarked fleet in another system has no identity; track record moves after a contradiction; a company never receives a report about a fleet no sensor of its own could see.

## Acceptance criteria

- [x] Reliability is computed from `SourceRecord` and from nothing else; the test constructs a truthful source with a bad record and confirms it shows low reliability (GDD §4: track record, never the truth). *`ReliabilityIsTheTrackRecordAndNotTheTruth`: a truthful source at 1 confirmed and 9 contradicted reads 10, a lucky liar at 9 and 1 reads 90, and the function takes no subject to consult.*
- [x] `BelievedSituation` cannot be built from a `World` in a client-visible or decision-facing path; only `Sensor.cpp` reads `World` to write reports (R18). *`Politics::Believe` is the builder and reads only the empire's own holdings and its **delivered** reports; `ChooseAnEnemy` still refuses a `World` by `static_assert`. `AnEmpireBelievesWhatItWasToldAndNotWhatIsThere` asserts the belief is the report's counts and that a report still in the post counts for nothing.*
- [x] Every report has an origin tick and a delivery tick, and the client's "age" is from the origin (GDD §3: "nine hours old"). *`ASightingCarriesItsAgeFromWhenItWasObserved`, on both the simulation's `Report` and the client's `WireReport`.*

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

**ADR — detection and report noise.** The Roadmap's recommendation; what it forecloses: perfect information from any source.

*Written as **ADR-020**, taking the recommendation and adding the two things building it settled: that detection follows movement rather than the clock, and that a report is checked on counts rather than on position.*

## Out of scope

Beliefs about incidents (NC-051), purchasable tips as board items (NC-067), rumours (full game).

## Notes

- The "captured courier" source is created by NC-053; declare the enumerator now so the wire schema does not renumber.

## Report

**The second category exists now.** Until this task, R18 was true by absence: there was nothing for anyone to believe, so nothing could cheat. There are reports now, and every later task reads them instead of the world — an empire's suspicion (NC-052), an admiral's plan (NC-060), the board and its panels (NC-067, NC-074).

**Detection follows movement, not the clock**, and that is the decision the rest of the shape follows from. `Sensor::ResolveDetection` runs as phase 3, straight after movement, and reports only on the fleets this tick's arrival, departure and drift events name. A sighting of a fleet that has not moved says nothing the previous one did not, and a report a tick would be four hundred thousand rows a simulated year. It is also what makes GDD §3's sighting nine hours old: nothing has been seen since. **Measured: 299 reports over a simulated year**, and a store that grew from 12 KB to 44 KB.

**What a sighting is.** Counts spread by the pinned PRNG across a band per jump of distance, symmetric, never below one when something was there; identity binary and never noisy — known when the fleet is marked or shares the observer's system (GDD §6), absent otherwise, because a half-known identity is a probability a client could read as information. Delivery costs a courier's time per jump to the observer's desk, and nothing may act on a report before it lands.

**Reliability takes no world, no report and no subject.** `SourceRecord::Reliability()` is confirmed over checked and nothing else, so there is no path by which it could consult whether the report happened to be right — which is the rule GDD §4 states and the one most easily broken by accident. An unchecked source reads 50 rather than 0 or 100: "nothing is known about this source" and "this source is wrong" are different things to show a player. A report also keeps the reliability it was *written* with, so a board shows what its reader was entitled to think at the time.

**A report is checked on counts, never on position.** A fleet that moved is not a source that lied. When an observer later sights the same subject in its own system — where the count is exact — the most recent unchecked report is confirmed or contradicted against it, once. That is GDD §4's "a battle contact reveals counts" doing the work that position cannot.

### Two defects found while building, both mine

**A quadratic scan that looked like "detection is expensive".** The first implementation handed `ResolveDetection` the caller's whole event vector and filtered by tick. A simulated year went from 0.112 s to **0.287 s** — a factor of 2.7 — and the obvious reading was that detection costs that much. It does not: `NomadSimulation` clears its events only when something drains them, so a headless year hands it a vector that grows all year and the per-tick scan is quadratic in the length of the run. The resolver now remembers where each tick's events begin and passes a span. **Back to back afterwards: 0.112 s without detection, 0.104 s with it** — the two differ by less than the run-to-run spread. The cost is nothing; the first measurement was measuring an accumulating buffer.

**A POD array that broke determinism, which is the more instructive one.** `Company` and `Empire` each gained a `SourceRecord[REPORT_SOURCE_COUNT]`, the first raw POD array either has held. Four tests build a company as `Company c;` rather than `Company c{}` — harmless while every plain member was assigned explicitly, and *not* harmless the moment one was not. The first symptom was `DeterminismTests::TwoRunsOfOneScriptEndInTheSameState` failing: two runs read different stack garbage into a reliability and hashed differently. Both halves are fixed — `SourceRecord`'s fields carry initializers so the type is safe wherever it is built, and the four sites value-initialize — and the comment on the type says why it breaks the tree's usual aggregate style. **R16 caught it in one run**, which is the whole argument for having written that harness in NC-043.

### Refined against the code as it is

- **The subject is a `SightedFleet` and not yet a variant.** The task lists it as a sighting *or* an incident reference *or* a market observation. `Incident` is NC-051's type and does not exist; a variant of one alternative is ceremony, and the schema version is what carries the change when NC-051 adds the second arm. Named here rather than quietly dropped.
- **`WireReport.h` could not hold its own `ToWire`.** ADR-001 forbids a `Wire*.h` including a reality header, and `Build/CheckProjectFiles.py` caught it on the first run. The conversion lives in `Report.h`, exactly as `ToWire(const Input&)` lives in `Input.h`, and the wire header carries its own `WIRE_REPORT_SOURCE_COUNT` with a `static_assert` in `Sensor.cpp` tying it to the real one — the same shape `WireInput.h` already uses for ship classes.
- **Track record is stored on the observer, not in a table.** The task's parenthetical asks for a record per (observer, source, and for tips the seller). The seller is NC-056's and does not exist; per (observer, source) is an array on `Company` and `Empire`, which serializes with the entity and needs no second table.
- **`BelievedSituation` gained two fields rather than being rebuilt.** `sightedForeignHulls` and `reportsRead` are counted from delivered reports only. They are the first fields on that type that are not simply something the empire owns, which is what "becomes real" meant in practice. NC-051 replaces the summary with real beliefs about incidents.
- **`News` and the delay it needs are declared and unused.** `Tuning::NEWS_DELAY_TICKS` exists so that NC-055's marked raid and NC-062's battle do not each invent one; no code path produces a `News` report yet, and the enumerator is declared for the schema's sake (ADR-004).

### What was verified, and what was not

**Verified here:** `python3 Build/CheckFormat.py` (178 files, clang-format 18.1.3) and `python3 Build/CheckProjectFiles.py` (9 projects) clean — the latter having first *failed* on the wire edge, which is the check doing its job. **clang-tidy 22.1.8**, CI's pinned version, on every changed `.cpp` — clean. **All 114 `GameLogicTests` methods compiled and run** at `-O1 -D_DEBUG` with clang 18.1.3 against a local stand-in for `CppUnitTest.h`, from the same sources MSVC compiles: 105 that existed before and 9 new.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent.

**Assumed:** that an empire reads its post at its capital and a company at its mothership. GDD §4 says orders are instant "within the mothership's own system" and says nothing about where an empire's desk is; a capital is the reading that costs nothing to change.

**Bent:** nothing. `Design/README.md` and `AGENTS.md` have their ADR count moved to twenty, which is a count and not a rule.

### Noticed and left alone

- **A besieging fleet reports nothing about itself.** Detection is driven by movement, so a fleet that parks is seen once and then not again until it moves. That is what makes an old sighting old, and it is also why a fleet sitting on a system generates no fresh intelligence. If NC-052 or NC-060 wants a periodic sweep, it is changing ADR-020's decision 1 rather than adding to it.
- **`LatestAbout` walks the reports table backwards from the end on every close sighting.** At 299 rows a year that is nothing. At Milestone 2's decades it is the same shape of problem as the dead fleet rows NC-048 named, and the same answer would serve both.
- **Nothing drains reports, ever.** They are a table like every other and rows are never erased (`Table.h`), which is right — the dossiers and NC-052's evidence refer back to them. It is worth knowing that the store now grows with what was *seen* as well as with what happened.
