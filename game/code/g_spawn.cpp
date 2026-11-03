#include "g_spawn.h"

#include <cstring>

#include "cg_ents.h"
#include "dobj.h"
#include "g_main.h"
#include "qcommon_fs.h"
#include "g_script.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "script_string.h"
#include "sv.h"
#include "title_data.h"
#include "tr_main.h"

namespace game {

namespace {

uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint8_t* pointer_at(uint8_t* base, uint32_t offset) {
    return reinterpret_cast<uint8_t*>(uintptr_t{*reinterpret_cast<uint32_t*>(base + offset)});
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

// The spawn var's value for a key hash, or null.
const char* find_spawn_value(uint32_t key) {
    const int32_t count = static_cast<int32_t>(num_spawn_vars());
    for (int32_t i = 0; i < count; ++i)
        if (key == spawn_var_key(static_cast<uint32_t>(i)))
            return reinterpret_cast<const char*>(uintptr_t{spawn_var_value(static_cast<uint32_t>(i))});
    return nullptr;
}

// The title CRT's __CIcos (0x0033adb0; x87 argument and result).
float title_ci_cos(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033adb0})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
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

void XB_CDECL SP_trigger_use(uint8_t* ent) {
    // Brush model, linked; trajectory stationary at the origin, contents 0x200000, wait
    // (+0x38c) in ms, use callback 0xc, cursor hint (+0xc0) 2 or "cursorhint" (HINT_INHERIT
    // -> -1, else its index in the hint name table 0x008dae44, 1..11). "hintstring": its index
    // among the 32 hint configstrings (0x273 + i; the first free one gets it) in byte 5 (0xff
    // without; 32 -> __break). "harc" degrees -> cos(harc / 2) (+0x90, x87 + __CIcos),
    // "angle" (atof) -> +0x94. Keys cached under 0x0098e434.
    SV_SetBrushModel(ent);
    SV_LinkEntity(ent);
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    word_at(ent, 0x134) = 0x200000;
    word_at(ent, 0xd4) = 1;
    field<float>(ent, 0x38c) = field<float>(ent, 0x38c) * title_global<float>(0x0083eb9c);
    word_at(ent, 0x33c) = 0xc;
    word_at(ent, 0xc0) = 2;
    const char* hint = find_spawn_value(cached_key(0x0098e434, 1, 0x0098e430, 0x0083afc4));  // "cursorhint"
    if (hint) {
        if (Q_stricmpn2(0x1869f, hint, &title_global<const char>(0x00832040)) == 0) {  // "HINT_INHERIT"
            word_at(ent, 0xc0) = 0xffffffffu;
        } else {
            for (uint32_t i = 1; i < 0xc; ++i) {
                const char* name = reinterpret_cast<const char*>(uintptr_t{title_global<uint32_t>(0x008dae44 + i * 4)});
                if (Q_stricmpn2(0x1869f, hint, name) == 0) {
                    word_at(ent, 0xc0) = i;
                    break;
                }
            }
        }
    }
    ent[5] = 0xff;
    const char* text = find_spawn_value(cached_key(0x0098e434, 2, 0x0098e42c, 0x0083afb8));  // "hintstring"
    if (text) {
        int32_t index = 0;
        for (; index < 0x20; ++index) {
            if (index < -0x273 || index >= 0x18d)  // configstring index outside 0..0x3ff
                title_function<void XB_CDECL(int32_t, const char*, int32_t)>(0x002938f0)(
                    1, &title_global<const char>(0x00832d34), index + 0x273);  // __break
            const uint32_t stored = title_global<uint32_t>(0x009656ac + static_cast<uint32_t>(index) * 4);
            char current[0x100];
            title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(  // _strncpy
                current, stored ? reinterpret_cast<const char*>(uintptr_t{stored + 6}) : nullptr, 0xff);
            current[0xff] = 0;
            if (!current[0]) {
                SV_SetConfigstring(index + 0x273, text);
                ent[5] = static_cast<uint8_t>(index);
                break;
            }
            if (std::strcmp(text, current) == 0) {
                ent[5] = static_cast<uint8_t>(index);
                break;
            }
        }
        if (index == 0x20)
            title_function<void XB_CDECL(int32_t, const char*, int32_t)>(0x002938f0)(
                1, &title_global<const char>(0x0083af50), index);  // __break
    }
    const char* arc_text = find_spawn_value(cached_key(0x0098e434, 4, 0x0098e428, 0x0083af48));  // "harc"
    const int32_t arc = arc_text ? title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(arc_text) : 0;  // _atol
    const float degrees = platform_x87_int_to_float(arc);
    const float half_arc = static_cast<float>(static_cast<long double>(degrees) * title_global<float>(0x0083eb24) *
                                              title_global<float>(0x0083ed00) / title_global<float>(0x0083eb60));
    field<float>(ent, 0x90) = title_ci_cos(half_arc);
    const char* angle_text = find_spawn_value(cached_key(0x0098e434, 8, 0x0098e424, 0x0083af40));  // "angle"
    if (!angle_text) {
        field<float>(ent, 0x94) = 0.0f;
        return;
    }
    field<float>(ent, 0x94) = title_function<float XB_CDECL(const char*)>(0x001ef056)(angle_text);  // atof
}

PLATFORM_ASM_ENTRY(G_CallSpawn_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_CallSpawn) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL G_CallSpawn(uint8_t* ent) {
    // Q3's G_CallSpawn: no classname (+0x25c script string, text at +6) -> 0. The spawn map
    // (0x00983e1c, built on first use) by the name hash (+0x260) gives the spawn function
    // (0x009885e4 + index * 8); else "weapon_*" classnames are an error and items (bg_itemlist
    // 0x0096bd44, 0x34 each, name hash +0) spawn by G_SpawnItem. The string hashes are
    // refreshed after a spawn.
    const uint32_t name = word_at(ent, 0x25c);
    if (!name || name + 6 == 0 || !*reinterpret_cast<const char*>(uintptr_t{name + 6})) return 0;
    if (!title_global<uint32_t>(0x00983e28)) G_InitSpawnFuncMap();
    uint32_t key = word_at(ent, 0x260);
    uint32_t node = 0;
    UintHashMap_Find(&node, &key, &title_global<uint8_t>(0x00983e1c));
    if (node != title_global<uint32_t>(0x00983e24)) {
        const uint32_t index = *reinterpret_cast<const uint32_t*>(uintptr_t{node + 0xc});
        title_function<void XB_CDECL(uint8_t*)>(title_global<uint32_t>(0x009885e4 + index * 8))(ent);
        G_UpdateEntityStringHashes(ent);
        return 1;
    }
    const uint32_t text = word_at(ent, 0x25c);
    const char* const classname = text ? reinterpret_cast<const char*>(uintptr_t{text + 6}) : nullptr;
    if (title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001eeaf0)(  // _strncmp
            classname, &title_global<const char>(0x00839dc8), 6) == 0) {               // "weapon"
        title_function<void XB_CDECL(const char*)>(0x00248af0)(&title_global<const char>(0x00839d98));  // G_Error
        return 0;
    }
    if (!title_global<uint32_t>(0x0096bd48)) return 0;
    uint32_t item = 0x0096bd44;
    for (;;) {
        if (title_global<uint32_t>(item) == word_at(ent, 0x260)) break;
        const uint32_t next = title_global<uint32_t>(item + 0x38);
        item += 0x34;
        if (!next) return 0;
    }
    G_SpawnItem(ent, reinterpret_cast<const uint8_t*>(uintptr_t{item}));
    G_UpdateEntityStringHashes(ent);
    return 1;
}

PLATFORM_ASM_ENTRY(G_SpawnItem_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_SpawnItem) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_SpawnItem(uint8_t* ent, const uint8_t* item) {
    // Q3's G_SpawnItem: "random" (+0x388) and "wait" (+0x384); the item's index (item -
    // bg_itemlist 0x0096bd10, 0x34 each) registered as a weapon; the item (+0x3b0) and its
    // model (+0xc); the 0x0083b640 key as a sound configstring (+0x295). Bounds -1..1 (z 0..2
    // unless item type +0x24 is 1), flags, contents 0x407c0108, touch 7, type 2, model index
    // (+6), use 8, +0x2a4 bit 0x8000. With 0x0098541c set: think 6 in 200 ms (FinishSpawningItem
    // later); else stationary at the origin (type-1 items turned 90 in roll, ground entity
    // cleared without spawn flag 1) and linked. Keys cached under 0x0098ee5c.
    uint32_t guard = title_global<uint32_t>(0x0098ee5c);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098ee5c) = guard;
        title_global<uint32_t>(0x0098ee58) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b648}));
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098ee5c) = guard;
        title_global<uint32_t>(0x0098ee54) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x00833c88}));
    }
    if (!(guard & 4)) {
        guard |= 4;
        title_global<uint32_t>(0x0098ee5c) = guard;
        title_global<uint32_t>(0x0098ee50) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b640}));
    }
    G_SpawnFloat(title_global<uint32_t>(0x0098ee58), 0.0f, &field<float>(ent, 0x388));
    G_SpawnFloat(title_global<uint32_t>(0x0098ee54), 0.0f, &field<float>(ent, 0x384));
    const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item));
    G_RegisterWeapon(0, 0, static_cast<uint32_t>(static_cast<int32_t>(address - 0x0096bd10) / 0x34), 0);
    word_at(ent, 0x3b0) = address;
    title_function<void XB_REGS3 XB_CDECL(int32_t, uint8_t*, uint32_t)>(0x002d7c60)(
        -1, ent, *reinterpret_cast<const uint32_t*>(item + 0xc));  // G_SetModel
    const int32_t count = static_cast<int32_t>(num_spawn_vars());
    for (int32_t i = 0; i < count; ++i) {
        if (title_global<uint32_t>(0x0098ee50) != spawn_var_key(static_cast<uint32_t>(i))) continue;
        const TitleRegs regs{0, 0x40, 0, 0, 0, 0};
        const uint32_t args[4] = {spawn_var_value(static_cast<uint32_t>(i)), 0xa0, 1, 0};
        ent[0x295] = static_cast<uint8_t>(g_ent_call(0x002c39a0, &regs, 4, args));  // G_FindConfigstringIndex
        break;
    }
    const float minus_one = title_global<float>(0x0083eb34);
    const float one = title_global<float>(0x0083eb20);
    const bool upright = *reinterpret_cast<const uint32_t*>(item + 0x24) == 1;
    field<float>(ent, 0xe0) = minus_one;
    field<float>(ent, 0xe4) = minus_one;
    field<float>(ent, 0xe8) = upright ? minus_one : 0.0f;
    field<float>(ent, 0xf0) = one;
    field<float>(ent, 0xf4) = one;
    field<float>(ent, 0xf8) = upright ? one : title_global<float>(0x0083eba8);
    word_at(ent, 0x10) |= 0x10;
    word_at(ent, 0xd4) |= 0x200;
    const uint32_t index = static_cast<uint32_t>(static_cast<int32_t>(word_at(ent, 0x3b0) - 0x0096bd10) / 0x34);
    word_at(ent, 0x134) = 0x407c0108;
    word_at(ent, 0x338) = 7;
    ent[0] = 2;
    *reinterpret_cast<uint16_t*>(ent + 6) = static_cast<uint16_t>(index);
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
    word_at(ent, 0x33c) = 8;
    word_at(ent, 0x2a4) |= 0x8000;
    const bool later = title_global<uint32_t>(0x0098541c) != 0;
    if (!later && !(ent[0x2a0] & 1)) {
        word_at(ent, 0xc) = 0;
        if (*reinterpret_cast<const uint32_t*>(item + 0x24) == 1)
            field<float>(ent, 0x158) = field<float>(ent, 0x158) + title_global<float>(0x0083eb64);
    }
    word_at(ent, 0x44) = word_at(ent, 0x150);
    word_at(ent, 0x48) = word_at(ent, 0x154);
    word_at(ent, 0x4c) = word_at(ent, 0x158);
    word_at(ent, 0x38) = 0;
    word_at(ent, 0x3c) = 0;
    word_at(ent, 0x40) = 0;
    field<float>(ent, 0x58) = 0.0f;
    field<float>(ent, 0x54) = 0.0f;
    field<float>(ent, 0x50) = 0.0f;
    if (later) {
        field<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c) + 200;
        word_at(ent, 0x32c) = 6;
        return;
    }
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x18) = 0;
    word_at(ent, 0x1c) = 0;
    field<float>(ent, 0x34) = 0.0f;
    field<float>(ent, 0x30) = 0.0f;
    field<float>(ent, 0x2c) = 0.0f;
    SV_LinkEntity(ent);
}

