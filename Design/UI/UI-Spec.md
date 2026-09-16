# UI Spec — the desk client

Cite as *UI §n*. Pixel values are given at 1920x1080 with the 1280x720 value in brackets; the two are the same layout on the same 80x45 cell grid (`GLYPH_SCALE` 3 vs 2).

## 1. Grid and chrome (every screen)

- Screen 1920x1080 [1280x720]. Cell 24 px [16 px]. Everything snaps to the cell; nothing draws at a non-integer position (NC-025).
- **Tab bar**, top, 48 px [32]: company name at left in ACCENT, 264 px [176] wide, then tabs `Board | Map | Operations | Contracts | Company | Receipts`. Active tab: PANEL fill, 3 px [2] ACCENT underline. Inactive: TEXT_DIM. A count after a tab name (`Board 3`, `Contracts 1`) is WARNING for items needing an answer, ACCENT otherwise. Right end: one line of context (`Kessel | Varn claim, tolerated`).
- **Status line**, bottom, 48 px [32]: simulated day and time | rate control (`pause 1x 4x 24x`, active one bordered ACCENT) | credits | upkeep per day in WARNING | mothership state.
- **Board strip** (Map and Operations screens only), 144 px [96] above the status line: the three soonest board items as three equal cells: kind and party in the kind's colour, time left at right, the projection sentence under it in TEXT_DIM.
- **Panels** are opaque: PANEL fill, 1 px [1] PANEL_EDGE border, 24 px [16] padding. Selected item: PANEL_SELECTED fill with an ACCENT or TEXT border. No shadow, no alpha, no rounded corners.
- **Dim is a colour, not opacity.** Older reports and uncovered triggers use TEXT_DIM / TEXT_FAINT.

## 2. Palette (`Palette.h`)

| Name | Hex | Use |
|---|---|---|
| BACKGROUND | `#0B0E13` | screen |
| BACKGROUND_RAISED | `#0F1319` | chrome, side panels |
| PANEL | `#141922` | cards, rows |
| PANEL_SELECTED | `#181F2A` | the selected row / reading |
| PANEL_EDGE | `#2A3140` | borders, lanes at rest |
| CHROME_EDGE | `#232A36` | separators |
| TEXT | `#E6E1D6` | primary |
| TEXT_DIM | `#7F8796` | secondary, older reports |
| TEXT_FAINT | `#4A505C` | disabled, uncovered |
| ACCENT | `#D9A441` | the company: own fleets, active tab, Confirm |
| WARNING | `#E07A3A` | stakes, upkeep, hardening beliefs |
| HOSTILE | `#D9534F` | acting-against state (reserved; not on these screens) |
| EMPIRE_0 (Varn) | `#C9524A` | |
| EMPIRE_1 (Oren) | `#4F9DD3` | |
| EMPIRE_2 (Sedu Compact) | `#7BB765` | canon since 2026-09-16 |
| NEUTRAL | `#7F8796` | unclaimed systems |

Text on ACCENT buttons is BACKGROUND. Dim variants of empire colours for old reports: Varn `#8A5A55`, edge `#5A3A36`.

**Sphere shading** (owner decision, 2026-09-16; NC-072). Each empire slot carries three colours — highlight, body, limb — as `inline constexpr` in `Palette.h` beside the rest, so a system node is three concentric filled discs or a coloured triangle fan rather than a flat one. Interpolating a colour across a triangle is not blending, so this was reachable even before blending was allowed. The 3D map (NC-027) may light a real sphere instead; this table is what the 2D map uses, and GDD §16 keeps the 2D map.

## 3. Board (NC-073) — `screens/01`

Layout: items column 1152 px [768] | detail panel 768 px [512].

- Header row: `3 items` | `sorted by expiry` | `time left`.
- **Item row**: three columns 144 [96] | flexible | 144 [96]. Kind in its colour (`ACCUSATION` Varn red, `MARKET` ACCENT, `OFFER` Oren blue) with the party beneath in TEXT_DIM; the projection sentence in TEXT with one line of what can be done beneath in TEXT_DIM; time left right-aligned, WARNING when it is a belief hardening or a deadline under a day.
- Rows are sorted by expiry. Opening a row routes by kind (accusation -> NC-075; offer -> contracts; sighting -> map with system selected; market -> detail here).
- Below the list, pinned to the bottom: burn per day and days of credits, operations in flight, docked counts.
- **Detail panel** (the "one tap behind"): title in ACCENT; the projection sentence at 24 px [16]; a key/value card of the arithmetic (`stock 1,140 over 30 an hour = 38 h`); a second card `THE REPORT BEHIND IT` with `source | observed | delivered | this source: N confirmed | M contradicted`. Never a probability (GDD §4). Two buttons at the bottom.

## 4. Map (NC-072) — `screens/02`

Layout: map 1296 px [864] | side panel 624 px [416] | board strip | status line.

