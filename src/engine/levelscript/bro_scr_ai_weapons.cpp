#include "bro_scr_ai_weapons.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_cover_turret.h"
#include "bro_scr_nodes_spawner.h"
#include "scr_access.h"

#include "g_scr_runtime.h"
#include "qcommon_fs.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_actor.h"

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

// An engine call through the interface block (pScr_* slot).
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

uint8_t* anim_object() { return title_global<uint8_t*>(0x00922efc); }  // the anim scripts' object
uint32_t undefined_int() { return title_global<uint32_t>(0x00922ec0); }       // g_scrUndefinedInt
uint32_t undefined_handle() { return title_global<uint32_t>(0x00922eb4); }    // g_scrUndefinedHandle

uint32_t entity_handle_is_valid(uint32_t handle) {
    return slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(handle);
}
int32_t get_time() { return slot<int32_t XB_CDECL()>(0x009223c4)(); }  // GScr_GetTime
int32_t random_int(int32_t max) { return slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(max); }
uint32_t actor_weapon(uint32_t actor) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(actor); }  // GetInt_f0

// The entity's script key table (ScrCall_GScr_GetEntScriptObject), the shared empty one without.
ScriptKeyTable* ent_script_object(uint32_t ent) {
    ScriptKeyTable* table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
    return table ? table : pointer<ScriptKeyTable>(0x00922e94);
}

// The value under `key` in the entity's table, added as `missing` first.
uint32_t* key_slot(uint32_t ent, uint32_t key, uint32_t missing) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) value = ScriptKeyTable_Set(table, key, missing);
    return value;
}

// The weapon table entry of `weapon`, or null.
const uint32_t* find_weapon(uint32_t weapon) {
    uint8_t* const obj = anim_object();
    uint32_t entry = at<uint32_t>(obj, 0x130);
    const uint32_t end = entry + at<uint32_t>(obj, 0x138) * 0x18;
    for (; entry != end; entry += 0x18)
        if (at<uint32_t>(pointer<void>(entry), 0) == weapon) break;
    if (entry == at<uint32_t>(obj, 0x130) + at<uint32_t>(obj, 0x138) * 0x18) return nullptr;
    return pointer<const uint32_t>(entry);
}

// The table entry of the actor's weapon, or null (no weapon, or not in the table).
const uint32_t* weapon_entry(uint32_t actor) {
    if (actor_weapon(actor) == undefined_handle()) return nullptr;
    return find_weapon(actor_weapon(actor));
}

uint32_t* weapon_field(uint32_t* out, uint32_t actor, uint32_t index, uint32_t missing) {
    const uint32_t* const entry = weapon_entry(actor);
    *out = entry ? entry[index] : missing;
    return out;
}

// The "bad parameter" assert string, built and dropped (the assert itself is compiled out).
void drop_bad_parameter() {
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x00825ee0));
    ScriptString_Release(&text);
}

}  // namespace

void XB_CDECL BroScr_InitScriptRadius(uint32_t ent) {
    bool defined;
    uint32_t radius;
    if (*ScrField_script_radius_IsDefined(&defined, ent)) {
        const int32_t value = static_cast<int32_t>(*key_slot(ent, 0x2266f963, undefined_int()));
        if (static_cast<uint32_t>(value) == undefined_int()) {
            radius = title_global<uint32_t>(0x00922eac);  // g_scrUndefinedFloat
        } else {
            const float f = static_cast<float>(value);
            radius = *reinterpret_cast<const uint32_t*>(&f);
        }
    } else {
        radius = title_global<uint32_t>(0x0083ebe8);  // 128.0f
    }
    const int32_t damage_a = static_cast<int32_t>(*key_slot(ent, 0x0e625b35, undefined_int()));
    const int32_t damage_b = static_cast<int32_t>(*key_slot(ent, 0x0e625b35, undefined_int()));
    uint32_t origin_out[3];
    const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin_out, ent);  // GetOrigin
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    const float min_damage = static_cast<float>(damage_a);
    const float max_damage = static_cast<float>(damage_b);
    slot<void XB_CDECL(const uint32_t*, uint32_t, float, float)>(0x00922428)(origin, radius, max_damage,
                                                                           min_damage);  // GScr_RadiusDamage
}

