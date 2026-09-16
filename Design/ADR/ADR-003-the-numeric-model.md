# ADR-003 — The numeric model

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-012
**Cites:** GDD §5 (credits), §6 (the weight table and its thresholds), §9 (confidence shown as a percentage), §10 (prices and margins); AGENTS.md R6, R16, R20

## Context

R16 forbids `float` in `GameLogic` where a fixed-point or integer quantity will do, and names the reason: the evidence weights of GDD §6 are fractions of a full attribution, and two builds of one simulation that disagree about one sum have no line to blame. The design states its fractions in two decimal places and nothing finer: weights of 0.25, 0.15, 0.30, −0.30, 0.60; thresholds at forty and seventy percent; a confidence of fifty-eight percent; market and payout multipliers in the same form. Nothing in the GDD asks for a third decimal.

What was undecided: the scale, the rounding, the width of a credit, and what happens at the ends of a type's range. Rounding is the one that cannot be left to each call site: GDD §6 decays a weight with distance and caps a repeated prior, so "0.15 twice under a 0.93 decay" has to have one answer forever, or a store written today replays differently tomorrow.

## Decision

1. **`Hundredths` is the fixed-point type for every fraction the design states**, a `std::int32_t` where 100 is unity. The raw count and the percentage are the same number, so `FromRaw(58)` is both 0.58 and "58%". It is signed, because §6's alibi is −0.30. There is no implicit conversion in either direction, and the only construction from a count is the named `FromRaw`: a bare `30` could be thirty percent or thirty wholes, and the compiler is not asked to guess.
2. **Rounding is half away from zero, everywhere.** 2.5 becomes 3 and −2.5 becomes −3. `DivideRound` states it once and `MulDivRound`, `Hundredths::Scale`, `Hundredths::Of` and `Lerp` inherit it. Worked example, from GDD §6: two priors of 0.15 under a 0.93 decay make 0.28 whether each is decayed and then summed or summed and then decayed. A tuning value whose result differs by grouping is a tuning value the resolver must apply in a stated order, and the test says so.
3. **Intermediate products are carried in `std::int64_t`**, and a product that would not fit asserts rather than wrapping. A quantity large enough to overflow it is a quantity this simulation does not have.
4. **An operation whose exact result is not representable asserts in Debug and saturates in Release.** Loud during development, defined in a shipped build, and never wrapping: a simulation that wraps is one whose replay diverges from its own store. This is the rule for the whole family — `SaturatingAdd`, `SaturatingSub`, `Hundredths`'s arithmetic, `Scale`'s clamp to the 32-bit range, and division by zero, which yields zero after asserting.
5. **`Credits` is a `std::int64_t`** (declared in `GameLogic` by NC-040), because a treasury, a contract and a cargo's value are whole credits and GDD §5 gives no sub-unit.
6. **`IntegerMath.h` does not depend on `Hundredths.h`.** It holds the operations that know only integers; `Lerp`, the one operation of the family that takes a fraction, lives in `Hundredths.h`, which is the header that knows both.

## What this forecloses

- A third decimal place. A design value of 0.125 cannot be held, and the answer is a different scale in a new ADR, not a quiet reinterpretation of the existing one: every stored weight would change meaning.
- `float` or `double` anywhere in the simulation, including "just for a ratio in a comparison".
- Rounding half to even, or towards zero, in any later operation. A new rounding rule is a new ADR and breaks every store.
- A `Hundredths` above about twenty-one million wholes (the 32-bit range divided by 100), which no design quantity approaches.

## Consequences

- Every GDD table in `Tuning.h` (NC-042, R20) is written in `Hundredths::FromRaw`, and reads as the GDD's own numbers with the decimal point moved two places.
- The client prints a confidence with `ToPercentString`, which has no arithmetic to get wrong, so GDD §9's "confidence seventy-one percent" cannot drift from what the empire acted on.
- Tests can assert exact equality on every derived quantity, which is what makes the determinism harness (NC-043) meaningful.
- A tuning value that outgrows its type announces itself as an assert during development rather than as a wrapped number during a playtest.

## Measurements

None quoted. The rounding and saturation behavior was checked by compiling the real headers under GCC 13 (`-std=c++23`) and Clang (`-std=c++2c`) with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`, in `_DEBUG` and `NDEBUG`, against a driver mirroring every case in `HundredthsTests.cpp` and `IntegerMathTests.cpp`; every assertion held in all four configurations, and the assert counts differed between `_DEBUG` and `NDEBUG` exactly as rule 4 states. CI runs the same assertions under MSVC.
