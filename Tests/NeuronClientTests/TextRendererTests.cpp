// Tests/NeuronClientTests/TextRendererTests.cpp
#include "pch.h"
#include "Font.h"
#include "GlyphPipeline.h"
#include "GraphicsDevice.h"
#include "IconAtlas.h"
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
/// A colour with three different channels, so a blend that mixed them up would show. Red in the low byte.
constexpr std::uint32_t AMBER = 0xFF2040C0u;

/// The faces, in the order the atlas stacks them, so a test can walk them.
constexpr Neuron::Font FACES[] = {Neuron::Font::Body, Neuron::Font::Small, Neuron::Font::Title};
constexpr std::uint32_t FACE_COUNT = 3;

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

/// The baked coverage of one texel of a glyph, straight from the face's bytes through the same metrics the renderer
/// uses.
[[nodiscard]] std::uint8_t Coverage(Neuron::Font _font, std::uint32_t _codepoint, std::uint32_t _column, std::uint32_t _row)
{
  const Neuron::FontMetrics metrics = Neuron::MetricsOf(_font);
  const std::size_t index = _codepoint - Neuron::FONT_FIRST_CODEPOINT;
  return metrics.coverage[(index * Neuron::FONT_LINE_HEIGHT_PIXELS + _row) * metrics.advancePixels + _column];
}

/// What a channel reads after the ink is blended over the background by a coverage: the straight-alpha formula,
/// rounded to nearest.
[[nodiscard]] std::uint32_t Blended(std::uint32_t _ink, std::uint32_t _background, std::uint8_t _coverage)
{
  return (_ink * _coverage + _background * (255u - _coverage) + 127u) / 255u;
}

[[nodiscard]] std::uint32_t Channel(std::uint32_t _rgba, unsigned _index)
{
  return (_rgba >> (8 * _index)) & 0xFFu;
}

/// Compares one glyph cell of the readback against its baked coverage blended over black. A texel of full or no
/// coverage must match exactly; a partial one may differ by one, which is the rounding a blend unit is allowed.
/// Returns the number of pixels wrong and names the first, because "one pixel wrong" is the failure worth reading.
[[nodiscard]] std::size_t CompareGlyph(const std::vector<std::uint32_t>& _pixels, std::uint32_t _originX, std::uint32_t _originY,
                                       Neuron::Font _font, std::uint32_t _codepoint, std::uint32_t _inkRgba, std::wstring& _outFirst)
{
  const Neuron::FontMetrics metrics = Neuron::MetricsOf(_font);
  std::size_t wrong = 0;
  for (std::uint32_t row = 0; row < Neuron::FONT_LINE_HEIGHT_PIXELS; ++row)
  {
    for (std::uint32_t column = 0; column < metrics.advancePixels; ++column)
    {
      const std::uint8_t coverage = Coverage(_font, _codepoint, column, row);
      const std::uint32_t tolerance = (coverage == 0 || coverage == 0xFF) ? 0u : 1u;
      const std::uint32_t actual = _pixels[PixelIndex(_originX + column, _originY + row)];
      bool matches = true;
      for (unsigned channel = 0; channel < 3; ++channel)
      {
        const std::uint32_t expected = Blended(Channel(_inkRgba, channel), Channel(BACKGROUND, channel), coverage);
        const std::uint32_t got = Channel(actual, channel);
        const std::uint32_t difference = expected > got ? expected - got : got - expected;
        if (difference > tolerance)
        {
          matches = false;
        }
      }
      if (!matches)
      {
        if (wrong == 0)
        {
          _outFirst = L"codepoint " + std::to_wstring(_codepoint) + L" row " + std::to_wstring(row) + L" column " +
                      std::to_wstring(column) + L": coverage " + std::to_wstring(coverage) + L", pixel " + std::to_wstring(actual);
        }
        ++wrong;
      }
    }
  }
  return wrong;
}

/// Every character of the set, once each, in codepoint order.
[[nodiscard]] std::string EveryCharacter()
{
  std::string everything;
  for (std::uint32_t codepoint = Neuron::FONT_FIRST_CODEPOINT; codepoint < Neuron::FONT_FIRST_CODEPOINT + Neuron::FONT_GLYPH_COUNT;
       ++codepoint)
  {
    everything.push_back(static_cast<char>(codepoint));
  }
  return everything;
}

} // namespace

