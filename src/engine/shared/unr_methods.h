#pragma once

// Unreached functions (own engine host 0.3, 2026-11-12): functions no recording reaches that data or level code
// points at (tools/host_unreached.py) - shared stubs, accessors and small helpers of classes whose files come later.

#include <cstdint>

#include "compiler.h"

namespace game {

// Shared bodies: vtable slots that only return a constant.
uint32_t XB_STDCALL EmptyVFunc_ReturnZero4(uint32_t);  // 0x0002bb10 (also the nglMeshManager / AnimManager2ce8 Load)
uint32_t XB_STDCALL EmptyVFunc_ReturnOne12(uint32_t, uint32_t, uint32_t);  // 0x00067390 (... Release)

// 0x00011a40 (UIElement vtable slot): self->+0x20 object's first word's +0xa0 word.
uint32_t XB_THISCALL UIElement_GetOwnerField0A0(uint8_t* self);
// 0x00011c50 / 0x00011dd0: tail jumps to the wrapped item's (+0x10) slot 8 / slot 50.
extern "C" void UIMenuItem_InnerSlot8();
extern "C" void UIMenuItem_InnerSlot50();

// 0x00042460: INT3 when the debugger flag (0x00901aec) is set.
void XB_CDECL Sys_DebugBreak();
// 0x0006bb40: the "profileshaders" console command (0x00248d90 jumps here): nothing.
void XB_CDECL nglProfileShaders();
// 0x001fbe90: entity controller 3 (table 0x008da8b0): nothing.
void XB_CDECL Ent_ControllerNop(uint8_t* ent, uint32_t* parts);
// 0x002aa000: the path node field table's (0x008d3990) setter of "type": nothing (read-only).
void XB_CDECL PathNodeField_SetReadOnly(uint8_t* node, int32_t field);

// 0x0007bcb0 (NAL_GenericAnim vtable slot 27): a new AnimState (Mem_AlignedAlloc 0x10) on self, range 0.
uint8_t* XB_THISCALL NAL_GenericAnim_NewState(uint8_t* self);
// 0x0001cd40 (slot 35): a new AnimTrack (0x30 bytes) of self on skeleton (null: self's +0x30).
uint8_t* XB_THISCALL NAL_GenericAnim_NewTrack(uint8_t* self, uint8_t* skeleton);

// 0x0011d040 (MONS1 calls it): six words: 3, 13, 0.5, 5.0, 0.1, 0.5 (written over the script undefined values).
uint32_t* XB_THISCALL LevelParams_InitDefaults(uint32_t* self);
// 0x0011d2d0 (BUCHHOLZ calls it): two zero words, the undefined int, then the level's two map constructors at +0x0c
// and +0x34 (level code 0x0078c6a0 / 0x0078c720).
uint32_t* XB_THISCALL ScrTwoMaps_Ctor(uint32_t* self);

}  // namespace game
