// GameLogic/Credits.h
#pragma once

#include <cstdint>

namespace Nomad
{

/// The currency (GDD §5): contracts, trade and loot pay in it; hulls, fuel, officers and intelligence cost it.
///
/// A whole credit is the smallest unit the design names, so this is an integer and not a fixed point -- Hundredths is
/// for fractions of a whole and a credit *is* the whole (ADR-003, decision 5). Sixty-four bits because a treasury, a
/// contract and a cargo's value are added together and ADR-003 carries every intermediate product in that width.
///
/// It is signed, and deliberately: GDD §5 has upkeep paid out of an empty treasury before hulls are mothballed, so the
/// balance crossing zero is a state the simulation passes through rather than an error. Nothing here saturates on its
/// own; arithmetic that could overflow goes through NeuronCore's SaturatingAdd and SaturatingSub, which already take
/// this width and carry ADR-003's assert-in-Debug, saturate-in-Release rule.
using Credits = std::int64_t;

inline constexpr Credits CREDITS_ZERO = 0;

} // namespace Nomad
