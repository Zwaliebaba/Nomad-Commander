# NC-051 — Beliefs, opinions and the threat assessment

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Done (PR #6) |

**Depends on:** NC-050
**Read first:** GDD §9 whole (one belief per empire, one opinion per character; the overwrite rule as a v0.1 release valve; successors inherit), §8 (*What an admiral knows in v0.1*; the employer's opinion: reliable, discreet, who they worked for last), §11 (the institutional threat assessment and its stages; ambitions change behaviour); AGENTS.md R18, R22

## Goal

The two kinds of memory v0.1 has and the number the hunt will one day read: an empire's belief about each incident (who did it, how sure, on what evidence), a character's opinion of each company (the leader's employer view, an admiral's grudge, an officer's loyalty), and the empire's institutional threat assessment that completed contracts and incident-free months step down. All keyed by `CompanyId` (R22), all built from reports and events, none from `World`.

## Deliverables

- `GameLogic/Incident.h`: `IncidentId`, `struct Incident { Tick tick; SystemId system; victim EmpireId; kind (ConvoyRaid, OutpostAttack, FleetAttack); hull classes observed; marked identity if any; the true culprit — a field only `World` holds and only tests and the `Misattribution` log line read; }`. The culprit lives on the reality side by design: the belief side never receives it.
- `GameLogic/Belief.h`: per empire: `std::vector<Suspicion>` where `struct Suspicion { IncidentId incident; CompanyId or EmpireId suspect; Hundredths confidence; std::vector<EvidenceId> evidence; BeliefStage { Silent, Accused, Acted }; }`; NC-052 computes confidence.
- `GameLogic/Opinion.h`: per character per company: `Hundredths warmth`, `reliable` and `discreet` counters, `lastEmployerContract`, `grudgeHundredths` (admirals), `loyaltyHundredths` (officers); `WireOpinion` exposes only what a character would say.
- `GameLogic/ThreatAssessment.h`: per empire per company: a step index into `Tuning::THREAT_STEPS` (fees, revocation, the hunt's threshold declared but unused in v0.1); `StepUp(reason)`, `StepDown(reason)`; the overwrite rule: each completed contract for the empire and each thirty-day period with no incident attributed to the company steps it down once (GDD §9).
- `GameLogic/Memory.cpp`: `ResolveDailyMemory(World&, Tick, events)` applies the overwrite rule and successor inheritance (`Tuning::INHERITANCE_HUNDREDTHS` of a predecessor's opinion; all of the record) when NC-060 replaces an admiral.
- `GameLogicTests/BeliefTests.cpp`, `OpinionTests.cpp`, `ThreatAssessmentTests.cpp`.

## Acceptance criteria

- [x] A belief's confidence and stage are the only things an empire's action reads (NC-052 enforces; here the types make `Incident::culprit` unreachable from `Belief`). *`BeliefTests::TheCulpritIsInTheWorldAndUnreachableFromABelief` asserts it with four concepts rather than with a comment: `Incident` names a culprit and `Suspicion` cannot, `Belief` holds no path back, and `Knowledge` offers neither `Incidents()` nor `Fleets()`.*
- [x] Two companies can be suspected of one incident with independent confidences (R22). *`TwoCompaniesAreSuspectedOfOneIncidentWithIndependentConfidences`, and the scenario is a misattribution on purpose — the empire is surest of the company that did not do it.*
- [x] The overwrite rule steps the assessment down exactly once per qualifying contract and once per clean thirty days, never below the floor step. *`TheOverwriteRuleStepsDownOncePerCleanPeriodAndNotOncePerDay` walks a day short of the period, the day it lands, a fortnight after, and two further periods. The contract half is `Memory::StepDown` with `AContractWasCompleted` — see the refinement below.*
- [x] Successor inheritance moves the tuned fraction of opinion and all of the record, and the event says who replaced whom (GDD §8). *`ASuccessorInheritsPartOfTheFeelingAndAllOfTheRecord` and `TheInheritanceEventSaysWhoReplacedWhom`.*

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None was expected. **One was written: [ADR-021](../../Design/ADR/ADR-021-reality-and-knowledge-are-two-containers.md).** See the report — NC-050 had broken the invariant `World.h` states, and putting it back meant deciding the shape of the fix rather than merely applying one.

## Out of scope

The inference arithmetic (NC-052), the hunt (full game; the step that would trigger it is declared and inert), layered memory (full game).

## Notes

- The Glossary says why `Belief` is keyed by empire now and can be keyed by character later without touching NC-052: the inference rule takes a `Belief&`, whoever owns it.

## Report

**The second half of the tree exists now, and it is a separate object.** Until NC-050 there was nothing anybody believed, so R18 was true by absence. NC-050 made reports and then put them in `World`, which compiled, passed 114 tests and both checkers, and quietly falsified a paragraph in `World.h` that nobody had changed: "belief, reports, opinions and evidence … are held beside a World rather than inside one … the way that rule is kept structural is that a decision routine takes belief and there is no path from belief to here." NC-051 is where that becomes true again, by building the *beside* it refers to.

`Knowledge` holds reports, beliefs, opinions, threat assessments and observer track records. `World` holds reality, including `Incident` and its `culprit` — the one field in this tree that is ground truth about who did something. The two are joined by an `IncidentId` and by nothing else, which is why NC-052's `Misattribution` line is the only place the comparison GDD §15 wants counted can be made at all. **[ADR-021](../../Design/ADR/ADR-021-reality-and-knowledge-are-two-containers.md)** records it, including what it forecloses: every call that advances the simulation now names both halves, and that signature *is* the enforcement.

**The overwrite rule is the piece with the most design weight, and it is a rule of the simulation rather than a courtesy.** GDD §9 names it the v0.1 release valve. Without it three empires lock a player out within weeks and the game is over before it starts. Each elapsed clean period steps an assessment down once and is then *consumed* — the clock advances by a period rather than being reset to now — so the count is periods and not days, an assessment resting at the floor spends its periods rather than banking them, and a run whose daily phase skipped a month still owes exactly one step for it. An attribution restarts the clock whether or not the step moved; they are different facts, and an assessment already against its ceiling that kept its old clock would forget an incident it had just been blamed for.

**GDD §15's "willing employers" metric stopped being a placeholder.** `TickResolver` counted alive empires and said so; it now counts the empires whose assessment of a given company has not reached `Revoked`, and writes **one line per company** rather than one bare count. A count that did not say whose would answer nothing (R22). The name `EmployersWilling` and the `count` field are unchanged, so `MeasureLog.py` and NC-101 read what they always did.

### Refined against the code as it is

- **`StepUp` and `StepDown` are `Memory`'s, not `ThreatAssessment`'s.** The deliverable puts them on the struct. A step that moves is a consequence, R19 requires it to carry its explanation, and a POD of two ids and an index cannot build one — nor can an event written afterwards be a *reason* rather than a reconstruction (`Explanation.h` says so in as many words). They take a world and an event list, and only a step that actually moved writes an event.
- **The contract half of the overwrite rule is a call, not a daily scan.** There is no contract type yet — NC-056 brings it, and `LogEvent::CONTRACT_PAID` is the name already reserved for it. "Exactly once per qualifying contract" is guaranteed by being *at* the completion; a daily scan would need a "counted already" flag on a type that does not exist. `Memory::StepDown(…, ReasonCode::AContractWasCompleted, …)` is the call NC-056 makes, and it is tested directly here.
- **`Incident` is written but nothing produces one.** The table, the schema and the serializers are here because `Suspicion` refers into them and `World::SCHEMA_VERSION` should move once rather than twice. Raids are NC-055's and battles are NC-062's; the tests construct incidents by hand, which is honest about what exists.
- **Nothing steps an assessment *up* on its own.** An incident is not attributed to anybody until NC-052 infers it, and stepping up on `Incident::culprit` would be precisely the R18 defect this task exists to make impossible. `StepUp` is public and waits for the inference rule.
- **`Knowledge::Seed` fills up to the empire count rather than guarding on emptiness, and the resolver calls it every tick.** The generator takes a `World&` and knows nothing about belief; a `Knowledge` built beside a world somebody else generated is the common case — every test and the executable both. Idempotent by construction costs one integer compare a tick and has no flag to keep in step with a reload.
- **`Knowledge` gained a `Hash()`, which is `Neuron::Simulation`'s own FNV-1a rather than a second copy of `World.cpp`'s.** Without it `TickResolverTests`' two-worlds comparison would have stopped seeing detection entirely — which is exactly the defect NC-050 shipped and R16 caught. That test now compares both halves, and `SensorTests::AReportAndATrackRecordSurviveTheStore` moved to `Knowledge` with the data it is about.
- **`Sensor::ReliabilityOf` is gone**; `Knowledge::ReliabilityOf` answers it and takes no world at all, which is a stronger statement of GDD §4's rule than the old signature could make. `Company::recordBySource` and `Empire::recordBySource` are gone with it — a track record is something an observer worked out, which makes it belief.

### Two defects found while building, both mine

**A reference held across a call that may grow the table it points into.** `ResolveDailyMemory` took `ThreatAssessment& threat` and then called `StepDown`, which calls `ThreatOf`, which appends a row when it finds none. It cannot find none here — the row is one the loop is walking — so the code was correct, and a loop that is only correct because of a fact about *another* function stops being correct the first time somebody changes that function. The ids are copied out and the row re-fetched each time round.

**A test that asserted four rows where three had been asked for.** `OneCharactersViewOfTwoCompaniesIsTwoOpinions` creates (character, first), (character, second) and (other, first), and then asks for the first pair again. Three rows. The assertion said four, which is the kind of arithmetic slip a test catches only because it runs — and it did, on the first run.

### Noticed and left alone

- **The generator makes no people.** `world.Characters()` is empty after `Generate`, because the three or four admirals GDD §15 asks for are NC-060's. `OpinionTests` adds its own and asserts the count is zero first, so that the moment NC-060 lands, the helper announces itself as the thing to delete.
- **`ThreatOf` and `OpinionOf` are linear finds over flat tables.** v0.1 has three empires, a handful of characters and one company, and an unordered container iterated into the simulation is the defect R16 names. At Milestone 2's decades it is the same shape of problem as the dead fleet rows NC-048 named and `LatestAbout`'s backwards walk in NC-050, and one answer would serve all three.
- **`ThreatStep::Hunted` is declared and unreachable.** `THREAT_STEP_MAX_IN_V0_1` is `Revoked`, and `StepUp` will not pass it. GDD §15 puts the hunt in the full game (R23); a step nothing can reach is better than a threshold invented later by whichever task first needs one.
- **`WireOpinion` was not written.** The deliverable lists it as "exposes only what a character would say". Nothing crosses the wire yet that would carry one — the dossier panel is NC-074's — and a wire record with no reader is a schema that gets renumbered before it is ever used (ADR-004). Named here rather than quietly dropped.

### What was verified, and what was not

**Verified here:** `python3 Build/CheckFormat.py` (189 files, clang-format 18.1.3) and `python3 Build/CheckProjectFiles.py` (9 projects) clean. **clang-tidy 22.1.8**, CI's pinned version, over every changed `.cpp` — clean, and verified to be *actually looking*: a probe function with a bad case style in `Memory.cpp` and a probe struct in `Memory.h` were both reported, then removed. **All 133 `GameLogicTests` methods compiled and run** at `-O1 -D_DEBUG` with clang 18.1.3 against a local stand-in for `CppUnitTest.h`, from the same sources MSVC compiles: the 114 that existed before and 19 new.

**Measured:** the NC-048 soak year, back to back against `d3c44b4` on one machine state, three alternating rounds — before 0.199 / 0.184 / 0.157 s, after 0.156 / 0.188 / 0.172 s. Indistinguishable; the spread within each build is larger than the difference between them. The figures are in ADR-021 with the caveat that they must not be compared to NC-050's 0.104 s, which was a different session on a shared vCPU — NC-049 already produced one phantom 3× regression that way.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent. The Windows-macro class of defect that made NC-050 go red is now covered by `Build/CheckProjectFiles.py`, which passes.

**Assumed:** that `THREAT_SURCHARGE_HUNDREDTHS` is a fee *multiplier* rather than a flat amount, and that `Revoked` carries no surcharge because GDD §5 has a revoked empire selling nothing. Both are R20 tuning values in one named table citing §9 and §11, so play answers them without touching code.

**Bent:** nothing. Two counts moved to twenty-one (`AGENTS.md`, `Design/README.md`), which are counts and not rules.
