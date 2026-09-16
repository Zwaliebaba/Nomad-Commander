# Codebase constraints that shape these screens

Read with `AGENTS.md` §5, R12, R13 and `Plan/Tasks/NC-020` to `NC-026`, `NC-072`, `NC-073`, `NC-076`.

## What the engine can and cannot draw

These were all "fixed by rule" when this package was written. Four are no longer rules: the rows marked **Changed** moved on 2026-09-16, and the reasons are in `AGENTS.md` §5, R12 and ADR-008. What is left unmarked an agent still may not change.

| Constraint | Source | Effect on the screens |
|---|---|---|
| The game draws at **1920x1080** `R8G8B8A8_UNORM` into a scene target, presented scaled to the window | R12; ADR-008, ADR-009; NC-020, NC-021 | **Changed.** The size the screens are authored at, and the only size anything draws at: never ask the window how big it is. `GLYPH_SCALE` is 3 and the cell is 24 px. What changed on 2026-09-16 is the last step — the target is scaled into the client area with the aspect preserved, so the game runs on a display that cannot hold 1920x1080. Text is pixel-perfect at 1:1 and softer anywhere else, which is the cost ADR-009 names. |
| Blending and samplers are a pass's own business; multisampling is unavailable, and now that matters | §5 (lifted 2026-09-16); NC-022; NC-027 | **Changed.** Alpha is allowed now, so the gradients, soft shadows, glow and haze in `screens/02` may be built as drawn — the *Flat fallbacks* below became a menu rather than a requirement. `PipelineDefaults` still leaves blending off, so a pass that wants it says so. Text on panels still uses `discard`, which stays the cheaper path. MSAA remains impossible: DXGI will not multisample a flip-model back buffer and R12 forbids the intermediate target a resolve would need. That was free while everything was axis-aligned; a perspective map has a silhouette on every sphere and a slope on every lane, so the 3D map is aliased until R12's first paragraph is opened. NC-027 writes that ADR. |
| **The map is 3D; the desk around it is 2D** | GDD §13, §15, §16 (v1.7); §5; NC-027, NC-072 | **Changed twice on 2026-09-16.** §5 stopped forbidding a camera and a mesh pipeline, and then GDD v1.7 put the 3D client inside v0.1 — so the map in `screens/02` may be rendered in perspective for real, and NC-027 builds the depth buffer, camera and mesh pass it needs. The desk stays flat on the cell grid. NC-072 draws **both** maps: GDD §16 guards the 3D one on the player being able to say what it tells them that the 2D one did not, and the comparison needs both to exist. UI §4's dimetric formula remains the 2D map's projection. |
| Bitmap font 8x8, 96 glyphs, integer scales only | R13; NC-023 | Type is monospaced and sized 8*k px; the cell grid is the truth. The font covers 0x20-0x7E and nothing else, so **every string the client draws is ASCII** — there is no fallback glyph. The screens were authored with typographic characters; UI §6 carries the substitution table (`·`->`|`, `→`->`->`, `≤`->`<=`, `×`->`x`, `−`/`–`->`-`, `…`->`...`) and the PNGs still show the originals. Owner decision, 2026-09-16: ASCII only, rather than extending the font. |
| Icons are in scope; no drag and drop, no animation | NC-026 (icons added 2026-09-16) | **Changed.** NC-026 now ships an `IconAtlas.h`, monochrome and tinted, drawn through the glyph pipeline on the 24-px cell. The screens still spell roles as words (`HARBOUR`, `CHOKEPOINT`) and that reads well — an icon accompanies a label, it does not replace one. Spheres are drawn with primitives. |
| Widgets: panel, label, button, list, tabs, stepper, toggle, number field, tooltip, detail, confirm, scrollbar | NC-025, NC-026 | Every control on the screens maps to one of these. Nothing else is introduced. |
| Client shows only wire data, sends only `WireInput`s | R18; NC-071 | Reports, readings, weights, projections are the wire's. The map draws other empires only from reports. |
| Panels opaque, layered by draw order; Detail drawn last | NC-025, NC-026 | Cards on the map are drawn after lanes and spheres. |
| The executable ships alone | R13 | Palette and any sphere shading table are `constexpr` in headers. |

## Flat fallbacks for the aspirational parts of `screens/02`

Since blending was permitted on 2026-09-16 these are **options, not requirements**. Build the effect as drawn, or take a fallback here because it is cheaper or crisper; say which in the report either way.

- **Sphere**: three concentric filled discs (highlight, body, limb) from a `constexpr` table of three colours per empire slot; or a 24-triangle fan with per-vertex colour, since `PrimitiveVertex` carries a packed colour and interpolation across a triangle is not blending.
- **Ground shadow**: a filled ellipse in `#05070A`.
- **Empire haze**: omit, or a Bayer-dithered disc of the dim empire colour on the plane grid.
- **Dashed lines**: segments.
- **Starfield**: single-pixel points from a seeded `Random` (R16) — no file.

## Needs an owner decision

1. ~~**Screen size**~~ — decided 2026-09-16: 1920x1080 at `GLYPH_SCALE` 3. R12 carries it and **ADR-008** records it. The one thing still outstanding is that ADR's measurement: nobody has yet confirmed `GetClientRect` reports 1920x1080 unscaled on a real display (NC-020's desktop criterion).
2. **Third empire's name** (`Sedu Compact`) and the invented system names (`Harrow`, `Tessa Gate`, `Pale Anchor`, `Ashfall`, `Cinder Reach`, `Low Meridian`, `Sedu Hold`), per Roadmap A11 — list them in `KesselScenario.h`'s header comment for renaming.
3. **Sphere shading** — whether NC-072 may add a `constexpr` shading table (three colours per slot) to `Palette.h`, or whether nodes stay flat discs in v0.1.
