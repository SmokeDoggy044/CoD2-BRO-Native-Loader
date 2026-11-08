#include "bro_scr_mission.h"

#include "bro_scr_common.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "title_data.h"
#include "bro_scr_level.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "levels_twins.h"
#include "bro_scr_actor_threads.h"
#include "bro_scr_ai_anim_threads.h"
#include "bro_scr_exploder.h"
#include "bro_scr_level_threads.h"
#include "bro_scr_thread_misc.h"
#include "bro_scr_trigger_threads.h"
#include "bro_scr_wait_notify.h"
#include "scr_field.h"

namespace game {

namespace {

uint32_t* hash_string(uint32_t* out, const ScriptString* string) {
    return own_function<uint32_t* XB_CDECL(uint32_t*, const ScriptString*)>(&Scr_HashScriptString)(out, string);  // Scr_HashScriptString
}

// The word at +4 of a level object (null object: read through address 0, as the original).
uint32_t object_word(uint32_t object) {
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(field);
}

}  // namespace

void XB_CDECL BroScr_FlagSet(ScriptString name) {
    uint32_t hash;
    const uint32_t* key = hash_string(&hash, &name);
    const uint32_t map = title_global<uint32_t>(0x00922ee0) + 0x32c;
    uint32_t pair[2];  // the found iterator, then the inserted (key, value) pair
    own_function<void XB_THISCALL(uint32_t, uint32_t*, const uint32_t*)>(&HashMap_Find)(map, &pair[0], key);  // HashMap_Find
    uint32_t node = pair[0];
    if (node == *reinterpret_cast<uint32_t*>(map + 8)) {  // end
        pair[0] = *key;
        pair[1] = title_global<uint8_t>(0x00922ed0);  // g_scrUndefinedBool (bytes 1-3 never written: 0)
        uint32_t result[2];
        using Insert = uint32_t* XB_THISCALL(uint32_t, uint32_t*, const uint32_t*);
        node = *own_function<Insert>(&HashMap_Insert_PROLOGUE_2)(map, result, pair);  // HashMap_Insert
    }
    *reinterpret_cast<uint8_t*>(node + 0xc) = 1;
    const uint32_t level = object_word(title_global<uint32_t>(0x00922ee0));
    const uint32_t notify = *hash_string(&hash, &name);
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(level, notify);  // Scr_QueueNotify
    scr_release(&name);
}

uint32_t* XB_CDECL BroScr_GetUseGearCvar(uint32_t* out) {
    *out = scr_slot<uint32_t XB_CDECL(uint32_t)>(0x009222dc)(0x00825ab8);  // GScr_GetCvarInt("g_useGear")
    return out;
}

void XB_CDECL BroScr_FriendlyFireMissionFail() {
    if (scr_slot<uint32_t XB_CDECL(uint32_t)>(0x009222dc)(0x00825b50) == 1) return;  // GScr_GetCvarInt("ff_off")
    using EndOn = void XB_CDECL(uint32_t, uint32_t);
    own_function<EndOn>(&Scr_EndOnString)(title_global<uint32_t>(0x009236dc), 0x00831f60);  // Scr_EndOnString(player, "death")
    own_function<EndOn>(&Scr_EndOnString)(object_word(title_global<uint32_t>(0x00922ef8)), 0x008242e4);  // "mine death"
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(object_word(title_global<uint32_t>(0x00922ef8)),
                                                            0xa264707f);  // Scr_QueueNotify
    uint8_t* const object = reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x00922ef8));
    if (object[0xf0] != 0) return;
    object[0xf0] = 1;
    ScriptString text;
    scr_string_from(&text, 0x00825b38);  // "INGAME_MISSIONFAIL_FF"
    scr_slot<void XB_CDECL(ScriptString*)>(0x00922438)(&text);  // GScr_MissionFailed
    scr_release(&text);
}

