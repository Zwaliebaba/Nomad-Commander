# NC-078 — The receipt and the replay

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 5 | NomadCommander | L | **yes** | no | Open |

**Depends on:** NC-077
**Read first:** GDD §4 (*The receipt* whole), §8 (every receipt names the template; replays searchable by admiral), §13 ("simple battle visualisation"), §15 ("whether they can explain the outcome"); AGENTS.md §5 (2D; nothing towards 3D)

## Goal

What the player finds on return: the receipt's sentences, the events of the operation attributed to the decisions, whether each reading held, the payout, the template's name, and a replay that is a simple 2D visualisation of the battle record, round by round, showing what each side did and which triggers fired and when they were recognized.

## Deliverables

- `NomadCommander/ReceiptScreen.h` + `.cpp`: the composed text from `WireReceipt` (NC-064's sentences), the decisions and their consequences as a list (hypothesis per assumption with held/failed; the courier's fate; the contract's payment; the opinion and belief changes the events carried), and Open replay.
- `NomadCommander/ReplayView.h` + `.cpp`: from `WireBattleRecord`: two columns (own side in full; the other side as reported) with counts per class per round as bars, the template's name and posture per round for the admiral, the plan's rules and which override fired at which round with the recognition delay visible ("heavies appeared round 4; recognized round 6; withdrew"), losses per round, the outcome; play, pause and step; searchable by admiral from the dossier (NC-074).
- The receipts tab: all receipts newest first; a filter by admiral.

## Acceptance criteria

- [ ] The §4 receipt renders word for word from the wire and the owner read it on screen.
- [ ] The replay shows every round of a twelve-round record and the fired triggers with their delays; nothing in it comes from outside `WireBattleRecord`.
- [ ] The visualisation is bars, glyphs and text on the primitive batch; no sprite, no mesh, no camera (AGENTS.md §5).

## Verification

```powershell
x64\Debug\NomadCommander.exe --scenario kessel
```

## Decisions to record

None.

## Out of scope

Animation beyond stepping rounds; sound (A3); a 3D or sprite battle (GDD §13).

## Notes

- "Simple battle visualisation" is a promise to the owner as much as to the player: keep the view to what explains the outcome.

## Report

_Filled in on hand-back._
