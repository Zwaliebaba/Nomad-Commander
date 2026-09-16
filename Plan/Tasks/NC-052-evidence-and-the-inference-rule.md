# NC-052 — Evidence and the inference rule

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | L | no | no | Open |

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

- [ ] `grep -n "40\|70\|25\|15\|30\|60" GameLogic/Inference.cpp` finds no tuning literal (R20).
- [ ] Confidence is a `Hundredths` and every weight is a table entry; no `float` (R16).
- [ ] The accusation event's explanation renders through `ExplanationText::Compose` into the §9 sentence shape with the same items the test supplied (R19).
- [ ] An empire never accuses a company it has no report about, however guilty (R18).
- [ ] `Misattribution` is logged with the incident, the suspect and the culprit's id.

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

_Filled in on hand-back._
