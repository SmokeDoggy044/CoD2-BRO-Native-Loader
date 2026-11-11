#include "g_combat.h"

#include <cstring>

#include "cg_view.h"
#include "fx.h"
#include "g_main.h"
#include "g_spawn.h"
#include "g_script.h"
#include "g_utils.h"
#include "qcommon_fs.h"
#include "script_string.h"
#include "sv.h"
#include "title_data.h"
#include "g_pathnode.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "actor.h"
#include "g_scr_runtime.h"
#include "title_crt.h"

namespace game {

namespace {

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint8_t* pointer_at_word(uint32_t value) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(value)); }

uint8_t* pointer_at(uint8_t* base, uint32_t offset) { return pointer_at_word(field<uint32_t>(base, offset)); }

// The entity of a handle (index & 0xfff below 0x540, generation >> 12), else null.
uint8_t* entity_from_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        return pointer_at_word(title_global<uint32_t>(0x0097a968 + index * 8));
    return nullptr;
}

}  // namespace

uint32_t XB_REGS3 XB_CDECL TriggerDamage_AcceptsMeansOfDeath(int32_t mod, uint32_t, const uint8_t* trigger) {
    // Spawn flags (+0x2a0) that make the trigger ignore groups of means of death.
    const uint32_t flags = *reinterpret_cast<const uint32_t*>(trigger + 0x2a0);
    if ((flags & 0x1) && mod == 1) return 0;
    if ((flags & 0x2) && mod == 2) return 0;
    if ((flags & 0x4) && mod >= 3 && mod <= 6) return 0;
    if ((flags & 0x8) && ((mod >= 9 && mod <= 13) || mod == 0x17)) return 0;
    if ((flags & 0x10) && (mod == 4 || mod == 6 || mod == 10 || mod == 12)) return 0;  // case table 0x00248260
    if ((flags & 0x20) && mod == 7) return 0;
    if ((flags & 0x40) && mod == 0x19) return 0;
    if ((flags & 0x100) && (mod == 0 || (mod > 0xe && mod <= 0x16))) return 0;
    return 1;
}

uint32_t XB_REGS3 XB_CDECL G_ActorClampLethalDamage(uint32_t damage, uint32_t, const uint8_t* ent) {
    if (!damage) return damage;
    const uint32_t actor = *reinterpret_cast<const uint32_t*>(ent + 0x240);
    if (!actor) return 0;
    const TitleRegs regs{damage, 0, address_of(ent), 0, 0, actor};
    return g_ent_call(own_address(&Actor_ClampLethalDamage_entry), &regs, 0, nullptr);  // Actor_ClampLethalDamage (EAX, EDI)
}

// The argument slot keeps level.time as a float (written before anything else, as the original).
PLATFORM_ASM_ENTRY(Pain_Func4_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Pain_Func4) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Pain_Func4(uint32_t* args) {
    uint8_t* const ent = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(args[0]));
    const float now = static_cast<float>(title_global<int32_t>(0x0098540c));  // level.time
    std::memcpy(&args[0], &now, sizeof now);
    const bool keep_origin = (ent[0x2a0] & 4) != 0;
    const int32_t r = own_function<int32_t XB_CDECL()>(&title_rand)() % 1000;  // _rand
    float limit = static_cast<float>(r) + *reinterpret_cast<const float*>(ent + 0x38c);
    limit = limit + *reinterpret_cast<const float*>(ent + 0x384);
    limit = limit + title_global<float>(0x0083ebec);
    float time;
    std::memcpy(&time, &args[0], sizeof time);
    if (!(time > limit)) return;
    *reinterpret_cast<float*>(ent + 0x384) = static_cast<float>(title_global<int32_t>(0x0098540c));
    if (keep_origin) {
        // The original copies +0x2e0 over the current origin and writes the old one back.
        uint32_t saved[3];
        std::memcpy(saved, ent + 0x140, sizeof saved);
        std::memcpy(ent + 0x140, ent + 0x2e0, sizeof saved);
        std::memcpy(ent + 0x140, saved, sizeof saved);
    }
}

