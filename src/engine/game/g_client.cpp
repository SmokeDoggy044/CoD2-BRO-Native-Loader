#include "g_client.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "g_main.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "qcommon_fs.h"
#include "sv.h"
#include "title_data.h"
#include "g_script.h"
#include "qcommon.h"

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint8_t* pointer_at(uint8_t* base, uint32_t offset) {
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(field<uint32_t>(base, offset)));
}

uint8_t* weapon_def(uint32_t weapon) {
    const uint32_t defs = title_global<uint32_t>(0x0095f4d8);  // bg_weaponDefs
    return reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(defs + weapon * 4))));
}

// ANGLE2SHORT then SHORT2ANGLE: (int)(a * 65536/360) & 0xffff, times 360/65536.
float short_angle(float scaled) {
    const int32_t s = static_cast<int32_t>(scaled) & 0xffff;
    return static_cast<float>(s) * title_global<float>(0x0083ebd4);
}

// One AngleNormalize180 step on a SHORT2ANGLE result: above 180 wraps by 360.
float normalize180(float a) {
    if (a > title_global<float>(0x0083eb60)) a = a - title_global<float>(0x0083ebcc);
    return a;
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

uint8_t* entity_from_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097a968 + index * 8)));
    return nullptr;
}

}  // namespace

uint32_t XB_REGS1 XB_CDECL Add_Ammo(uint8_t* ent, uint32_t weapon, int32_t count, uint32_t fill_clip) {
    uint8_t* def = weapon_def(weapon);
    const uint32_t clip_index = field<uint32_t>(def, 0x5a0);
    const uint32_t ammo_index = field<uint32_t>(def, 0x598);
    uint8_t* ps = pointer_at(ent, 0x23c);
    const int32_t old_ammo = field<int32_t>(ps, 0x110 + ammo_index * 4);
    const int32_t old_clip = field<int32_t>(ps, 0x280 + clip_index * 4);
    field<int32_t>(ps, 0x110 + ammo_index * 4) = old_ammo + count;
    uint32_t given = 0;
    if (field<uint32_t>(weapon_def(weapon), 0x6ec)) {
        BG_GivePlayerWeapon(weapon, pointer_at(ent, 0x23c));
        given = 1;
    }
    if (fill_clip || given) Fill_Clip(static_cast<int32_t>(weapon), pointer_at(ent, 0x23c));
    if (!field<uint32_t>(ent, 0x240) && given) {
        // A clip-only weapon just given to a player keeps no reserve.
        field<int32_t>(pointer_at(ent, 0x23c), 0x110 + ammo_index * 4) = 0;
    } else {
        int32_t& ammo = field<int32_t>(pointer_at(ent, 0x23c), 0x110 + ammo_index * 4);
        const int32_t max_ammo = title_global<int32_t>(0x00929818 + ammo_index * 4);
        if (ammo > max_ammo) ammo = max_ammo;
    }
    {
        int32_t& clip = field<int32_t>(pointer_at(ent, 0x23c), 0x280 + clip_index * 4);
        const int32_t max_clip = title_global<int32_t>(0x00935338 + clip_index * 4);
        if (clip > max_clip) clip = max_clip;
    }
    def = weapon_def(weapon);
    if (field<int32_t>(def, 0x5b0) >= 0) {
        // Shared ammo: over the group's room, take the excess back.
        const int32_t room = BG_AmmoRoomForWeapon(weapon, pointer_at(ent, 0x23c));
        if (room < 0) {
            ps = pointer_at(ent, 0x23c);
            if (field<uint32_t>(def, 0x6ec)) {
                int32_t& clip = field<int32_t>(ps, 0x280 + clip_index * 4);
                clip = clip + room;
                if (field<int32_t>(pointer_at(ent, 0x23c), 0x280 + clip_index * 4) <= 0) {
                    field<int32_t>(pointer_at(ent, 0x23c), 0x280 + clip_index * 4) = 0;
                    BG_RemoveWeaponFromSlot(weapon, pointer_at(ent, 0x23c));
                    return 0;
                }
            } else {
                int32_t& ammo = field<int32_t>(ps, 0x110 + ammo_index * 4);
                ammo = ammo + room;
                if (field<int32_t>(pointer_at(ent, 0x23c), 0x110 + ammo_index * 4) < 0)
                    field<int32_t>(pointer_at(ent, 0x23c), 0x110 + ammo_index * 4) = 0;
            }
        }
    }
    ps = pointer_at(ent, 0x23c);
    const int32_t ammo = field<int32_t>(ps, 0x110 + ammo_index * 4);
    const int32_t clip = field<int32_t>(ps, 0x280 + clip_index * 4);
    return static_cast<uint32_t>(clip + ammo - old_clip - old_ammo);
}

