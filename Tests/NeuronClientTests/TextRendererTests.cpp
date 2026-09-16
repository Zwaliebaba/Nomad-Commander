// Tests/NeuronClientTests/TextRendererTests.cpp
#include "pch.h"
#include "BitmapFont.h"
#include "GlyphPipeline.h"
#include "GraphicsDevice.h"
#include "SceneTarget.h"
#include "TextRenderer.h"
#include "Window.h"
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t BACKGROUND = 0xFF000000u;
constexpr std::uint32_t INK = 0xFFFFFFFFu;

/// A device, a scene target cleared to black, the glyph pipeline and a text renderer.
class TextFixture
{
public:
  [[nodiscard]] bool Create()
  {
    const Neuron::GraphicsDevice::Desc deviceDesc{true, true};
    if (!Neuron::GraphicsDevice::Create(deviceDesc, m_device))
    {
      m_why = L"the WARP device could not be created";
      return false;
    }
    const Neuron::SceneTarget::Desc sceneDesc{
      Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, DXGI_FORMAT_R8G8B8A8_UNORM, {0.0f, 0.0f, 0.0f, 1.0f}};
    if (!Neuron::SceneTarget::Create(m_device, sceneDesc, m_scene))
    {
      m_why = L"the scene target could not be created";
      return false;
    }
    if (!Neuron::GlyphPipeline::Create(m_device, m_pipeline))
    {
      m_why = L"the glyph pipeline could not be created";
      return false;
    }
    if (!Neuron::TextRenderer::Create(m_device, m_pipeline, m_text))
    {
      m_why = L"the text renderer could not be created (the atlas upload is the likely half)";
      return false;
    }
    return true;
  }

  template <typename DrawFn> [[nodiscard]] bool DrawAndReadBack(DrawFn _draw, std::vector<std::uint32_t>& _outPixels)
  {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(m_device.Device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(
          m_device.Device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList))))
    {
      return false;
    }
    m_scene.Transition(commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_scene.Clear(commandList.Get());
    const D3D12_CPU_DESCRIPTOR_HANDLE view = m_scene.RenderTargetView();
    commandList->OMSetRenderTargets(1, &view, FALSE, nullptr);

    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<FLOAT>(m_scene.WidthPixels()), static_cast<FLOAT>(m_scene.HeightPixels()),
                                  0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_scene.WidthPixels()), static_cast<LONG>(m_scene.HeightPixels())};
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    m_text.Begin(commandList.Get(), m_pipeline, 0, m_scene.WidthPixels(), m_scene.HeightPixels());
    _draw(m_text);
    m_text.End();

    if (FAILED(commandList->Close()))
    {
      return false;
    }
    ID3D12CommandList* const lists[] = {commandList.Get()};
    m_device.Queue()->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    if (FAILED(m_device.Device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
    {
      return false;
    }
    const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr)
    {
      return false;
    }
    bool waited = false;
    if (SUCCEEDED(m_device.Queue()->Signal(fence.Get(), 1)) && SUCCEEDED(fence->SetEventOnCompletion(1, event)))
    {
      waited = WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;
    }
    CloseHandle(event);
    return waited && m_scene.ReadBack(_outPixels);
  }

  [[nodiscard]] const wchar_t* Why() const noexcept
  {
    return m_why;
  }

  [[nodiscard]] Neuron::TextRenderer& Text() noexcept
  {
    return m_text;
  }

private:
  Neuron::GraphicsDevice m_device;
  Neuron::SceneTarget m_scene;
  Neuron::GlyphPipeline m_pipeline;
  Neuron::TextRenderer m_text;
  const wchar_t* m_why = L"";
};

[[nodiscard]] std::size_t PixelIndex(std::uint32_t _x, std::uint32_t _y)
{
  return static_cast<std::size_t>(_y) * Neuron::SCREEN_WIDTH_PIXELS + _x;
}

/// The eight bytes of a codepoint's glyph, straight out of the font array.
[[nodiscard]] const std::uint8_t* GlyphBits(std::uint32_t _codepoint)
{
  const std::uint32_t index = _codepoint - Neuron::FONT_FIRST_CODEPOINT;
  return &Neuron::FONT_8X8_GLYPHS[static_cast<std::size_t>(index) * Neuron::FONT_GLYPH_BYTES];
}

