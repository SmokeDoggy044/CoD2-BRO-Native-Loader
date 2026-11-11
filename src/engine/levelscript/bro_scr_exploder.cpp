#include "bro_scr_exploder.h"

#include "bro_scr_ai_common.h"
#include "g_script.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "levels_twins.h"
#include "bro_scr_actor_threads.h"
#include "bro_scr_ai_anim_threads.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_arrays.h"
#include "bro_scr_entity_threads.h"
#include "bro_scr_notify_threads.h"
#include "g_scr_builtins.h"

namespace game {

using namespace bro_ai;

namespace {

uint32_t* element(DynArray* array, uint32_t index) {
    if (index >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), index + 1, index + 4);
    return pointer<uint32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(array->data)) + index * 4);
}

using CreateThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using Alloc = uint32_t* XB_CDECL(uint32_t);

// A thread on `ent` running `function` (functor 0x007f5ba0 {vtable, function, ent}).
void ent_thread(uint32_t ent, uint32_t function) {
    uint32_t* const functor = slot<Alloc>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = ent;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}

// A thread on the level running `function` for the entity (functor 0x007f4a28 {vtable, function,
// level, entity}).
void level_thread(DynArray* ents, uint32_t index, uint32_t function) {
    uint8_t* const level_object = title_global<uint8_t*>(0x00922ef8);  // g_levelScriptObject
    const uint32_t* const level_self = level_object ? reinterpret_cast<const uint32_t*>(level_object + 4) : nullptr;
    const uint32_t* const ent = element(ents, index);
    const uint32_t self = *level_self;
    uint32_t* const functor = slot<Alloc>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = function;
        functor[2] = self;
        functor[3] = *ent;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, self, functor, 0);  // Scr_CreateThread
}

template <typename IsDefined>
bool defined(IsDefined is_defined, uint32_t ent) {
    bool value;
    return *is_defined(&value, ent);
}

void start_thread_functor1(uint32_t function, uint32_t ent) {
    own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(&Scr_StartThreadFunctor1_entry)(
        0, 0, 0, 0, function, ent);  // Scr_StartThreadFunctor1
}

}  // namespace

void XB_CDECL BroScr_Exploder(uint32_t, int32_t number) {
    slot<void XB_CDECL(int32_t, uint32_t)>(0x009223bc)(number, 1);  // GScr_SetIntMapValue
    DynArray ents = {nullptr, 0, 0};
    for (uint32_t i = 0;; ++i) {
        uint8_t* const level_object = title_global<uint8_t*>(0x00922ef8);
        DynArray* const list = reinterpret_cast<DynArray*>(level_object + 0x78);
        if (!(static_cast<int32_t>(i) < static_cast<int32_t>(list->size))) break;
        const uint32_t* const source = element(list, i);
        *element(&ents, i) = *source;
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i) {
        if (!entity_handle_is_valid(*element(&ents, i))) continue;
        const uint32_t ent = *element(&ents, i);
        if (!entity_handle_is_valid(ent)) continue;
        ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(ent);
        if (!table) continue;
        const uint32_t* const found = ScriptKeyTable_Find(table, 0xf3b8dce5);
        if ((found ? *found : undefined_int()) == undefined_int()) continue;
        if (*int_slot(*element(&ents, i), 0xf3b8dce5) != static_cast<uint32_t>(number)) continue;
        if (defined(ScrField_h0aedcb08_IsDefined, *element(&ents, i))) level_thread(&ents, i, own_address(&BroScr_ExploderSoundFx_2));  // BroScr_ExploderSoundFx_2
        if (defined(ScrField_script_fxid_IsDefined, *element(&ents, i))) level_thread(&ents, i, own_address(&BroScr_ExploderSoundFx));  // BroScr_ExploderSoundFx
        if (defined(ScrField_script_damage_IsDefined, *element(&ents, i)))
            ent_thread(*element(&ents, i), own_address(&BroScr_InitScriptRadius));  // BroScr_InitScriptRadius
        if (defined(ScrField_script_sound_IsDefined, *element(&ents, i)))
            ent_thread(*element(&ents, i), own_address(&BroScr_ExploderPlaySound));  // BroScr_ExploderPlaySound
        ScriptString name;
        bool named = ScrCall_ScrEnt_GetTargetname(element(&ents, i), &name)->rep != nullptr;
        ScriptString_Release(&name);
        if (named) {
            ScriptString first;
            const bool exploder =
                ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(element(&ents, i), &first), pointer<const char>(0x008246fc));
            ScriptString_Release(&first);
            if (exploder) {
                ent_thread(*element(&ents, i), own_address(&BroScr_NotSolidAfterDelay));  // BroScr_NotSolidAfterDelay
                continue;
            }
            ScriptString second;
            ScriptString third;
            bool chunk = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(element(&ents, i), &second),
                                             pointer<const char>(0x008246e0));  // "exploderchunk"
            bool made_third = false;
            if (!chunk) {
                made_third = true;
                chunk = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(element(&ents, i), &third),
                                            pointer<const char>(0x008246c8));  // "exploderchunk visible"
            }
            if (made_third) ScriptString_Release(&third);
            ScriptString_Release(&second);
            if (chunk) {
                start_thread_functor1(own_address(&BroScr_NotSolidThrow), *element(&ents, i));  // BroScr_NotSolidThrow
                continue;
            }
            if (defined(ScrField_script_fxid_IsDefined, *element(&ents, i))) continue;
            start_thread_functor1(own_address(&BroScr_ScriptDelayConnectPaths), *element(&ents, i));  // BroScr_ScriptDelayConnectPaths
        } else {
            if (defined(ScrField_script_fxid_IsDefined, *element(&ents, i))) continue;
            ent_thread(*element(&ents, i), own_address(&BroScr_ScriptDelayConnectPaths));  // BroScr_ScriptDelayConnectPaths
        }
    }
    if (ents.data) Scr_Free(ents.data);
}

