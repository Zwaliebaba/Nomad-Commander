// GameLogic/World.h
#pragma once

#include "Character.h"
#include "Company.h"
#include "Courier.h"
#include "Empire.h"
#include "Fleet.h"
#include "Incident.h"
#include "Lane.h"
#include "Market.h"
#include "MothballedHull.h"
#include "Relation.h"
#include "Outpost.h"
#include "StarSystem.h"
#include "Table.h"
#include "WreckAnalysis.h"

#include "Random.h"
#include "Tick.h"

#include <cstdint>
#include <vector>

namespace Neuron
{
class ByteReader;
class ByteWriter;
} // namespace Neuron

namespace Nomad
{

/// A subsystem that draws from the pinned PRNG.
///
/// Each gets its own stream off the world's seed, so adding a consumer later does not shift what the existing ones
/// draw and an old store still replays (R16, and NC-011's Fork). The values are part of the store's schema: renumber
/// one and every save replays differently. Append, never insert.
enum class RandomStream : std::uint64_t
{
  Generation,
  Detection,
  Courier,
  Battle,
  Economy,
  Empires,
  Inference,
  Admirals
};

inline constexpr std::uint32_t RANDOM_STREAM_COUNT = 8;

/// Reality: the whole world state, and the only thing in this tree that holds the truth (`Plan/Glossary.md`).
///
/// **What is not here is as fixed as what is.** No belief, no report, no opinion, no evidence and no confidence: those
/// are `Knowledge`'s (NC-050 to NC-052), and they are held beside a World rather than inside one. R18 is why --
/// "an admiral plans against reports about the player's fleet, not against its true position and strength" -- and the
/// way that rule is kept structural is that a decision routine takes belief and there is no path from belief to here.
/// Nothing outside GameLogic holds a World at all.
///
/// **NC-050 broke that paragraph and NC-051 put it back.** Detection landed its `Reports` table here, which compiled,
/// passed every test and quietly made the sentence above false. If a later task finds itself adding something anyone
/// merely *believes* to this class, the answer is `Knowledge` and the reason is this note.
///
/// `Incident` is the case that looks like an exception and is not: an incident **happened**, and `Incident::culprit`
/// is ground truth. What an empire makes of it is a `Suspicion`, and that is in `Knowledge`.
///
/// **No presentation either**, with one named exception: Empire::colorSlot, which the simulation never reads.
///
/// A class rather than an aggregate, because it has invariants -- the tables only grow, the tick only advances, and
/// the PRNG streams are forked once from the seed and thereafter belong to their subsystems (R8).
class World
{
public:
  /// Bumped when the layout below changes in any way that an older store could not be read as. ADR-004 puts one of
  /// these at the head of each store; this is the game's half of that number.
  static constexpr std::uint16_t SCHEMA_VERSION = 11;

  explicit World(std::uint64_t _seed);

  [[nodiscard]] Table<Company, CompanyId>& Companies() noexcept
  {
    return m_companies;
  }

  [[nodiscard]] const Table<Company, CompanyId>& Companies() const noexcept
  {
    return m_companies;
  }

  [[nodiscard]] Table<Empire, EmpireId>& Empires() noexcept
  {
    return m_empires;
  }

  [[nodiscard]] const Table<Empire, EmpireId>& Empires() const noexcept
  {
    return m_empires;
  }

  [[nodiscard]] Table<Fleet, FleetId>& Fleets() noexcept
  {
    return m_fleets;
  }

  [[nodiscard]] const Table<Fleet, FleetId>& Fleets() const noexcept
  {
    return m_fleets;
  }

  [[nodiscard]] Table<Character, CharacterId>& Characters() noexcept
  {
    return m_characters;
  }

  [[nodiscard]] const Table<Character, CharacterId>& Characters() const noexcept
  {
    return m_characters;
  }

  [[nodiscard]] Table<Outpost, OutpostId>& Outposts() noexcept
  {
    return m_outposts;
  }

