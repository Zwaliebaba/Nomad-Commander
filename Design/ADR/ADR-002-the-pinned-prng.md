# ADR-002 — The pinned PRNG

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-011
**Cites:** GDD §4 ("a small random spread remains"), §8 (identical situations, different choices), §15 (replay, re-runnable measurements); AGENTS.md R16

## Context

R16 makes determinism a built property of the simulation: the receipt and the replay are features, a Milestone 2 run of simulated decades has to reproduce from its seed, and the §15 measurements have to be re-runnable. Every random draw inside the simulation therefore has to come from a generator whose algorithm the repository owns, whose state can be saved into the universe store and restored, and whose sequence cannot change underneath an old store. `<random>` pins its engines by the standard but not its distributions (`std::uniform_int_distribution` may differ between standard libraries and versions), and `std::random_device` and address hashing are forbidden by R16 outright.

## Decision

1. **The algorithm is PCG32 with the XSH-RR output function** (64-bit LCG state, multiplier `6364136223846793005`, a 64-bit odd increment as the stream), written in `NeuronCore/Random.cpp` from the published description. Seeding follows the reference `pcg32_srandom_r` exactly: step, add the seed, step, with the increment `(stream << 1) | 1`.
2. **The distributions are ours.** `NextBelow(bound)` is Lemire's multiply-shift with rejection (unbiased); `NextHundredths()` is `NextBelow(100)`. There is no floating-point output.
3. **The state is sixteen bytes** (`RandomState { state, increment }`), read with `State()` and written back with `Restore()`; NC-013 adds the byte writer and reader forms. The universe store saves it; a replay restores it.
4. **Forks are the unit of independence.** `Fork(stream)` returns a generator on that stream, seeded from the SplitMix64 mix of the parent's state and the stream, without advancing the parent. Each subsystem owns a fork taken at world creation (NC-040), so adding a consumer later does not shift the draws of the earlier ones.
5. **The sequence is pinned by golden values in `RandomTests.cpp`:** for seed 42 and stream 54, `A15C02B7 7B47F409 BA1D3330 83D2F293 BFA4784B CBED606E BFC6A3AD 812FFF6D E61F305A F9384B90`; for seed 1 and stream 0, `E2393051 01112F35 D3509D35 0B932F4A 8AA46776 8C532036 A0CD21D8 B8E6A8D0 DD26E863 8C7D6FFA`. The first six of the former are the published output of the PCG reference demo for those inputs, which ties the implementation to the reference and not merely to itself.

## What this forecloses

- Changing the generator, the seeding, the rejection scheme or the fork derivation without invalidating every store and every replay written before the change. A change is a new ADR and a store version bump (ADR-003's header, when NC-013 lands), never a silent edit.
- Any use of `<random>`, `std::random_device`, `rand()` or an address-derived value inside the simulation.
- A `float` or `double` output; a normal or other continuous distribution. A later need is a new ADR.

## Consequences

- Every draw in `GameLogic` goes through a `Random` owned by `World` or a fork of it; a function that needs randomness takes a `Random&`, never creates one.
- `NextBelow(0)` asserts (`NOMAD_ASSERT`) and yields 0, so a tuning table with a zero range fails loudly in Debug and does not divide by zero in Release.
- The test file is the contract: it will fail on any platform where the implementation drifts, which is the point.

## Measurements

The golden values were computed twice independently: by a Python transcription of the reference algorithm, whose first six outputs for seed 42 / stream 54 matched the published PCG demo before any value was recorded, and by the C++ implementation compiled with GCC and Clang on Linux against a stub precompiled header, whose outputs matched the Python transcription for both seeds. CI's MSVC build runs the same assertions.
