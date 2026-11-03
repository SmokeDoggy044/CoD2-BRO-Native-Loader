#include "g_vehicle.h"

#include <cstring>

#include "bg_misc.h"
#include "cm.h"
#include "dobj.h"
#include "g_client.h"
#include "g_main.h"
#include "g_script.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "phys.h"
#include "script_string.h"
#include "sv.h"
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
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

// The title CRT's __CIfmod (0x0033acda): x in ST1, y in ST0, fmod(x, y) left in ST0.
long double title_ci_fmod(long double x, long double y) {
    long double result;
    __asm__ volatile("call *%3"
                     : "=t"(result)
                     : "0"(y), "u"(x), "r"(uintptr_t{0x0033acda})
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
        eax = static_cast<uint32_t>(title_function<StrICmp>(0x001f1443)(name, reinterpret_cast<const char*>(vehicle_def(i))));  // _stricmp
        if (eax == 0) return static_cast<uint16_t>(i);
    }
    return eax | 0xffff;
}

void XB_CDECL Vehicle_FreeDefs() {
    for (int32_t i = 0; i < vehicle_def_count(); ++i)
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(address_of(vehicle_def(i)));  // Z_Free
    vehicle_def_count() = 0;
}

PLATFORM_ASM_ENTRY(BG_AllocWeaponDef_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(BG_AllocWeaponDef) "addl $12, %esp\n"
                   "ret")

uint8_t* XB_CDECL BG_AllocWeaponDef(uint32_t index, const uint32_t* fields, int32_t count) {
    uint8_t* def = static_cast<uint8_t*>(title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(0x870));  // Z_Malloc
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
        if (static_cast<long double>(magnitude) > static_cast<long double>(title_global<float>(0x0083eed0))) {
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
        if (static_cast<long double>(d) > static_cast<long double>(title_global<float>(0x0083eb60))) d = d - full;
        if (title_global<float>(0x0083ebd0) > d) d = d + full;
        return d;
    };
    for (int i = 0; i < 2; ++i) {
        const float step = speed[i] * title_global<float>(0x008ddaa0);
        const long double difference = static_cast<long double>(target[i]) - angles[i];
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
    float d = wrap(static_cast<long double>(pitch) - before);
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
        vehicle_call(0x002ddf80, body, 0, 0, address_of(ent + 0x1b0), body + 0x1a0, 0);  // G_SetPoseFromPhysics
    }
}

uint32_t XB_REGS3 XB_CDECL Turret_IsUsableBy(uint8_t* player, uint32_t, uint8_t* turret) {
    const uint8_t* info = at<uint8_t*>(turret, 0x24c);
    if (!(info[3] & 0x10)) return 0;
    if (turret[0x297]) return 0;
    if (!info) return 0;
    if (!at<uint32_t>(turret, 0x29c)) return 0;
    if (!vehicle_call(0x00254bd0, at<uint32_t>(turret, 0x29c), 0, 0, address_of(turret), address_of(player), 0)) return 0;
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
        vehicle_call(0x0024ac50, 0, 0, 0, address_of(ent), static_cast<uint32_t>(i) * 0x340, ebx);  // G_RunThink
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
                vehicle_call(0x002d87a0, address_of(ent), 0, 0, address_of(ent), 0, 0);  // SV_SetBrushModel
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
        return vehicle_call(0x00260480, address_of(client), 0, 0, 0, address_of(other), 0);  // Vehicle_ExitPlayer
    return vehicle_call(0x0025bb60, address_of(self), 0, 0, 0, address_of(other), 0);  // Vehicle_EnterPlayer
}

void XB_CDECL Vehicle_Controller(uint8_t* ent) {
    using SetBoneAngles = uint32_t XB_CDECL(uint8_t*, uint32_t, float*, uint32_t, uint32_t);
    const uint8_t* vehicle = at<uint8_t*>(ent, 0x248);
    // Bone angle overrides (0x002de420, controller 0x0082eec0): body pitch/roll, turret yaw,
    // barrel pitch, each when the vehicle has the tag.
    float angles[6] = {at<float>(vehicle, 0x280), 0.0f, at<float>(vehicle, 0x288), 0.0f, 0.0f, 0.0f};
    if (at<int32_t>(vehicle, 0x2bc) >= 0)
        title_function<SetBoneAngles>(0x002de420)(ent, 0x0082eec0, angles, at<uint32_t>(vehicle, 0x2bc), 0);
    angles[1] = at<float>(vehicle, 0x294);
    angles[0] = 0.0f;
    angles[2] = 0.0f;
    angles[3] = at<float>(vehicle, 0x290);
    angles[4] = 0.0f;
    angles[5] = 0.0f;
    if (at<int32_t>(vehicle, 0x2c0) >= 0)
        title_function<SetBoneAngles>(0x002de420)(ent, 0x0082eec0, angles, at<uint32_t>(vehicle, 0x2c0), 0);
    if (at<int32_t>(vehicle, 0x2c4) >= 0)
        title_function<SetBoneAngles>(0x002de420)(ent, 0x0082eec0, angles + 3, at<uint32_t>(vehicle, 0x2c4), 0);
}