PLATFORM_ASM_ENTRY(LaunchItem_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(LaunchItem) "addl $24, %esp\n"
                   "ret")

uint8_t* XB_CDECL LaunchItem(uint32_t slot, const float* origin, int32_t model_index, const uint8_t* item,
                             const float* velocity, int32_t index) {
    // Q3's LaunchItem. EAX dropped-item slot, EBX origin; stack (model index for G_SetModel,
    // item, velocity, dropped-item index). A level-context entity (the inlined G_Spawn), its
    // handle in the slot's dropped-item table (0x00985e30, 32 per slot; G_FreeFarthestDroppedItem
    // frees the old one), item type 2 with its index (+6), the item's name as classname, item
    // bounds and contents as G_SpawnItem, owner = the level's world entity, model, touch 7, a
    // gravity trajectory (type 5) from the origin now; linked. Returns the entity.
    const TitleRegs spawn_regs{0xffffffffu, 0, 0, 0, 0, 0};
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{g_ent_call(0x002d7030, &spawn_regs, 0, nullptr)});  // G_Spawn
    const uint32_t which = G_FreeFarthestDroppedItem(index, 0, slot);
    title_global<uint32_t>(0x00985e30 + ((slot << 5) + which) * 4) = word_at(ent, 0x224);
    const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item));
    ent[0] = 2;
    *reinterpret_cast<uint16_t*>(ent + 6) = static_cast<uint16_t>(static_cast<int32_t>(address - 0x0096bd10) / 0x34);
    title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x0011dd20)(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent + 0x25c)),
        *reinterpret_cast<const uint32_t*>(item + 4));  // ScriptString_AssignCString
    const uint32_t text = word_at(ent, 0x25c);
    word_at(ent, 0x260) = Com_HashKeyNoCase(0, text ? reinterpret_cast<const char*>(uintptr_t{text + 6}) : nullptr);
    word_at(ent, 0x3b0) = address;
    const float minus_one = title_global<float>(0x0083eb34);
    const float one = title_global<float>(0x0083eb20);
    const bool upright = *reinterpret_cast<const uint32_t*>(item + 0x24) == 1;
    field<float>(ent, 0xe0) = minus_one;
    field<float>(ent, 0xe4) = minus_one;
    field<float>(ent, 0xe8) = upright ? minus_one : 0.0f;
    field<float>(ent, 0xf0) = one;
    field<float>(ent, 0xf4) = one;
    field<float>(ent, 0xf8) = upright ? one : title_global<float>(0x0083eba8);
    word_at(ent, 0x10) |= 0x10;
    word_at(ent, 0x134) = 0x407c0108;
    word_at(ent, 0xd4) |= 0x200;
    word_at(ent, 0x1a0) = *reinterpret_cast<const uint32_t*>(
        uintptr_t{*reinterpret_cast<const uint32_t*>(uintptr_t{title_global<uint32_t>(0x0095f534) + 4}) + 0x224});
    title_function<void XB_REGS3 XB_CDECL(int32_t, uint8_t*, uint32_t)>(0x002d7c60)(
        model_index, ent, *reinterpret_cast<const uint32_t*>(item + 0xc));  // G_SetModel
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
    word_at(ent, 0x338) = 7;
    const uint32_t* const from = reinterpret_cast<const uint32_t*>(origin);
    word_at(ent, 0x20) = from[0];
    word_at(ent, 0x24) = from[1];
    word_at(ent, 0x28) = from[2];
    field<float>(ent, 0x34) = 0.0f;
    field<float>(ent, 0x30) = 0.0f;
    field<float>(ent, 0x2c) = 0.0f;
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x18) = 0;
    word_at(ent, 0x1c) = 0;
    word_at(ent, 0x140) = from[0];
    word_at(ent, 0x144) = from[1];
    word_at(ent, 0x148) = from[2];
    word_at(ent, 0x14) = 5;
    word_at(ent, 0x18) = title_global<uint32_t>(0x0098540c);
    const uint32_t* const speed = reinterpret_cast<const uint32_t*>(velocity);
    word_at(ent, 0x2c) = speed[0];
    word_at(ent, 0x30) = speed[1];
    word_at(ent, 0x34) = speed[2];
    word_at(ent, 0x2a4) = 0x40;
    SV_LinkEntity(ent);
    return ent;
}

