# NC-011 — The pinned PRNG

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | S | no | no | Open |

**Depends on:** NC-010
**Read first:** GDD §4 ("a small random spread remains"), §8 (the identical-situation test), §15 (replay); AGENTS.md R16 (the last sentence), §6 (ADRs)

## Goal

The one source of randomness the simulation may use: a generator whose algorithm the repository owns, whose sequence is pinned by golden values in a test, whose state is a few bytes that serialize into the store, and whose distribution functions are written here rather than taken from `<random>`, because the standard pins the engines and not the distributions.

## Deliverables

- `NeuronCore/Random.h` + `.cpp`: `class Random` with `explicit Random(std::uint64_t _seed, std::uint64_t _stream = 0)`, `Next()` (32 bits), `NextBelow(std::uint32_t _bound)` (unbiased), `NextHundredths()` (0–99), `Fork(std::uint64_t _stream)` (an independent stream for a subsystem), `WriteState`/`ReadState` (NC-013 arrives later; provide `State()`/`Restore(State)` as a plain struct now and hook the writer in NC-013).
- `NeuronCoreTests/RandomTests.cpp`: golden values for two seeds (the first ten outputs, written into the test), `NextBelow` bounds and a chi-square-free sanity check (every bucket of ten hit in ten thousand draws), fork independence, restore-and-replay equality.

## Acceptance criteria

- [ ] The golden values are stated in the test and the ADR; a change to the algorithm fails the test, which is the point.
- [ ] `NextBelow` never returns `_bound` or above, and `_bound == 0` asserts.
- [ ] `Restore(State())` reproduces the sequence exactly.
- [ ] No `std::random_device`, no `<random>` engine, no address hashing anywhere in the file (R16).

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronCoreTests.dll /Platform:x64
```

## Decisions to record

**ADR — the pinned PRNG.** Recommendation: PCG32 (64-bit LCG state, 64-bit odd stream, XSH-RR output), written from the published description; `NextBelow` by multiply-shift with rejection (Lemire). State is 16 bytes. Record the golden values and the reason `<random>` distributions are not used.

## Out of scope

Floating-point outputs; a normal distribution; any use in GameLogic (NC-040 onward).

## Notes

- Streams: GDD §8 wants two admirals in one situation to differ; that is traits, not seeds, but each subsystem forking its own stream keeps one system's draws from shifting another's when a feature is added, which keeps old stores replaying (R16).

## Report

_Filled in on hand-back._
