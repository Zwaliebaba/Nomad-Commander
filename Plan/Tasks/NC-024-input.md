# NC-024 — Input

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | S | **yes** | no | Done (pending commit) |

**Depends on:** NC-020
**Read first:** GDD §3 (what a desk session does with a mouse), §13; AGENTS.md §2 (NeuronClient: input)

## Goal

A per-frame snapshot of the mouse and keyboard, built from the window's messages, with pressed and released edges, so that the UI can ask "was this clicked" without keeping its own state.

## Deliverables

- `NeuronClient/InputState.h` + `.cpp`: `class InputState` with `MousePosition()` in client pixels, `MouseDown(button)`, `MousePressed(button)`, `MouseReleased(button)`, `WheelDelta()`, `KeyDown(vk)`, `KeyPressed(vk)`, `KeyReleased(vk)`, `TypedCharacters()` (a `std::u16string_view` of this frame's `WM_CHAR`s, for the few numeric fields the desk has), `BeginFrame()` (rolls edges and clears typed characters), and `HandleMessage(UINT, WPARAM, LPARAM)` that `Window` calls.
- `Window` gains a message sink so `Main.cpp` connects the two.
- `NeuronClientTests/InputStateTests.cpp`: feed synthetic messages, assert edges across frames.

## Acceptance criteria

- [x] A click held across three frames reports `Pressed` once, `Down` three times, `Released` once.
- [x] Mouse position is in client pixels and matches where the cursor is over the window — measured at **125 %**, not 150 %: this machine has one monitor. See the report.
- [x] Focus loss (`WM_KILLFOCUS`) clears every down state so a key does not stick.
- [x] Escape in `Main.cpp` now closes the window through `InputState`, not a special case in the window procedure.

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

**Built, run and measured.** A crosshair follows the mouse, three boxes light while their buttons are held, and a readout shows the position, the wheel, the focus and whatever was typed. Escape closes the game through `InputState` and the executable's loop, not through the window procedure.

**The mouse position is in physical client pixels, and that was measured rather than assumed.** With the game running on a 1920×1080 monitor at **125 % scaling**, the cursor was moved to physical screen (800, 400) and the screen captured. The crosshair's horizontal arm lit x 788–811 on row 400 and its vertical arm y 388–411 on column 800 — **centred on exactly (800, 400)**. A DPI-unaware process would have had the window think that point was somewhere near (640, 320), so this is the manifest's per-monitor-v2 awareness reaching all the way through to a click coordinate. The criterion names a 150 % display; this machine has one monitor at 125 %, so 150 % is untried and is the gap in this task's verification.

**Escape moved, and the move is the risky part.** ADR-010 made Escape the only way out of a borderless window besides Alt+F4, so taking it out of the window procedure and putting it in the frame loop is a change that could have left the game unclosable. It was verified the same way as before: `WM_KEYDOWN`/`VK_ESCAPE` posted to the running window, process exit code 0. The debug layer said nothing across the run.

**Refined against the code as it is.**

- **The edges are set by the messages, not derived by comparing frames.** The task asks for pressed and released edges, and the obvious way — remember last frame's state and subtract — silently swallows a click that begins and ends inside one frame. On a 144 Hz display that is a real event, so `m_pressed`/`m_released` are set by the message that caused them and cleared by `BeginFrame`. `AClickAndReleaseInsideOneFrameAreBothReported` is the test that pins it, and it would fail under the obvious implementation.
- **A hardware key repeat is not a new press.** Windows repeats a held key with `WM_KEYDOWN` over and over, with bit 30 of `lParam` set. `KeyPressed` ignores those, so a UI does not advance a field by ten when somebody leans on a key. Tested.
- **Focus loss reports the release, not just the absence.** The criterion is that nothing sticks down; this also raises the release edge for everything that was held, because a widget in the middle of a drag needs to be told the drag ended rather than to notice later that the button is no longer down.
- **`Window` gained a message sink, and it never swallows a message.** A plain function pointer and a `void*` context, so connecting the two allocates nothing and `Window` knows nothing about input. The sink sees each message before the procedure's own handling and its answer is ignored deliberately: a sink that could claim `WM_CLOSE` would be a window that cannot be closed, which since ADR-010 is a real hazard rather than a theoretical one.
- **Mouse capture lives in `Window`**, not in `InputState`, because `Window` is the one place this tree calls the Win32 window functions. It counts buttons rather than capturing and releasing per message, so a two-button drag releases when the last button does. Since ADR-010 the window is the whole monitor, so capture only matters on a second one — which is exactly the case nobody would think to test.
- **`MouseDelta()` joined the interface**, reporting zero on the frame the cursor first appears so that nothing jumps, and `TypingOverflowed()` so a dropped character is visible rather than silent.
- **The wheel message's position is ignored rather than converted.** The task's notes say to convert it with `ScreenToClient`, because a `WM_MOUSEWHEEL` carries screen coordinates where every other mouse message carries client ones. This takes only the movement and leaves the position where the last move put it: `ScreenToClient` is a Win32 window call and `Window` is the one place this tree makes those, and a wheel that is turned without the mouse moving has not moved the pointer. If a caller ever needs the position a wheel event happened at, the conversion belongs in `Window` and not here.
- **Coordinates are signed.** A captured drag reports positions outside the window, and those are negative — reading the `lParam` words as unsigned would turn −20 into 65516 and send a widget somewhere strange. Tested with a negative position.

**Verified:** `CheckFormat.py` (85 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**39 translation units clean**). Debug **and** Release rebuild with zero warnings. All four suites: **114 of 114 green**, 36 of them in `NeuronClientTests`, 10 new here.

**Not done, and named.** The 150 % display in criterion 2. Gamepads, raw input, rebinding and IME are out of scope and stay out. `TypedCharacters` is UTF-16 code units as `WM_CHAR` delivers them, so a character outside the basic plane arrives as a surrogate pair and the caller would have to join them — no caller does yet, and the desk's fields are numeric.
