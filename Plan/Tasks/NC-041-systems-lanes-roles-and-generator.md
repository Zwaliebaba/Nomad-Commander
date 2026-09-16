# NC-041 — Systems, lanes, roles and the generator

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | M | no | no | Done (PENDING) |

**Depends on:** NC-040
**Read first:** GDD §7 (the graph, the roles, jump durations), §10 (what each role produces), §15 (about ten systems, three empires; Milestone 2: about twenty and five); AGENTS.md §2 (GameLogic: "the universe graph and its generator")

## Goal

The map as data and the generator that makes one from a seed: systems with the seven roles, lanes with jump times of two to four hours, a connected graph whose shape gives the roles their meaning (a chokepoint is an articulation point; a dead end has one lane; a crossroads has four or more), and the starting ownership by empires. The Kessel scenario's map (NC-090) is hand-written data in these same types.

## Deliverables

- `GameLogic/StarSystem.h`: `SystemId`, name, `SystemRole { Chokepoint, Bypass, DeadEnd, ResourceHub, Refinery, SafeHarbor, Frontier, Crossroads }`, map position (integer coordinates for the client; the sim never reads them for logic), owner (`EmpireId` or none), lane ids, `hasShipyard`, `alive`.
- `GameLogic/Lane.h`: `LaneId`, the two systems, `jumpTicks` (120–240), `fuelMultiplierHundredths`.
- `World` gains the two tables and `Adjacent(SystemId)`, `JumpsBetween(a, b)` (BFS, deterministic), `ShortestRoute(a, b, out)`.
- `GameLogic/UniverseGenerator.h` + `.cpp`: `struct Desc { std::uint32_t systemCount; std::uint32_t empireCount; std::uint64_t seed; }`, `Generate(const Desc&, World&)`: jittered grid placement, a spanning tree plus extra lanes for the bypasses, role assignment from graph shape with at least one of each role when the count allows, jump times from lane length, empire home systems far apart, contiguous holdings, one or two unowned harbours (GDD §8: the contraction that produces the nomad's harbours has happened before the game starts).
- `GameLogicTests/UniverseGeneratorTests.cpp`: connectivity, role invariants, determinism across two generations from one seed, every empire has a home with a shipyard, jump times within bounds, twenty systems and five empires also generate (Milestone 2's shape, so the generator is not sized to ten).

## Acceptance criteria

- [ ] Ten systems and three empires generate with every role present and the graph connected, for one hundred consecutive seeds.
- [ ] Role invariants hold by construction and are asserted by the test (chokepoint removal disconnects; dead end degree one; crossroads degree four or more).
- [ ] Two runs from one seed produce byte-identical worlds (R16).
- [ ] No lane has `jumpTicks` outside 120–240 (GDD §7: two to four real hours).

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

**ADR — universe generation.** The Roadmap's recommendation; what it forecloses: a map editor, hand-placed sandboxes.

## Out of scope

Production flows (NC-045), goals (NC-047), the Kessel map (NC-090).

## Notes

- Positions are for the map screen only; keep them integers on a 1920×1080 grid with margins so NC-072 draws them unchanged.

## Report

**The map is a graph whose roles are true of it, and that was the decision worth taking.** GDD §7 names eight roles and never says what they are; [ADR-017](../../Design/ADR/ADR-017-universe-generation.md) makes four of them claims about the graph — a chokepoint’s removal disconnects the map, a dead end has one lane, a crossroads has four or more, a bypass has a way around it — and guarantees each by construction rather than by luck. The other four are economic and are placed.

**Measured over 100 consecutive seeds**, and the figures are in the ADR: all eight roles present on 100 of 100 maps, every map connected, every structural claim true when the test walks the graph itself rather than reading the role back, no lane outside the two-to-four-hour band, and no seed where two runs differed by a byte. The Milestone 2 shape — twenty systems, five empires — generates and connects on 20 of 20, so the generator is not sized to ten (A13).

**Refined against the code as it is.**

- **`Desc` has no seed.** The task gave it one; the world already carries the seed it was constructed with, and a `Desc.seed` that disagreed with `World::Seed()` would be a second source of truth for the one thing R16 depends on. `Generate` draws from the world’s `Generation` stream instead, which is what NC-040 put that enumerator there for.
- **`MIN_SYSTEM_COUNT` is 8, and it is arithmetic rather than taste**: four systems are spoken for by the graph’s shape and one of each economic role needs four more. The task said "at least one of each role when the count allows"; this is what "allows" turned out to mean, and `Generate` refuses below it rather than producing a map that quietly lacks a role.
- **Distances are compared squared and never square-rooted.** An integer square root rounds two different lengths to one, and the nearest-neighbour choices that build the tree would then depend on that rounding (R16).
- **The dead end is held out of the spanning tree entirely** and excluded by name from every later lane. That is what makes its single lane a guarantee rather than an observation — and its one neighbour is then an articulation point, which is where the chokepoint comes from for free.
- **A lane’s jump time is interpolated on the *squared* distance**, which compresses the short end. On a jittered grid most lanes are short, and a linear interpolation put almost every lane at the two-hour floor, which would have made "depending on the lane" mean nothing.
- **`World` gained `Adjacent`, `JumpsBetween` and `ShortestRoute`**, and the breadth-first search visits neighbours in lane order. That is the whole of what makes a route deterministic: the route found is not merely *a* shortest one but the same one every run.
- **`SCHEMA_VERSION` went to 2.** Two tables were appended; nothing has been stored yet, but the rule NC-040 wrote into `World.cpp` says a layout change bumps it, and a rule that is skipped the first time it costs something is not a rule.

**The acceptance criteria, checked.** Ten systems and three empires with every role present and the graph connected, for 100 consecutive seeds — yes, and the test names the seed when it fails. Role invariants asserted by walking the graph — yes, all four, with an independent reachability walk written in the test rather than calling `ConnectedWithout`. Two runs from one seed byte-identical — yes, hash *and* bytes, plus the converse check that two seeds differ. No lane outside 120–240 ticks — yes.

**Verified:** `CheckFormat.py` (131 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**56 translation units clean**). Debug builds with zero warnings. `GameLogicTests`: **17 of 17 green**, 9 new here. Release was not built for this task — it is integer arithmetic with no floating point and no optimisation-sensitive construct; NC-048’s soak is the place to check that claim under optimisation.

**Assumed:** that "far apart" for empire homes means jumps rather than pixels. The map is a graph and has no metric (the coordinates are the client’s), so this is the rule applied rather than a choice — but it does mean two homes can be close on screen and far by lane, which is correct and will look odd on NC-072’s map until a player traces the route.

**Bent:** nothing.

**Noticed and left alone.**

- **The crossroads is also empire zero’s home**, because the greedy far-apart search starts there. It is legal and even thematic, but it means the first empire always holds the busiest system on the map, which is a balance property nobody has played against. NC-090’s hand-authored Kessel map does not inherit it, and NC-103 is where it would show up.
- **`ConnectedWithout` is brute force** — a full reachability walk per candidate — and the bypass search calls it up to `systemCount` times. At ten to twenty systems that is nothing; the comment names it as the thing to replace if `MAX_SYSTEM_COUNT` grows by an order of magnitude.
- **A lane’s `fuelMultiplierHundredths` is written and read by nobody yet.** NC-044 is what spends it; it is here because a lane that gained the field later would be a schema bump.
