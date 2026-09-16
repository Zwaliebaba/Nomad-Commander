# NC-010 — Ticks and typed ids

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | S | no | no | Open |

**Depends on:** NC-002
**Read first:** GDD §7 (the clock), §14 (the nomad as an entity type); AGENTS.md §2 (NeuronCore's contents), R3, R5, R6, R21, R22

## Goal

The two primitives every later type is built on: `Tick`, the simulation's only clock, and `Id<Tag>`, the typed index that makes a `FleetId` and a `CompanyId` different types the compiler keeps apart. Both are trivially serializable and hashable, and neither knows what a fleet is.

## Deliverables

- `NeuronCore/Tick.h`: `using Tick = std::uint64_t;` with `TICKS_PER_HOUR`, `TICKS_PER_DAY` and a comment that one tick is one simulated minute (ADR by NC-014 makes it official; this file states the constant once, R20 style). Helpers `TicksFromHours`, `TicksFromDays` as `constexpr`.
- `NeuronCore/Id.h`: `template <typename Tag> class Id` over `std::uint32_t` with the `INVALID_INDEX` sentinel, `IsValid()`, `Index()`, a defaulted `<=>`, and a nested `Hash`. A default `std::hash` is a full explicit specialization per alias (`template <> struct std::hash<FleetId> : FleetId::Hash {};`), written where the alias is; a partial specialization in this header is what clang-tidy 22's `bugprone-std-namespace-modification` rejects. A `using` per entity kind is declared where the entity is (GameLogic), never here.
- `NeuronCoreTests/IdTests.cpp` and `TickTests.cpp`; delete `SuiteSmoke.cpp` there.

## Acceptance criteria

- [ ] `Id<A>` and `Id<B>` do not convert to each other or to an integer implicitly; the test proves it with `static_assert(!std::is_convertible_v<…>)`.
- [ ] `Id<T>{}` is invalid; `Id<T>::FromIndex(n)` is valid for any `n` below the sentinel.
- [ ] `TicksFromHours(3) == 180` and `TicksFromDays(1) == 1440`.
- [ ] Both headers compile without `NeuronCore.h` (GameLogic's `pch.h` includes no Windows header).

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
vstest.console.exe x64\Debug\NeuronCoreTests.dll /Platform:x64
python Build\CheckProjectFiles.py; python Build\RunClangTidy.py; python Build\CheckFormat.py
```

## Decisions to record

None (the tick's duration is NC-014's ADR).

## Out of scope

Generational ids, entity tables, any `using FleetId`.

## Notes

- `Id` is a `class` with an invariant (the sentinel), so its member is `m_index` (R8). Constants are `UPPER_CASE` (R3).
- Keep the header standalone: `<cstdint>`, `<compare>`, `<cstddef>` (for `std::size_t`), `<functional>` and nothing else.

## Report

**Verified here (Linux, no MSVC):** both headers pass clang-tidy 22.1.8 with the repository's `.clang-tidy` on a Windows-free translation unit that carries the tests' `static_assert`s, so the compile-time claims (no conversion between tags or to an integer, the sentinel, ordering, `TicksFromHours(3) == 180`, `TicksFromDays(1) == 1440`) held under a real front end; the same run showed that a partial `std::hash` specialization is rejected and a full one per alias is accepted, in either spelling; `CheckFormat.py` and `CheckProjectFiles.py` pass; `SuiteSmoke.cpp` is gone from NeuronCoreTests now that two real test classes exist. **Verified by CI, not here:** the Debug build and the seven tests under MSVC.

**Assumed:** nothing.

**Refined:** the sentinel is spelled `INVALID_INDEX` (R6); `<cstddef>` joins the header's includes for `std::size_t`; the generic `std::hash` specialization the task sketched became the per-alias pattern above, for the linter reason given in the header. `TicksFromMinutes` exists beside the two helpers the task named, because a timer stated in minutes (GDD §3's six-hour scout job is also stated as "a six-hour timer") reads better through it than as a bare multiplication.

**Bent:** nothing.