  [[nodiscard]] Table<StarSystem, SystemId>& Systems() noexcept
  {
    return m_systems;
  }

  [[nodiscard]] const Table<StarSystem, SystemId>& Systems() const noexcept
  {
    return m_systems;
  }

  [[nodiscard]] Table<Lane, LaneId>& Lanes() noexcept
  {
    return m_lanes;
  }

  [[nodiscard]] const Table<Lane, LaneId>& Lanes() const noexcept
  {
    return m_lanes;
  }

  /// One market a system, indexed by the same id: `Markets().Get(systemId)` is that system's economy. A separate
  /// table rather than a field on `StarSystem`, because NC-041 owns the geography and a market is not geography.
  [[nodiscard]] Table<Market, SystemId>& Markets() noexcept
  {
    return m_markets;
  }

  [[nodiscard]] const Table<Market, SystemId>& Markets() const noexcept
  {
    return m_markets;
  }

  /// Hulls whose crews deserted, waiting out their grace period (GDD §5, NC-046). Rows stay after they expire, like
  /// every other entity here, because the record refers to them.
  [[nodiscard]] Table<MothballedHull, MothballId>& Mothballs() noexcept
  {
    return m_mothballs;
  }

  [[nodiscard]] const Table<MothballedHull, MothballId>& Mothballs() const noexcept
  {
    return m_mothballs;
  }

  /// Where each pair of empires stands (GDD §7, §8). One row per unordered pair.
  [[nodiscard]] Table<Relation, RelationId>& Relations() noexcept
  {
    return m_relations;
  }

  [[nodiscard]] const Table<Relation, RelationId>& Relations() const noexcept
  {
    return m_relations;
  }

  /// What has been done to the empires (GDD §6, NC-051). **Reality, culprit included** -- what anyone *believes*
  /// about who did it is a `Suspicion` in `Knowledge`, and the two are joined by an `IncidentId` and nothing else.
  [[nodiscard]] Table<Incident, IncidentId>& Incidents() noexcept
  {
    return m_incidents;
  }

  [[nodiscard]] const Table<Incident, IncidentId>& Incidents() const noexcept
  {
    return m_incidents;
  }

  /// Orders and messages physically crossing the lanes (GDD §4, §9; NC-053). **Reality**: a courier is at a place and
  /// a fleet can take it off somebody. What it carries is named by id and never held by value, so a routine with a
  /// `World&` still cannot read a report through one (`Courier.h`, ADR-021).
  ///
  /// Rows stay after delivery or capture, like every other table: a courier that was taken is a thing that happened,
  /// and the receipt refers back to it.
  [[nodiscard]] Table<Courier, CourierId>& Couriers() noexcept
  {
    return m_couriers;
  }

  [[nodiscard]] const Table<Courier, CourierId>& Couriers() const noexcept
  {
    return m_couriers;
  }

  /// Scouts reading incident sites, and what they found (GDD §3's six hours; NC-054). Reality: a scout being
  /// somewhere is a fact. What the finding *means* becomes belief only when it is submitted and weighed.
  [[nodiscard]] Table<WreckAnalysis, WreckAnalysisId>& WreckAnalyses() noexcept
  {
    return m_wreckAnalyses;
  }

  [[nodiscard]] const Table<WreckAnalysis, WreckAnalysisId>& WreckAnalyses() const noexcept
  {
    return m_wreckAnalyses;
  }

  /// The couriers still in the air, in dispatch order (NC-053).
  ///
  /// **Derived state, and it exists for a measured reason.** Rows are never erased from any table here, so the
  /// courier table grows for the whole run; walking all of it twice a tick cost a measured 1.7x over a simulated
  /// year for 273 couriers that had all long since landed. This is the working set, and `Deserialize` rebuilds it
  /// from the rows rather than carrying it in the store, so there is nothing a save can disagree with.
  ///
  /// Dispatch order is load-bearing: it is the order the resolver moves them in, and removal keeps it (R16).
  [[nodiscard]] std::vector<CourierId>& CouriersInFlight() noexcept
  {
    return m_couriersInFlight;
  }

