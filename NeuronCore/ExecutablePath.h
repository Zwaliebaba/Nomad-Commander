// NeuronCore/ExecutablePath.h
#pragma once

#include "NeuronCore.h"

#include <string>

namespace Neuron
{

/// The directory the running executable is in, with a trailing separator.
///
/// **This exists because R13 says a path a host writes resolves beside the executable, not against the working
/// directory.** A log or a store written relative to wherever the process happened to be launched from is one that
/// silently goes somewhere nobody looks — and the working directory is not the developer's to control: a shortcut, a
/// debugger, a test runner and Explorer all pick different ones.
///
/// It lives in NeuronCore because the executable and NeuronServer both need it. **GameLogic never includes it**: the
/// simulation reads no clock and opens no file (R16, R13), and this header brings in `<windows.h>`.
///
/// Returns an empty string only if Windows will not say, which it does not do in practice; a caller that gets one
/// should fail rather than fall back to the working directory, because falling back is the defect.
[[nodiscard]] std::wstring ExecutableDirectory();

} // namespace Neuron
