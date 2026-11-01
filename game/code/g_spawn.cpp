#include "g_spawn.h"

#include "g_main.h"
#include "g_script.h"
#include "g_utils.h"
#include "qcommon.h"
#include "script_string.h"
#include "sv.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

// level.numSpawnVars (0x00985420) and level.spawnVars {key, value} (0x00985424).
uint32_t& num_spawn_vars() { return title_global<uint32_t>(0x00985420); }
uint32_t& spawn_var_key(uint32_t i) { return title_global<uint32_t>(0x00985424 + i * 8); }
uint32_t& spawn_var_value(uint32_t i) { return title_global<uint32_t>(0x00985428 + i * 8); }

// A spawn key's hash, made once under `bit` of the guard word.
uint32_t cached_key(uint32_t guard, uint32_t bit, uint32_t slot, uint32_t text) {
    uint32_t value = title_global<uint32_t>(guard);
    if (!(value & bit)) {
        value |= bit;
        title_global<uint32_t>(guard) = value;
        title_global<uint32_t>(slot) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(static_cast<uintptr_t>(text)));
    }
    return title_global<uint32_t>(slot);
}

// Q3's trigger wait/random: "wait" (default 0.5) into +0x384, "random" into +0x388; a random
// spread not below the wait (with wait >= 0) becomes wait - 100.
void spawn_wait_random(uint8_t* ent, uint32_t wait_key, uint32_t random_key) {
    float* const wait = &field<float>(ent, 0x384);
    float* const random = &field<float>(ent, 0x388);
    G_SpawnFloat(wait_key, 0.5f, wait);
    G_SpawnFloat(random_key, 0.0f, random);
    if (*random >= *wait && *wait >= title_global<float>(0x0083eafc))
        *random = *wait - title_global<float>(0x0083eb90);
}

}  // namespace

void XB_THISCALL G_SetSpawnVarsFromList(const uint32_t* list) {
    num_spawn_vars() = 0;
    title_global<uint32_t>(0x00985624) = 0;
    // The count and the pairs are re-read for each access; an index past the count reads pair 0
    // (an inlined bounds-checked accessor).
    uint32_t count = list[0];
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t key_index = i < count ? i : 0;
        const uint32_t key =
            *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(list[1] + key_index * 8));
        spawn_var_key(num_spawn_vars()) = key;
        const uint32_t value_index = i < list[0] ? i : 0;
        const uint32_t value =
            *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(list[1] + value_index * 8 + 4));
        spawn_var_value(num_spawn_vars()) = value;
        ++num_spawn_vars();
        count = list[0];
    }
}

PLATFORM_ASM_ENTRY(G_SpawnVector_entry,
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_SpawnVector) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL G_SpawnVector(uint32_t key, float* out, const uint32_t* fallback) {
    const int32_t count = static_cast<int32_t>(num_spawn_vars());
    for (int32_t i = 0; i < count; ++i) {
        if (key == spawn_var_key(i)) {
            const char* const value = reinterpret_cast<const char*>(static_cast<uintptr_t>(spawn_var_value(i)));
            title_function<int XB_CDECL(const char*, const char*, ...)>(0x001f07e1)(
                value, reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00838c70)), out, out + 1,
                out + 2);  // sscanf "%f %f %f"
            return 1;
        }
    }
    uint32_t* const words = reinterpret_cast<uint32_t*>(out);
    words[0] = fallback[0];
    words[1] = fallback[1];
    words[2] = fallback[2];
    return 0;
}