// The original keeps EDX; 0x0025bbd7 passes it on to G_EntLinkToWithOffset.
PLATFORM_ASM_ENTRY(SetClientViewAngle_entry,
                   "pushl %edx\n" PLATFORM_ASM_CALL(SetClientViewAngle) "popl %edx\n"
                   "ret")

void XB_REGS2 XB_CDECL SetClientViewAngle(const float* angle, uint8_t* ent) {
    // Q3's SetClientViewAngle with a view clamp: a player with +0x2c bit 0 set (and +0xc0 bits
    // 0x6000 clear) keeps yaw within 45 degrees of +0x450 and pitch within -15..45 of +0x458,
    // the excess moved into the delta angles (+0x54/+0x58).
    float a[3];
    std::memcpy(a, angle, sizeof a);
    const float to_short = title_global<float>(0x0083ebd8);  // 65536 / 360
    uint8_t* ps = pointer_at(ent, 0x23c);
    if ((field<uint8_t>(ps, 0x2c) & 1) && !(field<uint32_t>(ps, 0xc0) & 0x6000)) {
        const float limit = title_global<float>(0x0083ef1c);  // 45
        float d = normalize180(short_angle((field<float>(ps, 0x450) - a[1]) * to_short));
        d = normalize180(short_angle(d * to_short));
        if (d > limit || title_global<float>(0x0083ef18) > d) {  // -45
            if (d > limit)
                d = d - limit;
            else
                d = d + limit;
            field<int32_t>(ps, 0x58) += static_cast<int32_t>(d * to_short) & 0xffff;
            ps = pointer_at(ent, 0x23c);
            float x;
            if (d > title_global<float>(0x0083eafc))
                x = (field<float>(ps, 0x450) - limit) * to_short;
            else
                x = field<float>(ps, 0x450) * to_short + title_global<float>(0x00830bd8);  // 8192
            a[1] = short_angle(x);
        }
        d = normalize180(short_angle((field<float>(ps, 0x458) - a[0]) * to_short));
        d = normalize180(short_angle(d * to_short));
        if (d > limit || title_global<float>(0x0083ef14) > d) {  // -15
            if (d > limit)
                d = d - limit;
            else
                d = d + title_global<float>(0x0083ef10);  // 15
            field<int32_t>(ps, 0x54) += static_cast<int32_t>(d * to_short) & 0xffff;
            ps = pointer_at(ent, 0x23c);
            float x;
            if (d > title_global<float>(0x0083eafc))
                x = (field<float>(ps, 0x458) - limit) * to_short;
            else
                x = field<float>(ps, 0x458) * to_short + title_global<float>(0x0083ef0c);  // 2730.67
            a[0] = normalize180(short_angle(x));
        }
    }
    for (uint32_t i = 0; i < 3; ++i) {
        ps = pointer_at(ent, 0x23c);
        const int32_t s = static_cast<int32_t>(a[i] * to_short) & 0xffff;
        field<int32_t>(ps, 0x54 + i * 4) = s - field<int32_t>(ps, 0x52c + i * 4);
    }
    field<float>(ent, 0x150) = a[0];
    field<float>(ent, 0x154) = a[1];
    field<float>(ent, 0x158) = a[2];
    field<uint32_t>(pointer_at(ent, 0x23c), 0x9c) = field<uint32_t>(ent, 0x150);
    field<uint32_t>(pointer_at(ent, 0x23c), 0xa0) = field<uint32_t>(ent, 0x154);
    field<uint32_t>(pointer_at(ent, 0x23c), 0xa4) = field<uint32_t>(ent, 0x158);
}

