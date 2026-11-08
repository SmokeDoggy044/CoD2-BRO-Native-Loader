#include "bro_scr_defaults.h"

#include "bro_scr_common.h"
#include "title_data.h"
#include "bro_scr_fx.h"
#include "bro_scr_mortar.h"
#include "bro_scr_vehicle.h"
#include "g_scr_runtime.h"
#include "levels_templates.h"
#include "levels_twins.h"
#include "script_string.h"

namespace game {

namespace {

uint32_t undefined_float() { return title_global<uint32_t>(0x00922eac); }  // g_scrUndefinedFloat (copied as a word)
uint32_t undefined_lane() { return title_global<uint32_t>(0x00922eb8); }   // g_scrUndefinedVecLane
uint32_t undefined_handle() { return title_global<uint32_t>(0x00922eb4); }  // g_scrUndefinedHandle
uint32_t undefined_int() { return title_global<uint32_t>(0x00922ec0); }     // g_scrUndefinedInt

ScriptString copy_string(const ScriptString* from) {
    ScriptString copy;
    own_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(&ScriptString_Copy)(&copy, from);  // ScriptString_Copy
    return copy;
}

DynArray copy_array(const DynArray* from) {
    DynArray copy{nullptr, 0, 0};
    own_function<void XB_THISCALL(DynArray*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&copy, from);  // DynArray_Assign4
    return copy;
}

// The by-value array argument's block (Scr_Free when set).
void free_array(const DynArray* array) {
    if (array->data) own_function<void XB_CDECL(void*)>(&Scr_Free)(array->data);  // Scr_Free
}

uint32_t hash_string(const ScriptString* string) {
    uint32_t out;
    return *own_function<uint32_t* XB_CDECL(uint32_t*, const ScriptString*)>(&Scr_HashScriptString)(&out, string);  // Scr_HashScriptString
}

// Default-argument wrapper of the SetWheelSmokeFx calls: the copy of the level's map at +0x57c is
// made and destroyed, then the call runs when its entry for 0x438c482f is 0. `ent` (when null: the
// word at 0x00922e84, read after the string is made) and the string (a copy of the empty string,
// or "day").
void wheel_smoke(const uint32_t* ent, bool day) {
    alignas(4) uint8_t map[0x28];
    own_function<void XB_THISCALL(uint8_t*, uint32_t)>(&HashMap_CopyCtor_PROLOGUE_6)(map, title_global<uint32_t>(0x00922ee0) + 0x57c);  // HashMap_CopyCtor
    own_function<void XB_THISCALL(uint8_t*)>(&HashMap_Dtor_PROLOGUE)(map);  // HashMap_Dtor
    uint32_t key = 0x438c482f;
    using Index = uint32_t* XB_THISCALL(uint32_t, uint32_t*);
    if (*title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x57c, &key) != 0) return;  // HashMap_Index
    ScriptString fx;
    if (day) {
        scr_string_from(&fx, 0x007ff364);  // "day"
    } else {
        fx = copy_string(&title_global<ScriptString>(0x00922e90));
    }
    const uint32_t target = ent ? *ent : title_global<uint32_t>(0x00922e84);
    own_function<void XB_CDECL(uint32_t, ScriptString)>(&BroScr_SetWheelSmokeFx)(target, fx);  // BroScr_SetWheelSmokeFx
}

void spawn_loop_sound(const uint32_t (&w)[12]) {
    using F = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            uint32_t, uint32_t, uint32_t);
    own_function<F>(&BroScr_SpawnLoopSoundOrigin)(w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11]);  // BroScr_SpawnLoopSoundOrigin
}

void drone_vehicle_riders(uint32_t vehicle, uint32_t a1, ScriptString* name, const uint32_t (&w)[7]) {
    const ScriptString copy = copy_string(name);
    using F = void XB_CDECL(uint32_t, uint32_t, ScriptString, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            uint32_t);
    own_function<F>(&BroScr_DroneVehicleRiders)(vehicle, a1, copy, w[0], w[1], w[2], w[3], w[4], w[5], w[6]);  // BroScr_DroneVehicleRiders
    scr_release(name);
}

