#include "bro_scr_entity_threads.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kDeath = 0x06730ae8;
constexpr uint32_t kKillAnimScript = 0x07b4e7f7;

using EntFn = void XB_CDECL(uint32_t);
using EntWord = void XB_CDECL(uint32_t, uint32_t);
using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using SetVec = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t);  // a vector passed by value
using CallEntEnt = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);

void end_on(uint32_t ent, uint32_t hash) { slot<EntWord>(0x00922188)(ent, hash); }  // Scr_EndOn
void end_on_death(uint32_t ent) { Scr_EndOnString(ent, pointer<const char>(0x00831f60)); }  // "death"
void set_goal_radius(uint32_t ai, uint32_t radius) { slot<EntWord>(0x00922df4)(ai, radius); }  // ScrSentient_SetFloat_18
void set_goal_radius(uint32_t ai, float radius) { slot<void XB_CDECL(uint32_t, float)>(0x00922df4)(ai, radius); }
uint32_t goal_radius_key(uint32_t ai) { return *key_slot(ai, 0xe223c6aa, undefined_float()); }
void delete_ent(uint32_t ent) { slot<EntFn>(0x00922540)(ent); }  // GScr_Delete
ScriptString* string_key(uint32_t ent, uint32_t key) {
    return title_function<GetString>(0x00458da0)(ent_script_object(ent), key);  // ScriptKeyTable_GetString
}
uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject, +4 its handle
    return *pointer<uint32_t>(object ? object + 4 : 0);
}

// The script_delay wait (when defined).
void script_delay_wait(uint32_t ent) {
    bool defined;
    if (*ScrField_script_delay_IsDefined(&defined, ent)) Scr_Wait(as_float(*ScrField_script_delay(ent)));
}
void connect_paths_if_flagged(uint32_t ent) {
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(ent) & 1)  // ScrEnt_GetSpawnflags
        slot<EntFn>(0x00922574)(ent);                            // GScr_ConnectPaths
}
// The entity's target by targetname (0x0d786b4c) when it has a target, else 0.
uint32_t target_entity(uint32_t* ent) {
    ScriptString name;
    const bool has = ScrCall_ScrEnt_GetTarget(ent, &name)->rep != nullptr;
    scr_release(&name);
    if (!has) return 0;
    ScriptString target_name;
    const uint32_t target = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(ent, &target_name), 0x0d786b4c, 0, 0,
                                                     0);  // GScr_GetEntArray
    scr_release(&target_name);
    return target;
}
// script_noteworthy is set and equals `text`.
bool noteworthy_is(uint32_t* ent, uint32_t text) {
    ScriptString first;
    bool is = false;
    if (ScrCall_ScrEnt_GetScriptNoteworthy(ent, &first)->rep) {
        ScriptString second;
        is = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(ent, &second), pointer<const char>(text));
        scr_release(&second);
    }
    scr_release(&first);
    return is;
}

