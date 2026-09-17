# NC-052 — Evidence and the inference rule

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | L | no | no | Done (PR #6) |

**Depends on:** NC-051
**Read first:** GDD §6 whole (the table, the thresholds, the window between accusation and action), §3 (0:00 and 3:00: what the panel shows), §9 (the example explanation), §1 (*Persistence is the structure; belief is the game*); AGENTS.md R16 (weights in integer hundredths), R19, R20; `Plan/Roadmap.md` finding 1

## Goal

The game's hook as a first-class rule: every incident produces evidence, each item weighs what GDD §6's table says in hundredths, distance decays it, priors accumulate to a cap, denials and exposed denials move it, and the sum against a suspect crosses forty to accuse and seventy to act. The accusation carries its reasoning in the shape of GDD §9 and the action carries the same reasoning with its consequences, because the panel and the receipt are built from that record and nothing else.

## Deliverables

- `GameLogic/Evidence.h`: `EvidenceId`, `EvidenceKind` with one enumerator per §6 row (`DetectedWithinTwoJumps`, `HullClassesMatch`, `TestimonyNames`, `RouteConflicts`, `PriorPattern`, `CapturedOrders`, `MarkedGoodsSold`, `RivalDenial`, `OthersDenial`, `ExposedFalseDenial`), `struct Evidence { kind; incident; suspect; Hundredths weight (after decay and caps); source ReportId; Tick tick; }`.
- `Tuning.h`: `EVIDENCE_WEIGHT[kind]` as the §6 column in hundredths, `DISTANCE_DECAY_HUNDREDTHS_PER_JUMP`, `PRIOR_PATTERN_CAP`, `ACCUSE_AT = 40`, `ACT_AT = 70`, `DISCRETION_PENALTY`, each citing §6.
- `GameLogic/Inference.h` + `.cpp`: `CollectEvidence(World&, const Incident&, empire's reports) -> std::vector<Evidence>` (reads reports, not `World`'s positions: "detected within two jumps" is true only if the empire has a report saying so); `Assess(Belief&, incident, evidence) -> Hundredths` (the sum, clamped); `ResolveDailyInference(World&, Tick, events)`: for each open incident and suspect, recompute, and on crossing `ACCUSE_AT` emit `Accusation` with the full for/against list; on crossing `ACT_AT` emit `Action` with the same and apply: claims revoked (NC-066's claim state; until then a flag), tolerance withdrawn, hostility (engage intent against the company's fleets in the empire's space), the incident entered in the record; on falling below a threshold, nothing is undone (GDD §6: the window is where the answer matters, and action is not reversible).
- `Accusation.h`: `struct Accusation { IncidentId; EmpireId; CompanyId; Hundredths confidence; evidence for; evidence against; Tick issuedAtTick; }` and `WireAccusation.h`.
- Log lines: `AccusationIssued`, `AccusationResolved`, and `Misattribution` when an accusation's suspect is not `Incident::culprit` (the one place the truth is compared to belief, for R24's metric).
- `GameLogicTests/InferenceTests.cpp`: one test per table row with the exact hundredths; the §3 case (detected within two jumps, hull class match, the Oren denial for others, a route conflict) lands between `ACCUSE_AT` and `ACT_AT` after adding the prior pattern and testimony the scenario will supply, and the test says which items it used (Roadmap finding 1); crossing seventy revokes and records; nothing reverses.

## Acceptance criteria

- [x] `grep -n "40\|70\|25\|15\|30\|60" GameLogic/Inference.cpp` finds no tuning literal (R20). *The grep comes back **empty**, not merely free of tuning literals — the one match it had was a `GDD §15` citation, which moved to `Inference.h` so the criterion is checkable exactly as written.*
- [x] Confidence is a `Hundredths` and every weight is a table entry; no `float` (R16). *`grep -c "float\|double" GameLogic/Inference.cpp` is 0; every weight is reached through `Tuning::EVIDENCE_WEIGHT[kind]`.*
- [x] The accusation event's explanation renders through `ExplanationText::Compose` into the §9 sentence shape with the same items the test supplied (R19). *`CrossingFortyAccusesAndCarriesItsReasoning` asserts the belief clause, the claim, the "For:" list and the text of the item the sum was built from.*
- [x] An empire never accuses a company it has no report about, however guilty (R18). *`AnEmpireNeverAccusesACompanyItHasNoReportAbout`: the culprit is in the world, the empire is not told, and two days of the daily pass leave the belief empty. It falls out of the rule rather than being checked for — no report means no evidence means zero, and zero is below forty.*
- [x] `Misattribution` is logged with the incident, the suspect and the culprit's id. *`AMisattributionIsLoggedWithTheIncidentTheSuspectAndTheCulprit` asserts all three fields.*

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None. The weights are GDD-settled and tunable (appendix); their home is `Tuning.h`.

