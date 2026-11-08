#include "levels_prologue_bodies_a.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_battle_threads.h"
#include "bro_scr_nodes_spawner.h"
#include "bro_scr_wait_misc.h"
#include "g_scr_runtime.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t level() { return title_global<uint32_t>(0x00921cbc); }
// The level's own entity: the word at +4 of the level block (a null block reads address 0).
uint32_t level_ent() {
    const uint32_t block = level();
    return *pointer<uint32_t>(block ? block + 4 : 0);
}
uint32_t& level_word(uint32_t offset) { return *pointer<uint32_t>(level() + offset); }

ScriptString* from_text(ScriptString* out, uint32_t text) {
    ScriptString_FromCString(out, pointer<const char>(text));
    return out;
}
ScriptString copy_string(const ScriptString* source) {
    ScriptString copy;
    ScriptString_Copy(&copy, source);
    return copy;
}
const ScriptString* empty_string() { return &title_global<ScriptString>(0x00922e90); }
const char* text_of(const ScriptString* s) { return s->rep ? s->rep->text : nullptr; }

void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn
void notify(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, hash); }  // Scr_QueueNotify
void delete_ent(uint32_t ent) { slot<void XB_CDECL(uint32_t)>(0x00922540)(ent); }                           // GScr_Delete
using GetEntArray = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t get_ent(ScriptString* value, uint32_t key) { return slot<GetEntArray>(0x009221c4)(value, key, 0, 0, 0); }  // GScr_GetEntArray
using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
ScrVec3 ent_vec(uint32_t slot_address, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* const v = slot<GetVec>(slot_address)(buffer, ent);
    const ScrVec3 copy = {{v[0], v[1], v[2]}};
    return copy;
}
ScrVec3 origin_of(uint32_t ent) { return ent_vec(0x00922b20, ent); }  // ScrEnt_GetOrigin
ScrVec3 angles_of(uint32_t ent) { return ent_vec(0x00922ba0, ent); }  // ScrEnt_GetAngles
void music_stop(uint32_t fade) { slot<void XB_CDECL(uint32_t)>(0x0092230c)(fade); }  // GScr_MusicStop(fade seconds)
// GScr_MusicPlay(name) on a temporary string.
void music_play(uint32_t text) {
    ScriptString name;
    slot<void XB_CDECL(ScriptString*)>(0x00922308)(from_text(&name, text));
    ScriptString_Release(&name);
}

// GScr_PlaySound(ent, "FLAK_METAL_HIT"), then Scr_Wait(seconds).
void flak_hit(uint32_t ent, float seconds) {
    ScriptString alias;
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, from_text(&alias, 0x007f4fac), 0, 0, 0);
    ScriptString_Release(&alias);
    Scr_Wait(seconds);
}

// The objective builtins: GScr_ObjectiveState(index, "done"), GScr_ObjectiveCurrent, GScr_ObjectivePosition.
void objective_done(uint32_t index) {
    ScriptString state;
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009222bc)(index, from_text(&state, 0x0083ae34));  // "done"
    ScriptString_Release(&state);
}
void objective_current(uint32_t index) { slot<void XB_CDECL(uint32_t)>(0x009222d0)(index); }
ScrVec3 vec_at(uint32_t x, uint32_t y, uint32_t z) {
    const ScrVec3 v = {{title_global<uint32_t>(x), title_global<uint32_t>(y), title_global<uint32_t>(z)}};
    return v;
}
void objective_position(uint32_t index, uint32_t x, uint32_t y, uint32_t z) {
    ScrVec3 v = vec_at(x, y, z);
    slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x009222cc)(index, &v);
}
// GScr_ObjectiveAddLocalizedWithPosition(index, "active", text, &position) straight through the slot.
void objective_add_slot(uint32_t index, uint32_t text, uint32_t x, uint32_t y, uint32_t z) {
    ScriptString state;
    from_text(&state, 0x0083aecc);  // "active"
    ScrVec3 v = vec_at(x, y, z);
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, ScrVec3*)>(0x009222b4)(index, &state, text, &v);
    ScriptString_Release(&state);
}
// The same through ScrCall_GScr_ObjectiveAddLocalizedWithPosition (0x0011a730, position by value).
using ObjectiveAdd = uint32_t XB_CDECL(uint32_t, ScriptString*, uint32_t, ScrVec3);
void objective_add(uint32_t index, uint32_t text, ScrVec3 position) {
    ScriptString state;
    from_text(&state, 0x0083aecc);  // "active"
    title_function<ObjectiveAdd>(0x0011a730)(index, &state, text, position);
    ScriptString_Release(&state);
}
bool stage_is(float stage, uint32_t value) { return stage == title_global<float>(value); }

}  // namespace

void XB_CDECL BroLvl_prologue_WaitLevelNotifyCount(uint32_t, int32_t count) {
    for (int32_t left = count; left > 0; --left) Scr_WaitTill(level_ent(), 0xb1ada748);
}