uint8_t* XB_CDECL BroScr_IsNearestEnemyInRange(uint8_t* out, uint32_t sentient) {
    const float nearest = slot<float XB_CDECL(uint32_t)>(0x00922690)(sentient);  // SentientNearestEnemyDist
    uint32_t distance_bits = *reinterpret_cast<const uint32_t*>(&nearest);
    if (nearest == title_global<float>(0x00922eac)) distance_bits = title_global<uint32_t>(0x0083ebec);  // 500
    const uint32_t range_bits = at<uint32_t>(anim_object(), 0x198);
    const int32_t inside =
        *reinterpret_cast<const float*>(&range_bits) > *reinterpret_cast<const float*>(&distance_bits) ? 1 : 0;
    *out = !(static_cast<float>(inside) == title_global<float>(0x0083eafc));
    return out;
}

void XB_CDECL BroScr_AnimFaceResetKeys(uint32_t ent) {
    bool defined;
    if (*ScrField_h03375c37_IsDefined(&defined, ent)) return;
    *key_slot(ent, 0x03375c37, undefined_int()) = 0;
    const ScriptString* const face = &at<ScriptString>(anim_object(), 0x98);
    ScriptString* const target = title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(
        ent_script_object(ent), 0xf8cb7452);  // ScriptKeyTable_GetString
    ScriptString_Assign(target, face);
    *key_slot(ent, 0x257acba4, undefined_int()) = 0;
}

void XB_CDECL BroScr_StopLookingAt(uint32_t actor) {
    bool defined;
    uint32_t rate = 0x447a0000;  // 1000.0f
    if (*ScrField_h5e1a2a0c_IsDefined(&defined, actor)) {
        const float scaled =
            static_cast<float>(static_cast<int32_t>(*key_slot(actor, 0x5e1a2a0c, undefined_int()))) *
            title_global<float>(0x0083eb58);  // 0.75
        rate = *reinterpret_cast<const uint32_t*>(&scaled);
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226cc)(actor, rate);       // GScr_ActorStopLookAt
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(actor, 0xe099bb44);  // Scr_QueueNotify
}

void XB_CDECL BroScr_StopLookingAt_2(uint32_t actor) { BroScr_StopLookingAt(actor); }

void XB_CDECL BroScr_SetCombatDelay(uint32_t ent, int32_t seconds) {
    int32_t until;
    if (static_cast<uint32_t>(seconds) == undefined_int()) {
        const int32_t spread = at<int32_t>(anim_object(), 0x8);
        const int32_t base = at<int32_t>(anim_object(), 0x18c) + get_time();
        until = random_int(spread) + base;
    } else {
        const int32_t delay = seconds * 1000;
        until = delay + get_time();
    }
    *key_slot(ent, 0x900ed884, undefined_int()) = static_cast<uint32_t>(until);
}

void XB_CDECL BroScr_UpdateWoundedTimeDefaults(uint32_t ent, uint32_t anim, int32_t time) {
    if (anim == undefined_handle()) anim = 0x0776460f;
    if (static_cast<uint32_t>(time) == undefined_int()) time = 0;
    BroScr_UpdateWoundedTime(ent, anim, time);
}

void XB_CDECL BroScr_AddWeaponClassEntry(uint32_t, ScriptString weapon, ScriptString type, ScriptString fire_type,
                                         uint32_t fire_rate, uint32_t field4, uint32_t clip_size) {
    uint32_t hash;
    uint32_t entry[6];
    entry[0] = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&weapon));
    entry[1] = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&type));
    entry[2] = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&fire_type));
    entry[3] = fire_rate;
    entry[4] = field4;
    entry[5] = clip_size;
    DynArray_PushBack24_2(&at<DynArray>(anim_object(), 0x130), entry);
    ScriptString_Release(&weapon);
    ScriptString_Release(&type);
    ScriptString_Release(&fire_type);
}

