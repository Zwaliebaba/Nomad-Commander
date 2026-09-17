// GameLogic/Report.h
#pragma once

#include "EntityIds.h"
#include "Explanation.h"
#include "ShipClass.h"
#include "WireReport.h"

#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>
#include <variant>

namespace Nomad
{

/// Who said so (GDD §4: "the player's own sensors and pickets, a scout, a captured courier, a purchased tip, an
/// employer's briefing").
///
/// **All seven are declared now although only four can be produced yet**, because the order is the wire schema and
/// the store's (ADR-004): `CapturedCourier` is NC-053's, `PurchasedTip` and `EmployerBriefing` are NC-056's, and
/// inserting an enumerator later would renumber every save and every message. Append, never insert.
enum class ReportSource : std::uint8_t
{
  OwnSensors,
  Picket,
  Scout,
  CapturedCourier,
  PurchasedTip,
  EmployerBriefing,
  News
};

inline constexpr std::uint32_t REPORT_SOURCE_COUNT = 7;

/// What a source has been right and wrong about, and the whole of what its reliability is made of.
///
/// **This is the rule GDD §4 states and the one most easily broken by accident: "the reliability shown is the
/// source's track record, never the game's own knowledge of the truth."** A truthful source with a bad record reads
/// as unreliable, and a lucky liar reads as reliable, because neither the client nor an admiral is ever allowed to
/// ask the world whether a report happens to be correct. `Reliability` therefore takes no `World`, no `Report` and no
/// subject -- there is nothing it could cheat with.
struct SourceRecord
{
  /// **Both carry an initializer, unlike most aggregates in this tree**, and the reason is worth the two characters:
  /// a `Company` or an `Empire` built as `Company c;` rather than `Company c{}` leaves a plain POD member
  /// indeterminate, and an array of these is the first such member either type has had. A record that starts as
  /// garbage is a reliability that starts as garbage, and it is read on the very first report.
  std::uint32_t confirmed = 0;
  std::uint32_t contradicted = 0;

  /// What share of what this source said has held up. A source nobody has checked yet reads as
  /// `UNPROVEN_RELIABILITY` rather than as zero or as certainty: "nothing is known about this source" and "this
  /// source is wrong" are different things to put in front of a player.
  [[nodiscard]] Neuron::Hundredths Reliability() const noexcept;
};

/// What no track record yet says, in hundredths. Named here rather than in `Tuning.h` because it is the definition of
/// an unchecked source rather than a lever anybody tunes.
inline constexpr std::int32_t UNPROVEN_RELIABILITY_HUNDREDTHS = 50;

/// A fleet somebody saw, as they saw it.
///
/// **The counts are what the observer thinks it saw, not what is there** -- `Sensor` spreads them by distance -- and
/// `identityKnown` is false unless the fleet was marked or shared the observer's system (GDD §6). A client that wants
/// the truth behind one of these has nowhere to ask (R18).
struct SightedFleet
{
  /// Which fleet it actually was. **Not an admission of identity**: the id is how the simulation keeps two reports
  /// about one fleet together, and `identityKnown` is what decides whether anyone may be told whose it is.
  FleetId subject;

  ShipCounts countsSeen;

  /// Where it was seen. A fleet caught in a lane is reported at the end it left, because that is what an observer a
  /// jump away can tell (GDD §12: a lane is crossed, not occupied).
  SystemId atSystem;

  /// True when the observer could tell whose fleet it was: it was flying an empire's marks, or it was close enough to
  /// read (GDD §6, "identity only when marked or in the same system").
  bool identityKnown;

  /// Whether it was seen to be flying marks at all, which is a fact about the sighting and not about the fleet.
  bool marked;

  /// Set when the fleet was seen crossing a lane rather than standing at a system.
  bool inTransit;
};

/// Who received a report. The same two-alternative shape as `FleetOwner`, and for the same reason: an observer is
/// exactly one of these and the variant makes the other case unrepresentable rather than merely wrong.
using Observer = std::variant<EmpireId, CompanyId>;

/// One thing somebody was told (GDD §4).
///
/// **Source, age and reliability, and no truth field.** `observedAtTick` is what the age on a board is measured from
/// -- GDD §3's "nine hours old" is the difference between now and *that*, never the difference between now and when
/// the report landed -- and `deliveredAtTick` is when the observer could first act on it, because intelligence
/// travels (§4, §9: "rumours and orders move as physical couriers along the lanes").
///
/// NC-051 makes `sighting` one arm of a variant when incidents exist; a variant of one alternative would be ceremony
/// today and the schema version is what carries the change.
struct Report
{
  Neuron::Tick observedAtTick;
  Neuron::Tick deliveredAtTick;
  ReportSource source;
  Observer observer;
  SightedFleet sighting;

  /// The source's track record **as it stood when this was written**, so that a board can show what the reader was
  /// entitled to think at the time and a later correction does not rewrite history. It is a copy on purpose.
  Neuron::Hundredths reliabilityWhenWritten;

  /// Whether a later, closer sighting has been compared against this one yet. A report is checked at most once, so
  /// one bad long-range guess costs its source one mark rather than one a tick.
  bool checked;
};

/// How old a report is at a given tick, from the moment of observation and not from delivery (GDD §3).
[[nodiscard]] constexpr Neuron::Tick AgeTicks(const Report& _report, Neuron::Tick _now) noexcept
{
  return _now > _report.observedAtTick ? _now - _report.observedAtTick : 0;
}

/// Whether the observer may act on this yet. A report in flight exists in the world but has reached nobody.
[[nodiscard]] constexpr bool IsDelivered(const Report& _report, Neuron::Tick _now) noexcept
{
  return _report.deliveredAtTick <= _now;
}

/// One report as the client is told it (ADR-018). **The conversion lives here and not in `WireReport.h`**, because a
/// Wire header may not include a reality one (ADR-001) -- the same shape as `ToWire(const Input&)` in `Input.h`.
[[nodiscard]] inline WireReport ToWire(const Report& _report)
{
  WireReport wire{};
  wire.observedAtTick = _report.observedAtTick;
  wire.deliveredAtTick = _report.deliveredAtTick;
  wire.source = static_cast<std::uint8_t>(_report.source);
  const auto* empire = std::get_if<EmpireId>(&_report.observer);
  const auto* company = std::get_if<CompanyId>(&_report.observer);
  wire.observerEmpireIndex = empire != nullptr ? WireIndexOf(*empire) : WIRE_INDEX_NONE;
  wire.observerCompanyIndex = company != nullptr ? WireIndexOf(*company) : WIRE_INDEX_NONE;
  wire.subjectFleetIndex = WireIndexOf(_report.sighting.subject);
  wire.systemIndex = WireIndexOf(_report.sighting.atSystem);
  for (std::uint32_t index = 0; index < SHIP_CLASS_COUNT; ++index)
  {
    wire.countsSeen[index] = _report.sighting.countsSeen.byClass[index];
  }
  wire.identityKnown = _report.sighting.identityKnown;
  wire.marked = _report.sighting.marked;
  wire.inTransit = _report.sighting.inTransit;
  wire.reliability = _report.reliabilityWhenWritten;
  return wire;
}

} // namespace Nomad