namespace {

uint8_t* item_pointer(uint8_t* base, uint32_t offset) {
    return reinterpret_cast<uint8_t*>(uintptr_t{*reinterpret_cast<uint32_t*>(base + offset)});
}

}  // namespace

PLATFORM_ASM_ENTRY(Drop_Item_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Drop_Item) "addl $16, %esp\n"
                   "ret")

uint8_t* XB_CDECL Drop_Item(uint8_t* ent, uint32_t still, const uint8_t* item, float angle) {
    // Q3's Drop_Item. ESI entity, EAX "no throw", cdecl (item, yaw offset): thrown at 150 along
    // the yaw (+0x154 + angle) and 200 +- 50 (title rand) up, else at rest; from the box's middle
    // height; LaunchItem in the level context. Returns the item entity.
    float yaw = field<float>(ent, 0x154) + angle;
    float velocity[3];
    if (still) {
        velocity[2] = 0.0f;
        velocity[1] = 0.0f;
        velocity[0] = 0.0f;
    } else {
        yaw = yaw * title_global<float>(0x0083edd8);
        float sine, cosine, sine0, cosine0;
        FastSinCos(&sine, 0, &cosine, yaw);
        FastSinCos(&sine0, 0, &cosine0, 0.0f);
        const float down = 0.0f - sine0;
        const float speed = title_global<float>(0x0083ebf0);
        velocity[0] = cosine0 * cosine * speed;
        velocity[1] = cosine0 * sine * speed;
        const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
        const float up = down * speed;
        velocity[2] = (static_cast<float>(r) * title_global<float>(0x0083ecf8) - title_global<float>(0x0083eb20)) *
                          title_global<float>(0x0083ec30) +
                      up + title_global<float>(0x0083ef08);
    }
    float origin[3];
    origin[0] = field<float>(ent, 0x140);
    origin[1] = field<float>(ent, 0x144);
    origin[2] = (field<float>(ent, 0xf8) - field<float>(ent, 0xe8)) * title_global<float>(0x0083eb24) +
                field<float>(ent, 0x148);
    const int32_t context = *reinterpret_cast<const int32_t*>(uintptr_t{title_global<uint32_t>(0x0095f638) + 0x34});
    return LaunchItem(0, origin, context, item, velocity, -1);
}

void XB_REGS1 XB_CDECL TossClientItems(uint8_t* ent) {
    // Q3's TossClientItems: the held weapon (entity +3; the player state's +0x52a in state 2 (+0x84))
    // when owned (bit in ps +0x3f0), valid, with ammo (+0x110) or clip (+0x280) and no
    // weapon-change flags (ps +0xc0 & 0x6000), dropped at rest (Drop_Item) with no think time.
    uint8_t* const ps = item_pointer(ent, 0x23c);
    int32_t weapon = ent[3];
    if (word_at(ps, 0x84) == 2) weapon = ps[0x52a];
    if (!(word_at(ps, 0x3f0 + (weapon >> 5) * 4) & (1u << (weapon & 0x1f)))) return;
    if (weapon <= 0 || weapon > title_global<int32_t>(0x0095f4d4)) return;
    uint8_t* const def = reinterpret_cast<uint8_t*>(uintptr_t{
        *reinterpret_cast<const uint32_t*>(uintptr_t{title_global<uint32_t>(0x0095f4d8) + static_cast<uint32_t>(weapon) * 4})});
    if (!word_at(ps, 0x110 + word_at(def, 0x598) * 4) && !word_at(ps, 0x280 + word_at(def, 0x5a0) * 4)) return;
    if (word_at(ps, 0xc0) & 0x6000) return;
    const TitleRegs regs{0, 0, 0, 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent)), 0};
    const uint32_t args[2] = {0x0096bd10 + static_cast<uint32_t>(weapon) * 0x34, 0};
    uint8_t* const dropped = reinterpret_cast<uint8_t*>(uintptr_t{g_ent_call(0x00264f60, &regs, 2, args)});  // Drop_Item
    if (dropped) word_at(dropped, 0x328) = 0;
}

void XB_CDECL ClientDisconnect(uint32_t handle) {
    // Q3's ClientDisconnect: the client's items tossed while playing (+0x520 == 2), the entity
    // freed, the connection state cleared.
    const uint32_t index = handle & 0xfff;
    uint8_t* ent = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        ent = title_global<uint8_t*>(0x0097a968 + index * 8);
    uint8_t* const client = item_pointer(ent, 0x23c);
    if (word_at(client, 0x520) == 2) TossClientItems(ent);
    title_function<void XB_CDECL(uint8_t*, uint32_t)>(0x002be420)(ent, 0);  // G_FreeEntity
    word_at(client, 0x520) = 0;
}

void XB_REGS3 XB_CDECL Scn_InitUpdateGroups(uint32_t, uint8_t* zone, uint8_t* scene) {
    auto word = [](uint8_t* base, uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(base + offset); };
    if (word(zone, 0x38)) word(scene, 0x190) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(zone + 0x38));
    for (uint32_t i = 0; i < word(zone, 0x30); ++i) {
        const uint32_t index = i < word(zone, 0x30) ? i : 0;
        uint8_t* const group = reinterpret_cast<uint8_t*>(uintptr_t{word(zone, 0x34)}) + index * 0x60;
        word(group, 0x54) = 0;
        *reinterpret_cast<float*>(group + 0x58) = static_cast<float>(static_cast<int32_t>(word(scene, 0x19c)));
        const int32_t slot = static_cast<int32_t>(word(scene, 0x198));
        word(scene, 0x198) = static_cast<uint32_t>(slot + 1);
        if (slot > 0x14) {
            const uint32_t frame = word(scene, 0x19c) + 1;
            word(scene, 0x198) = 0;
            word(scene, 0x19c) = frame;
        }
    }
}

void XB_REGS1 XB_CDECL SP_worldspawn(uint8_t* spawn) {
    auto text = [](uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); };
    auto field = [&](uint32_t offset) { return *reinterpret_cast<const char* const*>(spawn + offset); };
    SV_SetConfigstring(2, text(0x0083366c));  // "cod-sp"
    SV_SetConfigstring(3, field(0xe0));
    SV_SetConfigstring(4, field(0xe4));
    const char* const gravity = field(0xe8);
    Cvar_Set2(text(0x0083365c), const_cast<char*>(gravity ? gravity : text(0x00833668)), 1);  // g_gravity, "800"
    const char* const fog = field(0xec);
    SV_SetConfigstring(0xb, fog ? fog : text(0x0082e098));
    uint8_t* const world = *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 8);
    using Assign = void XB_THISCALL(uint32_t*, const uint32_t*);
    uint32_t* const classname = reinterpret_cast<uint32_t*>(world + 0x25c);
    title_function<Assign>(0x0011d600)(classname, &title_global<uint32_t>(0x00984c88));  // ScriptString_Assign
    const uint32_t name = *classname;
    *reinterpret_cast<uint32_t*>(world + 0x260) =
        Com_HashKeyNoCase(0, name ? reinterpret_cast<const char*>(uintptr_t{name + 6}) : nullptr);
    G_UpdateEntityStringHashes(world);
}

namespace {

uint32_t addr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// G_SpawnGEntityFromSpawnVars (0x002edf30): EDX level, ECX spawn index, the zone on the stack (callee pops); run at
// the original's ESP (`top`): spawn functions below keep stack addresses (the path query's list, 0x00929b40).
void spawn_from_spawn_vars(uint32_t top, uint8_t* level, uint32_t index, uint32_t zone) {
    const TitleRegs regs{0, 0, index, addr(level), 0, 0};
    title_call_at_regs(top, 0x002edf30, &regs, 1, &zone);
}

}  // namespace

