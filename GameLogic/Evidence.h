// GameLogic/Evidence.h
#pragma once

#include "EntityIds.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// One row of GDD §6's evidence table. The order is the store's schema (ADR-004); append, never insert.
///
/// **All ten exist from this commit even though five of them cannot yet be produced.** `CapturedOrders` is NC-053's,
/// the two denials and `ExposedFalseDenial` are NC-054's, and `MarkedGoodsSold` is NC-055's. Declaring them now is
/// what keeps the wire and the store from being renumbered three times over the next three tasks, and it is the same
/// reason `ReportSource::CapturedCourier` was declared before anything wrote one (NC-050).
enum class EvidenceKind : std::uint8_t
{
  DetectedWithinTwoJumps,
  HullClassesMatch,
  TestimonyNames,
  RouteConflicts,
  PriorPattern,
  CapturedOrders,
  MarkedGoodsSold,
  RivalDenial,
  OthersDenial,
  ExposedFalseDenial
};

inline constexpr std::uint8_t EVIDENCE_KIND_COUNT = 10;

/// One thing an empire has against a suspect, with what it was worth **after** decay and caps (GDD §6).
///
/// **The weight is stored, not recomputed.** An accusation shows its working (R19, GDD §9's example), and working
/// that was recalculated at display time from a world that has since moved is a reconstruction rather than a reason.
/// The same argument `Explanation.h` makes about explanations applies one level down, to the items inside one.
///
/// **This is belief, and it lives in `Knowledge`** (ADR-021). `source` points at the report the item was read out of,
/// which is what lets a panel show the player the same sighting the empire acted on, with its age and its source's
/// track record — and it is invalid for an item that came from no single report, such as a prior pattern.
struct Evidence
{
  EvidenceKind kind;
  IncidentId incident;

  /// Who it is against. Exactly one is valid, the same shape as a `Suspicion`'s suspect.
  CompanyId suspectCompany;
  EmpireId suspectEmpire;

  /// What this item was worth here: the table's weight, decayed by distance and capped where §6 caps it. Signed --
  /// an alibi is −0.30 and a rival's denial is −0.10 (ADR-003).
  Neuron::Hundredths weight;

  /// The report it was read out of, or an invalid id when no single report produced it.
  ReportId source;

  /// When it was collected, which is not when the incident happened: evidence accumulates.
  Neuron::Tick tick;

  /// **Somebody put this here, and it stays** (NC-054).
  ///
  /// The §6 rows read off reports are recomputed from scratch every day, because the reports they are read from can
  /// change: a sighting is checked, a courier lands, a source's record moves. An answer to an accusation is not like
  /// that. A denial was *said*; a route was *submitted*; a lie was *exposed*. Recomputing those from reports would
  /// quietly delete them on the next daily pass, so `Inference::CollectEvidence` carries the standing rows forward
  /// instead of re-deriving them.
  bool standing;
};

} // namespace Nomad
