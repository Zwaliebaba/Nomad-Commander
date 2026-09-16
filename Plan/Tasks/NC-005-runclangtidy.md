# NC-005 — `Build/RunClangTidy.py`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 0 | Build | M | no | no | Open |

**Depends on:** NC-002
**Read first:** `.clang-tidy` whole (the driver-mode command in its header, the pin, `HeaderFilterRegex`); AGENTS.md §1 *Enforcement*, §3 (*Run the checkers*), §6 (the CI table); `build.yml` (*Import the MSVC environment*, *Install the pinned clang-tidy*, *Run clang-tidy*)

## Goal

Run clang-tidy over every hand-written translation unit in the tree through clang's MSVC driver with the same switches the `.vcxproj` files set, in parallel, and fail on any finding. It is the last step of the Windows CI job and it must be runnable by a developer from a Developer PowerShell with the pip-installed binary CI pins.

## Deliverables

- `Build/RunClangTidy.py`: options `--clang-tidy <binary>` (default `clang-tidy` on the path), `--jobs <n>` (default: CPU count), `--files <paths…>` (default: every `.cpp` under the five projects and `Tests/*/`, skipping `pch.cpp` and anything under `CompiledShaders/` or `x64/`), `--verbose`.
- Per file, the invocation from `.clang-tidy`'s header, extended with the project's include directories read from its `.vcxproj` (so the script and the build cannot disagree): `--driver-mode=cl /std:c++latest /EHsc /permissive- /W4 /DUNICODE /D_UNICODE /D_DEBUG /I<dir>…`.
- A check at start that `INCLUDE` is set (the Windows SDK is invisible to clang without it), with a message naming the Developer PowerShell; and a print of the clang-tidy version, with a warning if it is not the pinned one.

## Acceptance criteria

- [ ] Exit 0 on the Phase-0 tree with clang-tidy 22.1.8 from pip.
- [ ] A translation unit with a parameter missing its `_` is reported and the exit code is 1.
- [ ] Findings from headers outside the tree (SDK, CRT) are not reported; `HeaderFilterRegex` from `.clang-tidy` is in force because the script does not override it.
- [ ] The per-file switch list is derived from the `.vcxproj`, not typed into the script a second time, except the defines `.clang-tidy`'s header names.
- [ ] `build.yml`'s *Run clang-tidy* step passes as written.

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

_Filled in on hand-back._