uint32_t* XB_CDECL BroLvl_prologue_SpawnVehicleDroneDefault(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString name,
                                                            uint32_t a5, uint32_t a6) {
    // BroLvl_prologue_SpawnVehicleDrone (0x0044e120, hidden out) with undefined handles and an empty string
    // for the other arguments.
    using Spawn = void XB_CDECL(uint32_t*, uint32_t, uint32_t, ScriptString, uint32_t, uint32_t, ScriptString,
                                uint32_t, uint32_t, uint32_t);
    const uint32_t undefined = title_global<uint32_t>(0x00922ea4);
    const ScriptString empty = copy_string(empty_string());
    const ScriptString copy = copy_string(&name);
    uint32_t result;
    title_function<Spawn>(0x0044e120)(&result, a2, a3, copy, undefined, title_global<uint32_t>(0x00922ebc), empty,
                                      undefined, a5, a6);
    *out = result;
    ScriptString_Release(&name);
    return out;
}

void XB_CDECL BroLvl_prologue_DeleteByChainOrTarget(uint32_t, ScriptString chain, ScriptString target) {
    uint32_t ent = 0;
    if (chain.rep) {
        ent = get_ent(&chain, 0x8007a3d3);  // script_chain
    } else if (target.rep) {
        ent = get_ent(&target, 0xee2890d4);  // target
    }
    delete_ent(ent);
    ScriptString_Release(&chain);
    ScriptString_Release(&target);
}

void XB_CDECL BroLvl_prologue_SaveCheckpointNamed(uint32_t, ScriptString name) {
    slot<void XB_CDECL(const char*)>(0x00922320)(text_of(&name));  // GScr_SaveCheckpoint
    ScriptString_Release(&name);
}

void XB_CDECL BroLvl_prologue_FindVehicleZones() {
    using FindZone = uint32_t XB_CDECL(ScriptString*);
    ScriptString name;
    const uint32_t first = slot<FindZone>(0x00922494)(from_text(&name, 0x007f4934));  // GScr_FindZone("prologue_Vehicles01")
    level_word(0x894) = first;
    ScriptString_Release(&name);
    const uint32_t second = slot<FindZone>(0x00922494)(from_text(&name, 0x007f4920));  // "prologue_Vehicles02"
    level_word(0x864) = second;
    ScriptString_Release(&name);
}

void XB_CDECL BroLvl_prologue_TurretAimAhead(uint32_t tank) {
    end_on(tank, 0x06730ae8);  // death
    notify(tank, 0x24f76131);  // stop_tank_firing
    const ScrVec3 angles = angles_of(tank);
    const float undefined = as_float(title_global<uint32_t>(0x00922eb8));
    float forward[3] = {undefined, undefined, undefined};
    slot<void XB_CDECL(float*, const ScrVec3*)>(0x00922290)(forward, &angles);  // Scr_AnglesToForward
    const float scale = title_global<float>(0x0083ebec);  // 500
    const float ahead_x = forward[0] * scale;
    const float ahead_y = forward[1] * scale;
    const float ahead_z = forward[2] * scale;
    const ScrVec3 origin = origin_of(tank);
    float spot[3];
    spot[0] = as_float(origin.v[0]) + ahead_x;
    spot[1] = as_float(origin.v[1]) + ahead_y;
    spot[2] = as_float(origin.v[2]) + ahead_z + title_global<float>(0x0083f058);  // + 104
    ScriptString classname;
    using Spawn = uint32_t XB_CDECL(ScriptString*, const float*, uint32_t);
    const uint32_t target = slot<Spawn>(0x009222f0)(from_text(&classname, 0x0083ab88), spot, 0);  // GScr_Spawn("script_origin")
    ScriptString_Release(&classname);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(target, tank);  // GScr_LinkTo
    const float offset[3] = {0.0f, 0.0f, 0.0f};
    slot<void XB_CDECL(uint32_t, uint32_t, const float*)>(0x009228cc)(tank, target, offset);  // GScr_Vehicle_SetTurretTargetEnt
    Scr_WaitTill(tank, 0x3d70a110);  // turret_on_target
    slot<void XB_CDECL(uint32_t)>(0x009228d0)(tank);  // GScr_Vehicle_ClearTurretTarget
    Scr_Wait(0.1f);
    delete_ent(target);
}

uint8_t XB_CDECL BroLvl_prologue_AnimTreeNameOp(int32_t index, uint32_t a2, uint32_t a3) {
    const uint32_t names[12] = {
        0x007f49d4, 0x007f49c4, 0x007f49b0, 0x0083a894,  // ?, "flakvierling", "flakvierling_base", ?
        0x007f49a8, 0x007f499c, 0x007f498c, 0x007f4978,  // "mg42", "panzerIV", "prologue_drones", "prologue_wounded"
        0x007f4970, 0x007f4960, 0x007f4954, 0x007f4948,  // "stuka", "tankdeadguys", "treeburst", "willyjeep"
    };
    if (index >= 12) return 0;
    return slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x00922160)(a2, names[index], a3) != 0;  // pScr_strncpy
}