void XB_REGS3 XB_CDECL TeleportPlayer(uint8_t* ent, uint32_t, const float* origin) {
    const uint32_t* const o = reinterpret_cast<const uint32_t*>(origin);
    field<uint32_t>(pointer_at(ent, 0x23c), 0x0) = o[0];
    field<uint32_t>(pointer_at(ent, 0x23c), 0x4) = o[1];
    field<uint32_t>(pointer_at(ent, 0x23c), 0x8) = o[2];
    uint8_t* ps = pointer_at(ent, 0x23c);
    field<float>(ps, 0x8) = field<float>(ps, 0x8) + title_global<float>(0x0083eb20);  // 1
    field<uint32_t>(pointer_at(ent, 0x23c), 0xc0) ^= 0x8;  // EF_TELEPORT_BIT
    ent = BG_PlayerStateToEntityState(ent, 0, pointer_at(ent, 0x23c));
    field<uint32_t>(ent, 0x140) = field<uint32_t>(pointer_at(ent, 0x23c), 0x0);
    field<uint32_t>(ent, 0x144) = field<uint32_t>(pointer_at(ent, 0x23c), 0x4);
    field<uint32_t>(ent, 0x148) = field<uint32_t>(pointer_at(ent, 0x23c), 0x8);
}

PLATFORM_ASM_ENTRY(SelectNearestDeathmatchSpawnPoint_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(SelectNearestDeathmatchSpawnPoint) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL SelectNearestDeathmatchSpawnPoint(const float* from) {
    // The closest entity of the spawn list (0x0097d370, count 0x0097d36c) whose classname hash
    // (+0x260) is info_player_deathmatch; distances on the x87.
    float nearest = title_global<float>(0x0083ef20);  // 999999
    const uint32_t classname = title_global<uint32_t>(0x0097a700);
    // The end pointer wraps like the original's (count * 4 + base).
    const uint32_t end = 0x0097d370 + title_global<uint32_t>(0x0097d36c) * 4;
    uint32_t best = 0;
    for (uint32_t at = 0x0097d370; at != end; at += 4) {
        uint8_t* const spot = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(at)));
        if (!spot) continue;
        const uint32_t hash = field<uint32_t>(spot, 0x260);
        if (!hash || hash != classname) continue;
        const float x = field<float>(spot, 0x140), y = field<float>(spot, 0x144), z = field<float>(spot, 0x148);
        if (platform_x87_greater_than_dist3(nearest, x, from[0], y, from[1], z, from[2])) {
            nearest = platform_x87_norm3(platform_x87_add(z, -from[2]), platform_x87_add(y, -from[1]),
                                         platform_x87_add(x, -from[0]));
            best = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(spot));
        }
    }
    return best;
}

int32_t XB_REGS3 XB_CDECL Pickup_Ammo(uint8_t* other, uint32_t, uint8_t* ent) {
    // The item's ammo (the entity's count +0x370, else the item's quantity +0x20) for its
    // weapon (+0x28); nothing taken: 0. Else the pickup message to the player, "trigger" on the
    // item and "pickup" on a client; respawn time 40 with spawn flag 8, else -1.
    uint8_t* const item = pointer_at(ent, 0x3b0);
    uint32_t count = field<uint32_t>(ent, 0x370);
    if (!count) count = field<uint32_t>(item, 0x20);
    if (!Add_Ammo(other, field<uint32_t>(item, 0x28), static_cast<int32_t>(count), 0)) return 0;
    const uint8_t* const def = weapon_def(field<uint32_t>(pointer_at(ent, 0x3b0), 0x28));
    const uint32_t handle = field<uint32_t>(other, 0x224);
    using Va = char* XB_CDECL(const char*, ...);
    const uint32_t format = field<uint32_t>(const_cast<uint8_t*>(def), 0x6ec) ? 0x0083b77c : 0x0083b760;
    SV_GameSendServerCommand(own_function<Va>(&va_entry)(reinterpret_cast<const char*>(uintptr_t{format}),
                                                            field<uint32_t>(const_cast<uint8_t*>(def), 0xc)),
                             handle);
    uint32_t number = field<uint32_t>(other, 0x224);
    const TitleRegs regs{0, handle, 0, 0, address_of(other), address_of(ent)};
    const uint32_t args[2] = {title_global<uint32_t>(0x0097a818), address_of(&number)};
    g_ent_call(own_address(&G_NotifyWithEntity_2_entry), &regs, 2, args);  // G_NotifyWithEntity_2 "trigger"
    if (field<uint32_t>(other, 0x23c)) Scr_Notify(other, title_global<uint32_t>(0x0097a758));  // "pickup"
    return (field<uint8_t>(ent, 0x2a0) & 8) ? 0x28 : -1;
}

