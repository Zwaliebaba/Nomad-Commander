# ADR-007 — clang-tidy and the Win32 message protocol

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-020
**Cites:** AGENTS.md §1 (`.clang-tidy` is the single source of truth), §4 (do not silence a diagnostic), §6 (an exception to a rule goes in an ADR), R12, R14

## Context

`Build/RunClangTidy.py` runs the whole tree and blocks CI, and AGENTS.md §4 is blunt about the temptation it creates: "Do not silence a diagnostic with `#pragma warning(disable: ...)` to make a build pass. Fix the cause, or report it."

The first Win32 window procedure in this tree produced three findings of `performance-no-int-to-ptr` that have no cause to fix. The check objects to casting an integer to a pointer, on the reasonable general ground that the compiler loses provenance and the pointer may alias anything. But the Win32 message protocol is *defined* to pass pointers in `WPARAM` and `LPARAM`, which are integer types. `WM_NCCREATE`'s `lParam` **is** a `CREATESTRUCTW*`. `WM_GETMINMAXINFO`'s `lParam` **is** a `MINMAXINFO*`. `GetWindowLongPtrW(GWLP_USERDATA)` returns the `LONG_PTR` that `SetWindowLongPtrW` was handed, and round-tripping a `this` through it is how every window procedure since 1985 has found its instance. There is no alternative spelling: R14 forbids a framework that would hide the cast, and R12's D3D12 client needs a window procedure of its own.

This will recur. Every window procedure, every `SetWindowLongPtr` pairing and every COM or Win32 callback that carries user data through an integer meets the same check. A convention decided once is cheaper than each agent re-deriving it, and much cheaper than the wrong resolution — widening `.clang-tidy` until the check stops firing anywhere, which would also stop it firing on the cases it is right about.

## Decision

1. **Where an SDK's own contract states that an integer parameter is a pointer, the cast is suppressed at the line that performs it**, with `// NOLINTNEXTLINE(<check>)` naming the single check and a comment naming the contract — which message, which slot. Never a bare `NOLINT`, never a range, never a check the line does not actually trigger.
2. **`.clang-tidy` is not edited for this.** It stays the single source of truth AGENTS.md §1 says it is, and it keeps firing on the cases where the premise does hold: an integer that is a handle, an offset or a hash, cast to a pointer because it was convenient.
3. **This covers a stated API contract and nothing else.** A cast that is merely *believed* safe is not covered; neither is a cast introduced to avoid designing a type. If the suppression needs a paragraph to justify, the design is wrong and the fix is the design.
4. **A `noexcept` function is kept genuinely non-throwing rather than suppressed.** `bugprone-exception-escape` is in the other category: it found a real defect in `Window::Create`, which allocated a `std::wstring` to null-terminate a `std::wstring_view` title. In a `noexcept` function an allocation failure is `std::terminate`, so the type changed — `Window::Desc::title` is a `const wchar_t*`, which is what R13 makes every title in this game anyway. The rule that generalizes: a finding about what the code *does* is fixed; only a finding about what an external contract *is* may be suppressed.

## What this forecloses

- Adding a check to `.clang-tidy`'s exclusion list to clear a Win32 cast, which would silently cover every future cast of that shape across all nine projects.
- A bare `// NOLINT`, which suppresses every check on the line including ones nobody has read yet.
- A hand-rolled wrapper whose only purpose is to hide the cast from the checker. It moves the cast; it does not make it safer, and it costs a layer R14's spirit does not want.
- Treating a `noexcept` violation as a checker artefact. It is a defect: `std::terminate` on a machine low on memory is not a theoretical failure mode.

## Consequences

- A Win32 callback in `NeuronClient` carries one commented `NOLINTNEXTLINE` per pointer it recovers from a message parameter, and a reader can see from the comment alone which contract is being relied on.
- A whole-tree clang-tidy run stays meaningful: a new `performance-no-int-to-ptr` finding is a real question, because the legitimate ones are already annotated.
- A `noexcept` function in this tree may not allocate. Where one needs a string, the caller owns it and hands over a pointer.

## Measurements

None quoted. The three suppressed findings and the one fixed defect are those reported by clang-tidy 22.1.8 — the version CI pins — on run 18 of the `Build` workflow; NC-020's report links it.
