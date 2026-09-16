# NC-001 — Solution, product projects and the executable shell

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 0 | NeuronCore, NeuronClient, NeuronServer, GameLogic, NomadCommander | L | no | no | Open |

**Depends on:** none
**Read first:** AGENTS.md §2, §3, §4 whole; R7, R9, R14, R16, R17; `.editorconfig`; `.gitignore`

## Goal

Make AGENTS.md §2 and §3 true for the five product projects: a solution that builds `x64` only, four static libraries and one executable with the compiler and linker settings AGENTS.md prescribes, the precompiled headers, `NeuronCore.h` owning the Windows macro family, and `Debug.h` with the assert every later task uses. The executable starts, does nothing and exits with code zero. No engine or game code beyond that.

## Deliverables

- `NomadCommander.slnx` at the root, listing the five projects (NC-002 adds the four test projects), with `Debug`/`Release` build types and the single platform `x64`.
- `NeuronCore/NeuronCore.vcxproj` + `.filters`, static library. Files: `pch.h`, `pch.cpp`, `NeuronCore.h` (the macro family, then `<windows.h>`), `Debug.h`, `Debug.cpp`.
- `NeuronClient/NeuronClient.vcxproj` + `.filters`, static library, references NeuronCore. Files: `pch.h`, `pch.cpp`, and one placeholder translation unit that NC-020 replaces (`Window.cpp` may be created empty here or left to NC-020; the library must contain at least one object).
- `NeuronServer/NeuronServer.vcxproj` + `.filters`, static library, references NeuronCore. Files: `pch.h`, `pch.cpp`, and the same placeholder rule.
- `GameLogic/GameLogic.vcxproj` + `.filters`, static library, references NeuronCore. Its `pch.h` includes no Windows header: the simulation is portable C++ over NeuronCore's pure headers.
- `NomadCommander/NomadCommander.vcxproj` + `.filters`, Windows application (`SubSystem` Windows, `wWinMain`), references all four libraries. Files: `pch.h`, `pch.cpp`, `Main.cpp` with a `wWinMain` that returns 0.
- A `.gitignore` entry is already present for `x64/`, `.vs/`, `*.user` and `CompiledShaders/`; verify, do not duplicate.

## Acceptance criteria

- [ ] `msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /nologo /warnaserror` and the same for `Release` both succeed from a clean tree, and every output lands in `x64\<Configuration>\` at the root (AGENTS.md §3, `$(SolutionDir)` anchoring).
- [ ] Every `.vcxproj` states, in a property group shared by both configurations: `PlatformToolset` v145, `CharacterSet` Unicode, `LanguageStandard` stdcpplatest, `ConformanceMode` true, `WarningLevel` Level4, `TreatWarningAsError` true, `FloatingPointModel` Precise, `EnableEnhancedInstructionSet` NotSet, `PrecompiledHeader` Use with `pch.h`, `MultiProcessorCompilation` true, `SDLCheck` true (R16, §3).
- [ ] No `.vcxproj` defines `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `NODRAWTEXT`, `NOGDI`, `NOBITMAP`, `NOMCX`, `NOSERVICE` or `NOHELP`; `NeuronCore.h` defines all eight before `<windows.h>` (§4).
- [ ] Debug and Release differ only in `UseDebugLibraries`, `Optimization`, `FunctionLevelLinking`, `IntrinsicFunctions`, `WholeProgramOptimization`, `LinkIncremental`, `EnableCOMDATFolding`, `OptimizeReferences`, `RuntimeLibrary` (debug vs release CRT) and the `_DEBUG`/`NDEBUG` define (§3; NC-004 will check this mechanically).
- [ ] No project lists its own directory in `AdditionalIncludeDirectories`; cross-project directories are `$(SolutionDir)<Project>` (§3).
- [ ] `NOMAD_ASSERT(expr)` breaks into the debugger with the expression, file and line in `_DEBUG` and compiles to a non-evaluating reference in `NDEBUG` without provoking C4189 on a variable used only in the assert; `NOMAD_VERIFY(expr)` always evaluates. `Debug.h` includes no Windows header (GameLogic includes it); `Debug.cpp` does.
- [ ] `x64\Debug\NomadCommander.exe` runs and exits 0.
- [ ] `NomadCommander.vcxproj` sets `DpiAwareness` to `PerMonitorHighDPIAware` so that NC-020's 1280×720 client area is 1280×720 physical pixels (R12: presented 1:1).
- [ ] `.filters` files list every file the `.vcxproj` lists, under `Source Files` / `Header Files`.

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
msbuild NomadCommander.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo /warnaserror
x64\Debug\NomadCommander.exe; $LASTEXITCODE
git status --porcelain   # nothing under x64/ or .vs/
```

## Decisions to record

None. AGENTS.md already made every decision this task implements.

## Out of scope

Test projects (NC-002), checkers (NC-003–005), shaders (NC-006), a window (NC-020), any type beyond `Debug.h`.

## Notes

- The `.slnx` format is XML: a `<Solution>` with `<Configurations>` holding `<BuildType Name="Debug"/>`, `<BuildType Name="Release"/>` and `<Platform Name="x64"/>`, then one `<Project Path="…"/>` per project. MSBuild builds it directly. Do not generate a `.sln`.
- Static libraries do not carry link dependencies. The executable and (later) the test DLLs get the libraries through `ProjectReference`; the Windows import libraries a library needs (`d3d12.lib`, `dxgi.lib`, `dxguid.lib`) are declared where they are used with `#pragma comment(lib, "…")` so that consumers do not repeat them. Nothing needs them in this task.
- `NDEBUG` form of the assert: `((void)(false && (expr)))` references the expression without evaluating it; `((void)0)` does not and trips C4189 under `/W4 /WX`.
- `Debug.cpp` reports through `OutputDebugStringW` and `__debugbreak()`. `std::format` is available under `/std:c++latest`; keep the formatting in the `.cpp`.
- Precompiled headers: each project's `pch.h` includes the standard-library headers it uses and, for NeuronClient, NeuronServer and NomadCommander, `NeuronCore.h`. Include order in every `.cpp` is `pch.h` first (§4).
- `x64` is the only platform in the solution and in every project; there is no `Win32` `ProjectConfiguration` anywhere.

## Report

_Filled in on hand-back._
