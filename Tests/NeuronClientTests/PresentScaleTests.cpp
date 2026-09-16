// Tests/NeuronClientTests/PresentScaleTests.cpp
#include "pch.h"
#include "Font.h"
#include "GlyphPipeline.h"
#include "GraphicsDevice.h"
#include "Palette.h"
#include "PresentPass.h"
#include "SceneTarget.h"
#include "TextRenderer.h"
#include "Window.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Microsoft::WRL::ComPtr;

/// The desk's own two colours, so what is measured is text a player would read rather than a test pattern.
constexpr std::uint32_t BACKGROUND = Neuron::Palette::BACKGROUND;
constexpr std::uint32_t INK = Neuron::Palette::TEXT;

/// The same colour as BACKGROUND, because the target is created and cleared with it. A scene target may only be
/// cleared to the value it was created with, so there is one of these and not two (the
/// CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE finding in NC-021).
constexpr float BACKGROUND_CLEAR[4] = {0x0B / 255.0f, 0x0E / 255.0f, 0x13 / 255.0f, 1.0f};

/// The bars. Black rather than the background, so a pixel outside the placement can never be mistaken for one inside
/// it in a failure message.
constexpr float LETTERBOX_CLEAR[4] = {0.0f, 0.0f, 0.0f, 1.0f};

/// ADR-009's three cases at the sizes that occur: the screen itself, an exact 2x, and the 0.71x the ADR names by that
/// figure. 1366x768 is the display its "What this forecloses" paragraph is written about.
constexpr std::uint32_t ONE_TO_ONE_WIDTH = 1920;
constexpr std::uint32_t ONE_TO_ONE_HEIGHT = 1080;
constexpr std::uint32_t DOUBLED_WIDTH = 3840;
constexpr std::uint32_t DOUBLED_HEIGHT = 2160;
constexpr std::uint32_t FRACTIONAL_WIDTH = 1366;
constexpr std::uint32_t FRACTIONAL_HEIGHT = 768;

[[nodiscard]] std::uint32_t Channel(std::uint32_t _rgba, unsigned _index)
{
  return (_rgba >> (8 * _index)) & 0xFFu;
}

/// Every printable character of the set, once, in codepoint order.
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

/// What the figures are measured over.
///
/// Every glyph of every face rather than one sentence, because a number about "text" should not be a number about the
/// letters one sentence happened to use: a face's widest stem and its thinnest diagonal resample differently and both
/// are on screen here. The sentence is there so the sample also holds ordinary spacing.
void DrawTheSample(Neuron::TextRenderer& _text)
{
  const std::string everything = EveryCharacter();
  _text.Draw(48.0f, 48.0f, "Convoy sighted at Tessa Gate. 9 h old, reliability 72%.", INK, Neuron::Font::Body);
  _text.Draw(48.0f, 120.0f, everything, INK, Neuron::Font::Title);
  _text.Draw(48.0f, 168.0f, everything, INK, Neuron::Font::Body);
  _text.Draw(48.0f, 216.0f, everything, INK, Neuron::Font::Small);
}

/// Which source texel a destination pixel stands over, by the mapping a point sampler uses.
///
/// It is the reference every case is measured against: at 1:1 and at an exact multiple the present step must land on
/// it exactly, and at a fractional scale the distance from it is what "softer" means. Integer arithmetic throughout --
/// this decides which pixel a figure is about, and a float here would put a rounding rule nobody chose in the middle
/// of the measurement.
[[nodiscard]] std::uint32_t NearestSourceIndex(std::uint32_t _destCoordinate, std::int32_t _placementOrigin, std::uint32_t _placementExtent,
                                               std::uint32_t _sourceExtent)
{
  const auto within = static_cast<std::uint64_t>(_destCoordinate - static_cast<std::uint32_t>(_placementOrigin));
  return static_cast<std::uint32_t>((within * 2 + 1) * _sourceExtent / (2ull * _placementExtent));
}

