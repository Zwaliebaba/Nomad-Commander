// NeuronClient/PipelineDefaults.h
#pragma once

#include "NeuronCore.h"

#include <d3d12.h>
#include <dxgi1_6.h>

namespace Neuron
{

/// The pipeline state this tree starts from, so that a pass states what it *changes* rather than restating what every
/// pass wants.
///
/// **These are defaults, not rules** (AGENTS.md §5, 2026-09-16): blending and samplers are a pass's own business, and
/// a pass that needs a blend mode sets one without asking anybody. Opaque is here because it is the common case and
/// the cheap one. `SampleDesc()` is the exception and it is not a preference at all — see below.
///
/// Every field of every description is named. That is not thoroughness for its own sake: a good many D3D12 enums have
/// no zero-valued enumerator, so the idiomatic `D3D12_RASTERIZER_DESC state{}` leaves values in the struct that those
/// enums have no name for. `d3dx12.h`'s `CD3DX12_*` constructors exist to solve exactly that and R14 excludes them, so
/// this file is that, written by hand.
class PipelineDefaults
{
public:
  /// Solid, unculled, and no anti-aliased lines. Culling is off because nothing in a 2D pass has a meaningful winding
  /// and the 3D map (NC-027) will set its own; anti-aliased lines are off because the map's lines are one pixel wide
  /// and aliased by design.
  [[nodiscard]] static D3D12_RASTERIZER_DESC Rasterizer() noexcept;

  /// No blending, every channel written. A pass that wants alpha over the top sets its own blend description; this is
  /// what the ones that do not want it get.
  [[nodiscard]] static D3D12_BLEND_DESC Blend() noexcept;

  /// Depth and stencil off, with the operations still named so a disabled stage holds no unnamed enum value. NC-027
  /// brings a depth buffer with the 3D map and will set its own.
  [[nodiscard]] static D3D12_DEPTH_STENCIL_DESC DepthStencil() noexcept;

  /// One sample, and **this one is a fact rather than a default.** Direct3D 12 supports only the flip-model swap
  /// effects and DXGI will not multisample a flip-model back buffer, so a pipeline that writes to one cannot be
  /// multisampled. A pass drawing into the scene target *may* be (ADR-009 made that possible by putting a target
  /// between the game and the back buffer), and such a pass overrides this deliberately; NC-027 decides whether the
  /// map is worth it, with a measurement.
  [[nodiscard]] static DXGI_SAMPLE_DESC SampleDesc() noexcept;
};

} // namespace Neuron
