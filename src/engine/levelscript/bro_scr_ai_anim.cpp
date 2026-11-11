#include "bro_scr_ai_anim.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_nodes_spawner.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_actor_threads.h"
#include "bro_scr_notify_threads.h"
#include "bro_scr_pose_threads.h"
#include "g_scr_builtins.h"

namespace game {

using namespace bro_ai;

namespace {

void drop_bad_parameter() {
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x00825ee0));  // "bad parameter: crap_out_now + please"
    ScriptString_Release(&text);
}

// ScrCall_GScr_GetEntScriptObject_2 (0x0011eb30, set aside): removes `key` from *ent's table.
void remove_key(const uint32_t* ent, uint32_t key) {
    own_function<void XB_THISCALL(const uint32_t*, uint32_t)>(&ScrCall_GScr_GetEntScriptObject_2)(ent, key);
}

bool stance_allowed(uint32_t actor, uint32_t pose) {
    return static_cast<uint8_t>(
               slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(actor, pose)) != 0;  // GScr_ActorIsStanceAllowed
}

uint32_t actor_pose(uint32_t actor) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(actor); }  // GetInt_184

uint8_t* is_fire_type(uint8_t* out, uint32_t actor, uint32_t type) {
    uint32_t value;
    *out = *BroScr_GetWeaponFireType(&value, actor) == type;
    return out;
}

constexpr uint32_t kStand = 0x070b89a4;
constexpr uint32_t kCrouch = 0xc1165808;
constexpr uint32_t kProne = 0x0738b1a5;

}  // namespace

void XB_CDECL BroScr_MeleePair_Unlink(uint32_t a, uint32_t b) {
    bool defined;
    if (entity_handle_is_valid(b) && *ScrField_h31aaf853_IsDefined(&defined, a) && *ScrField_h31aaf853(a) == b) {
        if (!*ScrField_h236fc839_IsDefined(&defined, b)) drop_bad_parameter();
        if (*ScrField_h236fc839(b) != a) drop_bad_parameter();
    }
    if (!entity_handle_is_valid(b)) {
        remove_key(&a, 0x31aaf853);
    } else if (*ScrField_h236fc839_IsDefined(&defined, b) && *ScrField_h236fc839(b) == a) {
        remove_key(&b, 0x236fc839);
        if (*ScrField_h31aaf853(a) != b) drop_bad_parameter();
        remove_key(&a, 0x31aaf853);
    }
    if (entity_handle_is_valid(b) && *ScrField_h31aaf853_IsDefined(&defined, a) && *ScrField_h31aaf853(a) == b)
        drop_bad_parameter();
    if (entity_handle_is_valid(b) && *ScrField_h236fc839_IsDefined(&defined, b) && *ScrField_h236fc839(b) == a)
        drop_bad_parameter();
}

uint32_t* XB_CDECL BroScr_GetAbsYawToTarget(uint32_t* out, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* const angles = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
    const uint32_t yaw = angles[1];
    uint32_t enemy;
    const float diff = as_float(yaw) - as_float(*BroScr_GetYawToEnemy(&enemy, ent));
    float folded = static_cast<float>(static_cast<int32_t>(diff) % 360);
    const int32_t over = folded > title_global<float>(0x0083eb60) ? 1 : 0;  // 180
    if (!(static_cast<float>(over) == 0.0f)) folded = folded - title_global<float>(0x0083ebcc);  // 360
    const int32_t negative = 0.0f > folded ? 1 : 0;
    if (!(static_cast<float>(negative) == 0.0f)) *reinterpret_cast<float*>(out) = folded * title_global<float>(0x0083eb34);
    else *reinterpret_cast<float*>(out) = folded;
    return out;
}

uint32_t* XB_CDECL BroScr_GetAllowedMovePose(uint32_t* out, uint32_t actor, uint32_t pose) {
    if (pose == undefined_handle() || pose == 0) pose = actor_pose(actor);
    uint8_t in_range;
    uint32_t result;
    if (*BroScr_IsNearestEnemyInRange(&in_range, actor) || pose == kStand) {
        if (stance_allowed(actor, kStand)) result = kStand;
        else if (stance_allowed(actor, kCrouch)) result = kCrouch;
        else result = stance_allowed(actor, kProne) ? kProne : kStand;
    } else if (pose == kProne) {
        if (stance_allowed(actor, kProne)) result = kProne;
        else if (stance_allowed(actor, kCrouch)) result = kCrouch;
        else result = stance_allowed(actor, kStand) ? kStand : kProne;
    } else if (pose == kCrouch) {
        if (stance_allowed(actor, kCrouch)) result = kCrouch;
        else if (stance_allowed(actor, kStand)) result = kStand;
        else result = stance_allowed(actor, kProne) ? kProne : kCrouch;
    } else {
        result = kStand;
    }
    *out = result;
    return out;
}