/// What the present step did to the pixels, as numbers ADR-009 can quote.
struct ScaleFigures
{
  std::size_t pixelsInPlacement = 0;
  /// Pixels differing from the source texel they stand over.
  std::size_t moved = 0;
  /// The largest single-channel difference from that texel, over the whole placement.
  std::uint32_t largestChannelMove = 0;
  /// Pixels carrying a colour the scene target does not contain anywhere.
  std::size_t inventedColors = 0;
  /// Lit is anything that is not the background; partial is lit but not the full ink -- the anti-aliased edge.
  std::size_t lit = 0;
  std::size_t partial = 0;
};

/// The distinct colours of an image, sorted, so membership is a binary search rather than a hash container.
[[nodiscard]] std::vector<std::uint32_t> DistinctColors(const std::vector<std::uint32_t>& _pixels)
{
  std::vector<std::uint32_t> colors(_pixels);
  std::sort(colors.begin(), colors.end());
  colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
  return colors;
}

/// Counts the lit and partial pixels of an image. The destination gets the same measurement, so the two are
/// comparable: partial over lit is the share of a glyph that is an anti-aliased edge rather than solid ink.
void CountLitAndPartial(const std::vector<std::uint32_t>& _pixels, std::size_t& _outLit, std::size_t& _outPartial)
{
  _outLit = 0;
  _outPartial = 0;
  for (const std::uint32_t pixel : _pixels)
  {
    if (pixel == BACKGROUND)
    {
      continue;
    }
    ++_outLit;
    if (pixel != INK)
    {
      ++_outPartial;
    }
  }
}

/// Measures one presented image against the scene target it came from, inside the placement and nowhere else.
[[nodiscard]] ScaleFigures Measure(const std::vector<std::uint32_t>& _source, const std::vector<std::uint32_t>& _destination,
                                   const Neuron::PresentPass::Placement& _placement, std::uint32_t _destinationWidth,
                                   const std::vector<std::uint32_t>& _sourceColors)
{
  ScaleFigures figures;
  const auto left = static_cast<std::uint32_t>(_placement.leftPixels);
  const auto top = static_cast<std::uint32_t>(_placement.topPixels);
  for (std::uint32_t y = top; y < top + _placement.heightPixels; ++y)
  {
    for (std::uint32_t x = left; x < left + _placement.widthPixels; ++x)
    {
      const std::uint32_t actual = _destination[static_cast<std::size_t>(y) * _destinationWidth + x];
      const std::uint32_t sourceX = NearestSourceIndex(x, _placement.leftPixels, _placement.widthPixels, Neuron::SCREEN_WIDTH_PIXELS);
      const std::uint32_t sourceY = NearestSourceIndex(y, _placement.topPixels, _placement.heightPixels, Neuron::SCREEN_HEIGHT_PIXELS);
      const std::uint32_t expected = _source[static_cast<std::size_t>(sourceY) * Neuron::SCREEN_WIDTH_PIXELS + sourceX];

      ++figures.pixelsInPlacement;
      if (actual != expected)
      {
        ++figures.moved;
        for (unsigned channel = 0; channel < 3; ++channel)
        {
          const std::uint32_t here = Channel(actual, channel);
          const std::uint32_t there = Channel(expected, channel);
          const std::uint32_t move = here > there ? here - there : there - here;
          figures.largestChannelMove = move > figures.largestChannelMove ? move : figures.largestChannelMove;
        }
      }
      if (!std::binary_search(_sourceColors.begin(), _sourceColors.end(), actual))
      {
        ++figures.inventedColors;
      }
      if (actual != BACKGROUND)
      {
        ++figures.lit;
        if (actual != INK)
        {
          ++figures.partial;
        }
      }
    }
  }
  return figures;
}