void XB_CDECL BroLvl_prologue_InitMusic(uint32_t, int32_t stage, float delay) {
    notify(level_ent(), 0xce4339fd);
    Scr_WaitFrames(2);
    end_on(level_ent(), 0xce4339fd);
    if (delay != as_float(title_global<uint32_t>(0x00922eac))) Scr_Wait(delay);  // g_scrUndefinedFloat
    if (stage == 1) {
        music_stop(0);
        Scr_Wait(0.1f);
        music_play(0x007f4d34);  // "BG_PROLOGUE_BRIEFING"
    } else if (stage == 2) {
        Scr_Wait(3.0f);
        music_stop(0);
        Scr_Wait(0.1f);
        music_play(0x007f4d1c);  // "PRO_E01_MXBG_IntoTown"
        Scr_Wait(30.0f);
        music_stop(0x3e4ccccd);  // 0.2
        Scr_Wait(0.25f);
        music_play(0x007f4d00);  // "BG_PROLOGUE_POST_BRIEFING"
    } else if (stage == 3) {
        music_stop(0);
        Scr_Wait(0.1f);
        music_play(0x007f4ce8);  // "BG_PROLOGUE_POST_WALL"
    } else if (stage == 4) {
        music_stop(0);
        Scr_Wait(0.1f);
        music_play(0x007f4cd4);  // "PRO_E04_MXBG_Tiger"
        Scr_Wait(65.0f);
        music_stop(0x3e4ccccd);  // 0.2
        Scr_Wait(0.25f);
        music_play(0x007f4ce8);  // "BG_PROLOGUE_POST_WALL"
    }
}

void XB_CDECL BroLvl_prologue_DamageOnGoal(uint32_t ent) {
    Scr_WaitTill(ent, 0x0032304c);  // goal
    const ScrVec3 origin = origin_of(ent);
    const int32_t health = slot<int32_t XB_CDECL(uint32_t)>(0x00922b90)(ent);  // ScrEnt_GetHealth
    slot<void XB_CDECL(uint32_t, float, const ScrVec3*)>(0x00922558)(ent, static_cast<float>(health + 50), &origin);  // GScr_DoDamage
}

void XB_CDECL BroLvl_prologue_TankScrapeSound(uint32_t tank) {
    Scr_Wait(3.3f);
    ScriptString alias;
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(
        tank, from_text(&alias, 0x007f4e60), 0, 0, 0);  // GScr_PlaySound("PROL_Event1_Tank_Scrape")
    ScriptString_Release(&alias);
}

void XB_CDECL BroLvl_prologue_SetEvent1DriverAnimname(uint32_t guy) {
    ScriptString name;
    from_text(&name, 0x007f4e78);  // "event1_driver"
    slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(guy, copy_string(&name));  // ScrEnt_SetAnimname
    ScriptString_Release(&name);
}

void XB_CDECL BroLvl_prologue_DeleteBelowFloor(uint32_t ent) {
    while (as_float(origin_of(ent).v[2]) > title_global<float>(0x007f4ea4)) Scr_Wait(0.25f);  // -192
    delete_ent(ent);
}

void XB_CDECL BroLvl_prologue_QuakeOnTargetTrigger(uint32_t trigger) {
    ScriptString name;
    const uint32_t target = get_ent(ScrCall_ScrEnt_GetTarget(&trigger, &name), 0x0d786b4c);  // by targetname
    ScriptString_Release(&name);
    Scr_WaitTill(trigger, 0xe2a210cf);  // trigger
    const ScrVec3 angles = angles_of(target);
    const uint32_t undefined = undefined_lane();
    ScrVec3 forward = {{undefined, undefined, undefined}};
    slot<void XB_CDECL(ScrVec3*, const ScrVec3*)>(0x00922290)(&forward, &angles);  // Scr_AnglesToForward
    const ScrVec3 origin = origin_of(target);
    // GScr_Earthquake(0.36, 2, origin, 2000)
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3eb851ec, 0x40000000, &origin, 0x44fa0000);
    ScriptString* const fx = ScrField_script_fxid(target);
    const ScrVec3 direction = forward;
    slot<void XB_CDECL(uint32_t, ScriptString*, const ScrVec3*, uint32_t, uint32_t)>(0x009221e0)(target, fx, &direction, 0, 0);  // GScr_PlaySoundAtPosition
}

void XB_CDECL BroLvl_prologue_Event3Panzer1OffTime() {
    ScriptString name;
    const uint32_t panzer = get_ent(from_text(&name, 0x007f4ea8), 0x0d786b4c);  // "event3_panzer1" by targetname
    ScriptString_Release(&name);
    *ScrField_script_offtime(panzer) = 1;
    Scr_Wait(13.0f);
    *ScrField_script_offtime(panzer) = 0;
}

