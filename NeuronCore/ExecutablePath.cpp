// NeuronCore/ExecutablePath.cpp
#include "pch.h"
#include "ExecutablePath.h"

#include <vector>

namespace Neuron
{

std::wstring ExecutableDirectory()
{
  // GetModuleFileNameW truncates rather than telling you the length it wanted, and reports the truncation only
  // through GetLastError. So the buffer grows until the call fits, which is the documented way to use it.
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;)
  {
    const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0)
    {
      return std::wstring{};
    }
    if (written < buffer.size())
    {
      buffer.resize(written);
      break;
    }
    if (buffer.size() > std::size_t{64} * 1024)
    {
      // A path longer than this is not a path, and doubling forever is worse than giving up.
      return std::wstring{};
    }
    buffer.resize(buffer.size() * 2);
  }

  std::wstring path(buffer.begin(), buffer.end());
  const std::size_t separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos)
  {
    return std::wstring{};
  }
  // With the separator kept, so a caller appends a name without knowing which slash Windows used.
  return path.substr(0, separator + 1);
}

} // namespace Neuron
