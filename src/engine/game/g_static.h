#pragma once

// Game-module static initializers (boot) and atexit destructors (the title never exits): C, untested by design.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// Constructors the static initializers call (C since 2026-11-11, 0.3 host), each returning the object:
// 0x00023ad0 (thiscall): 15 empty script strings (+0..+0x38), the first word of 15 0x104-byte records from +0x3c zero,
// +0xf98 zero.
uint8_t* XB_THISCALL StaticObj_ScriptStrings15_Ctor(uint8_t* self);
// 0x00026fc0 (object in ESI): 0x40 word pairs at +0xb4, +0xac0..+0xb3c zero, empty script strings +0xb50 and +0xcb8,
// +0xcb4 zero, the first two words of 0x100 16-byte entries from +0xcfc zero.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL StaticObj_985370_Ctor(uint8_t* self);
extern "C" void StaticObj_985370_Ctor_entry(void);
// 0x000270e0 (object in ESI): 136 empty script strings (+0..+0x21c).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL StaticObj_984ac0_Ctor(uint8_t* self);
extern "C" void StaticObj_984ac0_Ctor_entry(void);
// 0x00027cf0 (object in EBX): empty script strings at +0x38, +0x3c, +0x48 and the same 0x40 further.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL StaticObj_9883b0_Ctor(uint8_t* self);
extern "C" void StaticObj_9883b0_Ctor_entry(void);
// 0x001e4700 (thiscall): word 0 zero, words +4..+0x54 = g_scrUndefinedInt (0x00922ec0).
uint8_t* XB_THISCALL ScrUndefinedInts21_Ctor(uint8_t* self);
// 0x00014c80 (thiscall; an atexit destructor): releases the script string at +0xc, base vtable 0x007f48c4.
void XB_THISCALL ScrStringHolder_Dtor(uint8_t* self);

// 0x003731e0.
void XB_CDECL StaticInit_HandleTable_8dda2c();
// 0x00373380.
void XB_CDECL StaticInit_ItemList();
// 0x00376690.
void XB_CDECL StaticInit_DownVec4_95ff20();
// 0x003766d0.
void XB_CDECL StaticInit_Vec4_960060();
// 0x00376710.
void XB_CDECL StaticInit_Vec4_95fe60();
// 0x00376e00.
void XB_CDECL StaticInit_G_EntityList();
// 0x00376e40.
void XB_CDECL StaticInit_DownVec4_984ab0();
// 0x00376e80.
void XB_CDECL StaticInit_Vec4_97a8a0();
// 0x00376ec0.
void XB_CDECL StaticInit_Vec4_984d00();
// 0x00377290.
void XB_CDECL StaticInit_Vec4_988390();
// 0x003772d0.
void XB_CDECL StaticInit_Vec4_985360();
// 0x00377660.
void XB_CDECL StaticInit_SpawnFuncList();
// 0x00377870.
void XB_CDECL StaticInit_Vec4_989230();
// 0x00377a30.
void XB_CDECL StaticInit_VehiclePhysPool();
// 0x00377b80.
void XB_CDECL StaticInit_DownVec4_98aa80();
// 0x00377bc0.
void XB_CDECL StaticInit_g_actorMins();
// 0x00377c00.
void XB_CDECL StaticInit_g_actorMaxs();
// 0x00379b30.
void XB_CDECL AtExit_ReleaseScriptStrings_986028();
// 0x00379b50.
void XB_CDECL AtExit_FreeAligned_984cec();
// 0x00379b70.
void XB_CDECL AtExit_g_entityRemoveQueue();
// 0x00379b90.
void XB_CDECL AtExit_FreeAligned_97a640();
// 0x00379bb0.
void XB_CDECL AtExit_984ac0();
// 0x00027770 (block in ESI): the destructor of the 136 constant script strings GScr_LoadConsts fills
// (0x00984ac0..0x00984cdc): ScriptString_Release on each, the last first.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_ConstStrings_Dtor(uint8_t* strings);
extern "C" void GScr_ConstStrings_Dtor_entry(void);
// 0x00379bc0.
void XB_CDECL AtExit_9883b0();
// 0x00379bd0.
void XB_CDECL AtExit_FreeAligned_984ce0();
// 0x00379bf0.
void XB_CDECL AtExit_FreeAligned_97a8b0();
// 0x00379cd0.
void XB_CDECL AtExit_RagdollPool();
// 0x00379ce0.
void XB_CDECL AtExit_RagdollPhysPool();
// 0x00379d50.
void XB_CDECL AtExit_ReleaseScriptStrings_98aa74();
// 0x00379d70.
void XB_CDECL StaticInit_List_8feba0();

}  // namespace game