/// Compares an 8x8 cell of the readback against a glyph's bits at scale 1. Returns the number of pixels that differ,
/// and names the first, because "one pixel wrong" is the failure worth reading.
[[nodiscard]] std::size_t CompareGlyph(const std::vector<std::uint32_t>& _pixels, std::uint32_t _originX, std::uint32_t _originY,
                                       std::uint32_t _codepoint, std::wstring& _outFirst)
{
  const std::uint8_t* const bits = GlyphBits(_codepoint);
  std::size_t wrong = 0;
  for (std::uint32_t row = 0; row < Neuron::TextRenderer::GLYPH_HEIGHT_PIXELS; ++row)
  {
    for (std::uint32_t column = 0; column < Neuron::TextRenderer::GLYPH_WIDTH_PIXELS; ++column)
    {
      const bool set = ((bits[row] >> (7 - column)) & 1u) != 0;
      const std::uint32_t expected = set ? INK : BACKGROUND;
      const std::uint32_t actual = _pixels[PixelIndex(_originX + column, _originY + row)];
      if (actual != expected)
      {
        if (wrong == 0)
        {
          _outFirst = L"codepoint 0x" + std::to_wstring(_codepoint) + L" row " + std::to_wstring(row) + L" column " +
                      std::to_wstring(column) + L": expected " + (set ? L"ink" : L"background");
        }
        ++wrong;
      }
    }
  }
  return wrong;
}

} // namespace

