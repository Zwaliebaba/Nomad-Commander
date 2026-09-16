# NC-090 — The Kessel scenario data

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 6 | GameLogic | M | no | no | Open |

**Depends on:** NC-047, NC-060, NC-067
**Read first:** GDD §3 whole (every fact it states is a data item here), §15 (the three dilemmas), §8 (Varik's habit), §6 (the accusation at fifty-eight percent and Roadmap finding 1); AGENTS.md R13 (embedded data, no files); `Plan/Roadmap.md` A11

## Goal

The scripted scenario as compiled-in data: the map around Kessel, the three empires (Varn, Oren and a third the implementer names), the Varn siege of Kessel and its supply route, Varik with the traits that make bait his habit and a reserve his circumstance, the company's fleet, officer, outpost, credits and picket, the prior raid two days ago that the Oren flew unmarked, the resulting Varn accusation inside the accuse band, the convoy sighting nine hours old, and the market projection of about forty hours. The data reproduces GDD §3 at 0:00.

## Deliverables

- `GameLogic/Scenario.h`: `struct Scenario { name; seed; a function that populates a `World` (systems, lanes, empires, goals, relations, characters, fleets, outposts, markets, incidents, beliefs, reports, board items, clock); }`, `ScenarioByName(std::string_view)`.
- `GameLogic/KesselScenario.h` + `.cpp`: `KESSEL_SCENARIO`, with a header comment listing every name invented beyond the GDD's (A11) and every §3 fact and where it is set.
- Facts as data: the Oren raid contract (9,000 credits, on completion payable on their observation, deadline two days); the accusation with evidence: detected within two jumps, hull-class match, the Oren denial, the route conflict against, plus the prior pattern or testimony that lands the confidence between forty and seventy (finding 1; the comment says which); the convoy sighting from the picket, nine hours old, light escort; Varik's dossier from two engagements and one news item; Kessel's fuel projection of roughly forty hours from its stock and consumption; the company's docked counts (at least two raider wings' worth, a warship wing, a scout), an officer with capacity two, credits for fuel for three jumps at Kessel's price, and the Kessel outpost with a claim from the Varn (whose revocation is the stake); a Varn fleet under Varik near the jump point with a reserve, so that the 27:00 sighting can occur.
- `GameLogicTests/KesselScenarioTests.cpp`: the world at 0:00 satisfies each §3 fact (one assert per sentence, quoting it); the board holds the three items; the readings at 11:00 are the three; the plan at 19:00 validates with capacity two.

## Acceptance criteria

- [ ] Every §3 fact at 0:00 is asserted by a test that quotes the sentence.
- [ ] No file is read: the scenario is `constexpr` data or code (R13).
- [ ] The invented names are listed in one comment for the owner to rename.
- [ ] The scenario is deterministic from its seed under NC-043's harness.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64 /Tests:KesselScenarioTests
```

## Decisions to record

None.

## Out of scope

Starting the executable from it (NC-091), the prologue (full game).

## Notes

- The prior raid's culprit is the Oren on the reality side; the Varn's belief points at the company by the §6 rule from the evidence listed. That is the hook in data form; do not script the accusation, derive it (NC-052) from the incident and the reports.

## Report

_Filled in on hand-back._
