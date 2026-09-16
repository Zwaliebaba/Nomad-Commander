// NeuronClient/Palette.h
#pragma once

#include <cstdint>

namespace Neuron
{

/// A colour in the order the scene target stores one: red in the low byte, alpha in the high one. Written this way so
/// the constants below read digit for digit the same as the hex in the UI spec's table.
[[nodiscard]] constexpr std::uint32_t Rgb(std::uint8_t _red, std::uint8_t _green, std::uint8_t _blue) noexcept
{
  return 0xFF000000u | (static_cast<std::uint32_t>(_blue) << 16) | (static_cast<std::uint32_t>(_green) << 8) | _red;
}

/// The desk's colours, compiled in (AGENTS.md R13: no file beside the executable, so a palette is a header).
///
/// **The values are `Design/UI/UI-Spec.md` §2 and nothing here invents one.** The UI package is design context rather
/// than a design document, so where it and the GDD disagree the GDD wins — but on a palette the GDD says nothing and
/// that table is the whole of the answer.
///
/// **Dim is a colour, not an opacity** (UI §1). TEXT_DIM and TEXT_FAINT are what an older report or a disabled
/// control is drawn in; nothing in the desk is drawn at reduced alpha, and panels are opaque. Blending has been
/// available since 2026-09-16 and this palette still has no use for it.
///
/// A type rather than a nested namespace, because R9 gives this layer one namespace and `Neuron` is it.
struct Palette
{
  static constexpr std::uint32_t BACKGROUND = Rgb(0x0B, 0x0E, 0x13);        ///< the screen
  static constexpr std::uint32_t BACKGROUND_RAISED = Rgb(0x0F, 0x13, 0x19); ///< chrome, side panels
  static constexpr std::uint32_t PANEL = Rgb(0x14, 0x19, 0x22);             ///< cards, rows
  static constexpr std::uint32_t PANEL_SELECTED = Rgb(0x18, 0x1F, 0x2A);    ///< the selected row or reading
  static constexpr std::uint32_t PANEL_EDGE = Rgb(0x2A, 0x31, 0x40);        ///< borders, lanes at rest
  static constexpr std::uint32_t CHROME_EDGE = Rgb(0x23, 0x2A, 0x36);       ///< separators

  static constexpr std::uint32_t TEXT = Rgb(0xE6, 0xE1, 0xD6);       ///< primary
  static constexpr std::uint32_t TEXT_DIM = Rgb(0x7F, 0x87, 0x96);   ///< secondary, older reports
  static constexpr std::uint32_t TEXT_FAINT = Rgb(0x4A, 0x50, 0x5C); ///< disabled, uncovered

  /// The company: own fleets, the active tab, Confirm. Text drawn on an ACCENT fill is BACKGROUND (UI §2).
  static constexpr std::uint32_t ACCENT = Rgb(0xD9, 0xA4, 0x41);
  static constexpr std::uint32_t WARNING = Rgb(0xE0, 0x7A, 0x3A); ///< stakes, upkeep, hardening beliefs
  static constexpr std::uint32_t HOSTILE = Rgb(0xD9, 0x53, 0x4F); ///< an acting-against state

  /// One per empire slot. v0.1 has three (GDD §15), and the names are the scenario's (UI §2, canon 2026-09-16).
  static constexpr std::uint32_t EMPIRE_0_VARN = Rgb(0xC9, 0x52, 0x4A);
  static constexpr std::uint32_t EMPIRE_1_OREN = Rgb(0x4F, 0x9D, 0xD3);
  static constexpr std::uint32_t EMPIRE_2_SEDU = Rgb(0x7B, 0xB7, 0x65);
  static constexpr std::uint32_t NEUTRAL = Rgb(0x7F, 0x87, 0x96); ///< unclaimed systems

  /// Dim variants, for a report that has aged (UI §2). Only Varn's is given there; the others arrive with the screens
  /// that need them rather than being invented here.
  static constexpr std::uint32_t EMPIRE_0_VARN_DIM = Rgb(0x8A, 0x5A, 0x55);
  static constexpr std::uint32_t EMPIRE_0_VARN_DIM_EDGE = Rgb(0x5A, 0x3A, 0x36);
};

} // namespace Neuron