void Report(const wchar_t* _caseName, const Neuron::PresentPass::Placement& _placement, const ScaleFigures& _figures)
{
  std::wstring line = L"[NC-029] ";
  line += _caseName;
  line += L": placement " + std::to_wstring(_placement.widthPixels) + L"x" + std::to_wstring(_placement.heightPixels) + L" at (" +
          std::to_wstring(_placement.leftPixels) + L"," + std::to_wstring(_placement.topPixels) + L")";
  line += L"; pixels " + std::to_wstring(_figures.pixelsInPlacement);
  line += L"; moved " + std::to_wstring(_figures.moved);
  line += L"; largest channel move " + std::to_wstring(_figures.largestChannelMove);
  line += L"; invented colours " + std::to_wstring(_figures.inventedColors);
  line += L"; lit " + std::to_wstring(_figures.lit);
  line += L"; partial " + std::to_wstring(_figures.partial);
  Logger::WriteMessage(line.c_str());
}

/// The scene target with the sample drawn into it, read back, and the present pass over it. One of these is one
/// measurement: the source is identical across the three cases by construction, because it is drawn once, here.
class PresentFixture
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
    const Neuron::SceneTarget::Desc sceneDesc{Neuron::SCREEN_WIDTH_PIXELS,
                                              Neuron::SCREEN_HEIGHT_PIXELS,
                                              DXGI_FORMAT_R8G8B8A8_UNORM,
                                              {BACKGROUND_CLEAR[0], BACKGROUND_CLEAR[1], BACKGROUND_CLEAR[2], BACKGROUND_CLEAR[3]}};
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
      m_why = L"the text renderer could not be created";
      return false;
    }
    if (!Neuron::PresentPass::Create(m_device, m_scene, m_present))
    {
      m_why = L"the present pass could not be created";
      return false;
    }
    if (!DrawTheSampleAndReadItBack())
    {
      m_why = L"the sample could not be drawn into the scene target";
      return false;
    }
    m_sourceColors = DistinctColors(m_source);
    return true;
  }

  /// Presents the scene target into a target of the given size and reads that back.
  ///
  /// The destination is a SceneTarget rather than a back buffer, and that is the whole reason all three cases are
  /// reachable on one monitor: a target's extent comes from its Desc and a back buffer's from DXGI_SWAP_CHAIN_DESC1,
  /// and neither of them asks the display.
  [[nodiscard]] bool PresentInto(std::uint32_t _widthPixels, std::uint32_t _heightPixels, std::vector<std::uint32_t>& _outPixels,
                                 Neuron::PresentPass::Placement& _outPlacement)
  {
    Neuron::SceneTarget destination;
    const Neuron::SceneTarget::Desc desc{_widthPixels,
                                         _heightPixels,
                                         DXGI_FORMAT_R8G8B8A8_UNORM,
                                         {LETTERBOX_CLEAR[0], LETTERBOX_CLEAR[1], LETTERBOX_CLEAR[2], LETTERBOX_CLEAR[3]}};
    if (!Neuron::SceneTarget::Create(m_device, desc, destination))
    {
      return false;
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (!OpenList(allocator, commandList))
    {
      return false;
    }

    destination.Transition(commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    destination.Clear(commandList.Get());
    const D3D12_CPU_DESCRIPTOR_HANDLE view = destination.RenderTargetView();
    commandList->OMSetRenderTargets(1, &view, FALSE, nullptr);
    m_present.Execute(commandList.Get(), m_scene, destination.Resource(), _widthPixels, _heightPixels);

    if (!SubmitAndWait(commandList))
    {
      return false;
    }
    _outPlacement = m_present.LastPlacement();
    return destination.ReadBack(_outPixels);
  }

  [[nodiscard]] const wchar_t* Why() const noexcept
  {
    return m_why;
  }

  [[nodiscard]] const std::vector<std::uint32_t>& Source() const noexcept
  {
    return m_source;
  }

  [[nodiscard]] const std::vector<std::uint32_t>& SourceColors() const noexcept
  {
    return m_sourceColors;
  }

private:
  [[nodiscard]] bool OpenList(ComPtr<ID3D12CommandAllocator>& _outAllocator, ComPtr<ID3D12GraphicsCommandList>& _outList)
  {
    return SUCCEEDED(m_device.Device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_outAllocator))) &&
           SUCCEEDED(m_device.Device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _outAllocator.Get(), nullptr,
                                                          IID_PPV_ARGS(&_outList)));
  }

  [[nodiscard]] bool SubmitAndWait(ComPtr<ID3D12GraphicsCommandList>& _list)
  {
    if (FAILED(_list->Close()))
    {
      return false;
    }
    ID3D12CommandList* const lists[] = {_list.Get()};
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
    return waited;
  }

  [[nodiscard]] bool DrawTheSampleAndReadItBack()
  {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (!OpenList(allocator, commandList))
    {
      return false;
    }
    m_scene.Transition(commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_scene.Clear(commandList.Get());
    const D3D12_CPU_DESCRIPTOR_HANDLE view = m_scene.RenderTargetView();
    commandList->OMSetRenderTargets(1, &view, FALSE, nullptr);

    const D3D12_VIEWPORT viewport{
      0.0f, 0.0f, static_cast<FLOAT>(Neuron::SCREEN_WIDTH_PIXELS), static_cast<FLOAT>(Neuron::SCREEN_HEIGHT_PIXELS), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(Neuron::SCREEN_WIDTH_PIXELS), static_cast<LONG>(Neuron::SCREEN_HEIGHT_PIXELS)};
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    m_text.Begin(commandList.Get(), m_pipeline, 0, Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS);
    DrawTheSample(m_text);
    m_text.End();

    return SubmitAndWait(commandList) && m_scene.ReadBack(m_source);
  }

  Neuron::GraphicsDevice m_device;
  Neuron::SceneTarget m_scene;
  Neuron::GlyphPipeline m_pipeline;
  Neuron::TextRenderer m_text;
  Neuron::PresentPass m_present;
  std::vector<std::uint32_t> m_source;
  std::vector<std::uint32_t> m_sourceColors;
  const wchar_t* m_why = L"";
};

} // namespace

