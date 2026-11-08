#include "bro_scr_mortar.h"

#include <cstring>

#include "bro_scr_common.h"
#include "bro_scr_util.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "title_data.h"
#include "bro_scr_exploder.h"
#include "bro_scr_squad.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "levels_twins.h"
#include "scr_field.h"
#include "script_string.h"

namespace game {

namespace {

// The level object's word at +4 (null object: read through address 0, as the original).
uint32_t level_word() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(field);
}

void free_array(const DynArray* array) {
    if (array->data) own_function<void XB_CDECL(void*)>(&Scr_Free)(array->data);  // Scr_Free
}

void* alloc(uint32_t bytes) { return scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(bytes); }  // ScrIface_Alloc

void create_thread(uint32_t ent, uint32_t* functor) {
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}

}  // namespace

void XB_CDECL BroScr_StartMortarGroupNearEnt(uint32_t ent, DynArray ents, float f1, float f2, uint32_t handle,
                                             uint32_t w, uint32_t i, DynArray extra) {
    const float undefined = title_global<float>(0x00922eac);  // g_scrUndefinedFloat
    if (f1 == undefined) f1 = title_global<float>(0x0083f11c);  // 400
    if (f2 == undefined) f2 = title_global<float>(0x007f7bf8);  // 1200
    if (i == title_global<uint32_t>(0x00922ec0)) i = 1;       // g_scrUndefinedInt
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(ents.size); ++n) {
        if (n >= ents.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&ents), n + 1, n + 4);
        own_function<void XB_CDECL(uint32_t)>(&BroScr_MortarEntInit)(ents.data[n]);  // BroScr_MortarEntInit
    }
    const uint32_t level = level_word();
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const DynArray*, const float*, const float*, const uint32_t*,
                                          const uint32_t*, const uint32_t*, const DynArray*);
    own_function<StartThread>(&Scr_StartThread_ent_ent_arrent_f_f_hash_hash_i_arr_entry)(0, 0, 0, 0, 0x00171530, level, &ent, &ents, &f1, &f2, &handle, &w, &i,
                                            &extra);  // Scr_StartThread_ent_ent_arrent_f_f_hash_hash_i_arr(BroScr_MortarGroupNearEntLoop)
    free_array(&ents);
    free_array(&extra);
}

void XB_CDECL BroScr_StartMortarLoopsNearPlayer(uint32_t self, DynArray ents, uint32_t a4, uint32_t a5, uint32_t a6,
                                                uint32_t a7, uint32_t unused) {
    (void)self;
    (void)unused;
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(ents.size); ++n) {
        if (n >= ents.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&ents), n + 1, n + 4);
        const uint32_t ent = ents.data[n];
        uint32_t* init = static_cast<uint32_t*>(alloc(0xc));
        if (init) {
            init[0] = 0x007f5ba0;
            init[1] = 0x00149f60;  // BroScr_MortarEntInit
            init[2] = ent;
        }
        create_thread(ent, init);
        if (n >= ents.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&ents), n + 1, n + 4);
        const uint32_t ent2 = ents.data[n];
        uint32_t* loop = static_cast<uint32_t*>(alloc(0x1c));
        if (loop) {
            loop[0] = 0x00824488;
            loop[1] = 0x00170fb0;  // BroScr_MortarLoopNearPlayer
            loop[2] = ent2;
            loop[3] = a4;
            loop[4] = a5;
            loop[5] = a6;
            loop[6] = a7;
        }
        create_thread(ent2, loop);
    }
    free_array(&ents);
}

