// NomadCommander/Main.cpp
#include "pch.h"
#include "Window.h"

// The executable's entry point. It opens the 1920x1080 window the game presents (AGENTS.md R12) and pumps it until it
// closes. There is nothing to see in it yet: NC-021 brings the device and the swap chain, and the composition root of
// NC-070 replaces this loop with the hosted session and the client. The parameters stay unnamed until something reads
// them; /W4 /WX would otherwise report them unreferenced.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
  const Neuron::Window::Desc desc{Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, L"Nomad Commander"};
  Neuron::Window window;
  if (!Neuron::Window::Create(desc, window))
  {
    return 1;
  }
  window.Show();

  while (window.PumpMessages())
  {
    // Nothing yet. NC-021 draws a frame here.
  }
  return 0;
}
