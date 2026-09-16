// NeuronCore/Debug.h
#pragma once

// The assert and the debug print every project uses. This header includes nothing from Windows on purpose:
// GameLogic includes it, and GameLogic includes no Windows header (AGENTS.md R16). Debug.cpp does the Windows work.

namespace Neuron
{

/// What NOMAD_ASSERT calls when its condition is false. The default reports through the debugger's output stream,
/// breaks if a debugger is attached and aborts otherwise. A test suite installs its own with SetAssertHandler to
/// observe an assert without ending the process.
using AssertHandler = void (*)(const char* _expression, const char* _file, int _line);

/// Installs a handler and returns the previous one; nullptr restores the default. Not thread-safe by contract:
/// install it before anything else runs.
AssertHandler SetAssertHandler(AssertHandler _handler) noexcept;

/// Called by NOMAD_ASSERT. Routes to the installed handler.
void AssertFailed(const char* _expression, const char* _file, int _line) noexcept;

/// Writes one line to the debugger's output stream.
void DebugPrint(const char* _text) noexcept;

} // namespace Neuron

// NOMAD_ASSERT evaluates its condition in _DEBUG only; NOMAD_VERIFY evaluates it always and checks it in _DEBUG.
// The NDEBUG form of NOMAD_ASSERT references the expression without evaluating it, so a variable used only in an
// assert does not become C4189 under /W4 /WX.
#if defined(_DEBUG)
#   define NOMAD_ASSERT(expr) ((void)(!!(expr) || (::Neuron::AssertFailed(#expr, __FILE__, __LINE__), false)))
#   define NOMAD_VERIFY(expr) NOMAD_ASSERT(expr)
#else
#   define NOMAD_ASSERT(expr) ((void)(false && (expr)))
#   define NOMAD_VERIFY(expr) ((void)(expr))
#endif