## Out of scope

Answering an accusation (NC-054), covert raids (NC-055), the panel (NC-075).

## Notes

- "Decays with distance": weight × (1 − decay × jumps), floored at zero, rounded by NC-012's rule.
- "Repetition convicts": priors are the suspect's earlier `Accused` or `Acted` suspicions with the same incident kind, each worth the row's weight, summed and capped.

## Report

**The hook is a rule now.** Every incident produces evidence, each item weighs what GDD §6's table says in integer hundredths, distance decays the detection row, priors accumulate to a cap, and the sum crosses forty to accuse and seventy to act. The accusation carries the items the sum was actually built from, in GDD §9's sentence shape, and the action carries the same ones — because the panel (NC-075) and the receipt (§4) are built from that record and nothing else.

**Two decisions the design leaves open, taken here and named rather than buried.**

*One item per row of the table, not one per sighting.* §6's table is a checklist of *kinds* of evidence. An empire that had looked at a suspect ten times would otherwise convict on ten copies of one fact — arithmetic rather than design. `CollectEvidence` keeps the best report for each row (the nearest sighting for detection, the first qualifying one for the rest) and emits one item. The prior pattern is the one row §6 itself makes cumulative, and it says so and caps it.

*An alibi is an alibi only when nothing also puts them near.* §6 calls the negative row a conflict "with the timing". Two sightings a day apart, one close and one far, are a fleet that moved — not a suspect who was elsewhere — so `RouteConflicts` fires only when no detection row did. Without that rule a suspect seen at the scene and again across the map would collect both the evidence and the alibi for it.

**"Close contact" is the observer's distance to the suspect, not the sighting's distance to the raid.** That one cost a red test and is worth keeping. §6 allows testimony "only from marked fleets or close contact"; the first implementation read "close" as *near the incident*, so any picket sighting in the raided system counted as a witness. §6's three positive sighting rows sum to exactly seventy, so that made a single picket report enough to revoke a claim outright — no accusation, no window, nothing for the player to answer. `ReportSource::OwnSensors` is NC-050's own marker for a sighting made in the observer's system, and that is what testimony now reads.

**The report had to learn who it thought it was looking at.** This is the refinement with the most reach. §6 asks whether the suspect was detected near the incident, and a rule that took `Report::sighting.subject` and looked the owner up in `World` would be reading ground truth through a handle with a flag as the only thing in the way — the exact defect ADR-021 exists to make impossible. `SightedFleet` now carries `ownerCompany` and `ownerEmpire`, set only when `identityKnown`, and `Sensor` fills them. `WireReport` had anticipated the pair since NC-050 ("when it is false the client has no owner index to name"); this is that comment coming true. `Knowledge::SCHEMA_VERSION` 1 → 2.

**Roadmap finding 1, answered as the finding asks.** GDD §3's Varn accuses at fifty-eight percent on four items that sum to fifteen in §6's table, so the figure is illustrative and the *band* is the testable claim. `TheDesignsWorkedCaseLandsBetweenAccusingAndActing` builds it from three items and **says which three**: a sighting at the scene, a hull-class match, and one prior of the same kind already acted on — fifty-five, which accuses and leaves the window open. It deliberately omits testimony, because with it the case reaches exactly seventy and acts, and the window GDD §6 puts the player's answer in never opens. §3's rival denial is NC-054's; its route conflict would be an alibi this case does not have.

### Refined against the code as it is