// Both spawn loops run their calls where the original's are (below its saved registers).
PLATFORM_ASM_ENTRY(G_SpawnEntitiesFromString_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_SpawnEntitiesFromString) "addl $8, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $4")

void XB_CDECL G_SpawnEntitiesFromString(uint32_t entry, uint8_t* level) {
    // Every zone (99) with spawn data: worldspawn, the entities, then the vehicle nodes.
    const uint32_t top = entry - 0x10;
    for (uint32_t zone = 0; zone < 0x63; ++zone) {
        uint8_t* const spawns = reinterpret_cast<uint8_t*>(uintptr_t{*reinterpret_cast<uint32_t*>(level + 4 + zone * 4)});
        if (!spawns) continue;
        if (const uint32_t world = *reinterpret_cast<uint32_t*>(spawns + 0xc)) {
            const TitleRegs regs{world, zone, 0, 0, addr(spawns), 0};
            title_call_at_regs(top, 0x002c5e30, &regs, 0, nullptr);  // SP_worldspawn
        }
        for (uint32_t i = 0; i < *reinterpret_cast<uint32_t*>(spawns + 0x10); ++i)
            spawn_from_spawn_vars(top, level, i, zone);
        for (uint32_t i = 0; i < *reinterpret_cast<uint32_t*>(spawns + 0x28); ++i) {
            const TitleRegs regs{addr(level), zone, zone, i, addr(spawns), i};
            title_call_at_regs(top, 0x002b89f0, &regs, 0, nullptr);  // G_SpawnVehicleNode
        }
    }
}

PLATFORM_ASM_ENTRY(Scn_SpawnZone_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Scn_SpawnZone) "addl $12, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $8")

void XB_CDECL Scn_SpawnZone(uint32_t entry, uint8_t* level, uint32_t zone) {
    uint8_t* const spawns = reinterpret_cast<uint8_t*>(uintptr_t{*reinterpret_cast<uint32_t*>(level + 4 + zone * 4)});
    const uint32_t top = entry - 0xc;
    // Static models (0xec bytes each), model instance groups (0x10 bytes), the entities while the client loads
    // (cls_state 2 or 5), then the zone's update groups.
    for (uint32_t i = 0; i < *reinterpret_cast<uint32_t*>(spawns + 0x18); ++i) {
        const uint32_t index = i < *reinterpret_cast<uint32_t*>(spawns + 0x18) ? i : 0;
        const uint32_t models = *reinterpret_cast<uint32_t*>(spawns + 0x1c);
        const TitleRegs regs{0, models + index * 0xec, zone, models, addr(spawns), i};
        title_call_at_regs(top - 4, 0x002d9970, &regs, 0, nullptr);  // Scn_AddStaticModel
    }
    for (uint32_t i = 0; i < *reinterpret_cast<uint32_t*>(spawns + 0x20); ++i) {
        const uint32_t index = i < *reinterpret_cast<uint32_t*>(spawns + 0x20) ? i : 0;
        const TitleRegs regs{index * 0x10, 0, *reinterpret_cast<uint32_t*>(spawns + 0x24), 0, addr(spawns), i};
        const uint32_t args[2] = {zone, *reinterpret_cast<uint32_t*>(spawns + 0x24) + index * 0x10};
        title_call_at_regs(top, 0x002d90f0, &regs, 2, args);  // Scn_AddModelInstances
    }
    const uint32_t state = title_global<uint32_t>(0x0092bdf0);  // cls_state
    if (state == 2 || state == 5) {
        for (uint32_t i = 0; i < *reinterpret_cast<uint32_t*>(spawns + 0x10); ++i)
            spawn_from_spawn_vars(top, level, i, zone);
    }
    Scn_InitUpdateGroups(0, spawns, level);
}

namespace {

uint32_t& word(uint8_t* p) { return *reinterpret_cast<uint32_t*>(p); }
uint32_t word(const uint8_t* p) { return *reinterpret_cast<const uint32_t*>(p); }
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(address)); }

// A callee where the original's call puts it (ESP = top - 4 * count), with the original's return
// address written back into the dead stack.
uint32_t spawn_call_at(uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count,
                       const uint32_t* args) {
    const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
    stack_word(top - count * 4 - 4) = ret;
    return result;
}

const char* text_of(uint32_t p) { return reinterpret_cast<const char*>(uintptr_t{p}); }

int32_t spawn_stricmp(uint32_t a, uint32_t b) {
    return title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(text_of(a), text_of(b));  // __stricmp
}

}  // namespace

// EDX level, ECX spawn index, the zone on the stack (callee pops). The spawn functions and other deep callees run where
// the original's calls are (body ESP = entry - 0x28): the spawn code keeps stack addresses (0x00929b40).
PLATFORM_ASM_ENTRY(G_SpawnGEntityFromSpawnVars_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_SpawnGEntityFromSpawnVars) "addl $16, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $4")