void XB_CDECL ClientCommand(uint32_t handle) {
    // Q3's ClientCommand: for a client entity, argv[0] (0x3fd characters) against the 18 dev
    // commands (0x008ddae0, no arguments) and the 16 cheat commands (0x008ddb70, given the
    // entity); else "unrecognized command" to it (to everyone for entity number 0).
    char command[0x400];
    command[0] = 0;
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540) return;
    if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097a968 + index * 8)));
    if (!ent || !field<uint32_t>(ent, 0x23c)) return;
    const char* const arg0 = title_global<uint32_t>(0x00934248) ? reinterpret_cast<const char*>(static_cast<uintptr_t>(
                                                                      title_global<uint32_t>(0x0092df90)))
                                                                : reinterpret_cast<const char*>(uintptr_t{0x0082dfeb});
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(command, arg0, 0x3fd);  // _strncpy
    command[0x3fd] = 0;
    using Stricmpn = int32_t XB_CDECL(const char*, const char*, int32_t);
    for (uint32_t i = 0; i < 0x12; ++i) {
        const char* const name = reinterpret_cast<const char*>(static_cast<uintptr_t>(title_global<uint32_t>(0x008ddae0 + i * 8)));
        if (!own_function<Stricmpn>(&Q_stricmpn)(command, name, 0xfffffff)) {  // Q_stricmpn
            title_function<void XB_CDECL()>(title_global<uint32_t>(0x008ddae4 + i * 8))();
            return;
        }
    }
    for (uint32_t i = 0; i < 0x10; ++i) {
        const char* const name = reinterpret_cast<const char*>(static_cast<uintptr_t>(title_global<uint32_t>(0x008ddb70 + i * 8)));
        if (!own_function<Stricmpn>(&Q_stricmpn)(command, name, 0xfffffff)) {
            title_function<void XB_CDECL(uint8_t*)>(title_global<uint32_t>(0x008ddb74 + i * 8))(ent);
            return;
        }
    }
    const uint8_t* const self = entity_from_handle(handle);
    uint32_t client = 0;
    const uint32_t number = field<uint32_t>(const_cast<uint8_t*>(self), 0x224);
    if (number) {
        const uint8_t* const target = entity_from_handle(number);
        if (!field<uint32_t>(const_cast<uint8_t*>(target), 0x23c)) return;
        client = title_global<uint32_t>(0x00927e8c);
    }
    const uint32_t args[2] = {0x0082ddd4, 0x0083bac4};  // "%s", "unrecognized command"
    SV_SendServerCommand(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(client)), args);
}

void XB_CDECL ClientImpacts(uint8_t* ent, int32_t count, const uint32_t* touch) {
    // Each distinct touched entity handle once: "touch" notifies both ways, then both touch
    // callbacks (+0x338).
    uint32_t number = 0;
    for (int32_t i = 0; i < count; ++i) {
        int32_t j = 0;
        while (j < i && touch[j] != touch[i]) ++j;
        if (j != i) continue;
        uint8_t* const other = entity_from_handle(touch[i]);
        number = field<uint32_t>(other, 0x224);
        const TitleRegs a{0, static_cast<uint32_t>(i), 0, 0, address_of(other), address_of(ent)};
        const uint32_t a_args[2] = {title_global<uint32_t>(0x0097a814), address_of(&number)};
        g_ent_call(own_address(&G_NotifyWithEntity_entry), &a, 2, a_args);  // G_NotifyWithEntity "touch"
        number = field<uint32_t>(ent, 0x224);
        const TitleRegs b{0, static_cast<uint32_t>(i), 0, 0, address_of(other), address_of(other)};
        const uint32_t b_args[2] = {title_global<uint32_t>(0x0097a814), address_of(&number)};
        g_ent_call(own_address(&G_NotifyWithEntity_entry), &b, 2, b_args);
        using Touch = void XB_CDECL(uint8_t*, uint8_t*, uint32_t);
        if (field<uint32_t>(ent, 0x338))
            title_function<Touch>(title_global<uint32_t>(0x008da814 + field<uint32_t>(ent, 0x338) * 4))(ent, other, 1);
        if (field<uint32_t>(other, 0x338))
            title_function<Touch>(title_global<uint32_t>(0x008da814 + field<uint32_t>(other, 0x338) * 4))(other, ent, 1);
    }
}