uint8_t* XB_CDECL BroLvl_prologue_PlayerStanceAtMost(uint8_t* out, uint32_t, int32_t most) {
    ScriptString stance;
    ScriptString_FromRep(&stance, nullptr);
    if (most < 3) {
        int32_t rank = 3;
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x00922600)(title_global<uint32_t>(0x00921cd0), &stance);  // GScr_GetStance
        if (ScriptString_Equals(&stance, pointer<const char>(0x0083ae7c))) {  // "crouch"
            rank = 2;
        } else if (ScriptString_Equals(&stance, pointer<const char>(0x008367fc))) {  // "prone"
            rank = 1;
        }
        *out = rank > most ? 0 : 1;
    } else {
        *out = 1;
    }
    ScriptString_Release(&stance);
    return out;
}

void XB_CDECL BroLvl_prologue_DriftAroundCourtyard(uint32_t ent) {
    using RandomRange = long double XB_CDECL(float, float);
    using Distance = long double XB_CDECL(const ScrVec3*, const ScrVec3*);
    float min_x = title_global<float>(0x007f4ef8);  // -3704
    float max_x = title_global<float>(0x007f4ef0);  // -3328
    float min_y = title_global<float>(0x007f4ef4);  // -1096
    float max_y = title_global<float>(0x007f4eec);  // -728
    const float z = title_global<float>(0x0083ef04);  // 24
    bool switched = false;
    for (;;) {
        if (!switched && *pointer<uint8_t>(level() + 0x860)) {
            min_x = title_global<float>(0x007f4ee8);  // -4000
            max_x = title_global<float>(0x007f4ef0);  // -3328
            min_y = title_global<float>(0x007f4ef4);  // -1096
            max_y = title_global<float>(0x007f4ea4);  // -192
            switched = true;
        }
        const float x = static_cast<float>(slot<RandomRange>(0x00922238)(min_x, max_x));  // Scr_RandomFloatRange
        const float y = static_cast<float>(slot<RandomRange>(0x00922238)(min_y, max_y));
        ScrVec3 target = {{bits(x), bits(y), bits(z)}};
        const ScrVec3 origin = origin_of(ent);
        const float distance = static_cast<float>(slot<Distance>(0x00922268)(&origin, &target));  // Scr_Distance
        const int32_t speed = random_int(100) + 150;
        float time = distance / static_cast<float>(speed);
        if (title_global<float>(0x0083ebf8) > time) time = title_global<float>(0x0083ec24);  // < 0.1: 0.25
        slot<void XB_CDECL(uint32_t, ScrVec3*, float, uint32_t, uint32_t)>(0x00922794)(ent, &target, time, 0, 0);  // GScr_MoveTo
        Scr_WaitTill(ent, 0x2a63b2d6);  // movedone
    }
}

void XB_CDECL BroLvl_prologue_Event4TigerThroughWall() {
    using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
    ScriptString text;
    uint32_t wall = get_ent(from_text(&text, 0x007f4f8c), 0x0d786b4c);  // "event4_tiger_through_wall_left"
    ScriptString_Release(&text);
    slot<PlaySound>(0x009221dc)(wall, from_text(&text, 0x007f4f74), 0, 0, 0);  // GScr_PlaySound("event3_building1_exp2")
    ScriptString_Release(&text);
    slot<PlaySound>(0x009221dc)(wall, from_text(&text, 0x007f4f68), 0, 0, 0);  // "wall_explo"
    ScriptString_Release(&text);
    Scr_Wait(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000) + title_global<float>(0x0083ec8c)));  // Scr_RandomFloat(1) + 1.3
    wall = get_ent(from_text(&text, 0x007f4f48), 0x0d786b4c);  // "event4_tiger_through_wall_right"
    ScriptString_Release(&text);
    slot<PlaySound>(0x009221dc)(wall, from_text(&text, 0x007f4f74), 0, 0, 0);  // "event3_building1_exp2"
    ScriptString_Release(&text);
    Scr_Wait(2.0f);
}

void XB_CDECL BroLvl_prologue_FlakMetalHits(uint32_t ent) {
    using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
    ScriptString alias;
    slot<PlaySound>(0x009221dc)(ent, from_text(&alias, 0x007f4fac), 100, 0, 0);  // GScr_PlaySound("FLAK_METAL_HIT")
    ScriptString_Release(&alias);
    Scr_Wait(3.0f);
    slot<PlaySound>(0x009221dc)(ent, from_text(&alias, 0x007f4fac), 75, 0, 0);
    ScriptString_Release(&alias);
}

void XB_CDECL BroLvl_prologue_ActorGoToTag(uint32_t actor, uint32_t ent, ScriptString tag) {
    using TagVec = void XB_CDECL(uint32_t, ScrVec3*, ScriptString*);
    const uint32_t undefined = undefined_lane();
    ScrVec3 vec = {{undefined, undefined, undefined}};
    slot<TagVec>(0x0092261c)(ent, &vec, &tag);  // GScr_GetTagOrigin
    ScrVec3 origin = vec;
    vec.v[0] = vec.v[1] = vec.v[2] = undefined_lane();
    slot<TagVec>(0x00922620)(ent, &vec, &tag);  // GScr_GetTagAngles (unused)
    const float radius = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922df0)(actor));  // ScrSentient_GetFloat_18
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922df4)(actor, 0x42800000);  // ScrSentient_SetFloat_18(64)
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00922710)(actor, 0xc1165808, 0xc1165808, 0xc1165808);  // GScr_ActorAllowedStances(crouch x3)
    slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922684)(actor, &origin);  // GScr_SetGoalPos
    Scr_WaitTill(actor, 0x0032304c);  // goal
    slot<void XB_CDECL(uint32_t, float)>(0x00922df4)(actor, radius);
    ++level_word(0x870);
    ScriptString_Release(&tag);
}