PLATFORM_ASM_ENTRY(Actor_CopySpawnerFields_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_CopySpawnerFields) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_CopySpawnerFields(uint8_t* spawner, uint8_t* actor) {
    // The actor field table (0x0082ff38: {name, offset, type, ?}, ends at a null name).
    if (title_global<uint32_t>(0x0082ff38)) {
        uint32_t entry = 0x0082ff3c;
        do {
            const uint32_t type = title_global<uint32_t>(entry + 4);
            const uint32_t offset = title_global<uint32_t>(entry);
            switch (type) {
                case 0:   // int
                case 3:   // float
                    word_at(actor, offset) = word_at(spawner, offset);
                    break;
                case 1:  // short
                    *reinterpret_cast<uint16_t*>(actor + offset) = *reinterpret_cast<uint16_t*>(spawner + offset);
                    break;
                case 2:
                case 12:  // byte
                    actor[offset] = spawner[offset];
                    break;
                case 5:  // vector
                    word_at(actor, offset) = word_at(spawner, offset);
                    word_at(actor, offset + 4) = word_at(spawner, offset + 4);
                    word_at(actor, offset + 8) = word_at(spawner, offset + 8);
                    break;
                case 14:  // script string
                    title_function<void XB_THISCALL(uint8_t*, uint8_t*)>(0x0011d600)(actor + offset,
                                                                                    spawner + offset);  // ScriptString_Assign
                    break;
                default:
                    break;
            }
            entry += 0x10;
        } while (title_global<uint32_t>(entry - 4));
    }
    word_at(actor, 0x260) = word_at(spawner, 0x260);
    word_at(actor, 0x270) = word_at(spawner, 0x270);
    word_at(actor, 0x268) = word_at(spawner, 0x268);
    word_at(actor, 0x278) = word_at(spawner, 0x278);
    word_at(actor, 0x280) = word_at(spawner, 0x280);
}

void XB_CDECL SP_info_player_start(uint8_t* ent) {
    // Becomes an info_player_deathmatch (classname 0x00984b70 and its hash), thinks in 200 ms
    // (think 5).
    ScriptString* const classname = reinterpret_cast<ScriptString*>(ent + 0x25c);
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d600)(classname, 0x00984b70);  // ScriptString_Assign
    const uint32_t rep = word_at(ent, 0x25c);
    word_at(ent, 0x260) = Com_HashKeyNoCase(0, rep ? reinterpret_cast<const char*>(static_cast<uintptr_t>(rep + 6)) : nullptr);
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 200;
    word_at(ent, 0x32c) = 5;
    G_UpdateEntityStringHashes(ent);
}

void XB_CDECL SP_skyportal(uint8_t* ent) {
    // The sky portal configstring (10): origin, fov (default 90), how many of fogcolor /
    // fognear / fogfar were given, fog colour (default 0 0 0), fog near (0) and far (300).
    // The key hashes are made once (bits of 0x0098bc94).
    uint32_t& made = title_global<uint32_t>(0x0098bc94);
    if (!(made & 1)) {
        made |= 1;
        title_global<uint32_t>(0x0098bc90) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083af3c}));
    }
    float fov;
    G_SpawnFloat(title_global<uint32_t>(0x0098bc90), 90.0f, &fov);
    static const char* const kKeys[] = {nullptr, reinterpret_cast<const char*>(uintptr_t{0x0083af30}),
                                        reinterpret_cast<const char*>(uintptr_t{0x0083af28}),
                                        reinterpret_cast<const char*>(uintptr_t{0x0083af20})};
    const uint32_t zero_color[3] = {0, 0, 0};
    uint32_t bits = made;
    for (uint32_t i = 1; i < 4; ++i) {
        if (bits & (1u << i)) continue;
        bits |= 1u << i;
        made = bits;
        title_global<uint32_t>(0x0098bc90 - i * 4) = Com_HashKeyNoCase(0, kKeys[i]);  // fogcolor, fognear, fogfar
    }
    float color[3];
    uint32_t given = G_SpawnVector(title_global<uint32_t>(0x0098bc8c), color, zero_color);
    int32_t fog_near, fog_far;
    given += G_SpawnInt(title_global<uint32_t>(0x0098bc88), &fog_near, 0);
    given += G_SpawnInt(title_global<uint32_t>(0x0098bc84), &fog_far, 300);
    const float* const origin = reinterpret_cast<const float*>(ent + 0x140);
    const char* const text = title_function<char* XB_CDECL(const char*, ...)>(0x002a93d0)(
        reinterpret_cast<const char*>(uintptr_t{0x0083aef4}), static_cast<double>(origin[0]),
        static_cast<double>(origin[1]), static_cast<double>(origin[2]), static_cast<double>(fov), given,
        static_cast<double>(color[0]), static_cast<double>(color[1]), static_cast<double>(color[2]), fog_near,
        fog_far);  // va "%.2f %.2f %.2f %.1f %i %.2f %.2f %.2f %i %i"
    SV_SetConfigstring(10, text);
}

