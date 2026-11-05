#include "bro_scr_level_threads_2.h"

#include <cmath>
#include <cstring>

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_thread_misc.h"
#include "bro_scr_level_threads.h"
#include "bro_scr_mission.h"
#include "bro_scr_squad.h"
#include "bro_scr_util.h"
#include "bro_scr_world.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "scr_access.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

// (The file-local helpers below are copies of bro_scr_level_threads.cpp's.)

using CreateThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using Index = uint32_t XB_THISCALL(uint32_t, const uint32_t*);
using PlaySound = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using HudSet = void XB_CDECL(uint32_t, uint32_t);

inline uint32_t level() { return title_global<uint32_t>(0x00922ee0); }

// The level entity's handle: the word at +4 of the level object (0x00922ee0), read through null.
inline uint32_t level_handle() {
    const uint32_t object = level();
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(field));
}

// The level object's word at `offset` (0x00922ee0 + offset).
inline uint32_t& level_field(uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(level() + offset));
}

// The value byte of the level flag `key` (the level's flag map at +0x32c: HashMap_Find, inserted as
// the undefined bool when missing).
inline uint8_t* level_flag(uint32_t key) {
    const uint32_t map = level() + 0x32c;
    uint32_t pair[2];
    title_function<void XB_THISCALL(uint32_t, uint32_t*, const uint32_t*)>(0x0045e6d0)(map, &pair[0], &key);  // HashMap_Find
    uint32_t node = pair[0];
    if (node == *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(map + 8))) {  // end
        pair[0] = key;
        pair[1] = title_global<uint8_t>(0x00922ed0);  // g_scrUndefinedBool
        uint32_t result[2];
        using Insert = uint32_t* XB_THISCALL(uint32_t, uint32_t*, const uint32_t*);
        node = *title_function<Insert>(0x004608a0)(map, result, pair);  // HashMap_Insert
    }
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(node + 0xc));
}

// The int value under `*key` in the level map at `offset` (HashMap_Find, inserted as the undefined
// int when missing).
inline uint32_t* level_map_int(uint32_t offset, const uint32_t* key) {
    const uint32_t map = level() + offset;
    uint32_t pair[2];
    title_function<void XB_THISCALL(uint32_t, uint32_t*, const uint32_t*)>(0x0045e6d0)(map, &pair[0], key);  // HashMap_Find
    uint32_t node = pair[0];
    if (node == *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(map + 8))) {  // end
        pair[0] = *key;
        pair[1] = undefined_int();
        uint32_t result[2];
        using Insert = uint32_t* XB_THISCALL(uint32_t, uint32_t*, const uint32_t*);
        node = *title_function<Insert>(0x004608a0)(map, result, pair);  // HashMap_Insert
    }
    return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(node + 0xc));
}

inline uint32_t* new_functor(uint32_t size) {
    return slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(size);  // ScrIface_Alloc
}

inline void create_thread(uint32_t owner, uint32_t* functor) {
    slot<CreateThread>(0x0092216c)(0, 0, 0, owner, functor, 0);  // Scr_CreateThread
}

// Scr_CreateThread(owner, {vtable, body, arg}) (a null functor when the allocation fails).
inline void thread_1(uint32_t owner, uint32_t vtable, uint32_t body, uint32_t arg) {
    uint32_t* const functor = new_functor(0xc);
    if (functor) {
        functor[0] = vtable;
        functor[1] = body;
        functor[2] = arg;
    }
    create_thread(owner, functor);
}

// The DynArray element at i, grown first when i is past the end (the scripts' checked index).
inline uint32_t& element(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return array->data[i];
}

inline void hud_set(uint32_t setter, uint32_t value) { slot<HudSet>(setter)(level_field(0x80c), value); }

// Scr_QueueNotify(level, the hash of the title string `name`).
inline void notify_level(uint32_t name) {
    const uint32_t handle = level_handle();
    uint32_t hash;
    queue_notify(handle, *Scr_HashName(&hash, pointer<const char>(name)));
}

// Scr_QueueNotify(ent, the hash of the title string `name`).
inline void notify_ent(uint32_t ent, uint32_t name) {
    uint32_t hash;
    queue_notify(ent, *Scr_HashName(&hash, pointer<const char>(name)));
}

inline void notify_not_on_turret() { notify_level(0x00824f20); }  // "player not on turret"

constexpr uint32_t kSetAlpha = 0x00922a78;  // ScrHudElem_SetAlpha
constexpr uint32_t kOnTurret = 0xa26502a4;  // the level flag the shell hud keeps

// Whether cvar `name` equals `text`.
inline bool cvar_is(uint32_t name, uint32_t text) {
    ScriptString value;
    const bool equal = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, name), pointer<const char>(text));
    ScriptString_Release(&value);
    return equal;
}

// The level fx map (+0x57c) keys 0x06579a08 and "flesh small" set to `text`.
inline void set_impact_fx(uint32_t text) {
    using StringIndex = ScriptString* XB_THISCALL(uint32_t, const uint32_t*);
    const uint32_t key = 0x06579a08;
    ScriptString_AssignCString(title_function<StringIndex>(0x00461120)(level() + 0x57c, &key), pointer<const char>(text));
    uint32_t hash;
    const uint32_t* const flesh = Scr_HashName(&hash, pointer<const char>(0x0082086c));  // "flesh small"
    ScriptString_AssignCString(title_function<StringIndex>(0x00461120)(level() + 0x57c, flesh), pointer<const char>(text));
}

// The bool key `key` of `ent`, added as the undefined bool.
inline uint8_t* bool_slot(uint32_t ent, uint32_t key) {
    return reinterpret_cast<uint8_t*>(key_slot(ent, key, title_global<uint8_t>(0x00922ed0)));
}

// A fire-wait thread on (and for) `tank`.
inline void fire_wait_thread(uint32_t tank) { thread_1(tank, 0x007f5ba0, 0x0015b7e0, tank); }  // BroScr_TankShellHudFireWait

// The ready-shell hud as BroScr_TankShellReadyHud(_2) build it (a new hud elem in level +0x80c).
inline void build_shell_hud() {
    uint32_t handle;
    level_field(0x80c) = *slot<uint32_t* XB_CDECL(uint32_t*)>(0x0092248c)(&handle);  // GScr_GetCurrentZoneId (a new hud elem)
    hud_set(0x00922a58, 1);      // ScrHudElem_Set_14
    hud_set(0x00922a60, 1);      // ScrHudElem_Set_18
    hud_set(0x00922a48, 0x24e);  // ScrHudElem_Set_04 (x 590)
    hud_set(0x00922a50, 0x1a4);  // ScrHudElem_Set_08 (y 420)
    ScriptString shader;
    ScriptString_FromCString(&shader, pointer<const char>(0x00824f38));  // "gfx/hud/hud@fire_ready_shell.tga"
    using SetShader = void XB_CDECL(uint32_t*, ScriptString*, uint32_t, uint32_t);
    slot<SetShader>(0x009228ec)(&level_field(0x80c), &shader, 0x40, 0x40);  // GScr_HudSetShader
    ScriptString_Release(&shader);
    hud_set(kSetAlpha, 0);
}