void XB_CDECL BroScr_PrecacheModelBySpawn(ScriptString model) {
    uint32_t origin[3] = {0, 0, 0};  // 0.0f
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab98);  // "script_model"
    const uint32_t ent = scr_slot<uint32_t XB_CDECL(ScriptString*, uint32_t*, uint32_t)>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
    scr_release(&classname);
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    ScrVec3 player{{lane, lane, lane}};
    scr_slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922524)(title_global<uint32_t>(0x009236dc), &player);  // GScr_GetOrigin
    scr_slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, player);                                // ScrEnt_SetOrigin
    scr_slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, &model, 0);              // GScr_SetModel
    scr_slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);                                                  // GScr_Delete
    scr_release(&model);
}

namespace {

uint32_t level_entity() {
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    return title_global<uint32_t>(level ? level + 4 : 0);
}

using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);

// Script thread on `ent`: functor {0x007f5ba0, function, ent}.
void start_ent_thread(uint32_t ent, uint32_t function) {
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0xc));  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = ent;
    }
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);
}

// Script thread on the level entity with `ent` as its argument: functor {0x007f4a28, function, level, ent}.
void start_level_thread(uint32_t ent, uint32_t function) {
    const uint32_t level = level_entity();
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0x10));
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = function;
        functor[2] = level;
        functor[3] = ent;
    }
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, level, functor, 0);
}

bool field_defined(uint32_t is_defined, uint32_t ent) {
    bool defined;
    title_function<bool* XB_CDECL(bool*, uint32_t)>(is_defined)(&defined, ent);
    return defined;
}

}  // namespace