PLATFORM_ASM_ENTRY(InitTrigger_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(InitTrigger) "addl $4, %esp\n"
                   "ret")

void XB_CDECL InitTrigger(uint8_t* ent) {
    // Q3's InitTrigger: movedir (+0x310) from non-zero angles (+0x150), the brush model,
    // SVF_NOCLIENT-like flag 2 (+0x10), contents 0x405c0008 (+0x134), +0xd4 = 1.
    const float* const angles = reinterpret_cast<const float*>(ent + 0x150);
    if (angles[0] != 0.0f || angles[1] != 0.0f || angles[2] != 0.0f)
        G_SetMovedir(reinterpret_cast<float*>(ent + 0x150), reinterpret_cast<float*>(ent + 0x310));
    SV_SetBrushModel(ent);
    const uint32_t flags = word_at(ent, 0x10) | 2;
    word_at(ent, 0x134) = 0x405c0008;
    word_at(ent, 0xd4) = 1;
    word_at(ent, 0x10) = flags;
}

void XB_CDECL SP_trigger_mount(uint8_t* ent) {
    // Touch callback 0xa; contents gain 0x400000 with spawn flag 1, else 0x1000000; linked.
    word_at(ent, 0x338) = 0xa;
    InitTrigger(ent);
    if (ent[0x2a0] & 1)
        word_at(ent, 0x134) |= 0x400000;
    else
        word_at(ent, 0x134) |= 0x1000000;
    SV_LinkEntity(ent);
}

void XB_CDECL SP_trigger_lookat(uint8_t* ent) {
    // The brush model, flag 2 (+0x10), contents 0x20000000, +0xd4 = 1, linked.
    SV_SetBrushModel(ent);
    const uint32_t flags = word_at(ent, 0x10) | 2;
    word_at(ent, 0x134) = 0x20000000;
    word_at(ent, 0xd4) = 1;
    word_at(ent, 0x10) = flags;
    SV_LinkEntity(ent);
}

void XB_CDECL SP_trigger_damage(uint8_t* ent) {
    // wait/random (keys cached under 0x0098e8e4), "accumulate" (+0x370) and "threshold"
    // (+0x3b4); health 32000, takedamage, pain 0xa, die 5 / 5, InitTrigger, linked.
    uint32_t guard = title_global<uint32_t>(0x0098e8e4);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098e8e4) = guard;
        title_global<uint32_t>(0x0098e8e0) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x00833c88}));  // "wait"
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098e8e4) = guard;
        title_global<uint32_t>(0x0098e8dc) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b648}));  // "random"
    }
    spawn_wait_random(ent, title_global<uint32_t>(0x0098e8e0), title_global<uint32_t>(0x0098e8dc));
    cached_key(0x0098e8e4, 4, 0x0098e8d8, 0x00839c9c);  // "accumulate"
    cached_key(0x0098e8e4, 8, 0x0098e8d4, 0x00839c90);  // "threshold"
    G_SpawnInt(title_global<uint32_t>(0x0098e8d8), &field<int32_t>(ent, 0x370), 0);
    G_SpawnInt(title_global<uint32_t>(0x0098e8d4), &field<int32_t>(ent, 0x3b4), 0);
    word_at(ent, 0x350) = 32000;
    word_at(ent, 0x29c) = 1;
    word_at(ent, 0x33c) = 0xa;
    word_at(ent, 0x340) = 5;
    word_at(ent, 0x344) = 5;
    InitTrigger(ent);
    SV_LinkEntity(ent);
}