void XB_CDECL BroScr_InitAnimScript(uint32_t ent, uint32_t script) {
    bool defined;
    if (*ScrField_h31aaf853_IsDefined(&defined, ent) &&
        slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(*ent_slot(ent, 0x31aaf853))) {  // GScr_IsAlive
        BroScr_MeleePair_Unlink(ent, *ScrField_h31aaf853(ent));
    }
    using ClearAnim = void XB_CDECL(uint32_t, uint32_t, uint32_t);
    using SetAnim = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    slot<ClearAnim>(0x009225d4)(ent, title_global<uint32_t>(0x008cd7dc), 0x3e99999a);  // g_anim_Body, 0.3
    slot<SetAnim>(0x009225dc)(ent, title_global<uint32_t>(0x008cd7dc), 0x3e4ccccd, 0x3e4ccccd, 0x3f800000, 0);
    const uint32_t* const previous = handle_slot(ent, 0x175567a8);
    uint32_t* const saved = handle_slot(ent, 0xd82f7174);
    *saved = *previous;
    *handle_slot(ent, 0x175567a8) = 0x0036b1a5;
    *int_slot(ent, 0xe5f891d2) = 0;
    *int_slot(ent, 0xa31c116e) = 0;
    slot<SetAnim>(0x009225dc)(ent, title_global<uint32_t>(0x008cf318), 0, 0x3e4ccccd, 0x3f800000, 0);  // g_anim_Shoot
    BroScr_DelayTimerOnScriptChange(ent, *handle_slot(ent, 0x2181000b));
    if (script != undefined_handle()) BroScr_DelayTimerOnScriptChange(ent, script);
    *handle_slot(ent, 0x2181000b) = script;
    if (*key_slot(ent, 0xe057503b, 0) != 0)
        title_function<void XB_CDECL(uint32_t)>(*key_slot(ent, 0xe057503b, 0))(ent);  // the script's start callback
}

void XB_CDECL BroScr_ApplyDesiredAnimPose(uint32_t ent) {
    bool defined;
    if (!*ScrField_desired_anim_pose_IsDefined(&defined, ent)) {
        if (actor_pose(ent) == 0x0030880b && *ScrField_anim_script(ent) != 0x0039894e &&
            *ScrField_anim_script(ent) != 0x06730ae8) {
            *ScrField_desired_anim_pose(ent) = kProne;
        } else {
            const uint32_t pose = actor_pose(ent);
            *handle_slot(ent, 0x8cb5047a) = pose;
        }
    }
    if (*handle_slot(ent, 0x8cb5047a) == kProne) {
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226b8)(
            ent, 0xc2340000, 0x42340000, title_global<uint32_t>(0x008cf12c), title_global<uint32_t>(0x008cf138),
            title_global<uint32_t>(0x008cf144));  // GScr_ActorSetPitchBlendAnims(-45, 45, down, straight, up)
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b0)(ent, 0x3f000000);
    } else {
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b4)(ent, 0x3f000000);
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, *handle_slot(ent, 0x8cb5047a));  // SetInt_184
    remove_key(&ent, 0x8cb5047a);
}

void XB_CDECL BroScr_InitAnimScriptAndPose(uint32_t ent, uint32_t script) {
    BroScr_InitAnimScript(ent, script);
    BroScr_ApplyDesiredAnimPose(ent);
}

uint8_t* XB_CDECL BroScr_IsFlamethrower(uint8_t* out, uint32_t actor) {
    uint32_t type;
    *out = *BroScr_GetWeaponType(&type, actor) == 0xe5aa1ee7;
    return out;
}
uint8_t* XB_CDECL BroScr_IsWeaponFireTypeLmg(uint8_t* out, uint32_t actor) { return is_fire_type(out, actor, 0x0001bdc7); }
uint8_t* XB_CDECL BroScr_IsWeaponFireTypeAuto(uint8_t* out, uint32_t actor) { return is_fire_type(out, actor, 0x00315aef); }
uint8_t* XB_CDECL BroScr_IsWeaponFireTypeSemi(uint8_t* out, uint32_t actor) { return is_fire_type(out, actor, 0x003819c9); }

uint32_t* XB_CDECL BroScr_GetShotInterval_2(uint32_t* out, uint32_t actor) {
    uint32_t value;
    if (*BroScr_GetWeaponFireType(&value, actor) == 0x00315aef) {
        const float rate = as_float(*BroScr_GetWeaponFireRate(&value, actor));
        *reinterpret_cast<float*>(out) = rate * title_global<float>(0x0083ebf8);  // 0.1
    } else {
        *out = title_global<uint32_t>(0x0083eca0);  // 0.2
    }
    return out;
}

uint32_t* XB_CDECL BroScr_GetShotInterval(uint32_t* out, uint32_t actor) {
    uint32_t rate;
    BroScr_GetWeaponFireRate(&rate, actor);
    uint32_t value;
    if ((*BroScr_GetWeaponFireType(&value, actor) == 0x00315aef ||
         *BroScr_GetWeaponFireType(&value, actor) == 0x0001bdc7) &&
        !(as_float(rate) == 0.0f)) {
        const float current = as_float(*BroScr_GetWeaponFireRate(&value, actor));
        *reinterpret_cast<float*>(out) = title_global<float>(0x0083eb20) / current;
        return out;
    }
    const long double random = slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3ecccccd);  // Scr_RandomFloat(0.4), ST0
    const float scale = static_cast<float>(random + static_cast<long double>(title_global<float>(0x0083ebc0)));  // + 0.8
    const float field = as_float(*BroScr_GetWeaponTableField4(&value, actor));
    *reinterpret_cast<float*>(out) = scale * field;
    return out;
}

}  // namespace game