namespace {

inline uint32_t word_at(uint32_t address) { return title_global<uint32_t>(address); }
inline uint32_t word_ea4() { return title_global<uint32_t>(0x00922ea4); }

// One BroScr_AddWeaponClassEntry call: the by-value strings made fire type first, as the original.
inline void add_weapon_class(uint32_t ent, uint32_t weapon, uint32_t type, uint32_t fire_type, uint32_t fire_rate,
                             uint32_t shot_delay, uint32_t clip_size) {
    ScriptString fire;
    ScriptString_FromCString(&fire, pointer<const char>(fire_type));
    ScriptString kind;
    ScriptString_FromCString(&kind, pointer<const char>(type));
    ScriptString name;
    ScriptString_FromCString(&name, pointer<const char>(weapon));
    BroScr_AddWeaponClassEntry(ent, name, kind, fire, fire_rate, shot_delay, clip_size);
}

}  // namespace

// weapon: class, fire type, the rate (Auto, lmg) or the delay (semi, bolt) float; the other one is
// the word at 0x00922ea4.
void XB_CDECL BroScr_WeaponClassFor(uint32_t ent) {
    add_weapon_class(ent, 0x00832430, 0x0082b6a4, 0x00825fd8, word_at(0x0083ed78), word_ea4(), 30);  // none: rifle, Auto, 8
    add_weapon_class(ent, 0x00800b00, 0x0082b698, 0x00825fd0, word_ea4(), word_at(0x0083eca0), 8);  // p38: pistol, semi, 0.2
    add_weapon_class(ent, 0x00824808, 0x0082b6a4, 0x00825fc8, word_ea4(), word_at(0x0083ec44), 5);  // kar98: rifle, bolt, 0.4
    add_weapon_class(ent, 0x008247fc, 0x0082b6a4, 0x00825fd0, word_ea4(), word_at(0x0083ec44), 10);  // gewehr43: rifle, semi, 0.4
    add_weapon_class(ent, 0x008247f4, 0x0082b6a4, 0x00825fd8, word_at(0x007f9780), word_ea4(), 32);  // mp40: rifle, Auto, 8.5
    add_weapon_class(ent, 0x008247ec, 0x0082b6a4, 0x00825fd8, word_at(0x0083ed78), word_ea4(), 30);  // mp44: rifle, Auto, 8
    add_weapon_class(ent, 0x008244f4, 0x0082b6a4, 0x00825fc8, word_ea4(), word_at(0x0083eaf4), 5);  // kar98_sniper: rifle, bolt, 1.5
    add_weapon_class(ent, 0x00825fb4, 0x0082b6a4, 0x00825fc8, word_ea4(), word_at(0x0083eaf4), 5);  // kar98_supersniper: rifle, bolt, 1.5
    add_weapon_class(ent, 0x00824750, 0x00824750, 0x00825fc8, word_ea4(), word_at(0x0083ec9c), 1);  // panzerfaust: panzerfaust, bolt, 3
    add_weapon_class(ent, 0x00825fa4, 0x00825fa4, 0x00825fc8, word_ea4(), word_at(0x0083ece8), 1);  // panzerschreck: panzerschreck, bolt, 5
    add_weapon_class(ent, 0x008247e4, 0x008367f4, 0x008367f4, word_at(0x0083eb98), word_ea4(), 150);  // mg34: lmg, lmg, 10
    add_weapon_class(ent, 0x00824810, 0x0082b698, 0x00825fd0, word_ea4(), word_at(0x0083ec24), 7);  // colt: pistol, semi, 0.25
    add_weapon_class(ent, 0x0081f5b0, 0x0082b6a4, 0x00825fd0, word_ea4(), word_at(0x0083ec24), 15);  // m1carbine: rifle, semi, 0.25
    add_weapon_class(ent, 0x007f88c0, 0x0082b6a4, 0x00825fd0, word_ea4(), word_at(0x007c6490), 8);  // m1garand: rifle, semi, 0.3
    add_weapon_class(ent, 0x007f88cc, 0x0082b6a4, 0x00825fd8, word_at(0x007f4d98), word_ea4(), 30);  // thompson: rifle, Auto, 11
    add_weapon_class(ent, 0x0081f5bc, 0x0082b6a4, 0x00825fd8, word_at(0x00825fa0), word_ea4(), 20);  // bar: rifle, Auto, 7.5
    add_weapon_class(ent, 0x00824818, 0x0082b6a4, 0x00825fc8, word_ea4(), word_at(0x0083eaf4), 5);  // springfield: rifle, bolt, 1.5
    add_weapon_class(ent, 0x0081a91c, 0x0081a91c, 0x00825fc8, word_ea4(), word_at(0x0083eba8), 1);  // bazooka: bazooka, bolt, 2
    add_weapon_class(ent, 0x0080d0d4, 0x008367f4, 0x008367f4, word_at(0x0083eb98), word_ea4(), 150);  // mg30cal: lmg, lmg, 10
    add_weapon_class(ent, 0x008247b0, 0x0082b698, 0x00825fd0, word_ea4(), word_at(0x0083ec24), 8);  // mas1935: pistol, semi, 0.25
    add_weapon_class(ent, 0x008247b8, 0x0082b6a4, 0x00825fc8, word_ea4(), word_at(0x0083ec44), 5);  // mas36: rifle, bolt, 0.4
    add_weapon_class(ent, 0x008247a8, 0x0082b6a4, 0x00825fd8, word_at(0x00825f9c), word_ea4(), 32);  // mas_smg: rifle, Auto, 9.5
    add_weapon_class(ent, 0x00824790, 0x0082b6a4, 0x00825fd8, word_at(0x007f9780), word_ea4(), 25);  // chatellerault: rifle, Auto, 8.5
    add_weapon_class(ent, 0x008247d8, 0x0082b698, 0x00825fd0, word_ea4(), word_at(0x0083ec24), 7);  // beretta34: pistol, semi, 0.25
    add_weapon_class(ent, 0x008247a0, 0x0082b6a4, 0x00825fc8, word_ea4(), word_at(0x0083eb24), 6);  // carcano: rifle, bolt, 0.5
    add_weapon_class(ent, 0x008247cc, 0x0082b6a4, 0x00825fd8, word_at(0x00825f9c), word_ea4(), 30);  // beretta_smg: rifle, Auto, 9.5
    add_weapon_class(ent, 0x008247c0, 0x0082b6a4, 0x00825fd8, word_at(0x0083ed78), word_ea4(), 20);  // breda_lmg: rifle, Auto, 8
    add_weapon_class(ent, 0x00825f8c, 0x0082b6a4, 0x00825fd0, word_ea4(), word_at(0x007c6490), 8);  // m1garand_medic: rifle, semi, 0.3
    add_weapon_class(ent, 0x00825f78, 0x0082b6a4, 0x00825fd0, word_ea4(), word_at(0x007c6490), 8);  // m1garand_engineer: rifle, semi, 0.3
}

