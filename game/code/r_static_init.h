#pragma once

// The renderer's and particle library's C++ static initializers and atexit destructors
// (.text 0x00376480..0x00379e70): the CRT's initializer table runs the StaticInit_* ones
// before the title's main, and the AtExit_* ones are what the initializers hand to _atexit
// (never run on the Xbox: the title does not exit normally). No direct callers.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x003769e0: the down vector (0, 0, -1, 0) at 0x009717e0 (16-aligned).
void XB_CDECL StaticInit_DownVec4_9717e0();
// 0x003785e0: ngl_multisampleScale (0x00903c70) = (1, 1, 0, 0).
void XB_CDECL StaticInit_ngl_multisampleScale();
// 0x00378d40: the particle library's empty bounds: FLT_MAX in all four lanes at 0x00909ee0
// and -FLT_MAX (the sign mask 0x007c52d0 xored in) at 0x00909ef0.
void XB_CDECL StaticInit_FxParticleBounds();
// 0x00379a50, 0x00379ab0: two arrays of 15 script strings 0xf9c bytes apart (0x00970844 then
// 0x0096f8a8 / 0x0096e90c then 0x0096d970), released through the CRT's vector destructor
// iterator (0x00011280) with ScriptString_Release.
void XB_CDECL AtExit_FxScriptStrings_970844();
void XB_CDECL AtExit_FxScriptStrings_96e90c();
// 0x00379a80, 0x00379ae0: the same for two arrays 0x60 bytes apart (0x009718a0 then
// 0x00971840 / 0x00971970 then 0x00971910).
void XB_CDECL AtExit_FxScriptStrings_9718a0();
void XB_CDECL AtExit_FxScriptStrings_971970();
// 0x00379b10: ScriptString_Release of the script string at 0x00971908 (tail jump).
void XB_CDECL AtExit_ReleaseScriptString_971908();
// 0x00379c60, 0x00379c80, 0x00379ca0: Mem_AlignedFree of the effect list's storage
// (0x00988ce8), the effect pool's (0x00988804) and the block at 0x00988714, each pointer
// cleared after; nothing when it is null.
void XB_CDECL AtExit_FxEffectList();
void XB_CDECL AtExit_FxEffectPool();
void XB_CDECL AtExit_FreeAligned_988714();
// 0x00379da0, 0x00379db0, 0x00379dc0: HashStrMap_Clear (0x00094010, thiscall) of the shader
// caches 0x00902ac0 / 0x00902ad4 and the NAL class map 0x00903fe0 (tail jumps).
void XB_CDECL AtExit_ShaderCache_902ac0();
void XB_CDECL AtExit_ShaderCache_902ad4();
void XB_CDECL AtExit_NalClassMap_903fe0();
// 0x00379de0: the vtable 0x0083760c stored in the static object 0x008bb124 (the NAL
// library's default configuration, NAL_Init's fallback).
void XB_CDECL StaticInit_NalConfig_8bb124();

}  // namespace game