void script_delay_default(uint32_t ent) {
    bool defined;
    if (!*ScrField_script_delay_IsDefined(&defined, ent)) *ScrField_script_delay(ent) = bits(0.0f);
}
// script_delay = script_delay_min + random(script_delay_max) when both are defined.
void script_delay_random(uint32_t ent) {
    bool defined;
    if (*ScrField_script_delay_min_IsDefined(&defined, ent) && *ScrField_script_delay_max_IsDefined(&defined, ent)) {
        const float random = static_cast<float>(
            slot<long double XB_CDECL(uint32_t)>(0x00922230)(*ScrField_script_delay_max(ent)));  // Scr_RandomFloat
        const float delay = as_float(*ScrField_script_delay_min(ent)) + random;
        *ScrField_script_delay(ent) = bits(delay);
    }
}
// The script_delay wait, then the sound at the direction to the target, or on the entity.
void exploder_sound(uint32_t ent) {
    bool defined;
    if (*ScrField_script_delay_IsDefined(&defined, ent)) Scr_Wait(as_float(*ScrField_script_delay(ent)));
    const uint32_t target = target_entity(&ent);
    const uint32_t u = undefined_lane();
    uint32_t direction[3] = {u, u, u};
    if (entity_handle_is_valid(target)) {
        uint32_t buffer[3];
        const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        const uint32_t from[3] = {o[0], o[1], o[2]};
        uint32_t buffer2[3];
        o = slot<GetVec>(0x00922b20)(buffer2, target);
        const uint32_t to[3] = {o[0], o[1], o[2]};
        uint32_t delta[3];
        delta[0] = bits(as_float(to[0]) - as_float(from[0]));
        delta[1] = bits(as_float(to[1]) - as_float(from[1]));
        delta[2] = bits(as_float(to[2]) - as_float(from[2]));
        uint32_t normal[3] = {u, u, u};
        slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922280)(normal, delta);  // Scr_VectorNormalize
        direction[0] = normal[0];
        direction[1] = normal[1];
        direction[2] = normal[2];
    }
    const float undefined = as_float(u);
    if (as_float(direction[0]) == undefined && as_float(direction[1]) == undefined &&
        as_float(direction[2]) == undefined) {
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(
            ent, string_key(ent, 0xec02fc10), 0, 0, 0);  // GScr_PlaySound
        return;
    }
    ScriptString* const sound = string_key(ent, 0xec02fc10);
    uint32_t position[3] = {direction[0], direction[1], direction[2]};
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t*, uint32_t, uint32_t)>(0x009221e0)(ent, sound, position, 0,
                                                                                           0);  // GScr_PlaySoundAtPosition
}

// A script_origin at the world origin, linked to `ent` (at `tag` when set), deleted with it.
uint32_t spawn_linked_origin(uint32_t ent, const ScriptString* tag) {
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab88);  // "script_origin"
    uint32_t zero[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
    const uint32_t origin = slot<uint32_t XB_CDECL(ScriptString*, uint32_t*, uint32_t)>(0x009222f0)(&classname, zero,
                                                                                                   0);  // GScr_Spawn
    scr_release(&classname);
    end_on_death(origin);
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = 0x0018c130;  // BroScr_DeleteOnOtherDeath
        functor[2] = ent;
        functor[3] = origin;
    }
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
        0, 0, 0, ent, functor, 0);  // Scr_CreateThread
    if (tag->rep) {
        uint32_t offset[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        uint32_t angles[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        slot<void XB_CDECL(uint32_t, uint32_t, const ScriptString*, uint32_t*, uint32_t*, uint32_t)>(0x009224f4)(
            origin, ent, tag, offset, angles, 0);  // GScr_LinkToTagWithOffset
    } else {
        uint32_t buffer[3];
        const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        slot<SetVec>(0x00922b24)(origin, o[0], o[1], o[2]);        // ScrEnt_SetOrigin
        uint32_t buffer2[3];
        const uint32_t* a = slot<GetVec>(0x00922ba0)(buffer2, ent);  // ScrEnt_GetAngles
        slot<SetVec>(0x00922ba4)(origin, a[0], a[1], a[2]);         // ScrEnt_SetAngles
        slot<EntWord>(0x009224fc)(origin, ent);                     // GScr_LinkTo
    }
    return origin;
}

void use_30cal_tree(uint32_t turret) {
    ScriptString tree;
    scr_string_from(&tree, 0x00825de0);  // "30cal"
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(turret, &tree);  // GScr_UseAnimTree
    scr_release(&tree);
}
// The last user: the key removed, the anim tree cleared.
void clear_30cal_tree(uint32_t turret) {
    title_function<void XB_THISCALL(const uint32_t*, uint32_t)>(0x0011eb30)(&turret, 0xabc1df0a);  // key removed
    slot<EntFn>(0x009225f4)(turret);  // GScr_ClearAnimTree
}

}  // namespace

