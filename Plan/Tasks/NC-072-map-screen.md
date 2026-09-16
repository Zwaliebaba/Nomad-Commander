# NC-072 — The map screen

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | L | **yes** | no | Open |

**Depends on:** NC-071
**Read first:** GDD §13 whole (2D map; the Homeworld feel through fleet identity; decisions, not data), §7 (the graph and the roles), §3 (7:00: the convoy route, the last sighting nine hours old, the picket at the jump point); AGENTS.md §5 (*The client's dimensionality is a design question, not a conformance rule* — and a 2D map camera is available); `Design/UI/UI-Spec.md` §1, §2, §4, `Design/UI/screens/02-map-isometric-1920x1080.png`; `Design/UI/Codebase-Constraints.md`

## Goal

The 2D map: systems as nodes with their role and owner, lanes, the company's fleets and outposts, and everything else only as the company's reports show it, with age. Selecting a system or a fleet opens what the desk knows about it. A 2D map camera — pan and zoom — is available (AGENTS.md §5, owner decision 2026-09-16), but this task does not need one: about ten systems fit at 1920×1080 with room to spare, by design. Add it when a map outgrows the screen, not before.

## Deliverables

- `NomadCommander/MapScreen.h` + `.cpp`: draws from `ClientModel`: nodes (role glyph, owner colour from `Palette`, name), lanes (with jump hours on hover), own fleets (name, counts, position along a lane by the projection), reported fleets (as sighted: hull classes, counts, age, reliability; dimmer with age; identity only if the report had it), outposts, convoy sightings and their projected passage; selection by click; a side panel with the selected item's reports one tap behind (NC-026's `Detail`).
- Layout: the map occupies the screen minus a board strip and a status line; positions come from `WireSystem` (NC-041's integer coordinates).
- Fleet identity (GDD §13): fleet names and veterancy shown; the map is where a fleet with history looks like one.

## Acceptance criteria

- [ ] The owner opens the map on the sandbox and sees the ten systems, the lanes, their fleets and a sighting with its age; the report says so.
- [ ] Nothing drawn for another empire's fleet comes from anywhere but a report in `ClientModel` (the reviewer reads the draw code).
- [ ] Ages update every frame from the session tick; a nine-hour-old sighting reads "9 h".
- [ ] No transform other than pixel positions. A camera is permitted now (AGENTS.md §5, 2026-09-16); this task declines one because every system fits on the screen, and says so rather than inheriting a ban.
- [ ] Rendered beside `Design/UI/screens/02-map-isometric-1920x1080.png`, the owner recognises the screen row for row; every deviation is listed in the report with its UI § reference, and each aspirational effect says which treatment was used.

## Verification

```powershell
x64\Debug\NomadCommander.exe --sandbox 1
```

## Decisions to record

None.

## Out of scope

Panning, zooming, animation, a minimap, the battle visualisation (NC-078).

## Notes

- Hover and detail are opaque panels drawn last (NC-026).

## Report

_Filled in on hand-back._
