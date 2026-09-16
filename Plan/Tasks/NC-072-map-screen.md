# NC-072 — The map screen

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | L | **yes** | no | Open |

**Depends on:** NC-027, NC-071
**Read first:** GDD §13 whole (2D map; the Homeworld feel through fleet identity; decisions, not data), §7 (the graph and the roles), §3 (7:00: the convoy route, the last sighting nine hours old, the picket at the jump point); AGENTS.md §5 (*The client's dimensionality is a design question, not a conformance rule* — and a 2D map camera is available); [ADR-013](../../Design/ADR/ADR-013-anti-aliasing-the-3d-map.md) **whole, and §4 of its decision before you draw a moving fleet — this task is its named trigger**; `Design/UI/UI-Spec.md` §1, §2, §4, `Design/UI/screens/02-map-isometric-1920x1080.png`; `Design/UI/Codebase-Constraints.md`

## Goal

The map: systems as nodes with their role and owner, lanes, the company's fleets and outposts, and everything else only as the company's reports show it, with age. Selecting a system or a fleet opens what the desk knows about it.

**Two maps, and that is deliberate.** GDD §13 (v1.7) puts a 3D map in v0.1 and §16 guards it: it earns its place when the player can say what it tells them that the 2D map did not. So this task draws the map through `PrimitiveBatch` in pixel space *and* through NC-027's mesh pass in perspective, switchable, and the report answers that question with both in front of it. The 2D map is built first and is not deleted. About ten systems fit at 1920×1080 with room to spare either way, so neither needs pan or zoom; add a camera controller when a map outgrows the screen, not before.

## Deliverables

- `NomadCommander/MapScreen.h` + `.cpp`: draws from `ClientModel`: nodes (role glyph, owner colour from `Palette`, name), lanes (with jump hours on hover), own fleets (name, counts, position along a lane by the projection), reported fleets (as sighted: hull classes, counts, age, reliability; dimmer with age; identity only if the report had it), outposts, convoy sightings and their projected passage; selection by click; a side panel with the selected item's reports one tap behind (NC-026's `Detail`).
- Layout: the map occupies the screen minus a board strip and a status line; positions come from `WireSystem` (NC-041's integer coordinates).
- Fleet identity (GDD §13): fleet names and veterancy shown; the map is where a fleet with history looks like one.

## Acceptance criteria

- [ ] The owner opens the map on the sandbox and sees the ten systems, the lanes, their fleets and a sighting with its age; the report says so.
- [ ] Nothing drawn for another empire's fleet comes from anywhere but a report in `ClientModel` (the reviewer reads the draw code).
- [ ] Ages update every frame from the session tick; a nine-hour-old sighting reads "9 h".
- [ ] No transform other than pixel positions. A camera is permitted now (AGENTS.md §5, 2026-09-16); this task declines one because every system fits on the screen, and says so rather than inheriting a ban.
- [ ] **ADR-013's trigger is answered rather than skipped**: the 3D map is measured again with a fleet moving along a lane, and a new ADR either buys anti-aliasing or defers it with those figures. A report that does not mention ADR-013 fails this criterion.
- [ ] Rendered beside `Design/UI/screens/02-map-isometric-1920x1080.png`, the owner recognises the screen row for row; every deviation is listed in the report with its UI § reference, and each aspirational effect says which treatment was used.

## Verification

```powershell
x64\Debug\NomadCommander.exe --sandbox 1
```

## Decisions to record

**None outstanding for the sphere shading** — the owner settled it on 2026-09-16: `Palette.h` gains three colours per empire slot (highlight, body, limb) as `inline constexpr`, embedded like every other colour (R13), and a node is three concentric discs or a coloured triangle fan. Record the table's values in the report with a screenshot beside `screens/02`.

**[ADR-013](../../Design/ADR/ADR-013-anti-aliasing-the-3d-map.md) named this task as its trigger, and this is the something that checks.** NC-027 measured the 3D map aliased — 655 of 655 silhouette edge pixels with exactly the background immediately outside, so no partial coverage anywhere — and deferred anti-aliasing against a trigger rather than a date: *"The first task that moves the camera or a fleet along a lane is the one that should reopen this, with the same measurement taken again while it moves."* ADR-013's own cost paragraph says the deferral *"can be missed if nothing checks"*.

This task is that trigger. It declines a camera controller (see the criteria), but a fleet's position along a lane advances with the session tick, so fleets crawl across the mesh pass frame to frame — which is the case ADR-013 cares about, because aliasing on a still picture is a jagged edge and aliasing on a moving one is crawl. **Take NC-027's measurement again with a fleet in motion and write the ADR that supersedes ADR-013** — either paying for MSAA on the scene target (ADR-013 §2 says how, and that it is a small change) or deferring again with the new figures. "It still looks fine" is not an answer; neither is silence.

Nothing else.

## Out of scope

Panning, zooming, animation, a minimap, the battle visualisation (NC-078).

## Notes

- Hover and detail are opaque panels drawn last (NC-026).

## Report

_Filled in on hand-back._