// GScr_PlaySound(ent, the title string `text`, 0, 0, 0) through a ScriptString temporary.
inline void play_text(uint32_t ent, uint32_t text) {
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(text));
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &sound, 0, 0, 0);  // GScr_PlaySound
    ScriptString_Release(&sound);
}

// sqrt((a*a + b*b) + c*c) on the x87 as the title computes it.
inline float norm3(float a, float b, float c) { return platform_x87_norm3(a, b, c); }

// Whether any lane of `v` differs from the undefined vector lane.
inline bool vec_defined(const ScrVec3* v) {
    const float lane = as_float(title_global<uint32_t>(0x00922eb8));
    return as_float(v->v[0]) != lane || as_float(v->v[1]) != lane || as_float(v->v[2]) != lane;
}

// Whether `ent`'s targetname is "_fx_creation".
inline bool is_fx_creation(uint32_t ent) {
    ScriptString name;
    bool named = false;
    if (ScrCall_ScrEnt_GetTargetname(&ent, &name)->rep != nullptr) {
        ScriptString again;
        named = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&ent, &again), pointer<const char>(0x00825234));
        ScriptString_Release(&again);
    }
    ScriptString_Release(&name);
    return named;
}

}  // namespace

void XB_CDECL BroScr_ArrayUseAnimTreeByAnimname(DynArray ents) {
    if (static_cast<int32_t>(ents.size) > 0) {
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < static_cast<int32_t>(ents.size); ++i) {
            ScriptString name;
            const ScriptString* const animname = ScrCall_ScrEnt_GetAnimname(&element(&ents, i - 1), &name);
            uint32_t* const ent = &element(&ents, i - 1);
            uint32_t hash;
            const uint32_t* const key = Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(animname));
            const uint32_t tree = title_function<Index>(0x00461120)(level() + 0x238, key);  // HashMap_Index
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x009225f0)(*ent, tree);  // GScr_UseAnimTree
            ScriptString_Release(&name);
        }
    }
    if (ents.data) Scr_Free(ents.data);
}

void XB_CDECL BroScr_TankShellHudFireWait(uint32_t ent) {
    Scr_EndOnString(level_handle(), pointer<const char>(0x00824f20));  // "player not on turret"
    *level_flag(kOnTurret) = 1;
    while (level_field(0x3b0) == 1) {
        hud_set(kSetAlpha, 1);
        Scr_WaitTillString(ent, pointer<const char>(0x0083aa04));  // "turret_fire"
        hud_set(kSetAlpha, 0);
        Scr_Wait(4.0f);
    }
}

void XB_CDECL BroScr_TankShellHudHideOnDeath(uint32_t ent) {
    Scr_WaitTillString(ent, pointer<const char>(0x00831f60));  // "death"
    hud_set(kSetAlpha, 0);
    notify_not_on_turret();
    *level_flag(kOnTurret) = 0;
}

void XB_CDECL BroScr_TankShellReadyHud(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    BroScr_TankShellReadySetHealth(ent);
    thread_1(ent, 0x007f5ba0, 0x00136120, ent);  // BroScr_VehicleDeath_Flak88
    thread_1(ent, 0x007f5ba0, 0x0012f8b0, ent);  // BroScr_FlakFireAndReload
    build_shell_hud();
    level_field(0x3b0) = 0;
    *level_flag(kOnTurret) = 0;
    thread_1(ent, 0x007f5ba0, 0x0015b8d0, ent);  // BroScr_TankShellHudHideOnDeath
    for (;;) {
        const uint32_t owner = slot<uint32_t XB_CDECL(uint32_t)>(0x009228a8)(ent);  // GScr_Vehicle_GetOwnerHandle
        if (entity_handle_is_valid(owner)) {
            level_field(0x3b0) = 1;
            if ((*reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(level() + 0x204)) & 1) == 0)
                fire_wait_thread(ent);
        } else {
            level_field(0x3b0) = 0;
            hud_set(kSetAlpha, 0);
            notify_not_on_turret();
            *level_flag(kOnTurret) = 0;
        }
        Scr_Wait(0.15f);
    }
}

void XB_CDECL BroScr_LoopSoundUntilFlag(uint32_t ent, uint32_t alias, uint32_t ender) {
    if (ender != undefined_handle()) slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(level_handle(), ender);  // Scr_EndOn
    for (;;) {
        ScriptKeyTable* const table = ent_script_object(ent);
        uint32_t* value = ScriptKeyTable_Find(table, 0x75c85f71);
        if (!value) value = ScriptKeyTable_Set(table, 0x75c85f71, title_global<uint8_t>(0x00922ed0));
        if (*reinterpret_cast<uint8_t*>(value) != 0) return;
        const uint32_t sound = title_function<Index>(0x00461120)(level() + 0x458, &alias);  // HashMap_Index
        slot<PlaySound>(0x009221dc)(ent, sound, 0x10b1bdae, 1, 0);  // GScr_PlaySound
        Scr_WaitTill(ent, 0x10b1bdae);
    }
}

void XB_CDECL BroScr_RandomSoundBursts(uint32_t ent, uint32_t alias, int32_t min, int32_t max, float wait_base,
                                       float wait_random, float burst_base, float burst_random, uint32_t ender) {
    using RandomFloat = long double XB_CDECL(float);
    Scr_WaitFrames(0);
    if (ender != undefined_handle()) slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(level_handle(), ender);  // Scr_EndOn
    for (;;) {
        const int32_t bursts = slot<int32_t XB_CDECL(int32_t, int32_t)>(0x00922234)(min, max);  // Scr_RandomIntRange
        for (int32_t n = bursts; n > 0; --n) {
            const uint32_t sound = title_function<Index>(0x00461120)(level() + 0x57c, &alias);  // HashMap_Index
            slot<PlaySound>(0x009221dc)(ent, sound, 0x2c5b1495, 0, 0);  // GScr_PlaySound
            Scr_WaitTill(ent, 0x2c5b1495);
            const long double random = slot<RandomFloat>(0x00922230)(burst_random);  // Scr_RandomFloat
            Scr_Wait(static_cast<float>(burst_base + random));
        }
        const long double random = slot<RandomFloat>(0x00922230)(wait_random);  // Scr_RandomFloat
        Scr_Wait(static_cast<float>(wait_base + random));
    }
}

void XB_CDECL BroScr_PlayBloodImpactFx() {
    bool flesh = cvar_is(0x00838dfc, 0x0082e058);  // cg_blood == "1"
    if (flesh)
        set_impact_fx(0x007f92ac);  // "impact_flesh"
    else
        set_impact_fx(0x00824f5c);  // "impact_smg_generic"
    for (;;) {
        Scr_Wait(2.0f);
        if (flesh) {
            if (cvar_is(0x00838dfc, 0x0082e098)) continue;  // cg_blood == "0"
            flesh = false;
            set_impact_fx(0x00824f5c);
        } else {
            if (cvar_is(0x00838dfc, 0x0082e058)) continue;  // cg_blood == "1"
            flesh = true;
            set_impact_fx(0x007f92ac);
        }
    }
}

