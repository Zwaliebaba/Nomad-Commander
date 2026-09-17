// GameLogic/Knowledge.h
#pragma once

#include "Belief.h"
#include "Opinion.h"
#include "Report.h"
#include "Table.h"
#include "ThreatAssessment.h"

#include <cstdint>

namespace Neuron
{
class ByteReader;
class ByteWriter;
} // namespace Neuron

namespace Nomad
{

class World;

/// **Everything anyone knows, and never the world** (R18, GDD §4, §9).
///
/// `World` holds reality and says of itself that belief, reports, opinions and evidence are "held beside a World
/// rather than inside one ... the way that rule is kept structural is that a decision routine takes belief and there
/// is no path from belief to here." This is that *beside*. A routine handed a `Knowledge&` can reach a report, a
/// suspicion, an opinion and a threat step, and cannot reach a fleet's true position, a hull count or
/// `Incident::culprit` — not because it is discouraged from trying, but because there is no member to try it
/// through.
///
/// **NC-050 got this wrong and NC-051 is where it was put right.** Detection landed its `Reports` table inside
/// `World`, which compiled, passed and contradicted the paragraph in `World.h` that nobody had changed. The table
/// moved here; the rest of NC-051's types were built here from the start.
///
/// One thing is deliberately *not* here: **`Incident`**. An incident is something that happened, with a culprit,
/// and it is reality — `World` holds it. What an empire believes about who did it is a `Suspicion`, and that is
/// here. The two are joined by an `IncidentId` and by nothing else, which is the whole of GDD §6's design.
class Knowledge
{
public:
  /// Everything anybody was ever told (NC-050). Rows stay after delivery, because the dossiers and NC-052's evidence
  /// refer back to them.
  [[nodiscard]] Table<Report, ReportId>& Reports() noexcept
  {
    return m_reports;
  }

  [[nodiscard]] const Table<Report, ReportId>& Reports() const noexcept
  {
    return m_reports;
  }

  /// One row per empire, keyed by the same id, the way a `Market` is keyed by its `SystemId`. `Seed` fills it.
  [[nodiscard]] Table<Belief, EmpireId>& Beliefs() noexcept
  {
    return m_beliefs;
  }

  [[nodiscard]] const Table<Belief, EmpireId>& Beliefs() const noexcept
  {
    return m_beliefs;
  }

  /// One row per (character, company) pair that has ever had reason to exist. A flat table with a linear find rather
  /// than a map: v0.1 has a handful of characters and one company, and an unordered container iterated into the
  /// simulation is the defect R16 names.
  [[nodiscard]] Table<Opinion, OpinionId>& Opinions() noexcept
  {
    return m_opinions;
  }

  [[nodiscard]] const Table<Opinion, OpinionId>& Opinions() const noexcept
  {
    return m_opinions;
  }

  /// One row per (empire, company) pair, same shape and for the same reason.
  [[nodiscard]] Table<ThreatAssessment, ThreatId>& Threats() noexcept
  {
    return m_threats;
  }

  [[nodiscard]] const Table<ThreatAssessment, ThreatId>& Threats() const noexcept
  {
    return m_threats;
  }

  /// What each observer has found out about its sources (GDD §4's track record).
  [[nodiscard]] Table<ObserverRecord, ObserverRecordId>& ObserverRecords() noexcept
  {
    return m_observerRecords;
  }

  [[nodiscard]] const Table<ObserverRecord, ObserverRecordId>& ObserverRecords() const noexcept
  {
    return m_observerRecords;
  }

  /// Gives every empire a belief to hold. Called once when a universe is built, beside `Economy::Seed` and
  /// `Politics::Seed` -- an empire with nowhere to put a suspicion is a bug nobody sees until an incident.
  static void Seed(const World& _world, Knowledge& _outKnowledge);

  /// The opinion this character holds of this company, creating an empty one the first time it is asked for. There is
  /// no "no opinion": a character who has never met a company holds the neutral one, and saying so once here keeps
  /// every caller from inventing its own default.
  [[nodiscard]] Opinion& OpinionOf(CharacterId _character, CompanyId _company, Neuron::Tick _now);

  /// What this empire makes of this company, likewise created neutral on first use.
  [[nodiscard]] ThreatAssessment& ThreatOf(EmpireId _empire, CompanyId _company, Neuron::Tick _now);

  /// The empire's belief, or null when the id names no empire that was seeded.
  [[nodiscard]] Belief* BeliefOf(EmpireId _empire) noexcept;
  [[nodiscard]] const Belief* BeliefOf(EmpireId _empire) const noexcept;

  /// This observer's record with this source, created empty on first use. An observer nobody has checked reads as
  /// `UNPROVEN_RELIABILITY_HUNDREDTHS`, which is what an empty record answers.
  [[nodiscard]] SourceRecord& RecordFor(const Observer& _observer, ReportSource _source);
  [[nodiscard]] Neuron::Hundredths ReliabilityOf(const Observer& _observer, ReportSource _source) const;

  /// Written after the world and read after it, so one store carries both (ADR-004, ADR-014).
  void Serialize(Neuron::ByteWriter& _writer) const;
  [[nodiscard]] bool Deserialize(Neuron::ByteReader& _reader);

  /// A hash of everything `Serialize` writes, for the determinism harness and the store's round trip -- `World::Hash`
  /// for the other half. Taken over the bytes rather than field by field, so a field `Serialize` forgot cannot hide
  /// from it, and it is `Neuron::Simulation`'s own FNV-1a so that the two halves are hashed by one implementation.
  [[nodiscard]] std::uint64_t Hash() const;

  /// Bumped when the layout changes in a way an older store could not be read as. Separate from `World`'s, because
  /// the two halves change for different reasons.
  static constexpr std::uint16_t SCHEMA_VERSION = 1;

private:
  Table<Report, ReportId> m_reports;
  Table<Belief, EmpireId> m_beliefs;
  Table<Opinion, OpinionId> m_opinions;
  Table<ThreatAssessment, ThreatId> m_threats;
  Table<ObserverRecord, ObserverRecordId> m_observerRecords;
};

} // namespace Nomad
