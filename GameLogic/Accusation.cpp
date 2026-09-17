// GameLogic/Accusation.cpp
#include "pch.h"
#include "Accusation.h"

#include "Explanation.h"

namespace Nomad
{

WireAccusation ToWire(const Accusation& _accusation, const std::vector<WireEvidenceLine>& _for,
                      const std::vector<WireEvidenceLine>& _against)
{
  WireAccusation wire{};
  wire.incidentIndex = WireIndexOf(_accusation.incident);
  wire.accuserEmpireIndex = WireIndexOf(_accusation.accuser);
  wire.suspectEmpireIndex = WireIndexOf(_accusation.suspectEmpire);
  wire.suspectCompanyIndex = WireIndexOf(_accusation.suspectCompany);
  wire.confidence = _accusation.confidence;
  wire.evidenceFor = _for;
  wire.evidenceAgainst = _against;
  wire.issuedAtTick = _accusation.issuedAtTick;
  wire.actedAtTick = _accusation.actedAtTick;
  return wire;
}

} // namespace Nomad
