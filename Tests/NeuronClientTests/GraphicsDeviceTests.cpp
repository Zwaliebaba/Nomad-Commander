// Tests/NeuronClientTests/GraphicsDeviceTests.cpp
#include "pch.h"
#include "GraphicsDevice.h"
#include "PresentPass.h"
#include "SceneTarget.h"
#include "SwapChainTarget.h"
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

/// Everything here runs on the software rasterizer with the debug layer on, so CI exercises the D3D12 code with no GPU
/// (Plan/Roadmap.md A12). The debug layer breaks on error and corruption, so a rule broken anywhere below stops the
/// run rather than printing into a log nobody reads.
[[nodiscard]] bool CreateWarpDevice(Neuron::GraphicsDevice& _outDevice)
{
  const Neuron::GraphicsDevice::Desc desc{true, true};
  return Neuron::GraphicsDevice::Create(desc, _outDevice);
}

[[nodiscard]] std::wstring WhyTheDeviceFailed(const Neuron::GraphicsDevice& _device)
{
  std::wstring reason = L"GraphicsDevice::Create failed: ";
  switch (_device.Fault())
  {
  case Neuron::DeviceFault::None:
    reason += L"no fault recorded";
    break;
  case Neuron::DeviceFault::FactoryCreation:
    reason += L"CreateDXGIFactory2";
    break;
  case Neuron::DeviceFault::NoAdapter:
    reason += L"EnumWarpAdapter";
    break;
  case Neuron::DeviceFault::DeviceCreation:
    reason += L"D3D12CreateDevice";
    break;
  case Neuron::DeviceFault::ShaderModelTooLow:
    reason += L"the device's highest shader model is " + std::to_wstring(static_cast<int>(_device.HighestShaderModel())) +
              L", below the 0x67 ADR-011 requires";
    break;
  case Neuron::DeviceFault::CommandQueue:
    reason += L"CreateCommandQueue";
    break;
  }
  wchar_t code[32] = {};
  swprintf_s(code, L"; hr=0x%08lX", static_cast<unsigned long>(_device.Result()));
  return reason + code;
}

/// The colour every pixel of the scene target is expected to carry, as the bytes it is stored in. R8G8B8A8_UNORM and
/// not _SRGB, so what is cleared is what comes back (AGENTS.md R12).
constexpr std::uint8_t CLEAR_RED = 0x1E;
constexpr std::uint8_t CLEAR_GREEN = 0x28;
constexpr std::uint8_t CLEAR_BLUE = 0x3C;
constexpr std::uint32_t CLEAR_PIXEL =
  0xFF000000u | (static_cast<std::uint32_t>(CLEAR_BLUE) << 16) | (static_cast<std::uint32_t>(CLEAR_GREEN) << 8) | CLEAR_RED;
/// The same colour the target is created with, which since the CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE finding is
/// the only colour it can be cleared to.
constexpr float CLEAR_COLOR[4] = {CLEAR_RED / 255.0f, CLEAR_GREEN / 255.0f, CLEAR_BLUE / 255.0f, 1.0f};

