#pragma once

// Compiled level-script default-argument wrappers: each calls one script function with the
// arguments the caller gave and the script runtime's "undefined" values for the rest (float
// 0x00922eac, vector lane 0x00922eb8, handle 0x00922eb4, int 0x00922ec0, 0x00922ebc). Strings and
// arrays passed on are copies (the callee releases them); the wrapper then releases its own
// by-value string/array arguments.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "script_string.h"

namespace game {

// 0x00159d50: *out = BroScr_SpawnFlakvierling(name, a2..a6, 0x00922ebc x2); returns out.
uint32_t* XB_CDECL BroScr_SpawnFlakvierlingCall(uint32_t* out, ScriptString name, uint32_t a2, uint32_t a3, uint32_t a4,
                                                uint32_t a5, uint32_t a6);
// 0x0015ad00: BroScr_VehicleSpawnRiderByName(name, a1, 0x00922ebc).
void XB_CDECL BroScr_VehicleSpawnRiderByNameCall(ScriptString name, uint32_t a1);
// BroScr_SetWheelSmokeFx(ent, fx) unless the level's map at +0x57c holds a value for key
// 0x438c482f (a copy of that map is made and destroyed first, as the script's discarded read):
// 0x0015e450 / 0x0015fef0 with the word at 0x00922e84 and the empty string; 0x00160030 /
// 0x001600a0 / 0x00163980 with `ent` and "day".
void XB_CDECL BroScr_SetWheelSmokeFxCall();
void XB_CDECL BroScr_SetWheelSmokeFxCall_2();
void XB_CDECL BroScr_SetWheelSmokeFxCall_3(uint32_t ent);
void XB_CDECL BroScr_SetWheelSmokeFxCall_4(uint32_t ent);
void XB_CDECL BroScr_SetWheelSmokeFxCall_5(uint32_t ent);
// BroScr_SpawnLoopSoundOrigin (12 words): 0x00166450 (a0..a4, int x2, float x4, handle),
// 0x001664d0 (a0..a6, float x4, handle), 0x00166550 (a0..a8, float x2, handle), 0x001665c0
// (a0..a10, handle).
void XB_CDECL BroScr_SpawnLoopSoundOriginCall(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
void XB_CDECL BroScr_SpawnLoopSoundOriginCall_2(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                                uint32_t a5, uint32_t a6);
void XB_CDECL BroScr_SpawnLoopSoundOriginCall_3(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                                uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8);
void XB_CDECL BroScr_SpawnLoopSoundOriginCall_4(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                                uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                                                uint32_t a10);
// BroScr_DroneVehicleRiders(vehicle, a1, copy of name, 7 more words): 0x0016f6b0 (int, two
// undefined vectors), 0x0016f790 (a3, two undefined vectors), 0x0016f870 (int, a3..a8).
void XB_CDECL BroScr_DroneVehicleRidersCall(uint32_t vehicle, uint32_t a1, ScriptString name);
void XB_CDECL BroScr_DroneVehicleRidersCall_2(uint32_t vehicle, uint32_t a1, ScriptString name, uint32_t a3);
void XB_CDECL BroScr_DroneVehicleRidersCall_3(uint32_t vehicle, uint32_t a1, ScriptString name, uint32_t a3,
                                              uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8);
// 0x00170600: BroScr_MortarThinkWithDefaults(ent, float x6, a1).
void XB_CDECL BroScr_MortarThinkWithDefaultsCall(uint32_t ent, uint32_t a1);
// BroScr_StartMortarLoopsNearPlayer(ent, copy of ents, a4, a5, w6, w7, 0): 0x001738b0 (handle x2),
// 0x00173950 (a6, handle), 0x00173a00 (hash of the string a6, handle), 0x00173ad0 (a6, a7),
// 0x00173b80 (hash of a6, a7).
void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_1(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5);
void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_2(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5,
                                                      uint32_t a6);
void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_3(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5,
                                                      ScriptString a6);
void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_4(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5,
                                                      uint32_t a6, uint32_t a7);
void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_5(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5,
                                                      ScriptString a6, uint32_t a7);
// BroScr_StartMortarGroupNearEnt(ent, copy of ents, f1, f2, handle, w, i, empty array); the
// defaults: f1/f2 float, handle, i int. 0x00174250 (w), 0x00174310 (f1, f2, w), 0x001743c0 (w, i),
// 0x00174480 (f1, f2, w, i), 0x00174530 (handle, w), 0x001745f0 (f1, f2, handle, w), 0x001746a0
// (handle, w, i), 0x00174760 (all).
void XB_CDECL BroScr_StartMortarGroupNearEntCall_1(uint32_t ent, DynArray ents, uint32_t w);
void XB_CDECL BroScr_StartMortarGroupNearEntCall_2(uint32_t ent, DynArray ents, uint32_t f1, uint32_t f2, uint32_t w);
void XB_CDECL BroScr_StartMortarGroupNearEntCall_3(uint32_t ent, DynArray ents, uint32_t w, uint32_t i);
void XB_CDECL BroScr_StartMortarGroupNearEntCall_4(uint32_t ent, DynArray ents, uint32_t f1, uint32_t f2, uint32_t w,
                                                   uint32_t i);
void XB_CDECL BroScr_StartMortarGroupNearEntCall_5(uint32_t ent, DynArray ents, uint32_t handle, uint32_t w);
void XB_CDECL BroScr_StartMortarGroupNearEntCall_6(uint32_t ent, DynArray ents, uint32_t f1, uint32_t f2,
                                                   uint32_t handle, uint32_t w);
void XB_CDECL BroScr_StartMortarGroupNearEntCall_7(uint32_t ent, DynArray ents, uint32_t handle, uint32_t w,
                                                   uint32_t i);
void XB_CDECL BroScr_StartMortarGroupNearEntCall_8(uint32_t ent, DynArray ents, uint32_t f1, uint32_t f2,
                                                   uint32_t handle, uint32_t w, uint32_t i);

}  // namespace game
