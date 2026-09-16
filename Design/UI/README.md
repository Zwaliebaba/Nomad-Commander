# Nomad Commander — desk client UI package

Reference designs for the v0.1 desk client (GDD §3, §13). Everything here derives from `Design/GameDesign.md` v1.6, `AGENTS.md`, and the Phase 5 tasks in `Plan/Tasks/`. It is *design context*, not a design document: the GDD stays authoritative, and where this package and the GDD disagree, the GDD wins.

## Contents

| Path | What it is |
|---|---|
| `screens/01-situation-board-1920x1080.png` | Board screen (NC-073) at session 0:00, market projection drilled in |
| `screens/02-map-isometric-1920x1080.png` | Map screen (NC-072) at 7:00, convoy sighting selected, isometric treatment |
| `screens/03-operation-composer-plan-editor-1920x1080.png` | Composer + plan editor (NC-076) at 11:00-25:00 |
| `UI-Spec.md` | Layout grid, palette, per-screen anatomy, interaction rules, copy rules |
| `Codebase-Constraints.md` | What `AGENTS.md` and the plan allow or forbid, and what needs an owner decision |
| `Agent-Prompt.md` | The prompt and PR instructions that make an implementing agent honour these screens |
| *(was `ADR-draft-screen-size.md`)* | Decided and promoted to [`../ADR/ADR-008-the-screen-is-1920x1080-at-glyph-scale-3.md`](../ADR/ADR-008-the-screen-is-1920x1080-at-glyph-scale-3.md) |
| `Nomad Commander Desk.dc.html` | Editable source of the mockups (open in a browser) |

## How this was put in the repo

All four steps are done, on 2026-09-16.

1. The package lives at `Design/UI/`.
2. `Design/README.md`'s table carries a `UI/` row: design *context*, cited as *UI §n*, owner-edited, with an agent reconciling it against `AGENTS.md` when a rule it quotes changes.
3. NC-072, NC-073 and NC-076 cite their own screen and its UI sections under **Read first**, and carry an acceptance criterion that the owner recognises the screen row for row. NC-074, NC-075, NC-077, NC-078 and NC-079 cite UI §1, §2 and §6 for the shared chrome with the matching criterion. `.github/pull_request_template.md` asks for the sections implemented and a screenshot beside the reference PNG.
4. The ADR is decided: **1920x1080 at `GLYPH_SCALE` 3**, recorded as [ADR-008](../ADR/ADR-008-the-screen-is-1920x1080-at-glyph-scale-3.md) and implemented. Its one open item is the measurement — nobody has yet confirmed `GetClientRect` on a real display.

## What changed in this package after it landed

It was authored against the tree as it stood on the morning of 2026-09-16, and three owner decisions that afternoon moved rules it quotes. `Codebase-Constraints.md` and `Agent-Prompt.md` are reconciled; their changed rows say so.

- The screen is 1920x1080, not 1280x720 (ADR-008).
- Blending and samplers are no longer forbidden, so the aspirational effects in `screens/02` may be built as drawn and the flat fallbacks became a menu rather than a requirement. Multisampling is still unavailable — because DXGI will not multisample a flip-model back buffer, not because a rule forbids it.
- A camera and a mesh pipeline are no longer forbidden by `AGENTS.md` §5, but GDD §13 and §15 still say the 3D client waits and they outrank it on design. A 2D map camera was never that question.
- NC-026 has icons, so "text labels are the icons" is gone.
- **On-screen copy is ASCII** (owner decision). The font is 96 glyphs over 0x20-0x7E with no fallback glyph, and the screens were authored with typographic characters. UI §6 carries the substitution table. **The PNGs still show the originals**: they are pictures and cannot be edited here, so where a PNG and `UI-Spec.md` disagree, the file wins (Agent-Prompt rule 1). Regenerating them from `Nomad Commander Desk.dc.html` would settle it.