- **Projection**: dimetric, precomputed. Plane point (x, y) -> screen (0.707*(x - y), 0.354*(x + y)) + centre, rounded to whole pixels. No camera, no zoom, no pan (AGENTS.md §5). The grid on the plane is 46 plane-px, drawn as two families of lines.
- **System**: a sphere (the isometric treatment) or a filled disc (flat fallback, same radius) in the owner's colour; radius 21 px [14], Kessel 27 px [18] as the selected/contested system; an oval ground shadow beneath; a thin owner-colour ring at radius 39 px [26] for harbours. Label to the right (or left for the two eastern systems): name in owner colour, `ROLE | owner` in TEXT_DIM beneath. Roles are spelled as words here and that reads well; icons became available on 2026-09-16 (NC-026) and an icon accompanies a label rather than replacing one.
- **Company presence**: a small ACCENT "moon" at the sphere's upper right where the company has hulls (Tessa Gate picket, Kessel mothership). Own fleet card at Kessel: system line, `NOMAD MERIDIAN | mothership`, docked counts, commander and engagements; ACCENT border.
- **Lanes**: PANEL_EDGE 3 px [2]. The lane of the selected item is drawn in the empire colour with a darker under-stroke. Lane hours appear on hover in a small opaque tooltip at the top right of the map (`Tessa Gate -> Kessel | 4 h | raider 3 h`).
- **Reports** (everything not the company's): drawn only from `ClientModel` reports. A sighting is a hollow square on the lane with a dashed projection toward its destination; its card (TEXT border when selected) reads `VARN CONVOY | sighted 9 h`, composition, `projected at Kessel in 5-9 h`. An older report is dimmer: `VARN FLEET | 14 h` in dim Varn red with a dashed circle at its last position and the source (`Oren briefing`) on the second line. Age updates every frame.
- **Territory**: a soft empire-colour haze under each empire's systems (flat fallback: none, or a dithered disc). A dashed ring around Kessel marks the siege.
- **Side panel**: selected item title, one-sentence context, `REPORTS | newest first` as cards (source in its colour, age at right, content, `track record` line) | the relevant admiral's dossier card (Varn border) with templates seen and `Open dossier ->` | two buttons: `Scout it | 4 h` and `Plan operation` (ACCENT).
- Legend bottom-left: `bright = own or fresh | dim = older report | gold moon = your ships there`.

## 5. Composer + plan editor (NC-076) — `screens/03`

Layout: three columns 528 [352] | 624 [416] | 768 [512], then a 144 px [96] projection strip with Confirm, then the status line.

- **`1 | READING`**: three cards from `WireReading`; the selected one PANEL_SELECTED with ACCENT border and a filled dot; each shows `binds: ...` beneath. `EVIDENCE USED` lists the reports behind the readings. Footer: the cost of confirming instead.
- **`2 | OPERATION`**: steppers per class with `of N` docked; a consequence line under them (`arrives in 14 h; adding the hauler makes it 17 h`). `Markings` two-state toggle; its consequence line in WARNING quotes the payout rule. Fuel stepper with `need | reserve`, price source and total. `commander | route | employer` key/values; the commander shows `capacity N` in ACCENT.
- **`3 | PLAN`**: six base rules as chips/steppers (`objective | priority | engage if escort <= | withdraw at | pursuit | reserve`); values defaulted from the reading say so (`- from the reading`). `OVERRIDES` header with `2 of 2 used | <commander>'s capacity` in WARNING; each override a row `trigger -> action`; the add row disabled with `no budget left` when full. Then `UNCOVERED | what you are willing not to plan for` in TEXT_FAINT — the last thing before the strip.
- **Projection strip**: `projection` (arrival, window), `orders after` (courier only, hours), `stake` (fuel, hulls out of position, opinion), `if attributed` in WARNING, `fuel` (reserve covers one emergency jump). `Commit | Confirm` button in ACCENT with `nothing departs without Confirm` beneath. Status line shows credits `14,250 -> 11,550`.

## 6. Copy rules

- Sentences, not fields: `Kessel runs out of fuel in about 38 hours.` The data is one tap behind.
- Sources are named and dated; track records are two counts, never a percentage.
- Every stake appears before its click; every answer shows its projection first (GDD §7, feedback twice).
- British spelling in prose (`HARBOUR`), identifiers per AGENTS.md R11.
- **Every string the client draws is ASCII**, 0x20-0x7E. The bitmap font is 96 glyphs covering exactly that range (R13), so a character outside it cannot be drawn at all — there is no fallback glyph and no substitution at run time. This is a rule about rendered copy, not about this document's prose, which keeps `§` and `—` like the rest of the repository. The screens were authored with typographic characters and the substitutions below were applied; the PNGs still show the originals, and where a PNG and this file disagree the file wins (Agent-Prompt rule 1).

 | Was | Is | Used for |
  |---|---|---|
 | `·` | <code>&#124;</code> | the separator between peer items, everywhere |
 | `→` | `->` | routing, a lane's direction, a trigger's action, a value becoming another |
 | `≤` | `<=` | a threshold in a plan rule |
 | `×` | `x` | the rate control (`1x 4x 24x`) |
 | `−` | `-` | a stepper's decrement, and a negative number |
 | `–` | `-` | a range (`5-9 h`) |
 | `…` | `...` | an elision |

  The hyphen now carries decrement, negative and range. That is what ASCII terminal interfaces have always done and context separates them: `upkeep -620 / day` reads as a negative, `5-9 h` as a range, and a stepper's `-` sits in its own cell beside a `+`.