uint32_t* XB_CDECL BroScr_GetWeaponType(uint32_t* out, uint32_t actor) {
    uint32_t type = 0x0036b1a5;
    if (actor_weapon(actor) != undefined_handle()) {
        const uint32_t model = slot<uint32_t XB_CDECL(uint32_t)>(0x009223e0)(actor_weapon(actor));  // GetWeaponModel
        bool defined;
        if ((!*ScrField_hasweapon_IsDefined(&defined, actor) || *ScrField_hasweapon(actor) != 0) && model != 0x0036b1a5) {
            const uint32_t* const entry = find_weapon(actor_weapon(actor));
            if (entry) type = entry[1];
        }
    }
    *out = type;
    return out;
}

uint32_t* XB_CDECL BroScr_GetWeaponFireType(uint32_t* out, uint32_t actor) {
    return weapon_field(out, actor, 2, 0x0036b1a5);
}

uint32_t* XB_CDECL BroScr_GetWeaponFireRate(uint32_t* out, uint32_t actor) { return weapon_field(out, actor, 3, 0); }

uint32_t* XB_CDECL BroScr_GetWeaponTableField4(uint32_t* out, uint32_t actor) {
    return weapon_field(out, actor, 4, 0);
}

uint32_t* XB_CDECL BroScr_GetWeaponClipSize(uint32_t* out, uint32_t actor) { return weapon_field(out, actor, 5, 0); }

void XB_CDECL BroScr_DelayTimerOnScriptChangeBoth(uint32_t ent, uint32_t first, uint32_t second) {
    BroScr_DelayTimerOnScriptChange(ent, first);
    if (second != undefined_handle()) BroScr_DelayTimerOnScriptChange(ent, second);
}