// A tail jump to G_FreeEntity: its second argument is the caller's word above ours.
PLATFORM_ASM_ENTRY(hurt_think_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(hurt_think) "addl $4, %esp\n"
                   "ret")

void XB_CDECL hurt_think(const uint32_t* args) {
    uint8_t* const ent = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(args[0]));
    const int32_t now = title_global<int32_t>(0x0098540c);  // level.time
    *reinterpret_cast<int32_t*>(ent + 0x328) = now + 1;    // nextthink
    if (static_cast<float>(title_global<int32_t>(0x0098540c)) > *reinterpret_cast<const float*>(ent + 0x384))
        own_function<void XB_CDECL(uint32_t, uint32_t)>(&G_FreeEntity)(args[0], args[1]);  // G_FreeEntity
}

PLATFORM_ASM_ENTRY(G_ApplyDamage_entry,
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_ApplyDamage) "addl $36, %esp\n"
                   "ret")

void XB_CDECL G_ApplyDamage(const float* dir, uint8_t* targ, int32_t damage, uint8_t* inflictor, uint8_t* attacker,
                            const float* point, int32_t mod, int32_t weapon, int32_t hitloc) {
    // The second half of Q3's G_Damage: the client's damage record (+0x5d4 total, +0x5d8 dir,
    // +0x5e4 = 0), health (+0x350) down, "damage" notify, then death (health clamped to -999,
    // "death" notify for non-clients, die callback) or pain (pain callback with the dir in +0x390
    // and the point in +0x2e0); the client's health copy (+0x100).
    alignas(16) float d[3];
    std::memcpy(d, dir, sizeof d);
    int32_t value = damage;
    uint8_t* const ps = pointer_at(targ, 0x23c);
    if (ps) {
        field<int32_t>(ps, 0x5d4) += damage;
        std::memcpy(ps + 0x5d8, d, sizeof d);
        field<uint32_t>(ps, 0x5e4) = 0;
    }
    if (!damage) return;
    field<int32_t>(targ, 0x350) -= damage;
    {
        uint32_t other = field<uint32_t>(attacker, 0x224);
        const TitleRegs regs{0, 0, 0, 0, 0, 0};
        const uint32_t args[4] = {address_of(targ), title_global<uint32_t>(0x0097a67c), address_of(&value),
                                  address_of(&other)};
        g_ent_call(own_address(&G_NotifyWithIntEntity_entry), &regs, 4, args);  // G_NotifyWithIntEntity "damage"
    }
    const int32_t health = field<int32_t>(targ, 0x350);
    if (health <= 0) {
        if (ps) field<uint32_t>(targ, 0x2a4) |= 0x20;
        if (health < -999) field<int32_t>(targ, 0x350) = -999;
        if (!ps) {
            uint32_t other = field<uint32_t>(attacker, 0x224);
            const TitleRegs regs{0, static_cast<uint32_t>(damage), 0, 0, address_of(attacker), address_of(targ)};
            const uint32_t args[2] = {title_global<uint32_t>(0x0097a684), address_of(&other)};
            g_ent_call(own_address(&G_NotifyWithEntity_entry), &regs, 2, args);  // G_NotifyWithEntity "death"
        }
        field<uint32_t>(targ, 0x374) = address_of(attacker);
        const uint32_t die = field<uint32_t>(targ, 0x344);
        if (die) {
            using Die = void XB_CDECL(uint8_t*, uint8_t*, uint8_t*, int32_t, int32_t, int32_t, const float*, const float*,
                                      int32_t);
            title_function<Die>(title_global<uint32_t>(0x008da88c + die * 4))(targ, inflictor, attacker, damage, mod,
                                                                               weapon, point, d, hitloc);
        }
    } else {
        const uint32_t pain = field<uint32_t>(targ, 0x340);
        if (pain) {
            std::memcpy(targ + 0x390, d, sizeof d);
            const uint32_t* const p = reinterpret_cast<const uint32_t*>(point);
            field<uint32_t>(targ, 0x2e0) = p[0];
            field<uint32_t>(targ, 0x2e4) = p[1];
            field<uint32_t>(targ, 0x2e8) = p[2];
            using Pain = void XB_CDECL(uint8_t*, uint8_t*, int32_t, const float*, int32_t, const float*, int32_t);
            title_function<Pain>(title_global<uint32_t>(0x008da874 + field<uint32_t>(targ, 0x340) * 4))(
                targ, attacker, damage, point, mod, d, hitloc);
        }
    }
    uint8_t* const client = pointer_at(targ, 0x23c);
    if (client) field<int32_t>(client, 0x100) = field<int32_t>(targ, 0x350);
}

