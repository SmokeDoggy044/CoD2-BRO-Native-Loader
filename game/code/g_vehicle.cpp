#include "g_vehicle.h"

#include <cstring>

#include "bg_misc.h"
#include "dobj.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
#include "qcommon.h"
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

PLATFORM_ASM_ENTRY(Vehicle_FreeDefs_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_FreeDefs) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Vehicle_FreeDefs(uint32_t eax) {
    for (int32_t i = 0; i < vehicle_def_count(); ++i) {
        eax = address_of(vehicle_def(i));
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(eax);  // Z_Free
    }
    vehicle_def_count() = 0;
    return eax;
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

}  // namespace game
