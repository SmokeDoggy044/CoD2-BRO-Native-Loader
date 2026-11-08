#pragma once

// Direct3D 9: DXVK's (dxvk-native, linked) on Linux; on Windows DXVK's or the
// system's, loaded at run time (backend_d3d9.cpp), so only the types are used.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <d3d9.h>
