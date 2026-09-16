// NeuronCore/Id.h
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace Neuron
{

/// A typed index into one entity table (AGENTS.md §2, R22): Id<FleetTag> and Id<CompanyTag> are different types the
/// compiler keeps apart, and neither converts to or from an integer by accident. A table is a vector indexed by
/// Index(); a default-constructed Id is the invalid one. The game declares one alias per entity kind where that entity
/// lives (GameLogic), never here. R8: the sentinel is an invariant, so the index is m_.
template <typename Tag> class Id
{
public:
  static constexpr std::uint32_t INVALID_INDEX = 0xFFFFFFFFu;

  constexpr Id() noexcept = default;

  [[nodiscard]] static constexpr Id FromIndex(std::uint32_t _index) noexcept
  {
    return Id{_index};
  }

  [[nodiscard]] constexpr bool IsValid() const noexcept
  {
    return m_index != INVALID_INDEX;
  }

  [[nodiscard]] constexpr std::uint32_t Index() const noexcept
  {
    return m_index;
  }

  constexpr auto operator<=>(const Id&) const noexcept = default;

  /// The hasher, for a container that names it (`std::unordered_set<FleetId, FleetId::Hash>`). A default std::hash
  /// is a full explicit specialization per alias, written where the alias is: `template <> struct std::hash<FleetId>
  /// : FleetId::Hash {};`. Not a partial specialization here: clang-tidy's bugprone-std-namespace-modification treats
  /// one as a modification of namespace std, and R16 keeps unordered containers out of the simulation anyway.
  struct Hash
  {
    [[nodiscard]] std::size_t operator()(Id _id) const noexcept
    {
      return std::hash<std::uint32_t>{}(_id.m_index);
    }
  };

private:
  constexpr explicit Id(std::uint32_t _index) noexcept
    : m_index(_index)
  {
  }

  std::uint32_t m_index = INVALID_INDEX;
};

} // namespace Neuron
