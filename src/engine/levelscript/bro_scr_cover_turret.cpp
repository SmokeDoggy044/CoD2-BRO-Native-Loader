#include "bro_scr_cover_turret.h"

#include <cstring>

#include "g_scr_runtime.h"
#include "scr_access.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_banim_handles.h"

namespace game {

namespace {

template <typename T>
T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}
template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// An engine call through the interface block (pScr_* slot).
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

uint8_t* level_object() { return title_global<uint8_t*>(0x00922ef8); }  // g_levelScriptObject

uint32_t& animset_word(uint8_t* set, uint32_t offset) { return at<uint32_t>(set, offset); }

constexpr uint32_t kOne = 0x3f800000;  // 1.0f

void set_anim_knob(uint32_t ent, uint32_t anim, uint32_t goal, uint32_t blend, uint32_t rate, uint32_t restart) {
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225d8)(ent, anim, goal, blend,
                                                                                             rate, restart);
}
void actor_set_int_1370(uint32_t actor, uint32_t value) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(actor, value);
}
uint32_t entity_handle_is_valid(uint32_t handle) {
    return slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(handle);
}
int32_t random_int(int32_t max) { return slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(max); }

// One of the level object's name counters: random at first, then +1 modulo `count`.
void step_name_counter(uint32_t offset, uint32_t count, uint8_t* obj, uint32_t undefined) {
    uint32_t& counter = at<uint32_t>(obj, offset);
    if (counter == undefined) {
        const int32_t r = random_int(static_cast<int32_t>(count));
        at<int32_t>(level_object(), offset) = r;
    } else {
        const uint32_t next = counter + 1;
        counter = next;
        at<uint32_t>(level_object(), offset) = next % count;
    }
}

}  // namespace

uint32_t XB_REGS3 XB_CDECL BroScr_BAnimHandles_mg42(uint32_t* out, uint32_t value, uint32_t hash) {
    uint32_t handle;
    if (hash == 0x19effa78)
        handle = 0x008cf7bc;
    else if (hash == 0x7eb70b0f)
        handle = 0x008cf7a4;
    else if (hash == 0x88333b05)
        handle = 0x008cf7b0;
    else
        return 0;
    if (out) {
        *out = title_global<uint32_t>(handle);
        return title_global<uint32_t>(handle);
    }
    title_global<uint32_t>(handle) = value;
    return value;
}

void XB_CDECL BroScr_EmptyFunc_77() {}

uint32_t XB_CDECL BroScr_BAnimHandle_GetSet(uint32_t table, uint32_t hash, uint32_t* out, uint32_t value) {
    if (table == 0x00371ab2) return BroScr_BAnimHandles_mg42(out, value, hash);
    if (table == 0x3b8fe918)  // BroScr_BAnimHandles_generic_human (EAX hash, EDX value, ECX out)
        return own_function<uint32_t XB_REGS3 XB_CDECL(uint32_t, uint32_t, uint32_t*)>(&BroScr_BAnimHandles_generic_human)(hash, value, out);
    return 0;
}

uint32_t XB_CDECL BroScr_ReturnFalse() { return 0; }

uint8_t XB_CDECL BroScr_ReturnTrue() { return 1; }

uint32_t XB_CDECL BroScr_BAnimHandle_Lookup(uint32_t table, uint32_t hash) {
    if (table == 0x00371ab2) return BroScr_BAnimHandles_mg42(&table, 0, hash);
    if (table == 0x3b8fe918)
        return own_function<uint32_t XB_REGS3 XB_CDECL(uint32_t, uint32_t, uint32_t*)>(&BroScr_BAnimHandles_generic_human)(hash, 0, &table);
    return 0;
}

float* XB_CDECL BroScr_GetNodeRadius(float* out, uint32_t node) {
    *out = slot<float XB_CDECL(uint32_t)>(0x00922ac8)(node);  // pScr_ScrPathNode_GetRadius
    return out;
}

