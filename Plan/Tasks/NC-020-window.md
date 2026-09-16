# NC-020 — The window

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient, NomadCommander | M | **yes** | no | Open |

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
- [ ] `PumpMessages` returns promptly with no messages pending (`PeekMessage`, not `GetMessage`).
- [ ] No GDI call anywhere (`NOGDI` makes one a compile error; the criterion is that nobody worked around it).
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

_Filled in on hand-back._
