# AGENTS.md — Engineering Rules for *Nomad Commander*

Operating instructions for every agent (and human) writing code in this repository. **Read this before generating a single line.**

*Nomad Commander* is a greenfield C++23 game and a hobby project with one developer: an **asynchronous operational strategy game driven by imperfect information and persistent AI relationships**. The player commands a nomad mothership fleet with no country of its own, moving between AI empires that hire it, fear it, suspect it and remember it, in a universe that runs whether the player is present or not. It is single-player by design. Technically it is a Direct3D 12 client and an authoritative simulation host, in **one executable**, presenting a fixed **1280×720 R8G8B8A8** screen, drawn straight into the swap chain's back buffer and presented 1:1. The client is a 2D map with simple battle visualisation, and the design protects that decision. There is no legacy tree here and nothing is grandfathered. A rule below is not a target to migrate towards; it describes the code as it must be written today, and a whole-tree run of any checker comes back clean.

**What is authoritative, in order:**

1. **This file** — conformance: naming, style, build, and how to work here.
2. **[Design/GameDesign.md](Design/GameDesign.md)** — the game design document (v1.6): what the game is, how every system serves the core loop, what v0.1 contains and what waits, and, in its appendix, what is settled and what is open. It is the owner's document. Read the sections your task touches before you start, and cite them by number (this file writes them as *GDD §n*).
3. **`Design/ADR/`** — engineering decisions taken while building, one file per decision (§6). The folder does not exist yet; the first decision creates it.
4. **The surrounding code** — for anything none of the above covers, match the file you are editing.

If a rule here conflicts with a habit from another codebase, this file wins. If you think a rule is wrong or your task cannot be done without deviating, **say so in your report — never deviate silently.**

---

## 1. Naming convention (normative — no exceptions)

| Kind | Convention | Example |
|---|---|---|
| Type (class, struct, enum, concept, alias) | `PascalCase` | `SwapChainTarget` |
| Function, method | `PascalCase` | `PresentFrame()` |
| Member variable | `m_camelCase` | `m_deviceRemoved` |
| Static member (mutable) | `sm_camelCase` | `sm_activeDevice` |
| Global | `g_camelCase` | `g_instance`, `g_frameCount` |
| Parameter | `_camelCase` | `_fileName`, `_fleetId` |
| Local | `camelCase` | `shadedColor` |
| Compile-time constant | `UPPER_CASE` | `WIDTH_PIXELS`, `GLYPH_SCALE` |
| Enumerator | `PascalCase` | `DeviceLost`, `OutOfVideoMemory` |
| Macro | `UPPER_CASE` | `NOMAD_ASSERT` |
| Namespace | `PascalCase` | `Neuron`, `Nomad` |
| File | `PascalCase.cpp` / `.h` | `SwapChainTarget.cpp` |

**Note the split that catches people out: a `constexpr` is `UPPER_CASE`, an enumerator is `PascalCase`.** They are both compile-time and they are spelled differently on purpose — an enumerator is a *value of a type* and reads as one at the use site (`PageFault::OutOfVideoMemory`), while a constant is a number with a name and is meant to look like one. [`.clang-tidy`](.clang-tidy) enforces both, and it is the single source of truth for the option values; this document does not repeat them, so there is nothing to drift.

### The rules behind the table

