// GameLogic/GameLogicSmoke.cpp
//
// The placeholder that keeps GameLogic.lib from being an archive with no public symbol until NC-040 lands World.cpp:
// lib.exe reports LNK4221 for an object that defines none, and /warnaserror makes that fatal. Delete this file when
// the first real translation unit arrives, never before (the SuiteSmoke rule, AGENTS.md §3).
#include "pch.h"

namespace Nomad
{

bool GameLogicSmoke() noexcept
{
  return true;
}

} // namespace Nomad
