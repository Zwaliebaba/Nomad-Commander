// Tests/NeuronClientTests/PrimitiveBatchTests.cpp
#include "pch.h"
#include "GraphicsDevice.h"
#include "PrimitiveBatch.h"
#include "PrimitivePipeline.h"
#include "SceneTarget.h"
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

/// The background every pattern below is drawn onto, and the colour every pixel outside a primitive must still be.
constexpr std::uint32_t BACKGROUND = 0xFF000000u;

/// Opaque red, written the way PrimitiveBatch packs a colour: red in the low byte. This is the number the task names.
constexpr std::uint32_t RED = 0xFF0000FFu;
constexpr std::uint32_t GREEN = 0xFF00FF00u;

/// A device, a 1920x1080 scene target cleared to BACKGROUND, the pipeline and a batch — everything a drawing test
/// needs, so that each test says what it draws rather than how to set up D3D12.
class DrawingFixture
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
    if (!Neuron::PrimitivePipeline::Create(m_device, m_pipeline))
    {
      m_why = L"the primitive pipeline could not be created";
      return false;
    }
    if (!Neuron::PrimitiveBatch::Create(m_device, m_batch))
    {
      m_why = L"the primitive batch could not be created";
      return false;
    }
    return true;
  }

  /// Records a clear, whatever the caller draws, and a wait, then reads every pixel back.
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

    m_batch.Begin(commandList.Get(), m_pipeline, 0, m_scene.WidthPixels(), m_scene.HeightPixels());
    _draw(m_batch);
    m_batch.End();

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

  [[nodiscard]] Neuron::PrimitiveBatch& Batch() noexcept
  {
    return m_batch;
  }

private:
  Neuron::GraphicsDevice m_device;
  Neuron::SceneTarget m_scene;
  Neuron::PrimitivePipeline m_pipeline;
  Neuron::PrimitiveBatch m_batch;
  const wchar_t* m_why = L"";
};

[[nodiscard]] std::size_t PixelIndex(std::uint32_t _x, std::uint32_t _y)
{
  return static_cast<std::size_t>(_y) * Neuron::SCREEN_WIDTH_PIXELS + _x;
}

/// Counts every pixel that is not the background, and reports the bounding box of those that are not. A rectangle
/// that is one pixel too wide, or shifted by one, is caught by the box rather than by a spot check that misses it.
struct ChangedPixels
{
  std::size_t count = 0;
  std::uint32_t left = Neuron::SCREEN_WIDTH_PIXELS;
  std::uint32_t top = Neuron::SCREEN_HEIGHT_PIXELS;
  std::uint32_t right = 0;
  std::uint32_t bottom = 0;
  std::size_t wrongColor = 0;
};

[[nodiscard]] ChangedPixels SurveyChanged(const std::vector<std::uint32_t>& _pixels, std::uint32_t _expectedColor)
{
  ChangedPixels survey;
  for (std::uint32_t y = 0; y < Neuron::SCREEN_HEIGHT_PIXELS; ++y)
  {
    for (std::uint32_t x = 0; x < Neuron::SCREEN_WIDTH_PIXELS; ++x)
    {
      const std::uint32_t pixel = _pixels[PixelIndex(x, y)];
      if (pixel == BACKGROUND)
      {
        continue;
      }
      ++survey.count;
      if (pixel != _expectedColor)
      {
        ++survey.wrongColor;
      }
      survey.left = survey.left < x ? survey.left : x;
      survey.top = survey.top < y ? survey.top : y;
      survey.right = survey.right > x ? survey.right : x;
      survey.bottom = survey.bottom > y ? survey.bottom : y;
    }
  }
  return survey;
}

[[nodiscard]] std::wstring Describe(const ChangedPixels& _survey)
{
  return L"changed=" + std::to_wstring(_survey.count) + L" wrongColour=" + std::to_wstring(_survey.wrongColor) + L" box=(" +
         std::to_wstring(_survey.left) + L"," + std::to_wstring(_survey.top) + L")-(" + std::to_wstring(_survey.right) + L"," +
         std::to_wstring(_survey.bottom) + L")";
}

} // namespace