void XB_CDECL BroScr_MinefieldKill(uint32_t ent, uint32_t trigger) {
    *bool_slot(ent, 0x8903ef3c) = 1;
    const long double random = slot<long double XB_CDECL(float)>(0x00922230)(0.5f);  // Scr_RandomFloat
    Scr_Wait(static_cast<float>(random + title_global<float>(0x0083eb24)));       // + 0.5
    if (slot<bool XB_CDECL(uint32_t, uint32_t)>(0x00922530)(ent, trigger)) {     // GScr_IsTouching
        if (ent == title_global<uint32_t>(0x00922eec)) {
            notify_level(0x008242e4);  // "mine death"
        } else {
            const uint32_t object = level();
            const uint32_t* const handle = object ? pointer<uint32_t>(object + 4) : nullptr;
            ScriptString sound;
            ScriptString* const text = ScriptString_FromCString(&sound, pointer<const char>(0x00824f90));  // "explo_mine"
            uint32_t buffer[3];
            const uint32_t* const o = slot<const uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
            uint32_t origin[3] = {o[0], o[1], o[2]};
            title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*,
                                             uint32_t*)>(0x0017b150)(0, 0, 0, 0, 0x001823a0, *handle, text,
                                                                     origin);  // Scr_StartThreadFunctorN(BroScr_PlaySoundOnTempOrigin_4)
            ScriptString_Release(&sound);
        }
        const uint32_t lane = undefined_lane();
        uint32_t at[3] = {lane, lane, lane};
        slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922524)(ent, at);  // GScr_GetOrigin
        const uint32_t origin[3] = {at[0], at[1], at[2]};
        const uint32_t key = 0x7857af8e;
        const uint32_t sound = title_function<Index>(0x00461120)(level() + 0x57c, &key);  // HashMap_Index
        slot<PlaySound>(0x009221dc)(ent, sound, 0, 0, 0);  // GScr_PlaySound
        slot<void XB_CDECL(const uint32_t*, float, float, float)>(0x00922428)(origin, 300.0f, 2000.0f, 50.0f);  // GScr_RadiusDamage
        if (ent == title_global<uint32_t>(0x00922eec)) {
            ScriptString text;
            ScriptString_FromCString(&text, pointer<const char>(0x00824f70));  // "INGAME_MISSIONFAIL_MINEFIELD"
            slot<void XB_CDECL(ScriptString*)>(0x00922438)(&text);  // GScr_MissionFailed
            ScriptString_Release(&text);
        }
    }
    *bool_slot(ent, 0x8903ef3c) = 0;
}

void XB_CDECL BroScr_PlayShellIncoming(uint32_t ent, uint32_t type) {
    using PlayString = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
    bool interior = false;
    bool defined;
    if (*ScrField_ambient_IsDefined(&defined, level_handle())) {
        const ScriptKeyTable* const table = ent_script_object(*pointer<uint32_t>(level() + 4));
        ScriptString ambient;
        interior = ScriptString_Equals(
            static_cast<ScriptString*>(ScrField_ambient_GetCopy(
                const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(table) + 0xc), &ambient)),
            pointer<const char>(0x00816a4c));  // "interior"
        ScriptString_Release(&ambient);
    }
    const uint32_t count = *title_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(0x00460f80)(
        level() + 0x430, &type);  // HashMap_Index_14
    if (count != undefined_int()) return;
    ScriptString sound;
    if (interior)
        ScriptString_FromCString(&sound, pointer<const char>(0x00816a58));  // "shell_incoming_muffled"
    else
        ScriptString_FromCString(&sound, pointer<const char>(0x00816454));  // "shell_incoming"
    slot<PlayString>(0x009221dc)(ent, &sound, 0, 0, 0);  // GScr_PlaySound
    ScriptString_Release(&sound);
    Scr_Wait(1.5f);
}

void XB_CDECL BroScr_TankShellReadyHud_2(uint32_t ent, uint32_t tank_a, uint32_t tank_b) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    build_shell_hud();
    level_field(0x3b0) = 0;
    level_field(0x204) &= ~1u;
    thread_1(ent, 0x007f5ba0, 0x0015b8d0, ent);  // BroScr_TankShellHudHideOnDeath
    if (entity_handle_is_valid(tank_a) && entity_handle_is_valid(tank_b)) {
        for (;;) {
            const uint32_t owner_a = slot<uint32_t XB_CDECL(uint32_t)>(0x009228a8)(tank_a);  // GScr_Vehicle_GetOwnerHandle
            const uint32_t owner_b = slot<uint32_t XB_CDECL(uint32_t)>(0x009228a8)(tank_b);
            if (entity_handle_is_valid(owner_a) || entity_handle_is_valid(owner_b)) {
                level_field(0x3b0) = 1;
                if (*level_flag(kOnTurret) == 0) {
                    fire_wait_thread(tank_a);
                    fire_wait_thread(tank_b);
                }
            } else {
                level_field(0x3b0) = 0;
                hud_set(kSetAlpha, 0);
                notify_not_on_turret();
                const uint32_t key = kOnTurret;
                *title_function<uint8_t* XB_THISCALL(uint32_t, const uint32_t*)>(0x004611e0)(level() + 0x32c, &key) = 0;  // HashMap_IndexBool
            }
            Scr_Wait(0.35f);
        }
    }
    for (;;) {
        const uint32_t owner = slot<uint32_t XB_CDECL(uint32_t)>(0x009228a8)(ent);  // GScr_Vehicle_GetOwnerHandle
        if (entity_handle_is_valid(owner)) {
            level_field(0x3b0) = 1;
            if (*level_flag(kOnTurret) == 0) fire_wait_thread(ent);
        } else {
            level_field(0x3b0) = 0;
            hud_set(kSetAlpha, 0);
            notify_not_on_turret();
            *level_flag(kOnTurret) = 0;
        }
        Scr_Wait(0.15f);
    }
}

void XB_CDECL BroScr_MinefieldTriggerThink(uint32_t trigger) {
    uint32_t other = 0;
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(trigger, 0xe2a210cf, &other);  // Scr_WaitTillOutputEnt_ORAN("trigger")
        const uint32_t ent = other;
        if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(ent)) continue;  // GScr_IsSentient
        if (*bool_slot(ent, 0x8903ef3c) != 0) continue;
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
        title_function<StartThread>(0x0045a6d0)(0, 0, 0, 0, 0x0015c120, ent, &trigger);  // Scr_StartThread_ent_ent(BroScr_MinefieldKill)
    }
}

void XB_CDECL BroScr_PlayShellIncomingCall(uint32_t ent) { BroScr_PlayShellIncoming(ent, title_global<uint32_t>(0x00922e88)); }