namespace game {

using namespace bro_ai;

namespace {

// The anim-script object's aim/shoot anim words +0x3c..+0x50 from six anim handle globals.
void set_aim_anims(const uint32_t* globals) {
    for (uint32_t i = 0; i < 6; ++i) at<uint32_t>(anim_object(), 0x3c + i * 4) = title_global<uint32_t>(globals[i]);
}

const uint32_t kStandPistol[6] = {0x008cf084, 0x008cf078, 0x008cf09c, 0x008cf084, 0x008cf090, 0x008cf09c};
const uint32_t kStandFlame[6] = {0x008ce820, 0x008ce82c, 0x008ce838, 0x008ce844, 0x008ce850, 0x008ce85c};
const uint32_t kStandAuto[6] = {0x008cf384, 0x008cf390, 0x008cf3cc, 0x008cf51c, 0x008cf528, 0x008cf534};
const uint32_t kStandRifle[6] = {0x008cf384, 0x008cf390, 0x008cf3cc, 0x008cf540, 0x008cf5b8, 0x008cf5c4};
const uint32_t kCrouchPistol[6] = {0x008ced24, 0x008ced00, 0x008ced3c, 0x008ced24, 0x008ced30, 0x008ced3c};
const uint32_t kCrouchPanzer[6] = {0x008cec70, 0x008cec7c, 0x008cec88, 0x008cec94, 0x008ceca0, 0x008cecac};
const uint32_t kCrouchAuto[6] = {0x008ce514, 0x008ce520, 0x008ce52c, 0x008ce610, 0x008ce61c, 0x008ce628};
const uint32_t kCrouchRifle[6] = {0x008ce514, 0x008ce520, 0x008ce52c, 0x008ce634, 0x008ce694, 0x008ce6a0};

uint32_t actor_weapon(uint32_t actor) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(actor); }  // GetInt_f0

}  // namespace

void XB_CDECL BroScr_SetAimAnimsForWeapon(uint32_t actor) {
    uint32_t value;
    const uint32_t type = *BroScr_GetWeaponType(&value, actor);
    if (type == 0xe6a85d8c) set_aim_anims(kStandPistol);
    else if (type == 0xe5aa1ee7) set_aim_anims(kStandFlame);
    else if (*BroScr_GetWeaponFireType(&value, actor) == 0x00315aef) set_aim_anims(kStandAuto);
    else set_aim_anims(kStandRifle);
}

void XB_CDECL BroScr_SetAimAnimsForWeapon_2(uint32_t actor) {
    uint32_t value;
    const uint32_t type = *BroScr_GetWeaponType(&value, actor);
    if (type == 0xe6a85d8c) {
        set_aim_anims(kCrouchPistol);
    } else if (type == 0xa39d17dd || type == 0x45963119 || type == 0x318fbe80) {
        set_aim_anims(kCrouchPanzer);
    } else {
        const uint32_t fire = *BroScr_GetWeaponFireType(&value, actor);
        set_aim_anims(fire == 0x00315aef || fire == 0x0001bdc7 ? kCrouchAuto : kCrouchRifle);
    }
}

void XB_CDECL BroScr_RefillClip(uint32_t actor) {
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x00825f5c));  // "Self.weapon is not defined"
    actor_weapon(actor);
    ScriptString_Release(&text);
    ScriptString_FromCString(&text, pointer<const char>(0x00825f38));  // "anim.AIWeapon array is not defined"
    uint8_t in_table;
    BroScr_ActorWeaponInLevelTable(&in_table, actor);
    ScriptString_Release(&text);
    ScriptString_FromCString(&text, pointer<const char>(0x00825f1c));  // "anim.AIWeapon clip is zero"
    uint32_t clip;
    BroScr_GetWeaponClipSize(&clip, actor);
    ScriptString_Release(&text);
    const uint32_t* const size = BroScr_GetWeaponClipSize(&clip, actor);
    *int_slot(actor, 0x0feddbea) = *size;
    ScriptString_FromCString(&text, pointer<const char>(0x00825f08));  // "RefillClip failed"
    bool defined;
    ScrField_h0feddbea_IsDefined(&defined, actor);
    ScriptString_Release(&text);
}

uint32_t* XB_CDECL BroScr_GetClipSizeOrZero(uint32_t* out, uint32_t actor) {
    if (actor_weapon(actor) == 0x0036b1a5) {
        *out = 0;
        return out;
    }
    BroScr_GetWeaponClipSize(out, actor);
    return out;
}

void XB_CDECL BroScr_SetWoundedPose(uint32_t ent, ScriptString unused) {
    uint32_t pose;
    if (*handle_slot(ent, 0xb7f3f874) == 0x06088a8c) pose = 0x0738b1a5;
    else if (*handle_slot(ent, 0xb7f3f874) != 0x068698d3 && *ScrField_wounded_anim(ent) == 0x070b89a4) pose = 0x070b89a4;
    else pose = 0xc1165808;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, pose);  // ScrActor_SetInt_184
    ScriptString_Release(&unused);
}

}  // namespace game

namespace game {

using namespace bro_ai;

namespace {

// A two-entity functor thread (vtable 0x007f4a28: function, actor, turret) on `actor`.
void start_turret_thread(uint32_t function, uint32_t actor, uint32_t turret) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = function;
        functor[2] = actor;
        functor[3] = turret;
    }
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, actor,
                                                                                               functor, 0);  // Scr_CreateThread
}

uint32_t ent_by_targetname(ScriptString* name) {
    return slot<uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221c4)(
        name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray (the first match)
}

}  // namespace