TEST_CLASS(PrimitiveBatchTests)
{
public:
  TEST_METHOD(AFilledRectangleCoversExactlyThePixelsItNames)
  {
    // The task's own case: a 10x10 rectangle at (20, 30) in 0xFF0000FF. "Exactly" is the point — a pixel coordinate
    // names a pixel's top-left corner, so this is pixels 20..29 by 30..39 and not one more.
    DrawingFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(
      fixture.DrawAndReadBack([](Neuron::PrimitiveBatch& _batch) { _batch.FillRect(20.0f, 30.0f, 10.0f, 10.0f, RED); }, pixels),
      L"the draw could not be submitted");

    const ChangedPixels survey = SurveyChanged(pixels, RED);
    Assert::AreEqual(std::size_t{100}, survey.count, Describe(survey).c_str());
    Assert::AreEqual(std::size_t{0}, survey.wrongColor, Describe(survey).c_str());
    Assert::AreEqual(20u, survey.left, Describe(survey).c_str());
    Assert::AreEqual(30u, survey.top, Describe(survey).c_str());
    Assert::AreEqual(29u, survey.right, Describe(survey).c_str());
    Assert::AreEqual(39u, survey.bottom, Describe(survey).c_str());

    // The corners, named individually, because a bounding box of the right size can still be the wrong pixels.
    Assert::AreEqual(RED, pixels[PixelIndex(20, 30)]);
    Assert::AreEqual(RED, pixels[PixelIndex(29, 39)]);
    Assert::AreEqual(BACKGROUND, pixels[PixelIndex(19, 30)]);
    Assert::AreEqual(BACKGROUND, pixels[PixelIndex(30, 39)]);
    Assert::AreEqual(BACKGROUND, pixels[PixelIndex(20, 29)]);
    Assert::AreEqual(BACKGROUND, pixels[PixelIndex(29, 40)]);
  }

  TEST_METHOD(AColorAuthoredIsTheColorReadBack)
  {
    // R12: the target is _UNORM and not _SRGB, so a channel authored 0xAA arrives as 0xAA. If this ever reads 0xC1 or
    // thereabouts, someone has changed the format and every colour in the UI is now wrong by a gamma curve.
    DrawingFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    constexpr std::uint32_t AUTHORED = 0xFFAAAAAAu;
    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(
      fixture.DrawAndReadBack([](Neuron::PrimitiveBatch& _batch) { _batch.FillRect(100.0f, 100.0f, 4.0f, 4.0f, AUTHORED); }, pixels),
      L"the draw could not be submitted");
    Assert::AreEqual(AUTHORED, pixels[PixelIndex(101, 101)]);
  }

  TEST_METHOD(AHorizontalLineCoversOneRow)
  {
    // Line rasterization follows the diamond-exit rule, so the batch shifts a line to pixel centres. A line asked for
    // at y = 200 therefore covers row 200 and neither 199 nor 201.
    DrawingFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(
      fixture.DrawAndReadBack([](Neuron::PrimitiveBatch& _batch) { _batch.Line(50.0f, 200.0f, 150.0f, 200.0f, GREEN); }, pixels),
      L"the draw could not be submitted");

    const ChangedPixels survey = SurveyChanged(pixels, GREEN);
    Assert::AreEqual(std::size_t{0}, survey.wrongColor, Describe(survey).c_str());
    Assert::AreEqual(200u, survey.top, Describe(survey).c_str());
    Assert::AreEqual(200u, survey.bottom, Describe(survey).c_str());
    Assert::AreEqual(50u, survey.left, Describe(survey).c_str());
    Assert::IsTrue(survey.right == 149u || survey.right == 150u,
                   (L"the line's far end is not where it should be: " + Describe(survey)).c_str());
    Assert::AreEqual(GREEN, pixels[PixelIndex(100, 200)]);
    Assert::AreEqual(BACKGROUND, pixels[PixelIndex(100, 199)]);
    Assert::AreEqual(BACKGROUND, pixels[PixelIndex(100, 201)]);
  }

  TEST_METHOD(AnOutlineIsOnePixelThickAndEncloses)
  {
    DrawingFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack([](Neuron::PrimitiveBatch& _batch) { _batch.Rect(400.0f, 400.0f, 20.0f, 10.0f, RED); }, pixels),
                   L"the draw could not be submitted");

    const ChangedPixels survey = SurveyChanged(pixels, RED);
    // A 20x10 outline is 2*20 + 2*(10-2) = 56 pixels, and no pixel is drawn twice.
    Assert::AreEqual(std::size_t{56}, survey.count, Describe(survey).c_str());
    Assert::AreEqual(400u, survey.left, Describe(survey).c_str());
    Assert::AreEqual(419u, survey.right, Describe(survey).c_str());
    Assert::AreEqual(400u, survey.top, Describe(survey).c_str());
    Assert::AreEqual(409u, survey.bottom, Describe(survey).c_str());
    Assert::AreEqual(BACKGROUND, pixels[PixelIndex(410, 405)]);
  }

  TEST_METHOD(PaintersOrderPutsTheLastPrimitiveOnTop)
  {
    // The property that stops this batch being sorted by pipeline state. A line is drawn across a rectangle and a
    // second rectangle over both; if the runs were reordered, the line would be on top and the middle pixel green.
    DrawingFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack(
                     [](Neuron::PrimitiveBatch& _batch)
                     {
                       _batch.FillRect(600.0f, 600.0f, 100.0f, 100.0f, RED);
                       _batch.Line(600.0f, 650.0f, 700.0f, 650.0f, GREEN);
                       _batch.FillRect(620.0f, 620.0f, 60.0f, 60.0f, 0xFFFFFFFFu);
                     },
                     pixels),
                   L"the draw could not be submitted");

    Assert::AreEqual(0xFFFFFFFFu, pixels[PixelIndex(650, 650)], L"the last rectangle is not on top");
    Assert::AreEqual(GREEN, pixels[PixelIndex(605, 650)], L"the line is not over the first rectangle");
    Assert::AreEqual(RED, pixels[PixelIndex(605, 610)], L"the first rectangle is missing");
  }

  TEST_METHOD(APolygonAndACircleCoverRoughlyTheirArea)
  {
    // Exact pixel counts for a circle would be asserting the rasterizer's tie-breaking rules, which is not this
    // code's contract. What is: a circle of radius 50 covers about pi*r^2 pixels and is centred where it was asked
    // for, and a triangle covers about half its bounding box.
    DrawingFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack([](Neuron::PrimitiveBatch& _batch)
                                           { _batch.FillCircle(Neuron::Point{300.0f, 300.0f}, 50.0f, 64, RED); }, pixels),
                   L"the draw could not be submitted");

    const ChangedPixels survey = SurveyChanged(pixels, RED);
    Assert::AreEqual(std::size_t{0}, survey.wrongColor, Describe(survey).c_str());
    // A 64-sided polygon inscribed in the circle is a little smaller than it; 5 % either side of pi*r^2 is generous
    // enough not to be brittle and tight enough to catch a radius or a centre that is wrong.
    Assert::IsTrue(survey.count > 7300 && survey.count < 7900, (L"the circle's area is off: " + Describe(survey)).c_str());
    Assert::IsTrue(survey.left >= 249 && survey.left <= 252, (L"the circle is not centred: " + Describe(survey)).c_str());
    Assert::IsTrue(survey.right >= 348 && survey.right <= 351, (L"the circle is not centred: " + Describe(survey)).c_str());
    Assert::AreEqual(RED, pixels[PixelIndex(300, 300)]);
  }

  TEST_METHOD(AFullFrameOfVerticesDrawsAndTheSliceIsNeverOverrun)
  {
    // The task asks for fifty thousand vertices in one frame without a debug-layer message. Each rectangle is six, so
    // this is nine thousand rectangles — and then the slice is deliberately overrun, to prove that the batch drops
    // the primitive and says so rather than writing into the next frame's slice.
    DrawingFixture fixture;
    Assert::IsTrue(fixture.Create(), fixture.Why());

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(fixture.DrawAndReadBack(
                     [](Neuron::PrimitiveBatch& _batch)
                     {
                       for (int index = 0; index < 9000; ++index)
                       {
                         const int column = index % 480;
                         const int row = index / 480;
                         const float x = static_cast<float>(column) * 4.0f;
                         const float y = static_cast<float>(row) * 4.0f;
                         _batch.FillRect(x, y, 3.0f, 3.0f, RED);
                       }
                     },
                     pixels),
                   L"the draw could not be submitted");
    Assert::AreEqual(54000u, fixture.Batch().VertexCount());
    Assert::IsFalse(fixture.Batch().Overflowed(), L"fifty-four thousand vertices should fit a slice");

    std::vector<std::uint32_t> overrun;
    Assert::IsTrue(fixture.DrawAndReadBack(
                     [](Neuron::PrimitiveBatch& _batch)
                     {
                       for (int index = 0; index < 40000; ++index)
                       {
                         _batch.FillRect(0.0f, 0.0f, 2.0f, 2.0f, RED);
                       }
                     },
                     overrun),
                   L"the overrun draw could not be submitted");
    Assert::IsTrue(fixture.Batch().Overflowed(), L"an overrun slice must be reported");
    Assert::IsTrue(fixture.Batch().VertexCount() <= Neuron::PrimitiveBatch::MAX_VERTICES_PER_FRAME,
                   L"the batch wrote past the end of its slice");
  }
};

} // namespace NeuronClientTests
