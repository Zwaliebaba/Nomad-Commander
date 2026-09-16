# NC-024 — Input

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | S | **yes** | no | Open |

**Depends on:** NC-020
**Read first:** GDD §3 (what a desk session does with a mouse), §13; AGENTS.md §2 (NeuronClient: input)

## Goal

A per-frame snapshot of the mouse and keyboard, built from the window's messages, with pressed and released edges, so that the UI can ask "was this clicked" without keeping its own state.

## Deliverables

- `NeuronClient/InputState.h` + `.cpp`: `class InputState` with `MousePosition()` in client pixels, `MouseDown(button)`, `MousePressed(button)`, `MouseReleased(button)`, `WheelDelta()`, `KeyDown(vk)`, `KeyPressed(vk)`, `KeyReleased(vk)`, `TypedCharacters()` (a `std::u16string_view` of this frame's `WM_CHAR`s, for the few numeric fields the desk has), `BeginFrame()` (rolls edges and clears typed characters), and `HandleMessage(UINT, WPARAM, LPARAM)` that `Window` calls.
- `Window` gains a message sink so `Main.cpp` connects the two.
- `NeuronClientTests/InputStateTests.cpp`: feed synthetic messages, assert edges across frames.

## Acceptance criteria

- [ ] A click held across three frames reports `Pressed` once, `Down` three times, `Released` once.
- [ ] Mouse position is in client pixels and matches where the cursor is over the window on a 150 % display (DPI awareness from NC-001).
- [ ] Focus loss (`WM_KILLFOCUS`) clears every down state so a key does not stick.
- [ ] Escape in `Main.cpp` now closes the window through `InputState`, not a special case in the window procedure.

## Verification

```powershell
x64\Debug\NomadCommander.exe        # the test pattern follows the mouse and reports clicks in the title or on screen
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Raw Input, gamepads, key rebinding, IME.

## Notes

- `WM_MOUSEWHEEL` positions are screen coordinates; convert with `ScreenToClient`.
- Capture the mouse on button down (`SetCapture`) so a drag that leaves the window still releases.

## Report

_Filled in on hand-back._
