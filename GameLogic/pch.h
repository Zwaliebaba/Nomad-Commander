// GameLogic/pch.h: the precompiled header. Include it first in every .cpp of this project (AGENTS.md §4).
//
// No Windows header, on purpose: the simulation is portable C++ over NeuronCore's pure headers, and nothing in it
// reads a clock, a file or a socket (AGENTS.md R16, R21). Debug.h is pure; NeuronCore.h is not and is never
// included here.
#pragma once

#include "Debug.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