void XB_CDECL BroScr_UseNodeTurret(uint32_t actor, uint32_t turret, uint32_t aim_anim) {
    using ActorUseTurret = void XB_CDECL(uint32_t, uint32_t);
    using SetMode = void XB_CDECL(uint32_t, uint32_t);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(
        actor, aim_anim ? aim_anim : title_global<uint32_t>(0x008cf630));  // ScrActor_SetInt_1370 (g_anim_standmg42gunner_aim)
    slot<ActorUseTurret>(0x0092272c)(actor, turret);  // GScr_ActorUseTurret
    slot<SetMode>(0x00922584)(turret, 0xad76f051);    // GScr_Turret_SetMode
    ScriptString target_a, targetname, target_c, target_d, classname;
    bool target_entity = false;
    uint32_t live = 1;
    if (ScrCall_ScrEnt_GetTarget(&turret, &target_a)->rep) {
        ScriptString* const name = ScrCall_ScrEnt_GetTargetname(&turret, &targetname);
        ScriptString* const target = ScrCall_ScrEnt_GetTarget(&turret, &target_c);
        live = 7;
        if (!ScriptString_EqualsString(target, name)) {
            uint32_t ent = ent_by_targetname(ScrCall_ScrEnt_GetTarget(&turret, &target_d));
            live = 0x1f;
            target_entity = ScriptString_Equals(ScrCall_ScrEnt_GetClassname(&ent, &classname),
                                                pointer<const char>(0x0083ab88));  // "script_origin"
        }
    }
    if (live & 0x10) ScriptString_Release(&classname);
    if (live & 0x8) ScriptString_Release(&target_d);
    if (live & 0x4) ScriptString_Release(&target_c);
    if (live & 0x2) ScriptString_Release(&targetname);
    if (live & 0x1) ScriptString_Release(&target_a);
    if (target_entity) {
        ScriptString target;
        const uint32_t ent = ent_by_targetname(ScrCall_ScrEnt_GetTarget(&turret, &target));
        ScriptString_Release(&target);
        *key_slot(turret, 0xcf1e52a5, 0) = ent;
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922590)(turret, ent);  // GScr_Turret_SetTargetEntity
        slot<SetMode>(0x00922584)(turret, 0x6c271e5a);
        start_turret_thread(own_address(&BroScr_TurretAutoAiOnNotify), actor, turret);  // BroScr_TurretAutoAiOnNotify
        bool defined;
        ScrField_hbe5f9a9f_IsDefined(&defined, actor);
    }
    start_turret_thread(own_address(&BroScr_TurretManualTargetThink), actor, turret);  // BroScr_TurretManualTargetThink
    queue_notify(turret, 0xcb6a4b2a);
    slot<ActorUseTurret>(0x0092272c)(actor, turret);
}

}  // namespace game

// Push c3: weapon/reload checks, aim blends, cover anim sets, run anims, the gun hand.
namespace game {

using namespace bro_ai;

namespace {

// ScriptKeyTable_GetString (0x00458da0): the entity's string under `key`, added empty when missing.
ScriptString* key_string(uint32_t ent, uint32_t key) {
    return title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(ent_script_object(ent), key);
}

// ScriptKeyTable_GetStruct28 (0x001e4a20) of the anim struct key 0x12993a44.
uint32_t* anim_struct(uint32_t ent) {
    return pointer<uint32_t>(ScriptKeyTable_GetStruct28(ent_script_object(ent), 0x12993a44));
}

long double random_float(uint32_t limit_bits) { return slot<long double XB_CDECL(uint32_t)>(0x00922230)(limit_bits); }

uint32_t cover_anim_set(uint32_t actor, const uint32_t (&sets)[7]) {
    uint32_t value;
    const uint32_t type = *BroScr_GetWeaponType(&value, actor);
    if (type == 0xa39d17dd) return sets[0];
    if (type == 0xe6a85d8c) return actor_pose(actor) == 0x070b89a4 ? sets[1] : sets[2];
    if (*ScrField_anim_idleset(actor) == 0x61) return actor_pose(actor) == 0x070b89a4 ? sets[3] : sets[4];
    return actor_pose(actor) == 0x070b89a4 ? sets[5] : sets[6];
}

constexpr uint32_t kCoverLeftSets[7] = {0x009232b8, 0x00923188, 0x00923350, 0x00923220, 0x00923480, 0x009235b0, 0x009233e8};
constexpr uint32_t kCoverRightSets[7] = {0x00923518, 0x00923648, 0x00922f08, 0x009230f0, 0x00923058, 0x00922fc0, 0x00923708};

// A hand tag string ("tag_weapon_left" / "tag_weapon_right").
const char* hand_tag(uint32_t hand) {
    return pointer<const char>(hand == 0x003798b4 ? 0x00831e28 : 0x00831e14);
}

}  // namespace

uint32_t* XB_CDECL BroScr_GetFieldUnlessAutoOrSemi(uint32_t* out, uint32_t actor) {
    uint32_t type;
    if (*BroScr_GetWeaponFireType(&type, actor) == 0x00315aef || *BroScr_GetWeaponFireType(&type, actor) == 0x003819c9) {
        *out = 0;
        return out;
    }
    *out = *ScrField_hc786fa48(actor);
    return out;
}

uint8_t* XB_CDECL BroScr_NeedsReload(uint8_t* out, uint32_t actor, uint32_t fraction_bits) {
    float fraction = as_float(fraction_bits);
    if (fraction == as_float(title_global<uint32_t>(0x00922eac))) fraction = 0.0f;
    if (*handle_slot(actor, 0x0ccee5c1) == 0x0001c6ce) {
        *out = 0;
        return out;
    }
    const int32_t left = static_cast<int32_t>(*int_slot(actor, 0x0feddbea));
    int32_t clip;
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(actor) == 0x0036b1a5) {  // ScrActor_GetInt_f0
        clip = 0;
    } else {
        uint32_t size;
        clip = static_cast<int32_t>(*BroScr_GetWeaponClipSize(&size, actor));
    }
    const int32_t threshold = static_cast<int32_t>(static_cast<float>(clip) * fraction);
    *out = left > threshold ? 0 : 1;
    return out;
}