void XB_CDECL BroScr_RestoreGoalRadiusOnGoal(uint32_t ai) {
    end_on_death(ai);
    Scr_WaitTill(ai, 0x0032304c);
    set_goal_radius(ai, goal_radius_key(ai));
    const float radius = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922df0)(ai));  // ScrSentient_GetFloat_18
    const int32_t small = title_global<float>(0x0083eeac) > radius ? 1 : 0;  // 32
    if (static_cast<float>(small) != title_global<float>(0x0083eafc)) set_goal_radius(ai, 400.0f);
}

void XB_CDECL BroScr_LeaveTurretToNode(uint32_t ai) {
    end_on_death(ai);
    const uint32_t turret = *key_slot(ai, 0xaf6fd78f, 0);
    *key_slot(ai, 0xaf6fd78f, 0) = title_global<uint32_t>(0x00922e84);
    slot<EntFn>(0x00922730)(ai);                   // GScr_ActorLeaveTurret
    slot<EntWord>(0x00922d84)(ai, 0);              // ScrActor_SetByte_1374
    set_goal_radius(ai, goal_radius_key(ai));
    if (!entity_handle_is_valid(turret)) return;
    ScriptString target;
    const bool none = ScrCall_ScrEnt_GetTarget(&ai, &target)->rep == nullptr;
    scr_release(&target);
    if (none) return;
    ScriptString key;
    scr_string_from(&key, 0x0082dda8);  // "targetname"
    ScriptString target_name;
    uint32_t node = slot<uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t)>(0x009221cc)(
        ScrCall_ScrEnt_GetTarget(&ai, &target_name), &key, 0, 0);  // GScr_GetNode
    if (node == 0xffffffff) node = 0;
    scr_release(&target_name);
    scr_release(&key);
    slot<long double XB_CDECL(uint32_t)>(0x00922df0)(ai);  // ScrSentient_GetFloat_18 (unused)
    set_goal_radius(ai, 8.0f);
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ai, &node);  // GScr_SetGoalPosFromNode
    Scr_Wait(2.0f);
    set_goal_radius(ai, 384.0f);
}

void XB_CDECL BroScr_DecrementAiSizeOnDeath(uint32_t group, uint32_t ai) {
    Scr_WaitTill(ai, kDeath);
    uint32_t* const count = int_slot(group, 0x97c82b3d);
    *count = *count - 1;
    if (static_cast<int32_t>(*int_slot(group, 0x97c82b3d)) <= 0) queue_notify(group, 0xddc0d6b6);
}

void XB_CDECL BroScr_MagicBulletShieldWait(uint32_t ent, uint32_t health) {
    end_on_death(ent);
    Scr_WaitTillString(ent, pointer<const char>(0x007f64f4));  // "stop magic bullet shield"
    *reinterpret_cast<uint8_t*>(key_slot(ent, 0x83ccaf2a, title_global<uint8_t>(0x00922ed0))) = 0;
    slot<EntWord>(0x0092255c)(ent, 1);       // GScr_SetTakeDamage
    slot<EntWord>(0x00922b94)(ent, health);  // ScrEnt_SetHealth
}

