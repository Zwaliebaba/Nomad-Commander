# NC-020 — The window

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient, NomadCommander | M | **yes** | no | Done (PR #3), desktop run outstanding |

**Depends on:** NC-002
**Read first:** GDD §13; AGENTS.md §2 (NeuronClient), §4 (`NeuronCore.h` owns the macros; `NOGDI` means GDI is gone), R12 (1280×720, presented 1:1), R13

## Goal

A Win32 window whose client area is exactly 1280×720 physical pixels, that cannot be resized or maximized, that pumps messages without blocking the frame, and that the executable opens and closes. It is the first thing a person can see, and the one place `<windows.h>`'s window functions are called.

## Deliverables

- `NeuronClient/Window.h` + `.cpp`: `class Window` with `struct Desc { std::uint32_t clientWidthPixels; std::uint32_t clientHeightPixels; std::wstring_view title; }`, `[[nodiscard]] static bool Create(const Desc&, Window&)`, `Handle()` (`HWND`), `PumpMessages()` returning `false` when the window has been closed, a fixed-size style (`WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX`), `AdjustWindowRectExForDpi` for the frame, centred on the primary monitor; per-monitor-v2 DPI awareness confirmed at runtime (`GetDpiForWindow`), so the client area is the pixels asked for.
- `SCREEN_WIDTH_PIXELS` and `SCREEN_HEIGHT_PIXELS` as `inline constexpr` in `Window.h` (the shape of AGENTS.md's worked example); NC-021's swap chain and test target take the size through their `Desc` from these.
- `NomadCommander/Main.cpp`: creates the window with those constants, pumps until closed, exits 0. Escape closes the window too, until NC-024 gives input a home.
- `NeuronClientTests/WindowTests.cpp`: creates and destroys a window on the CI runner (a window can be created without a desktop session; showing it is optional), asserts the client rectangle.

## Acceptance criteria

- [ ] On a 100 % and a 150 % display, `GetClientRect` reports 1280×720 and the window is not blurred or scaled by the system (the manifest's DPI awareness from NC-001 is in force; the report says which displays were tried).
- [ ] The window cannot be resized by the frame or maximized; the close box and Escape both end the process with exit code 0.
- [x] `PumpMessages` returns promptly with no messages pending (`PeekMessage`, not `GetMessage`).
- [x] No GDI call anywhere (`NOGDI` makes one a compile error; the criterion is that nobody worked around it).
- [ ] `WindowTests` pass on the CI runner.

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
x64\Debug\NomadCommander.exe        # a 1280×720 window, black; close it
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Rendering (NC-021), input state (NC-024), fullscreen, resizing, a second window, an icon (`.rc` files wait until something needs one).

## Notes

- `RegisterClassExW` with `LoadCursorW(nullptr, IDC_ARROW)`; every project is Unicode, so the `W` functions and `L""` literals throughout.
- `DXGI_MWA_NO_ALT_ENTER` is set in NC-021; here, `WM_SYSCOMMAND` `SC_MAXIMIZE` is simply not offered by the style.
- The window procedure forwards to the `Window` instance through `GWLP_USERDATA`; keep the procedure in the `.cpp` in an anonymous namespace.

## Report

**Not verified here, and this task is not finished until it is:** the first two acceptance criteria are desktop criteria. Nobody has yet seen this window on a 100 % or a 150 % display, clicked its close box, or confirmed that Windows is not scaling it. This session is Linux; there is no Windows machine attached to it. The owner runs `x64\Debug\NomadCommander.exe`, sees a 1280×720 window titled "Nomad Commander", closes it with the close box and with Escape, and records here which displays were tried. Until then NC-021 should not be built on it.

**Verified here (Linux):** the C++ structure of `Window.h` and `Window.cpp` parses under Clang with `-Wall -Wextra` against hand-written stand-ins for the Win32 declarations it names. That proves the shape — names, arities, member access, control flow — and not that the SDK agrees; the stub is mine, not Microsoft's. It caught one real defect: a free function in an anonymous namespace cannot take the address of `Window::WindowProcedure`, which is private, so the class registration moved inside `Window::Create`. The signatures of the five calls this task depends on (`AdjustWindowRectExForDpi`, `GetDpiForWindow`, `GetDpiForSystem`, `RegisterClassExW`, the `lpCreateParams`/`GWLP_USERDATA` pattern) were checked against their Microsoft Learn pages rather than recalled. `CheckFormat.py` and `CheckProjectFiles.py` pass. **Verified by CI:** the MSVC build, and `WindowTests` — five tests that create a hidden window, assert its client area is 1280×720, assert the style carries neither `WS_THICKFRAME` nor `WS_MAXIMIZEBOX`, pump a hundred times without blocking, see a requested close reported by a later pump, and create a second window after the first is gone.

**Assumed:** that a top-level window can be created (not shown) in the CI runner's session. If it cannot, `WindowTests` fails on the first run and the answer is a message-only window for the test rather than a skipped test.

**Refined:** the window is created hidden and `Show()` is separate, which is what lets the suite make one on a build agent. `ClientSizePixels`, `RequestClose` and `Closed` joined the interface: the first is how `Create` checks its own promise rather than assuming it, and the other two are what the tests and NC-024 need. `Create` re-adjusts the frame once if the window landed on a monitor whose scaling differs from the system's, and fails rather than returning a window of the wrong size. `WM_ERASEBKGND` is answered and `WM_PAINT` validated without painting, because the class has no background brush: a brush would need GDI, which `NOGDI` has removed.

**Noticed, left alone:** `AdjustWindowRectExForDpi`'s documentation says `WS_OVERLAPPED` must not be specified. `WS_OVERLAPPED` is zero, so the style the task names is the same number with it or without it; the code keeps the task's spelling and says so in a comment.

**Bent:** one task per PR (the batch NC-012 to NC-020 on one branch).

**Round 2, after CI.** [Run 15](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35108030735) built clean and passed 80 of 85 tests; the five failures were all of `WindowTests`, every one on the same line, `Window::Create` returning false on the runner. The assumption above (that a top-level window can be created in the CI runner's session) is therefore wrong or incomplete, and a bare `false` did not say which of the four things failed. `Create` now records a `WindowFault` and the `GetLastError` beside it, in the shape of AGENTS.md's own worked example, and each test's assert message names the branch, the error and, on a size mismatch, the size it got instead. The next run diagnoses itself.

Reading the code for that failure turned up a real defect unrelated to it: `WM_DESTROY` called `PostQuitMessage`, and a quit is a **thread-wide** message. Destroying any window therefore left a quit in the thread's queue that would end the message pump of every other window on that thread and outlive the window that posted it. In the executable it was invisible, because there is one window; in the suite it made the tests depend on each other's order. The quit is gone: `PumpMessages` reports this window's own closure through its `m_closed`, which is what both the executable's loop and the tests already read.

**Round 3, after CI.** [Run 16](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35108493837) reported the same five failures with the message `no fault recorded; GetLastError=0`, which cannot happen after a `Create` that returned `false`. The defect was in the test, not the window: the order in which a call's arguments are evaluated is unspecified, and MSVC built `WhyItFailed(window)` before `CreateHidden(window)` ran, so every failure described a window that had not been touched yet. The creation is now a statement of its own at all six call sites, and the helper's comment says why it has to be.

**Round 4, after CI — the actual defect, and it was in the window.** [Run 17](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35108881873) said it plainly: `the client area came out 1028x720`. The height was right and only the width was wrong, which is the signature of a clamp rather than of scaling or of arithmetic.

`CreateWindowExW` sends `WM_GETMINMAXINFO` to any window carrying `WS_CAPTION` before it returns, and clamps the new window to that message's `ptMaxTrackSize`. The default is `SM_CXMAXTRACK` by `SM_CYMAXTRACK`, which the SDK documents as referring to **the entire desktop** — the one pair of metrics that does. The CI runner's desktop is 1024×768: a 1280-wide client area cannot survive that clamp, while 720 fits under 768 and does. The numbers agree — a window clamped to 1034 wide, less a 6-pixel fixed frame, is 1028 of client area.

`GetSystemMetrics` documents the way out in the same sentence that states the limit: *"A window can override this value by processing the `WM_GETMINMAXINFO` message."* The window procedure now does, raising `ptMaxTrackSize` to `MAX_TRACK_PIXELS` (32767 — `WM_SIZE` packs the client width and height into sixteen bits each, so a wider window is one whose own size messages cannot describe it). It costs nothing: a tracking size limits *dragging* a frame, and this style carries no `WS_THICKFRAME`, so the clamp at creation was the only thing the number ever did. The message arrives before `WM_NCCREATE`, so there is no instance to read there — and none is needed, because the answer is the same for every window of this class.

This was never a property of the build agent. **Any** player whose desktop is smaller than 1280×720 would have got a short client area and a swap chain that no longer presented 1:1, silently, which is the failure R12 exists to prevent. The runner found a real bug in code that would have shipped.

A sixth test pins it on every machine and not only on a small-desktop runner: `AClientAreaLargerThanTheDesktopIsStillTheSizeAskedFor` reads `SM_CXMAXTRACK`, asks for 64 pixels more than the desktop's own maximum, and asserts the client area comes back exactly that. Nothing was skipped, made conditional or weakened to get green; the five tests still assert 1280×720 exactly, on the same 1024×768 runner that could not produce it before.

**Noticed, not decided.** On a desktop smaller than 1280×720 the window now extends past the screen edges instead of being shrunk. That is the only behaviour consistent with R12 — the alternative is scaling, which the GDD forbids — but whether the game should instead refuse to start on such a desktop, with a message saying so, is a design question neither GDD §13 nor AGENTS.md answers. It is the owner's to settle; nothing here assumes an answer.