/// ADR-009's Measurements section owed three figures and quoted none, across NC-021, NC-027 and NC-028. These are them.
///
/// Each of those reports read the obligation as a photograph, and a photograph of a scaled present needs a display
/// that is not 1920x1080. The numbers never did: a destination's extent is a parameter rather than a property of the
/// monitor, so all three cases are reachable here and reproducible on a runner with no display at all (A12).
TEST_CLASS(PresentScaleTests)
{
public:
  TEST_METHOD(TheOneToOnePathMovesNoPixel)
  {
    // The path every 1920x1080 player takes since ADR-010, and the one claim ADR-009 makes that can be proved rather
    // than argued: it is a CopyResource, so the guarantee is that no filter runs at all.
    PresentFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> presented;
    Neuron::PresentPass::Placement placement{};
    Assert::IsTrue(fixture.PresentInto(ONE_TO_ONE_WIDTH, ONE_TO_ONE_HEIGHT, presented, placement), L"the present could not be submitted");
    Assert::IsTrue(placement.filter == Neuron::PresentPass::Filter::None, L"1920x1080 took a filtered path");

    const ScaleFigures figures = Measure(fixture.Source(), presented, placement, ONE_TO_ONE_WIDTH, fixture.SourceColors());
    Report(L"1:1 (1920x1080, Filter::None)", placement, figures);

    Assert::AreEqual(fixture.Source().size(), presented.size(), L"the presented image is not the size of the scene target");
    Assert::AreEqual(std::size_t{0}, figures.moved,
                     (L"the copy path moved " + std::to_wstring(figures.moved) + L" pixels; it must move none").c_str());
    Assert::AreEqual(std::size_t{0}, figures.inventedColors);
    Assert::IsTrue(figures.lit > 0, L"nothing was drawn, so this measures nothing");
  }

  TEST_METHOD(ThePointPathKeepsEveryTexelASquareBlockAndInventsNoColor)
  {
    // ADR-009's second case, and PresentPass.h's own claim about it: "a glyph's bit pattern stays a bit pattern, just
    // bigger". Since ADR-016 a glyph reaches the scene target already blended, so this is a check on the sampler and
    // not on the font -- and it is the stronger form of the claim, every texel against its whole 2x2 block.
    PresentFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> presented;
    Neuron::PresentPass::Placement placement{};
    Assert::IsTrue(fixture.PresentInto(DOUBLED_WIDTH, DOUBLED_HEIGHT, presented, placement), L"the present could not be submitted");
    Assert::IsTrue(placement.filter == Neuron::PresentPass::Filter::Point, L"an exact 2x did not take the point path");

    const ScaleFigures figures = Measure(fixture.Source(), presented, placement, DOUBLED_WIDTH, fixture.SourceColors());
    Report(L"2x point (3840x2160, Filter::Point)", placement, figures);

    Assert::AreEqual(std::size_t{0}, figures.inventedColors,
                     (L"point sampling introduced " + std::to_wstring(figures.inventedColors) +
                      L" pixels of colour the scene target does not contain, which it cannot do without filtering")
                       .c_str());
    Assert::AreEqual(std::size_t{0}, figures.moved,
                     (L"pixels differing from the texel they stand over: " + std::to_wstring(figures.moved) +
                      L"; at an exact multiple every one must be its own block")
                       .c_str());
    Assert::AreEqual(std::uint32_t{0}, figures.largestChannelMove);
  }

  TEST_METHOD(TheBilinearPathIsMeasuredRatherThanDescribed)
  {
    // ADR-009 calls this case "softer text" and quotes nothing. 1366x768 is the display its "What this forecloses"
    // paragraph is written about, so it is the one measured here.
    PresentFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::size_t sourceLit = 0;
    std::size_t sourcePartial = 0;
    CountLitAndPartial(fixture.Source(), sourceLit, sourcePartial);

    std::vector<std::uint32_t> presented;
    Neuron::PresentPass::Placement placement{};
    Assert::IsTrue(fixture.PresentInto(FRACTIONAL_WIDTH, FRACTIONAL_HEIGHT, presented, placement), L"the present could not be submitted");
    Assert::IsTrue(placement.filter == Neuron::PresentPass::Filter::Linear, L"a fractional scale did not take the bilinear path");

    const ScaleFigures figures = Measure(fixture.Source(), presented, placement, FRACTIONAL_WIDTH, fixture.SourceColors());
    Report(L"0.71x bilinear (1366x768, Filter::Linear)", placement, figures);

    // The softness figure, and the one worth quoting: the share of lit pixels that are an edge rather than solid ink.
    // In the scene target that share is the face's own anti-aliasing; here it is that plus whatever the sampler added.
    std::wstring softness = L"[NC-029] scene target: lit " + std::to_wstring(sourceLit) + L", partial " + std::to_wstring(sourcePartial) +
                            L" (" + std::to_wstring(sourceLit == 0 ? 0 : sourcePartial * 100 / sourceLit) + L"%)";
    softness += L"; presented: lit " + std::to_wstring(figures.lit) + L", partial " + std::to_wstring(figures.partial) + L" (" +
                std::to_wstring(figures.lit == 0 ? 0 : figures.partial * 100 / figures.lit) + L"%)";
    Logger::WriteMessage(softness.c_str());

    // This case exists to produce numbers, not to pass a threshold nobody has played against. What is asserted is only
    // that it really is the filtered path and really did change the pixels: a bilinear present that moved nothing
    // would mean the measurement was not measuring what it claims to.
    Assert::IsTrue(figures.moved > 0, L"the bilinear path moved no pixel, so it did not filter anything");
    Assert::IsTrue(sourceLit > 0 && figures.lit > 0, L"nothing was drawn, so this measures nothing");
    Assert::IsTrue(figures.partial * sourceLit > sourcePartial * figures.lit,
                   L"the bilinear present did not soften the text, which contradicts what ADR-009 says it costs");
  }
};

} // namespace NeuronClientTests