// The original writes its argument slots (damage, inflictor, attacker, hit location): the
// entry hands the C version the argument block.
PLATFORM_ASM_ENTRY(G_Damage_entry,
                   "leal 4(%esp), %ecx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_Damage) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_Damage(const float* point, uint32_t* args) {
    // Q3's G_Damage: args = targ, inflictor, attacker, dir, damage, dflags, mod, hit location,
    // weapon (<= 0: the inflictor's byte +3).
    uint8_t* targ = pointer_at_word(args[0]);
    if (!field<uint32_t>(targ, 0x29c)) return;  // takedamage
    if (title_global<uint32_t>(0x0095bf9c)) return;
    if (field<uint32_t>(targ, 0x248)) {
        // Vehicles: the player's tank under god mode (0x0095f678) and mods the vehicle ignores.
        if (ScriptString_Equals(reinterpret_cast<const ScriptString*>(targ + 0x264),
                                reinterpret_cast<const char*>(uintptr_t{0x0082dd9c})) &&  // "player_tank"
            title_global<uint8_t>(0x0095f678))
            return;
        if (own_function<uint32_t XB_REGS1 XB_CDECL(uint8_t*, int32_t)>(&Vehicle_TakesDamageFromMod_entry)(
                targ, static_cast<int32_t>(args[6])))  // Vehicle_TakesDamageFromMod
            return;
    }
    // The point as {x, y, z, x} (zeros without one); the first copy's slots later hold the dir.
    alignas(16) float pt[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    alignas(16) float dn[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (point) {
        std::memcpy(&pt[0], &point[0], 4);
        std::memcpy(&pt[1], &point[1], 4);
        std::memcpy(&pt[2], &point[2], 4);
        std::memcpy(&pt[3], &point[0], 4);
    }
    std::memcpy(dn, pt, sizeof dn);
    if (pointer_at(targ, 0x23c)) {
        using CvarGet = const uint8_t* XB_CDECL(const char*, const char*, uint32_t);
        const uint8_t* const intro = own_function<CvarGet>(&Cvar_Get)(
            reinterpret_cast<const char*>(uintptr_t{0x008344f4}), reinterpret_cast<const char*>(uintptr_t{0x0082e098}),
            0);  // Cvar_Get "introScreen"
        if (*reinterpret_cast<const uint32_t*>(intro + 0x20)) return;
        const int32_t mod = static_cast<int32_t>(args[6]);
        if (mod >= 3 && mod <= 6)
            args[4] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(args[4])) *
                                                                 title_global<float>(0x0083eb58)));  // 0.75
        if (G_ClientInOpenVehicle(targ)) {
            if (args[6] == 6 || args[6] == 10) return;
            args[4] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(args[4])) *
                                                                 title_global<float>(0x0083eb24)));  // 0.5
        }
        if (G_ClientInVehicle(targ)) return;
    }
    if (field<uint32_t>(targ, 0x240)) {
        // Actors: damage scaled per hit location (0x0094cc38); location 1 counts as 2 with +0x10 bit 0x1000.
        if ((field<uint32_t>(targ, 0x10) & 0x1000) && args[7] == 1) args[7] = 2;
        args[4] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(args[4])) *
                                                             title_global<float>(0x0094cc38 + args[7] * 4)));
    }
    const uint32_t world = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f534) + 8));
    if (!args[1]) args[1] = world;
    if (!args[2]) args[2] = world;
    uint8_t* const attacker = pointer_at_word(args[2]);
    if (targ[0] == 4) {
        // Damage-triggered movers: "trigger" notify and their use callback.
        if (!field<uint32_t>(targ, 0x33c) || targ[0x298]) return;
        const TitleRegs regs{args[2], 0, 0, 0, 0, args[0]};
        const uint32_t notify[1] = {title_global<uint32_t>(0x0097a818)};
        g_ent_call(own_address(&G_NotifyWithOwnHandle_entry), &regs, 1, notify);  // G_NotifyWithOwnHandle "trigger"
        using Use = void XB_CDECL(uint8_t*, uint32_t, uint32_t);
        title_function<Use>(title_global<uint32_t>(0x008da840 + field<uint32_t>(targ, 0x33c) * 4))(targ, args[1], args[2]);
        return;
    }
    if (targ[0] == 7) {
        uint32_t other = field<uint32_t>(attacker, 0x224);
        const TitleRegs regs{0, 0, 0, 0, 0, 0};
        const uint32_t notify[4] = {args[0], title_global<uint32_t>(0x0097a67c), address_of(&args[4]), address_of(&other)};
        g_ent_call(own_address(&G_NotifyWithIntEntity_entry), &regs, 4, notify);  // G_NotifyWithIntEntity "damage"
        return;
    }
    if (pointer_at(targ, 0x23c) && field<uint32_t>(pointer_at(targ, 0x23c), 0x584)) return;
    uint32_t dflags = args[5];
    if (!args[3]) {
        dflags |= 8;  // DAMAGE_NO_KNOCKBACK
        dn[0] = 0.0f;
        dn[1] = 0.0f;
        std::memcpy(&dn[2], &title_global<float>(0x0083eb20), 4);  // 1
    } else {
        VectorNormalize2(reinterpret_cast<const float*>(static_cast<uintptr_t>(args[3])), dn);
    }
    const int32_t damage = static_cast<int32_t>(args[4]);
    uint8_t* ps = pointer_at(targ, 0x23c);
    float scale = title_global<float>(0x0083edc8);  // 0.05
    if (ps) {
        if (field<uint32_t>(ps, 0x2c) & 1)
            scale = title_global<float>(0x0083eb94);  // 0.01
        else if (field<uint32_t>(ps, 0x2c) & 2)
            scale = title_global<float>(0x0083ef7c);  // 0.025
    }
    int32_t knockback = static_cast<int32_t>(static_cast<float>(damage) * scale);
    if (knockback > 60) knockback = 60;
    if (field<uint8_t>(targ, 0x2a4) & 0x20) knockback = 0;
    const int32_t mod = static_cast<int32_t>(args[6]);
    if (!(dflags & 8) && knockback && ps) {
        // Knockback into the client's velocity (+0x10, times g_knockback 0x00950758 * 0.004),
        // a quarter of the vertical push for own explosions, and a knockback timer.
        const float f = static_cast<float>(knockback) * title_global<float>(0x00950758) * title_global<float>(0x0083ef78);
        const float vx = f * dn[0];
        field<float>(pointer_at(targ, 0x23c), 0x10) = field<float>(pointer_at(targ, 0x23c), 0x10) + vx;
        const float vy = f * dn[1];
        const float vz = f * dn[2];
        field<float>(pointer_at(targ, 0x23c), 0x14) = field<float>(pointer_at(targ, 0x23c), 0x14) + vy;
        field<float>(pointer_at(targ, 0x23c), 0x18) = field<float>(pointer_at(targ, 0x23c), 0x18) + vz;
        if (args[0] == args[2] && (mod == 5 || mod == 6 || mod == 3 || mod == 4)) {
            float& z = field<float>(pointer_at(targ, 0x23c), 0x18);
            z = platform_x87_mul(z, 0.25f);  // FMUL by the double 0.25 (0x0083ee68)
        }
        if (!field<int32_t>(pointer_at(targ, 0x23c), 0x30)) {
            int32_t t = knockback * 2;
            if (t < 50)
                t = 50;
            else if (t > 200)
                t = 200;
            field<int32_t>(pointer_at(targ, 0x23c), 0x30) = t;
            field<uint32_t>(pointer_at(targ, 0x23c), 0x2c) |= 0x200;
        }
    }
    if (field<uint8_t>(targ, 0x2a4) & 1) return;  // FL_GODMODE
    if (static_cast<int32_t>(args[4]) < 1) args[4] = 1;
    const int32_t final_damage = static_cast<int32_t>(args[4]);
    const int32_t taken = final_damage - static_cast<int32_t>(G_ActorClampLethalDamage(static_cast<uint32_t>(final_damage), 0, targ));
    int32_t weapon = static_cast<int32_t>(args[8]);
    if (weapon <= 0) weapon = pointer_at_word(args[1])[3];
    G_ApplyDamage(dn, pointer_at_word(args[0]), taken, pointer_at_word(args[1]), pointer_at_word(args[2]), pt, mod, weapon,
                  static_cast<int32_t>(args[7]));
}