void XB_CDECL BroScr_ExploderIfSet(uint32_t self, ScriptString number) {
    const char* const text = number.rep ? reinterpret_cast<const char*>(number.rep) + 6 : nullptr;
    BroScr_Exploder(self, slot<int32_t XB_CDECL(const char*)>(0x009229e0)(text));  // atol
    ScriptString_Release(&number);
}

void XB_CDECL BroScr_BuildTargetChain(uint32_t owner, uint32_t start) {
    uint32_t current = start;
    do {
        ScriptKeyTable* table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(owner);
        if (!table) table = pointer<ScriptKeyTable>(0x00922e94);
        uint32_t* chain = ScriptKeyTable_Find(table, 0x0605894e);
        if (!chain) {
            const DynArray empty = {nullptr, 0, 0};
            chain = own_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const DynArray*)>(&ScriptKeyTable_SetArray)(
                table, 0x0605894e, &empty);  // ScriptKeyTable_SetArray
        }
        DynArray_PushBack(pointer<uint8_t>(*chain), &current);
        ScriptString target;
        const bool targeted = ScrCall_ScrEnt_GetTarget(&current, &target)->rep != nullptr;
        ScriptString_Release(&target);
        if (targeted) {
            ScriptString name;
            using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
            current = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&current, &name), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
            ScriptString_Release(&name);
        } else {
            current = title_global<uint32_t>(0x00922e84);
        }
    } while (entity_handle_is_valid(current));
}

void XB_CDECL BroScr_SetTargetnamesFromOrigins(DynArray ents) {
    if (ents.capacity != 0 && ents.size != 0) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i)
            ent_thread(*element(&ents, i), own_address(&BroScr_SetTargetnameFromOrigin));  // BroScr_SetTargetnameFromOrigin
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i) {
            const uint32_t ent = *element(&ents, i);
            uint32_t* const functor = slot<Alloc>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a28;
                functor[1] = own_address(&BroScr_TriggeredSpawnerThink);  // BroScr_TriggeredSpawnerThink
                functor[2] = ent;
                functor[3] = title_global<uint32_t>(0x00922e84);
            }
            slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
        }
    }
    if (ents.data) Scr_Free(ents.data);
}

void XB_CDECL BroScr_SetTargetnamesFromOriginsCall(const DynArray* ents) {
    DynArray copy = {nullptr, 0, 0};
    own_function<void XB_THISCALL(DynArray*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&copy, ents);  // DynArray_Assign4_PROLOGUE
    BroScr_SetTargetnamesFromOrigins(copy);
}

void XB_CDECL BroScr_SetFriendlyWaveTrigger(uint32_t, uint32_t trigger) {
    uint32_t hash;
    const uint32_t callback = *Scr_HashName(&hash, pointer<const char>(0x00825adc));  // "_spawner_gmi::friendly_wave_callback"
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221bc)(trigger, 0xe2a210cf, callback);  // GScr_SetEntVar
    uint8_t* const level_object = title_global<uint8_t*>(0x00922ef8);
    if (level_object[0x1cc]) return;
    level_object[0x1cc] = 1;
    const uint32_t self = *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x00922ef8) + 4);
    start_thread_functor1(own_address(&BroScr_FriendlyWaveSpawn), self);  // BroScr_FriendlyWaveSpawn
}

