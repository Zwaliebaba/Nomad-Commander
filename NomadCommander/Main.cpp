// NomadCommander/Main.cpp
#include "pch.h"
#include "Window.h"
#include "GraphicsDevice.h"
#include "SceneTarget.h"
#include "SwapChainTarget.h"
#include "PresentPass.h"
#include "PrimitiveBatch.h"
#include "PrimitivePipeline.h"
#include "GlyphPipeline.h"
#include "TextRenderer.h"
#include "InputState.h"
#include "Ui.h"
#include "Palette.h"
#include "Rect.h"
#include "UiState.h"
#include "IconAtlas.h"
#include "Camera.h"
#include "DepthTarget.h"
#include "MeshBuilder.h"
#include "MeshPipeline.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>
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

// NC-022's test pattern, and nothing more: one of each primitive at a stated place, so that a person looking at the
// screen can check the renderer against the numbers. NC-025 replaces it with the desk. Red is in the low byte, so
// 0xFF0000FF is opaque red (see PrimitiveBatch.h).
constexpr std::uint32_t SLATE = 0xFF5A4636u;   // a muted blue-grey
constexpr std::uint32_t AMBER = 0xFF21A5E8u;   // the warm accent
constexpr std::uint32_t PALE = 0xFFC8C8C8u;    // near-white
constexpr std::uint32_t CRIMSON = 0xFF3A3AD2u; // a warning red

void DrawTestPattern(Neuron::PrimitiveBatch& _batch)
{
  // A filled rectangle at a stated place, and its outline one pixel outside it, so the two can be told apart.
  _batch.FillRect(120.0f, 120.0f, 400.0f, 220.0f, SLATE);
  _batch.Rect(118.0f, 118.0f, 404.0f, 224.0f, PALE);

  // A row of lines fanning out from a point, which is what shows whether the line pipeline and painter's order work.
  for (int step = 0; step <= 10; ++step)
  {
    const float toY = 420.0f + static_cast<float>(step) * 40.0f;
    _batch.Line(120.0f, 420.0f, 620.0f, toY, AMBER);
  }

  // A convex polygon: a hexagon, the shape the map's systems will want.
  const Neuron::Point hexagon[] = {{820.0f, 140.0f}, {960.0f, 180.0f}, {1000.0f, 300.0f},
                                   {900.0f, 380.0f}, {780.0f, 340.0f}, {750.0f, 220.0f}};
  _batch.FillPolygon(hexagon, SLATE);

  // Circles, which are polygons with enough sides not to look like one, at growing radii.
  for (int index = 0; index < 5; ++index)
  {
    const float radius = 12.0f + static_cast<float>(index) * 14.0f;
    const Neuron::Point center{820.0f + static_cast<float>(index) * 110.0f, 560.0f};
    _batch.FillCircle(center, radius, Neuron::PrimitiveBatch::DEFAULT_CIRCLE_SEGMENTS, AMBER);
  }

  // Painter's order, stated as a picture: a rectangle, then a line across it, then a smaller rectangle over both.
  // If the batch sorted by pipeline the line would come out on top, and this is what would show it.
  _batch.FillRect(1300.0f, 140.0f, 400.0f, 160.0f, SLATE);
  _batch.Line(1300.0f, 220.0f, 1700.0f, 220.0f, CRIMSON);
  _batch.FillRect(1420.0f, 190.0f, 160.0f, 60.0f, PALE);

  // The screen's corners, one pixel each, which is how a person checks that nothing is off by one or scaled.
  _batch.FillRect(0.0f, 0.0f, 1.0f, 1.0f, CRIMSON);
  _batch.FillRect(static_cast<float>(Neuron::SCREEN_WIDTH_PIXELS) - 1.0f, 0.0f, 1.0f, 1.0f, CRIMSON);
  _batch.FillRect(0.0f, static_cast<float>(Neuron::SCREEN_HEIGHT_PIXELS) - 1.0f, 1.0f, 1.0f, CRIMSON);
  _batch.FillRect(static_cast<float>(Neuron::SCREEN_WIDTH_PIXELS) - 1.0f, static_cast<float>(Neuron::SCREEN_HEIGHT_PIXELS) - 1.0f, 1.0f,
                  1.0f, CRIMSON);
}