void XB_CDECL Pain_ForwardToOwner(uint8_t* self, uint8_t* attacker, int32_t damage, const float* point, int32_t mod,
                                  const float* dir, int32_t hitloc) {
    // Health back to 99999, the damage passed on to the owner (handle +0x1a0) when it takes damage.
    field<int32_t>(self, 0x350) = 99999;
    uint8_t* const owner = entity_from_handle(field<uint32_t>(self, 0x1a0));
    if (!field<uint32_t>(owner, 0x29c)) return;
    uint32_t args[9] = {address_of(owner), address_of(attacker), address_of(attacker), address_of(dir),
                        static_cast<uint32_t>(damage), 0, static_cast<uint32_t>(mod), static_cast<uint32_t>(hitloc),
                        0xffffffffu};
    G_Damage(point, args);
}

void XB_CDECL Die_ForwardToOwner(uint8_t* self, uint8_t*, uint8_t* attacker, int32_t damage, int32_t mod, int32_t,
                                 const float*, const float* dir, int32_t hitloc) {
    // As a pain at the entity's origin (+0x140).
    Pain_ForwardToOwner(self, attacker, damage, reinterpret_cast<const float*>(self + 0x140), mod, dir, hitloc);
}

PLATFORM_ASM_ENTRY(Activate_trigger_damage_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Activate_trigger_damage) "addl $16, %esp\n"
                   "ret")