uint32_t XB_CDECL ClientConnect(uint32_t handle) {
    // Q3's ClientConnect: the client cleared and connected (+0x520), its ping-like value from
    // 0x00950ce8 + 0.5 (+0x580 and +0x108), entity number (+0x7c), touch 2, no pain, a
    // sentient (team 2, +0x2c = 1; G_Error without one). Returns 0.
    uint8_t* const ent = entity_from_handle(handle);
    uint8_t* const client = pointer_at(ent, 0x23c);
    G_ClearClient(client);
    field<uint32_t>(client, 0x520) = 1;
    const int32_t value = static_cast<int32_t>(title_global<float>(0x00950ce8) + title_global<float>(0x0083eb24));
    field<int32_t>(client, 0x580) = value;
    field<int32_t>(client, 0x108) = value;
    field<uint32_t>(client, 0x7c) = field<uint32_t>(ent, 0x224);
    field<uint32_t>(ent, 0x338) = 2;
    field<uint32_t>(ent, 0x340) = 0;
    field<uint32_t>(ent, 0x23c) = address_of(client);
    uint8_t* const sentient = Sentient_Alloc();
    if (!sentient)
        own_function<void XB_CDECL(const char*, ...)>(&G_Error_entry)(
            reinterpret_cast<const char*>(uintptr_t{0x0083bd98}));  // G_Error "No sentient for player."
    field<uint32_t>(ent, 0x244) = address_of(sentient);
    field<uint32_t>(sentient, 0x0) = address_of(ent);
    field<uint32_t>(sentient, 0x4) = 2;
    field<float>(sentient, 0x2c) = title_global<float>(0x0083eb20);
    field<uint32_t>(client, 0xc0) = 0x10;
    field<uint32_t>(ent, 0xd4) = 0x200;
    return 0;
}

void XB_CDECL ClientDeathReload() {
    // Unless 0x0095bf9c: fade the screen out, g_reloading 1, the reload time (0x00985e28) in
    // 2 s and the sound fade.
    if (title_global<uint32_t>(0x0095bf9c)) return;
    using Va = char* XB_CDECL(const char*, ...);
    SV_GameSendServerCommand(own_function<Va>(&va_entry)(reinterpret_cast<const char*>(uintptr_t{0x0083bdc4}),
                                                            title_global<int32_t>(0x0098540c) + 1000),
                             0);  // "scr_fade 1 %i 2000"
    own_function<uint32_t XB_CDECL(const char*, const char*, uint32_t)>(&Cvar_Set2)(
        reinterpret_cast<const char*>(uintptr_t{0x0082c89c}), reinterpret_cast<const char*>(uintptr_t{0x0082e058}),
        1);  // Cvar_Set2 g_reloading "1"
    title_global<int32_t>(0x00985e28) = title_global<int32_t>(0x0098540c) + 2000;
    SV_GameSendServerCommand(
        own_function<Va>(&va_entry)(reinterpret_cast<const char*>(uintptr_t{0x0083bdb4}), 2000), 0);  // "snd_fade 0 %d"
}

PLATFORM_ASM_ENTRY(G_ClientActivateTargetActor_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_ClientActivateTargetActor) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_ClientActivateTargetActor(const uint8_t* ps) {
    // The player's (entity by +0x7c) target actor (client +0x5f4: alive actor) within range
    // (0x0095be70, x87 distance) once its delay (+0x5f8) passed: delay renewed (0x0094d614), the
    // actor's sentient takes the player's node claim and gets "trigger".
    const uint32_t handle = *reinterpret_cast<const uint32_t*>(ps + 0x7c);
    if (!field<uint32_t>(entity_from_handle(handle), 0x23c)) return;
    uint8_t* const player = entity_from_handle(handle);
    uint8_t* const client = pointer_at(player, 0x23c);
    uint8_t* const actor = pointer_at(client, 0x5f4);
    if (!actor || !field<uint32_t>(actor, 0x240) || field<int32_t>(actor, 0x350) <= 0) return;
    const int32_t now = title_global<int32_t>(0x0098540c);
    if (field<int32_t>(client, 0x5f8) > now) return;
    uint8_t* const target = pointer_at(client, 0x5f4);
    const float dist = platform_x87_norm3(platform_x87_add(field<float>(target, 0x148), -field<float>(player, 0x148)),
                                          platform_x87_add(field<float>(target, 0x144), -field<float>(player, 0x144)),
                                          platform_x87_add(field<float>(target, 0x140), -field<float>(player, 0x140)));
    if (platform_x87_greater(dist, title_global<float>(0x0095be70))) return;
    field<int32_t>(client, 0x5f8) = title_global<int32_t>(0x0094d614) + now;
    const TitleRegs regs{field<uint32_t>(pointer_at(pointer_at(player, 0x23c), 0x5f4), 0x244), field<uint32_t>(player, 0x244), 0, 0,
                         0, 0};
    g_ent_call(own_address(&Sentient_TakeNodeClaim_entry), &regs, 0, nullptr);  // Sentient_TakeNodeClaim (EAX, EBX)
    Scr_Notify(pointer_at(pointer_at(player, 0x23c), 0x5f4), title_global<uint32_t>(0x0097a818));  // "trigger"
}

