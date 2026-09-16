// NeuronClient/Rect.h
#pragma once

#include "NeuronCore.h"

#include <cstdint>

namespace Neuron
{

/// The cell every layout snaps to: 24 pixels (UI §1), which is also the line every text face is baked to (ADR-016),
/// so a row of text is a cell tall and a Body character is half a cell wide. 1920/24 by 1080/24 is 80 by 45, and
/// every position in the UI spec is a whole number of these.
inline constexpr std::int32_t CELL_PIXELS = 24;
inline constexpr std::int32_t GRID_COLUMNS = 80;
inline constexpr std::int32_t GRID_ROWS = 45;

/// A rectangle in screen pixels, origin top left. Integers throughout, because UI §1 says nothing draws at a
/// non-integer position and a float here is how that rule would quietly stop being true.
///
/// A public aggregate, so plain fields (R8) — with the unit in the name (R6), because this type is never anything but
/// pixels and a reader who assumes cells would be wrong by a factor of 24.
struct Rect
{
  std::int32_t xPixels;
  std::int32_t yPixels;
  std::int32_t widthPixels;
  std::int32_t heightPixels;

  [[nodiscard]] constexpr std::int32_t Right() const noexcept
  {
    return xPixels + widthPixels;
  }

  [[nodiscard]] constexpr std::int32_t Bottom() const noexcept
  {
    return yPixels + heightPixels;
  }

  [[nodiscard]] constexpr bool IsEmpty() const noexcept
  {
    return widthPixels <= 0 || heightPixels <= 0;
  }

  /// Half-open on both axes: the rectangle covers [x, x + width) by [y, y + height), which is the same convention
  /// PrimitiveBatch::FillRect fills, so what a hit test claims and what the screen shows are the same pixels.
  [[nodiscard]] constexpr bool Contains(std::int32_t _xPixels, std::int32_t _yPixels) const noexcept
  {
    return _xPixels >= xPixels && _xPixels < Right() && _yPixels >= yPixels && _yPixels < Bottom();
  }

  /// The rectangle shrunk by the same amount on every side. A panel's 24-pixel padding is `Inset(CELL_PIXELS)`.
  [[nodiscard]] constexpr Rect Inset(std::int32_t _byPixels) const noexcept
  {
    return Rect{xPixels + _byPixels, yPixels + _byPixels, widthPixels - 2 * _byPixels, heightPixels - 2 * _byPixels};
  }

  // The four Split calls TAKE from this rectangle and return what they took, which is the layout idiom the desk
  // wants: `const Rect header = body.SplitTop(CELL_PIXELS * 2);` leaves `body` as what is left underneath. They
  // mutate on purpose; a const version that returned both halves would need an out parameter at every call site.

  [[nodiscard]] constexpr Rect SplitLeft(std::int32_t _pixels) noexcept
  {
    const std::int32_t taken = _pixels < widthPixels ? _pixels : widthPixels;
    const Rect piece{xPixels, yPixels, taken, heightPixels};
    xPixels += taken;
    widthPixels -= taken;
    return piece;
  }

  [[nodiscard]] constexpr Rect SplitRight(std::int32_t _pixels) noexcept
  {
    const std::int32_t taken = _pixels < widthPixels ? _pixels : widthPixels;
    widthPixels -= taken;
    return Rect{xPixels + widthPixels, yPixels, taken, heightPixels};
  }

  [[nodiscard]] constexpr Rect SplitTop(std::int32_t _pixels) noexcept
  {
    const std::int32_t taken = _pixels < heightPixels ? _pixels : heightPixels;
    const Rect piece{xPixels, yPixels, widthPixels, taken};
    yPixels += taken;
    heightPixels -= taken;
    return piece;
  }

  [[nodiscard]] constexpr Rect SplitBottom(std::int32_t _pixels) noexcept
  {
    const std::int32_t taken = _pixels < heightPixels ? _pixels : heightPixels;
    heightPixels -= taken;
    return Rect{xPixels, yPixels + heightPixels, widthPixels, taken};
  }

  /// A rectangle on the 24-pixel cell grid, in cells. `Cell(0, 0)` is the top-left cell; `Cell(2, 1, 6, 2)` is six
  /// cells wide and two tall starting at column 2, row 1.
  [[nodiscard]] static constexpr Rect Cell(std::int32_t _column, std::int32_t _row, std::int32_t _columnSpan = 1,
                                           std::int32_t _rowSpan = 1) noexcept
  {
    return Rect{_column * CELL_PIXELS, _row * CELL_PIXELS, _columnSpan * CELL_PIXELS, _rowSpan * CELL_PIXELS};
  }
};

} // namespace Neuron
