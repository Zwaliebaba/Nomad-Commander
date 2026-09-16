# NC-012 — Hundredths and integer arithmetic

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | M | no | no | Open |

**Depends on:** NC-010
**Read first:** GDD §5 (credits), §6 (weights as fractions, thresholds as percents), §10 (prices, margins); AGENTS.md R6, R16 (no `float` in GameLogic; weights in integer hundredths)

## Goal

The numbers the simulation is allowed to use. `Hundredths` is the fixed-point type for every fraction, weight, confidence, reliability and percentage in the design; `IntegerMath.h` holds the operations that make integer arithmetic safe to reason about: rounding that is stated, overflow that is impossible or asserted, and a multiply-divide that goes through 64 bits. Every GDD table in `Tuning.h` will be written in these types.

## Deliverables

- `NeuronCore/Hundredths.h`: `class Hundredths` over `std::int32_t` (100 is unity, so `Hundredths{58}` reads as fifty-eight percent and the §6 weight 0.25 is `Hundredths{25}`); construction from a raw count only through `FromRaw`; `+`, `-`, comparison; `Scale(Hundredths)` (product, rounded half away from zero through `std::int64_t`); `Clamp(lo, hi)`; `ToPercentString` for the client; `UNITY`, `ZERO` constants.
- `NeuronCore/IntegerMath.h`: `MulDivRound(std::int64_t, std::int64_t, std::int64_t)`, `SaturatingAdd`, `SaturatingSub`, `CeilDiv`, `IntegerSqrt`, `Lerp(a, b, Hundredths)`, all `constexpr` where possible, all asserting on division by zero.
- `NeuronCoreTests/HundredthsTests.cpp`, `IntegerMathTests.cpp`.

## Acceptance criteria

- [ ] `Hundredths{25}.Scale(Hundredths{60}) == Hundredths{15}` and the rounding rule is tested at the half (`.5` rounds away from zero, both signs).
- [ ] No operation on `Hundredths` can overflow silently: the test covers `INT32_MAX` inputs and expects saturation or an assert, as the ADR states.
- [ ] No `float` or `double` appears in either header.
- [ ] `constexpr` evaluation works for every function that claims it (a `static_assert` per function).

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

_Filled in on hand-back._
