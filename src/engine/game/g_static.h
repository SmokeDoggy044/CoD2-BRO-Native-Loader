#pragma once

// Game-module static initializers (boot) and atexit destructors (the title never exits): C, untested by design.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

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
