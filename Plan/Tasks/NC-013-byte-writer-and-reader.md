# NC-013 — The byte writer and reader

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | M | no | no | Open |

**Depends on:** NC-010
**Read first:** AGENTS.md §2 (NeuronCore: "the byte reader and writer"), R13 (the store), R17; `.clang-tidy` (why `-bugprone-narrowing-conversions` is off)

## Goal

The one way bytes are written and read in this tree: for the wire (NC-015), the store (NC-031), the determinism hash (NC-043) and the tests. Bounds-checked, endian-fixed, with no format cleverness, so that a record is the size the schema says it is and a truncated buffer is an error rather than a crash.

## Deliverables

- `NeuronCore/ByteWriter.h` + `.cpp`: appends to a `std::vector<std::byte>` it owns or is lent; `Write(std::uint8_t|16|32|64)`, `Write(std::int8_t|16|32|64)`, `WriteBool`, `WriteString(std::string_view)` (length-prefixed UTF-8), `WriteBytes(std::span<const std::byte>)`, `WriteHundredths`, `WriteTick`, `WriteId`; `Size()`, `Bytes()`.
- `NeuronCore/ByteReader.h` + `.cpp`: over a `std::span<const std::byte>`; the mirror `Read…` functions returning `bool` with an out parameter (no exceptions, Roadmap *Conventions*); `Remaining()`, `Failed()` sticky after the first short read; `Skip(n)`.
- A `Serializable` concept: `T::Serialize(ByteWriter&) const` and `static bool T::Deserialize(ByteReader&, T&)`, used by every record from here on.
- `Random::WriteState`/`ReadState` (NC-011) implemented on top.
- `NeuronCoreTests/ByteStreamTests.cpp`.

## Acceptance criteria

- [ ] Round trip of every type, including the extremes, and of a string with a multi-byte UTF-8 character.
- [ ] Reading past the end sets `Failed()`, returns `false`, and never touches memory outside the span (test with a one-byte buffer and every reader).
- [ ] Byte layout is little-endian and independent of the host: the test asserts the exact bytes of `Write(std::uint32_t{0x01020304})`.
- [ ] A 100,000-integer write and read completes in the time budget the report states (measured, not guessed).

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

_Filled in on hand-back._
