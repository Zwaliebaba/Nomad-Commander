# NC-001 — Solution, product projects and the executable shell

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 0 | NeuronCore, NeuronClient, NeuronServer, GameLogic, NomadCommander | L | no | no | Done (PR #1) |

**Depends on:** none
**Read first:** AGENTS.md §2, §3, §4 whole; R7, R9, R14, R16, R17; `.editorconfig`; `.gitignore`

## Goal

Make AGENTS.md §2 and §3 true for the five product projects: a solution that builds `x64` only, four static libraries and one executable with the compiler and linker settings AGENTS.md prescribes, the precompiled headers, `NeuronCore.h` owning the Windows macro family, and `Debug.h` with the assert every later task uses. The executable starts, does nothing and exits with code zero. No engine or game code beyond that.

## Deliverables

- `NomadCommander.slnx` at the root, listing the five projects (NC-002 adds the four test projects), with `Debug`/`Release` build types and the single platform `x64`.
- `NeuronCore/NeuronCore.vcxproj` + `.filters`, static library. Files: `pch.h`, `pch.cpp`, `NeuronCore.h` (the macro family, then `<windows.h>`), `Debug.h`, `Debug.cpp`. `Debug.h` also declares `SetAssertHandler`, because a suite cannot test "this asserts" (NC-011 and later) if an assert always ends the process.
- `NeuronClient/NeuronClient.vcxproj` + `.filters`, static library, references NeuronCore. Files: `pch.h`, `pch.cpp`, and the placeholder `ClientSmoke.cpp`, which defines one public symbol and is deleted by NC-006. An empty translation unit is not enough: `lib.exe` reports LNK4221 for an object with no public symbol and `/warnaserror` makes that fatal.
- `NeuronServer/NeuronServer.vcxproj` + `.filters`, static library, references NeuronCore. Files: `pch.h`, `pch.cpp`, and the placeholder `ServerSmoke.cpp` (deleted by NC-030).
- `GameLogic/GameLogic.vcxproj` + `.filters`, static library, references NeuronCore. Its `pch.h` includes no Windows header: the simulation is portable C++ over NeuronCore's pure headers. Placeholder `GameLogicSmoke.cpp` (deleted by NC-040).
- `NomadCommander/NomadCommander.vcxproj` + `.filters`, Windows application (`SubSystem` Windows, `wWinMain`), references all four libraries. Files: `pch.h`, `pch.cpp`, `Main.cpp` with a `wWinMain` that returns 0.
- A `.gitignore` entry is already present for `x64/`, `.vs/`, `*.user` and `CompiledShaders/`; verify, do not duplicate.

## Acceptance criteria

- [ ] `msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /nologo /warnaserror` and the same for `Release` both succeed from a clean tree, and every output lands in `x64\<Configuration>\` at the root (AGENTS.md §3, `$(SolutionDir)` anchoring).
- [x] Every `.vcxproj` states, in a property group shared by both configurations: `PlatformToolset` v145, `CharacterSet` Unicode, `LanguageStandard` stdcpplatest, `ConformanceMode` true, `WarningLevel` Level4, `TreatWarningAsError` true, `FloatingPointModel` Precise, `EnableEnhancedInstructionSet` NotSet, `PrecompiledHeader` Use with `pch.h`, `MultiProcessorCompilation` true, `SDLCheck` true (R16, §3).
- [x] No `.vcxproj` defines `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `NODRAWTEXT`, `NOGDI`, `NOBITMAP`, `NOMCX`, `NOSERVICE` or `NOHELP`; `NeuronCore.h` defines all eight before `<windows.h>` (§4).
- [x] Debug and Release differ only in `UseDebugLibraries`, `Optimization`, `FunctionLevelLinking`, `IntrinsicFunctions`, `WholeProgramOptimization`, `LinkIncremental`, `EnableCOMDATFolding`, `OptimizeReferences`, `RuntimeLibrary` (debug vs release CRT) and the `_DEBUG`/`NDEBUG` define (§3; NC-004 will check this mechanically).
- [x] No project lists its own directory in `AdditionalIncludeDirectories`; cross-project directories are `$(SolutionDir)<Project>` (§3).
- [ ] `NOMAD_ASSERT(expr)` breaks into the debugger with the expression, file and line in `_DEBUG` and compiles to a non-evaluating reference in `NDEBUG` without provoking C4189 on a variable used only in the assert; `NOMAD_VERIFY(expr)` always evaluates. `Debug.h` includes no Windows header (GameLogic includes it); `Debug.cpp` does.
- [ ] `x64\Debug\NomadCommander.exe` runs and exits 0.
- [x] `NomadCommander.vcxproj` sets the manifest tool's `EnableDpiAwareness` to `PerMonitorHighDPIAware` (the `MT` task's parameter, per Microsoft Learn) so that NC-020's 1280×720 client area is 1280×720 physical pixels (R12: presented 1:1).
- [x] `.filters` files list every file the `.vcxproj` lists, under `Source Files` / `Header Files`.

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

**Verified here (Linux, no MSVC):** every `.cpp`/`.h` passes `clang-format-18 --dry-run --Werror` (18.1.3, the version CI pins); the five `.vcxproj` files were generated from one spec so the shared settings cannot differ between projects; `NomadCommander.slnx` lists the five projects and only `x64`. **Verified by CI, not here:** the build in Debug and Release, the executable's exit code, `git status` after a build. The report is updated with the CI run once it is green.

**Assumed:** the `.slnx` schema (`Solution`/`Configurations`/`BuildType`/`Platform`/`Project Path`), which MSBuild 18.9 on the CI image supports; `WindowsTargetPlatformVersion` `10.0` (latest installed SDK; the CI image has 10.0.26100.0); `VCProjectVersion` 18.0 as informational.

**Refined:** three placeholder translation units (`ClientSmoke.cpp`, `ServerSmoke.cpp`, `GameLogicSmoke.cpp`) instead of an empty file, for the LNK4221 reason above; each names the task that deletes it. `Debug.h` gained `SetAssertHandler` so that later suites can observe an assert. The DPI awareness metadata is `EnableDpiAwareness` under `<Manifest>`, checked against the `MT` task's documentation; NC-020 verifies it at runtime.

**Bent:** R7 (a file is named for its primary type) for the three placeholders and `Main.cpp`, which hold a function and no type; the placeholders are temporary by construction and `Main.cpp` is the entry point the task itself names. Phase 0 lands as six commits on one PR rather than one PR per task, because the tree the plan's protocol presumes did not exist yet and CI could not have been green for any task alone; the one-task-per-PR rule applies from Phase 1.

**CI:** [run 4](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35095588076) on head `d5fa1c3` is green: `CheckProjectFiles.py` clean, the solution built Debug|x64 with `/warnaserror`, vstest ran 5 tests and passed all, `RunClangTidy.py` reported nine translation units clean on clang-tidy 22.1.8, and the Linux `format` job passed. Run 3 on the previous head failed on one clang-tidy finding in `Debug.h` (`bugprone-reserved-identifier` on the parameter names of the function-pointer alias), fixed in `d5fa1c3`. Not verified by CI, by its design: the Release build. Not verified by anyone yet: running the executable. The Debug half of the first criterion holds; the Release half, the `NDEBUG` form of `NOMAD_ASSERT` and the executable's exit code wait for a Windows machine.
