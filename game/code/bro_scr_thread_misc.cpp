#include "bro_scr_thread_misc.h"

#include <cstring>

#include "bro_scr_exploder.h"
#include "bro_scr_wait_misc.h"
#include "g_scr_runtime.h"
#include "scr_field.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A script-interface slot (pScr_*: a word in the import block at 0x00922xxx) called as F.
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

constexpr uint32_t kDeath = 0x06730ae8;

using Ent = void XB_CDECL(uint32_t);
using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);

// The level entity's handle: the word at +4 of the level object (0x00922ee0), read through null.
uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(field));
}

ScriptKeyTable* script_object_or_empty(uint32_t ent) {
    const uint32_t object = title_function<uint32_t XB_CDECL(uint32_t)>(0x0011db20)(ent);  // GScr_GetEntScriptObject
    return object ? pointer<ScriptKeyTable>(object) : &title_global<ScriptKeyTable>(0x00922e94);
}

}  // namespace

void XB_CDECL BroScr_AttachDroneGun(uint32_t, uint32_t ent, ScriptString model, ScrVec3 velocity) {
    ScriptString tag;
    ScriptString_FromCString(&tag, pointer<const char>(0x00831e14));  // "tag_weapon_right"
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    ScriptString* const held = title_function<GetString>(0x00458da0)(script_object_or_empty(ent), 0x1a7e7545);  // ScriptKeyTable_GetString
    slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x009224d8)(ent, held, &tag);  // GScr_Attach
    ScriptString_Release(&tag);

    ScriptString_FromCString(&tag, pointer<const char>(0x00831e14));
    ScriptString classname;
    ScriptString_FromCString(&classname, pointer<const char>(0x0083ab98));  // "script_model"
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t at[3] = {lane, lane, lane};
    slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x0092261c)(ent, at, &tag);  // GScr_GetTagOrigin
    const uint32_t origin[3] = {at[0], at[1], at[2]};
    const uint32_t gun = slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
    ScriptString_Release(&classname);
    ScriptString_Release(&tag);
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(gun, &model, 0);  // GScr_SetModel
    slot<void XB_CDECL(uint32_t, const ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x009227b8)(gun, &velocity, 0x40a00000, 0,
                                                                                           0);  // GScr_RotateVelocity 5 s
    slot<void XB_CDECL(uint32_t, const ScrVec3*, uint32_t)>(0x009227a4)(gun, &velocity, 0x40a00000);  // GScr_MoverMoveGravity 5 s
    Scr_Wait(as_float(0x40c00000));  // 6
    slot<Ent>(0x00922540)(gun);      // GScr_Delete
    ScriptString_Release(&model);
}

void XB_CDECL BroScr_FlakFireAndReload(uint32_t ent) {
    for (;;) {
        ScriptKeyTable* const table = script_object_or_empty(ent);
        uint32_t* firing = ScriptKeyTable_Find(table, 0xa027aabf);
        if (!firing) firing = ScriptKeyTable_Set(table, 0xa027aabf, title_global<uint32_t>(0x00922ec0));  // g_scrUndefinedInt
        if (*firing != 1) return;
        Scr_WaitTillString(ent, pointer<const char>(0x0083aa04));  // "turret_fire"
        slot<Ent>(0x009228d4)(ent);                                // GScr_Vehicle_FireWeapon
        Scr_Wait(as_float(0x3f800000));                            // 1
        ScriptString sound;
        ScriptString_FromCString(&sound, pointer<const char>(0x007ff3dc));  // "flak_reload"
        slot<PlaySound>(0x009221dc)(ent, &sound, 0, 0, 0);                  // GScr_PlaySound
        ScriptString_Release(&sound);
    }
}

void XB_CDECL BroScr_FxCreationSoundWait(uint32_t ent, uint32_t notify) {
    Scr_WaitTill(level_handle(), notify);
    ScriptKeyTable* const table = script_object_or_empty(ent);
    uint32_t* done = ScriptKeyTable_Find(table, 0x75c85f71);
    if (!done)
        done = ScriptKeyTable_Set(table, 0x75c85f71,
                                  (notify & 0xffffff00u) | title_global<uint8_t>(0x00922ed0));  // g_scrUndefinedBool
    *reinterpret_cast<uint8_t*>(done) = 1;
}

void XB_CDECL BroScr_AiSpawnInit(uint32_t ent) {
    bool defined;
    if (*ScrField_script_health_IsDefined(&defined, ent)) BroScr_AiMaybeDropHealth(ent, *ScrField_script_health(ent));
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(ent) & 1) {  // ScrEnt_GetSpawnflags
        const uint32_t level = level_handle();
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a28;  // a two-entity functor
            functor[1] = 0x001e25d0;  // BroScr_SpawnedThink
            functor[2] = level;
            functor[3] = ent;
        }
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, level, functor,
                                                                                                   0);  // Scr_CreateThread
        return;
    }
    ScriptString none;
    ScriptString_Copy(&none, &title_global<ScriptString>(0x00922e90));
    title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString)>(0x001e1f60)(level_handle(), ent,
                                                                               none);  // BroScr_SpawnerFinishSpawn
    using StartThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*, uint32_t,
                                      uint32_t);
    title_function<StartThread>(0x0052c6d0)(0, 0, 0, 0, 0x001a7010, level_handle(), &ent, 0x00922ebc,
                                            0x00922e90);  // Scr_StartThread_ent_ent_i_str (BroScr_SetupFriendlyFireMissionFail)
}

