#include "bro_scr_battle_threads.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_mortar.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "title_data.h"
#include "bro_scr_drone_threads.h"
#include "bro_scr_level_threads_2.h"
#include "bro_scr_mortar_loops.h"
#include "bro_scr_stuka_bomb.h"
#include "levels_twins.h"

namespace game {

using namespace bro_ai;

namespace {

// DynArray copy-assign (0x0045c270, thiscall) into a zeroed by-value argument.
DynArray copy_array(const DynArray* source) {
    DynArray copy = {0, 0, 0};
    own_function<void XB_THISCALL(DynArray*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&copy, source);
    return copy;
}
ScriptString copy_string(const ScriptString* source) {
    ScriptString copy;
    ScriptString_Copy(&copy, source);
    return copy;
}
uint32_t undefined_bool_word() { return title_global<uint8_t>(0x00922ed0); }  // g_scrUndefinedBool, zero-extended
uint32_t word_ea4() { return title_global<uint32_t>(0x00922ea4); }
uint32_t word_ebc() { return title_global<uint32_t>(0x00922ebc); }

// BroScr_AnimReach (0x0016ce40) / BroScr_AnimReachEnt (0x0016cf80) with a one-element array.
void anim_reach(uint32_t function, uint32_t ent, uint32_t guy, uint32_t a3, const ScriptString* anime, uint32_t a5,
                uint32_t a6) {
    DynArray guys = {0, 0, 0};
    DynArray_Resize(reinterpret_cast<uint8_t*>(&guys), 1, 4);
    uint32_t* const data = guys.data;
    data[0] = guy;
    const ScriptString copy = copy_string(anime);
    title_function<void XB_CDECL(uint32_t, DynArray, uint32_t, ScriptString, uint32_t, uint32_t)>(function)(
        ent, copy_array(&guys), a3, copy, a5, a6);
    Scr_Free(data);
}

using PlaneDropBomb = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void plane_drop_bomb(uint32_t plane, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                     uint32_t a8) {
    own_function<PlaneDropBomb>(&BroScr_PlaneDropBomb)(plane, a2, a3, a4, a5, a6, a7, a8, 1);  // BroScr_PlaneDropBomb(.., true)
}

// BroScr_DronePathSpawnLoop (0x001706b0) with a copy of `name`; releases `name`.
void drone_path_spawn_loop(uint32_t a1, uint32_t a2, ScriptString* name, uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7,
                           uint32_t p8, uint32_t p9) {
    using Loop = void XB_CDECL(uint32_t, uint32_t, ScriptString, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    own_function<Loop>(&BroScr_DronePathSpawnLoop)(a1, a2, copy_string(name), p4, p5, p6, p7, p8, p9);
    scr_release(name);
}

// BroScr_MortarGroupShuffledLoop (0x00170a90; never returns).
void mortar_group_shuffled_loop(uint32_t ent, const DynArray* group, uint32_t a, uint32_t b) {
    own_function<void XB_CDECL(uint32_t, DynArray, uint32_t, uint32_t)>(&BroScr_MortarGroupShuffledLoop)(ent, copy_array(group), a, b);
}

// BroScr_DronePathsStartAll (0x00171f80) / ..Staggered (0x00172150) with copies; frees `paths`, releases `name`.
void drone_paths_start_all(uint32_t function, uint32_t a1, DynArray* paths, ScriptString* name, uint32_t p6, uint32_t p7,
                           uint32_t p8, uint32_t p9, uint32_t p10, uint32_t p11) {
    using Start = void XB_CDECL(uint32_t, DynArray, ScriptString, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const ScriptString copy = copy_string(name);
    title_function<Start>(function)(a1, copy_array(paths), copy, p6, p7, p8, p9, p10, p11);
    if (paths->data) Scr_Free(paths->data);
    scr_release(name);
}

}  // namespace

void XB_CDECL BroScr_AnimReachCall(uint32_t ent, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5, uint32_t a6) {
    anim_reach(0x0016ce40, ent, guy, a3, &anime, a5, a6);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimReachEntCall(uint32_t ent, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5, uint32_t a6) {
    anim_reach(0x0016cf80, ent, guy, a3, &anime, a5, a6);
    scr_release(&anime);
}

void XB_CDECL BroScr_DroneMainCall(uint32_t drone, uint32_t flag) {
    own_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(&BroScr_DroneMain)(drone, flag, 0);  // BroScr_DroneMain
}

void XB_CDECL BroScr_DroneMainDelayed(uint32_t drone, uint32_t min, uint32_t max, uint32_t flag) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(drone, 0xf9ed2681);  // Scr_EndOn
    Scr_WaitExact(slot<float XB_CDECL(uint32_t, uint32_t)>(0x00922238)(min, max));  // Scr_RandomFloatRange
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
    title_function<StartThread>(0x0045b810)(0, 0, 0, 0, 0x0016d390, drone, &flag);  // Scr_StartThread_ent_b(BroScr_DroneMainCall)
}

void XB_CDECL BroScr_ShellIncomingThenImpact(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                             uint32_t a7, uint32_t type) {
    own_function<void XB_CDECL(uint32_t, uint32_t)>(&BroScr_PlayShellIncoming)(ent, type);  // BroScr_PlayShellIncoming
    if (entity_handle_is_valid(ent)) BroScr_MortarThinkWithDefaults(ent, a2, a3, a4, a5, a6, a7, type);
}

void XB_CDECL BroScr_ShellIncomingThenImpact_2(uint32_t ent, uint32_t type) {
    const uint32_t f = undefined_float();
    own_function<void XB_CDECL(uint32_t, uint32_t)>(&BroScr_PlayShellIncoming)(ent, type);  // BroScr_PlayShellIncoming
    if (entity_handle_is_valid(ent)) BroScr_MortarThinkWithDefaults(ent, f, f, f, f, f, f, type);
}

void XB_CDECL BroScr_ShellIncomingAtPos(uint32_t ent, uint32_t type, ScrVec3 pos) {
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, pos);  // ScrEnt_SetOrigin
    BroScr_DropToGround(ent);
    BroScr_ShellIncomingThenImpact_2(ent, type);
}

void XB_CDECL BroScr_PlaneDropBombCall(uint32_t plane, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                       uint32_t a7, uint32_t a8) {
    plane_drop_bomb(plane, a2, a3, a4, a5, a6, a7, a8);
}

void XB_CDECL BroScr_PlaneDropBombCall_2(uint32_t plane, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                         uint32_t a7) {
    plane_drop_bomb(plane, a2, a3, a4, a5, a6, a7, undefined_float());
}

void XB_CDECL BroScr_PlaneBombLoop(uint32_t plane) {
    Scr_EndOnString(plane, pointer<const char>(0x00831f60));  // "death"
    for (;;) {
        const uint32_t count = undefined_int();
        const uint32_t f = undefined_float();
        const float count_float = static_cast<float>(static_cast<int32_t>(count));
        plane_drop_bomb(plane, count, bits(count_float), f, 0, 0, 0, f);
    }
}

void XB_CDECL BroScr_DronePathSpawnLoopCall_1(uint32_t a1, uint32_t a2, ScriptString name) {
    drone_path_spawn_loop(a1, a2, &name, word_ebc(), word_ea4(), word_ea4(), word_ea4(), word_ea4(), undefined_bool_word());
}

void XB_CDECL BroScr_DronePathSpawnLoopCall_2(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p9) {
    drone_path_spawn_loop(a1, a2, &name, word_ebc(), word_ea4(), word_ea4(), word_ea4(), word_ea4(), p9);
}

void XB_CDECL BroScr_DronePathSpawnLoopCall_3(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p4) {
    drone_path_spawn_loop(a1, a2, &name, p4, word_ea4(), word_ea4(), word_ea4(), word_ea4(), undefined_bool_word());
}

void XB_CDECL BroScr_DronePathSpawnLoopCall_4(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p4, uint32_t p9) {
    drone_path_spawn_loop(a1, a2, &name, p4, word_ea4(), word_ea4(), word_ea4(), word_ea4(), p9);
}

void XB_CDECL BroScr_DronePathSpawnLoopCall_5(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p4, uint32_t p5,
                                              uint32_t p6) {
    drone_path_spawn_loop(a1, a2, &name, p4, p5, p6, word_ea4(), word_ea4(), undefined_bool_word());
}

void XB_CDECL BroScr_DronePathSpawnLoopCall_6(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p4, uint32_t p5,
                                              uint32_t p6, uint32_t p9) {
    drone_path_spawn_loop(a1, a2, &name, p4, p5, p6, word_ea4(), word_ea4(), p9);
}

void XB_CDECL BroScr_MortarGroupShuffledLoopCall_1(uint32_t ent, DynArray group) {
    const uint32_t h = undefined_handle();
    mortar_group_shuffled_loop(ent, &group, h, h);
}

void XB_CDECL BroScr_MortarGroupShuffledLoopCall_2(uint32_t ent, DynArray group, uint32_t a) {
    mortar_group_shuffled_loop(ent, &group, a, undefined_handle());
}

void XB_CDECL BroScr_MortarGroupShuffledLoopCall_3(uint32_t ent, DynArray group, ScriptString name) {
    const uint32_t h = undefined_handle();
    uint32_t hash;
    Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name));
    mortar_group_shuffled_loop(ent, &group, hash, h);
}