// NC-023's text pattern: the whole printable set at both scales, so a person can read every glyph and check the
// scaling, plus a sentence of the kind the desk will actually carry.
void DrawTextPattern(Neuron::TextRenderer& _text)
{
  // ASCII 0x20 to 0x7E, in rows of 32, at the screen's own GLYPH_SCALE. This is the whole set the client can draw
  // (UI §6), so displaying the whole set is the test.
  char row[33] = {};
  for (int block = 0; block < 3; ++block)
  {
    int length = 0;
    for (int index = 0; index < 32; ++index)
    {
      const int codepoint = 0x20 + block * 32 + index;
      if (codepoint > 0x7E)
      {
        break;
      }
      row[length++] = static_cast<char>(codepoint);
    }
    row[length] = '\0';
    _text.Draw(120.0f, 700.0f + static_cast<float>(block) * 30.0f, row, PALE, Neuron::TextRenderer::GLYPH_SCALE);
  }

  // The same set again at scale 1, immediately below, so the two can be compared without moving your head. At scale 1
  // a glyph is the bit pattern it was authored as, one texel a pixel.
  for (int block = 0; block < 3; ++block)
  {
    int length = 0;
    for (int index = 0; index < 32; ++index)
    {
      const int codepoint = 0x20 + block * 32 + index;
      if (codepoint > 0x7E)
      {
        break;
      }
      row[length++] = static_cast<char>(codepoint);
    }
    row[length] = '\0';
    _text.Draw(120.0f, 810.0f + static_cast<float>(block) * 10.0f, row, AMBER, 1);
  }

  _text.Draw(120.0f, 60.0f, "NOMAD COMMANDER", PALE, Neuron::TextRenderer::GLYPH_SCALE);
  _text.Draw(120.0f, 880.0f, "The Kessel Combine paid for an escort, not for questions. (jump 3/7)", PALE,
             Neuron::TextRenderer::GLYPH_SCALE);
  _text.Draw(120.0f, 930.0f, "Reliability 62% - source: courier, 4 days old - confidence falling", AMBER,
             Neuron::TextRenderer::GLYPH_SCALE);
}

// NC-024's visible half: a crosshair where the mouse is, so that "the pointer is here" is a thing a person can see
// rather than a number to trust, and a box that lights while a button is held.
void DrawPointer(Neuron::PrimitiveBatch& _batch, const Neuron::InputState& _input)
{
  const Neuron::MousePoint mouse = _input.MousePosition();
  const auto x = static_cast<float>(mouse.xPixels);
  const auto y = static_cast<float>(mouse.yPixels);
  _batch.Line(x - 12.0f, y, x + 12.0f, y, PALE);
  _batch.Line(x, y - 12.0f, x, y + 12.0f, PALE);

  // One box a button, filled while it is down. Left, right, middle, left to right.
  const Neuron::MouseButton buttons[] = {Neuron::MouseButton::Left, Neuron::MouseButton::Right, Neuron::MouseButton::Middle};
  for (int index = 0; index < 3; ++index)
  {
    const float boxX = 1500.0f + static_cast<float>(index) * 34.0f;
    if (_input.MouseDown(buttons[index]))
    {
      _batch.FillRect(boxX, 700.0f, 28.0f, 28.0f, AMBER);
    }
    _batch.Rect(boxX, 700.0f, 28.0f, 28.0f, PALE);
  }
}

// The numbers behind the crosshair, so the readout and the picture can be checked against each other.
void DrawInputReadout(Neuron::TextRenderer& _text, const Neuron::InputState& _input)
{
  const Neuron::MousePoint mouse = _input.MousePosition();
  char line[96] = {};
  sprintf_s(line, "MOUSE %4d,%-4d  WHEEL %+3d  FOCUS %s", mouse.xPixels, mouse.yPixels, _input.WheelDelta(),
            _input.HasFocus() ? "yes" : "no ");
  _text.Draw(1200.0f, 620.0f, line, PALE, 2);

  // The typed characters, echoed, which is the only way to see that WM_CHAR is arriving at all.
  char typed[Neuron::InputState::MAX_TYPED_CHARACTERS + 1] = {};
  const std::u16string_view characters = _input.TypedCharacters();
  std::size_t length = 0;
  for (const char16_t unit : characters)
  {
    if (unit >= 0x20 && unit < 0x7F && length + 1 < sizeof typed)
    {
      typed[length++] = static_cast<char>(unit);
    }
  }
  typed[length] = '\0';
  if (length != 0)
  {
    _text.Draw(1200.0f, 650.0f, typed, AMBER, 2);
  }
}

