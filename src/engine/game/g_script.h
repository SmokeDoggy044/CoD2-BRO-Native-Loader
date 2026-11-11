#pragma once

// Game module: script-thread wrappers (the generated Scr_StartThread_* functors in .text)
// and small entity helpers (gentity_t by byte offsets).

#include <cstdint>

#include "compiler.h"

namespace game {

// The Scr_StartThread_* wrappers (cdecl a1..a6 plus one pointer per functor field): a
// functor {vtable, a5, a6, *a7, ...} from ScrIface_Alloc (0 when it fails), then
// Scr_CreateThread(a2, a3, a4, a6, functor, a1). The entries hand the C versions the
// argument block. Replaced without a comparison (2026-11-11): the indirect CALL to ScrIface_Alloc
// sits at byte 2 (the compare harness cannot hook them); replays check them.
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
// Wrappers whose functor class has a constructor (thiscall (a5, &a6, a7, ...)); the hookable ones
// (SEH prologue) are registered, the others are unhookable like the ones above.
// 0x0017ae80: constructor 0x0017a3c0 (5 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_ent_str_vec(const uint32_t* args);
extern "C" void Scr_StartThread_ent_ent_str_vec_entry(void);
// 0x0017af10: constructor 0x0017a490 (6 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_ent_i_str_b_2(const uint32_t* args);
extern "C" void Scr_StartThread_ent_ent_i_str_b_2_entry(void);
// 0x0017afa0: constructor 0x00459b70 (4 words), Scr_BeginThread + Scr_WaitFrames(0) in a script
// thread (switches the thread away: Replaced without a comparison).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_ent_str(const uint32_t* args);
extern "C" void Scr_CallThread_ent_ent_str_entry(void);
// 0x0017b050 (unhookable): constructor 0x0017a550 (8 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_hash_vec_hash_f_hash_f(const uint32_t* args);
extern "C" void Scr_StartThread_ent_hash_vec_hash_f_hash_f_entry(void);
// 0x0017b150: constructor 0x0017a650 (ent, str, vec: 4 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThreadFunctorN(const uint32_t* args);
extern "C" void Scr_StartThreadFunctorN_entry(void);
// 0x0017b1e0: constructor 0x0017a740 (6 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_vec_str_vec_f_3(const uint32_t* args);
extern "C" void Scr_StartThread_ent_vec_str_vec_f_3_entry(void);
// "call" = the Scr_CallThread_* kind: Scr_BeginThread, then Scr_WaitFrames(0) in a script thread
// (switches the thread away: Replaced without a comparison, checked by replays).
// 0x0017b370: constructor 0x0017a870 (6 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_ent_RespawnerAttributes_i_str(const uint32_t* args);
extern "C" void Scr_StartThread_ent_ent_RespawnerAttributes_i_str_entry(void);
// 0x0017b410: constructor 0x0017a9a0 (3 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_RespawnerAttributes(const uint32_t* args);
extern "C" void Scr_StartThread_ent_RespawnerAttributes_entry(void);
// 0x0017b4a0: constructor 0x0017aa40 (5 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_str_i_str_2(const uint32_t* args);
extern "C" void Scr_StartThread_ent_str_i_str_2_entry(void);
// 0x0017b530: constructor 0x004591e0 (a6, &a7, a8), then Scr_BeginThreadEndOn(a1, a2, a3, a7, a4, a5, functor).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_BeginThread_ent_str(const uint32_t* args);
extern "C" void Scr_BeginThread_ent_str_entry(void);
// 0x0017cc10: constructor 0x0017c770 (10 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_ent_arrent_f_f_hash_hash_i_arr(const uint32_t* args);
extern "C" void Scr_StartThread_ent_ent_arrent_f_f_hash_hash_i_arr_entry(void);
// 0x0017ccc0: constructor 0x0017c830 (ent, ent, ent, arrent: 5 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThreadFunctor_20_3(const uint32_t* args);
extern "C" void Scr_StartThreadFunctor_20_3_entry(void);
// 0x001e4ae0: constructor 0x001e3df0 (6 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_ent_str_ent_ent_4(const uint32_t* args);
extern "C" void Scr_StartThread_ent_ent_str_ent_ent_4_entry(void);
// 0x001e4b70: constructor 0x001e3ec0 (5 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_StartThread_ent_i_pathnode_str_3(const uint32_t* args);
extern "C" void Scr_StartThread_ent_i_pathnode_str_3_entry(void);
// 0x001e4c00: call; fields 4, 4 (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_vt00825be4_2(const uint32_t* args);
extern "C" void Scr_CallThread_vt00825be4_2_entry(void);
// 0x001e4c80: call; fields 4, 4 (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_f_ent_3(const uint32_t* args);
extern "C" void Scr_CallThread_ent_f_ent_3_entry(void);
// 0x001e4d00: call; constructor 0x001e3fb0 (7 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_hash_i_str_i_ent(const uint32_t* args);
extern "C" void Scr_CallThread_ent_hash_i_str_i_ent_entry(void);
// 0x001e4dc0: call; fields 4, 4 (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_hash_f_4(const uint32_t* args);
extern "C" void Scr_CallThread_ent_hash_f_4_entry(void);
// 0x001e4e40: call; constructor 0x00564fa0 (8 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_vec_f_str_i_ent_i(const uint32_t* args);
extern "C" void Scr_CallThread_ent_vec_f_str_i_ent_i_entry(void);
// 0x001e4f00: call; vec field (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_vec(const uint32_t* args);
extern "C" void Scr_CallThread_ent_vec_entry(void);
// 0x001e4f80: call; fields 4, 4 (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_f_f_5(const uint32_t* args);
extern "C" void Scr_CallThread_ent_f_f_5_entry(void);
// 0x001e5000: call; constructor 0x001e4080 (6 words; unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_hash_AnimDirections_AnimAngles_f_2(const uint32_t* args);
extern "C" void Scr_CallThread_ent_hash_AnimDirections_AnimAngles_f_2_entry(void);
// 0x001e5080: call; fields 4, 4, 4 (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_hash_i_f_2(const uint32_t* args);
extern "C" void Scr_CallThread_ent_hash_i_f_2_entry(void);
// 0x001e5100: call; constructor 0x001e4150 (9 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_anim_str_f_hash_str_f_b(const uint32_t* args);
extern "C" void Scr_CallThread_ent_anim_str_f_hash_str_f_b_entry(void);
// 0x001e51c0: call; constructor 0x001e4280 (5 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_str_hash_hash_3(const uint32_t* args);
extern "C" void Scr_CallThread_ent_str_hash_hash_3_entry(void);
// 0x001e5270: call; fields 4, 4, 4 (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_f_hash_hash_4(const uint32_t* args);
extern "C" void Scr_CallThread_ent_f_hash_hash_4_entry(void);
// 0x001e52f0: call; constructor 0x001e4330 (6 words; unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_vec_vec_vec_i(const uint32_t* args);
extern "C" void Scr_CallThread_ent_vec_vec_vec_i_entry(void);
// 0x001e5370: call; field 4 (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_f(const uint32_t* args);
extern "C" void Scr_CallThread_ent_f_entry(void);
// 0x001e53e0: call; six 4-byte fields (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_anim_anim_anim_anim_anim_anim(const uint32_t* args);
extern "C" void Scr_CallThread_ent_anim_anim_anim_anim_anim_anim_entry(void);
// 0x001e5480: call; constructor 0x001e4430 (6 words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_vt00825c50(const uint32_t* args);
extern "C" void Scr_CallThread_vt00825c50_entry(void);
// 0x001e5530: call; five 4-byte fields (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_f_hash_hash_hash_i_2(const uint32_t* args);
extern "C" void Scr_CallThread_ent_f_hash_hash_hash_i_2_entry(void);
// 0x001e55c0: call; fields 4, 4, 4 (unhookable).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_CallThread_ent_hash_hash_f_5(const uint32_t* args);
extern "C" void Scr_CallThread_ent_hash_hash_f_5_entry(void);
// 0x0011f5d0: empties the dynamic array (a3) and appends GScr_GetEntArray(a1, a2, buffer, 0x400, a4).
void XB_CDECL Scr_GetEntArray(uint32_t a1, uint32_t a2, uint8_t* array, uint32_t a4);
// 0x0011fac0 (thread start pointer, immediates at 0x00177e45/0x00177f75): Scr_EndOn(object, name),
// waits for "death" on the thread (switches away: Replaced without a comparison), then Scr_QueueNotify(object, name).
void XB_CDECL Scr_NotifyAfterDeath(uint32_t object, uint32_t thread, uint32_t name);
// 0x00135fb0: releases a2's a4 script strings and frees the block (when a2 is set), then releases
// the by-value string in the a5 slot. a1 and a3 are not read.
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_ReleaseStringArrayAndString(uint32_t* args);
extern "C" void Scr_ReleaseStringArrayAndString_entry(void);
// 0x00151270 (level script code): the entity's health by difficulty, then the vehicle health
// thread 0x0014d0a0 on it.
void XB_CDECL BroScr_PanzerIVSetDifficultyHealth(uint32_t ent);
// 0x00168220 (dead: no call, pointer or immediate): the "panzerIV" anim tree, then thread 0x001679b0.
uint32_t XB_CDECL BroScr_PanzerIVUseAnimTree(uint32_t ent);
// 0x0011a960: a functor {0x007f5ba0, a6, a7}; the argument slots become (a1, a2, a3, a7,
// a4, a5, functor) and it tail-jumps to Scr_BeginThreadEndOn.
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_BeginThreadEndOnFunctor1(uint32_t* args);
extern "C" void Scr_BeginThreadEndOnFunctor1_entry(void);
// 0x0011b4d0: Scr_BeginThread(a2, a3, a4, a6, functor {0x007f5ba0, a5, a6}, a1), then
// Scr_WaitFrames(0) when called from a script thread; returns the thread. Replaced without a
// comparison (same prologue the harness cannot hook).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Scr_BeginThreadFunctor1(const uint32_t* args);
extern "C" void Scr_BeginThreadFunctor1_entry(void);
// 0x0011c170 (entity in ECX): the entity's script object (0x0011db20 of its first word), or the
// empty key table 0x00922e94 when it has none.
uint32_t XB_THISCALL Scr_GetEntObjectOrEmpty(const uint32_t* entity);
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

// 0x0024ac50 (entity in ESI; one word passed on): Q3's G_RunThink - unless +0x2a4 has 0x4000000, a due think time
// (+0x328, 0 < it <= level_time) is cleared and entityThinkFuncs[+0x32c](ent, word) runs (G_Error for none); the callback runs at the original's ESP.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_RunThink(uint32_t entry_esp, uint8_t* ent, uint32_t arg, uint32_t ebx,
                                                    uint32_t edi);
extern "C" void G_RunThink_entry(void);

}  // namespace game