**R1 — The leading underscore on parameters is deliberate.** It is legal C++: the reserved forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A parameter is never at global scope, so `_fileName` is safe. Never introduce a reserved form — no `_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2 — A type name carries no prefix or affix, and that includes abstract ones.** An interface is `Transport`, not `ITransport`. A base class is not `BaseTransport` or `AbstractTransport`. PascalCase means the name and nothing else. This bans `CFoo`, `SFoo`, `EFoo`, `IFoo`, `FooBase`, `FooAbstract`, `FooImpl` and `_t` suffixes. Name the concept and let the concrete types say what they are:

```
Transport             ← the concept
├── UdpTransport      ← a socket-backed one
└── LoopbackTransport ← in-process, for tests
```

That tree is an illustration of the rule, not a description of anything. A base class for one derived class is ceremony: name the concept, and add the layer when a second thing needs it.

clang-tidy can require an *absent* prefix but cannot see a *present* suffix, so `Build/CheckProjectFiles.py` carries the other half.

**R3 — Compile-time constants are `UPPER_CASE`.** `constexpr`, `inline constexpr` and `static constexpr` members: `WIDTH_PIXELS`, `TICKS_PER_SECOND`, `GLYPH_SCALE`. `sm_` is reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4 — Acronyms capitalize as words**: `HlslSource`, `DxgiFactory`, `UdpTransport` — never `HLSLSource`. Identifiers from an external SDK keep that SDK's spelling (`ID3D12Device`, `DXGI_FORMAT`, `HRESULT`, `IDXGISwapChain4`) and are never renamed to fit.

**R5 — Template parameters are PascalCase**: `T`, `Fn`, `BlockBytes`, `Ts...`.

**R6 — Units belong in names; types do not.** `fuelPerJump`, `upkeepCreditsPerDay`, `arrivalTick`, `confidencePercent` are encouraged — a simulation measured in credits, jumps, ticks and real hours (GDD §5, §7) makes unit ambiguity a real defect class. Never encode the type: no `iCount`, `pFleet`, `strName`.

**R7 — A file is named for its primary type**, PascalCase, `.h` / `.cpp` only. `.hpp`, `.cc` and `.inl` are not used; template implementations live in the header. Exceptions, because MSBuild and the Visual Studio wizards spell them this way: `pch.h`, `pch.cpp`, `framework.h`, `targetver.h`, `Resource.h`.

Adding, removing or moving a file means editing the owning `.vcxproj` **and** its `.filters`. `Build/CheckProjectFiles.py` checks both halves and the on-disk spelling.

**R8 — `m_` marks encapsulated state, not every field.** A `class` with invariants prefixes private members `m_`. A public aggregate — a `Desc` config struct, a wire record, a POD handed to the renderer — uses plain `camelCase` fields so brace initialization reads naturally.

**R9 — One namespace per layer.** Engine code (`NeuronCore`, `NeuronClient`, `NeuronServer`) is `namespace Neuron`. Game code (`GameLogic`, and the game half of the executable) is `namespace Nomad`. The engine knows nothing about this game; if a type needs to know what a courier or a convoy is, it is in the wrong library. Test suites use `namespace <Project>Tests`.

**R10 — No `using namespace` at file scope in a header.** It leaks into every translation unit that includes it, and the failure it causes appears somewhere else. In a `.cpp` it is allowed for the unit-test framework and nothing else; otherwise qualify the name or write a local alias.

**R11 — One spelling per family, and it is the SDK's.** `color`, `initialize`, `serialize`, `normalize`, `quantize`, `synchronize`, `behavior`, `neighbor`, `center`, `gray`, `canceled`. Neither spelling is wrong English; the defect is a tree where a reader has to know which half they are in and a grep for one finds half the uses. `D3D12_CLEAR_VALUE::Color` settles which half wins. Comments and prose are not checked; identifiers are, by `Build/CheckProjectFiles.py`. (The design document is prose and spells `flavour` and `harbour`; that is fine, and an identifier spells `flavor` and `harbor`.)

### Worked example — this is the target style

```cpp
// NeuronClient/SceneTarget.h
#pragma once

#include <cstdint>

namespace Neuron
{

// R3: constant → UPPER_CASE. R6: the unit is in the name.
inline constexpr std::uint32_t SCREEN_WIDTH_PIXELS = 1280;
inline constexpr std::uint32_t SCREEN_HEIGHT_PIXELS = 720;

// R1 (enumerator) → PascalCase, unlike the constants above.
enum class TargetFault : std::uint8_t
{
  DeviceRemoved,
  BadFormat,
  OutOfVideoMemory
};

/// The 1280x720 color framebuffer the game draws into, and the depth buffer that goes with it.
/// R2: no prefix on the type. R8: private state carries m_.
class SceneTarget
{
public:
  struct Desc                                            // R8: aggregate → plain fields
  {
    std::uint32_t widthPixels;                           // R6: unit in the name
    std::uint32_t heightPixels;
    DXGI_FORMAT colorFormat;                             // R4: SDK spelling kept as-is
  };

