#include "g_client.h"

#include <cstring>

#include "bg_misc.h"
#include "g_main.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "sv.h"
#include "title_data.h"

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
    const uint32_t count = title_global<uint32_t>(0x0097d36c);
    uint32_t best = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* const spot = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097d370 + i * 4)));
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
    SV_GameSendServerCommand(title_function<Va>(0x002a93d0)(reinterpret_cast<const char*>(uintptr_t{format}),
                                                            field<uint32_t>(const_cast<uint8_t*>(def), 0xc)),
                             handle);
    uint32_t number = field<uint32_t>(other, 0x224);
    const TitleRegs regs{0, handle, 0, 0, address_of(other), address_of(ent)};
    const uint32_t args[2] = {title_global<uint32_t>(0x0097a818), address_of(&number)};
    g_ent_call(0x000264d0, &regs, 2, args);  // G_NotifyWithEntity_2 "trigger"
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
        if (!title_function<Stricmpn>(0x000441c0)(command, name, 0xfffffff)) {  // Q_stricmpn
            title_function<void XB_CDECL()>(title_global<uint32_t>(0x008ddae4 + i * 8))();
            return;
        }
    }
    for (uint32_t i = 0; i < 0x10; ++i) {
        const char* const name = reinterpret_cast<const char*>(static_cast<uintptr_t>(title_global<uint32_t>(0x008ddb70 + i * 8)));
        if (!title_function<Stricmpn>(0x000441c0)(command, name, 0xfffffff)) {
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
        g_ent_call(0x000265a0, &a, 2, a_args);  // G_NotifyWithEntity "touch"
        number = field<uint32_t>(ent, 0x224);
        const TitleRegs b{0, static_cast<uint32_t>(i), 0, 0, address_of(other), address_of(other)};
        const uint32_t b_args[2] = {title_global<uint32_t>(0x0097a814), address_of(&number)};
        g_ent_call(0x000265a0, &b, 2, b_args);
        using Touch = void XB_CDECL(uint8_t*, uint8_t*, uint32_t);
        if (field<uint32_t>(ent, 0x338))
            title_function<Touch>(title_global<uint32_t>(0x008da814 + field<uint32_t>(ent, 0x338) * 4))(ent, other, 1);
        if (field<uint32_t>(other, 0x338))
            title_function<Touch>(title_global<uint32_t>(0x008da814 + field<uint32_t>(other, 0x338) * 4))(other, ent, 1);
    }
}

}  // namespace game
