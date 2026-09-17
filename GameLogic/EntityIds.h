// GameLogic/EntityIds.h
#pragma once

#include "Id.h"

namespace Nomad
{

/// The typed index of every entity the world holds (AGENTS.md §2, R22), in one header rather than beside each struct.
///
/// The entities refer to one another in a cycle and there is no way to break it: a Company holds FleetIds, a Fleet is
/// owned by a CompanyId or an EmpireId, a Character is allegiant to either, and an Outpost is owned by any of them. An
/// alias next to its own struct -- which is what `Plan/Glossary.md` assigns -- would need Company.h and Fleet.h to
/// include each other. The type names are the Glossary's unchanged; only their home moved.
///
/// A tag is declared and never defined. Neuron::Id names its parameter and never instantiates it, so an incomplete
/// type is enough, and two aliases over two tags are two types the compiler keeps apart.
struct CompanyTag;
struct EmpireTag;
struct FleetTag;
struct CharacterTag;
struct OutpostTag;
struct SystemTag;
struct LaneTag;
struct EventTag;
struct MothballTag;
struct RelationTag;
struct ReportTag;
struct IncidentTag;
struct OpinionTag;
struct ThreatTag;
struct EvidenceTag;
struct ObserverRecordTag;
struct AccusationTag;
struct CourierTag;
struct WreckAnalysisTag;
struct ContractTag;

/// The nomad (GDD §11, §14). Named Company because `namespace Nomad` already exists and a type of that name inside it
/// would shadow the namespace for every qualified name in game code (`Plan/Glossary.md`).
using CompanyId = Neuron::Id<CompanyTag>;
using EmpireId = Neuron::Id<EmpireTag>;
using FleetId = Neuron::Id<FleetTag>;
using CharacterId = Neuron::Id<CharacterTag>;
using OutpostId = Neuron::Id<OutpostTag>;

/// NC-041 brings the structs these name; the ids are here because NC-040's Fleet already has to say where a fleet is.
using SystemId = Neuron::Id<SystemTag>;
using LaneId = Neuron::Id<LaneTag>;

/// NC-042 brings Event. A Company's record and a Fleet's history are lists of these from this commit, because the
/// record is what GDD §8 and §11 refer back to and an entity that gains a history later gains it everywhere.
using EventId = Neuron::Id<EventTag>;

/// A hull whose crew deserted, waiting out its grace period (NC-046).
using MothballId = Neuron::Id<MothballTag>;

/// Where one pair of empires stands (NC-047).
using RelationId = Neuron::Id<RelationTag>;

/// One thing somebody was told, with its source, its age and its reliability (GDD §4, NC-050). Everything the AI and
/// the client ever reason from is one of these; reality is `World` and nothing outside `GameLogic` holds one.
using ReportId = Neuron::Id<ReportTag>;

/// A raid or an attack an empire suffered (GDD §6, NC-051). **Reality**, and the only place the culprit is written.
using IncidentId = Neuron::Id<IncidentTag>;

/// What one character thinks of one company (GDD §9), and how dangerous one empire finds one company (§11).
using OpinionId = Neuron::Id<OpinionTag>;
using ThreatId = Neuron::Id<ThreatTag>;

/// One item of GDD §6's evidence table, with what it weighed (NC-052). Declared here because NC-051's `Suspicion`
/// holds a list of them: a belief that could not say what it was built from would fail R19 on the first accusation.
using EvidenceId = Neuron::Id<EvidenceTag>;

/// What one observer has found out about its sources (GDD §4's track record, NC-050).
using ObserverRecordId = Neuron::Id<ObserverRecordTag>;

/// An empire saying out loud that it thinks somebody did something (GDD §6's forty, NC-052). NC-054 answers one.
using AccusationId = Neuron::Id<AccusationTag>;

/// An order or a message physically crossing the lanes (GDD §4, §9; NC-053). **Reality**: it has a position and it
/// can be taken off somebody. What it carries is named by id, never held by value -- see `Courier.h`.
using CourierId = Neuron::Id<CourierTag>;

/// Six hours of a scout's time on an incident's site (GDD §3, NC-054). **Reality**: the scout is either there or it
/// is not.
using WreckAnalysisId = Neuron::Id<WreckAnalysisTag>;

/// An offer an empire made and what became of it (GDD §8, NC-056). One id covers the offer and the contract because
/// they are one row: an offer that was taken is a contract, and one that was not is still the thing the board showed.
using ContractId = Neuron::Id<ContractTag>;

} // namespace Nomad
