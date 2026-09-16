# ADR-010 — The borderless window

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-020
**Cites:** GDD §13; AGENTS.md R12, R13; ADR-008, ADR-009

## Context

ADR-009 made every pass draw into a 1920×1080 scene target and the frame end by presenting that target into the window's client area, scaled with the aspect preserved. It named the cost in its own *What this forecloses*: the 1:1 guarantee is lost whenever the scale is not 1, and "this client is dense with 8-pixel bitmap text, and resampling an 8×8 glyph by a non-integer factor is the one thing bitmap type is worst at."

The owner then chose the *fit* policy for the window the same day: the client area is the requested pixels where the work area can hold a window around them, and otherwise the largest area of the same shape that it can. That is what NC-020 implemented and what shipped on `main`.

**The first desktop run of NC-020, on 2026-09-16, is what reopened it.** The owner ran the executable and asked why the work area is not 1920×1080 when every screen in the game is authored at that size. The answer is arithmetic rather than configuration, and it is worse than ADR-008 recorded:

| On this machine (1920×1080 panel at 125 %) | |
|---|---|
| Panel | 1920×1080 physical |
| Taskbar | 60 px → work area 1920×1020 |
| Caption and borders of the fixed style | 18×47 |
| Largest client area a decorated window can have | 1902×973 |
| What *fit* therefore produced | **1729×973, a scale of 0.9005** |

A decorated window needs 1127 px of window to hold 1080 px of client area, on a screen that has 1080. **It does not fit even with the taskbar hidden** (1033 px available), and it does not fit at 100 % scaling either (that gives 1765×993, a scale of 0.9193). The conclusion is not about this machine: *no* window with a title bar can present 1920×1080 unscaled on a 1920×1080 display, and 1080p is the most common PC display there is. The *fit* policy therefore guaranteed a resampled screen for the majority of players and for the owner, on the one thing ADR-009 said was worst at being resampled.

Removing the frame is the only thing that gives the client area the monitor's exact pixels. The owner asked for it on 2026-09-16.

## Decision

**The window is borderless and covers the whole of the primary monitor.** Its client area is that monitor's size in physical pixels, and on a 1920×1080 monitor that is exactly the screen, presented 1:1 and unfiltered.