PLATFORM_ASM_ENTRY(LookAtKiller_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(LookAtKiller) "addl $12, %esp\n"
                   "ret")

void XB_CDECL LookAtKiller(const uint8_t* attacker, const uint8_t* inflictor, uint8_t* self) {
    // Q3's LookAtKiller: the client's death yaw (+0x104) toward the attacker, else the
    // inflictor, else its own yaw (+0x154).
    const uint8_t* from = nullptr;
    if (attacker && attacker != self)
        from = attacker;
    else if (inflictor && inflictor != self)
        from = inflictor;
    if (!from) {
        field<int32_t>(pointer_at(self, 0x23c), 0x104) = static_cast<int32_t>(field<float>(self, 0x154));
        return;
    }
    float dir[3];
    dir[0] = *reinterpret_cast<const float*>(from + 0x140) - field<float>(self, 0x140);
    dir[1] = *reinterpret_cast<const float*>(from + 0x144) - field<float>(self, 0x144);
    dir[2] = *reinterpret_cast<const float*>(from + 0x148) - field<float>(self, 0x148);
    const float yaw = vectoyaw(dir);
    field<int32_t>(pointer_at(self, 0x23c), 0x104) = static_cast<int32_t>(yaw);
}

PLATFORM_ASM_ENTRY(SpotWouldTelefrag_entry,
                   "pushl %edx\n" PLATFORM_ASM_CALL(SpotWouldTelefrag) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL SpotWouldTelefrag(const float* origin) {
    // Q3's SpotWouldTelefrag: a live client (+0x24 < 4) or a live actor in the player box at
    // the origin (0x0097a8a0 mins, 0x00984d00 maxs; contents 0x0200c000).
    alignas(16) float mins[4];
    alignas(16) float maxs[4];
    int32_t touch[0x540];
    std::memset(touch, 0, sizeof touch);
    mins[0] = origin[0] + title_global<float>(0x0097a8a0);
    mins[1] = title_global<float>(0x0097a8a4) + origin[1];
    mins[2] = title_global<float>(0x0097a8a8) + origin[2];
    maxs[0] = origin[0] + title_global<float>(0x00984d00);
    maxs[1] = title_global<float>(0x00984d04) + origin[1];
    maxs[2] = title_global<float>(0x00984d08) + origin[2];
    const int32_t count = SV_AreaEntities(maxs, 0, mins, touch, 0x540, 0x0200c000);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const hit = entity_from_handle(static_cast<uint32_t>(touch[i]));
        const uint8_t* const client = pointer_at(hit, 0x23c);
        if (client && *reinterpret_cast<const int32_t*>(client + 0x24) < 4) return 1;
        if (field<uint32_t>(hit, 0x240) && field<int32_t>(hit, 0x350) > 0) return 1;
    }
    return 0;
}

