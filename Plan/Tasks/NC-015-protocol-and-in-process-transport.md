# NC-015 — `Protocol` and the in-process transport

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronCore | M | no | **yes** | Open |

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

- [ ] Frame then unframe returns the same channel and payload bytes for a zero-length and a 1 MiB payload.
- [ ] A header with an unknown version, or a payload length beyond the buffer, is rejected without reading past the end.
- [ ] Ten thousand messages sent on one endpoint are received on the other in order, and `Receive` on an empty queue returns `false` without blocking.
- [ ] Nothing in the three headers names a game type or includes anything outside NeuronCore.

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

_Filled in on hand-back._
