# NC-040 — The world: entities and reality state

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | no | Open |

**Depends on:** NC-010, NC-011, NC-012, NC-013
**Read first:** GDD §5 (hulls, the mothership), §8 (empires, leaders, admirals), §9 (reality distinct from belief), §11 (the career, outposts, officers), §12 (fleets, four classes), §14 (the nomad as an entity type); AGENTS.md R9, R16, R18, R22; `Plan/Glossary.md` whole; `Plan/Roadmap.md` *Conventions* (entities never deleted; vectors indexed by id)

## Goal

The schema of reality. Every entity GDD §15 puts in v0.1 has a record here with the fields the later tasks will fill, stored in tables indexed by typed ids, serializable as a whole, hashable, and free of anything that is belief (R18) or presentation. Later tasks add fields to these records; this task fixes their existence, their ids and the pattern for adding a table.

## Deliverables

- `GameLogic/Credits.h`: `using Credits = std::int64_t;` and the strong-typed helpers the ADR of NC-012 named.
- `GameLogic/ShipClass.h`: `enum class ShipClass : std::uint8_t { Scout, Raider, Warship, Hauler }`, `SHIP_CLASS_COUNT`, `struct ShipCounts` (an array by class with `Total()`, `Add`, `Remove`), and the per-class table `SHIP_CLASS_STATS` (speed, fuel per jump, sensor range in jumps, cargo, combat strength, daily upkeep, hull price base) as a `Tuning`-style `constexpr` table citing GDD §12 and §5 (moved into `Tuning.h` by NC-042 if that reads better; one home, not two).
- `GameLogic/Company.h`: `struct Company` (R22: the nomad entity) with `CompanyId`, name, `Mothership`, treasury `Credits`, officer ids, fleet ids, outpost ids, the record (a vector of event ids), `ActiveWindow` (NC-066 fills it), `alive`.
- `GameLogic/Mothership.h`: location, `MothershipState` enumerator (`Healthy` used; the others declared for the full game, per Roadmap *Beyond v0.1*), reserve fuel, fabricator (NC-046 fills it).
- `GameLogic/Empire.h`: `EmpireId`, name, leader `CharacterId`, home system, colour slot, systems held, fleet ids, relations (NC-047), tolerance and fees per company (NC-051/066), `alive`.
- `GameLogic/Fleet.h`: `FleetId`, name, owner (`std::variant<EmpireId, CompanyId>`), `FleetRole` (`Operational`, `Convoy`, `Picket`, `Scout`), commander `CharacterId`, `ShipCounts`, fuel, cargo by good, `FleetPosition` (`AtSystem{ SystemId }` or `InLane{ LaneId, from, departureTick, arrivalTick }` or `Drifting{ SystemId }`), marked flag, veterancy in hundredths, history (event ids), `alive`.
- `GameLogic/Character.h`: `CharacterId`, name, `CharacterRole { Leader, Admiral, Officer }`, allegiance (empire or company), `commandCapacity` (officers), traits (NC-060), opinions (NC-051), `alive`.
- `GameLogic/Outpost.h`: `OutpostId`, owner, system, stock by good, docked `ShipCounts`, `GovernorPolicy` (NC-066), `Claim` (NC-066), `alive`.
- `GameLogic/World.h` + `.cpp`: `class World` holding the tables (`std::vector<T>` per entity, `Add`, `Get(Id)`, `Count`), the clock (`Tick`), the `Random` (with `Fork` per subsystem), the seed; `Serialize`/`Deserialize` over every table in a fixed order; `Hash()`. Systems and lanes are added by NC-041; markets by NC-045; couriers, contracts, incidents, beliefs by their tasks, following this file's pattern.
- `GameLogicTests/WorldTests.cpp` (deletes `SuiteSmoke` there): add one of each entity, serialize, deserialize, compare hashes; an id from one table does not index another (compile-time test).

## Acceptance criteria

- [ ] Every record is a public aggregate with plain fields (R8) except `World`, which has invariants and `m_` members.
- [ ] `World` round-trips through bytes to an equal hash with every table populated.
- [ ] No `float`, no `double`, no `std::unordered_*`, no `<chrono>` in any GameLogic header (R16); NC-004 may gain a grep for these if the reviewer wants it mechanical.
- [ ] Nothing in `World` records a belief, a report or an opinion of anything; those are NC-050/051's types (R18).
- [ ] The nomad is a table with any number of rows (R22); the test creates two companies.

## Verification

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64
python Build\CheckProjectFiles.py
```

## Decisions to record

None. The Glossary fixed the names; the numeric model is NC-012's ADR.

## Out of scope

Behaviour of any kind. Systems and lanes (NC-041). Any tuning value not needed to declare a record.

## Notes

- `alive = false` rather than erasure: the record and the dossiers (GDD §8, §11) refer to dead admirals and lost fleets.
- Per-subsystem `Random` forks live in `World` so a replay is exact even when a later task adds a consumer.
- `FleetPosition` as a `std::variant` serializes by index then payload; the variant order is part of the store's schema version.

## Report

_Filled in on hand-back._