void XB_CDECL BroScr_BlendAimAnimsByYaw(uint32_t ent, uint32_t anim_a, uint32_t anim_b, uint32_t anim_c, uint32_t a_bits,
                                       uint32_t b_bits, uint32_t c_bits, uint32_t base_bits) {
    const uint32_t floor_bits = title_global<uint32_t>(0x0083eb94);  // 0.01
    uint32_t yaw_bits;
    BroScr_GetYawToEnemy(&yaw_bits, ent);
    const float full = title_global<float>(0x0083ebcc);  // 360
    const int32_t whole = static_cast<int32_t>(platform_addss(as_float(base_bits) - as_float(yaw_bits), full));
    float yaw = static_cast<float>(whole % 360);
    if (yaw > title_global<float>(0x0083eb60)) yaw = yaw - full;  // 180
    const float a = as_float(a_bits);
    const float b = as_float(b_bits);
    const float c = as_float(c_bits);
    const float floor = as_float(floor_bits);
    const float ceiling = title_global<float>(0x0083ed08);  // 0.99
    const float one = title_global<float>(0x0083eb20);
    uint32_t weight_a;
    uint32_t weight_b;
    uint32_t weight_c;
    if (a >= yaw) {
        weight_a = bits(one);
        weight_b = floor_bits;
        weight_c = floor_bits;
    } else if (b > yaw) {
        const float span = b - a;
        float t = (b - yaw) / span;
        if (floor > t) t = floor;
        if (t > ceiling) t = ceiling;
        weight_a = bits(t);
        weight_b = bits(one - t);
        weight_c = floor_bits;
    } else if (c > yaw) {
        const float span = c - b;
        float t = (c - yaw) / span;
        if (floor > t) t = floor;
        if (t > ceiling) t = ceiling;
        weight_a = floor_bits;
        weight_b = bits(t);
        weight_c = bits(one - t);
    } else {
        weight_a = floor_bits;
        weight_b = floor_bits;
        weight_c = bits(one);
    }
    using SetAnim = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    slot<SetAnim>(0x009225dc)(ent, anim_a, weight_a, 0x3dcccccd, 0x3f800000, 0);  // GScr_SetAnim (0.1, 1.0)
    slot<SetAnim>(0x009225dc)(ent, anim_b, weight_b, 0x3dcccccd, 0x3f800000, 0);
    slot<SetAnim>(0x009225dc)(ent, anim_c, weight_c, 0x3dcccccd, 0x3f800000, 0);
}

uint32_t XB_CDECL BroScr_GetCoverLeftAnimSet(uint32_t actor) { return cover_anim_set(actor, kCoverLeftSets); }
uint32_t XB_CDECL BroScr_GetCoverRightAnimSet(uint32_t actor) { return cover_anim_set(actor, kCoverRightSets); }

void XB_CDECL BroScr_AnimScript_GrenadeRecover(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x07b4e7f7);  // Scr_EndOn
    BroScr_InitAnimScript(ent, 0x5f32132f);
    BroScr_ApplyDesiredAnimPose(ent);
    own_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(&BroScr_SetPoseMovement)(ent, 0xc1165808, 0x00385d90);  // BroScr_SetPoseMovement
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922e2c)(ent, 0);  // ScrSentient_SetInt_38
}

