# ADR-020 — Detection and report noise

**Status:** Accepted
**Date:** 2026-09-17
**Task:** NC-050
**Cites:** GDD §4 (*Intelligence*: source, age, reliability; "the AI sees the player through the same fog"), §9 (*Reality, belief and evidence*), §12 (sensor range per class), §3 (the nine-hour-old sighting); AGENTS.md R16, R18, R20; ADR-001 (the wire seam), ADR-002 (the pinned PRNG), ADR-004 (byte schema)

## Context

R18 asks that reality, belief and evidence be distinct types and that no decision routine be able to reach ground truth. Until now that was true by absence: there was nothing for an admiral to believe, so nothing could cheat. This is the task that creates the second category, and every later one — an empire's suspicion (NC-052), an admiral's plan (NC-060), the board and every panel on it (NC-067, NC-074) — reads what is decided here and never a `World` again.

What had to be settled: **what a sighting contains, how wrong it is allowed to be, when it arrives, and where reliability comes from.**

## Decision

1. **A report is written when something changed, not on a clock.** `Sensor::ResolveDetection` runs as phase 3 of the tick, immediately after movement, and reports only on the fleets this tick's `FleetArrived`, `FleetDeparted` and `FleetDrifting` events name. A sighting of a fleet that has not moved says nothing the last one did not, and a report a tick would be four hundred thousand rows a simulated year that no board could hold. **This is also why GDD §3's sighting can be nine hours old**: nothing has been seen since. Measured: **299 reports over a simulated year** on a ten-system, three-empire map.
2. **An observer sees only what a hull of its own could see.** The range is the best `ShipStats::sensorRangeJumps` among the hulls a fleet actually holds, and the distance is `World::JumpsBetween` — the map is a graph and has no metric (GDD §7). An observer with no fleets learns nothing, however much happens.
3. **Counts are spread by distance and identity is not.** A sighting's per-class counts are drawn from the pinned PRNG's `Detection` stream across a band of `Tuning::SIGHTING_NOISE_HUNDREDTHS_PER_JUMP` per jump, symmetric so a sighting is as likely to overstate as to understate, and never below one when something was there. **Identity is binary and not noisy**: known when the fleet is marked or shares the observer's system, and absent otherwise (GDD §6). A half-known identity would be a probability the client could read as information.
4. **Reliability is the source's track record and is computed from nothing else.** `SourceRecord::Reliability()` takes no world, no report and no subject, so there is no path by which it could consult whether the report happened to be right. An unchecked source reads as `UNPROVEN_RELIABILITY_HUNDREDTHS` (50) rather than as zero or as certainty, because "nothing is known about this source" and "this source is wrong" are different things to put in front of a player. Each report also carries the reliability **as it stood when it was written**, so a board shows what its reader was entitled to think at the time.
5. **A report is checked on counts, never on position.** When an observer later sights the same subject *in its own system* — where the count is exact — the most recent unchecked report is confirmed or contradicted against it. A fleet that moved is not a source that lied, so position is not evidence of anything; GDD §4's "a battle contact reveals counts" is. A report is checked at most once, so one bad long-range guess costs its source one mark.
6. **Intelligence travels.** `deliveredAtTick` is `observedAtTick` plus `Tuning::COURIER_TICKS_PER_JUMP` per jump from the sighting to the observer's desk — an empire's capital, a company's mothership. A sighting in the observer's own system is on the desk at once (GDD §4: "within the mothership's own system, orders are instant"). Nothing may act on a report before it is delivered, and `Politics::Believe` counts only delivered ones.
7. **Age is measured from observation, not delivery.** GDD §3's "nine hours old" is what should worry a reader, and a report that took six hours to arrive is still nine hours old. The client is sent both ticks and computes the age itself, because the answer changes every tick and the report does not.
8. **All seven `ReportSource` values are declared now**, though only `OwnSensors` and `Picket` can be produced yet. The order is the wire schema and the store's (ADR-004); `CapturedCourier` is NC-053's and the tips and briefings are NC-056's, and inserting an enumerator later would renumber every save.

