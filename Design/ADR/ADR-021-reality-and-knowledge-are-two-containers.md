# ADR-021 — Reality and knowledge are two containers

**Status:** Accepted
**Date:** 2026-09-17
**Task:** NC-051
**Cites:** GDD §4 (*Intelligence*: source, age, reliability), §6 (the inference rule), §9 (*Reality, belief and evidence*), §15 (measured outcomes); AGENTS.md R18, R19, R22; ADR-004 (byte encoding and versioning), ADR-018 (the wire schema)

## Context

R18 is the rule the whole design hangs off: "an admiral plans against reports about the player's fleet, not against its true position and strength." `World.h` has said since NC-040 that belief, reports, opinions and evidence "are held beside a World rather than inside one … the way that rule is kept structural is that a decision routine takes belief and there is no path from belief to here." Until NC-050 that sentence was true by absence — there was nothing anyone believed, so nothing could cheat.

**NC-050 made it false, and nothing noticed.** Detection needed somewhere to put its reports, `World` was the container that existed, and a `Table<Report, ReportId>` went into it. It compiled, passed 114 tests and two checkers, and contradicted a paragraph nobody had changed. That is the failure mode this decision exists to close: a rule kept by remembering it will be broken by the next task that needs a table, and the break will look exactly like ordinary work.

NC-051 then adds four more belief-side types — `Suspicion`, `Belief`, `Opinion`, `ThreatAssessment` — and the track record that NC-050 had put on `Company` and `Empire`. What had to be decided is where they live, and how the separation is enforced by something other than review.

## Decision

1. **Two containers, and the second one is `Knowledge`.** `World` is reality: systems, lanes, fleets, markets, companies, empires, characters, outposts, mothballs, relations and incidents. `Knowledge` is everything anybody has worked out: reports, beliefs and their suspicions, opinions, threat assessments and observer track records. Neither holds the other, and `Knowledge` has no accessor that returns anything of the world's.

2. **The seam is an id and nothing else.** An `Incident` is reality and carries `culprit`, the one field in this tree that is ground truth about who did something. A `Suspicion` is belief and carries a *suspect*. They are joined by an `IncidentId`. There is no member on either that leads from the second to the first, so the comparison the design wants counted — GDD §15's "misattributions per ten hours" — can only be made where NC-052 writes its `Misattribution` log line, which is the one place it belongs.

3. **A routine takes what it is allowed to read, as a parameter.** `Sensor::ResolveDetection` is the only function handed both, and it reads the first to write the second. `Politics::Believe(const World&, const Knowledge&, EmpireId)` takes both because an empire genuinely knows its own holdings and knows about everybody else only what it was told. Everything downstream — `ChooseAnEnemy`, NC-052's inference, NC-060's admiral — takes belief and cannot reach reality through it.

4. **Each half carries its own schema version and its own hash.** `World::SCHEMA_VERSION` and `Knowledge::SCHEMA_VERSION` are separate `std::uint16_t`s and move independently; `World::Hash()` and `Knowledge::Hash()` are the same FNV-1a over each half's own bytes. `NomadSimulation::WriteState` writes reality and then belief, so one store carries both and `Simulation::StateHash()` still covers everything.

5. **`Knowledge::Seed` fills up to the empire count rather than guarding on emptiness**, and the resolver calls it every tick. An empire with nowhere to put a suspicion is a bug nobody sees until the first incident; a `Knowledge` built beside a world somebody else generated is the common case — every test and the executable both — and the generator takes a `World&` and knows nothing about belief.

## What this forecloses

- **One object for the whole simulation state.** Every call that advances or inspects the game now names both halves, and a function that wants both has to say so in its signature. That is the cost, and it is the point: the signature is the enforcement.
- **Moving a field between the halves quietly.** A field that migrates changes both schema versions and invalidates every store, because a reader of either half cannot tell a moved field from a missing one.
- **A `World`-only determinism check.** `TickResolverTests` compared `World::Hash()` alone; after this it must compare both, or a detection phase that drew from the PRNG differently in two runs would no longer be seen. NC-050 shipped exactly that defect and R16's harness caught it (NC-050's report); a harness that hashed half the state would not have.
- **A client that is handed the second container.** `Knowledge` is host-side like the rest of `GameLogic` (AGENTS.md §2). What crosses to the client is still `WireReport` and `WireEvent` and nothing else (ADR-018) — the split does not make belief safe to ship, it makes reality impossible to reach.

## Consequences

- `World::SCHEMA_VERSION` goes 7 → 8: reports and their source records left, `Incident` arrived.
- `Company::recordBySource` and `Empire::recordBySource` are gone; `Knowledge::ObserverRecords()` holds them, keyed by an `Observer` variant. A track record is something an observer worked out, which makes it belief.
- `Sensor::ReliabilityOf` is gone as such; `Knowledge::ReliabilityOf` answers it, taking no world at all. `RecordOutcome` and `DeliveredTo` likewise take a `Knowledge&` and no world.
- `TickResolver::Advance` gains a `Knowledge&` as its second parameter, and every call site names it — fourteen in the test suite and one in `NomadSimulation`.
- If this is reversed, the reports and the four belief types move back into `World`, both schema versions collapse into one, and R18 goes back to being a rule kept by review. The tests that assert the separation structurally (`BeliefTests::TheCulpritIsInTheWorldAndUnreachableFromABelief`) are what would have to be deleted to do it, which is the intended cost.

## Measurements

A simulated year of the NC-048 soak — ten systems, three empires, seed `0x50A4`, 525,600 ticks — built with clang 18.1.3 at `-O1 -D_DEBUG` against a Linux stand-in for `CppUnitTest.h`, run back to back on one machine state. Three rounds, alternating, in the same shell:

| Round | `d3c44b4` (before) | NC-051 |
|---|---|---|
| 1 | 0.199 s | 0.156 s |
| 2 | 0.184 s | 0.188 s |
| 3 | 0.157 s | 0.172 s |

The two are indistinguishable: the spread within each build is larger than the difference between them. The split costs a parameter, not time — the containers are the same tables, in two objects instead of one.

**Do not compare these to the 0.104 s in NC-050's report.** That figure was taken in a different session on a shared vCPU, and NC-049 already produced one phantom 3× regression that way. A timing claim here is a back-to-back pair or it is nothing.