void XB_CDECL BroLvl_prologue_Event4FlakFireFx() {
    using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
    ScriptString text;
    const uint32_t fx1 = get_ent(from_text(&text, 0x007f503c), 0x0d786b4c);  // "event4_flak_fire_effect_and_sound"
    ScriptString_Release(&text);
    const uint32_t fx2 = get_ent(from_text(&text, 0x007f5018), 0x0d786b4c);  // "..._sound2"
    ScriptString_Release(&text);
    const uint32_t fx3 = get_ent(from_text(&text, 0x007f4ff4), 0x0d786b4c);  // "..._sound3"
    ScriptString_Release(&text);
    const uint32_t fx4 = get_ent(from_text(&text, 0x007f4fd0), 0x0d786b4c);  // "..._sound4"
    ScriptString_Release(&text);
    Scr_Wait(9.3f);
    // GScr_PlayLoopSound(fx1, "Event4_Flak_Fire", notify 0x25635b90)
    const uint32_t loop = slot<uint32_t XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221e4)(
        fx1, from_text(&text, 0x007f4fbc), 0x25635b90, 0, 0);
    ScriptString_Release(&text);
    Scr_WaitTill(fx1, 0x25635b90);
    Scr_Wait(2.0f);
    slot<void XB_CDECL(uint32_t)>(0x009221ec)(loop);  // GScr_StopSound
    flak_hit(fx1, 0.25f);
    flak_hit(fx2, 0.25f);
    flak_hit(fx4, 0.5f);
    flak_hit(fx1, 0.5f);
    flak_hit(fx3, 0.25f);
    flak_hit(fx4, 0.5f);
    flak_hit(fx2, 0.5f);
    flak_hit(fx1, 0.25f);
    flak_hit(fx3, 5.0f);
    delete_ent(fx1);
    delete_ent(fx2);
    delete_ent(fx3);
    delete_ent(fx4);
}

void XB_CDECL BroLvl_prologue_FlakCollisionForPlayer() {
    ScriptString text;
    const uint32_t collision = get_ent(from_text(&text, 0x007f5068), 0x0d786b4c);  // "collision_for_flak"
    ScriptString_Release(&text);
    BroScr_TriggerOff(collision);
    bool on = false;
    const uint32_t flak = get_ent(from_text(&text, 0x007f5060), 0x0d786b4c);  // "jl830"
    ScriptString_Release(&text);
    for (;;) {
        Scr_WaitTill(flak, 0x2b740dd3);  // turretownerchange
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922588)(flak) == title_global<uint32_t>(0x00921cd0)) {  // GScr_Turret_GetUser == player
            if (!on) {
                on = true;
                BroScr_TriggerOn(collision);
            }
        } else if (on) {
            on = false;
            BroScr_TriggerOff(collision);
        }
    }
}

void XB_CDECL BroLvl_prologue_KillOnDeathOf(uint32_t ent, uint32_t other) {
    Scr_WaitTill(other, 0x06730ae8);  // death
    const ScrVec3 origin = origin_of(ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*)>(0x00922558)(ent, 0x461c4000, &origin);  // GScr_DoDamage(10000)
}

void XB_CDECL BroLvl_prologue_Event5RedSmoke(uint32_t, uint32_t grenade) {
    using PlaySound = uint32_t XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
    using Spawn = uint32_t XB_CDECL(ScriptString*, const ScrVec3*, uint32_t);
    ScriptString text;
    from_text(&text, 0x0083ab88);  // "script_origin"
    const ScrVec3 origin = origin_of(grenade);
    const uint32_t smoke = slot<Spawn>(0x009222f0)(&text, &origin, 0);  // GScr_Spawn
    ScriptString_Release(&text);
    // GScr_PlaySound(smoke, "event5_red_smoke", notify 0x75c3b1bc, 1)
    const uint32_t sound = slot<PlaySound>(0x009221dc)(smoke, from_text(&text, 0x007f50dc), 0x75c3b1bc, 1, 0);
    ScriptString_Release(&text);
    slot<PlaySound>(0x009221dc)(grenade, from_text(&text, 0x007f50cc), 0, 0, 0);  // "Smoke_Grenade"
    ScriptString_Release(&text);
    const int32_t end = get_time() + 30000;
    const uint32_t undefined = undefined_lane();
    ScrVec3 angles = {{undefined, undefined, undefined}};
    if (get_time() < end) {
        do {
            using RandomFloat = long double XB_CDECL(uint32_t);
            const float pitch = static_cast<float>(slot<RandomFloat>(0x00922230)(0x42340000));  // Scr_RandomFloat(45)
            const float yaw = static_cast<float>(slot<RandomFloat>(0x00922230)(0x43b40000));    // (360)
            angles.v[0] = bits(pitch);
            angles.v[1] = bits(yaw);
            angles.v[2] = bits(0.0f);
            const float time = static_cast<float>(slot<RandomFloat>(0x00922230)(0x40400000) + title_global<float>(0x0083eb20));  // (3) + 1
            // GScr_RotateTo(smoke, angles, time, 0.5, 0.5)
            slot<void XB_CDECL(uint32_t, ScrVec3*, float, uint32_t, uint32_t)>(0x009227a8)(smoke, &angles, time, 0x3f000000, 0x3f000000);
            Scr_WaitTill(smoke, 0xae6d9d1d);  // rotatedone
        } while (get_time() < end);
    }
    slot<void XB_CDECL(uint32_t)>(0x009221f4)(sound);  // GScr_FreeSound
    Scr_Wait(5.0f);
    delete_ent(grenade);
    delete_ent(smoke);
}