  [[nodiscard]] const std::vector<CourierId>& CouriersInFlight() const noexcept
  {
    return m_couriersInFlight;
  }

  /// What JumpsBetween answers when there is no route at all. A disconnected map is a generator bug (NC-041 asserts
  /// connectivity), but a route to a system that does not exist is an ordinary caller error and gets an answer.
  static constexpr std::uint32_t UNREACHABLE = 0xFFFFFFFFu;

  /// The systems one jump away, in the order this system's lanes were laid down.
  ///
  /// That order is the whole of what makes the searches below deterministic (R16): a breadth-first search visits
  /// neighbours in it, so two runs of one seed walk the map identically and the shortest route between two systems is
  /// not merely as short as any other but the same one every time.
  void Adjacent(SystemId _system, std::vector<SystemId>& _outNeighbors) const;

  /// Jumps along the shortest route, counting lanes and not systems, so a system is zero jumps from itself.
  [[nodiscard]] std::uint32_t JumpsBetween(SystemId _from, SystemId _to) const;

  /// The shortest route, both ends included. False when there is none, and the route is then empty.
  [[nodiscard]] bool ShortestRoute(SystemId _from, SystemId _to, std::vector<SystemId>& _outRoute) const;

  [[nodiscard]] const Table<Outpost, OutpostId>& Outposts() const noexcept
  {
    return m_outposts;
  }

  /// The clock, and the only one the simulation has (R21: nothing in here reads wall time; the host maps wall time to
  /// ticks at the seam).
  [[nodiscard]] Neuron::Tick CurrentTick() const noexcept
  {
    return m_tick;
  }

  void AdvanceTick() noexcept
  {
    ++m_tick;
  }

  [[nodiscard]] std::uint64_t Seed() const noexcept
  {
    return m_seed;
  }

  /// The generator a subsystem draws from. Never a Random of one's own, and never std::random_device: the replay is a
  /// feature (GDD §4, §8) and it is made of exactly these streams.
  [[nodiscard]] Neuron::Random& RandomFor(RandomStream _stream) noexcept;

  /// Every table, in a fixed order, followed by the clock and the PRNG streams. The order is the store's schema and is
  /// the same on the way in and the way out (ADR-004).
  void Serialize(Neuron::ByteWriter& _writer) const;

  /// False on a truncated or malformed buffer, and on a schema version this build does not know. A World that failed
  /// to deserialize is left empty rather than half-filled, so a caller cannot act on a partial world.
  [[nodiscard]] bool Deserialize(Neuron::ByteReader& _reader);

  /// A hash of everything Serialize writes, for the determinism harness (NC-043) and the store's round trip.
  ///
  /// It is taken over the serialized bytes rather than field by field, so that equal hashes and equal stores are the
  /// same statement: a field that Serialize forgot cannot hide from it, which is the failure a hand-written hash has.
  [[nodiscard]] std::uint64_t Hash() const;

private:
  Table<Company, CompanyId> m_companies;
  Table<Empire, EmpireId> m_empires;
  Table<Fleet, FleetId> m_fleets;
  Table<Character, CharacterId> m_characters;
  Table<Outpost, OutpostId> m_outposts;
  Table<StarSystem, SystemId> m_systems;
  Table<Lane, LaneId> m_lanes;
  Table<Market, SystemId> m_markets;
  Table<MothballedHull, MothballId> m_mothballs;
  Table<Relation, RelationId> m_relations;
  Table<Incident, IncidentId> m_incidents;
  Table<Courier, CourierId> m_couriers;
  std::vector<CourierId> m_couriersInFlight;
  Table<WreckAnalysis, WreckAnalysisId> m_wreckAnalyses;

  std::vector<Neuron::Random> m_randomStreams;
  Neuron::Tick m_tick = 0;
  std::uint64_t m_seed = 0;
};

} // namespace Nomad