void XB_CDECL BroScr_StoneBreakTriggerThink(uint32_t trigger) {
    {
        ScriptString target;
        const bool none = ScrCall_ScrEnt_GetTarget(&trigger, &target)->rep == nullptr;
        ScriptString_Release(&target);
        if (none) {
            slot<void XB_CDECL(uint32_t)>(0x00922540)(trigger);  // GScr_Delete
            return;
        }
    }
    DynArray stones = {nullptr, 0, 0};
    {
        ScriptString target;
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&trigger, &target))),
                        0x0d786b4c, reinterpret_cast<uint8_t*>(&stones), 0);
        ScriptString_Release(&target);
    }
    uint32_t other = 0;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(trigger, 0xce270885, &other);  // Scr_WaitTillOutputEnt_ORAN("damage")
    uint8_t light = title_global<uint8_t>(0x00922ed0);  // g_scrUndefinedBool
    for (uint32_t i = 1; static_cast<int32_t>(i - 1) < static_cast<int32_t>(stones.size); ++i) {
        light = 0;
        if (!entity_handle_is_valid(element(&stones, i - 1))) continue;
        random_int(100);
        ScriptString noteworthy;
        const bool is_light =
            ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &noteworthy), pointer<const char>(0x0083b07c));  // "light"
        ScriptString_Release(&noteworthy);
        if (is_light) light = 1;
        const uint32_t key = 0xc7458393;
        const uint32_t stone = element(&stones, i - 1);
        const uint32_t sound = title_function<Index>(0x00461120)(level() + 0x57c, &key);  // HashMap_Index
        slot<PlaySound>(0x009221dc)(stone, sound, 0, 0, 0);  // GScr_PlaySound
        uint32_t buffer[3];
        const uint32_t* const o = slot<const uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, other);  // ScrEnt_GetOrigin
        uint32_t origin[3] = {o[0], o[1], o[2]};
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint8_t*);
        title_function<StartThread>(0x0017b5c0)(0, 0, 0, 0, 0x001455e0, element(&stones, i - 1), origin,
                                                &light);  // Scr_StartThread_ent_vec_b_3(BroScr_SpawnStoneGibs)
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(trigger);  // GScr_Delete
    if (stones.data) Scr_Free(stones.data);
}

void XB_CDECL Bro__treeburst_gmi__tree_burst_playBurst(uint32_t tree) {
    using Ent = void XB_CDECL(uint32_t);
    ScriptString model;
    ScriptString_FromRep(&model, nullptr);
    ScriptString default_text;
    ScriptString_FromCString(&default_text, pointer<const char>(0x00825150));  // "^1...Using default tree burst animation..."
    {
        ScriptString got;
        title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(
            &model, ScrCall_ScrEnt_GetModel(&tree, &got));  // ScriptString_Assign
        ScriptString_Release(&got);
    }
    ScriptString no_model;
    {
        ScriptString text;
        ScriptString_FromCString(&text, pointer<const char>(0x008250d8));  // "...model type could not be retrieved fr..."
        ScriptString name;
        ScriptString joined;
        ScriptString_Concat(&joined, &text, ScrCall_ScrEnt_GetTargetname(&tree, &name));
        ScriptString_ConcatCString(&no_model, &joined, pointer<const char>(0x008250b4));  // "!  offset will return ( 0, 0, 0 )."
        ScriptString_Release(&joined);
        ScriptString_Release(&name);
        ScriptString_Release(&text);
    }
    uint32_t offset[3];
    if (model.rep == nullptr) {
        offset[0] = 0;  // 0.0f
        offset[1] = 0;
        offset[2] = 0;
    } else {
        ScriptString arg;
        ScriptString_Copy(&arg, &model);
        uint32_t buffer[3];
        const uint32_t* const v = BroScr_GetLevelVecOrZero(buffer, arg);
        offset[0] = v[0];
        offset[1] = v[1];
        offset[2] = v[2];
    }
    uint32_t anim = 0;
    bool none;
    {
        ScriptString note;
        none = ScrCall_ScrEnt_GetScriptNoteworthy(&tree, &note)->rep == nullptr;
        ScriptString_Release(&note);
    }
    if (none) {
        anim = title_global<uint32_t>(0x008cd3f8);
    } else {
        ScriptString note;
        const bool some = ScrCall_ScrEnt_GetScriptNoteworthy(&tree, &note)->rep != nullptr;
        ScriptString_Release(&note);
        if (some) {
            ScriptString a;
            const bool is_a = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&tree, &a), pointer<const char>(0x00416fcc));  // "a"
            ScriptString_Release(&a);
            if (is_a) {
                anim = title_global<uint32_t>(0x008cd3f8);
            } else {
                ScriptString b;
                const bool is_b = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&tree, &b), pointer<const char>(0x00416fc8));  // "b"
                ScriptString_Release(&b);
                if (is_b) {
                    anim = title_global<uint32_t>(0x008cd404);
                } else {
                    ScriptString c;
                    const bool is_c = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&tree, &c), pointer<const char>(0x00416fc4));  // "c"
                    ScriptString_Release(&c);
                    if (is_c)
                        anim = title_global<uint32_t>(0x008cd410);
                    else
                        anim = title_global<uint32_t>(0x008cd3f8);
                }
            }
        }
    }
    slot<Ent>(0x00922564)(tree);  // GScr_ClearEntFlag400
    ScriptString targetname;
    ScrCall_ScrEnt_GetTargetname(&tree, &targetname);
    ScriptString target;
    ScrCall_ScrEnt_GetTarget(&tree, &target);
    ScriptString key_text;
    ScriptString_Copy(&key_text, title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(
                                     ent_script_object(tree), 0x2a11980e));  // ScriptKeyTable_GetString
    uint32_t hidden;
    {
        ScriptString name;
        using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
        hidden = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&tree, &name), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
        ScriptString_Release(&name);
    }
    slot<Ent>(0x00922568)(hidden);  // GScr_SetEntFlag400
    uint32_t origin_ent;
    {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        uint32_t buffer[3];
        const uint32_t* const o = slot<const uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, tree);  // ScrEnt_GetOrigin
        float at[3];
        at[0] = as_float(o[0]) + as_float(offset[0]);
        at[1] = as_float(o[1]) + as_float(offset[1]);
        at[2] = as_float(o[2]) + as_float(offset[2]);
        origin_ent = slot<uint32_t XB_CDECL(ScriptString*, const float*, uint32_t)>(0x009222f0)(&classname, at, 0);  // GScr_Spawn
        ScriptString_Release(&classname);
    }
    play_text(origin_ent, 0x008250a0);  // "artillery_treeburst"
    play_text(origin_ent, 0x00825090);  // "tree_shatter"
    play_text(origin_ent, 0x00825080);  // "tree_fall_start"
    {
        uint32_t buffer[3];
        const uint32_t* const a = slot<const uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, tree);  // ScrEnt_GetAngles
        ScrVec3 angles;
        angles.v[0] = a[0];
        angles.v[1] = a[1];
        angles.v[2] = a[2];
        uint32_t buffer2[3];
        const uint32_t* const o = slot<const uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer2, tree);  // ScrEnt_GetOrigin
        ScrVec3 origin;
        origin.v[0] = o[0];
        origin.v[1] = o[1];
        origin.v[2] = o[2];
        slot<void XB_CDECL(uint32_t, uint32_t, ScrVec3*, ScrVec3*, uint32_t)>(0x009224c0)(tree, 0xce00786e, &origin, &angles,
                                                                                    anim);  // GScr_AnimScriptedNoNotify
    }
    Scr_Wait(static_cast<float>(
        slot<long double XB_CDECL(float, float)>(0x00922238)(0.5f, 1.0f)));  // Scr_RandomFloatRange
    play_text(origin_ent, 0x00825070);  // "tree_fall_imp"
    slot<Ent>(0x00922540)(origin_ent);  // GScr_Delete
    ScriptString_Release(&key_text);
    ScriptString_Release(&target);
    ScriptString_Release(&targetname);
    ScriptString_Release(&no_model);
    ScriptString_Release(&default_text);
    ScriptString_Release(&model);
}

