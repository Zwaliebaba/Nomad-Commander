# NC-041 — Systems, lanes, roles and the generator

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | M | no | no | Open |

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

- Positions are for the map screen only; keep them integers on a 1280×720 grid with margins so NC-072 draws them unchanged.

## Report

_Filled in on hand-back._
