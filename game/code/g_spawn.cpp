#include "g_spawn.h"

#include "g_script.h"
#include "g_utils.h"
#include "qcommon.h"
#include "script_string.h"
#include "sv.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

// level.numSpawnVars (0x00985420) and level.spawnVars {key, value} (0x00985424).
uint32_t& num_spawn_vars() { return title_global<uint32_t>(0x00985420); }
uint32_t& spawn_var_key(uint32_t i) { return title_global<uint32_t>(0x00985424 + i * 8); }
uint32_t& spawn_var_value(uint32_t i) { return title_global<uint32_t>(0x00985428 + i * 8); }

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

}  // namespace game
