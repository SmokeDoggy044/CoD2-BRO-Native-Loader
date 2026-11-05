#include "bro_scr_ai_weapons.h"

#include "bro_scr_ai_look.h"

#include "g_scr_runtime.h"
#include "qcommon_fs.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

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
