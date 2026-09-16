# ADR-004 — Byte encoding and versioning

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-013
**Cites:** AGENTS.md §2 (NeuronCore holds the byte reader and writer), R13 (the universe store), R16 (replay), R17; GDD §1, §7 (the universe is reloaded from its store)

## Context

Three things in this tree turn state into bytes and back: the wire between the client and the host (NC-015), the universe store a universe is reloaded from (NC-031), and the determinism hash that proves a replay matched (NC-043). They can share one encoding or grow three, and three is how a store comes to disagree with the wire about what a `Tick` is. What had to be decided: the byte order, the widths, how a length is carried, how a failure is reported, and how a reader knows it is looking at a record it understands.

`/permissive-` and `/WX` are in force, exceptions are not used across an engine API (`Plan/Roadmap.md`), and the store is written by a host that may be killed at any moment, so a truncated or corrupt record is an ordinary case rather than an exceptional one.

## Decision

1. **Little-endian, fixed width, written byte by byte.** Every integer is written least significant byte first by shifting, not by copying the object's representation, so the encoding does not change if the tree is ever built for a big-endian target. `std::uint8_t` through `std::uint64_t` and their signed counterparts, each exactly its own width. A signed value is written as its two's-complement bits, because `INT_MIN` has no positive magnitude to take.
2. **A `bool` is one byte, 0 or 1.** It is written through `WriteBool` rather than as an integer, so a call site cannot silently widen a flag into four bytes.
3. **A string is a `std::uint32_t` length and that many bytes of UTF-8.** The bytes are not inspected or validated: what goes in comes out. A reader checks the length against what remains **before** allocating, so a corrupt length cannot ask for a gigabyte.
4. **No varints, no compression, no framing inside the encoding.** A record is the size the schema says it is. Framing is `Protocol`'s (NC-015), and the store's own structure is `UniverseStore`'s (NC-031).
5. **Reads return `bool` and the failure is sticky.** `ByteReader::Read*` returns false and leaves its out parameter untouched; after the first short read `Failed()` is true and every later read returns false without touching the buffer. A record is therefore decoded as a chain of `&&` with one test at the end, rather than a check after every field. Nothing reads past the span, whatever a length in the bytes claims.
6. **Every top-level record begins with a `std::uint16_t` schema version**, and a reader rejects a version it does not know. "Top-level" means a stored universe (NC-031) and a protocol message (NC-015), not every nested record: a `Fleet` inside a `World` inside a store is covered by the store's version.
7. **`Serializable` is a concept, not a base class**: `value.Serialize(ByteWriter&)` and `static bool T::Deserialize(ByteReader&, T&)`. Nothing inherits anything, and a record that forgets one half fails to compile where it is used.

## What this forecloses

- Reading a store or a message written by a build whose schema version differs, which is the point: a silent misread is worse than a refusal (R16 — a replay that diverges has no line to blame).
- Compact encodings. A `std::uint64_t` tick costs eight bytes even when it is small, and a journal of a million inputs pays for that. If a measured store ever makes this matter, the answer is a new ADR and a version bump, not a quiet change.
- Hand-rolled serialization in any subsystem: a type that wants to cross the wire implements the concept.
- Endianness or width changing with the target.

## Consequences

- `Random`'s state crosses through the same streams (`WriteState`/`ReadState`), and an even increment is rejected on read, because the generator's period depends on it being odd (ADR-002).
- A truncated record is a first-class test: `ByteStreamTests` deserializes every prefix of a record and requires each to fail with the out parameter untouched.
- The wire schema of the game (`GameLogic/Wire*.h`, ADR-001) is written in these primitives and inherits this decision without restating it.

## Measurements

One hundred thousand `std::uint32_t` values written and read back, on this session's Linux container (the CI runner and a developer machine will differ; the figure is a floor for "fast enough", not a budget):

| Compiler | Configuration | Time |
|---|---|---|
| GCC 13 | `-D_DEBUG -O0` | 11.34 ms |
| GCC 13 | `-DNDEBUG -O2` | 1.03 ms |
| Clang 18 | `-D_DEBUG -O0` | 6.47 ms |
| Clang 18 | `-DNDEBUG -O2` | 0.74 ms |

A universe store's journal is tens of thousands of small records rather than millions, so the byte-by-byte shift loop is not the cost that will matter first; NC-031 measures the load that decides whether a snapshot section is needed.