void XB_CDECL Activate_trigger_damage(uint8_t* self, int32_t damage, uint8_t* attacker, int32_t mod) {
    // trigger_damage: unless waiting (think set and not 0x1c), under the threshold (+0x3b4) or
    // filtered by means of death: health (+0x350) down; when the total (32000 - health) reaches
    // +0x370 (or it is 0) it fires (activator +0x378, G_Trigger unless mod -1), then waits
    // wait (+0x384) + random spread (+0x388) seconds (think 0x12) or turns off (think 0xc next
    // frame, no touch) and resets the health.
    if (field<uint32_t>(self, 0x328) && field<uint32_t>(self, 0x32c) != 0x1c) return;
    const int32_t threshold = field<int32_t>(self, 0x3b4);
    if (threshold > 0 && damage < threshold) return;
    if (!TriggerDamage_AcceptsMeansOfDeath(mod, 0, self)) return;
    const int32_t health = field<int32_t>(self, 0x350) - damage;
    field<int32_t>(self, 0x350) = health;
    const int32_t needed = field<int32_t>(self, 0x370);
    if (needed && 32000 - health < needed) return;
    field<uint32_t>(self, 0x378) = address_of(attacker);
    if (mod != -1) G_Trigger(self, attacker);
    if (field<uint32_t>(self, 0x32c) != 0x1c) {
        if (field<float>(self, 0x384) > title_global<float>(0x0083eafc)) {
            field<uint32_t>(self, 0x32c) = 0x12;
            const int32_t r = own_function<int32_t XB_CDECL()>(&title_rand)();  // _rand
            const int32_t now = title_global<int32_t>(0x0098540c);
            float t = static_cast<float>(r) * title_global<float>(0x0083ecf8) - title_global<float>(0x0083eb20);
            t = t * field<float>(self, 0x388) + field<float>(self, 0x384);
            t = t * title_global<float>(0x0083ec74);  // -1000
            field<int32_t>(self, 0x328) = now - static_cast<int32_t>(t);
            field<int32_t>(self, 0x350) = 32000;
            return;
        }
        field<uint32_t>(self, 0x338) = 0;
        field<int32_t>(self, 0x328) = title_global<int32_t>(0x0098540c) + 1;
        field<uint32_t>(self, 0x32c) = 0xc;
    }
    field<int32_t>(self, 0x350) = 32000;
}