void XB_CDECL Vehicle_Die(uint8_t* self, uint32_t, const uint8_t* attacker, uint32_t, uint32_t, uint32_t, uint32_t,
                          const float* direction) {
    at<uint32_t>(at<uint8_t*>(self, 0x248), 0x200) = 0;
    Vehicle_StopEffects(self);
    if (const uint32_t handle = at<uint32_t>(self, 0x1a0)) {
        // The driver gets out (Vehicle_ExitPlayer, player in EDI; 0 when the handle is stale).
        vehicle_call(0x00260480, handle & 0xfff, 0, 0, 0, address_of(entity_from_handle(handle)), 0);
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
    const uint32_t args[6] = {title_global<uint32_t>(0x0095f5d4), address_of(&found), 0x00835620, length, 0x002bc7d0, found};
    return vehicle_call_args(0x00024820, title_global<uint32_t>(0x0095f5d4), at<uint32_t>(title_global<uint8_t*>(0x0095f638), 0x34), 6, args);
}

PLATFORM_ASM_ENTRY(G_ReleaseTurretUser_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_ReleaseTurretUser) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL G_ReleaseTurretUser(uint8_t* turret) {
    const uint32_t handle = at<uint32_t>(turret, 0x1a0);
    if (uint8_t* user = entity_from_handle(handle)) {
        if (at<uint32_t>(user, 0x23c)) {
            vehicle_call(0x0025b3f0, address_of(turret), 0, 0, 0, address_of(turret), 0);  // G_ClientStopUsingTurret
        } else if (const uint32_t actor = at<uint32_t>(user, 0x240)) {
            vehicle_call(0x00205ef0, actor, 0, 0, actor, address_of(turret), 0);  // Actor_LeaveTurret
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
    auto radius = [ent]() { return static_cast<long double>(at<float>(ent, 0xf0)) / 2.0L; };
    auto step4 = [](float v) { return static_cast<float>(static_cast<long double>(v) + static_cast<long double>(4.0f)); };
    if (radius() > 4.0L && radius() > 0.0L) {
        for (float a = 0.0f;;) {
            float b = 0.0f - a;
            if (!(a < b)) {
                for (;;) {
                    for (float c = 4.0f; radius() > static_cast<long double>(c); c = step4(c)) {
                        float d = 0.0f - c;
                        if (c < d) continue;
                        for (;;) {
                            for (float e = 4.0f; radius() > static_cast<long double>(e); e = step4(e)) {
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
            if (!(radius() > static_cast<long double>(a))) break;
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
    return title_function<uint32_t XB_CDECL(uint8_t*)>(0x00270560)(ent);  // Vehicle_FireTurretWeapon
}

PLATFORM_ASM_ENTRY(Player_UpdateTurretUse_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(Player_UpdateTurretUse) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Player_UpdateTurretUse(uint8_t* player) {
    uint8_t* turret = entity_from_handle(at<uint32_t>(player, 0x1a0));
    if (turret[0x297] == 1 && at<int32_t>(turret, 0x350) > 0) {
        vehicle_call_ebx(0x00271b60, address_of(player), address_of(player), address_of(turret));
        return;
    }
    vehicle_call(0x0025b3f0, address_of(player), 0, 0, 0, 0, 0);  // G_ClientStopUsingTurret
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
        title_function<Release>(0x0011d640)(entry + 0x10);  // ScriptString_Release
        title_function<Release>(0x0011d640)(entry + 4);
        title_function<Release>(0x0011d640)(entry);
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
    title_function<void XB_STDCALL(uint8_t*)>(0x0022d650)(vehicle);
    if (!copy_matrix) return;
    uint32_t* const to = at<uint32_t*>(at<uint8_t*>(vehicle, 0x444), 8);
    for (uint32_t i = 0; i < 16; ++i) to[i] = at<uint32_t>(vehicle, 0x460 + i * 4);
}

namespace {

// A path node's reset: both script strings cleared, +8/+0xc = -1, the vectors +0x14/+0x20 zero,
// +0x2c = the vector at 0x008db094, +0x38 = 0; bit fields +0x3c: bits 28-29 cleared, the low 28 set.
void path_node_reset(uint8_t* node) {
    using Clear = void XB_THISCALL(uint8_t*);
    title_function<Clear>(0x0011d660)(node);  // ScriptString_Clear
    title_function<Clear>(0x0011d660)(node + 4);
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
    vehicle_call(0x00248080, 0, 0, 0, address_of(state + 0x38), address_of(from), 0);
    vehicle_call(0x00248080, 0, 0, 0, address_of(state + 0x78), address_of(from), 0);
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
    title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x002257f0)(vehicle);
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
    title_function<void XB_STDCALL(uint8_t*, uint32_t, uint32_t)>(0x0022a590)(vehicle, at<uint32_t>(vehicle, 0x420), 1);
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
    vehicle_call(0x002ddf80, address_of(entity), 0, address_of(record) + 0x160, address_of(m + 16), address_of(entity) + 0x1a0,
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
        const uint32_t origin = call_eax_ecx_ebx(0x0022d580, address_of(ent), i, address_of(buffer));  // G_DObjGetTagLocalOrigin
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
    call_eax_ecx_ebx(0x00233c40, address_of(ent), 0, address_of(record));  // VehiclePhys_Init (EAX entity, EBX record)
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
    title_function<Assign>(0x0011d600)(dst, src);          // ScriptString_Assign
    title_function<Assign>(0x0011d600)(dst + 4, src + 4);  // ScriptString_Assign
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
    const long double rock = platform_x87_sin_mul(angle, at<float>(vehicle, 0x1f0)) * static_cast<long double>(at<float>(vehicle, 0x1f0));
    v[0] = static_cast<float>(rock * static_cast<long double>(at<float>(vehicle, 0x1e8)) + static_cast<long double>(v[0]));
    v[2] = static_cast<float>(rock * static_cast<long double>(at<float>(vehicle, 0x1ec)) + static_cast<long double>(v[2]));
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
        const uint32_t block = title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x10, 0x2000);  // Heap_AllocAligned
        at<uint32_t>(pool, 8 + static_cast<uint32_t>(at<int16_t>(pool, 2)) * 4) = block;
        uint8_t* node = at<uint8_t*>(pool, 8 + static_cast<uint32_t>(at<int16_t>(pool, 2)) * 4);
        std::memset(node, 0, 0x40);  // only the first node's bytes
        using FromRep = void XB_THISCALL(uint8_t*, uint32_t);
        for (uint32_t i = 0; i < 0x80; ++i, node += 0x40) {
            if (!node) continue;
            title_function<FromRep>(0x0011d5d0)(node, 0);         // ScriptString_FromRep
            title_function<FromRep>(0x0011d5d0)(node + 4, 0);     // ScriptString_FromRep
            title_function<FromRep>(0x0011d5d0)(node + 0x10, 0);  // ScriptString_FromRep
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
            title_function<Release>(0x0011d640)(node + 0x10);  // ScriptString_Release
            title_function<Release>(0x0011d640)(node + 4);     // ScriptString_Release
            title_function<Release>(0x0011d640)(node);         // ScriptString_Release
        }
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(at<uint32_t>(pool, 8 + static_cast<uint32_t>(i) * 4));  // Z_Free
    }
    at<uint16_t>(pool, 0) = 0;
    at<uint16_t>(pool, 2) = 0;
    at<uint16_t>(pool, 4) = 0;
    std::memset(pool + 8, 0, 0x40);
    return 0;
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
    const long double diff = static_cast<long double>(at<float>(vehicle, 0xe4)) - static_cast<long double>(at<float>(vehicle, 0xf4));
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
    const auto va_title = title_function<Va>(0x002a93d0);  // va
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
    if (!vehicle_call(0x00248e10, address_of(other), 0, 0, 0, 0, 0)) return 0;  // FUN_00248e10
    if (title_global<int32_t>(0x0098540c) - at<int32_t>(client, 0x62c) <= title_global<int32_t>(0x0095bc34)) return 0;  // level.time
    at<uint32_t>(client, 0x628) = 0;
    vehicle_call(0x0024f570, address_of(ent), 0, 0, 0, 0, title_global<uint32_t>(0x0097a814));  // G_NotifyWithOwnHandle(touch)
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

PLATFORM_ASM_ENTRY(VehNode_FillUnsetAngles_entry,
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
            if (title_function<Equals>(0x0011d820)(node, veh_node(j) + 4)) {  // ScriptString_EqualsString
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
    title_function<VectorCtor>(0x00011240)(list, 4, 0x540, 0x0001d0e0);  // eh_vector_constructor_iterator
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
            const Quad new_lo = quad_sub(lo, margin);
            std::memcpy(cache_lo, new_lo.v, 16);
            const Quad new_hi = quad_add(hi, quad(pointer<const float>(0x0098bf40)));
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
    if (vehicle_call(0x00201d40, 0, 0, address_of(actor), 0, 0, 0)) {  // Actor_IsAtClaimedNode
        const uint8_t* const client = at<uint8_t*>(self, 0x23c);
        const float dy = at<float>(client, 0x5b4) - at<float>(self, 0x144);
        const float dx = at<float>(client, 0x5b0) - at<float>(self, 0x140);
        if (dy * dy + dx * dx >= title_global<float>(0x0083eb94)) return;
        vehicle_call_ebx(0x002048c0, at<uint32_t>(other, 0x244), at<uint32_t>(self, 0x244), 0);  // Sentient_TakeNodeClaim
    }
    const int32_t enemy = title_function<int32_t XB_CDECL(int32_t)>(0x000123d0)(at<int32_t>(at<uint8_t*>(self, 0x244), 4));  // Sentient_EnemyTeam
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

void XB_REGS3 XB_CDECL Vehicle_UpdateWheelBones(uint32_t, uint32_t, const uint8_t* def, uint8_t* ent) {
    using SetBoneAngles = uint32_t XB_CDECL(uint8_t*, const float*, const float*, uint32_t, uint32_t);
    const uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
    const int32_t wheels = at<int16_t>(def, 0x20) != 1 ? 6 : 4;
    // Each wheel bone rolls by +0x1c4; the two steering wheels also turn by +0x270.
    float angles[3] = {0.0f, 0.0f, 0.0f};  // the third word is never written (zero dead stack in tests)
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t i = 0; i < wheels; ++i) {
        const uint32_t bone = at<uint32_t>(vehicle, 0x2dc + static_cast<uint32_t>(i) * 4);
        if (static_cast<int32_t>(bone) <= 0) continue;
        angles[1] = at<float>(vehicle, 0x270);
        angles[0] = at<float>(vehicle, 0x1c4);
        if (i != title_global<int32_t>(0x0095f658) && i != title_global<int32_t>(0x008d9d58)) angles[1] = 0.0f;
        title_function<SetBoneAngles>(0x002de420)(ent, origin, angles, bone, 0);  // G_DObjSetBoneRotation
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
    const long double scaled = (static_cast<long double>(abs_bits(f)) - static_cast<long double>(dead)) /
                               (static_cast<long double>(title_global<float>(0x0083ebe8)) - static_cast<long double>(dead));  // 128
    return static_cast<float>(scaled * static_cast<long double>(static_cast<float>(sign)));
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
    const float magnitude = title_ci_sqrt(static_cast<float>(static_cast<long double>(stick[1]) * stick[1] +
                                                             static_cast<long double>(stick[0]) * stick[0]));
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
    const float limit = static_cast<float>(static_cast<long double>(stick[1]) * title_global<float>(0x0083ec30) +
                                           static_cast<long double>(title_global<float>(0x0083f04c)));
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
        if (static_cast<long double>(top) * static_cast<long double>(title_global<float>(0x0083ed28)) >
            static_cast<long double>(abs_bits(speed))) {  // 0.7, FCOMIP
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
    title_function<TagMatrix>(0x002e3780)(ent, at<int32_t>(vehicle, 0x2b0), seat);  // G_DObjGetWorldTagMatrix
    title_function<TagMatrix>(0x002e3780)(ent, at<int32_t>(vehicle, 0x2c4), gun);
    float angles[3];
    MatrixToAngles(angles, 0, gun);
    angles[2] = 0.0f;
    TeleportPlayer(player, 0, seat + 12);
    SetClientViewAngle(angles, player);
    const TitleRegs regs{0, 0, 0x00838a54, address_of(player), 0, address_of(player)};  // ECX "tag_player", EDX player
    const uint32_t args[3] = {address_of(ent), 0x0082eec0, 0x0082eec0};
    if (!g_ent_call(0x002e44b0, &regs, 3, args))  // G_EntLinkToWithOffset
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
    float body[3] = {0.0f, 0.0f, 0.0f}, turret[3] = {0.0f, 0.0f, 0.0f}, barrel[3] = {0.0f, 0.0f, 0.0f};
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
    const auto set_bone = title_function<SetBoneAngles>(0x002de420);  // G_DObjSetBoneRotation
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
        p = title_function<uint8_t* XB_THISCALL(uint8_t*, uint32_t, uint32_t)>(0x0028c0f0)(context, 0x10, size);  // MemContext_Alloc_r
    }
    if (!p) p = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
                title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x10, size)));  // Heap_AllocAligned
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
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
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
    const uint32_t hit = g_ent_call(0x0025bd10, &regs, 3, first) != 0;  // Vehicle_SlideMove
    const uint32_t second[3] = {edi, 1, hit};
    const int32_t result = static_cast<int32_t>(g_ent_call(0x0025bd10, &regs, 3, second));
    if (!result) {
        at<int32_t>(vehicle, 0x308) = title_global<int32_t>(0x0098540c);  // level.time
        for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(vehicle, 0x310 + k * 4) = at<uint32_t>(motion, k * 4);
        for (uint32_t k = 0; k < 4; ++k) at<uint32_t>(vehicle, 0x320 + k * 4) = at<uint32_t>(motion, 0x20 + k * 4);
        return;
    }
    if (result > 1 && static_cast<int32_t>(g_ent_call(0x0025bd10, &regs, 3, first)) > 1)
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
        const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
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
        float at_pos[3];
        at_pos[0] = static_cast<float>(static_cast<int32_t>(d * fx + origin[0]));
        at_pos[1] = static_cast<float>(static_cast<int32_t>(d * fy + origin[1]));
        at_pos[2] = static_cast<float>(static_cast<int32_t>(d * up + origin[2]));
        using Fire = uint8_t* XB_REGS3 XB_CDECL(const float*, uint32_t, uint8_t*, int32_t);
        grenade = title_function<Fire>(0x0025e450)(at_pos, 0, owner, fuse);  // fire_grenadeCooked
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
    title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x002b7900)(player);  // G_EntUnlink
    // The exit point: tag_detach (vehicle +0x2b4), else the origin raised by the def's +0x1b8.
    float exit[3];
    const int32_t tag = at<int32_t>(at<uint8_t*>(ent, 0x248), 0x2b4);
    if (tag < 0) {
        exit[0] = at<float>(ent, 0x140);
        exit[1] = at<float>(ent, 0x144);
        exit[2] = at<float>(def, 0x1b8) + at<float>(ent, 0x148);
    } else {
        alignas(16) float matrix[16];
        title_function<void XB_CDECL(uint8_t*, int32_t, float*)>(0x002e3780)(ent, tag, matrix);  // G_DObjGetWorldTagMatrix
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
        g_ent_call(0x002e9af0, &regs, 11, args);  // SV_Trace (start in EBX)
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
    const uint32_t id = title_call_at_regs(frame, 0x002a5c40, &regs, 3, args);  // FXCmd_Type8AtTag
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
    title_function<Trace>(0x002600c0)(trace, start, end, pass, 0x280e031, 0x008da6f0, 0);  // G_LocationalTrace
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
    const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
    const float range = static_cast<float>(at<int32_t>(at<uint8_t*>(fire, 0x3c), 0x750)) * title_global<float>(0x0083eca0);
    const float d = static_cast<float>(static_cast<int32_t>(
        (((static_cast<float>(r) / title_global<float>(0x0083ebc8)) * title_global<float>(0x0083eba8)) - title_global<float>(0x0083eb20)) * range));
    float point[3];
    point[0] = d * forward[0] + at<float>(trace, 0);
    point[1] = d * forward[1] + at<float>(trace, 4);
    point[2] = d * forward[2] + at<float>(trace, 8);
    Weapon_SpawnClusterGrenades(owner, fire, point);
}

}  // namespace game