void XB_CDECL P_DamageFeedback(uint8_t* player) {
    // Q3's P_DamageFeedback on the player state (+0x23c): below pm_type 4 with damage taken
    // (+0x5d4) and a max health (+0x580): the blend count (damage * 100 / max, at most 127)
    // adds to the red blend (+0x4f0, at most 255); the damage direction as pitch/yaw bytes
    // (+0xf4 / +0xf0; both 255 for damage without a direction, +0x5e4); the pain event
    // (0xc0, health percent 0..100) at most every 700 ms (+0x634) unless +0x2a4 bit 0; then
    // the damage event count (+0xec), the count (+0xf8) and the damage reset.
    uint8_t* const ps = pointer_at(player, 0x23c);
    if (field<int32_t>(ps, 0x24) >= 4) return;
    const int32_t damage = field<int32_t>(ps, 0x5d4);
    if (damage <= 0) return;
    const int32_t max_health = field<int32_t>(ps, 0x580);
    if (max_health <= 0) return;
    int32_t count = damage * 100 / max_health;
    if (count > 0x7f) count = 0x7f;
    const float limit = title_global<float>(0x0083eb8c);  // 255
    const float blend = static_cast<float>(count) + field<float>(ps, 0x4f0);
    field<float>(ps, 0x4f0) = blend;
    if (blend > limit) field<float>(ps, 0x4f0) = limit;
    int32_t pitch;
    if (field<uint32_t>(ps, 0x5e4)) {
        pitch = 0xff;
        field<int32_t>(ps, 0xf0) = 0xff;
        field<uint32_t>(ps, 0x5e4) = 0;
    } else {
        float angles[3];
        vectoangles(reinterpret_cast<const float*>(ps + 0x5d8), angles);
        const float scale = title_global<float>(0x0083eff0);  // 256/360
        pitch = static_cast<int32_t>(angles[0] * scale);
        field<int32_t>(ps, 0xf0) = static_cast<int32_t>(angles[1] * scale);
    }
    field<int32_t>(ps, 0xf4) = pitch;
    if (title_global<int32_t>(0x0098540c) > field<int32_t>(pointer_at(player, 0x23c), 0x634) &&
        !(player[0x2a4] & 1)) {
        float health = static_cast<float>(field<int32_t>(ps, 0x100)) / static_cast<float>(field<int32_t>(ps, 0x108));
        health = health * title_global<float>(0x0083eb90);  // 100
        int32_t percent = static_cast<int32_t>(health);
        if (percent < 0)
            percent = 0;
        else if (percent > 100)
            percent = 100;
        G_AddEvent(player, 0xc0, static_cast<uint32_t>(percent));
        field<int32_t>(pointer_at(player, 0x23c), 0x634) = title_global<int32_t>(0x0098540c) + 700;
    }
    field<int32_t>(ps, 0xec) = field<int32_t>(ps, 0xec) + 1;
    field<int32_t>(ps, 0xf8) = count;
    field<int32_t>(ps, 0x5d4) = 0;
}

uint32_t XB_CDECL SelectRandomDeathmatchSpawnPoint() {
    // Q3's SelectRandomDeathmatchSpawnPoint: up to 128 info_player_deathmatch spots that would
    // not telefrag, one of them at random (_rand % count); with none free the first spot found
    // (with no spots at all the original returns an unwritten stack word: zero in tests).
    uint32_t found[0x1001];
    found[0] = 0;
    found[1] = 0;
    G_GatherEntsByIntField(found, 0x260, title_global<uint32_t>(0x0097a700));  // classname
    uint32_t spots[0x80];
    int32_t count = 0;
    const uint32_t total = found[0];
    for (uint32_t i = 0; i < total; ++i) {
        const uint32_t spot = found[1 + i];
        if (static_cast<uint8_t>(SpotWouldTelefrag(reinterpret_cast<const float*>(static_cast<uintptr_t>(spot + 0x140)))))
            continue;
        if (count >= 0x80) continue;
        spots[count] = spot;
        ++count;
    }
    if (!count) return found[1];
    const int32_t pick = title_function<int32_t XB_CDECL()>(0x001efcc0)() % count;  // _rand
    return spots[pick];
}

PLATFORM_ASM_ENTRY(SelectSpawnPoint_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(SelectSpawnPoint) "addl $12, %esp\n"
                   "ret")

uint8_t* XB_CDECL SelectSpawnPoint(const float* avoid, float* angles, float* origin) {
    // Q3's SelectSpawnPoint: a random deathmatch spot, up to three tries to avoid the one
    // nearest `avoid`; origin raised 9 units, its angles.
    const uint32_t nearest = SelectNearestDeathmatchSpawnPoint(avoid);
    uint32_t spot = SelectRandomDeathmatchSpawnPoint();
    if (spot == nearest) {
        spot = SelectRandomDeathmatchSpawnPoint();
        if (spot == nearest) spot = SelectRandomDeathmatchSpawnPoint();
    }
    if (!spot)
        own_function<int32_t XB_CDECL(const char*)>(&G_Error_entry)(
            reinterpret_cast<const char*>(uintptr_t{0x0083bdd8}));  // G_Error "Couldn't find a spawn point"
    uint8_t* const e = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(spot));
    uint32_t* const out = reinterpret_cast<uint32_t*>(origin);
    out[0] = field<uint32_t>(e, 0x140);
    out[1] = field<uint32_t>(e, 0x144);
    origin[2] = field<float>(e, 0x148) + title_global<float>(0x0083eef8);
    uint32_t* const ang = reinterpret_cast<uint32_t*>(angles);
    ang[0] = field<uint32_t>(e, 0x150);
    ang[1] = field<uint32_t>(e, 0x154);
    ang[2] = field<uint32_t>(e, 0x158);
    return e;
}