void XB_CDECL Touch_TriggerNotify(uint8_t* self, uint8_t* other) { G_Trigger(self, other); }

void XB_CDECL hurt_touch(uint8_t* self, uint8_t* other) {
    // trigger_hurt: damage (+0x358, mod 0x16; dflags 0x20 with spawn flag 8) to entities that
    // take it (not actors with flag 2) every 100 ms (1 s with flag 0x10); flag 0x20 hurts once.
    if (!field<uint32_t>(other, 0x29c)) return;
    if ((field<uint8_t>(self, 0x2a0) & 2) && field<uint32_t>(other, 0x240)) return;
    if (field<int32_t>(self, 0x2f0) > title_global<int32_t>(0x0098540c)) return;
    G_Trigger(self, other);
    const uint8_t flags = field<uint8_t>(self, 0x2a0);
    field<int32_t>(self, 0x2f0) = title_global<int32_t>(0x0098540c) + ((flags & 0x10) ? 1000 : 100);
    uint32_t args[9] = {address_of(other), address_of(self), address_of(self), 0, field<uint32_t>(self, 0x358),
                        static_cast<uint32_t>((flags & 8) << 2), 0x16, 0, 0xffffffffu};
    G_Damage(nullptr, args);
    if (field<uint8_t>(self, 0x2a0) & 0x20) field<uint32_t>(self, 0x338) = 0;
}

void XB_CDECL Touch_FriendlyChain(uint8_t* self, uint8_t* other) {
    // The toucher's sentient (+0x244) gets the friendly chain node named by the trigger's
    // target (+0x26c), then G_Trigger.
    own_function<void XB_THISCALL(uint32_t, uint32_t, uint32_t)>(&G_FriendlyChainNode)(
        field<uint32_t>(other, 0x244), title_global<uint32_t>(0x0095f4f4), address_of(self + 0x26c));  // G_FriendlyChainNode
    G_Trigger(self, other);
}

void XB_CDECL Touch_Multi(uint8_t* self, uint8_t* other) {
    G_Trigger(self, other);
    multi_trigger(address_of(other), self);
}

void XB_CDECL Die_trigger_damage(uint8_t* self, uint8_t*, uint8_t* attacker, int32_t damage, int32_t mod, int32_t,
                                 const float*, const float*, int32_t) {
    // The activation; without a damage total (+0x370) the health goes back to 32000.
    Activate_trigger_damage(self, damage, attacker, mod);
    if (!field<int32_t>(self, 0x370)) field<int32_t>(self, 0x350) = 32000;
}

void XB_CDECL Pain_trigger_damage(uint8_t* self, uint8_t* attacker, int32_t damage, const float*, int32_t mod,
                                  const float*, int32_t) {
    Activate_trigger_damage(self, damage, attacker, mod);
    if (!field<int32_t>(self, 0x370)) field<int32_t>(self, 0x350) = 32000;
}

void XB_CDECL Use_trigger_damage(uint8_t* self, uint8_t* other, uint8_t*) {
    // Fires at once: the damage total plus one, means of death -1 (no G_Trigger).
    Activate_trigger_damage(self, field<int32_t>(self, 0x370) + 1, other, -1);
}

