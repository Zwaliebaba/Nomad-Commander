# NC-020 — The window

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient, NomadCommander | M | **yes** | **yes** | Done (PR #3), desktop run outstanding, **window policy open (ADR-009)** |

**Depends on:** NC-002
**Read first:** GDD §13; AGENTS.md §2 (NeuronClient), §4 (`NeuronCore.h` owns the macros; `NOGDI` means GDI is gone), R12 (1920×1080, presented 1:1), R13

## Goal

A Win32 window whose client area is exactly 1920×1080 physical pixels, that cannot be resized or maximized, that pumps messages without blocking the frame, and that the executable opens and closes. It is the first thing a person can see, and the one place `<windows.h>`'s window functions are called.

## Deliverables

- `NeuronClient/Window.h` + `.cpp`: `class Window` with `struct Desc { std::uint32_t clientWidthPixels; std::uint32_t clientHeightPixels; const wchar_t* title; }`, `[[nodiscard]] static bool Create(const Desc&, Window&)`, `Handle()` (`HWND`), `PumpMessages()` returning `false` when the window has been closed, a fixed-size style (`WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX`), `AdjustWindowRectExForDpi` for the frame, centred on the primary monitor; per-monitor-v2 DPI awareness confirmed at runtime (`GetDpiForWindow`), so the client area is the pixels asked for.
- `SCREEN_WIDTH_PIXELS` and `SCREEN_HEIGHT_PIXELS` as `inline constexpr` in `Window.h` (the shape of AGENTS.md's worked example); NC-021's swap chain and test target take the size through their `Desc` from these.
- `NomadCommander/Main.cpp`: creates the window with those constants, pumps until closed, exits 0. Escape closes the window too, until NC-024 gives input a home.
- `NeuronClientTests/WindowTests.cpp`: creates and destroys a window on the CI runner (a window can be created without a desktop session; showing it is optional), asserts the client rectangle.

## Acceptance criteria

- [ ] On a 100 % and a 150 % display, `GetClientRect` reports 1920×1080 and the window is not blurred or scaled by the system (the manifest's DPI awareness from NC-001 is in force; the report says which displays were tried). A 1080p display cannot show the whole window; that it overhangs is expected, and what matters is that the client area measures 1920×1080.
- [ ] The window cannot be resized by the frame or maximized; the close box and Escape both end the process with exit code 0.
- [x] `PumpMessages` returns promptly with no messages pending (`PeekMessage`, not `GetMessage`).
- [x] No GDI call anywhere (`NOGDI` makes one a compile error; the criterion is that nobody worked around it).
- [x] `WindowTests` pass on the CI runner (run 18, 86 of 86 green; re-proved at 1920×1080 in the run this change triggers).

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
x64\Debug\NomadCommander.exe        # a 1920×1080 window, black; close it
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None by this task. The screen changed from 1280×720 to 1920×1080 by owner decision on 2026-09-16; AGENTS.md R12 carries it, which is where the screen is stated. The question R12 now leaves open — what the game does on a desktop that cannot hold its screen — belongs to whichever task first needs an answer, and that task writes the ADR.

## Out of scope

Rendering (NC-021), input state (NC-024), fullscreen, resizing, a second window, an icon (`.rc` files wait until something needs one).

## Notes

- `RegisterClassExW` with `LoadCursorW(nullptr, IDC_ARROW)`; every project is Unicode, so the `W` functions and `L""` literals throughout.
- `DXGI_MWA_NO_ALT_ENTER` is set in NC-021; here, `WM_SYSCOMMAND` `SC_MAXIMIZE` is simply not offered by the style.
- The window procedure forwards to the `Window` instance through `GWLP_USERDATA`; keep the procedure in the `.cpp` in an anonymous namespace.

## Report

**Not verified here, and this task is not finished until it is:** the first two acceptance criteria are desktop criteria. Nobody has yet seen this window on a 100 % or a 150 % display, clicked its close box, or confirmed that Windows is not scaling it. This session is Linux; there is no Windows machine attached to it. The owner runs `x64\Debug\NomadCommander.exe`, sees a 1920×1080 window titled "Nomad Commander", closes it with the close box and with Escape, and records here which displays were tried. Until then NC-021 should not be built on it.

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

**Round 5, after CI — the window is right, and the tree reached a step it had never got to.** [Run 18](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35110234040) ran **86 of 86 tests green**, `WindowTests` included: the client area is exactly 1280×720 on the same 1024×768 runner that could not produce it for four rounds, and `AClientAreaLargerThanTheDesktopIsStillTheSizeAskedFor` passes too. The clamp was the whole of it.

The job then failed at `RunClangTidy.py`, which **no previous run had reached** — `vstest` had aborted the job before it every time since NC-012. So the five findings are not a regression from round 4; they are the first sight of work written across NC-012 to NC-020. Three translation units of twenty-six.

One is a real defect. `bugprone-exception-escape` on `Window::Create`, which is `noexcept`: the note chain names `Window.cpp:128`, `const std::wstring title(_desc.title)`, with every frame above it inside `basic_string`'s constructor reaching `_Throw_bad_array_new_length`. An allocation in a `noexcept` function is `std::terminate` on a machine low on memory, and the allocation existed only to null-terminate a `std::wstring_view`, which is not guaranteed to be. `Window::Desc::title` is now a `const wchar_t*`, handed to `CreateWindowExW` as it stands — which is what R13 makes every title in this game anyway, and what all three existing call sites were already passing. `Create` now allocates nothing at all.

Three are `performance-no-int-to-ptr` on casts the Win32 message protocol *defines*: `WM_NCCREATE`'s `lParam` is a `CREATESTRUCTW*`, `WM_GETMINMAXINFO`'s is a `MINMAXINFO*`, and `GWLP_USERDATA` round-trips the `this` that `SetWindowLongPtrW` stored. There is no cause to fix and no alternative spelling R14 permits. Each carries a `NOLINTNEXTLINE` naming that one check and the contract it relies on. Because this recurs at every future window procedure and callback, the convention is written down rather than left to be re-derived: **ADR-007** draws the line — a finding about what the code *does* is fixed, only a finding about what an external contract *is* may be suppressed, never by editing `.clang-tidy` and never as a bare `NOLINT`.

Two are `bugprone-implicit-widening-of-multiplication-result` in NC-015's test files, where a byte count was multiplied in `unsigned int` and then widened. Both are now named `constexpr std::size_t` constants that multiply in the right width (R3, R6).

**Verified here:** `performance-no-int-to-ptr` and `bugprone-implicit-widening-of-multiplication-result` were reproduced on scratch units with clang-tidy 22.1.8 — the version CI pins — against this repository's own `.clang-tidy`, and both come back clean in the fixed spelling. `bugprone-exception-escape` could **not** be reproduced locally: its throw path runs through MSVC's standard library and libstdc++ does not offer the same one, so that fix is reasoned from the note chain rather than measured, and the next CI run is what confirms it. `Window.cpp` and `WindowTests.cpp` parse clean under clang++ and g++ in both configurations after the signature change; `CheckFormat.py` and `CheckProjectFiles.py` pass.

**Noticed, left alone:** `Plan/Roadmap.md`'s table of ADRs the plan expects was not amended. ADR-007 is a decision the plan did not foresee, and the table records predictions, not the register — `Design/ADR/` is the register.

**Round 6 — the screen is 1920×1080 (owner decision, 2026-09-16).** The rounds above are left as they were written: they are the record of five CI runs against a 1280×720 screen, and rewriting their numbers would falsify it. What they say happened, happened at 1280×720.

What changed is `SCREEN_WIDTH_PIXELS` and `SCREEN_HEIGHT_PIXELS`, and the documentation that quotes them — AGENTS.md's opening statement, its §1 worked example, its §2 repository map and R12; this plan's roadmap, glossary and the task files for NC-001, NC-021, NC-023, NC-025, NC-041 and NC-072. `Design/GameDesign.md` needed no edit: it never stated a resolution, which is why R12 is the only authority on one. No ADR was written, because the screen is an AGENTS.md fact rather than an engineering decision taken while building.

**Nothing in `Window` needed changing, and that is the point.** The class was already written against its `Desc` rather than against two constants, and round 4's `WM_GETMINMAXINFO` override already told Windows the desktop is not a limit. The five tests assert `SCREEN_*_PIXELS`, so they followed the constants; on the CI runner's 1024×768 desktop they now overshoot on *both* axes instead of one, which is a harder case than the one that was red for four rounds. Had round 4 been resolved by making the test tolerant instead of fixing the window, this change would have silently gone untested.

**The cost, stated plainly.** A 1920×1080 client area does not fit on a 1920×1080 desktop: the caption and borders put the window near 1926×1117, taller than the screen before the taskbar takes its share. At 1280×720 the overhang case was CI runners and old hardware; at 1920×1080 it is the most common PC display. The open question in the "Noticed, not decided" note above is therefore no longer an edge case, and R12 now records it as open in its own words.

**GLYPH_SCALE moves from 2 to 3,** in NC-023's deliverable and NC-025's criteria and ADR recommendation. 1080 ÷ 16 is 67½, so a 16-pixel cell no longer divides the screen. The new screen is exactly 1.5× the old one, so a 24-pixel cell gives back the identical 80×45 grid with larger glyphs, and every layout NC-025 and NC-072 assume carries over untouched. Neither task is built, so this costs a line to reverse if the owner would rather have 120×67 cells and the density.

**Round 7 — the window policy is open again, and this task carries it (ADR-009, 2026-09-16).** R12 no longer says the game draws straight into the back buffer: every pass draws into a 1920×1080 scene target, and the frame ends by presenting that target into the client area, scaled with the aspect preserved. The reason was the problem round 6 recorded and could not solve — 1920×1080 does not fit a 1920×1080 desktop — and the 3D map GDD v1.7 put in v0.1, which had no way to be anti-aliased while nothing could be resolved.

**That makes this task less finished, not more.** `Window::Create` still demands a client area of exactly the pixels it was asked for and fails with `ClientAreaMismatch` otherwise. Under the old rule that was the whole promise of the class and five CI rounds went into making it true. Under ADR-009 it is one of three policies, and the other two are now reachable:

1. **Fit.** The client area is 1920×1080 where the desktop can hold it, and otherwise the largest 16:9 area that can. The window stays fixed and unresizable; the present scale does the rest. Smallest change to the class, and it delivers what ADR-009 was asked for.
2. **Resize.** The style gains `WS_THICKFRAME` and `WS_MAXIMIZEBOX`, the client area is whatever the player drags it to, and the scale follows. Most flexible; the largest change, and it puts a `WM_SIZE` path and a swap-chain resize into NC-021.
3. **Keep.** Exactly as today: fixed at 1920×1080, overhanging a desktop that cannot hold it. The scene target still buys anti-aliasing, but the scale is always 1:1 and the display problem is unsolved.

**Until the owner picks one the code does (3), because that is what it already does, and none of the five tests changes.** Picking (1) or (2) rewrites this task's central acceptance criterion — the client area would no longer be exactly what was asked for — so it is not a change to make on inference. The tests as they stand would fail under (1) on the CI runner, which is the clearest possible sign that this is a contract change rather than a tweak.