void XB_CDECL BroScr_GetCombatRunAnim(uint32_t actor) {
    uint32_t value;
    const uint32_t type = *BroScr_GetWeaponType(&value, actor);
    if (type == 0xe6a85d8c || type == 0x0036b1a5) {
        *key_slot(actor, 0x66a44409, 0) = title_global<uint32_t>(0x008cdaa0);  // g_anim_combatrun_forward_1
        *key_slot(actor, 0x682ea6e9, 0) = title_global<uint32_t>(0x008ced18);  // g_anim_pistol_crouchrun_loop_forward_1
    } else if (type == 0x0001bdc7) {
        *ScrField_anim_combatrunanim(actor) = title_global<uint32_t>(0x008cd80c);  // g_anim_c_mg_mg34_run
        *ScrField_anim_crouchrunanim(actor) = title_global<uint32_t>(0x008ce718);  // g_anim_crouchrun_loop_forward_1
        *ScrField_h93348a05(actor) = title_global<uint32_t>(0x008cd80c);
    } else {
        *ScrField_anim_combatrunanim(actor) = title_global<uint32_t>(0x008cdaa0);
        *ScrField_anim_crouchrunanim(actor) = title_global<uint32_t>(0x008ce718);
    }
    bool defined;
    if (!*ScrField_h6937c3b9_IsDefined(&defined, actor)) {
        uint32_t* const field = ScrField_h6937c3b9(actor);
        const long double r = random_float(0x3ecccccd) + static_cast<long double>(title_global<float>(0x0083ebc0));  // 0.4, 0.8
        *reinterpret_cast<float*>(field) = static_cast<float>(r);
    }
    if (*ScrField_haff9c3b8_IsDefined(&defined, actor)) return;
    if (!entity_handle_is_valid(actor)) return;
    ScriptString team;
    ScriptString team2;
    bool axis = true;
    const bool has_team = ScrCall_ScrSentient_GetTeam(&actor, &team)->rep != nullptr;
    if (has_team) {
        axis = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&actor, &team2), pointer<const char>(0x0083208c));  // "axis"
        ScriptString_Release(&team2);
    }
    ScriptString_Release(&team);
    uint32_t* const field = ScrField_haff9c3b8(actor);
    const long double offset = static_cast<long double>(title_global<float>(axis ? 0x0083ebc0 : 0x0083ec18));  // 0.8 / 0.9
    *reinterpret_cast<float*>(field) = static_cast<float>(random_float(0x3ecccccd) + offset);
}

void XB_CDECL BroScr_ResetAnimStruct_12993a44(uint32_t ent) {
    anim_struct(ent)[0] = 0;
    const uint32_t undefined = undefined_int();
    anim_struct(ent)[7] = undefined;
    uint32_t* const s = anim_struct(ent);
    if (s[8]) {
        ScriptStringRep_Release(pointer<ScriptStringRep>(s[8]));
        s[8] = 0;
    }
    const uint32_t undefined2 = undefined_int();
    anim_struct(ent)[9] = undefined2;
}

void XB_CDECL BroScr_SetAnimStruct_12993a44(uint32_t ent, const uint32_t* source) {
    anim_struct(ent)[0] = source[0];
    uint32_t* s = anim_struct(ent);
    s[1] = source[1];
    s[2] = source[2];
    s[3] = source[3];
    s = anim_struct(ent);
    s[4] = source[4];
    s[5] = source[5];
    s[6] = source[6];
    anim_struct(ent)[7] = source[7];
    if (source[8]) {
        ScriptString_Assign(reinterpret_cast<ScriptString*>(anim_struct(ent) + 8),
                            reinterpret_cast<const ScriptString*>(source + 8));
    } else {
        ScriptString_AssignCString(reinterpret_cast<ScriptString*>(anim_struct(ent) + 8), pointer<const char>(0x0082d25c));
    }
    if (source[9] == 0xfefefefe) {
        const uint32_t body = title_global<uint32_t>(0x008cd7dc);  // g_anim_Body
        anim_struct(ent)[9] = body;
    } else {
        anim_struct(ent)[9] = source[9];
    }
}

uint8_t* XB_CDECL BroScr_MeleePair_Relink(uint8_t* out, uint32_t a, uint32_t b) {
    if (!entity_handle_is_valid(b)) {
        *out = 0;
        return out;
    }
    bool defined;
    if (*ScrField_h31aaf853_IsDefined(&defined, a)) {
        BroScr_MeleePair_Unlink(a, *ScrField_h31aaf853(a));
        if (*ScrField_h31aaf853_IsDefined(&defined, a)) drop_bad_parameter();
    }
    if (*ScrField_h236fc839_IsDefined(&defined, b)) {
        const uint32_t partner = *ScrField_h236fc839(b);
        if (*ScrField_h31aaf853_IsDefined(&defined, partner) && *ScrField_h31aaf853(partner) == b) {
            *out = 0;
            return out;
        }
        remove_key(&b, 0x236fc839);
    }
    if (*ScrField_h31aaf853_IsDefined(&defined, a)) drop_bad_parameter();
    if (*ScrField_h236fc839_IsDefined(&defined, b)) drop_bad_parameter();
    *out = 1;
    return out;
}