void XB_CDECL G_SpawnGEntityFromSpawnVars(uint32_t entry, uint8_t* level, uint32_t index, uint32_t zone) {
    const uint32_t top = entry - 0x28;
    uint8_t* const spawns = reinterpret_cast<uint8_t*>(uintptr_t{word(level + 4 + zone * 4)});
    if (index >= word(spawns + 0x10)) index = 0;
    uint8_t* const spawn = reinterpret_cast<uint8_t*>(uintptr_t{word(reinterpret_cast<uint8_t*>(uintptr_t{word(spawns + 0x14)}) + index * 4)});
    const uint32_t scene = word(spawn + 0xc8);
    // Entities whose scene slot (+0xc8) is marked in the level's table (+0x194: count, bytes) are skipped: 2 = gone
    // for a checkpoint start, >= 1 otherwise.
    if (scene != 0xffffffff) {
        const uint8_t* const checkpoint = title_global<uint8_t*>(0x0095f4e0);
        if (checkpoint[0x390] && checkpoint[0]) {
            if (uint8_t* const table = reinterpret_cast<uint8_t*>(uintptr_t{word(level + 0x194)})) {
                const uint32_t slot = scene < word(table) ? scene : 0;
                if (reinterpret_cast<const uint8_t*>(uintptr_t{word(table + 4)})[slot] == 2) return;
            }
        }
        if (uint8_t* const table = reinterpret_cast<uint8_t*>(uintptr_t{word(level + 0x194)})) {
            const uint32_t slot = scene < word(table) ? scene : 0;
            if (reinterpret_cast<const uint8_t*>(uintptr_t{word(table + 4)})[slot] >= 1) return;
        }
    }
    // "transparent" / "noshadow" = "1" among the second key list (+0xdc, hashed keys; bounded by +0xd8, run over +0xd0).
    uint32_t transparent = 0;
    uint32_t noshadow = 0;
    for (uint32_t i = 0; i < word(spawn + 0xd0); ++i) {
        const uint32_t slot = i < word(spawn + 0xd8) ? i : 0;
        const uint8_t* const pair = reinterpret_cast<uint8_t*>(uintptr_t{word(spawn + 0xdc)}) + slot * 8;
        uint32_t& guard = title_global<uint32_t>(0x0098f21c);
        if (!(guard & 1)) {
            guard |= 1;
            title_global<uint32_t>(0x0098f218) = HashStr_Hash(text_of(0x00833540));  // "transparent"
        }
        if (!(guard & 2)) {
            guard |= 2;
            title_global<uint32_t>(0x0098f214) = HashStr_Hash(text_of(0x00833534));  // "noshadow"
        }
        if (word(pair) == title_global<uint32_t>(0x0098f218))
            transparent = spawn_stricmp(word(pair + 4), 0x0082e058) == 0;  // "1"
        else if (word(pair) == title_global<uint32_t>(0x0098f214))
            noshadow = spawn_stricmp(word(pair + 4), 0x0082e058) == 0;
    }
    // "auto_thread" and the "on_<notify>" callbacks (block 0x3c bytes from the 0x0095f568 small heap).
    uint32_t auto_thread = 0;
    uint8_t* block = nullptr;
    for (uint32_t i = 0; i < word(spawn + 0xd0); ++i) {
        const uint32_t slot = i < word(spawn + 0xd0) ? i : 0;
        const uint8_t* const pair = reinterpret_cast<uint8_t*>(uintptr_t{word(spawn + 0xd4)}) + slot * 8;
        if (spawn_stricmp(word(pair), 0x00833528) == 0) {  // "auto_thread"
            auto_thread = word(pair + 4);
            continue;
        }
        if (title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001f1486)(text_of(word(pair)),
                                                                                         text_of(0x00833524), 3))
            continue;  // __strnicmp "on_"
        if (!block) {
            block = SmallHeap_Alloc(title_global<uint8_t*>(0x0095f568), 0x3c, 0);
            if (block) {
                for (uint32_t f = 0; f <= 0x38; f += 4) word(block + f) = 0;
                for (uint32_t f = 0xc; f < 0x3c; f += 8) word(block + f) = 0;
            }
        }
        const uint32_t key = Com_HashKeyNoCase(0, text_of(word(pair) + 3));
        const uint32_t callback = Com_HashKeyNoCase(0, text_of(word(pair + 4)));
        const TitleRegs regs{address_of(block), 0, callback, 0, key, i};
        const uint32_t args[2] = {key, callback};
        spawn_call_at(top, 0x002ee1e7, 0x002b6ea0, regs, 2, args);  // G_EntityAddNotifyCallback
    }
    // The entity from the zone's memory context, else the aligned heap.
    int32_t context = static_cast<int32_t>(zone);
    uint8_t* mem = nullptr;
    if (context == -1) context = static_cast<int32_t>(word(title_global<uint8_t*>(0x0095f638) + 0x34));
    if (context != -1) {
        uint8_t* const heap = context >= 0 && context < 0x63
                                  ? reinterpret_cast<uint8_t*>(uintptr_t{word(title_global<uint8_t*>(0x0095f638) + 0x38 + context * 4)})
                                  : nullptr;
        mem = MemContext_Alloc_r(heap, 0x10, 0x460);
    }
    if (!mem) mem = title_function<uint8_t* XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x10, 0x460);  // Heap_AllocAligned
    uint8_t* ent = nullptr;
    if (mem) {
        const TitleRegs regs{address_of(mem), 0, 0, 0, static_cast<uint32_t>(context), 0};
        const uint32_t args[1] = {static_cast<uint32_t>(context)};
        ent = reinterpret_cast<uint8_t*>(uintptr_t{spawn_call_at(top, 0x002ee24a, 0x002d6ba0, regs, 1, args)});  // G_InitGentity
    }
    ++title_global<int32_t>(0x00985374);
    if (block) word(ent + 0x234) = address_of(block);
    if (transparent) CG_GetEntityRefEntity(ent)[0xef] = 1;
    if (noshadow) CG_GetEntityRefEntity(ent)[0xed] |= 2;
    word(ent + 0x290) = scene;
    {
        const TitleRegs regs{scene, 0, address_of(spawn), 0, address_of(ent), 0};
        const uint32_t args[2] = {address_of(spawn), address_of(ent)};
        spawn_call_at(top, 0x002ee2a0, 0x002d8a90, regs, 2, args);  // G_ApplySpawnFields
    }
    const uint32_t classname = word(ent + 0x25c);
    word(ent + 0x260) = Com_HashKeyNoCase(0, classname ? text_of(classname + 6) : nullptr);
    G_SetSpawnVarsFromList(reinterpret_cast<const uint32_t*>(spawn + 0xd8));
    uint8_t* const checkpoint = title_global<uint8_t*>(0x0095f4e0);
    if (checkpoint[0x390] && checkpoint[0]) {
        const TitleRegs regs{address_of(ent), 0, address_of(checkpoint), 0, address_of(ent), 0};
        spawn_call_at(top, 0x002ee311, 0x002c7f00, regs, 0, nullptr);  // G_ApplyCheckpointStart
    }
    title_global<uint8_t>(0x0095f528) = 1;
    {
        const TitleRegs regs{address_of(ent), 0, address_of(checkpoint), 0, address_of(ent), address_of(ent)};
        spawn_call_at(top, 0x002ee31f, 0x00269cb0, regs, 0, nullptr);  // G_CallSpawnByClassname
    }
    // The trajectories start stationary at the spawned origin (+0x140) and angles (+0x150).
    word(ent + 0x20) = word(ent + 0x140);
    word(ent + 0x24) = word(ent + 0x144);
    word(ent + 0x28) = word(ent + 0x148);
    word(ent + 0x14) = 0;
    word(ent + 0x18) = 0;
    word(ent + 0x1c) = 0;
    word(ent + 0x34) = 0;  // 0.0f
    word(ent + 0x30) = 0;
    word(ent + 0x2c) = 0;
    word(ent + 0x44) = word(ent + 0x150);
    word(ent + 0x48) = word(ent + 0x154);
    word(ent + 0x4c) = word(ent + 0x158);
    word(ent + 0x38) = 0;
    word(ent + 0x3c) = 0;
    word(ent + 0x40) = 0;
    word(ent + 0x58) = 0;
    word(ent + 0x54) = 0;
    word(ent + 0x50) = 0;
    uint8_t* const import = title_global<uint8_t*>(0x0095f49c);
    title_global<uint8_t>(0x0095f528) = 0;
    const uint32_t keys_callback = import ? word(import + 0x804) : 0;
    if (keys_callback) {
        // The game's own key handler (import +0x804) gets the key list (+0xd4, count +0xd0).
        const uint32_t count = word(spawn + 0xd0);
        const uint32_t list = count ? word(spawn + 0xd4) : 0;
        const TitleRegs regs{list, 0, count, keys_callback, address_of(ent), address_of(spawn)};
        const uint32_t args[2] = {list, count};
        word(ent + 0x428) = spawn_call_at(top, count ? 0x002ee401 : 0x002ee412, keys_callback, regs, 2, args);
    } else {
        word(ent + 0x428) = 0;
    }
    {
        const TitleRegs regs{0, 0, 0, 0, address_of(ent), address_of(spawn)};
        spawn_call_at(top, 0x002ee42c, 0x002fb310, regs, 0, nullptr);  // G_UpdateEntityStringHashes
    }
    if (auto_thread) {
        // The auto thread started through import +0x880 (name hash, entity handle, two empty words, the script's
        // undefined float twice).
        const uint32_t hash = Com_HashKeyNoCase(0, text_of(auto_thread));
        const uint32_t undefined = title_global<uint32_t>(0x00922eb8);
        const uint32_t handle = word(ent + 0x224);
        const TitleRegs regs{hash, 0, handle, top - 0x14, address_of(ent), address_of(spawn)};
        const uint32_t args[6] = {hash, handle, 0, 0, undefined, undefined};
        spawn_call_at(top, 0x002ee490, word(title_global<uint8_t*>(0x0095f49c) + 0x880), regs, 6, args);
    }
    if (const uint32_t callbacks = word(ent + 0x234)) {
        const uint32_t key = title_global<uint32_t>(0x0097a7e4);  // scr_const_spawned
        const TitleRegs regs{key, 0, callbacks, 0, address_of(ent), address_of(spawn)};
        const uint32_t args[3] = {address_of(ent), key, 0};
        spawn_call_at(top, 0x002ee4aa, 0x002b6de0, regs, 3, args);  // G_EntityRunNotifyCallbacks
    }
    // A brush model ("sbmo..." at +0x14) gets flag 0x800000.
    const uint32_t model = word(spawn + 0x14);
    if (model && word(reinterpret_cast<uint8_t*>(uintptr_t{model})) == 0x6f6d6273) word(ent + 0x2a4) |= 0x800000;
}