namespace {

// G_MissileActivateDamageTriggers / G_CheckHitTriggerDamage: the box spanned by start and end
// (MAXPS/MINPS over all four lanes, start first: NaN lanes take end's), the trigger_damage
// entities (contents 0x400000) in it with takedamage, reached by a point sight trace from start
// to end (vec4 {0, 0, 0, 1} bounds): "damage" (damage, attacker), Activate_trigger_damage, and
// health 32000 again without a damage total (+0x370). `missile` also wants +0x2a4 bit 0x40000
// and has no null check on the handle's entity.
void activate_damage_triggers(uint8_t* ent, const float* start, const float* end, int32_t damage, int32_t mod,
                              bool missile) {
    uint32_t list[0x540];
    for (uint32_t i = 0; i < 0x540; ++i) list[i] = 0;
    alignas(16) float mins[4];
    alignas(16) float maxs[4];
    for (uint32_t i = 0; i < 4; ++i) {
        const float a = start[i];
        const float b = end[i];
        maxs[i] = a > b ? a : b;
        mins[i] = a < b ? a : b;
    }
    const int32_t count =
        SV_AreaEntities(maxs, 0, mins, reinterpret_cast<int32_t*>(list), 0x540, 0x400000);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const target = entity_from_handle(list[i]);
        if (!missile && !target) continue;
        if (!field<uint32_t>(target, 0x29c)) continue;
        if (field<uint32_t>(target, 0x260) != title_global<uint32_t>(0x0097a824)) continue;  // trigger_damage
        if (missile && !(field<uint32_t>(target, 0x2a4) & 0x40000)) continue;
        alignas(16) float box_min[4];
        alignas(16) float box_max[4];
        for (uint32_t k = 0; k < 4; ++k) box_min[k] = box_max[k] = title_global<float>(0x0082e290 + k * 4);
        if (!SV_EntitySightTrace(end, box_max, start, box_min, field<uint32_t>(target, 0x224), 0xffffffffu, 1)) continue;
        uint32_t attacker = field<uint32_t>(ent, 0x224);
        {
            const TitleRegs regs{0, 0, 0, 0, 0, 0};
            const uint32_t args[4] = {address_of(target), title_global<uint32_t>(0x0097a67c), address_of(&damage),
                                      address_of(&attacker)};
            g_ent_call(own_address(&G_NotifyWithIntEntity_entry), &regs, 4, args);  // G_NotifyWithIntEntity "damage"
        }
        Activate_trigger_damage(target, damage, ent, mod);
        if (!field<int32_t>(target, 0x370)) field<int32_t>(target, 0x350) = 32000;
    }
}

}  // namespace

void XB_CDECL G_MissileActivateDamageTriggers(uint8_t* ent, const float* start, const float* end, int32_t damage,
                                              int32_t mod) {
    activate_damage_triggers(ent, start, end, damage, mod, true);
}

void XB_CDECL G_CheckHitTriggerDamage(uint8_t* ent, const float* start, const float* end, int32_t damage, int32_t mod) {
    activate_damage_triggers(ent, start, end, damage, mod, false);
}

