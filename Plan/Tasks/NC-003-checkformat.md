# NC-003 — `Build/CheckFormat.py`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 0 | Build | S | no | no | Done (PR #1) |

**Depends on:** NC-001
**Read first:** AGENTS.md §3 (*Run the checkers*), §4, §6 (the CI table); `.clang-format` whole; `.github/workflows/build.yml` (the `format` job); `.editorconfig` (`[*.py]`)

## Goal

The whole-tree format gate: run clang-format over every hand-written C++ file, report the ones that would change, exit non-zero if any would, and rewrite them on `--fix`. It is what the Linux CI job runs, so it must run on Python 3 with no dependency beyond the standard library and a `clang-format` binary.

## Deliverables

- `Build/CheckFormat.py`: options `--clang-format <binary>` (default `clang-format`), `--fix`, and `--verbose`. Walks `NeuronCore/`, `NeuronClient/`, `NeuronServer/`, `GameLogic/`, `NomadCommander/`, `Tests/*/`; includes `*.cpp` and `*.h`; skips `CompiledShaders/`, `x64/`, `.vs/`, and `Resource.h`.
- A header comment saying what CI pins (clang-format 18.1.3) and that the script prints the version it used and warns when it is not 18.

## Acceptance criteria

- [ ] On the tree as NC-001 and NC-002 left it, the script exits 0 and prints the clang-format version.
- [ ] With one file deliberately misformatted, it exits 1 and names that file and only that file; `--fix` rewrites it and a second run exits 0.
- [ ] It never rewrites without `--fix`, and it never reorders includes (`SortIncludes: Never` is honoured because the style file is used, not overridden).
- [ ] Line endings survive: a CRLF file stays CRLF after `--fix` on Windows and after a check on Linux (`.clang-format` leaves `LineEnding` at its default for this reason).
- [ ] The `format` job in `build.yml` passes as written: `python3 Build/CheckFormat.py --clang-format clang-format-18`.

## Verification

```bash
python3 Build/CheckFormat.py --clang-format clang-format-18          # Linux
```
```powershell
python Build\CheckFormat.py                                          # Windows: prints the version and a warning if it is not 18
```

## Decisions to record

None.

## Out of scope

Formatting anything other than C++ (`.editorconfig` covers the rest); a pre-commit hook; running clang-tidy.

## Notes

- Use `clang-format --dry-run --Werror <file>` per file, or pipe and compare output to the file's bytes; the second is what makes the CRLF criterion checkable.
- Python files are two-space indented and LF (`.editorconfig`).

## Report

**Verified here, on clang-format 18.1.3 (the pinned version, from Ubuntu's package):** a clean tree exits 0 and prints the version; a deliberately misformatted `NeuronCore/Scratch.cpp` exits 1 and is the only file named; `--fix` rewrites it and a second run exits 0; a CRLF file stays CRLF through `--fix` (checked with `od -c`); the version warning path prints when the major is not 18. Comparison is byte for byte against `clang-format --style=file <path>` output, so line endings are part of the check rather than an assumption. **Verified by CI:** the `format` job as written.

**Assumed:** nothing beyond the standard library and the binary.

**Refined:** nothing in the task's scope; the implementation runs files in a thread pool, which the task did not ask for and which costs nothing.

**Bent:** nothing.
