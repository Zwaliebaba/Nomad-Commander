// NeuronClient/PresentPass.h
#pragma once

#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "SceneTarget.h"
#include "SwapChainTarget.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace Neuron
{

/// The one step in the frame that looks at the difference between the screen and the window (ADR-009, ADR-010).
///
/// Everything else draws into the 1920x1080 scene target and never asks how big anything is. This takes that target
/// and puts it into the back buffer, at the largest size of the screen's shape that the client area holds, centred,
/// with the bars left in whatever colour BeginFrame cleared to.
///
/// It takes the cheapest path available to it, which is R12's requirement and not an optimisation:
///   * the client area IS 1920x1080 -- a straight copy, no pipeline, no sampler, no filtering of any kind;
///   * the client area is an exact integer multiple of it -- point sampling, so a glyph's bit pattern stays a bit
///     pattern, just bigger;
///   * anything else -- bilinear, letterboxed, which is the case ADR-009 admits is a real cost and the one ADR-010
///     knowingly made more common on displays larger than the screen.
class PresentPass
{
public:
  /// What the present step does with the pixels, which is the whole of ADR-009's decision in one value.
  enum class Filter : std::uint8_t
  {
    /// No sampling at all: the client area is the screen and the pass copies.
    None,
    /// An exact integer multiple; texels stay square.
    Point,
    /// Anything else. Soft, and the reason the two cases above are named separately.
    Linear
  };

  /// Where the scene target lands in the back buffer, and how it gets there.
  struct Placement
  {
    std::int32_t leftPixels;
    std::int32_t topPixels;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    Filter filter;
  };

  PresentPass() = default;
  PresentPass(const PresentPass&) = delete;
  PresentPass& operator=(const PresentPass&) = delete;
  PresentPass(PresentPass&&) = delete;
  PresentPass& operator=(PresentPass&&) = delete;
  ~PresentPass() = default;

  /// The fit arithmetic, on its own and with no device in sight, because it decides what a person sees and deserves to
  /// be tested over a swept input space rather than at one window size (the technique NC-020 round 8 introduced).
  [[nodiscard]] static Placement Fit(std::uint32_t _sceneWidthPixels, std::uint32_t _sceneHeightPixels, std::uint32_t _clientWidthPixels,
                                     std::uint32_t _clientHeightPixels) noexcept;

  /// Builds the root signature, the pipeline and the one shader-visible descriptor that names the scene target.
  [[nodiscard]] static bool Create(GraphicsDevice& _device, const SceneTarget& _scene, PresentPass& _outPass) noexcept;

  /// Records the present step into an open frame. The back buffer is already a bound, cleared render target.
  void Execute(ID3D12GraphicsCommandList* _commandList, SceneTarget& _scene, SwapChainTarget& _swapChain) noexcept;

  /// What the last Execute did, for the report and the instrumentation log.
  [[nodiscard]] Placement LastPlacement() const noexcept
  {
    return m_lastPlacement;
  }

  [[nodiscard]] TargetFault Fault() const noexcept
  {
    return m_fault;
  }

  [[nodiscard]] HRESULT Result() const noexcept
  {
    return m_result;
  }

private:
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_sceneDescriptorHeap;
  Placement m_lastPlacement{};
  HRESULT m_result = S_OK;
  TargetFault m_fault = TargetFault::None;
};

} // namespace Neuron
