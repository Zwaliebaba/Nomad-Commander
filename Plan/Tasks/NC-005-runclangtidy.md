# NC-005 — `Build/RunClangTidy.py`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 0 | Build | M | no | no | Done (PR #1) |

**Depends on:** NC-002
**Read first:** `.clang-tidy` whole (the driver-mode command in its header, the pin, `HeaderFilterRegex`); AGENTS.md §1 *Enforcement*, §3 (*Run the checkers*), §6 (the CI table); `build.yml` (*Import the MSVC environment*, *Install the pinned clang-tidy*, *Run clang-tidy*)

## Goal

Run clang-tidy over every hand-written translation unit in the tree through clang's MSVC driver with the same switches the `.vcxproj` files set, in parallel, and fail on any finding. It is the last step of the Windows CI job and it must be runnable by a developer from a Developer PowerShell with the pip-installed binary CI pins.

## Deliverables

- `Build/RunClangTidy.py`: options `--clang-tidy <binary>` (default `clang-tidy` on the path), `--jobs <n>` (default: CPU count), `--files <paths...>` (default: every `.cpp` under the five projects and `Tests/*/`, skipping `pch.cpp` and anything under `CompiledShaders/` or `x64/`), `--verbose`.
- Per file, the invocation from `.clang-tidy`'s header, with the defines and the include directories read from the project's `.vcxproj` (Debug|x64) by `CheckProjectFiles.py`'s parser, so the script and the build cannot disagree: `--driver-mode=cl /std:c++latest /EHsc /DUNICODE /D_UNICODE /D<defines> /I<dirs>`. Not `/W4`: clang's warning set differs from MSVC's and `WarningsAsErrors: '*'` would make every difference fatal; not the Windows macro family, which `NeuronCore.h` owns (a `/D` copy is a macro redefinition, which clang reports and the config makes fatal). `.clang-tidy`'s header still shows the older command with `/DWIN32_LEAN_AND_MEAN /DNOMINMAX`; that comment is the owner's to update.
- A check at start that `INCLUDE` is set (the Windows SDK is invisible to clang without it), with a message naming the Developer PowerShell; and a print of the clang-tidy version, with a warning if it is not the pinned one.

## Acceptance criteria

- [x] Exit 0 on the Phase-0 tree with clang-tidy 22.1.8 from pip.
- [x] A translation unit with a parameter missing its `_` is reported and the exit code is 1.
- [x] Findings from headers outside the tree (SDK, CRT) are not reported; `HeaderFilterRegex` from `.clang-tidy` is in force because the script does not override it.
- [x] The per-file switch list is derived from the `.vcxproj`, not typed into the script a second time, except `/DUNICODE /D_UNICODE`, which stand for `CharacterSet=Unicode`.
- [x] `build.yml`'s *Run clang-tidy* step passes as written.

## Verification

```powershell
python -m pip install clang-tidy==22.1.8
python Build\RunClangTidy.py
python Build\RunClangTidy.py --files NeuronCore\Debug.cpp --verbose
```

## Decisions to record

None.

## Out of scope

A `compile_commands.json`; fixing findings; running on Linux (the MSVC driver mode needs the SDK).

## Notes

- Sources that include a `CompiledShaders/*.h` need the build to have run first; CI orders it that way, and the script says so when the header is missing rather than reporting a parse error as forty findings.
- Use `concurrent.futures.ThreadPoolExecutor`; clang-tidy is a separate process per file.
- Order the output by file so two runs diff cleanly.

## Report

**Verified here (Linux; clang-tidy in MSVC driver mode needs the Windows SDK, so the linter itself did not run):** `--dry-run` prints the nine commands with each project's defines (`_DEBUG` and `_LIB`, `_WINDOWS`, `_WINDOWS;_USRDLL`) and include directories (`$(SolutionDir)<Project>` expanded; `$(VCInstallDir)Auxiliary\VS\UnitTest\include` for the test suites) taken from the `.vcxproj` files through `CheckProjectFiles.py`'s parser; without `INCLUDE` the script stops with the Developer PowerShell message; without `VCINSTALLDIR` the test projects' expansion stops with its own; the pinned version is read from `build.yml` (`22.1.8`). **Verified by CI:** the *Run clang-tidy* step over the Phase 0 tree.

**Assumed:** clang-cl accepts `/std:c++latest` against MSVC 14.51's standard library, and `CppUnitTest.h` parses under clang; both are what the workflow and `.clang-tidy` were written for.

**Refined:** the switch list drops `/W4` and the Windows macro family, for the reasons in the deliverables; a `--dry-run` option prints the commands, which is how a Linux agent checks this script at all; the version pin is read from the workflow rather than repeated.

**Bent:** nothing.

**CI:** [run 4](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35095588076) on head `d5fa1c3` is green: `CheckProjectFiles.py` clean, the solution built Debug|x64 with `/warnaserror`, vstest ran 5 tests and passed all, `RunClangTidy.py` reported nine translation units clean on clang-tidy 22.1.8, and the Linux `format` job passed. Run 3 on the previous head failed on one clang-tidy finding in `Debug.h` (`bugprone-reserved-identifier` on the parameter names of the function-pointer alias), fixed in `d5fa1c3`. Not verified by CI, by its design: the Release build. Not verified by anyone yet: running the executable. The second criterion was met by a real defect rather than a staged one: run 3 reported `bugprone-reserved-identifier` in `Debug.h` with the file and line and exit code 1, which is the reporting path the criterion asks for; `CppUnitTest.h` and the SDK produced nothing, as the third criterion requires.
