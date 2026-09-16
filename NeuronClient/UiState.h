// NeuronClient/UiState.h
#pragma once

#include "NeuronCore.h"

#include <cstdint>

namespace Neuron
{

/// The small amount of state a caller keeps between frames on behalf of a widget.
///
/// Immediate mode means `Ui` holds three ids and nothing else (ADR-012), so a scroll position or a caret belongs to
/// the *screen* that draws the list, not to the list. These types are what a screen declares to hold it — one line
/// per widget that needs remembering, and a `static` in the screen's own function is the usual place for it.
struct ScrollState
{
  /// How far down the list is scrolled, in pixels. Clamped by the widget every frame, so a list that shrinks does not
  /// leave the view past its end.
  std::int32_t offsetPixels = 0;

  /// Set while a drag of the scrollbar's thumb is in progress, with where in the thumb the drag started, so the thumb
  /// does not jump to the cursor on the first frame.
  std::int32_t dragGrabPixels = 0;
  bool dragging = false;
};

/// What a text field remembers: the digits typed so far, and whether it is being edited. The value itself belongs to
/// the caller — the field parses into it — because the desk's numbers live in the model, not in the widget.
struct FieldState
{
  static constexpr std::size_t MAX_DIGITS = 12;

  char digits[MAX_DIGITS + 1] = {};
  std::uint8_t length = 0;

  /// True while the caller's value and the typed digits may disagree, which is the whole of an edit: the field shows
  /// what is being typed and writes it back when the edit ends.
  bool editing = false;
};

} // namespace Neuron
