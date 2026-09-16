# NC-011 — The pinned PRNG

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | S | no | no | Done (PR #2) |

**Depends on:** NC-010
**Read first:** GDD §4 ("a small random spread remains"), §8 (the identical-situation test), §15 (replay); AGENTS.md R16 (the last sentence), §6 (ADRs)

## Goal

The one source of randomness the simulation may use: a generator whose algorithm the repository owns, whose sequence is pinned by golden values in a test, whose state is a few bytes that serialize into the store, and whose distribution functions are written here rather than taken from `<random>`, because the standard pins the engines and not the distributions.

## Deliverables

- `NeuronCore/Random.h` + `.cpp`: `class Random` with `explicit Random(std::uint64_t _seed, std::uint64_t _stream = 0)`, `Next()` (32 bits), `NextBelow(std::uint32_t _bound)` (unbiased; a bound of zero asserts and yields 0), `NextHundredths()` (0–99), `Fork(std::uint64_t _stream)` (an independent stream for a subsystem, seeded from the parent's state and the stream through SplitMix64, without advancing the parent), and `State()`/`Restore()` over the public aggregate `RandomState { state, increment }` (named so, because a nested type and a method cannot share the name `State`); NC-013 adds the byte writer and reader forms.
- `NeuronCoreTests/RandomTests.cpp`: golden values for two seeds (the first ten outputs, written into the test), `NextBelow` bounds and a chi-square-free sanity check (every bucket of ten hit in ten thousand draws), fork independence, restore-and-replay equality.

## Acceptance criteria

- [x] The golden values are stated in the test and the ADR; a change to the algorithm fails the test, which is the point.
- [x] `NextBelow` never returns `_bound` or above, and `_bound == 0` asserts.
- [x] `Restore(State())` reproduces the sequence exactly.
- [x] No `std::random_device`, no `<random>` engine, no address hashing anywhere in the file (R16).

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

**Verified here (Linux):** the real `Random.cpp`, compiled against a stub `pch.h` and a Linux stand-in for `Debug.cpp`, was run under GCC (`-std=c++23`) and Clang (`-std=c++2c`) with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`, in both `_DEBUG` and `NDEBUG`, through a driver that mirrors every case in `RandomTests.cpp`: both golden sequences match, `NextBelow` stays below nine bounds over 2000 draws each, all ten buckets are hit in 10,000 draws (counts 951 to 1037), `NextHundredths` stays below 100, restore replays exactly, forks are independent of each other and of the parent and deterministic, and a bound of zero calls the assert handler once in `_DEBUG` and not at all in `NDEBUG`, yielding 0 either way. The golden values were first computed by a Python transcription of PCG32 whose first six outputs for seed 42 / stream 54 matched the published reference demo, so the C++ is tied to the reference and not only to itself. clang-tidy 22.1.8 with the repository's configuration is clean on `Random.cpp`; `CheckFormat.py` and `CheckProjectFiles.py` pass. **Verified by CI, not here:** the MSVC build and the eight tests under vstest.

**Assumed:** nothing.

**Refined:** `RandomState` as the aggregate's name, for the reason in the deliverables; `Fork` derives the child's seed through SplitMix64 rather than reusing the parent's state verbatim, so children on adjacent streams do not start from related states; `NextBelow(0)` returns 0 after asserting, so a zero range in a tuning table cannot divide by zero in Release; a private `Step()` replaces the discarded `Next()` calls the reference seeding would otherwise need.

**Bent:** one task per PR. NC-011 was asked for while PR #2 (NC-010) was still open, this session works on one branch, and the repository's stop hook asks for every commit to be pushed; so NC-011 rides on PR #2 as its own commit rather than as PR #3. Merging each PR before the next task starts keeps the rule intact from here.
