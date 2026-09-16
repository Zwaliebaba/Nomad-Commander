# ADR-006 — The client–host transport in v0.1

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-015
**Owner-visible:** yes — this decision is what the always-on host of the full game will be built on, and it is the structure that keeps the client from seeing the world.
**Cites:** GDD §4 (the AI and the client see through the same fog), §9 (reality, belief and evidence distinct), §14 (the hosting model is deferred), §15 (v0.1 has no always-on host); AGENTS.md §2 (the wire protocol; `GameLogic` is host-side), R18, R23; ADR-001; `Plan/Roadmap.md` A2

## Context

v0.1 is one executable that hosts the simulation and runs the client in one process (GDD §15), and the hosting model of the full game is deferred by the design (§14). Two obvious shapes were available. The client could call the session's API directly, with serialization arriving when a remote host does; or the client and the host could exchange serialized messages from the first line, over a transport that happens to be in memory.

The second costs a little now. The first costs R18 later: if the client can call into the host, then nothing but review stops a screen from reading `World`, and GDD §4 and §9 make that a design failure rather than a style one — the client is handed reports, beliefs and explanations, never the truth. AGENTS.md §2 puts it plainly: "a client-side file that reaches for `GameLogic` is a client that can see through the fog."

AGENTS.md §2 also lists `Socket` and `FrameStream` among NeuronCore's contents. v0.1 needs neither, and R23 says build what v0.1 needs and nothing beyond it.

## Decision

1. **The client and the host exchange framed byte messages, from day one.** `Protocol` puts a seven-byte envelope in front of every payload: a `std::uint16_t` version, a `std::uint8_t` channel, a `std::uint32_t` length. A reader refuses a version it does not know, a channel value that names no enumerator, a length beyond `MAX_PAYLOAD_BYTES` (64 MiB), and a message its buffer does not hold in full.
2. **Three channels.** `SessionControl` carries what is the host's and not the simulation's: the clock's rate, a skip, a save. `SimulationInput` and `SimulationOutput` carry `Simulation` bytes verbatim; what those bytes mean is the game's schema (`GameLogic/Wire*.h`, ADR-001) and nothing the engine knows. `SessionControl` is zero, so a decoder never has to cast a value no enumerator has.
3. **`Transport` is an interface that moves whole messages**, never a stream of bytes: what `Receive` hands back is exactly what one `Send` put in. A socket implementation will have to keep that contract, so the in-process one keeps it too rather than passing payloads around unframed.
4. **v0.1's implementation is `MemoryTransport`**: two endpoints, each sending into the other's queue, in one process, single-threaded. `Receive` never blocks; an empty queue is the ordinary case, because the host pumps.
5. **`Socket` and `FrameStream` are deferred** to the always-on host, with no stub and no placeholder. They are a task in the full game's plan, not an empty file here.
6. **Nothing in a message may assume one process.** No pointer, no handle, no index that means something only in this address space. What crosses is bytes that a socket could carry unchanged.

## What this forecloses

- The client calling the host directly, which is the point.
- A message design that depends on shared memory. A payload is bytes, and a wire record that held a pointer would fail the day the socket arrives, when nobody would remember why.
- Two threads over `MemoryTransport` as written: the queue has no lock. If the host ever runs on its own thread, this class is where the lock goes, and this ADR is superseded rather than quietly stretched.
- A transport that reorders or coalesces messages.

## Consequences

- `NomadCommander` builds a `MemoryTransport` pair in its composition root (NC-070) and hands one end to the session and one to the client model; nothing else in the client can reach the host.
- `ClientModel` (NC-071) is written against wire records from the start, so the screens of Phase 5 cannot be written against `World` even by accident.
- The full game's socket is a second `Transport` implementation and a `FrameStream` that reassembles messages from a byte stream. Nothing above the interface changes, which is what this decision buys.
- The cost paid today is one serialization and one copy per message inside one process. For the message rates of a desk session (a board of items, a few inputs a minute) that cost is not measurable; a headless soak (NC-102) sends nothing at all, because it has no client.

## Measurements

None quoted for the transport itself: ten thousand small messages through a pair, sent and received in order, complete inside the noise of the test that measures them, and the byte streams they are built on are measured in ADR-004. What would be worth measuring is the wire cost of a full board update once the board exists (NC-067); it is not measurable before there is one.