void XB_CDECL BroScr_MagicBulletShield(uint32_t ent, uint32_t health, float time, uint32_t restore_health,
                                       uint32_t scale) {
    using GetInt = uint32_t XB_CDECL(uint32_t);
    Scr_EndOnString(ent, pointer<const char>(0x007f64f4));  // "stop magic bullet shield"
    end_on_death(ent);
    const uint32_t multiplier = scale == undefined_int() ? 1 : scale;
    uint32_t restore = restore_health;
    if (restore == undefined_int()) restore = slot<GetInt>(0x00922b90)(ent);  // ScrEnt_GetHealth
    title_function<CallEntEnt>(0x0045a680)(0, 0, 0, 0, 0x0019bb60, ent,
                                           &restore);  // Scr_StartThread_ent_i_PROLOGUE(BroScr_MagicBulletShieldWait)
    *reinterpret_cast<uint8_t*>(key_slot(ent, 0x83ccaf2a, title_global<uint8_t>(0x00922ed0))) = 1;
    float wait = time;
    if (wait == title_global<float>(0x00922eac)) wait = title_global<float>(0x0083ece8);  // 5
    const uint32_t shield = health == undefined_int() ? 100000000u : health;
    for (;;) {
        slot<EntWord>(0x00922b94)(ent, shield);                                          // ScrEnt_SetHealth
        slot<EntWord>(0x00922c34)(ent, slot<GetInt>(0x00922b90)(ent) * multiplier);      // ScrEnt_SetMaxhealth
        Scr_WaitTill(ent, 0x0039894e);
        slot<EntWord>(0x00922b94)(ent, shield);
        slot<EntWord>(0x00922c34)(ent, slot<GetInt>(0x00922b90)(ent) * multiplier);
        slot<EntWord>(0x00922e0c)(ent, 1);  // ScrSentient_SetInt_30
        if (!slot<GetInt>(0x00922c50)(ent)) {  // ScrEnt_GetTakedamage
            Scr_Wait(wait);
            slot<EntWord>(0x00922e0c)(ent, 0);
            continue;
        }
        slot<EntWord>(0x0092255c)(ent, 0);  // GScr_SetTakeDamage
        Scr_Wait(wait);
        slot<EntWord>(0x0092255c)(ent, 1);
        slot<EntWord>(0x00922e0c)(ent, 0);
    }
}

void XB_CDECL BroScr_ExploderPlaySound(uint32_t ent) {
    script_delay_wait(ent);
    const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
    const uint32_t* const level = object ? pointer<uint32_t>(object + 4) : nullptr;
    uint32_t buffer[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    uint32_t origin[3] = {o[0], o[1], o[2]};
    ScriptString* const sound = string_key(ent, 0x81047339);
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*,
                                     uint32_t*)>(0x0017b150)(0, 0, 0, 0, 0x001823a0, *level, sound,
                                                             origin);  // Scr_StartThreadFunctorN(BroScr_PlaySoundOnTempOrigin_4)
}

void XB_CDECL BroScr_ExploderSoundFx(uint32_t, uint32_t ent) {
    script_delay_default(ent);
    script_delay_random(ent);
    exploder_sound(ent);
}

void XB_CDECL BroScr_ExploderSoundFx_2(uint32_t, uint32_t ent) {
    script_delay_default(ent);
    bool defined;
    if (*ScrField_script_waittill_IsDefined(&defined, ent)) {
        const uint32_t level = level_handle();
        uint32_t hash;
        end_on(level, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(string_key(ent, 0x8035694d))));
    }
    script_delay_random(ent);
    exploder_sound(ent);
}

void XB_CDECL BroScr_ScriptDelayConnectPaths(uint32_t ent) {
    script_delay_wait(ent);
    connect_paths_if_flagged(ent);
    delete_ent(ent);
}

void XB_CDECL BroScr_NotSolidAfterDelay(uint32_t ent) {
    script_delay_wait(ent);
    slot<EntFn>(0x00922564)(ent);  // GScr_ClearEntFlag400
    if (noteworthy_is(&ent, 0x008246f0))  // "notsolid"
        slot<EntFn>(0x009227c0)(ent);     // GScr_MoverNotSolid
    else
        slot<EntFn>(0x009227bc)(ent);  // GScr_MoverSolid
    connect_paths_if_flagged(ent);
}

void XB_CDECL BroScr_NotSolidThrow(uint32_t ent) {
    script_delay_wait(ent);
    const uint32_t target = target_entity(&ent);
    if (!entity_handle_is_valid(target)) {
        delete_ent(ent);
        return;
    }
    if (noteworthy_is(&ent, 0x00825db8)) slot<EntFn>(0x009227c0)(ent);  // "notsolid_throw": GScr_MoverNotSolid
    slot<EntFn>(0x00922564)(ent);                                       // GScr_ClearEntFlag400
    uint32_t buffer[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, target);  // ScrEnt_GetOrigin
    const uint32_t to[3] = {o[0], o[1], o[2]};
    uint32_t buffer2[3];
    o = slot<GetVec>(0x00922b20)(buffer2, ent);
    const uint32_t from[3] = {o[0], o[1], o[2]};
    uint32_t velocity[3];
    velocity[0] = bits(as_float(to[0]) - as_float(from[0]));
    velocity[1] = bits(as_float(to[1]) - as_float(from[1]));
    velocity[2] = bits(as_float(to[2]) - as_float(from[2]));
    slot<void XB_CDECL(uint32_t, uint32_t*, float, uint32_t, uint32_t)>(0x009227b8)(ent, velocity, 12.0f, 0,
                                                                                    0);  // GScr_RotateVelocity
    slot<void XB_CDECL(uint32_t, uint32_t*, float)>(0x009227a4)(ent, velocity, 12.0f);  // GScr_MoverMoveGravity
    delete_ent(target);
    Scr_Wait(6.0f);
    delete_ent(ent);
}

