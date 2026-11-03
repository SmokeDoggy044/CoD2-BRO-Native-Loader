#include "g_vehicle.h"

#include <cstring>

#include "bg_misc.h"
#include "dobj.h"
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

}  // namespace game