void XB_CDECL BroLvl_prologue_PitchUpStep(uint32_t ent, float step) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    bool defined;
    if (!*ScrField_pitch_IsDefined(&defined, ent)) {
        const ScrVec3 angles = angles_of(ent);
        float* const pitch = reinterpret_cast<float*>(ScrField_pitch(ent));
        *pitch = as_float(angles.v[0]);
        slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x40a00000);  // Scr_RandomFloat(5), unused
    }
    if (title_global<float>(0x0083ef1c) > as_float(*ScrField_pitch(ent))) {  // < 45
        const float scaled = step * title_global<float>(0x0083ece8);  // 5
        const float sum = scaled + as_float(*ScrField_pitch(ent));
        *reinterpret_cast<float*>(ScrField_pitch(ent)) = sum;
        if (as_float(*ScrField_pitch(ent)) > title_global<float>(0x0083ef1c)) {
            *reinterpret_cast<float*>(ScrField_pitch(ent)) = title_global<float>(0x0083ef1c);
        }
    }
}

void XB_CDECL BroLvl_prologue_ArtilleryWhenBelow(uint32_t, uint32_t ent, float height) {
    while (as_float(origin_of(ent).v[2]) > height) Scr_Wait(0.1f);
    ScriptString text;
    from_text(&text, 0x0083ab88);  // "script_origin"
    const ScrVec3 origin = origin_of(ent);
    const uint32_t spot = slot<uint32_t XB_CDECL(ScriptString*, const ScrVec3*, uint32_t)>(0x009222f0)(&text, &origin, 0);  // GScr_Spawn
    ScriptString_Release(&text);
    // GScr_PlaySound(spot, "artillery_generic", notify 0x75c3b1bc)
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(spot, from_text(&text, 0x007f50f0), 0x75c3b1bc, 0, 0);
    ScriptString_Release(&text);
    notify(level_ent(), 0x7ad8c8f5);
    delete_ent(ent);
    Scr_WaitTill(spot, 0x75c3b1bc);
    Scr_Wait(0.5f);
    delete_ent(spot);
}

void XB_CDECL BroLvl_prologue_BuildingFallSound(uint32_t, ScrVec3 pos) {
    ScriptString text;
    const uint32_t spot = slot<uint32_t XB_CDECL(ScriptString*, const ScrVec3*, uint32_t)>(0x009222f0)(
        from_text(&text, 0x0083ab88), &pos, 0);  // GScr_Spawn("script_origin")
    ScriptString_Release(&text);
    // GScr_PlaySound(spot, "building_fall", notify 0x75c3b1bc)
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(spot, from_text(&text, 0x007f5104), 0x75c3b1bc, 0, 0);
    ScriptString_Release(&text);
    Scr_WaitTill(spot, 0x75c3b1bc);
    delete_ent(spot);
}

void XB_CDECL BroLvl_prologue_ShellImpactAt(uint32_t, ScrVec3 pos, uint32_t type) {
    ScriptString text;
    const uint32_t spot = slot<uint32_t XB_CDECL(ScriptString*, const ScrVec3*, uint32_t)>(0x009222f0)(
        from_text(&text, 0x0083ab88), &pos, 0);  // GScr_Spawn("script_origin")
    ScriptString_Release(&text);
    const uint32_t undefined = title_global<uint32_t>(0x00922ea4);
    BroScr_ShellIncomingThenImpact(spot, title_global<uint32_t>(0x0083ebf4), title_global<uint32_t>(0x0083ec30),
                                   title_global<uint32_t>(0x0083eb20), undefined, undefined, undefined, type);  // 512, 50, 1
    Scr_WaitTill(spot, 0xbd1fe198);
    delete_ent(spot);
}