void XB_CDECL BroScr_PlaySoundOnTag(uint32_t ent, ScriptString alias, ScriptString tag) {
    using IsFn = uint32_t XB_CDECL(uint32_t);
    if (!slot<IsFn>(0x00922220)(ent) || slot<IsFn>(0x00922210)(ent)) {  // GScr_IsSentient, GScr_IsAlive
        const uint32_t origin = spawn_linked_origin(ent, &tag);
        Scr_WaitTillString(origin, pointer<const char>(0x00825dc8));  // "sounddone"
        delete_ent(origin);
    }
    scr_release(&alias);
    scr_release(&tag);
}

void XB_CDECL BroScr_PlayLoopSoundOnTag(uint32_t ent, ScriptString alias, ScriptString tag) {
    const uint32_t origin = spawn_linked_origin(ent, &tag);
    ScriptString stop;
    scr_string_from(&stop, 0x00825dd4);  // "stop sound"
    ScriptString notify;
    const ScriptStringRep* const rep = ScriptString_Concat(&notify, &stop, &alias)->rep;
    Scr_WaitTillString(ent, rep ? rep->text : nullptr);
    scr_release(&notify);
    scr_release(&stop);
    delete_ent(origin);
    scr_release(&alias);
    scr_release(&tag);
}

uint8_t* XB_CDECL BroScr_WaitFinishedSpawning(uint8_t* out, uint32_t ent) {
    using IsAlive = uint32_t XB_CDECL(uint32_t);
    if (!slot<IsAlive>(0x00922210)(ent)) {  // GScr_IsAlive
        *out = 1;
        return out;
    }
    bool defined;
    if (!*ScrField_finished_spawning_IsDefined(&defined, ent))
        Scr_WaitTillString(ent, pointer<const char>(0x007f6000));  // "finished spawning"
    *out = slot<IsAlive>(0x00922210)(ent) ? 0 : 1;
    return out;
}

void XB_CDECL BroScr_TurretPlayAnimDone(uint32_t ent, uint32_t turret) {
    end_on(ent, kKillAnimScript);
    title_function<CallEntEnt>(0x004b09e0)(0, 0, 0, 0, 0x001827d0, ent,
                                           &turret);  // Scr_CallThread_ent_ent(BroScr_TurretSetAnimKnob_8cf684)
    slot<EntWord>(0x00922748)(ent, title_global<uint32_t>(0x008cf0d8));  // ScrActor_SetInt_1370
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, float, uint32_t)>(0x009225e0)(
        ent, 0x54e3b2b6, title_global<uint32_t>(0x008cf0d8), 1.0f, 0.15f, 0.0f, 1);  // GScr_SetFlaggedAnimKnob
    Scr_WaitTill(ent, 0x54e3b2b6);
}

void XB_CDECL BroScr_Mg30calAnimTree(uint32_t turret) {
    use_30cal_tree(turret);
    bool defined;
    if (*ScrField_habc1df0a_IsDefined(&defined, turret))
        ++*int_slot(turret, 0xabc1df0a);
    else
        *int_slot(turret, 0xabc1df0a) = 0;
    Scr_WaitTill(turret, kKillAnimScript);
    if (*int_slot(turret, 0xabc1df0a) != 0)
        --*int_slot(turret, 0xabc1df0a);
    else
        clear_30cal_tree(turret);
}