void XB_REGS3 XB_CDECL G_SpawnVehicleNode(uint8_t* spawns, uint32_t item, uint32_t slot) {
    // A vehicle node from spawn record `item` (0x38 bytes; index past the count reads 0) of the
    // level's list (+4 + slot * 4: count +0x28, records +0x2c): names (+0 / +4 / +8 into node +0 /
    // +4 / +0x10), origin, angles, speed, +0xc and two flag bits (+0x2c into +0x3c bits 28-29); then
    // every key/value pair (+0x30 count, +0x34 pairs) by the interface's node setters (+0x83c int,
    // +0x844 "script_delay" float, +0x834 names as a script string); unknown keys only format a
    // warning. An unnamed node reports itself (__break); a speed >= 0 is scaled by 17.6 (mph).
    using AssignCString = void XB_THISCALL(uint8_t*, const char*);
    const uint8_t* const list = *reinterpret_cast<uint8_t* const*>(spawns + slot * 4 + 4) + 0x28;
    if (item >= *reinterpret_cast<const uint32_t*>(list)) item = 0;
    const uint8_t* const spawn = *reinterpret_cast<uint8_t* const*>(list + 4) + item * 0x38;
    uint8_t* const node = VehNode_Alloc();
    for (uint32_t i = 0; i < 3; ++i) {
        static const uint32_t kNames[3] = {0, 4, 0x10};
        const char* const text = *reinterpret_cast<const char* const*>(spawn + i * 4);
        if (text != nullptr) title_function<AssignCString>(0x0011dd20)(node + kNames[i], text);
    }
    const uint32_t flags = field<uint32_t>(node, 0x3c);
    field<uint32_t>(node, 0x14) = *reinterpret_cast<const uint32_t*>(spawn + 0xc);
    field<uint32_t>(node, 0x18) = *reinterpret_cast<const uint32_t*>(spawn + 0x10);
    field<uint32_t>(node, 0x1c) = *reinterpret_cast<const uint32_t*>(spawn + 0x14);
    field<uint32_t>(node, 0x2c) = *reinterpret_cast<const uint32_t*>(spawn + 0x18);
    field<uint32_t>(node, 0x30) = *reinterpret_cast<const uint32_t*>(spawn + 0x1c);
    field<uint32_t>(node, 0x34) = *reinterpret_cast<const uint32_t*>(spawn + 0x20);
    field<uint32_t>(node, 0x8) = *reinterpret_cast<const uint32_t*>(spawn + 0x24);
    field<uint32_t>(node, 0x3c) = flags ^ (((*reinterpret_cast<const uint32_t*>(spawn + 0x2c) << 28) ^ flags) & 0x30000000);
    field<uint32_t>(node, 0xc) = *reinterpret_cast<const uint32_t*>(spawn + 0x28);
    using Stricmp = int32_t XB_CDECL(const char*, const char*);
    const auto is = [](const char* key, uint32_t name) {
        return title_function<Stricmp>(0x001f1443)(key, reinterpret_cast<const char*>(uintptr_t{name})) == 0;
    };
    for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(spawn + 0x30); ++i) {
        const uint32_t count = *reinterpret_cast<const uint32_t*>(spawn + 0x30);
        const uint32_t pair = i < count ? i : 0;
        const char* const* const pairs = *reinterpret_cast<const char* const* const*>(spawn + 0x34);
        const char* const key = pairs[pair * 2];
        const char* const value = pairs[pair * 2 + 1];
        const uint32_t hash = HashStr_Hash(key);
        const int32_t number = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(value);  // _atol
        const float real = title_function<float XB_CDECL(const char*)>(0x001ef056)(value);         // atof
        int32_t index = -1;
        for (int32_t n = 0; n < title_global<int32_t>(0x0094d4d8); ++n) {  // g_numVehNodes
            if (title_global<uint8_t*>(0x0095c228 + n * 4) == node) {       // g_vehNodes
                index = n;
                break;
            }
        }
        uint8_t* const import = title_global<uint8_t*>(0x0095f49c);
        using SetInt = void XB_CDECL(int32_t, uint32_t, int32_t);
        using SetFloat = void XB_CDECL(int32_t, uint32_t, float);
        using SetName = void XB_CDECL(int32_t, uint32_t, uint32_t);
        if (is(key, 0x0083364c) || is(key, 0x00833638) || is(key, 0x0083362c) || is(key, 0x00833620) ||
            is(key, 0x00833614)) {
            // "friendlywait", "playerhasbeenhere", "detoured", "detourstart", "detourpath"
            title_function<SetInt>(field<uint32_t>(import, 0x83c))(index, hash, number);
        } else if (is(key, 0x00833608) || is(key, 0x008335fc)) {  // "endswitch", "startswitch"
            title_function<SetInt>(field<uint32_t>(import, 0x83c))(index, hash, number);
        } else if (is(key, 0x00833a28)) {  // "script_delay"
            title_function<SetFloat>(field<uint32_t>(import, 0x844))(index, hash, real);
        } else if (is(key, 0x008335e8) || is(key, 0x008335dc)) {  // "crash_path_target", "derailed"
            title_function<SetInt>(field<uint32_t>(import, 0x83c))(index, hash, number);
        } else if (is(key, 0x008335c8) || is(key, 0x008335bc)) {  // "script_uniquename", "groupname"
            uint32_t name = 0;  // the script string handed over by value (the callee releases it)
            title_function<void XB_THISCALL(uint32_t*, const char*)>(0x0011eb50)(&name, value);  // ScriptString_FromCString
            title_function<SetName>(field<uint32_t>(import, 0x834))(index, hash, name);
        } else {
            char warning[0x400];
            title_function<int32_t XB_CDECL(char*, const char*, const char*, const char*)>(0x001ee901)(
                warning, reinterpret_cast<const char*>(uintptr_t{0x00833584}), key, value);  // _sprintf
        }
    }
    const uint8_t* const name = *reinterpret_cast<uint8_t* const*>(node);
    if (name == nullptr || name[6] == 0) {  // the rep's text (+6)
        const double x = field<float>(node, 0x14);
        const double y = field<float>(node, 0x18);
        const double z = field<float>(node, 0x1c);
        title_function<void XB_CDECL(int32_t, const char*, double, double, double)>(0x002938f0)(
            1, reinterpret_cast<const char*>(uintptr_t{0x0083354c}), x, y, z);  // __break: "... found with no name"
    }
    if (field<float>(node, 8) >= title_global<float>(0x0083eafc)) {
        field<float>(node, 8) = field<float>(node, 8) * title_global<float>(0x0083ee64);  // 17.6
    }
}