uint8_t* XB_CDECL BroScr_IsValidHandle(uint8_t* out, uint32_t handle) {
    bool valid = false;
    if (handle != 0 && handle != 0xffffffffu && slot<uint32_t XB_CDECL(uint32_t)>(0x00922224)(handle) != 0)
        valid = true;
    *out = valid;
    return out;
}

uint8_t* XB_CDECL BroScr_MgTurretCanTargetStance(uint8_t* out) {
    if (!entity_handle_is_valid(at<uint32_t>(level_object(), 0x190))) {
        *out = 0;
        return out;
    }
    ScriptString stance;
    ScriptString_FromRep(&stance, nullptr);
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x00922600)(title_global<uint32_t>(0x009236dc), &stance);  // GetStance
    if (ScriptString_Equals(&stance, pointer<const char>(0x008367fc))  // "prone"
        || (ScriptString_Equals(&at<ScriptString>(level_object(), 0x1c8), pointer<const char>(0x00825c70))  // "cow"
            && ScriptString_Equals(&stance, pointer<const char>(0x0083ae7c))))                         // "crouch"
        *out = 1;
    else
        *out = 0;
    ScriptString_Release(&stance);
    return out;
}

ScriptString* XB_CDECL BroScr_SetPrivateName(ScriptString* out, uint32_t actor) {
    ScriptString name;
    ScriptString_FromRep(&name, nullptr);
    const int32_t roll = random_int(100);
    const uint32_t undefined = title_global<uint32_t>(0x00922ec0);  // g_scrUndefinedInt
    uint8_t* const obj = level_object();
    ScriptString rank;
    const auto get_int_f0 = slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30);  // pScr_ScrActor_GetInt_f0
    const char* const space = pointer<const char>(0x0082d6e4);               // " "
    if (roll > 60) {
        step_name_counter(0x60, 101, obj, undefined);
        if (get_int_f0(actor) == 0xd8e9c234) {
            ScriptString_Assign(&name, ScrCall_GScr_GetLocalizedString(&rank, 0x04e39b9d));
            ScriptString_Release(&rank);
        } else {
            ScriptString_Assign(&name, ScrCall_GScr_GetLocalizedString(&rank, 0x9c730394));
            ScriptString_Release(&rank);
            ScriptString_AssignCString(&name, pointer<const char>(0x00825c74));  // "Pvt. "
        }
        ScriptString_AppendCString(&name, space);
        ScriptString_AppendCString(&name, pointer<const char* const>(0x008cd510)[at<uint32_t>(level_object(), 0x60)]);
    } else {
        step_name_counter(0x154, 0x31, obj, undefined);
        ScriptString* text;
        if (get_int_f0(actor) == 0xd8e9c234)
            text = ScrCall_GScr_GetLocalizedString(&rank, 0x04e39b9d);
        else if (random_int(100) < 0x32)
            text = ScrCall_GScr_GetLocalizedString(&rank, 0x9c734f4c);
        else
            text = ScrCall_GScr_GetLocalizedString(&rank, 0x9c730db4);
        ScriptString_Assign(&name, text);
        ScriptString_Release(&rank);
        ScriptString_AppendCString(&name, space);
        ScriptString_AppendCString(&name, pointer<const char* const>(0x008cd6a8)[at<uint32_t>(level_object(), 0x154)]);
    }
    ScriptString_Copy(out, &name);
    ScriptString_Release(&name);
    return out;
}

void XB_CDECL BroScr_DeleteOthersWithKey(uint32_t keep, uint32_t key) {
    uint8_t* obj = level_object();
    auto list_end = [&obj]() { return at<uint32_t>(obj, 0x50) + at<uint32_t>(obj, 0x58) * 12; };
    uint32_t entry;
    do {
        // The search starts over at the first entry after each delete.
        entry = at<uint32_t>(obj, 0x50);
        const uint32_t end = list_end();
        for (; entry != end; entry += 12) {
            if (key == at<uint32_t>(pointer<void>(entry), 0) && at<uint32_t>(pointer<void>(entry), 8) != keep) {
                slot<void XB_CDECL(uint32_t)>(0x00922540)(at<uint32_t>(pointer<void>(entry), 8));  // GScr_Delete
                obj = level_object();
                break;
            }
        }
    } while (entry != list_end());
}