// Everything a screen keeps between frames. Immediate mode means `Ui` holds three ids and nothing else (ADR-012), so
// a scroll position, a selection and a tab live here — in the screen, which is what Phase 5's screens will do too.
struct DeskDemo
{
  std::vector<std::string> itemStorage;
  std::vector<std::string_view> items;
  Neuron::ScrollState scroll;
  Neuron::FieldState priceField;
  std::int32_t activeTab = 0;
  std::int32_t selected = 0;
  std::int32_t withdrawPercent = 40;
  std::int32_t priceCredits = 1140;
  bool marked = false;
  bool showDetail = false;
  bool asking = false;
  int committed = 0;

  DeskDemo()
  {
    // Two hundred, which is the size NC-026's criterion names and the size the board really reaches.
    for (int index = 0; index < 200; ++index)
    {
      itemStorage.push_back("report " + std::to_string(index) + " | courier | " + std::to_string(index % 9 + 1) + " days old");
    }
    for (const std::string& text : itemStorage)
    {
      items.push_back(text);
    }
  }
};

// NC-026's showcase: every widget once, laid out in cells. It is the shape every screen in Phase 5 takes — a
// function that runs each frame and asks the `Ui` for what it needs — and Phase 5 replaces it with the real desk.
void DrawWidgetShowcase(Neuron::Ui& _ui, DeskDemo& _demo)
{
  using Neuron::CELL_PIXELS;
  using Neuron::Palette;
  using Neuron::Rect;

  // The tab bar across the top, 48 px, which is UI §1's chrome.
  static constexpr std::array<std::string_view, 6> TABS{"Board", "Map", "Operations", "Contracts", "Company", "Receipts"};
  (void)_ui.Tabs("tabs", Rect::Cell(0, 0, 80, 2), TABS, _demo.activeTab);

  // The desk sits in a side panel down the right, and the 3D map fills what is left of the screen beneath the tab
  // bar. That is UI §5's arrangement and GDD §13's rule in one picture: the map is rendered in perspective and the
  // board, panels and composer around it are a flat interface on the cell grid.
  Rect side = Rect::Cell(48, 3, 31, 38);
  Rect listRect = side;
  (void)listRect.SplitBottom(CELL_PIXELS * 22);
  (void)_ui.List("reports", listRect, _demo.items, _demo.selected, _demo.scroll);

  Rect below = side;
  (void)below.SplitTop(CELL_PIXELS * 17);
  Rect right = _ui.Panel(below, "THE REPORT BEHIND IT");

  // An icon beside a label, never instead of one (UI §4).
  Rect iconRow = right.SplitTop(CELL_PIXELS);
  _ui.Icon(iconRow.SplitLeft(CELL_PIXELS), Neuron::Icon::Report, Palette::ACCENT);
  _ui.Label(iconRow, "source | courier", Palette::TEXT);

  Rect ageRow = right.SplitTop(CELL_PIXELS);
  _ui.Icon(ageRow.SplitLeft(CELL_PIXELS), Neuron::Icon::Time, Palette::TEXT_DIM);
  _ui.Label(ageRow, "observed 4 days ago", Palette::TEXT_DIM);

  Rect warnRow = right.SplitTop(CELL_PIXELS);
  _ui.Icon(warnRow.SplitLeft(CELL_PIXELS), Neuron::Icon::Warning, Palette::WARNING);
  _ui.Label(warnRow, "belief hardening", Palette::WARNING);
  (void)right.SplitTop(CELL_PIXELS);

  // The plan rules: a stepper with its unit in the label (R6), a toggle, a number field.
  char withdrawLabel[64] = {};
  sprintf_s(withdrawLabel, "withdraw at %d percent", _demo.withdrawPercent);
  (void)_ui.Stepper("withdraw", right.SplitTop(CELL_PIXELS * 2), withdrawLabel, _demo.withdrawPercent, 0, 100, 10);
  (void)right.SplitTop(CELL_PIXELS / 2);
  (void)_ui.Toggle("marked", right.SplitTop(CELL_PIXELS), "marked cargo", _demo.marked);
  (void)right.SplitTop(CELL_PIXELS / 2);

  Rect priceRow = right.SplitTop(CELL_PIXELS * 2);
  _ui.Label(priceRow.SplitLeft(CELL_PIXELS * 9), "sell above", Palette::TEXT);
  (void)_ui.NumberField("price", priceRow.SplitLeft(CELL_PIXELS * 7), _demo.priceCredits, 0, 99999, _demo.priceField);
  (void)right.SplitTop(CELL_PIXELS);

  // The two buttons, and the modal one of them opens.
  Rect buttons = right.SplitTop(CELL_PIXELS * 2);
  const Rect commitRect = buttons.SplitLeft(CELL_PIXELS * 7);
  (void)buttons.SplitLeft(CELL_PIXELS / 2);
  const Rect detailRect = buttons.SplitLeft(CELL_PIXELS * 13);
  if (_ui.Button("commit", commitRect, "Commit"))
  {
    _demo.asking = true;
  }
  if (_ui.Button("detail", detailRect, "One tap behind"))
  {
    _demo.showDetail = !_demo.showDetail;
  }

  char committed[64] = {};
  sprintf_s(committed, "operations committed: %d", _demo.committed);
  _ui.Label(right.SplitTop(CELL_PIXELS), committed, Palette::TEXT_DIM);

  // Hovering the Commit button explains it, without a click.
  static constexpr std::array<std::string_view, 2> HOVER{"Commit | Confirm", "nothing departs without Confirm"};
  _ui.Tooltip(commitRect, HOVER);

  // The drill-in: the report, its source and its age, one tap behind the decision (GDD §13).
  if (_demo.showDetail)
  {
    static constexpr std::array<std::string_view, 4> DETAIL{"THE REPORT BEHIND IT", "source courier | observed 4 days ago",
                                                            "delivered 2 days ago | this source: 7 confirmed", "3 contradicted"};
    _ui.Detail(detailRect, DETAIL);
  }

  // And the modal, drawn over everything, holding the frame behind it.
  if (_demo.asking)
  {
    static constexpr std::array<std::string_view, 2> LINES{"fuel 40 | two hulls out of position", "credits 14,250 -> 11,550"};
    const Neuron::Ui::Choice answer = _ui.Confirm("commit", "Commit the operation?", LINES);
    if (answer == Neuron::Ui::Choice::Confirmed)
    {
      ++_demo.committed;
      _demo.asking = false;
    }
    else if (answer == Neuron::Ui::Choice::Canceled)
    {
      _demo.asking = false;
    }
  }
}

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

  Neuron::DepthTarget depth;
  if (!Neuron::DepthTarget::Create(device, Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, depth))
  {
    Neuron::DebugPrint("Nomad Commander: the depth buffer could not be created.");
    return 1;
  }

  Neuron::MeshPipeline meshes;
  if (!Neuron::MeshPipeline::Create(device, meshes))
  {
    Neuron::DebugPrint("Nomad Commander: the mesh pipeline could not be created.");
    return 1;
  }

  // The map's geometry: built once, uploaded once, drawn every frame (NC-027's criterion that a frame allocates
  // nothing). NC-072 replaces these three systems and two lanes with the real universe.
  Neuron::MeshBuilder map;
  const Neuron::MeshSection laneA = map.AddLane({-6.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 4.0f}, 0.35f, Neuron::Palette::PANEL_EDGE);
  const Neuron::MeshSection laneB = map.AddLane({0.0f, 0.0f, 4.0f}, {6.5f, 0.0f, -1.0f}, 0.35f, Neuron::Palette::PANEL_EDGE);
  const Neuron::MeshSection varn =
    map.AddSphere({-6.0f, 0.0f, 0.0f}, 1.4f, 20, 28, 0xFF8C86E4u, Neuron::Palette::EMPIRE_0_VARN, 0xFF2E2A6Eu);
  const Neuron::MeshSection kessel = map.AddSphere({0.0f, 0.0f, 4.0f}, 1.8f, 20, 28, 0xFF6BD9F0u, Neuron::Palette::ACCENT, 0xFF1B4A72u);
  const Neuron::MeshSection oren =
    map.AddSphere({6.5f, 0.0f, -1.0f}, 1.2f, 20, 28, 0xFFF0CB9Au, Neuron::Palette::EMPIRE_1_OREN, 0xFF6E3A1Fu);
  if (!map.Upload(device))
  {
    Neuron::DebugPrint("Nomad Commander: the map geometry could not be uploaded.");
    return 1;
  }

  // Looking down at the plane from in front of it, which is the reading angle UI §5's dimetric projection already
  // implies. There is no controller: this is set once and never moves (NC-027's out-of-scope list).
  Neuron::Camera camera;
  camera.positionUnits = {0.0f, 11.0f, -11.0f};
  camera.targetUnits = {0.0f, 0.0f, 1.5f};
  camera.aspectRatio = static_cast<float>(Neuron::SCREEN_WIDTH_PIXELS) / static_cast<float>(Neuron::SCREEN_HEIGHT_PIXELS);

  Neuron::PrimitivePipeline primitives;
  if (!Neuron::PrimitivePipeline::Create(device, primitives))
  {
    Neuron::DebugPrint("Nomad Commander: the primitive pipeline could not be created.");
    return 1;
  }

  Neuron::PrimitiveBatch batch;
  if (!Neuron::PrimitiveBatch::Create(device, batch))
  {
    Neuron::DebugPrint("Nomad Commander: the primitive batch could not be created.");
    return 1;
  }

  Neuron::GlyphPipeline glyphs;
  if (!Neuron::GlyphPipeline::Create(device, glyphs))
  {
    Neuron::DebugPrint("Nomad Commander: the glyph pipeline could not be created.");
    return 1;
  }

  Neuron::TextRenderer text;
  if (!Neuron::TextRenderer::Create(device, glyphs, text))
  {
    Neuron::DebugPrint("Nomad Commander: the glyph atlas could not be uploaded.");
    return 1;
  }

  Neuron::InputState input;
  window.SetMessageSink(&Neuron::InputState::MessageSink, &input);
  Neuron::Ui ui(batch, text, input);
  DeskDemo demo;

  window.Show();

  bool faulted = false;
  for (;;)
  {
    // The frame's shape (NC-024): roll the edges, pump the window so the messages set them, then draw what they say.
    input.BeginFrame();
    if (!window.PumpMessages())
    {
      break;
    }
    // Escape closes, and it is the executable's business now rather than the window procedure's. ADR-010 makes this
    // load-bearing: a borderless window has no close box.
    if (input.KeyPressed(VK_ESCAPE))
    {
      window.RequestClose();
    }

    ID3D12GraphicsCommandList* const commandList = swapChain.BeginFrame(LETTERBOX_COLOR);
    if (commandList == nullptr)
    {
      faulted = true;
      break;
    }

    // Everything the game will ever draw happens here, into the scene target, at 1920x1080 and no other size
    // (ADR-009). For now that is NC-022's test pattern, which NC-025 replaces with the desk.
    scene.Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
    scene.Clear(commandList);
    const D3D12_CPU_DESCRIPTOR_HANDLE sceneView = scene.RenderTargetView();

    // The map, in perspective, with depth. It is drawn FIRST and the desk is drawn flat over it, which is GDD §13's
    // arrangement exactly: the map is rendered in 3D and the board, panels and composer around it are a 2D interface
    // on the cell grid.
    const D3D12_CPU_DESCRIPTOR_HANDLE depthView = depth.DepthStencilView();
    commandList->OMSetRenderTargets(1, &sceneView, FALSE, &depthView);
    depth.Clear(commandList);
    const D3D12_VIEWPORT mapViewport{0.0f, 0.0f, static_cast<FLOAT>(scene.WidthPixels()), static_cast<FLOAT>(scene.HeightPixels()),
                                     0.0f, 1.0f};
    const D3D12_RECT mapScissor{0, 0, static_cast<LONG>(scene.WidthPixels()), static_cast<LONG>(scene.HeightPixels())};
    commandList->RSSetViewports(1, &mapViewport);
    commandList->RSSetScissorRects(1, &mapScissor);
    meshes.Begin(commandList, camera);
    map.Bind(commandList);
    map.Draw(commandList, laneA);
    map.Draw(commandList, laneB);
    map.Draw(commandList, varn);
    map.Draw(commandList, kessel);
    map.Draw(commandList, oren);

    // And now the desk, with NO depth bound at all: the 2D passes are untouched by any of the above.
    commandList->OMSetRenderTargets(1, &sceneView, FALSE, nullptr);

    // The batch and the text renderer are opened together, because Ui draws into both and a widget is a rectangle
    // with a label on it. Both are recorded into the same command list, so the text lands over the panels.
    batch.Begin(commandList, primitives, swapChain.BufferIndex(), scene.WidthPixels(), scene.HeightPixels());
    text.Begin(commandList, glyphs, swapChain.BufferIndex(), scene.WidthPixels(), scene.HeightPixels());

    DrawTestPattern(batch);
    DrawPointer(batch, input);
    DrawTextPattern(text);
    DrawInputReadout(text, input);

    ui.BeginFrame();
    DrawWidgetShowcase(ui, demo);
    ui.EndFrame();

    // Primitives first, then every glyph over them: two draws rather than interleaving, which is what keeps text on
    // top of the panels it sits on without any depth or blending (NC-023 discards, it does not blend).
    batch.End();
    text.End();

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