void XB_CDECL BroScr_MortarGroupShuffledLoopCall_4(uint32_t ent, DynArray group, ScriptString name, uint32_t b) {
    uint32_t hash;  // the original writes it into b's argument slot after pushing b
    Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name));
    mortar_group_shuffled_loop(ent, &group, hash, b);
}

void XB_CDECL BroScr_DronePathsStartAllCall_1(uint32_t a1, DynArray paths, ScriptString name) {
    drone_paths_start_all(0x00171f80, a1, &paths, &name, word_ebc(), word_ea4(), word_ea4(), word_ea4(), word_ea4(),
                          undefined_bool_word());
}

void XB_CDECL BroScr_DronePathsStartAllCall_2(uint32_t a1, DynArray paths, ScriptString name, uint32_t p11) {
    drone_paths_start_all(0x00171f80, a1, &paths, &name, word_ebc(), word_ea4(), word_ea4(), word_ea4(), word_ea4(), p11);
}

void XB_CDECL BroScr_DronePathsStartAllCall_3(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6) {
    drone_paths_start_all(0x00171f80, a1, &paths, &name, p6, word_ea4(), word_ea4(), word_ea4(), word_ea4(),
                          undefined_bool_word());
}

void XB_CDECL BroScr_DronePathsStartAllCall_4(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6, uint32_t p11) {
    drone_paths_start_all(0x00171f80, a1, &paths, &name, p6, word_ea4(), word_ea4(), word_ea4(), word_ea4(), p11);
}

void XB_CDECL BroScr_DronePathsStartAllCall_5(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6, uint32_t p7,
                                              uint32_t p8) {
    drone_paths_start_all(0x00171f80, a1, &paths, &name, p6, p7, p8, word_ea4(), word_ea4(), undefined_bool_word());
}

void XB_CDECL BroScr_DronePathsStartAllCall_6(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6, uint32_t p7,
                                              uint32_t p8, uint32_t p11) {
    drone_paths_start_all(0x00171f80, a1, &paths, &name, p6, p7, p8, word_ea4(), word_ea4(), p11);
}

void XB_CDECL BroScr_DronePathsStartAllStaggeredCall(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6,
                                                     uint32_t p7, uint32_t p8, uint32_t p11) {
    drone_paths_start_all(0x00172150, a1, &paths, &name, p6, p7, p8, word_ea4(), word_ea4(), p11);
}

}  // namespace game