void XB_CDECL BroScr_Mg30calAnimTree_2(uint32_t turret) {
    use_30cal_tree(turret);
    bool defined;
    if (*ScrField_habc1df0a_IsDefined(&defined, turret)) {
        const uint32_t users = *int_slot(turret, 0xabc1df0a) + 1;
        *int_slot(turret, 0xabc1df0a) = users;
    } else {
        *int_slot(turret, 0xabc1df0a) = 0;
    }
    Scr_WaitTill(turret, kKillAnimScript);
    if (*int_slot(turret, 0xabc1df0a) != 0) {
        const uint32_t users = *int_slot(turret, 0xabc1df0a);
        *int_slot(turret, 0xabc1df0a) = users - 1;
    } else {
        clear_30cal_tree(turret);
    }
}

void XB_CDECL BroScr_ReloadLauncherTimeout(uint32_t ent) {
    end_on(ent, 0x5e12d3d2);
    end_on(ent, kKillAnimScript);
    Scr_Wait(3.0f);
    slot<uint32_t XB_CDECL(uint32_t)>(0x00922640)(ent);  // GScr_GetEntityNumber (unused)
    *int_slot(ent, 0xc786fa48) = 0;
    queue_notify(ent, 0x5e12d3d2);
}

void XB_CDECL BroScr_OrientToEntityKillWatch(uint32_t ent, uint32_t other) {
    title_function<CallEntEnt>(0x004b09e0)(0, 0, 0, 0, 0x001828d0, ent,
                                           &other);  // Scr_CallThread_ent_ent(BroScr_RelayNotifyWhileBothAlive)
    end_on(ent, kDeath);
    end_on(other, kDeath);
    end_on(ent, 0x1e4aecac);
    Scr_WaitTill(ent, kKillAnimScript);
    queue_notify(ent, 0x1e4aecac);
}

void XB_CDECL BroScr_CoverShootOnNotify(uint32_t ent, uint32_t end, int32_t count, uint32_t target) {
    ScriptString text;
    scr_string_from(&text, 0x00825e90);  // "doh!" (built and dropped)
    scr_release(&text);
    end_on(ent, end);
    end_on(ent, kKillAnimScript);
    end_on(ent, kDeath);
    if (count > 0) {
        int32_t left = count;
        do {
            Scr_WaitTillArgs(ent, 0x54e3b2b6, 0x0032aa25, 0, 0);
            slot<EntWord>(0x0092269c)(ent, target);  // GScr_ActorShoot
            const uint32_t shots = *int_slot(ent, 0x0feddbea);
            *int_slot(ent, 0x0feddbea) = shots - 1;
        } while (--left != 0);
    }
    queue_notify(ent, end);
}

void XB_CDECL BroScr_AnimFaceWaitSetFlag(uint32_t ent) {
    end_on(ent, kDeath);
    end_on(ent, 0xad9b8a7b);
    Scr_WaitTill(ent, 0x91935ca7);
    *int_slot(ent, 0xaa35a0b5) = 1;
    queue_notify(ent, 0x1c87198d);
}

void XB_CDECL BroScr_AnimFaceWaitOnNotify(uint32_t ent, ScriptString notify, uint32_t done, uint32_t end) {
    end_on(ent, kDeath);
    end_on(ent, end);
    uint32_t hash;
    Scr_WaitTill(ent, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&notify)));
    ScriptString_AssignCString(string_key(ent, 0x1cc76848), pointer<const char>(0x00825ea8));  // "notify"
    queue_notify(ent, done);
    scr_release(&notify);
}

void XB_CDECL BroScr_AnimFaceWaitOnTime(uint32_t ent, float time, uint32_t done, uint32_t end) {
    end_on(ent, kDeath);
    end_on(ent, end);
    Scr_Wait(time);
    ScriptString_AssignCString(string_key(ent, 0x1cc76848), pointer<const char>(0x00839ef8));  // "time"
    queue_notify(ent, done);
}

}  // namespace game