TEST_CLASS(TextRendererTests)
{
public:
  TEST_METHOD(TheLetterAMatchesItsGlyphBitsExactly)
  {
    // The task's own case: "A" at (0, 0), scale 1, read back against the eight bytes it was authored as.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack([](Neuron::TextRenderer& _text) { _text.Draw(0.0f, 0.0f, "A", INK, 1); }, pixels),
                   L"the draw could not be submitted");

    std::wstring first;
    const std::size_t wrong = CompareGlyph(pixels, 0, 0, 'A', first);
    Assert::AreEqual(std::size_t{0}, wrong, first.c_str());
  }

  TEST_METHOD(EveryPrintableCharacterMatchesItsGlyphBits)
  {
    // The whole font, not a sample of it: all 96 glyphs drawn at scale 1 and compared bit for bit. This is what
    // catches an atlas laid out wrong, an off-by-one in the codepoint index, or a row pitch mishandled on upload —
    // none of which a single letter would show.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::string everything;
    for (std::uint32_t codepoint = Neuron::FONT_FIRST_CODEPOINT; codepoint < Neuron::FONT_FIRST_CODEPOINT + Neuron::FONT_GLYPH_COUNT;
         ++codepoint)
    {
      everything.push_back(static_cast<char>(codepoint));
    }

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(
      fixture.DrawAndReadBack([&everything](Neuron::TextRenderer& _text) { _text.Draw(0.0f, 0.0f, everything, INK, 1); }, pixels),
      L"the draw could not be submitted");
    Assert::AreEqual(Neuron::FONT_GLYPH_COUNT, fixture.Text().GlyphCount());

    std::size_t totalWrong = 0;
    std::wstring first;
    for (std::uint32_t index = 0; index < Neuron::FONT_GLYPH_COUNT; ++index)
    {
      std::wstring thisFirst;
      const std::size_t wrong =
        CompareGlyph(pixels, index * Neuron::TextRenderer::GLYPH_WIDTH_PIXELS, 0, Neuron::FONT_FIRST_CODEPOINT + index, thisFirst);
      if (wrong != 0 && first.empty())
      {
        first = thisFirst;
      }
      totalWrong += wrong;
    }
    Assert::AreEqual(std::size_t{0}, totalWrong,
                     (L"pixels wrong across the whole font: " + std::to_wstring(totalWrong) + L"; first at " + first).c_str());
  }

  TEST_METHOD(ScalingIsExactlyIntegerSoATexelIsASquareBlock)
  {
    // The point of Load over a sampler. At scale 3 every texel must be a 3x3 block of identical pixels: no blending
    // at the edges, no half-lit pixel anywhere. A sampler would put one in, which is what AGENTS.md §5 warns about.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    constexpr std::uint32_t SCALE = 3;
    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack([](Neuron::TextRenderer& _text) { _text.Draw(0.0f, 0.0f, "A", INK, SCALE); }, pixels),
                   L"the draw could not be submitted");

    const std::uint8_t* const bits = GlyphBits('A');
    for (std::uint32_t row = 0; row < Neuron::TextRenderer::GLYPH_HEIGHT_PIXELS; ++row)
    {
      for (std::uint32_t column = 0; column < Neuron::TextRenderer::GLYPH_WIDTH_PIXELS; ++column)
      {
        const bool set = ((bits[row] >> (7 - column)) & 1u) != 0;
        const std::uint32_t expected = set ? INK : BACKGROUND;
        for (std::uint32_t subY = 0; subY < SCALE; ++subY)
        {
          for (std::uint32_t subX = 0; subX < SCALE; ++subX)
          {
            const std::uint32_t x = column * SCALE + subX;
            const std::uint32_t y = row * SCALE + subY;
            Assert::AreEqual(expected, pixels[PixelIndex(x, y)],
                             (L"texel (" + std::to_wstring(column) + L"," + std::to_wstring(row) + L") sub-pixel (" +
                              std::to_wstring(subX) + L"," + std::to_wstring(subY) + L") is not a solid block")
                               .c_str());
          }
        }
      }
    }
  }

  TEST_METHOD(AnUnknownCharacterDrawsTheReplacementBox)
  {
    // A character this font does not have draws 0x7F, the filled box, so a gap in the data is loud on screen.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack(
                     [](Neuron::TextRenderer& _text)
                     {
                       const char unknown[] = {'\x01', '\0'};
                       _text.Draw(0.0f, 0.0f, unknown, INK, 1);
                     },
                     pixels),
                   L"the draw could not be submitted");

    std::wstring first;
    Assert::AreEqual(std::size_t{0}, CompareGlyph(pixels, 0, 0, 0x7F, first), first.c_str());
  }

  TEST_METHOD(MeasureAgreesWithWhatDrawCovers)
  {
    // NC-025 lays text out with Measure and then draws it; if the two disagree, every panel is wrong by a character.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    const std::string_view sample = "Kessel";
    constexpr std::uint32_t SCALE = 2;
    constexpr std::uint32_t ORIGIN_X = 40;
    constexpr std::uint32_t ORIGIN_Y = 24;
    const Neuron::TextExtent extent = Neuron::TextRenderer::Measure(sample, SCALE);

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack([sample](Neuron::TextRenderer& _text)
                                           { _text.Draw(static_cast<float>(ORIGIN_X), static_cast<float>(ORIGIN_Y), sample, INK, SCALE); },
                                           pixels),
                   L"the draw could not be submitted");

    // Nothing may be drawn outside the box Measure named, and the box must be tight enough to matter: every lit pixel
    // inside it, and at least one within a cell of each edge that carries ink.
    std::uint32_t left = Neuron::SCREEN_WIDTH_PIXELS;
    std::uint32_t top = Neuron::SCREEN_HEIGHT_PIXELS;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;
    std::size_t lit = 0;
    for (std::uint32_t y = 0; y < Neuron::SCREEN_HEIGHT_PIXELS; ++y)
    {
      for (std::uint32_t x = 0; x < Neuron::SCREEN_WIDTH_PIXELS; ++x)
      {
        if (pixels[PixelIndex(x, y)] == BACKGROUND)
        {
          continue;
        }
        ++lit;
        left = left < x ? left : x;
        top = top < y ? top : y;
        right = right > x ? right : x;
        bottom = bottom > y ? bottom : y;
      }
    }
    Assert::IsTrue(lit > 0, L"nothing was drawn");
    Assert::IsTrue(left >= ORIGIN_X, L"ink left of where Draw was asked to start");
    Assert::IsTrue(top >= ORIGIN_Y, L"ink above where Draw was asked to start");
    Assert::IsTrue(right < ORIGIN_X + extent.widthPixels, L"ink beyond the width Measure reported");
    Assert::IsTrue(bottom < ORIGIN_Y + extent.heightPixels, L"ink below the height Measure reported");
    Assert::AreEqual(6u * Neuron::TextRenderer::GLYPH_WIDTH_PIXELS * SCALE, extent.widthPixels);
    Assert::AreEqual(Neuron::TextRenderer::GLYPH_HEIGHT_PIXELS * SCALE, extent.heightPixels);
  }
};

} // namespace NeuronClientTests
