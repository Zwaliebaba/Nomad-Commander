// NeuronClient/GraphicsDevice.h
#pragma once

#include "NeuronCore.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>

namespace Neuron
{

/// The shader model every pipeline in this tree is compiled at (ADR-011). A device below it cannot create any pipeline
/// state here, so it is checked once, at device creation, where the fault can say so in words.
inline constexpr D3D_SHADER_MODEL REQUIRED_SHADER_MODEL = D3D_SHADER_MODEL_6_7;

/// Why a device could not be created. Each names one call, so a log says which rather than only that.
enum class DeviceFault : std::uint8_t
{
  None,
  FactoryCreation,
  NoAdapter,
  DeviceCreation,
  ShaderModelTooLow,
  CommandQueue
};

/// The Direct3D 12 device, its DXGI factory and the one direct queue everything is submitted on. Everything the
/// renderer will ever need from D3D12 that is not a pipeline lives here, and nothing here knows what a rectangle is.
///
/// Every COM object is a ComPtr (AGENTS.md R12): there is no AddRef or Release in this tree.
class GraphicsDevice
{
public:
  struct Desc
  {
    /// Use the software rasterizer whatever hardware is present. The tests set it; the game does not.
    bool useWarp;
    /// Turn on the debug layer and break on error and corruption. Meaningful in _DEBUG only.
    bool enableDebugLayer;
  };

  GraphicsDevice() = default;
  GraphicsDevice(const GraphicsDevice&) = delete;
  GraphicsDevice& operator=(const GraphicsDevice&) = delete;
  GraphicsDevice(GraphicsDevice&&) = delete;
  GraphicsDevice& operator=(GraphicsDevice&&) = delete;
  ~GraphicsDevice() = default;

  /// Creates the factory, picks an adapter, creates the device and the direct queue.
  ///
  /// The adapter is the first hardware one that makes a `D3D_FEATURE_LEVEL_11_0` device, in DXGI's
  /// high-performance order; WARP when asked for, and WARP when no hardware adapter will do it, so that a machine
  /// with no usable GPU still runs rather than failing at the first call.
  [[nodiscard]] static bool Create(const Desc& _desc, GraphicsDevice& _outDevice) noexcept;

  [[nodiscard]] ID3D12Device* Device() const noexcept
  {
    return m_device.Get();
  }

  [[nodiscard]] ID3D12CommandQueue* Queue() const noexcept
  {
    return m_queue.Get();
  }

  [[nodiscard]] IDXGIFactory6* Factory() const noexcept
  {
    return m_factory.Get();
  }

  [[nodiscard]] DeviceFault Fault() const noexcept
  {
    return m_fault;
  }

  /// The HRESULT of the call that failed, or of the last call that mattered.
  [[nodiscard]] HRESULT Result() const noexcept
  {
    return m_result;
  }

  /// The adapter's own description, for the report and the instrumentation log. Empty before a successful Create.
  [[nodiscard]] const wchar_t* AdapterName() const noexcept
  {
    return m_adapterName;
  }

  [[nodiscard]] bool IsWarp() const noexcept
  {
    return m_isWarp;
  }

  /// The highest shader model the device reports, which is at least REQUIRED_SHADER_MODEL after a successful Create.
  [[nodiscard]] D3D_SHADER_MODEL HighestShaderModel() const noexcept
  {
    return m_shaderModel;
  }

private:
  Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
  wchar_t m_adapterName[128] = {};
  HRESULT m_result = S_OK;
  D3D_SHADER_MODEL m_shaderModel = D3D_SHADER_MODEL_5_1;
  DeviceFault m_fault = DeviceFault::None;
  bool m_isWarp = false;
};

} // namespace Neuron