void XB_CDECL BroScr_DeleteFloodSpawner(uint32_t ent) {
    if (!entity_handle_is_valid(ent)) return;
    ScriptString targetname;
    const bool flood = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&ent, &targetname),
                                           pointer<const char>(0x00824e54));  // "flood_spawner"
    ScriptString_Release(&targetname);
    if (flood) slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void XB_CDECL Bro__spawner_gmi__Axis_Death(uint32_t self, uint32_t attacker) {
    uint32_t hash;
    const uint32_t callback = *Scr_HashName(&hash, pointer<const char>(0x007f51b0));  // "_spawner_gmi::Axis_Death"
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221c0)(self, 0x06730ae8, callback);  // GScr_GetEntVar
    if (!entity_handle_is_valid(attacker) || attacker != title_global<uint32_t>(0x009236dc)) return;
    uint8_t* const obj = level_object();
    const uint32_t handle_address = obj ? address_of(obj) + 4 : 0;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(title_global<uint32_t>(handle_address), 0xb1ada748);  // QueueNotify
    at<uint32_t>(level_object(), 0xdc) += 1;
}

void XB_CDECL BroScr_SetAimBlendTimes(uint32_t first, uint32_t second) {
    const float& undefined = title_global<float>(0x00922eac);  // g_scrUndefinedFloat
    uint32_t& first_time = title_global<uint32_t>(0x009230ec);
    uint32_t& second_time = title_global<uint32_t>(0x0092379c);
    uint32_t current = first;
    first_time = first;
    second_time = second;
    if (*reinterpret_cast<const float*>(&first) == undefined) {
        first_time = title_global<uint32_t>(0x0083ebf8);  // 0.1f
        current = first_time;
    }
    if (!(*reinterpret_cast<const float*>(&second) == undefined)) return;
    second_time = title_global<uint32_t>(0x0083eba8);  // 2.0f
    const uint32_t two = second_time;
    const int32_t larger = *reinterpret_cast<const float*>(&current) > *reinterpret_cast<const float*>(&two) ? 1 : 0;
    const float flag = static_cast<float>(larger);
    if (!(flag == title_global<float>(0x0083eafc))) second_time = current;  // != 0.0f
}

void XB_CDECL BroScr_TurretSetIdleKnobs(uint32_t gunner, uint32_t turret) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(gunner, 0x07b4e7f7);  // Scr_EndOn
    set_anim_knob(turret, title_global<uint32_t>(0x008cf684), kOne, 0x3e19999a, 0, 1);
    actor_set_int_1370(gunner, title_global<uint32_t>(0x008cf0d8));
    set_anim_knob(gunner, title_global<uint32_t>(0x008cf0d8), kOne, 0x3e19999a, 0, 1);
}

void XB_CDECL BroScr_Turret30calFire_2(uint32_t gunner, uint32_t turret) {
    set_anim_knob(turret, title_global<uint32_t>(0x008cf684), kOne, 0x3e19999a, 0, 1);
    actor_set_int_1370(gunner, title_global<uint32_t>(0x008cf348));  // g_anim_stand30calgunner_fire
    set_anim_knob(gunner, title_global<uint32_t>(0x008cf348), kOne, 0x3e19999a, 0, 1);
}

uint32_t* XB_CDECL BroScr_GetFloatOrDefault500(uint32_t* out, uint32_t sentient) {
    float distance = slot<float XB_CDECL(uint32_t)>(0x00922690)(sentient);  // GScr_SentientNearestEnemyDist
    uint32_t bits;
    std::memcpy(&bits, &distance, 4);
    if (distance == title_global<float>(0x00922eac)) bits = title_global<uint32_t>(0x0083ebec);  // 500.0f
    *out = bits;
    return out;
}

#include "bro_scr_cover_animsets.inc"

