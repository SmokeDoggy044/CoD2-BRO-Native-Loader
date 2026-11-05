#include "bro_scr_level_threads_2.h"

#include <cstring>

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_thread_misc.h"
#include "bro_scr_level_threads.h"
#include "bro_scr_util.h"
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

}  // namespace game
