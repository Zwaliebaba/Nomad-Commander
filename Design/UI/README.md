# Nomad Commander — desk client UI package

Reference designs for the v0.1 desk client (GDD §3, §13). Everything here derives from `Design/GameDesign.md` v1.6, `AGENTS.md`, and the Phase 5 tasks in `Plan/Tasks/`. It is *design context*, not a design document: the GDD stays authoritative, and where this package and the GDD disagree, the GDD wins.

## Contents

| Path | What it is |
|---|---|
| `screens/01-situation-board-1920x1080.png` | Board screen (NC-073) at session 0:00, market projection drilled in |
| `screens/02-map-isometric-1920x1080.png` | Map screen (NC-072) at 7:00, convoy sighting selected, isometric treatment |
| `screens/03-operation-composer-plan-editor-1920x1080.png` | Composer + plan editor (NC-076) at 11:00–25:00 |
| `UI-Spec.md` | Layout grid, palette, per-screen anatomy, interaction rules, copy rules |
| `Codebase-Constraints.md` | What `AGENTS.md` and the plan allow or forbid, and what needs an owner decision |
| `Agent-Prompt.md` | The prompt and PR instructions that make an implementing agent honour these screens |
| `ADR-draft-screen-size.md` | Draft ADR for 1920×1080, for the owner to accept or reject |
| `Nomad Commander Desk.dc.html` | Editable source of the mockups (open in a browser) |

## How to put this in the repo

1. Copy `screens/`, `UI-Spec.md`, `Codebase-Constraints.md` and `Agent-Prompt.md` to `Design/UI/`.
2. Add one line to `Design/README.md`'s table: *`UI/` — reference screens for Phase 5; cited as* UI §n; *design context, edited by the owner.*
3. In each of NC-072, NC-073, NC-076 (and NC-074, NC-075, NC-077, NC-078, NC-079 for the shared chrome) add under **Read first**: `Design/UI/UI-Spec.md §<screen>` and the screen PNG.
4. Decide the ADR draft. Until it is accepted the screens are read at 1280×720 with `GLYPH_SCALE` 2; the grid is identical.
