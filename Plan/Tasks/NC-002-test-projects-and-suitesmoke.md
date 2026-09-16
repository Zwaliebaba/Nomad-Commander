# NC-002 — The four test projects and `SuiteSmoke`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 0 | Tests/NeuronCoreTests, Tests/NeuronClientTests, Tests/NeuronServerTests, Tests/GameLogicTests | M | no | no | Done (PR #1) |

**Depends on:** NC-001
**Read first:** AGENTS.md §2 (the test rows and the project graph), §3 (*Run the tests*, *vstest reports "no tests found" as a pass*), R9, R10; `.clang-format` (the `Macros` block)

## Goal

Four MSVC CppUnitTest DLLs, one per library, each referencing the library it tests and the libraries that library is built on, each holding the placeholder `SuiteSmoke` test that stands guard until a real test lands, all four found and run by `vstest.console.exe` exactly as `.github/workflows/build.yml` runs them.

## Deliverables

- `Tests/NeuronCoreTests/NeuronCoreTests.vcxproj` + `.filters`, `pch.h`, `pch.cpp`, `SuiteSmoke.cpp`. References NeuronCore.
- `Tests/NeuronClientTests/…` references NeuronClient and NeuronCore.
- `Tests/NeuronServerTests/…` references NeuronServer and NeuronCore.
- `Tests/GameLogicTests/…` references GameLogic and NeuronCore.
- The four projects added to `NomadCommander.slnx` under a `Tests` solution folder.
- Each `SuiteSmoke.cpp`: `namespace <Project>Tests`, `TEST_CLASS(SuiteSmoke)` with one `TEST_METHOD(SuiteIsDiscovered)` asserting true, and a comment saying it is deleted by the first real test (AGENTS.md §3).

## Acceptance criteria

- [ ] All four DLLs build in Debug and Release with the same shared settings as NC-001's projects (they are checked by NC-004 too) and land in `x64\<Configuration>\`.
- [x] `vstest.console.exe` over the four DLLs reports four tests run, four passed, zero skipped.
- [x] Each test project's `AdditionalIncludeDirectories` lists `$(VCInstallDir)Auxiliary\VS\UnitTest\include` and the directories of the libraries it references, and nothing else; `AdditionalLibraryDirectories` lists `$(VCInstallDir)Auxiliary\VS\UnitTest\lib`. (Microsoft Learn places the framework under `VC\Auxiliary\VS\UnitTest`; the plan's first draft had the pre-2017 path.)
- [x] `using namespace Microsoft::VisualStudio::CppUnitTestFramework;` appears only in `.cpp` files (R10's one permitted case).
- [x] The workflow's *Run the tests* step passes as written; no edit to `build.yml` was needed.

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
vstest.console.exe x64\Debug\NeuronCoreTests.dll x64\Debug\NeuronClientTests.dll `
                   x64\Debug\NeuronServerTests.dll x64\Debug\GameLogicTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Any real test. Test helpers (a fake `Simulation`, a WARP device) come with the task that first needs them.

## Notes

- A native unit test project sets `<ProjectSubType>NativeUnitTestProject</ProjectSubType>`, `ConfigurationType` DynamicLibrary, and `UseOfMfc` false. The framework header is `CppUnitTest.h`; the import library is `Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib`, found through the `Auxiliary\VS\UnitTest\lib` directory and pulled in by the header itself.
- The test DLL links the tested static library and its dependencies through `ProjectReference` with `LinkLibraryDependencies` true; nothing is listed by file name.
- `.clang-format` already knows the `TEST_CLASS`/`TEST_METHOD` macros; format the file and confirm it survives a round trip.

## Report

**Verified here (Linux, no MSVC):** the four `SuiteSmoke.cpp` and `pch.*` files pass `clang-format-18 --dry-run --Werror` (the `TEST_CLASS`/`TEST_METHOD` macro definitions in `.clang-format` do their job); the four projects were generated from the same spec as NC-001's, so every shared setting is identical across the nine; `NomadCommander.slnx` gains a `/Tests/` folder with the four. **Verified by CI, not here:** the build of the four DLLs, and `vstest` finding and passing four tests.

**Assumed:** `CppUnitTest.h` pulls its import library in through a `#pragma comment(lib, …)`, as every wizard-generated test project relies on (none lists it under `AdditionalDependencies`).

**Refined:** the framework paths are `$(VCInstallDir)Auxiliary\VS\UnitTest\include` and `\lib`, per Microsoft Learn; the task's draft named the older location. Each test `pch.h` includes `NeuronCore.h` before `CppUnitTest.h` so the Windows macro family is set before anything from Windows is pulled in.

**Bent:** nothing.

**CI:** [run 4](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35095588076) on head `d5fa1c3` is green: `CheckProjectFiles.py` clean, the solution built Debug|x64 with `/warnaserror`, vstest ran 5 tests and passed all, `RunClangTidy.py` reported nine translation units clean on clang-tidy 22.1.8, and the Linux `format` job passed. Run 3 on the previous head failed on one clang-tidy finding in `Debug.h` (`bugprone-reserved-identifier` on the parameter names of the function-pointer alias), fixed in `d5fa1c3`. Not verified by CI, by its design: the Release build. Not verified by anyone yet: running the executable. The count is five rather than four because NC-006 replaced NeuronClientTests' `SuiteSmoke` with two real tests before the first run. The Debug half of the first criterion holds; Release waits.