void XB_CDECL BroScr_WindowBreakOnHit(uint32_t trigger, uint32_t window, uint32_t clip, DynArray origins) {
    using VecOut = void XB_CDECL(float*, const float*);
    using PlayAt = void XB_CDECL(uint32_t, uint32_t, const float*, uint32_t, uint32_t);
    const float lane = as_float(title_global<uint32_t>(0x00922eb8));  // g_scrUndefinedVecLane
    float dir[3] = {lane, lane, lane};
    uint8_t heavy;
    const uint8_t hit = *BroScr_WaitHeavyHitOrDamage(&heavy, trigger, dir);
    const float lane2 = as_float(title_global<uint32_t>(0x00922eb8));
    float back[3] = {lane2, lane2, lane2};
    const float minus_one = title_global<float>(0x0083eb34);  // -1
    if (hit) {
        slot<VecOut>(0x00922280)(back, dir);  // Scr_VectorNormalize
        const float n[3] = {back[0], back[1], back[2]};
        const float five = title_global<float>(0x0083ece8);  // 5
        back[0] = n[0] * minus_one;
        back[1] = n[1] * minus_one;
        back[2] = n[2] * minus_one;
        dir[0] = n[0] * five;
        dir[1] = n[1] * five;
        dir[2] = n[2] * five;
    } else {
        slot<VecOut>(0x00922280)(back, dir);  // Scr_VectorNormalize
        dir[0] = back[0];
        dir[1] = back[1];
        dir[2] = back[2];
        back[0] = dir[0] * minus_one;
        back[1] = dir[1] * minus_one;
        back[2] = dir[2] * minus_one;
    }
    slot<void XB_CDECL(uint32_t)>(0x00922568)(window);  // GScr_SetEntFlag400
    if (entity_handle_is_valid(clip)) slot<void XB_CDECL(uint32_t)>(0x00922564)(clip);  // GScr_ClearEntFlag400
    if (static_cast<int32_t>(origins.size) > 0) {
        const uint32_t key_a = 0x51dd6402;
        const uint32_t key_b = 0x51dd6402;
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < static_cast<int32_t>(origins.size); ++i) {
            uint32_t* ent = &element(&origins, i - 1);
            const float at_a[3] = {dir[0], dir[1], dir[2]};
            const uint32_t sound_a = title_function<Index>(0x00461120)(level() + 0x57c, &key_a);  // HashMap_Index
            slot<PlayAt>(0x009221e0)(*ent, sound_a, at_a, 0, 0);  // GScr_PlaySoundAtPosition
            ent = &element(&origins, i - 1);
            const float at_b[3] = {back[0], back[1], back[2]};
            const uint32_t sound_b = title_function<Index>(0x00461120)(level() + 0x57c, &key_b);
            slot<PlayAt>(0x009221e0)(*ent, sound_b, at_b, 0, 0);
        }
    }
    Scr_Wait(1.0f);
    slot<void XB_CDECL(uint32_t)>(0x00922540)(window);   // GScr_Delete
    slot<void XB_CDECL(uint32_t)>(0x00922540)(trigger);  // GScr_Delete
    if (origins.data) Scr_Free(origins.data);
}

void XB_CDECL BroScr_FxCreationSound(uint32_t ent, uint32_t sound, ScrVec3 origin, ScrVec3 to, float delay) {
    using PlayAt = void XB_CDECL(uint32_t, uint32_t, const uint32_t*, uint32_t, uint32_t);
    Scr_WaitFrames(0);
    if (delay != as_float(undefined_float())) Scr_Wait(delay);
    ScrVec3 dir = to;
    if (vec_defined(&dir)) {
        const float dz = as_float(dir.v[2]) - as_float(origin.v[2]);
        const float dy = as_float(dir.v[1]) - as_float(origin.v[1]);
        const float dx = as_float(dir.v[0]) - as_float(origin.v[0]);
        const float length = norm3(dz, dy, dx);
        const float inverse = title_global<float>(0x0083eb20) / length;  // 1
        dir.v[0] = bits(inverse * dx);
        dir.v[1] = bits(inverse * dy);
        dir.v[2] = bits(inverse * dz);
    }
    if (*pointer<uint32_t>(title_function<Index>(0x00461120)(level() + 0x458, &sound)) != 0) {  // HashMap_Index
        const uint32_t key = sound;
        slot<PlaySound>(0x009221dc)(ent, title_function<Index>(0x00461120)(level() + 0x458, &key), 0, 0, 0);  // GScr_PlaySound
    }
    const bool named = is_fx_creation(ent);
    if (named) {
        if (vec_defined(&dir)) {
            const uint32_t at[3] = {dir.v[0], dir.v[1], dir.v[2]};
            slot<PlayAt>(0x009221e0)(ent, title_function<Index>(0x00461120)(level() + 0x57c, &sound), at, 0x75c3b344,
                                     0);  // GScr_PlaySoundAtPosition
        } else {
            slot<PlaySound>(0x009221dc)(ent, title_function<Index>(0x00461120)(level() + 0x57c, &sound), 0x75c3b344, 0, 0);
        }
        Scr_WaitTill(ent, 0x75c3b344);
        slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
        return;
    }
    if (vec_defined(&dir)) {
        const uint32_t at[3] = {dir.v[0], dir.v[1], dir.v[2]};
        slot<PlayAt>(0x009221e0)(ent, title_function<Index>(0x00461120)(level() + 0x57c, &sound), at, 0, 0);
    } else {
        slot<PlaySound>(0x009221dc)(ent, title_function<Index>(0x00461120)(level() + 0x57c, &sound), 0, 0, 0);
    }
}