void XB_CDECL BroLvl_prologue_PlayerKnockdown(uint32_t, float time) {
    slot<void XB_CDECL(uint32_t)>(0x00922a14)(0x3fa00000);  // GScr_SetFogPassFinalAlpha(1.25)
    slot<void XB_CDECL(uint32_t)>(0x00922a18)(0x3f8ccccd);  // GScr_SetFogPassStep(1.1)
    slot<void XB_CDECL(uint32_t)>(0x00922a1c)(0x3f933333);  // GScr_SetFogPassAlpha(1.15)
    slot<void XB_CDECL(uint32_t)>(0x00922a20)(4);           // GScr_SetFogPassCount
    const float half = time * title_global<float>(0x0083eb24);  // 0.5
    const uint32_t player = title_global<uint32_t>(0x00921cd0);
    const ScrVec3 angles = angles_of(player);
    BroScr_LerpPlayerAnglesCall_2(player, title_global<float>(0x0083ec84), as_float(angles.v[1]), title_global<float>(0x0083ef1c),
                                  half);  // (40, yaw, 45)
    BroScr_LerpPlayerAnglesCall_2(title_global<uint32_t>(0x00921cd0), title_global<float>(0x0083ed04), title_global<float>(0x0083eed4),
                                  title_global<float>(0x0083ef18), half);  // (300, 240, -45)
    const ScrVec3 origin = origin_of(title_global<uint32_t>(0x00921cd0));
    // GScr_Earthquake(1, 0.25, origin, 300)
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3f800000, 0x3e800000, &origin, 0x43960000);
    ScriptString text;
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(
        title_global<uint32_t>(0x00921cd0), from_text(&text, 0x007f5118), 0, 0, 0);  // GScr_PlaySound("Body_Land")
    ScriptString_Release(&text);
    BroScr_LerpPlayerAnglesCall_2(title_global<uint32_t>(0x00921cd0), title_global<float>(0x007f5114), title_global<float>(0x0083eb60),
                                  title_global<float>(0x0083f298), half);  // (345, 180, -75)
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922844)(title_global<uint32_t>(0x00921cd0), 1);  // GScr_PlayerSetInt_58c
}

void XB_CDECL BroLvl_prologue_PlayerAllowOnlyStance(uint32_t, uint32_t stance) {
    using Allow = void XB_CDECL(uint32_t, uint32_t);
    if (stance == title_global<uint32_t>(0x00922eb4)) return;  // g_scrUndefinedHandle
    slot<Allow>(0x00922828)(title_global<uint32_t>(0x00921cd0), 1);  // GScr_PlayerAllowPmFlag200000
    slot<Allow>(0x00922824)(title_global<uint32_t>(0x00921cd0), 1);  // GScr_PlayerAllowPmFlag100000
    slot<Allow>(0x00922820)(title_global<uint32_t>(0x00921cd0), 1);  // GScr_PlayerAllowPmFlag80000
    if (stance == 0x070b89a4) {  // stand
        slot<Allow>(0x00922828)(title_global<uint32_t>(0x00921cd0), 0);
        slot<Allow>(0x00922824)(title_global<uint32_t>(0x00921cd0), 0);
    } else if (stance == 0xc1165808) {  // crouch
        slot<Allow>(0x00922828)(title_global<uint32_t>(0x00921cd0), 0);
        slot<Allow>(0x00922820)(title_global<uint32_t>(0x00921cd0), 0);
    } else if (stance == 0x0738b1a5) {  // prone
        slot<Allow>(0x00922824)(title_global<uint32_t>(0x00921cd0), 0);
        slot<Allow>(0x00922820)(title_global<uint32_t>(0x00921cd0), 0);
    } else if (stance == 0xc0d4ba61) {
        slot<Allow>(0x00922828)(title_global<uint32_t>(0x00921cd0), 0);
    }
}