/// Submits one clear of the scene target and waits for it, so that a readback right after sees it.
[[nodiscard]] bool ClearAndWait(Neuron::GraphicsDevice& _device, Neuron::SceneTarget& _target)
{
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commandList;
  if (FAILED(_device.Device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
      FAILED(_device.Device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList))))
  {
    return false;
  }
  _target.Transition(commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
  _target.Clear(commandList.Get());

  if (FAILED(commandList->Close()))
  {
    return false;
  }
  ID3D12CommandList* const lists[] = {commandList.Get()};
  _device.Queue()->ExecuteCommandLists(1, lists);

  ComPtr<ID3D12Fence> fence;
  if (FAILED(_device.Device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
  {
    return false;
  }
  const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (event == nullptr)
  {
    return false;
  }
  bool waited = false;
  if (SUCCEEDED(_device.Queue()->Signal(fence.Get(), 1)) && SUCCEEDED(fence->SetEventOnCompletion(1, event)))
  {
    waited = WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;
  }
  CloseHandle(event);
  return waited;
}

} // namespace

TEST_CLASS(GraphicsDeviceTests)
{
public:
  TEST_METHOD(AWarpDeviceIsCreatedWithAQueueAndShaderModelSixSeven)
  {
    Neuron::GraphicsDevice device;
    const bool created = CreateWarpDevice(device);
    Assert::IsTrue(created, WhyTheDeviceFailed(device).c_str());
    Assert::IsNotNull(device.Device());
    Assert::IsNotNull(device.Queue());
    Assert::IsNotNull(device.Factory());
    Assert::IsTrue(device.IsWarp(), L"the software rasterizer was asked for and something else answered");

    // ADR-011 put every shader in this tree at 6.7. Create refuses a device below it, so a successful Create is
    // already the assertion; this states it, because it is the thing most likely to break on someone else's machine.
    Assert::IsTrue(device.HighestShaderModel() >= Neuron::REQUIRED_SHADER_MODEL,
                   L"the device reports a shader model below 6.7 yet Create succeeded");
  }

  TEST_METHOD(TheSceneTargetIsTheScreenAndReadsBackWhatWasClearedIntoIt)
  {
    Neuron::GraphicsDevice device;
    const bool created = CreateWarpDevice(device);
    Assert::IsTrue(created, WhyTheDeviceFailed(device).c_str());

    Neuron::SceneTarget target;
    const Neuron::SceneTarget::Desc desc{Neuron::SCREEN_WIDTH_PIXELS,
                                         Neuron::SCREEN_HEIGHT_PIXELS,
                                         DXGI_FORMAT_R8G8B8A8_UNORM,
                                         {CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2], CLEAR_COLOR[3]}};
    Assert::IsTrue(Neuron::SceneTarget::Create(device, desc, target), L"the scene target could not be created");
    Assert::AreEqual(Neuron::SCREEN_WIDTH_PIXELS, target.WidthPixels());
    Assert::AreEqual(Neuron::SCREEN_HEIGHT_PIXELS, target.HeightPixels());
    Assert::IsTrue(DXGI_FORMAT_R8G8B8A8_UNORM == target.ColorFormat(), L"the scene target is not R8G8B8A8_UNORM");

    Assert::IsTrue(ClearAndWait(device, target), L"the clear could not be submitted");

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(target.ReadBack(pixels), L"the readback failed");
    Assert::AreEqual(static_cast<std::size_t>(Neuron::SCREEN_WIDTH_PIXELS) * Neuron::SCREEN_HEIGHT_PIXELS, pixels.size());

    // Every pixel, not a sample of them: a clear that misses a row or a column is exactly the defect this catches,
    // and two million comparisons cost less than the rest of the test.
    std::size_t wrong = 0;
    std::size_t firstWrong = 0;
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
      if (pixels[index] != CLEAR_PIXEL)
      {
        if (wrong == 0)
        {
          firstWrong = index;
        }
        ++wrong;
      }
    }
    if (wrong != 0)
    {
      std::wstring message = L"pixels differing from the clear colour: " + std::to_wstring(wrong) + L" of " +
                             std::to_wstring(pixels.size()) + L"; first at index " + std::to_wstring(firstWrong);
      wchar_t got[64] = {};
      swprintf_s(got, L", which is 0x%08X not 0x%08X", pixels[firstWrong], CLEAR_PIXEL);
      Assert::Fail((message + got).c_str());
    }
  }

  TEST_METHOD(TheFitIsOneToOneOnlyWhenTheClientAreaIsTheScreen)
  {
    // ADR-009's three cases, at the sizes that actually occur.
    const auto exact = Neuron::PresentPass::Fit(1920, 1080, 1920, 1080);
    Assert::IsTrue(exact.filter == Neuron::PresentPass::Filter::None, L"1920x1080 must not be filtered at all");
    Assert::AreEqual(1920u, exact.widthPixels);
    Assert::AreEqual(1080u, exact.heightPixels);
    Assert::AreEqual(0, exact.leftPixels);
    Assert::AreEqual(0, exact.topPixels);

    const auto doubled = Neuron::PresentPass::Fit(1920, 1080, 3840, 2160);
    Assert::IsTrue(doubled.filter == Neuron::PresentPass::Filter::Point, L"an exact 2x must be point sampled");
    Assert::AreEqual(3840u, doubled.widthPixels);
    Assert::AreEqual(2160u, doubled.heightPixels);

    // The case ADR-010 knowingly made worse: 1440p is not an integer multiple, so it is bilinear and letterboxed.
    const auto awkward = Neuron::PresentPass::Fit(1920, 1080, 2560, 1440);
    Assert::IsTrue(awkward.filter == Neuron::PresentPass::Filter::Linear);
    Assert::AreEqual(2560u, awkward.widthPixels);
    Assert::AreEqual(1440u, awkward.heightPixels);

    // A 16:10 monitor: the same 1920x1080 rectangle, centred, with bars above and below.
    const auto tall = Neuron::PresentPass::Fit(1920, 1080, 1920, 1200);
    Assert::IsTrue(tall.filter == Neuron::PresentPass::Filter::Linear);
    Assert::AreEqual(1920u, tall.widthPixels);
    Assert::AreEqual(1080u, tall.heightPixels);
    Assert::AreEqual(0, tall.leftPixels);
    Assert::AreEqual(60, tall.topPixels);
  }

  TEST_METHOD(TheFitAlwaysFitsKeepsTheShapeAndStaysCentered)
  {
    // The property check NC-020 round 8 introduced, applied to the arithmetic that decides what a person sees. The
    // real function is swept rather than a transcription of it.
    for (std::uint32_t width = 320; width <= 4096; width += 17)
    {
      for (std::uint32_t height = 240; height <= 2304; height += 23)
      {
        const auto placement = Neuron::PresentPass::Fit(Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, width, height);
        std::wstring at = L" at " + std::to_wstring(width) + L"x" + std::to_wstring(height);
        Assert::IsTrue(placement.widthPixels > 0 && placement.heightPixels > 0, (L"empty placement" + at).c_str());
        Assert::IsTrue(placement.leftPixels >= 0 && placement.topPixels >= 0, (L"negative origin" + at).c_str());
        Assert::IsTrue(static_cast<std::uint64_t>(placement.leftPixels) + placement.widthPixels <= width,
                       (L"wider than the client area" + at).c_str());
        Assert::IsTrue(static_cast<std::uint64_t>(placement.topPixels) + placement.heightPixels <= height,
                       (L"taller than the client area" + at).c_str());

        // It reaches one of the two bounds: a fit smaller than it needs to be is a bug nothing else would catch.
        Assert::IsTrue(placement.widthPixels == width || placement.heightPixels == height, (L"not the largest that fits" + at).c_str());

        // The screen's shape, to within the pixel integer division costs.
        const std::int64_t cross = static_cast<std::int64_t>(placement.widthPixels) * Neuron::SCREEN_HEIGHT_PIXELS -
                                   static_cast<std::int64_t>(placement.heightPixels) * Neuron::SCREEN_WIDTH_PIXELS;
        const std::int64_t tolerance = Neuron::SCREEN_WIDTH_PIXELS + Neuron::SCREEN_HEIGHT_PIXELS;
        Assert::IsTrue(cross <= tolerance && -cross <= tolerance, (L"the shape was not kept" + at).c_str());
      }
    }
  }

  TEST_METHOD(TwoHundredFramesPresentWithoutTheDebugLayerComplaining)
  {
    // The fence is the point. A buffer is drawn again every second frame, and nothing may touch one the GPU is still
    // reading; two hundred frames with the debug layer set to break on error is what proves it, and the swap chain is
    // real rather than stubbed, on a window that is created and never shown.
    Neuron::Window window;
    const Neuron::Window::Desc windowDesc{L"NomadCommanderFrameLoopTest"};
    Assert::IsTrue(Neuron::Window::Create(windowDesc, window), L"the test window could not be created");

    std::uint32_t clientWidth = 0;
    std::uint32_t clientHeight = 0;
    Assert::IsTrue(window.ClientSizePixels(clientWidth, clientHeight));

    Neuron::GraphicsDevice device;
    const bool created = CreateWarpDevice(device);
    Assert::IsTrue(created, WhyTheDeviceFailed(device).c_str());

    Neuron::SwapChainTarget swapChain;
    const Neuron::SwapChainTarget::Desc swapChainDesc{clientWidth, clientHeight};
    Assert::IsTrue(Neuron::SwapChainTarget::Create(device, window.Handle(), swapChainDesc, swapChain),
                   L"the swap chain could not be created on the test window");

    Neuron::SceneTarget scene;
    const Neuron::SceneTarget::Desc sceneDesc{Neuron::SCREEN_WIDTH_PIXELS,
                                              Neuron::SCREEN_HEIGHT_PIXELS,
                                              DXGI_FORMAT_R8G8B8A8_UNORM,
                                              {CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2], CLEAR_COLOR[3]}};
    Assert::IsTrue(Neuron::SceneTarget::Create(device, sceneDesc, scene), L"the scene target could not be created");

    Neuron::PresentPass present;
    Assert::IsTrue(Neuron::PresentPass::Create(device, scene, present), L"the present pass could not be created");

    constexpr int FRAMES = 200;

    const float letterbox[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int frame = 0; frame < FRAMES; ++frame)
    {
      ID3D12GraphicsCommandList* const commandList = swapChain.BeginFrame(letterbox);
      Assert::IsNotNull(commandList, (L"BeginFrame returned nothing at frame " + std::to_wstring(frame)).c_str());
      scene.Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
      scene.Clear(commandList);
      present.Execute(commandList, scene, swapChain);
      Assert::IsTrue(swapChain.EndFrame(), (L"EndFrame failed at frame " + std::to_wstring(frame)).c_str());
    }
    swapChain.WaitForGpu();
    Assert::AreEqual(static_cast<std::uint64_t>(FRAMES), swapChain.PresentedFrames());
    Assert::IsTrue(swapChain.Fault() == Neuron::TargetFault::None, L"a fault was recorded across the run");
  }
};

} // namespace NeuronClientTests
