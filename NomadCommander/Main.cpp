// NomadCommander/Main.cpp
#include "pch.h"
#include "Window.h"
#include "GraphicsDevice.h"
#include "SceneTarget.h"
#include "SwapChainTarget.h"
#include "PresentPass.h"
#include "Debug.h"

#include <cstdio>

namespace
{

// The colour the scene target is cleared to, until something draws into it. Provisional: the desk's real background
// is UI §1's and arrives with NC-025. Written as bytes over 255 because the target is R8G8B8A8_UNORM and not _SRGB
// (AGENTS.md R12), so these reach the glass as exactly these bytes and the suite can assert them.
constexpr float SCENE_CLEAR_COLOR[4] = {0x1E / 255.0f, 0x28 / 255.0f, 0x3C / 255.0f, 1.0f};

// What shows in the letterbox bars when the monitor is not the screen's shape. Black, so the bars read as absence
// rather than as part of the picture.
constexpr float LETTERBOX_COLOR[4] = {0.0f, 0.0f, 0.0f, 1.0f};

} // namespace

// The executable's entry point. It opens the borderless window the game presents into -- the whole of the primary
// monitor (AGENTS.md R12, ADR-010) -- builds the device, the swap chain, the 1920x1080 scene target and the present
// pass, and runs the frame loop until the window closes. NC-070's composition root replaces this with the hosted
// session and the client; until then the frame clears the screen and presents it, which is the whole of what there is
// to see. The parameters stay unnamed until something reads them; /W4 /WX would otherwise report them unreferenced.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
  const Neuron::Window::Desc windowDesc{L"Nomad Commander"};
  Neuron::Window window;
  if (!Neuron::Window::Create(windowDesc, window))
  {
    Neuron::DebugPrint("Nomad Commander: the window could not be created.");
    return 1;
  }

  std::uint32_t clientWidth = 0;
  std::uint32_t clientHeight = 0;
  if (!window.ClientSizePixels(clientWidth, clientHeight))
  {
    Neuron::DebugPrint("Nomad Commander: the window would not report its client area.");
    return 1;
  }

  Neuron::GraphicsDevice device;
  const Neuron::GraphicsDevice::Desc deviceDesc{false, true};
  if (!Neuron::GraphicsDevice::Create(deviceDesc, device))
  {
    Neuron::DebugPrint(device.Fault() == Neuron::DeviceFault::ShaderModelTooLow
                         ? "Nomad Commander: this GPU does not support shader model 6.7 (ADR-011)."
                         : "Nomad Commander: no Direct3D 12 device could be created.");
    return 1;
  }

  Neuron::SwapChainTarget swapChain;
  const Neuron::SwapChainTarget::Desc swapChainDesc{clientWidth, clientHeight};
  if (!Neuron::SwapChainTarget::Create(device, window.Handle(), swapChainDesc, swapChain))
  {
    Neuron::DebugPrint("Nomad Commander: the swap chain could not be created.");
    return 1;
  }

  Neuron::SceneTarget scene;
  const Neuron::SceneTarget::Desc sceneDesc{Neuron::SCREEN_WIDTH_PIXELS,
                                            Neuron::SCREEN_HEIGHT_PIXELS,
                                            DXGI_FORMAT_R8G8B8A8_UNORM,
                                            {SCENE_CLEAR_COLOR[0], SCENE_CLEAR_COLOR[1], SCENE_CLEAR_COLOR[2], SCENE_CLEAR_COLOR[3]}};
  if (!Neuron::SceneTarget::Create(device, sceneDesc, scene))
  {
    Neuron::DebugPrint("Nomad Commander: the 1920x1080 scene target could not be created.");
    return 1;
  }

  Neuron::PresentPass present;
  if (!Neuron::PresentPass::Create(device, scene, present))
  {
    Neuron::DebugPrint("Nomad Commander: the present pass could not be created.");
    return 1;
  }

  window.Show();

  bool faulted = false;
  while (window.PumpMessages())
  {
    ID3D12GraphicsCommandList* const commandList = swapChain.BeginFrame(LETTERBOX_COLOR);
    if (commandList == nullptr)
    {
      faulted = true;
      break;
    }

    // Everything the game will ever draw happens here, into the scene target, at 1920x1080 and no other size
    // (ADR-009). For now that is a clear and nothing else.
    scene.Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
    scene.Clear(commandList);

    present.Execute(commandList, scene, swapChain);

    if (!swapChain.EndFrame())
    {
      faulted = true;
      break;
    }
  }

  // Nothing is destroyed while the GPU is still reading it.
  swapChain.WaitForGpu();

  if (faulted)
  {
    Neuron::DebugPrint(swapChain.Fault() == Neuron::TargetFault::DeviceRemoved
                         ? "Nomad Commander: the graphics device was removed; the frame loop stopped."
                         : "Nomad Commander: the frame loop stopped on a presentation fault.");
    return 1;
  }

  // The frame count on the way out, so that "the debug layer said nothing across N frames" is a number somebody read
  // rather than one inferred from a clock. The debug layer writes to the same stream, so a run's whole story is in one
  // place. NC-032 gives instrumentation a home of its own (R24); this is the debug output and not that.
  char summary[128] = {};
  sprintf_s(summary, "Nomad Commander: %llu frames presented, %s.", static_cast<unsigned long long>(swapChain.PresentedFrames()),
            present.LastPlacement().filter == Neuron::PresentPass::Filter::None    ? "copied 1:1"
            : present.LastPlacement().filter == Neuron::PresentPass::Filter::Point ? "point sampled"
                                                                                   : "bilinear and letterboxed");
  Neuron::DebugPrint(summary);
  return 0;
}
