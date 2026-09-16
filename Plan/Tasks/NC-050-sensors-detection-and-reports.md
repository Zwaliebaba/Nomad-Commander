# NC-050 — Sensors, detection and reports

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 3 | GameLogic | M | no | no | Open |

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

- [ ] Reliability is computed from `SourceRecord` and from nothing else; the test constructs a truthful source with a bad record and confirms it shows low reliability (GDD §4: track record, never the truth).
- [ ] `BelievedSituation` cannot be built from a `World` in a client-visible or decision-facing path; only `Sensor.cpp` reads `World` to write reports (R18).
- [ ] Every report has an origin tick and a delivery tick, and the client's "age" is from the origin (GDD §3: "nine hours old").

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

**ADR — detection and report noise.** The Roadmap's recommendation; what it forecloses: perfect information from any source.

## Out of scope

Beliefs about incidents (NC-051), purchasable tips as board items (NC-067), rumours (full game).

## Notes

- The "captured courier" source is created by NC-053; declare the enumerator now so the wire schema does not renumber.

## Report

_Filled in on hand-back._