void mortar_loops(uint32_t ent, DynArray* ents, uint32_t a4, uint32_t a5, uint32_t w6, uint32_t w7) {
    const DynArray copy = copy_array(ents);
    using F = void XB_CDECL(uint32_t, DynArray, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    own_function<F>(&BroScr_StartMortarLoopsNearPlayer)(ent, copy, a4, a5, w6, w7, 0);  // BroScr_StartMortarLoopsNearPlayer
    free_array(ents);
}

void mortar_group(uint32_t ent, DynArray* ents, uint32_t f1, uint32_t f2, uint32_t handle, uint32_t w, uint32_t i) {
    const DynArray copy = copy_array(ents);
    using F = void XB_CDECL(uint32_t, DynArray, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, DynArray);
    own_function<F>(&BroScr_StartMortarGroupNearEnt)(ent, copy, f1, f2, handle, w, i, DynArray{nullptr, 0, 0});  // BroScr_StartMortarGroupNearEnt
    free_array(ents);
}

}  // namespace

uint32_t* XB_CDECL BroScr_SpawnFlakvierlingCall(uint32_t* out, ScriptString name, uint32_t a2, uint32_t a3, uint32_t a4,
                                                uint32_t a5, uint32_t a6) {
    const uint32_t undefined = title_global<uint32_t>(0x00922ebc);
    const ScriptString copy = copy_string(&name);
    using F = void XB_CDECL(uint32_t*, ScriptString, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    own_function<F>(&BroScr_SpawnFlakvierling)(out, copy, a2, a3, a4, a5, a6, undefined, undefined);  // BroScr_SpawnFlakvierling
    scr_release(&name);
    return out;
}

void XB_CDECL BroScr_VehicleSpawnRiderByNameCall(ScriptString name, uint32_t a1) {
    const uint32_t undefined = title_global<uint32_t>(0x00922ebc);
    const ScriptString copy = copy_string(&name);
    own_function<void XB_CDECL(ScriptString, uint32_t, uint32_t)>(&BroScr_VehicleSpawnRiderByName)(copy, a1, undefined);  // BroScr_VehicleSpawnRiderByName
    scr_release(&name);
}

void XB_CDECL BroScr_SetWheelSmokeFxCall() { wheel_smoke(nullptr, false); }
void XB_CDECL BroScr_SetWheelSmokeFxCall_2() { wheel_smoke(nullptr, false); }
void XB_CDECL BroScr_SetWheelSmokeFxCall_3(uint32_t ent) { wheel_smoke(&ent, true); }
void XB_CDECL BroScr_SetWheelSmokeFxCall_4(uint32_t ent) { wheel_smoke(&ent, true); }
void XB_CDECL BroScr_SetWheelSmokeFxCall_5(uint32_t ent) { wheel_smoke(&ent, true); }

void XB_CDECL BroScr_SpawnLoopSoundOriginCall(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    const uint32_t f = undefined_float();
    const uint32_t i = undefined_int();
    spawn_loop_sound({a0, a1, a2, a3, a4, i, i, f, f, f, f, undefined_handle()});
}

void XB_CDECL BroScr_SpawnLoopSoundOriginCall_2(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                                uint32_t a5, uint32_t a6) {
    const uint32_t f = undefined_float();
    spawn_loop_sound({a0, a1, a2, a3, a4, a5, a6, f, f, f, f, undefined_handle()});
}

void XB_CDECL BroScr_SpawnLoopSoundOriginCall_3(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                                uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8) {
    const uint32_t f = undefined_float();
    spawn_loop_sound({a0, a1, a2, a3, a4, a5, a6, a7, a8, f, f, undefined_handle()});
}

void XB_CDECL BroScr_SpawnLoopSoundOriginCall_4(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                                uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                                                uint32_t a10) {
    spawn_loop_sound({a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, undefined_handle()});
}

void XB_CDECL BroScr_DroneVehicleRidersCall(uint32_t vehicle, uint32_t a1, ScriptString name) {
    const uint32_t v = undefined_lane();
    drone_vehicle_riders(vehicle, a1, &name, {undefined_int(), v, v, v, v, v, v});
}

void XB_CDECL BroScr_DroneVehicleRidersCall_2(uint32_t vehicle, uint32_t a1, ScriptString name, uint32_t a3) {
    const uint32_t v = undefined_lane();
    drone_vehicle_riders(vehicle, a1, &name, {a3, v, v, v, v, v, v});
}

void XB_CDECL BroScr_DroneVehicleRidersCall_3(uint32_t vehicle, uint32_t a1, ScriptString name, uint32_t a3,
                                              uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8) {
    drone_vehicle_riders(vehicle, a1, &name, {undefined_int(), a3, a4, a5, a6, a7, a8});
}

void XB_CDECL BroScr_MortarThinkWithDefaultsCall(uint32_t ent, uint32_t a1) {
    const uint32_t f = undefined_float();
    using F = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    own_function<F>(&BroScr_MortarThinkWithDefaults)(ent, f, f, f, f, f, f, a1);  // BroScr_MortarThinkWithDefaults
}

void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_1(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5) {
    const uint32_t h = undefined_handle();
    mortar_loops(ent, &ents, a4, a5, h, h);
}

void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_2(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5,
                                                      uint32_t a6) {
    mortar_loops(ent, &ents, a4, a5, a6, undefined_handle());
}

void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_3(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5,
                                                      ScriptString a6) {
    const uint32_t h = undefined_handle();
    mortar_loops(ent, &ents, a4, a5, hash_string(&a6), h);
    scr_release(&a6);
}

void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_4(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5,
                                                      uint32_t a6, uint32_t a7) {
    mortar_loops(ent, &ents, a4, a5, a6, a7);
}

void XB_CDECL BroScr_StartMortarLoopsNearPlayerCall_5(uint32_t ent, DynArray ents, uint32_t a4, uint32_t a5,
                                                      ScriptString a6, uint32_t a7) {
    mortar_loops(ent, &ents, a4, a5, hash_string(&a6), a7);
    scr_release(&a6);
}

void XB_CDECL BroScr_StartMortarGroupNearEntCall_1(uint32_t ent, DynArray ents, uint32_t w) {
    const uint32_t f = undefined_float();
    mortar_group(ent, &ents, f, f, undefined_handle(), w, undefined_int());
}

void XB_CDECL BroScr_StartMortarGroupNearEntCall_2(uint32_t ent, DynArray ents, uint32_t f1, uint32_t f2, uint32_t w) {
    mortar_group(ent, &ents, f1, f2, undefined_handle(), w, undefined_int());
}

void XB_CDECL BroScr_StartMortarGroupNearEntCall_3(uint32_t ent, DynArray ents, uint32_t w, uint32_t i) {
    const uint32_t f = undefined_float();
    mortar_group(ent, &ents, f, f, undefined_handle(), w, i);
}

void XB_CDECL BroScr_StartMortarGroupNearEntCall_4(uint32_t ent, DynArray ents, uint32_t f1, uint32_t f2, uint32_t w,
                                                   uint32_t i) {
    mortar_group(ent, &ents, f1, f2, undefined_handle(), w, i);
}

void XB_CDECL BroScr_StartMortarGroupNearEntCall_5(uint32_t ent, DynArray ents, uint32_t handle, uint32_t w) {
    const uint32_t f = undefined_float();
    mortar_group(ent, &ents, f, f, handle, w, undefined_int());
}

void XB_CDECL BroScr_StartMortarGroupNearEntCall_6(uint32_t ent, DynArray ents, uint32_t f1, uint32_t f2,
                                                   uint32_t handle, uint32_t w) {
    mortar_group(ent, &ents, f1, f2, handle, w, undefined_int());
}

void XB_CDECL BroScr_StartMortarGroupNearEntCall_7(uint32_t ent, DynArray ents, uint32_t handle, uint32_t w,
                                                   uint32_t i) {
    const uint32_t f = undefined_float();
    mortar_group(ent, &ents, f, f, handle, w, i);
}

void XB_CDECL BroScr_StartMortarGroupNearEntCall_8(uint32_t ent, DynArray ents, uint32_t f1, uint32_t f2,
                                                   uint32_t handle, uint32_t w, uint32_t i) {
    mortar_group(ent, &ents, f1, f2, handle, w, i);
}

}  // namespace game