void XB_CDECL BroScr_CoverLeftInitAnimSets() {
    const uint8_t done = title_global<uint8_t>(0x009233e4);
    if (done != title_global<uint8_t>(0x00922ed0) && done != 0) return;
    title_global<uint8_t>(0x009233e4) = 1;
    BroScr_CoverLeftAnimSet_1(pointer<uint8_t>(0x00923220));
    BroScr_CoverLeftAnimSet_2(pointer<uint8_t>(0x009235b0));
    BroScr_CoverLeftAnimSet_3(pointer<uint8_t>(0x00923480));
    BroScr_CoverLeftAnimSet_4(pointer<uint8_t>(0x009233e8));
    BroScr_CoverLeftAnimSet_5(pointer<uint8_t>(0x00923350));
    BroScr_CoverLeftAnimSet_6(pointer<uint8_t>(0x00923188));
    BroScr_CoverLeftAnimSet_7(pointer<uint8_t>(0x009232b8));
}

void XB_CDECL BroScr_EmptyFunc_78() {}

void XB_CDECL BroScr_CoverRightInitAnimSets() {
    const uint8_t done = title_global<uint8_t>(0x00922f00);
    if (done != title_global<uint8_t>(0x00922ed0) && done != 0) return;
    title_global<uint8_t>(0x00922f00) = 1;
    BroScr_CoverRightAnimSet_1(pointer<uint8_t>(0x009230f0));
    BroScr_CoverRightAnimSet_2(pointer<uint8_t>(0x00922fc0));
    BroScr_CoverRightAnimSet_3(pointer<uint8_t>(0x00923058));
    BroScr_CoverRightAnimSet_4(pointer<uint8_t>(0x00923708));
    BroScr_CoverRightAnimSet_5(pointer<uint8_t>(0x00922f08));
    BroScr_CoverRightAnimSet_6(pointer<uint8_t>(0x00923648));
    BroScr_CoverRightAnimSet_7(pointer<uint8_t>(0x00923518));
}

void XB_CDECL BroScr_EmptyFunc_79() {}

uint32_t* XB_CDECL BroScr_ActorMayMoveToPointForStance(uint32_t* out, uint32_t actor, ScrVec3 point, uint32_t yaw) {
    // The original hands over the address of its by-value argument; the flag word's upper bytes are
    // the caller's ECX there (the callee reads only the low byte).
    const uint32_t prone = slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(actor) == 0x0738b1a5 ? 1 : 0;  // GetInt_184
    const ScrVec3 copy = point;
    const uint8_t ok = slot<uint8_t XB_CDECL(uint32_t, const ScrVec3*, uint32_t, uint32_t)>(0x0092278c)(actor, &copy, yaw,
                                                                                                      prone);
    *out = ok != 0 ? 1 : 0;
    return out;
}

void XB_CDECL BroScr_MeleeNoteTrackHandler(uint32_t ent, uint32_t note) {
    if (note == 0x06e33cc5) slot<void XB_CDECL(uint32_t)>(0x009226a0)(ent);
}

void XB_CDECL BroScr_TurretProneMG42Aim(uint32_t gunner, uint32_t turret) {
    set_anim_knob(turret, title_global<uint32_t>(0x008cf7a4), kOne, 0x3e4ccccd, kOne, 1);
    actor_set_int_1370(gunner, title_global<uint32_t>(0x008cf204));  // g_anim_pronemg42gunner_aim
    set_anim_knob(gunner, title_global<uint32_t>(0x008cf204), kOne, 0x3e4ccccd, kOne, 1);
}

void XB_CDECL BroScr_TurretProneMG42AimLoop(uint32_t gunner) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(gunner, 0x07b4e7f7);  // Scr_EndOn
    actor_set_int_1370(gunner, title_global<uint32_t>(0x008cf204));
    set_anim_knob(gunner, title_global<uint32_t>(0x008cf204), kOne, 0x3e4ccccd, kOne, 1);
}

void XB_CDECL BroScr_TurretStandMG42Aim(uint32_t gunner) {
    actor_set_int_1370(gunner, title_global<uint32_t>(0x008cf630));  // g_anim_standmg42gunner_aim
    set_anim_knob(gunner, title_global<uint32_t>(0x008cf630), kOne, 0x3e4ccccd, kOne, 1);
}

}  // namespace game