void XB_CDECL BroScr_FallbackerJoinFallback(uint32_t ent, uint32_t index, uint32_t node, ScriptString name) {
    auto table = [](uint32_t e) {
        ScriptKeyTable* t = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(e);
        return t ? t : pointer<ScriptKeyTable>(0x00922e94);
    };
    auto int_at = [](DynArray* array, uint32_t i) {
        if (i >= array->size) DynArray_ResizeInt(array, i + 1, i + 4);
        return pointer<uint32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(array->data)) + i * 4);
    };
    bool defined;
    if (*ScrField_fallback_IsDefined(&defined, ent)) {
        DynArray* const joined = pointer<DynArray>(ScriptKeyTable_GetOrAddArray_7(table(ent), 0x9ef08b23));
        if (*int_at(joined, index) != undefined_int()) {
            ScriptString_Release(&name);
            return;
        }
    }
    *int_at(pointer<DynArray>(ScriptKeyTable_GetOrAddArray_7(table(ent), 0x9ef08b23)), index) = 1;
    *int_slot(ent, 0x8bbe4822) = index;
    uint8_t* level_object = title_global<uint8_t*>(0x00922ef8);
    if (!name.rep) ++*int_at(reinterpret_cast<DynArray*>(level_object + 0x6c), index);
    if (node != 0 && node != 0xffffffffu) {
        level_object = title_global<uint8_t*>(0x00922ef8);
        if (*int_at(reinterpret_cast<DynArray*>(level_object + 0xe4), index) != 0) {
            using Start = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                        const uint32_t*);
            title_function<Start>(0x00660450)(0, 0, 0, 0, own_address(&BroScr_FallbackGoalThink), ent, &index, &node);  // BroScr_FallbackGoalThink
        }
    }
    level_object = title_global<uint8_t*>(0x00922ef8);
    const uint32_t* const level_self = level_object ? reinterpret_cast<const uint32_t*>(level_object + 4) : nullptr;
    const uint32_t self = *level_self;
    uint32_t* const functor = slot<Alloc>(0x00922928)(0x14);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4af4;
        functor[1] = own_address(&BroScr_NotifyFallbackerDied);  // BroScr_NotifyFallbackerDied
        functor[2] = self;
        functor[3] = ent;
        functor[4] = index;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, self, functor, 0);  // Scr_CreateThread
    ScriptString_Release(&name);
}

uint32_t* XB_CDECL BroScr_PickWeightedChoice(uint32_t* out, uint32_t current, uint32_t keep_bits, uint32_t choice1,
                                             uint32_t weight1, uint32_t choice2, uint32_t weight2, uint32_t choice3,
                                             uint32_t weight3) {
    using Reserve = void XB_THISCALL(DynArray*, uint32_t, uint32_t);
    auto at = [](DynArray* array, uint32_t index, uint32_t reserve) {
        if (index >= array->size) title_function<Reserve>(reserve)(array, index + 1, index + 4);
        return pointer<uint32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(array->data)) + index * 4);
    };
    constexpr uint32_t kReserveInt = 0x0045d480;    // DynArray_ReserveInt0_PROLOGUE
    constexpr uint32_t kReserveFloat = 0x0052d530;  // DynArray_Reserve_KASSPASS (in the KASSPASS section)
    DynArray choices = {nullptr, 0, 0};
    DynArray weights = {nullptr, 0, 0};
    title_function<Reserve>(kReserveInt)(&choices, 3, 3);
    title_function<Reserve>(kReserveFloat)(&weights, 3, 3);
    uint32_t result = 0;
    uint32_t count = 0;
    const uint32_t pairs[3][2] = {{choice1, weight1}, {choice2, weight2}, {choice3, weight3}};
    for (uint32_t k = 0; k < 3; ++k) {
        if (!pairs[k][0]) continue;
        *at(&choices, count, kReserveInt) = pairs[k][0];
        *at(&weights, count, kReserveFloat) = pairs[k][1];
        ++count;
    }
    float keep = as_float(keep_bits);
    bool listed = false;
    if (current) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
            if (*at(&choices, i, kReserveInt) == current) {
                listed = true;
                break;
            }
        }
    }
    if (!listed) keep = 0.0f;
    using RandomFloat = long double XB_CDECL(uint32_t);
    const float roll = static_cast<float>(slot<RandomFloat>(0x00922230)(0x3f800000));  // Scr_RandomFloat(1)
    if (keep > roll) {
        result = current;
    } else {
        const bool any = static_cast<int32_t>(count) > 0;
        if (any) {
            float sum = 0.0f;
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i)
                sum = platform_addss(as_float(*at(&weights, i, kReserveFloat)), sum);
            const float scale = title_global<float>(0x0083eb20) / sum;  // 1 / sum
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
                uint32_t* const weight = at(&weights, i, kReserveFloat);
                *weight = bits(platform_mulss(scale, as_float(*weight)));
            }
        }
        const float pick = static_cast<float>(slot<RandomFloat>(0x00922230)(0x3f800000));
        if (any) {
            float total = 0.0f;
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
                total = platform_addss(as_float(*at(&weights, i, kReserveFloat)), total);
                if (total > pick) {
                    result = *at(&choices, i, kReserveInt);
                    break;
                }
            }
        }
    }
    *out = result;
    if (weights.data) Scr_Free(weights.data);
    if (choices.data) Scr_Free(choices.data);
    return out;
}

