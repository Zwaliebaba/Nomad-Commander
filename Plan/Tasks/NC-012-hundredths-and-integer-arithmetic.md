# NC-012 — Hundredths and integer arithmetic

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | M | no | no | Done (PR #3) |

**Depends on:** NC-010
**Read first:** GDD §5 (credits), §6 (weights as fractions, thresholds as percents), §10 (prices, margins); AGENTS.md R6, R16 (no `float` in GameLogic; weights in integer hundredths)

## Goal

The numbers the simulation is allowed to use. `Hundredths` is the fixed-point type for every fraction, weight, confidence, reliability and percentage in the design; `IntegerMath.h` holds the operations that make integer arithmetic safe to reason about: rounding that is stated, overflow that is impossible or asserted, and a multiply-divide that goes through 64 bits. Every GDD table in `Tuning.h` will be written in these types.

## Deliverables

- `NeuronCore/Hundredths.h`: `class Hundredths` over `std::int32_t` (100 is unity, so `Hundredths{58}` reads as fifty-eight percent and the §6 weight 0.25 is `Hundredths{25}`); construction from a raw count only through `FromRaw`; `+`, `-`, comparison; `Scale(Hundredths)` (product, rounded half away from zero through `std::int64_t`); `Clamp(lo, hi)`; `ToPercentString` for the client; `UNITY`, `ZERO` constants.
- `NeuronCore/IntegerMath.h`: `MulDivRound(std::int64_t, std::int64_t, std::int64_t)`, `SaturatingAdd`, `SaturatingSub`, `CeilDiv`, `IntegerSqrt`, `Lerp(a, b, Hundredths)`, all `constexpr` where possible, all asserting on division by zero.
- `NeuronCoreTests/HundredthsTests.cpp`, `IntegerMathTests.cpp`.

## Acceptance criteria

- [x] `Hundredths{25}.Scale(Hundredths{60}) == Hundredths{15}` and the rounding rule is tested at the half (`.5` rounds away from zero, both signs).
- [x] No operation on `Hundredths` can overflow silently: the test covers `INT32_MAX` inputs and expects saturation or an assert, as the ADR states.
- [x] No `float` or `double` appears in either header.
- [x] `constexpr` evaluation works for every function that claims it (a `static_assert` per function).

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronCoreTests.dll /Platform:x64
```

## Decisions to record

**ADR — the numeric model.** `Hundredths` as described; `Credits` (declared in GameLogic, NC-040) is `std::int64_t`; rounding is half away from zero everywhere, stated once here; intermediate products are 64-bit; saturation versus assert is decided per operation and written in the header.

## Out of scope

A general fixed-point template; ten-thousandths; any use in GameLogic.

## Notes

- GDD §6's "0.15 per prior, capped" and "decays with distance" are products of hundredths; the rounding rule decides whether two priors at 0.15 with a 0.93 decay make 0.28 or 0.27, and a replay depends on that answer being the same forever.

## Report

**Verified here (Linux):** both headers were compiled under GCC (`-std=c++23`) and Clang (`-std=c++2c`) with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`, in `_DEBUG` and `NDEBUG`, against a driver carrying every `static_assert` from the two test files and a runtime mirror of every `TEST_METHOD`; all four configurations pass, and the assert counts differ between the two configurations exactly as ADR-003's rule 4 states. `Hundredths::FromRaw(25).Scale(FromRaw(60)) == FromRaw(15)`, and the half rounds away from zero on both signs and with either operand negative. The overflow criterion is covered at both ends of `std::int32_t` for addition, subtraction and `Scale`, and at both ends of `std::int64_t` for the product inside `MulDivRound`. `grep` finds no `float` or `double` in either header. Every function that claims `constexpr` is evaluated in a `static_assert`, 31 of them for `IntegerMath.h` and 20 for `Hundredths.h`. clang-tidy 22.1.8 with the repository's configuration is clean on a unit carrying both headers; `CheckFormat.py` and `CheckProjectFiles.py` pass. **Verified by CI, not here:** the MSVC build and the tests under vstest.

**Assumed:** nothing.

**Refined:** the task's `Hundredths{58}` is prose for how the value reads; the code writes `Hundredths::FromRaw(58)`, since the same sentence requires construction from a raw count to go through `FromRaw` alone. The constants are `HUNDREDTHS_ZERO` and `HUNDREDTHS_UNITY` at namespace scope rather than `ZERO`/`UNITY` members, so a call site reads without a qualifier. `Of(quantity)` joins `Scale(fraction)`, because a fraction of a whole quantity (sixty percent of a 9,000-credit contract, GDD §3) is the more common operation and doing it through `Scale` would lose the quantity's 64 bits. `DivideRound` is public rather than a private helper, since it is where the rounding rule is stated once. `Lerp` is in `Hundredths.h`, not `IntegerMath.h`: `IntegerMath` must not depend on `Hundredths`, and `Lerp` is the one operation of the family that takes a fraction (ADR-003, rule 6).

**Bent:** one task per PR, again: NC-012 through NC-020 were asked for as a batch, and this session works on one branch.