PLATFORM_ASM_ENTRY(Drop_Weapon_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Drop_Weapon) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL Drop_Weapon(uint8_t* ent, uint32_t weapon, const char* tag) {
    // Q3's Drop_Weapon: none for defs with +0x6f8 or type 4 (+0x94). The item (0x0096bd10 +
    // weapon * 0x34, Drop_Item) carries the ammo (+0x370) and clip (+0x3bc; -1 for none): a
    // player's (ammo slot +0x598, clip slot +0x5a0; both cleared, BG_RemoveWeaponFromSlot), else
    // random counts from the def's range (+0x71c..+0x720) or a random share of the clip size
    // (0x00935338). From a tag (hash cached once in 0x0098f1bc) the item starts at the tag,
    // traced from the entity's centre (capsule, mask 0x411); then the entity's angles and a
    // random spin (type 2, (50, 40, 60) * [-1, 1)).
    using Rand = int32_t XB_CDECL();
    const auto rand_ = [] { return title_function<Rand>(0x001efcc0)(); };
    const auto def_of = [](uint32_t w) {
        return reinterpret_cast<const uint8_t*>(uintptr_t{title_global<uint32_t*>(0x0095f4d8)[w]});
    };
    const uint8_t* def = def_of(weapon);
    if (*reinterpret_cast<const uint32_t*>(def + 0x6f8) != 0 || *reinterpret_cast<const uint32_t*>(def + 0x94) == 4) {
        return 0;
    }
    uint8_t* drop;
    {
        const TitleRegs regs{0, 0, 0, 0, address_of(ent), address_of(ent)};
        const uint32_t args[2] = {0x0096bd10 + weapon * 0x34, 0};
        drop = reinterpret_cast<uint8_t*>(uintptr_t{g_ent_call(0x00264f60, &regs, 2, args)});  // Drop_Item
    }
    def = def_of(weapon);
    const uint32_t clip_slot = *reinterpret_cast<const uint32_t*>(def + 0x5a0);
    const uint32_t ammo_slot = *reinterpret_cast<const uint32_t*>(def + 0x598);
    int32_t ammo;
    int32_t clip;
    if (field<uint32_t>(ent, 0x23c) != 0) {
        uint8_t* client = pointer_at(ent, 0x23c);
        ammo = field<int32_t>(client, 0x110 + ammo_slot * 4);
        field<int32_t>(client, 0x110 + ammo_slot * 4) = 0;
        client = pointer_at(ent, 0x23c);
        clip = field<int32_t>(client, 0x280 + clip_slot * 4);
        field<int32_t>(client, 0x280 + clip_slot * 4) = 0;
        BG_RemoveWeaponFromSlot(weapon, pointer_at(ent, 0x23c));
    } else {
        int32_t high = *reinterpret_cast<const int32_t*>(def + 0x720);
        int32_t low = *reinterpret_cast<const int32_t*>(def + 0x71c);
        if (high < low) {
            high = low;
            low = *reinterpret_cast<const int32_t*>(def + 0x720);
        }
        const int32_t clip_size = title_global<int32_t>(0x00935338 + clip_slot * 4);
        if (high == 0 && low == 0) {
            // A random total of 1..clip size (rounded), a random quarter..more of it in the clip.
            const float span = static_cast<float>(clip_size - 1);
            float t = static_cast<float>(rand_()) * title_global<float>(0x0083ebe0) + title_global<float>(0x0083eb20);
            t = t * span;
            const float half = title_global<float>(0x0083eb24);
            t = t * half;
            t = t + half;
            ammo = static_cast<int32_t>(t) + 1;
            float c = static_cast<float>(rand_()) * title_global<float>(0x0083f1e8) + title_global<float>(0x0083ec24);
            c = c * static_cast<float>(ammo);
            c = c + title_global<float>(0x0083eb24);
            clip = static_cast<int32_t>(c);
            ammo -= clip;
        } else if (high < 0) {
            ammo = 0;
            clip = 0;
        } else {
            int32_t total;
            if (high == low) {
                total = low;
            } else {
                total = rand_() % (high - low) + low;
            }
            if (total <= 0) {
                ammo = 0;
                clip = 0;
            } else {
                clip = clip_size != 0 ? rand_() % clip_size : 0;
                if (clip < total) {
                    ammo = total - clip;
                } else {
                    clip = total;
                    ammo = 0;
                }
            }
        }
    }
    field<int32_t>(drop, 0x370) = ammo;
    field<int32_t>(drop, 0x3bc) = clip;
    if (ammo == 0) field<int32_t>(drop, 0x370) = -1;
    if (clip == 0) field<int32_t>(drop, 0x3bc) = -1;
    if (tag == nullptr) return address_of(drop);
    uint32_t& guard = title_global<uint32_t>(0x0098f1c0);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098f1bc) = Com_HashKeyNoCase(0, tag);
    }
    const DObj* const obj = reinterpret_cast<const DObj*>(uintptr_t{field<uint32_t>(ent, 0x22c)});
    if (obj != nullptr) {
        const int32_t bone = DObj_GetTagIndex(obj, title_global<uint32_t>(0x0098f1bc));
        if (bone >= 0) {
            alignas(16) float matrix[16];
            G_DObjGetWorldTagMatrix(ent, static_cast<uint32_t>(bone), matrix);
            const float half = title_global<float>(0x0083eb24);
            alignas(16) float start[4] = {};
            start[1] = field<float>(ent, 0x144) + (field<float>(ent, 0xf4) + field<float>(ent, 0xe4)) * half;
            start[2] = field<float>(ent, 0x148) + (field<float>(ent, 0xf8) + field<float>(ent, 0xe8)) * half;
            start[0] = field<float>(ent, 0x140) + (field<float>(ent, 0xf0) + field<float>(ent, 0xe0)) * half;
            // The end point as the original's 4-lane copy: tag origin x, y, z and x again.
            alignas(16) float end[4] = {matrix[12], matrix[13], matrix[14], matrix[12]};
            alignas(16) float trace[0x20] = {};
            using Trace = void XB_CDECL(float*, const float*, const float*, const float*, const float*, uint32_t, uint32_t);
            title_function<Trace>(0x002601a0)(trace, start, reinterpret_cast<const float*>(drop + 0xe0),
                                              reinterpret_cast<const float*>(drop + 0xf0), end,
                                              field<uint32_t>(ent, 0x224), 0x411);  // G_TraceCapsule
            for (uint32_t i = 0; i < 3; ++i) {
                field<float>(drop, 0x20 + i * 4) = trace[i];
            }
            for (uint32_t i = 0; i < 3; ++i) {
                field<float>(drop, 0x140 + i * 4) = trace[i];
            }
            field<int32_t>(drop, 0x18) = title_global<int32_t>(0x0098540c);
        }
    }
    G_SetAngle(drop, 0, reinterpret_cast<const float*>(ent + 0x150));
    const float spin_scale = title_global<float>(0x0083ecf8);  // 1 / 16384
    const float one = title_global<float>(0x0083eb20);
    const float spin_x = (static_cast<float>(rand_()) * spin_scale - one) * title_global<float>(0x0083ec30);  // 50
    const float spin_y = (static_cast<float>(rand_()) * spin_scale - one) * title_global<float>(0x0083ec84);  // 40
    const float spin_z = (static_cast<float>(rand_()) * spin_scale - one) * title_global<float>(0x0083eb3c);  // 60
    field<uint32_t>(drop, 0x38) = 2;
    field<float>(drop, 0x50) = spin_x;
    field<int32_t>(drop, 0x3c) = title_global<int32_t>(0x0098540c);
    field<float>(drop, 0x54) = spin_y;
    field<float>(drop, 0x58) = spin_z;
    return address_of(drop);
}

