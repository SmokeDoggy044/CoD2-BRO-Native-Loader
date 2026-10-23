#pragma once

// Game module: script-thread wrappers (the generated Scr_StartThread_* functors in .text)
// and small entity helpers (gentity_t by byte offsets).

#include <cstdint>

#include "compiler.h"

namespace game {

// The Scr_StartThread_* wrappers (cdecl a1..a6 plus one pointer per functor field): a
// functor {vtable, a5, a6, *a7, ...} from ScrIface_Alloc (0 when it fails), then
// Scr_CreateThread(a2, a3, a4, a6, functor, a1). The entries hand the C versions the
// argument block. Not registered: the indirect CALL to ScrIface_Alloc sits at byte 2
// (unhookable for the compare harness); they are ready for Replaced.
// 0x0011a910: no fields (vtable 0x007f5ba0).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThreadFunctor1(const uint32_t* args);
extern "C" void Scr_StartThreadFunctor1_entry(void);
// 0x0017ae10: f, f, b.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_f_f_b(const uint32_t* args);
extern "C" void Scr_StartThread_ent_f_f_b_entry(void);
// 0x0017ada0: ent, hash, ent, hash.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_ent_hash_ent_hash(const uint32_t* args);
extern "C" void Scr_StartThread_ent_ent_hash_ent_hash_entry(void);
// 0x0017b5c0: vec, b.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_vec_b_3(const uint32_t* args);
extern "C" void Scr_StartThread_ent_vec_b_3_entry(void);
// 0x0017b270: hash, f, i, f, f.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_hash_f_i_f_f_2(const uint32_t* args);
extern "C" void Scr_StartThread_ent_hash_f_i_f_f_2_entry(void);
// 0x0017b2f0: ent, f, i, i, i, f.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_ent_f_i_i_i_f(const uint32_t* args);
extern "C" void Scr_StartThread_ent_ent_f_i_i_i_f_entry(void);
// 0x0017b0c0: hash, i, i, f, f, f, f, hash.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_hash_i_i_f_f_f_f_hash_2(const uint32_t* args);
extern "C" void Scr_StartThread_ent_hash_i_i_f_f_f_f_hash_2_entry(void);
// 0x0011a960: a functor {0x007f5ba0, a6, a7}; the argument slots become (a1, a2, a3, a7,
// a4, a5, functor) and it tail-jumps to Scr_BeginThreadEndOn.
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_BeginThreadEndOnFunctor1(uint32_t* args);
extern "C" void Scr_BeginThreadEndOnFunctor1_entry(void);
// 0x00248af0 (cdecl fmt, ...): formats into a local buffer (_vsprintf) and drops it.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL G_Error(const uint32_t* args);
extern "C" void G_Error_entry(void);
// 0x0024acb0: the error/break call 0x002938f0 (5, "endgame").
void XB_CDECL G_EndGame();
// 0x00225b30 (entity in ECX): kills it through its die callback (+0x344, table 0x008da88c)
// with health+1 damage from the world, health -1.
void XB_REGS3 XB_CDECL G_KillEntityOutright(uint32_t, uint32_t, uint8_t* ent);
// 0x00250cf0 (self in EAX; cdecl other): records the trigger pair for this frame
// (0x0098606c, 256 at most); when full, notifies "trigger" at once.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_Trigger(uint8_t* self, uint8_t* other);
extern "C" void G_Trigger_entry(void);
// 0x0024fdd0 / 0x0024fde0 / 0x0024fdf0 / 0x0024fe00: spawn functions that free the entity.
void XB_CDECL SP_corona(uint8_t* ent);
void XB_CDECL SP_misc_model(uint8_t* ent);
void XB_CDECL SP_light(uint8_t* ent);
void XB_CDECL SP_info_null(uint8_t* ent);
// 0x002fb310 (entity in ESI): the six script-string fields' name hashes (+4 each). EAX = the last.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_UpdateEntityStringHashes(uint8_t* ent);
extern "C" void G_UpdateEntityStringHashes_entry(void);
// 0x002b8450 (entity in ECX): the DObj's LOD from the scaled squared distance to the
// player (5 when the DObj has flag 1).
void XB_REGS3 XB_CDECL G_UpdateEntityLOD(uint32_t, uint32_t, uint8_t* ent);
// 0x002b7780 (entity in EDX): clears references to it in the clients, actors, level
// client, vehicles and the 0x00985e30 table. Returns 0x20.
uint32_t XB_REGS2 XB_CDECL G_ClearEntityReferences(uint32_t, uint8_t* ent);
// 0x002f54c0 (name in ESI): the level script module entry for a level name, 0 when none.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_FindLevelScriptModule(const char* name);
extern "C" void G_FindLevelScriptModule_entry(void);
// 0x002d70a0 (angles in ESI, movedir in EDI): the move direction of the angles (up/down
// specials, else from pitch and yaw); the angles are cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_SetMovedir(float* angles, float* movedir);
extern "C" void G_SetMovedir_entry(void);
// 0x002de120 (stdcall ent, out): the entity's 4x4 (+0x160: forward, -left, up, origin;
// each row's fourth word its first) copied to out. Returns out.
float* XB_STDCALL G_GetEntityMatrix(uint8_t* ent, float* out);

}  // namespace game