void XB_CDECL BroScr_FxCreationSoundThink(uint32_t ent, uint32_t sound, ScrVec3 to, uint32_t ender, float duration,
                                          uint32_t far_sound, float far_dist) {
    using PlayAtHandle = uint32_t XB_CDECL(uint32_t, uint32_t, const uint32_t*, uint32_t, uint32_t);
    using PlayHandle = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using GetVec = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
    Scr_WaitFrames(0);
    float end_time = as_float(undefined_float());
    bool far = false;
    bool first = true;
    uint32_t current = sound;
    uint32_t handle = undefined_int();
    if (ender != undefined_handle()) {
        *reinterpret_cast<uint8_t*>(ScrField_h75c85f71(ent, 0)) = 0;
        uint32_t* const functor = new_functor(0x10);
        if (functor) {
            functor[0] = 0x007f4b00;
            functor[1] = 0x00130870;  // BroScr_FxCreationSoundWait
            functor[2] = ent;
            functor[3] = ender;
        }
        create_thread(ent, functor);
    }
    if (duration != as_float(undefined_float())) {
        const float ms = duration * title_global<float>(0x0083eb9c);  // 1000
        end_time = static_cast<float>(get_time()) + ms;
    }
    if (*pointer<uint32_t>(title_function<Index>(0x00461120)(level() + 0x458, &sound)) != 0) {  // HashMap_Index
        uint32_t* const functor = new_functor(0x14);
        if (functor) {
            functor[0] = 0x00824464;
            functor[1] = 0x0015bd20;  // BroScr_LoopSoundUntilFlag
            functor[2] = ent;
            functor[3] = sound;
            functor[4] = ender;
        }
        create_thread(ent, functor);
    }
    ScrVec3 dir = to;
    if (vec_defined(&dir)) {
        uint32_t buffer[3];
        const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        const uint32_t at[3] = {o[0], o[1], o[2]};
        float delta[3];
        delta[0] = as_float(dir.v[0]) - as_float(at[0]);
        delta[1] = as_float(dir.v[1]) - as_float(at[1]);
        delta[2] = as_float(dir.v[2]) - as_float(at[2]);
        const uint32_t lane = undefined_lane();
        uint32_t normal[3] = {lane, lane, lane};
        slot<void XB_CDECL(uint32_t*, const float*)>(0x00922280)(normal, delta);  // Scr_VectorNormalize
        dir.v[0] = normal[0];
        dir.v[1] = normal[1];
        dir.v[2] = normal[2];
    }
    float far_sq = far_dist;
    if (far_sq != as_float(undefined_float())) far_sq = far_sq * far_sq;
    for (;;) {
        if (end_time != as_float(undefined_float())) {
            const float now = static_cast<float>(get_time());
            if (static_cast<float>(now > end_time ? 1 : 0) != 0.0f) break;
        }
        if (*bool_slot(ent, 0x75c85f71) != 0) break;
        bool is_far = false;
        if (far_sound != undefined_handle()) {
            uint32_t buffer[3];
            const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
            const uint32_t at[3] = {o[0], o[1], o[2]};
            uint32_t buffer2[3];
            const uint32_t* const p = slot<GetVec>(0x00922b20)(buffer2, level_field(0x7f8));  // the player
            const uint32_t player_at[3] = {p[0], p[1], p[2]};
            const long double distance_sq =
                slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092226c)(player_at, at);  // Scr_DistanceSquared
            is_far = static_cast<float>(distance_sq > far_sq ? 1 : 0) != 0.0f;
        }
        bool restart;
        if (is_far) {
            if (!far) {
                far = true;
                current = far_sound;
                restart = true;
            } else {
                restart = first;
            }
        } else if (far) {
            far = false;
            current = sound;
            restart = true;
        } else {
            restart = first;
        }
        if (restart) {
            first = false;
            if (handle != undefined_int()) slot<void XB_CDECL(uint32_t)>(0x009221f4)(handle);  // GScr_FreeSound
            if (vec_defined(&dir)) {
                const uint32_t at[3] = {dir.v[0], dir.v[1], dir.v[2]};
                handle = slot<PlayAtHandle>(0x009221e0)(ent, title_function<Index>(0x00461120)(level() + 0x57c, &current), at,
                                                        0x75c3b344, 1);  // GScr_PlaySoundAtPosition
            } else {
                handle = slot<PlayHandle>(0x009221dc)(ent, title_function<Index>(0x00461120)(level() + 0x57c, &current),
                                                      0x75c3b344, 1, 0);  // GScr_PlaySound
            }
        }
        Scr_Wait(static_cast<float>(slot<long double XB_CDECL(float, float)>(0x00922238)(0.4f, 0.6f)));  // Scr_RandomFloatRange
    }
    *bool_slot(ent, 0x75c85f71) = 1;
    if (handle != undefined_int()) slot<void XB_CDECL(uint32_t)>(0x009221f4)(handle);  // GScr_FreeSound
    if (is_fx_creation(ent)) slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void XB_CDECL BroScr_PlantBombObjective(uint32_t self, uint32_t bomb) {
    using Ent = void XB_CDECL(uint32_t);
    using SetModel = void XB_CDECL(uint32_t, ScriptString*, uint32_t);
    using Set = void XB_CDECL(uint32_t, uint32_t);
    {
        ScriptString target;
        ScrCall_ScrEnt_GetTarget(&bomb, &target);
        ScriptString_Release(&target);
    }
    uint32_t trigger;
    {
        ScriptString target;
        using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
        trigger = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&bomb, &target), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
        ScriptString_Release(&target);
    }
    {
        ScriptString model;
        ScriptString_FromCString(&model, pointer<const char>(0x008252bc));  // "xmodel/bomb_objective_incomplete"
        slot<SetModel>(0x00922548)(bomb, &model, 0);                       // GScr_SetModel
        ScriptString_Release(&model);
    }
    {
        ScriptString hint;
        ScriptString_FromCString(&hint, pointer<const char>(0x0082529c));  // "SCRIPT_HINTSTR_PLANTEXPLOSIVES"
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225b8)(trigger, &hint);  // GScr_ResetHintCache
        ScriptString_Release(&hint);
    }
    Scr_WaitTillString(trigger, pointer<const char>(0x0083aa58));  // "trigger"
    play_text(self, 0x00825288);                                    // "explo_plant_rand"
    {
        ScriptString model;
        ScriptString_FromCString(&model, pointer<const char>(0x0082527c));  // "xmodel/bomb"
        slot<SetModel>(0x00922548)(bomb, &model, 0);                       // GScr_SetModel
        ScriptString_Release(&model);
    }
    if (*pointer<uint8_t>(level() + 0x234) == title_global<uint8_t>(0x00922ed0)) {  // g_scrUndefinedBool
        title_function<void XB_CDECL(const char*)>(0x005b45b0)(pointer<const char>(0x00825260));  // ScrCall_SendGameMessageBoldCStr("SCRIPT_EXPLOSIVESPLANTED")
        *pointer<uint8_t>(level() + 0x234) = 1;
    }
    {
        ScriptString team;
        ScriptString_FromCString(&team, pointer<const char>(0x00832084));  // "allies"
        uint32_t buffer[3];
        const uint32_t* const o = slot<const uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, self);  // ScrEnt_GetOrigin
        const uint32_t origin[3] = {o[0], o[1], o[2]};
        ScriptString* const name = ScrField_script_objective(self);
        using BadPlace = void XB_CDECL(ScriptString*, float, const uint32_t*, float, float, ScriptString*);
        slot<BadPlace>(0x00922484)(name, 6.0f, origin, 400.0f, 400.0f, &team);  // GScr_BadPlace_Cylinder
        ScriptString_Release(&team);
    }
    slot<Ent>(0x00922540)(trigger);  // GScr_Delete
    bool defined;
    if (*ScrField_script_objective_IsDefined(&defined, self)) {
        ScriptString flag;
        ScriptString_ConcatCString(&flag, ScrField_script_objective(self), pointer<const char>(0x00825254));  // "bomb placed"
        ScriptString arg;
        ScriptString_Copy(&arg, &flag);
        BroScr_FlagSet(arg);
        ScriptString_Release(&flag);
    }
    uint32_t handle;
    const uint32_t hud = *slot<uint32_t* XB_CDECL(uint32_t*)>(0x0092248c)(&handle);  // GScr_GetCurrentZoneId (a new hud elem)
    uint32_t stopwatch = hud;
    slot<Set>(0x00922a48)(hud, 0x32);  // ScrHudElem_Set_04 (x 50)
    slot<Set>(0x00922a50)(hud, 0xf0);  // ScrHudElem_Set_08 (y 240)
    slot<Set>(0x00922a58)(hud, 1);     // ScrHudElem_Set_14
    slot<Set>(0x00922a60)(hud, 1);     // ScrHudElem_Set_18
    {
        ScriptString shader;
        ScriptString_FromCString(&shader, pointer<const char>(0x00825244));  // "hudStopwatch"
        using Timer = void XB_CDECL(uint32_t*, float, float, ScriptString*, uint32_t, uint32_t);
        slot<Timer>(0x00922900)(&stopwatch, 5.0f, 60.0f, &shader, 0x40, 0x40);  // GScr_HudSetShaderTimer8
        ScriptString_Release(&shader);
    }
    Scr_Wait(5.0f);
    slot<void XB_CDECL(uint32_t*)>(0x0092291c)(&stopwatch);  // GScr_HudDestroy
    slot<Ent>(0x00922540)(bomb);                              // GScr_Delete
    notify_ent(self, 0x00824310);                             // "explode"
}