void XB_CDECL BroScr_SetupSpawnerTriggers(uint32_t trigger) {
    const uint32_t ent = trigger;
    bool not_wave = true;
    {
        ScriptString name1;
        ScriptString name2;
        bool second = false;
        if (ScrCall_ScrEnt_GetTargetname(&trigger, &name1)->rep != nullptr) {
            second = true;
            not_wave = !ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&trigger, &name2),
                                            reinterpret_cast<const char*>(static_cast<uintptr_t>(0x0081351c)));  // "friendly_wave"
        }
        if (second) scr_release(&name2);
        scr_release(&name1);
    }
    if (not_wave) {
        ScriptString target;
        const bool has_target = ScrCall_ScrEnt_GetTarget(&trigger, &target)->rep != nullptr;
        scr_release(&target);
        if (has_target) {
            // Spawners (spawnflags bit 0) among the targets: BroScr_TriggerSpawnerThink(trigger, &spawner),
            // ending on 0xe2a210cf.
            DynArray targets = {nullptr, 0, 0};
            own_function<uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t)>(&Scr_GetEntArray)(
                ScrCall_ScrEnt_GetTarget(&trigger, &target), 0x0d786b4c, &targets, 0);  // Scr_GetEntArray
            scr_release(&target);
            const auto at = [&targets](uint32_t i) {
                if (i >= targets.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&targets), i + 1, i + 4);
                return targets.data + i;
            };
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
                if (!(scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(*at(i)) & 1)) continue;  // ScrEnt_GetSpawnflags
                using BeginThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
                title_function<BeginThread>(0x0078a980)(0, 0, 0, ent, 0xe2a210cf, own_address(&BroScr_TriggerSpawnerThink), ent, at(i));  // Scr_BeginThread_ent_ent
            }
            if (targets.data) own_function<void XB_CDECL(void*)>(&Scr_Free)(targets.data);  // Scr_Free
        }
    }
    static const uint32_t kFieldThreads[8][2] = {
        {own_address(ScrField_script_increment_IsDefined), own_address(&BroScr_IncrementTriggerThink)},  // script_increment: BroScr_IncrementTriggerThink
        {own_address(ScrField_script_fallback_IsDefined), own_address(&BroScr_FallbackerTriggerThink)},  // script_fallback: BroScr_FallbackerTriggerThink
        {own_address(ScrField_hbe5f9a9f_IsDefined), own_address(&BroScr_Mg42AutoTriggerThink)},  // 0xbe5f9a9f: BroScr_Mg42AutoTriggerThink
        {own_address(ScrField_script_killspawner_IsDefined), own_address(&BroScr_FloodSpawnerDeleteWatch)},  // script_killspawner: BroScr_FloodSpawnerDeleteWatch
        {own_address(ScrField_h83048760_IsDefined), own_address(&BroScr_FriendlyChainOnTrigger)},  // 0x83048760
        {own_address(ScrField_script_hint_IsDefined), own_address(&BroScr_WaitTillTrigger)},  // script_hint
        {own_address(ScrField_script_exploder_IsDefined), own_address(&BroScr_ExploderTriggerThink)},  // script_exploder: BroScr_ExploderTriggerThink
        {own_address(ScrField_ambient_IsDefined), 0x00120e60},  // ambient: BroScr_EmptyFunc_2
    };
    for (const auto& field : kFieldThreads)
        if (field_defined(field[0], ent)) start_ent_thread(ent, field[1]);
    if (field_defined(own_address(ScrField_h7fa1fc35_IsDefined), ent)) start_level_thread(ent, own_address(&BroScr_StartFxOnTrigger));  // 0x7fa1fc35: BroScr_StartFxOnTrigger
    if (field_defined(own_address(ScrField_h0bfd0e20_IsDefined), ent)) start_level_thread(ent, own_address(&BroScr_StopFxOnTrigger));  // 0x0bfd0e20
    {
        ScriptString name;
        const bool named = ScrCall_ScrEnt_GetTargetname(&trigger, &name)->rep != nullptr;
        scr_release(&name);
        if (!named) return;
    }
    const auto name_is = [&trigger](uint32_t text) {
        ScriptString name;
        const bool same = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&trigger, &name),
                                              reinterpret_cast<const char*>(static_cast<uintptr_t>(text)));
        scr_release(&name);
        return same;
    };
    // The original keeps a stack address (its entry ESP - 0x40) in the argument slot on the
    // Scr_StartThreadFunctor1 and wall-hop paths.
    const uint32_t stack_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&trigger)) - 4 - 0x40;
    const auto functor1 = [&trigger, ent, stack_word](uint32_t function) {
        trigger = stack_word;
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        title_function<StartThread>(0x0011a910)(0, 0, 0, 0, function, ent);  // Scr_StartThreadFunctor1
    };
    const auto level_thread = [&trigger](uint32_t function) {
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
        title_function<StartThread>(0x0045a6d0)(0, 0, 0, 0, function, level_entity(), &trigger);  // Scr_StartThread_ent_ent
    };
    if (name_is(0x00824e54)) {  // "flood_spawner"
        start_ent_thread(ent, own_address(&BroScr_FloodSpawnerTriggerThink));  // BroScr_FloodSpawnerTriggerThink
    } else if (name_is(0x00824e44)) {  // "grenade_spawner"
        start_ent_thread(ent, own_address(&BroScr_GrenadeThrowTriggers));  // BroScr_GrenadeThrowTriggers
    } else if (name_is(0x00824e2c)) {  // "grenade_spawner_once"
        functor1(own_address(&BroScr_GrenadeSpawnerOnceTriggerThink));  // BroScr_GrenadeSpawnerOnceTriggerThink
    } else if (name_is(0x0081351c) || name_is(0x00824e18)) {  // "friendly_wave", "friendly_wave_off"
        level_thread(own_address(&BroScr_SetFriendlyWaveTrigger));  // BroScr_SetFriendlyWaveTrigger
    } else if (name_is(0x00824724)) {  // "friendly_chat"
        functor1(own_address(&BroScr_FriendlyChatTriggerThink));  // BroScr_FriendlyChatTriggerThink
    } else if (name_is(0x00824e08)) {  // "axis_touching"
        level_thread(own_address(&BroScr_HalftrackGunnerGrenadeHint));  // BroScr_HalftrackGunnerGrenadeHint
    } else if (name_is(0x00824dfc)) {  // "axis_dead"
        level_thread(own_address(&BroScr_SpawnGuyDiedThink));  // BroScr_SpawnGuyDiedThink
    } else if (name_is(0x00824dec)) {  // "friendly_mg42"
        level_thread(own_address(&BroScr_FriendlyMg42TriggerThink));  // BroScr_FriendlyMg42TriggerThink
    } else if (name_is(0x00824de4)) {  // "context"
        trigger = stack_word;
        own_function<void XB_CDECL(uint32_t)>(&BroScr_SetupWallHopTrigger_entry)(ent);  // BroScr_SetupWallHopTrigger
    }
}

}  // namespace game
