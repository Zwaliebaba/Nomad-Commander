// NeuronCore/NeuronCore.h
#pragma once

// AGENTS.md §4: this header owns the Windows macro family. Nothing else defines any of these, and no .vcxproj
// passes one with /D: two owners of one macro is C4005, and /WX makes that fatal. If you need <windows.h>,
// include this header; never add the macros yourself.
//
// NOGDI means GDI is genuinely gone (GetStockObject, TextOut and their kin are not declared): the swap chain
// owns every pixel (AGENTS.md R12).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP

#include <windows.h>