void XB_CDECL BroScr_TriggerSpawnerThink(uint32_t trigger, uint32_t spawner) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(spawner, kDeath);  // Scr_EndOn
    using Count = int32_t XB_CDECL(uint32_t);
    if (!slot<Count>(0x00922b88)(spawner)) return;  // the spawner's count left
    {
        ScriptString name, target;
        const ScriptString* const own = ScrCall_ScrEnt_GetTargetname(&spawner, &name);
        const bool differ = !ScriptString_EqualsString(ScrCall_ScrEnt_GetTarget(&trigger, &target), own);
        ScriptString_Release(&target);
        ScriptString_Release(&name);
        if (differ) return;
    }
    if (*ScrField_h02d62a20(spawner) == 1)
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922520)(spawner, 0);  // GScr_ForceSpawn
    else
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922518)(spawner, 0);  // GScr_DoSpawn
    using GetWait = long double XB_CDECL(uint32_t);
    // The script's comparison goes through a float truth value (FCOMIP of the x87 result).
    if (static_cast<float>(slot<GetWait>(0x00922b78)(trigger) > platform_x87_ld(as_float(title_global<uint32_t>(0x0083eafc))) ? 1 : 0) !=
        as_float(title_global<uint32_t>(0x0083eafc)))
        Scr_Wait(static_cast<float>(slot<GetWait>(0x00922b78)(trigger)));  // ScrEnt_GetWait
    if (slot<Count>(0x00922b88)(spawner)) {
        using BeginThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
        title_function<BeginThread>(0x0078a980)(0, 0, 0, trigger, 0xe2a210cf, 0x00130c80, trigger,
                                                &spawner);  // Scr_BeginThread_ent_ent (BroScr_TriggerSpawnerThink)
    }
}

void XB_CDECL BroScr_WaitForNotifyOrTimeoutCall(uint32_t a, ScrVec3 v1, ScriptString s, ScrVec3 v2, ScriptString notify) {
    // The arguments are built in place, last first.
    const float timeout = as_float(title_global<uint32_t>(0x00922eac));  // g_scrUndefinedFloat
    ScriptString endon, notify_copy, s_copy;
    ScriptString_FromRep(&endon, nullptr);
    ScriptString_Copy(&notify_copy, &notify);
    ScriptString_Copy(&s_copy, &s);
    BroScr_WaitForNotifyOrTimeout(a, v1, s_copy, v2, notify_copy, endon, timeout);
    ScriptString_Release(&s);
    ScriptString_Release(&notify);
}

void XB_CDECL BroScr_WaitForNotifyOrTimeoutCall_2(uint32_t a, ScrVec3 v1, ScriptString s, ScrVec3 v2) {
    const float timeout = as_float(title_global<uint32_t>(0x00922eac));
    ScriptString endon, notify, s_copy;
    ScriptString_FromRep(&endon, nullptr);
    ScriptString_FromRep(&notify, nullptr);
    ScriptString_Copy(&s_copy, &s);
    BroScr_WaitForNotifyOrTimeout(a, v1, s_copy, v2, notify, endon, timeout);
    ScriptString_Release(&s);
}

void XB_CDECL BroScr_WaitForNotifyOrTimeoutCall_3(uint32_t a, ScrVec3 v1, ScriptString s) {
    const float timeout = as_float(title_global<uint32_t>(0x00922eac));
    ScriptString endon, notify, s_copy;
    ScriptString_FromRep(&endon, nullptr);
    ScriptString_FromRep(&notify, nullptr);
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    ScrVec3 v2;
    v2.v[0] = lane;
    v2.v[1] = lane;
    v2.v[2] = lane;
    ScriptString_Copy(&s_copy, &s);
    BroScr_WaitForNotifyOrTimeout(a, v1, s_copy, v2, notify, endon, timeout);
    ScriptString_Release(&s);
}

void XB_CDECL BroScr_WaitForNotifyOrTimeoutCall_4(uint32_t a, ScrVec3 v1, ScriptString s, ScrVec3 v2, float timeout) {
    ScriptString endon, notify, s_copy;
    ScriptString_FromRep(&endon, nullptr);
    ScriptString_FromRep(&notify, nullptr);
    ScriptString_Copy(&s_copy, &s);
    BroScr_WaitForNotifyOrTimeout(a, v1, s_copy, v2, notify, endon, timeout);
    ScriptString_Release(&s);
}

}  // namespace game
