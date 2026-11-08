#pragma once

// Tiny compiled level-script functions: empty bodies (the script function did nothing, or only
// released a string argument), single-global getters/setters and lookups.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// Shared bodies: the BroScr_EmptyFunc_* rows are all `ret` (registered on BroScr_Stub_Ret);
// EmptyFuncStrArg_* and the unnamed copies release their by-value string argument (tail jump to
// ScriptString_Release) in slot 1 or slot 2. EAX = ScriptString_Release's result.
void XB_CDECL BroScr_Stub_Ret();
uint32_t XB_CDECL BroScr_Stub_ReleaseArg1(ScriptString string);
uint32_t XB_CDECL BroScr_Stub_ReleaseArg2(uint32_t a1, ScriptString string);

// 0x001836e0 (xor eax,eax; ret: 3 bytes, not hookable).
uint32_t XB_CDECL Bro_anim_exit_run();

// 0x001838f0 (unnamed; called from level code only): sets the script global 0x00922e8c.
uint32_t XB_CDECL BroScr_SetScriptGlobal_922e8c(uint32_t value);

// 0x00184220 (unnamed; level code only): *out = the player entity (0x009236dc).
uint32_t* XB_CDECL BroScr_GetPlayer(uint32_t* out);

// 0x00183710: *out = the string global 0x00922ea8 (a word copy, no reference taken); the string
// argument is released.
uint32_t* XB_CDECL BroScr_GetDefaultString(uint32_t* out, uint32_t a2, ScriptString string);

// 0x001819b0: GScr_Delete(entity) on the second argument.
void XB_CDECL BroScr_DeleteEntArg2(uint32_t a1, uint32_t entity);

// 0x001838c0: Scr_EndOn(entity, "death" hash 0x06730ae8).
void XB_CDECL BroScr_EndOnDeath(uint32_t entity);

// 0x00183740 (unnamed; level code only): defaults on the anim state object (0x00922efc):
// +0xec = 8, +0x5c = +0xc4 = +0x12c = 1. EAX = 1.
uint32_t XB_CDECL BroScr_AnimStateSetDefaults();

// 0x00183310 / 0x00183370 (unnamed; level code only): the 0x98-byte state block for an index,
// 1-7 (second argument) / 8-14 (first argument); null otherwise.
uint32_t XB_CDECL BroScr_GetStateBlock1To7(uint32_t a1, uint32_t index);
uint32_t XB_CDECL BroScr_GetStateBlock8To14(uint32_t index);

// 0x00184c20: *out = 0.25.
uint32_t* XB_CDECL BroScr_ReturnQuarter(uint32_t* out);

}  // namespace game