  [[nodiscard]] static bool Create(ID3D12Device* _device,        // R1: _ on parameters
                                   const Desc& _desc,
                                   SceneTarget& _outTarget) noexcept;

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept { return m_widthPixels; }

private:
  ID3D12Resource* m_depthTarget = nullptr;
  std::uint32_t m_widthPixels = 0;
  bool m_deviceRemoved = false;
};

} // namespace Neuron
```

### Enforcement

| Rule | Enforced by |
|---|---|
| The naming table, R1, R3, R5, R8 | [`.clang-tidy`](.clang-tidy), gated in CI over the whole tree |
| R2 affixes, R7 file names and project registration, R11 spellings, §3 flat directories | `Build/CheckProjectFiles.py`, gated in CI |
| R4, R6, R9, R10 | Review. Check your own diff against the table before handing it back. |

---

## 2. Repository map

| Path | What it is | May you edit it? |
|---|---|---|
| `NeuronCore/` | Engine static library used by **both** halves: `Debug.h`, the typed index `Id`, the pinned PRNG, integer arithmetic the simulation can trust, the byte reader and writer, the `Simulation` seam, the `TickSchedule`, and the wire protocol between client and host — `Socket`, `FrameStream`, `Protocol` | Yes |
| `NeuronClient/` | Engine static library used by the **client only**: the window, the D3D12 device and swap chain, the 1280×720 colour target, input, audio, UI | Yes |
| `NeuronServer/` | Engine static library used by the **host only**: `Session` owns a simulation and drives it on a schedule, a store persists it, a server puts it on a socket. It never names a game type — the seam speaks in bytes | Yes |
| `GameLogic/` | The game itself, in the design's own terms: the universe graph and its generator (GDD §7), empires and their goals, admirals and the eight templates (§8), fleets and the mobility rules (§12), contracts, the economy (§10), evidence and the inference rule (§6), beliefs and opinions (§9), couriers, plans and the branch budget (§4), the tick resolver, and the receipt and the explanation every consequence carries. Host-side; the client never links it | Yes |
| `NomadCommander/` | The executable and the composition root — the one thing that sees both halves. The situation board and the desk session (GDD §3), the 2D map and battle visualisation (§13), hypothesis and plan authoring, the receipt, the client connection, and the hosted simulation. Where every embedded asset and compiled shader ends up | Yes |
| `Tests/NeuronCoreTests/`, `Tests/NeuronClientTests/`, `Tests/NeuronServerTests/`, `Tests/GameLogicTests/` | MSVC CppUnitTest DLLs, one per library, each referencing the library it tests and the libraries that library is built on. **CI builds and runs all four** | Yes |
| `Design/` | The design record: `GameDesign.md` (the design, owner-edited) and `ADR/` (engineering decisions) — see §6 | Yes — see §6 |
| `Build/*.py` | Repository checkers (§6). They gate CI | Yes, carefully |
| `.clang-format`, `.clang-tidy`, `.editorconfig` | Layout and naming, machine-readable (§1, §4) | Yes — with an owner decision |
| `.github/workflows/build.yml` | CI. All of it blocks | Yes, carefully |
| `x64/`, `.vs/`, `*.user` | Build and IDE output | **No — and never commit them** |

**Nine projects, and the edges run one way.** `NomadCommander.slnx` is the solution; its only platform is `x64`.

```
NeuronCore.lib          ← the engine everything else builds on
├── NeuronClient.lib    ← references NeuronCore
├── NeuronServer.lib    ← references NeuronCore
├── GameLogic.lib       ← references NeuronCore
└── NomadCommander.exe  ← references all four

NeuronCoreTests.dll     ← NeuronCore
NeuronClientTests.dll   ← NeuronClient, NeuronCore
NeuronServerTests.dll   ← NeuronServer, NeuronCore
GameLogicTests.dll      ← GameLogic, NeuronCore
```

**`GameLogic` is referenced by the executable and by nothing else.** It is host-side game code, and the design makes that edge a rule rather than a preference: the client shows the player reports, beliefs and explanations, never the truth (GDD §4, §9), so a client-side file that reaches for `GameLogic` is a client that can see through the fog. `NeuronServer` drives a game it cannot see, through the byte-shaped `Neuron::Simulation` seam, which is what keeps that edge absent rather than merely discouraged. Likewise nothing in `NeuronClient` may reach `NeuronServer` or the reverse — they share `NeuronCore` and that is the whole of their common ground.

**Project directories are flat, with exactly two sanctioned subdirectories.** C++ source lives directly in `NeuronCore/`, `GameLogic/` and so on. This is not taste: `.clang-tidy`'s `HeaderFilterRegex` matches headers exactly one level in, so a header in a subdirectory is silently unchecked. `Build/CheckProjectFiles.py` fails the build on one. The two exceptions are the shader pipeline (owner decision, 2026-09-09):

- **`<Library>/Shaders/`** holds the HLSL, hand-written, named `<Shader>VS.hlsl` and `<Shader>PS.hlsl` for the vertex and pixel halves of one shader.
- **`<Library>/CompiledShaders/`** holds what the compiler wrote: one header per `.hlsl`, `<Shader>VS.h` and `<Shader>PS.h`, each declaring a byte array `g_<Shader>VS` / `g_<Shader>PS`. It is **build output** — produced by an `FXCompile` item in the `.vcxproj` on every build, listed in `.gitignore`, skipped by every checker, and never edited or committed. The `.cpp` that binds the pipeline state includes it and nothing else does.

**There are no vendored SDKs and no package manager.** The build depends on the Windows SDK and the MSVC standard library, and on nothing else. See R14.

---

## 3. Build and verify

**x64 is the only platform.** There are no Win32/x86 configurations in any `.vcxproj` or in the `.slnx`; do not add them, and do not write code that only works at 32 bits. Toolset `v145` (Visual Studio 2026), `/std:c++latest`, `/permissive-`, `/W4` with **warnings as errors**, and there is no CMake. If a build error tempts you to change the toolset, lower the language standard, turn off `/permissive-` or silence a warning — stop and report instead.

**Debug and Release are aligned by rule, not by luck.** Every setting that is not *about* optimisation reads identically in both configurations: language standard, conformance, warning level, include directories, precompiled header, floating-point model. The two differ in exactly four things — `Optimization`, `_DEBUG` vs `NDEBUG`, `FunctionLevelLinking`/`IntrinsicFunctions`, and the linker's folding and LTCG switches. `Build/CheckProjectFiles.py` fails the build when anything else drifts apart.

That check matters more than it looks, because **CI builds Debug only** (§6). Release is compiled by whoever ships, and a Release that quietly lost an include directory or sat on an older language standard would not be discovered until then. The static check is what stands in for the build nobody runs.

```powershell
# Build everything: the executable, the four libraries it references, and the four test DLLs.
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo

# Just the game and its libraries, still through the solution.
msbuild NomadCommander.slnx /t:NomadCommander /p:Configuration=Debug /p:Platform=x64 /m /nologo

# Release, before you claim anything about it.
msbuild NomadCommander.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
```

All commands run from the repository root, and all of them name the **solution**.

**Build through `NomadCommander.slnx`, never a `.vcxproj` directly.** Output paths and cross-project include directories are anchored on `$(SolutionDir)`, and MSBuild defines `SolutionDir` only for a solution build. `msbuild NeuronCore\NeuronCore.vcxproj` therefore resolves every one of those paths against the *project* folder instead of the repository root. **It does not fail — that is the problem.** Output lands in `NeuronCore\x64\Debug\` instead of `x64\Debug\`, so the next solution build links against whichever copy is staler, and `$(SolutionDir)NeuronCore` becomes a path relative to the project that does not exist. The include breakage is latent: it bites the first time a file reaches across projects, which for a fresh test suite may be weeks after someone got into the habit. To build one project, use `/t:<ProjectName>` on the solution, as above. Everything lands in `x64\Debug\` (or `x64\Release\`) at the repository root; intermediates stay in each project's own `x64\` folder, which is `IntDir`'s default base.

**A project does not put its own directory on the include path.** `cl.exe` already searches the directory of the including file first for a quoted include, so `#include "FileSys.h"` from `NeuronCore\FileSys.cpp` resolves without help. Only the directories of *other* projects are listed, as `$(SolutionDir)<Project>`.

**Run the tests.** All four suites, through `vstest.console.exe`:

```powershell
vstest.console.exe x64\Debug\NeuronCoreTests.dll x64\Debug\NeuronClientTests.dll `
                   x64\Debug\NeuronServerTests.dll x64\Debug\GameLogicTests.dll /Platform:x64
```

**vstest reports "no tests found" as a pass.** An empty suite is therefore worse than no suite: it is a green check mark over a library nobody exercised. Each project ships a placeholder `SuiteSmoke` for exactly this reason; delete it when the first real test lands, never before.

**Run the checkers before you push.** They are seconds of Python and they are what CI runs:

```powershell
python Build\CheckFormat.py           # clang-format, whole tree. --fix rewrites the offenders
python Build\CheckProjectFiles.py     # build shape, project registration, R2/R7/R11
python Build\RunClangTidy.py          # needs a Developer PowerShell (INCLUDE must be set)
```

**A green build says nothing about whether the game draws.** For anything touching rendering, input, audio or presentation, launch it:

```powershell
x64\Debug\NomadCommander.exe
```

**Report what you actually did.** "Builds clean, not run" and "builds and runs" are different claims. Never imply the second when you only did the first, and say which configurations you built.

---

## 4. Layout and formatting

[`.clang-format`](.clang-format) is the authority for C++ layout: 2-space indent, 140 columns, Allman braces, pointer and reference bound left, includes never reordered. [`.editorconfig`](.editorconfig) covers everything clang-format does not — CRLF, UTF-8, final newline, trailing whitespace, and the non-C++ formats — and repeats the two numbers an editor needs before the first save.

**This tree is formatted, and CI keeps it that way.** Unlike a migration repository, a whole-tree `CheckFormat.py` run here is a no-op. Format what you write; if the check fires, run `--fix` and commit the result rather than arguing with it.

- **Do not reformat what your task did not touch.** The check being green tree-wide means a drive-by reformat produces pure churn and buries your actual change.
- **Include order is load-bearing and grouped by hand**, which is why `SortIncludes` is `Never`: `pch.h`, then `<windows.h>` before any D3D12/DXGI/XAudio2 header, then the rest of the SDK, then project headers, then the standard library. A formatter reordering these behind a change's back is a correctness risk, not a style preference.
- **`NeuronCore.h` owns the Windows macro family, and nothing else defines any of it.** `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `NODRAWTEXT`, `NOGDI`, `NOBITMAP`, `NOMCX`, `NOSERVICE`, `NOHELP` are set there, before `<windows.h>`, and the `.vcxproj` files deliberately define none of them. Two owners of one macro is C4005, and `/WX` makes that fatal — `/D` spells a bare macro as `1` where a `#define` spells it as nothing, so the collision is guaranteed rather than possible. If you need `<windows.h>`, include `NeuronCore.h`; do not add the macros yourself.
- **`NOGDI` means GDI is genuinely gone**, not discouraged. `GetStockObject`, `TextOut` and their kin are not declared. That is the point: the swap chain owns every pixel, and there is no case in this game where a GDI call is the right answer.
- Do not silence a diagnostic with `#pragma warning(disable: ...)` to make a build pass. Fix the cause, or report it.

---

## 5. Rules for this codebase

**R12 — Graphics is Direct3D 12 only**, and the screen it presents is fixed. **1280×720 `R8G8B8A8_UNORM`** — not `_SRGB`, so a channel authored as `0xAA` is presented as `0xAA` — drawn straight into the swap chain's back buffer, whose client area is those same 1280×720 pixels. There is no intermediate render target, no resolve pass and no present scale. No D3D11, no D3D11On12, no immediate-mode helper layers. COM lifetimes are RAII from the first line — a raw `AddRef`/`Release` pair in new code is a defect, not a style.

**The client is 2D, and the design protects that.** GDD §13: the game's complexity is informational, not visual; v0.1 is a 2D map with simple battle visualisation; the 3D client is "the reward for a working 2D game, the largest cost in the project and the least validated value, and it waits." A mesh pipeline, a camera, a lighting model or any other step towards it is not a task anyone has. It appears in a report as a proposal, never in a diff.

**The renderer starts with no sampler, no blending and no multisampling, and adding one is a decision.** A bitmap font is read with `Texture2D<uint>::Load()`, which takes integer texel coordinates and has no filtering to switch on, and the shared pipeline defaults turn blending, multisampling and anti-aliased lines off for every pipeline built from them. A pass that wants one of those has to say so: **blending, multisampling or a sampler in a new pass is an ADR, not a pipeline field.**

**R13 — The executable ships alone.** There is no assets folder, no data directory, nothing beside `NomadCommander.exe` at runtime. Art, colours, fonts and sound are embedded as `constexpr` arrays in headers — a bitmap font is 96 glyphs, 8×8, one bit a pixel, 768 bytes, and nothing to load. **Shaders are compiled at build time**, never at runtime: `<Library>/Shaders/<Shader>VS.hlsl` goes through the `.vcxproj`'s `FXCompile` step into `<Library>/CompiledShaders/<Shader>VS.h` as `g_<Shader>VS` (§2). No `D3DCompile`, no `d3dcompiler_47.dll` beside the executable, no `.cso` on disk. Never add a runtime file dependency, a working-directory assumption or a "just for development" loose-file path; the loose path is the one that ships.

**R13 binds a process acting as the CLIENT, and has exactly two sanctioned exceptions.** A process **acting as the host** may write **one universe store** — the file a universe is reloaded from, which is what "the universe runs continuously whether the player is present or not" (GDD §1, §7) costs — and **one instrumentation log**, the timestamped event stream from which the measured outcomes in GDD §15 are counted rather than guessed. Nothing else, and never a client. The store's form — a full state snapshot, or the seed and every player input re-resolved, which R16 makes possible — is an ADR that has not been written; write it before the first byte is saved. The exceptions are named rather than general: the next thing that wants to write a file is a new decision, not an inference from these.

**It is a role and not a binary.** v0.1 is one executable that hosts the simulation and the client in one process, on a compressed local clock (GDD §15); that process acts as the host and may write those two files. Milestone 2 needs the simulation to run headless, for simulated decades, with no client at all; the full game needs an always-on host that a client joins, and later a light panel for check-ins (§7, §15). The hosting model itself is deferred by the design (§14, appendix), so nothing about how a client reaches a remote host is decided here. What is decided is the edge: **the simulation never depends on a client being present**, and the shipped executable still needs nothing beside it — a client reads nothing at all, and both permitted files are ones the host *creates*, never ones it requires in order to start.

**A path a host writes resolves beside the executable, not against the working directory.** R13's ban on a working-directory assumption binds the two files it allows exactly as hard as the ones it forbids — a log written relative to the launch directory is a log that silently goes somewhere nobody looks.

**R14 — No third-party dependencies and no package manager.** The Windows SDK and the MSVC standard library, and nothing else. If you believe something is unavoidable, propose it in your report with what it buys and what it costs — do not add it. This is a closed list, not a high bar.

**R15 — Memory is plain C++.** `new`/`delete` where it must be, RAII everywhere, standard containers by default. No pool, slab or free-list allocator without an owner decision recorded in `Design/ADR/`.

**R16 — Determinism is a property of the simulation, and it is built, not hoped for.** Every project compiles `/fp:precise` with no `/arch`, stated explicitly in the `.vcxproj` rather than inherited from an MSVC default — a default is not a decision, and the symptom of losing one is two builds of the same simulation disagreeing about the same sum with no line to blame. In `GameLogic`, additionally: no `float` where a fixed-point or integer quantity will do (the evidence weights in GDD §6 are fractions of a full attribution — hold them in integer hundredths), no iteration over an unordered container whose order reaches the simulation, and no wall-clock time — the tick is the clock. The design is why this is not optional: the receipt and the replay (§4, §8) are features, a Milestone 2 run of five empires for simulated decades has to reproduce from its seed for a bug in year thirty to be findable, and the sandbox measurements in §15 have to be re-runnable. "A small random spread remains" (§4) is the pinned PRNG in `NeuronCore`, seeded from the universe store — never `std::random_device`, never a hash of an address, inside the simulation.

**R17 — A string you do not write is `const`.** `/permissive-` turns on `/Zc:strictStrings`: a literal is `const char[N]` and will not bind to `char*`. The fix is `const` on the signature, never a cast at the call site — a `const_cast` here is a lie about a literal that lives in a read-only section, and writing through it is a real crash rather than a theoretical one.

### What the design imposes on the code

The GDD is a design document, not a technical one, but several of its rules are constraints on how the code is shaped. They are written down here so nobody has to rediscover them from a bug.

**R18 — Reality, belief and evidence are distinct types, and the AI reads only the second two.** GDD §4 and §9: "the AI sees the player through the same fog: an admiral plans against reports about the player's fleet, not against its true position and strength," and the reliability shown for a report "is the source's track record, never the game's own knowledge of the truth." So the world state, an empire's belief state and the evidence behind it are separate types, and a function that chooses an admiral's template, an empire's action or a contract offer takes belief as its input and cannot reach ground truth through it. A decision routine with a world-state parameter is a defect, however convenient. The same rule reaches the client (§2): it is handed reports, beliefs and explanations, never the world.

**R19 — Every consequence carries its explanation.** GDD §9: "Every major event explains itself." The simulation does not emit "claim revoked"; it emits the revocation together with the belief it acted on, its confidence, and the evidence for and against, in the shape of the §9 example, because the receipt (§4) and the accusation panel (§3) are built from that record and nothing else. An event without its explanation is a defect in the simulation, not a gap in the UI.

**R20 — Tuning values are data, named, and cite their section.** The evidence weights and thresholds in GDD §6, the playstyle levers in §10, the starting clock in §7, and upkeep and hull prices in §5 are "open, answered by play" (appendix): they exist to be changed. Each lives in one named `constexpr` table with a comment naming the GDD section, and the resolver reads the table. A literal `30` in the inference code is a magic number twice over — nobody can find it to tune it, and nobody can tell whether it is a weight, a threshold or a percentage.

**R21 — The tick is the clock, and a timer does not know whether the player is online.** The universe runs while the player is away (GDD §7), the full game paces jumps in real hours, and "timers apply identically online and offline." So the simulation advances in ticks and nothing inside it reads wall-clock time (R16); the host maps wall time to ticks at the seam, which is also where v0.1's compressed local clock lives. A reinforcement timer, an offer's expiry, a courier's arrival and a mothballing grace period are all tick counts, and a code path that behaves differently because a client is connected is a bug the design names.

**R22 — The nomad is an entity type, not the player.** GDD §14: "the kernel treats the nomad as an entity type with any number of instances from day one, which costs nothing and avoids a rewrite." The mothership, its fleets, officers, outposts, record and reputation hang off a nomad `Id` like any other entity; there is no singleton player object, no global "the fleet," and an empire's opinion is of a nomad, not of "the player." That the first game has one nomad is a fact about the save, not about the types.

**R23 — Build what v0.1 needs and nothing beyond it.** GDD §15 lists the scope: three empires and about ten systems; four goods with abstract per-system production; hull upkeep, insolvency and the hull market; the mothership floor; four ship classes (scout, raider, warship, hauler) with the §12 mobility rules; three or four admirals drawing on the eight-template list; two contract types, escort and raid; reports with source, age and reliability; hypothesis as selection; plans with a branch budget; courier-carried orders; the §6 inference rule with empire covert raids; one belief per empire and one opinion per character; a compressed local clock and a 2D client. It also names what waits: "No production chain, no 3D, no always-on host, no memory layers, no ghosts," and the full game adds the light panel, notifications and the prologue later. Tier 3 systems — the player's own production, deep logistics, territory mechanics, long-term faction dynamics (§2) — are built only when a Tier 1 decision demonstrably needs them, and the rule for a fifth ship class is that the player can say why they would choose it over every existing one without a spreadsheet (§12). A task does not enlarge the scope; the owner does, in the GDD.

**R24 — The instrumentation log can answer §15.** The measured outcomes of v0.1 — decisions per hour and the share reversed, whether the hypothesis held, misattributions per ten hours, willing employers after two months, admirals choosing differently in identical situations — are counted from the log, not recalled. When you add an event the design measures — a decision, a hypothesis and its outcome, an accusation and its answer, an admiral's template choice and the situation it was chosen in — log it with its tick. A metric that cannot be computed from the log after the fact is a metric nobody will measure.

---

## 6. Working rules

**Stay in scope.** Do what the task asks. Adjacent code that offends you is not part of the task — note it in your report and move on. Unrequested "while I was in there" changes are the main way a young tree acquires regressions it cannot bisect.

**Keep the design record true.** `Design/GameDesign.md` is the owner's document: it records what the game *is*, and its appendix says what is settled, what play will answer and what is deferred by decision. Do not rewrite it on your own initiative. If your change reveals that the code and the GDD disagree, or that a settled item cannot be built as written, say so in your report with the section number, and the owner moves the design. Engineering decisions — a file format, a wire protocol, a subsystem the GDD leaves open, an exception to a rule here — go in `Design/ADR/` as one file per decision, numbered in order (`ADR-001-<slug>.md`), stating the context, the decision and what it forecloses, in the same commit as the change that implements it. Figures in an ADR are measured, not estimated — if you quote one, say how you measured it.

**Keep the project files honest.** Adding, removing or moving a source file means editing the owning `.vcxproj` **and** its `.filters`. A file that compiles locally but is missing from the project fails only in CI — or worse, links a stale object nobody notices. `python Build\CheckProjectFiles.py` is the cheapest way to catch a half-done move.

**What CI runs, and what it does not.** [`.github/workflows/build.yml`](.github/workflows/build.yml) has two jobs, and every step of both blocks:

| Job | Steps |
|---|---|
| **Windows** | `CheckProjectFiles.py` → build **Debug\|x64** → build the four test DLLs → `vstest.console.exe` over all four → `RunClangTidy.py` over the whole tree |
| **Linux** | `CheckFormat.py` on clang-format 18.1.3 |

**CI does not build Release** (owner decision, 2026-09-09). The Windows build is the slow half of the pipeline and a second configuration roughly doubles it for a tree where the two differ only in optimisation. What stands in for it is the static alignment check in `CheckProjectFiles.py` (§3) — and, before a release, an actual `Configuration=Release` build by whoever is shipping. If you change something that could plausibly break only under optimisation, build Release yourself and say so.

**Commits and PRs.** Branch off `main`; small, focused commits with an imperative subject describing the change, not the process. CI must be green. Never commit build output, `.vs/` or `.user` files.

---

## 7. Before you hand work back

- [ ] Naming conforms to §1 — `_` on parameters, `m_` on class state, `UPPER_CASE` constants, `PascalCase` enumerators, no `I`/`C`/`Base` affixes.
- [ ] Only the lines the task required were changed; no reformatting, no drive-by fixes.
- [ ] New, removed or moved files are in the `.vcxproj` **and** the `.filters` of every project involved.
- [ ] No project's `ConformanceMode`, `LanguageStandard`, `WarningLevel` or `TreatWarningAsError` was changed, and no warning was silenced with a pragma.
- [ ] Debug and Release still agree on everything §3 says they must.
- [ ] `python Build\CheckFormat.py`, `python Build\CheckProjectFiles.py` and `python Build\RunClangTidy.py` pass.
- [ ] It builds Debug|x64, and the four suites run and pass.
- [ ] If it touches rendering, input, audio or presentation: it was **run**, not just built.
- [ ] Nothing in `GameLogic` reads ground truth where the design says belief (R18), every consequence it emits carries its explanation (R19), and no tuning value is a literal in a resolver (R20).
- [ ] Nothing was built that GDD §15 says waits (R23); if the task needed something beyond v0.1, the report says so.
- [ ] `Design/ADR/` has a new file if the change *was* a decision, and `Design/GameDesign.md` was not edited unless the task said to.
- [ ] Your report states plainly what you verified, what you assumed, and any rule here you had to bend.