uint8_t* XB_CDECL BroScr_UpdateCombatDelay(uint8_t* out, uint32_t sentient) {
    uint32_t ref;
    if (entity_handle_is_valid(*slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922e00)(&ref, sentient))) {  // GetRef_3c
        const int32_t spread = at<int32_t>(anim_object(), 0x8);
        const int32_t base = at<int32_t>(anim_object(), 0x18c) + get_time();
        const int32_t until = random_int(spread) + base;
        if (until > static_cast<int32_t>(*key_slot(sentient, 0x900ed884, undefined_int())))
            *ScrField_h900ed884(sentient) = static_cast<uint32_t>(until);
    }
    const int32_t now = get_time();
    *out = static_cast<int32_t>(*key_slot(sentient, 0x900ed884, undefined_int())) > now;
    return out;
}

uint8_t* XB_CDECL BroScr_HasWeapon(uint8_t* out, uint32_t actor) {
    bool defined;
    *out = !(*ScrField_hasweapon_IsDefined(&defined, actor) && *key_slot(actor, 0x2861269a, undefined_int()) == 0);
    return out;
}

uint32_t* XB_CDECL BroScr_GetYawToEnemy(uint32_t* out, uint32_t ent) {
    uint32_t eye_bits[3];
    BroScr_GetEnemyEyePos(eye_bits, ent);
    const float* const eye = reinterpret_cast<const float*>(eye_bits);
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);           // g_scrUndefinedVecLane
    uint32_t dir_bits[3] = {undefined, undefined, undefined};
    uint32_t origin_out[3];
    const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin_out, ent);  // GetOrigin
    const uint32_t origin_bits[3] = {o[0], o[1], o[2]};
    const float* const origin = reinterpret_cast<const float*>(origin_bits);
    float* const dir = reinterpret_cast<float*>(dir_bits);
    dir[0] = eye[0] - origin[0];
    dir[1] = eye[1] - origin[1];
    dir[2] = eye[2] - origin[2];
    uint32_t angles[3] = {undefined, undefined, undefined};
    slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922284)(angles, dir_bits);  // Scr_VectorToAngles
    *out = angles[1];
    return out;
}

void XB_CDECL BroScr_MeleePair_Link(uint32_t a, uint32_t b) {
    bool defined;
    if (!entity_handle_is_valid(b)) drop_bad_parameter();
    if (*ScrField_h236fc839_IsDefined(&defined, b)) drop_bad_parameter();
    if (*ScrField_h31aaf853_IsDefined(&defined, a)) drop_bad_parameter();
    *key_slot(b, 0x236fc839, 0) = a;
    *key_slot(a, 0x31aaf853, 0) = b;
}

}  // namespace game