void XB_CDECL BroScr_PutGunInHand(uint32_t actor, uint32_t hand) {
    using GetInt = uint32_t XB_CDECL(uint32_t);
    ScriptString weapon;
    ScriptString_FromCString(&weapon, pointer<const char>(0x0082dfeb));  // ""
    if (slot<GetInt>(0x00922d30)(actor) != 0) {  // ScrActor_GetInt_f0
        ScriptString name;
        ScriptString_Assign(&weapon, ScrCall_GScr_GetWeaponDisplayName(&name, slot<GetInt>(0x00922d30)(actor)));
        ScriptString_Release(&name);
    }
    bool defined;
    uint32_t wanted = 0x0036b1a5;
    if (!*ScrField_hasweapon_IsDefined(&defined, actor) || *int_slot(actor, 0x2861269a) != 0) {
        if (weapon.rep && *reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(weapon.rep) + 2) != 0 &&
            !ScriptString_Equals(&weapon, pointer<const char>(0x0082dfeb)))
            wanted = hand;
    }
    if (!*ScrField_h81ea0dd3_IsDefined(&defined, actor))
        ScriptString_AssignCString(key_string(actor, 0x81ea0dd3), pointer<const char>(0x00832430));  // "none"
    if (wanted == *handle_slot(actor, 0x1de0c645) && ScriptString_EqualsString(&weapon, key_string(actor, 0x81ea0dd3))) {
        ScriptString_Release(&weapon);
        return;
    }
    const int32_t now = get_time();
    if (static_cast<int32_t>(*int_slot(actor, 0x77f1e6fa)) != now) *int_slot(actor, 0x77f1e6fa) = static_cast<uint32_t>(now);
    using Attach = void XB_CDECL(uint32_t, ScriptString*, ScriptString*);
    using Detach = void XB_CDECL(uint32_t, ScriptString*, ScriptString*, uint32_t);
    const uint32_t held = *handle_slot(actor, 0x1de0c645);
    if (held == 0x003798b4 || *handle_slot(actor, 0x1de0c645) == 0x07151174) {
        ScriptString tag;
        ScriptString_FromCString(&tag, hand_tag(held));
        slot<Attach>(0x009224d8)(actor, key_string(actor, 0x81ea0dd3), &tag);  // GScr_Detach
        ScriptString_Release(&tag);
    }
    if (wanted == 0x003798b4 || wanted == 0x07151174) {
        ScriptString tag;
        ScriptString_FromCString(&tag, hand_tag(wanted));
        slot<Detach>(0x009224d0)(actor, &weapon, &tag, 0);  // GScr_AttachWithOwner
        ScriptString_Release(&tag);
        ScriptString_Assign(key_string(actor, 0x81ea0dd3), &weapon);
        if (wanted == 0x003798b4) *handle_slot(actor, 0x1de0c645) = 0x003798b4;
        else *ScrField_anim_gunhand(actor) = 0x07151174;
    } else {
        *ScrField_anim_gunhand(actor) = 0x0036b1a5;
        remove_key(&actor, 0x81ea0dd3);
    }
    BroScr_GetCombatRunAnim(actor);
    ScriptString_Release(&weapon);
}

void XB_CDECL BroScr_ReattachGunInHand(uint32_t actor) {
    slot<void XB_CDECL(uint32_t)>(0x009224e0)(actor);  // GScr_DetachAll
    const uint32_t hand = *handle_slot(actor, 0x1de0c645);
    if (hand == undefined_handle()) return;
    *handle_slot(actor, 0x1de0c645) = 0x0036b1a5;
    BroScr_PutGunInHand(actor, hand);
}

void XB_CDECL BroScr_ReattachAlliesGuns() {
    int32_t budget = 100000;
    DynArray ai = {nullptr, 0, 0};
    ScriptString team;
    ScriptString_FromCString(&team, pointer<const char>(0x00832084));  // "allies"
    slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
    ScriptString_Release(&team);
    int32_t ai_left = static_cast<int32_t>(ai.size);
    DynArray spawners = {nullptr, 0, 0};
    ScriptString team2;
    ScriptString_FromCString(&team2, pointer<const char>(0x00832084));
    slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223dc)(&spawners, &team2);  // GScr_GetSpawnerTeamArray
    ScriptString_Release(&team2);
    int32_t spawners_left = static_cast<int32_t>(spawners.size);
    uint32_t ai_index = 0;
    uint32_t spawner_index = 0;
    auto at_index = [](DynArray* array, uint32_t index) {
        if (index >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), index + 1, index + 4);
        return pointer<uint32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(array->data)) + index * 4);
    };
    // Whether the entity's field 0xa1763cc1 (a string) is set.
    auto has_field = [](uint32_t ent) {
        ScriptKeyTable* table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(ent);
        if (!table) table = pointer<ScriptKeyTable>(0x00922e94);
        ScriptString copy;
        const bool set = static_cast<ScriptString*>(ScrField_ha1763cc1_Copy(reinterpret_cast<uint8_t*>(table) + 0xc, &copy))
                             ->rep != nullptr;
        ScriptString_Release(&copy);
        return set;
    };
    while ((ai_left > 0 || spawners_left > 0) && budget > 0) {
        if (ai_left > 0) {
            if (!has_field(*at_index(&ai, ai_index))) {
                BroScr_ReattachGunInHand(*at_index(&ai, ai_index));
                --budget;
            }
            --ai_left;
            ++ai_index;
        } else {
            const bool set = has_field(*at_index(&spawners, spawner_index));
            --spawners_left;
            ++spawner_index;
            if (!set) --budget;
        }
    }
    if (spawners.data) Scr_Free(spawners.data);
    if (ai.data) Scr_Free(ai.data);
}