void XB_REGS3 XB_CDECL G_PlayerDeathSetup(uint32_t mod, uint32_t, uint8_t* self, uint32_t weapon) {
    // The client's dead type (+0x24: 5 from 1, else 4), items tossed unless in a contents-0x80000000
    // volume, the view angles from the entity's angles (+0x150), the entity made a corpse (contents
    // 0x4000000, no weapon, pitch/roll 0, height 16, respawn in 1700 ms). For the local player
    // (0x0095f534 +4) killed by an explosive kind (mods 3-6, 13, 14, 23; 21 only with a weapon): the
    // interface's +0x4e4 "default" at 3.0, a camera shake from the attacker (self +0x244) and a rumble.
    // Then the link and the interface's +0x2f8 "INGAME_PLAYER_DIED".
    using FromCString = void XB_THISCALL(uint32_t*, const char*);
    using Release = void XB_THISCALL(uint32_t*);
    uint8_t* client = pointer_at(self, 0x23c);
    field<uint32_t>(client, 0x24) = field<uint32_t>(client, 0x24) == 1 ? 5 : 4;
    field<uint32_t>(pointer_at(self, 0x23c), 0x100) = 0;
    field<uint32_t>(self, 0x350) = 0;
    if (SV_PointContents(reinterpret_cast<const float*>(self + 0x140), 0, 0x80000000u) == 0) TossClientItems(self);
    field<uint32_t>(pointer_at(self, 0x23c), 0x9c) = field<uint32_t>(self, 0x150);
    field<uint32_t>(pointer_at(self, 0x23c), 0xa0) = field<uint32_t>(self, 0x154);
    field<uint32_t>(pointer_at(self, 0x23c), 0xa4) = field<uint32_t>(self, 0x158);
    client = pointer_at(self, 0x23c);
    field<uint32_t>(self, 0x29c) = 1;
    field<uint32_t>(self, 0x134) = 0x4000000;
    self[3] = 0;
    field<uint32_t>(client, 0x80) = 0;
    field<float>(self, 0x150) = 0.0f;
    field<float>(self, 0x158) = 0.0f;
    self[1] = 0;
    field<float>(self, 0xf8) = title_global<float>(0x0083ebb8);  // 16
    field<int32_t>(pointer_at(self, 0x23c), 0x5e8) = title_global<int32_t>(0x0098540c) + 0x6a4;
    field<uint32_t>(self, 0x344) = 0;
    uint32_t text = 0;  // ScriptString
    if (self == pointer_at(title_global<uint8_t*>(0x0095f534), 4)) {
        bool shake = false;
        if (mod == 0x15) {
            shake = weapon != 0;
        } else {
            shake = mod == 3 || mod == 4 || mod == 5 || mod == 6 || mod == 0xd || mod == 0xe || mod == 0x17;
        }
        if (shake) {
            const uint8_t* const attacker = *reinterpret_cast<uint8_t* const*>(pointer_at(self, 0x244));
            alignas(16) float origin[3];
            for (uint32_t i = 0; i < 3; ++i) origin[i] = reinterpret_cast<const float*>(attacker + 0x140)[i];
            own_function<FromCString>(&ScriptString_FromCString)(&text, reinterpret_cast<const char*>(uintptr_t{0x00831f70}));  // "default"
            using Cue = void XB_CDECL(uint32_t, uint32_t*, float);
            title_function<Cue>(field<uint32_t>(title_global<uint8_t*>(0x0095f49c), 0x4e4))(
                field<uint32_t>(self, 0x224), &text, 3.0f);
            own_function<Release>(&ScriptString_Release_entry)(&text);  // ScriptString_Release
            CG_StartShakeCamera(origin, title_global<float>(0x0083eb20), 800, 2000.0f);  // 0x44fa0000
            title_global<int32_t>(0x00972f7c) = title_global<int32_t>(0x0097253c) + 500;  // cg_time + 500
            uint8_t* const manager = title_global<uint8_t*>(0x0095f5c4);  // shakeRumbleMgr
            if (manager != nullptr) {
                alignas(4) uint8_t desc[0x40] = {};
                ShakeRumbleDesc_Init(desc);
                const float one = title_global<float>(0x0083eb20);
                field<float>(desc, 8) = one;
                field<float>(desc, 0x10) = one;
                field<float>(desc, 0x28) = one;
                field<float>(desc, 0x24) = title_global<float>(0x0083ebf8);  // 0.1
                const float half = title_global<float>(0x0083eb24);
                field<float>(desc, 0x30) = half;
                field<float>(desc, 0x2c) = half;
                desc[0] = 1;
                desc[0x20] = 1;
                field<float>(desc, 4) = 0.0f;
                field<float>(desc, 0x34) = title_global<float>(0x0083eca0);  // 0.2
                ShakeRumble_Start(desc, manager, &text, 1.0f);
                for (uint32_t i = 2; i-- > 0;) {
                    own_function<Release>(&ScriptString_Release_entry)(reinterpret_cast<uint32_t*>(desc + 0x18 + i * 0x20));
                }
            }
        }
    }
    SV_LinkEntity(self);
    own_function<FromCString>(&ScriptString_FromCString)(&text, reinterpret_cast<const char*>(uintptr_t{0x008324a4}));  // "INGAME_PLAYER_DIED"
    title_function<void XB_CDECL(uint32_t*)>(field<uint32_t>(title_global<uint8_t*>(0x0095f49c), 0x2f8))(&text);
    own_function<Release>(&ScriptString_Release_entry)(&text);
}

}  // namespace game
