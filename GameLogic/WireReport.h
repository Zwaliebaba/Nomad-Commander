// GameLogic/WireReport.h
#pragma once

#include "WireExplanation.h"
#include "WireInput.h"

#include "ByteReader.h"
#include "ByteWriter.h"
#include "Hundredths.h"
#include "Tick.h"

#include <cstdint>

namespace Nomad
{

/// How many `ReportSource` values the wire knows, declared here rather than reached for across the seam: a Wire
/// header includes only NeuronCore and other Wire headers (ADR-001), for the same reason `WireInput.h` carries its
/// own ship-class count. `Sensor.cpp` static_asserts that this and `REPORT_SOURCE_COUNT` are the same number, which
/// is where the two would be caught drifting apart.
inline constexpr std::uint32_t WIRE_REPORT_SOURCE_COUNT = 7;

/// One report, as the client is told it (ADR-018, ADR-020).
///
/// **There is no truth field here and there is no way to add one without saying so.** The client draws a board from
/// these: who said it, when they saw it, what they thought they saw, and what that source's record is. Whether the
/// report is *correct* is not a property the client is ever given, because the player's advantage is interpretation
/// and not information (GDD §4, R18).
///
/// `subjectFleetIndex` travels because two reports about one fleet have to be recognisable as being about one fleet
/// -- a dossier is exactly that. It is **not** an admission of whose fleet it is: `identityKnown` is what decides
/// whether the client may name an owner, and when it is false the client has no owner index to name.
struct WireReport
{
  Neuron::Tick observedAtTick;
  Neuron::Tick deliveredAtTick;

  /// `ReportSource` as its raw value.
  std::uint8_t source;

  /// The observer, exactly one of which is set; the other is `WIRE_INDEX_NONE`.
  std::uint32_t observerEmpireIndex;
  std::uint32_t observerCompanyIndex;

  std::uint32_t subjectFleetIndex;
  std::uint32_t systemIndex;

  /// What the observer thought it saw, per `ShipClass`.
  std::uint32_t countsSeen[WIRE_SHIP_CLASS_COUNT];

  bool identityKnown;
  bool marked;
  bool inTransit;

  /// The source's track record when this was written (GDD §4), never a probability that the report is right.
  Neuron::Hundredths reliability;
};

/// How old the report is at a tick, from when it was *observed* (GDD §3: "nine hours old"). The client computes it
/// rather than being sent it, because the answer changes every tick and the report does not.
[[nodiscard]] constexpr Neuron::Tick AgeTicks(const WireReport& _report, Neuron::Tick _now) noexcept
{
  return _now > _report.observedAtTick ? _now - _report.observedAtTick : 0;
}

inline void Serialize(Neuron::ByteWriter& _writer, const WireReport& _report)
{
  _writer.WriteTick(_report.observedAtTick);
  _writer.WriteTick(_report.deliveredAtTick);
  _writer.Write(_report.source);
  _writer.Write(_report.observerEmpireIndex);
  _writer.Write(_report.observerCompanyIndex);
  _writer.Write(_report.subjectFleetIndex);
  _writer.Write(_report.systemIndex);
  for (const std::uint32_t count : _report.countsSeen)
  {
    _writer.Write(count);
  }
  _writer.WriteBool(_report.identityKnown);
  _writer.WriteBool(_report.marked);
  _writer.WriteBool(_report.inTransit);
  _writer.WriteHundredths(_report.reliability);
}

[[nodiscard]] inline bool Deserialize(Neuron::ByteReader& _reader, WireReport& _outReport)
{
  if (!_reader.ReadTick(_outReport.observedAtTick) || !_reader.ReadTick(_outReport.deliveredAtTick) || !_reader.Read(_outReport.source) ||
      _outReport.source >= WIRE_REPORT_SOURCE_COUNT || !_reader.Read(_outReport.observerEmpireIndex) ||
      !_reader.Read(_outReport.observerCompanyIndex) || !_reader.Read(_outReport.subjectFleetIndex) ||
      !_reader.Read(_outReport.systemIndex))
  {
    return false;
  }
  for (std::uint32_t& count : _outReport.countsSeen)
  {
    if (!_reader.Read(count))
    {
      return false;
    }
  }
  return _reader.ReadBool(_outReport.identityKnown) && _reader.ReadBool(_outReport.marked) && _reader.ReadBool(_outReport.inTransit) &&
         _reader.ReadHundredths(_outReport.reliability);
}

} // namespace Nomad