void XB_CDECL BroScr_AiMaybeDropHealth(uint32_t ent, uint32_t group) {
    HashMap* const groups = reinterpret_cast<HashMap*>(title_global<uint8_t*>(0x00922ef8) + 0xa4);
    DynArray_PushBack(HashMap_Index_3(groups, &group), &ent);
    DynArray* const members =
        reinterpret_cast<DynArray*>(HashMap_Index_3(reinterpret_cast<HashMap*>(title_global<uint8_t*>(0x00922ef8) + 0xa4), &group));
    const uint32_t chosen = static_cast<uint32_t>(random_int(static_cast<int32_t>(members->size)));
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(members->size); ++i) {
        if (!entity_handle_is_valid(*element(members, i))) continue;
        *int_slot(*element(members, i), 0xd66dcf8c) = chosen == i ? 1 : 0;
    }
}

void XB_CDECL BroScr_DropHealth(uint32_t ent) {
    ScriptString skill;
    const bool hardest = ScriptString_Equals(ScrCall_GScr_GetCvar(&skill, 0x00832fec), pointer<const char>(0x008398e0));  // "g_gameskill", "3"
    const bool skip = hardest || *int_slot(ent, 0xd66dcf8c) == 0;
    ScriptString_Release(&skill);
    if (skip) return;
    ScriptString classname;
    ScriptString_FromCString(&classname, pointer<const char>(0x008372e4));  // "item_health_medium"
    uint32_t buffer[3];
    const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    const uint32_t position[3] = {origin[0], origin[1], bits(as_float(origin[2]) + title_global<float>(0x0083eb98))};  // +10
    const uint32_t health =
        slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&classname, position, 0);  // GScr_Spawn
    ScriptString_Release(&classname);
    const float yaw = static_cast<float>(random_int(360));
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00922ba4)(health, bits(0.0f), bits(yaw), bits(0.0f));  // ScrEnt_SetAngles
    bool defined;
    if (*ScrField_script_health_IsDefined(&defined, ent)) {
        const uint32_t* const key = ScrField_script_health(ent);
        uint8_t* const members = HashMap_Index_3(reinterpret_cast<HashMap*>(title_global<uint8_t*>(0x00922ef8) + 0xa4), key);
        DynArray_Resize(members, 0, 0);
    }
    // The level's ring of dropped health (+0xd0, next index +0x200, size +0x88): the oldest goes.
    uint8_t* level_object = title_global<uint8_t*>(0x00922ef8);
    if (at<uint32_t>(level_object, 0xd4) != 0) {
        DynArray* ring = reinterpret_cast<DynArray*>(level_object + 0xd0);
        if (entity_handle_is_valid(*element(ring, at<uint32_t>(level_object, 0x200)))) {
            level_object = title_global<uint8_t*>(0x00922ef8);
            ring = reinterpret_cast<DynArray*>(level_object + 0xd0);
            slot<void XB_CDECL(uint32_t)>(0x00922540)(*element(ring, at<uint32_t>(level_object, 0x200)));  // GScr_Delete
        }
    }
    level_object = title_global<uint8_t*>(0x00922ef8);
    *element(reinterpret_cast<DynArray*>(level_object + 0xd0), at<uint32_t>(level_object, 0x200)) = health;
    ++at<uint32_t>(title_global<uint8_t*>(0x00922ef8), 0x200);
    level_object = title_global<uint8_t*>(0x00922ef8);
    if (static_cast<int32_t>(at<uint32_t>(level_object, 0x200)) > static_cast<int32_t>(at<uint32_t>(level_object, 0x88)))
        at<uint32_t>(level_object, 0x200) = 0;
}