namespace {

// A damage value: `value` unless it is the undefined float; then the level map's (+offset) value
// for `type` (FUN_0x004b2ac0, operator[]), or `fallback` when that is undefined too (the map is
// read twice then, as in the original).
uint32_t mortar_value(uint32_t value, uint32_t offset, const uint32_t* type, uint32_t fallback) {
    const float undefined = title_global<float>(0x00922eac);  // g_scrUndefinedFloat
    float f;
    std::memcpy(&f, &value, 4);
    if (!(f == undefined)) return value;
    using Index = uint32_t* XB_THISCALL(uint32_t, const uint32_t*);
    const auto index = title_function<Index>(0x004b2ac0);
    const uint32_t stored = *index(title_global<uint32_t>(0x00922ee0) + offset, type);
    std::memcpy(&f, &stored, 4);
    if (f == title_global<float>(0x00922eac)) return title_global<uint32_t>(fallback);
    return *index(title_global<uint32_t>(0x00922ee0) + offset, type);
}

}  // namespace

void XB_CDECL BroScr_MortarThinkWithDefaults(uint32_t ent, uint32_t radius, uint32_t max_damage, uint32_t min_damage,
                                             uint32_t a4, uint32_t a5, uint32_t a6, uint32_t type) {
    radius = mortar_value(radius, 0x65c, &type, 0x0083ed04);              // 300
    max_damage = mortar_value(max_damage, 0x690, &type, 0x0083f11c);      // 400
    min_damage = mortar_value(min_damage, 0x7a0, &type, 0x0083ebc4);      // 25
    using GetOrigin = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
    uint32_t buffer[3];
    const uint32_t* o = scr_slot<GetOrigin>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    uint32_t origin[3] = {o[0], o[1], o[2]};
    scr_slot<void XB_CDECL(const uint32_t*, uint32_t, uint32_t, uint32_t)>(0x00922428)(origin, radius, max_damage,
                                                                                        min_damage);  // GScr_RadiusDamage
    uint32_t buffer2[3];
    o = scr_slot<GetOrigin>(0x00922b20)(buffer2, ent);
    const ScrVec3 pos{{o[0], o[1], o[2]}};
    using Think = void XB_CDECL(uint32_t, ScrVec3, uint32_t, uint32_t, uint32_t, uint32_t);
    own_function<Think>(&BroScr_MortarThink)(ent, pos, a4, a5, a6, type);  // BroScr_MortarThink
}

namespace {

float title_float_bits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

uint32_t level_entity() {
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    return title_global<uint32_t>(level ? level + 4 : 0);
}

uint32_t* key_value(uint32_t ent, uint32_t hash) {
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
    uint32_t* value = ScriptKeyTable_Find(table, hash);
    if (!value) value = ScriptKeyTable_Set(table, hash, 0);
    return value;
}

bool debug_mortars() {
    ScriptString value;
    const bool on = ScriptString_EqualsInt(ScrCall_GScr_GetCvar(&value, 0x00818b94), 1) != 0;  // "debug_mortars"
    scr_release(&value);
    return on;
}

// Scr_StartThread_ent_vec_str_vec(0, 0, 0, 0, BroScr_WaitForNotifyOrTimeoutCall_2, level entity,
// &origin, &"Boom!", &red): the debug print.
void print_boom(ScrVec3* origin) {
    const uint32_t level = level_entity();
    ScriptString text;
    scr_string_from(&text, 0x00818b8c);  // "Boom!"
    const uint32_t red[3] = {title_global<uint32_t>(0x0083eb20), 0, 0};
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScrVec3*, ScriptString*,
                                          const uint32_t*);
    title_function<StartThread>(0x006d1370)(0, 0, 0, 0, 0x00130ee0, level, origin, &text, red);
    scr_release(&text);
}

}  // namespace