PLATFORM_ASM_ENTRY(Scn_AddStaticModel_entry,
                   "pushl %ebx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Scn_AddStaticModel) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Scn_AddStaticModel(uint32_t zone, uint8_t* inst) {
    // A static model instance (0xec bytes): the zone (+0xe0), the model registered by name once
    // (+0x68 = {name, model}, then the model itself); unless +0xe8, its box (axis +0x6c, origin
    // +0x90) linked into the world cells (0x0095e608 +0x100 = cm). With at most 64 bones (the first
    // LOD with parts: +0x24.., part +8, count +0x10) the bone matrices (0x0098cbd0, built once) and
    // the hit-box bounds scaled (+0x9c) around the origin into +0xa0 / +0xac; linked to its sector
    // when the model has +0x40.
    field<uint32_t>(inst, 0xe0) = zone;
    uint8_t* entry = pointer_at(inst, 0x68);
    if (entry == nullptr) return;
    if (field<uint32_t>(entry, 4) == 0) {
        uint32_t model = 0;
        R_RegisterModel(reinterpret_cast<const char*>(uintptr_t{field<uint32_t>(entry, 0)}),
                        title_global<uint32_t>(0x0095f6ac), &model, zone);  // rm_models
        field<uint32_t>(pointer_at(inst, 0x68), 4) = model;
    }
    field<uint32_t>(inst, 0x68) = field<uint32_t>(pointer_at(inst, 0x68), 4);
    const float* const origin = reinterpret_cast<const float*>(inst + 0x90);
    alignas(16) float box[8];  // the original's stack block: origin copy / hit-box mins +0, maxs +0x10
    if (field<uint32_t>(inst, 0xe8) == 0) {
        alignas(16) float bounds[8];
        for (uint32_t i = 0; i < 4; ++i) bounds[i] = title_global<float>(0x0082eed0);      // FLT_MAX
        for (uint32_t i = 0; i < 4; ++i) bounds[4 + i] = title_global<float>(0x0083ef2c);  // -FLT_MAX
        Bounds_AddPoints8(reinterpret_cast<const float*>(inst + 0x6c), reinterpret_cast<const float*>(pointer_at(inst, 0x68)),
                          bounds);
        // Both corners move by the origin as 4 lanes (x, y, z, x).
        for (uint32_t c = 0; c < 2; ++c) {
            const float lanes[4] = {origin[0], origin[1], origin[2], origin[0]};
            for (uint32_t i = 0; i < 4; ++i) bounds[c * 4 + i] = bounds[c * 4 + i] + lanes[i];
        }
        uint8_t* const world = title_global<uint8_t*>(0x0095e608);
        field<uint32_t>(world, 0x100) = title_global<uint32_t>(0x0095f6b8);  // cm
        uint8_t* const cells = title_global<uint8_t*>(0x0095e608);
        const uint8_t* const node = reinterpret_cast<const uint8_t*>(uintptr_t{field<uint32_t>(pointer_at(cells, 0x100), 0xc)});
        R_LinkEntityToCells_r(cells, node, inst, bounds, bounds + 4);
    }
    uint8_t* const model = pointer_at(inst, 0x68);
    const auto first_lod = [&]() {
        uint32_t k = 0;
        if (field<uint32_t>(model, 0x24) == 0) {
            do ++k;
            while (field<uint32_t>(model, 0x24 + k * 4) == 0);
        }
        return pointer_at(model, 0x24 + k * 4);
    };
    int32_t bones = 0;
    if (field<uint32_t>(first_lod(), 8) != 0) bones = field<int32_t>(pointer_at(first_lod(), 8), 0x10);
    uint32_t& guard = title_global<uint32_t>(0x0098dbd0);
    if (!(guard & 1)) {
        guard |= 1;
        title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00011240)(0x0098cbd0, 0x40, 0x40,
                                                                                         0x0001b920);  // eh_vector_constructor_iterator
    }
    if (bones > 0x40) return;
    float* const matrices = reinterpret_cast<float*>(uintptr_t{0x0098cbd0});
    R_ModelBoneMatrices(model, matrices);
    {
        const TitleRegs regs{0, 0, address_of(inst + 0x6c), 0, 0, 0};
        const uint32_t args[4] = {address_of(model), address_of(box), address_of(box + 4), 0x0098cbd0};
        if (g_ent_call(0x00240f70, &regs, 4, args) == 0) return;  // R_ModelBoxBounds
    }
    const float scale = field<float>(inst, 0x9c);
    field<float>(inst, 0xa0) = scale * box[0];
    field<float>(inst, 0xa4) = field<float>(inst, 0x9c) * box[1];
    field<float>(inst, 0xa8) = field<float>(inst, 0x9c) * box[2];
    field<float>(inst, 0xac) = field<float>(inst, 0x9c) * box[4];
    field<float>(inst, 0xb0) = field<float>(inst, 0x9c) * box[5];
    field<float>(inst, 0xb4) = field<float>(inst, 0x9c) * box[6];
    field<float>(inst, 0xa0) = field<float>(inst, 0x90) + field<float>(inst, 0xa0);
    field<float>(inst, 0xa4) = field<float>(inst, 0x94) + field<float>(inst, 0xa4);
    field<float>(inst, 0xa8) = field<float>(inst, 0xa8) + field<float>(inst, 0x98);
    field<float>(inst, 0xac) = field<float>(inst, 0x90) + field<float>(inst, 0xac);
    field<float>(inst, 0xb0) = field<float>(inst, 0x94) + field<float>(inst, 0xb0);
    field<float>(inst, 0xb4) = field<float>(inst, 0xb4) + field<float>(inst, 0x98);
    if (field<uint32_t>(pointer_at(inst, 0x68), 0x40) != 0) SV_LinkStaticModelToSector(inst);
}

namespace {

// G_ApplySpawnFields' field table (built on its stack): spawn offset, entity offset, size, kind,
// present bit (spawn +0xcc bit set).
struct SpawnField {
    uint32_t from, to, size, kind, bit;
};
const SpawnField kSpawnFields[28] = {
    {0x04, 0x25c, 4, 0xe, 0x00}, {0x08, 0x140, 0xc, 5, 0x01}, {0x14, 0x258, 4, 0xc, 0x02},
    {0x18, 0x2a0, 4, 0x0, 0x03}, {0x1c, 0x2fc, 4, 3, 0x04},   {0x20, 0x300, 4, 3, 0x05},
    {0x24, 0x26c, 4, 0xe, 0x06}, {0x28, 0x264, 4, 0xe, 0x07}, {0x38, 0x2f8, 2, 4, 0x08},
    {0x3c, 0x384, 4, 3, 0x09},   {0x40, 0x388, 4, 3, 0x0a},   {0x44, 0x370, 4, 0, 0x0b},
    {0x48, 0x350, 4, 0, 0x0c},   {0x4c, 0x358, 4, 0, 0x0d},   {0x50, 0x150, 0xc, 5, 0x0e},
    {0x60, 0x390, 0xc, 5, 0x0f}, {0x6c, 0x2f4, 4, 3, 0x10},   {0x70, 0x2fc, 4, 3, 0x11},
    {0x8c, 0x3b4, 4, 0, 0x12},   {0x90, 0x38c, 4, 3, 0x13},   {0xa0, 0x370, 4, 0, 0x14},
    {0xa4, 0x3b8, 4, 0xe, 0x15}, {0xac, 0x274, 4, 0xe, 0x16}, {0xb4, 0x27c, 4, 0xe, 0x17},
    {0xb8, 0x354, 4, 0, 0x18},   {0xc0, 0x284, 4, 0xe, 0x19}, {0xc8, 0x290, 4, 0, 0x1a},
    {0xc4, 0x29c, 4, 0, 0x1b},
};

}  // namespace

void XB_STDCALL G_ApplySpawnFields(const uint8_t* spawn, uint8_t* ent) {
    // The parsed spawn fields present in the spawn record (+0xcc bits) copied to the entity, by
    // kind: plain copies (0, 1, 2, 3, 5, 15), a zeroed short (4), script strings (14: the text
    // assigned as a script string), models (12: "*N" = brush model N in +6; else R_RegisterModel
    // in the entity's zone into +0x258, and "sbmodel"-style "sbmo..." names also take the number
    // after 7 characters; "sbco..." names set +0x2a4 bit 0x400 and only take the number).
    for (const SpawnField& f : kSpawnFields) {
        if (!(*reinterpret_cast<const uint32_t*>(spawn + 0xcc + (static_cast<int32_t>(f.bit) >> 5) * 4) &
              (1u << (f.bit & 0x1f))))
            continue;
        switch (f.kind) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 5:
        case 15:
            for (uint32_t i = 0; i < f.size; ++i) ent[f.to + i] = spawn[f.from + i];
            break;
        case 4: *reinterpret_cast<uint16_t*>(ent + f.to) = 0; break;
        case 14: {
            uint32_t handle;
            const uint32_t text = *reinterpret_cast<const uint32_t*>(spawn + f.from);
            const uint32_t made = title_function<uint32_t XB_THISCALL(uint32_t*, uint32_t)>(0x0011eb50)(
                &handle, text);  // ScriptString_FromCString
            title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x0011d600)(ent + f.to, made);  // ScriptString_Assign
            title_function<void XB_THISCALL(uint32_t*)>(0x0011d640)(&handle);  // ScriptString_Release
            break;
        }
        case 12: {
            const char* const name = text_of(*reinterpret_cast<const uint32_t*>(spawn + f.from));
            if (name == nullptr) break;
            if (name[0] == '*') {
                *reinterpret_cast<uint16_t*>(ent + 6) =
                    static_cast<uint16_t>(title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(name + 1));  // atol
                break;
            }
            uint32_t tag;
            std::memcpy(&tag, name, 4);
            if (tag == 0x6f636273) {  // "sbco"
                word(ent + 0x2a4) |= 0x400;
            } else {
                uint32_t model;
                const uint32_t zone = G_GetEntityZone(ent);
                word(ent + 0x258) = *R_RegisterModel(name, title_global<uint32_t>(0x0095f6ac), &model, zone);
                std::memcpy(&tag, name, 4);
                if (tag != 0x6f6d6273) break;  // "sbmo"
            }
            *reinterpret_cast<uint16_t*>(ent + 6) =
                static_cast<uint16_t>(title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(name + 7));  // atol
            break;
        }
        default: break;
        }
    }
}

}  // namespace game
