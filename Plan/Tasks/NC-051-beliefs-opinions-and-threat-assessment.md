# NC-051 — Beliefs, opinions and the threat assessment

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Open |

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

- [ ] A belief's confidence and stage are the only things an empire's action reads (NC-052 enforces; here the types make `Incident::culprit` unreachable from `Belief`).
- [ ] Two companies can be suspected of one incident with independent confidences (R22).
- [ ] The overwrite rule steps the assessment down exactly once per qualifying contract and once per clean thirty days, never below the floor step.
- [ ] Successor inheritance moves the tuned fraction of opinion and all of the record, and the event says who replaced whom (GDD §8).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

The inference arithmetic (NC-052), the hunt (full game; the step that would trigger it is declared and inert), layered memory (full game).

## Notes

- The Glossary says why `Belief` is keyed by empire now and can be keyed by character later without touching NC-052: the inference rule takes a `Belief&`, whoever owns it.

## Report

_Filled in on hand-back._
