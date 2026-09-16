// NeuronClient/IconAtlas.h
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Neuron
{

// The desk's icons, embedded exactly as the font is (AGENTS.md R13: art is compiled in, never loaded). Eight by
// eight, one bit a pixel, most significant bit leftmost. They share the text atlas, its pipeline, its Load() and its
// complete absence of a sampler: a set bit becomes a cell-sized block of full coverage (three texels a bit since
// ADR-016), so an icon stays the pixel art it was drawn as. An icon is monochrome and is tinted at the call site.
//
// **Every icon names the line of the design that needs it** (NC-026's note: a widget -- or an icon -- with no line is
// one the desk does not need). They accompany a label and never replace one: the desk is read, not scanned (UI §4).
//
// Authored for this repository, like the font, so there is no licence to honour.
enum class Icon : std::uint8_t
{
  Report,
  Courier,
  Fleet,
  Accusation,
  Offer,
  Market,
  Contract,
  Time,
  Credits,
  Warning,
  Selected,
  Uncovered,
};

inline constexpr std::uint32_t ICON_COUNT = 12;
/// An icon's art is eight bits square: one byte a row, eight rows.
inline constexpr std::uint32_t ICON_ART_PIXELS = 8;
inline constexpr std::uint32_t ICON_BYTES = ICON_ART_PIXELS;
inline constexpr std::size_t ICON_TOTAL_BYTES = static_cast<std::size_t>(ICON_COUNT) * ICON_BYTES;

inline constexpr std::array<std::uint8_t, ICON_TOTAL_BYTES> ICON_8X8_ART = {
  // Report -- UI §3 board item and §5 side panel: REPORTS | newest first
  0b11111111,
  0b10000001,
  0b10111101,
  0b10000001,
  0b10111101,
  0b10000001,
  0b10110001,
  0b11111111,
  // Courier -- GDD §4 courier-carried orders; UI §6 `orders after` on the projection strip
  0b00000000,
  0b00111110,
  0b01100011,
  0b11011101,
  0b11011101,
  0b01100011,
  0b00111110,
  0b00000000,
  // Fleet -- UI §5 own fleets on the map; §7 wing counts
  0b00011000,
  0b00111100,
  0b01111110,
  0b11111111,
  0b00111100,
  0b01100110,
  0b11000011,
  0b00000000,
  // Accusation -- GDD §6 accusation and its answer; UI §3 board item kind ACCUSATION
  0b00011000,
  0b00111100,
  0b00111100,
  0b00111100,
  0b00011000,
  0b00000000,
  0b00011000,
  0b00000000,
  // Offer -- UI §3 board item kind OFFER; GDD §10 contract offers
  0b00000000,
  0b11111111,
  0b11000011,
  0b11011011,
  0b11000011,
  0b11000011,
  0b11111111,
  0b00000000,
  // Market -- UI §3 board item kind MARKET; GDD §10 the economy
  0b00000011,
  0b00000111,
  0b00011100,
  0b00111000,
  0b11110000,
  0b11100000,
  0b00000000,
  0b11111111,
  // Contract -- GDD §10 escort and raid contracts; UI §3 the Contracts tab
  0b01111110,
  0b01000010,
  0b01011010,
  0b01000010,
  0b01011010,
  0b01000010,
  0b01111110,
  0b00000000,
  // Time -- UI §1 status line: simulated day and time; every `time left` column
  0b00111100,
  0b01100110,
  0b11001011,
  0b11001011,
  0b11001110,
  0b01100110,
  0b00111100,
  0b00000000,
  // Credits -- UI §1 status line: credits and upkeep per day
  0b00011000,
  0b00111100,
  0b01100110,
  0b01100000,
  0b01100110,
  0b00111100,
  0b00011000,
  0b00000000,
  // Warning -- UI §1: WARNING for a deadline under a day or a hardening belief
  0b00011000,
  0b00011000,
  0b00111100,
  0b00111100,
  0b01100110,
  0b01111110,
  0b11111111,
  0b00000000,
  // Selected -- UI §1 the selected row; §4 a covered trigger
  0b00000000,
  0b00000011,
  0b00000110,
  0b11001100,
  0b01111110,
  0b00111000,
  0b00010000,
  0b00000000,
  // Uncovered -- UI §6 `UNCOVERED | what you are willing not to plan for`, in TEXT_FAINT
  0b00000000,
  0b11000011,
  0b01100110,
  0b00111100,
  0b00111100,
  0b01100110,
  0b11000011,
  0b00000000,
};

} // namespace Neuron