void XB_REGS1 XB_CDECL G_UpdatePlayerHeadEnt(uint8_t* ent) {
    // The eye position (origin + view height ps +0xac) with the lean (ps +0x4c, yaw ps +0xa0,
    // 16, 20). The head entity (ps +0x5f0) is spawned once: bounds -8 (0x003f48ac) .. 8,
    // contents 0x2080, owner = the player's handle (+0x224), +0x340/+0x344 = 2, health 99999,
    // takedamage. It is placed stationary there and linked.
    uint8_t* const ps = pointer_at(ent, 0x23c);
    float position[3];
    position[0] = field<float>(ent, 0x140);
    position[1] = field<float>(ent, 0x144);
    position[2] = field<float>(ps, 0xac) + field<float>(ent, 0x148);
    AddLeanToPosition(position, field<float>(ps, 0x4c), field<float>(ps, 0xa0), 16.0f, 20.0f);
    uint8_t* head = pointer_at(pointer_at(ent, 0x23c), 0x5f0);
    if (!head) {
        head = own_function<uint8_t* XB_REGS1 XB_CDECL(int32_t)>(&G_Spawn)(-1);  // G_Spawn
        field<uint8_t*>(pointer_at(ent, 0x23c), 0x5f0) = head;
        head = pointer_at(pointer_at(ent, 0x23c), 0x5f0);
        const float low = title_global<float>(0x003f48ac);
        field<float>(head, 0xe0) = low;
        field<float>(head, 0xe4) = low;
        field<float>(head, 0xe8) = low;
        const float high = title_global<float>(0x0083ed78);
        field<float>(head, 0xf0) = high;
        field<float>(head, 0xf4) = high;
        field<float>(head, 0xf8) = high;
        field<uint32_t>(head, 0x134) = 0x2080;
        field<uint32_t>(head, 0x1a0) = field<uint32_t>(ent, 0x224);
        field<uint32_t>(head, 0x340) = 2;
        field<uint32_t>(head, 0x344) = 2;
        field<uint32_t>(head, 0x350) = 0x1869f;
        field<uint32_t>(head, 0x29c) = 1;
    }
    field<float>(head, 0x20) = position[0];
    field<float>(head, 0x24) = position[1];
    field<float>(head, 0x28) = position[2];
    field<uint32_t>(head, 0x14) = 0;
    field<uint32_t>(head, 0x18) = 0;
    field<uint32_t>(head, 0x1c) = 0;
    field<float>(head, 0x34) = 0.0f;
    field<float>(head, 0x30) = 0.0f;
    field<float>(head, 0x2c) = 0.0f;
    field<float>(head, 0x140) = position[0];
    field<float>(head, 0x144) = position[1];
    field<float>(head, 0x148) = position[2];
    SV_LinkEntity(head);
}

uint8_t* XB_REGS1 XB_CDECL SelectInitialSpawnPoint(float* origin, float* angles) {
    // The spawn point list (count 0x0097d36c, entities 0x0097d370): the first
    // info_player_deathmatch (+0x260) with spawn flag 1, else the last one; when there is none
    // or it would telefrag, SelectSpawnPoint (avoid point vec3_origin). Origin z + 9.
    const uint32_t classname = title_global<uint32_t>(0x0097a700);
    const uint32_t end = 0x0097d370 + title_global<uint32_t>(0x0097d36c) * 4;
    uint8_t* spot = nullptr;
    uint32_t cursor = 0x0097d370;
    while (cursor != end) {
        uint8_t* const e = title_global<uint8_t*>(cursor);
        cursor += 4;
        if (!e || !field<uint32_t>(e, 0x260) || field<uint32_t>(e, 0x260) != classname) continue;
        spot = e;
        if (e[0x2a0] & 1) break;
    }
    if (!spot || (SpotWouldTelefrag(&field<float>(spot, 0x140)) & 0xff))
        return SelectSpawnPoint(reinterpret_cast<const float*>(uintptr_t{0x0082eec0}), angles, origin);
    uint32_t* const out = reinterpret_cast<uint32_t*>(origin);
    out[0] = field<uint32_t>(spot, 0x140);
    out[1] = field<uint32_t>(spot, 0x144);
    origin[2] = field<float>(spot, 0x148) + title_global<float>(0x0083eef8);
    uint32_t* const ang = reinterpret_cast<uint32_t*>(angles);
    ang[0] = field<uint32_t>(spot, 0x150);
    ang[1] = field<uint32_t>(spot, 0x154);
    ang[2] = field<uint32_t>(spot, 0x158);
    return spot;
}

}  // namespace game