// Push c3: line-of-fire checks from the muzzle and from a pose.
namespace game {

namespace {

using bro_ai::as_float;
using bro_ai::bits;
using bro_ai::undefined_lane;

using CanShoot = uint32_t XB_CDECL(uint32_t, const uint32_t*, const uint32_t*);

const uint32_t* eye_pos(uint32_t* out, const uint32_t* ent) {
    return own_function<const uint32_t* XB_CDECL(uint32_t*, const uint32_t*)>(&BroScr_GetEyePos)(out, ent);  // BroScr_GetEyePos
}

uint32_t actor_int(uint32_t slot_address, uint32_t actor) { return slot<uint32_t XB_CDECL(uint32_t)>(slot_address)(actor); }

}  // namespace

uint8_t* XB_CDECL BroScr_GetMuzzleOrigin(uint8_t* out, uint32_t actor, ScrVec3 offset, uint32_t target, uint32_t flag) {
    uint32_t ref;
    if (!entity_handle_is_valid(*slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922e00)(&ref, actor)) &&
        !entity_handle_is_valid(target)) {  // ScrSentient_GetRef_3c
        *out = 0;
        return out;
    }
    uint8_t armed;
    if (!*BroScr_HasWeapon(&armed, actor)) {
        *out = 0;
        return out;
    }
    uint32_t aim[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    uint32_t low[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    uint32_t buffer[3];
    const uint32_t* eye = nullptr;
    if (entity_handle_is_valid(target)) {
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(target)) {  // GScr_IsSentient
            eye = eye_pos(buffer, &target);
        } else {
            const uint32_t* const o = ScrCall_ScrEnt_GetOrigin(&target, buffer);
            aim[0] = o[0];
            aim[1] = o[1];
            aim[2] = o[2];
            low[0] = aim[0];
            low[1] = aim[1];
            low[2] = aim[2];
        }
    } else {
        eye = BroScr_GetEnemyEyePos(buffer, actor);
    }
    if (eye) {
        aim[0] = eye[0];
        aim[1] = eye[1];
        aim[2] = eye[2];
        low[0] = aim[0];
        low[1] = aim[1];
        low[2] = bits(as_float(aim[2]) - title_global<float>(0x0083eb5c));  // 20
    }
    uint32_t shifted[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    ScriptString tag_name;
    ScriptString_FromCString(&tag_name, pointer<const char>(0x008389c0));  // "tag_flash"
    uint32_t tag[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x0092261c)(actor, tag, &tag_name);  // GScr_GetTagOrigin
    const uint32_t flash[3] = {tag[0], tag[1], tag[2]};
    ScriptString_Release(&tag_name);
    if (flag == bro_ai::undefined_int() || flag != 0) {
        uint32_t eye_buffer[3];
        const uint32_t* const e = eye_pos(eye_buffer, &actor);
        const uint32_t self_eye[3] = {e[0], e[1], e[2]};
        shifted[0] = bits(platform_addss(as_float(offset.v[0]), as_float(self_eye[0]) - as_float(flash[0])));
        shifted[1] = bits(platform_addss(as_float(offset.v[1]), as_float(self_eye[1]) - as_float(flash[1])));
        shifted[2] = bits(platform_addss(as_float(offset.v[2]), as_float(self_eye[2]) - as_float(flash[2])));
        if (!slot<CanShoot>(0x009226d0)(actor, aim, shifted)) {  // GScr_ActorCanShootWithOffset
            *out = 0;
            return out;
        }
    }
    uint32_t result = slot<CanShoot>(0x009226d0)(actor, aim, offset.v);
    if (!result && slot<CanShoot>(0x009226d0)(actor, low, offset.v)) result = 1;
    *out = result != 0;
    return out;
}

uint8_t* XB_CDECL BroScr_GetMuzzleOriginCall(uint8_t* out, uint32_t actor, uint32_t target, uint32_t flag) {
    const ScrVec3 zero = {{bits(0.0f), bits(0.0f), bits(0.0f)}};
    BroScr_GetMuzzleOrigin(out, actor, zero, target, flag);
    return out;
}

uint8_t* XB_CDECL BroScr_CanShootFromPose(uint8_t* out, uint32_t actor, uint32_t pose, ScrVec3 offset, uint32_t flag) {
    uint32_t local[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
    constexpr uint32_t kStand = 0x070b89a4;
    constexpr uint32_t kCrouch = 0xc1165808;
    constexpr uint32_t kProne = 0x0738b1a5;
    // The height change from the current pose (no change for an unknown one).
    uint32_t heights[3] = {0, 0, 0};  // global addresses of the stand / crouch / prone heights, 0 = 0.0
    bool known_pose = true;
    if (pose == kStand) {
        static const uint32_t kBlocked[6] = {0xa39d17dd, 0x318fbe80, 0x45963119, 0x00371a54, 0x0033c678, 0x8d298057};
        for (uint32_t i = 0; i < 6; ++i) {
            if (actor_int(0x00922d30, actor) == kBlocked[i]) {  // ScrActor_GetInt_f0
                *out = 0;
                return out;
            }
        }
        heights[1] = 0x0083eb5c;  // 20
        heights[2] = 0x0083f14c;  // 55
    } else if (pose == kCrouch) {
        if (actor_int(0x00922d30, actor) == 0x00371a54) {
            *out = 0;
            return out;
        }
        heights[0] = 0x0083f198;  // -20
        heights[2] = 0x0083ebc4;  // 25
    } else if (pose == kProne) {
        if (actor_int(0x00922d30, actor) == 0x00371a54) {
            *out = 1;
            return out;
        }
        heights[0] = 0x007f8948;  // -55
        heights[1] = 0x007fcdc0;  // -25
    } else {
        known_pose = false;
    }
    if (known_pose) {
        int32_t index = -1;
        if (actor_int(0x00922dc0, actor) == kStand) index = 0;  // ScrActor_GetInt_184
        else if (actor_int(0x00922dc0, actor) == kCrouch) index = 1;
        else if (actor_int(0x00922dc0, actor) == kProne) index = 2;
        if (index >= 0) local[2] = heights[index] ? title_global<uint32_t>(heights[index]) : bits(0.0f);
    }
    uint32_t v[3];
    const float lane = as_float(undefined_lane());
    if (as_float(offset.v[0]) == lane && as_float(offset.v[1]) == lane && as_float(offset.v[2]) == lane) {
        v[0] = local[0];
        v[1] = local[1];
        v[2] = local[2];
    } else {
        uint32_t world[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
        slot<void XB_CDECL(uint32_t, const uint32_t*, uint32_t*)>(0x0092263c)(actor, offset.v, world);  // GScr_LocalToWorldCoords
        uint32_t buffer[3];
        const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, actor);  // ScrEnt_GetOrigin
        const uint32_t origin[3] = {o[0], o[1], o[2]};
        const float z = platform_addss(as_float(world[2]), as_float(local[2]));
        v[0] = bits(as_float(world[0]) - as_float(origin[0]));
        v[1] = bits(as_float(world[1]) - as_float(origin[1]));
        v[2] = bits(z - as_float(origin[2]));
    }
    const ScrVec3 shifted = {{v[0], v[1], v[2]}};
    BroScr_GetMuzzleOrigin(out, actor, shifted, 0, flag);
    return out;
}

namespace {

// The common test of BroScr_ShouldGoProne / _2: the nearest enemy (500 when undefined) is not
// closer than anim-object +0x90 and the actor can shoot prone.
bool prone_worthwhile(uint32_t actor, uint32_t flag) {
    float distance = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922690)(actor));  // GScr_SentientNearestEnemyDist
    if (distance == as_float(title_global<uint32_t>(0x00922eac))) distance = title_global<float>(0x0083ebec);  // 500
    const float limit = as_float(at<uint32_t>(bro_ai::anim_object(), 0x90));
    if (limit > distance) return false;
    const ScrVec3 none = {{title_global<uint32_t>(0x00922ec4), title_global<uint32_t>(0x00922ec8),
                           title_global<uint32_t>(0x00922ecc)}};
    uint8_t can_shoot;
    return *BroScr_CanShootFromPose(&can_shoot, actor, 0x0738b1a5, none, flag) != 0;
}

}  // namespace

uint32_t* XB_CDECL BroScr_ShouldGoProne(uint32_t* out, uint32_t actor, ScrVec3 point, uint32_t yaw) {
    if (!prone_worthwhile(actor, title_global<uint32_t>(0x00922ebc))) {
        *out = 0;
        return out;
    }
    BroScr_ActorMayMoveToPointForStance(out, actor, point, yaw);
    return out;
}

uint32_t* XB_CDECL BroScr_ShouldGoProne_2(uint32_t* out, uint32_t actor, ScrVec3 point, uint32_t yaw) {
    if (!prone_worthwhile(actor, 1)) {
        *out = 0;
        return out;
    }
    uint8_t may;
    *out = *BroScr_ActorMayMoveToPointForStance_2(&may, actor, point, yaw) != 0;
    return out;
}

uint32_t* XB_CDECL BroScr_CheckCanShoot(uint32_t* out, uint32_t actor, uint32_t count_misses) {
    bool defined;
    if (!*ScrField_ha31c116e_IsDefined(&defined, actor)) *key_slot(actor, 0xa31c116e, undefined_int()) = 0;
    if (count_misses == undefined_int()) count_misses = 1;
    const ScrVec3 zero = {{bits(0.0f), bits(0.0f), bits(0.0f)}};
    uint8_t can;
    const uint32_t shoot = *BroScr_GetMuzzleOrigin(&can, actor, zero, title_global<uint32_t>(0x00922e84),
                                                   title_global<uint32_t>(0x00922ebc)) != 0;
    if (shoot) *key_slot(actor, 0xa31c116e, undefined_int()) = 0;
    else if (count_misses) ++*ScrField_ha31c116e(actor);
    if (static_cast<int32_t>(*key_slot(actor, 0xa31c116e, undefined_int())) > 4) {
        slot<void XB_CDECL(uint32_t)>(0x009226e8)(actor);  // GScr_ActorFindBetterCover
        *key_slot(actor, 0xa31c116e, undefined_int()) = 0;
    }
    *out = shoot;
    return out;
}

}  // namespace game