void XB_CDECL BroScr_RespawnerThink_2(uint32_t ent, uint32_t respawner, RespawnerAttributes attribs, uint32_t limit,
                                      ScriptString mode) {
    using Count = uint32_t XB_CDECL(uint32_t);
    using SetCount = void XB_CDECL(uint32_t, uint32_t);
    using Spawn = uint32_t XB_CDECL(uint32_t, uint32_t);
    using EntHash = void XB_CDECL(uint32_t, uint32_t);
    using RandomFloat = long double XB_CDECL(float);
    using GetVec = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
    // The spawn zone: the spawner's zone when the level uses zones (+0x188 defined and set), else 0.
    uint32_t zone;
    const uint32_t zones = *pointer<uint32_t>(level() + 0x188);
    if (zones != undefined_int() && zones) {
        uint32_t buffer[3];
        const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        zone = slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x0092249c)(o[0], o[1], o[2]);  // GScr_GetZoneAtPoint
    } else {
        zone = 0;
    }
    bool defined;
    if (!*ScrField_haa3cddde_IsDefined(&defined, ent)) *reinterpret_cast<uint8_t*>(ScrField_haa3cddde(ent, 0)) = 1;  // the first spawn
    *bool_slot(ent, 0x18b0887b) = 0;
    {
        ScriptString group;
        uint32_t hash;
        const uint32_t* const key = Scr_HashScriptString(
            &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetGroupname(&ent, &group)));
        ++*level_map_int(0x4b4, key);  // the group's spawner count
        ScriptString_Release(&group);
    }
    if (entity_handle_is_valid(respawner)) {
        slot<EntHash>(0x00922188)(respawner, 0x0c34930d);  // Scr_EndOn
        *int_slot(respawner, 0x846e35f4) = 0;  // dead count
        *int_slot(respawner, 0xaa7e54b7) = 0;  // spawned count
    }
    slot<EntHash>(0x00922188)(ent, 0x0c34930d);  // Scr_EndOn
    *bool_slot(ent, 0x788065eb) = 1;  // respawning
    if (entity_handle_is_valid(respawner)) {
        if (limit != undefined_int() && !*ScrField_h5f01d656_IsDefined(&defined, respawner)) *ScrField_h5f01d656(respawner) = limit;
    } else if (limit != undefined_int()) {
        *int_slot(ent, 0x5f01d656) = limit;
    }
    if (slot<Count>(0x00922b88)(ent) == undefined_int()) slot<SetCount>(0x00922b8c)(ent, 1);
    if (*ScrField_new_count_IsDefined(&defined, ent)) {
        slot<SetCount>(0x00922b8c)(ent, *int_slot(ent, 0x78063481));
        queue_notify(ent, 0x9767cf68);
        *int_slot(ent, 0x78063481) = title_global<uint32_t>(0x00922ebc);
    }
    if (*ScrField_h6e62eedd_IsDefined(&defined, ent)) {
        Scr_Wait(as_float(*ScrField_h6e62eedd(ent)));
        *ScrField_h6e62eedd(ent) = title_global<uint32_t>(0x00922ea4);
    } else if (!*reinterpret_cast<uint8_t*>(ScrField_haa3cddde(ent, 0))) {
        if (*ScrField_script_delay_IsDefined(&defined, ent)) {
            const float delay = as_float(*ScrField_script_delay(ent));
            const uint32_t* const base = ScrField_script_delay(ent);
            const long double r = slot<RandomFloat>(0x00922230)(delay);  // Scr_RandomFloat
            Scr_Wait(static_cast<float>(as_float(*base) + r));
        } else if (*ScrField_script_delay_min_IsDefined(&defined, ent) &&
                   *ScrField_script_delay_max_IsDefined(&defined, ent)) {
            const float high = as_float(*ScrField_script_delay_max(ent));
            const float low = as_float(*ScrField_script_delay_min(ent));
            const uint32_t* const base = ScrField_script_delay_min(ent);
            const long double r = slot<RandomFloat>(0x00922230)(high - low);  // Scr_RandomFloat
            Scr_Wait(static_cast<float>(as_float(*base) + r));
        }
    }
    BroScr_StopRespawning(ent, respawner);
    if (*bool_slot(ent, 0x788065eb)) {
        uint32_t guy;
        if (*ScrField_h02d62a20_IsDefined(&defined, ent) && *ScrField_h02d62a20(ent))
            guy = slot<Spawn>(0x00922520)(ent, zone);  // GScr_ForceSpawn
        else
            guy = slot<Spawn>(0x00922518)(ent, zone);  // GScr_DoSpawn
        if (entity_handle_is_valid(guy)) {
            *reinterpret_cast<uint8_t*>(ScrField_using_respawner(guy, 0)) = 1;
            *reinterpret_cast<uint8_t*>(ScrField_haa3cddde(ent, 0)) = 0;
            if (!*ScrField_alive_ai_IsDefined(&defined, ent)) *ScrField_alive_ai(ent) = 0;
            ++*ScrField_alive_ai(ent);
            Scr_WaitTill(guy, 0xc0bb2676);  // "finished spawning"
            if (entity_handle_is_valid(respawner)) ++*ScrField_alive_num(respawner);
            {
                ScriptString group;
                ScriptString name;
                ScriptString_ConcatCString(&name, ScrCall_ScrEnt_GetGroupname(&ent, &group), pointer<const char>(0x00824ea4));  // "_ai"
                ScriptString copy;
                ScriptString_Copy(&copy, &name);
                slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(guy, copy);  // ScrEnt_SetTargetname (releases its copy)
                ScriptString_Release(&name);
                ScriptString_Release(&group);
            }
            title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, RespawnerAttributes*)>(0x0017b410)(
                0, 0, 0, 0, 0x00132f40, guy, &attribs);  // Scr_StartThread_ent_RespawnerAttributes(BroScr_RespawnerApplyAttribs)
            bool targeted;
            {
                ScriptString target;
                targeted = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
                ScriptString_Release(&target);
            }
            if (targeted) {
                DynArray nodes = {nullptr, 0, 0};
                {
                    ScriptString key;
                    ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
                    ScriptString target;
                    Scr_GetNodeArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&ent, &target))),
                                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&key)), reinterpret_cast<uint32_t*>(&nodes));
                    ScriptString_Release(&target);
                    ScriptString_Release(&key);
                }
                if (static_cast<int32_t>(nodes.size) > 0) {
                    const uint32_t i = static_cast<uint32_t>(random_int(static_cast<int32_t>(nodes.size)));
                    slot<void XB_CDECL(uint32_t, float)>(0x00922df4)(guy, 32.0f);  // ScrSentient_SetFloat_18
                    const uint32_t* const node =
                        title_function<uint32_t* XB_THISCALL(DynArray*, uint32_t)>(0x0045ceb0)(&nodes, i);  // DynArray_AtInt0
                    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                                     const ScriptString*)>(0x004b0ae0)(
                        0, 0, 0, 0, 0x0013b880, guy, node, &mode);  // Scr_StartThread_ent_pathnode_str_ORAN(BroScr_FollowNodeChain)
                }
                if (nodes.data) Scr_Free(nodes.data);
            }
            title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*)>(0x0045a620)(
                0, 0, 0, 0, 0x00152310, ent, &respawner, &guy);  // Scr_StartThread_ent_ent_ent(BroScr_RespawnerCountDeath)
            *reinterpret_cast<uint8_t*>(ScrField_h18b0887b(ent, 0)) = 0;
            Scr_WaitTill(guy, 0x06730ae8);  // "death"
            --*ScrField_alive_ai(ent);
        }
        ScriptString group;
        uint32_t hash;
        const uint32_t* const key = Scr_HashScriptString(
            &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetGroupname(&ent, &group)));
        --*title_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(0x00460f80)(level() + 0x4b4, key);  // HashMap_Index_14
        ScriptString_Release(&group);
    }
    title_function<void XB_THISCALL(RespawnerAttributes*)>(0x0052b970)(&attribs);  // RespawnerAttributes_Dtor
    ScriptString_Release(&mode);
}

