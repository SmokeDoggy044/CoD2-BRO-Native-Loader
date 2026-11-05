#include "bro_scr_ai_anim.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_nodes_spawner.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

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
    title_function<void XB_THISCALL(const uint32_t*, uint32_t)>(0x0011eb30)(ent, key);
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
        start_turret_thread(0x001817b0, actor, turret);  // BroScr_TurretAutoAiOnNotify
        bool defined;
        ScrField_hbe5f9a9f_IsDefined(&defined, actor);
    }
    start_turret_thread(0x001a4f40, actor, turret);  // BroScr_TurretManualTargetThink
    queue_notify(turret, 0xcb6a4b2a);
    slot<ActorUseTurret>(0x0092272c)(actor, turret);
}

}  // namespace game
