# NC-015 — `Protocol` and the in-process transport

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | M | no | **yes** | Done (PR #3) |

**Depends on:** NC-013
**Read first:** GDD §4 (the AI and the client see through the same fog), §9 (reality, belief, evidence distinct), §14 (the hosting model is deferred); AGENTS.md §2 (the wire protocol, `GameLogic` is host-side), R2 (the `Transport` illustration), R18, R23; `Plan/Roadmap.md` A2

## Goal

The message envelope the client and the host exchange, and the transport that carries it inside one process in v0.1. The client receives bytes the host chose to send and nothing else; that is how R18 becomes a structure rather than a rule. The transport is an interface with one implementation now (in memory) so that the always-on host later is a second implementation and not a rewrite.

## Deliverables

- `NeuronCore/Protocol.h` + `.cpp`: `MessageHeader { std::uint16_t version; std::uint8_t channel; std::uint32_t payloadBytes; }` (R8: a public aggregate), `enum class Channel : std::uint8_t { SessionControl, SimulationInput, SimulationOutput }`, `Protocol::Frame(channel, payload, ByteWriter&)`, `Protocol::Unframe(ByteReader&, MessageHeader&, std::span<const std::byte>&)` returning `bool`, and `PROTOCOL_VERSION`.
- `NeuronCore/Transport.h`: `class Transport` with `Send(Channel, std::span<const std::byte>)` and `Receive(std::vector<std::byte>&)` returning `bool` (one whole message or nothing), plus `Connected()`.
- `NeuronCore/MemoryTransport.h` + `.cpp`: a pair of endpoints over two in-process queues with `MemoryTransport::CreatePair(client, host)`; a message sent on one is received whole on the other, in order. Single-threaded; if the client and host ever run on different threads, the ADR below is superseded.
- `NeuronCoreTests/ProtocolTests.cpp`, `MemoryTransportTests.cpp`.

## Acceptance criteria

- [x] Frame then unframe returns the same channel and payload bytes for a zero-length and a 1 MiB payload.
- [x] A header with an unknown version, or a payload length beyond the buffer, is rejected without reading past the end.
- [x] Ten thousand messages sent on one endpoint are received on the other in order, and `Receive` on an empty queue returns `false` without blocking.
- [x] Nothing in the three headers names a game type or includes anything outside NeuronCore.

## Verification

```powershell
vstest.console.exe x64\Debug\NeuronCoreTests.dll /Platform:x64
```

## Decisions to record

**ADR — the client–host transport in v0.1** (owner-visible). Recommendation (Roadmap A2): serialized `Protocol` messages over `MemoryTransport` in one process; `Socket` and `FrameStream` (AGENTS.md §2) are deferred to the always-on host; the `Transport` interface is the seam a socket implementation fills later. What it forecloses: nothing on the wire changes when the socket arrives, so message design must not assume shared memory (no pointers, no ids that only mean something in one process).

## Out of scope

Sockets, threads, reconnection, encryption, compression.

## Notes

- `SessionControl` carries what is the host's and not the simulation's: clock rate, skip, save now. `SimulationInput` and `SimulationOutput` carry `Simulation` bytes verbatim (NC-014). The game's schema for those bytes is `GameLogic/Wire*.h` (NC-042); this task never sees it.

## Report

**Verified here (Linux):** `Protocol.cpp`, `MemoryTransport.cpp` and the byte streams were compiled under GCC (`-std=c++23`) and Clang (`-std=c++2c`) with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`, in `_DEBUG` and `NDEBUG`, against a driver mirroring every `TEST_METHOD`; all four pass. A zero-length and a one-mebibyte payload round-trip with their channel; three messages back to back unframe in order; an unknown version, an unknown channel value, a length beyond the buffer and a length beyond the limit are each refused, and every prefix of a framed message is refused. Ten thousand messages sent on one endpoint arrive whole and in order on the other, `Receive` on an empty queue returns false without touching the caller's buffer, and disconnecting one end disconnects both and drops what was queued. clang-tidy 22.1.8 with the repository's configuration is clean, including the `EnumCastOutOfRange` path the workflow's comments warn about: the channel value is checked against the known channels before it becomes one. `grep` finds no game vocabulary in the four headers, and they include only NeuronCore. **Verified by CI, not here:** the MSVC build and the tests under vstest.

**Assumed:** nothing. The transport is now a decision rather than an assumption (ADR-006 supersedes `Plan/Roadmap.md` A2).

**Refined:** `Transport::Receive` hands back one whole *framed* message and the caller splits it with `Protocol::Unframe`, rather than the transport unframing on the receiver's behalf: that is the contract a socket implementation can keep, and it keeps `Transport` ignorant of the envelope's shape. `MAX_PAYLOAD_BYTES` (64 MiB) and `HEADER_BYTES` are named constants, so a corrupt length is refused before it becomes an allocation. `MemoryTransport` gained `Disconnect()` and `PendingCount()`: the first is what a socket's close will be, and the second is what a test and the debug overlay (NC-070) ask. The pairing holds the peer's queue through a `std::weak_ptr`, so one endpoint's disconnection or destruction leaves the other correctly unconnected rather than sending into a queue nobody reads.

**Bent:** one task per PR (the batch NC-012 to NC-020 on one branch), and `Plan/README.md`'s rule that an owner-visible task lands alone and first. ADR-006 and ADR-005 are the two decisions in this PR most worth a careful read.

**After CI, from NC-020's clang-tidy round.** `RunClangTidy.py` had never run in this PR before [run 18](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35110234040) — `vstest` aborted the job ahead of it every time — and it found `bugprone-implicit-widening-of-multiplication-result` on both of this task's large-payload tests: `256u * 1024u` and `1024u * 1024u` were multiplied in `unsigned int` and then widened to `std::size_t`. Neither can overflow at these sizes, but a byte count that is computed in a narrower type than it is stored in is exactly the defect the check exists for. Both are now named `constexpr std::size_t PAYLOAD_BYTES` constants that multiply in the right width (R3, R6). Nothing about the protocol or the transport changed. NC-020's report carries the full account of that round.