- **`PRIOR_PATTERN_CAP` is `EVIDENCE_PRIOR_INCIDENT_CAP`, which NC-042 already declared.** A second name for one number is the thing R20 exists to prevent, so the deliverable's name is not added.
- **`EVIDENCE_WEIGHT[kind]` is built from the named constants rather than being a second copy of the column.** Each entry *is* `EVIDENCE_DETECTED_WITHIN_TWO_JUMPS` and its siblings; the table is an index, not a duplicate.
- **`ACCUSE_AT` and `ACT_AT` are `ACCUSE_THRESHOLD` and `ACT_THRESHOLD`**, likewise already there from NC-042 and reused.
- **Five of the ten evidence rows are declared and cannot yet fire.** `CapturedOrders` is NC-053's, the two denials and `ExposedFalseDenial` are NC-054's, `MarkedGoodsSold` is NC-055's. Declaring all ten now is what keeps the store and the wire from being renumbered three times over the next three tasks — the same argument that had `ReportSource::CapturedCourier` declared before anything wrote one.
- **`Tuning::INCIDENT_OPEN_TICKS` is new and is a decision the GDD does not make.** §6 names no horizon and one is needed: a rule that re-weighed every incident every day forever grows evidence rows without bound over Milestone 2's decades, and an empire still re-litigating a raid from four years ago is not what §9 describes. It is a month, matching §9's own clean period, and it is a tuning value with its section cited.
- **`Memory::StepTo` was added** so that acting is one move and one event. §6's action revokes a claim outright; walking a company up three steps would put three lines in a receipt about one decision. `StepUp` and `StepDown` are unchanged.
- **Nothing produces an `Incident` yet.** Raids are NC-055's and battles are NC-062's, so `ResolveDailyInference` finds nothing in every existing test and `InferenceTests` builds incidents and reports by hand. That is the same honest position NC-051 was in, and it is why the daily phase costs nothing measurable.
- **`Accusation`'s wire conversion takes the evidence lines as an argument** rather than reading them out of `Knowledge`. `Accusation.h` may include a Wire header but a Wire header may not include a reality one (ADR-001), and the ids in an accusation mean nothing to a client — what a panel draws is the words.

### What was verified, and what was not

**Verified here:** `python3 Build/CheckFormat.py` (196 files, clang-format 18.1.3) and `python3 Build/CheckProjectFiles.py` (9 projects) clean. **clang-tidy 22.1.8**, CI's pinned version, over every changed `.cpp` — clean. **All 144 `GameLogicTests` methods compiled and run** at `-O1 -D_DEBUG` with clang 18.1.3 against a local stand-in for `CppUnitTest.h`: the 133 that existed after NC-051 and 11 new.

**Measured:** the NC-048 soak year, back to back against `85f8232` on one machine state, three alternating rounds — NC-051 0.187 / 0.155 / 0.194 s, NC-052 0.158 / 0.162 / 0.207 s. Indistinguishable, and expected to be: with no incidents in the world the daily inference pass is a loop over an empty table.

**Not done, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build, no executable run. There is no Windows on this agent.

**Assumed:** that "at the time" is a day either side of the incident (`EVIDENCE_WINDOW_TICKS`), that "within two jumps" is `EVIDENCE_WITHIN_JUMPS`, and that an alibi needs four jumps of separation (`EVIDENCE_ALIBI_JUMPS`) so that the positive and negative rows cannot both fire on one sighting. All three are R20 tuning values in the §6 block citing the section; play answers them without touching code.

**Bent:** nothing.

### Noticed and left alone

- **Evidence rows accumulate daily while an incident is open.** Each day's pass replaces a suspicion's list and leaves the previous day's rows in the table, as every other table in the tree does. `INCIDENT_OPEN_TICKS` bounds it to a month of them per suspect per incident. It is the same shape of problem as the dead fleet rows NC-048 named and `LatestAbout`'s backwards walk in NC-050, and one answer would serve all three.
- **A suspicion is created the first time anything at all points at a suspect**, not only at forty. Below forty an empire "suspects and says nothing" (§6), which is a state that has to exist somewhere for the number to climb out of. A row is not created when the sum is zero, so an empire does not hold a belief about every company it has ever seen.
- **`DISCRETION_PENALTY` is declared and nothing spends it.** §6's exposed-false-denial row carries "and a region-wide discretion penalty"; that penalty is NC-054's to apply, and the constant is here so NC-054 does not invent a second one.
- **An empire can be a suspect and the arithmetic is the same.** §6's "empires raid each other's convoys unmarked" is what makes ambiguity generated rather than scripted, and `CollectEvidence` takes either kind of suspect. NC-055 is what produces those raids; nothing exercises the empire arm yet beyond the type check.
