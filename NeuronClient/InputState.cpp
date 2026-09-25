// NeuronClient/InputState.cpp
#include "pch.h"
#include "InputState.h"
#include "Debug.h"

namespace Neuron
{

namespace
{

/// One wheel detent, as Windows reports it. WHEEL_DELTA is 120; the desk wants notches, not raw units.
constexpr std::int32_t WHEEL_UNITS_PER_NOTCH = WHEEL_DELTA;

[[nodiscard]] std::size_t ButtonIndex(MouseButton _button) noexcept
{
  return static_cast<std::size_t>(_button);
}

/// The low and high words of an lParam carrying a client-space point. Signed, because a captured drag reports
/// positions outside the window and GET_X_LPARAM's cast is the only thing that makes those negative rather than
/// enormous.
[[nodiscard]] MousePoint PointFromLParam(LPARAM _lparam) noexcept
{
  return MousePoint{static_cast<std::int32_t>(static_cast<std::int16_t>(LOWORD(_lparam))),
                    static_cast<std::int32_t>(static_cast<std::int16_t>(HIWORD(_lparam)))};
}

} // namespace

bool InputState::MessageSink(void* _context, UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept
{
  if (_context == nullptr)
  {
    return false;
  }
  return static_cast<InputState*>(_context)->HandleMessage(_message, _wparam, _lparam);
}

void InputState::SetScenePlacement(const PresentPass::Placement& _placement, std::uint32_t _sceneWidthPixels,
                                   std::uint32_t _sceneHeightPixels) noexcept
{
  // Fit returns an empty placement only for an empty client area or scene, which Window::Create and SceneTarget::Create
  // both refuse. An empty one here would leave nothing to point at, and ToScene would treat it as unplaced.
  NOMAD_ASSERT(_placement.widthPixels != 0 && _placement.heightPixels != 0 && _sceneWidthPixels != 0 && _sceneHeightPixels != 0);
  m_scenePlacement = _placement;
  m_sceneWidthPixels = _sceneWidthPixels;
  m_sceneHeightPixels = _sceneHeightPixels;
}

MousePoint InputState::ToScene(MousePoint _client) const noexcept
{
  if (m_scenePlacement.widthPixels == 0 || m_scenePlacement.heightPixels == 0)
  {
    return _client;
  }
  return MousePoint{
    PresentPass::ScenePixelUnder(_client.xPixels, m_scenePlacement.leftPixels, m_scenePlacement.widthPixels, m_sceneWidthPixels),
    PresentPass::ScenePixelUnder(_client.yPixels, m_scenePlacement.topPixels, m_scenePlacement.heightPixels, m_sceneHeightPixels)};
}

void InputState::BeginFrame() noexcept
{
  m_mousePressed.fill(false);
  m_mouseReleased.fill(false);
  m_keyPressed.fill(false);
  m_keyReleased.fill(false);
  m_wheelDelta = 0;
  m_mouseDelta = MousePoint{0, 0};
  m_typedCount = 0;
  m_typingOverflowed = false;
}

void InputState::ReleaseEverything() noexcept
{
  for (std::size_t index = 0; index < MOUSE_BUTTON_COUNT; ++index)
  {
    if (m_mouseDown[index])
    {
      m_mouseDown[index] = false;
      m_mouseReleased[index] = true;
    }
  }
  for (std::size_t key = 0; key < KEY_COUNT; ++key)
  {
    if (m_keyDown[key])
    {
      m_keyDown[key] = false;
      m_keyReleased[key] = true;
    }
  }
}

bool InputState::HandleMessage(UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept
{
  switch (_message)
  {
  case WM_MOUSEMOVE:
  {
    // Into the scene before anything is stored, so the delta below is in scene pixels too and no reader of this class
    // ever holds a client-space point (NC-033).
    const MousePoint position = ToScene(PointFromLParam(_lparam));
    if (m_haveMousePosition)
    {
      m_mouseDelta.xPixels += position.xPixels - m_mouse.xPixels;
      m_mouseDelta.yPixels += position.yPixels - m_mouse.yPixels;
    }
    m_mouse = position;
    m_haveMousePosition = true;
    return true;
  }

  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN:
  {
    const MouseButton button = _message == WM_LBUTTONDOWN   ? MouseButton::Left
                               : _message == WM_RBUTTONDOWN ? MouseButton::Right
                                                            : MouseButton::Middle;
    const std::size_t index = ButtonIndex(button);
    m_mouse = ToScene(PointFromLParam(_lparam));
    m_haveMousePosition = true;
    // The edge is set by the message, not by comparing frames, so a click and a release inside one frame both land.
    m_mousePressed[index] = true;
    m_mouseDown[index] = true;
    return true;
  }

  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
  {
    const MouseButton button = _message == WM_LBUTTONUP   ? MouseButton::Left
                               : _message == WM_RBUTTONUP ? MouseButton::Right
                                                          : MouseButton::Middle;
    const std::size_t index = ButtonIndex(button);
    m_mouse = ToScene(PointFromLParam(_lparam));
    m_haveMousePosition = true;
    m_mouseReleased[index] = true;
    m_mouseDown[index] = false;
    return true;
  }

  case WM_MOUSEWHEEL:
    // The position in a wheel message is in SCREEN coordinates, unlike every other mouse message. The conversion is
    // the window's to make, so this takes only the movement and leaves the position alone.
    m_wheelDelta += static_cast<std::int32_t>(GET_WHEEL_DELTA_WPARAM(_wparam)) / WHEEL_UNITS_PER_NOTCH;
    return true;

  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
  {
    const std::size_t key = static_cast<std::size_t>(_wparam) & 0xFFu;
    // Bit 30 of lParam is set when the key was already down: this is the hardware repeating, not a new press. The
    // down state is already true, and a repeat must not read as a fresh edge.
    const bool repeat = (_lparam & (1 << 30)) != 0;
    if (!repeat)
    {
      m_keyPressed[key] = true;
    }
    m_keyDown[key] = true;
    return true;
  }

  case WM_KEYUP:
  case WM_SYSKEYUP:
  {
    const std::size_t key = static_cast<std::size_t>(_wparam) & 0xFFu;
    m_keyReleased[key] = true;
    m_keyDown[key] = false;
    return true;
  }

  case WM_CHAR:
    if (m_typedCount < MAX_TYPED_CHARACTERS)
    {
      m_typed[m_typedCount] = static_cast<char16_t>(_wparam);
      ++m_typedCount;
    }
    else
    {
      m_typingOverflowed = true;
    }
    return true;

  case WM_KILLFOCUS:
    m_hasFocus = false;
    ReleaseEverything();
    return true;

  case WM_SETFOCUS:
    m_hasFocus = true;
    return true;

  default:
    return false;
  }
}

bool InputState::MouseDown(MouseButton _button) const noexcept
{
  return m_mouseDown[ButtonIndex(_button)];
}

bool InputState::MousePressed(MouseButton _button) const noexcept
{
  return m_mousePressed[ButtonIndex(_button)];
}

bool InputState::MouseReleased(MouseButton _button) const noexcept
{
  return m_mouseReleased[ButtonIndex(_button)];
}

bool InputState::KeyDown(std::uint8_t _virtualKey) const noexcept
{
  return m_keyDown[_virtualKey];
}

bool InputState::KeyPressed(std::uint8_t _virtualKey) const noexcept
{
  return m_keyPressed[_virtualKey];
}

bool InputState::KeyReleased(std::uint8_t _virtualKey) const noexcept
{
  return m_keyReleased[_virtualKey];
}

std::u16string_view InputState::TypedCharacters() const noexcept
{
  return std::u16string_view{m_typed.data(), m_typedCount};
}

} // namespace Neuron
