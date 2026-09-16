// GameLogic/Input.h
#pragma once

#include "EntityIds.h"
#include "Explanation.h"
#include "WireInput.h"

#include "Tick.h"

namespace Nomad
{

/// A decision, reality-side, after the wire record has been validated (GDD §3, §4).
///
/// It is the same shape as `WireInput` with typed ids in place of indices, and that is deliberate rather than
/// redundant: `NomadSimulation::ApplyInput` is the one place an index becomes an id, and it is the one place that can
/// refuse an index that names nothing. Everything downstream of it holds a handle that is known to be good.
struct Input
{
  Neuron::Tick applyAtTick;
  InputKind kind;
  CompanyId company;

  Neuron::Tick activeWindowStartTickOfDay;
  Neuron::Tick activeWindowLengthTicks;
};

[[nodiscard]] inline WireInput ToWire(const Input& _input)
{
  return WireInput{_input.applyAtTick, _input.kind, WireIndexOf(_input.company), _input.activeWindowStartTickOfDay,
                   _input.activeWindowLengthTicks};
}

} // namespace Nomad
