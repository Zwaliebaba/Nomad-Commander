# NC-013 — The byte writer and reader

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | M | no | no | Done (PR #3) |

**Depends on:** NC-010
**Read first:** AGENTS.md §2 (NeuronCore: "the byte reader and writer"), R13 (the store), R17; `.clang-tidy` (why `-bugprone-narrowing-conversions` is off)

## Goal

The one way bytes are written and read in this tree: for the wire (NC-015), the store (NC-031), the determinism hash (NC-043) and the tests. Bounds-checked, endian-fixed, with no format cleverness, so that a record is the size the schema says it is and a truncated buffer is an error rather than a crash.

## Deliverables

- `NeuronCore/ByteWriter.h` + `.cpp`: appends to a `std::vector<std::byte>` it owns or is lent; `Write(std::uint8_t|16|32|64)`, `Write(std::int8_t|16|32|64)`, `WriteBool`, `WriteString(std::string_view)` (length-prefixed UTF-8), `WriteBytes(std::span<const std::byte>)`, `WriteHundredths`, `WriteTick`, `WriteId`; `Size()`, `Bytes()`.
- `NeuronCore/ByteReader.h` + `.cpp`: over a `std::span<const std::byte>`; the mirror `Read...` functions returning `bool` with an out parameter (no exceptions, Roadmap *Conventions*); `Remaining()`, `Failed()` sticky after the first short read; `Skip(n)`.
- A `Serializable` concept: `T::Serialize(ByteWriter&) const` and `static bool T::Deserialize(ByteReader&, T&)`, used by every record from here on.
- `Random::WriteState`/`ReadState` (NC-011) implemented on top.
- `NeuronCoreTests/ByteStreamTests.cpp`.

## Acceptance criteria

- [x] Round trip of every type, including the extremes, and of a string with a multi-byte UTF-8 character.
- [x] Reading past the end sets `Failed()`, returns `false`, and never touches memory outside the span (test with a one-byte buffer and every reader).
- [x] Byte layout is little-endian and independent of the host: the test asserts the exact bytes of `Write(std::uint32_t{0x01020304})`.
- [x] A 100,000-integer write and read completes in the time budget the report states (measured, not guessed).

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronCoreTests.dll /Platform:x64
```

## Decisions to record

**ADR — byte encoding and versioning.** Little-endian fixed width; lengths are `std::uint32_t`; no varints; every top-level record (a message, a store) begins with a `std::uint16_t` schema version and a reader rejects a version it does not know. What it forecloses: compact encodings, and reading a store from a newer build.

## Out of scope

Compression; a schema language; reflection.

## Notes

- `std::byte` throughout; `char` buffers are for strings only (R17 makes literal handling strict; keep string views `const`).
- `std::bit_cast` and `std::endian` are available; the writer should not assume `std::endian::native` is little, even though x64 is.

## Report

**Verified here (Linux):** the real `ByteWriter.cpp`, `ByteReader.cpp` and `Random.cpp` were compiled under GCC (`-std=c++23`) and Clang (`-std=c++2c`) with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`, at `-O0` with `_DEBUG` and `-O2` with `NDEBUG`, against a driver mirroring every `TEST_METHOD`; all four configurations pass. The exact bytes of `Write(std::uint32_t{0x01020304})` are `04 03 02 01`. Every reader on a one-byte buffer fails, leaves its out parameter alone and sets the sticky flag, and a length of `0xFFFFFFFF` with nothing behind it is refused before anything is allocated. Every prefix of a serialized record fails to deserialize with the out parameter untouched. A hundred thousand `std::uint32_t` round-trip in 11.34 ms (GCC, `-O0`) down to 0.74 ms (Clang, `-O2`); the table is in ADR-004. clang-tidy 22.1.8 with the repository's configuration is clean on all three translation units and the headers; `CheckFormat.py` and `CheckProjectFiles.py` pass. **Verified by CI, not here:** the MSVC build and the tests under vstest.

**Assumed:** nothing.

**Refined:** the task's time budget is stated as a measurement in ADR-004 rather than asserted in the test, because a wall-clock bound on a shared CI runner is a flake, and AGENTS.md does not let a flaky test be excused later; the test asserts the round trip's correctness and the size of the buffer. `Serializable` lives in its own header (it needs both streams, and neither stream should include the other). `Reserve` joins the writer, since the 100,000-value case shows what a missing reservation costs. `Position()` joins `Remaining()`, for a reader that needs to record where a sub-record began. The writer deletes its copy and move operations: it may point at a buffer it was lent, and a moved writer that pointed at its own member would point at the wrong one. `Random::ReadState` rejects an even increment, which is a corrupt store rather than a usable generator.

**Bent:** one task per PR, as for NC-012: NC-012 through NC-020 were asked for as a batch on one branch.