void XB_CDECL BroScr_SpawnGlobalAmbient(ScriptString name) {
    ScriptKeyTable* table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(
        *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x00922ef8) + 4));
    if (!table) table = pointer<ScriptKeyTable>(0x00922e94);
    title_function<void XB_THISCALL(ScriptKeyTable*, uint32_t, const ScriptString*)>(0x00458d40)(table, 0x9c1519ac,
                                                                                               &name);  // ScriptKeyTable_SetString
    uint32_t hash;
    const uint32_t* const key = Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name));
    const ScriptString* const alias = title_function<const ScriptString* XB_THISCALL(uint8_t*, const uint32_t*)>(0x00461120)(
        title_global<uint8_t*>(0x00922ef8) + 0x28, key);  // HashMap_Index_PROLOGUE_11
    ScriptString copy;
    ScriptString_Copy(&copy, alias);
    if (at<uint32_t>(title_global<uint8_t*>(0x00922ef8), 0x1c) != 0) {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        uint32_t buffer[3];
        const uint32_t* const o =
            slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, title_global<uint32_t>(0x009236dc));  // player origin
        const uint32_t origin[3] = {o[0], o[1], o[2]};
        const uint32_t spawned =
            slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
        at<uint32_t>(title_global<uint8_t*>(0x00922ef8), 0x1d4) = spawned;
        ScriptString_Release(&classname);
        ScriptString targetname;
        ScriptString_FromCString(&targetname, pointer<const char>(0x00826ec4));  // "global_ambient"
        const uint32_t* const ambient = reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x00922ef8) + 0x1d4);
        ScriptString argument;
        ScriptString_Copy(&argument, &targetname);
        slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(*ambient, argument);  // ScrEnt_SetTargetname (releases)
        ScriptString_Release(&targetname);
    }
    ScriptString_Release(&copy);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_InitMg42CoverSpots() {
    ScriptString value;
    const bool unset = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x007f49a8), pointer<const char>(0x0082dfeb));  // "mg42", ""
    ScriptString_Release(&value);
    if (unset) slot<void XB_CDECL(const char*, const char*)>(0x009222e4)(pointer<const char>(0x007f49a8),
                                                                        pointer<const char>(0x0080da58));  // GScr_SetCvar("mg42", "off")
    DynArray turrets = {nullptr, 0, 0};
    ScriptString name;
    ScriptString_FromCString(&name, pointer<const char>(0x007f49a8));  // "mg42"
    Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0x0d786b4c, reinterpret_cast<uint8_t*>(&turrets), 0);
    ScriptString_Release(&name);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(turrets.size); ++i)
        ent_thread(*element(&turrets, i), own_address(&BroScr_Mg42CoverSpotThink));  // BroScr_Mg42CoverSpotThink
    DynArray spots = {nullptr, 0, 0};
    // A BroScr_TrackTriggerTouch thread (functor 0x007f4a40 {vtable, function, ent, kind}) per spot.
    auto track_all = [&](uint32_t targetname, uint32_t kind) {
        ScriptString n;
        ScriptString_FromCString(&n, pointer<const char>(targetname));
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&n)), 0x0d786b4c, reinterpret_cast<uint8_t*>(&spots), 0);
        ScriptString_Release(&n);
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spots.size); ++i) {
            ScriptString k;
            ScriptString* const kind_string = ScriptString_FromCString(&k, pointer<const char>(kind));
            const uint32_t spot = *element(&spots, i);
            uint32_t* const functor = slot<Alloc>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a40;
                functor[1] = own_address(&BroScr_TrackTriggerTouch);  // BroScr_TrackTriggerTouch
                functor[2] = spot;
                ScriptString_Copy(reinterpret_cast<ScriptString*>(functor + 3), kind_string);
            }
            slot<CreateThread>(0x0092216c)(0, 0, 0, spot, functor, 0);  // Scr_CreateThread
            ScriptString_Release(&k);
        }
    };
    track_all(0x00826ef4, 0x00826eec);  // "mg42cover", "crater"
    track_all(0x00826edc, 0x00825c70);  // "mg42cover_cow", "cow"
    at<uint32_t>(title_global<uint8_t*>(0x00922ef8), 0x14c) = title_global<uint32_t>(0x0083ef04);  // 24
    if (spots.data) Scr_Free(spots.data);
    if (turrets.data) Scr_Free(turrets.data);
}

}  // namespace game