void XB_CDECL BroLvl_prologue_FriendlyChainThink(uint32_t, float stage) {
    uint32_t current;
    if (stage_is(stage, 0x0083eb20)) {  // 1
        objective_add_slot(1, 0x9515d7f4, 0x007f4e3c, 0x007f4e38, 0x007f4e34);  // (-736, -1936, 152)
        current = 1;
    } else if (stage_is(stage, 0x0083eba8)) {  // 2
        objective_done(1);
        objective_add_slot(2, 0x9515d7f7, 0x007f4e30, 0x007f4e2c, 0x0083ef00);  // (-1248, -1856, 75)
        current = 2;
    } else if (stage_is(stage, 0x0083ec9c)) {  // 3
        objective_done(2);
        objective_add_slot(3, 0x9515d7f6, 0x007f4e28, 0x007f4e24, 0x0083ebe8);  // (-992, -1260, 128)
        current = 3;
    } else if (stage_is(stage, 0x007bd0c8)) {  // 4
        objective_done(3);
        objective_add_slot(4, 0x9515d7f1, 0x007f4e20, 0x007f4e1c, 0x007f4e18);  // (-216, -1120, 92)
        current = 4;
    } else if (stage_is(stage, 0x0083ece8)) {  // 5
        objective_done(4);
        objective_add(5, 0x9515d7f0, vec_at(0x007f4e14, 0x0083f184, 0x0083eeb0));  // (-892, -4, 28)
        current = 5;
    } else if (stage_is(stage, 0x0083ecd0)) {  // 6
        objective_done(5);
        objective_add(6, 0x9515d7f3, vec_at(0x007f4e10, 0x007f4e0c, 0x007f4e08));  // (-1480, -232, -152)
        current = 6;
    } else if (stage_is(stage, 0x0083f1f8)) {  // 7
        objective_done(6);
        objective_add(7, 0x9515d7f2, vec_at(0x007f4e04, 0x007f4e00, 0x007f4dfc));  // (-1816, -296, -22)
        current = 7;
    } else if (stage_is(stage, 0x007f4df8)) {  // 7.1
        objective_position(7, 0x007f4df4, 0x007f4df0, 0x007f4dec);  // (-2234, -552, 46)
        return;
    } else if (stage_is(stage, 0x007f4de8)) {  // 7.2
        objective_position(7, 0x007f4de4, 0x007f4de0, 0x0083ecd8);  // (-2640, -896, 48)
        return;
    } else if (stage_is(stage, 0x007f4ddc)) {  // 7.3
        objective_position(7, 0x007f4dd8, 0x007f4dd4, 0x0083ecd8);  // (-3160, -1004, 48)
        return;
    } else if (stage_is(stage, 0x0083ed78)) {  // 8
        objective_add(8, 0x9515d7fd, vec_at(0x007f4dd0, 0x007f4dcc, 0x0083ee38));  // (-3928, -38, 30)
        current = 8;
    } else if (stage_is(stage, 0x007f4dc8)) {  // 8.1
        objective_done(8);
        objective_current(7);
        objective_position(7, 0x007f4dc4, 0x007f4dc0, 0x0083ebb8);  // (-4128, 488, 16)
        return;
    } else if (stage_is(stage, 0x007f4dbc)) {  // 8.2
        objective_position(7, 0x007f4db8, 0x007f4db4, 0x0083ebb8);  // (-3984, 1292, 16)
        return;
    } else if (stage_is(stage, 0x0083eef8)) {  // 9
        objective_done(7);
        objective_add(9, 0x9515d7fc, vec_at(0x007f4db0, 0x007f4dac, 0x0083ebb8));  // (-3208, 1460, 16)
        current = 9;
    } else if (stage_is(stage, 0x0083eb98)) {  // 10
        objective_done(9);
        ScriptString name;
        uint32_t bomb;
        ScrCall_GScr_GetEntArray(&bomb, address_of(from_text(&name, 0x007f4d9c)), 0x0d786b4c, 0);  // "event4_use_bomb" by targetname
        ScriptString_Release(&name);
        ScriptString state;
        from_text(&state, 0x0083aecc);  // "active"
        uint32_t buffer[3];
        const uint32_t* const o = ScrCall_ScrEnt_GetOrigin(&bomb, buffer);
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        title_function<ObjectiveAdd>(0x0011a730)(10, &state, 0xa2bafea2, origin);
        ScriptString_Release(&state);
        current = 10;
    } else if (stage_is(stage, 0x007f4d98)) {  // 11
        objective_add(11, 0xa2bafea3, vec_at(0x007f4d94, 0x0083f27c, 0x0083ebb8));  // (-3848, 600, 16)
        current = 11;
    } else if (stage_is(stage, 0x007f4d90)) {  // 11.1
        objective_position(11, 0x007f4d8c, 0x007f4d88, 0x0083ef28);  // (-3744, 544, 160)
        return;
    } else if (stage_is(stage, 0x0083eccc)) {  // 12
        objective_add(12, 0xa2bafea0, vec_at(0x007f4d84, 0x007f4d80, 0x007f4d7c));  // (-2639, 935, 43)
        current = 12;
    } else if (stage_is(stage, 0x007f4d78)) {  // 12.1
        objective_done(12);
        objective_current(11);
        objective_position(11, 0x007f4d74, 0x007f4d70, 0x0083ebb8);  // (-3496, 728, 16)
        return;
    } else if (stage_is(stage, 0x007f4d6c)) {  // 12.2
        objective_position(11, 0x007f4d68, 0x007f4d64, 0x0083ebb8);  // (-3472, 1360, 16)
        return;
    } else if (stage_is(stage, 0x007f4d60)) {  // 12.3
        objective_done(11);
        current = 10;
    } else if (stage_is(stage, 0x0083ec78)) {  // 13
        objective_done(10);
        objective_add(13, 0xa2bafea1, vec_at(0x007f4d5c, 0x007f4d58, 0x007e5bf0));  // (-2176, 1344, 64)
        current = 13;
    } else if (stage_is(stage, 0x0083f294)) {  // 14
        objective_done(13);
        objective_add(14, 0xa2bafea6, vec_at(0x007f4d54, 0x007f4d50, 0x007f4d4c));  // (-1978, 1182, 312)
        current = 14;
    } else if (stage_is(stage, 0x0083ef10)) {  // 15
        objective_done(14);
        objective_add(14, 0xa2bafea7, vec_at(0x007f4d54, 0x007f4d50, 0x007f4d4c));
        current = 14;
    } else {
        return;
    }
    objective_current(current);
}

}  // namespace game
