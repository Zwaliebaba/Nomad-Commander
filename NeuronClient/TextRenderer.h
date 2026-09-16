// NeuronClient/TextRenderer.h
#pragma once

#include "Font.h"
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
/// pixel shader truncates it back to an integer, so each screen pixel reads exactly the texel under it.
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

/// Text on screen, from the coverage in FontCoverage.h and nothing on disk (AGENTS.md R13; ADR-016).
///
/// The three faces and the icons are uploaded once as one R8_UNORM atlas and read with `Texture2D<float>::Load()` —
/// integer texel coordinates, no sampler, nothing to filter. A glyph is drawn at the size it was baked, one texel a
/// pixel, and its coverage is what the pixel shader blends the text colour by: that is how anti-aliased type reaches
/// the glass exactly as the rasterizer left it, and why nothing here scales.
class TextRenderer
{
public:
  /// Sixteen cells to an atlas row, whatever the face's advance.
  static constexpr std::uint32_t ATLAS_COLUMNS = 16;

  /// The rows a face takes: 96 glyphs, sixteen to a row.
  static constexpr std::uint32_t FACE_ROWS = (FONT_GLYPH_COUNT + ATLAS_COLUMNS - 1) / ATLAS_COLUMNS;
  static constexpr std::uint32_t FACE_HEIGHT_TEXELS = FACE_ROWS * FONT_LINE_HEIGHT_PIXELS;

  /// An icon is a cell: the 8×8 art of IconAtlas.h at three texels a bit, which keeps NC-026's icons the crisp pixel
  /// art they were drawn as, at the size they always were.
  static constexpr std::uint32_t ICON_PIXELS = FONT_LINE_HEIGHT_PIXELS;
  static constexpr std::uint32_t ICON_TEXELS_PER_BIT = ICON_PIXELS / ICON_ART_PIXELS;
  static_assert(ICON_TEXELS_PER_BIT * ICON_ART_PIXELS == ICON_PIXELS, "an icon's art must divide the cell exactly");

  /// The atlas: the faces stacked in `Font` order, then one row of icons. Sixteen icons wide, which is wider than
  /// sixteen glyphs of any face.
  static constexpr std::uint32_t ATLAS_WIDTH_TEXELS = ATLAS_COLUMNS * ICON_PIXELS;
  static constexpr std::uint32_t ICONS_ORIGIN_Y_TEXELS = FONT_COUNT * FACE_HEIGHT_TEXELS;
  static constexpr std::uint32_t ATLAS_HEIGHT_TEXELS = ICONS_ORIGIN_Y_TEXELS + ICON_PIXELS;
  static_assert(ICON_COUNT <= ATLAS_COLUMNS, "the icons fit one atlas row");
  static_assert(FONT_BODY_ADVANCE_PIXELS <= ICON_PIXELS && FONT_SMALL_ADVANCE_PIXELS <= ICON_PIXELS &&
                  FONT_TITLE_ADVANCE_PIXELS <= ICON_PIXELS,
                "a face's row of sixteen glyphs fits the atlas width");

  static constexpr std::uint32_t FRAMES_IN_FLIGHT = SwapChainTarget::BUFFER_COUNT;

  /// Characters a frame may draw. Six vertices each, so this is a little under a hundred thousand vertices a slice —
  /// the 160×45 characters of a screen full of Body text use a fraction of it.
  static constexpr std::uint32_t MAX_GLYPHS_PER_FRAME = 16384;

  TextRenderer() = default;
  TextRenderer(const TextRenderer&) = delete;
  TextRenderer& operator=(const TextRenderer&) = delete;
  TextRenderer(TextRenderer&&) = delete;
  TextRenderer& operator=(TextRenderer&&) = delete;
  ~TextRenderer();

  /// Builds the atlas from the baked faces and the icon art, uploads it once, and writes its view into the pipeline's
  /// heap. The upload buffer is a local that goes away as soon as the copy has fenced.
  [[nodiscard]] static bool Create(GraphicsDevice& _device, const GlyphPipeline& _pipeline, TextRenderer& _outRenderer) noexcept;

  void Begin(ID3D12GraphicsCommandList* _commandList, const GlyphPipeline& _pipeline, std::uint32_t _frameSlot,
             std::uint32_t _targetWidthPixels, std::uint32_t _targetHeightPixels) noexcept;

  /// Draws text with its top-left corner at (x, y) in the face given. A character the set does not hold draws the
  /// 0x7F box, so a gap in the data is visible rather than a hole.
  void Draw(float _xPixels, float _yPixels, std::string_view _text, std::uint32_t _colorRgba, Font _font = Font::Body) noexcept;

  /// What Draw will cover, so a layout can be computed before anything is drawn. It agrees with Draw by construction:
  /// both are the character count times the face's advance, by one line, and neither kerns.
  [[nodiscard]] static TextExtent Measure(std::string_view _text, Font _font = Font::Body) noexcept;

  /// One icon, tinted, a cell square. It accompanies a label rather than replacing one (UI §4).
  void DrawIcon(float _xPixels, float _yPixels, Icon _icon, std::uint32_t _colorRgba) noexcept;

  void End() noexcept;

  /// Where a face's glyph starts in the atlas, in texels. Public so a test can read the atlas the way the renderer
  /// does.
  static void GlyphOrigin(Font _font, std::uint32_t _glyphIndex, std::uint32_t& _outTexelX, std::uint32_t& _outTexelY) noexcept;

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
  /// Appends one quad. Both Draw and DrawIcon are this, differing only in which atlas cell they point at and how big
  /// it is.
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
