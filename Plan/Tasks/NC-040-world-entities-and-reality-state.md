# NC-040 — The world: entities and reality state

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 2 | GameLogic | L | no | no | Done (cd0c9ac) |

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

**Phase 2 has a spine.** Eleven headers, one translation unit and eight tests: every entity GDD §15 puts in v0.1 has a record, the tables are indexed by typed ids, the whole world serializes and hashes, and `GameLogic.lib` has its first real code. `GameLogicSmoke.cpp` and `GameLogicTests/SuiteSmoke.cpp` are both deleted, which was their whole contract.

**Refined against the code as it is.**

- **The ids live in one header, `EntityIds.h`, and could not live anywhere else.** `Plan/Glossary.md` put `CompanyId` in `Company.h` and `FleetId` in `Fleet.h`. That is not buildable: a `Company` holds `FleetId`s, a `Fleet` is owned by a `CompanyId` or an `EmpireId`, a `Character` is allegiant to either, and an `Outpost` is owned by any of them — the four headers would have to include each other. The **type names are the Glossary's, unchanged**; only their home moved, and the Glossary now says so in every affected row. `SystemId`, `LaneId` (NC-041) and `EventId` (NC-042) are there too, because `Fleet` already has to say where a fleet is and what happened to it.
- **`Table<T, IdType>` is the pattern the task asked me to fix**, rather than four hand-written methods per entity. NC-041 adds a table in one line. Rows are only ever appended, which is what makes an id a permanent handle and what the *entities are never deleted* convention actually needs.
- **`Table` exposes `Rows()` and not `begin()`/`end()`.** clang-tidy rejected the lowercase pair — R1 makes every method PascalCase and the check is tree-wide with no exception. A `std::span` named `Rows()` satisfies both rules without a `NOLINT` and without touching `.clang-tidy`, which is an owner decision (§2). `for (const Fleet& fleet : world.Fleets().Rows())` reads no worse and says which sequence it walks.
- **`SHIP_CLASS_STATS` is in `ShipClass.h`, not a new `Tuning.h`.** The task allowed either and asked only for one home and not two. It sits beside the enumerator that gives the array its length; **NC-042 may move it into `Tuning.h`, and should decide that rather than inherit it.** Every number in it is a guess, says so, and cites GDD §12 and §5 (R20).
- **The store schema is entirely inside `World.cpp`'s anonymous namespace**, declared in no header, so a reader who wants to know what a save contains reads one file. It is deliberately *not* the `Wire*.h` seam: that is what the client may see (NC-042, R18), and confusing the two would be the exact mistake R18 exists to prevent.
- **`Hash()` is FNV-1a over the bytes `Serialize` wrote**, not a field-by-field hash. That makes "equal hashes" and "equal stores" one statement, so a field `Serialize` forgot cannot hide from the determinism harness — which is the failure mode a hand-written hash has, and NC-043 is about to depend on this.
- **`ActiveWindow` landed here rather than in NC-066.** A5 puts it on the company, `Company` is this task's record, and a field added later is a schema version bump. NC-066 fills the reinforcement timer that reads it.
- **No `Credits` helpers were written.** The task asked for "the strong-typed helpers the ADR of NC-012 named"; ADR-003 names `SaturatingAdd` and `SaturatingSub`, which already exist in `IntegerMath.h` and already take this width. A second set wrapping them would be the second home the task warns against.

**The acceptance criteria, checked.**

- Every record is a public aggregate with plain fields (R8); `World` is the only class, and it has invariants — tables only grow, the tick only advances, the streams are forked once.
- `World` round-trips to an equal hash with every table populated, and `EveryFieldReachesTheStore` proves the hash *notices* each field rather than merely surviving a copy.
- **No `float`, `double`, `std::unordered_*` or `<chrono>` in any GameLogic header**, verified with comments stripped first — the headers discuss floats a good deal and a naive grep would have found only prose.
- Nothing in `World` records a belief, a report, an opinion or a confidence. The one presentation field is `Empire::colorSlot`, named as such, which the simulation never reads.
- The nomad is a table and the test creates two (R22).

**On making the R16 grep mechanical**, which the task left to the reviewer: I checked it by hand and did **not** add it to `CheckProjectFiles.py`. A grep for `float` over these headers hits eight comments explaining why there is no float, so the check would have to strip comments to be useful — which is more C++ parsing than that checker does anywhere else. It is worth doing when NC-042 brings the resolver, and the three-line script I used is in this task's history.

**Verified:** `CheckFormat.py` (126 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**54 translation units clean**, on 22.1.3 against CI's pinned 22.1.8). Debug **and** Release rebuild with zero warnings. All four suites: **183 of 183 green**, 8 of them new here. Desktop run: not applicable and not claimed — nothing in this task reaches the client.

**Two defects in my own tests, found before they were committed**, both worth naming because they are the kind a green suite hides: a placeholder method that asserted nothing but its own local variable, and an `AreNotEqual` of two PRNG draws passed as two function arguments — whose evaluation order C++ leaves unspecified, so a test of a generator would have depended on the compiler. Both are fixed; the second is now sequenced into locals with a comment saying why.

**Assumed:** that an outpost's claim expires on a tick rather than on a date (`claimExpiresTick`). GDD §7 and R21 make every timer a tick count, so this is the rule applied rather than a choice, but NC-066 owns the field and may want a duration instead of an absolute.

**Bent:** nothing. The one rule that pushed back — R1 on `begin`/`end` — was obeyed rather than suppressed.

**Noticed and left alone.**

- **`Allegiance::IsValid()` is written and never called.** `Deserialize` does not enforce it, because a store is read before the tables it points into are complete and a cross-table invariant cannot be checked field by field. A whole-world validation pass belongs with NC-041's generator, which is the first thing that can produce a world worth validating.
- **`ShipStats::jumpTimeHundredths` is a hundredths *multiplier* held as a `std::uint32_t`, not a `Neuron::Hundredths`.** `Hundredths` is signed and clamps to a 32-bit range on multiply; a lane time scaled by it would be doing fixed-point arithmetic where a plain `MulDivRound` is exact and obvious. NC-044 owns the mobility arithmetic and should confirm that reading.
- **`Table::Resize` exists only for `Deserialize`** and default-constructs rows, so every record needs a sensible default. They all have one today. A record that later gains a member without one fails to compile here rather than silently — the right failure, but worth knowing where it comes from.
