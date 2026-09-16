// NeuronServer/ServerSmoke.cpp
//
// The placeholder that keeps NeuronServer.lib from being an archive with no public symbol until NC-030 lands
// Session.cpp: lib.exe reports LNK4221 for an object that defines none, and /warnaserror makes that fatal.
// Delete this file when the first real translation unit arrives, never before (the SuiteSmoke rule, AGENTS.md §3).
#include "pch.h"

namespace Neuron
{

bool ServerSmoke() noexcept
{
  return true;
}

} // namespace Neuron