void XB_CDECL BroScr_SpawnerGroupManager_2(uint32_t, ScriptString group, ScriptString name, RespawnerAttributes attribs,
                                           uint32_t limit, float start, ScriptString mode, int32_t max_alive) {
    using EntHash = void XB_CDECL(uint32_t, uint32_t);
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    using StartRespawner = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                             const RespawnerAttributes*, const uint32_t*, const ScriptString*);
    // The group's spawners (groupname == `group`).
    DynArray spawners = {nullptr, 0, 0};
    slot<void XB_CDECL(DynArray*)>(0x009223d8)(&spawners);  // GScr_GetSpawnerArray
    DynArray list = {nullptr, 0, 0};
    for (uint32_t i = 1; static_cast<int32_t>(i - 1) < static_cast<int32_t>(spawners.size); ++i) {
        bool match = false;
        ScriptString first;
        if (ScrCall_ScrEnt_GetGroupname(&element(&spawners, i - 1), &first)->rep) {
            ScriptString again;
            match = ScriptString_EqualsString(ScrCall_ScrEnt_GetGroupname(&element(&spawners, i - 1), &again), &group);
            ScriptString_Release(&again);
        }
        ScriptString_Release(&first);
        if (match) DynArray_PushBack(reinterpret_cast<uint8_t*>(&list), &element(&spawners, i - 1));
    }
    if (start != as_float(undefined_float())) {
        const uint32_t object = level();
        const uint32_t* const handle = pointer<uint32_t>(object ? object + 4 : 0);
        ScriptString text;
        const ScriptStringRep* const rep = ScriptString_FromFloat(&text, start)->rep;
        Scr_WaitTillString(*handle, rep ? reinterpret_cast<const char*>(rep) + 6 : nullptr);
        ScriptString_Release(&text);
    }
    uint32_t manager = 0;
    if (name.rep) {
        {
            ScriptString kind;
            ScriptString_FromCString(&kind, pointer<const char>(0x0083ab88));  // "script_origin"
            const uint32_t origin[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
            manager = slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&kind, origin, 0);  // GScr_Spawn
            ScriptString_Release(&kind);
        }
        {
            ScriptString text;
            ScriptString_ConcatCString(&text, &group, pointer<const char>(0x00824304));  // "_manager"
            ScriptString copy;
            ScriptString_Copy(&copy, &text);
            slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c1c)(manager, copy);  // ScrEnt_SetGroupname (releases its copy)
            ScriptString_Release(&text);
        }
        title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(
            title_function<GetString>(0x00458da0)(ent_script_object(manager), 0xdfe2186c), &group);  // ScriptString_Assign(ScriptKeyTable_GetString)
        *bool_slot(manager, 0x18b0887b) = 0;
        slot<EntHash>(0x00922188)(manager, 0x0c34930d);  // Scr_EndOn
        title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0045c270)(
            title_function<DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x0045cd50)(ent_script_object(manager), 0xc548bcb7),
            &list);  // DynArray_Assign4(ScriptKeyTable_GetArray)
        title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(
            title_function<GetString>(0x00458da0)(ent_script_object(manager), 0x22cb4cdd), &name);  // ScriptString_Assign(ScriptKeyTable_GetString)
        *int_slot(manager, 0xa8ce4655) = list.size;
    }
    for (uint32_t i = 1; static_cast<int32_t>(i - 1) < static_cast<int32_t>(list.size); ++i) {
        *int_slot(element(&list, i - 1), 0xd553f35a) = 0;          // alive_ai
        slot<EntHash>(0x00922188)(element(&list, i - 1), 0x0c34930d);  // Scr_EndOn
    }
    uint32_t hash;
    *level_map_int(0x4b4, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&group))) = 0;
    *level_map_int(0x35c, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&group))) =
        static_cast<uint32_t>(max_alive);
    int32_t max = max_alive;
    for (;;) {
        const int32_t alive = static_cast<int32_t>(
            *level_map_int(0x4b4, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&group))));
        if (alive < max) {
            uint32_t pick = title_global<uint32_t>(0x00922ebc);
            if (pick == undefined_int()) {
                do {
                    const uint32_t r = static_cast<uint32_t>(random_int(static_cast<int32_t>(list.size)));
                    if (*int_slot(element(&list, r), 0xd553f35a) == 0) pick = r;  // alive_ai
                    Scr_Wait(0.05f);
                } while (pick == undefined_int());
            }
            const uint32_t spawner = element(&list, pick);
            title_function<StartRespawner>(0x0017b370)(0, 0, 0, 0, 0x0015c510, spawner, &manager, &attribs, &limit,
                                                       &mode);  // Scr_StartThread_ent_ent_RespawnerAttributes_i_str(BroScr_RespawnerThink_2)
            if (static_cast<int32_t>(*level_map_int(0x35c, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&group)))) !=
                max_alive)
                max_alive = static_cast<int32_t>(
                    *level_map_int(0x35c, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&group))));
            Scr_Wait(0.5f);
            max = max_alive;
        } else {
            Scr_Wait(1.0f);
            if (static_cast<int32_t>(*level_map_int(0x35c, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&group)))) !=
                max) {
                max = static_cast<int32_t>(
                    *level_map_int(0x35c, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&group))));
                max_alive = max;
            }
        }
    }
}

}  // namespace game
