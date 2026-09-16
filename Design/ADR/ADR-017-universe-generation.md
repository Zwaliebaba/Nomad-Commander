# ADR-017 — Universe generation

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-041
**Cites:** GDD §7 (the graph and its roles), §8 (the contraction that left the harbours), §10 (what a role produces), §15 (about ten systems and three empires; Milestone 2's twenty and five); AGENTS.md R16, R20; ADR-002, ADR-004

## Context

GDD §7 opens with a list: "a graph of star systems joined by lanes, with roles: chokepoints, bypasses, dead ends, resource hubs, safe harbours, frontier systems and crossroads." The design says what the roles are called and never says what they *are*, because it is a design document and the answer is engineering.

There are two readings, and they produce different games.

**A role could be a label.** Generate a graph, sprinkle eight names over it, and let the economy and the AI read the names. This is easy, and it is what most procedural maps do. Its failure is quiet: a "chokepoint" the fleet can simply go around, a "dead end" with three lanes out of it. Nothing crashes. The player learns, over a few hours, that the words on the map do not mean anything, and the map stops being information.

**Or a role could be a claim about the graph, true by construction.** Then a chokepoint really is the only way through, and taking it really does cut the map; a dead end really is a corner a fleet can be trapped in. GDD §13 asks the map to carry "decisions, not data", and a role that is merely a label is data.

The plan's own recommendation (`Plan/Roadmap.md`) was the first reading: "assigns roles by graph shape" — shape, but with no statement of what happens when the shape does not oblige.

## Decision

**Four of the eight roles are properties of the graph, established by construction and asserted by the tests. The other four are economic and are placed.**

| Role | What it means, exactly | How it is guaranteed |
|---|---|---|
| **Chokepoint** | Removing it disconnects the map | It is the dead end's only neighbour, which is an articulation point whenever the map has three or more systems |
| **DeadEnd** | Exactly one lane | Held out of the spanning tree, given one lane, and excluded from every later lane by name |
| **Crossroads** | Four or more lanes | The system nearest the middle, raised to degree four by joining it to its nearest neighbours |
| **Bypass** | Removing it leaves the map connected — there is a way around it | Chosen after the extra lanes have made cycles, by testing removal; if no candidate exists, lanes are added until one does |

`ResourceHub`, `Refinery`, `SafeHarbor` and `Frontier` are handed round-robin to whatever the graph did not speak for. **`MIN_SYSTEM_COUNT` is 8** and that is the arithmetic of this table: four systems are spoken for by shape, and one of each economic role needs four more.

**The rest of the decision, briefly.**

1. **Systems sit on a jittered grid** in 1920×1080 pixel space with a margin, so NC-072 draws the positions unchanged and no layout pass has to exist. The simulation never reads a coordinate for logic; the map is a graph and distance is `JumpsBetween`.
2. **The generator is the one exception to that**, and it is a generation-time one: a lane's `jumpTicks` is interpolated from the length of the line drawn, between two and four hours (GDD §7's starting clock), on the *squared* distance — which compresses the short end and suits a map where most lanes are short. Afterwards the tick count in the lane is the only truth.
3. **The spanning tree is Prim's by nearest neighbour**, which gives short lanes and a map that reads as a region rather than a web. Extra lanes number `systemCount / 3`.
4. **Distances are compared squared and never square-rooted.** An integer square root would round two different lengths to one and make a nearest-neighbour choice depend on that rounding (R16).
5. **Empire homes are chosen far apart by *jumps*, not by pixels**, greedily: the crossroads, then repeatedly whichever system is furthest from every home already chosen. The dead end is never a home — a capital with one lane out cannot be reinforced. Territory then grows out of each home a jump at a time, round-robin, so holdings are contiguous and no empire runs away with the map.
6. **One or two systems are left to nobody** (`HarborCount`): two at twelve systems and up, one below. GDD §8's contraction has already happened when the game starts, and the harbours are where a nomad is welcome.
7. **It draws from the world's `Generation` stream**, so generating a map does not shift what detection, battle or inference will draw (NC-011's `Fork`).

## Measurements

Over **100 consecutive seeds** at ten systems and three empires, and 20 seeds at Milestone 2's twenty and five:

| | |
|---|---|
| Maps generated with all eight roles present | **100 of 100** |
| Maps connected | **100 of 100** |
| Chokepoint whose removal disconnects the map | **100 of 100** |
| Bypass whose removal does not | **100 of 100** |
| Dead ends with exactly one lane | **100 of 100** |
| Crossroads with four lanes or more | **100 of 100** |
| Lanes outside the two-to-four-hour band | **0** |
| Seeds where two runs differed | **0** (hash and bytes) |
| Milestone 2 shape (20 systems, 5 empires) generated and connected | **20 of 20** |

The first six rows are the point. They are checked by walking the graph in the test rather than by reading the role back, so they measure the map and not the generator's opinion of it.

## What this forecloses

**A map editor, and hand-placed sandbox maps.** The sandbox map is a function of a seed and nothing else; there is no file to author and nowhere to put one (R13). The Kessel scenario's map is hand-written *data in these same types* (NC-090) and does not come through the generator, which is the sanctioned way to place a map by hand.

**Maps below eight systems**, which is a real restriction: a five-system prologue sector (GDD §7's thirty-minute prologue) cannot have one of each role and would need this decision reopened, or a smaller role set of its own. The prologue is outside v0.1 (A4), so nothing is blocked today.

**Roles as a free-form tag.** A later task that wants a ninth role has to say what it is true of, or accept that it is decoration. That is deliberate: it is the question this ADR exists to have answered once.

It does **not** foreclose a different graph topology. Prim's tree plus cycles is one shape; ring maps, clustered regions and layered frontiers would all satisfy the four structural guarantees, and swapping the construction is a change to one function.
