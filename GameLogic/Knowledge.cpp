// GameLogic/Knowledge.cpp
#include "pch.h"
#include "Knowledge.h"

#include "Tuning.h"
#include "World.h"

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Simulation.h"

#include <utility>
#include <variant>
#include <vector>

namespace Nomad
{

namespace
{

template <typename E> void WriteEnum(Neuron::ByteWriter& _writer, E _value)
{
  _writer.Write(static_cast<std::uint8_t>(_value));
}

template <typename E> [[nodiscard]] bool ReadEnum(Neuron::ByteReader& _reader, E& _outValue, std::uint8_t _valueCount)
{
  std::uint8_t raw = 0;
  if (!_reader.Read(raw) || raw >= _valueCount)
  {
    return false;
  }
  _outValue = static_cast<E>(raw);
  return true;
}

/// An observer is a two-alternative variant, so its arm index is written before its id, exactly as a fleet's position
/// is. Reordering the alternatives renumbers every save (ADR-004).
void WriteObserver(Neuron::ByteWriter& _writer, const Observer& _observer)
{
  _writer.Write(static_cast<std::uint8_t>(_observer.index()));
  if (const auto* empire = std::get_if<EmpireId>(&_observer); empire != nullptr)
  {
    _writer.WriteId(*empire);
  }
  else
  {
    _writer.WriteId(std::get<CompanyId>(_observer));
  }
}

[[nodiscard]] bool ReadObserver(Neuron::ByteReader& _reader, Observer& _outObserver)
{
  std::uint8_t kind = 0;
  if (!_reader.Read(kind) || kind > 1)
  {
    return false;
  }
  if (kind == 0)
  {
    EmpireId empire{};
    if (!_reader.ReadId(empire))
    {
      return false;
    }
    _outObserver = empire;
    return true;
  }
  CompanyId company{};
  if (!_reader.ReadId(company))
  {
    return false;
  }
  _outObserver = company;
  return true;
}

void WriteShipCounts(Neuron::ByteWriter& _writer, const ShipCounts& _counts)
{
  for (const std::uint32_t count : _counts.byClass)
  {
    _writer.Write(count);
  }
}

[[nodiscard]] bool ReadShipCounts(Neuron::ByteReader& _reader, ShipCounts& _outCounts)
{
  for (std::uint32_t& count : _outCounts.byClass)
  {
    if (!_reader.Read(count))
    {
      return false;
    }
  }
  return true;
}

void WriteReport(Neuron::ByteWriter& _writer, const Report& _report)
{
  _writer.WriteTick(_report.observedAtTick);
  _writer.WriteTick(_report.deliveredAtTick);
  WriteEnum(_writer, _report.source);
  WriteObserver(_writer, _report.observer);
  _writer.WriteId(_report.sighting.subject);
  _writer.WriteId(_report.sighting.ownerCompany);
  _writer.WriteId(_report.sighting.ownerEmpire);
  WriteShipCounts(_writer, _report.sighting.countsSeen);
  _writer.WriteId(_report.sighting.atSystem);
  _writer.WriteBool(_report.sighting.identityKnown);
  _writer.WriteBool(_report.sighting.marked);
  _writer.WriteBool(_report.sighting.inTransit);
  _writer.WriteHundredths(_report.reliabilityWhenWritten);
  _writer.WriteBool(_report.checked);
  _writer.WriteBool(_report.lost);
}

[[nodiscard]] bool ReadReport(Neuron::ByteReader& _reader, Report& _outReport)
{
  return _reader.ReadTick(_outReport.observedAtTick) && _reader.ReadTick(_outReport.deliveredAtTick) &&
         ReadEnum(_reader, _outReport.source, static_cast<std::uint8_t>(REPORT_SOURCE_COUNT)) &&
         ReadObserver(_reader, _outReport.observer) && _reader.ReadId(_outReport.sighting.subject) &&
         _reader.ReadId(_outReport.sighting.ownerCompany) && _reader.ReadId(_outReport.sighting.ownerEmpire) &&
         ReadShipCounts(_reader, _outReport.sighting.countsSeen) && _reader.ReadId(_outReport.sighting.atSystem) &&
         _reader.ReadBool(_outReport.sighting.identityKnown) && _reader.ReadBool(_outReport.sighting.marked) &&
         _reader.ReadBool(_outReport.sighting.inTransit) && _reader.ReadHundredths(_outReport.reliabilityWhenWritten) &&
         _reader.ReadBool(_outReport.checked) && _reader.ReadBool(_outReport.lost);
}

void WriteBelief(Neuron::ByteWriter& _writer, const Belief& _belief)
{
  _writer.WriteId(_belief.believer);
  _writer.Write(static_cast<std::uint32_t>(_belief.suspicions.size()));
  for (const Suspicion& suspicion : _belief.suspicions)
  {
    _writer.WriteId(suspicion.incident);
    _writer.WriteId(suspicion.suspectCompany);
    _writer.WriteId(suspicion.suspectEmpire);
    _writer.WriteHundredths(suspicion.confidence);
    _writer.Write(static_cast<std::uint32_t>(suspicion.evidence.size()));
    for (const EvidenceId evidence : suspicion.evidence)
    {
      _writer.WriteId(evidence);
    }
    WriteEnum(_writer, suspicion.stage);
    _writer.WriteTick(suspicion.stageChangedAtTick);
  }
}

[[nodiscard]] bool ReadBelief(Neuron::ByteReader& _reader, Belief& _outBelief)
{
  std::uint32_t count = 0;
  if (!_reader.ReadId(_outBelief.believer) || !_reader.Read(count) || count > _reader.Remaining())
  {
    return false;
  }
  _outBelief.suspicions.assign(count, Suspicion{});
  for (Suspicion& suspicion : _outBelief.suspicions)
  {
    std::uint32_t evidenceCount = 0;
    if (!_reader.ReadId(suspicion.incident) || !_reader.ReadId(suspicion.suspectCompany) || !_reader.ReadId(suspicion.suspectEmpire) ||
        !_reader.ReadHundredths(suspicion.confidence) || !_reader.Read(evidenceCount) || evidenceCount > _reader.Remaining())
    {
      return false;
    }
    suspicion.evidence.assign(evidenceCount, EvidenceId{});
    for (EvidenceId& evidence : suspicion.evidence)
    {
      if (!_reader.ReadId(evidence))
      {
        return false;
      }
    }
    if (!ReadEnum(_reader, suspicion.stage, BELIEF_STAGE_COUNT) || !_reader.ReadTick(suspicion.stageChangedAtTick))
    {
      return false;
    }
  }
  return true;
}

void WriteOpinion(Neuron::ByteWriter& _writer, const Opinion& _opinion)
{
  _writer.WriteId(_opinion.character);
  _writer.WriteId(_opinion.company);
  _writer.WriteHundredths(_opinion.warmth);
  _writer.Write(_opinion.reliable);
  _writer.Write(_opinion.discreet);
  _writer.WriteId(_opinion.lastEmployerContract);
  _writer.WriteHundredths(_opinion.grudge);
  _writer.WriteHundredths(_opinion.loyalty);
  _writer.WriteTick(_opinion.changedAtTick);
}

[[nodiscard]] bool ReadOpinion(Neuron::ByteReader& _reader, Opinion& _outOpinion)
{
  return _reader.ReadId(_outOpinion.character) && _reader.ReadId(_outOpinion.company) && _reader.ReadHundredths(_outOpinion.warmth) &&
         _reader.Read(_outOpinion.reliable) && _reader.Read(_outOpinion.discreet) && _reader.ReadId(_outOpinion.lastEmployerContract) &&
         _reader.ReadHundredths(_outOpinion.grudge) && _reader.ReadHundredths(_outOpinion.loyalty) &&
         _reader.ReadTick(_outOpinion.changedAtTick);
}

void WriteThreat(Neuron::ByteWriter& _writer, const ThreatAssessment& _threat)
{
  _writer.WriteId(_threat.empire);
  _writer.WriteId(_threat.company);
  _writer.Write(_threat.step);
  _writer.WriteTick(_threat.stepChangedAtTick);
  _writer.WriteTick(_threat.cleanSinceTick);
}

[[nodiscard]] bool ReadThreat(Neuron::ByteReader& _reader, ThreatAssessment& _outThreat)
{
  return _reader.ReadId(_outThreat.empire) && _reader.ReadId(_outThreat.company) && _reader.Read(_outThreat.step) &&
         _outThreat.step < Tuning::THREAT_STEP_COUNT && _reader.ReadTick(_outThreat.stepChangedAtTick) &&
         _reader.ReadTick(_outThreat.cleanSinceTick);
}

void WriteEvidence(Neuron::ByteWriter& _writer, const Evidence& _evidence)
{
  WriteEnum(_writer, _evidence.kind);
  _writer.WriteId(_evidence.incident);
  _writer.WriteId(_evidence.suspectCompany);
  _writer.WriteId(_evidence.suspectEmpire);
  _writer.WriteHundredths(_evidence.weight);
  _writer.WriteId(_evidence.source);
  _writer.WriteTick(_evidence.tick);
}

[[nodiscard]] bool ReadEvidence(Neuron::ByteReader& _reader, Evidence& _outEvidence)
{
  return ReadEnum(_reader, _outEvidence.kind, EVIDENCE_KIND_COUNT) && _reader.ReadId(_outEvidence.incident) &&
         _reader.ReadId(_outEvidence.suspectCompany) && _reader.ReadId(_outEvidence.suspectEmpire) &&
         _reader.ReadHundredths(_outEvidence.weight) && _reader.ReadId(_outEvidence.source) && _reader.ReadTick(_outEvidence.tick);
}

void WriteEvidenceIds(Neuron::ByteWriter& _writer, const std::vector<EvidenceId>& _ids)
{
  _writer.Write(static_cast<std::uint32_t>(_ids.size()));
  for (const EvidenceId id : _ids)
  {
    _writer.WriteId(id);
  }
}

[[nodiscard]] bool ReadEvidenceIds(Neuron::ByteReader& _reader, std::vector<EvidenceId>& _outIds)
{
  std::uint32_t count = 0;
  if (!_reader.Read(count) || count > _reader.Remaining())
  {
    return false;
  }
  _outIds.assign(count, EvidenceId{});
  for (EvidenceId& id : _outIds)
  {
    if (!_reader.ReadId(id))
    {
      return false;
    }
  }
  return true;
}

void WriteAccusation(Neuron::ByteWriter& _writer, const Accusation& _accusation)
{
  _writer.WriteId(_accusation.incident);
  _writer.WriteId(_accusation.accuser);
  _writer.WriteId(_accusation.suspectCompany);
  _writer.WriteId(_accusation.suspectEmpire);
  _writer.WriteHundredths(_accusation.confidence);
  WriteEvidenceIds(_writer, _accusation.evidenceFor);
  WriteEvidenceIds(_writer, _accusation.evidenceAgainst);
  _writer.WriteTick(_accusation.issuedAtTick);
  _writer.WriteTick(_accusation.actedAtTick);
}

[[nodiscard]] bool ReadAccusation(Neuron::ByteReader& _reader, Accusation& _outAccusation)
{
  return _reader.ReadId(_outAccusation.incident) && _reader.ReadId(_outAccusation.accuser) &&
         _reader.ReadId(_outAccusation.suspectCompany) && _reader.ReadId(_outAccusation.suspectEmpire) &&
         _reader.ReadHundredths(_outAccusation.confidence) && ReadEvidenceIds(_reader, _outAccusation.evidenceFor) &&
         ReadEvidenceIds(_reader, _outAccusation.evidenceAgainst) && _reader.ReadTick(_outAccusation.issuedAtTick) &&
         _reader.ReadTick(_outAccusation.actedAtTick);
}

void WriteDossier(Neuron::ByteWriter& _writer, const DossierEntry& _entry)
{
  _writer.WriteId(_entry.observer);
  _writer.WriteId(_entry.admiral);
  _writer.Write(static_cast<std::uint32_t>(_entry.timesUsed.size()));
  for (const std::uint32_t used : _entry.timesUsed)
  {
    _writer.Write(used);
  }
  _writer.WriteTick(_entry.lastSeenAtTick);
  _writer.WriteId(_entry.lastSeenAtSystem);
  _writer.Write(_entry.engagementsSeen);
}

[[nodiscard]] bool ReadDossier(Neuron::ByteReader& _reader, DossierEntry& _outEntry)
{
  std::uint32_t usedCount = 0;
  if (!_reader.ReadId(_outEntry.observer) || !_reader.ReadId(_outEntry.admiral) || !_reader.Read(usedCount) ||
      usedCount > _reader.Remaining())
  {
    return false;
  }
  _outEntry.timesUsed.resize(usedCount);
  for (std::uint32_t& used : _outEntry.timesUsed)
  {
    if (!_reader.Read(used))
    {
      return false;
    }
  }
  return _reader.ReadTick(_outEntry.lastSeenAtTick) && _reader.ReadId(_outEntry.lastSeenAtSystem) &&
         _reader.Read(_outEntry.engagementsSeen);
}

void WriteObserverRecord(Neuron::ByteWriter& _writer, const ObserverRecord& _record)
{
  WriteObserver(_writer, _record.observer);
  for (const SourceRecord& source : _record.bySource)
  {
    _writer.Write(source.confirmed);
    _writer.Write(source.contradicted);
  }
}

[[nodiscard]] bool ReadObserverRecord(Neuron::ByteReader& _reader, ObserverRecord& _outRecord)
{
  if (!ReadObserver(_reader, _outRecord.observer))
  {
    return false;
  }
  for (SourceRecord& source : _outRecord.bySource)
  {
    if (!_reader.Read(source.confirmed) || !_reader.Read(source.contradicted))
    {
      return false;
    }
  }
  return true;
}

template <typename T, typename IdType, typename WriteRow>
void WriteTableOf(Neuron::ByteWriter& _writer, const Table<T, IdType>& _table, WriteRow _writeRow)
{
  _writer.Write(_table.Count());
  for (std::uint32_t index = 0; index < _table.Count(); ++index)
  {
    _writeRow(_writer, _table.Get(IdType::FromIndex(index)));
  }
}

template <typename T, typename IdType, typename ReadRow>
[[nodiscard]] bool ReadTableOf(Neuron::ByteReader& _reader, Table<T, IdType>& _outTable, ReadRow _readRow)
{
  std::uint32_t count = 0;
  if (!_reader.Read(count) || count > _reader.Remaining())
  {
    return false;
  }
  for (std::uint32_t index = 0; index < count; ++index)
  {
    T row{};
    if (!_readRow(_reader, row))
    {
      return false;
    }
    (void)_outTable.Add(row);
  }
  return true;
}

} // namespace

void Knowledge::Seed(const World& _world, Knowledge& _outKnowledge)
{
  // One row per empire, keyed by the same id: an empire with nowhere to put a suspicion is a bug nobody sees until
  // the first incident, which is exactly when it matters most.
  //
  // **Idempotent by construction rather than by a guard.** It fills up to the empire count, so calling it twice adds
  // nothing and calling it after a world grew an empire adds exactly that empire -- which is what lets the resolver
  // call it every tick without either a flag to keep in step with a reload or a hole the first incident falls into.
  for (std::uint32_t index = _outKnowledge.m_beliefs.Count(); index < _world.Empires().Count(); ++index)
  {
    Belief belief{};
    belief.believer = EmpireId::FromIndex(index);
    (void)_outKnowledge.m_beliefs.Add(belief);
  }
}

Belief* Knowledge::BeliefOf(EmpireId _empire) noexcept
{
  return m_beliefs.Holds(_empire) ? &m_beliefs.Get(_empire) : nullptr;
}

const Belief* Knowledge::BeliefOf(EmpireId _empire) const noexcept
{
  return m_beliefs.Holds(_empire) ? &m_beliefs.Get(_empire) : nullptr;
}

DossierEntry& Knowledge::DossierOf(CompanyId _company, CharacterId _admiral)
{
  for (std::uint32_t index = 0; index < m_dossiers.Count(); ++index)
  {
    const auto dossierId = DossierId::FromIndex(index);
    if (m_dossiers.Get(dossierId).observer == _company && m_dossiers.Get(dossierId).admiral == _admiral)
    {
      return m_dossiers.Get(dossierId);
    }
  }
  DossierEntry entry{};
  entry.observer = _company;
  entry.admiral = _admiral;
  entry.timesUsed.assign(TEMPLATE_COUNT, 0);
  return m_dossiers.Get(m_dossiers.Add(entry));
}

Opinion& Knowledge::OpinionOf(CharacterId _character, CompanyId _company, Neuron::Tick _now)
{
  for (std::uint32_t index = 0; index < m_opinions.Count(); ++index)
  {
    Opinion& opinion = m_opinions.Get(OpinionId::FromIndex(index));
    if (opinion.character == _character && opinion.company == _company)
    {
      return opinion;
    }
  }
  // **There is no "no opinion".** A character who has never met a company holds the neutral one, and saying so once
  // here keeps every caller from inventing its own default (GDD §9).
  Opinion created{};
  created.character = _character;
  created.company = _company;
  created.warmth = Tuning::OPINION_NEUTRAL;
  created.loyalty = Tuning::OPINION_NEUTRAL;
  created.changedAtTick = _now;
  return m_opinions.Get(m_opinions.Add(created));
}

ThreatAssessment& Knowledge::ThreatOf(EmpireId _empire, CompanyId _company, Neuron::Tick _now)
{
  for (std::uint32_t index = 0; index < m_threats.Count(); ++index)
  {
    ThreatAssessment& threat = m_threats.Get(ThreatId::FromIndex(index));
    if (threat.empire == _empire && threat.company == _company)
    {
      return threat;
    }
  }
  ThreatAssessment created{};
  created.empire = _empire;
  created.company = _company;
  created.step = static_cast<std::uint32_t>(Tuning::ThreatStep::Ignored);
  created.stepChangedAtTick = _now;
  created.cleanSinceTick = _now;
  return m_threats.Get(m_threats.Add(created));
}

SourceRecord& Knowledge::RecordFor(const Observer& _observer, ReportSource _source)
{
  const auto slot = static_cast<std::uint32_t>(_source);
  for (std::uint32_t index = 0; index < m_observerRecords.Count(); ++index)
  {
    ObserverRecord& record = m_observerRecords.Get(ObserverRecordId::FromIndex(index));
    if (record.observer == _observer)
    {
      return record.bySource[slot];
    }
  }
  ObserverRecord created{};
  created.observer = _observer;
  return m_observerRecords.Get(m_observerRecords.Add(created)).bySource[slot];
}

Neuron::Hundredths Knowledge::ReliabilityOf(const Observer& _observer, ReportSource _source) const
{
  const auto slot = static_cast<std::uint32_t>(_source);
  for (const ObserverRecord& record : m_observerRecords.Rows())
  {
    if (record.observer == _observer)
    {
      return record.bySource[slot].Reliability();
    }
  }
  return Neuron::Hundredths::FromRaw(UNPROVEN_RELIABILITY_HUNDREDTHS);
}

void Knowledge::Serialize(Neuron::ByteWriter& _writer) const
{
  _writer.Write(SCHEMA_VERSION);
  WriteTableOf(_writer, m_reports, WriteReport);
  WriteTableOf(_writer, m_beliefs, WriteBelief);
  WriteTableOf(_writer, m_opinions, WriteOpinion);
  WriteTableOf(_writer, m_threats, WriteThreat);
  WriteTableOf(_writer, m_evidence, WriteEvidence);
  WriteTableOf(_writer, m_accusations, WriteAccusation);
  WriteTableOf(_writer, m_observerRecords, WriteObserverRecord);
  WriteTableOf(_writer, m_dossiers, WriteDossier);
}

std::uint64_t Knowledge::Hash() const
{
  Neuron::ByteWriter writer;
  Serialize(writer);
  return Neuron::Simulation::HashBytes(writer.Bytes());
}

bool Knowledge::Deserialize(Neuron::ByteReader& _reader)
{
  std::uint16_t version = 0;
  if (!_reader.Read(version) || version != SCHEMA_VERSION)
  {
    return false;
  }
  // Into a fresh object, so that a buffer running out half way leaves nothing partial behind (World does the same).
  Knowledge loaded;
  if (!ReadTableOf(_reader, loaded.m_reports, ReadReport) || !ReadTableOf(_reader, loaded.m_beliefs, ReadBelief) ||
      !ReadTableOf(_reader, loaded.m_opinions, ReadOpinion) || !ReadTableOf(_reader, loaded.m_threats, ReadThreat) ||
      !ReadTableOf(_reader, loaded.m_evidence, ReadEvidence) || !ReadTableOf(_reader, loaded.m_accusations, ReadAccusation) ||
      !ReadTableOf(_reader, loaded.m_observerRecords, ReadObserverRecord) || !ReadTableOf(_reader, loaded.m_dossiers, ReadDossier))
  {
    return false;
  }
  *this = std::move(loaded);
  return true;
}

} // namespace Nomad
