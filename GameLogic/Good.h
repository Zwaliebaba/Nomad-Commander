// GameLogic/Good.h
#pragma once

#include <cstdint>

namespace Nomad
{

/// The four goods of v0.1 (GDD §10). Small and closed on purpose: the economy "exists to create situations and to
/// give the three playstyles different risks. It is not the game."
///
/// The order is the store's schema (ADR-004) and the index into every per-good array. Append, never insert.
enum class Good : std::uint8_t
{
  Fuel,
  Metals,
  Components,
  ConsumerGoods
};

inline constexpr std::uint32_t GOOD_COUNT = 4;

/// A quantity of each good: a system's stock, a hold's cargo, a day's flow.
struct Stock
{
  std::uint32_t byGood[GOOD_COUNT];

  [[nodiscard]] constexpr std::uint32_t Of(Good _good) const noexcept
  {
    return byGood[static_cast<std::uint32_t>(_good)];
  }

  constexpr void Add(Good _good, std::uint32_t _units) noexcept
  {
    byGood[static_cast<std::uint32_t>(_good)] += _units;
  }

  /// Removes at most what is there and answers how much went, so a caller that asked for more than the hold has finds
  /// out rather than wrapping. A stock that wrapped would be four billion units of fuel out of nowhere.
  constexpr std::uint32_t Remove(Good _good, std::uint32_t _units) noexcept
  {
    std::uint32_t& held = byGood[static_cast<std::uint32_t>(_good)];
    const std::uint32_t taken = _units < held ? _units : held;
    held -= taken;
    return taken;
  }

  [[nodiscard]] constexpr std::uint32_t Total() const noexcept
  {
    std::uint32_t total = 0;
    for (std::uint32_t index = 0; index < GOOD_COUNT; ++index)
    {
      total += byGood[index];
    }
    return total;
  }

  [[nodiscard]] constexpr bool operator==(const Stock&) const noexcept = default;
};

} // namespace Nomad
