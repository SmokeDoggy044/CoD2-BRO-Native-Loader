#include "g_vehicle.h"

#include <array>
#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "cg_view.h"
#include "cm.h"
#include "dobj.h"
#include "fx.h"
#include "fx_cmd.h"
#include "g_client.h"
#include "g_main.h"
#include "g_mover.h"
#include "g_script.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "phys.h"
#include "script_string.h"
#include "sv.h"
#include "title_crt.h"
#include "title_data.h"
#include "ragdoll.h"
#include "cg_ents.h"
#include "g_combat.h"
#include "pmove.h"
#include "stream.h"

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
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// g_vehicleDefs (0x00950a28) and their count (int16 0x0095e228).
uint8_t* vehicle_def(int32_t index) { return title_global<uint8_t*>(0x00950a28 + index * 4); }
int16_t& vehicle_def_count() { return title_global<int16_t>(0x0095e228); }
uint8_t* weapon_def(uint32_t index) { return pointer<uint8_t>(title_global<uint32_t>(title_global<uint32_t>(0x0095f4d8) + index * 4)); }

// The title CRT's __CIsqrt (argument and result in ST), stored as float.
float title_ci_sqrt(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(own_address(&title_CIsqrt))
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

// The title CRT's __CIfmod (0x0033acda): x in ST1, y in ST0, fmod(x, y) left in ST0.
long double title_ci_fmod(long double x, long double y) {
    long double result;
    __asm__ volatile("call *%3"
                     : "=t"(result)
                     : "0"(y), "u"(x), "r"(own_address(&title_CIfmod))
                     : "st(1)", "eax", "ecx", "edx", "memory");
    return result;
}

using StrICmp = int32_t XB_CDECL(const char*, const char*);

}  // namespace

int32_t XB_CDECL Vehicle_CountActive() {
    int32_t count = 0;
    const uint8_t* slot = title_global<uint8_t*>(0x0095f694) + 0x170;
    for (int32_t n = title_global<uint16_t>(0x00986000); n > 0; --n, slot += 0x340)
        if (at<uint32_t>(slot, 0)) ++count;
    return count;
}

PLATFORM_ASM_ENTRY(Vehicle_FindDefIndex_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_FindDefIndex) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Vehicle_FindDefIndex(uint32_t eax, const char* name) {
    // AX = the index or -1; the rest of EAX is what the last _stricmp (or the caller) left.
    if (!name || !*name) return eax | 0xffff;
    for (int16_t i = 0; i < vehicle_def_count(); ++i) {
        eax = static_cast<uint32_t>(own_function<StrICmp>(&title_stricmp)(name, reinterpret_cast<const char*>(vehicle_def(i))));  // _stricmp
        if (eax == 0) return static_cast<uint16_t>(i);
    }
    return eax | 0xffff;
}

void XB_CDECL Vehicle_FreeDefs() {
    for (int32_t i = 0; i < vehicle_def_count(); ++i)
        own_function<void XB_CDECL(uint32_t)>(&Z_Free)(address_of(vehicle_def(i)));  // Z_Free
    vehicle_def_count() = 0;
}

PLATFORM_ASM_ENTRY(BG_AllocWeaponDef_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(BG_AllocWeaponDef) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL WeaponDef_SetStringField(const char** out, const char* text) {
    if (!*text) {
        *out = &title_global<const char>(0x0095f4dd);  // ""
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out));
    }
    *out = text;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text));
}

const char* XB_CDECL VehicleDef_CopyStringField(char* dst, const char* src) {
    char c;
    do {
        c = *src;
        *dst++ = c;
        ++src;
    } while (c);
    return src;
}

uint8_t* XB_CDECL BG_AllocWeaponDef(uint32_t index, const uint32_t* fields, int32_t count) {
    uint8_t* def = static_cast<uint8_t*>(own_function<void* XB_CDECL(uint32_t)>(&Z_Malloc)(0x870));  // Z_Malloc
    if (def) {
        at<uint32_t>(def, 0xac) = 0;
        at<uint32_t>(def, 0x1b0) = 0;
        at<uint32_t>(def, 0x2b4) = 0;
        at<uint32_t>(def, 0x3b8) = 0;
    }
    std::memset(def, 0, 0x870);
    title_global<uint32_t>(title_global<uint32_t>(0x0095f4d8) + index * 4) = address_of(def);
    at<uint32_t>(def, 0) = index;
    at<uint32_t>(def, 8) = 0x0095f4dd;  // ""
    at<uint32_t>(def, 4) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0082dfeb}));
    // String fields (type 0 of the {name, offset, type} records) default to "".
    for (const uint32_t* field = fields + 1; count > 0; --count, field += 3)
        if (!field[1]) at<uint32_t>(def, field[0]) = 0x0095f4dd;
    return def;
}

int32_t XB_STDCALL Vehicle_InitWheelTags(int32_t* vehicle) {
    const DObj* obj = at<const DObj*>(pointer<uint8_t>(static_cast<uint32_t>(vehicle[0x440 / 4])), 0x22c);
    for (int32_t i = 0; i < 8; ++i)
        vehicle[i] = DObj_FindBoneIndex2(at<const DObj*>(pointer<uint8_t>(static_cast<uint32_t>(vehicle[0x440 / 4])), 0x22c),
                                         title_global<const char*>(0x008db414 + i * 4));
    (void)obj;
    for (int32_t i = 0; i < 8; ++i) {
        if (vehicle[i] < 0) continue;
        const uint32_t* pose = reinterpret_cast<const uint32_t*>(
            DObj_GetBoneBasePose(at<const DObj*>(pointer<uint8_t>(static_cast<uint32_t>(vehicle[0x440 / 4])), 0x22c), vehicle[i]));
        // The 3x3 rows written as 16-byte rows: each row's fourth word is the next row's
        // first (the third row's is the first word again); row 3 is pose +0x24.
        const uint32_t m[16] = {pose[0], pose[1], pose[2], pose[3], pose[3], pose[4], pose[5], pose[6],
                                pose[6], pose[7], pose[8], pose[0], pose[9], pose[10], pose[11], pose[12]};
        std::memcpy(reinterpret_cast<uint8_t*>(vehicle) + 0x220 + i * 0x40, m, 0x40);
    }
    return 8;
}

PLATFORM_ASM_ENTRY(BG_GivePlayerWeapon_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(BG_GivePlayerWeapon) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL BG_GivePlayerWeapon(uint32_t weapon, uint8_t* ps) {
    const uint32_t bit = 1u << (weapon & 0x1f);
    const int32_t word = static_cast<int32_t>(weapon) >> 5;
    uint32_t* owned = &at<uint32_t>(ps, 0x3f0);
    uint32_t* other = &at<uint32_t>(ps, 0x400);
    if (owned[word] & bit) return 0;
    const uint8_t* def = weapon_def(weapon);
    const uint32_t kind = at<uint32_t>(def, 0x98);
    if (kind == 7 || kind == 9) return 0;
    G_RegisterWeapon(0, 0, weapon, 1);
    owned[word] |= bit;
    other[word] &= ~bit;
    const uint32_t slot_kind = at<uint32_t>(def, 0x9c);
    if (slot_kind == 1 || slot_kind == 2) {
        if (!ps[0x3f9])
            ps[0x3f9] = static_cast<uint8_t>(weapon);
        else if (!ps[0x3fa])
            ps[0x3fa] = static_cast<uint8_t>(weapon);
    } else if (slot_kind >= 3 && slot_kind <= 6) {
        if (!ps[0x3f8 + slot_kind]) ps[0x3f8 + slot_kind] = static_cast<uint8_t>(weapon);
    }
    // The alternate weapons chain (+0x718) comes along; the original clears the first
    // weapon's bit in +0x400 again for each.
    for (uint32_t alt = at<uint32_t>(def, 0x718); alt; alt = at<uint32_t>(weapon_def(alt), 0x718)) {
        const uint32_t alt_bit = 1u << (alt & 0x1f);
        const int32_t alt_word = static_cast<int32_t>(alt) >> 5;
        if (owned[alt_word] & alt_bit) break;
        G_RegisterWeapon(0, 0, alt, 1);
        owned[alt_word] |= alt_bit;
        other[word] &= ~bit;
    }
    return 1;
}

PLATFORM_ASM_ENTRY(BG_RemoveWeaponFromSlot_entry,
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(BG_RemoveWeaponFromSlot) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL BG_RemoveWeaponFromSlot(uint32_t weapon, uint8_t* ps) {
    const uint32_t bit = 1u << (weapon & 0x1f);
    const int32_t word = static_cast<int32_t>(weapon) >> 5;
    uint32_t* owned = &at<uint32_t>(ps, 0x3f0);
    if (!(owned[word] & bit)) return 0;
    const uint8_t* def = weapon_def(weapon);
    const uint32_t slot = BG_GetWeaponSlot(ps, static_cast<int32_t>(weapon), 1);
    if (slot) {
        if (!at<uint32_t>(def, 0xa0)) {
            ps[0x3f8 + slot] = 0;
        } else {
            // The slot gets the first other weapon the player has that is in no slot.
            bool found = false;
            for (int32_t i = 1; i <= title_global<int32_t>(0x0095f4d4); ++i) {
                if (!(owned[i >> 5] & (1u << (i & 0x1f)))) continue;
                if (BG_GetWeaponSlot(ps, i, 1)) continue;
                ps[0x3f8 + slot] = static_cast<uint8_t>(i);
                found = i <= title_global<int32_t>(0x0095f4d4);
                break;
            }
            if (!found) ps[0x3f8 + slot] = 0;
        }
    }
    owned[word] &= ~bit;
    for (uint32_t alt = at<uint32_t>(def, 0x718); alt; alt = at<uint32_t>(weapon_def(alt), 0x718)) {
        const uint32_t alt_bit = 1u << (alt & 0x1f);
        const int32_t alt_word = static_cast<int32_t>(alt) >> 5;
        if (!(owned[alt_word] & alt_bit)) break;
        owned[alt_word] &= ~alt_bit;
    }
    return 1;
}

uint32_t XB_CDECL Vehicle_ApplyPush(uint8_t* ent, const float* direction, float scale, float spin, uint32_t extra) {
    uint8_t* vehicle = at<uint8_t*>(ent, 0x248);
    if (!vehicle) return address_of(ent);
    const uint8_t* def = vehicle_def(at<int16_t>(vehicle, 0x178));
    if (0.0f > scale)
        scale = 0.0f;
    else if (scale > title_global<float>(0x0083eb20))
        scale = title_global<float>(0x0083eb20);
    float forward[3];
    float right[3];
    float up[3];
    uint32_t args[2] = {address_of(vehicle + 0xe0), address_of(forward)};
    AnglesToAxis(args, up, right);  // its C version takes (args, EDI buffer, ESI buffer)
    float& x = at<float>(vehicle, 0x1e8);
    float& y = at<float>(vehicle, 0x1ec);
    x = ((direction[2] * forward[2]) + (direction[1] * forward[1])) + (direction[0] * forward[0]);
    y = 0.0f - (((direction[2] * (0.0f - up[2])) + (direction[1] * (0.0f - up[1]))) + (direction[0] * (0.0f - up[0])));
    at<float>(vehicle, 0x1f0) = title_global<float>(0x0083eb20);
    at<float>(vehicle, 0x1f4) = 0.0f;
    const float length = title_ci_sqrt(x * x + y * y);
    if (!(length == title_global<float>(0x0083eafc))) {
        const float inverse = title_global<float>(0x0083eb20) / length;
        const float nx = x * inverse;
        const float ny = inverse * y;
        x = nx;
        y = ny;
    }
    x = (at<float>(def, 0x54) * x) * scale;
    y = (at<float>(def, 0x58) * y) * scale;
    at<float>(vehicle, 0x1f8) = at<float>(ent, 0x2fc) * spin;
    at<uint32_t>(vehicle, 0x1fc) = extra;
    return address_of(def);
}

PLATFORM_ASM_ENTRY(Turret_UpdateTargetAngles_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edx\n"
                   "leal 16(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Turret_UpdateTargetAngles) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL Turret_UpdateTargetAngles(uint32_t* args, uint32_t ai, uint8_t* ent, const float* target) {
    if (!ent) return 0;
    const uint8_t* turret = at<uint8_t*>(ent, 0x24c);
    if (!turret) return 0;
    float* angles = &at<float>(ent, 0x90);
    const float before = angles[0];
    angles[0] = before + at<float>(ent, 0x98);
    uint32_t on_target = 1;
    // Turn speeds (degrees per second) for pitch and yaw.
    float speed[2];
    if (!ai) {
        speed[1] = title_global<float>(0x0083ef08);
        speed[0] = speed[1];
    } else {
        const uint8_t* def = weapon_def(ent[3]);
        speed[0] = at<float>(def, 0x828);
        speed[1] = at<float>(def, 0x82c);
        uint32_t bits;
        std::memcpy(&bits, &angles[0], 4);
        bits &= 0x7fffffffu;  // FABS
        float magnitude;
        std::memcpy(&magnitude, &bits, 4);
        if (platform_x87_ld(magnitude) > platform_x87_ld(title_global<float>(0x0083eed0))) {
            speed[0] = speed[0] * title_global<float>(0x0083ec9c);
        } else {
            const uint8_t* user = at<uint8_t*>(turret, 0xc);
            bool slow = true;
            if (user) {
                const uint8_t* client = at<uint8_t*>(user, 0x244);
                slow = client && at<uint32_t>(client, 0x30);
            }
            if (slow) {
                speed[1] = speed[1] * title_global<float>(0x0083eb24);
                speed[0] = speed[0] * title_global<float>(0x0083eb24);
            }
        }
    }
    const uint8_t flags = turret[3];
    if ((flags & 2) && (flags & 1) && title_global<float>(0x0083ebcc) > speed[0]) speed[0] = title_global<float>(0x0083ebcc);
    const float full = title_global<float>(0x0083ebcc);
    auto wrap = [full](long double difference) {
        float d = static_cast<float>(title_ci_fmod(difference, title_global<double>(0x0083ed40)));  // 360.0
        if (platform_x87_ld(d) > platform_x87_ld(title_global<float>(0x0083eb60))) d = d - full;
        if (title_global<float>(0x0083ebd0) > d) d = d + full;
        return d;
    };
    for (int i = 0; i < 2; ++i) {
        const float step = speed[i] * title_global<float>(0x008ddaa0);
        const long double difference = platform_x87_ld(target[i]) - angles[i];
        speed[i] = step;
        float d = wrap(difference);
        if (d > step) {
            d = step;
            on_target = 0;
        } else if (0.0f - step > d) {
            d = 0.0f - step;
            on_target = 0;
        }
        angles[i] = d + angles[i];
    }
    // The pitch offset (+0x98) is what the turn left of the free pitch change.
    const float pitch = angles[0];
    at<float>(ent, 0x98) = pitch;
    float d = wrap(platform_x87_ld(pitch) - before);
    std::memcpy(&args[0], &d, 4);  // the original keeps it in its first argument slot
    uint32_t result;
    if (d > speed[0]) {
        result = 0;
        d = speed[0];
    } else if (0.0f - speed[0] > d) {
        on_target = 0;
        d = 0.0f - speed[0];
        result = on_target;
    } else {
        result = on_target;
    }
    angles[0] = d + before;
    at<float>(ent, 0x98) = at<float>(ent, 0x98) - angles[0];
    if (result) Scr_Notify(ent, title_global<uint32_t>(0x0097a834));  // turret_on_target
    return result;
}

namespace {

uint32_t vehicle_tag(uint32_t& guard, uint32_t bit, uint32_t hash_at, uint32_t name, const DObj* obj) {
    if (!(guard & bit)) {
        guard |= bit;
        title_global<uint32_t>(0x0098bc2c) = guard;
        title_global<uint32_t>(hash_at) = Com_HashKeyNoCase(0, pointer<const char>(name));
    }
    return obj ? static_cast<uint32_t>(DObj_GetTagIndex(obj, title_global<uint32_t>(hash_at))) : 0xffffffffu;
}

}  // namespace

uint32_t XB_CDECL Vehicle_CacheTags(uint8_t* ent) {
    uint8_t* vehicle = at<uint8_t*>(ent, 0x248);
    uint32_t guard = title_global<uint32_t>(0x0098bc2c);
    auto obj = [ent]() { return at<const DObj*>(ent, 0x22c); };
    at<uint32_t>(vehicle, 0x2b0) = vehicle_tag(guard, 0x01, 0x0098bc28, 0x00838a54, obj());  // tag_player
    at<uint32_t>(vehicle, 0x2b4) = vehicle_tag(guard, 0x02, 0x0098bc24, 0x00839fe0, obj());  // tag_detach
    at<uint32_t>(vehicle, 0x2b8) = vehicle_tag(guard, 0x04, 0x0098bc20, 0x00839fd4, obj());  // tag_popout
    at<uint32_t>(vehicle, 0x2bc) = vehicle_tag(guard, 0x08, 0x0098bc1c, 0x0083a0e0, obj());  // tag_body
    at<uint32_t>(vehicle, 0x2c0) = vehicle_tag(guard, 0x10, 0x0098bc18, 0x00838a48, obj());  // tag_turret
    at<uint32_t>(vehicle, 0x2c4) = vehicle_tag(guard, 0x20, 0x0098bc14, 0x00838a28, obj());  // tag_barrel
    at<uint32_t>(vehicle, 0x2c8) = vehicle_tag(guard, 0x40, 0x0098bc10, 0x0083872c, obj());  // tag_coax
    // Bone tables by name hash (0x009886e8: 4, 0x009886f8: 6); "tag_origin" counts as bone 0.
    uint32_t value = 0;
    for (int k = 0; k < 4; ++k) {
        const uint32_t name = title_global<uint32_t>(0x009886e8 + k * 4);
        if (!obj()) {
            value = 0xffffffffu;
        } else {
            const int32_t bone = name ? DObj_GetBoneIndex(obj(), name) : -1;
            if (!(title_global<uint32_t>(0x0098b840) & 1)) {
                title_global<uint32_t>(0x0098b840) |= 1;
                title_global<uint32_t>(0x0098b83c) = Com_HashKeyNoCase(0, pointer<const char>(0x00832b04));
            }
            value = bone < 0 && name == title_global<uint32_t>(0x0098b83c) ? 0 : static_cast<uint32_t>(bone);
        }
        at<uint32_t>(vehicle, 0x2cc + k * 4) = value;
    }
    for (int k = 0; k < 6; ++k) {
        const uint32_t name = title_global<uint32_t>(0x009886f8 + k * 4);
        if (!obj()) {
            value = 0xffffffffu;
        } else {
            const int32_t bone = name ? DObj_GetBoneIndex(obj(), name) : -1;
            if (!(title_global<uint32_t>(0x0098b840) & 1)) {
                title_global<uint32_t>(0x0098b840) |= 1;
                uint32_t hash = 0;
                for (const char* c = pointer<const char>(0x00832b04); *c; ++c) {
                    char ch = *c;
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 0x20);
                    hash = static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(ch))) ^ ((hash << 5) | (hash >> 27));
                }
                title_global<uint32_t>(0x0098b83c) = hash;
            }
            value = bone < 0 && name == title_global<uint32_t>(0x0098b83c) ? 0 : static_cast<uint32_t>(bone);
        }
        at<uint32_t>(vehicle, 0x2dc + k * 4) = value;
    }
    return value;
}

namespace {

// Calls a title function with EAX, ECX, EDX, ESI and EDI set and one stack word pushed (ESP
// restored afterwards: cdecl, stdcall and register-only callees alike).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL vehicle_call(uint32_t function, uint32_t eax, uint32_t ecx, uint32_t edx,
                                                          uint32_t esi, uint32_t edi, uint32_t arg);
PLATFORM_ASM_ENTRY(vehicle_call,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl 32(%ebp)\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 16(%ebp), %ecx\n"
                   "movl 20(%ebp), %edx\n"
                   "movl 24(%ebp), %esi\n"
                   "movl 28(%ebp), %edi\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")

// Calls a title function with EAX and EDX set and `count` stack words (args[0] pushed last);
// ESP restored afterwards.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL vehicle_call_args(uint32_t function, uint32_t eax, uint32_t edx,
                                                               uint32_t count, const uint32_t* args);
PLATFORM_ASM_ENTRY(vehicle_call_args,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 20(%ebp), %ecx\n"
                   "movl 24(%ebp), %esi\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "decl %ecx\n"
                   "pushl (%esi,%ecx,4)\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 16(%ebp), %edx\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")

// The entity a handle names (index in the low 12 bits, generation above), 0 when stale.
uint8_t* entity_from_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (static_cast<int32_t>(index) >= 0x540) return nullptr;
    if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return title_global<uint8_t*>(0x0097a968 + index * 8);
}

}  // namespace

void XB_CDECL Vehicle_PhysicsPreFrame() {
    uint8_t* const* end = pointer<uint8_t*>(title_global<uint32_t>(0x008feb9c) * 4 + 0x008feb70);
    for (uint8_t* const* item = pointer<uint8_t*>(0x008feb70); item != end; ++item) {
        uint8_t* ent = *item;
        std::memcpy(at<uint8_t*>(ent, 0x154) + 0x160, ent + 0x170, 0x40);
        const uint32_t body = at<uint32_t>(ent, 0x154);
        vehicle_call(own_address(&G_SetPoseFromPhysics), body, 0, 0, address_of(ent + 0x1b0), body + 0x1a0, 0);  // G_SetPoseFromPhysics
    }
}

uint32_t XB_REGS3 XB_CDECL Turret_IsUsableBy(uint8_t* player, uint32_t, uint8_t* turret) {
    const uint8_t* info = at<uint8_t*>(turret, 0x24c);
    if (!(info[3] & 0x10)) return 0;
    if (turret[0x297]) return 0;
    if (!info) return 0;
    if (!at<uint32_t>(turret, 0x29c)) return 0;
    if (!vehicle_call(own_address(&Turret_IsPlayerWithinArc_entry), at<uint32_t>(turret, 0x29c), 0, 0, address_of(turret), address_of(player), 0)) return 0;
    const uint8_t* client = at<uint8_t*>(player, 0x23c);
    if (at<uint32_t>(client, 0x3c)) return 0;
    return at<uint32_t>(client, 0x60) != 0;
}

void XB_CDECL Vehicle_Pain(uint8_t* self, const uint8_t* attacker, uint32_t, uint32_t, uint32_t, const float* direction) {
    if (!attacker || !attacker[3]) return;
    const uint32_t kind = at<uint32_t>(weapon_def(attacker[3]), 0x94);
    if (kind != 2 && kind != 1) return;
    alignas(16) const float push[4] = {direction[0], direction[1], direction[2], direction[0]};
    Vehicle_ApplyPush(self, push, title_global<float>(0x0083eb20), 0.0f, 0);
}

PLATFORM_ASM_ENTRY(G_RunPlayerVehicleThinks_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(G_RunPlayerVehicleThinks) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_RunPlayerVehicleThinks(uint32_t ebx) {
    title_global<uint32_t>(0x0095f684) = 1;
    if (!title_global<uint16_t>(0x00986000)) {
        title_global<uint32_t>(0x0095f684) = 0;
        return;
    }
    for (int32_t i = 0; i < title_global<uint16_t>(0x00986000); ++i) {
        const uint32_t handle = at<uint32_t>(title_global<uint8_t*>(0x0095f694) + i * 0x340, 0x170);
        if (!handle) continue;
        uint8_t* ent = entity_from_handle(handle);
        if (ent[0x297] != 2) continue;
        // The original pushes its caller's EBX as G_RunThink's argument (entity in ESI).
        vehicle_call(own_address(&G_RunThink_entry), 0, 0, 0, address_of(ent), static_cast<uint32_t>(i) * 0x340, ebx);  // G_RunThink
    }
    title_global<uint32_t>(0x0095f684) = 0;
}

PLATFORM_ASM_ENTRY(Vehicle_InitBrushModels_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_InitBrushModels) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Vehicle_InitBrushModels(uint32_t eax) {
    if (!title_global<uint16_t>(0x00986000)) return eax;
    int16_t i = 0;
    do {
        const uint32_t handle = at<uint32_t>(title_global<uint8_t*>(0x0095f694) + i * 0x340, 0x170);
        if (handle) {
            uint8_t* ent = Vehicle_FromEntityHandle(handle);
            const uint8_t* collmap = Vehicle_FindCollmap(at<const char*>(at<uint8_t*>(ent, 0x258), 0x48));
            if (collmap && at<uint16_t>(collmap, 6)) {
                at<uint16_t>(ent, 6) = at<uint16_t>(collmap, 6);
                vehicle_call(own_address(&SV_SetBrushModel), address_of(ent), 0, 0, address_of(ent), 0, 0);  // SV_SetBrushModel
                at<uint32_t>(ent, 0x134) = 0x800000;
                if (at<uint32_t>(ent, 0x2a0) & 1) at<uint32_t>(ent, 0x134) = 0xa00000;
            }
        }
        ++i;
    } while (i < title_global<uint16_t>(0x00986000));
    return static_cast<uint32_t>(static_cast<int32_t>(i));
}

uint32_t XB_CDECL Vehicle_Use(uint8_t* self, uint8_t* other) {
    const uint8_t* client = at<uint8_t*>(other, 0x23c);
    if (!client) return 0;
    if (at<uint32_t>(client, 0xc0) & 0x100000)
        return vehicle_call(own_address(&Vehicle_ExitPlayer_entry), address_of(client), 0, 0, 0, address_of(other), 0);  // Vehicle_ExitPlayer
    return vehicle_call(own_address(&Vehicle_EnterPlayer_entry), address_of(self), 0, 0, 0, address_of(other), 0);  // Vehicle_EnterPlayer
}

void XB_CDECL Vehicle_Controller(uint8_t* ent) {
    using SetBoneAngles = uint32_t XB_CDECL(uint8_t*, uint32_t, float*, uint32_t, uint32_t);
    const uint8_t* vehicle = at<uint8_t*>(ent, 0x248);
    // Bone angle overrides (0x002de420, controller 0x0082eec0): body pitch/roll, turret yaw,
    // barrel pitch, each when the vehicle has the tag.
    float angles[6] = {at<float>(vehicle, 0x280), 0.0f, at<float>(vehicle, 0x288), 0.0f, 0.0f, 0.0f};
    if (at<int32_t>(vehicle, 0x2bc) >= 0)
        own_function<SetBoneAngles>(&G_DObjSetBoneRotation_entry)(ent, 0x0082eec0, angles, at<uint32_t>(vehicle, 0x2bc), 0);
    angles[1] = at<float>(vehicle, 0x294);
    angles[0] = 0.0f;
    angles[2] = 0.0f;
    angles[3] = at<float>(vehicle, 0x290);
    angles[4] = 0.0f;
    angles[5] = 0.0f;
    if (at<int32_t>(vehicle, 0x2c0) >= 0)
        own_function<SetBoneAngles>(&G_DObjSetBoneRotation_entry)(ent, 0x0082eec0, angles, at<uint32_t>(vehicle, 0x2c0), 0);
    if (at<int32_t>(vehicle, 0x2c4) >= 0)
        own_function<SetBoneAngles>(&G_DObjSetBoneRotation_entry)(ent, 0x0082eec0, angles + 3, at<uint32_t>(vehicle, 0x2c4), 0);
}

void XB_CDECL Vehicle_Die(uint8_t* self, uint32_t, const uint8_t* attacker, uint32_t, uint32_t, uint32_t, uint32_t,
                          const float* direction) {
    at<uint32_t>(at<uint8_t*>(self, 0x248), 0x200) = 0;
    Vehicle_StopEffects(self);
    if (const uint32_t handle = at<uint32_t>(self, 0x1a0)) {
        // The driver gets out (Vehicle_ExitPlayer, player in EDI; 0 when the handle is stale).
        vehicle_call(own_address(&Vehicle_ExitPlayer_entry), handle & 0xfff, 0, 0, 0, address_of(entity_from_handle(handle)), 0);
    }
    if (!attacker || !attacker[3]) return;
    const uint32_t kind = at<uint32_t>(weapon_def(attacker[3]), 0x94);
    if (kind != 2 && kind != 1) return;
    alignas(16) const float push[4] = {direction[0], direction[1], direction[2], direction[0]};
    Vehicle_ApplyPush(self, push, title_global<float>(0x0083eb20), 0.0f, 0);
}

uint32_t XB_CDECL BG_InitDefaultWeapon() {
    // Weapon 0 ("none") with the weapon field table 0x008d2440 (0x10a fields), then the
    // weapon files listed under "WEAPONFILE" (0x00024820, callback 0x002bc7d0).
    uint8_t* def = BG_AllocWeaponDef(0, pointer<const uint32_t>(0x008d2440), 0x10a);
    at<uint32_t>(def, 8) = 0x00832430;
    if (!(title_global<uint8_t>(0x0098bc50) & 1)) {
        title_global<uint32_t>(0x0098bc50) |= 1;
        title_global<uint32_t>(0x0098bc4c) = Com_HashKeyNoCase(0, pointer<const char>(0x00832430));
    }
    at<uint32_t>(def, 4) = title_global<uint32_t>(0x0098bc4c);
    title_global<int32_t>(0x0095f4d4) = 0;
    uint32_t found = 0;  // its low byte is set to 0; the rest is never read
    const uint32_t length = static_cast<uint32_t>(std::strlen(pointer<const char>(0x00835620)));
    // 0x00024820 hands EAX and EDX (the zone: the memory system's +0x34) on to
    // Stream_ListSearchZones; it pops its six arguments.
    const uint32_t args[6] = {title_global<uint32_t>(0x0095f5d4), address_of(&found), 0x00835620, length, own_address(&BG_ParseWeaponFile), found};
    return vehicle_call_args(own_address(&Stream_ForEachAssetWithPrefix), title_global<uint32_t>(0x0095f5d4), at<uint32_t>(title_global<uint8_t*>(0x0095f638), 0x34), 6, args);
}

PLATFORM_ASM_ENTRY(G_ReleaseTurretUser_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_ReleaseTurretUser) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL G_ReleaseTurretUser(uint8_t* turret) {
    const uint32_t handle = at<uint32_t>(turret, 0x1a0);
    if (uint8_t* user = entity_from_handle(handle)) {
        if (at<uint32_t>(user, 0x23c)) {
            vehicle_call(own_address(&G_ClientStopUsingTurret), address_of(turret), 0, 0, 0, address_of(turret), 0);  // G_ClientStopUsingTurret
        } else if (const uint32_t actor = at<uint32_t>(user, 0x240)) {
            vehicle_call(own_address(&Actor_LeaveTurret_entry), actor, 0, 0, actor, address_of(turret), 0);  // Actor_LeaveTurret
        }
    }
    uint8_t* info = at<uint8_t*>(turret, 0x24c);
    turret[0x297] = 0;
    info[0] = 0;
    at<uint32_t>(turret, 0x24c) = 0;
    return info;
}

namespace {

// vehicle_call with EBX set as well.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL vehicle_call_ebx(uint32_t function, uint32_t eax, uint32_t ebx, uint32_t arg);
PLATFORM_ASM_ENTRY(vehicle_call_ebx,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl 20(%ebp)\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 16(%ebp), %ebx\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")

// G_TryPushingEntity's success: the entity moves to `origin` (its ground entity +0xc
// cleared unless it is the pusher's), clients and actors turn with the pusher's yaw.
uint32_t pushed_to(uint8_t* ent, const uint8_t* pusher, const float* amove, const float* origin) {
    if (at<uint32_t>(ent, 0xc) != at<uint32_t>(pusher, 0x224)) at<uint32_t>(ent, 0xc) = 0;
    const float x = origin[0], y = origin[1], z = origin[2];
    at<float>(ent, 0x140) = x;
    at<float>(ent, 0x144) = y;
    at<float>(ent, 0x148) = z;
    at<float>(ent, 0x20) = x;
    at<float>(ent, 0x24) = y;
    at<float>(ent, 0x28) = z;
    const float to_short = title_global<float>(0x0083ebd8);
    if (uint8_t* client = at<uint8_t*>(ent, 0x23c)) {
        at<uint32_t>(client, 0x58) += static_cast<uint32_t>(static_cast<int32_t>(amove[1] * to_short)) & 0xffff;
        float* position = at<float*>(ent, 0x23c);
        position[0] = x;
        at<float*>(ent, 0x23c)[1] = y;
        at<float*>(ent, 0x23c)[2] = z;
    } else if (uint8_t* actor = at<uint8_t*>(ent, 0x240)) {
        const float to_angle = title_global<float>(0x0083ebd4);
        at<float>(actor, 0x138) =
            static_cast<float>(static_cast<int32_t>((amove[1] + at<float>(actor, 0x138)) * to_short) & 0xffff) * to_angle;
        uint8_t* same = at<uint8_t*>(ent, 0x240);
        at<float>(same, 0x14c) =
            static_cast<float>(static_cast<int32_t>((at<float>(same, 0x14c) + amove[1]) * to_short) & 0xffff) * to_angle;
    }
    title_global<uint32_t>(0x0095bef8) += 0x14;  // pushed_p
    return 1;
}

}  // namespace

PLATFORM_ASM_ENTRY(G_TryPushingEntity_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_TryPushingEntity) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL G_TryPushingEntity(const float* move, uint8_t* ent, uint8_t* pusher, const float* amove) {
    if ((at<uint32_t>(pusher, 0x10) & 0x4000000) && at<uint32_t>(ent, 0xc) != at<uint32_t>(pusher, 0x224)) return 0;
    alignas(16) float origin[4] = {move[0] + at<float>(ent, 0x140), move[1] + at<float>(ent, 0x144), move[2] + at<float>(ent, 0x148)};
    // The inverse of the pusher's rotation (rows forward, -left, up, transposed) applied to
    // the entity's offset from the pusher.
    float forward[3];
    float left[3];
    float up[3];
    uint32_t args[2] = {address_of(amove), address_of(forward)};
    AnglesToAxis(args, left, up);  // C version: (args, EDI buffer, ESI buffer)
    left[0] = 0.0f - left[0];
    left[1] = 0.0f - left[1];
    left[2] = 0.0f - left[2];
    float t[9];
    for (int i = 0; i < 3; ++i) {
        t[i * 3] = forward[i];
        t[i * 3 + 1] = left[i];
        t[i * 3 + 2] = up[i];
    }
    const float x = origin[0] - at<float>(pusher, 0x140);
    const float y = origin[1] - at<float>(pusher, 0x144);
    const float z = origin[2] - at<float>(pusher, 0x148);
    origin[0] = origin[0] + ((((x * t[0]) + (t[2] * z)) + (t[1] * y)) - x);
    origin[1] = origin[1] + ((((t[5] * z) + (t[4] * y)) + (t[3] * x)) - y);
    origin[2] = origin[2] + ((((t[8] * z) + (t[7] * y)) + (t[6] * x)) - z);
    if (!G_TestEntityPosition(ent, origin)) return pushed_to(ent, pusher, amove, origin);
    // Stuck: try offsets in shells of 4 units up to half the entity's size (+0xf0): z by
    // +-a (a = 0, 4, ...), then x by +-c and y by +-e (c, e = 4, 8, ...).
    auto radius = [ent]() { return platform_x87_ld(at<float>(ent, 0xf0)) / 2.0L; };
    auto step4 = [](float v) { return static_cast<float>(platform_x87_ld(v) + platform_x87_ld(4.0f)); };
    if (radius() > 4.0L && radius() > 0.0L) {
        for (float a = 0.0f;;) {
            float b = 0.0f - a;
            if (!(a < b)) {
                for (;;) {
                    for (float c = 4.0f; radius() > platform_x87_ld(c); c = step4(c)) {
                        float d = 0.0f - c;
                        if (c < d) continue;
                        for (;;) {
                            for (float e = 4.0f; radius() > platform_x87_ld(e); e = step4(e)) {
                                float f = 0.0f - e;
                                if (e < f) continue;
                                alignas(16) float probe[4] = {};  // the fourth word: the zeroed dead stack
                                probe[0] = origin[0] + d;
                                probe[2] = origin[2] + b;
                                for (;;) {
                                    probe[1] = origin[1] + f;
                                    if (!G_TestEntityPosition(ent, probe)) return pushed_to(ent, pusher, amove, probe);
                                    f = (e * 2.0f) + f;
                                    if (!(e >= f)) break;
                                }
                            }
                            d = (c * 2.0f) + d;
                            if (!(c >= d)) break;
                        }
                    }
                    if (b == 0.0f) break;
                    b = (a * 2.0f) + b;
                    if (!(a >= b)) break;
                }
            }
            a = step4(a);
            if (!(radius() > platform_x87_ld(a))) break;
        }
    }
    if (G_TestEntityPosition(ent, &at<float>(ent, 0x140))) return 0;
    at<uint32_t>(ent, 0xc) = 0;
    return 1;
}

uint32_t XB_CDECL Vehicle_UpdateTurret(uint8_t* ent, int32_t elapsed) {
    uint8_t* vehicle = at<uint8_t*>(ent, 0x248);
    if (!at<uint32_t>(vehicle, 0x1a4)) return 0;
    const uint32_t handle = at<uint32_t>(ent, 0x1a0);
    if (!handle) {
        at<uint32_t>(vehicle, 0x2fc) = 0;
    } else if (address_of(entity_from_handle(handle)) == at<uint32_t>(title_global<uint8_t*>(0x0095f534), 4)) {
        // The player's attack button (client +0x528 bit 1) fires, unless +0x2c bit 0x4000.
        const uint8_t* player = G_EntHandleToEnt(&at<uint32_t>(ent, 0x1a0));
        at<uint32_t>(vehicle, 0x2fc) = at<uint8_t>(at<uint8_t*>(player, 0x23c), 0x528) & 2;
        if (at<uint32_t>(at<uint8_t*>(player, 0x23c), 0x2c) & 0x4000) at<uint32_t>(vehicle, 0x2fc) = 0;
    }
    const int32_t left = at<int32_t>(vehicle, 0x184) - elapsed;
    at<int32_t>(vehicle, 0x184) = left;
    if (left > 0) return static_cast<uint32_t>(left);
    at<uint32_t>(vehicle, 0x184) = 0;
    if (!at<uint32_t>(vehicle, 0x2fc)) return 0;
    if (vehicle[0x19c]) return vehicle[0x19c];
    return own_function<uint32_t XB_CDECL(uint8_t*)>(&Vehicle_FireTurretWeapon_entry)(ent);  // Vehicle_FireTurretWeapon
}

PLATFORM_ASM_ENTRY(Player_UpdateTurretUse_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(Player_UpdateTurretUse) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Player_UpdateTurretUse(uint8_t* player) {
    uint8_t* turret = entity_from_handle(at<uint32_t>(player, 0x1a0));
    if (turret[0x297] == 1 && at<int32_t>(turret, 0x350) > 0) {
        vehicle_call_ebx(own_address(&Turret_UpdatePlayerFire_entry), address_of(player), address_of(player), address_of(turret));
        return;
    }
    vehicle_call(own_address(&G_ClientStopUsingTurret), address_of(player), 0, 0, 0, 0, 0);  // G_ClientStopUsingTurret
}

// A path state: two int16 at +0, four words, three vec3 (+0x14, +0x20, +0x2c), then two path nodes
// of 0x40 bytes at +0x38.
PLATFORM_ASM_ENTRY(Vehicle_CopyPathState_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_CopyPathState) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL Vehicle_CopyPathState(const uint8_t* src, uint8_t* dst) {
    at<uint16_t>(dst, 0) = at<uint16_t>(src, 0);
    at<uint16_t>(dst, 2) = at<uint16_t>(src, 2);
    for (uint32_t offset = 4; offset < 0x38; offset += 4) at<uint32_t>(dst, offset) = at<uint32_t>(src, offset);
    for (int32_t i = 0; i < 2; ++i) VehiclePathNode_Copy(dst + 0x38 + i * 0x40, src + 0x38 + i * 0x40);
    return dst;
}

// A path node: three script strings (+0, +4, +0x10), two words, three vec3 (+0x14, +0x20, +0x2c),
// a word at +0x38 and bit fields at +0x3c (14, 14, 2 and 2 bits, copied one field at a time).
PLATFORM_ASM_ENTRY(VehiclePathNode_Copy_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(VehiclePathNode_Copy) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL VehiclePathNode_Copy(uint8_t* dst, const uint8_t* src) {
    ScriptString_Copy(&at<ScriptString>(dst, 0), &at<ScriptString>(src, 0));
    ScriptString_Copy(&at<ScriptString>(dst, 4), &at<ScriptString>(src, 4));
    at<uint32_t>(dst, 8) = at<uint32_t>(src, 8);
    at<uint32_t>(dst, 0xc) = at<uint32_t>(src, 0xc);
    ScriptString_Copy(&at<ScriptString>(dst, 0x10), &at<ScriptString>(src, 0x10));
    for (uint32_t offset = 0x14; offset < 0x3c; offset += 4) at<uint32_t>(dst, offset) = at<uint32_t>(src, offset);
    uint32_t& bits = at<uint32_t>(dst, 0x3c);
    const uint32_t& from = at<uint32_t>(src, 0x3c);  // re-read for each field, as the original does
    bits = (bits & ~0x00003fffu) | (from & 0x00003fffu);
    bits = (bits & ~0x0fffc000u) | (from & 0x0fffc000u);
    bits = (bits & ~0x30000000u) | (from & 0x30000000u);
    bits = (bits & 0x3fffffffu) | (from & ~0x3fffffffu);
    return dst;
}

// Releases the script strings (+0x10, +4, +0) of the two 0x40-byte entries at +0x78 and +0x38,
// in that order.
PLATFORM_ASM_ENTRY(VehiclePathState_Dtor_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(VehiclePathState_Dtor) "addl $4, %esp\n"
                   "ret")

void XB_CDECL VehiclePathState_Dtor(uint8_t* state) {
    using Release = void XB_THISCALL(uint8_t*);
    for (uint8_t* entry = state + 0x78; entry != state - 8; entry -= 0x40) {
        own_function<Release>(&ScriptString_Release_entry)(entry + 0x10);  // ScriptString_Release
        own_function<Release>(&ScriptString_Release_entry)(entry + 4);
        own_function<Release>(&ScriptString_Release_entry)(entry);
    }
}

}  // namespace game

namespace game {

void XB_THISCALL VehiclePhys_RebuildPendingBody(uint8_t* vehicle) {
    if (!((at<uint32_t>(vehicle, 0x450) >> 1) & 1)) return;
    const uint32_t flags = at<uint32_t>(vehicle, 0x450) & ~2u;
    const uint32_t cleared = flags & ~1u;
    at<uint32_t>(vehicle, 0x450) = flags;
    at<uint32_t>(vehicle, 0x450) = cleared;
    const bool copy_matrix = (cleared >> 2) & 1;
    own_function<void XB_STDCALL(uint8_t*)>(&VehiclePhys_CreateBody)(vehicle);
    if (!copy_matrix) return;
    uint32_t* const to = at<uint32_t*>(at<uint8_t*>(vehicle, 0x444), 8);
    for (uint32_t i = 0; i < 16; ++i) to[i] = at<uint32_t>(vehicle, 0x460 + i * 4);
}

namespace {

// A path node's reset: both script strings cleared, +8/+0xc = -1, the vectors +0x14/+0x20 zero,
// +0x2c = the vector at 0x008db094, +0x38 = 0; bit fields +0x3c: bits 28-29 cleared, the low 28 set.
void path_node_reset(uint8_t* node) {
    using Clear = void XB_THISCALL(uint8_t*);
    own_function<Clear>(&ScriptString_Clear)(node);  // ScriptString_Clear
    own_function<Clear>(&ScriptString_Clear)(node + 4);
    const uint32_t bits = at<uint32_t>(node, 0x3c) & 0xcfffffffu;
    at<float>(node, 8) = title_global<float>(0x0083eb34);
    at<float>(node, 0xc) = title_global<float>(0x0083eb34);
    at<uint32_t>(node, 0x3c) = bits;
    for (uint32_t offset = 0x14; offset < 0x2c; offset += 4) at<float>(node, offset) = 0.0f;
    for (uint32_t i = 0; i < 3; ++i) at<float>(node, 0x2c + i * 4) = title_global<float>(0x008db094 + i * 4);
    at<float>(node, 0x38) = 0.0f;
    at<uint32_t>(node, 0x3c) = bits | 0x0fffffffu;
}

uint8_t* veh_node(int32_t index) { return title_global<uint8_t*>(0x0095c228 + static_cast<uint32_t>(index) * 4); }  // g_vehNodes

}  // namespace

void XB_REGS1 XB_CDECL Vehicle_SetSwitchNodes(uint32_t node_a, uint8_t* state, uint32_t node_b) {
    path_node_reset(state + 0x38);
    path_node_reset(state + 0x78);
    const int16_t a = static_cast<int16_t>(node_a);
    if (a < 0) return;
    const int16_t b = static_cast<int16_t>(node_b);
    if (b < 0) return;
    const uint8_t* const to = veh_node(b);
    const uint8_t* const from = veh_node(a);
    // 0x00248080 (ESI = path node, EDI = vehicle node): copies the node into the path state.
    vehicle_call(own_address(&VehNode_CopyToPathNode_entry), 0, 0, 0, address_of(state + 0x38), address_of(from), 0);
    vehicle_call(own_address(&VehNode_CopyToPathNode_entry), 0, 0, 0, address_of(state + 0x78), address_of(from), 0);
    const uint32_t index = static_cast<uint32_t>(static_cast<int32_t>(b));
    at<uint32_t>(state, 0x74) = (at<uint32_t>(state, 0x74) & ~0x3fffu) | (index & 0x3fffu);
    at<float>(state, 0x58) = at<float>(to, 0x14) - at<float>(from, 0x14);
    at<float>(state, 0x5c) = at<float>(to, 0x18) - at<float>(from, 0x18);
    at<float>(state, 0x60) = at<float>(to, 0x1c) - at<float>(from, 0x1c);
    at<float>(state, 0x70) = VectorNormalize(reinterpret_cast<float*>(state + 0x58));
}

}  // namespace game

namespace game {

// A vehicle's physics body record: flags +0x450 bit 0 cleared, bit 2 set; 0x002257f0 (EAX =
// vehicle); a record from the pool at 0x008fccf0 (30, count 0x008fcde4; null when used up) in
// +0x444 with a world body (+8), the entity +0x440 at +0/+4, its +0x160 at +0xc, the model bounds'
// centre ((maxs + mins) * 0.5 of entity +0x22c -> +0x38) at +0x20 and an identity frame moved by
// -centre at +0x70 (the entity matrix fetched first is overwritten); then 0x0022a590 builds the
// parts (vehicle, +0x420, 1).
void XB_STDCALL VehiclePhys_CreateBody(uint8_t* vehicle) {
    alignas(16) float frame[16];
    G_GetEntityMatrix(at<uint8_t*>(vehicle, 0x440), frame);
    const uint32_t flags = at<uint32_t>(vehicle, 0x450) & ~1u;
    at<uint32_t>(vehicle, 0x450) = flags;
    at<uint32_t>(vehicle, 0x450) = flags | 4;
    own_function<void XB_REGS1 XB_CDECL(uint8_t*)>(&Vehicle_RestoreTagMatrices)(vehicle);
    const float* const bounds = at<float*>(at<uint8_t*>(at<uint8_t*>(vehicle, 0x440), 0x22c), 0x38);
    const float half = title_global<float>(0x0083eb24);
    float centre[4];
    for (int32_t i = 0; i < 4; ++i) centre[i] = (bounds[4 + i] + bounds[i]) * half;
    Matrix_Identity(frame);
    for (int32_t i = 0; i < 4; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &centre[i], 4);
        bits ^= 0x80000000u;
        std::memcpy(&frame[12 + i], &bits, 4);
    }
    int32_t& used = title_global<int32_t>(0x008fcde4);
    uint8_t* body = nullptr;
    if (used < 30) {
        body = title_global<uint8_t*>(0x008fccf0 + static_cast<uint32_t>(used) * 4);
        used = used + 1;
    }
    at<uint8_t*>(vehicle, 0x444) = body;
    uint8_t* const world_body = PhysWorld_AllocBody();
    uint8_t* const ent = at<uint8_t*>(vehicle, 0x440);
    at<uint8_t*>(body, 8) = world_body;
    at<uint8_t*>(body, 0xc) = ent + 0x160;
    std::memcpy(body + 0x20, centre, 16);
    at<uint8_t*>(body, 0) = ent;
    at<uint8_t*>(body, 4) = ent;
    at<float>(body, 0x10) = 0.0f;
    at<uint32_t>(body, 0x40) = 0;
    at<uint32_t>(body, 0x48) = 0;
    std::memcpy(body + 0x70, frame, sizeof(frame));
    at<uint8_t*>(at<uint8_t*>(vehicle, 0x444), 0x48) = vehicle;
    own_function<void XB_STDCALL(uint8_t*, uint32_t, uint32_t)>(&VehiclePhys_SetupFromDef)(vehicle, at<uint32_t>(vehicle, 0x420), 1);
}

// EAX vehicle: each of its eight tag matrices (+0x220, 0x40 each) whose bone index (+0 + 4 i) is set goes
// to the model's bone matrices (entity +0x440 -> +0x22c -> +0x20, from +0x30).
void XB_REGS1 XB_CDECL Vehicle_RestoreTagMatrices(uint8_t* vehicle) {
    for (uint32_t i = 0; i < 8; ++i) {
        if (at<int32_t>(vehicle, i * 4) < 0) continue;
        uint8_t* const bones = at<uint8_t*>(at<uint8_t*>(at<uint8_t*>(vehicle, 0x440), 0x22c), 0x20);
        for (uint32_t w = 0; w < 16; ++w) at<uint32_t>(bones, 0x30 + i * 0x40 + w * 4) = at<uint32_t>(vehicle, 0x220 + i * 0x40 + w * 4);
    }
}

PLATFORM_ASM_ENTRY(VehiclePhys_Destroy_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(VehiclePhys_Destroy) "addl $4, %esp\n"
                   "ret")

// ESI record (pool 0x008fe0d0): its world body (+0x34) and object (+0x30) destroyed, the record freed.
void XB_CDECL VehiclePhys_Destroy(uint8_t* phys) {
    if (at<uint32_t>(phys, 0x34)) {
        PhysWorld_DestroyBody(at<uint32_t>(phys, 0x34));
        at<uint32_t>(phys, 0x34) = 0;
    }
    if (at<uint32_t>(phys, 0x30)) {
        PhysWorld_DestroyObject1c0(at<uint32_t>(phys, 0x30));
        at<uint32_t>(phys, 0x30) = 0;
    }
    VehiclePhysPool_Free(phys, 0, reinterpret_cast<uint8_t*>(uintptr_t{0x008fe0d0}));
}

namespace {

// The bone matrix the skeleton (+0x22c, matrices +0x20 from bone +0x60) keeps for `bone`, null when
// there are no matrices and the bone is 0 (its +0x38 is the height the wheels get).
uint8_t* skeleton_bone(const uint8_t* ent, int32_t bone) {
    const uint8_t* const obj = at<uint8_t*>(ent, 0x22c);
    uint8_t* const matrices = at<uint8_t*>(obj, 0x20);
    const uint32_t base = matrices ? address_of(matrices) + (static_cast<uint32_t>(obj[0x60]) << 6) + 0x30 : 0;
    return pointer<uint8_t>(base + (static_cast<uint32_t>(bone) << 6));
}

}  // namespace

void XB_STDCALL Vehicle_UpdateFromPhysics(uint8_t* record) {
    alignas(16) float m[16];
    const float* const pose = G_GetEntityMatrix(at<uint8_t*>(record, 0x154), m);
    std::memcpy(record + 0x170, pose, 0x40);
    std::memcpy(m, at<uint8_t*>(record, 0x34), 0x40);
    alignas(16) float center[4];
    std::memcpy(center, record + 0x160, 16);
    for (int k = 0; k < 4; ++k) {
        const float t = (m[k] * center[0] + m[4 + k] * center[1]) + m[8 + k] * center[2];
        m[12 + k] = m[12 + k] - t;
    }
    uint8_t* const entity = at<uint8_t*>(record, 0x154);
    std::memcpy(entity + 0x160, m, 0x40);
    vehicle_call(own_address(&G_SetPoseFromPhysics), address_of(entity), 0, address_of(record) + 0x160, address_of(m + 16), address_of(entity) + 0x1a0,
                 0);  // G_SetPoseFromPhysics
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t name = title_global<uint32_t>(0x0098a490 + i * 4);
        uint8_t* const ent = at<uint8_t*>(record, 0x154);
        const DObj* const obj = at<const DObj*>(ent, 0x22c);
        const int32_t bone = obj ? DObj_GetTagIndex(obj, name) : -1;
        SV_DObjCreateSkeleton(ent);
        at<float>(skeleton_bone(ent, bone), 0x38) = at<float>(record, 0x134 + i * 4) + title_global<float>(0x0083ef10);
    }
}

uint8_t XB_CDECL VehiclePhys_HasWheelBones(uint8_t* ent) {
    for (uint32_t i = 0; i < 4; ++i) {
        const DObj* const obj = at<const DObj*>(ent, 0x22c);
        const int32_t bone = obj ? DObj_GetTagIndex(obj, title_global<uint32_t>(0x0098a490 + i * 4)) : -1;
        if (bone < 0) return 0;
        SV_DObjCreateSkeleton(ent);
        if (!skeleton_bone(ent, bone)) return 0;
    }
    float v[4];
    std::memcpy(v, ent + 0x160, 16);
    const float squared[4] = {v[0] * v[0], v[1] * v[1], v[2] * v[2], v[3] * v[3]};  // MULPS: every lane's flags
    platform_keep(squared);
    const float length_squared = (squared[0] + squared[1]) + squared[2];
    float length = length_squared;
    if (length_squared == length_squared && length_squared != 0.0f) {  // UCOMISS + JNE: NaN counts as equal
        const float r = platform_rsqrt_estimate(length_squared);
        const float scaled = length_squared * r;
        length = ((title_global<float>(0x0082e660) - scaled * r) * scaled) * title_global<float>(0x0082e670);  // 3, 0.5
    }
    return title_global<float>(0x0083eb24) > length ? 0 : 1;  // 0.5; NaN gives 1
}

namespace {

// A title function taking EAX, ECX and EBX; returns its EAX.
uint32_t call_eax_ecx_ebx(uint32_t function, uint32_t eax, uint32_t ecx, uint32_t ebx) {
    __asm__ volatile("pushl %%ebx\n\tmovl %%esi, %%ebx\n\tcall *%%edx\n\tpopl %%ebx"
                     : "+a"(eax), "+c"(ecx), "+d"(function)
                     : "S"(ebx)
                     : "memory", "cc");
    return eax;
}

// MINPS / MAXPS per lane: the second operand when either is NaN.
float min_lane(float a, float b) { return a < b ? a : b; }
float max_lane(float a, float b) { return a > b ? a : b; }

}  // namespace

float* XB_CDECL VehiclePhys_GetWheelBounds(uint8_t* ent, float* wheels, float* size, float* center, uint32_t* height) {
    *height = at<uint32_t>(at<uint8_t*>(ent, 0x248), 0x1c0);
    for (uint32_t i = 0; i < 4; ++i) {
        alignas(16) float buffer[4];
        const uint32_t origin = call_eax_ecx_ebx(own_address(&G_DObjGetTagLocalOrigin_entry), address_of(ent), i, address_of(buffer));  // G_DObjGetTagLocalOrigin
        std::memcpy(wheels + i * 4, pointer<float>(origin), 16);
        wheels[i * 4 + 2] = title_global<float>(0x0083ef10);  // 15
    }
    float lo[4], hi[4];
    std::memcpy(lo, wheels, 16);
    std::memcpy(hi, wheels, 16);
    for (uint32_t i = 1; i < 4; ++i)
        for (uint32_t k = 0; k < 4; ++k) {
            lo[k] = min_lane(lo[k], wheels[i * 4 + k]);
            hi[k] = max_lane(hi[k], wheels[i * 4 + k]);
        }
    for (uint32_t k = 0; k < 4; ++k) size[k] = hi[k] - lo[k];
    size[2] = title_global<float>(0x0083f09c);  // 35
    size[0] = *reinterpret_cast<const float*>(height) * title_global<float>(0x0083ec98) + size[0];  // 3.5
    const float half = title_global<float>(0x0083eb24);
    for (uint32_t k = 0; k < 4; ++k) center[k] = half * (lo[k] + hi[k]);
    center[2] = *reinterpret_cast<const float*>(height) + center[2];
    return center;
}

PLATFORM_ASM_ENTRY(VehiclePhys_GetOrCreate_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(VehiclePhys_GetOrCreate) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL VehiclePhys_GetOrCreate(uint8_t* ent) {
    uint8_t* const existing = at<uint8_t*>(at<uint8_t*>(ent, 0x248), 0x300);
    if (existing) return existing;
    uint8_t* const record = VehiclePhys_AllocFromPool(0, pointer<uint8_t>(0x008fe0d0));
    call_eax_ecx_ebx(own_address(&VehiclePhys_Init_entry), address_of(ent), 0, address_of(record));  // VehiclePhys_Init (EAX entity, EBX record)
    at<uint8_t*>(at<uint8_t*>(ent, 0x248), 0x300) = record;
    return record;
}

uint32_t XB_REGS3 XB_CDECL G_CheckOtherClientField100(const uint8_t* ent, uint32_t, const uint8_t* other) {
    if (!at<uint32_t>(ent, 0x29c) || ent == other) return 0;
    const uint8_t* const client = at<uint8_t*>(ent, 0x23c);
    if (!client || !at<uint32_t>(other, 0x23c)) return 0;
    return at<int32_t>(client, 0x100) > 0;
}

PLATFORM_ASM_ENTRY(VehPath_GetLookaheadPos_entry,
                   "pushl %ebx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(VehPath_GetLookaheadPos) "addl $8, %esp\n"
                   "ret")

namespace {

// The next node: the low 14 bits of +0x3c, sign-extended.
int32_t veh_next(const uint8_t* node) { return static_cast<int32_t>(at<uint32_t>(node, 0x3c) << 18) >> 18; }

}  // namespace

uint32_t XB_CDECL VehPath_GetLookaheadPos(const uint8_t* state, float* out) {
    const uint8_t* node = veh_node(*reinterpret_cast<const int16_t*>(state));
    float d;
    uint32_t eax;
    const int32_t first = veh_next(node);
    if (first < 0) {
        d = title_global<float>(0x0083eb98);  // 10
        eax = static_cast<uint32_t>(first);
    } else {
        const uint8_t* const next = veh_node(first);
        eax = address_of(next);
        const float t = at<float>(state, 4);
        d = ((at<float>(next, 8) - at<float>(node, 8)) * t + at<float>(node, 8)) * at<float>(state, 0xc);
        d = d + at<float>(node, 0x38) * t;
        const int32_t count = title_global<int32_t>(0x0094d4d8);
        for (int32_t i = 0; i < count;) {
            const int32_t index = veh_next(node);
            ++i;
            if (index < 0) {
                d = 0.0f;
                break;
            }
            const float length = at<float>(node, 0x38);
            if (length == 0.0f) {  // UCOMISS/LAHF: ordered equal only
                d = 0.0f;
                break;
            }
            if (length > d) break;
            node = veh_node(index);
            d = d - length;
        }
    }
    out[0] = at<float>(node, 0x20) * d + at<float>(node, 0x14);
    out[1] = at<float>(node, 0x24) * d + at<float>(node, 0x18);
    out[2] = at<float>(node, 0x28) * d + at<float>(node, 0x1c);
    return eax;
}

PLATFORM_ASM_ENTRY(VehPath_GetFlagBlend_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(VehPath_GetFlagBlend) "addl $4, %esp\n"
                   "subl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL VehPath_GetFlagBlend(const uint8_t* state) {
    const uint8_t* const node = veh_node(*reinterpret_cast<const int16_t*>(state));
    const uint32_t bits = at<uint32_t>(node, 0x3c);
    const float one = title_global<float>(0x0083eb20);
    if (veh_next(node) < 0) return (bits & 0x30000000) ? one : 0.0f;
    const bool next_set = (at<uint32_t>(veh_node(veh_next(node)), 0x3c) & 0x30000000) != 0;
    if ((bits & 0x30000000) && next_set) return one;
    if (!(static_cast<int32_t>(bits << 2) >> 30)) return next_set ? at<float>(state, 4) : 0.0f;
    return next_set ? 0.0f : one - at<float>(state, 4);
}

PLATFORM_ASM_ENTRY(Vehicle_TakesDamageFromMod_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_TakesDamageFromMod) "addl $8, %esp\n"
                   "ret")

// Means of death 1-2, 3-4, 5-6: the def's +0x30 / +0x34 / +0x38 unset; 23 and 27: never; else always.
uint32_t XB_CDECL Vehicle_TakesDamageFromMod(const uint8_t* ent, uint32_t mod) {
    const uint8_t* const def = vehicle_def(at<int16_t>(at<uint8_t*>(ent, 0x248), 0x178));
    switch (mod) {
        case 1: case 2: return at<uint32_t>(def, 0x30) == 0;
        case 3: case 4: return at<uint32_t>(def, 0x34) == 0;
        case 5: case 6: return at<uint32_t>(def, 0x38) == 0;
        case 23: case 27: return 0;
        default: return 1;
    }
}

uint32_t XB_REGS1 XB_CDECL Vehicle_HasDefType1(const uint8_t* ent) {
    const uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    if (!vehicle) return 0;
    return at<int16_t>(vehicle_def(at<int16_t>(vehicle, 0x178)), 0x20) == 1;
}

PLATFORM_ASM_ENTRY(VehNode_CopyToPathNode_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(VehNode_CopyToPathNode) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL VehNode_CopyToPathNode(uint8_t* dst, const uint8_t* src) {
    using Assign = void XB_THISCALL(uint8_t*, const uint8_t*);
    own_function<Assign>(&ScriptString_Assign_entry)(dst, src);          // ScriptString_Assign
    own_function<Assign>(&ScriptString_Assign_entry)(dst + 4, src + 4);  // ScriptString_Assign
    uint32_t bits = at<uint32_t>(dst, 0x3c);
    bits ^= ((static_cast<uint32_t>(static_cast<int32_t>(at<uint32_t>(src, 0x3c) << 2) >> 2)) ^ bits) & 0x30000000u;
    at<uint32_t>(dst, 0x3c) = bits;
    at<uint32_t>(dst, 8) = at<uint32_t>(src, 8);
    at<uint32_t>(dst, 0xc) = at<uint32_t>(src, 0xc);
    for (uint32_t offset = 0x14; offset <= 0x38; offset += 4) at<uint32_t>(dst, offset) = at<uint32_t>(src, offset);
    bits = at<uint32_t>(dst, 0x3c);
    const uint32_t low = bits ^ ((static_cast<uint32_t>(veh_next(src)) ^ bits) & 0x3fffu);
    at<uint32_t>(dst, 0x3c) = low;
    const uint32_t mid = static_cast<uint32_t>(static_cast<int32_t>(at<uint32_t>(src, 0x3c) << 4) >> 4);
    at<uint32_t>(dst, 0x3c) = ((mid ^ low) & 0x0fffc000u) ^ low;
    return low;
}

void XB_REGS3 XB_CDECL Vehicle_GetBodyRock(const uint8_t* ent, uint32_t, float* v) {
    const uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    if (!(at<float>(vehicle, 0x1f0) > title_global<float>(0x0083eafc))) return;
    const float angle = platform_x87_mul(platform_x87_mul(at<float>(vehicle, 0x1f4), title_global<float>(0x0083ed00)),
                                         title_global<float>(0x0083ecfc));  // degrees to radians
    const long double rock = platform_x87_sin_mul(angle, at<float>(vehicle, 0x1f0)) * platform_x87_ld(at<float>(vehicle, 0x1f0));
    v[0] = static_cast<float>(rock * platform_x87_ld(at<float>(vehicle, 0x1e8)) + platform_x87_ld(v[0]));
    v[2] = static_cast<float>(rock * platform_x87_ld(at<float>(vehicle, 0x1ec)) + platform_x87_ld(v[2]));
}

PLATFORM_ASM_ENTRY(Vehicle_ApproachProportional_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm3, (%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(Vehicle_ApproachProportional) "addl $16, %esp\n"
                   "subl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

namespace {

float abs_bits(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    bits &= 0x7fffffffu;
    std::memcpy(&x, &bits, 4);
    return x;
}

// The step unless the gap is within 0x0083eb88 or the step overshoots it (x87 compares).
float approach(float target, float current, float gap, float step) {
    const float eps = title_global<float>(0x0083eb88);
    if (!platform_x87_greater(abs_bits(gap), eps)) return target;
    if (platform_x87_greater(abs_bits(step), abs_bits(gap))) return target;
    return step + current;
}

}  // namespace

float XB_CDECL Vehicle_ApproachProportional(float target, float current, float scale, int32_t count) {
    const float gap = target - current;
    const float step = ((static_cast<float>(count) * title_global<float>(0x0083eb4c)) * gap) * scale;
    return approach(target, current, gap, step);
}

PLATFORM_ASM_ENTRY(Vehicle_ApproachAtRate_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm3, (%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(Vehicle_ApproachAtRate) "addl $12, %esp\n"
                   "subl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL Vehicle_ApproachAtRate(float target, float current, float rate) {
    const float gap = target - current;
    const float step = gap > 0.0f ? title_global<float>(0x008ddaa0) * rate : 0.0f - title_global<float>(0x008ddaa0) * rate;
    return approach(target, current, gap, step);
}

namespace {

int32_t veh_prev(const uint8_t* node) { return static_cast<int32_t>(at<uint32_t>(node, 0x3c) << 4) >> 18; }

bool value_set(float v, bool zero_counts) { return zero_counts ? v >= 0.0f : v > 0.0f; }

float fill_unset(int32_t index, uint32_t offset, bool zero_counts) {
    const uint8_t* node = veh_node(index);
    const float own = at<float>(node, offset);
    if (own >= 0.0f) return own;
    const float none = title_global<float>(0x0083eb34);  // -1
    float before = none, after = none, back = 0.0f, ahead = 0.0f;
    const int32_t count = title_global<int32_t>(0x0094d4d8);
    const int32_t prev = veh_prev(node);
    if (prev >= 0) {
        const uint8_t* p = veh_node(prev);
        for (int16_t i = 0; i < count;) {
            back = at<float>(p, 0x38) + back;
            ++i;
            if (value_set(at<float>(p, offset), zero_counts)) {
                before = at<float>(p, offset);
                break;
            }
            const int32_t k = veh_prev(p);
            if (k < 0 || k == index) break;
            p = veh_node(k);
        }
    }
    for (int16_t i = 0; i < count;) {
        ++i;
        if (value_set(at<float>(node, offset), zero_counts)) {
            after = at<float>(node, offset);
            break;
        }
        const int32_t k = veh_next(node);
        if (k < 0 || k == index) break;
        ahead = at<float>(node, 0x38) + ahead;
        node = veh_node(k);
    }
    if (0.0f > before) return 0.0f > after ? 0.0f : after;
    if (0.0f > after) return before;
    const float total = ahead + back;
    if (0.0f >= total) return 0.0f;
    return (back / total) * (after - before) + before;
}

}  // namespace

PLATFORM_ASM_ENTRY(VehNode_FillUnsetValue0c_entry,
                   "subl $32, %esp\n"
                   "movups %xmm6, (%esp)\n"
                   "movups %xmm7, 16(%esp)\n"
                   "movswl %di, %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(VehNode_FillUnsetValue0c) "addl $4, %esp\n"
                   "movups (%esp), %xmm6\n"
                   "movups 16(%esp), %xmm7\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $32, %esp\n"
                   "ret")

float XB_CDECL VehNode_FillUnsetValue0c(int32_t index) { return fill_unset(index, 0xc, false); }

PLATFORM_ASM_ENTRY(VehNode_FillUnsetValue08_entry,
                   "subl $32, %esp\n"
                   "movups %xmm6, (%esp)\n"
                   "movups %xmm7, 16(%esp)\n"
                   "movswl %di, %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(VehNode_FillUnsetValue08) "addl $4, %esp\n"
                   "movups (%esp), %xmm6\n"
                   "movups 16(%esp), %xmm7\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $32, %esp\n"
                   "ret")

float XB_CDECL VehNode_FillUnsetValue08(int32_t index) { return fill_unset(index, 8, true); }

PLATFORM_ASM_ENTRY(PhysBody_Clear_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(PhysBody_Clear) "addl $4, %esp\n"
                   "ret")


uint32_t XB_CDECL PhysBody_Clear(uint8_t* state) {
    at<uint16_t>(state, 0) = 0xffff;
    at<uint16_t>(state, 2) = 0;
    for (uint32_t offset = 4; offset <= 0x34; offset += 4) at<float>(state, offset) = 0.0f;
    path_node_reset(state + 0x38);
    path_node_reset(state + 0x78);
    return at<uint32_t>(state, 0x78 + 0x3c);
}

PLATFORM_ASM_ENTRY(VehNode_AllocSlot_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(VehNode_AllocSlot) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL VehNode_AllocSlot(uint8_t* pool) {
    if (!at<int16_t>(pool, 2) || at<int16_t>(pool, 4) >= 0x80) {
        const uint32_t block = own_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(&Heap_AllocAligned)(0x10, 0x2000);  // Heap_AllocAligned
        at<uint32_t>(pool, 8 + static_cast<uint32_t>(at<int16_t>(pool, 2)) * 4) = block;
        uint8_t* node = at<uint8_t*>(pool, 8 + static_cast<uint32_t>(at<int16_t>(pool, 2)) * 4);
        std::memset(node, 0, 0x40);  // only the first node's bytes
        using FromRep = void XB_THISCALL(uint8_t*, uint32_t);
        for (uint32_t i = 0; i < 0x80; ++i, node += 0x40) {
            if (!node) continue;
            own_function<FromRep>(&ScriptString_FromRep)(node, 0);         // ScriptString_FromRep
            own_function<FromRep>(&ScriptString_FromRep)(node + 4, 0);     // ScriptString_FromRep
            own_function<FromRep>(&ScriptString_FromRep)(node + 0x10, 0);  // ScriptString_FromRep
        }
        ++at<int16_t>(pool, 2);
        at<int16_t>(pool, 4) = 0;
    }
    uint8_t* const block = at<uint8_t*>(pool, 4 + static_cast<uint32_t>(at<int16_t>(pool, 2)) * 4);
    const uint16_t used = at<uint16_t>(pool, 4);
    uint8_t* const node = block + static_cast<int32_t>(static_cast<int16_t>(used)) * 0x40;
    at<uint16_t>(pool, 4) = static_cast<uint16_t>(used + 1);
    title_global<uint8_t*>(0x0095c228 + static_cast<uint32_t>(at<int16_t>(pool, 0)) * 4) = node;
    ++at<int16_t>(pool, 0);
    return node;
}

uint32_t XB_STDCALL VehNodePool_Free(uint8_t* pool) {
    using Release = void XB_THISCALL(uint8_t*);
    for (int32_t i = 0; i < at<int16_t>(pool, 2); ++i) {
        uint8_t* node = at<uint8_t*>(pool, 8 + static_cast<uint32_t>(i) * 4);
        for (uint32_t k = 0; k < 0x80; ++k, node += 0x40) {
            own_function<Release>(&ScriptString_Release_entry)(node + 0x10);  // ScriptString_Release
            own_function<Release>(&ScriptString_Release_entry)(node + 4);     // ScriptString_Release
            own_function<Release>(&ScriptString_Release_entry)(node);         // ScriptString_Release
        }
        own_function<void XB_CDECL(uint32_t)>(&Z_Free)(at<uint32_t>(pool, 8 + static_cast<uint32_t>(i) * 4));  // Z_Free
    }
    at<uint16_t>(pool, 0) = 0;
    at<uint16_t>(pool, 2) = 0;
    at<uint16_t>(pool, 4) = 0;
    std::memset(pool + 8, 0, 0x40);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pool + 8));  // EAX = the cleared block list
}

uint8_t* XB_CDECL VehNode_Alloc() {
    uint8_t* const node = VehNode_AllocSlot(pointer<uint8_t>(0x00988520));
    path_node_reset(node);
    ++title_global<int32_t>(0x0094d4d8);  // g_numVehNodes
    return node;
}

PLATFORM_ASM_ENTRY(VehPath_InitAtNode_entry,
                   "pushl %edi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(VehPath_InitAtNode) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL VehPath_InitAtNode(uint32_t index, uint8_t* state) {
    const uint8_t* const node = veh_node(static_cast<int16_t>(index));
    at<uint16_t>(state, 0) = static_cast<uint16_t>(index);
    at<uint16_t>(state, 2) = 0;
    at<float>(state, 4) = 0.0f;
    at<uint32_t>(state, 8) = at<uint32_t>(node, 8);
    at<uint32_t>(state, 0xc) = at<uint32_t>(node, 0xc);
    at<float>(state, 0x10) = (at<uint32_t>(node, 0x3c) & 0x30000000) ? title_global<float>(0x0083eb20) : 0.0f;
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(state, 0x14 + k * 4) = at<uint32_t>(node, 0x14 + k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(state, 0x20 + k * 4) = at<uint32_t>(node, 0x2c + k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(state, 0x2c + k * 4) = at<uint32_t>(node, 0x14 + k * 4);
    path_node_reset(state + 0x38);
    path_node_reset(state + 0x78);
    return at<uint32_t>(state, 0x78 + 0x3c);
}

uint8_t* XB_THISCALL Vehicle_LoadPhysicsState(uint8_t* ent) {
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(vehicle, 0xd0 + k * 4) = at<uint32_t>(ent, 0x140 + k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(vehicle, 0xf0 + k * 4) = at<uint32_t>(ent, 0x150 + k * 4);
    for (uint32_t k = 0; k < 0x2e; ++k) title_global<uint32_t>(0x009883b0 + k * 4) = at<uint32_t>(vehicle, k * 4);
    for (uint32_t k = 0; k < 0x2c; ++k) title_global<uint32_t>(0x00988470 + k * 4) = at<uint32_t>(vehicle, 0xc0 + k * 4);
    return vehicle;
}

void XB_REGS3 XB_CDECL VehPath_GetNodeAngles(uint32_t, uint32_t, float* out, const uint8_t* state) {
    const uint8_t* const node = veh_node(*reinterpret_cast<const int16_t*>(state));
    const uint32_t bits = at<uint32_t>(node, 0x3c);
    if (veh_next(node) < 0) {
        if (bits & 0x30000000) for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(out, k * 4) = at<uint32_t>(node, 0x2c + k * 4);
        return;
    }
    const uint8_t* const next = veh_node(veh_next(node));
    const bool next_set = (at<uint32_t>(next, 0x3c) & 0x30000000) != 0;
    if (!(bits & 0x30000000) && !next_set) return;
    // From / to: the node's angles, the next node's or the current ones in out (copied first).
    float from[3], to[3];
    const float* a = reinterpret_cast<const float*>(node + 0x2c);
    const float* b = out;
    if (static_cast<int32_t>(bits << 2) >> 30) {
        if (next_set) b = reinterpret_cast<const float*>(next + 0x2c);
    } else if (next_set) {
        a = out;
        b = reinterpret_cast<const float*>(next + 0x2c);
    }
    for (uint32_t k = 0; k < 3; ++k) from[k] = a[k];
    for (uint32_t k = 0; k < 3; ++k) to[k] = b[k];
    const float half = title_global<float>(0x0083eb60);    // 180
    const float full = title_global<float>(0x0083ebcc);    // 360
    const float minus = title_global<float>(0x0083ebd0);   // -180
    const float to_short = title_global<float>(0x0083ebd8);  // 65536 / 360
    const float from_short = title_global<float>(0x0083ebd4);  // 360 / 65536
    for (uint32_t k = 0; k < 3; ++k) {
        float x = to[k];
        const float base = from[k];
        if (x - base > half) x = x - full;
        if (minus > x - base) x = x + full;
        x = (x - base) * at<float>(state, 4) + base;
        out[k] = x;
        const int32_t s = static_cast<int32_t>(x * to_short) & 0xffff;  // CVTTSS2SI
        x = static_cast<float>(s) * from_short;
        if (x > half) x = x - full;
        out[k] = x;
    }
}

uint32_t XB_REGS1 XB_CDECL VehNode_FindByTargetname(const uint32_t* name, const float* origin) {
    // AX = the node index or -1; the upper half is what the original's EAX held there.
    const uint32_t rep = *name;
    if (!rep) return 0xffff;
    const char* const text = pointer<const char>(rep + 6);
    if (!text || !*text) return rep | 0xffff;
    const int32_t count = title_global<int32_t>(0x0094d4d8);  // g_numVehNodes
    if (count <= 0) return static_cast<uint32_t>(count) | 0xffff;
    int16_t i = 0;
    do {
        const uint8_t* const node = veh_node(i);
        const uint32_t own = at<uint32_t>(node, 0);
        const char* const own_text = own ? pointer<const char>(own + 6) : nullptr;
        if (std::strcmp(own_text, text) == 0) {
            if (!origin) return static_cast<uint16_t>(i);
            if (at<float>(node, 0x14) == origin[0] && at<float>(node, 0x18) == origin[1] && at<float>(node, 0x1c) == origin[2])
                return static_cast<uint16_t>(i);
        }
        ++i;
    } while (static_cast<int32_t>(i) < title_global<int32_t>(0x0094d4d8));
    return (static_cast<uint32_t>(static_cast<int32_t>(i)) & 0xffff0000u) | 0xffff;
}

void XB_REGS1 XB_CDECL Vehicle_UpdateTurnRateAndWheelAngle(uint8_t* ent, int32_t elapsed) {
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    if (!at<uint32_t>(def, 0x24)) {
        at<float>(vehicle, 0x2a0) = 0.0f;
        return;
    }
    // The yaw change since the last frame, wrapped to -180..180, per second; clamped to +-60.
    const long double diff = platform_x87_ld(at<float>(vehicle, 0xe4)) - platform_x87_ld(at<float>(vehicle, 0xf4));
    const float wrapped = static_cast<float>(title_ci_fmod(diff, title_global<double>(0x0083ed40)));  // 360
    float d = wrapped;
    if (platform_x87_greater(wrapped, title_global<float>(0x0083eb60))) d = d - title_global<float>(0x0083ebcc);
    if (title_global<float>(0x0083ebd0) > d) d = d + title_global<float>(0x0083ebcc);
    const float seconds = static_cast<float>(elapsed) * title_global<float>(0x0083eb4c);
    float rate = (title_global<float>(0x008feff4) / seconds) * d;
    at<uint32_t>(vehicle, 0x270) = at<uint32_t>(vehicle, 0x2a0);
    if (title_global<float>(0x0083ee2c) > rate) rate = title_global<float>(0x0083ee2c);
    else if (rate > title_global<float>(0x0083eb3c)) rate = title_global<float>(0x0083eb3c);
    at<float>(vehicle, 0x2a0) = rate;
    // Wheel angle: ground speed over the wheel circumference, in degrees.
    const float vx = at<float>(vehicle, 0x100), vy = at<float>(vehicle, 0x104);
    const float speed = platform_x87_mul(title_ci_sqrt(vx * vx + vy * vy), title_global<float>(0x008ddaa0));
    const float circumference = at<float>(vehicle, 0x1c0) * title_global<float>(0x0083edac);
    at<float>(vehicle, 0x1c4) = at<float>(vehicle, 0x1c4) + (speed / circumference) * title_global<float>(0x0083ebcc);
}

uint8_t* XB_THISCALL Vehicle_ResetMotionState(uint8_t* ent) {
    uint8_t* const state = at<uint8_t*>(ent, 0x248) + 0xc0;
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(state, k * 4) = at<uint32_t>(ent, 0x140 + k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(state, 0x10 + k * 4) = at<uint32_t>(ent, 0x140 + k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(state, 0x20 + k * 4) = at<uint32_t>(ent, 0x150 + k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(state, 0x30 + k * 4) = at<uint32_t>(ent, 0x150 + k * 4);
    for (uint32_t offset = 0x40; offset < 0x90; offset += 4) {
        if (offset == 0x4c || offset == 0x5c) continue;
        at<float>(state, offset) = 0.0f;
    }
    for (uint32_t offset = 0x90; offset < 0xa8; offset += 4) at<uint32_t>(state, offset) = 0;
    return state;
}

uint32_t XB_CDECL Vehicle_ResetCachedBounds() {
    title_global<uint32_t>(0x0095f68c) = 0;
    const uint32_t slots = title_global<uint32_t>(0x0098537c);
    if (!slots) return 0;
    if (!title_global<uint16_t>(0x00986000)) return slots;  // g_vehicleSlotCount
    for (int32_t i = 0; i < title_global<uint16_t>(0x00986000); ++i) {
        uint8_t* const ent = entity_from_handle(at<uint32_t>(pointer<uint8_t>(title_global<uint32_t>(0x0098537c)), i * 0x340 + 0x170));
        if (!ent) continue;
        for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(ent, 0x430 + k * 4) = 0x7f7fffff;   // FLT_MAX
        for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(ent, 0x440 + k * 4) = at<uint32_t>(ent, 0x430 + k * 4) ^ 0x80000000u;
    }
    return title_global<uint16_t>(0x00986000);
}

void XB_REGS1 XB_CDECL Player_UpdateLookAtActorInfo(uint8_t* ent) {
    using Va = const char* XB_CDECL(const char*, ...);
    const auto va_title = own_function<Va>(&va_entry);  // va
    const char* const fmt_s = pointer<const char>(0x0082ddd4);  // "%s"
    const uint8_t* const actor = at<uint8_t*>(at<uint8_t*>(ent, 0x23c), 0x5f4);
    const uint8_t* const info = actor ? at<uint8_t*>(actor, 0x240) : nullptr;
    const uint32_t rep = info && at<uint32_t>(info, 0x2d0) ? at<uint32_t>(info, 0xec) : 0;
    if (!rep || !*pointer<const char>(rep + 6)) {
        SV_SetConfigstring(0xd, va_title(fmt_s, pointer<const char>(0x00832430)));  // "none"
        return;
    }
    SV_SetConfigstring(0xd, va_title(fmt_s, pointer<const char>(rep + 6)));
    float health = static_cast<float>(at<int32_t>(actor, 0x350)) / static_cast<float>(at<int32_t>(actor, 0x354));
    if (health > title_global<float>(0x0083eb20)) health = title_global<float>(0x0083eb20);
    SV_SetConfigstring(0xe, va_title(pointer<const char>(0x008325b4), static_cast<double>(health)));  // "%f"
    const uint8_t weapon = static_cast<uint8_t>(BG_FindWeaponIndexForHash(at<uint32_t>(at<uint8_t*>(actor, 0x240), 0xf0)));
    SV_SetConfigstring(0xf, va_title(fmt_s, at<const char*>(weapon_def(weapon), 0x10)));
}

uint32_t XB_CDECL ClientThink_RunPendingTouch(uint8_t* ent) {
    uint8_t* const client = at<uint8_t*>(ent, 0x23c);
    const uint32_t handle = at<uint32_t>(client, 0x628);
    if (!handle) return 0;
    uint8_t* const other = entity_from_handle(handle);
    if (!other) return 0;
    if (!vehicle_call(own_address(&G_EntityWeaponRecordFlag), address_of(other), 0, 0, 0, 0, 0)) return 0;  // FUN_00248e10
    if (title_global<int32_t>(0x0098540c) - at<int32_t>(client, 0x62c) <= title_global<int32_t>(0x0095bc34)) return 0;  // level.time
    at<uint32_t>(client, 0x628) = 0;
    // EDI = the touched entity (notified), as the original (0x002509a3): EDI 0 crashed G_NotifyWithEntity on weapon pickup.
    vehicle_call(own_address(&G_NotifyWithOwnHandle_entry), address_of(ent), 0, 0, 0, address_of(other), title_global<uint32_t>(0x0097a814));  // G_NotifyWithOwnHandle(touch)
    const uint32_t touch = at<uint32_t>(other, 0x338);
    if (!touch) return 0;
    at<uint8_t>(other, 0x297) = 1;
    using Touch = void XB_CDECL(uint8_t*, uint8_t*, uint32_t);
    title_function<Touch>(title_global<uint32_t>(0x008da814 + touch * 4))(other, ent, 0);  // entityTouchFuncs
    return 1;
}
PLATFORM_ASM_ENTRY(VehPath_UpdateSegment_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(VehPath_UpdateSegment) "addl $20, %esp\n"
                   "ret")

uint32_t XB_CDECL VehPath_UpdateSegment(uint8_t* state, const float* dir, const uint8_t* ent, uint8_t keep_values,
                                        int32_t stop) {
    float t = at<float>(state, 4);
    int32_t index = at<uint16_t>(state, 0);  // the word, zero-extended
    int32_t current = static_cast<int16_t>(index);
    const uint8_t* node = veh_node(current);
    uint32_t reached = 0;
    for (int32_t i = 0; i < title_global<int32_t>(0x0094d4d8);) {
        node = veh_node(current);
        ++i;
        const int32_t k = veh_next(node);
        if (k < 0 || at<float>(node, 0x38) == 0.0f || reached) {
            t = 0.0f;
            break;
        }
        const uint8_t* const next = veh_node(k);
        const uint8_t* def = nullptr;
        if (ent && at<uint8_t*>(ent, 0x248)) def = vehicle_def(at<int16_t>(at<uint8_t*>(ent, 0x248), 0x178));
        // How far along dir the vehicle is past this node and short of the next one.
        float past = (dir[2] * (at<float>(state, 0x1c) - at<float>(node, 0x1c)) +
                      dir[1] * (at<float>(state, 0x18) - at<float>(node, 0x18))) +
                     dir[0] * (at<float>(state, 0x14) - at<float>(node, 0x14));
        if (def && at<int16_t>(def, 0x20) == 3 && 0.0f > past) past = 0.0f;
        const float ahead = (dir[2] * (at<float>(next, 0x1c) - at<float>(state, 0x1c)) +
                             dir[1] * (at<float>(next, 0x18) - at<float>(state, 0x18))) +
                            dir[0] * (at<float>(next, 0x14) - at<float>(state, 0x14));
        if (past == 0.0f && ahead == 0.0f) {
            t = 0.0f;
            break;
        }
        if (!(0.0f > past) && !(0.0f > ahead)) {
            t = past / (ahead + past);
            break;
        }
        // Behind the node or past the next one: step on (far jumps only for def type 3).
        const float far = title_global<float>(0x0083ecf4);  // 256
        if (!platform_x87_greater(abs_bits(ahead), far) || (def && at<int16_t>(def, 0x20) == 3)) {
            current = static_cast<int16_t>(k);
            index = k;
            if (current == stop) reached = 1;
        }
        if (!(static_cast<int16_t>(i) < title_global<int32_t>(0x0094d4d8))) break;
    }
    at<uint16_t>(state, 0) = static_cast<uint16_t>(index);
    at<uint16_t>(state, 2) = veh_next(node) < 0 ? 1 : 0;
    at<float>(state, 4) = t;
    const uint8_t* const here = veh_node(static_cast<int16_t>(index));
    for (uint32_t offset = 8; offset <= 0xc; offset += 4) {
        if (offset == 8 && keep_values) continue;
        const int32_t k = veh_next(here);
        at<float>(state, offset) =
            k < 0 ? at<float>(here, offset) : (at<float>(veh_node(k), offset) - at<float>(here, offset)) * t + at<float>(here, offset);
    }
    at<float>(state, 0x10) = VehPath_GetFlagBlend(state);
    return reached;
}

// The original sign-extends BX in EBX itself (MOVSX EBX,BX) and leaves it so.
PLATFORM_ASM_ENTRY(VehNode_FillUnsetAngles_entry,
                   "movswl %bx, %ebx\n"
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(VehNode_FillUnsetAngles) "addl $8, %esp\n"
                   "ret")

namespace {

// The unset-angles sentinel (0x008db094) compared component by component (UCOMISS: NaN counts as set).
bool angles_set(const float* a) {
    return !(a[0] == title_global<float>(0x008db094) && a[1] == title_global<float>(0x008db098) &&
             a[2] == title_global<float>(0x008db09c));
}

}  // namespace

void XB_CDECL VehNode_FillUnsetAngles(uint32_t ebx, float* out) {
    const int32_t index = static_cast<int16_t>(ebx);
    const uint8_t* const node = veh_node(index);
    const float* const own = reinterpret_cast<const float*>(node + 0x2c);
    if (angles_set(own)) {
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(out, k * 4) = at<uint32_t>(node, 0x2c + k * 4);
        return;
    }
    const float* const unset = pointer<const float>(0x008db094);
    float before[3] = {unset[0], unset[1], unset[2]}, after[3] = {unset[0], unset[1], unset[2]};
    float back = 0.0f, ahead = 0.0f;
    const int32_t count = title_global<int32_t>(0x0094d4d8);  // g_numVehNodes
    const int32_t prev = veh_prev(node);
    if (prev >= 0) {
        const uint8_t* p = veh_node(prev);
        for (int16_t i = 0; i < count;) {
            back = at<float>(p, 0x38) + back;
            ++i;
            if (angles_set(reinterpret_cast<const float*>(p + 0x2c))) {
                for (uint32_t k = 0; k < 3; ++k) before[k] = at<float>(p, 0x2c + k * 4);
                break;
            }
            const int32_t k = veh_prev(p);
            if (k < 0 || k == index) break;
            p = veh_node(k);
        }
    }
    const uint8_t* n = node;
    for (int16_t i = 0; i < count;) {
        ++i;
        if (angles_set(reinterpret_cast<const float*>(n + 0x2c))) {
            for (uint32_t k = 0; k < 3; ++k) after[k] = at<float>(n, 0x2c + k * 4);
            break;
        }
        const int32_t k = veh_next(n);
        if (k < 0 || k == index) break;
        ahead = at<float>(n, 0x38) + ahead;
        n = veh_node(k);
    }
    const bool have_before = angles_set(before), have_after = angles_set(after);
    if (!have_before && !have_after) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    if (!have_before) {
        for (uint32_t k = 0; k < 3; ++k) out[k] = after[k];
        return;
    }
    if (!have_after) {
        for (uint32_t k = 0; k < 3; ++k) out[k] = before[k];
        return;
    }
    const float total = ahead + back;
    if (0.0f >= total) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const float frac = back / total;
    for (uint32_t k = 0; k < 3; ++k) {
        float x = after[k];
        if (x - before[k] > title_global<float>(0x0083eb60)) x = x - title_global<float>(0x0083ebcc);
        if (title_global<float>(0x0083ebd0) > x - before[k]) x = x + title_global<float>(0x0083ebcc);
        out[k] = (x - before[k]) * frac + before[k];
    }
}
namespace {

// An angle through the 16-bit short angle and back (CVTTSS2SI), then into -180..180.
float short_angle_round_trip(float x) {
    const int32_t s = static_cast<int32_t>(x * title_global<float>(0x0083ebd8)) & 0xffff;
    float r = static_cast<float>(s) * title_global<float>(0x0083ebd4);
    if (r > title_global<float>(0x0083eb60)) r = r - title_global<float>(0x0083ebcc);
    return r;
}

bool name_set(const uint8_t* ref) {
    const uint32_t rep = at<uint32_t>(ref, 0);
    return rep && *pointer<const char>(rep + 6);
}

// A pending path node (+0x38 / +0x78 of the state) written back over the vehicle node of the same targetname.
void store_pending_node(uint8_t* state, uint32_t pending) {
    if (!name_set(state + 0x38)) return;
    const uint32_t found = VehNode_FindByTargetname(reinterpret_cast<const uint32_t*>(state + 0x38), nullptr);
    if (static_cast<int16_t>(found) < 0) return;
    VehNode_CopyToPathNode(veh_node(static_cast<int16_t>(found)), state + pending);
}

}  // namespace

uint32_t XB_CDECL Vehicle_PathStep(const uint8_t* ent, uint8_t* state, uint8_t keep_values, int32_t elapsed, int32_t stop) {
    if (at<uint16_t>(state, 2) && !name_set(state + 0x38)) return 0;
    uint32_t reached = 0;
    store_pending_node(state, 0x38);
    VehPath_GetLookaheadPos(state, reinterpret_cast<float*>(state + 0x2c));
    float dir[3];
    dir[0] = at<float>(state, 0x2c) - at<float>(state, 0x14);
    dir[1] = at<float>(state, 0x30) - at<float>(state, 0x18);
    dir[2] = at<float>(state, 0x34) - at<float>(state, 0x1c);
    const float dist = title_ci_sqrt((dir[2] * dir[2] + dir[1] * dir[1]) + dir[0] * dir[0]);
    if (!(dist == title_global<float>(0x0083eafc))) {  // FUCOMIP: NaN normalizes too
        const float inv = title_global<float>(0x0083eb20) / dist;
        dir[0] = inv * dir[0];
        dir[1] = inv * dir[1];
        dir[2] = inv * dir[2];
    }
    if (dist > title_global<float>(0x0083eafc)) {
        float* const angles = reinterpret_cast<float*>(state + 0x20);
        vectoangles_call(dir, angles);
        angles[0] = short_angle_round_trip(angles[0]);
        angles[1] = short_angle_round_trip(angles[1]);
        angles[2] = short_angle_round_trip(angles[2]);
        float step = (static_cast<float>(elapsed) * title_global<float>(0x0083eb4c)) * at<float>(state, 8);
        if (step > dist) {
            const int32_t k = veh_next(veh_node(*reinterpret_cast<const int16_t*>(state)));
            if (k >= 0 && veh_next(veh_node(k)) >= 0) step = dist;
        }
        at<float>(state, 0x14) = step * dir[0] + at<float>(state, 0x14);
        at<float>(state, 0x18) = step * dir[1] + at<float>(state, 0x18);
        at<float>(state, 0x1c) = step * dir[2] + at<float>(state, 0x1c);
        reached = VehPath_UpdateSegment(state, dir, ent, keep_values, stop);
        VehPath_GetNodeAngles(0, 0, angles, state);
    } else {
        at<uint16_t>(state, 2) = 1;
    }
    store_pending_node(state, 0x78);
    return reached;
}

uint32_t XB_CDECL VehNode_LinkAll() {
    using Equals = uint8_t XB_CDECL(const uint8_t*, const uint8_t*);
    // Links: next = the node named by +4 (target), prev = the node whose target is our +0 name.
    for (int16_t i = 0; i < title_global<int32_t>(0x0094d4d8);) {
        uint8_t* const node = veh_node(i);
        if (name_set(node + 4)) {
            const uint32_t k = VehNode_FindByTargetname(reinterpret_cast<const uint32_t*>(node + 4), nullptr);
            const uint32_t bits = at<uint32_t>(node, 0x3c);
            at<uint32_t>(node, 0x3c) = bits ^ ((k ^ bits) & 0x3fffu);
        }
        for (int16_t j = 0; j < title_global<int32_t>(0x0094d4d8); ++j) {
            if (i == j) continue;
            if (own_function<Equals>(&ScriptString_EqualsString)(node, veh_node(j) + 4)) {  // ScriptString_EqualsString
                const uint32_t bits = at<uint32_t>(node, 0x3c);
                at<uint32_t>(node, 0x3c) = bits ^ (((static_cast<uint32_t>(static_cast<int32_t>(j)) << 14) ^ bits) & 0x0fffc000u);
                break;
            }
        }
        if (veh_next(node) == i) at<uint32_t>(node, 0x3c) |= 0x3fffu;
        if (veh_prev(node) == i) at<uint32_t>(node, 0x3c) |= 0x0fffc000u;
        ++i;
    }
    // Segment direction (+0x20), length (+0x38) and, for nodes without angles, the angles of the direction.
    for (int16_t i = 0; i < title_global<int32_t>(0x0094d4d8); ++i) {
        uint8_t* const node = veh_node(i);
        const int32_t k = veh_next(node);
        if (k < 0) continue;
        float* const dir = reinterpret_cast<float*>(node + 0x20);
        dir[0] = at<float>(veh_node(k), 0x14) - at<float>(node, 0x14);
        dir[1] = at<float>(veh_node(k), 0x18) - at<float>(node, 0x18);
        dir[2] = at<float>(veh_node(k), 0x1c) - at<float>(node, 0x1c);
        const float length = title_ci_sqrt((dir[0] * dir[0] + dir[1] * dir[1]) + dir[2] * dir[2]);
        if (!(length == title_global<float>(0x0083eafc))) {  // FUCOMIP: NaN normalizes too
            const float inv = title_global<float>(0x0083eb20) / length;
            dir[0] = dir[0] * inv;
            dir[1] = dir[1] * inv;
            dir[2] = dir[2] * inv;
        }
        at<float>(node, 0x38) = length;
        if (!(at<uint32_t>(node, 0x3c) & 0x30000000)) vectoangles_call(dir, reinterpret_cast<float*>(node + 0x2c));
    }
    // Unset speeds/values filled from the neighbours; angles filled and rounded; dead ends.
    using Break = void XB_CDECL(int32_t, const char*, double, double, double);
    int16_t i = 0;
    for (; i < title_global<int32_t>(0x0094d4d8); ++i) {
        uint8_t* const node = veh_node(i);
        const float speed = VehNode_FillUnsetValue08(i);
        at<float>(node, 8) = speed;
        at<float>(node, 0xc) = VehNode_FillUnsetValue0c(i);
        if (0.0f > speed)
            title_function<Break>(0x002938f0)(1, pointer<const char>(0x00839c58), at<float>(node, 0x14), at<float>(node, 0x18),
                                              at<float>(node, 0x1c));  // __break
        if (at<uint32_t>(node, 0x3c) & 0x30000000) VehNode_FillUnsetAngles(static_cast<uint16_t>(i), reinterpret_cast<float*>(node + 0x2c));
        for (uint32_t k = 0; k < 3; ++k) at<float>(node, 0x2c + k * 4) = short_angle_round_trip(at<float>(node, 0x2c + k * 4));
        if (0.0f >= at<float>(node, 8) || 0.0f >= at<float>(node, 0xc)) at<uint32_t>(node, 0x3c) |= 0x3fffu;
        if (veh_next(node) < 0) {
            if (0.0f >= at<float>(node, 8)) at<float>(node, 8) = title_global<float>(0x0083eb20);
            if (0.0f >= at<float>(node, 0xc)) at<float>(node, 0xc) = title_global<float>(0x0083eb20);
        }
    }
    return static_cast<uint32_t>(static_cast<int32_t>(i));
}

PLATFORM_ASM_ENTRY(Player_FindMountTrigger_entry,
                   "pushl %ebx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Player_FindMountTrigger) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Player_FindMountTrigger(const float* point, int32_t* out) {
    alignas(16) float at_point[4] = {point[0], point[1], point[2], point[0]};
    const uint32_t contents = CM_PointContents(at_point, 0, nullptr);
    if (contents & 0x400000) {
        if (out) *out = 1;
        return 1;
    }
    if (contents & 0x1000000) return 1;
    alignas(16) uint32_t list[0x540];
    using VectorCtor = void XB_STDCALL(void*, uint32_t, uint32_t, uint32_t);
    title_vector_construct(list, 4, 0x540, 0x0001d0e0);  // eh_vector_constructor_iterator
    const float pad = title_global<float>(0x0083ebf8);  // 0.1
    alignas(16) float mins[4], maxs[4];
    mins[0] = point[0] - pad;
    mins[1] = point[1] - pad;
    mins[2] = point[2] - pad;
    maxs[0] = point[0] + pad;
    maxs[1] = point[1] + pad;
    maxs[2] = point[2] + pad;
    const int32_t count = SV_AreaEntities(maxs, 0, mins, reinterpret_cast<int32_t*>(list), 0x540, 0x415c0008);
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* const ent = entity_from_handle(list[i]);
        if (at<uint32_t>(ent, 0x240)) continue;
        if (at<uint32_t>(ent, 0x260) == title_global<uint32_t>(0x0097a820)) {  // scr_const.trigger_mount
            if (out) *out = at<int8_t>(ent, 0x2a0) & 1;
            return 1;
        }
        const uint32_t flags = at<uint32_t>(ent, 0x134);
        if (flags & 0x1000000) return 1;
        if (flags & 0x400000) {
            if (out) *out = 1;
            return 1;
        }
    }
    return 0;
}

PLATFORM_ASM_ENTRY(Vehicle_MoveAlongPath_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_MoveAlongPath) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Vehicle_MoveAlongPath(int32_t elapsed, uint8_t* ent) {
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    alignas(16) uint8_t saved[0xb8];  // a copy of the path state, released at the end
    Vehicle_CopyPathState(vehicle, saved);
    const float old_speed = at<float>(ent, 0x2fc);
    uint32_t reached = 0;
    if (at<int16_t>(vehicle, 0) >= 0) {
        float& speed = at<float>(ent, 0x2fc);
        // Speed toward the target speed (+0x1b4, or the path's speed in mode 2) at the acceleration (+0x1b8).
        if (at<uint32_t>(vehicle, 0x1b0)) {
            const float target = at<uint32_t>(vehicle, 0x1b0) == 2 ? at<float>(vehicle, 8) : at<float>(vehicle, 0x1b4);
            if (target > speed) {
                const float s = (static_cast<float>(elapsed) * title_global<float>(0x0083eb4c)) * at<float>(vehicle, 0x1b8) + speed;
                speed = s;
                if (s > target) speed = target;
            } else {
                const float s = speed - (static_cast<float>(elapsed) * title_global<float>(0x0083eb4c)) * at<float>(vehicle, 0x1b8);
                speed = s;
                if (target > s) speed = target;
            }
            if (at<uint32_t>(vehicle, 0x1b0) == 2 && speed == target) {
                at<uint32_t>(vehicle, 0x1b0) = 0;
            } else {
                if (target > title_global<float>(0x0083eafc)) at<float>(vehicle, 0x1bc) = speed / target + at<float>(vehicle, 0x1bc);
                at<uint32_t>(vehicle, 8) = at<uint32_t>(ent, 0x2fc);
            }
        }
        if (!at<uint32_t>(vehicle, 0x1b0)) {
            at<uint32_t>(ent, 0x2fc) = at<uint32_t>(vehicle, 8);
            if (at<float>(vehicle, 8) > title_global<float>(0x0083eafc))
                at<float>(vehicle, 0x1bc) = speed / at<float>(vehicle, 8) + at<float>(vehicle, 0x1bc);
        }
        // Path steps of 50 ms, then the rest.
        const uint8_t keep = at<uint32_t>(vehicle, 0x1b0) != 0;
        int32_t rest = elapsed;
        if (elapsed > 50) {
            uint32_t steps = static_cast<uint32_t>(elapsed - 51) / 50 + 1;
            rest = elapsed - static_cast<int32_t>(steps * 50);
            do {
                if (Vehicle_PathStep(ent, vehicle, keep, 50, at<int16_t>(vehicle, 0x17a))) reached = 1;
            } while (--steps);
        }
        if (Vehicle_PathStep(ent, vehicle, keep, rest, at<int16_t>(vehicle, 0x17a))) reached = 1;
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(vehicle, 0xc0 + k * 4) = at<uint32_t>(vehicle, 0x14 + k * 4);
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(vehicle, 0xe0 + k * 4) = at<uint32_t>(vehicle, 0x20 + k * 4);
        if (at<uint16_t>(vehicle, 2)) speed = 0.0f;
        // Smoothed angles, then velocity and angular velocity per second.
        at<float>(vehicle, 0xe0) = AngleWrapNear(at<float>(vehicle, 0xe0), at<float>(vehicle, 0xf0), 6.0f, elapsed);
        at<float>(vehicle, 0xe4) = AngleWrapNear(at<float>(vehicle, 0xe4), at<float>(vehicle, 0xf4), 4.0f, elapsed);
        at<float>(vehicle, 0xe8) = AngleWrapNear(at<float>(vehicle, 0xe8), at<float>(vehicle, 0xf8), 6.0f, elapsed);
        for (uint32_t k = 0; k < 3; ++k) at<float>(vehicle, 0x100 + k * 4) = at<float>(vehicle, 0xc0 + k * 4) - at<float>(vehicle, 0xd0 + k * 4);
        const float inv = title_global<float>(0x0083eb20) / (static_cast<float>(elapsed) * title_global<float>(0x0083eb4c));
        for (uint32_t k = 0; k < 3; ++k) at<float>(vehicle, 0x100 + k * 4) = inv * at<float>(vehicle, 0x100 + k * 4);
        AnglesNormalize180(reinterpret_cast<const float*>(vehicle + 0xf0), reinterpret_cast<const float*>(vehicle + 0xe0),
                           reinterpret_cast<float*>(vehicle + 0x110));
        for (uint32_t k = 0; k < 3; ++k) at<float>(vehicle, 0x110 + k * 4) = inv * at<float>(vehicle, 0x110 + k * 4);
        if (reached && at<int16_t>(vehicle, 0x17a) > -1) {
            Scr_Notify(ent, title_global<uint32_t>(0x0097a76c));  // reached_wait_node
            const int32_t count = at<int32_t>(vehicle, 0x2f8);
            at<int32_t>(vehicle, 0x2f8) = count + 1;
            if (count > 10) {
                at<int16_t>(vehicle, 0x17a) = -1;
                at<int32_t>(vehicle, 0x2f8) = -1;
            }
        }
        if (at<uint16_t>(vehicle, 2)) Scr_Notify(ent, title_global<uint32_t>(0x0097a768));  // reached_end_node
        // Throttle fraction of the def's top speed (+0x194).
        float frac = at<float>(ent, 0x2fc) / at<float>(def, 0x194);
        const float one = title_global<float>(0x0083eb20);
        if (0.0f > frac) frac = 0.0f;
        else if (frac > one) frac = one;
        at<float>(vehicle, 0x240) = frac;
        at<float>(vehicle, 0x23c) = one - frac;
        const float wait = at<float>(vehicle, 0x17c);
        if (wait >= 0.0f) {
            const float now = at<float>(ent, 0x2fc);
            if ((wait >= old_speed && now >= wait) || (old_speed >= wait && wait >= now))
                Scr_Notify(ent, title_global<uint32_t>(0x0097a770));  // reached_wait_speed
        }
    }
    VehiclePathState_Dtor(saved);
}

PLATFORM_ASM_ENTRY(Vehicle_GatherCollisionCandidates_entry,
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Vehicle_GatherCollisionCandidates) "addl $32, %esp\n"
                   "ret")

namespace {

// Four packed lanes (every lane computed: MXCSR flags of the unused fourth lane match).
struct Quad {
    alignas(16) float v[4];
};
Quad quad(const float* p) { return {{p[0], p[1], p[2], p[3]}}; }
Quad quad_add(const Quad& a, const Quad& b) { return {{a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3]}}; }
Quad quad_sub(const Quad& a, const Quad& b) { return {{a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3]}}; }
Quad quad_min(const Quad& a, const Quad& b) {  // MINPS: the second operand on NaN
    Quad r;
    for (int32_t i = 0; i < 4; ++i) r.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i];
    return r;
}
Quad quad_max(const Quad& a, const Quad& b) {  // MAXPS: the second operand on NaN
    Quad r;
    for (int32_t i = 0; i < 4; ++i) r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
    return r;
}
// MAXPS(a, b) < 0 (CMPLTPS, every lane) on lanes 0-2.
bool quad_inside_xyz(const Quad& a, const Quad& b) {
    int32_t mask = 0;
    for (int32_t i = 0; i < 4; ++i) {
        const float m = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
        if (m < 0.0f && i < 3) mask |= 1 << i;
    }
    return mask == 7;
}

}  // namespace

int32_t XB_CDECL Vehicle_GatherCollisionCandidates(const float* a, const float* b, uint8_t* ent, float pad, uint32_t contents,
                                                   uint8_t* out_triangles, uint8_t* out_brushes, uint8_t* out) {
    const Quad lo_in = quad_min(quad(a), quad(b));
    const Quad hi_in = quad_max(quad(a), quad(b));
    if (ent && at<uint32_t>(ent, 0x450) && at<uint32_t>(ent, 0x454)) {
        // The entity's cached world candidates (+0x430/+0x440 box, regathered 50 units wider when the box leaves it).
        const Quad lo = quad_sub(lo_in, Quad{{pad, pad, 0.0f, pad}});
        const Quad hi = quad_add(hi_in, Quad{{pad, pad, pad * title_global<float>(0x0083eba8), pad}});
        float* const cache_lo = reinterpret_cast<float*>(ent + 0x430);
        float* const cache_hi = reinterpret_cast<float*>(ent + 0x440);
        if (!quad_inside_xyz(quad_sub(quad(cache_lo), lo), quad_sub(hi, quad(cache_hi)))) {
            if (!(title_global<uint8_t>(0x0098bf50) & 1)) {
                title_global<uint32_t>(0x0098bf50) |= 1;
                const float wide = title_global<float>(0x0083ec30);  // 50
                for (uint32_t k = 0; k < 4; ++k) title_global<float>(0x0098bf40 + k * 4) = wide;
            }
            const Quad margin = quad(pointer<const float>(0x0098bf40));
            // Lane 3 is never read; it comes from whatever follows the caller's 3-float points (the original's
            // Xbox stack, our host stack: host addresses that differ every run). Stored as 0 so that the
            // entity's state is the same in every run (record/replay, 2026-11-08).
            Quad new_lo = quad_sub(lo, margin);
            new_lo.v[3] = 0.0f;
            std::memcpy(cache_lo, new_lo.v, 16);
            Quad new_hi = quad_add(hi, quad(pointer<const float>(0x0098bf40)));
            new_hi.v[3] = 0.0f;
            std::memcpy(cache_hi, new_hi.v, 16);
            CM_GatherBoxCandidates(cache_lo, at<uint8_t*>(ent, 0x450), at<uint8_t*>(ent, 0x454), cache_hi);
        }
        CM_FilterBoxCandidates(out_brushes, 0, at<uint8_t*>(ent, 0x454), lo.v, hi.v, contents, at<uint8_t*>(ent, 0x450),
                               out_triangles);
    }
    // The candidate entities in the box grown by pad.
    const Quad p{{pad, pad, pad, pad}};
    const Quad mins = quad_sub(lo_in, p);
    std::memcpy(out + 0x10, mins.v, 16);
    const Quad maxs = quad_add(hi_in, p);
    std::memcpy(out + 0x20, maxs.v, 16);
    const int32_t count = SV_AreaEntities(reinterpret_cast<const float*>(out + 0x20), 0, reinterpret_cast<const float*>(out + 0x10),
                                          reinterpret_cast<int32_t*>(out + 0x30), 0x80, contents);
    at<int32_t>(out, 0) = count;
    return count;
}

void XB_CDECL Player_Touch(uint8_t* self, uint8_t* other) {
    if (!Player_PushAwayFromEntity(other, self)) at<int32_t>(at<uint8_t*>(self, 0x23c), 0x620) = title_global<int32_t>(0x0098540c);
    at<int32_t>(at<uint8_t*>(self, 0x23c), 0x624) = title_global<int32_t>(0x0098540c);  // level.time
    uint8_t* const actor = at<uint8_t*>(other, 0x240);
    if (!actor || at<uint32_t>(actor, 0x1390) || at<uint32_t>(actor, 0x7d8) || !(at<uint32_t>(actor, 0x310) & 0x2000000)) return;
    // An actor standing on its claimed node gives it up to the player standing there.
    if (vehicle_call(own_address(&Actor_IsAtClaimedNode), 0, 0, address_of(actor), 0, 0, 0)) {  // Actor_IsAtClaimedNode
        const uint8_t* const client = at<uint8_t*>(self, 0x23c);
        const float dy = at<float>(client, 0x5b4) - at<float>(self, 0x144);
        const float dx = at<float>(client, 0x5b0) - at<float>(self, 0x140);
        if (dy * dy + dx * dx >= title_global<float>(0x0083eb94)) return;
        vehicle_call_ebx(own_address(&Sentient_TakeNodeClaim_entry), at<uint32_t>(other, 0x244), at<uint32_t>(self, 0x244), 0);  // Sentient_TakeNodeClaim
    }
    const int32_t enemy = own_function<int32_t XB_CDECL(int32_t)>(&Sentient_EnemyTeam)(at<int32_t>(at<uint8_t*>(self, 0x244), 4));  // Sentient_EnemyTeam
    const int32_t team = at<int32_t>(at<uint8_t*>(other, 0x244), 4);
    if ((1u << (team & 31)) & ~(1u << (enemy & 31))) at<uint8_t*>(actor, 0x1390) = self;
}

void XB_CDECL Vehicle_UpdateTreadScroll(uint8_t* ent, int32_t elapsed) {
    const uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    if (!at<uint32_t>(def, 0x28)) {
        at<int32_t>(ent, 0xa0) = 0;
        return;
    }
    const float speed = at<float>(ent, 0x2fc);
    if (speed > title_global<float>(0x0083eafc)) {
        // Speed along the heading, per 176 units of tread, scaled by 0x00954fd0 (or the def's +0x40) in ms.
        float forward[3];
        AnglesToForward(reinterpret_cast<const float*>(vehicle + 0xe0), forward);
        const float vx = at<float>(vehicle, 0x100), vy = at<float>(vehicle, 0x104);
        const float n = (forward[2] * forward[2] + vy * vy) + vx * vx;
        uint32_t bits;
        std::memcpy(&bits, &n, 4);
        const uint32_t guess_bits = 0x5f3759dfu - static_cast<uint32_t>(static_cast<int32_t>(bits) >> 1);
        float y;
        std::memcpy(&y, &guess_bits, 4);
        const float r = (title_global<float>(0x0083eaf4) - (y * y) * (n * title_global<float>(0x0083eb24))) * y;
        const float rx = r * vx, ry = r * vy;
        float scroll = ((forward[1] * ry + forward[0] * rx) * speed) / title_global<float>(0x0083f044);
        scroll = (static_cast<float>(elapsed) * title_global<float>(0x0083eb4c)) * scroll;
        const float scale = title_global<float>(0x00954fd0);
        scroll = scale > title_global<float>(0x0083eafc) ? scroll * scale : scroll * at<float>(def, 0x40);
        at<int32_t>(ent, 0xa0) = at<int32_t>(ent, 0xa0) + static_cast<int32_t>(scroll * title_global<float>(0x0083eb9c));
    }
    if (!(at<float>(vehicle, 0x114) == title_global<float>(0x0083eafc)))
        at<int32_t>(ent, 0xa0) = static_cast<int32_t>(static_cast<float>(at<int32_t>(ent, 0xa0)) + at<float>(vehicle, 0x114));
}

PLATFORM_ASM_ENTRY(Player_GetViewOrigin_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Player_GetViewOrigin) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Player_GetViewOrigin(float* out, const uint8_t* ent) {
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(out, k * 4) = at<uint32_t>(ent, 0x140 + k * 4);
    out[2] = at<float>(at<uint8_t*>(ent, 0x23c), 0xac) + out[2];  // view height
    const uint8_t* const client = at<uint8_t*>(ent, 0x23c);
    float position[3] = {out[0], out[1], out[2]};
    AddLeanToPosition(position, at<float>(client, 0x4c), at<float>(client, 0xa0), 16.0f, 20.0f);
    for (uint32_t k = 0; k < 3; ++k) out[k] = position[k];
    for (uint32_t k = 0; k < 3; ++k) out[k] = at<float>(at<uint8_t*>(ent, 0x23c), 0x5c8 + k * 4) + out[k];
}

PLATFORM_ASM_ENTRY(Vehicle_SetOriginAndAngles_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_SetOriginAndAngles) "addl $12, %esp\n"
                   "ret")

namespace {

// Places a linked entity: trajectory bases and current origin/angles, both trajectories stationary.
void place_linked(uint8_t* e, const float* origin, const float* angles) {
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(e, 0x20 + k * 4) = at<uint32_t>(origin, k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(e, 0x14 + k * 4) = 0;
    for (uint32_t k = 0; k < 3; ++k) at<float>(e, 0x2c + k * 4) = 0.0f;
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(e, 0x140 + k * 4) = at<uint32_t>(origin, k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(e, 0x44 + k * 4) = at<uint32_t>(angles, k * 4);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(e, 0x38 + k * 4) = 0;
    for (uint32_t k = 0; k < 3; ++k) at<float>(e, 0x50 + k * 4) = 0.0f;
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(e, 0x150 + k * 4) = at<uint32_t>(angles, k * 4);
    at<uint32_t>(e, 0x14) = 1;
    at<uint32_t>(e, 0x38) = 1;
    SV_LinkEntity(e);
}

bool same3(const uint8_t* e, uint32_t offset, const float* v) {
    return at<float>(e, offset) == v[0] && at<float>(e, offset + 4) == v[1] && at<float>(e, offset + 8) == v[2];
}

}  // namespace

uint32_t XB_CDECL Vehicle_SetOriginAndAngles(uint8_t* ent, const float* origin, const float* angles) {
    const uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    if (!at<uint32_t>(ent, 0xd0) || !same3(ent, 0x140, origin) || !same3(ent, 0x20, origin) || !same3(ent, 0x150, angles) ||
        !same3(ent, 0x44, angles)) {
        // Interpolating trajectories from the old origin/angles to the new ones.
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ent, 0x20 + k * 4) = at<uint32_t>(ent, 0x140 + k * 4);
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ent, 0x2c + k * 4) = at<uint32_t>(origin, k * 4);
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ent, 0x140 + k * 4) = at<uint32_t>(origin, k * 4);
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ent, 0x44 + k * 4) = at<uint32_t>(ent, 0x150 + k * 4);
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ent, 0x50 + k * 4) = at<uint32_t>(angles, k * 4);
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ent, 0x150 + k * 4) = at<uint32_t>(angles, k * 4);
        at<uint32_t>(ent, 0x14) = 1;
        at<uint32_t>(ent, 0x38) = 1;
        SV_LinkEntity(ent);
        // The two attached entities (+0x204 / +0x208 handles) placed the same way.
        for (uint32_t offset = 0x204; offset <= 0x208; offset += 4) {
            const uint32_t handle = at<uint32_t>(vehicle, offset);
            if (handle) place_linked(entity_from_handle(handle), origin, angles);
        }
    }
    if (at<uint32_t>(ent, 0x2a4) & 0x20000000) G_FloatOnWater(ent);
    return 1;
}

PLATFORM_ASM_ENTRY(Vehicle_InitRecord_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_InitRecord) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Vehicle_InitRecord(uint8_t* vehicle, uint8_t* ent, uint32_t def_index) {
    PhysBody_Clear(vehicle);
    Vehicle_ResetMotionState(ent);
    at<uint32_t>(vehicle, 0x170) = at<uint32_t>(ent, 0x224);
    at<uint16_t>(vehicle, 0x178) = static_cast<uint16_t>(def_index);
    at<float>(vehicle, 0x17c) = title_global<float>(0x0083eb34);  // -1
    at<uint16_t>(vehicle, 0x17a) = 0xffff;
    static const uint32_t kZeroWords[] = {0x180, 0x188, 0x18c, 0x190, 0x194, 0x1ac, 0x1b0, 0x1c8, 0x1cc, 0x204, 0x208, 0x2f4, 0x2fc, 0x300};
    static const uint32_t kZeroFloats[] = {0x1a8, 0x1b4, 0x1b8, 0x1bc, 0x1c4, 0x23c, 0x240};
    for (uint32_t i = 0; i < sizeof(kZeroWords) / 4; ++i) at<uint32_t>(vehicle, kZeroWords[i]) = 0;
    for (uint32_t i = 0; i < sizeof(kZeroFloats) / 4; ++i) at<float>(vehicle, kZeroFloats[i]) = 0.0f;
    for (uint32_t offset = 0x1d0; offset <= 0x1fc; offset += 4) at<float>(vehicle, offset) = 0.0f;
    at<float>(vehicle, 0x1c0) = title_global<float>(0x0083ef10);  // wheel radius 15
    for (uint32_t offset = 0x20c; offset <= 0x228; offset += 4) at<uint32_t>(vehicle, offset) = 0xffffffffu;
    at<uint32_t>(vehicle, 0x234) = 0xffffffffu;
    at<uint8_t>(vehicle, 0x238) = 0;
    Vehicle_SetOriginAndAngles(ent, reinterpret_cast<const float*>(ent + 0x140), reinterpret_cast<const float*>(ent + 0x150));
    // The wheel radius from the model's tag_wheel_front_left (+0x68 of its bone record).
    const DObj* const obj = at<const DObj*>(ent, 0x22c);
    if (!obj || !at<uint32_t>(obj, 0x20)) return;
    if (!(title_global<uint8_t>(0x0098e904) & 1)) {
        title_global<uint32_t>(0x0098e904) |= 1;
        title_global<uint32_t>(0x0098e900) = Com_HashKeyNoCase(0, pointer<const char>(0x0083a444));  // tag_wheel_front_left
    }
    const int32_t bone = DObj_GetTagIndex(obj, title_global<uint32_t>(0x0098e900));
    if (bone < 0) return;
    at<uint32_t>(vehicle, 0x1c0) = at<uint32_t>(at<uint8_t*>(obj, 0x20), static_cast<uint32_t>(bone) * 64 + 0x68);
}

PLATFORM_ASM_ENTRY(Vehicle_GetTagOrigin_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Vehicle_GetTagOrigin) "addl $12, %esp\n"
                   "ret")

int32_t XB_CDECL Vehicle_GetTagOrigin(uint8_t* ent, int32_t slot, float* out) {
    const uint8_t* const def = vehicle_def(at<int16_t>(at<uint8_t*>(ent, 0x248), 0x178));
    int32_t bone = -1;
    const uint8_t* matrix = nullptr;
    if (at<const DObj*>(ent, 0x22c)) {
        bone = DObj_GetTagIndex(at<const DObj*>(ent, 0x22c), title_global<uint32_t>(0x009886f8 + static_cast<uint32_t>(slot) * 4));
        if (bone >= 0) {
            SV_DObjCreateSkeleton(ent);
            const uint8_t* const obj = at<uint8_t*>(ent, 0x22c);
            const uint32_t bones = at<uint32_t>(obj, 0x20);
            const uint32_t base = bones ? static_cast<uint32_t>(at<uint8_t>(obj, 0x60)) * 64 + bones + 0x30 : 0;
            matrix = pointer<const uint8_t>(static_cast<uint32_t>(bone) * 64 + base);
        }
    }
    if (!matrix) {
        const uint32_t rep = at<uint32_t>(ent, 0x264);
        using Break = void XB_CDECL(int32_t, const char*, const char*, uint32_t);
        title_function<Break>(0x002938f0)(1, pointer<const char>(0x0083a0ec), rep ? pointer<const char>(rep + 6) : nullptr,
                                          title_global<uint32_t>(0x008daea0 + static_cast<uint32_t>(slot) * 4));  // __break
    }
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(out, k * 4) = at<uint32_t>(matrix, 0x30 + k * 4);
    if (at<uint8_t>(ent, 0x297) == 2) {
        // Clamped to the def's radius (+0x1b0) less 2: the packed lanes all computed (MXCSR flags).
        float sq[4];
        for (uint32_t k = 0; k < 4; ++k) sq[k] = out[k] * out[k];
        const float length_sq = (sq[0] + sq[1]) + sq[2];
        const float radius = at<float>(def, 0x1b0);
        if (length_sq > radius * radius) {
            const float length = platform_x87_sqrt(length_sq);
            float v[4];
            for (uint32_t k = 0; k < 4; ++k) v[k] = out[k] / length;
            for (uint32_t k = 0; k < 4; ++k) out[k] = v[k];
            const float scale = at<float>(def, 0x1b0) - title_global<float>(0x0083eba8);
            for (uint32_t k = 0; k < 4; ++k) out[k] = v[k] * scale;
        }
    }
    return bone;
}

void XB_CDECL Player_GetViewOriginAndAxis(const uint8_t* ent, float* out) {
    const uint8_t* const client = at<uint8_t*>(ent, 0x23c);
    float angles[3];
    at<uint32_t>(angles, 8) = at<uint32_t>(client, 0xa4);
    at<uint32_t>(angles, 0) = at<uint32_t>(client, 0x5c0);
    at<uint32_t>(angles, 4) = at<uint32_t>(client, 0x5c4);
    uint32_t args[2] = {address_of(angles), address_of(out)};
    AnglesToAxis(args, out + 3, out + 6);
    Player_GetViewOrigin(angles, ent);
    for (uint32_t k = 0; k < 3; ++k) out[9 + k] = angles[k];
}

// ECX = def, [esp+4] = ent. G_DObjSetBoneRotation reads stack words it never sets, and the angles' third word
// (E-0x10) is never written: the locals sit at the original's frame addresses (gap entry) and each call runs
// at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_UpdateWheelBones_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl 4(%edx)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Vehicle_UpdateWheelBones) "addl $12, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_UpdateWheelBones(const uint8_t* def, uint8_t* ent, uint32_t entry_esp) {
    const uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const int32_t wheels = at<int16_t>(def, 0x20) != 1 ? 6 : 4;
    // sub esp,0x18: origin at E-0xc (zeros), angles at E-0x18 (third word never written); top after 4 pushes.
    float* const origin = pointer<float>(entry_esp - 0xc);
    float* const angles = pointer<float>(entry_esp - 0x18);
    const uint32_t top = entry_esp - 0x28;
    origin[2] = 0.0f;
    origin[1] = 0.0f;
    origin[0] = 0.0f;
    // Each wheel bone rolls by +0x1c4; the two steering wheels also turn by +0x270.
    for (int32_t i = 0; i < wheels; ++i) {
        const uint32_t bone = at<uint32_t>(vehicle, 0x2dc + static_cast<uint32_t>(i) * 4);
        if (static_cast<int32_t>(bone) <= 0) continue;
        angles[1] = at<float>(vehicle, 0x270);
        angles[0] = at<float>(vehicle, 0x1c4);
        if (i != title_global<int32_t>(0x0095f658) && i != title_global<int32_t>(0x008d9d58)) angles[1] = 0.0f;
        const TitleRegs regs{address_of(origin), static_cast<uint32_t>(wheels), address_of(ent), address_of(angles),
                             static_cast<uint32_t>(i), address_of(vehicle) + 0x2dc + static_cast<uint32_t>(i) * 4};
        const uint32_t args[5] = {address_of(ent), address_of(origin), address_of(angles), bone, 0};
        title_call_at_regs(top, own_address(&G_DObjSetBoneRotation_entry), &regs, 5, args);  // G_DObjSetBoneRotation
    }
}

PLATFORM_ASM_ENTRY(Vehicle_PlayerInputToMove_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_PlayerInputToMove) "addl $12, %esp\n"
                   "ret")

namespace {

// A stick axis (-128..127) past the dead zone (0x008ff004), rescaled to 0..1 with its sign (x87, as the title).
float stick_axis(int8_t value) {
    const float f = static_cast<float>(value);
    const float dead = title_global<float>(0x008ff004);
    if (platform_x87_greater(dead, abs_bits(f))) return 0.0f;
    const int32_t sign = value > 0 ? 1 : (value >= 0 ? 0 : -1);
    const long double scaled = (platform_x87_ld(abs_bits(f)) - platform_x87_ld(dead)) /
                               (platform_x87_ld(title_global<float>(0x0083ebe8)) - platform_x87_ld(dead));  // 128
    return static_cast<float>(scaled * platform_x87_ld(static_cast<float>(sign)));
}

// Through the 16-bit short angle and back, no wrap.
float short_angle(float x) {
    const int32_t s = static_cast<int32_t>(x * title_global<float>(0x0083ebd8)) & 0xffff;
    return static_cast<float>(s) * title_global<float>(0x0083ebd4);
}

}  // namespace

void XB_CDECL Vehicle_PlayerInputToMove(uint8_t* ent, const int8_t* cmd, int32_t elapsed) {
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    float view_yaw = 0.0f;  // the driver's view yaw offset (client +0xa0)
    if (const uint8_t* const driver = entity_from_handle(at<uint32_t>(ent, 0x1a0)))
        view_yaw = at<float>(at<uint8_t*>(driver, 0x23c), 0xa0);
    float stick[2];
    stick[0] = stick_axis(cmd[1]);  // right
    stick[1] = stick_axis(cmd[0]);  // forward
    const float magnitude = title_ci_sqrt(static_cast<float>(platform_x87_ld(stick[1]) * stick[1] +
                                                             platform_x87_ld(stick[0]) * stick[0]));
    const float zero = 0.0f;
    const float half = title_global<float>(0x0083eb60);  // 180
    float delta = zero;
    const float wanted = (vectoyaw(stick) + view_yaw) - title_global<float>(0x0083eb64);  // 90
    bool reverse = false;
    if (title_global<uint32_t>(0x0095bba4)) {
        delta = zero - at<float>(def, 0x4c) * stick[0];  // direct steering
    } else if (!(magnitude == zero)) {
        delta = wanted - at<float>(vehicle, 0xe4);
        if (title_global<float>(0x0083ebd0) >= delta) {
            do delta = delta + title_global<float>(0x0083ebcc);
            while (title_global<float>(0x0083ebd0) >= delta);
        } else if (delta > half) {
            do delta = delta - title_global<float>(0x0083ebcc);
            while (delta > half);
        }
    }
    // Stick pointing further back than 105 + 50 * forward degrees: reverse toward the opposite heading.
    const float limit = static_cast<float>(platform_x87_ld(stick[1]) * title_global<float>(0x0083ec30) +
                                           platform_x87_ld(title_global<float>(0x0083f04c)));
    float off = abs_bits(delta);
    if (platform_x87_greater(off, limit)) {
        const int32_t sign = delta > zero ? 1 : (zero > delta ? -1 : 0);
        delta = delta - static_cast<float>(sign) * half;
        reverse = true;
    }
    // Steering rate (+0x50 per second) toward the wanted angle, clamped to +-0x4c.
    const float seconds = static_cast<float>(elapsed) * title_global<float>(0x0083eb4c);
    const float step = at<float>(def, 0x50) * seconds;
    const float steer_old = title_global<float>(0x0095f680);
    float steer = delta - steer_old;
    if (zero - step > steer) steer = zero - step;
    else if (steer > step) steer = step;
    steer = steer + steer_old;
    title_global<float>(0x0095f680) = steer;
    if (zero - at<float>(def, 0x4c) > steer) {
        steer = zero - at<float>(def, 0x4c);
        title_global<float>(0x0095f680) = steer;
    } else if (steer > at<float>(def, 0x4c)) {
        steer = at<float>(def, 0x4c);
        title_global<float>(0x0095f680) = steer;
    }
    const float turn = seconds * steer;
    at<float>(vehicle, 0xe4) = short_angle(at<float>(vehicle, 0xe4) + turn);
    at<float>(vehicle, 0xe0) = zero;
    at<float>(vehicle, 0xe8) = zero;
    at<float>(vehicle, 0x294) = short_angle(at<float>(vehicle, 0x294) - turn);
    float cosine, sine;
    FastSinCos(&sine, 0, &cosine, at<float>(vehicle, 0xe4) * title_global<float>(0x0083edd8));  // degrees to radians
    const float vx = at<float>(vehicle, 0x100), vy = at<float>(vehicle, 0x104);
    float speed = title_ci_sqrt(vx * vx + vy * vy);
    const float along = (at<float>(vehicle, 0x104) * sine + at<float>(vehicle, 0x100) * cosine) + at<float>(vehicle, 0x108) * zero;
    if (zero > along) speed = speed * title_global<float>(0x0083eb34);  // -1
    // Throttle: forward stick (or magnitude) times the top speed (+0x44), less when turning hard at speed.
    float target;
    if (title_global<uint32_t>(0x0095bba4)) {
        target = at<float>(def, 0x44) * stick[1];
    } else {
        const float top = at<float>(def, 0x44);
        target = reverse ? zero - top : top;
        target = target * magnitude;
        if (platform_x87_ld(top) * platform_x87_ld(title_global<float>(0x0083ed28)) >
            platform_x87_ld(abs_bits(speed))) {  // 0.7, FCOMIP
            const float angle = abs_bits(delta);
            const float wide = title_global<float>(0x0083f048);  // 85
            if (platform_x87_greater(wide, angle)) {
                const float k = (wide - angle) / wide;
                target = ((k * k) * title_global<float>(0x0083ebc0) + title_global<float>(0x0083ebf8)) * target;
            } else {
                target = target * title_global<float>(0x0083ebf8);
            }
        }
    }
    const float accel = seconds * at<float>(def, 0x48);
    float change = target - speed;
    if (zero - accel > change) change = zero - accel;
    else if (change > accel) change = accel;
    const float v = change + speed;
    at<float>(vehicle, 0x100) = cosine * v;
    at<float>(vehicle, 0x104) = sine * v;
}

PLATFORM_ASM_ENTRY(Vehicle_EnterPlayer_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_EnterPlayer) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Vehicle_EnterPlayer(uint8_t* ent, uint8_t* player) {
    uint8_t* const client = at<uint8_t*>(player, 0x23c);
    uint8_t* vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    using TagMatrix = void XB_CDECL(uint8_t*, int32_t, float*);
    alignas(16) float seat[16], gun[16];
    own_function<TagMatrix>(&G_DObjGetWorldTagMatrix)(ent, at<int32_t>(vehicle, 0x2b0), seat);  // G_DObjGetWorldTagMatrix
    own_function<TagMatrix>(&G_DObjGetWorldTagMatrix)(ent, at<int32_t>(vehicle, 0x2c4), gun);
    float angles[3];
    MatrixToAngles(angles, 0, gun);
    angles[2] = 0.0f;
    TeleportPlayer(player, 0, seat + 12);
    SetClientViewAngle(angles, player);
    const TitleRegs regs{0, 0, 0x00838a54, address_of(player), 0, address_of(player)};  // ECX "tag_player", EDX player
    const uint32_t args[3] = {address_of(ent), 0x0082eec0, 0x0082eec0};
    if (!g_ent_call(own_address(&G_EntLinkToWithOffset), &regs, 3, args))  // G_EntLinkToWithOffset
        title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, pointer<const char>(0x0083a064));  // __break
    if (at<uint32_t>(vehicle, 0x1cc) == at<uint32_t>(player, 0x224)) {
        at<uint32_t>(vehicle, 0x1c8) = 0;
        at<uint32_t>(vehicle, 0x1cc) = 0;
    }
    uint32_t mode;
    if (!at<uint32_t>(def, 0x3c)) {
        at<uint8_t>(ent, 0x297) = 2;
        mode = 1;
    } else {
        at<uint32_t>(ent, 0x10) |= 0x400;
        mode = 2;
    }
    at<uint32_t>(ent, 0x10) |= 0x100000;
    at<uint32_t>(player, 0x1a0) = at<uint32_t>(ent, 0x224);
    at<uint32_t>(ent, 0x1a0) = at<uint32_t>(player, 0x224);
    at<uint32_t>(client, 0x4e4) = mode;
    at<int32_t>(client, 0x4e8) = at<int16_t>(def, 0x20);
    at<uint32_t>(client, 0xc0) = (at<uint32_t>(client, 0xc0) & 0xffbfffffu) | 0x300000u;
    if (player == at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4)) {
        title_global<uint8_t>(0x0095f91f) = 0;
        Cvar_Set2(pointer<const char>(0x0082c538), pointer<char>(0x0082e058), 1);  // cl_run "1"
    }
    at<uint32_t>(client, 0x468) = at<int16_t>(def, 0x20) == 5 ? 0 : at<uint32_t>(ent, 0x224);
    vehicle = at<uint8_t*>(ent, 0x248);
    at<uint8_t>(vehicle, 0x238) = 1;
    Scr_Notify(ent, title_global<uint32_t>(0x0097a83c));  // player_on_vehicle
}

PLATFORM_ASM_ENTRY(Vehicle_UpdateTurretBones_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Vehicle_UpdateTurretBones) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Vehicle_UpdateTurretBones(uint8_t* ent) {
    using SetBoneAngles = uint32_t XB_CDECL(uint8_t*, const float*, const float*, int32_t, uint32_t);
    const uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const DObj* const obj = at<const DObj*>(ent, 0x22c);
    if (!obj || !at<uint32_t>(obj, 0x20)) return;
    // Body pitch/roll (+0x250/+0x258, plus the rock), turret yaw (+0x264), barrel pitch (+0x260).
    alignas(16) float body[3] = {0.0f, 0.0f, 0.0f}, turret[3] = {0.0f, 0.0f, 0.0f}, barrel[3] = {0.0f, 0.0f, 0.0f};
    if (vehicle) {
        body[0] = at<float>(vehicle, 0x250);
        body[2] = at<float>(vehicle, 0x258);
        barrel[0] = at<float>(vehicle, 0x260);
        turret[1] = at<float>(vehicle, 0x264);
        Vehicle_GetBodyRock(ent, 0, body);
    }
    uint32_t guard = title_global<uint32_t>(0x0098ebc4);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098ebc4) = guard;
        title_global<uint32_t>(0x0098ebc0) = Com_HashKeyNoCase(0, pointer<const char>(0x0083a0e0));  // tag_body
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098ebc4) = guard;
        title_global<uint32_t>(0x0098ebbc) = Com_HashKeyNoCase(0, pointer<const char>(0x00838a48));  // tag_turret
    }
    if (!(guard & 4)) {
        guard |= 4;
        title_global<uint32_t>(0x0098ebc4) = guard;
        title_global<uint32_t>(0x0098ebb8) = Com_HashKeyNoCase(0, pointer<const char>(0x00838a28));  // tag_barrel
    }
    const auto set_bone = own_function<SetBoneAngles>(&G_DObjSetBoneRotation_entry);  // G_DObjSetBoneRotation
    int32_t bone = DObj_GetTagIndex(obj, title_global<uint32_t>(0x0098ebc0));
    if (bone >= 0) set_bone(ent, nullptr, body, bone, 1);
    if (at<const DObj*>(ent, 0x22c)) {
        bone = DObj_GetTagIndex(at<const DObj*>(ent, 0x22c), title_global<uint32_t>(0x0098ebbc));
        if (bone >= 0) set_bone(ent, nullptr, turret, bone, 0);
    }
    if (at<const DObj*>(ent, 0x22c)) {
        bone = DObj_GetTagIndex(at<const DObj*>(ent, 0x22c), title_global<uint32_t>(0x0098ebb8));
        if (bone >= 0) set_bone(ent, nullptr, barrel, bone, 0);
    }
}

PLATFORM_ASM_ENTRY(Vehicle_SetupEntity_entry,
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_SetupEntity) "addl $12, %esp\n"
                   "ret")

namespace {

uint8_t weapon_for_name(const char* name) {
    if (!name || !*name) return 0;
    return static_cast<uint8_t>(BG_FindWeaponIndexForHash(Com_HashKeyNoCase(0, name)));
}

// A candidate list (count at +size+4, its own pointer at +size) in the entity's memory context or the heap.
uint8_t* alloc_candidate_list(const uint8_t* ent, uint32_t size) {
    int32_t slot = at<int32_t>(ent, 0x220);
    const uint8_t* const contexts = title_global<uint8_t*>(0x0095f638);
    uint8_t* p = nullptr;
    if (slot == -1) slot = at<int32_t>(contexts, 0x34);
    if (slot != -1) {
        uint8_t* const context = slot >= 0 && slot < 0x63 ? at<uint8_t*>(contexts, 0x38 + static_cast<uint32_t>(slot) * 4) : nullptr;
        p = own_function<uint8_t* XB_THISCALL(uint8_t*, uint32_t, uint32_t)>(&MemContext_Alloc_r)(context, 0x10, size);  // MemContext_Alloc_r
    }
    if (!p) p = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
                own_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(&Heap_AllocAligned)(0x10, size)));  // Heap_AllocAligned
    if (!p) return nullptr;
    at<uint8_t*>(p, size - 0x10) = p;
    at<uint32_t>(p, size - 0xc) = 0;
    return p;
}

}  // namespace

void XB_CDECL Vehicle_SetupEntity(uint32_t def_index, uint8_t* ent, uint8_t* vehicle) {
    const uint8_t* const def = vehicle_def(static_cast<int16_t>(def_index));
    at<uint32_t>(ent, 0x344) = 4;
    at<uint32_t>(ent, 0x338) = 4;
    at<uint32_t>(ent, 0x34c) = 2;
    at<uint32_t>(ent, 0x348) = 2;
    at<uint32_t>(ent, 0x32c) = 0x17;
    at<uint32_t>(ent, 0x340) = 3;
    at<uint32_t>(ent, 0x33c) = 3;
    at<uint32_t>(ent, 0xd4) = 0x10;
    at<uint32_t>(ent, 0x134) = (at<uint8_t>(ent, 0x2a0) & 1) ? 0x202080 : 0x2080;
    at<uint8_t>(ent, 0) = 0xe;
    at<uint32_t>(ent, 0x10) = 0;
    at<uint32_t>(ent, 0x14) = 1;
    at<uint32_t>(ent, 0x38) = 1;
    at<uint32_t>(ent, 0xa0) = 0;
    at<uint32_t>(ent, 0xa4) = 0;
    at<uint8_t>(ent, 1) = 0;
    // Weapons: the def's main (+0x78) and turret (+0x98, only when named) weapon names.
    at<uint8_t>(ent, 3) = weapon_for_name(reinterpret_cast<const char*>(def + 0x78));
    at<uint32_t>(vehicle, 0x1a4) = 0;
    if (at<char>(def, 0x98)) at<uint32_t>(vehicle, 0x1a4) = weapon_for_name(reinterpret_cast<const char*>(def + 0x98));
    static const uint32_t kZeroFloats[] = {0x2a0, 0x298, 0x294, 0x290, 0x288, 0x284, 0x280, 0x270, 0x268, 0x264, 0x260, 0x258, 0x254, 0x250};
    for (uint32_t i = 0; i < sizeof(kZeroFloats) / 4; ++i) at<float>(vehicle, kZeroFloats[i]) = 0.0f;
    at<uint32_t>(vehicle, 0x2f8) = 0;
    at<uint8_t*>(ent, 0x248) = vehicle;
    at<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c) + 1;  // next think: level.time + 1
    at<uint32_t>(ent, 0x2a4) |= 0x1000;
    at<float>(ent, 0x2fc) = 0.0f;
    at<uint32_t>(ent, 0x29c) = 1;
    at<uint8_t>(ent, 0x297) = 0;
    at<uint32_t>(ent, 0x2a8) = 0x00810211;
    for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(ent, 0x430 + k * 4) = 0x7f7fffff;   // FLT_MAX
    for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(ent, 0x440 + k * 4) = at<uint32_t>(ent, 0x430 + k * 4) ^ 0x80000000u;
    if (!at<uint32_t>(ent, 0x450)) at<uint8_t*>(ent, 0x450) = alloc_candidate_list(ent, 0x1010);  // triangles
    if (!at<uint32_t>(ent, 0x454)) at<uint8_t*>(ent, 0x454) = alloc_candidate_list(ent, 0x810);   // brushes
    own_function<void XB_CDECL(uint8_t*)>(&G_DObjUpdate)(ent);  // G_DObjUpdate
    // Bounds from the model (+0x38 of the DObj: mins +0, maxs +0x10), else -FLT_MAX.
    const uint8_t* const bounds = at<uint8_t*>(at<uint8_t*>(ent, 0x22c), 0x38);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ent, 0xe0 + k * 4) = bounds ? at<uint32_t>(bounds, k * 4) : 0xff7fffffu;
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ent, 0xf0 + k * 4) = bounds ? at<uint32_t>(bounds, 0x10 + k * 4) : 0xff7fffffu;
    SV_LinkEntity(ent);
    G_RegisterWeapon(0, 0, at<uint8_t>(ent, 3), 1);
}

PLATFORM_ASM_ENTRY(Vehicle_Init_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_Init) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Vehicle_Init(uint8_t* ent, const char* name, uint32_t keep_model) {
    uint8_t* vehicle = at<uint8_t*>(ent, 0x248);
    if (!vehicle) {
        // The first free vehicle slot (+0x170 unset).
        const int32_t count = title_global<uint16_t>(0x00986000);  // g_vehicleSlotCount
        int16_t i = 0;
        while (i < count) {
            vehicle = title_global<uint8_t*>(0x0095f694) + i * 0x340;  // g_vehicles
            if (!at<uint32_t>(vehicle, 0x170)) break;
            ++i;
        }
        if (i == count) title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, pointer<const char>(0x00839fec));  // __break
    }
    std::memset(vehicle, 0, 0x340);
    at<uint32_t>(vehicle, 0x1cc) = 0;
    at<uint32_t>(vehicle, 0x204) = 0;
    at<uint32_t>(vehicle, 0x208) = 0;
    at<uint32_t>(vehicle, 0x200) = 1;
    const uint32_t def_index = Vehicle_FindDefIndex(0, name);
    Vehicle_SetupEntity(def_index, ent, vehicle);
    Vehicle_InitRecord(vehicle, ent, def_index);
    if (!keep_model) {
        const uint8_t* const collmap = Vehicle_FindCollmap(at<const char*>(at<uint8_t*>(ent, 0x258), 0x48));
        if (collmap && at<uint16_t>(collmap, 6)) {
            at<uint16_t>(ent, 6) = at<uint16_t>(collmap, 6);
            SV_SetBrushModel(ent);
            at<uint32_t>(ent, 0x134) = (at<uint8_t>(ent, 0x2a0) & 1) ? 0xa00000 : 0x800000;
        }
    }
    const uint8_t* const v = at<uint8_t*>(ent, 0x248);
    if (!v) return;
    const uint8_t* const def = vehicle_def(at<int16_t>(v, 0x178));
    if (!def || at<int16_t>(def, 0x20) != 3 || (at<uint32_t>(ent, 0x2a4) & 0x200000)) return;
    at<uint32_t>(ent, 0x2a4) |= 0x200000;
    const int32_t n = title_global<int32_t>(0x00960070);
    if (n < 0x20) {
        title_global<uint32_t>(0x00960074 + static_cast<uint32_t>(n) * 4) = at<uint32_t>(ent, 0x224);
        ++title_global<int32_t>(0x00960070);
    }
}

PLATFORM_ASM_ENTRY(Vehicle_GroundMove_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_GroundMove) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Vehicle_GroundMove(uint8_t* ent, uint32_t edi) {
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    uint8_t* const motion = vehicle + 0xc0;
    float* const vel = reinterpret_cast<float*>(vehicle + 0x100);
    const float speed = platform_x87_norm3(vel[0], vel[1], vel[2]);
    if (!(speed == title_global<float>(0x0083eafc))) {  // FUCOMIP: NaN goes on
        // Clipped against the ground plane (g_vehicleGroundNormal, overbounce 1.01 / 1/1.01), speed kept unless it turned.
        const float* const n = pointer<const float>(0x00988580);
        const float ox = vel[0], oy = vel[1], oz = vel[2];
        float d = (vel[1] * n[1] + vel[2] * n[2]) + n[0] * vel[0];
        d = 0.0f > d ? d * title_global<float>(0x0083ee34) : d * title_global<float>(0x0083ee30);
        vel[0] = vel[0] - n[0] * d;
        vel[1] = vel[1] - n[1] * d;
        vel[2] = vel[2] - n[2] * d;
        if ((vel[0] * ox + oz * vel[2]) + oy * vel[1] > 0.0f) {
            Vec3NormalizeSimd(vel);
            vel[0] = vel[0] * speed;
            vel[1] = speed * vel[1];
            vel[2] = speed * vel[2];
        }
    }
    const TitleRegs regs{0, address_of(ent), 0, 0, address_of(motion), edi};
    const uint32_t first[3] = {edi, 0, 0};
    const uint32_t hit = g_ent_call(own_address(&Vehicle_SlideMove_entry), &regs, 3, first) != 0;  // Vehicle_SlideMove
    const uint32_t second[3] = {edi, 1, hit};
    const int32_t result = static_cast<int32_t>(g_ent_call(own_address(&Vehicle_SlideMove_entry), &regs, 3, second));
    if (!result) {
        at<int32_t>(vehicle, 0x308) = title_global<int32_t>(0x0098540c);  // level.time
        for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(vehicle, 0x310 + k * 4) = at<uint32_t>(motion, k * 4);
        for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(vehicle, 0x320 + k * 4) = at<uint32_t>(motion, 0x20 + k * 4);
        return;
    }
    if (result > 1 && static_cast<int32_t>(g_ent_call(own_address(&Vehicle_SlideMove_entry), &regs, 3, first)) > 1)
        for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(motion, 0x20 + k * 4) = at<uint32_t>(motion, 0x30 + k * 4);
    at<int32_t>(vehicle, 0x304) = title_global<int32_t>(0x0098540c);
}

uint8_t* XB_CDECL Weapon_SpawnClusterGrenades(uint8_t* owner, const uint8_t* source, const float* origin) {
    // 15 grenades: random yaw (LCG 0x008d5a70), distance (+-range +0x750, _rand) and fuse (+0x754, then
    // +0x758..+0x75c more per grenade), dropped at whole-unit positions around the origin.
    int32_t fuse = at<int32_t>(at<uint8_t*>(source, 0x3c), 0x754);
    int32_t yaw[15], dist[15], extra[15];
    for (int32_t i = 0; i < 15; ++i) {
        const uint8_t* const def = at<uint8_t*>(source, 0x3c);
        uint32_t seed = title_global<uint32_t>(0x008d5a70) * 0x343fd + 0x269ec3;
        title_global<uint32_t>(0x008d5a70) = seed;
        yaw[i] = static_cast<int32_t>((seed >> 17) * 0x168) >> 15;
        const int32_t r = own_function<int32_t XB_CDECL()>(&title_rand)();  // _rand
        const float range = static_cast<float>(at<int32_t>(def, 0x750));
        dist[i] = static_cast<int32_t>((((static_cast<float>(r) / title_global<float>(0x0083ebc8)) * title_global<float>(0x0083eba8)) -
                                        title_global<float>(0x0083eb20)) * range);
        const int32_t lo = at<int32_t>(def, 0x758);
        seed = title_global<uint32_t>(0x008d5a70) * 0x343fd + 0x269ec3;
        extra[i] = ((at<int32_t>(def, 0x75c) - lo) * static_cast<int32_t>(seed >> 17) >> 15) + lo;
        title_global<uint32_t>(0x008d5a70) = seed;
    }
    const float pitch = 0.0f * title_global<float>(0x0083edd8);
    uint8_t* grenade = nullptr;
    for (int32_t i = 0; i < 15; ++i) {
        float yaw_sin, yaw_cos, pitch_sin, pitch_cos;
        FastSinCos(&yaw_sin, 0, &yaw_cos, static_cast<float>(yaw[i]) * title_global<float>(0x0083edd8));
        FastSinCos(&pitch_sin, 0, &pitch_cos, pitch);
        const float up = 0.0f - pitch_sin;
        const float fy = pitch_cos * yaw_sin;
        const float fx = pitch_cos * yaw_cos;
        const float d = static_cast<float>(dist[i]);
        alignas(16) float at_pos[3];
        at_pos[0] = static_cast<float>(static_cast<int32_t>(d * fx + origin[0]));
        at_pos[1] = static_cast<float>(static_cast<int32_t>(d * fy + origin[1]));
        at_pos[2] = static_cast<float>(static_cast<int32_t>(d * up + origin[2]));
        using Fire = uint8_t* XB_REGS3 XB_CDECL(const float*, uint32_t, uint8_t*, int32_t);
        grenade = own_function<Fire>(&fire_grenadeCooked)(at_pos, 0, owner, fuse);  // fire_grenadeCooked
        fuse += extra[i];
    }
    return grenade;
}

PLATFORM_ASM_ENTRY(Vehicle_ExitPlayer_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Vehicle_ExitPlayer) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Vehicle_ExitPlayer(uint8_t* player) {
    uint8_t* const client = at<uint8_t*>(player, 0x23c);
    uint8_t* const ent = entity_from_handle(at<uint32_t>(player, 0x1a0));
    at<uint8_t>(at<uint8_t*>(ent, 0x248), 0x238) = 0;
    const uint8_t* const def = vehicle_def(at<int16_t>(at<uint8_t*>(ent, 0x248), 0x178));
    own_function<void XB_REGS1 XB_CDECL(uint8_t*)>(&G_EntUnlink)(player);  // G_EntUnlink
    // The exit point: tag_detach (vehicle +0x2b4), else the origin raised by the def's +0x1b8.
    float exit[3];
    const int32_t tag = at<int32_t>(at<uint8_t*>(ent, 0x248), 0x2b4);
    if (tag < 0) {
        exit[0] = at<float>(ent, 0x140);
        exit[1] = at<float>(ent, 0x144);
        exit[2] = at<float>(def, 0x1b8) + at<float>(ent, 0x148);
    } else {
        alignas(16) float matrix[16];
        own_function<void XB_CDECL(uint8_t*, int32_t, float*)>(&G_DObjGetWorldTagMatrix)(ent, tag, matrix);  // G_DObjGetWorldTagMatrix
        exit[0] = matrix[12];
        exit[1] = matrix[13];
        exit[2] = matrix[14];
    }
    at<uint32_t>(ent, 0x10) &= 0xffefffffu;
    at<uint32_t>(ent, 0x1a0) = 0;
    at<uint32_t>(player, 0x1a0) = 0;
    if (player == at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4))
        Cvar_Set2(pointer<const char>(0x0082c538), pointer<char>(0x0082e058), 1);  // cl_run "1"
    at<uint32_t>(client, 0xc0) &= 0xff8fffffu;
    at<uint32_t>(client, 0x468) = 0;
    at<uint32_t>(client, 0x4e4) = 0;
    at<uint32_t>(client, 0x4e8) = 0;
    // Away from the vehicle's origin in xy; up to 15 tries 16 units further out and 8 up until a box trace
    // 32 down from the spot starts clear.
    const float zero = 0.0f;
    float dir[3];
    dir[1] = exit[1] - at<float>(ent, 0x144);
    dir[0] = exit[0] - at<float>(ent, 0x140);
    dir[2] = zero;
    const float length = title_ci_sqrt((dir[1] * dir[1] + dir[0] * dir[0]) + zero * zero);
    if (!(length == title_global<float>(0x0083eafc))) {  // FUCOMIP: NaN normalizes too
        const float inv = title_global<float>(0x0083eb20) / length;
        dir[0] = inv * dir[0];
        dir[1] = inv * dir[1];
        dir[2] = inv * title_global<float>(0x0083eafc);
    }
    float spot[3] = {exit[0], exit[1], exit[2]};
    for (int32_t tries = 0;;) {
        ++tries;
        alignas(16) float start[4] = {spot[0], spot[1], spot[2], spot[0]};
        alignas(16) float end[4] = {spot[0], spot[1], spot[2] - title_global<float>(0x0083eeac), spot[0]};  // 32
        alignas(16) uint8_t trace[0x40] = {};
        const TitleRegs regs{0, address_of(start), 0, 0, 0, 0};
        const uint32_t args[11] = {address_of(trace), address_of(player + 0xe0), address_of(player + 0xf0), address_of(end), 0,
                                   at<uint32_t>(player, 0x2a8), 1, 0, 0, 0, 0};
        g_ent_call(own_address(&SV_Trace_entry), &regs, 11, args);  // SV_Trace (start in EBX)
        if (!at<uint8_t>(trace, 0x3c) && !at<uint8_t>(trace, 0x3d)) {  // neither start nor all solid
            for (uint32_t k = 0; k < 3; ++k) exit[k] = spot[k];
            break;
        }
        const float step = title_global<float>(0x0083ebb8);  // 16
        spot[0] = dir[0] * step + spot[0];
        spot[1] = dir[1] * step + spot[1];
        spot[2] = (dir[2] * step + spot[2]) + title_global<float>(0x0083ed78);  // 8
        if (!(tries < 15)) break;
    }
    TeleportPlayer(player, 0, exit);
    Scr_Notify(ent, title_global<uint32_t>(0x0097a840));  // player_off_vehicle
}

// FXCmd_Type8AtTag copies 0x30 bytes of the transform the original fills only to +0x24: the record is built at
// the original's frame address (gap entry) and the command runs at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_PlayFxAtTag_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %edx\n"
                   "pushl 12(%edx)\n"
                   "pushl 8(%edx)\n"
                   "pushl 4(%edx)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_PlayFxAtTag) "addl $32, %esp\n"
                   "popl %esp\n"
                   "ret")

int32_t XB_CDECL Vehicle_PlayFxAtTag(uint8_t* ent, const float* origin, uint32_t scale, uint32_t tag, uint32_t entry_esp,
                                    uint32_t ebx, uint32_t esi, uint32_t edi) {
    const uint8_t* const def = vehicle_def(at<int16_t>(at<uint8_t*>(ent, 0x248), 0x178));
    const uint32_t frame = ((entry_esp - 4) & ~15u) - 0x30;  // push ebp; and esp,-16; sub esp,0x30
    uint8_t* const transform = pointer<uint8_t>(frame);
    uint32_t quad_in[4];
    std::memcpy(quad_in, origin, 16);  // MOVUPS: all four lanes read before the stores
    for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(transform, 0x10 + k * 4) = title_global<uint32_t>(0x0082e290 + k * 4);
    at<uint32_t>(transform, 0x20) = scale;
    std::memcpy(transform, quad_in, 16);
    const TitleRegs regs{address_of(ent), ebx, tag, frame, esi, edi};
    const uint32_t args[3] = {address_of(def), 0x21, frame};
    const uint32_t id = title_call_at_regs(frame, own_address(&FXCmd_Type8AtTag_entry), &regs, 3, args);  // FXCmd_Type8AtTag
    return id ? static_cast<int32_t>(id) : -1;
}

PLATFORM_ASM_ENTRY(Weapon_FireClusterAtTrace_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Weapon_FireClusterAtTrace) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Weapon_FireClusterAtTrace(uint8_t* fire, uint8_t* owner, uint32_t spread_bits) {
    float spread;
    std::memcpy(&spread, &spread_bits, 4);
    float angles[3] = {0.0f, 0.0f, 0.0f};
    alignas(16) float end[4];
    Bullet_RandomSpread(fire, end, spread);
    end[3] = end[0];
    alignas(16) float start[4] = {at<float>(fire, 0x24), at<float>(fire, 0x28), at<float>(fire, 0x2c), at<float>(fire, 0x24)};
    alignas(16) uint8_t trace[0x40];
    const uint32_t pass = at<uint32_t>(at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), 0x224);
    using Trace = void XB_CDECL(uint8_t*, const float*, const float*, uint32_t, uint32_t, uint32_t, uint32_t);
    own_function<Trace>(&G_LocationalTrace_entry)(trace, start, end, pass, 0x280e031, 0x008da6f0, 0);  // G_LocationalTrace
    const uint8_t* const def = at<uint8_t*>(fire, 0x3c);
    if (!(title_global<float>(0x0083eb20) > at<float>(trace, 0x20)) || (at<uint8_t>(trace, 0x24) & 4)) {
        Add_Ammo(owner, at<uint32_t>(def, 0), 1, 0);  // nothing hit in range (or sky): the round back
        return;
    }
    // The cluster lands around the hit point: random yaw, distance up to 0.2 of the def's range +0x750.
    uint32_t seed = title_global<uint32_t>(0x008d5a70) * 0x343fd + 0x269ec3;
    title_global<uint32_t>(0x008d5a70) = seed;
    angles[1] = static_cast<float>(static_cast<int32_t>((seed >> 17) * 0x168) >> 15);
    float forward[3];
    AnglesToForward(angles, forward);
    const int32_t r = own_function<int32_t XB_CDECL()>(&title_rand)();  // _rand
    const float range = static_cast<float>(at<int32_t>(at<uint8_t*>(fire, 0x3c), 0x750)) * title_global<float>(0x0083eca0);
    const float d = static_cast<float>(static_cast<int32_t>(
        (((static_cast<float>(r) / title_global<float>(0x0083ebc8)) * title_global<float>(0x0083eba8)) - title_global<float>(0x0083eb20)) * range));
    float point[3];
    point[0] = d * forward[0] + at<float>(trace, 0);
    point[1] = d * forward[1] + at<float>(trace, 4);
    point[2] = d * forward[2] + at<float>(trace, 8);
    Weapon_SpawnClusterGrenades(owner, fire, point);
}

// G_SightTrace reads the unset words of its vectors: the locals sit at the original's frame addresses (gap entry;
// F = aligned base - 0x130) and the trace runs at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_UpdateTurretAim_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 4(%eax)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_UpdateTurretAim) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_UpdateTurretAim(uint32_t entry_esp, uint8_t* ent, uint32_t ebx, uint32_t esi, uint32_t edi,
                                      uint32_t ebp) {
    // The turret bone (vehicle +0x2c4) turns toward the target: the target entity (handle +0x1cc, plus offset
    // +0x1dc) or point (+0x1d0); with a driver (+0x1a0) toward his view (client +0x9c/+0xa0) instead, the
    // vehicle's yaw following the view unless the client's 0x200000 flag or the def's +0x3c. The aim angles
    // relative to the vehicle's axis approach (AngleWrapNear2, def +0xc8) from +0x290/+0x294 (old ones kept
    // at +0x260), clamped to the def's pitch (-+0xc0..+0xc4) and yaw (-+0xbc..+0xb8) limits. State +0x18c:
    // 2 when a limit stops a move of 2 degrees or more, else decays 2 -> 1 -> 0. Within a degree:
    // "turret_on_target", and (3 frames of 4) with a target entity a sight trace (+0x2f4) for "turret_on_vistarget".
    *pointer<uint32_t>(entry_esp - 4) = ebp;
    const uint32_t F = ((entry_esp - 4) & ~15u) - 0x130;
    auto wd = [F](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(F + off); };
    auto fl = [F](uint32_t off) -> float& { return *pointer<float>(F + off); };
    wd(0) = edi;
    wd(4) = esi;
    wd(8) = ebx;
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    wd(0x7c) = address_of(def);
    int32_t& state = at<int32_t>(vehicle, 0x18c);
    if (!at<uint32_t>(vehicle, 0x1c8) || at<int32_t>(ent, 0x350) <= 0) {
        if (state == 2) state = 1;
        else if (state == 1) state = 0;
        return;
    }
    if (!at<uint32_t>(vehicle, 0x1cc)) {
        fl(0x60) = at<float>(vehicle, 0x1d0);
        fl(0x64) = at<float>(vehicle, 0x1d4);
        wd(0x5c) = 0;
        fl(0x68) = at<float>(vehicle, 0x1d8);
    } else {
        const uint8_t* const target = G_EntHandleToEnt(&at<uint32_t>(vehicle, 0x1cc));
        fl(0x60) = at<float>(vehicle, 0x1dc) + at<float>(target, 0x140);
        fl(0x64) = at<float>(vehicle, 0x1e0) + at<float>(target, 0x144);
        wd(0x5c) = address_of(target);
        fl(0x68) = at<float>(vehicle, 0x1e4) + at<float>(target, 0x148);
    }
    const int32_t tag = at<int32_t>(vehicle, 0x2c4);
    if (tag < 0) return;
    {
        const TitleRegs regs{static_cast<uint32_t>(tag), address_of(vehicle), address_of(def), F + 0xc8,
                             address_of(ent), 0};
        const uint32_t args[3] = {address_of(ent), static_cast<uint32_t>(tag), F + 0xc8};
        title_call_at_regs(F, own_address(&G_DObjGetWorldTagMatrix), &regs, 3, args);  // G_DObjGetWorldTagMatrix (origin F+0xf8)
    }
    fl(0x10) = at<float>(vehicle, 0xe0);
    fl(0x14) = at<float>(vehicle, 0xe4);
    fl(0x18) = at<float>(vehicle, 0xe8);
    if (at<uint32_t>(ent, 0x1a0)) {
        const uint8_t* const driver = G_EntHandleToEnt(&at<uint32_t>(ent, 0x1a0));
        wd(0x3c) = address_of(driver);
        const uint8_t* const client = at<uint8_t*>(driver, 0x23c);
        if (!(at<uint32_t>(client, 0xc0) & 0x200000) && !at<uint32_t>(def, 0x3c)) fl(0x14) = at<float>(client, 0xa0);
        fl(0x20) = at<float>(vehicle, 0x1d0) - fl(0xf8);
        fl(0x24) = at<float>(vehicle, 0x1d4) - fl(0xfc);
        fl(0x28) = at<float>(vehicle, 0x1d8) - fl(0x100);
        VectorNormalize(&fl(0x20));
        vectoangles(&fl(0x20), &fl(0x40));
        const uint8_t* const view = at<uint8_t*>(pointer<uint8_t>(wd(0x3c)), 0x23c);
        fl(0x44) = at<float>(view, 0xa0);
        fl(0x40) = at<float>(view, 0x9c);
        fl(0x48) = 0.0f;
    } else {
        fl(0x20) = fl(0x60) - fl(0xf8);
        fl(0x24) = fl(0x64) - fl(0xfc);
        fl(0x28) = fl(0x68) - fl(0x100);
        VectorNormalize(&fl(0x20));
        vectoangles(&fl(0x20), &fl(0x40));
    }
    // The aim axis times the transposed vehicle axis: the aim relative to the vehicle.
    AnglesToAxisLeft2(&fl(0x40), &fl(0x10c));
    AnglesToAxisLeft2(&fl(0x10), &fl(0x80));
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 3; ++c) fl(0xa4 + r * 12 + c * 4) = fl(0x80 + c * 12 + r * 4);
    MatrixMultiply3x3(&fl(0x10c), &fl(0xa4), &fl(0x80));
    AxisToAngles(&fl(0x40), 0, &fl(0x80));
    float* const old = &at<float>(vehicle, 0x260);
    float* const aim = &at<float>(vehicle, 0x290);
    at<uint32_t>(vehicle, 0x260) = at<uint32_t>(vehicle, 0x290);
    at<uint32_t>(vehicle, 0x264) = at<uint32_t>(vehicle, 0x294);
    at<float>(vehicle, 0x268) = 0.0f;
    fl(0x18) = fl(0x48);
    fl(0x1c) = fl(0x40);
    fl(0x14) = fl(0x44);
    fl(0x10) = fl(0x40);
    for (uint32_t k = 0; k < 16; k += 4) wd(0x20 + k) = wd(0x10 + k);
    AnglesNormalize180(old, &fl(0x20), &fl(0x10));
    fl(0x3c) = abs_bits(fl(0x10));
    fl(0x10) = fl(0x3c);
    fl(0x3c) = abs_bits(fl(0x14));
    fl(0x14) = fl(0x3c);
    aim[0] = AngleWrapNear2(fl(0x40), old[0], at<float>(def, 0xc8));
    aim[1] = AngleWrapNear2(fl(0x44), at<float>(vehicle, 0x264), at<float>(def, 0xc8));
    fl(0x20) = aim[0];
    const float zero = 0.0f;
    fl(0x24) = aim[1];
    float v = zero - at<float>(def, 0xc0);
    fl(0x28) = zero;
    if (!(v > aim[0])) v = aim[0] > at<float>(def, 0xc4) ? at<float>(def, 0xc4) : aim[0];
    aim[0] = v;
    v = zero - at<float>(def, 0xbc);
    if (!(v > aim[1])) v = aim[1] > at<float>(def, 0xb8) ? at<float>(def, 0xb8) : aim[1];
    aim[1] = v;
    AnglesNormalize180(aim, &fl(0x20), &fl(0x20));
    const float two = title_global<float>(0x0083eba8);
    const float d0 = fl(0x10), d1 = fl(0x14);
    const bool stopped = (!(two > d0) && fl(0x20) == title_global<float>(0x0083eafc)) ||
                         (!(two > d1) && fl(0x24) == title_global<float>(0x0083eafc));
    if (stopped) state = 2;
    else if (state == 2) state = 1;
    else if (state == 1) state = 0;
    const float one = title_global<float>(0x0083eb20);
    if (!(one > d0) || !(one > d1)) return;
    Scr_Notify(ent, title_global<uint32_t>(0x0097a834));  // turret_on_target
    const uint8_t* const target = pointer<uint8_t>(wd(0x5c));
    if (!target) return;
    if (!(((static_cast<int32_t>(address_of(ent)) >> 5) + title_global<int32_t>(0x00947b0c)) & 3)) return;
    // Start: the tag origin; end: the target point (fourth words: x); box (0, 0, 0, 1) twice.
    const uint32_t z0 = title_global<uint32_t>(0x0082e290), z1 = title_global<uint32_t>(0x0082e294);
    const uint32_t z2 = title_global<uint32_t>(0x0082e298), z3 = title_global<uint32_t>(0x0082e29c);
    wd(0x40) = z0;
    wd(0x20) = z0;
    fl(0x18) = fl(0x68);
    fl(0x14) = fl(0x64);
    wd(0x44) = z1;
    wd(0x24) = z1;
    fl(0x1c) = fl(0x60);
    fl(0x10) = fl(0x60);
    for (uint32_t k = 0; k < 16; k += 4) wd(0x60 + k) = wd(0x10 + k);
    fl(0x18) = fl(0x100);
    fl(0x1c) = fl(0xf8);
    fl(0x14) = fl(0xfc);
    fl(0x10) = fl(0xf8);
    wd(0x4c) = z3;
    wd(0x2c) = z3;
    wd(0x48) = z2;
    wd(0x28) = z2;
    uint32_t* const hit = &at<uint32_t>(vehicle, 0x2f4);
    const TitleRegs regs{F + 0x60, F + 0x40, F + 0x20, z2, address_of(hit), F + 0x10};
    const uint32_t args[6] = {address_of(hit), F + 0x20, at<uint32_t>(ent, 0x224), at<uint32_t>(target, 0x224), 1, 0};
    title_call_at_regs(F, own_address(&G_SightTrace_entry), &regs, 6, args);  // G_SightTrace
    if (!*hit) Scr_Notify(ent, title_global<uint32_t>(0x0097a838));  // turret_on_vistarget
}

// ECX = entity. The candidate gather reads the unset fourth words of its corners and the capsule traces run on
// the gathered lists: the frame sits at the original's addresses (gap entry; F = aligned base - 0x18c0) and both
// callees run at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_TraceGround_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_TraceGround) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_TraceGround(uint32_t entry_esp, uint8_t* ent, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // World and entity candidates in the vehicle's origin +-50 (pad 0x008dae84, its contents +0x2a8) into the
    // record 0x0098eef0 (cleared once), then a capsule trace (mins -40,-40,0, maxs 40,40,80; contents less
    // 0x800000) from 50 above to 50 below; started solid: again with -20,-20,0 / 20,20,40. The result goes to
    // 0x00988570 and its end to the origin; when it hit (fraction not 1): ground (0x009885b0) unless the vehicle
    // rises (+0x108 > 0) away from the plane (velocity . normal > 10); walkable (0x009885b4) at normal z 0.7.
    *pointer<uint32_t>(entry_esp - 4) = ebp;
    const uint32_t F = ((entry_esp - 4) & ~15u) - 0x18c0;
    auto wd = [F](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(F + off); };
    auto fl = [F](uint32_t off) -> float& { return *pointer<float>(F + off); };
    wd(0) = edi;
    wd(4) = esi;
    wd(8) = ebx;
    float* const origin = &at<float>(at<uint8_t*>(ent, 0x248), 0xc0);
    wd(0x18b0) = F + 0x8b0;
    wd(0x18b4) = 0;
    wd(0x8a0) = F + 0xa0;
    wd(0x8a4) = 0;
    if (!(title_global<uint8_t>(0x0098f120) & 1)) {
        title_global<uint32_t>(0x0098f120) |= 1;
        std::memset(pointer<void>(0x0098ef20), 0, 0x200);
        edi = 0x0098f120;
    }
    const float half = title_global<float>(0x0083ec30);  // 50
    const uint32_t contents = at<uint32_t>(ent, 0x2a8);
    wd(0x90) = 0;
    wd(0x94) = 0;
    fl(0x20) = origin[0];
    fl(0x30) = origin[0];
    fl(0x24) = origin[1];
    fl(0x34) = origin[1];
    fl(0x28) = origin[2] + half;
    fl(0x38) = origin[2] - half;
    const uint32_t pad = title_global<uint32_t>(0x008dae84);
    {
        const TitleRegs regs{F + 0x30, address_of(origin), F + 0x20, pad, address_of(ent), edi};
        const uint32_t args[6] = {address_of(ent), pad, contents, F + 0x8b0, F + 0xa0, 0x0098eef0};
        title_call_at_regs(F, own_address(&Vehicle_GatherCollisionCandidates_entry), &regs, 6, args);  // Vehicle_GatherCollisionCandidates
    }
    const auto capsule = [&](uint32_t top, float low, float side, float height) {
        const uint32_t mask = at<uint32_t>(ent, 0x2a8) & 0xff7fffffu;
        const uint32_t pass = at<uint32_t>(ent, 0x224);
        const float zero = 0.0f;
        fl(0x1c) = low;
        fl(0x18) = zero;
        fl(0x14) = low;
        fl(0x10) = low;
        for (uint32_t k = 0; k < 16; k += 4) wd(0x50 + k) = wd(0x10 + k);
        fl(0x1c) = side;
        fl(0x18) = height;
        fl(0x14) = side;
        fl(0x10) = side;
        for (uint32_t k = 0; k < 16; k += 4) wd(0x40 + k) = wd(0x10 + k);
        const TitleRegs regs{F + 0x60, address_of(origin), F + 0xa0, F + 0x8b0, address_of(ent), edi};
        const uint32_t args[9] = {F + 0x8b0, F + 0xa0, F + 0x60, F + 0x20, F + 0x50, F + 0x40, F + 0x30, pass, mask};
        title_call_at_regs(top, own_address(&SV_CapsuleTrace), &regs, 9, args);  // SV_CapsuleTrace
    };
    capsule(F - 0x18, title_global<float>(0x0083f19c), title_global<float>(0x0083ec84),
            title_global<float>(0x0083ee48));  // -40, 40, 80
    if (pointer<uint8_t>(F + 0x9c)[0] || pointer<uint8_t>(F + 0x9c)[1])
        capsule(F, title_global<float>(0x0083f198), title_global<float>(0x0083eb5c),
                title_global<float>(0x0083ec84));  // -20, 20, 40
    const float end_x = fl(0x60);
    std::memcpy(pointer<void>(0x00988570), pointer<void>(F + 0x60), 0x40);
    title_global<uint32_t>(0x009885b0) = 0;
    title_global<uint32_t>(0x009885b4) = 0;
    origin[0] = end_x;
    origin[1] = fl(0x64);
    origin[2] = fl(0x68);
    if (fl(0x80) == title_global<float>(0x0083eb20)) return;  // fraction 1: nothing hit
    const float normal_z = fl(0x78);
    if (origin[0x48 / 4] > title_global<float>(0x0083eafc)) {
        const float along = (origin[0x40 / 4] * fl(0x70) + origin[0x44 / 4] * fl(0x74)) + origin[0x48 / 4] * normal_z;
        if (along > title_global<float>(0x0083eb98)) return;  // 10
    }
    title_global<uint32_t>(0x009885b0) = 1;
    if (title_global<float>(0x0083ed28) > normal_z) return;  // 0.7
    title_global<uint32_t>(0x009885b4) = 1;
}

// ESI = player entity. G_LocationalTrace reads the unset fourth words of its points: the frame sits at the
// original's addresses (gap entry; F = aligned base - 0xd0) and the trace runs at the original's ESP.
PLATFORM_ASM_ENTRY(Player_UpdateLookAtEntity_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Player_UpdateLookAtEntity) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Player_UpdateLookAtEntity(uint32_t entry_esp, uint8_t* ent, uint32_t ebx, uint32_t edi, uint32_t ebp) {
    // A trace (contents 0x2280e001, hit-location table 0x008da704 for weapons with def +0x6d0, else 0x008da6f0)
    // 8192 units (or 0x008d20e0 when not negative) along the view: from the flash tag of the turret (+0x468) when
    // using one (+0x297), else the eye; in a vehicle (client flag 0x100000) from cgame's view origin (VM_Call
    // 0x13) along the client's view angles. Its distance goes to client +0x5fc. The hit entity: a lookat trigger
    // fires (G_Trigger; client +0x5f4); with flag 0x4000 (not through surface flag 0x10, and not a sentient of
    // an allied team) it becomes the looked-at entity (+0x5f4; none while 0x0095f91f), and within 0x0094d860
    // sets client flag 0x1000000, plus 0x2000000 for an actor with +0x7d8 or without +0x310 bit 0x2000000.
    *pointer<uint32_t>(entry_esp - 4) = ebp;
    const uint32_t F = ((entry_esp - 4) & ~15u) - 0xd0;
    auto wd = [F](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(F + off); };
    auto fl = [F](uint32_t off) -> float& { return *pointer<float>(F + off); };
    wd(0) = edi;
    wd(4) = ebx;
    fl(0x20) = title_global<float>(0x00830bd8);  // 8192
    const float range_override = title_global<float>(0x008d20e0);
    wd(0xc0) = 0;
    if (range_override >= title_global<float>(0x0083eafc)) fl(0x20) = range_override;
    const uint8_t* const looked = at<uint8_t*>(at<uint8_t*>(ent, 0x23c), 0x5f4);
    if (!looked || !at<uint32_t>(looked, 0x240)) at<int16_t>(at<uint8_t*>(ent, 0x244), 0x50) = 0;
    uint8_t* const client = at<uint8_t*>(ent, 0x23c);
    at<uint32_t>(client, 0x2c) &= 0xfcffffffu;
    at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0x5f4) = 0;
    if (at<uint8_t>(ent, 0x297)) {
        const uint32_t handle = at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0x468);
        const uint32_t index = handle & 0xfff;
        uint8_t* turret = nullptr;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            turret = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (!G_GetFlashTagMatrix(0, 0, turret, &fl(0x50))) return;
    } else {
        Player_GetViewOriginAndAxis(ent, &fl(0x50));
    }
    const uint8_t* const ps = at<uint8_t*>(ent, 0x23c);
    const uint32_t weapon = at<uint32_t>(ps, 0x80);
    const uint32_t table = weapon && at<uint32_t>(weapon_def(weapon), 0x6d0) ? 0x008da704u : 0x008da6f0u;
    const float range = fl(0x20);
    uint32_t top, pass;
    float sx, sy, sz, ex, ey, ez;
    if (at<uint32_t>(ps, 0xc0) & 0x100000) {
        {
            const TitleRegs regs{0, address_of(client), F + 0x34, title_global<uint32_t>(0x0093f7f8), address_of(ent), table};
            const uint32_t args[2] = {0x13, F + 0x34};
            title_call_at_regs(F, own_address(&VM_Call_entry), &regs, 2, args);  // VM_Call(cgvm, 0x13): the view origin
        }
        const float d2r = title_global<float>(0x0083edd8);
        FastSinCos(&fl(0x2c), 0, &fl(0x28), at<float>(client, 0xa0) * d2r);
        FastSinCos(&fl(0x24), 0, &fl(0x30), at<float>(client, 0x9c) * d2r);
        const float cp = fl(0x30);
        sx = fl(0x34);
        sy = fl(0x38);
        sz = fl(0x3c);
        ex = (cp * fl(0x28)) * range + sx;
        ey = (cp * fl(0x2c)) * range + sy;
        ez = (0.0f - fl(0x24)) * range + sz;
        pass = at<uint32_t>(ent, 0x1a0);
        top = F - 4;
    } else {
        sx = fl(0x74);
        sy = fl(0x78);
        sz = fl(0x7c);
        ex = fl(0x50) * range + sx;
        ey = fl(0x54) * range + sy;
        ez = fl(0x58) * range + sz;
        pass = at<uint32_t>(ent, 0x224);
        top = F;
    }
    fl(0x40) = ex;
    fl(0x44) = ey;
    fl(0x48) = ez;
    fl(0x4c) = ex;
    fl(0x10) = sx;
    fl(0x14) = sy;
    fl(0x18) = sz;
    fl(0x1c) = sx;
    {
        const TitleRegs regs{F + 0x40, address_of(client), F + 0x10, F + 0x90, address_of(ent), table};
        const uint32_t args[7] = {F + 0x90, F + 0x10, F + 0x40, pass, 0x2280e001, table, 0};
        title_call_at_regs(top, own_address(&G_LocationalTrace_entry), &regs, 7, args);  // G_LocationalTrace
    }
    at<float>(at<uint8_t*>(ent, 0x23c), 0x5fc) = fl(0xb0) * fl(0x20);
    const uint32_t hit = wd(0xc0);
    if (hit == at<uint32_t>(at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), 0x224) || !hit) return;
    const uint32_t index = hit & 0xfff;
    uint8_t* other = nullptr;
    if (index < 0x540 && (hit >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        other = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!(at<uint32_t>(other, 0x134) & 0x4000)) {
        if (at<uint32_t>(other, 0x260) != title_global<uint32_t>(0x0097a828)) return;  // trigger_lookat
        at<uint8_t*>(at<uint8_t*>(ent, 0x23c), 0x5f4) = other;
        G_Trigger(other, ent);
        return;
    }
    if (pointer<uint8_t>(F + 0xb4)[0] & 0x10) return;
    if (at<uint32_t>(other, 0x244)) {
        const int32_t enemy = Sentient_EnemyTeam(at<int32_t>(at<uint8_t*>(ent, 0x244), 4));
        wd(0x24) = 1;
        const uint32_t team = at<uint32_t>(at<uint8_t*>(other, 0x244), 4);
        if (!((1u << (team & 31)) & ~(1u << (static_cast<uint32_t>(enemy) & 31)))) return;
    }
    if (title_global<uint8_t>(0x0095f91f)) {
        at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0x5f4) = 0;
        return;
    }
    at<uint8_t*>(at<uint8_t*>(ent, 0x23c), 0x5f4) = other;
    if (!(title_global<float>(0x0094d860) > at<float>(at<uint8_t*>(ent, 0x23c), 0x5fc))) return;
    const uint32_t flags = at<uint32_t>(client, 0x2c) | 0x1000000;
    at<uint32_t>(client, 0x2c) = flags;
    const uint8_t* const actor = at<uint8_t*>(other, 0x240);
    if (!actor) return;
    if (!at<uint32_t>(actor, 0x7d8) && (at<uint32_t>(actor, 0x310) & 0x2000000)) return;
    at<uint32_t>(client, 0x2c) = flags | 0x2000000;
}

// The three command bytes live in the entity argument's slot (E+4), and the ground trace reads stack words its
// callees leave: every callee runs at the original's ESP (gap entry).
PLATFORM_ASM_ENTRY(Vehicle_PlayerDriveThink_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_PlayerDriveThink) "addl $12, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_PlayerDriveThink(uint32_t entry_esp, uint32_t esi, uint32_t edi) {
    // The driver's (+0x1a0) stick bytes (client +0x538..+0x53a; none with client flag 0x400000 or +0x2c bit
    // 0x4000) steer the player's vehicle (owner +0x174 is 0x0095f534's +4; client flag 0x200000 set, cleared
    // again for a positive third byte); then the ground trace and, for the player's vehicle, the ground move.
    // Without a driver the velocity (+0x100) is cleared, else its length goes to +0x2fc.
    uint8_t* const ent = *pointer<uint8_t*>(entry_esp + 4);
    const uint32_t elapsed = *pointer<uint32_t>(entry_esp + 8);
    int8_t* const cmd = pointer<int8_t>(entry_esp + 4);
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint32_t top = entry_esp - 0xc;
    const auto resolve = [](uint32_t handle) -> uint8_t* {
        const uint32_t index = handle & 0xfff;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            return title_global<uint8_t*>(0x0097a968 + index * 8);
        return nullptr;
    };
    cmd[0] = 0;
    cmd[1] = 0;
    cmd[2] = 0;
    if (at<uint32_t>(ent, 0x1a0)) {
        uint8_t* const driver = resolve(at<uint32_t>(ent, 0x1a0));
        uint8_t* const owner = resolve(at<uint32_t>(vehicle, 0x174));
        edi = address_of(owner);
        if (owner == at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4)) {
            at<uint32_t>(at<uint8_t*>(driver, 0x23c), 0xc0) |= 0x200000;
            const uint8_t* const client = at<uint8_t*>(driver, 0x23c);
            if (!(at<uint32_t>(client, 0xc0) & 0x400000) && !(at<uint32_t>(client, 0x2c) & 0x4000)) {
                cmd[0] = at<int8_t>(client, 0x538);
                cmd[1] = at<int8_t>(client, 0x539);
                cmd[2] = at<int8_t>(client, 0x53a);
                if (cmd[2] > 0) at<uint32_t>(at<uint8_t*>(driver, 0x23c), 0xc0) &= 0xffdfffffu;
            }
            const TitleRegs regs{address_of(ent), 0, 0, elapsed, address_of(vehicle), edi};
            const uint32_t args[2] = {entry_esp + 4, elapsed};
            title_call_at_regs(top, own_address(&Vehicle_PlayerInputToMove_entry), &regs, 2, args);  // Vehicle_PlayerInputToMove
        }
    }
    {
        const TitleRegs regs{0, 0, address_of(ent), 0, address_of(vehicle), edi};
        title_call_at_regs(top, own_address(&Vehicle_TraceGround_entry), &regs, 0, nullptr);  // Vehicle_TraceGround
    }
    if (resolve(at<uint32_t>(vehicle, 0x174)) == at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4)) {
        const TitleRegs regs{address_of(ent), 0, 0, 0, address_of(vehicle), elapsed};
        title_call_at_regs(top, own_address(&Vehicle_GroundMove_entry), &regs, 0, nullptr);  // Vehicle_GroundMove
    }
    if (!at<uint32_t>(ent, 0x1a0)) {
        const float zero = 0.0f;
        at<float>(vehicle, 0x108) = zero;
        at<float>(vehicle, 0x104) = zero;
        at<float>(vehicle, 0x100) = zero;
        at<float>(ent, 0x2fc) = zero;
        return;
    }
    float length;
    __asm__("flds %1\n\tfmul %%st(0), %%st\n\tflds %2\n\tfmul %%st(0), %%st\n\tfaddp\n\tflds %3\n\t"
            "fmul %%st(0), %%st\n\tfaddp\n\tfsqrt\n\tfstps %0"
            : "=m"(length)
            : "m"(at<float>(vehicle, 0x100)), "m"(at<float>(vehicle, 0x104)), "m"(at<float>(vehicle, 0x108))
            : "st", "st(1)");
    at<float>(ent, 0x2fc) = length;
}

// EAX = elapsed ms, EBX = entity. The sound commands are built on the stack: they run at the original's ESP
// (E - 0x1c; gap entry).
PLATFORM_ASM_ENTRY(Vehicle_UpdateEngineSound_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ecx\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_UpdateEngineSound) "addl $12, %esp\n"
                   "popl %esp\n"
                   "ret")

namespace {

// A level stepped toward target by elapsed * (target - level) * 4, snapped to it when within 0.005 or past it
// (the distances compared as floats on the x87).
float engine_level_step(float level, float target, float seconds) {
    const float gap = target - level;
    const float distance = abs_bits(gap);
    const float step = (seconds * gap) * title_global<float>(0x0083100c);  // 4
    if (!(distance > title_global<float>(0x0083eb88))) return target;  // 0.005
    const float moved = step + level;
    return abs_bits(step) > distance ? target : moved;
}

}  // namespace

void XB_CDECL Vehicle_UpdateEngineSound(int32_t elapsed, uint8_t* ent, uint32_t entry_esp) {
    // Idle (+0x23c) and moving (+0x240) volumes: without a driver both fade to 0; with one, idle 1 when nearly
    // stopped (+0x2fc and the velocity +0x110 under 1; Vehicle_ApproachProportional), else moving 1. While
    // sound is on (+0x200) each plays (fx 0x1b/0x1d, 0x1c/0x1e with +0x238; ids +0x20c/+0x210) with its
    // volume ("SOUND_VOLUME"), freed at 0. Turret state +0x18c: 2 starts fx 0x22 (+0x214), 1 swaps it for
    // 0x23 (+0x218). A pending one-shot (+0x330) plays fx 0x27 at volume +0x334.
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    const uint32_t top = entry_esp - 0x1c;
    at<uint8_t>(ent, 1) = 0;
    const float zero = 0.0f, one = title_global<float>(0x0083eb20);
    float moving;
    bool stepped = true;
    if (at<uint32_t>(ent, 0x1a0) && one > at<float>(ent, 0x2fc)) {
        const float vx = at<float>(vehicle, 0x110), vy = at<float>(vehicle, 0x114), vz = at<float>(vehicle, 0x118);
        if (one > title_ci_sqrt((vx * vx + vy * vy) + vz * vz)) {
            at<float>(vehicle, 0x23c) = Vehicle_ApproachProportional(one, at<float>(vehicle, 0x23c), 4.0f, elapsed);
            moving = Vehicle_ApproachProportional(zero, at<float>(vehicle, 0x240), 4.0f, elapsed);
            stepped = false;
        }
    }
    if (stepped) {
        const float target = at<uint32_t>(ent, 0x1a0) ? one : zero;
        const float seconds = static_cast<float>(elapsed) * title_global<float>(0x0083eb4c);
        at<float>(vehicle, 0x23c) = engine_level_step(at<float>(vehicle, 0x23c), zero, seconds);
        moving = engine_level_step(at<float>(vehicle, 0x240), target, seconds);
    }
    at<float>(vehicle, 0x240) = moving;
    const bool alt = at<uint8_t>(vehicle, 0x238) != 0;
    const uint32_t idle_fx = alt ? 0x1c : 0x1b;
    const uint32_t move_fx = alt ? 0x1e : 0x1d;
    *pointer<uint32_t>(entry_esp - 0xc) = move_fx;
    *pointer<uint32_t>(entry_esp - 0x10) = address_of(def);
    const auto play = [&](uint32_t fx) {
        const TitleRegs regs{address_of(ent), address_of(ent), fx, address_of(def), 0, address_of(vehicle)};
        const uint32_t args[2] = {address_of(def), fx};
        return title_call_at_regs(top, own_address(&FXCmd_Type8_entry), &regs, 2, args);  // FXCmd_Type8
    };
    const auto release = [&](uint32_t offset) {
        if (at<int32_t>(vehicle, offset) != -1) FX_FreeEffectsById(title_global<FxIdList*>(0x0095f5b4), at<uint32_t>(vehicle, offset));
        at<int32_t>(vehicle, offset) = -1;
    };
    if (at<uint32_t>(vehicle, 0x200)) {
        if (!(at<float>(vehicle, 0x23c) == zero)) {
            if (at<int32_t>(vehicle, 0x20c) == -1) at<uint32_t>(vehicle, 0x20c) = play(idle_fx);
            FXParam_Broadcast(title_global<uint8_t*>(0x0095f5b4), at<uint32_t>(vehicle, 0x20c), 0x008379d8,
                              at<uint32_t>(vehicle, 0x23c));  // SOUND_VOLUME
        } else {
            release(0x20c);
        }
        if (!(at<float>(vehicle, 0x240) == zero)) {
            if (at<int32_t>(vehicle, 0x210) == -1) at<uint32_t>(vehicle, 0x210) = play(move_fx);
            FXParam_Broadcast(title_global<uint8_t*>(0x0095f5b4), at<uint32_t>(vehicle, 0x210), 0x008379d8,
                              at<uint32_t>(vehicle, 0x240));
        } else {
            release(0x210);
        }
    } else {
        release(0x20c);
        release(0x210);
    }
    const int32_t state = at<int32_t>(vehicle, 0x18c);
    if (state == 2) {
        if (at<int32_t>(vehicle, 0x214) == -1) at<uint32_t>(vehicle, 0x214) = play(0x22);
    } else if (state == 1) {
        if (at<int32_t>(vehicle, 0x214) != -1) release(0x214);
        if (at<int32_t>(vehicle, 0x218) == -1) at<uint32_t>(vehicle, 0x218) = play(0x23);
    } else {
        if (at<int32_t>(vehicle, 0x214) != -1) release(0x214);
        at<int32_t>(vehicle, 0x218) = -1;  // dropped without a free
    }
    if (at<uint32_t>(vehicle, 0x330)) {
        const uint32_t id = play(0x27);
        FXParam_Broadcast(title_global<uint8_t*>(0x0095f5b4), id, 0x008379d8, at<uint32_t>(vehicle, 0x334));
        at<uint32_t>(vehicle, 0x330) = 0;
    }
}

// Turret_FireForUser runs at the original's ESP (E - 4; gap entry).
PLATFORM_ASM_ENTRY(Turret_FireForUserOrDriver_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edi\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Turret_FireForUserOrDriver) "addl $16, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Turret_FireForUserOrDriver(uint8_t* turret, uint8_t* user, uint32_t entry_esp, uint32_t edi) {
    // EAX = turret, ECX = user: without a user the turret's driver (+0x1a0 handle) fires it (Turret_FireForUser).
    if (!user) {
        const uint32_t handle = at<uint32_t>(turret, 0x1a0);
        const uint32_t index = handle & 0xfff;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            user = title_global<uint8_t*>(0x0097a968 + index * 8);
    }
    const TitleRegs regs{address_of(turret), 0, address_of(user), 0, address_of(user), edi};
    title_call_at_regs(entry_esp - 4, own_address(&Turret_FireForUser_entry), &regs, 0, nullptr);  // Turret_FireForUser (EAX turret, ESI user)
}

// Bullet_Trace_r runs at the original's ESP with the spread end at the original's frame address (E - 0xc).
PLATFORM_ASM_ENTRY(Bullet_Fire_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ecx\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Bullet_Fire) "addl $12, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Bullet_Fire(uint8_t* fire, uint8_t* attacker, uint32_t entry_esp) {
    // A spread end (Bullet_RandomSpread, spread = the second argument) and the bullet trace from the fire's
    // start (+0x24); the pass entity is the attacker's, else the player's. (No caller: dead.)
    const uint32_t* const arg = pointer<uint32_t>(entry_esp + 4);
    float* const end = pointer<float>(entry_esp - 0xc);
    float spread;
    std::memcpy(&spread, &arg[1], 4);
    Bullet_RandomSpread(fire, end, spread);
    const uint32_t pass = attacker ? at<uint32_t>(attacker, 0x224)
                                   : at<uint32_t>(at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), 0x224);
    const TitleRegs regs{pass, address_of(attacker), address_of(end), arg[0], 0, address_of(fire) + 0x24};
    const uint32_t args[9] = {pass, arg[0], address_of(fire) + 0x24, address_of(end), arg[2], 0, address_of(fire), pass, arg[3]};
    return title_call_at_regs(entry_esp - 0x14, own_address(&Bullet_Trace_r_entry), &regs, 9, args);  // Bullet_Trace_r
}

// EAX = fire record. Weapon_BulletTrace_r runs at the original's ESP with the spread end at the original's frame
// address (E - 0xc).
PLATFORM_ASM_ENTRY(Weapon_FireBullet_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Weapon_FireBullet) "addl $16, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Weapon_FireBullet(uint8_t* fire, uint32_t entry_esp, uint32_t esi, uint32_t edi) {
    // Arguments: shooter, spread, damage, attacker, flags. A spread end, the damage scaled by the weapon's
    // actor scale (def +0x808, when positive) for actors, then the bullet trace from the fire's start (+0x24);
    // the pass entity is the attacker's, else the player's.
    static_cast<void>(esi);
    static_cast<void>(edi);
    const uint32_t* const arg = pointer<uint32_t>(entry_esp + 4);
    uint8_t* const shooter = pointer<uint8_t>(arg[0]);
    float* const end = pointer<float>(entry_esp - 0xc);
    float spread;
    std::memcpy(&spread, &arg[1], 4);
    Bullet_RandomSpread(fire, end, spread);
    int32_t damage = static_cast<int32_t>(arg[2]);
    if (at<uint32_t>(shooter, 0x240)) {
        const uint8_t* const def = at<uint8_t*>(fire, 0x3c);
        if (at<float>(def, 0x808) > title_global<float>(0x0083eafc))
            damage = static_cast<int32_t>(static_cast<float>(damage) * at<float>(def, 0x808));
    }
    const uint8_t* const attacker = pointer<uint8_t>(arg[3]);
    const uint32_t pass = attacker ? at<uint32_t>(attacker, 0x224)
                                   : at<uint32_t>(at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), 0x224);
    const TitleRegs regs{pass, static_cast<uint32_t>(damage), arg[4], address_of(end), address_of(end), address_of(fire) + 0x24};
    const uint32_t args[9] = {pass, arg[0], address_of(fire) + 0x24, address_of(end), static_cast<uint32_t>(damage), 0,
                              address_of(fire), pass, arg[4]};
    return title_call_at_regs(entry_esp - 0x1c, own_address(&Weapon_BulletTrace_r_entry), &regs, 9, args);  // Weapon_BulletTrace_r
}

// ClientThink_real runs at the original's ESP (E - 0xc; gap entry).
PLATFORM_ASM_ENTRY(ClientThink_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %eax\n" PLATFORM_ASM_CALL(ClientThink) "addl $4, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL ClientThink(uint32_t entry_esp) {
    // Q3's ClientThink for the client's entity handle: the previous command (+0x550) keeps the current one, the
    // new one comes from 0x00927e8c +0x30 (11 words), the command time (+0x590) is the level time; then
    // ClientThink_real, and the head entity follows (a client with +0x4c) or the held entity is freed.
    const uint32_t handle = *pointer<uint32_t>(entry_esp + 4);
    const uint32_t index = handle & 0xfff;
    uint8_t* ent = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        ent = title_global<uint8_t*>(0x0097a968 + index * 8);
    uint8_t* const ps = at<uint8_t*>(ent, 0x23c);
    std::memmove(ps + 0x550, ps + 0x524, 0x2c);
    std::memmove(at<uint8_t*>(ent, 0x23c) + 0x524, title_global<uint8_t*>(0x00927e8c) + 0x30, 0x2c);
    at<int32_t>(at<uint8_t*>(ent, 0x23c), 0x590) = title_global<int32_t>(0x0098540c);
    {
        const TitleRegs regs{address_of(at<uint8_t*>(ent, 0x23c)), address_of(ent), 0, 0, 0, 0};
        const uint32_t args[1] = {address_of(ent)};
        title_call_at_regs(entry_esp - 0xc, own_address(&ClientThink_real_entry), &regs, 1, args);  // ClientThink_real
    }
    if (!(at<float>(at<uint8_t*>(ent, 0x23c), 0x4c) == title_global<float>(0x0083eafc)))
        G_UpdatePlayerHeadEnt(ent);
    else
        G_ClientFreeHeldEntity(ent);
}

// ESI = entity. Vehicle_UpdateEngineSound runs at the original's ESP (E - 8; gap entry).
PLATFORM_ASM_ENTRY(Vehicle_FreePhysics_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Vehicle_FreePhysics) "addl $8, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_FreePhysics(uint8_t* ent, uint32_t entry_esp) {
    // The vehicle's physics (+0x300: body +0x34, object +0x30) back to the pool (0x008fe0d0), health (+0x350)
    // 0, the engine sounds stopped (elapsed 0), the two linked entities (+0x204/+0x208) freed, the entity's
    // vehicle fields reset (+0x348 = 2), the four script strings (+0x38/+0x78/+0x3c/+0x7c) cleared, +0x170 and
    // the vehicle pointer (+0x248) zeroed.
    using Clear = void XB_THISCALL(uint8_t*);
    uint8_t* const phys = at<uint8_t*>(at<uint8_t*>(ent, 0x248), 0x300);
    if (phys) {
        if (at<uint32_t>(phys, 0x34)) {
            PhysWorld_DestroyBody(at<uint32_t>(phys, 0x34));
            at<uint32_t>(phys, 0x34) = 0;
        }
        if (at<uint32_t>(phys, 0x30)) {
            PhysWorld_DestroyObject1c0(at<uint32_t>(phys, 0x30));
            at<uint32_t>(phys, 0x30) = 0;
        }
        VehiclePhysPool_Free(phys, 0, pointer<uint8_t>(0x008fe0d0));
        at<uint32_t>(at<uint8_t*>(ent, 0x248), 0x300) = 0;
    }
    at<uint32_t>(ent, 0x350) = 0;
    {
        const TitleRegs regs{0, address_of(ent), 0, 0, address_of(ent), address_of(phys)};
        title_call_at_regs(entry_esp - 8, own_address(&Vehicle_UpdateEngineSound_entry), &regs, 0, nullptr);  // Vehicle_UpdateEngineSound
    }
    for (const uint32_t offset : std::array<uint32_t, 2>{0x204u, 0x208u}) {
        const uint32_t handle = at<uint32_t>(at<uint8_t*>(ent, 0x248), offset);
        if (!handle) continue;
        const uint32_t index = handle & 0xfff;
        uint8_t* linked = nullptr;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            linked = title_global<uint8_t*>(0x0097a968 + index * 8);
        const TitleRegs regs{index, 0, handle >> 12, address_of(linked), address_of(ent), address_of(phys)};
        const uint32_t args[2] = {address_of(linked), 0};
        title_call_at_regs(entry_esp - 8, own_address(&G_FreeEntity), &regs, 2, args);  // G_FreeEntity
    }
    at<uint32_t>(ent, 0x32c) = 0;
    at<uint32_t>(ent, 0x340) = 0;
    at<uint32_t>(ent, 0x344) = 0;
    at<uint32_t>(ent, 0x338) = 0;
    at<uint32_t>(ent, 0x33c) = 0;
    at<uint32_t>(ent, 0x34c) = 0;
    at<uint32_t>(ent, 0x348) = 2;
    at<uint32_t>(ent, 0x328) = 0;
    at<uint32_t>(ent, 0x29c) = 0;
    at<float>(ent, 0x2fc) = 0.0f;
    at<uint8_t>(ent, 0x297) = 0;
    at<uint32_t>(ent, 0x10) = 0;
    at<uint32_t>(ent, 0x14) = 0;
    at<uint32_t>(ent, 0x38) = 0;
    for (const uint32_t offset : std::array<uint32_t, 4>{0x38u, 0x78u, 0x3cu, 0x7cu})
        own_function<Clear>(&ScriptString_Clear)(at<uint8_t*>(ent, 0x248) + offset);  // ScriptString_Clear
    at<uint32_t>(at<uint8_t*>(ent, 0x248), 0x170) = 0;
    at<uint32_t>(ent, 0x248) = 0;
}

// The def replaces the entity in its argument slot (E+4), and the per-frame steps read stack words their
// neighbours leave: every callee runs at the original's ESP (gap entry).
PLATFORM_ASM_ENTRY(Vehicle_Think_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_Think) "addl $8, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_Think(uint32_t entry_esp, uint32_t edi) {
    // The vehicle entity's think (arguments entity, elapsed ms): parked player vehicles (+0x297 = 2 with
    // 0x0095f684 clear) only renew the think time. Else the physics state, the ground result 0x00988570
    // cleared, the driver (+0x1a0, kept as owner +0x174; out when dead), path (+0x297 = 1) or player (2)
    // motion unless def type 5; for types 1 and 2 the physics step in a valid cell (wheel bones when it has a
    // body, effects stopped otherwise); the clip-entity update for path vehicles; origin/angles (unless flag
    // 0x400000), touches, turret bones, fire, aim, turret and overheat; the recoil timer +0x1f0 drives +0x1f4
    // (450/s, 2.5 times within 0..90 above 0.6); turn rate, treads, engine sound; +0xa4 = def +0x64 * 1000.
    uint32_t* const arg = pointer<uint32_t>(entry_esp + 4);
    uint8_t* const ent = pointer<uint8_t>(arg[0]);
    const int32_t elapsed = static_cast<int32_t>(arg[1]);
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    arg[0] = address_of(def);
    if (at<uint8_t>(ent, 0x297) == 2 && !title_global<uint32_t>(0x0095f684)) {
        at<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c);
        return;
    }
    const uint32_t E = entry_esp;
    const uint32_t caller_edi = edi;
    const uint32_t ent_w = address_of(ent), def_w = address_of(def), elapsed_w = static_cast<uint32_t>(elapsed);
    const auto call = [](uint32_t top, uint32_t function, const TitleRegs& regs, uint32_t count = 0,
                         const uint32_t* args = nullptr) {
        return title_call_at_regs(top, function, &regs, count, args);
    };
    call(E - 0x10, own_address(&Vehicle_LoadPhysicsState), TitleRegs{0, ent_w, ent_w, 0, def_w, edi});  // Vehicle_LoadPhysicsState (ECX)
    std::memset(pointer<void>(0x00988570), 0, 0x50);
    edi = 0x009885c0;
    const auto resolve = [](uint32_t handle) -> uint8_t* {
        const uint32_t index = handle & 0xfff;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            return title_global<uint8_t*>(0x0097a968 + index * 8);
        return nullptr;
    };
    if (const uint32_t handle = at<uint32_t>(ent, 0x1a0)) {
        at<uint32_t>(vehicle, 0x174) = handle;
        if (at<int32_t>(resolve(handle), 0x350) <= 0) {
            edi = address_of(resolve(at<uint32_t>(ent, 0x1a0)));
            call(E - 0x10, own_address(&Vehicle_ExitPlayer_entry), TitleRegs{0, ent_w, 0, 0, def_w, edi});  // Vehicle_ExitPlayer (EDI driver)
        }
    } else if (at<int32_t>(ent, 0x350) > 0) {
        at<uint32_t>(vehicle, 0x174) = 0;
    }
    if (at<int16_t>(def, 0x20) != 5) {
        const uint8_t mode = at<uint8_t>(ent, 0x297);
        if (mode == 1) {
            const uint32_t args[1] = {ent_w};
            call(E - 0x10, own_address(&Vehicle_MoveAlongPath_entry), TitleRegs{elapsed_w, ent_w, 0, 0, elapsed_w, edi}, 1, args);  // Vehicle_MoveAlongPath
        } else if (mode == 2) {
            const uint32_t args[2] = {ent_w, elapsed_w};
            call(E - 0x10, own_address(&Vehicle_PlayerDriveThink_entry), TitleRegs{0, ent_w, 0, 0, elapsed_w, edi}, 2, args);  // Vehicle_PlayerDriveThink
        }
    }
    edi = def_w;
    const int16_t type = at<int16_t>(def, 0x20);
    if (type == 1 || type == 2) {
        const uint32_t args[1] = {ent_w};
        const bool valid = uint8_t(call(E - 0x10, own_address(&G_EntityInValidCell), TitleRegs{0, ent_w, 0, 0, elapsed_w, edi}, 1, args));  // G_EntityInValidCell
        bool stop = !valid;
        if (valid) {
            if (!at<uint32_t>(at<uint8_t*>(ent, 0x248), 0x300)) {
                const uint32_t pargs[3] = {ent_w, 1, elapsed_w};
                call(E - 0x10, own_address(&Vehicle_UpdatePhysics_entry), TitleRegs{0, ent_w, 0, 0, elapsed_w, edi}, 3, pargs);  // Vehicle_UpdatePhysics
            } else {
                call(E - 0x10, own_address(&Vehicle_UpdateWheelBones_entry), TitleRegs{0, ent_w, def_w, 0, elapsed_w, edi}, 1, args);  // Vehicle_UpdateWheelBones
                stop = true;
            }
        }
        if (stop) call(E - 0x10, own_address(&Vehicle_StopEffects), TitleRegs{ent_w, ent_w, 0, 0, elapsed_w, edi});  // Vehicle_StopEffects
    }
    if (at<int16_t>(def, 0x20) != 5 && at<uint8_t>(ent, 0x297) == 1)
        call(E - 0x10, own_address(&Vehicle_ResetOnVehicleOverlap_entry), TitleRegs{0, ent_w, 0, 0, elapsed_w, edi});  // the clip-entity update (EBX)
    if (!(at<uint32_t>(ent, 0x2a4) & 0x400000)) {
        const uint32_t args[2] = {address_of(vehicle) + 0xc0, address_of(vehicle) + 0xe0};
        call(E - 0x10, own_address(&Vehicle_SetOriginAndAngles_entry), TitleRegs{ent_w, ent_w, address_of(vehicle) + 0xe0, 0, elapsed_w, edi}, 2,
             args);  // Vehicle_SetOriginAndAngles
    }
    {
        const uint32_t args[2] = {ent_w, 0};
        call(E - 0x10, own_address(&G_TouchEntitiesInBox), TitleRegs{0, ent_w, ent_w + 0x140, 0, elapsed_w, edi}, 2, args);  // G_TouchEntitiesInBox
    }
    at<uint32_t>(vehicle, 0x1ac) = 0;
    call(E - 0x10, own_address(&Vehicle_UpdateTurretBones_entry), TitleRegs{0, ent_w, 0, 0, elapsed_w, ent_w});  // Vehicle_UpdateTurretBones (EDI)
    const uint32_t pair[2] = {ent_w, elapsed_w};
    if (at<uint8_t>(ent, 0x297) == 2 || at<uint32_t>(pointer<uint8_t>(arg[0]), 0x3c))
        call(E - 0xc, own_address(&Vehicle_FireTurret_entry), TitleRegs{0, ent_w, 0, 0, elapsed_w, caller_edi}, 2, pair);  // Vehicle_FireTurret
    {
        const uint32_t args[1] = {ent_w};
        call(E - 0xc, own_address(&Vehicle_UpdateTurretAim_entry), TitleRegs{0, ent_w, 0, 0, elapsed_w, caller_edi}, 1, args);  // Vehicle_UpdateTurretAim
    }
    call(E - 0x10, own_address(&Vehicle_UpdateTurret), TitleRegs{0, ent_w, 0, 0, elapsed_w, caller_edi}, 2, pair);  // Vehicle_UpdateTurret
    call(E - 0x18, own_address(&Vehicle_UpdateOverheat_entry), TitleRegs{0, ent_w, 0, 0, elapsed_w, caller_edi}, 2, pair);  // Vehicle_UpdateOverheat
    uint8_t* const v = at<uint8_t*>(ent, 0x248);
    const float zero = 0.0f;
    if (at<float>(v, 0x1f0) > zero) {
        const float recoil = at<float>(v, 0x1f0);
        const float spin = at<float>(v, 0x1f4);
        const float seconds = static_cast<float>(elapsed) * title_global<float>(0x0083eb4c);
        const float left = recoil - seconds;
        at<float>(v, 0x1f0) = left;
        float add;
        if (!(spin < zero) && !(title_global<float>(0x0083eb64) < spin) && spin == spin &&
            left > title_global<float>(0x0083eba4))
            add = (seconds * title_global<float>(0x0083eba0)) * title_global<float>(0x0083eaf8);
        else
            add = seconds * title_global<float>(0x0083eba0);
        at<float>(v, 0x1f4) = add + at<float>(v, 0x1f4);
    }
    {
        const uint32_t args[1] = {elapsed_w};
        call(E - 0xc, own_address(&Vehicle_UpdateTurnRateAndWheelAngle), TitleRegs{ent_w, ent_w, 0, 0, elapsed_w, caller_edi}, 1, args);  // Vehicle_UpdateTurnRateAndWheelAngle
    }
    call(E - 0x10, own_address(&Vehicle_UpdateTreadScroll), TitleRegs{0, ent_w, 0, 0, elapsed_w, caller_edi}, 2, pair);  // Vehicle_UpdateTreadScroll
    call(E - 0xc, own_address(&Vehicle_UpdateEngineSound_entry), TitleRegs{elapsed_w, ent_w, 0, 0, elapsed_w, caller_edi});  // Vehicle_UpdateEngineSound
    at<int32_t>(ent, 0xa4) = static_cast<int32_t>(at<float>(pointer<uint8_t>(arg[0]), 0x64) * title_global<float>(0x0083eb9c));
    at<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c) + 1;
}

PLATFORM_ASM_ENTRY(Vehicle_FireSpreadProjectile_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ecx\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_FireSpreadProjectile) "addl $12, %esp\n"
                   "popl %esp\n"
                   "ret")

namespace {

// tan(degrees * pi / 180) on the x87 through the title CRT's __CItan, stored as float.
float title_ci_tan_degrees(float degrees) {
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfdivs %3\n\tcall *%4\n\tfstps %0"
                     : "=m"(result)
                     : "m"(degrees), "m"(title_global<float>(0x0083ed00)), "m"(title_global<float>(0x0083eb60)),
                       "r"(own_address(&title_CItan))
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

// The shared body of Weapon_FireProjectile / Vehicle_FireSpreadProjectile (E = entry ESP; arguments spread
// degrees, fuse): a direction inside the spread cone around the fire record's forward axis (+0: forward,
// +0xc right, +0x18 up; 16 units ahead, radius tan(spread) * 16 at a random angle and fraction), normalized,
// and fire_projectile from the start (+0x24) at the original's ESP (E - 0x28). The temporaries reuse the
// spread argument's slot: the length is what stays there.
uint8_t* fire_spread_projectile(uint8_t* owner, const float* fire, uint32_t E) {
    float* const slot = pointer<float>(E + 4);
    float* const dir = pointer<float>(E - 0x18);
    float* const start = pointer<float>(E - 0xc);
    using Rand = int32_t XB_CDECL();
    *slot = title_ci_tan_degrees(*slot);
    const float radius = *slot * title_global<float>(0x0083ebb8);  // 16
    *pointer<float>(E - 0x1c) = radius;
    *slot = (static_cast<float>(own_function<Rand>(&title_rand)()) / title_global<float>(0x0083ebc8)) *
            title_global<float>(0x0083ebcc);  // 0..360
    const float fraction = static_cast<float>(own_function<Rand>(&title_rand)()) / title_global<float>(0x0083ebc8);
    *pointer<float>(E - 0x24) = fraction;
    FastSinCos(pointer<float>(E - 0x20), 0, slot, (*slot * title_global<float>(0x0083ed00)) / title_global<float>(0x0083eb60));
    const float c = (*slot * fraction) * radius;
    const float u = (*pointer<float>(E - 0x20) * fraction) * radius;
    const float ahead = title_global<float>(0x0083ebb8);
    const float f0 = fire[0] * ahead, f1 = fire[1] * ahead, f2 = fire[2] * ahead;
    const float d0 = fire[6] * u + (fire[3] * c + f0);
    const float d1 = fire[7] * u + (fire[4] * c + f1);
    const float d2 = fire[8] * u + (fire[5] * c + f2);
    dir[2] = d2;
    dir[1] = d1;
    *slot = (d2 * d2 + d1 * d1) + d0 * d0;
    dir[0] = d0;
    *slot = title_ci_sqrt(*slot);
    if (!(*slot == title_global<float>(0x0083eafc))) {
        const float inv = title_global<float>(0x0083eb20) / *slot;
        dir[0] = inv * dir[0];
        dir[1] = inv * dir[1];
        dir[2] = inv * dir[2];
    }
    start[0] = fire[9];
    start[1] = fire[10];
    start[2] = fire[11];
    const uint32_t fuse = *pointer<uint32_t>(E + 8);
    const TitleRegs regs{address_of(dir), 0, address_of(start), 0, address_of(fire), address_of(owner)};
    const uint32_t args[2] = {address_of(owner), fuse};
    return pointer<uint8_t>(title_call_at_regs(E - 0x28, own_address(&fire_projectile_entry), &regs, 2, args));  // fire_projectile
}

// The owner's client velocity (+0x10) pushed back 64 units along the forward axis.
void projectile_kickback(uint8_t* owner, const float* fire) {
    if (!at<uint8_t*>(owner, 0x23c)) return;
    const float kick = title_global<float>(0x00830c1c);  // 64
    for (uint32_t k = 0; k < 3; ++k) {
        float& v = at<float>(at<uint8_t*>(owner, 0x23c), 0x10 + k * 4);
        v = v - fire[k] * kick;
    }
}

}  // namespace

void XB_CDECL Vehicle_FireSpreadProjectile(uint8_t* owner, const float* fire, uint32_t entry_esp) {
    // EAX = owner, ESI = fire record; cdecl spread degrees, fuse: the projectile gets kind 5 (+0x14).
    uint8_t* const missile = fire_spread_projectile(owner, fire, entry_esp);
    at<uint32_t>(missile, 0x14) = 5;
    projectile_kickback(owner, fire);
}

PLATFORM_ASM_ENTRY(Weapon_FireProjectile_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ecx\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Weapon_FireProjectile) "addl $12, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Weapon_FireProjectile(uint8_t* owner, const float* fire, uint32_t entry_esp) {
    // EAX = owner, ESI = fire record; cdecl spread degrees, fuse, keep-think flag: without the flag the
    // projectile's think (+0x32c) becomes 0xc.
    uint8_t* const missile = fire_spread_projectile(owner, fire, entry_esp);
    if (!*pointer<uint8_t>(entry_esp + 0xc) && missile) at<uint32_t>(missile, 0x32c) = 0xc;
    projectile_kickback(owner, fire);
}

// The push and damage take the original's 16-byte blocks (unset fourth words copied): the frame sits at the
// original's addresses (gap entry; F = aligned base - 0x50) and both run at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_Touch_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_Touch) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_Touch(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // A moving vehicle (cdecl self, other) against an actor or player (type 1 / 0xb, not linked +0x3cc) when its
    // def crushes (+0x5c > 0): below speed 1 only one stuck in it (G_TestEntityPosition). The move since the
    // last frame (+0xc0 - +0xd0, angles +0xe0 vs +0xf0) under 0.005 does nothing; else the other is pushed
    // (G_TryPushingEntity). Unpushable (health under 99999): killed (999999, dflags 0x20, mod 0x10). Pushed and
    // ahead (direction . move at least 0.8): prone players killed, others take (dot - 0.8) * min(speed / def
    // +0x60, 1) * 5 * def +0x5c. The driver (+0x1a0) is the attacker.
    *pointer<uint32_t>(entry_esp - 4) = ebp;
    const uint32_t F = ((entry_esp - 4) & ~15u) - 0x50;
    auto wd = [F](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(F + off); };
    auto fl = [F](uint32_t off) -> float& { return *pointer<float>(F + off); };
    wd(0) = edi;
    wd(4) = esi;
    wd(8) = ebx;
    uint8_t* const self = *pointer<uint8_t*>(entry_esp + 4);
    uint8_t* const other = *pointer<uint8_t*>(entry_esp + 8);
    const uint8_t* const vehicle = at<uint8_t*>(self, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    wd(0xc) = address_of(def);
    if (other[0] != 1 && other[0] != 0xb) return;
    if (at<uint32_t>(other, 0x3cc)) return;
    if (!(title_global<float>(0x0083eafc) < at<float>(def, 0x5c)) && !(at<float>(def, 0x5c) != at<float>(def, 0x5c))) return;
    long double speed;
    __asm__("flds %1\n\tfmul %%st(0), %%st\n\tflds %2\n\tfmul %%st(0), %%st\n\tfaddp\n\tflds %3\n\t"
            "fmul %%st(0), %%st\n\tfaddp\n\tfsqrt"
            : "=t"(speed)
            : "m"(at<float>(vehicle, 0x100)), "m"(at<float>(vehicle, 0x104)), "m"(at<float>(vehicle, 0x108)));
    if (platform_x87_ld(title_global<float>(0x0083eb20)) > speed && !G_TestEntityPosition(other, &at<float>(other, 0x140)))
        return;
    fl(0x10) = at<float>(vehicle, 0xc0) - at<float>(vehicle, 0xd0);
    fl(0x14) = at<float>(vehicle, 0xc4) - at<float>(vehicle, 0xd4);
    fl(0x18) = at<float>(vehicle, 0xc8) - at<float>(vehicle, 0xd8);
    AnglesNormalize180(&at<float>(vehicle, 0xf0), &at<float>(vehicle, 0xe0), &fl(0x30));
    fl(0x28) = fl(0x18);
    fl(0x2c) = fl(0x10);
    fl(0x24) = fl(0x14);
    fl(0x20) = fl(0x10);
    for (uint32_t k = 0; k < 16; k += 4) {
        wd(0x40 + k) = wd(0x20 + k);
        wd(0x10 + k) = wd(0x20 + k);
    }
    const long double length = Vec4Normalize(&fl(0x10), &fl(0x20));
    if (platform_x87_ld(title_global<float>(0x0083eb88)) > length) return;  // 0.005
    fl(0x18) = fl(0x38);
    fl(0x1c) = fl(0x30);
    fl(0x10) = fl(0x30);
    fl(0x14) = fl(0x34);
    for (uint32_t k = 0; k < 16; k += 4) wd(0x30 + k) = wd(0x10 + k);
    uint32_t pushed;
    {
        const TitleRegs regs{F + 0x40, address_of(other), F + 0x30, 0, address_of(self), address_of(vehicle) + 0xe0};
        const uint32_t args[3] = {address_of(other), address_of(self), F + 0x30};
        pushed = title_call_at_regs(F, own_address(&G_TryPushingEntity_entry), &regs, 3, args);  // G_TryPushingEntity
    }
    uint32_t damage = 999999, dflags = 0x20;
    if (pushed) {
        fl(0x10) = at<float>(other, 0x140) - at<float>(self, 0x140);
        fl(0x14) = at<float>(other, 0x144) - at<float>(self, 0x144);
        fl(0x18) = 0.0f;
        Vec3NormalizeSimd(&fl(0x10));
        const float dot = (fl(0x28) * fl(0x18) + fl(0x24) * fl(0x14)) + fl(0x20) * fl(0x10);
        const float ahead = title_global<float>(0x0083ebc0);  // 0.8
        if (ahead > dot) return;
        const uint8_t* const client = at<uint8_t*>(other, 0x23c);
        if (!client || !(at<uint8_t>(client, 0x2c) & 1)) {
            const uint8_t* const d = pointer<uint8_t>(wd(0xc));
            float scale = at<float>(self, 0x2fc) / at<float>(d, 0x60);
            const float one = title_global<float>(0x0083eb20);
            if (scale > one) scale = one;
            const int32_t amount = static_cast<int32_t>((((dot - ahead) * scale) * title_global<float>(0x0083f234)) *
                                                        at<float>(d, 0x5c));
            if (amount <= 0) return;
            damage = static_cast<uint32_t>(amount);
            dflags = 0;
        } else if (at<int32_t>(other, 0x350) >= 99999) {
            return;
        }
    } else if (at<int32_t>(other, 0x350) >= 99999) {
        return;
    }
    const uint32_t attacker = address_of(G_EntHandleToEnt(&at<uint32_t>(self, 0x1a0)));
    const uint32_t point = address_of(other) + 0x140;
    const TitleRegs regs{point, address_of(other), 0, 0, address_of(self), point};
    const uint32_t args[9] = {address_of(other), address_of(self), attacker, F + 0x20, damage, dflags, 0x10, 0, 0xffffffffu};
    title_call_at_regs(F, own_address(&G_Damage_entry), &regs, 9, args);  // G_Damage
}

// The candidate lists (B+0x210, B+0xa20) and the trace blocks are the original's frame: their addresses reach the
// clip code's globals, so the frame sits at the original's addresses (gap entry; B = aligned base - 0x1a30) and
// every title callee runs at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_UpdatePhysics_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_UpdatePhysics) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_UpdatePhysics(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl (entity, fall, elapsed ms). The body axis from the angles (+0xe0; forward, left = -right, up, origin
    // +0xc0/+0xc4/+0xd8) is matched against the entity's slot in the wheel cache (0x0095f690, 0xe0 bytes, next
    // slot 0x0095f68c wrapping at g_vehicleSlotCount): unchanged (each term within 0.005) keeps the cached wheel
    // results, else the slot takes the axis and its triangle ids are reset. Each wheel tag (4, 6 unless type 1)
    // at the wheel height (+0x1c0) gives a segment 64 up to 256 down; their box (with margin 0x008feff8 when
    // outside the entity's cached box +0x430/+0x440) refreshes the clip candidates. Per wheel: the cached
    // triangle retested (then entries filtered and the segment traced from its crossing), else the candidates
    // filtered and traced; above 0.4 an SV_Trace (mask 0x211, 0x10211 for player drive). A hit sets the wheel's
    // surface (+0x150, bits 20-24 of the surface flags); with `fall` the wheel drops (velocity -800/s, +0x120)
    // to the ground, else snaps; the wheel bone moves by the suspension (clamped +-15) and turns (+0x1c4,
    // steering +0x270 for the two steer wheels); dust effects (+0x21c, EmissionRate = speed +0x2fc / def +0x44)
    // above speed 0.1. Then the wheel points give a new forward and left, up = forward x left and the ground
    // plane distance (the highest wheel less def +0x64), pitch and roll eased (AngleWrapNear 6) and clamped to
    // +-60, the height from the plane unless player-driven, and the angular velocity +0x110 per second.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0x1a30;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    auto by = [B](uint32_t off) -> uint8_t& { return *pointer<uint8_t>(B + off); };
    const auto call = [](uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count = 0,
                         const uint32_t* args = nullptr) {
        const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
        *pointer<uint32_t>(top - count * 4 - 4) = ret;
        return result;
    };
    const auto store4 = [&](uint32_t off, const float (&v)[4]) {
        for (uint32_t k = 0; k < 4; ++k) fl(off + k * 4) = v[k];
    };
    const auto load4 = [&](uint32_t address, float (&v)[4]) {
        for (uint32_t k = 0; k < 4; ++k) v[k] = *pointer<float>(address + k * 4);
    };
    // MINPS / MAXPS lanes: the second operand when the compare is false (NaN included).
    const auto minps = [](float (&a)[4], const float (&b)[4]) {
        for (uint32_t k = 0; k < 4; ++k) a[k] = a[k] < b[k] ? a[k] : b[k];
    };
    const auto maxps = [](float (&a)[4], const float (&b)[4]) {
        for (uint32_t k = 0; k < 4; ++k) a[k] = a[k] > b[k] ? a[k] : b[k];
    };
    const uint32_t* const arg = pointer<uint32_t>(E + 4);
    const uint32_t ent_w = arg[0];
    uint8_t* const ent = pointer<uint8_t>(ent_w);
    const uint32_t fall = arg[1];
    const int32_t elapsed = static_cast<int32_t>(arg[2]);
    const uint32_t v_w = at<uint32_t>(ent, 0x248);
    uint8_t* const v = pointer<uint8_t>(v_w);
    const uint32_t origin_w = v_w + 0xc0;
    const uint32_t def_w = title_global<uint32_t>(0x00950a28 + static_cast<int32_t>(at<int16_t>(v, 0x178)) * 4);
    uint8_t* const def = pointer<uint8_t>(def_w);
    wd(0x28) = v_w;
    wd(8) = ebx;
    const uint32_t count = (at<int16_t>(def, 0x20) != 1 ? 2u : 0u) + 4u;
    wd(4) = esi;
    wd(0) = edi;
    wd(0x150) = 0;
    wd(0x154) = 0;
    fl(0x74) = at<float>(v, 0xc0);
    wd(0x9c) = def_w;
    fl(0x78) = at<float>(v, 0xc4);
    wd(0xec) = origin_w;
    wd(0x30) = count;
    fl(0x7c) = at<float>(v, 0xd8);
    wd(0xf0) = v_w + 0xe0;
    {
        const uint32_t args[2] = {v_w + 0xe0, B + 0x50};
        call(B, 0x0026a468, own_address(&AnglesToAxis_entry), TitleRegs{v_w + 0xe0, origin_w, B + 0x50, count, B + 0x68, B + 0x18}, 2,
             args);  // AnglesToAxis (forward B+0x50, right B+0x18, up B+0x68)
    }
    const float zero = 0.0f;
    const float left[3] = {zero - fl(0x18), zero - fl(0x1c), zero - fl(0x20)};
    fl(0x5c) = left[0];
    fl(0x60) = left[1];
    fl(0x64) = left[2];
    by(0x12) = 1;
    // The wheel cache slot of this entity.
    uint32_t used = title_global<uint32_t>(0x0095f68c);
    const uint32_t slots = title_global<uint32_t>(0x0095f690);
    uint32_t slot = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(used); ++i) {
        if (*pointer<uint32_t>(slots + 0xa8 + i * 0xe0) == ent_w) {
            slot = slots + i * 0xe0;
            break;
        }
    }
    bool store = true;
    if (slot) {
        const float limit = title_global<float>(0x0083ebdc);  // 2.5e-5
        wd(0xc) = B + 0x50;
        wd(0x24) = 0;
        bool same = true;
        for (uint32_t row = 0; row < 4 && same; ++row) {
            for (uint32_t k = 0; k < 3; ++k) {
                const float d = fl(0x50 + row * 12 + k * 4) - *pointer<float>(slot + 0xac + row * 12 + k * 4);
                if (d * d > limit) {
                    same = false;
                    break;
                }
            }
            if (same) {
                wd(0x24) = row + 1;
                wd(0xc) = B + 0x50 + (row + 1) * 12;
            }
        }
        if (!same) by(0x12) = 0;
        store = !same;
    } else {
        if (used == title_global<uint16_t>(0x00986000)) used = 0;  // g_vehicleSlotCount
        slot = slots + used * 0xe0;
        title_global<uint32_t>(0x0095f68c) = used + 1;
        for (uint32_t k = 0; k < 6; ++k) *pointer<uint32_t>(slot + 0x90 + k * 4) = 0xffffffffu;
        by(0x12) = 0;
    }
    if (store) {
        *pointer<uint32_t>(slot + 0xa8) = ent_w;
        *pointer<float>(slot + 0xac) = fl(0x50);
        *pointer<float>(slot + 0xb0) = fl(0x54);
        *pointer<float>(slot + 0xb4) = fl(0x58);
        *pointer<float>(slot + 0xc4) = fl(0x68);
        *pointer<float>(slot + 0xc8) = fl(0x6c);
        *pointer<float>(slot + 0xcc) = fl(0x70);
        *pointer<float>(slot + 0xd0) = fl(0x74);
        *pointer<float>(slot + 0xd4) = fl(0x78);
        *pointer<float>(slot + 0xb8) = left[0];
        *pointer<float>(slot + 0xbc) = left[1];
        *pointer<float>(slot + 0xc0) = left[2];
        *pointer<float>(slot + 0xd8) = fl(0x7c);
    }
    // A wheel tag's point on the body axis (wheel height +0x1c0 as its z).
    const auto wheel_point = [&](float tx, float ty, float tz, float& x, float& y, float& z) {
        x = tx * fl(0x50) + tz * fl(0x68) + ty * fl(0x5c) + fl(0x74);
        y = fl(0x54) * tx + fl(0x6c) * tz + ty * fl(0x60) + fl(0x78);
        z = fl(0x58) * tx + fl(0x70) * tz + ty * fl(0x64) + fl(0x7c);
    };
    const uint32_t mask = (((at<uint8_t>(ent, 0x297) != 2 ? 1u : 0u) - 1u) & 0x10000u) + 0x211u;
    const float big = title_global<float>(0x0082eed0);  // FLT_MAX
    float mins[4] = {big, big, big, big};
    store4(0x80, mins);
    float maxs[4];
    for (uint32_t k = 0; k < 4; ++k) {
        uint32_t bits;
        std::memcpy(&bits, &mins[k], 4);
        bits ^= title_global<uint32_t>(0x0082e0b0 + k * 4);
        std::memcpy(&maxs[k], &bits, 4);
    }
    wd(0xbc) = mask;
    store4(0x100, mins);
    store4(0xa0, maxs);
    const float up64 = title_global<float>(0x00830c1c), down256 = title_global<float>(0x0083ecf4);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t args[2] = {i, B + 0x110};
        call(B, 0x0026a6f1, own_address(&Vehicle_GetTagOrigin_entry), TitleRegs{0, ent_w, B + 0x110, 0, slot, i}, 2,
             args);  // Vehicle_GetTagOrigin (EBX entity)
        const float wz = at<float>(v, 0x1c0);
        float x, y, z;
        wheel_point(fl(0x110), fl(0x114), wz, x, y, z);
        const float top = z + up64, bottom = z - down256;
        fl(0x8c) = x;
        fl(0x80) = x;
        fl(0x84) = y;
        fl(0x88) = top;
        float a[4] = {x, y, top, x};
        fl(0xdc) = x;
        fl(0xd8) = bottom;
        fl(0xd4) = y;
        fl(0xd0) = x;
        const float b[4] = {x, y, bottom, x};
        float lo[4] = {a[0], a[1], a[2], a[3]};
        minps(lo, b);
        maxps(a, b);
        minps(mins, lo);
        maxps(maxs, a);
        fl(0x118) = wz;
        store4(0x100, mins);
        store4(0xa0, maxs);
    }
    // The clip candidates for the wheels' box, refreshed when it leaves the cached box.
    {
        float cmax[4], cmin[4];
        load4(ent_w + 0x440, cmax);
        load4(ent_w + 0x430, cmin);
        uint32_t inside = 0;
        for (uint32_t k = 0; k < 3; ++k) {
            const float d1 = maxs[k] - cmax[k];
            const float d0 = cmin[k] - mins[k];
            const float m = d0 > d1 ? d0 : d1;
            if (m < title_global<float>(0x0082e3c0 + k * 4)) inside |= 1u << k;
        }
        {  // the w lane runs too (MXCSR)
            volatile float d1 = maxs[3] - cmax[3];
            volatile float d0 = cmin[3] - mins[3];
            (void)d1;
            (void)d0;
        }
        if (inside != 7) {
            const float margin = title_global<float>(0x008feff8);
            fl(0x8c) = margin;
            fl(0x88) = margin;
            fl(0x84) = margin;
            fl(0x80) = margin;
            float lo[4], hi[4];
            for (uint32_t k = 0; k < 4; ++k) {
                lo[k] = mins[k] - margin;
                hi[k] = maxs[k] + margin;
            }
            for (uint32_t k = 0; k < 4; ++k) {
                *pointer<float>(ent_w + 0x430 + k * 4) = lo[k];
                *pointer<float>(ent_w + 0x440 + k * 4) = hi[k];
            }
            const uint32_t args[3] = {ent_w + 0x430, at<uint32_t>(ent, 0x450), at<uint32_t>(ent, 0x454)};
            call(B, 0x0026a8f0, own_address(&CM_GatherBoxCandidates_entry), TitleRegs{ent_w + 0x430, origin_w, args[1], args[2], slot, ent_w + 0x440}, 3,
                 args);  // CM_GatherBoxCandidates (EDI maxs)
        }
    }
    wd(0xa10) = B + 0x210;
    wd(0x1a20) = B + 0xa20;
    wd(0x1a24) = 0;
    wd(0xa14) = 0;
    wd(0x2c) = 0;
    wd(0x98) = slot;
    fl(0x4c) = zero;
    wd(0xc) = B + 0x168;
    wd(0x94) = v_w + 0x21c;
    wd(0x90) = v_w + 0x138;
    wd(0xe8) = slot + 0x90;
    wd(0x24) = origin_w - slot;
    const float one = title_global<float>(0x0083eb20);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t ids = slot + 0x90 + i * 4;  // the cached triangle id
        const uint32_t result = slot + i * 0x10;   // the cached hit point, fraction in w
        uint32_t ebx_now = ent_w;
        {
            const uint32_t args[2] = {i, B + 0x1b0};
            wd(0x14) = call(B, 0x0026a991, own_address(&Vehicle_GetTagOrigin_entry), TitleRegs{0, ent_w, B + 0x1b0, 0, i, ids}, 2,
                            args);  // Vehicle_GetTagOrigin: the bone
        }
        const float wz = at<float>(v, 0x1c0);
        fl(0x1b8) = wz;
        float x, y, z;
        wheel_point(fl(0x1b0), fl(0x1b4), wz, x, y, z);
        const float top = z + up64, bottom = z - down256;
        fl(0x18) = x;
        fl(0x1c) = y;
        fl(0x3c) = z;
        fl(0xa0) = x;
        fl(0xa4) = y;
        fl(0xc0) = x;
        fl(0xc4) = y;
        fl(0xa8) = top;
        fl(0xc8) = bottom;
        by(0x13) = 0;
        float frac;
        if (!by(0x12)) {
            for (uint32_t k = 0; k < 16; k += 4) {
                wd(0x80 + k) = wd(0xa0 + k);
                wd(0xd0 + k) = wd(0xc0 + k);
            }
            float s[4], e[4];
            load4(B + 0xa0, s);
            load4(B + 0xc0, e);
            float lo[4] = {s[0], s[1], s[2], s[3]}, hi[4] = {s[0], s[1], s[2], s[3]};
            minps(lo, e);
            maxps(hi, e);
            store4(0x1d0, lo);
            store4(0x1c0, hi);
            bool retested = false;
            if (*pointer<uint32_t>(ids) != 0xffffffffu) {
                const uint32_t args[3] = {B + 0x80, B + 0xd0, ids};
                ebx_now = B + 0x1f0;
                retested = static_cast<uint8_t>(call(B, 0x0026ab6a, own_address(&CM_RetestCachedMeshTriangle_entry),
                                                     TitleRegs{ids - 0x18, B + 0x1f0, B + 0x80, wd(0xcc), ids - 0x18, ids},
                                                     3, args)) != 0;  // CM_RetestCachedMeshTriangle
                if (!retested) *pointer<uint32_t>(ids) = 0xffffffffu;
            }
            if (retested) {
                // The segment from the cached triangle's crossing: (start - point) . plane - 0.125 over
                // (start - end) . plane.
                float p[4], n[4];
                load4(B + 0x1f0, p);
                load4(B + 0x200, n);
                float d0[4], d1[4];
                for (uint32_t k = 0; k < 4; ++k) {
                    d0[k] = (s[k] - e[k]) * n[k];
                    d1[k] = (s[k] - p[k]) * n[k];
                }
                const float den = d0[0] + d0[1] + d0[2];
                d1[0] = (d1[0] + d1[1] + d1[2] - title_global<float>(0x0083ec68)) / den;  // 0.125
                const uint32_t surface = *pointer<uint32_t>(*pointer<uint32_t>(ids - 0x18) + 4);
                wd(0x144) = surface;
                store4(0x1e0, d1);
                fl(0x140) = fl(0x1e0);
                const uint32_t fargs[3] = {B + 0x1d0, B + 0x1c0, mask};
                const uint32_t list = at<uint32_t>(ent, 0x454);
                call(B, 0x0026ac37, own_address(&Phys_FilterEntriesInBox_entry), TitleRegs{B + 0x1d0, B + 0x1f0, ent_w, B + 0x1c0, B + 0x210, list}, 3,
                     fargs);  // Phys_FilterEntriesInBox (ESI out, EDI list)
                const uint32_t targs[7] = {B + 0xa20, B + 0x210, B + 0x80, B + 0xd0, B + 0x140, B + 0x144, B + 0x148};
                wd(0x1a24) = 0;
                call(B - 0xc, 0x0026ac7a, own_address(&CM_TraceSegmentThroughCandidates), TitleRegs{B + 0x80, B + 0x1f0, B + 0x210, B + 0xa20, B + 0x210, list},
                     7, targs);  // CM_TraceSegmentThroughCandidates
                frac = fl(0x140);
                float hit[4];
                for (uint32_t k = 0; k < 4; ++k) hit[k] = s[k] + (e[k] - s[k]) * frac;
                store4(0x120, hit);
                by(0x13) = 1;
            } else {
                const uint32_t in_brushes = at<uint32_t>(ent, 0x454);
                const uint32_t fargs[5] = {B + 0x1d0, B + 0x1c0, mask, at<uint32_t>(ent, 0x450), B + 0xa20};
                call(B, 0x0026ad0a, own_address(&CM_FilterBoxCandidates), TitleRegs{B + 0x210, ebx_now, in_brushes, B + 0x1c0, i, ids}, 5,
                     fargs);  // CM_FilterBoxCandidates (EAX out brushes, ECX in brushes)
                const uint32_t targs[7] = {B + 0xa20, B + 0x210, B + 0x80, B + 0xd0, B + 0x140, B + 0x144, B + 0x148};
                title_global<uint32_t>(0x009507e0) = 0xffffffffu;
                fl(0x140) = one;
                call(B - 0x14, 0x0026ad62, own_address(&CM_TraceSegmentThroughCandidates), TitleRegs{B + 0x210, ebx_now, B + 0xa20, B + 0x80, i, ids}, 7,
                     targs);  // CM_TraceSegmentThroughCandidates
                frac = fl(0x140);
                if (one > frac) {
                    float hit[4];
                    for (uint32_t k = 0; k < 4; ++k) hit[k] = s[k] + (e[k] - s[k]) * frac;
                    store4(0x120, hit);
                    const uint32_t id = title_global<uint32_t>(0x009507e0);
                    if (id != 0xffffffffu) {
                        *pointer<uint32_t>(ids) = id;
                        *pointer<uint32_t>(ids - 0x18) = title_global<uint32_t>(0x0094cb9c);
                    }
                }
            }
            if (frac > title_global<float>(0x0083ec44)) {  // 0.4: the entities too
                const uint32_t w0 = title_global<uint32_t>(0x0082e290), w1 = title_global<uint32_t>(0x0082e294);
                const uint32_t w2 = title_global<uint32_t>(0x0082e298), w3 = title_global<uint32_t>(0x0082e29c);
                wd(0x100) = w0;
                wd(0x110) = w0;
                wd(0x104) = w1;
                wd(0x114) = w1;
                wd(0x108) = w2;
                wd(0x118) = w2;
                wd(0x10c) = w3;
                wd(0x11c) = w3;
                const uint32_t args[11] = {B + 0x120, B + 0x110, B + 0x100, B + 0xc0, at<uint32_t>(ent, 0x224), mask,
                                           0, 0, 0, 0, 0};
                call(B, 0x0026ae5d, own_address(&SV_Trace_entry), TitleRegs{B + 0x100, B + 0xa0, B + 0x110, B + 0x120, i, ids}, 11,
                     args);  // SV_Trace (EBX start)
                frac = fl(0x140);
            }
            for (uint32_t k = 0; k < 16; k += 4) *pointer<uint32_t>(result + k) = wd(0x120 + k);
            *pointer<float>(result + 0xc) = frac;
            *pointer<uint32_t>(ids - 0x30) = wd(0x144);
        } else {
            frac = *pointer<float>(result + 0xc);
            for (uint32_t k = 0; k < 16; k += 4) wd(0x120 + k) = *pointer<uint32_t>(result + k);
            wd(0x144) = *pointer<uint32_t>(ids - 0x30);
        }
        float ground;
        uint32_t* const surface_type = pointer<uint32_t>(wd(0x24) + ids);  // +0x150
        if (one > frac || by(0x13)) {
            ground = fl(0x128);
            fl(0x18) = fl(0x120);
            fl(0x1c) = fl(0x124);
            x = fl(0x18);
            y = fl(0x1c);
            *surface_type = static_cast<uint32_t>(static_cast<int32_t>(wd(0x144)) >> 20) & 0x1f;
        } else {
            ground = fl(0xc8);
            *surface_type = 0;
        }
        // The wheel height: falling toward the ground with `fall`, else on it.
        float* const wheel = pointer<float>(wd(0x90));  // +0x138; its velocity at -0x18
        if (fall) {
            const float dt = static_cast<float>(elapsed) * title_global<float>(0x0083eb4c);  // 0.001
            const float vel = wheel[-6] - dt * title_global<float>(0x0083f194);                 // 800
            wheel[-6] = vel;
            const float h = vel * dt + wheel[0];
            wheel[0] = h;
            if (ground > h) {
                wheel[-6] = zero;
                wheel[0] = ground;
            }
        } else {
            wheel[-6] = zero;
            wheel[0] = ground;
        }
        {
            float* const point = pointer<float>(wd(0xc));
            float lift = z - ground - at<float>(v, 0x1c0);
            point[0] = x;
            point[1] = y;
            point[2] = wheel[0];
            fl(0xf8) = zero;
            fl(0xf4) = zero;
            const float high = title_global<float>(0x0083ef10);  // 15
            if (lift > high) {
                lift = high;
            } else {
                const float low = title_global<float>(0x0083ef14);  // -15
                if (low > lift) lift = low;
            }
            fl(0xfc) = zero - lift;
            fl(0x48) = at<float>(v, 0x270);
            fl(0x44) = at<float>(v, 0x1c4);
            if (static_cast<int32_t>(i) != title_global<int32_t>(0x0095f658) &&
                static_cast<int32_t>(i) != title_global<int32_t>(0x008d9d58))
                fl(0x48) = zero;
            const uint32_t args[5] = {ent_w, B + 0xf4, B + 0x44, wd(0x14), 0};
            call(B, 0x0026b074, own_address(&G_DObjSetBoneRotation_entry), TitleRegs{ent_w, v_w, B + 0x44, B + 0xf4, i, ids}, 5,
                 args);  // G_DObjSetBoneRotation
        }
        uint32_t* const fx = pointer<uint32_t>(wd(0x94));  // +0x21c
        const uint32_t list = title_global<uint32_t>(0x0095f5b4);
        if (at<float>(v, 0x240) > title_global<float>(0x0083ebf8) && *surface_type) {  // 0.1
            if (*fx == 0xffffffffu) {
                const uint32_t args[3] = {wd(0xc), *surface_type, title_global<uint32_t>(0x009886f8 + i * 4)};
                *fx = call(B, 0x0026b0b5, own_address(&Vehicle_PlayFxAtTag_entry), TitleRegs{ent_w, address_of(fx), wd(0x24), args[2], i, ids}, 3,
                           args);  // Vehicle_PlayFxAtTag (EAX entity)
            }
            float rate;
            __asm__("flds %1\n\tfdivs %2\n\tfstps %0"
                    : "=m"(rate)
                    : "m"(at<float>(ent, 0x2fc)), "m"(at<float>(def, 0x44))
                    : "st");
            fl(0x14) = rate;
            const uint32_t args[4] = {list, *fx, 0x0083b284, wd(0x14)};
            call(B, 0x0026b0ea, own_address(&FXParam_Broadcast), TitleRegs{wd(0x14), address_of(fx), *fx, list, i, ids}, 4,
                 args);  // FXParam_Broadcast "EmissionRate" (stdcall)
        } else {
            if (*fx != 0xffffffffu) {
                const uint32_t args[1] = {*fx};
                call(B, 0x0026b106, own_address(&FX_FreeEffectsById_entry), TitleRegs{*fx, address_of(fx), 0, 0, list, ids}, 1,
                     args);  // FX_FreeEffectsById (ESI list; stdcall)
            }
            *fx = 0xffffffffu;
        }
        wd(0x90) += 4;
        wd(0x2c) = i + 1;
        wd(0x98) = result + 0x10;
        wd(0xe8) = ids + 4;
        wd(0x94) = address_of(fx) + 4;
        wd(0xc) += 12;
    }
    if (!(at<float>(v, 0x240) > title_global<float>(0x0083ebf8)) && at<uint32_t>(v, 0x234) != 0xffffffffu) {
        const uint32_t args[1] = {at<uint32_t>(v, 0x234)};
        call(B, 0x0026b195, own_address(&FX_FreeEffectsById_entry), TitleRegs{args[0], origin_w, 0, 0, title_global<uint32_t>(0x0095f5b4), v_w}, 1,
             args);  // FX_FreeEffectsById (ESI list; stdcall)
        at<uint32_t>(v, 0x234) = 0xffffffffu;
    }
    // The ground axis from the wheel points P0..P3 (B+0x168, 12 bytes each).
    const auto P = [&](uint32_t k, uint32_t c) -> float& { return fl(0x168 + k * 12 + c * 4); };
    const float half = title_global<float>(0x0083eb24);
    const auto length_of = [&](float sum) {
        fl(0xc) = sum;
        const float r = title_ci_sqrt(sum);  // __CIsqrt: x87 round to nearest, the title chops
        fl(0x14) = r;
        fl(0xc) = r;
        return r;
    };
    {
        const float fx_ = (P(3, 0) + P(1, 0)) * half - (P(2, 0) + P(0, 0)) * half;
        const float fz = (P(3, 2) + P(1, 2)) * half - (P(2, 2) + P(0, 2)) * half;
        const float fy = (P(3, 1) + P(1, 1)) * half - (P(2, 1) + P(0, 1)) * half;
        fl(0x4c) = fz;
        fl(0x48) = fy;
        fl(0x44) = fx_;
        const float len = length_of(fz * fz + fy * fy + fx_ * fx_);
        if (!(len == title_global<float>(0x0083eafc))) {
            const float inv = one / len;
            fl(0x44) = inv * fl(0x44);
            fl(0x48) = inv * fl(0x48);
            fl(0x4c) = inv * fl(0x4c);
        }
    }
    float lx, ly, lz;
    {
        const float h = title_global<float>(0x0083eb24);
        lz = (P(0, 2) + P(1, 2)) * h - (P(2, 2) + P(3, 2)) * h;
        ly = (P(0, 1) + P(1, 1)) * h - (P(2, 1) + P(3, 1)) * h;
        lx = (P(0, 0) + P(1, 0)) * h - (P(2, 0) + P(3, 0)) * h;
        fl(0x20) = lz;
        fl(0x1c) = ly;
        fl(0x18) = lx;
        const float len = length_of(lz * lz + ly * ly + lx * lx);
        if (!(len == title_global<float>(0x0083eafc))) {
            const float inv = one / len;
            ly = inv * fl(0x1c);
            lx = inv * fl(0x18);
            lz = inv * fl(0x20);
        } else {
            lz = fl(0x20);
            ly = fl(0x1c);
            lx = fl(0x18);
        }
    }
    // up = forward x left; the plane distance: the highest wheel above the first less def +0x64.
    const float Fx = fl(0x44), Fy = fl(0x48), Fz = fl(0x4c);
    const float ux = lz * Fy - ly * Fz;
    const float uy = lx * Fz - lz * Fx;
    const float uz = ly * Fx - lx * Fy;
    float dist = uz * P(0, 2) + uy * P(0, 1) + ux * P(0, 0);
    fl(0x34) = ux;
    fl(0x38) = uy;
    fl(0x3c) = uz;
    fl(0x40) = dist;
    {
        const float slack = at<float>(def, 0x64);
        const auto check = [&](float d) {
            if (d - dist > slack) {
                dist = d - slack;
                fl(0x40) = dist;
            }
        };
        int32_t k = 1;
        if (static_cast<int32_t>(count) - 1 >= 4) {
            const uint32_t groups = ((count - 5) >> 2) + 1;
            for (uint32_t g = 0; g < groups; ++g) {
                const uint32_t b = 1 + g * 4;
                check(P(b, 2) * uz + P(b, 0) * ux + P(b, 1) * uy);
                check(P(b + 1, 2) * uz + P(b + 1, 1) * uy + P(b + 1, 0) * ux);
                check(P(b + 2, 2) * uz + P(b + 2, 1) * uy + P(b + 2, 0) * ux);
                check(P(b + 3, 2) * uz + P(b + 3, 1) * uy + P(b + 3, 0) * ux);
            }
            k = static_cast<int32_t>(groups * 4 + 1);
        }
        for (; k < static_cast<int32_t>(count); ++k) check(P(k, 2) * uz + P(k, 0) * ux + P(k, 1) * uy);
    }
    // left = body forward x up, forward = up x left (each normalized), then the angles.
    {
        const float fx0 = fl(0x50), fy0 = fl(0x54), fz0 = fl(0x58);
        float a = fz0 * uy - fy0 * uz;
        float c = fy0 * ux - fx0 * uy;
        float b = fx0 * uz - fz0 * ux;
        fl(0x5c) = a;
        fl(0x60) = b;
        fl(0x64) = c;
        const float len = length_of(c * c + b * b + a * a);
        if (!(len == title_global<float>(0x0083eafc))) {
            const float inv = one / len;
            a = a * inv;
            b = b * inv;
            c = c * inv;
            fl(0x5c) = a;
            fl(0x60) = b;
            fl(0x64) = c;
        }
        const float nx = b * uz - c * uy;
        const float nz = a * uy - b * ux;
        fl(0x58) = nz;
        const float ny = c * ux - a * uz;
        fl(0x50) = nx;
        fl(0x54) = ny;
        const float len2 = length_of(nz * nz + ny * ny + nx * nx);
        if (!(len2 == title_global<float>(0x0083eafc))) {
            const float inv = one / len2;
            fl(0x50) = inv * fl(0x50);
            fl(0x54) = fl(0x54) * inv;
            fl(0x58) = fl(0x58) * inv;
        }
    }
    *pointer<uint32_t>(B - 4) = 0x0026b7e8;
    AxisToAngles(&fl(0xf4), 0, &fl(0x50));
    float* const angles = pointer<float>(wd(0xf0));  // +0xe0
    const uint32_t old = origin_w + 0x30;           // +0xf0: last frame's angles
    *pointer<uint32_t>(B - 4) = static_cast<uint32_t>(elapsed);
    *pointer<uint32_t>(B - 8) = 0x40c00000;
    *pointer<uint32_t>(B - 0xc) = 0x0026b807;
    angles[0] = AngleWrapNear(fl(0xf4), *pointer<float>(old), 6.0f, elapsed);
    *pointer<uint32_t>(B - 0xc) = static_cast<uint32_t>(elapsed);
    *pointer<uint32_t>(B - 0x10) = 0x40c00000;
    *pointer<uint32_t>(B - 0x14) = 0x0026b82b;
    at<float>(v, 0xe8) = AngleWrapNear(fl(0xfc), *pointer<float>(old + 8), 6.0f, elapsed);
    {
        const float low = title_global<float>(0x0083ee2c), high = title_global<float>(0x0083eb3c);  // -60, 60
        const auto clamp = [&](float& a) {
            float r;
            if (low > a)
                r = low;
            else
                r = a > high ? high : a;
            a = r;
        };
        clamp(angles[0]);
        clamp(at<float>(v, 0xe8));
    }
    if (at<uint8_t>(ent, 0x297) != 2) {
        const float h = (at<float>(v, 0xc0) * fl(0x34) + at<float>(v, 0xc4) * fl(0x38) - fl(0x40)) / fl(0x3c);
        at<float>(v, 0xc8) = zero - h;
    }
    *pointer<uint32_t>(B - 4) = v_w + 0x110;
    *pointer<uint32_t>(B - 8) = 0x0026b8d0;
    AnglesNormalize180(pointer<float>(old), angles, pointer<float>(v_w + 0x110));
    {
        const float rate = one / (static_cast<float>(elapsed) * title_global<float>(0x0083eb4c));
        at<float>(v, 0x110) = at<float>(v, 0x110) * rate;
        at<float>(v, 0x114) = rate * at<float>(v, 0x114);
        at<float>(v, 0x118) = rate * at<float>(v, 0x118);
    }
}

// The tag matrices go to the original's frame (gap entry; F = aligned base - 0x80) and every title callee runs
// at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_ThinkInit_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_ThinkInit) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_ThinkInit(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // The vehicle entity's first think (cdecl entity; parked player vehicles only renew the think time): the six
    // wheel tags (names 0x009886f8; "tag_origin" counts as bone 0) give their heights to +0x138.. and their
    // indices (+0x21c..) are reset to -1; the distance between the turret tags (+0x2cc and +0x2c4) goes to
    // +0x1a8; types 1 and 2 take a physics step (0x008dda9c ms) when forced (0x0095bf0c) or in a cell with
    // collision; then origin/angles set and kept as the previous ones, touches, and Vehicle_Think next (0x18).
    *pointer<uint32_t>(entry_esp - 4) = ebp;
    const uint32_t F = ((entry_esp - 4) & ~15u) - 0x80;
    auto wd = [F](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(F + off); };
    auto fl = [F](uint32_t off) -> float& { return *pointer<float>(F + off); };
    wd(0) = edi;
    wd(4) = esi;
    wd(8) = ebx;
    uint8_t* const ent = *pointer<uint8_t*>(entry_esp + 4);
    const uint32_t ent_w = address_of(ent);
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    wd(0x20) = address_of(vehicle);
    float* const origin = &at<float>(vehicle, 0xc0);
    wd(0x24) = address_of(def);
    if (at<uint8_t>(ent, 0x297) == 2 && !title_global<uint32_t>(0x0095f684)) {
        at<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c);
        return;
    }
    const auto tag_matrix = [&](int32_t bone, uint32_t ebx_now) {
        const TitleRegs regs{static_cast<uint32_t>(bone), ebx_now, 0, F + 0x40, ent_w, address_of(origin)};
        const uint32_t args[3] = {ent_w, static_cast<uint32_t>(bone), F + 0x40};
        title_call_at_regs(F, own_address(&G_DObjGetWorldTagMatrix), &regs, 3, args);  // G_DObjGetWorldTagMatrix
    };
    for (uint32_t i = 0; i < 6; ++i) {
        wd(0x1c) = address_of(vehicle) + 0x21c + i * 4;
        wd(0x18) = 0x009886f8 + i * 4;
        wd(0x10) = address_of(origin) + 0x78 + i * 4;
        const uint32_t name = title_global<uint32_t>(0x009886f8 + i * 4);
        int32_t bone = -1;
        if (const DObj* const obj = at<const DObj*>(ent, 0x22c)) {
            wd(0x14) = name ? static_cast<uint32_t>(DObj_GetBoneIndex(obj, name)) : 0xffffffffu;
            if (!(title_global<uint8_t>(0x0098b840) & 1)) {
                title_global<uint32_t>(0x0098b840) |= 1;
                title_global<uint32_t>(0x0098b83c) = Com_HashKeyNoCase(0, pointer<const char>(0x00832b04));  // "tag_origin"
            }
            bone = static_cast<int32_t>(wd(0x14));
            if (bone < 0 && name == title_global<uint32_t>(0x0098b83c)) bone = 0;
        }
        if (bone >= 0) {
            tag_matrix(bone, name);
            *pointer<float>(wd(0x10)) = fl(0x78);
        }
        *pointer<int32_t>(wd(0x1c)) = -1;
    }
    const int32_t first = at<int32_t>(pointer<uint8_t>(wd(0x20)), 0x2cc);
    if (first >= 0) {
        tag_matrix(first, title_global<uint32_t>(0x00988710 - 4));
        fl(0x34) = fl(0x70);
        uint8_t* const v = pointer<uint8_t>(wd(0x20));
        const int32_t second = at<int32_t>(v, 0x2c4);
        fl(0x38) = fl(0x74);
        fl(0x3c) = fl(0x78);
        if (second >= 0) {
            tag_matrix(second, address_of(v));
            float d[3];
            __asm__("flds %3\n\tfsubs %4\n\tfstps %0\n\tflds %5\n\tfsubs %6\n\tfstps %1\n\tflds %7\n\tfsubs %8\n\tfstps %2"
                    : "=m"(d[0]), "=m"(d[1]), "=m"(d[2])
                    : "m"(fl(0x34)), "m"(fl(0x70)), "m"(fl(0x38)), "m"(fl(0x74)), "m"(fl(0x3c)), "m"(fl(0x78)));
            fl(0x28) = d[0];
            fl(0x2c) = d[1];
            fl(0x30) = d[2];
            float length;
            __asm__ volatile("flds %1\n\tfmuls %1\n\tflds %2\n\tfmuls %2\n\tfaddp\n\tflds %3\n\tfmuls %3\n\tfaddp\n\t"
                             "call *%4\n\tfstps %0"
                             : "=m"(length)
                             : "m"(d[2]), "m"(d[1]), "m"(d[0]), "r"(own_address(&title_CIsqrt))
                             : "eax", "ecx", "edx", "st", "st(1)", "memory");  // __CIsqrt
            fl(0x20) = length;
            at<float>(v, 0x1a8) = length;
        }
    }
    const int16_t type = at<int16_t>(pointer<uint8_t>(wd(0x24)), 0x20);
    if (type == 1 || type == 2) {
        bool step = title_global<uint32_t>(0x0095bf0c) != 0;
        if (!step) {
            const int32_t cell = at<int32_t>(ent, 0x458);
            if (cell >= 0) {
                const uint8_t* const cm = title_global<uint8_t*>(0x0095f6b8);
                const uint32_t index = static_cast<uint32_t>(cell) < at<uint32_t>(cm, 0x18) ? static_cast<uint32_t>(cell) : 0;
                step = at<uint32_t>(at<uint8_t*>(cm, 0x1c) + index * 0x50, 0x3c) != 0;
            }
        }
        if (step) {
            const uint32_t args[3] = {ent_w, 0, title_global<uint32_t>(0x008dda9c)};
            const TitleRegs regs{0, 0, 0, args[2], ent_w, address_of(origin)};
            title_call_at_regs(F, own_address(&Vehicle_UpdatePhysics_entry), &regs, 3, args);  // Vehicle_UpdatePhysics
        }
    }
    {
        const uint32_t args[2] = {address_of(origin), address_of(origin) + 0x20};
        const TitleRegs regs{ent_w, address_of(origin) + 0x20, 0, 0, ent_w, address_of(origin)};
        title_call_at_regs(F, own_address(&Vehicle_SetOriginAndAngles_entry), &regs, 2, args);  // Vehicle_SetOriginAndAngles
    }
    for (uint32_t k = 0; k < 3; ++k) {
        at<uint32_t>(origin, 0x10 + k * 4) = at<uint32_t>(origin, k * 4);
        at<uint32_t>(origin, 0x30 + k * 4) = at<uint32_t>(origin, 0x20 + k * 4);
    }
    {
        const uint32_t args[2] = {ent_w, 0};
        const TitleRegs regs{0, address_of(origin) + 0x20, ent_w + 0x140, 0, ent_w, address_of(origin)};
        title_call_at_regs(F - 8, own_address(&G_TouchEntitiesInBox), &regs, 2, args);  // G_TouchEntitiesInBox
    }
    at<uint32_t>(ent, 0x32c) = 0x18;
    at<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c) + 1;
}

void XB_CDECL Phys_FilterEntriesInBox_body(uint8_t* out, const uint8_t* list, const float* mins, const float* maxs,
                                           uint32_t mask) {
    // The entries of list (+0x800 pointers, +0x804 count) with a contents bit of mask (+8) whose box (centre +0x10,
    // half size +0x20) overlaps mins..maxs on x, y, z (packed: max(centre - maxs, mins - centre) < half) go into
    // out (same layout, at most 0x200).
    at<int32_t>(out, 0x804) = 0;
    const int32_t count = at<int32_t>(list, 0x804);
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* entry = at<uint8_t* const*>(list, 0x800)[i];
        if (!(at<uint32_t>(entry, 8) & mask)) continue;
        const float* const centre = &at<float>(entry, 0x10);
        const float* const half = &at<float>(entry, 0x20);
        float below[4], above[4];
        for (uint32_t k = 0; k < 4; ++k) below[k] = mins[k] - centre[k];
        for (uint32_t k = 0; k < 4; ++k) above[k] = centre[k] - maxs[k];
        uint32_t bits = 0;
        for (uint32_t k = 0; k < 4; ++k) {
            const float m = above[k] > below[k] ? above[k] : below[k];  // MAXPS (second operand on NaN)
            if (m < half[k]) bits |= 1u << k;
        }
        if ((bits & 7) != 7) continue;
        const int32_t n = at<int32_t>(out, 0x804);
        if (n >= 0x200) continue;
        uint8_t** const slot = at<uint8_t**>(out, 0x800) + n;
        at<int32_t>(out, 0x804) = n + 1;
        if (slot) *slot = at<uint8_t* const*>(list, 0x800)[i];
    }
}

// ESI = out list, EDI = list; cdecl mins, maxs, mask.
PLATFORM_ASM_ENTRY(Phys_FilterEntriesInBox_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Phys_FilterEntriesInBox_body) "addl $20, %esp\n"
                   "ret")

PLATFORM_ASM_ENTRY(Vehicle_ResetOnVehicleOverlap_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Vehicle_ResetOnVehicleOverlap) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Vehicle_ResetOnVehicleOverlap(uint8_t* ent) {
    // A vehicle (+0xdc set) whose clip model overlaps another vehicle slot's entity (g_vehicles +0x170 handle, not
    // its own +0x224, +0x297 = 2; box = its origin + 0x00988390 / + 0x00985360) gets its state (+0x248) reset from
    // the defaults at 0x009883b0 (0xb8 bytes) and 0x00988470 (0xb0 bytes at +0xc0), and +0x2fc = 0.
    uint8_t* const state = at<uint8_t*>(ent, 0x248);
    if (!at<uint32_t>(ent, 0xdc)) return;
    if (!title_global<uint16_t>(0x00986000)) return;  // g_vehicleSlotCount
    for (int32_t i = 0; i < title_global<uint16_t>(0x00986000); ++i) {
        const uint32_t handle = at<uint32_t>(title_global<uint8_t*>(0x0095f694), static_cast<uint32_t>(i) * 0x340 + 0x170);
        if (!handle || handle == at<uint32_t>(ent, 0x224)) continue;
        const uint32_t index = handle & 0xfff;
        const uint8_t* other = nullptr;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            other = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (at<uint8_t>(other, 0x297) != 2) continue;
        float mins[3], maxs[3];
        for (uint32_t k = 0; k < 3; ++k) mins[k] = at<float>(other, 0x140 + k * 4) + title_global<float>(0x00988390 + k * 4);
        for (uint32_t k = 0; k < 3; ++k) maxs[k] = at<float>(other, 0x140 + k * 4) + title_global<float>(0x00985360 + k * 4);
        if (!SV_ClipToEntity(ent, mins, maxs, 1)) continue;
        for (uint32_t k = 0; k < 0x2e; ++k) at<uint32_t>(state, k * 4) = title_global<uint32_t>(0x009883b0 + k * 4);
        for (uint32_t k = 0; k < 0x2c; ++k) at<uint32_t>(state, 0xc0 + k * 4) = title_global<uint32_t>(0x00988470 + k * 4);
        at<float>(ent, 0x2fc) = 0.0f;
        return;
    }
}

int32_t XB_REGS3 XB_STDCALL VehicleNode_FindByKey(uint32_t all, uint32_t, const ScriptString* key,
                                                  const ScriptString* value, int32_t* out) {
    // EAX = all: every node index into `out` (count returned). Without a value or key the same when there is
    // an `out`. Else the field the key names (text at the rep + 6), compared to the value on every node.
    int32_t found = 0;
    const uint32_t* const key_rep = reinterpret_cast<const uint32_t*>(key);
    if (!all) {
        if (*reinterpret_cast<const uint32_t*>(value) && *key_rep) {
        } else if (out) {
            all = 1;
        }
    }
    if (all) {
        for (int32_t i = 0; i < title_global<int32_t>(0x0094d4d8); ++i) out[found++] = i;  // g_numVehNodes
        return found;
    }
    static const uint32_t kKeys[7][2] = {
        {0x0082dda8, 0x00},  // "targetname"
        {0x00833ad0, 0x04},  // "target"
        {0x00833aa8, 0x14},  // "origin"
        {0x00833aa0, 0x2c},  // "angles"
        {0x00833a44, 0x08},  // "speed"
        {0x00833a38, 0x0c},  // "lookahead"
        {0x00833ab0, 0x10},  // "script_noteworthy"
    };
    const auto stricmp = own_function<StrICmp>(&title_stricmp);  // _stricmp
    uint32_t field = 0;
    bool known = false;
    for (uint32_t k = 0; k < 7; ++k) {
        const char* const text = *key_rep ? pointer<const char>(*key_rep + 6) : nullptr;
        if (stricmp(pointer<const char>(kKeys[k][0]), text) == 0) {
            field = kKeys[k][1];
            known = true;
            break;
        }
    }
    if (!known) return -1;
    for (int32_t i = 0; i < title_global<int32_t>(0x0094d4d8); ++i) {
        uint8_t* const slot = title_global<uint8_t*>(0x0095c228 + static_cast<uint32_t>(i) * 4) + field;  // g_vehNodes
        const uint32_t rep = at<uint32_t>(slot, 0);
        if (!rep || rep + 6 == 0 || !*pointer<const char>(rep + 6)) continue;
        if (!ScriptString_EqualsString(reinterpret_cast<const ScriptString*>(slot), value)) continue;
        if (!out) return i;
        out[found++] = i;
    }
    if (out) return found;
    return -1;
}

// title_call_at_regs that also hands the callee the original's EBP (regs->ebp, a word of the rebuilt original
// frame: the saved caller EBP). That word holds this helper's frame pointer across the call (restored after), so
// the return gets back without writing below the callee's final ESP: actor.cpp's ai_call_at_ebp pushes there and
// a later callee's never-set locals copied those words (CG_Bullet's effect transform under Weapon_BulletTrace_r).
struct VehRegsBp {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp;
};
extern "C" PLATFORM_HIDDEN uint64_t XB_CDECL veh_call_at_ebp(uint32_t top, uint32_t function, const VehRegsBp* regs,
                                                           uint32_t count, const uint32_t* args);
PLATFORM_ASM_ENTRY(veh_call_at_ebp,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl %esp\n" PLATFORM_ASM_CALL(game_floor_enter) "movl %eax, (%esp)\n"
                   "movl 16(%ebp), %eax\n"
                   "movl 24(%eax), %eax\n"
                   "pushl (%eax)\n"
                   "movl %ebp, (%eax)\n"
                   "movl 8(%ebp), %edx\n"
                   "movl 20(%ebp), %ecx\n"
                   "movl 24(%ebp), %edi\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "decl %ecx\n"
                   "subl $4, %edx\n"
                   "movl (%edi,%ecx,4), %eax\n"
                   "movl %eax, (%edx)\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 12(%ebp), %ecx\n"
                   "movl %ecx, -4(%edx)\n"  // the call reads its target before it pushes over it
                   "movl 16(%ebp), %eax\n"
                   "movl %edx, %esp\n"
                   "movl 4(%eax), %ebx\n"
                   "movl 8(%eax), %ecx\n"
                   "movl 12(%eax), %edx\n"
                   "movl 16(%eax), %esi\n"
                   "movl 20(%eax), %edi\n"
                   "movl 24(%eax), %ebp\n"
                   "movl 0(%eax), %eax\n"
                   "call *-4(%esp)\n"
                   "xchgl %ebp, (%ebp)\n"
                   "leal -20(%ebp), %esp\n"
                   "movl 16(%ebp), %ecx\n"
                   "movl 24(%ecx), %ecx\n"
                   "popl (%ecx)\n"
                   "movl %eax, %esi\n"
                   "movl %edx, %edi\n" PLATFORM_ASM_CALL(game_floor_leave) "movl %esi, %eax\n"
                   "movl %edi, %edx\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %ebx\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebp\n"
                   "ret")

namespace {

// Runs a title callee at the original's ESP (top) and writes back the original call's return address.
uint32_t veh_call_at(uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count = 0,
                     const uint32_t* args = nullptr) {
    const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
    *pointer<uint32_t>(top - count * 4 - 4) = ret;
    return result;
}

// veh_call_at with the original's EBP in the callee: title code that pushes it leaves it in words it copies
// later without setting them (CG_Bullet's effect transform, words 9 and 11, under Weapon_BulletTrace_r).
// `ebp` must be the rebuilt frame's saved-EBP word (veh_call_at_ebp borrows it).
uint32_t veh_call_bp(uint32_t ebp, uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs,
                     uint32_t count = 0, const uint32_t* args = nullptr) {
    const VehRegsBp r{regs.eax, regs.ebx, regs.ecx, regs.edx, regs.esi, regs.edi, ebp};
    const uint32_t result = static_cast<uint32_t>(veh_call_at_ebp(top, function, &r, count, args));
    *pointer<uint32_t>(top - count * 4 - 4) = ret;
    return result;
}

// (heat - 0x0083ee68) * 0x0083f1d8 * 0x008fefec on the x87 (doubles, then the float), stored as float.
float overheat_emission_rate(float heat) {
    long double v = platform_x87_ld(heat);
    v = v - platform_x87_ld(title_global<double>(0x0083ee68));
    v = v * platform_x87_ld(title_global<double>(0x0083f1d8));
    v = v * platform_x87_ld(title_global<float>(0x008fefec));
    return static_cast<float>(v);
}

}  // namespace

// FXCmd_PlayWeaponSound copies a partly filled stack block: every callee runs at the original's ESP (gap entry).
PLATFORM_ASM_ENTRY(Vehicle_UpdateOverheat_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_UpdateOverheat) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_UpdateOverheat(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Heat (+0x198) reaching 1 overheats the turret (+0x19c, sound 0x2a, "overheated", the smoke +0x1a0 at
    // rate 200); the overheat ends at 0.5 (smoke freed). It cools by elapsed * def +0x850 / 1000 (0 at or
    // below 0). Above 0.25 the smoke runs (sound 0x29 starts it) at a rate from the heat; else it is freed.
    // The turret weapon is +0x1a4, else the entity's weapon (+3). Frame: entry - 0x10.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebx;
    *pointer<uint32_t>(E - 8) = ebp;
    *pointer<uint32_t>(E - 0xc) = esi;
    *pointer<uint32_t>(E - 0x10) = edi;
    const uint32_t T = E - 0x10;
    uint8_t* const ent = pointer<uint8_t>(*pointer<uint32_t>(E + 4));
    const int32_t elapsed = *pointer<int32_t>(E + 8);
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint32_t weapon = at<uint32_t>(vehicle, 0x1a4);
    const uint8_t* const def = weapon ? weapon_def(weapon) : weapon_def(at<uint8_t>(ent, 3));
    const uint32_t ent_w = address_of(ent), def_w = address_of(def), veh_w = address_of(vehicle);
    const uint32_t list = 0x0095f5b4;
    const uint32_t name = at<uint32_t>(def, 8);
    const auto free_smoke = [&](uint32_t ret) {
        esi = title_global<uint32_t>(list);
        const uint32_t args[1] = {at<uint32_t>(vehicle, 0x1a0)};
        veh_call_at(T, ret, own_address(&FX_FreeEffectsById_entry), TitleRegs{args[0], def_w, 0, 0, esi, veh_w}, 1, args);  // FX_FreeEffectsById
        at<uint32_t>(vehicle, 0x1a0) = 0;
    };
    if (at<float>(vehicle, 0x198) >= title_global<float>(0x0083eb20)) {
        at<uint8_t>(vehicle, 0x19c) = 1;
        {
            const uint32_t args[2] = {name, 0x2a};
            veh_call_at(T, 0x00266b84, own_address(&FXCmd_PlayWeaponSound_entry), TitleRegs{ent_w, def_w, name, 0, esi, veh_w}, 2,
                        args);  // FXCmd_PlayWeaponSound
        }
        esi = ent_w;
        {
            const uint32_t args[1] = {title_global<uint32_t>(0x0097a888)};  // "overheated"
            veh_call_at(T, 0x00266b95, own_address(&Scr_Notify_entry), TitleRegs{0, def_w, 0, args[0], esi, veh_w}, 1, args);  // Scr_Notify
        }
        if (const uint32_t id = at<uint32_t>(vehicle, 0x1a0)) {
            const uint32_t args[4] = {title_global<uint32_t>(list), id, 0x0083b284, 0x43480000};  // EmissionRate, 200
            veh_call_at(T, 0x00266bb5, own_address(&FXParam_Broadcast), TitleRegs{args[0], def_w, 0, 0, esi, veh_w}, 4,
                        args);  // FXParam_Broadcast
        }
    } else if (at<uint8_t>(vehicle, 0x19c) && title_global<float>(0x0083eb24) >= at<float>(vehicle, 0x198)) {
        at<uint8_t>(vehicle, 0x19c) = 0;
        if (at<uint32_t>(vehicle, 0x1a0)) free_smoke(0x00266bef);
    }
    const float heat = at<float>(vehicle, 0x198);
    const float zero = 0.0f;
    if (heat > zero)
        at<float>(vehicle, 0x198) =
            heat - (static_cast<float>(elapsed) * at<float>(def, 0x850)) * title_global<float>(0x0083eb4c);
    else
        at<float>(vehicle, 0x198) = zero;
    if (at<float>(vehicle, 0x198) > title_global<float>(0x0083ec24)) {
        if (!at<uint32_t>(vehicle, 0x1a0)) {
            const uint32_t args[2] = {name, 0x29};
            at<uint32_t>(vehicle, 0x1a0) = veh_call_at(T, 0x00266c5d, own_address(&FXCmd_PlayWeaponSound_entry),
                                                       TitleRegs{ent_w, def_w, name, 0, esi, veh_w}, 2,
                                                       args);  // FXCmd_PlayWeaponSound
        }
        const float rate = overheat_emission_rate(at<float>(vehicle, 0x198));
        uint32_t rate_w;
        std::memcpy(&rate_w, &rate, 4);
        const uint32_t args[4] = {title_global<uint32_t>(list), at<uint32_t>(vehicle, 0x1a0), 0x0083b284, rate_w};
        veh_call_at(T, 0x00266c99, own_address(&FXParam_Broadcast), TitleRegs{args[0], def_w, name, args[1], esi, veh_w}, 4,
                    args);  // FXParam_Broadcast
        return;
    }
    if (at<uint32_t>(vehicle, 0x1a0)) free_smoke(0x00266cb4);
}

// The trace reads its points' fourth words and the callees stack words: the frame sits at the original's
// addresses (gap entry; B = aligned base - 0x80) and every callee runs at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_FireTurret_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_FireTurret) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_FireTurret(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Clears the fire state (+0x1c8/+0x1cc, recoil +0x1dc..+0x1e4). Needs a live driver (+0x1a0) and a
    // weapon (+3), not with client flag 0x400000. The refire timer +0x180 counts down; the driver's attack
    // button (client +0x528 bit 0, not with +0x2c bit 0x4000) fires when it ran out: "turret_fire", and for the
    // entity's own weapon (no +0x1a4) the heat rises by def +0x5dc * +0x84c / 1000 (no fire while overheated).
    // Then +0x1c8 = 1 and the aim point (+0x1d0) 10240 along the driver's view from the cgame view origin (the
    // player) or the view tag (+0x2c4; +0x2cc first), pulled in to what a trace hits; outside a valid cell
    // +0x1ac = 1.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0x80;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    wd(8) = ebx;
    wd(4) = esi;
    uint8_t* const ent = pointer<uint8_t>(*pointer<uint32_t>(E + 4));
    const int32_t elapsed = *pointer<int32_t>(E + 8);
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint32_t ent_w = address_of(ent), veh_w = address_of(vehicle);
    at<uint32_t>(vehicle, 0x1c8) = 0;
    at<uint32_t>(vehicle, 0x1cc) = 0;
    const float zero = 0.0f;
    at<float>(vehicle, 0x1e4) = zero;
    at<float>(vehicle, 0x1e0) = zero;
    at<float>(vehicle, 0x1dc) = zero;
    const uint32_t handle = at<uint32_t>(ent, 0x1a0);
    const uint32_t index = handle & 0xfff;
    wd(0) = edi;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8) ||
        !title_global<uint32_t>(0x0097a968 + index * 8))
        return;
    if (!at<uint8_t>(ent, 3)) return;
    wd(0x10) = title_global<uint32_t>(0x0097a968 + index * 8);
    uint8_t* const driver = pointer<uint8_t>(wd(0x10));
    uint8_t* const client = at<uint8_t*>(driver, 0x23c);
    const uint32_t client_w = address_of(client);
    if (at<uint32_t>(client, 0xc0) & 0x400000) return;
    const int32_t refire = at<int32_t>(vehicle, 0x180) - elapsed;
    at<int32_t>(vehicle, 0x180) = refire;
    if ((at<uint8_t>(client, 0x528) & 1) && !(at<uint32_t>(at<uint8_t*>(driver, 0x23c), 0x2c) & 0x4000) && refire <= 0) {
        bool notify = true;
        if (!at<uint32_t>(vehicle, 0x1a4)) {
            const uint8_t* const def = weapon_def(at<uint8_t>(ent, 3));
            if (at<uint8_t>(vehicle, 0x19c)) {
                notify = false;
            } else {
                at<float>(vehicle, 0x198) =
                    (static_cast<float>(at<int32_t>(def, 0x5dc)) * at<float>(def, 0x84c)) * title_global<float>(0x0083eb4c) +
                    at<float>(vehicle, 0x198);
            }
        }
        if (notify) {
            const uint32_t args[1] = {title_global<uint32_t>(0x0097a830)};  // "turret_fire"
            veh_call_at(B, 0x0026095c, own_address(&Scr_Notify_entry), TitleRegs{0, veh_w, 0, args[0], ent_w, client_w}, 1, args);  // Scr_Notify
        }
    }
    at<uint32_t>(vehicle, 0x1c8) = 1;
    if (at<int32_t>(vehicle, 0x2cc) < 0) {
        const uint32_t args[3] = {1, 0x0083a0c4, title_global<uint32_t>(0x008daeb8)};
        veh_call_at(B, 0x00260983, 0x002938f0, TitleRegs{at<uint32_t>(vehicle, 0x2cc), veh_w, args[2], 0, ent_w, client_w}, 3,
                    args);  // __break
    }
    {
        const uint32_t args[3] = {ent_w, at<uint32_t>(vehicle, 0x2cc), B + 0x40};
        veh_call_at(B, 0x00260998, own_address(&G_DObjGetWorldTagMatrix), TitleRegs{args[1], veh_w, 0, B + 0x40, ent_w, client_w}, 3,
                    args);  // G_DObjGetWorldTagMatrix
    }
    if (at<int32_t>(vehicle, 0x2c4) < 0) {
        const uint32_t args[2] = {1, 0x0083a0a0};
        veh_call_at(B, 0x002609b1, 0x002938f0, TitleRegs{at<uint32_t>(vehicle, 0x2c4), veh_w, 0, 0, ent_w, client_w}, 2,
                    args);  // __break
    }
    {
        const uint32_t args[3] = {ent_w, at<uint32_t>(vehicle, 0x2c4), B + 0x40};
        veh_call_at(B, 0x002609c6, own_address(&G_DObjGetWorldTagMatrix), TitleRegs{args[1], veh_w, B + 0x40, args[1], ent_w, client_w}, 3,
                    args);  // G_DObjGetWorldTagMatrix
    }
    veh_call_at(B, 0x002609d8, own_address(&AnglesToForward_entry), TitleRegs{0, veh_w, 0, 0, B + 0x20, client_w + 0x9c});  // AnglesToForward
    if (wd(0x10) == at<uint32_t>(title_global<uint8_t*>(0x0095f534), 4)) {
        const uint32_t args[2] = {0x13, B + 0x14};
        veh_call_at(B, 0x002609f8, own_address(&VM_Call_entry), TitleRegs{0, veh_w, wd(0x10), title_global<uint32_t>(0x0093f7f8), B + 0x20,
                                                         client_w + 0x9c},
                    2, args);  // VM_Call(cgvm, 0x13): the view origin
    } else {
        fl(0x14) = fl(0x70);
        fl(0x18) = fl(0x74);
        fl(0x1c) = fl(0x78);
    }
    const float range = title_global<float>(0x0083f190);  // 10240
    const float ex = fl(0x20) * range + fl(0x14);
    const float ey = fl(0x24) * range + fl(0x18);
    const float ez = fl(0x28) * range + fl(0x1c);
    at<float>(vehicle, 0x1d0) = ex;
    at<float>(vehicle, 0x1d4) = ey;
    at<float>(vehicle, 0x1d8) = ez;
    bool valid;
    {
        const uint32_t args[1] = {ent_w};
        valid = static_cast<uint8_t>(veh_call_at(B, 0x00260a7a, own_address(&G_EntityInValidCell), TitleRegs{0, veh_w, 0, 0, ent_w, client_w + 0x9c}, 1,
                                                 args)) != 0;  // G_EntityInValidCell
    }
    if (!valid) {
        at<uint32_t>(vehicle, 0x1ac) = 1;
        return;
    }
    const float sx = fl(0x14), sy = fl(0x18), sz = fl(0x1c);
    fl(0x30) = ex;
    fl(0x34) = ey;
    fl(0x38) = ez;
    fl(0x3c) = ex;
    fl(0x20) = sx;
    fl(0x24) = sy;
    fl(0x28) = sz;
    fl(0x2c) = sx;
    {
        const uint32_t args[7] = {B + 0x40, B + 0x20, B + 0x30, at<uint32_t>(ent, 0x1a0), 0x0280e091, 0x008da6f0, 0};
        veh_call_at(B, 0x00260aff, own_address(&G_LocationalTrace_entry), TitleRegs{B + 0x40, veh_w, B + 0x30, B + 0x20, ent_w, client_w + 0x9c}, 7,
                    args);  // G_LocationalTrace
    }
    if (title_global<float>(0x0083eb20) > fl(0x60)) {
        at<float>(vehicle, 0x1d0) = fl(0x40);
        at<float>(vehicle, 0x1d4) = fl(0x44);
        at<float>(vehicle, 0x1d8) = fl(0x48);
    }
}

// The fire record (B + 0x30) and the angle temporaries sit at the original's frame addresses (gap entry;
// B = aligned base - 0xe0); every callee runs at the original's ESP.
PLATFORM_ASM_ENTRY(Vehicle_FireTurretWeapon_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_FireTurretWeapon) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Vehicle_FireTurretWeapon(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // The turret weapon (+0x1a4) sets the refire delay (+0x184 = def +0x5dc). The flash tag (+0x2c8, else
    // +0x2cc) gives the muzzle and axes. With a driver, no lock (+0x1ac) and an aim point (+0x1d0, not all
    // zero), the direction turns from the tag's forward toward the aim point by at most 10 degrees in pitch
    // and yaw; else the tag's forward. With +0x1ac the muzzle moves back by +0x1a8 along it. Projectile
    // weapons (def +0x94) fire one (speed 10); else a bullet, the fire event 0xa4 and heat (+0x198).
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0xe0;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    wd(8) = ebx;
    wd(4) = esi;
    wd(0) = edi;
    uint8_t* const ent = pointer<uint8_t>(*pointer<uint32_t>(E + 4));
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = weapon_def(at<uint32_t>(vehicle, 0x1a4));
    const uint32_t ent_w = address_of(ent), veh_w = address_of(vehicle), def_w = address_of(def);
    at<uint32_t>(vehicle, 0x184) = at<uint32_t>(def, 0x5dc);
    wd(0x6c) = def_w;
    wd(0x70) = def_w;
    int32_t tag = at<int32_t>(vehicle, 0x2c8);
    if (tag < 0) tag = at<int32_t>(vehicle, 0x2cc);
    {
        const uint32_t args[3] = {ent_w, static_cast<uint32_t>(tag), B + 0x80};
        veh_call_at(B, 0x002705ba, own_address(&G_DObjGetWorldTagMatrix), TitleRegs{args[1], veh_w, B + 0x80, at<uint32_t>(def, 0x5dc), def_w, ent_w}, 3,
                    args);  // G_DObjGetWorldTagMatrix
    }
    const float ox = fl(0xb0), oy = fl(0xb4), oz = fl(0xb8);
    fl(0x54) = ox;
    fl(0x58) = oy;
    fl(0x5c) = oz;
    const float zero = 0.0f;
    bool aimed = false;
    if (at<uint32_t>(ent, 0x1a0) && !at<uint32_t>(vehicle, 0x1ac)) {
        aimed = at<float>(vehicle, 0x1d0) != zero || at<float>(vehicle, 0x1d4) != zero || at<float>(vehicle, 0x1d8) != zero;
    }
    if (aimed) {
        fl(0x60) = at<float>(vehicle, 0x1d0) - ox;
        fl(0x64) = at<float>(vehicle, 0x1d4) - oy;
        fl(0x68) = at<float>(vehicle, 0x1d8) - oz;
        veh_call_at(B, 0x0027067f, own_address(&VectorNormalize_entry), TitleRegs{0, veh_w, 0, 0, B + 0x60, ent_w});  // VectorNormalize
        veh_call_at(B, 0x0027068f, own_address(&vectoangles_entry), TitleRegs{0, veh_w, 0, 0, B + 0x80, B + 0x74});  // vectoangles (tag forward)
        veh_call_at(B, 0x0027069c, own_address(&vectoangles_entry), TitleRegs{0, veh_w, 0, 0, B + 0x60, B + 0x24});  // vectoangles (aim)
        // Both angle sets as vec4 temporaries (fourth word = x); the difference, then the clamped turn.
        const float ax = fl(0x24), ay = fl(0x28), az = fl(0x2c);
        const float tx = fl(0x74), ty = fl(0x78), tz = fl(0x7c);
        fl(0xc0) = ax;
        fl(0xc4) = ay;
        fl(0xc8) = az;
        fl(0xcc) = ax;
        fl(0x10) = tx;
        fl(0x14) = ty;
        fl(0x18) = tz;
        fl(0x1c) = tx;
        fl(0xd0) = tx;
        fl(0xd4) = ty;
        fl(0xd8) = tz;
        fl(0xdc) = tx;
        {
            const uint32_t args[1] = {B + 0x24};
            veh_call_at(B, 0x00270724, own_address(&AnglesNormalize180_entry), TitleRegs{0, veh_w, 0, B + 0x24, B + 0xc0, B + 0x10}, 1,
                        args);  // AnglesNormalize180 (aim - forward)
        }
        const float low = title_global<float>(0x0083ed10), high = title_global<float>(0x0083eb98);  // -10, 10
        float dx = fl(0x24);
        if (low > dx)
            dx = low;
        else if (dx > high)
            dx = high;
        float dy = fl(0x28);
        if (low > dy)
            dy = low;
        else if (dy > high)
            dy = high;
        fl(0x1c) = dx;
        fl(0x10) = dx;
        fl(0x18) = zero;
        fl(0x14) = dy;
        fl(0xc0) = dx;
        fl(0xc4) = dy;
        fl(0xc8) = zero;
        fl(0xcc) = dx;
        {
            const uint32_t args[1] = {B + 0x74};
            veh_call_at(B, 0x002707b7, own_address(&AnglesNormalize180_entry), TitleRegs{B + 0x74, veh_w, 0, B + 0x24, B + 0xc0, B + 0xd0}, 1,
                        args);  // AnglesNormalize180 (forward + turn)
        }
        veh_call_at(B, 0x002707c7, own_address(&AnglesToForward_entry), TitleRegs{B + 0x74, veh_w, 0, B + 0x24, B + 0x60, B + 0x74});  // AnglesToForward
    } else {
        fl(0x60) = fl(0x80);
        fl(0x64) = fl(0x84);
        fl(0x68) = fl(0x88);
    }
    const float fx = fl(0x60), fy = fl(0x64), fz = fl(0x68);
    fl(0x3c) = fl(0x90);
    fl(0x40) = fl(0x94);
    fl(0x44) = fl(0x98);
    fl(0x48) = fl(0xa0);
    fl(0x4c) = fl(0xa4);
    fl(0x30) = fx;
    fl(0x34) = fy;
    fl(0x38) = fz;
    fl(0x50) = fl(0xa8);
    if (at<uint32_t>(vehicle, 0x1ac)) {
        fl(0x54) = (zero - at<float>(vehicle, 0x1a8)) * fx + ox;
        fl(0x58) = (zero - at<float>(vehicle, 0x1a8)) * fy + oy;
        fl(0x5c) = (zero - at<float>(vehicle, 0x1a8)) * fz + oz;
    } else {
        fl(0x54) = ox;
        fl(0x58) = oy;
        fl(0x5c) = oz;
    }
    if (at<uint32_t>(pointer<uint8_t>(wd(0x6c)), 0x94)) {
        const uint32_t args[3] = {0, 0x41200000, 1};  // speed 10
        veh_call_at(B, 0x00270980, own_address(&Weapon_FireProjectile_entry), TitleRegs{ent_w, veh_w, wd(0x6c), 0, B + 0x30, ent_w}, 3,
                    args);  // Weapon_FireProjectile
        return;
    }
    {
        const uint32_t spread = at<uint32_t>(def, 0x5b8);
        const uint32_t args[5] = {ent_w, title_global<uint32_t>(0x008feff0), spread, ent_w, 0};
        veh_call_at(B, 0x0027092a, own_address(&Weapon_FireBullet_entry), TitleRegs{B + 0x30, veh_w, wd(0x6c), spread, def_w, ent_w}, 5,
                    args);  // Weapon_FireBullet
    }
    {
        const uint32_t args[2] = {0xa4, 0};
        veh_call_at(B - 0x14, 0x0027093a, own_address(&CG_FireWeapon_entry), TitleRegs{ent_w, veh_w, ent_w, 0, def_w, ent_w}, 2,
                    args);  // CG_FireWeapon
    }
    at<float>(vehicle, 0x198) =
        (static_cast<float>(at<int32_t>(def, 0x5dc)) * at<float>(def, 0x84c)) * title_global<float>(0x0083eb4c) +
        at<float>(vehicle, 0x198);
}

// Player_GetUseList runs at the original's ESP with the list at the original's frame address (gap entry;
// B = entry - 0x2a44, list B + 0x44): its trace reads stack words the caller's depth decides.
PLATFORM_ASM_ENTRY(Player_UpdateCursorHints_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Player_UpdateCursorHints) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Player_UpdateCursorHints(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    const uint32_t B = entry_esp - 0x2a44;
    *pointer<uint32_t>(B + 0xc) = ebx;
    *pointer<uint32_t>(B + 8) = ebp;
    *pointer<uint32_t>(B + 4) = edi;
    uint8_t* const ent = pointer<uint8_t>(*pointer<uint32_t>(entry_esp + 4));
    // The use hint (client +0x47c), its entity (+0x480), hint string (+0x484) and use entity (+0x4c0), cleared
    // first. Nothing while dead, reloading a clip weapon (def +0x94 = 1, +0x6e4, ammo below +0x618 and not 0,
    // weapon bit in +0x3f0), in a vehicle seat (+0x297), with 0x0095bf9c = 4, with client flag 0x100000, or
    // prone (+0x2c bit 0x20) with a mountable weapon (def +0x98 = 3). Mountable weapons look for a mount
    // trigger 25 units ahead (hint 7, string def +0x844 when +0x840 is set). Then the first usable entity of
    // the use list decides: the player's own (9 with +0x4b4 bit 8, not +0x2c bit 0x10); sentients (11 for
    // squad types 2/3; string = their +0x1380 when not negative); trigger_use (its +0xc0, string +5); turrets
    // (6, usable by the player, else the next entry) and vehicles (2, likewise); items by type (weapon 0xb +
    // index, or 0x8b + index when held; ammo 0x8b + index; 8; 0x8c; 8); doors closed or opening the right way
    // (4, 5 when locked by +0x3b4). An entity hint (+0xc0 > 0) overrides a nonzero hint; +0x28c is a fixed
    // string. Without a hint the use entity is cleared.
    uint8_t* const ps = at<uint8_t*>(ent, 0x23c);
    at<uint32_t>(ps, 0x47c) = 0;
    at<uint32_t>(ps, 0x480) = 0;
    at<uint32_t>(ps, 0x4c0) = 0;
    if (at<int32_t>(ent, 0x350) <= 0) return;
    *pointer<uint32_t>(B) = esi;
    {
        const uint8_t* const def = weapon_def(at<uint32_t>(ps, 0x80));
        if (at<int32_t>(def, 0x94) == 1 && at<uint32_t>(def, 0x6e4) && at<int32_t>(ps, 0x3c) < at<int32_t>(def, 0x618) &&
            at<int32_t>(ps, 0x3c) != 0 && Com_BitCheck(0, at<int32_t>(ps, 0x80), &at<uint32_t>(ps, 0x3f0)))
            return;
    }
    Player_UpdateLookAtActorInfo(ent);
    if (at<uint8_t>(ent, 0x297)) return;
    if (title_global<int32_t>(0x0095bf9c) == 4) return;
    at<uint32_t>(ps, 0x47c) = 0;
    at<uint32_t>(ps, 0x480) = 0;
    at<int32_t>(ps, 0x484) = -1;
    int32_t text = -1;
    const uint8_t* const client = at<uint8_t*>(ent, 0x23c);
    const int32_t weapon_class = at<int32_t>(weapon_def(at<uint32_t>(client, 0x80)), 0x98);
    int32_t hint = 0;
    uint32_t hint_ent = 0;
    if ((at<uint8_t>(client, 0x2c) & 0x20) && weapon_class == 3) return;
    if (at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0xc0) & 0x100000) return;
    const uint32_t* const list = pointer<uint32_t>(B + 0x44);
    int32_t count;
    {
        const uint32_t args[2] = {address_of(ent), B + 0x44};
        count = static_cast<int32_t>(veh_call_at(B, 0x00267f22, own_address(&Player_GetUseList_entry),
                                                 TitleRegs{address_of(client), address_of(ps), B + 0x44, 0,
                                                           static_cast<uint32_t>(weapon_class), 0},
                                                 2, args));  // Player_GetUseList
    }
    if (weapon_class == 3) {
        float angles[3];
        angles[1] = at<float>(at<uint8_t*>(ent, 0x23c), 0xa0);
        int32_t mount = 0;
        angles[0] = 0.0f;
        angles[2] = 0.0f;
        float forward[3];
        AnglesToForward(angles, forward);
        const float reach = title_global<float>(0x0083ebc4);  // 25
        float point[3];
        point[0] = forward[0] * reach + at<float>(ent, 0x140);
        point[1] = forward[1] * reach + at<float>(ent, 0x144);
        point[2] = (forward[2] * reach + at<float>(ent, 0x148)) + title_global<float>(0x0083eb20);
        if (Player_FindMountTrigger(point, &mount)) {
            hint_ent = static_cast<uint32_t>(mount);
            const uint8_t* const def = weapon_def(at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0x80));
            hint = 7;
            if (*at<const char*>(def, 0x840)) text = at<int32_t>(def, 0x844);
        }
        at<int32_t>(ps, 0x47c) = hint;
        at<uint32_t>(ps, 0x480) = hint_ent;
        at<int32_t>(ps, 0x484) = text;
    }
    if (count == 0) return;
    bool fixed_text = false;
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const e = pointer<uint8_t>(list[i * 2]);
        at<uint32_t>(ps, 0x4c0) = at<uint32_t>(e, 0x224);
        bool common = true;
        if (e == at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8)) {
            if ((at<uint8_t>(ps, 0x4b4) & 8) && !(at<uint8_t>(ps, 0x2c) & 0x10)) hint = 9;
            common = false;
        } else if (at<uint32_t>(e, 0x240)) {
            const uint8_t* const squad = at<uint8_t*>(e, 0x244);
            if (squad && (at<int32_t>(squad, 4) == 2 || at<int32_t>(squad, 4) == 3)) hint = 0xb;
            const int16_t name = at<int16_t>(at<uint8_t*>(e, 0x240), 0x1380);
            if (name >= 0) text = name;
            common = false;
        } else {
            const uint8_t type = e[0];
            if (type == 0) {
                if (at<uint32_t>(e, 0x260) == title_global<uint32_t>(0x0097a81c)) {  // trigger_use
                    hint = at<int32_t>(e, 0xc0);
                    if (hint && e[5] != 0xff) text = e[5];
                }
            } else if (type == 0xa) {
                if (!Turret_IsUsableBy(ent, 0, e)) continue;
                const uint8_t* const def = weapon_def(e[3]);
                hint = 6;
                if (*at<const char*>(def, 0x840)) text = at<int32_t>(def, 0x844);
            } else if (type == 0xe) {
                if (!G_PlayerCanUseTurret(0, e, ent)) continue;
                const uint8_t* const def = weapon_def(e[3]);
                hint = 2;
                if (*at<const char*>(def, 0x840)) text = at<int32_t>(def, 0x844);
            } else if (type == 2) {
                const int32_t index = static_cast<int32_t>(at<uint32_t>(e, 0x3b0) - 0x0096bd10u) / 0x34;
                const uint8_t* const item = pointer<uint8_t>(static_cast<uint32_t>(index * 0x34) + 0x0096bd10u);
                switch (at<int32_t>(item, 0x24)) {
                case 1: {
                    const int32_t w = at<int32_t>(item, 0x28);
                    hint = Com_BitCheck(0, w, &at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0x3f0)) ? w + 0x8b : w + 0xb;
                    break;
                }
                case 2:
                    hint = at<int32_t>(item, 0x28) + 0x8b;
                    break;
                case 3:
                case 5:
                    hint = 8;
                    break;
                case 4:
                    hint = 0x8c;
                    break;
                default:
                    break;
                }
            } else if (type == 4) {
                bool door = false;
                const uint8_t state = e[0x298];
                if (at<uint32_t>(e, 0x260) == title_global<uint32_t>(0x0097a6e0)) {  // func_door_rotating
                    door = state == 7 || (state == 8 && static_cast<int8_t>(e[0x2a4]) < 0);
                } else if (at<uint32_t>(e, 0x260) == title_global<uint32_t>(0x0097a6dc)) {  // func_door
                    door = state == 0 || (state == 1 && static_cast<int8_t>(e[0x2a4]) < 0);
                }
                if (door) hint = at<uint32_t>(e, 0x3b4) ? 5 : 4;
            }
        }
        if (common) {
            if (at<int32_t>(e, 0xc0) > 0 && hint) hint = at<int32_t>(e, 0xc0);
            if (const uint32_t fixed = at<uint32_t>(e, 0x28c)) {
                at<uint32_t>(ps, 0x484) = fixed;
                fixed_text = true;
            }
        }
        at<uint32_t>(ps, 0x480) = hint_ent;
        at<int32_t>(ps, 0x47c) = hint;
        if (!fixed_text) at<int32_t>(ps, 0x484) = text;
        if (!hint) at<uint32_t>(ps, 0x4c0) = 0;
        return;
    }
}
namespace {

// x, y, z and x again: the original's vec4 copies of a point (fourth word = x).
void put_point4(uint32_t address, const float* v) {
    float* const d = pointer<float>(address);
    d[0] = v[0];
    d[1] = v[1];
    d[2] = v[2];
    d[3] = v[0];
}

// The impact direction reflected about the normal: dir - 2 * (dir . n) * n, the dot summed z, y, x.
void reflect_about(float* dir, const float* normal) {
    const float d = ((normal[2] * dir[2] + normal[1] * dir[1]) + normal[0] * dir[0]) * title_global<float>(0x0083ec5c);
    const float x = normal[0] * d + dir[0];
    const float y = normal[1] * d + dir[1];
    const float z = normal[2] * d + dir[2];
    dir[0] = x;
    dir[1] = y;
    dir[2] = z;
}

// The penetration step: start = hit point + (0.25 / -(n . v)) * v (0 when -(n . v) is below 0.125).
void penetrate_start(float* start, const float* v, const float* normal, const float* hit) {
    const float zero = 0.0f;
    const float dot = (normal[2] * v[2] + normal[1] * v[1]) + normal[0] * v[0];
    const float neg = zero - dot;
    float s;
    if (neg >= title_global<float>(0x0083ec68))
        s = title_global<float>(0x0083ec24) / neg;
    else
        s = zero;
    start[0] = s * v[0] + hit[0];
    const float y = s * v[1] + hit[1];
    const float z = s * v[2] + hit[2];
    start[1] = y;
    start[2] = z;
}

uint8_t* entity_of_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        return title_global<uint8_t*>(0x0097a968 + index * 8);
    return nullptr;
}

// |u . v| for u = the fire direction turned flat by 90 degrees (normalised) and v = hit - origin: how far
// to the side of a client the hit is. u at B + 0x18, v at B + 0x24/0x28 (VectorNormalize at the original's ESP).
float side_offset(uint32_t B, uint32_t v_off, const float* fire, const uint8_t* client, const float* hit, uint32_t ret,
                  const TitleRegs& regs) {
    float* const v = pointer<float>(B + v_off);
    float* const u = pointer<float>(B + 0x18);
    v[0] = hit[0] - at<float>(client, 0);
    v[1] = hit[1] - at<float>(client, 4);
    v[2] = hit[2] - at<float>(client, 8);
    u[0] = fire[1];
    const float zero = 0.0f;
    u[1] = zero - fire[0];
    u[2] = zero;
    TitleRegs r = regs;
    r.esi = B + 0x18;
    veh_call_at(B, ret, own_address(&VectorNormalize_entry), r);  // VectorNormalize
    const float d = (u[2] * v[2] + u[1] * v[1]) + u[0] * v[0];
    uint32_t bits;
    std::memcpy(&bits, &d, 4);
    bits &= 0x7fffffffu;
    float a;
    std::memcpy(&a, &bits, 4);
    return a;
}

}  // namespace

// Recursive; the trace and the effect callees read their points' fourth words: the frame sits at the
// original's addresses (gap entry; B = aligned base - 0x110), every callee at the original's ESP.
PLATFORM_ASM_ENTRY(Bullet_Trace_r_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x40000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Bullet_Trace_r) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Bullet_Trace_r(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl (pass, attacker, start, end, a4, depth, fire, a7, a8): Weapon_BulletTrace_r without damage or
    // actor events (only caller Bullet_Fire: dead). Traces (contents 0x280e031, hit-location table by def
    // +0x6d0) up to depth 12: impact effect (CG_Bullet; flesh 0x700000 for damageable +0x29c clients and
    // squad members) unless the surface has flag 4 or the hit is a squad mate (tracer only); a miss draws a
    // tracer. Penetrable hits (byte +0x28 bit 0x10) go on from just past the hit point (start written,
    // depth + 1, a8 = 0). A damageable hit continues through the attacker's linked entity, squad mates and
    // clients hit more than 8 units off the line.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0x110;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    wd(8) = ebx;
    wd(4) = esi;
    wd(0) = edi;
    uint32_t* const arg = pointer<uint32_t>(E + 4);
    const uint32_t pass = arg[0];
    uint8_t* const attacker = pointer<uint8_t>(arg[1]);
    float* const start = pointer<float>(arg[2]);
    const float* const end = pointer<float>(arg[3]);
    const float* const fire = pointer<float>(arg[6]);
    const uint32_t att_w = arg[1], start_w = arg[2], end_w = arg[3], fire_w = arg[6];
    for (;;) {
        wd(0x60) = 0;
        wd(0x64) = 0;
        if (static_cast<int32_t>(arg[5]) > 12) return;
        const uint8_t* const def = at<uint8_t*>(fire, 0x3c);
        const uint32_t table = at<uint32_t>(def, 0x6d0) ? 0x008da704u : 0x008da6f0u;
        put_point4(B + 0x80, end);
        put_point4(B + 0x100, end);
        put_point4(B + 0xa0, start);
        put_point4(B + 0xc0, start);
        {
            const uint32_t args[7] = {B + 0x30, B + 0xc0, B + 0x100, pass, 0x0280e031, table, arg[8]};
            veh_call_at(B, 0x0026f18f, own_address(&G_LocationalTrace_entry), TitleRegs{B + 0x30, start_w, B + 0x100, B + 0xc0, fire_w, end_w}, 7,
                        args);  // G_LocationalTrace
        }
        wd(0x14) = address_of(entity_of_handle(wd(0x60)));
        const uint32_t weapon = at<uint32_t>(def, 0);
        if (title_global<float>(0x0083eb20) > fl(0x50)) {
            fl(0x24) = end[0] - start[0];
            fl(0x28) = end[1] - start[1];
            fl(0x2c) = end[2] - start[2];
            veh_call_at(B, 0x0026f20f, own_address(&VectorNormalize_entry), TitleRegs{wd(0x14), start_w, wd(0x60), wd(0x60) >> 12, B + 0x24, end_w});  // VectorNormalize
            reflect_about(&fl(0x24), &fl(0x40));
            bool tracer = (wd(0x54) & 4) != 0;
            if (!tracer) {
                uint8_t* const hit = pointer<uint8_t>(wd(0x14));
                if (hit) {
                    const uint8_t* const squad = at<uint8_t*>(hit, 0x244);
                    if (squad && at<uint32_t>(attacker, 0x240) &&
                        at<int32_t>(squad, 4) == at<int32_t>(at<uint8_t*>(attacker, 0x244), 4)) {
                        tracer = true;
                    } else if ((at<uint32_t>(hit, 0x23c) || (squad && !wd(0x54))) && at<uint32_t>(hit, 0x29c)) {
                        wd(0x54) = 0x700000;
                    }
                }
            }
            if (!tracer) {
                const uint32_t args[2] = {B + 0x30, static_cast<uint32_t>((static_cast<int32_t>(wd(0x54)) >> 20) & 0x1f)};
                veh_call_at(B, 0x0026f2ff, own_address(&CG_Bullet_entry), TitleRegs{B + 0x40, start_w, att_w, B + 0x30, fire_w, weapon}, 2,
                            args);  // CG_Bullet
            } else {
                put_point4(B + 0xb0, start);
                put_point4(B + 0xf0, start);
                const uint32_t args[2] = {B + 0xf0, B + 0x30};
                veh_call_at(B, 0x0026f3ea, own_address(&CG_MaybeSpawnTracer_entry), TitleRegs{B + 0xf0, start_w, address_of(def), B + 0x30, weapon, end_w}, 2,
                            args);  // CG_MaybeSpawnTracer
            }
        } else {
            put_point4(B + 0x70, end);
            put_point4(B + 0xd0, end);
            put_point4(B + 0x90, start);
            put_point4(B + 0xe0, start);
            const uint32_t args[2] = {B + 0xe0, B + 0xd0};
            veh_call_at(B, 0x0026f3ea, own_address(&CG_MaybeSpawnTracer_entry), TitleRegs{B + 0xe0, start_w, address_of(def), B + 0xd0, weapon, end_w}, 2,
                        args);  // CG_MaybeSpawnTracer
        }
        if (!(at<uint8_t>(pointer<uint8_t>(B), 0x58) & 0x10)) break;
        fl(0x18) = end[0] - start[0];
        fl(0x1c) = end[1] - start[1];
        fl(0x20) = end[2] - start[2];
        veh_call_at(B, 0x0026f430, own_address(&VectorNormalize_entry), TitleRegs{0, start_w, 0, 0, B + 0x18, end_w});  // VectorNormalize
        penetrate_start(start, &fl(0x18), &fl(0x40), &fl(0x30));
        *pointer<float>(E + 0x24) = 0.0f;
        arg[5] = arg[5] + 1;
    }
    uint8_t* const hit = pointer<uint8_t>(wd(0x14));
    if (!hit || !at<uint32_t>(hit, 0x29c)) return;
    bool go_on = false;
    const uint32_t sentient = at<uint32_t>(attacker, 0x240);
    const uint8_t* const linked = at<uint8_t*>(attacker, 0x3cc);
    if (sentient && linked && at<uint8_t*>(linked, 0) == hit) {
        go_on = true;
    } else if (at<uint8_t*>(hit, 0x244) && sentient &&
               at<int32_t>(at<uint8_t*>(hit, 0x244), 4) == at<int32_t>(at<uint8_t*>(attacker, 0x244), 4)) {
        go_on = true;
    } else {
        const uint8_t* const client = at<uint8_t*>(hit, 0x23c);
        if (!client) return;
        const float off = side_offset(B, 0x24, fire, client, &fl(0x30), 0x0026f5bf,
                                      TitleRegs{0, address_of(hit), 0, 0, 0, end_w});
        fl(0x14) = off;
        if (!(off > title_global<float>(0x0083ed78))) return;  // 8
        go_on = true;
    }
    if (!go_on) return;
    fl(0x18) = fl(0x30);
    fl(0x1c) = fl(0x34);
    fl(0x20) = fl(0x38);
    const uint32_t args[9] = {at<uint32_t>(hit, 0x224), att_w, B + 0x18, end_w, arg[4], arg[5] + 1, fire_w, arg[7], 0};
    // The nested call runs our C directly at the original's nested entry (B - 0x28): a nested gap entry would
    // leave its own frames in the dead stack.
    for (uint32_t k = 0; k < 9; ++k) *pointer<uint32_t>(B - 0x24 + k * 4) = args[k];
    *pointer<uint32_t>(B - 0x28) = 0x0026f65a;
    Bullet_Trace_r(B - 0x28, at<uint32_t>(hit, 0x224), at<uint32_t>(hit, 0x224), end_w, E - 4);
}

// Recursive; the trace and the effect callees read their points' fourth words: the frame sits at the
// original's addresses (gap entry; B = aligned base - 0x180), every callee at the original's ESP.
PLATFORM_ASM_ENTRY(Weapon_BulletTrace_r_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x40000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Weapon_BulletTrace_r) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Weapon_BulletTrace_r(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl (pass, attacker, start, end, damage, depth, fire, a7, a8): one bullet segment, up to depth 12.
    // Means of death 2 with damage flag 0x40 for weapons with hit locations (def +0x6d0, table 0x008da704),
    // else 1. Traces contents 0x280e031; a squad attacker sends the point event 0xa at the start (to its team's
    // enemy mask) and the line event 0x10 along the shot; trigger damage along it. A hit: impact effect
    // (CG_Bullet from the pass entity, else the attacker; flesh 0x700000 for damageable +0x29c non-vehicle
    // clients and squad members; none on clients) unless surface flag 4 or a squad mate (tracer only); a miss
    // draws a tracer. Penetrable hits (byte +0x28 bit 0x10) go on from just past the hit point (start written,
    // depth + 1, a8 = 0). A damageable hit (+0x29c) passes through the attacker's linked entity, squad mates
    // and clients hit more than 8 units off the line; else the whiz-by (clients), G_Damage, and for flag 0x40
    // on a squad target half the damage goes on through it.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0x180;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    wd(8) = ebx;
    wd(4) = esi;
    wd(0) = edi;
    uint32_t* const arg = pointer<uint32_t>(E + 4);
    const uint32_t pass = arg[0];
    uint8_t* const attacker = pointer<uint8_t>(arg[1]);
    float* const start = pointer<float>(arg[2]);
    const float* const end = pointer<float>(arg[3]);
    const float* const fire = pointer<float>(arg[6]);
    const uint32_t att_w = arg[1], start_w = arg[2], end_w = arg[3], fire_w = arg[6];
    uint32_t edi_now = edi;
    for (;;) {
        wd(0x70) = 0;
        wd(0x74) = 0;
        wd(0x38) = 0;
        if (static_cast<int32_t>(arg[5]) > 12) return;
        const uint8_t* const def = at<uint8_t*>(fire, 0x3c);
        const uint32_t located = at<uint32_t>(def, 0x6d0);
        if (located) {
            wd(0x3c) = 2;
            wd(0x38) = 0x40;
        } else {
            wd(0x3c) = 1;
        }
        const uint32_t table = located ? 0x008da704u : 0x008da6f0u;
        put_point4(B + 0xc0, end);
        put_point4(B + 0x150, end);
        put_point4(B + 0xa0, start);
        put_point4(B + 0x110, start);
        {
            const uint32_t args[7] = {B + 0x40, B + 0x110, B + 0x150, pass, 0x0280e031, table, arg[8]};
            veh_call_bp(E - 4, B, 0x0026fdc3, own_address(&G_LocationalTrace_entry), TitleRegs{B + 0x150, start_w, B + 0x110, B + 0x40, end_w, edi_now}, 7,
                        args);  // G_LocationalTrace
        }
        edi_now = att_w;
        if (attacker && at<uint8_t*>(attacker, 0x244)) {
            const int32_t team = at<int32_t>(at<uint8_t*>(attacker, 0x244), 4);
            wd(0xfc) = 0;
            wd(0x108) = 0;
            wd(0x10c) = 0;
            wd(0x104) = 1;
            wd(0x100) = 2;
            const uint32_t enemy = 1u << (wd(0xfc + static_cast<uint32_t>(team) * 4) & 31);
            put_point4(B + 0xd0, start);
            put_point4(B + 0x170, start);
            {
                const uint32_t args[4] = {att_w, 0xa, B + 0x170, 0};
                veh_call_bp(E - 4, B, 0x0026fe6f, own_address(&Actor_SendPointEvent_entry), TitleRegs{enemy, start_w, B + 0x170, 0, enemy, att_w}, 4,
                            args);  // Actor_SendPointEvent
            }
            uint32_t mask = enemy;
            uint32_t eax_now = B + 0x40;
            if (at<uint32_t>(attacker, 0x23c)) {
                eax_now = at<uint32_t>(attacker, 0x244);
                mask = (1u << (static_cast<uint32_t>(at<int32_t>(at<uint8_t*>(attacker, 0x244), 4)) & 31)) | enemy;
            }
            const uint32_t args[6] = {att_w, 0x10, mask, start_w, B + 0x40, 0};
            veh_call_bp(E - 4, B, 0x0026fea5, own_address(&Actor_SendLineEvent_entry), TitleRegs{eax_now, start_w, 0, mask, enemy, att_w}, 6,
                        args);  // Actor_SendLineEvent
        }
        put_point4(B + 0xe0, start);
        put_point4(B + 0x160, start);
        {
            const uint32_t args[5] = {att_w, B + 0x160, B + 0x40, arg[4], wd(0x3c)};
            veh_call_bp(E - 4, B, 0x0026ff06, own_address(&G_CheckHitTriggerDamage), TitleRegs{B + 0x40, start_w, B + 0x160, arg[4], end_w, att_w}, 5,
                        args);  // G_CheckHitTriggerDamage
        }
        wd(0x24) = address_of(entity_of_handle(wd(0x70)));
        const uint32_t weapon = at<uint32_t>(def, 0);
        if (title_global<float>(0x0083eb20) > fl(0x60)) {
            fl(0x28) = end[0] - start[0];
            fl(0x2c) = end[1] - start[1];
            fl(0x30) = end[2] - start[2];
            veh_call_bp(E - 4, B, 0x0026ff89, own_address(&VectorNormalize_entry), TitleRegs{wd(0x24), start_w, wd(0x70), wd(0x70) >> 12, B + 0x28, att_w});  // VectorNormalize
            reflect_about(&fl(0x28), &fl(0x50));
            bool tracer = (at<uint8_t>(pointer<uint8_t>(B), 0x64) & 4) != 0;
            bool impact = true;
            if (!tracer) {
                uint8_t* const hit = pointer<uint8_t>(wd(0x24));
                if (hit) {
                    const uint32_t squad = at<uint32_t>(hit, 0x244);
                    if (squad && at<uint32_t>(attacker, 0x240) &&
                        at<int32_t>(pointer<uint8_t>(squad), 4) == at<int32_t>(at<uint8_t*>(attacker, 0x244), 4)) {
                        tracer = true;
                    } else {
                        bool flesh_check = at<uint32_t>(hit, 0x23c) != 0;
                        if (!flesh_check && squad && !wd(0x64)) flesh_check = true;
                        if (flesh_check) {
                            const uint32_t in_vehicle = veh_call_at(
                                B, 0x00270052, own_address(&G_ClientInVehicle), TitleRegs{squad, start_w, address_of(hit), 0, address_of(hit), att_w});  // G_ClientInVehicle
                            if (in_vehicle || !at<uint32_t>(hit, 0x29c)) {
                                impact = false;
                            } else {
                                wd(0x64) = 0x700000;
                            }
                        }
                        if (impact && at<uint32_t>(hit, 0x23c)) impact = false;
                    }
                }
                if (!tracer && impact) {
                    uint32_t shooter = veh_call_bp(E - 4, B, 0x00270086, own_address(&G_EntHandleToEnt),
                                                   TitleRegs{E + 4, start_w, 0, 0, wd(0x24), att_w});  // G_EntHandleToEnt
                    if (!shooter) shooter = att_w;
                    const uint32_t args[2] = {B + 0x40, static_cast<uint32_t>((static_cast<int32_t>(wd(0x64)) >> 20) & 0x1f)};
                    veh_call_bp(E - 4, B, 0x002700ae, own_address(&CG_Bullet_entry), TitleRegs{B + 0x50, start_w, shooter, 0, wd(0x24), weapon}, 2,
                                args);  // CG_Bullet
                }
            }
            if (tracer) {
                put_point4(B + 0x80, start);
                put_point4(B + 0x140, start);
                const uint32_t args[2] = {B + 0x140, B + 0x40};
                veh_call_bp(E - 4, B, 0x002701af, own_address(&CG_MaybeSpawnTracer_entry), TitleRegs{B + 0x40, start_w, B + 0x140, address_of(def), weapon, att_w}, 2,
                            args);  // CG_MaybeSpawnTracer
            }
        } else {
            put_point4(B + 0x90, end);
            put_point4(B + 0x120, end);
            put_point4(B + 0xb0, start);
            put_point4(B + 0x130, start);
            const uint32_t args[2] = {B + 0x130, B + 0x120};
            veh_call_bp(E - 4, B, 0x002701af, own_address(&CG_MaybeSpawnTracer_entry), TitleRegs{at<uint32_t>(fire, 0x3c), start_w, B + 0x120, B + 0x130, weapon, att_w}, 2,
                        args);  // CG_MaybeSpawnTracer
        }
        if (!(at<uint8_t>(pointer<uint8_t>(B), 0x68) & 0x10)) break;
        fl(0x18) = end[0] - start[0];
        fl(0x1c) = end[1] - start[1];
        fl(0x20) = end[2] - start[2];
        veh_call_bp(E - 4, B, 0x002701fb, own_address(&VectorNormalize_entry), TitleRegs{end_w, start_w, 0, 0, B + 0x18, att_w});  // VectorNormalize
        penetrate_start(start, &fl(0x18), &fl(0x50), &fl(0x40));
        *pointer<float>(E + 0x24) = 0.0f;
        arg[5] = arg[5] + 1;
    }
    uint8_t* const hit = pointer<uint8_t>(wd(0x24));
    if (!hit || !at<uint32_t>(hit, 0x29c)) return;
    const auto go_on = [&](uint32_t damage, uint32_t ebx_now, uint32_t esi_now) {
        fl(0x18) = fl(0x40);
        fl(0x1c) = fl(0x44);
        fl(0x20) = fl(0x48);
        const uint32_t args[9] = {at<uint32_t>(hit, 0x224), att_w, B + 0x18, end_w, damage, arg[5] + 1, fire_w, arg[7], 0};
        // The nested call runs our C directly at the original's nested entry (B - 0x28): a nested gap entry
        // would leave its own frames in the dead stack.
        for (uint32_t k = 0; k < 9; ++k) *pointer<uint32_t>(B - 0x24 + k * 4) = args[k];
        *pointer<uint32_t>(B - 0x28) = 0x002704a8;
        Weapon_BulletTrace_r(B - 0x28, ebx_now, esi_now, att_w, E - 4);
    };
    const uint32_t sentient = at<uint32_t>(attacker, 0x240);
    const uint8_t* const linked = at<uint8_t*>(attacker, 0x3cc);
    if (sentient && linked && at<uint8_t*>(linked, 0) == hit) {
        go_on(arg[4], start_w, at<uint32_t>(hit, 0x224));
        return;
    }
    if (at<uint8_t*>(hit, 0x244) && sentient &&
        at<int32_t>(at<uint8_t*>(hit, 0x244), 4) == at<int32_t>(at<uint8_t*>(attacker, 0x244), 4)) {
        go_on(arg[4], start_w, at<uint32_t>(hit, 0x224));
        return;
    }
    const uint8_t* const client = at<uint8_t*>(hit, 0x23c);
    if (client) {
        const float off = side_offset(B, 0x28, fire, client, &fl(0x40), 0x00270374,
                                      TitleRegs{fire_w, address_of(client), 0, 0, 0, att_w});
        fl(0x34) = off;
        if (off > title_global<float>(0x0083ed78)) {  // 8
            go_on(arg[4], address_of(client), B + 0x18);
            return;
        }
        const uint32_t args[3] = {pass, B + 0x40, at<uint32_t>(at<uint8_t*>(fire, 0x3c), 0)};
        veh_call_bp(E - 4, B, 0x00270413, own_address(&CG_BulletTracerAndWhizBy), TitleRegs{title_global<uint32_t>(0x008da6e0), fire_w, B + 0x40, pass, address_of(hit), att_w}, 3,
                    args);  // CG_BulletTracerAndWhizBy
    }
    {
        const uint32_t weapon = at<uint32_t>(at<uint8_t*>(fire, 0x3c), 0);
        const uint32_t args[9] = {address_of(hit), att_w, att_w, fire_w, arg[4], wd(0x38), wd(0x3c), wd(0x78), weapon};
        veh_call_bp(E - 4, B, 0x00270441, own_address(&G_Damage_entry), TitleRegs{B + 0x40, fire_w, wd(0x38), arg[4], address_of(hit), att_w}, 9,
                    args);  // G_Damage
    }
    if (!at<uint32_t>(hit, 0x244) || !(wd(0x38) & 0x40)) return;
    const int32_t half = static_cast<int32_t>(arg[4]) / 2;
    if (half > 0) go_on(static_cast<uint32_t>(half), fire_w, at<uint32_t>(hit, 0x224));
}

// The pmove block (B + 0x40, 0x140 bytes) and every local sit at the original's frame addresses (gap entry;
// B = aligned base - 0x180); every callee runs at the original's ESP.
PLATFORM_ASM_ENTRY(ClientThink_real_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ClientThink_real) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL ClientThink_real(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Q3's ClientThink_real for a connected client (+0x520 = 2): the command time (+0x524) clamped to level
    // time +200 / -1000 (none under 1 ms after the last; more than 100 behind pulls the state time up);
    // pmove_msec kept in 8..33 and the time snapped to it (pmove_fixed or client +0x57c); pm_type from
    // +0x584/+0x588/health/linked entity; gravity, speed, +0x5ec = +0x4f0 / 255. Pmove (command, old command
    // +0x550, trace mask 0x1810011 when dead, else the entity's +0x2a8; capsule traces, point contents) in a
    // valid cell (or noclip), frozen controls clearing the moves; a moving player that did not move is pushed
    // out of solids. Standing on another player under speed 200 slides off randomly and up 200. Then the
    // entity state, bounds, events, link, triggers, impacts, button history (+0x594..+0x5a8), the command's
    // view words (+0x5c0), use (button 0x40), pending touches, and death reload or the node claim.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0x180;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    auto by = [B](uint32_t off) -> uint8_t& { return *pointer<uint8_t>(B + off); };
    wd(8) = ebx;
    wd(4) = esi;
    wd(0) = edi;
    uint8_t* const ent = pointer<uint8_t>(*pointer<uint32_t>(E + 4));
    const uint32_t ent_w = address_of(ent);
    veh_call_at(B, 0x00271c48, own_address(&BlockTail80_Clear), TitleRegs{B + 0x40, ebx, 0, 0, esi, edi});  // BlockTail80_Clear
    uint8_t* const client = at<uint8_t*>(ent, 0x23c);
    const uint32_t cl_w = address_of(client);
    if (at<int32_t>(client, 0x520) != 2) return;
    int32_t* const cmd_time = &at<int32_t>(client, 0x524);
    const uint32_t cmd_w = cl_w + 0x524;
    wd(0x1c) = cmd_w;
    const int32_t level_time = title_global<int32_t>(0x0098540c);
    if (*cmd_time > level_time + 200) *cmd_time = level_time + 200;
    if (*cmd_time < title_global<int32_t>(0x0098540c) - 1000) *cmd_time = title_global<int32_t>(0x0098540c) - 1000;
    {
        const int32_t msec = *cmd_time - at<int32_t>(client, 0x20);
        if (msec < 1) {
            if (msec >= -100) return;
            at<int32_t>(client, 0x20) = *cmd_time - 100;
        }
    }
    int32_t step = title_global<int32_t>(0x00948ae4);  // pmove_msec
    if (step < 8) {
        const uint32_t args[3] = {0x008392f0, 0x00839454, 1};  // "pmove_msec", "8"
        veh_call_at(B, 0x00271cd7, own_address(&Cvar_Set2), TitleRegs{0, cl_w, static_cast<uint32_t>(step), 0, cmd_w, ent_w}, 3,
                    args);  // Cvar_Set2
        step = title_global<int32_t>(0x00948ae4);
    } else if (step > 0x21) {
        const uint32_t args[3] = {0x008392f0, 0x00838ca4, 1};  // "pmove_msec", "33"
        veh_call_at(B, 0x00271cd7, own_address(&Cvar_Set2), TitleRegs{0, cl_w, static_cast<uint32_t>(step), 0, cmd_w, ent_w}, 3,
                    args);  // Cvar_Set2
        step = title_global<int32_t>(0x00948ae4);
    }
    if (title_global<uint32_t>(0x0094be4c) || at<uint32_t>(client, 0x57c))  // pmove_fixed
        *cmd_time = ((*cmd_time + step - 1) / step) * step;
    if (at<uint32_t>(client, 0x58c))
        at<uint32_t>(client, 0x2c) |= 0x4000;
    else
        at<uint32_t>(client, 0x2c) &= 0xffffbfffu;
    if (at<uint32_t>(client, 0x584))
        at<int32_t>(client, 0x24) = 2;
    else if (at<uint32_t>(client, 0x588))
        at<int32_t>(client, 0x24) = 3;
    else if (at<int32_t>(client, 0x100) > 0)
        at<int32_t>(client, 0x24) = at<uint32_t>(ent, 0x3cc) ? 1 : 0;
    else
        at<int32_t>(client, 0x24) = (at<uint32_t>(ent, 0x3cc) ? 1 : 0) + 4;
    at<uint32_t>(client, 0x48) = title_global<uint32_t>(0x0094d454);  // g_gravity
    at<uint32_t>(client, 0x50) = title_global<uint32_t>(0x0095bd54);  // g_speed
    at<float>(client, 0x5ec) = at<float>(client, 0x4f0) / title_global<float>(0x0083eb8c);  // 255
    const uint32_t old_sequence = at<uint32_t>(client, 0xc4);
    for (uint32_t k = 0; k < 0x140; k += 4) wd(0x40 + k) = 0;
    wd(0x40) = cl_w;
    for (uint32_t k = 0; k < 0x2c; k += 4) wd(0x44 + k) = at<uint32_t>(client, 0x524 + k);
    for (uint32_t k = 0; k < 0x2c; k += 4) wd(0x70 + k) = at<uint32_t>(client, 0x550 + k);
    wd(0x18) = old_sequence;
    wd(0x9c) = at<int32_t>(client, 0x24) >= 4 ? 0x01810011u : at<uint32_t>(ent, 0x2a8);
    const uint32_t fixed = title_global<uint32_t>(0x0094be4c);
    wd(0x17c) = own_address(&SV_PointContents);  // SV_PointContents
    wd(0xa0) = 0;
    wd(0x170) = own_address(&G_TraceCapsule);  // G_TraceCapsule
    wd(0x174) = own_address(&G_TraceCapsule);
    wd(0x178) = own_address(&G_TraceCapsule);
    wd(0x16c) = title_global<uint32_t>(0x00948ae4);
    wd(0x168) = at<uint32_t>(client, 0x57c) | fixed;
    at<uint32_t>(client, 0x5b0) = at<uint32_t>(client, 0);
    at<uint32_t>(client, 0x5b4) = at<uint32_t>(client, 4);
    at<uint32_t>(client, 0x5b8) = at<uint32_t>(client, 8);
    bool valid;
    {
        const uint32_t args[1] = {ent_w};
        valid = static_cast<uint8_t>(veh_call_at(B, 0x00271e67, own_address(&G_EntityInValidCell),
                                                 TitleRegs{at<uint32_t>(client, 0), cl_w, 0, at<uint32_t>(client, 8), fixed, ent_w},
                                                 1, args)) != 0;  // G_EntityInValidCell
    }
    const uint8_t* const ps = pointer<uint8_t>(wd(0x40));
    if (valid || at<int32_t>(ps, 0x24) == 2) {
        if (title_global<uint32_t>(0x00927314)) {  // g_freezeControls
            by(0x5a) = 0;
            by(0x59) = 0;
            by(0x58) = 0;
            by(0x86) = 0;
            by(0x85) = 0;
            by(0x84) = 0;
            by(0x13) = 0;
        } else {
            by(0x13) = (by(0x58) || by(0x59)) ? 1 : 0;
        }
        for (uint32_t k = 0; k < 0x10; k += 4) wd(0x30 + k) = at<uint32_t>(ps, k);
        {
            const uint32_t args[1] = {0};
            veh_call_at(B, 0x00271ef3, own_address(&Pmove_entry), TitleRegs{wd(0x40), cl_w, at<uint32_t>(ps, 8), at<uint32_t>(ps, 0xc), B + 0x40, ent_w}, 1,
                        args);  // Pmove
        }
        const uint8_t* const after = pointer<uint8_t>(wd(0x40));
        for (uint32_t k = 0; k < 0x10; k += 4) wd(0x20 + k) = at<uint32_t>(after, k);
        if (by(0x13)) {
            const bool same0 = fl(0x20) == fl(0x30);
            const bool same1 = fl(0x24) == fl(0x34);
            const bool same2 = fl(0x28) == fl(0x38);
            const bool same3 = fl(0x2c) == fl(0x3c);
            (void)same3;
            if (same0 && same1 && same2) {
                const uint32_t args[2] = {B + 0x40, 0x4171999a};
                veh_call_at(B, 0x00271f42, own_address(&G_PushCapsuleOutOfSolids), TitleRegs{0, cl_w, B + 0x40, wd(0x2c), B + 0x40, ent_w}, 2,
                            args);  // G_PushCapsuleOutOfSolids
            }
        }
    }
    if (at<uint8_t>(client, 0xc0) & 0x10)
        at<uint32_t>(ent, 0xd4) |= 0x200;
    else
        at<uint32_t>(ent, 0xd4) &= 0xfffffdffu;
    {
        uint8_t* const cl = at<uint8_t*>(ent, 0x23c);
        const uint32_t handle = at<uint32_t>(cl, 0x60);
        const uint32_t index = handle & 0xfff;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8) &&
            title_global<uint32_t>(0x0097a968 + index * 8)) {
            const uint8_t* const ground = entity_of_handle(handle);
            if (at<uint32_t>(ground, 0x23c)) {
                const float vx = at<float>(cl, 0x10), vy = at<float>(cl, 0x14), vz = at<float>(cl, 0x18);
                const float speed = title_ci_sqrt((vx * vx + vy * vy) + vz * vz);
                if (platform_x87_greater(title_global<float>(0x0083ef08), speed)) {  // 200
                    const auto random = [&](uint32_t ret, uint32_t esi_now) {
                        const int32_t r = static_cast<int32_t>(
                            veh_call_at(B, ret, own_address(&title_rand), TitleRegs{0, cl_w, 0, 0, esi_now, ent_w}));  // rand
                        return ((static_cast<float>(r) / title_global<float>(0x0083ebc8)) * title_global<float>(0x0083eba8) -
                                title_global<float>(0x0083eb20)) *
                               title_global<float>(0x0083eb90);
                    };
                    const float dx = random(0x0027202b, address_of(cl));
                    at<float>(cl, 0x10) = dx + at<float>(cl, 0x10);
                    uint8_t* const c2 = at<uint8_t*>(ent, 0x23c);
                    const float dy = random(0x00272067, address_of(c2) + 0x14);
                    at<float>(c2, 0x14) = dy + at<float>(c2, 0x14);
                    uint8_t* const c3 = at<uint8_t*>(ent, 0x23c);
                    at<float>(c3, 0x18) = at<float>(c3, 0x18) + title_global<float>(0x0083ef08);
                }
            }
        }
    }
    uint8_t* const c4 = at<uint8_t*>(ent, 0x23c);
    if (at<uint32_t>(c4, 0xc4) != old_sequence) at<int32_t>(ent, 0x1a8) = title_global<int32_t>(0x0098540c);
    {
        const uint32_t args[1] = {at<uint32_t>(c4, 0x20)};
        veh_call_at(B, 0x002720d3, own_address(&BG_PlayerStateToEntityStateExtraPolate), TitleRegs{ent_w, cl_w, address_of(c4), args[0], old_sequence, ent_w}, 1,
                    args);  // BG_PlayerStateToEntityStateExtraPolate
    }
    at<uint32_t>(ent, 0x140) = at<uint32_t>(ent, 0x20);
    at<uint32_t>(ent, 0x144) = at<uint32_t>(ent, 0x24);
    at<uint32_t>(ent, 0x148) = at<uint32_t>(ent, 0x28);
    at<float>(ent, 0xe0) = fl(0x140);
    at<float>(ent, 0xe4) = fl(0x144);
    at<float>(ent, 0xe8) = fl(0x148);
    at<float>(ent, 0xf0) = fl(0x150);
    at<float>(ent, 0xf4) = fl(0x154);
    at<float>(ent, 0xf8) = fl(0x158);
    uint32_t eax_now;
    {
        const uint32_t args[1] = {ent_w};
        eax_now = veh_call_at(B - 4, 0x0027215c, own_address(&ClientEvents_entry), TitleRegs{old_sequence, cl_w, 0, at<uint32_t>(ent, 0x28), old_sequence, ent_w},
                              1, args);  // ClientEvents
    }
    // EAX after ClientEvents is dead (SV_LinkEntity loads EAX before reading it); passed on as the original's
    // only so Compare inputs match - a Replaced ClientEvents leaves whatever its C left there.
    veh_call_at(B, 0x00272164, own_address(&SV_LinkEntity_entry), TitleRegs{eax_now, cl_w, 0, 0, old_sequence, ent_w});  // SV_LinkEntity
    if (at<float>(client, 0) != at<float>(client, 0x5b0) || at<float>(client, 4) != at<float>(client, 0x5b4) ||
        at<float>(client, 8) != at<float>(client, 0x5b8))
        at<uint8_t>(at<uint8_t*>(ent, 0x244), 0x5a) = 0;
    {
        uint8_t* const c = at<uint8_t*>(ent, 0x23c);
        if (!at<uint32_t>(c, 0x584) && !at<uint32_t>(c, 0x588) && at<int32_t>(c, 0x24) < 4 &&
            title_global<int32_t>(0x0092bdf0) != 5) {  // cls_state
            const uint32_t args[1] = {ent_w};
            veh_call_at(B, 0x002721dc, own_address(&G_TouchTriggers_entry), TitleRegs{address_of(c), cl_w, 0, 0, address_of(c), ent_w}, 1,
                        args);  // G_TouchTriggers
        }
    }
    at<uint32_t>(ent, 0x140) = at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0);
    at<uint32_t>(ent, 0x144) = at<uint32_t>(at<uint8_t*>(ent, 0x23c), 4);
    at<uint32_t>(ent, 0x148) = at<uint32_t>(at<uint8_t*>(ent, 0x23c), 8);
    const float zero = 0.0f;
    at<float>(ent, 0x158) = zero;
    at<float>(ent, 0x154) = zero;
    at<float>(ent, 0x150) = zero;
    const uint32_t yaw = at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0xa0);
    at<uint32_t>(ent, 0x154) = yaw;
    {
        const uint32_t args[3] = {ent_w, wd(0xbc), B + 0xc0};
        veh_call_at(B, 0x00272252, own_address(&ClientImpacts), TitleRegs{B + 0xc0, cl_w, wd(0xbc), yaw, old_sequence, ent_w}, 3,
                    args);  // ClientImpacts
    }
    if (title_global<int32_t>(0x0098540c) >= at<int32_t>(client, 0x624) + 500)
        at<int32_t>(client, 0x620) = title_global<int32_t>(0x0098540c);
    if (at<uint32_t>(at<uint8_t*>(ent, 0x23c), 0xc4) != old_sequence) at<int32_t>(ent, 0x1a8) = title_global<int32_t>(0x0098540c);
    const uint8_t* const cmd = pointer<uint8_t>(wd(0x1c));
    {
        const uint32_t old_buttons = at<uint32_t>(client, 0x594);
        at<uint32_t>(client, 0x598) = old_buttons;
        const uint32_t buttons = cmd[4];
        at<uint32_t>(client, 0x594) = buttons;
        at<uint32_t>(client, 0x59c) = ~old_buttons & buttons;
        at<uint32_t>(client, 0x5a4) = at<uint32_t>(client, 0x5a0);
        const uint32_t buttons2 = cmd[5];
        const uint32_t old2 = at<uint32_t>(client, 0x5a4);
        at<uint32_t>(client, 0x5a0) = buttons2;
        at<uint32_t>(client, 0x5a8) = ~old2 & buttons2;
        for (uint32_t k = 0; k < 0x14; k += 4) at<uint32_t>(client, 0x5c0 + k) = at<uint32_t>(cmd, 0x18 + k);
    }
    uint32_t esi_now = old_sequence;
    if (at<uint8_t>(client, 0x59c) & 0x40) {
        esi_now = ent_w;
        veh_call_at(B, 0x00272310, own_address(&Cmd_Activate_f_entry), TitleRegs{at<uint32_t>(cmd, 0x28), cl_w, 0, 0, ent_w, ent_w});  // Cmd_Activate_f
    }
    if (at<uint8_t>(client, 0x594) & 0x40) {
        const uint32_t args[1] = {ent_w};
        veh_call_at(B, 0x0027231f, own_address(&ClientThink_RunPendingTouch), TitleRegs{at<uint32_t>(cmd, 0x28), cl_w, 0, 0, esi_now, ent_w}, 1,
                    args);  // ClientThink_RunPendingTouch
    }
    if (at<int32_t>(client, 0x24) >= 4) {
        if (title_global<int32_t>(0x0098540c) > at<int32_t>(client, 0x5e8))
            veh_call_at(B, 0x0027233b, own_address(&ClientDeathReload), TitleRegs{at<uint32_t>(cmd, 0x28), cl_w, title_global<uint32_t>(0x0098540c), 0, esi_now, ent_w});  // ClientDeathReload
        return;
    }
    const uint32_t args[1] = {ent_w};
    veh_call_at(B, 0x00272348, own_address(&G_UpdatePlayerNodeClaim_entry), TitleRegs{at<uint32_t>(cmd, 0x28), cl_w, 0, 0, esi_now, ent_w}, 1,
                args);  // G_UpdatePlayerNodeClaim
}

namespace {

// a == b on the x87 (FLD/FUCOMIP, LAHF + TEST AH,0x44 + JNP): only an ordered equal.
bool x87_equal_vehicle(float a, float b) {
    bool equal, ordered;
    __asm__("flds %3\n\tflds %2\n\tfucomip %%st(1), %%st\n\tfstp %%st(0)\n\tsete %0\n\tsetnp %1"
            : "=q"(equal), "=q"(ordered)
            : "m"(a), "m"(b)
            : "st", "st(1)", "cc");
    return equal && ordered;
}

// The title's name hash (rotate left 5, xor the lower-cased character), kept in a guarded static.
uint32_t use_hash(uint32_t guard, uint32_t bit, uint32_t slot, uint32_t text) {
    if (title_global<uint32_t>(guard) & bit) return title_global<uint32_t>(slot);
    title_global<uint32_t>(guard) |= bit;
    uint32_t h = 0;
    for (const char* s = pointer<const char>(text); *s; ++s) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 0x20);
        h = ((h << 5) | (h >> 27)) ^ static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(c)));
    }
    title_global<uint32_t>(slot) = h;
    return h;
}

}  // namespace

// The entity list, the view vectors and the trace sit at the original's frame addresses (gap entry;
// B = aligned base - 0x1670); every title callee at the original's ESP.
PLATFORM_ASM_ENTRY(Player_GetUseList_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Player_GetUseList) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

int32_t XB_CDECL UseList_CompareScore(const float* a, const float* b) { return static_cast<int32_t>(a[1] - b[1]); }

uint32_t XB_CDECL Player_GetUseList(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl (ent, list of {entity, score}): usable entities around the eye (box 150 x 150 x 96; content
    // flags 0x204000): items, entities with +0x134 bit 0x200000 and sentients with +0x1374. Each within 100
    // units (128 for vehicles, type 0xe, which also need a dot above 0.38) in front of the view: score
    // (1 - (dot - 0.76) / 0.24) * 200 (-200 for trigger_use; +10000 for items that cannot be grabbed) plus the
    // distance; sorted. Then from the best: trigger_use cones (+0x90 / +0x94) the player is outside of,
    // and entities a trace from the eye (to the tag_aim of turrets, the eye height for vehicles) finds
    // blocked, get +100000 and the next is tried; "second_88" or the player standing inside the entity's box
    // ends it. Sorted again; returns the count minus the blocked ones.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0x1670;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    wd(8) = ebx;
    wd(4) = esi;
    wd(0) = edi;
    uint8_t* const ent = pointer<uint8_t>(*pointer<uint32_t>(E + 4));
    const uint32_t list_w = *pointer<uint32_t>(E + 8);
    const uint32_t ent_w = address_of(ent);
    for (uint32_t k = 0; k < 0x540 * 4; k += 4) wd(0x170 + k) = 0;
    uint8_t* const client = at<uint8_t*>(ent, 0x23c);
    const uint32_t cl_w = address_of(client);
    const float d2r = title_global<float>(0x0083edd8);
    wd(0x160) = 0;
    wd(0x1c) = 0;
    wd(0x14) = 0;
    wd(0xac) = cl_w;
    {
        const float a = at<float>(client, 0xa0) * d2r;
        uint32_t w;
        std::memcpy(&w, &a, 4);
        veh_call_at(B, 0x002640c7, own_address(&FastSinCos), TitleRegs{B + 0x18, 0, B + 0xc, 0, cl_w, ent_w}, 1, &w);  // FastSinCos (yaw)
    }
    {
        const float a = at<float>(client, 0x9c) * d2r;
        uint32_t w;
        std::memcpy(&w, &a, 4);
        veh_call_at(B, 0x002640e9, own_address(&FastSinCos), TitleRegs{B + 0x10, 0, B + 0x20, 0, cl_w, ent_w}, 1, &w);  // FastSinCos (pitch)
    }
    const float zero = 0.0f;
    const float ox = at<float>(ent, 0x140), oy = at<float>(ent, 0x144);
    const float cp = fl(0x20);
    fl(0x58) = cp * fl(0x18);
    fl(0x5c) = zero - fl(0x10);
    fl(0x54) = cp * fl(0xc);
    const float ez = at<float>(client, 0xac) + at<float>(ent, 0x148);
    const float half_box = title_global<float>(0x0083ebf0), height = title_global<float>(0x00830bf0);  // 150, 96
    fl(0x80) = ox - half_box;
    fl(0x84) = oy - half_box;
    fl(0x88) = ez - height;
    fl(0x60) = ox + half_box;
    fl(0xc0) = ox;
    fl(0xc4) = oy;
    fl(0xc8) = ez;
    fl(0x64) = oy + half_box;
    fl(0x68) = ez + height;
    int32_t count;
    {
        const uint32_t args[3] = {B + 0x170, 0x540, 0x00204000};
        count = static_cast<int32_t>(veh_call_at(B - 4, 0x002641e1, own_address(&SV_AreaEntities), TitleRegs{B + 0x60, 0, B + 0x80, 0, cl_w, ent_w}, 3,
                                                 args));  // SV_AreaEntities
    }
    wd(0x20) = static_cast<uint32_t>(count);
    uint32_t slot = list_w + 4;
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const e = entity_of_handle(wd(0x170 + static_cast<uint32_t>(i) * 4));
        if (e == ent) continue;
        const uint8_t type = e[0];
        if (type != 2 && !(at<uint32_t>(e, 0x134) & 0x200000)) {
            const uint8_t* const sentient = at<uint8_t*>(e, 0x240);
            if (!sentient || !sentient[0x1374]) continue;
        }
        fl(0xc) = type == 0xe ? title_global<float>(0x0083ebe8) : title_global<float>(0x0083eb90);  // 128 / 100
        const float half = title_global<float>(0x0083eb24);
        const float cy = (at<float>(e, 0x114) + at<float>(e, 0x104)) * half;
        const float cx = (at<float>(e, 0x110) + at<float>(e, 0x100)) * half;
        const float cz = (at<float>(e, 0x118) + at<float>(e, 0x108)) * half;
        fl(0x34) = cy - oy;
        fl(0x30) = cx - ox;
        const float limit = at<float>(ent, 0x148) + title_global<float>(0x008ff00c);
        fl(0x38) = cz - ez;
        if (cz > limit) continue;
        float d[4], sq[4];
        for (uint32_t k = 0; k < 4; ++k) {
            d[k] = fl(0x30 + k * 4);
            sq[k] = d[k] * d[k];
        }
        const float len2 = (sq[0] + sq[1]) + sq[2];
        fl(0x70) = len2;
        fl(0x74) = sq[1];
        fl(0x78) = sq[2];
        fl(0x7c) = sq[3];
        if (!(len2 != zero)) {
            for (uint32_t k = 0; k < 16; k += 4) wd(0x40 + k) = wd(0x70 + k);
        } else {
            fl(0x60) = len2;
            fl(0x64) = sq[1];
            fl(0x68) = sq[2];
            fl(0x6c) = sq[3];
            const float r = platform_rsqrt_estimate(len2);
            fl(0x60) = r;
            const float lr = len2 * r;
            const float three = title_global<float>(0x0082e660);
            fl(0x40) = ((three - lr * r) * lr) * title_global<float>(0x0082e670);
            fl(0x44) = title_global<float>(0x0082e664);
            fl(0x48) = title_global<float>(0x0082e668);
            fl(0x4c) = title_global<float>(0x0082e66c);
        }
        const float len = fl(0x40);
        if (len != zero) {
            float q[4];
            for (uint32_t k = 0; k < 4; ++k) q[k] = d[k] / len;
            for (uint32_t k = 0; k < 4; ++k) fl(0x30 + k * 4) = q[k];
        }
        if (len > fl(0xc)) continue;
        const float dot = (fl(0x38) * fl(0x5c) + fl(0x34) * fl(0x58)) + fl(0x30) * fl(0x54);
        if (zero >= dot) continue;
        if (type == 0xe && title_global<float>(0x0083f1ac) > dot) continue;  // 0.38
        const float two_hundred = title_global<float>(0x0083ef08);
        float score = (title_global<float>(0x0083eb20) - (dot - title_global<float>(0x0083f1a8)) / title_global<float>(0x0083f1a4)) *
                      two_hundred;
        *pointer<float>(slot) = score;
        if (at<uint32_t>(e, 0x260) == title_global<uint32_t>(0x0097a81c)) {  // trigger_use
            score = score - two_hundred;
            *pointer<float>(slot) = score;
        }
        if (e[0] == 2) {
            const uint32_t args[1] = {cl_w};
            const uint32_t ok = veh_call_at(B, 0x00264466, own_address(&BG_CanItemBeGrabbed), TitleRegs{0, slot, address_of(e), 0, static_cast<uint32_t>(i), address_of(e)}, 1,
                                            args);  // BG_CanItemBeGrabbed
            if (!ok) {
                *pointer<float>(slot) = *pointer<float>(slot) + title_global<float>(0x0083f010);  // 10000
                wd(0x14) = wd(0x14) + 1;
            }
        }
        *pointer<uint32_t>(slot - 4) = address_of(e);
        *pointer<float>(slot) = len + *pointer<float>(slot);
        wd(0x1c) = wd(0x1c) + 1;
        slot += 8;
    }
    const auto sort = [&](uint32_t n, uint32_t ret) {
        const uint32_t args[4] = {list_w, n, 8, own_address(&UseList_CompareScore)};
        veh_call_at(B, ret, own_address(&title_qsort), TitleRegs{0, 0, 0, 0, n, 0}, 4, args);  // qsort
    };
    sort(wd(0x1c), 0x002644bf);
    const int32_t usable = static_cast<int32_t>(wd(0x1c) - wd(0x14));
    wd(0x1c) = static_cast<uint32_t>(usable);
    wd(0x10) = 0;
    wd(0x20) = 0;
    if (usable > 0) {
        uint32_t entry = list_w + 4;
        wd(0x14) = entry;
        for (;;) {
            uint8_t* const e = pointer<uint8_t>(*pointer<uint32_t>(entry - 4));
            const float half = title_global<float>(0x0083eb24);
            fl(0x40) = (at<float>(e, 0x110) + at<float>(e, 0x100)) * half;
            fl(0x44) = (at<float>(e, 0x114) + at<float>(e, 0x104)) * half;
            fl(0x48) = (at<float>(e, 0x118) + at<float>(e, 0x108)) * half;
            if (e[0] == 0xe) fl(0x48) = fl(0xc8);
            if (e[0] == 0xa) {
                const uint32_t aim = use_hash(0x0098f154, 1, 0x0098f150, 0x00839a18);  // "tag_aim"
                int32_t bone = -1;
                if (at<uint32_t>(e, 0x22c)) {
                    if (aim) {
                        const uint32_t args[1] = {aim};
                        bone = static_cast<int32_t>(veh_call_at(B, 0x002645cb, own_address(&DObj_GetBoneIndex),
                                                                TitleRegs{at<uint32_t>(e, 0x22c), 1, aim, 0, address_of(e), entry}, 1,
                                                                args));  // DObj_GetBoneIndex
                    }
                    const uint32_t origin = use_hash(0x0098b840, 1, 0x0098b83c, 0x00832b04);  // "tag_origin"
                    if (bone < 0 && title_global<uint32_t>(0x0098f150) == origin) bone = 0;
                }
                if (bone >= 0) {
                    const uint32_t args[3] = {address_of(e), static_cast<uint32_t>(bone), B + 0xf0};
                    veh_call_at(B, 0x00264641, own_address(&G_DObjGetWorldTagMatrix), TitleRegs{static_cast<uint32_t>(bone), 1, 0, B + 0xf0, address_of(e), entry}, 3,
                                args);  // G_DObjGetWorldTagMatrix
                    fl(0x40) = fl(0x120);
                    fl(0x44) = fl(0x124);
                    fl(0x48) = fl(0x128);
                }
            }
            bool blocked = false;
            bool cone_checked = false;
            if (at<uint32_t>(e, 0x260) == title_global<uint32_t>(0x0097a81c) &&
                title_global<float>(0x0083eb20) > at<float>(e, 0x90)) {
                cone_checked = true;
                const float fy = fl(0x58), fx = fl(0x54);
                fl(0x28) = fy;
                fl(0x2c) = zero;
                const float zz = zero * zero;
                const float len2 = (fy * fy + fx * fx) + zz;
                fl(0x18) = len2;
                fl(0x24) = fx;
                fl(0xa0) = zz;
                fl(0xc) = title_ci_sqrt(len2);
                fl(0x18) = fl(0xc);
                if (!x87_equal_vehicle(fl(0x18), title_global<float>(0x0083eafc))) {
                    const float inv = title_global<float>(0x0083eb20) / fl(0x18);
                    fl(0x24) = inv * fl(0x54);
                    fl(0x28) = inv * fl(0x58);
                    fl(0x2c) = inv * title_global<float>(0x0083eafc);
                }
                {
                    const float a = at<float>(e, 0x94) * d2r;
                    uint32_t w;
                    std::memcpy(&w, &a, 4);
                    veh_call_at(B, 0x0026476e, own_address(&FastSinCos), TitleRegs{B + 0x98, 1, B + 0x9c, 0, address_of(e), entry}, 1, &w);  // FastSinCos
                }
                {
                    const float a = zero * d2r;
                    uint32_t w;
                    std::memcpy(&w, &a, 4);
                    veh_call_at(B, 0x00264791, own_address(&FastSinCos), TitleRegs{B + 0xa4, 1, B + 0xa8, 0, address_of(e), entry}, 1, &w);  // FastSinCos
                }
                const float c = fl(0xa8);
                const float uy = c * fl(0x98);
                const float ux = c * fl(0x9c);
                const float uz = zero - fl(0xa4);
                fl(0x78) = uz;
                fl(0x70) = ux;
                fl(0x74) = uy;
                const float dot = (uz * fl(0x2c) + uy * fl(0x28)) + ux * fl(0x24);
                const float neg = zero - dot;
                if (!(neg > at<float>(e, 0x90))) {
                    blocked = true;
                } else {
                    const float dx = fl(0x30), dy = fl(0x34);
                    fl(0x24) = dx;
                    fl(0x28) = dy;
                    fl(0x2c) = zero;
                    const long double sum = (platform_x87_ld(dy) * dy + platform_x87_ld(dx) * dx) +
                                            platform_x87_ld(fl(0xa0));
                    fl(0xc) = static_cast<float>(sum);
                    fl(0xc) = title_ci_sqrt(fl(0xc));
                    float vx, vy, vz;
                    if (!x87_equal_vehicle(fl(0xc), title_global<float>(0x0083eafc))) {
                        const float inv = title_global<float>(0x0083eb20) / fl(0xc);
                        vx = fl(0x30) * inv;
                        vy = fl(0x34) * inv;
                        vz = inv * title_global<float>(0x0083eafc);
                    } else {
                        vy = fl(0x28);
                        vx = fl(0x24);
                        vz = fl(0x2c);
                    }
                    const float dot2 = (fl(0x78) * vz + fl(0x74) * vy) + fl(0x70) * vx;
                    const float neg2 = zero - dot2;
                    if (at<float>(e, 0x90) > neg2) blocked = true;
                }
            }
            (void)cone_checked;
            if (!blocked) {
                const uint32_t second = use_hash(0x0098f154, 2, 0x0098f14c, 0x0083b390);  // "second_88"
                if (at<uint32_t>(e, 0x268) == second) break;
                const float fifteen = title_global<float>(0x0083ef10);
                fl(0xb8) = zero;
                fl(0xbc) = fifteen;
                fl(0xb4) = fifteen;
                fl(0xb0) = fifteen;
                const uint8_t* const cl = at<uint8_t*>(ent, 0x23c);
                float lo[4], hi[4], c4[4];
                for (uint32_t k = 0; k < 4; ++k) lo[k] = at<float>(ent, 0x140 + k * 4) - fl(0xb0 + k * 4);
                for (uint32_t k = 0; k < 16; k += 4) wd(0xd0 + k) = at<uint32_t>(ent, 0x140 + k);
                fl(0xec) = fifteen;
                fl(0xe4) = fifteen;
                fl(0xe0) = fifteen;
                fl(0xe8) = at<float>(cl, 0xac);
                for (uint32_t k = 0; k < 4; ++k) hi[k] = fl(0xd0 + k * 4) + fl(0xe0 + k * 4);
                for (uint32_t k = 0; k < 4; ++k) c4[k] = fl(0x40 + k * 4);
                bool inside[4];
                for (uint32_t k = 0; k < 4; ++k) {
                    const float a = lo[k] - c4[k];
                    const float b = c4[k] - hi[k];
                    const float m = a > b ? a : b;
                    inside[k] = m < zero;
                }
                if (inside[0] && inside[1]) break;
                for (uint32_t k = 0; k < 16; k += 4) {
                    wd(0x60 + k) = title_global<uint32_t>(0x0082e290 + k);
                    wd(0x80 + k) = title_global<uint32_t>(0x0082e290 + k);
                }
                {
                    const uint32_t args[11] = {B + 0x130, B + 0x60, B + 0x80, B + 0x40, at<uint32_t>(cl, 0x7c), 0x00200011, 0, 0, 0, 0, 0};
                    veh_call_at(B, 0x00264ab8, own_address(&SV_Trace_entry), TitleRegs{B + 0x130, B + 0xc0, B + 0x80, B + 0x60, address_of(e), title_global<uint32_t>(0x0082e29c)}, 11,
                                args);  // SV_Trace (EBX = the eye)
                }
                const uint8_t* const hit = entity_of_handle(wd(0x160));
                if (!(title_global<float>(0x0083eb20) > fl(0x150))) break;
                if (hit == e) break;
                blocked = true;
            }
            const uint32_t score = wd(0x14);
            *pointer<float>(score) = *pointer<float>(score) + title_global<float>(0x0083f0dc);  // 100000
            wd(0x10) = wd(0x10) + 1;
            wd(0x20) = wd(0x20) + 1;
            entry = score + 8;
            wd(0x14) = entry;
            if (!(static_cast<int32_t>(wd(0x20)) < static_cast<int32_t>(wd(0x1c)))) break;
        }
    }
    const uint32_t n = wd(0x1c);
    sort(n, 0x00264b4d);
    return n - wd(0x10);
}
// EAX = fire record; cdecl ent. The trace, the area box and the shake/rumble records sit at the original's
// frame addresses (gap entry; B = aligned base - 0x2c0), every title callee at the original's ESP.
PLATFORM_ASM_ENTRY(Weapon_MeleeTrace_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Weapon_MeleeTrace) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Weapon_MeleeTrace(uint32_t entry_esp, uint8_t* fire, uint32_t ebx, uint32_t esi, uint32_t edi,
                                uint32_t ebp) {
    // A melee swing 90 units along the fire direction (+0, from +0x24; contents 0x280e031). A client first looks
    // for a player or sentient whose bounds come within 0x008fefe8 of the point 45 units out (SV_AreaEntities
    // over its own bounds pushed 30 along the swing, +-20): the trace goes to the nearest point of that box and
    // counts as a half-way hit on it unless something else was hit first. Hit-trigger damage (def +0x5cc, mod
    // 7); a hit (fraction below 1, no surface flag 0x10) spawns event 0xac (0xab on clients/sentients) with
    // the nearest byte direction to the normal. Damageable targets take the damage + rand % 5 (hit location,
    // flag -1), the player also a rumble ("default", 0.3), camera shake (0.7, 500 ms, 1000) and the rumble
    // record; others get "damage" (unless squad mates).
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0x2c0;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    auto by = [B](uint32_t off) -> uint8_t& { return *pointer<uint8_t>(B + off); };
    uint8_t* const ent = pointer<uint8_t>(*pointer<uint32_t>(E + 4));
    const uint32_t ent_w = address_of(ent), fire_w = address_of(fire);
    wd(8) = ebx;
    wd(4) = esi;
    const float* const f = reinterpret_cast<const float*>(fire);
    const float reach = title_global<float>(0x0083eb64);  // 90
    const float ex = f[0] * reach + f[9];
    const float ey = f[1] * reach + f[10];
    const float ez = f[2] * reach + f[11];
    wd(0) = edi;
    wd(0x80) = 0;
    wd(0x84) = 0;
    const auto put4 = [&](uint32_t off, float x, float y, float z) {
        fl(off) = x;
        fl(off + 4) = y;
        fl(off + 8) = z;
        fl(off + 12) = x;
    };
    const auto trace = [&](uint32_t start, uint32_t end, uint32_t ret) {
        const uint32_t args[7] = {B + 0x50, start, end, at<uint32_t>(ent, 0x224), 0x0280e031, 0x008da6f0, 0};
        veh_call_at(B, ret, own_address(&G_LocationalTrace_entry), TitleRegs{B + 0x50, ent_w, start, end, fire_w, edi}, 7, args);  // G_LocationalTrace
    };
    if (!at<uint32_t>(ent, 0x23c)) {
        put4(0x10, ex, ey, ez);
        put4(0x30, ex, ey, ez);
        put4(0x10, f[9], f[10], f[11]);
        trace(B + 0x10, B + 0x30, 0x00265f7d);
    } else {
        for (uint32_t k = 0; k < 0x200; k += 4) wd(0xc0 + k) = 0;
        const float thirty = title_global<float>(0x0083ee38), twenty = title_global<float>(0x0083eb5c);
        put4(0x10, f[0], f[1], f[2]);
        const float s[4] = {fl(0x10) * thirty, fl(0x14) * thirty, fl(0x18) * thirty, fl(0x1c) * thirty};
        for (uint32_t k = 0; k < 16; k += 4) fl(0x10 + k) = twenty;
        for (uint32_t k = 0; k < 16; k += 4) wd(0x10 + k) = at<uint32_t>(ent, 0x100 + k);
        float mins[4];
        for (uint32_t k = 0; k < 4; ++k) mins[k] = at<float>(ent, 0x100 + k * 4) + (s[k] - twenty);
        for (uint32_t k = 0; k < 16; k += 4) wd(0x10 + k) = at<uint32_t>(ent, 0x110 + k);
        for (uint32_t k = 0; k < 4; ++k) fl(0xb0 + k * 4) = fl(0x10 + k * 4) + (s[k] + twenty);
        const float out = title_global<float>(0x0083ef1c);  // 45
        fl(0x30) = f[0] * out + f[9];
        fl(0x34) = f[1] * out + f[10];
        for (uint32_t k = 0; k < 4; ++k) fl(0xa0 + k * 4) = mins[k];
        fl(0x38) = f[2] * out + f[11];
        int32_t count;
        {
            const uint32_t args[3] = {B + 0xc0, 0x80, 0x0200c000};
            count = static_cast<int32_t>(veh_call_at(B, 0x00265e2a, own_address(&SV_AreaEntities), TitleRegs{B + 0xb0, ent_w, B + 0xa0, 0, fire_w, edi}, 3,
                                                     args));  // SV_AreaEntities
        }
        wd(0x90) = static_cast<uint32_t>(count);
        const float radius = title_global<float>(0x008fefe8);
        float p[4];
        for (uint32_t k = 0; k < 4; ++k) p[k] = fl(0x30 + k * 4);
        uint8_t* found = nullptr;
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t handle = wd(0xc0 + static_cast<uint32_t>(i) * 4);
            const uint32_t index = handle & 0xfff;
            if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) continue;
            uint8_t* const e = title_global<uint8_t*>(0x0097a968 + index * 8);
            if (!e) continue;
            if (!at<uint32_t>(e, 0x23c) && !at<uint32_t>(e, 0x240)) continue;
            if (ent == e) continue;
            float d[4];
            for (uint32_t k = 0; k < 4; ++k) {
                const float lo = at<float>(e, 0x100 + k * 4), hi = at<float>(e, 0x110 + k * 4);
                const float t = p[k] > lo ? p[k] : lo;
                const float c = t < hi ? t : hi;
                fl(0x30 + k * 4) = c;
                const float diff = p[k] - c;
                d[k] = diff * diff;
            }
            const float sum = (d[0] + d[1]) + d[2];
            fl(0x10) = sum;
            fl(0x14) = d[1];
            fl(0x18) = d[2];
            fl(0x1c) = d[3];
            if (radius * radius > sum) {
                found = e;
                break;
            }
        }
        if (!found) {
            put4(0x30, ex, ey, ez);
            put4(0x10, ex, ey, ez);
            put4(0x30, f[9], f[10], f[11]);
            trace(B + 0x30, B + 0x10, 0x00265f7d);
        } else {
            put4(0x10, f[9], f[10], f[11]);
            trace(B + 0x10, B + 0x30, 0x002660c1);
            const uint32_t target = at<uint32_t>(found, 0x224);
            const float one = title_global<float>(0x0083eb20);
            if (wd(0x80) == target || fl(0x70) == one) {
                fl(0x70) = title_global<float>(0x0083eb24);  // 0.5
                fl(0x50) = fl(0x30);
                fl(0x54) = fl(0x34);
                wd(0x80) = target;
                fl(0x58) = fl(0x38);
            }
        }
    }
    const uint8_t* const def = weapon_def(at<uint8_t>(ent, 3));
    const uint32_t damage = at<uint32_t>(def, 0x5cc);
    put4(0x30, f[9], f[10], f[11]);
    wd(0x4c) = damage;
    {
        const uint32_t args[5] = {ent_w, B + 0x30, B + 0x50, damage, 7};
        veh_call_at(B, 0x00265fdb, own_address(&G_CheckHitTriggerDamage), TitleRegs{damage, ent_w, B + 0x50, B + 0x30, fire_w, edi}, 5,
                    args);  // G_CheckHitTriggerDamage
    }
    if (by(0x74) & 0x10) return;
    if (fl(0x70) == title_global<float>(0x0083eb20)) return;
    uint8_t* const hit = entity_of_handle(wd(0x80));
    if (!hit) return;
    const uint32_t hit_w = address_of(hit);
    uint8_t* temp;
    {
        const uint32_t event = (!at<uint32_t>(hit, 0x23c) && !at<uint32_t>(hit, 0x240)) ? 0xac : 0xab;
        const uint32_t args[2] = {B + 0x50, event};
        temp = pointer<uint8_t>(veh_call_at(B, 0x00266139, own_address(&G_TempEntity), TitleRegs{B + 0x50, ent_w, B + 0x50, 0, fire_w, hit_w}, 2,
                                            args));  // G_TempEntity
    }
    at<uint32_t>(temp, 8) = wd(0x80);
    wd(0x2c) = address_of(temp);
    by(0xf) = 0;
    {
        const float nx = fl(0x60), ny = fl(0x64), nz = fl(0x68);
        float best = 0.0f;
        for (uint32_t i = 0; i < 0xa2; ++i) {
            const float* const dir = pointer<float>(0x008d52d8 + i * 0xc);  // byte directions
            const float d = (dir[1] * ny + dir[0] * nx) + nz * dir[2];
            if (d > best) {
                best = d;
                by(0xf) = static_cast<uint8_t>(i);
            }
        }
    }
    temp[4] = by(0xf);
    temp[3] = at<uint8_t>(ent, 3);
    if (!wd(0x80)) return;
    if (!at<uint32_t>(hit, 0x29c)) {
        const uint8_t* const squad = at<uint8_t*>(ent, 0x244);
        if (squad && at<uint8_t*>(hit, 0x244) && at<int32_t>(squad, 4) == at<int32_t>(at<uint8_t*>(hit, 0x244), 4)) return;
        wd(0x2c) = at<uint32_t>(ent, 0x224);
        wd(0x4c) = 0;
        const uint32_t args[4] = {hit_w, title_global<uint32_t>(0x0097a67c), B + 0x4c, B + 0x2c};  // "damage"
        veh_call_at(B, 0x0026621a, own_address(&G_NotifyWithIntEntity_entry), TitleRegs{B + 0x2c, ent_w, B + 0x4c, args[1], fire_w, hit_w}, 4,
                    args);  // G_NotifyWithIntEntity
        return;
    }
    const int32_t r = static_cast<int32_t>(veh_call_at(B, 0x00266226, own_address(&title_rand), TitleRegs{0, ent_w, 0, 0, fire_w, hit_w}));  // rand
    {
        const uint32_t amount = wd(0x4c) + static_cast<uint32_t>(r % 5);
        const uint32_t args[9] = {hit_w, ent_w, ent_w, fire_w, amount, 0, 7, wd(0x88), 0xffffffffu};
        veh_call_at(B, 0x00266250, own_address(&G_Damage_entry), TitleRegs{B + 0x50, ent_w, 5, wd(0x88), fire_w, hit_w}, 9, args);  // G_Damage
    }
    if (hit != at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4)) return;
    {
        const uint32_t args[1] = {0x00831f70};  // "default"
        veh_call_at(B, 0x00266271, own_address(&ScriptString_FromCString), TitleRegs{0, ent_w, B + 0x2c, 0, fire_w, hit_w}, 1, args);  // ScriptString_FromCString
    }
    {
        const uint32_t function = at<uint32_t>(title_global<uint8_t*>(0x0095f49c), 0x4e4);
        const uint32_t args[3] = {at<uint32_t>(hit, 0x224), B + 0x2c, 0x3e99999a};  // 0.3
        veh_call_at(B, 0x0026628e, function, TitleRegs{args[0], ent_w, B + 0x2c, title_global<uint32_t>(0x0095f49c), fire_w, hit_w}, 3,
                    args);  // the rumble by name
    }
    veh_call_at(B, 0x0026629a, own_address(&ScriptString_Release_entry), TitleRegs{0, ent_w, B + 0x2c, 0, fire_w, hit_w});  // ScriptString_Release
    {
        const uint8_t* const owner = at<uint8_t*>(at<uint8_t*>(hit, 0x244), 0);
        fl(0x10) = at<float>(owner, 0x140);
        fl(0x14) = at<float>(owner, 0x144);
        fl(0x18) = at<float>(owner, 0x148);
        CG_StartShakeCamera(&fl(0x10), title_global<float>(0x0083ed28), 500, 1000.0f);  // 0.7
        *pointer<uint32_t>(B - 0xc) = 0x002662e7;
    }
    uint8_t* const manager = title_global<uint8_t*>(0x0095f5c4);  // shakeRumbleMgr
    if (!manager) return;
    veh_call_at(B, 0x00266300, own_address(&ShakeRumbleDesc_Init_entry), TitleRegs{0, ent_w, 0, 0, B + 0x50, at<uint32_t>(hit, 0x244)});  // ShakeRumbleDesc_Init
    by(0x50) = 1;
    by(0x70) = 1;
    const float zero = 0.0f;
    fl(0x58) = title_global<float>(0x0083eb24);
    fl(0x60) = title_global<float>(0x0083ebf8);
    fl(0x54) = zero;
    fl(0x78) = title_global<float>(0x0083ed28);
    fl(0x74) = zero;
    fl(0x80) = title_global<float>(0x0083ebf8);
    fl(0x7c) = zero;
    fl(0x84) = zero;
    {
        const uint32_t args[3] = {address_of(manager), B + 0x4c, 0x3f800000};
        veh_call_at(B, 0x0026637e, own_address(&ShakeRumble_Start), TitleRegs{1, ent_w, B + 0x50, 0, B + 0x50, at<uint32_t>(hit, 0x244)}, 3,
                    args);  // ShakeRumble_Start
    }
    veh_call_at(B, 0x00266385, own_address(&ShakeRumbleDesc_Destroy), TitleRegs{B + 0x50, ent_w, 0, 0, B + 0x50, at<uint32_t>(hit, 0x244)});  // ShakeRumbleDesc_Destroy
}

namespace {

// sqrt(d2) on the x87, stored as float in `dist`; depth = (radius - the unrounded root) + skin, stored as float.
void veh_x87_sqrt_depth(float d2, float radius, float skin, float* dist, float* depth) {
    __asm__("flds %2\n\tfsqrt\n\tfsts %0\n\tflds %3\n\tfsub %%st(1), %%st\n\tfadds %4\n\tfstps %1\n\tfstp %%st(0)"
            : "=m"(*dist), "=m"(*depth)
            : "m"(d2), "m"(radius), "m"(skin)
            : "st");
}

// ((origin + r0 * l.x) + r1 * l.y) + r2 * l.z over four lanes (a local point back to the world).
void veh_to_world(const float* m, const float* l, float* out) {
    for (uint32_t k = 0; k < 4; ++k) out[k] = ((m[12 + k] + m[k] * l[0]) + m[4 + k] * l[1]) + m[8 + k] * l[2];
}

}  // namespace

uint8_t XB_CDECL Vehicle_ClipAgainstEntities(uint8_t* ent, uint8_t* triangles, uint8_t* brushes, const uint8_t* record,
                                             const float* point, float radius, float* out) {
    // Never reached (under Vehicle_SlideMove). A sphere (point, radius) pushed out of what
    // Vehicle_GatherCollisionCandidates found; out starts as the point and carries each push. Entities of the
    // record (count +0, handles +0x30; not `ent`, with a model +0xdc), in their frame (G_GetEntityMatrix): with
    // flag 0x800000 (+0x134) a box (model +0x10 / +0x20; 0.01 out past the radius), else the model's brushes
    // (count +0, 0x40-byte entries from +4) whose centre lies within 9 along row 2; brushes (list +0x800, count
    // +0x804) within 9 in height (CM_PushPointOutOfBrush); triangles (8-byte entries mesh, index from +0x1000,
    // count +0x1004) with |normal z| <= 0.7 the sphere is in front of (CM_PushSphereOutOfTriangle). 1 = pushed.
    for (uint32_t k = 0; k < 4; ++k) out[k] = point[k];
    uint8_t pushed = 0;
    const float nine = title_global<float>(0x0083eef8);  // 9
    for (int32_t n = 0; n < at<int32_t>(record, 0); ++n) {
        uint8_t* const e = entity_of_handle(at<uint32_t>(record, 0x30 + n * 4));
        if (!e || e == ent || !at<uint32_t>(e, 0xdc)) continue;
        alignas(16) float m[16];
        G_GetEntityMatrix(e, m);
        alignas(16) float p[4] = {out[0], out[1], out[2], out[3]};
        const uint8_t* const model = at<uint8_t*>(e, 0xdc);
        // Into the entity's frame: the transposed rows (lane 3 repeats row 2's lane) times p - origin.
        alignas(16) float local[4];
        {
            const float d[4] = {p[0] - m[12], p[1] - m[13], p[2] - m[14], p[3] - m[15]};
            const float t0[4] = {m[0], m[4], m[8], m[8]}, t1[4] = {m[1], m[5], m[9], m[9]},
                        t2[4] = {m[2], m[6], m[10], m[10]};
            for (uint32_t k = 0; k < 4; ++k) local[k] = (t0[k] * d[0] + t1[k] * d[1]) + t2[k] * d[2];
        }
        if (at<uint32_t>(e, 0x134) & 0x800000) {
            const float* const lo = &at<float>(model, 0x10);
            const float* const hi = &at<float>(model, 0x20);
            float v[4];
            for (uint32_t k = 0; k < 4; ++k) v[k] = (local[k] - hi[k]) * m[8 + k];
            if (!(nine > (v[0] + v[1]) + v[2])) continue;
            float diff[4], sq[4];
            for (uint32_t k = 0; k < 4; ++k) {
                float c = local[k] > lo[k] ? local[k] : lo[k];  // MAXPS / MINPS: the second operand on NaN
                c = c < hi[k] ? c : hi[k];
                diff[k] = local[k] - c;
                sq[k] = diff[k] * diff[k];
            }
            const float d2 = (sq[0] + sq[1]) + sq[2];
            if (!(radius * radius > d2) || !(d2 > title_global<float>(0x0083eb4c))) continue;  // 0.001
            float dist, depth;
            veh_x87_sqrt_depth(d2, radius, title_global<float>(0x0083eb94), &dist, &depth);  // 0.01
            pushed = 1;
            for (uint32_t k = 0; k < 4; ++k) local[k] = local[k] + depth * (diff[k] / dist);
            veh_to_world(m, local, out);
            continue;
        }
        const uint8_t* brush = at<uint8_t*>(model, 4);
        for (uint32_t k = 0; k < at<uint32_t>(model, 0); ++k, brush += 0x40) {
            float v[4];
            for (uint32_t j = 0; j < 4; ++j)
                v[j] = (local[j] - (at<float>(brush, 0x10 + j * 4) + at<float>(brush, 0x20 + j * 4))) * m[8 + j];
            if (!(nine > (v[0] + v[1]) + v[2])) continue;
            pushed |= CM_PushPointOutOfBrush(brush, local, radius, local);
        }
        veh_to_world(m, local, out);
    }
    const uint8_t* const* const brush_list = at<const uint8_t* const*>(brushes, 0x800);
    for (int32_t k = 0, count = at<int32_t>(brushes, 0x804); k < count; ++k) {
        const uint8_t* const b = brush_list[k];
        float centre[4];  // ADDPS: all four lanes (MXCSR flags)
        for (uint32_t j = 0; j < 4; ++j) centre[j] = at<float>(b, 0x10 + j * 4) + at<float>(b, 0x20 + j * 4);
        platform_keep(centre);
        if (!(nine > out[2] - centre[2])) continue;
        if (CM_PushPointOutOfBrush(b, out, radius, out)) pushed = 1;
    }
    const uint8_t* const tri_list = at<const uint8_t*>(triangles, 0x1000);
    for (int32_t k = 0, count = at<int32_t>(triangles, 0x1004); k < count; ++k) {
        const uint8_t* const mesh = at<const uint8_t*>(tri_list, k * 8);
        const uint32_t index = at<uint32_t>(tri_list, k * 8 + 4);
        alignas(16) float plane[4];
        std::memcpy(plane, at<uint8_t*>(mesh, 0x44) + index * 16, 16);
        uint32_t bits;
        std::memcpy(&bits, &plane[2], 4);
        bits &= 0x7fffffffu;
        float nz;
        std::memcpy(&nz, &bits, 4);
        if (nz > title_global<float>(0x0083ed28)) continue;  // 0.7: FCOMIP + JA, NaN goes on
        float prod[4], side[4];  // MULPS / ADDPS over all four lanes (MXCSR flags); lane 0 is the plane side
        for (uint32_t j = 0; j < 4; ++j) prod[j] = plane[j] * out[j];
        side[0] = ((prod[0] + prod[1]) + prod[2]) + plane[3];
        for (uint32_t j = 1; j < 4; ++j) side[j] = prod[j] + plane[3];
        platform_keep(side);
        if (0.0f > side[0]) continue;
        const uint32_t tri = at<uint32_t>(at<uint8_t*>(mesh, 0x3c), index * 4);
        const uint8_t* const verts = at<uint8_t*>(mesh, 0x34);
        alignas(16) float a[4], b[4], c[4];
        std::memcpy(a, verts + (tri & 0x3f) * 16, 16);
        std::memcpy(b, verts + ((tri >> 6) & 0x3f) * 16, 16);
        std::memcpy(c, verts + ((tri >> 12) & 0x3f) * 16, 16);
        pushed |= CM_PushSphereOutOfTriangle(out, 0, out, radius, a, b, c, plane);
    }
    return pushed;
}

PLATFORM_ASM_ENTRY(Vehicle_SlideMove_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Vehicle_SlideMove) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL Vehicle_SlideMove(uint8_t* ent, int32_t msec, uint32_t move, uint32_t force) {
    // Entity in EBX. Never reached (only under Vehicle_GroundMove, tank driving). Three spheres along the
    // vehicle's tag frame (vehicle +0x2bc; rows 0 and 2 times 0x008ff000 / 0x008feffc: front, back, top; radii
    // def +0x68, +0x68, 0.85 * +0x68) go from the move's end point (origin + velocity * msec / 1000, or the
    // origin) half the def's height (+0x70 / 2 - +0x68; 0.7 of it for the third) out along their direction and are
    // pushed out of the gathered world triangles, brushes and entities (Vehicle_ClipAgainstEntities); each push
    // restarts the round at the second sphere, at most three. With `move`, the end point becomes the origin unless
    // it was pushed three times (unless `force`) or moved 36 or more in height; faster than 40 and over a second
    // since +0x304, the last push direction against the velocity gives a hit impulse (+0x334, Vehicle_ApplyPush,
    // +0x330 = 1). Returns the push count. Lane 3 of the frame rows, the end point and the push results are
    // stack words the original never sets (0 here).
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    float* const motion = reinterpret_cast<float*>(vehicle + 0xc0);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    const float dt = static_cast<float>(msec) * title_global<float>(0x0083eb4c);  // 0.001
    alignas(16) float target[4] = {};
    if (move) {
        target[0] = motion[0x10] * dt + motion[0];
        target[1] = motion[0x11] * dt + motion[1];
        target[2] = motion[0x12] * dt + motion[2];
    } else {
        target[0] = motion[0];
        target[1] = motion[1];
        target[2] = motion[2];
    }
    // Candidate lists: 0x1000 / 0x800 bytes of entries, then the write pointer and the count.
    alignas(16) uint8_t triangles[0x1008];
    alignas(16) uint8_t brushes[0x808];
    at<uint32_t>(triangles, 0x1004) = 0;
    at<uint32_t>(brushes, 0x804) = 0;
    at<uint32_t>(triangles, 0x1000) = address_of(triangles);
    at<uint32_t>(brushes, 0x800) = address_of(brushes);
    if (!(title_global<uint32_t>(0x0098eb90) & 1)) {  // the entity record's list, cleared once
        title_global<uint32_t>(0x0098eb90) |= 1;
        std::memset(pointer<uint8_t>(0x0098e990), 0, 0x200);
    }
    {
        const TitleRegs regs{address_of(motion), address_of(ent), address_of(motion), address_of(motion), address_of(def), 0};
        const uint32_t args[6] = {address_of(ent), 0x43480000u, at<uint32_t>(ent, 0x2a8), address_of(triangles),
                                  address_of(brushes), 0x0098e960};  // pad 200
        g_ent_call(own_address(&Vehicle_GatherCollisionCandidates_entry), &regs, 6, args);  // Vehicle_GatherCollisionCandidates
    }
    alignas(16) float m[16];
    G_DObjGetWorldTagMatrix(ent, at<uint32_t>(vehicle, 0x2bc), m);
    const float half = at<float>(def, 0x70) * title_global<float>(0x0083eb24) - at<float>(def, 0x68);  // 0.5
    const float c0 = title_global<float>(0x008feffc), c1 = title_global<float>(0x008ff000);
    const float row0[4] = {m[0], m[1], m[2], 0.0f}, row2[4] = {m[8], m[9], m[10], 0.0f};
    alignas(16) float probes[3][4];
    for (uint32_t k = 0; k < 4; ++k) {
        const float up = c0 * row2[k];
        probes[2][k] = up;
        probes[0][k] = c1 * row0[k] + up;
        probes[1][k] = (0.0f - c1) * row0[k] + up;
    }
    const float radius = at<float>(def, 0x68);
    const float radii[3] = {radius, radius, radius * title_global<float>(0x0083f008)};  // 0.85
    uint32_t hits = 0;
    alignas(16) float out[4] = {};
    float push[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t i = 0; i < 3; ++i) {
        if (hits >= 3) break;
        const float* const probe = probes[i];
        const float len2 = (probe[0] * probe[0] + probe[1] * probe[1]) + probe[2] * probe[2];
        float len = len2;  // UCOMISS + JNE: 0 or NaN divides by itself
        if (!(len2 == 0.0f || len2 != len2)) {
            const float rs = platform_rsqrt_estimate(len2);
            const float a = len2 * rs;
            len = ((title_global<float>(0x0082e660) - a * rs) * a) * title_global<float>(0x0082e670);  // 3, 0.5
        }
        alignas(16) float dir[4];
        for (uint32_t k = 0; k < 4; ++k) dir[k] = probe[k] / len;
        const float scale = (i == 2 ? title_global<float>(0x0083ed28) : title_global<float>(0x0083eb20)) * half;  // 0.7 / 1
        alignas(16) float point[4];
        for (uint32_t k = 0; k < 4; ++k) point[k] = target[k] + dir[k] * scale;
        uint32_t hit;
        {
            float r = radii[i];
            uint32_t r_bits;
            std::memcpy(&r_bits, &r, 4);
            const TitleRegs regs{address_of(brushes), address_of(ent), address_of(triangles), address_of(point), hits,
                                 static_cast<uint32_t>(i)};
            const uint32_t args[7] = {address_of(ent), address_of(triangles), address_of(brushes), 0x0098e960,
                                      address_of(point), r_bits, address_of(out)};
            hit = g_ent_call(own_address(&Vehicle_ClipAgainstEntities), &regs, 7, args) & 0xff;  // Vehicle_ClipAgainstEntities
        }
        float add = 0.0f;
        if (hit) {
            push[2] = out[2] - point[2];
            push[1] = out[1] - point[1];
            push[0] = out[0] - point[0];
            const float length = title_ci_sqrt((push[2] * push[2] + push[1] * push[1]) + push[0] * push[0]);
            if (!(length == title_global<float>(0x0083eafc))) {  // FUCOMIP + JNP: NaN normalizes too
                const float inv = title_global<float>(0x0083eb20) / length;
                push[0] = inv * push[0];
                push[1] = inv * push[1];
                push[2] = inv * push[2];
            }
            ++hits;
            add = title_global<float>(0x0083eb20);
            i = 0;
        }
        const float back = 0.0f - (add + scale);
        for (uint32_t k = 0; k < 4; ++k) target[k] = out[k] + dir[k] * back;
    }
    if (!move) return hits;
    if (hits < 3 || force) {
        uint32_t bits;
        const float dz = motion[2] - target[2];
        std::memcpy(&bits, &dz, 4);
        bits &= 0x7fffffffu;
        float height;
        std::memcpy(&height, &bits, 4);
        if (title_global<float>(0x0083f01c) > height)  // 36
            for (uint32_t k = 0; k < 4; ++k) motion[k] = target[k];
    }
    if (!hits) return 0;
    float* const vel = motion + 0x10;
    const float speed2 = (vel[2] * vel[2] + vel[1] * vel[1]) + vel[0] * vel[0];
    if (!(title_ci_sqrt(speed2) > title_global<float>(0x0083ec84))) return hits;  // 40
    if (!(at<int32_t>(vehicle, 0x304) < title_global<int32_t>(0x0098540c) - 1000)) return hits;  // level.time
    long double v = platform_x87_ld(title_ci_sqrt(speed2));
    v = v - platform_x87_ld(title_global<float>(0x0083eb5c));  // 20
    v = v / platform_x87_ld(title_global<float>(0x0083ed04));  // 300
    const float impulse = static_cast<float>(v);
    alignas(16) float n[4];
    {
        const TitleRegs regs{0, address_of(ent), 0, 0, address_of(vel), address_of(n)};
        g_ent_call(own_address(&VectorNormalize2_entry), &regs, 0, nullptr);  // VectorNormalize2 (ESI in, EDI out)
    }
    float f = title_global<float>(0x0083eb20) - ((n[2] * push[2] + n[1] * push[1]) + n[0] * push[0]);
    if (0.0f > f)
        f = 0.0f;
    else if (f > title_global<float>(0x0083eb20))
        f = title_global<float>(0x0083eb20);
    f = f * impulse;
    at<float>(vehicle, 0x334) = f;
    alignas(16) const float direction[4] = {push[0], push[1], push[2], push[0]};
    {
        uint32_t f_bits;
        std::memcpy(&f_bits, &f, 4);
        const TitleRegs regs{0, address_of(ent), 0, 0, address_of(vehicle), 0};
        const uint32_t args[5] = {address_of(ent), address_of(direction), f_bits, 0, 0};
        g_ent_call(own_address(&Vehicle_ApplyPush), &regs, 5, args);  // Vehicle_ApplyPush
    }
    at<uint32_t>(vehicle, 0x330) = 1;
    return hits;
}

namespace {

// Q3's PM_ClipVelocity backoff: the dot product scaled by the overbounce 1.01 (into the plane) or 1/1.01.
float veh_overclip(float d) {
    return 0.0f > d ? d * title_global<float>(0x0083ee34) : d * title_global<float>(0x0083ee30);
}

}  // namespace

uint32_t XB_CDECL Vehicle_PlaneSlideMove(uint8_t* ent, uint32_t gravity, int32_t msec) {
    // Dead (no caller, no pointer): Q3's PM_SlideMove for a vehicle's motion (+0xc0 origin, +0x100 velocity), up to
    // four bumps against at most five planes (g_vehicleGroundNormal when 0x009885b0 is set, the velocity's direction,
    // then the hit normals) with the box of the vehicle definition (+0x1a0 / +0x1b0). Gravity 800 averages the fall
    // into the move and restores the end velocity. 1 = clipped by something that is not an entity of type 1 or 0xe.
    uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const uint8_t* const def = vehicle_def(at<int16_t>(vehicle, 0x178));
    float* const origin = reinterpret_cast<float*>(vehicle + 0xc0);
    float* const vel = origin + 0x10;
    const float* const ground = pointer<const float>(0x00988580);
    float time_left = static_cast<float>(msec) * title_global<float>(0x0083eb4c);  // 0.001
    alignas(16) uint8_t trace[0x40];
    at<uint32_t>(trace, 0x30) = 0;
    uint32_t hit_other = 0;
    float primal[3];  // set only with gravity (the original reads it either way)
    if (gravity) {
        const float fall = time_left * title_global<float>(0x0083f194);  // 800
        primal[0] = vel[0];
        primal[1] = vel[1];
        primal[2] = vel[2] - fall;
        vel[2] = (primal[2] + vel[2]) * title_global<float>(0x0083eb24);  // 0.5
        if (title_global<uint32_t>(0x009885b0)) {
            const float d = veh_overclip((vel[1] * ground[1] + vel[2] * ground[2]) + vel[0] * ground[0]);
            vel[0] = vel[0] - d * ground[0];
            vel[1] = vel[1] - d * ground[1];
            vel[2] = vel[2] - d * ground[2];
        }
    }
    alignas(16) float planes[5][4];
    int32_t numplanes = 0;
    if (title_global<uint32_t>(0x009885b0)) {
        planes[0][0] = ground[0];
        planes[0][1] = ground[1];
        planes[0][2] = ground[2];
        numplanes = 1;
    }
    Vec4Normalize(vel, planes[numplanes]);
    ++numplanes;
    int32_t bump = 0;
    for (; bump < 4; ++bump) {
        alignas(16) float end[4];
        end[0] = vel[0] * time_left + origin[0];
        end[1] = vel[1] * time_left + origin[1];
        end[2] = vel[2] * time_left + origin[2];
        {
            const TitleRegs regs{0, address_of(origin), 0, 0, 0, 0};
            const uint32_t args[11] = {address_of(trace), address_of(def + 0x1a0), address_of(def + 0x1b0), address_of(end),
                                       at<uint32_t>(ent, 0x224), at<uint32_t>(ent, 0x2a8), 1, 0, 0, 0, 0};
            g_ent_call(own_address(&SV_Trace_entry), &regs, 11, args);  // SV_Trace (start in EBX)
        }
        if (at<uint8_t>(trace, 0x3c)) {  // all solid
            vel[2] = 0.0f;
            return 1;
        }
        const float fraction = at<float>(trace, 0x20);
        if (fraction > 0.0f) {
            origin[0] = at<float>(trace, 0);
            origin[1] = at<float>(trace, 4);
            origin[2] = at<float>(trace, 8);
        }
        if (fraction == title_global<float>(0x0083eb20)) break;  // UCOMISS + JNP: only an exact 1
        const uint32_t handle = at<uint32_t>(trace, 0x30);
        if (handle && (at<uint8_t>(entity_of_handle(handle), 0) == 0xe || at<uint8_t>(entity_of_handle(handle), 0) == 1))
            hit_other = 1;
        time_left = time_left - fraction * time_left;
        if (numplanes >= 5) {
            vel[2] = 0.0f;
            vel[1] = 0.0f;
            vel[0] = 0.0f;
            return hit_other == 0;
        }
        const float* const normal = &at<float>(trace, 0x10);
        int32_t i = 0;
        for (; i < numplanes; ++i)
            if ((planes[i][0] * normal[0] + planes[i][2] * normal[2]) + normal[1] * planes[i][1] >
                title_global<float>(0x0083ed08))  // 0.99: the same plane again, nudge off it
                break;
        if (i < numplanes) {
            vel[0] = vel[0] + normal[0];
            vel[1] = normal[1] + vel[1];
            vel[2] = normal[2] + vel[2];
            continue;
        }
        planes[numplanes][0] = normal[0];
        planes[numplanes][1] = normal[1];
        planes[numplanes][2] = normal[2];
        ++numplanes;
        for (i = 0; i < numplanes; ++i) {
            const float* const p = planes[i];
            if ((p[0] * vel[0] + p[2] * vel[2]) + p[1] * vel[1] >= title_global<float>(0x0083ebf8)) continue;  // 0.1
            // Slide along the plane; the end velocity too.
            float b = veh_overclip((p[1] * vel[1] + vel[2] * p[2]) + p[0] * vel[0]);
            alignas(16) float clip[4];
            clip[0] = vel[0] - p[0] * b;
            clip[1] = vel[1] - p[1] * b;
            clip[2] = vel[2] - b * p[2];
            b = veh_overclip((p[2] * primal[2] + p[1] * primal[1]) + p[0] * primal[0]);
            float end_clip[3] = {primal[0] - p[0] * b, primal[1] - p[1] * b, primal[2] - p[2] * b};
            for (int32_t j = 0; j < numplanes; ++j) {
                if (j == i) continue;
                const float* const q = planes[j];
                if ((clip[0] * q[0] + clip[1] * q[1]) + clip[2] * q[2] >= title_global<float>(0x0083ebf8)) continue;
                b = veh_overclip((clip[0] * q[0] + clip[1] * q[1]) + clip[2] * q[2]);
                clip[0] = clip[0] - q[0] * b;
                clip[1] = clip[1] - q[1] * b;
                clip[2] = clip[2] - q[2] * b;
                b = veh_overclip((end_clip[0] * q[0] + end_clip[1] * q[1]) + end_clip[2] * q[2]);
                end_clip[0] = end_clip[0] - b * q[0];
                end_clip[1] = end_clip[1] - b * q[1];
                end_clip[2] = end_clip[2] - b * q[2];
                if ((clip[0] * p[0] + clip[1] * p[1]) + clip[2] * p[2] >= title_global<float>(0x0083eafc)) continue;
                // Along the crease of the two planes (packed cross product, all four lanes).
                alignas(16) float dir[4];
                dir[0] = p[1] * q[2] - p[2] * q[1];
                dir[1] = p[2] * q[0] - p[0] * q[2];
                dir[2] = p[0] * q[1] - p[1] * q[0];
                dir[3] = p[3] * q[3] - p[3] * q[3];
                Vec3NormalizeSimd(dir);
                b = (dir[1] * vel[1] + dir[2] * vel[2]) + vel[0] * dir[0];
                clip[2] = dir[2] * b;
                clip[0] = dir[0] * b;
                clip[1] = dir[1] * b;
                b = (dir[0] * primal[0] + dir[1] * primal[1]) + dir[2] * primal[2];
                end_clip[2] = dir[2] * b;
                end_clip[0] = dir[0] * b;
                end_clip[1] = dir[1] * b;
                for (int32_t k = 0; k < numplanes; ++k) {
                    if (k == i || k == j) continue;
                    if (!((planes[k][0] * clip[0] + planes[k][2] * clip[2]) + clip[1] * planes[k][1] >=
                          title_global<float>(0x0083ebf8))) {  // a third plane: stop dead
                        vel[2] = 0.0f;
                        vel[1] = 0.0f;
                        vel[0] = 0.0f;
                        return hit_other == 0;
                    }
                }
            }
            vel[0] = clip[0];
            vel[1] = clip[1];
            vel[2] = clip[2];
            primal[0] = end_clip[0];
            primal[1] = end_clip[1];
            primal[2] = end_clip[2];
            break;
        }
    }
    if (gravity) {
        vel[0] = primal[0];
        vel[1] = primal[1];
        vel[2] = primal[2];
    }
    return !hit_other && bump != 0;
}

// 0x00248e10 (EAX: entity): 1 when the entity is of type 2 and its weapon record (+0x3b0, into the 0x34-byte
// records at 0x0096bd10) has +0x24 == 1, else 0.
uint32_t XB_REGS1 XB_CDECL G_EntityWeaponRecordFlag(uint8_t* ent) {
    if (ent[0] != 2) return 0;
    const int32_t index = (static_cast<int32_t>(at<uint32_t>(ent, 0x3b0)) - 0x0096bd10) / 0x34;
    return title_global<uint32_t>(0x0096bd34 + static_cast<uint32_t>(index * 0x34)) == 1 ? 1 : 0;
}

// ---- Gap functions (no Ghidra functions before 11-11): vehicle / weapon file parse callbacks ----

int32_t XB_CDECL Vehicle_ParseTypeField(uint8_t* def, const char* value, int32_t type) {
    using Break = void XB_CDECL(int32_t, const char*, uintptr_t);
    if (type != 8) {
        title_function<Break>(0x002938f0)(1, reinterpret_cast<const char*>(uintptr_t{0x0083a178}),
                                          static_cast<uintptr_t>(type));  // __break "Bad vehicle field type %i"
        return 0;
    }
    // "** unknown **", "4 wheel", "tank", "plane", "boat", "artillery"
    int32_t index = 0;
    for (; index < 6; ++index) {
        if (!title_stricmp(value, reinterpret_cast<const char*>(uintptr_t{title_global<uint32_t>(0x008dae88 + index * 4)}))) {
            *reinterpret_cast<uint16_t*>(def + 0x20) = static_cast<uint16_t>(index);
            break;
        }
    }
    if (index == 6)
        title_function<Break>(0x002938f0)(1, reinterpret_cast<const char*>(uintptr_t{0x0083a194}),
                                          reinterpret_cast<uintptr_t>(value));  // __break "Unknown vehicle type"
    return 1;
}

uint32_t XB_CDECL BG_ParseWeaponFile(const char* name, uint8_t* map) {
    const uint32_t index = ++title_global<uint32_t>(0x0095f4d4);
    uint8_t* const def = BG_AllocWeaponDef(index, &title_global<uint32_t>(0x008d2440), 0x10a);
    *reinterpret_cast<uint32_t*>(def + 8) = name[0] ? address_of(name) : 0x0095f4dd;  // "" for no name
    *reinterpret_cast<uint32_t*>(def + 4) = Com_HashKeyNoCase(0, name);
    const uint32_t parsed = Info_ParseFieldsToStruct(0, &title_global<uint8_t>(0x008d2440), def, 0x10a, map, 0xf,
                                                     own_address(&WeaponDef_ParseEnumField), own_address(&WeaponDef_SetStringField));
    if (parsed) return parsed;
    // A def that did not parse is dropped again.
    title_global<uint32_t>(title_global<uint32_t>(0x0095f4d8) + title_global<uint32_t>(0x0095f4d4) * 4) = 0;
    return --title_global<uint32_t>(0x0095f4d4);
}

// --- Gap functions (no Ghidra function until 2026-11-11) ---

uint32_t XB_CDECL Vehicle_ParseDef(const char* name, uint8_t* info) {
    const auto f = [](uint8_t* def, uint32_t offset) -> float& { return *reinterpret_cast<float*>(def + offset); };
    const auto w = [](uint8_t* def, uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(def + offset); };
    char message[0x80];
    if (std::strlen(name) > 0x20)
        title_sprintf(message, &title_global<const char>(0x0083a040), name);  // "Vehicle name too long(32max): %s"
    uint8_t* const def = Heap_AllocAligned(0x10, 0x1d0);
    title_global<uint8_t*>(0x00950a28 + static_cast<int32_t>(title_global<int16_t>(0x0095e228)) * 4) = def;
    std::memset(def, 0, 0x1d0);
    for (uint32_t i = 0;; ++i) {
        def[i] = static_cast<uint8_t>(name[i]);
        if (!name[i]) break;
    }
    if (!Info_ParseFieldsToStruct(0, &title_global<const uint8_t>(0x008daed0), def, 0x25, info, 9, own_address(&Vehicle_ParseTypeField),
                                  own_address(&VehicleDef_CopyStringField)))
        return 0;
    const float scale = title_global<float>(0x0083ee64);  // 17.6
    f(def, 0x48) = f(def, 0x48) * scale;
    f(def, 0x60) = f(def, 0x60) * scale;
    f(def, 0x44) = f(def, 0x44) * scale;
    f(def, 0x194) = f(def, 0x194) * scale;
    w(def, 0x18c) = 0;
    *reinterpret_cast<uint16_t*>(def + 0x190) = 0;
    if (!w(def, 0x74)) w(def, 0x74) = 0x5dc;
    if (f(def, 0x68) == 0.0f) f(def, 0x68) = title_global<float>(0x0083ef00);  // 75
    if (f(def, 0x70) == 0.0f) f(def, 0x70) = title_global<float>(0x0083ed04);  // 300
    if (f(def, 0x68) > title_global<float>(0x0083eb90))                          // 100
        title_sprintf(message, &title_global<const char>(0x0083a018), name, 100);  // "Bounds radius too big ..."
    if (f(def, 0x6c) == 0.0f) f(def, 0x6c) = title_global<float>(0x0083ebf0);  // 150
    const uint32_t radius = w(def, 0x68);
    const uint32_t height = w(def, 0x6c);
    ++title_global<int16_t>(0x0095e228);
    const float zero = 0.0f;
    const float low = zero - f(def, 0x68);
    f(def, 0x1a0) = low;
    f(def, 0x1a4) = low;
    f(def, 0x1a8) = zero;
    w(def, 0x1b0) = radius;
    w(def, 0x1b4) = radius;
    w(def, 0x1b8) = height;
    return radius;
}

uint32_t XB_CDECL WeaponDef_ParseEnumField(uint8_t* def, const char* value, int32_t type) {
    using Break = void XB_CDECL(int32_t, const char*, uint32_t, uint32_t);
    static constexpr struct {
        uint32_t names;    // name table
        int32_t count;
        uint32_t field;    // the index's field in the def
        uint32_t message;  // "Unknown ... \"%s\" in \"%s\""
    } kTables[7] = {
        {0x008d237c, 6, 0x94, 0x00835738},   // weapon type
        {0x008d23d0, 13, 0x98, 0x00835714},  // weapon class
        {0x008d2404, 6, 0xa8, 0x008356f0},   // ammo type
        {0x008d2394, 5, 0x638, 0x008356c0},  // overlay reticle
        {0x008d23a8, 7, 0x9c, 0x0083569c},   // weapon slot
        {0x008d23c4, 3, 0xa4, 0x00835674},   // weapon stance
        {0x008d241c, 9, 0x73c, 0x00835648},  // projectile explosion
    };
    if (static_cast<uint32_t>(type - 8) > 6) {
        title_function<Break>(0x002938f0)(1, pointer<const char>(0x0083562c), static_cast<uint32_t>(type),
                                          at<uint32_t>(def, 8));  // __break "Bad field type %i in %s"
        return 0;
    }
    const auto& table = kTables[type - 8];
    int32_t i = 0;
    for (; i < table.count; ++i) {
        if (!own_function<int32_t XB_CDECL(const char*, const char*)>(&title_stricmp)(
                value, title_global<const char*>(table.names + static_cast<uint32_t>(i) * 4))) {
            at<int32_t>(def, table.field) = i;
            break;
        }
    }
    if (i == table.count)
        title_function<Break>(0x002938f0)(1, pointer<const char>(table.message),
                                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)), at<uint32_t>(def, 8));
    return 1;
}

}  // namespace game