1. **`WS_POPUP`, extended style zero.** No caption, no border, no system menu, no minimize box — and deliberately **not** `WS_EX_TOPMOST`, because a window covering the screen that also insists on being above everything is one a player cannot get out from under and one that fights a debugger.
2. **The geometry is the monitor's, not the caller's.** `Window::Desc` carries a title and nothing else. The window is created at the primary monitor's origin with the monitor's width and height, so the client area equals the window rectangle, which equals the monitor.
3. **Physical pixels, because the process is per-monitor-v2 DPI aware** (the executable's manifest, NC-001). This matters more here than under any windowed policy: an unaware process is told a 1920×1080 monitor at 125 % is 1536×864, would size itself to that, and Windows would stretch the result — the blur R12 exists to prevent, on every machine rather than only on a display too small for the screen. `Create` checks the client area it got against the monitor it asked for, so a virtualized process fails loudly instead of shipping soft.
4. **This is not exclusive fullscreen.** No display mode is changed and no swap chain goes fullscreen. Alt+Tab, the taskbar, the debugger and a second monitor all behave as they do for any ordinary top-level window.
5. **ADR-009 is unchanged in substance.** The scene target is still 1920×1080, the game still never draws at any other size, and the present step still fits that target into the client area with the aspect preserved. What changes is which client areas occur: on a 1920×1080 monitor the fit is now the 1:1 unfiltered path rather than a 0.90 bilinear one.
6. **Escape and Alt+F4 are the way out**, and until NC-024 gives input a home that is the whole of it, because a borderless window has no close box.

## What this forecloses

**A windowed mode, entirely.** There is no window to move, drag or put beside anything else. Running the game next to the debugger on a single display is gone; a second monitor is the answer, and on a laptop that is a real cost to whoever is debugging a rendering problem.

**Pixel-perfect presentation on displays *larger* than 1920×1080 — and this is a genuine reversal, not a free win.** Under *fit*, a 2560×1440 desktop produced a client area of exactly 1920×1080 and a scale of 1: unfiltered, in a window, surrounded by desktop. Under this decision the same display produces a client area of 2560×1440 and a scale of 1.333, which is neither 1:1 nor an integer multiple, so the present step lands on bilinear and the text is soft. **The decision trades the large-display case for the 1920×1080 case**, on the grounds that 1080p is the most common display and is the one in front of the owner.

Some of that is recoverable and the recovery is not this ADR's to mandate: NC-021's present step could choose the largest *integer* scale that fits and letterbox the rest, which would make 3840×2160 a crisp 2× and 2560×1440 a pixel-perfect 1920×1080 inside black bars. ADR-009 already asks NC-021 to photograph 1:1 against a scaled case rather than argue about it; that picture is what should settle this, and until it exists the straightforward fill is what the code does.

It does **not** foreclose going back. The fit arithmetic is one commit away in this file's history, and the three policies NC-020 round 7 set out — fit, resize, keep — are all still reachable. What going back would cost is exactly the thing this decision bought.

## Consequences

- **`Window::Desc` loses `clientWidthPixels` and `clientHeightPixels`.** There is nothing to ask for. Every call site passes a title.
- **`WindowFault::FrameArithmetic` becomes `MonitorQuery`** — there is no frame to compute, and the way creation now fails early is Windows declining to say how big the primary monitor is. `DesktopTooSmall` survives with a narrower meaning: the primary monitor reports no pixels at all.
- **`FittedToDesktop()` becomes `RequiresPresentScale()`**, which is what the renderer actually wants to know and is now simply "the monitor is not 1920×1080".
- **About 110 lines of arithmetic are deleted**: `FitClientAreaToWorkArea`, `FramePadding`, `FrameForClientArea`, `CenterOnPrimaryMonitor`, and the re-fit after a cross-DPI creation. The property-based driver that measured the fit over roughly 86,000 desktop sizes goes with them; the technique it demonstrated is worth keeping and the arithmetic it checked no longer exists.
- **The `WM_GETMINMAXINFO` override stays, and is now unreachable.** It defeats the desktop-sized clamp in `ptMaxTrackSize` that cost NC-020 four red CI runs. A window that is exactly one monitor can never be clamped by a limit that is the size of the whole desktop, so no path through `Window::Create` exercises it any more, and its regression test — which asked for a client area wider than the desktop, which `Desc` can no longer express — is gone with it. It is kept as insurance rather than as live code: one branch in a message this class receives a handful of times, against a trap that took four CI rounds to find and would return silently if the window ever regained a caption or a size of its own. That is an asymmetric bet, and this paragraph is the record of taking it deliberately.
- **Escape stops being a convenience and becomes load-bearing.** NC-024 inherits an obligation the windowed policy did not create: whatever replaces this path must still close the game, because nothing else on screen can.
- **NC-021 needs no change.** Its present pass already specifies 1:1 and unfiltered at 1920×1080, point at an exact integer multiple and bilinear otherwise; this decision only changes which of those three the common case lands on.
- **The primary monitor, always.** Which monitor the game opens on, and what happens when it is not the one the player wants, is not decided here and nothing in the code assumes an answer.

## Measurements

Every figure above was measured on the developer's machine on 2026-09-16, not estimated.

- **The display and the frame.** A PowerShell process was put into per-monitor-v2 awareness with `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`, then `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`, `SystemParametersInfoW(SPI_GETWORKAREA)` and `AdjustWindowRectExForDpi` were called directly: primary monitor 1920×1080 physical at 120 dpi (125 %), work area 1920×1020, and a frame padding for `WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX` of 18×47 at 120 dpi and 16×39 at 96 dpi.
- **What *fit* produced, 1729×973 at 0.9005**, and the 100 % comparison of 1765×993 at 0.9193, were computed from those measured numbers with the same arithmetic `FitClientAreaToWorkArea` used. The 100 % case assumes a 48-pixel taskbar, which is its height at that scaling; the rest is measured.
- **What this decision produces** was read off the running executable rather than reasoned about: `x64\Debug\NomadCommander.exe` was launched and queried from a per-monitor-v2-aware process — `GetClientRect` 1920×1080, `GetWindowRect` 1920×1080 at (0,0), `GetDpiForWindow` 120, style `0x94000000` (`WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS`), extended style `0x00000000`. The client area matching the monitor at 120 dpi is also what confirms the manifest's DPI awareness is in force; an unaware process would have reported 1536×864.
- **Escape** was verified the same way: `PostMessage(WM_KEYDOWN, VK_ESCAPE)` to the running window, process exit code 0.
- **Not measured, and owed:** the appearance of 8-pixel text at 0.9005 against 1:1. Nothing in this tree draws a glyph yet (NC-023), so the claim that the resampling is the cost worth avoiding rests on ADR-009's reasoning and not on a photograph. ADR-009 already puts that photograph on NC-021; this decision does not discharge it.