void XB_CDECL BroScr_MortarThink(uint32_t ent, ScrVec3 origin, uint32_t scale, uint32_t duration, uint32_t radius,
                                 uint32_t type) {
    // Earthquake parameters: the level's override (+0x29c) for the scale, else the arguments, else
    // the level maps by type (+0x3e0 / +0x554 / +0x51c) or 0.45 / 2 / 850.
    const uint32_t level_scale = title_global<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x29c);
    float f;
    std::memcpy(&f, &level_scale, 4);
    if (!(f == title_global<float>(0x00922eac)))
        scale = level_scale;
    else
        scale = mortar_value(scale, 0x3e0, &type, 0x0083f034);
    duration = mortar_value(duration, 0x554, &type, 0x0083eba8);
    radius = mortar_value(radius, 0x51c, &type, 0x0081ddd0);
    ScriptString fx;
    own_function<void XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_FromRep)(&fx, 0);  // ScriptString_FromRep(null)
    uint32_t target = 0;
    {
        ScriptString temp;
        const bool has_target = ScrCall_ScrEnt_GetTarget(&ent, &temp)->rep != nullptr;
        scr_release(&temp);
        if (has_target) {
            using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
            target = scr_slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&ent, &temp), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
            scr_release(&temp);
        }
    }
    using Assign = void XB_THISCALL(ScriptString*, const ScriptString*);
    bool defined;
    own_function<bool* XB_CDECL(bool*, uint32_t)>(ScrField_he5d3774f_IsDefined)(&defined, ent);  // ScrField_he5d3774f_IsDefined
    if (defined) {
        own_function<Assign>(&ScriptString_Assign_entry)(&fx, own_function<ScriptString* XB_CDECL(uint32_t)>(ScrField_he5d3774f)(ent));  // ScrField_he5d3774f
    } else if (type != title_global<uint32_t>(0x00922eb4)) {  // g_scrUndefinedHandle
        using Index = ScriptString* XB_THISCALL(uint32_t, const uint32_t*);
        own_function<Assign>(&ScriptString_Assign_entry)(&fx, title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x488, &type));
    } else {
        ScriptString* fallback = &title_global<ScriptString>(title_global<uint32_t>(0x00922ee0) + 0x6e8);
        if (fallback->rep) own_function<Assign>(&ScriptString_Assign_entry)(&fx, fallback);
    }
    if (fx.rep == nullptr) {
        // Scr_StartThread_ent_vec_str_vec_f_3(.., BroScr_WaitForNotifyOrTimeoutCall_4, level entity,
        // &origin, &"No FX!", &red, &3.0)
        const uint32_t level = level_entity();
        ScriptString text;
        scr_string_from(&text, 0x00818b84);  // "No FX!"
        const uint32_t red[3] = {title_global<uint32_t>(0x0083eb20), 0, 0};
        const uint32_t time = title_global<uint32_t>(0x0083ec9c);  // 3.0
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScrVec3*,
                                              ScriptString*, const uint32_t*, const uint32_t*);
        own_function<StartThread>(&Scr_StartThread_ent_vec_str_vec_f_3_entry)(0, 0, 0, 0, 0x001310b0, level, &origin, &text, red, &time);
        scr_release(&text);
    } else if (scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(target)) {  // Scr_EntityHandleIsValid
        using GetOrigin = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
        uint32_t buffer[3];
        const uint32_t* o = scr_slot<GetOrigin>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        const float from[3] = {title_float_bits(o[0]), title_float_bits(o[1]), title_float_bits(o[2])};
        uint32_t buffer2[3];
        o = scr_slot<GetOrigin>(0x00922b20)(buffer2, target);
        const float to[3] = {title_float_bits(o[0]), title_float_bits(o[1]), title_float_bits(o[2])};
        const float delta[3] = {platform_addss(to[0], -from[0]), platform_addss(to[1], -from[1]), platform_addss(to[2], -from[2])};
        const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
        uint32_t angles[3] = {lane, lane, lane};
        scr_slot<void XB_CDECL(uint32_t*, const float*)>(0x00922284)(angles, delta);  // Scr_VectorToAngles
        const uint32_t angles_copy[3] = {angles[0], angles[1], angles[2]};
        uint32_t forward[3] = {lane, lane, lane};
        scr_slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, angles_copy);  // Scr_AnglesToForward
        if (debug_mortars()) print_boom(&origin);
        const uint32_t direction[3] = {forward[0], forward[1], forward[2]};
        scr_slot<void XB_CDECL(uint32_t, ScriptString*, const uint32_t*, uint32_t, uint32_t)>(0x009221e0)(
            ent, &fx, direction, 0, 0);  // GScr_PlaySoundAtPosition (the effect along the target direction)
    } else {
        if (debug_mortars()) print_boom(&origin);
        scr_slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &fx, 0xbd1fe198, 0,
                                                                                                0);  // GScr_PlaySound
        scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(level_entity(), 0x7652bc7c);  // Scr_QueueNotify
    }
    // Deletes the ents of array field 0x05c4ceb7 when field 0xb2f8556f is set.
    own_function<bool* XB_CDECL(bool*, uint32_t)>(ScrField_hb2f8556f_IsDefined)(&defined, ent);  // ScrField_hb2f8556f_IsDefined
    if (defined && *own_function<uint8_t* XB_CDECL(uint32_t)>(&ScrField_hb2f8556f_entry)(ent)) {
        own_function<bool* XB_CDECL(bool*, uint32_t)>(ScrField_h05c4ceb7_IsDefined)(&defined, ent);  // ScrField_h05c4ceb7_IsDefined
        if (defined) {
            using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);
            const auto array = [ent]() { return own_function<GetArray>(&ScriptKeyTable_GetArray)(BroScr_ScriptObjectOrEmpty(ent), 0x05c4ceb7); };
            const auto at = [&array](uint32_t i) {
                uint32_t* a = array();
                if (i >= a[2]) DynArray_Resize(reinterpret_cast<uint8_t*>(a), i + 1, i + 4);
                return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(a[0]))[i];
            };
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array()[2]); ++i) {
                if (scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(at(i))) scr_slot<void XB_CDECL(uint32_t)>(0x00922540)(at(i));  // GScr_Delete
            }
        }
    }
    if (scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(ent)) {
        if (ScriptKeyTable* table = reinterpret_cast<ScriptKeyTable*>(static_cast<uintptr_t>(
                title_function<uint32_t XB_CDECL(uint32_t)>(0x0011db20)(ent)))) {  // ScrCall_GScr_GetEntScriptObject
            const uint32_t* value = ScriptKeyTable_Find(table, 0x28667a5b);
            if (scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(value ? *value : 0))
                scr_slot<void XB_CDECL(uint32_t)>(0x00922564)(*key_value(ent, 0x28667a5b));  // GScr_ClearEntFlag400
        }
    }
    // The script_exploder check writes its byte into the entity's argument slot, over the stack
    // address the original keeps there (entry ESP - 0x7c, then - 0x80 for the exploder call).
    const uint32_t vehicle = ent;
    const uint32_t entry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ent)) - 4;
    ent = entry - 0x7c;
    own_function<bool* XB_CDECL(bool*, uint32_t)>(ScrField_script_exploder_IsDefined)(reinterpret_cast<bool*>(&ent), vehicle);  // ScrField_script_exploder_IsDefined
    if (*reinterpret_cast<bool*>(&ent)) {
        const uint32_t level = level_entity();
        ent = entry - 0x7c;
        const uint32_t exploder = *own_function<uint32_t* XB_CDECL(uint32_t)>(ScrField_script_exploder)(vehicle);  // ScrField_script_exploder
        ent = entry - 0x80;
        own_function<void XB_CDECL(uint32_t, uint32_t)>(&BroScr_Exploder)(level, exploder);  // BroScr_Exploder
    }
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0xc));  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = 0x0016c9d0;  // BroScr_StukaStartNearbyDrones
        functor[2] = vehicle;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, vehicle, functor, 0);
    scr_slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(scale, duration, &origin, radius);  // GScr_Earthquake
    scr_release(&fx);
}

}  // namespace game
