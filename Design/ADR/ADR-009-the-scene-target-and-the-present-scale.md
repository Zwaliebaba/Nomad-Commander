# ADR-009 — The scene target and the present scale

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-021 (implements); NC-020 amended, NC-027 relieved
**Cites:** GDD §13 (v1.7); AGENTS.md R12, R13, R14, §5; ADR-008; `Design/UI/UI-Spec.md` §1

## Context

R12 said the game drew straight into the swap chain's back buffer, whose client area was exactly 1920×1080, with "no intermediate render target, no resolve pass and no present scale". That bought a real guarantee — a channel authored `0xAA` reached the glass as `0xAA`, and an 8×8 glyph landed on whole pixels — and it cost two things that had both become concrete by the end of 2026-09-16.

**It could not run on most displays.** The frame around a 1920×1080 client area makes the window about 1926×1117, which is taller than a 1080p screen before the taskbar takes its share. ADR-008 named this and accepted it; it is the most common PC display resolution, and any laptop at 125–150 % scaling is worse.

**It made anti-aliasing impossible, at the moment the game acquired 3D.** DXGI will not multisample a flip-model back buffer, and a resolve needs a target to resolve from — which that sentence forbade. So did a post-pass, and so did supersampling. GDD v1.7 then put a 3D map in v0.1, where every sphere has a silhouette and every lane has a slope, and there was no way to anti-alias any of it.

The owner asked for the intermediate target on 2026-09-16, to make the screen look right at different resolutions.

## Decision

**Every pass draws into a scene target of exactly 1920×1080 `R8G8B8A8_UNORM`** — not `_SRGB` — and the frame ends by presenting that target into the swap chain's back buffer, **scaled to the window's client area with the aspect ratio preserved** and letterboxed or pillarboxed as needed.

1. **The game never draws at any other size.** The 80×45 cell grid, `GLYPH_SCALE` 3 and every layout in UI §1 are unconditional. A pass that asks the window how big it is has misunderstood this decision; exactly one place knows, and it knows only how to fit one rectangle inside another.
2. **The present step takes the cheapest path available to it**: exactly 1:1 with no filtering when the client area is 1920×1080; point sampling at an exact integer multiple; bilinear otherwise. The common case stays pixel-perfect.
3. **The scene target may be multisampled** and resolved before the present step, because it is not a back buffer. Whether to pay for that is NC-027's to decide with a measurement.
4. **R13 is untouched.** The target is created, never loaded; no file appears beside the executable.

## What this forecloses

**The 1:1 guarantee, at the last step, whenever the scale is not 1.** This is the whole cost and it should not be understated: this client is dense with 8-pixel bitmap text, and resampling an 8×8 glyph by a non-integer factor is the one thing bitmap type is worst at. A player on a 1366×768 desktop gets the whole game at 0.71× and softer text than a player at 1920×1080 gets. Point sampling would keep it crisp and make glyph stems uneven instead; bilinear is the least-bad of two bad options and is why the integer and 1:1 cases are called out separately rather than left to a general filter.

It also forecloses the simplest possible frame loop — there is now a target to create, resize and transition, and a present pass with its own pipeline, sampler and root signature. That is the first sampler in the tree; §5 stopped forbidding one on the same day, and this is what it was needed for.

It does **not** foreclose going back: deleting the scene target and drawing into the back buffer restores the old rule exactly, at the cost of the two things above.

## Consequences

- `NC-021` grows the scene target, its RTV, the present pass and the fit arithmetic. Its `FrameTarget` becomes the same thing the game already uses rather than a test-only object, which simplifies A12.
- `NC-027` is relieved: MSAA is available without opening R12 again, and its ADR becomes "is it worth paying for" rather than "is it possible".
- **`NC-020` is not finished by this ADR and is made less finished by it.** `Window::Create` still demands a client area of exactly the pixels requested and fails with `ClientAreaMismatch` otherwise. That was correct under the old rule. Under this one the window should probably be allowed to be smaller — by fitting the largest 16:9 area the desktop can hold, or by becoming resizable — and until that is decided the scale this ADR permits is a 1:1 scale in practice and the window still overhangs a small desktop. **The choice is the owner's and NC-020 carries it as open.**
- ADR-008's "What this forecloses" is relieved but not withdrawn: 1920×1080 is still what the game draws, and it is still what a display must have to see it unscaled.

## Measurements

None quoted. The three filter cases in the decision are stated from how point and bilinear sampling behave, not from a measurement of this client; **NC-021 is to photograph the same text at 1:1, at 2× point and at 0.71× bilinear and put the three in its report**, because "softer text" is the kind of claim that should be looked at rather than argued about.
