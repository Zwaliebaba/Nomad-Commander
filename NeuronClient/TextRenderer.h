// NeuronClient/TextRenderer.h
#pragma once

#include "GlyphPipeline.h"
#include "IconAtlas.h"
#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "SceneTarget.h"
#include "SwapChainTarget.h"

#include <cstdint>
#include <d3d12.h>
#include <string_view>
#include <wrl/client.h>

namespace Neuron
{

/// One vertex of a glyph quad. The texel coordinate is a float pair because it interpolates across the quad; the
/// pixel shader truncates it back to an integer, which is what makes the scaling exact.
struct GlyphVertex
{
  float positionXPixels;
  float positionYPixels;
  float texelX;
  float texelY;
  std::uint32_t colorRgba;
};

/// How much room a piece of text takes.
struct TextExtent
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
};

/// Text on screen, from the 768 bytes of BitmapFont.h and nothing on disk (AGENTS.md R13).
///
/// The font is uploaded once as a 128x48 R8_UINT atlas, sixteen glyphs to a row, and read with
/// `Texture2D<uint>::Load()` — integer texel coordinates, no sampler, nothing to filter. Scaling is by whole numbers
/// only, so a glyph authored as a bit pattern is drawn as that bit pattern, three pixels to a texel at GLYPH_SCALE.
class TextRenderer
{
public:
  static constexpr std::uint32_t GLYPH_WIDTH_PIXELS = 8;
  static constexpr std::uint32_t GLYPH_HEIGHT_PIXELS = 8;

  /// Three, because the screen is 1920x1080: a 24-pixel cell divides it exactly, into the same 80x45 grid a 16-pixel
  /// cell gave when the screen was 1280x720 (AGENTS.md R12, ADR-008).
  static constexpr std::uint32_t GLYPH_SCALE = 3;

  static constexpr std::uint32_t ATLAS_COLUMNS = 16;
  static constexpr std::uint32_t ATLAS_WIDTH_TEXELS = ATLAS_COLUMNS * GLYPH_WIDTH_PIXELS;
  /// Six rows of glyphs and two of icons. An icon is eight by eight and monochrome, which is exactly what a glyph
  /// is, so it shares this atlas, this pipeline and this complete absence of a sampler (NC-026).
  static constexpr std::uint32_t GLYPH_ROWS = 6;
  static constexpr std::uint32_t ICON_ROWS = 2;
  static constexpr std::uint32_t ATLAS_HEIGHT_TEXELS = (GLYPH_ROWS + ICON_ROWS) * GLYPH_HEIGHT_PIXELS;

  /// Icons follow the 96 glyphs in the same cell numbering, so one helper turns either into a texel origin.
  static constexpr std::uint32_t ICON_FIRST_CELL = 96;

  static constexpr std::uint32_t FRAMES_IN_FLIGHT = SwapChainTarget::BUFFER_COUNT;

  /// Characters a frame may draw. Six vertices each, so this is a little over a hundred thousand vertices a slice —
  /// the 80x45 grid is 3,600 cells, so a frame that fills the screen with text uses a fraction of it.
  static constexpr std::uint32_t MAX_GLYPHS_PER_FRAME = 16384;

  TextRenderer() = default;
  TextRenderer(const TextRenderer&) = delete;
  TextRenderer& operator=(const TextRenderer&) = delete;
  TextRenderer(TextRenderer&&) = delete;
  TextRenderer& operator=(TextRenderer&&) = delete;
  ~TextRenderer();

  /// Builds the atlas from FONT_8X8_GLYPHS, uploads it once, and writes its view into the pipeline's heap. The upload
  /// buffer is a local that goes away as soon as the copy has fenced.
  [[nodiscard]] static bool Create(GraphicsDevice& _device, const GlyphPipeline& _pipeline, TextRenderer& _outRenderer) noexcept;

  void Begin(ID3D12GraphicsCommandList* _commandList, const GlyphPipeline& _pipeline, std::uint32_t _frameSlot,
             std::uint32_t _targetWidthPixels, std::uint32_t _targetHeightPixels) noexcept;

  /// Draws text with its top-left corner at (x, y). A character this font does not have draws the 0x7F box, so a
  /// missing glyph is visible rather than a gap.
  void Draw(float _xPixels, float _yPixels, std::string_view _text, std::uint32_t _colorRgba, std::uint32_t _scale = GLYPH_SCALE) noexcept;

  /// What Draw will cover, so a layout can be computed before anything is drawn. It agrees with Draw by construction:
  /// both are the character count times the cell, and neither kerns.
  [[nodiscard]] static TextExtent Measure(std::string_view _text, std::uint32_t _scale = GLYPH_SCALE) noexcept;

  /// One icon, tinted, at the same integer scale as text. It accompanies a label rather than replacing one (UI §4).
  void DrawIcon(float _xPixels, float _yPixels, Icon _icon, std::uint32_t _colorRgba, std::uint32_t _scale = GLYPH_SCALE) noexcept;

  void End() noexcept;

  [[nodiscard]] std::uint32_t GlyphCount() const noexcept
  {
    return m_glyphCount;
  }

  [[nodiscard]] bool Overflowed() const noexcept
  {
    return m_overflowed;
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
  /// Appends one quad. Both Draw and DrawIcon are this, differing only in which atlas cell they point at.
  void PushQuad(float _leftPixels, float _topPixels, float _widthPixels, float _heightPixels, float _texelX, float _texelY,
                std::uint32_t _colorRgba) noexcept;

  Microsoft::WRL::ComPtr<ID3D12Resource> m_atlas;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
  GlyphVertex* m_mapped = nullptr;
  ID3D12GraphicsCommandList* m_commandList = nullptr;
  const GlyphPipeline* m_pipeline = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS m_bufferAddress = 0;
  std::uint32_t m_frameSlot = 0;
  std::uint32_t m_glyphCount = 0;
  std::uint32_t m_targetWidthPixels = 0;
  std::uint32_t m_targetHeightPixels = 0;
  HRESULT m_result = S_OK;
  TargetFault m_fault = TargetFault::None;
  bool m_overflowed = false;
};

} // namespace Neuron