uint8_t* XB_CDECL BroScr_AnimMoveUntilObstacle(uint8_t* out, uint32_t actor, uint32_t seconds_bits) {
    const int32_t frames = static_cast<int32_t>(as_float(seconds_bits) * title_global<float>(0x0083eb5c));  // x 20
    using EntVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    using Track = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    for (int32_t i = 0; i < frames; ++i) {
        slot<void XB_CDECL(uint32_t)>(0x00922760)(actor);  // GScr_ActorAdvanceAnimTime
        slot<void XB_CDECL(uint32_t)>(0x0092275c)(actor);  // GScr_ActorApplyAnimMove
        uint32_t angles_buffer[3];
        const uint32_t* const a = slot<EntVec>(0x00922ba0)(angles_buffer, actor);  // ScrEnt_GetAngles
        const uint32_t angles[3] = {a[0], a[1], a[2]};
        uint32_t origin_buffer[3];
        const uint32_t* const o = slot<EntVec>(0x00922b20)(origin_buffer, actor);  // ScrEnt_GetOrigin
        title_function<Track>(0x001c2aa0)(actor, 4, o[0], o[1], o[2], angles[0], angles[1], angles[2]);  // FUN_001c2aa0
        ScriptString kind;
        ScriptString_FromRep(&kind, nullptr);
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x00922764)(actor, &kind);  // GScr_ActorGetObstacleKind_39c
        uint32_t hash;
        const uint32_t kind_hash = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&kind));
        if (kind_hash == 0x074645e4) {
            ScriptString mode;
            ScriptString_FromCString(&mode, pointer<const char>(0x0083adfc));  // "face angle"
            const float yaw = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922768)(actor));  // ObstacleYaw_3a8
            slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(actor, &mode, bits(yaw), 0);  // GScr_ActorSetOrientMode
            ScriptString_Release(&mode);
            *out = 1;
            ScriptString_Release(&kind);
            return out;
        }
        if (kind_hash == 0x61710285) {
            *out = 0;
            ScriptString_Release(&kind);
            return out;
        }
        ScriptString_Release(&kind);
    }
    *out = 0;
    return out;
}

void XB_CDECL BroScr_InitPoseMovementTable() {
    // anim object +0x168..+0x188: stand / crouch / prone x stop / walk / run handlers.
    static const uint32_t kHandlers[9] = {own_address(&BroScr_PoseMove_StandStop), own_address(&BroScr_PoseMove_StandWalk), own_address(&BroScr_PoseMove_StandRun), own_address(&BroScr_PoseMove_CrouchStop), own_address(&BroScr_PoseMove_CrouchWalk),
                                          own_address(&BroScr_PoseMove_CrouchRun), own_address(&BroScr_PoseMove_ProneStop), own_address(&BroScr_PoseMove_ProneWalk), own_address(&BroScr_PoseMove_ProneRun)};
    for (uint32_t i = 0; i < 9; ++i) at<uint32_t>(anim_object(), 0x168 + i * 4) = kHandlers[i];
}

void XB_CDECL BroScr_SetPoseMovement(uint32_t ent, uint32_t pose, uint32_t movement) {
    // The original uses ent's argument slot as scratch (the IsDefined byte, the two dropped strings).
    *int_slot(ent, 0x521b4bf0) = static_cast<uint32_t>(get_time());
    *handle_slot(ent, 0x6f631b0e) = actor_pose(ent);
    {
        const uint32_t* const from = handle_slot(ent, 0x0ccee5c1);
        *handle_slot(ent, 0xbad1ef6d) = *from;
    }
    bool defined;
    if (*ScrField_wounded_anim_IsDefined(&defined, ent)) {
        const uint32_t* const from = handle_slot(ent, 0xb7f3f874);  // wounded_anim
        *handle_slot(ent, 0x4de32b74) = *from;
    } else {
        *handle_slot(ent, 0x4de32b74) = 0;
    }
    {
        const uint32_t* const from = handle_slot(ent, 0x175567a8);
        *handle_slot(ent, 0x72e59ffd) = *from;
    }
    *handle_slot(ent, 0xdce273fb) = pose;
    if (movement == undefined_handle() || movement == 0) movement = *handle_slot(ent, 0x0ccee5c1);
    {
        ScriptString text;
        ScriptString_FromCString(&text, pointer<const char>(0x00825ed4));  // "bad input" (dropped)
        ScriptString_Release(&text);
    }
    if (movement != undefined_handle())
        *handle_slot(ent, 0x358ad77b) = movement;
    else
        *handle_slot(ent, 0x358ad77b) = 0;
    if (actor_pose(ent) == 0x19669cd9) {
        ScriptString text;
        ScriptString_FromCString(&text, pointer<const char>(0x00825ebc));  // "crap_out_now + please" (dropped)
        ScriptString_Release(&text);
    }
    if (pose == 0) {
        // The script compares the movement hash with the "walk" / "run" string addresses (never equal
        // for the hashes the callers pass), so a prone actor keeps prone here.
        if (actor_pose(ent) == 0x0738b1a5 && (movement == 0x00825eb4 || movement == 0x00825eb0))
            pose = 0xc1165808;
        else
            pose = actor_pose(ent);
    }
    // Handler index: stand 0 / crouch 3 / prone 6 (else the undefined int) + stop 0 / walk 1 / run 2;
    // out of range (unsigned) takes the last entry.
    uint32_t index = undefined_int();
    if (pose == 0x070b89a4)
        index = 0;
    else if (pose == 0xc1165808)
        index = 3;
    else if (pose == 0x0738b1a5)
        index = 6;
    if (movement == 0x00385d90) {
    } else if (movement == 0x003a09eb) {
        ++index;
    } else if (movement == 0x0001c6ce) {
        index += 2;
    }
    const uint32_t handler = at<uint32_t>(anim_object(), index < 9 ? 0x168 + index * 4 : 0x188);
    title_function<void XB_CDECL(uint32_t)>(handler)(ent);
    *int_slot(ent, 0x521b4bf0) = static_cast<uint32_t>(get_time());
    *handle_slot(ent, 0xcbffac0d) = actor_pose(ent);
    const uint32_t* const from = handle_slot(ent, 0x0ccee5c1);
    *handle_slot(ent, 0xcaeba6a6) = *from;
}

}  // namespace game