## What this forecloses

- **Perfect information from any source.** There is no report without a hull that could have written it, and no field anywhere that says whether one is correct. A later task that wants certainty has to add a *source* good enough to have earned it, which is the design's own answer (GDD §4's captured courier is worth more than a rumour because of what it is, not because the game says so).
- **Asking whether a report is true.** `WireReport` carries no truth field and `Report` carries none either; the only thing resembling one is the source's record, which is about the source. Adding one would be a change to this decision rather than a field.
- **A sighting of a fleet standing still.** Detection is driven by movement events, so a fleet that parks is reported once and then not again until it moves. That is deliberate — it is what makes an old sighting old — but it means a besieging fleet that sits for a week generates no fresh intelligence about itself, and a later task that wants a periodic sweep is changing this.
- **Knowing *who* without a mark or a meeting.** `identityKnown` has no middle value, so a task that wants "probably the Varn" has to add evidence (NC-052's business) rather than soften this.
- **Detection reading more than one tick of events.** `ResolveDetection` takes a span of this tick's events rather than the caller's whole vector. That is load-bearing: `NomadSimulation` clears its events only when something drains them, so a headless year hands it a vector that grows all year, and scanning it per tick made a simulated year quadratic. Measured below.

It does **not** foreclose the courier becoming real: NC-053 replaces point 6's arithmetic with a courier that can be intercepted, and `Tuning::COURIER_TICKS_PER_JUMP` is the number it should be measured against rather than a second opinion beside it.

## Consequences

- `GameLogic/Report.h` (`Report`, `SightedFleet`, `SourceRecord`, `ReportSource`, `Observer`, `ToWire`), `GameLogic/Sensor.h`/`.cpp`, `GameLogic/WireReport.h`.
- `World` gains a `Reports` table and `World::SCHEMA_VERSION` goes to **7**; `Company` and `Empire` each gain a `SourceRecord` per source, which is the first raw POD array either has held.
- **`SourceRecord`'s fields carry initializers, unlike most aggregates in this tree.** Four tests built a company as `Company c;` rather than `Company c{}`, which had been harmless while every plain member was assigned explicitly; a POD array is not, and the first symptom was `DeterminismTests` failing because two runs read different stack garbage into a reliability. Both halves are fixed: the type initializes itself, and the four sites value-initialize.
- `BelievedSituation` gains `sightedForeignHulls` and `reportsRead`, built from delivered reports only. It is the first field on that type that is not simply something the empire owns.
- `TickResolver` phase 3 is no longer empty, and it now remembers where each tick's events begin.

## Measurements

Taken with clang 18.1.3 on an Intel Xeon at 2.10 GHz (4 vCPU, Ubuntu 24.04), `-O2 -DNDEBUG`, on a generated ten-system three-empire world run for one simulated year through `NomadSimulation::Advance`. Timings are back-to-back pairs on one machine state, which is the only form that means anything on this agent (ADR-019's caution).

| | |
|---|---|
| Reports written in a simulated year | **299** |
| Store size after that year | 43,699 bytes (12,097 before) |
| A simulated year **without** detection (NC-049 head) | **0.112 s** |
| A simulated year **with** detection | **0.104 s** |

**Detection costs nothing measurable** — the two figures differ by less than the run-to-run spread, and the "after" is nominally the faster of the two. That is the point of decision 1: reporting on change rather than on the clock keeps the work proportional to what happened.

**It was not free in the first implementation, and the difference is worth recording.** `ResolveDetection` originally took the caller's whole event vector and filtered it by tick. A simulated year then took **0.287 s against 0.112 s** — a factor of 2.7 — because the vector is never drained in a headless run and the scan is therefore quadratic in the length of the run. Handing detection a span of this tick's events removed all of it. A cost that looks like "detection is expensive" and is really "an accumulating buffer was scanned per tick" is exactly the kind of thing a measurement finds and a review does not.