void XB_CDECL SP_trigger_once(uint8_t* ent) {
    // wait -1 (+0x384), touch and use callbacks 9, InitTrigger, contents from the spawn flags,
    // linked.
    field<float>(ent, 0x384) = title_global<float>(0x0083eb34);
    word_at(ent, 0x338) = 9;
    word_at(ent, 0x33c) = 9;
    InitTrigger(ent);
    G_TriggerContentsFromSpawnflags(ent);
    SV_LinkEntity(ent);
}

void XB_CDECL SP_trigger_hurt(uint8_t* ent) {
    // InitTrigger (not linked here); the "world_hurt_me" sound index (+0x295), "dmg"
    // (+0x358, 0 -> 50), contents 0x405c0008, use callback 1, touch 3 unless spawn flag 1,
    // "life" (+0x38c). Keys cached under 0x0098e8f0.
    InitTrigger(ent);
    {
        const TitleRegs regs{0, 0x40, 0, 0, 0, 0};
        const uint32_t args[4] = {0x00839cb0, 0xa0, 1, 0};
        ent[0x295] = static_cast<uint8_t>(g_ent_call(0x002c39a0, &regs, 4, args));  // G_FindConfigstringIndex
    }
    const uint32_t dmg = cached_key(0x0098e8f0, 1, 0x0098e8ec, 0x0083b060);  // "dmg"
    G_SpawnInt(dmg, &field<int32_t>(ent, 0x358), 0x32);
    if (!word_at(ent, 0x358)) word_at(ent, 0x358) = 0x32;
    const bool start_off = (ent[0x2a0] & 1) != 0;
    word_at(ent, 0x134) = 0x405c0008;
    word_at(ent, 0x33c) = 1;
    if (!start_off) word_at(ent, 0x338) = 3;
    const uint32_t life_key = cached_key(0x0098e8f0, 2, 0x0098e8e8, 0x00839ca8);  // "life"
    float life;
    G_SpawnFloat(life_key, 0.0f, &life);
    field<float>(ent, 0x38c) = life;
}

void XB_CDECL SP_trigger_friendlychain(uint8_t* ent) {
    // Touch callback 5; G_Error without a target (+0x26c string); InitTrigger, contents from
    // the spawn flags, linked.
    word_at(ent, 0x338) = 5;
    const uint32_t target = word_at(ent, 0x26c);
    if (!target || !(target + 6) || !*reinterpret_cast<const char*>(static_cast<uintptr_t>(target + 6)))
        title_function<int32_t XB_CDECL(const char*)>(0x00248af0)(reinterpret_cast<const char*>(
            uintptr_t{0x00839cc0}));  // G_Error "trigger_friendlychain must target a friendly chain node"
    InitTrigger(ent);
    G_TriggerContentsFromSpawnflags(ent);
    SV_LinkEntity(ent);
}

void XB_CDECL SP_trigger_multiple(uint8_t* ent) {
    // Q3's SP_trigger_multiple: wait/random (keys cached under 0x0098e8fc), touch and use
    // callbacks 9, InitTrigger, contents from the spawn flags, linked.
    uint32_t guard = title_global<uint32_t>(0x0098e8fc);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098e8fc) = guard;
        title_global<uint32_t>(0x0098e8f8) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x00833c88}));  // "wait"
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098e8fc) = guard;
        title_global<uint32_t>(0x0098e8f4) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b648}));  // "random"
    }
    spawn_wait_random(ent, title_global<uint32_t>(0x0098e8f8), title_global<uint32_t>(0x0098e8f4));
    word_at(ent, 0x338) = 9;
    word_at(ent, 0x33c) = 9;
    InitTrigger(ent);
    G_TriggerContentsFromSpawnflags(ent);
    SV_LinkEntity(ent);
}

}  // namespace game
