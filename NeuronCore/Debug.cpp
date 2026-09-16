// NeuronCore/Debug.cpp
#include "pch.h"
#include <intrin.h>
#include "Debug.h"
#include <cstdio>
#include <cstdlib>

namespace Neuron
{

namespace
{

void DefaultAssertHandler(const char* _expression, const char* _file, int _line) noexcept
{
  char line[1024];
  std::snprintf(line, sizeof line, "%s(%d): assertion failed: %s\n", _file, _line, _expression);
  OutputDebugStringA(line);
  if (IsDebuggerPresent() != FALSE)
  {
    __debugbreak();
    return;
  }
  std::abort();
}

// AGENTS.md R1: file-scope state is g_ in an anonymous namespace. Not thread-safe by contract (Debug.h).
AssertHandler g_assertHandler = DefaultAssertHandler;

} // namespace

AssertHandler SetAssertHandler(AssertHandler _handler) noexcept
{
  const AssertHandler previous = g_assertHandler;
  g_assertHandler = _handler != nullptr ? _handler : DefaultAssertHandler;
  return previous;
}

void AssertFailed(const char* _expression, const char* _file, int _line) noexcept
{
  g_assertHandler(_expression, _file, _line);
}

void DebugPrint(const char* _text) noexcept
{
  OutputDebugStringA(_text);
  OutputDebugStringA("\n");
}

} // namespace Neuron