TEST_CLASS(TextRendererTests)
{
public:
  TEST_METHOD(TheLetterAMatchesItsBakedCoverageExactly)
  {
    // "A" at (0, 0) in Body, white on black: with those two colours a channel reads the coverage byte itself, so this
    // is the atlas read back byte for byte.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(
      fixture.DrawAndReadBack([](Neuron::TextRenderer& _text) { _text.Draw(0.0f, 0.0f, "A", INK, Neuron::Font::Body); }, pixels),
      L"the draw could not be submitted");

    std::wstring first;
    const std::size_t wrong = CompareGlyph(pixels, 0, 0, Neuron::Font::Body, 'A', INK, first);
    Assert::AreEqual(std::size_t{0}, wrong, first.c_str());
  }

  TEST_METHOD(EveryGlyphOfEveryFaceMatchesItsCoverage)
  {
    // All 96 glyphs of all three faces, one face a row. This is what catches an atlas laid out wrong, a face at the
    // wrong origin, an off-by-one in the codepoint index or a row pitch mishandled on upload — none of which one letter
    // of one face would show.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());
    const std::string everything = EveryCharacter();

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack(
                     [&everything](Neuron::TextRenderer& _text)
                     {
                       for (std::uint32_t face = 0; face < FACE_COUNT; ++face)
                       {
                         _text.Draw(0.0f, static_cast<float>(face * Neuron::FONT_LINE_HEIGHT_PIXELS), everything, INK, FACES[face]);
                       }
                     },
                     pixels),
                   L"the draw could not be submitted");
    Assert::AreEqual(FACE_COUNT * Neuron::FONT_GLYPH_COUNT, fixture.Text().GlyphCount());

    std::size_t totalWrong = 0;
    std::wstring first;
    for (std::uint32_t face = 0; face < FACE_COUNT; ++face)
    {
      const std::uint32_t advance = Neuron::MetricsOf(FACES[face]).advancePixels;
      for (std::uint32_t index = 0; index < Neuron::FONT_GLYPH_COUNT; ++index)
      {
        std::wstring thisFirst;
        const std::size_t wrong = CompareGlyph(pixels, index * advance, face * Neuron::FONT_LINE_HEIGHT_PIXELS, FACES[face],
                                               Neuron::FONT_FIRST_CODEPOINT + index, INK, thisFirst);
        if (wrong != 0 && first.empty())
        {
          first = L"face " + std::to_wstring(face) + L" " + thisFirst;
        }
        totalWrong += wrong;
      }
    }
    Assert::AreEqual(std::size_t{0}, totalWrong,
                     (L"pixels wrong across the three faces: " + std::to_wstring(totalWrong) + L"; first at " + first).c_str());
  }

  TEST_METHOD(PartialCoverageBlendsTheInkTowardWhatIsUnderIt)
  {
    // The point of coverage over bits: an edge texel is a mix of the ink and the background in the texel's own
    // proportion, per channel, which is what anti-aliased type is. Amber has three different channels, so a blend
    // that mixed them up, or applied the coverage to the wrong one, would show.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::size_t partial = 0;
    const Neuron::FontMetrics metrics = Neuron::MetricsOf(Neuron::Font::Body);
    for (std::uint32_t row = 0; row < Neuron::FONT_LINE_HEIGHT_PIXELS; ++row)
    {
      for (std::uint32_t column = 0; column < metrics.advancePixels; ++column)
      {
        const std::uint8_t coverage = Coverage(Neuron::Font::Body, 'g', column, row);
        if (coverage != 0 && coverage != 0xFF)
        {
          ++partial;
        }
      }
    }
    Assert::IsTrue(partial > 0, L"the glyph has no anti-aliased edge, so this test would prove nothing");

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(
      fixture.DrawAndReadBack([](Neuron::TextRenderer& _text) { _text.Draw(0.0f, 0.0f, "g", AMBER, Neuron::Font::Body); }, pixels),
      L"the draw could not be submitted");

    std::wstring first;
    const std::size_t wrong = CompareGlyph(pixels, 0, 0, Neuron::Font::Body, 'g', AMBER, first);
    Assert::AreEqual(std::size_t{0}, wrong, first.c_str());
  }

  TEST_METHOD(AnUnknownCharacterDrawsTheReplacementBox)
  {
    // A character the set does not hold draws 0x7F, the box, so a gap in the data is loud on screen.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack(
                     [](Neuron::TextRenderer& _text)
                     {
                       const char unknown[] = {'\x01', '\0'};
                       _text.Draw(0.0f, 0.0f, unknown, INK, Neuron::Font::Body);
                     },
                     pixels),
                   L"the draw could not be submitted");

    std::wstring first;
    Assert::AreEqual(std::size_t{0}, CompareGlyph(pixels, 0, 0, Neuron::Font::Body, 0x7F, INK, first), first.c_str());
  }

  TEST_METHOD(MeasureAgreesWithWhatDrawCovers)
  {
    // NC-025 lays text out with Measure and then draws it; if the two disagree, every panel is wrong by a character.
    // The Small face, because its advance is the one that is not two characters to a cell.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    const std::string_view sample = "Kessel";
    constexpr std::uint32_t ORIGIN_X = 40;
    constexpr std::uint32_t ORIGIN_Y = 24;
    const Neuron::TextExtent extent = Neuron::TextRenderer::Measure(sample, Neuron::Font::Small);

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack(
                     [sample](Neuron::TextRenderer& _text)
                     { _text.Draw(static_cast<float>(ORIGIN_X), static_cast<float>(ORIGIN_Y), sample, INK, Neuron::Font::Small); }, pixels),
                   L"the draw could not be submitted");

    // Nothing may be drawn outside the box Measure named, and the box must be tight enough to matter.
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
    Assert::AreEqual(6u * Neuron::MetricsOf(Neuron::Font::Small).advancePixels, extent.widthPixels);
    Assert::AreEqual(Neuron::FONT_LINE_HEIGHT_PIXELS, extent.heightPixels);
  }

  TEST_METHOD(AnIconIsItsArtAtThreeTexelsABit)
  {
    // An icon's bits become a cell of full or no coverage, three texels a bit, so it reaches the glass as exactly the
    // pixel art it was drawn as: every one of the nine pixels behind a bit is the ink, and behind a clear bit the
    // background, with no blended edge anywhere.
    TextFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(
      fixture.DrawAndReadBack([](Neuron::TextRenderer& _text) { _text.DrawIcon(0.0f, 0.0f, Neuron::Icon::Warning, INK); }, pixels),
      L"the draw could not be submitted");

    const auto icon = static_cast<std::size_t>(Neuron::Icon::Warning);
    for (std::uint32_t bitRow = 0; bitRow < Neuron::ICON_ART_PIXELS; ++bitRow)
    {
      const std::uint8_t bits = Neuron::ICON_8X8_ART[icon * Neuron::ICON_BYTES + bitRow];
      for (std::uint32_t bitColumn = 0; bitColumn < Neuron::ICON_ART_PIXELS; ++bitColumn)
      {
        const bool set = ((bits >> (Neuron::ICON_ART_PIXELS - 1 - bitColumn)) & 1u) != 0;
        const std::uint32_t expected = set ? INK : BACKGROUND;
        for (std::uint32_t subY = 0; subY < Neuron::TextRenderer::ICON_TEXELS_PER_BIT; ++subY)
        {
          for (std::uint32_t subX = 0; subX < Neuron::TextRenderer::ICON_TEXELS_PER_BIT; ++subX)
          {
            const std::uint32_t x = bitColumn * Neuron::TextRenderer::ICON_TEXELS_PER_BIT + subX;
            const std::uint32_t y = bitRow * Neuron::TextRenderer::ICON_TEXELS_PER_BIT + subY;
            Assert::AreEqual(expected, pixels[PixelIndex(x, y)],
                             (L"bit (" + std::to_wstring(bitColumn) + L"," + std::to_wstring(bitRow) + L") sub-pixel (" +
                              std::to_wstring(subX) + L"," + std::to_wstring(subY) + L") is not a solid block")
                               .c_str());
          }
        }
      }
    }
  }
};

} // namespace NeuronClientTests
