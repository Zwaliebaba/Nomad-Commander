// GameLogic/Table.h
#pragma once

#include "Debug.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Nomad
{

/// One entity table: a std::vector indexed by a typed id (`Plan/Roadmap.md` *Conventions*).
///
/// **Rows are never erased.** A destroyed fleet and a dead admiral keep theirs with `alive = false`, because the
/// record and the dossiers refer to them afterwards (GDD §8, §11) and an index that could be reused would make a
/// receipt name the wrong entity. So an id is a permanent handle and Add only ever appends.
///
/// **Iteration order is table order**, which is insertion order, which is fixed by the seed and the inputs. That is
/// what R16 asks for and it is the reason this is a vector and not an associative container: a simulation that
/// iterated an unordered map into the world would replay differently on another standard library.
///
/// The template implementation is in the header because R7 gives this tree no .inl files.
template <typename T, typename IdType> class Table
{
public:
  /// Appends a row and answers its permanent id.
  IdType Add(const T& _row)
  {
    const auto index = static_cast<std::uint32_t>(m_rows.size());
    m_rows.push_back(_row);
    return IdType::FromIndex(index);
  }

  /// The row an id names. An invalid or out-of-range id is a programming error rather than a recoverable one: the ids
  /// come from this table, so a bad one means a caller kept an id from another world or another table.
  [[nodiscard]] const T& Get(IdType _id) const
  {
    NOMAD_ASSERT(_id.IsValid() && _id.Index() < m_rows.size());
    return m_rows[_id.Index()];
  }

  [[nodiscard]] T& Get(IdType _id)
  {
    NOMAD_ASSERT(_id.IsValid() && _id.Index() < m_rows.size());
    return m_rows[_id.Index()];
  }

  [[nodiscard]] bool Holds(IdType _id) const noexcept
  {
    return _id.IsValid() && _id.Index() < m_rows.size();
  }

  [[nodiscard]] std::uint32_t Count() const noexcept
  {
    return static_cast<std::uint32_t>(m_rows.size());
  }

  /// The id of the row at a position, for a loop that walks the whole table.
  [[nodiscard]] IdType IdAt(std::uint32_t _index) const noexcept
  {
    return IdType::FromIndex(_index);
  }

  /// Every row, in table order, for a loop that walks the whole table.
  ///
  /// A span named Rows() rather than begin() and end() on the table itself: R1 makes every method PascalCase and
  /// clang-tidy enforces it tree-wide, while a range-for needs the lowercase pair. Handing out the span satisfies
  /// both -- `for (const Fleet& fleet : world.Fleets().Rows())` reads no worse and says which sequence it walks.
  [[nodiscard]] std::span<const T> Rows() const noexcept
  {
    return std::span<const T>{m_rows};
  }

  [[nodiscard]] std::span<T> Rows() noexcept
  {
    return std::span<T>{m_rows};
  }

  void Clear() noexcept
  {
    m_rows.clear();
  }

  /// Sizes the table for a Deserialize that has just read a count. The rows are then filled in order.
  void Resize(std::uint32_t _count)
  {
    m_rows.resize(_count);
  }

private:
  std::vector<T> m_rows;
};

} // namespace Nomad
