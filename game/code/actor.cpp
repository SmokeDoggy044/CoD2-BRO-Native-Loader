#include "actor.h"

#include <cstring>

#include "bg_misc.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

uint8_t* sentients() { return title_global<uint8_t*>(0x00985378); }
int32_t level_time() { return title_global<int32_t>(0x0098540c); }

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}
template <typename T>
const T& field(const uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<const T*>(base + offset);
}

// The first used sentient from index `start` on whose team is in `mask`.
uint8_t* sentient_of_teams(int32_t start, uint32_t mask) {
    uint8_t* const base = sentients();
    for (int32_t i = start; i < 0x21; ++i) {
        uint8_t* sentient = base + i * 0x60;
        if (field<int32_t>(sentient, 0x5c) < 0) continue;
        if (mask & (1u << (field<uint32_t>(sentient, 4) & 31))) return sentient;
    }
    return nullptr;
}

}  // namespace

int32_t XB_CDECL Sentient_EnemyTeam(int32_t team) {
    const int32_t enemies[5] = {0, 2, 1, 0, 0};
    return enemies[team];
}

float* XB_REGS3 XB_CDECL Sentient_GetOrigin(float* out, uint32_t, uint8_t* const* sentient) {
    uint32_t* words = reinterpret_cast<uint32_t*>(out);
    words[0] = field<uint32_t>(*sentient, 0x140);
    words[1] = field<uint32_t>(*sentient, 0x144);
    words[2] = field<uint32_t>(*sentient, 0x148);
    return out;  // EAX stays the first argument
}

uint32_t XB_REGS1 XB_CDECL Sentient_HasGoalYaw(const uint8_t* sentient) {
    return field<float>(sentient, 0x20) != title_global<float>(0x0083eb34) ? 1 : 0;  // -1
}

PLATFORM_ASM_ENTRY(Sentient_SetGoalYaw_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Sentient_SetGoalYaw) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Sentient_SetGoalYaw(uint8_t* sentient, float yaw) {
    const float zero = 0.0f;
    if (zero > yaw) {
        const float full = title_global<float>(0x0083ebcc);  // 360
        do yaw = yaw + full;
        while (zero > yaw);
    } else {
        const float full = title_global<float>(0x0083ebcc);
        if (yaw >= full) {
            do yaw = yaw - full;
            while (yaw >= full);
        }
    }
    field<float>(sentient, 0x20) = yaw;
}

PLATFORM_ASM_ENTRY(Sentient_FirstOfTeams_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Sentient_FirstOfTeams) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL Sentient_FirstOfTeams(uint32_t mask) { return sentient_of_teams(0, mask); }

PLATFORM_ASM_ENTRY(Sentient_NextOfTeams_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Sentient_NextOfTeams) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL Sentient_NextOfTeams(const uint8_t* previous, uint32_t mask) {
    const int32_t index = static_cast<int32_t>(previous - sentients()) / 0x60;
    return sentient_of_teams(index + 1, mask);
}

void XB_CDECL Sentient_ClearAll() {
    for (uint32_t address = 0x0095f424; address <= 0x0095f448; address += 4) title_global<uint32_t>(address) = 0;
    title_global<int32_t>(0x0095f420) = -1;
    for (uint32_t offset = 0; offset < 0xc60; offset += 0x60) field<int32_t>(sentients(), offset + 0x5c) = -1;
}

uint32_t XB_THISCALL Actor_Grenade_Resume(const uint8_t* actor) { return field<uint32_t>(actor, 0x1310) == 0; }

uint32_t XB_THISCALL Actor_CriticalSection_Start(uint8_t* actor) {
    field<int32_t>(actor, 0xc4) = level_time();
    return 0;
}

uint32_t XB_THISCALL Actor_Negotiation_Start(uint8_t* actor) {
    field<uint32_t>(actor, 0x12c) = 1;
    field<uint32_t>(actor, 0x268) = 2;
    return 0;
}

uint32_t XB_REGS1 XB_CDECL Path_LookaheadAtMinimum(const uint8_t* path) {
    return field<float>(path, 0x3b4) <= title_global<float>(0x0083eb4c) ? 1 : 0;  // 0.001
}

uint32_t XB_REGS1 XB_CDECL Actor_HasPendingSentientSlot(const uint8_t* actor) {
    for (uint32_t i = 0; i < 4; ++i) {
        if (field<uint32_t>(actor, 0x12a8 + i * 0x18) && field<uint32_t>(actor, 0x12bc + i * 0x18) == 0) return 1;
    }
    return 0;
}

PLATFORM_ASM_ENTRY(Actor_IsInState_entry,
                   "pushl %esi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(Actor_IsInState) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_IsInState(const uint8_t* actor, uint32_t state) {
    const int32_t top = field<int32_t>(actor, 0xbc);
    for (int32_t i = 0; i <= top; ++i)
        if (field<uint32_t>(actor, 0xa4 + i * 4) == state) return 1;
    return 0;
}

PLATFORM_ASM_ENTRY(Actor_SetSentientVisibility_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_SetSentientVisibility) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Actor_SetSentientVisibility(uint8_t* record, uint32_t visible, float value) {
    record[0] = static_cast<uint8_t>(visible);
    field<float>(record, 4) = value;
    field<int32_t>(record, 8) = level_time();
    if (visible) field<int32_t>(record, 0xc) = level_time();
}

int32_t XB_THISCALL Actor_GetSquadSlot(const uint8_t* ent) {
    if (!title_global<uint32_t>(0x00986008)) return -1;
    const uint8_t* actor = field<const uint8_t*>(ent, 0x240);
    if (!actor || !field<uint32_t>(actor, 0x137c)) return -1;
    if (field<int32_t>(field<const uint8_t*>(ent, 0x244), 4) != 2) return -1;
    return field<int16_t>(actor, 0x1382);
}

namespace {

uint8_t* actor_of_teams(int32_t start, uint32_t mask) {
    for (int32_t i = start; i < 0x20; ++i) {
        uint8_t* actor = title_global<uint8_t*>(0x00985384 + i * 4);  // g_actors
        if (!actor || field<int32_t>(actor, 0x1360) < 0) continue;
        const uint32_t team = field<uint32_t>(field<const uint8_t*>(actor, 4), 4);
        if (mask & (1u << (team & 31))) return actor;
    }
    return nullptr;
}

int32_t sentient_index(const uint8_t* sentient) { return static_cast<int32_t>(sentient - sentients()) / 0x60; }

}  // namespace

PLATFORM_ASM_ENTRY(Actor_FirstOfTeams_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_FirstOfTeams) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL Actor_FirstOfTeams(uint32_t mask) { return actor_of_teams(0, mask); }

PLATFORM_ASM_ENTRY(Actor_NextOfTeams_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_NextOfTeams) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL Actor_NextOfTeams(const uint8_t* previous, uint32_t mask) {
    const int32_t next = field<int16_t>(previous, 0x1382) + 1;
    if (next >= 0x20) return nullptr;
    return actor_of_teams(next, mask);
}

void XB_REGS3 XB_CDECL Actor_WasAttackedBy(const uint8_t* attacker, uint32_t, uint8_t* actor) {
    const uint8_t* ent = field<const uint8_t*>(actor, 0);
    if (ent && (field<uint32_t>(ent, 0x2a4) & 0x10000000)) return;
    field<const uint8_t*>(actor, 0x12a4) = attacker;
    field<int32_t>(actor, 0x864 + sentient_index(attacker) * 0x50) = level_time();
}

PLATFORM_ASM_ENTRY(Actor_ForgetEnemyLastKnownTime_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_ForgetEnemyLastKnownTime) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_ForgetEnemyLastKnownTime(uint8_t* actor) {
    const uint8_t* enemy = field<const uint8_t*>(field<const uint8_t*>(actor, 4), 0x3c);
    field<uint32_t>(actor, 0x868 + sentient_index(enemy) * 0x50) = 0;
}

PLATFORM_ASM_ENTRY(Actor_IsUsingTurret_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_IsUsingTurret) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_IsUsingTurret(const uint8_t* actor) {
    const uint8_t* turret = field<const uint8_t*>(actor, 0x136c);
    if (!turret) return 0;
    const uint32_t handle = field<uint32_t>(turret, 0x1a0);
    const uint32_t index = handle & 0xfff;
    uint32_t user = 0;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        user = title_global<uint32_t>(0x0097a968 + index * 8);
    return user == field<uint32_t>(actor, 0) ? 1 : 0;
}

long double XB_REGS3 XB_CDECL Actor_NodeDistanceSquared(uint32_t, const uint8_t* node, uint8_t* const* ent) {
    using ext = long double;
    const uint8_t* origin = *ent + 0x140;
    float d[3];
    for (uint32_t i = 0; i < 3; ++i)
        d[i] = static_cast<float>(static_cast<ext>(field<float>(origin, i * 4)) - field<float>(node, 0x44 + i * 4));
    ext sum = static_cast<ext>(d[2]) * d[2];
    sum = sum + static_cast<ext>(d[1]) * d[1];
    return sum + static_cast<ext>(d[0]) * d[0];
}

namespace {

void set_move_script(uint8_t* actor, uint32_t script, uint32_t fallback) {
    const int32_t until = field<int32_t>(actor, 0x798);
    field<uint32_t>(actor, 0x254) = until != 0 && until - level_time() < 200 ? script : fallback;
    field<uint32_t>(actor, 0x27c) = 1;
    actor[0x285] = 0;
}

}  // namespace

uint8_t* XB_REGS1 XB_STDCALL Actor_AnimRun(uint8_t* actor, uint32_t script) {
    set_move_script(actor, script, 0x00987b98);  // g_animScript_run
    return actor;  // EAX stays the first argument
}

uint8_t* XB_REGS1 XB_STDCALL Actor_AnimWalk(uint8_t* actor, uint32_t script) {
    set_move_script(actor, script, 0x00987be8);  // g_animScript_walk
    return actor;  // EAX stays the first argument
}

uint32_t XB_REGS3 XB_CDECL Actor_IsStatePushAllowed(uint32_t state, uint32_t, const uint8_t* actor) {
    const int32_t top = field<int32_t>(actor, 0xbc);
    const uint32_t* blockers = &title_global<uint32_t>(0x00830da0 + state * 20);
    for (int32_t i = 1; i <= top; ++i) {
        const uint32_t current = field<uint32_t>(actor, 0xa4 + i * 4);
        for (uint32_t j = 0; j < 5; ++j)
            if (current == blockers[j]) return 0;
    }
    return 1;
}

// The Start callbacks run where the original's calls put them (one push: EDI), with its
// registers: some keep stack addresses in globals (0x00929b40). The C frame sits below a gap.
PLATFORM_ASM_ENTRY(Actor_StartState_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebx\n"
                   "pushl 8(%ebp)\n"
                   "pushl %esi\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_StartState) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret $4")

uint32_t XB_CDECL Actor_StartState(uint32_t entry, uint8_t* actor, uint32_t state, uint32_t ebx) {
    const uint32_t top = entry - 4;  // after push edi
    if (field<uint32_t>(actor, 0x20) == 0) field<int32_t>(actor, 0x24) = level_time();
    for (uint32_t count = 1; count <= 0xd; ++count) {
        uint32_t& slot = field<uint32_t>(actor, 8 + field<uint32_t>(actor, 0x20) * 4);
        const uint32_t previous = slot;
        slot = state;
        // Start (slot 0 of the state's nine): actor in ECX (and ESI), the replaced state in EDX.
        const uint32_t start = title_global<uint32_t>(0x00830c28 + state * 9 * 4);
        const TitleRegs regs = {state * 9, ebx, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)), previous,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)), count};
        state = title_call_at_regs(top, start, &regs, 0, nullptr);
        if (!state) return 0;
    }
    return state;
}

namespace {

void copy_path(const uint8_t* from, uint8_t* to, int16_t points) {
    const uint32_t bytes = static_cast<uint32_t>(points * 0x1c);
    for (uint32_t i = 0; i < bytes; ++i) to[i] = from[i];
    for (uint32_t i = 0; i < 0x5c; i += 4) field<uint32_t>(to, 0x380 + i) = field<uint32_t>(from, 0x380 + i);
}

const uint32_t kSentientInfoWords[] = {0x18, 0x20, 0x24, 0x28, 0x34};

}  // namespace

const uint8_t* XB_REGS2 XB_CDECL Path_CopyToActor(const uint8_t* path, uint8_t* actor) {
    copy_path(path, actor + 0x3c0, field<int16_t>(path, 0x382));
    return path;  // EAX stays the first argument
}

void XB_REGS2 XB_CDECL Path_CopyFromActor(const uint8_t* actor, uint8_t* path) {
    copy_path(actor + 0x3c0, path, field<int16_t>(actor, 0x742));
}

uint8_t* XB_REGS3 XB_CDECL Actor_CopySentientInfo(uint8_t* to, uint32_t, const uint8_t* from) {
    field<uint32_t>(to, 0x14) = 0;
    field<uint32_t>(to, 0x1c) = 0;
    for (uint32_t i = 0; i < 5; ++i)
        field<uint32_t>(to, kSentientInfoWords[i]) = field<uint32_t>(from, kSentientInfoWords[i]);
    field<uint16_t>(to, 0x30) = field<uint16_t>(from, 0x30);
    for (uint32_t offset = 0x40; offset <= 0x48; offset += 4) field<uint32_t>(to, offset) = field<uint32_t>(from, offset);
    return to;  // EAX stays the first argument
}

uint8_t* XB_REGS1 XB_CDECL Actor_AnimReacquire(uint8_t* actor) {
    const uint8_t* sentient = field<const uint8_t*>(field<const uint8_t*>(actor, 0), 0x244);
    const uint32_t script = field<uint32_t>(actor, 0x270);
    if (field<int32_t>(sentient, 4) == 2 && script) {
        field<uint32_t>(actor, 0x254) = script;
        const uint32_t mode = field<uint32_t>(actor, 0x274);
        field<uint32_t>(actor, 0x27c) = mode;
        if (mode == 1) field<uint32_t>(actor, 0x27c) = 2;
    } else {
        field<uint32_t>(actor, 0x254) = 0x00987b88;  // g_animScript_reacquire
        field<uint32_t>(actor, 0x27c) = 2;
    }
    actor[0x285] = 0;
    return actor;  // EAX stays the first argument
}

PLATFORM_ASM_ENTRY(Actor_FindSentientSlot_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_FindSentientSlot) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Actor_FindSentientSlot(const uint8_t* actor, uint32_t sentient) {
    uint32_t oldest = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        if (field<uint32_t>(actor, 0x12ac + i * 0x18) == sentient) return i;
        if (field<int32_t>(actor, 0x12a8 + i * 0x18) < field<int32_t>(actor, 0x12a8 + oldest * 0x18)) oldest = i;
    }
    return oldest;
}

uint8_t* XB_REGS1 XB_CDECL Path_ShrinkLookaheadBy(uint8_t* path) {
    const uint32_t flags = field<uint32_t>(path, 0x3bc);
    float& distance = field<float>(path, 0x3b4);
    if (flags & 2)
        distance = (distance - title_global<float>(0x0083eb44)) * title_global<float>(0x0083eb58);  // 6.4, 0.75
    else
        distance = (distance - title_global<float>(0x0083eb54)) * title_global<float>(0x0083eb50);  // 12.8, 0.5625
    const float minimum = title_global<float>(0x0083eb4c);  // 0.001
    if (minimum > distance) distance = minimum;
    field<uint32_t>(path, 0x3bc) = flags & ~3u;
    return path;  // EAX stays the first argument
}

uint8_t* XB_REGS1 XB_CDECL Actor_ClearLookAt(uint8_t* actor) {
    field<uint32_t>(actor, 0x1e8) = 0;
    for (uint32_t offset = 0x1ec; offset <= 0x1fc; offset += 4) field<uint32_t>(actor, offset) = 0;  // 0.0f
    field<float>(actor, 0x200) = title_global<float>(0x0083eb68);                                    // 120
    field<uint32_t>(actor, 0x204) = 0;
    field<float>(actor, 0x208) = title_global<float>(0x0083eb64);  // 90
    field<float>(actor, 0x20c) = title_global<float>(0x0083eb64);
    return actor;  // EAX stays the first argument
}

PLATFORM_ASM_ENTRY(Path_IsLongerThan_entry,
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Path_IsLongerThan) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Path_IsLongerThan(const uint8_t* path, float distance) {
    if (field<int16_t>(path, 0x386)) return 1;
    float length = field<float>(path, 0x3b8) + field<float>(path, 0x3b0);
    if (length > distance) return 1;
    const int16_t points = field<int16_t>(path, 0x388);
    if (!points) return 0;
    int32_t i = points - 1;
    if (i == field<int16_t>(path, 0x380) - 2) {
        length = field<float>(path, 0x3c8) + length;
        if (length > distance) return 1;
        --i;
    }
    for (; i >= 0; --i) {
        length = length + field<float>(path, 0x14 + i * 0x1c);
        if (length > distance) return 1;
    }
    return 0;
}

uint32_t XB_REGS3 XB_STDCALL Path_IsStale(const uint8_t* path, uint32_t, const float* goal, float radius) {
    const float dz = field<float>(path, 0x398) - goal[2];
    const float dy = field<float>(path, 0x394) - goal[1];
    const float dx = field<float>(path, 0x390) - goal[0];
    const float limit = radius + title_global<float>(0x0083eb88);  // 0.005
    if ((dz * dz + dy * dy) + dx * dx > limit) return 1;
    if (!(path[0x3bc] & 4)) return 0;
    return field<int16_t>(path, 0x380) * 4 > field<int16_t>(path, 0x382) ? 0 : 1;
}

uint32_t XB_REGS1 XB_STDCALL Actor_IsEventWorthNoticing(const uint8_t* event, const uint8_t* base) {
    const uint32_t handle = field<uint32_t>(event, 0x224);
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return 0;
    const uint8_t* ent = title_global<const uint8_t*>(0x0097a968 + index * 8);
    if (!ent) return 0;
    const uint8_t* record = base + sentient_index(field<const uint8_t*>(ent, 0x244)) * 0x50 + 0x850;
    const int32_t now = level_time();
    if (field<int32_t>(record, 0x1c) > now) return 1;
    const int32_t noticed = field<int32_t>(record, 0x18);
    if (noticed <= 0) return 1;
    return now - noticed >= 2000 ? 1 : 0;
}

uint8_t* XB_REGS2 XB_CDECL Actor_ClearSentientSlots(uint8_t* actor, uint32_t sentient) {
    for (uint32_t slot = 0x12a8; slot < 0x1308; slot += 0x18) {
        if (field<uint32_t>(actor, slot + 4) != sentient) continue;
        field<uint32_t>(actor, slot) = 0;
        field<uint32_t>(actor, slot + 4) = 0;
        field<uint32_t>(actor, slot + 0x14) = 0;
    }
    return actor;  // EAX stays the first argument
}

uint8_t* XB_CDECL Sentient_Alloc() {
    uint8_t* const base = sentients();
    int32_t chosen = -1;
    int32_t oldest = static_cast<int32_t>(0x80000000u);
    for (int32_t i = 0; i < 0x21; ++i) {
        const int32_t used = field<int32_t>(base, i * 0x60 + 0x5c);
        if (used == -1) {
            chosen = i;
            break;
        }
        if (used < 0 && used > oldest) {
            chosen = i;
            oldest = used;
        }
    }
    if (chosen < 0) return nullptr;
    for (uint32_t i = 0; i < 0x18; ++i) field<uint32_t>(base, chosen * 0x60 + i * 4) = 0;
    field<int32_t>(sentients(), chosen * 0x60 + 0x5c) = level_time();
    return sentients() + chosen * 0x60;
}

PLATFORM_ASM_ENTRY(Actor_SetOrientFromDir_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Actor_SetOrientFromDir) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL Actor_SetOrientFromDir(uint8_t* out, const float* dir, float yaw_offset) {
    float angles[3];
    vectoangles_call(dir, angles);
    const float to_short = title_global<float>(0x0083ebd8);  // 65536 / 360
    const float yaw = angles[1] + yaw_offset;
    const int32_t pitch16 = static_cast<int32_t>(angles[0] * to_short) & 0xffff;
    const int32_t yaw16 = static_cast<int32_t>(yaw * to_short) & 0xffff;
    const float to_angle = title_global<float>(0x0083ebd4);  // 360 / 65536
    const float new_yaw = static_cast<float>(yaw16) * to_angle;
    field<float>(out, 4) = static_cast<float>(pitch16) * to_angle;
    field<float>(out, 8) = new_yaw;
    field<float>(out, 0xc) = new_yaw;
}

uint8_t* XB_REGS1 XB_CDECL Actor_ClearMoveHistory(uint8_t* actor) {
    for (uint32_t offset = 0x1398; offset <= 0x13e4; offset += 4) field<uint32_t>(actor, offset) = 0;  // 0.0f
    field<uint32_t>(actor, 0x1394) = 0;
    return actor;  // EAX stays the first argument
}

PLATFORM_ASM_ENTRY(Actor_RecordSentientPosition_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Actor_RecordSentientPosition) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL Actor_RecordSentientPosition(const uint8_t* sentient, uint8_t* actor) {
    const uint32_t offset = sentient_index(sentient) * 0x50;
    field<int32_t>(actor, offset + 0x868) = level_time();
    const uint8_t* ent = field<const uint8_t*>(sentient, 0);
    uint8_t* record = actor + offset + 0x850;
    for (uint32_t i = 0; i < 12; i += 4) field<uint32_t>(record, 0x20 + i) = field<uint32_t>(ent, 0x140 + i);
    field<uint32_t>(record, 0x34) = sentient[0x5a];
    field<uint16_t>(record, 0x30) = field<uint16_t>(sentient, 0x58);
    for (uint32_t i = 0; i < 12; i += 4)
        field<uint32_t>(record, 0x40 + i) = field<uint32_t>(field<const uint8_t*>(actor, 0), 0x140 + i);
    return record;
}

void XB_REGS1 XB_CDECL Actor_ResetSentientInfo(uint8_t* actor) {
    uint8_t* ent = field<uint8_t*>(actor, 0);
    for (uint32_t i = 0; i < 16; i += 4) field<uint32_t>(ent, 0x430 + i) = title_global<uint32_t>(0x0082eed0);  // FLT_MAX
    ent = field<uint8_t*>(actor, 0);
    for (uint32_t i = 0; i < 16; i += 4)
        field<uint32_t>(ent, 0x440 + i) = field<uint32_t>(ent, 0x430 + i) ^ title_global<uint32_t>(0x0082e0b0 + i);
    field<uint32_t>(actor, 0x844) = 0;
    for (uint32_t i = 0; i < 0x21; ++i) field<uint32_t>(actor, 0x860 + i * 0x50) = 0;
}

PLATFORM_ASM_ENTRY(Actor_ScoreEnemyByDistance_entry,
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_ScoreEnemyByDistance) "addl $12, %esp\n"
                   "ret")

int32_t XB_CDECL Actor_ScoreEnemyByDistance(const uint8_t* sentient, const uint8_t* actor, float distance) {
    const uint8_t* ent = field<const uint8_t*>(actor, 0);
    if (ent && (field<uint32_t>(ent, 0x2a4) & 0x10000000)) return 0;
    const float range = title_global<float>(0x0083ecc8);  // 2000
    if (distance >= range) return 0;
    const uint32_t offset = sentient_index(sentient) * 0x50;
    const int32_t unseen = actor[offset + 0x850] ? 0 : level_time() - field<int32_t>(actor, offset + 0x868);
    const float closeness = range - distance;
    float score = closeness * closeness * title_global<float>(0x00831008);  // 0.00125
    score = score - static_cast<float>(unseen);
    return static_cast<int32_t>(score + title_global<float>(0x0083eb24));  // 0.5
}

PLATFORM_ASM_ENTRY(Path_GetLookaheadPoint_entry,
                   "pushl %esi\n"
                   "pushl %edx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Path_GetLookaheadPoint) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL Path_GetLookaheadPoint(const uint8_t* path, float* out, const float* origin) {
    if (field<int16_t>(path, 0x380) <= 1 || (path[0x3bc] & 1)) return 0;
    const float limit = title_global<float>(0x0083eb3c);  // 60
    const float length = field<float>(path, 0x3b0) > limit ? limit : field<float>(path, 0x3b0);
    out[0] = field<float>(path, 0x39c) * length + origin[0];
    out[1] = field<float>(path, 0x3a0) * length + origin[1];
    return 1;
}

namespace {

// A path node by handle: block (handle >> 9) - 1 of g_pathNodeBlocks (+8), node handle & 0x1ff
// (0x80 bytes each, from the block's +0x10); 0 for a missing block.
uint8_t* path_node(uint16_t handle) {
    if (handle == 0 || handle == 0xffff) return nullptr;
    const uint8_t* blocks = title_global<const uint8_t*>(0x0095f4f4);
    const uint8_t* block = field<const uint8_t*>(blocks, 8 + static_cast<uint16_t>((handle >> 9) - 1) * 4);
    if (!block) return nullptr;
    return field<uint8_t*>(const_cast<uint8_t*>(block), 0x10) + (handle & 0x1ff) * 0x80;
}

bool node_handle_set(uint16_t handle) { return handle != 0 && handle != 0xffff; }

}  // namespace

void XB_REGS2 XB_CDECL Sentient_ClaimNodeTimed(uint32_t, uint8_t* sentient) {
    field<int32_t>(sentient, 8) = level_time() + 5000;
    for (uint32_t offset = 0x58; offset <= 0x5a; offset += 2) {
        const uint16_t handle = field<uint16_t>(sentient, offset);
        if (!node_handle_set(handle)) return;
        uint8_t* node = path_node(handle);
        if (--field<int16_t>(node, 0x26) == 0) {
            field<uint32_t>(node, 4) = field<uint32_t>(sentient, 4);
            field<uint32_t>(node, 8) = field<uint32_t>(sentient, 8);
        }
    }
}

void XB_REGS3 XB_CDECL Sentient_ClaimNodeIndefinitely(uint32_t claimer, uint32_t, uint8_t* sentient) {
    field<uint32_t>(sentient, 4) = claimer;
    field<uint32_t>(sentient, 8) = 0x7fffffff;
    for (uint32_t offset = 0x58; offset <= 0x5a; offset += 2) {
        const uint16_t handle = field<uint16_t>(sentient, offset);
        if (!node_handle_set(handle)) return;
        uint8_t* node = path_node(handle);
        ++field<int16_t>(node, 0x26);
        field<uint32_t>(node, 4) = 0;
        field<uint32_t>(node, 8) = 0x7fffffff;
    }
}

uint8_t* XB_REGS1 XB_CDECL Actor_ExpirePathPlanes(uint8_t* actor) {
    for (uint32_t slot = 0x12a8; slot < 0x1308; slot += 0x18) {
        const int32_t since = field<int32_t>(actor, slot);
        if (since <= 0) continue;
        const int32_t age = level_time() - since;
        if ((field<uint32_t>(actor, slot + 0x14) != 0 && age >= 300) || age >= field<int32_t>(actor, 0x1308)) {
            field<int32_t>(actor, slot) = 0;
            field<uint8_t*>(actor, 4)[0x5a] = 0;
        }
    }
    return actor;  // EAX stays the first argument
}

uint32_t XB_REGS1 XB_CDECL Actor_IsOnClaimedNode(const uint8_t* actor) {
    const uint16_t handle = field<uint16_t>(field<const uint8_t*>(actor, 4), 0x48);
    const uint8_t* node = path_node(handle);
    const uint8_t* ent = field<const uint8_t*>(actor, 0);
    const float dx = field<float>(ent, 0x140) - field<float>(node, 0x44);
    const float dy = field<float>(ent, 0x144) - field<float>(node, 0x48);
    const float dz = field<float>(ent, 0x148) - field<float>(node, 0x4c);
    return title_global<float>(0x0083eef8) > (dz * dz + dy * dy) + dx * dx ? 1 : 0;  // 9
}

namespace {

float clear_sign(float value) {
    union {
        float f;
        uint32_t u;
    } bits = {value};
    bits.u &= 0x7fffffff;
    return bits.f;
}

// |dir.x * (p.y - o.y) - dir.y * (p.x - o.x)| for a path point (0x1c bytes: position, direction
// +0xc, length +0x14).
float swept(const uint8_t* point, const float* origin) {
    const float dy = field<float>(point, 4) - origin[1];
    const float a = field<float>(point, 0xc) * dy;
    const float dx = field<float>(point, 0) - origin[0];
    const float b = field<float>(point, 0x10) * dx;
    return clear_sign(a - b);
}

}  // namespace

void XB_REGS3 XB_CDECL Path_ShrinkLookahead(uint32_t, uint8_t* path, const float* origin) {
    const int16_t start = field<int16_t>(path, 0x380);
    const int16_t end = field<int16_t>(path, 0x382);
    if (start == end) return;
    const uint8_t* first = path + (start - 2) * 0x1c;
    const float length = field<float>(first, 0x14) - field<float>(path, 0x3c8);
    float area = length * swept(first, origin);
    for (int32_t i = start - 1; i < end - 1; ++i) {
        const uint8_t* point = path + i * 0x1c;
        area = field<float>(point, 0x14) * swept(point, origin) + area;
    }
    float& distance = field<float>(path, 0x3b4);
    distance = distance - area;
    const float minimum = title_global<float>(0x00830c1c);  // 64
    if (minimum > distance) distance = minimum;
}

namespace {

// The title CRT's __CIfmod (0x0033acda): x in ST1, y in ST0, fmod(x, y) left in ST0.
long double title_ci_fmod(long double x, long double y) {
    long double result;
    __asm__ volatile("call *%3"
                     : "=t"(result)
                     : "0"(y), "u"(x), "r"(uintptr_t{0x0033acda})
                     : "st(1)", "eax", "ecx", "edx", "memory");
    return result;
}

}  // namespace

PLATFORM_ASM_ENTRY(Sentient_IsYawInGoalArc_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Sentient_IsYawInGoalArc) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Sentient_IsYawInGoalArc(const uint8_t* sentient, float yaw) {
    const float goal = field<float>(sentient, 0x20);
    if (goal == title_global<float>(0x0083eb34)) return 1;  // -1: no goal yaw
    const long double difference = static_cast<long double>(goal) - yaw;
    float delta = static_cast<float>(title_ci_fmod(difference, title_global<double>(0x0083ed40)));  // 360.0
    const float full = title_global<float>(0x0083ebcc);                                           // 360
    if (platform_x87_greater(delta, title_global<float>(0x0083eb60))) delta = delta - full;       // 180
    if (title_global<float>(0x0083ebd0) > delta) delta = delta + full;                            // -180
    const float arc = field<float>(sentient, 0x24);
    if (!(delta >= 0.0f - arc)) return 0;
    if (!(arc >= delta)) return 0;
    return 1;
}

void XB_REGS1 XB_STDCALL Actor_UpdateYaw(uint8_t* actor, int32_t msec) {
    field<uint32_t>(actor, 0x154) = field<uint32_t>(actor, field<uint32_t>(actor, 0x140) ? 0x14c : 0x138);
    uint8_t* ent = field<uint8_t*>(actor, 0);
    const long double difference = static_cast<long double>(field<float>(actor, 0x154)) - field<float>(ent, 0x154);
    float delta = static_cast<float>(title_ci_fmod(difference, title_global<double>(0x0083ed40)));  // 360.0
    const float full = title_global<float>(0x0083ebcc);                                           // 360
    if (platform_x87_greater(delta, title_global<float>(0x0083eb60))) delta = delta - full;       // 180
    if (title_global<float>(0x0083ebd0) > delta) delta = delta + full;                            // -180
    // Turn rate: slow (0.036 / ms) while a timed slow turn (+0x19c start, +0x1a0 length; negative
    // = until start - length) is running, else 0.4 / ms.
    float rate = title_global<float>(0x0083ec44);  // 0.4
    const int32_t start = field<int32_t>(actor, 0x19c);
    if (start && !field<uint32_t>(actor, 0x198)) {
        const int32_t length = field<int32_t>(actor, 0x1a0);
        if (length > 0) {
            if (length + start < level_time()) field<int32_t>(actor, 0x1a0) = 0;
            rate = title_global<float>(0x00830c08);  // 0.036
        } else if (length == 0) {
            rate = title_global<float>(0x00830c08);
        } else if (start - length >= level_time()) {
            rate = title_global<float>(0x00830c08);
        } else {
            field<int32_t>(actor, 0x19c) = 0;
        }
    }
    const float limit = static_cast<float>(msec) * rate;
    if (delta > limit) {
        delta = limit;
    } else {
        const float negative = 0.0f - limit;
        if (negative > delta) delta = negative;
    }
    const int32_t yaw16 =
        static_cast<int32_t>((field<float>(ent, 0x154) + delta) * title_global<float>(0x0083ebd8)) & 0xffff;
    field<uint32_t>(ent, 0x150) = 0;  // 0.0f
    field<float>(ent, 0x154) = static_cast<float>(yaw16) * title_global<float>(0x0083ebd4);
    field<uint32_t>(ent, 0x158) = 0;
}

PLATFORM_ASM_ENTRY(Path_DropPassedPoints_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Path_DropPassedPoints) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Path_DropPassedPoints(uint32_t count, uint8_t* path) {
    if (!count) return;
    const int16_t dropped = static_cast<int16_t>(count);
    field<int16_t>(path, 0x380) = static_cast<int16_t>(field<int16_t>(path, 0x380) - dropped);
    field<int16_t>(path, 0x382) = static_cast<int16_t>(field<int16_t>(path, 0x382) - dropped);
    const int16_t start = field<int16_t>(path, 0x380);
    const int16_t end = field<int16_t>(path, 0x382);
    if (field<int16_t>(path, 0x384) >= 0) {
        field<int16_t>(path, 0x384) = static_cast<int16_t>(field<int16_t>(path, 0x384) - dropped);
        if (field<int16_t>(path, 0x384) < 0) field<int16_t>(path, 0x384) = 0;
    }
    field<int16_t>(path, 0x388) = static_cast<int16_t>(field<int16_t>(path, 0x388) - dropped);
    if (field<int16_t>(path, 0x388) < 0) {
        field<int16_t>(path, 0x388) = 0;
        field<uint32_t>(path, 0x3b8) = 0;  // 0.0f
    }
    field<int16_t>(path, 0x386) = static_cast<int16_t>(field<int16_t>(path, 0x386) - dropped);
    if (field<int16_t>(path, 0x386) < 0) field<int16_t>(path, 0x386) = 0;
    if (start > 1) {
        title_function<void* XB_CDECL(void*, const void*, uint32_t)>(0x001ef0c0)(  // _memmove
            path, path + count * 0x1c, static_cast<uint32_t>(end * 0x1c));
    } else {
        for (uint32_t i = 0; i < 12; i += 4) field<uint32_t>(path, i) = field<uint32_t>(path, 0x3cc + i);
        field<int16_t>(path, 0x382) = 1;
        field<uint32_t>(path, 0x3c8) = 0;
        field<uint16_t>(path, 0x18) = 0;
        field<uint32_t>(path, 0x14) = 0;
        field<uint32_t>(path, 0xc) = 0;
        field<uint32_t>(path, 0x10) = 0;
    }
    for (uint32_t i = 0; i < 12; i += 4) field<uint32_t>(path, 0x390 + i) = field<uint32_t>(path, i);
    field<uint32_t>(path, 0x3bc) &= ~4u;
    field<uint32_t>(path, 0x3d8) = 0;
}

namespace {

void record_toucher(const uint8_t* other, uint8_t* actor) {
    if (const uint8_t* sentient = field<const uint8_t*>(other, 0x244)) Actor_RecordSentientPosition(sentient, actor);
}

bool ignores_events(const uint8_t* actor) {
    const uint8_t* ent = field<const uint8_t*>(actor, 0);
    return ent && (field<uint32_t>(ent, 0x2a4) & 0x10000000);
}

}  // namespace

void XB_REGS3 XB_CDECL Actor_Turret_Touch(uint32_t, const uint8_t* other, uint8_t* actor) { record_toucher(other, actor); }
void XB_REGS3 XB_CDECL Actor_Noncombat_Touch(uint32_t, const uint8_t* other, uint8_t* actor) {
    record_toucher(other, actor);
}
void XB_REGS3 XB_CDECL Actor_Lmg_Touch(uint32_t, const uint8_t* other, uint8_t* actor) { record_toucher(other, actor); }

void XB_REGS1 XB_STDCALL Actor_EventNoticeSentient(uint8_t* actor, const uint8_t* sentient) {
    if (!ignores_events(actor)) Actor_RecordSentientPosition(sentient, actor);
}

PLATFORM_ASM_ENTRY(Actor_EventAttacked_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_EventAttacked) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_EventAttacked(uint8_t* actor, const uint8_t* attacker) {
    if (ignores_events(actor)) return;
    Actor_WasAttackedBy(attacker, 0, actor);
    Actor_RecordSentientPosition(attacker, actor);
}

PLATFORM_ASM_ENTRY(Actor_ForgetGrenade_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_ForgetGrenade) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_ForgetGrenade(uint8_t* grenade) {
    for (uint8_t* actor = Actor_FirstOfTeams(0xffffffff); actor; actor = Actor_NextOfTeams(actor, 0xffffffff))
        if (field<uint8_t*>(actor, 0x1310) == grenade) field<uint32_t>(actor, 0x1310) = 0;
    field<uint32_t>(grenade, 0x378) = 0;
}

PLATFORM_ASM_ENTRY(Actor_GetAnimTree_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_GetAnimTree) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_GetAnimTree(const uint8_t* actor) { return actor_anim_tree(actor); }

void XB_REGS3 XB_CDECL Actor_AnimScripted_Finish(uint32_t, uint32_t next, const uint8_t* actor) {
    uint8_t* ent = field<uint8_t*>(actor, 0);
    if (next == 10 || !field<uint32_t>(ent, 0x3d4)) return;
    title_function<void XB_CDECL(uint32_t)>(0x000428d0)(field<uint32_t>(ent, 0x3d4));  // Z_Free
    field<uint32_t>(ent, 0x3d4) = 0;
}

uint8_t* XB_REGS1 XB_CDECL Actor_MergeStateTransitions(uint8_t* actor) {
    // EAX stays the actor (callers rely on it).
    int32_t& count = field<int32_t>(actor, 0xa0);
    if (count < 2) return actor;
    do {
        if (!field<uint32_t>(actor, 0x2d0)) {
            count = 1;
            return actor;
        }
        int32_t next = count;
        const uint32_t a = field<uint32_t>(actor, 0x28 + next * 8);
        const uint32_t b = field<uint32_t>(actor, 0x30 + next * 8);
        const int32_t merged = title_global<int32_t>(0x00830e00 + (b + a * 4) * 4);  // g_aiTransitionMerge
        if (merged == -1) return actor;
        if (merged == 0) {
            next -= 2;
        } else {
            field<int32_t>(actor, 0x28 + next * 8) = merged;
            field<uint32_t>(actor, 0x2c + count * 8) = field<uint32_t>(actor, 0x34 + count * 8);
            next = count - 1;
        }
        count = next;
    } while (count >= 2);
    return actor;
}

namespace {

void queue_transition(uint8_t* actor, uint32_t kind, uint32_t state) {
    field<uint32_t>(actor, 0x38 + field<uint32_t>(actor, 0xa0) * 8) = kind;
    field<uint32_t>(actor, 0x3c + field<uint32_t>(actor, 0xa0) * 8) = state;
    ++field<uint32_t>(actor, 0xa0);
    Actor_MergeStateTransitions(actor);
}

void anim_move(uint8_t* actor, uint32_t script, bool walk) {
    if (!field<uint32_t>(actor, 0x1390)) {
        if (walk)
            Actor_AnimWalk(actor, script);
        else
            Actor_AnimRun(actor, script);
        return;
    }
    const float x = field<float>(actor, 0x2a0), y = field<float>(actor, 0x2a4);
    const float speed = x * x + y * y;
    field<uint32_t>(actor, 0x254) = title_global<float>(0x0083eb20) > speed ? 0x00987bb8 : 0x00987be8;  // stop / walk
    field<uint32_t>(actor, 0x27c) = 7;
    actor[0x285] = 0;
}

}  // namespace

void XB_REGS1 XB_CDECL Actor_PopState(uint8_t* actor) {
    queue_transition(actor, 3, 0);
    --field<int32_t>(actor, 0xbc);
}

PLATFORM_ASM_ENTRY(Actor_SetState_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_SetState) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_SetState(uint8_t* actor, uint32_t state) {
    queue_transition(actor, 1, state);
    field<uint32_t>(actor, 0xa4 + field<uint32_t>(actor, 0xbc) * 4) = state;
}

uint8_t* XB_REGS1 XB_STDCALL Actor_AnimMoveRun(uint8_t* actor, uint32_t script) { anim_move(actor, script, false); return actor; }
uint8_t* XB_REGS1 XB_STDCALL Actor_AnimMoveWalk(uint8_t* actor, uint32_t script) { anim_move(actor, script, true); return actor; }

void XB_REGS3 XB_CDECL Actor_EventLearnFromActor(const uint8_t* sentient, uint32_t, uint8_t* actor) {
    if (ignores_events(actor)) return;
    uint8_t* other = field<uint8_t*>(field<const uint8_t*>(sentient, 0), 0x240);
    const uint8_t* enemy = field<const uint8_t*>(sentient, 0x3c);
    if (!other) {
        Actor_RecordSentientPosition(enemy, actor);
        return;
    }
    const uint32_t offset = sentient_index(enemy) * 0x50;
    if (field<int32_t>(actor, offset + 0x868) >= field<int32_t>(other, offset + 0x868)) return;
    Actor_CopySentientInfo(actor + offset + 0x850, 0, other + offset + 0x850);
}

void XB_STDCALL Actor_ClearSentientThreatTimes(uint8_t* actor) {
    const int32_t enemies[5] = {0, 2, 1, 0, 0};
    const int32_t enemy = enemies[field<uint32_t>(field<const uint8_t*>(actor, 4), 4)];
    if (!enemy) return;
    const uint32_t mask = 1u << (enemy & 31);
    for (uint8_t* sentient = Sentient_FirstOfTeams(mask); sentient; sentient = Sentient_NextOfTeams(sentient, mask))
        field<uint32_t>(actor, sentient_index(sentient) * 0x50 + 0x86c) = 0;
}

uint32_t XB_THISCALL Actor_Lmg_Start(uint8_t* actor) {
    if (actor && field<uint32_t>(actor, 0)) {
        const uint32_t index = BG_FindWeaponIndexForHash(field<uint32_t>(actor, 0xf0)) & 0xff;
        const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[index];  // bg_weaponDefs
        if (def && field<int32_t>(def, 0x98) == 3) {
            if (field<int32_t>(actor, 0x28) != 0x2be) {
                field<int32_t>(actor, 0x28) = 0x2bc;
                field<int32_t>(actor, 0x2c) = level_time() + 1;
                field<int32_t>(actor, 0x34) = level_time() + 0x1f5;
            }
            return 0;
        }
    }
    return field<uint32_t>(field<const uint8_t*>(actor, 4), 0x3c) ? 2 : 5;
}

// The Finish callbacks run at the original's ESP (no pushes before the call).
// EBP is saved below the gap: the callbacks' return address goes to entry ESP - 4.
PLATFORM_ASM_ENTRY(Actor_ForceState_entry,
                   "movl %esp, %eax\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_ForceState) "addl $16, %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
                   "ret")

void XB_CDECL Actor_ForceState(uint32_t entry, uint8_t* actor, uint32_t state, uint32_t ebx) {
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor));
    while (field<int32_t>(actor, 0x20) > 0) {
        const uint32_t current = field<uint32_t>(actor, 8 + field<uint32_t>(actor, 0x20) * 4);
        const uint32_t finish = title_global<uint32_t>(0x00830c2c + current * 9 * 4);
        const TitleRegs regs = {current * 9, ebx, self, state, self, state};
        title_call_at_regs(entry, finish, &regs, 0, nullptr);
        --field<int32_t>(actor, 0x20);
    }
    field<uint32_t>(actor, 0xa0) = 0;
    field<uint32_t>(actor, 0xbc) = 0;
    field<uint32_t>(actor, 0x38) = 1;
    field<uint32_t>(actor, 0x3c + field<uint32_t>(actor, 0xa0) * 8) = state;
    ++field<uint32_t>(actor, 0xa0);
    Actor_MergeStateTransitions(actor);
    field<uint32_t>(actor, 0xa4 + field<uint32_t>(actor, 0xbc) * 4) = state;
}

PLATFORM_ASM_ENTRY(Actor_StopAnimScript_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_StopAnimScript) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_StopAnimScript(uint8_t* actor) {
    const uint8_t* script = field<const uint8_t*>(actor, 0x250);
    if (script && field<uint32_t>(script, 0xc)) {
        // The stop callback: the script in EAX, the entity number on the stack (caller pops) and
        // in EDX, the entity in ECX.
        const uint8_t* ent = field<const uint8_t*>(actor, 0);
        const uint32_t number = field<uint32_t>(ent, 0x224);
        title_function<void XB_REGS3 XB_CDECL(const uint8_t*, uint32_t, const uint8_t*, uint32_t)>(
            field<uint32_t>(script, 0xc))(field<const uint8_t*>(actor, 0x250), number, ent, number);
    }
    const uint32_t thread = field<uint32_t>(actor, 0x24c);
    if (!thread) return;
    Scr_TerminateThread(thread);
    Scr_Notify(field<uint8_t*>(actor, 0), title_global<uint32_t>(0x0097a710));  // scr_const_killanimscript
    field<uint32_t>(actor, 0x24c) = 0;
}

PLATFORM_ASM_ENTRY(Actor_Exposed_StartReacquire_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_Exposed_StartReacquire) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_Exposed_StartReacquire(uint32_t mode, uint8_t* actor) {
    if (mode == 1) {
        const uint8_t* enemy = field<const uint8_t*>(field<const uint8_t*>(actor, 4), 0x3c);
        if (field<int32_t>(actor, sentient_index(enemy) * 0x50 + 0x86c) > level_time()) {
            queue_transition(actor, 1, 1);
            field<uint32_t>(actor, 0xa4 + field<uint32_t>(actor, 0xbc) * 4) = 1;
        }
    }
    field<uint32_t>(actor, 0x28) = 0xca;
    field<int32_t>(actor, 0x2c) = level_time() + 1;
    field<int32_t>(actor, 0x34) = level_time() + 0x1f5;
}

namespace {

// PathNode_HandleIsValid (0x00014480) / PathNode_FromHandle2 (0x002ba010): handle pointer in EAX.
bool node_handle_valid(const uint16_t* handle) {
    return static_cast<uint8_t>(title_function<uint32_t XB_REGS1 XB_CDECL(const uint16_t*)>(0x00014480)(handle)) != 0;
}
uint8_t* node_from_handle(const uint16_t* handle) {
    return title_function<uint8_t* XB_REGS1 XB_CDECL(const uint16_t*)>(0x002ba010)(handle);
}

}  // namespace

void XB_STDCALL Sentient_SetClaimedNode(uint8_t* sentient, uint32_t node) {
    uint16_t* claimed = &field<uint16_t>(sentient, 0x48);
    if (static_cast<uint16_t>(node) == *claimed) return;
    if (node_handle_valid(claimed)) Sentient_ClaimNodeTimed(0, node_from_handle(claimed));
    *claimed = static_cast<uint16_t>(node);
    if (node_handle_valid(claimed))
        Sentient_ClaimNodeIndefinitely(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sentient)), 0,
                                       node_from_handle(claimed));
}

void XB_REGS3 XB_STDCALL Actor_Cover_Pain(uint32_t, const uint8_t* attacker, const uint8_t* actor, uint32_t, uint32_t,
                                          uint32_t, uint32_t, uint32_t) {
    const uint8_t* sentient = field<const uint8_t*>(actor, 4);
    if (field<int32_t>(sentient, 4) != 1) return;
    const uint8_t* other = field<const uint8_t*>(attacker, 0x244);
    if (other && field<int32_t>(other, 4) == 1) return;
    const uint16_t* claimed = &field<uint16_t>(const_cast<uint8_t*>(sentient), 0x48);
    if (!node_handle_valid(claimed)) return;
    const int32_t until = level_time() + 5000;
    uint8_t* node = node_from_handle(claimed);
    field<int32_t>(node, 0x14 + field<uint32_t>(sentient, 4) * 4) = until;
}

PLATFORM_ASM_ENTRY(Actor_SetDesiredAngles_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_SetDesiredAngles) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL Actor_SetDesiredAngles(uint8_t* actor, float pitch, float yaw) {
    const float to_short = title_global<float>(0x0083ebd8), to_angle = title_global<float>(0x0083ebd4);
    float angles[3];
    angles[0] = static_cast<float>(static_cast<int32_t>(pitch * to_short) & 0xffff) * to_angle;
    field<float>(actor, 0x100) = angles[0];
    angles[1] = static_cast<float>(static_cast<int32_t>(yaw * to_short) & 0xffff) * to_angle;
    field<float>(actor, 0x104) = angles[1];
    angles[2] = 0.0f;
    uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles)),
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor + 0x108))};
    AnglesToAxis(args, reinterpret_cast<float*>(actor + 0x114), reinterpret_cast<float*>(actor + 0x120));
}

PLATFORM_ASM_ENTRY(Actor_InitSpawner_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_InitSpawner) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_InitSpawner(uint8_t* ent) {
    const uint8_t guard = title_global<uint8_t>(0x0098b5d4);
    field<uint32_t>(ent, 0x2a8) = 0;
    field<uint32_t>(ent, 0x134) = 0;
    field<uint32_t>(ent, 0xd4) = 1;
    ent[0] = 0xc;
    field<int32_t>(ent, 0x2f0) = -1;
    field<uint32_t>(ent, 0x3b4) = 0;
    uint32_t hash;
    if (!(guard & 1)) {
        title_global<uint32_t>(0x0098b5d4) |= 1;
        hash = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083ae84}));  // "count"
        title_global<uint32_t>(0x0098b5d0) = hash;
    } else {
        hash = title_global<uint32_t>(0x0098b5d0);
    }
    if (field<uint32_t>(ent, 0x370)) return 1;
    field<uint32_t>(ent, 0x370) = 1;
    const int32_t count = title_global<int32_t>(0x00985420);  // level_numSpawnVars
    for (int32_t i = 0; i < count; ++i) {
        if (title_global<uint32_t>(0x00985424 + i * 8) == hash) {
            field<uint32_t>(ent, 0x370) = 0;
            break;
        }
    }
    return 1;
}

uint8_t* XB_REGS1 XB_CDECL Actor_Alloc(int32_t context) {
    uint8_t* actor = nullptr;
    if (context != -1) {
        const uint32_t memory =
            context >= 0 && context < 0x63 ? field<uint32_t>(title_global<uint8_t*>(0x0095f638), 0x38 + context * 4) : 0;
        // MemContext_Alloc_r (0x0028c0f0): context in ECX, alignment and size, callee pops.
        actor = title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x0028c0f0)(memory, 0x10, 0x1400);
    }
    if (!actor) actor = title_function<uint8_t* XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x10, 0x1400);  // Heap_AllocAligned
    if (!actor) return nullptr;
    for (uint32_t i = 0; i < 0x500; ++i) field<uint32_t>(actor, i * 4) = 0;
    field<uint16_t>(actor, 0x1382) = 0xffff;
    for (uint16_t slot = 0; slot < 0x20; ++slot) {
        if (title_global<uint8_t*>(0x00985384 + slot * 4)) continue;  // g_actors
        title_global<uint8_t*>(0x00985384 + slot * 4) = actor;
        field<uint16_t>(actor, 0x1382) = slot;
        break;
    }
    field<int32_t>(actor, 0x1360) = level_time();
    Actor_InitDefaults(actor);
    return actor;
}

uint32_t XB_REGS1 XB_STDCALL Path_CrossesBadPlace(const uint8_t* path, uint32_t bad_place) {
    const uint32_t blocks = title_global<uint32_t>(0x0095f4f4);  // g_pathNodeBlocks
    for (int32_t i = field<int16_t>(path, 0x380) - 2; i >= 0; --i) {
        const uint16_t* from = &field<uint16_t>(const_cast<uint8_t*>(path), 0x18 + i * 0x1c);
        const uint16_t* to = &field<uint16_t>(const_cast<uint8_t*>(path), 0x34 + i * 0x1c);
        if (!node_handle_set(*from) || !node_handle_set(*to)) continue;
        // PathNode_IsLinkInBadPlace (0x002b96c0): EAX = to, EDX = blocks, ECX = from; from and
        // the bad place on the stack (callee pops).
        const uint32_t in = title_function<uint32_t XB_REGS3 XB_STDCALL(const uint16_t*, uint32_t, const uint16_t*,
                                                                         const uint16_t*, uint32_t)>(0x002b96c0)(
            to, blocks, from, from, bad_place);
        if (static_cast<uint8_t>(in)) return 1;
    }
    return 0;
}

void XB_CDECL G_ExpireBadPlaces() {
    for (uint32_t i = 0; i < 0x20; ++i) {
        uint8_t* place = &title_global<uint8_t>(0x0098a570 + i * 0x28);
        if (!place[8] || level_time() < field<int32_t>(place, 0)) continue;
        if (place[8] == 1)
            title_function<void XB_STDCALL(uint32_t, uint8_t*, uint32_t, int32_t)>(0x002d0db0)(  // AdjustBadPlaceArc
                title_global<uint32_t>(0x0095f4f4), place + 0xc, place[9], -1);
        place[8] = 0;
        title_function<void XB_THISCALL(uint8_t*)>(0x0011d660)(place + 4);  // ScriptString_Clear
    }
}

void XB_CDECL Actor_ClearAll() {
    for (uint32_t address = 0x00985384; address <= 0x00985400; address += 4) title_global<uint32_t>(address) = 0;
    title_global<uint32_t>(0x0095ee0c) = title_global<uint32_t>(0x00987a94);
    title_global<int32_t>(0x0095f44c) = -1;
}

uint8_t* XB_REGS1 XB_CDECL Actor_InitDefaults(uint8_t* actor) {
    static const uint32_t kFloats[][2] = {{0x828, 0x0083eb94}, {0x82c, 0x0083ecec}, {0x824, 0x0083eb24},
                                          {0x7a4, 0x0083ebe8}, {0x7b8, 0x00830bf0}, {0x7bc, 0x0083ec84},
                                          {0x130c, 0x0083eca4}};
    for (const auto& entry : kFloats) field<uint32_t>(actor, entry[0]) = title_global<uint32_t>(entry[1]);
    field<uint32_t>(actor, 8) = 5;
    field<uint32_t>(actor, 0xa4) = 5;
    field<uint32_t>(actor, 0x830) = 2000;
    field<uint32_t>(actor, 0x268) = 2;
    field<uint32_t>(actor, 0x12c) = 1;
    field<uint32_t>(actor, 0x140) = 0;
    field<uint32_t>(actor, 0x7ec) = 20000;
    field<uint32_t>(actor, 0x1308) = 5000;
    field<int32_t>(actor, 0x7b0) = -5;
    field<int32_t>(actor, 0x7b4) = -1;
    field<uint32_t>(actor, 0x15c) = 0;
    field<uint32_t>(actor, 0x158) = 0;  // 0.0f
    return actor;  // EAX stays the first argument
}

void XB_REGS2 XB_CDECL Actor_InitPhysics(uint32_t, uint8_t* actor) {
    for (uint32_t i = 0; i < 0x4c; ++i) field<uint32_t>(actor, 0x290 + i * 4) = 0;
    const uint8_t* ent = field<const uint8_t*>(actor, 0);
    field<uint32_t>(actor, 0x2d4) = field<uint32_t>(ent, 0x224);
    field<uint32_t>(actor, 0x310) = 0x2820011;
    field<uint32_t>(actor, 0x2d8) = 0;
    field<uint32_t>(actor, 0x2e0) = 1;
    field<uint32_t>(actor, 0x2d0) = 1;
    field<uint32_t>(actor, 0x39c) = 0;
    const uint8_t* level = title_global<const uint8_t*>(0x0095f534);
    field<uint32_t>(actor, 0x2b0) = field<uint32_t>(field<const uint8_t*>(level, 8), 0x224);
    for (uint32_t i = 0; i < 12; i += 4) field<uint32_t>(actor, 0x2f0 + i) = field<uint32_t>(ent, 0xe0 + i);
    for (uint32_t i = 0; i < 12; i += 4) field<uint32_t>(actor, 0x300 + i) = field<uint32_t>(ent, 0xf0 + i);
}

float* XB_REGS3 XB_CDECL Actor_SumMoveHistory(float* out, uint32_t, const uint8_t* actor) {
    uint32_t* words = reinterpret_cast<uint32_t*>(out);
    words[1] = 0;
    words[0] = 0;
    words[2] = 0;
    words[0] = field<uint32_t>(actor, 0x1398);
    words[1] = field<uint32_t>(actor, 0x139c);
    for (uint32_t step = 0x13a0; step < 0x13e8; step += 8) {
        out[0] = field<float>(actor, step) + out[0];
        out[1] = field<float>(actor, step + 4) + out[1];
    }
    return out;  // EAX stays the first argument
}

uint8_t* XB_REGS1 XB_CDECL Path_GetNegotiationEndNode(const uint8_t* path) {
    const int16_t current = field<int16_t>(path, 0x386);
    const uint8_t* node = path_node(field<uint16_t>(path, current * 0x1c + 0x18));
    const char* name = field<const char*>(node, 0x38);  // the original reads it even from a null node
    if (!name) return nullptr;
    const int32_t count = field<int16_t>(node, 0x62);
    const uint8_t* link = field<const uint8_t*>(node, 0x64);
    for (int32_t i = 0; i < count; ++i, link += 0xc) {
        uint8_t* other = path_node(field<uint16_t>(link, 0));
        if (!other || field<int32_t>(other, 0x28) != 0x11) continue;
        const char* other_name = field<const char*>(other, 0x30);
        if (!other_name) continue;
        if (std::strcmp(name + 6, other_name + 6) == 0) return other;
    }
    return nullptr;
}

namespace {

// The title CRT's __CIsqrt of an x87 value, stored as float.
float title_ci_sqrt_ext(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

float distance_2d(float dx, float dy) {
    return title_ci_sqrt_ext(static_cast<long double>(dy) * dy + static_cast<long double>(dx) * dx);
}

float abs_bits(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    bits &= 0x7fffffffu;
    std::memcpy(&x, &bits, 4);
    return x;
}

}  // namespace

PLATFORM_ASM_ENTRY(Path_AccumulateTrim_entry,
                   "pushl %ebx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Path_AccumulateTrim) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Path_AccumulateTrim(uint8_t* path, const float* point) {
    int32_t i = field<int16_t>(path, 0x380) - 1;
    if (i <= field<int16_t>(path, 0x386)) return;
    // Walk back from the last point to the one nearest to `point` (distance in xy), summing the
    // weighted |cross| of each segment's direction with the offset (stop past 32768).
    const uint8_t* before = path + (i - 1) * 0x1c;
    float best = distance_2d(point[0] - field<float>(before, 0), point[1] - field<float>(before, 4));
    float sum = 0.0f;
    float sum_at_best = 0.0f;
    const uint8_t* record = path + i * 0x1c;
    for (;;) {
        const float dx = field<float>(record, 0) - point[0];
        const float dy = field<float>(record, 4) - point[1];
        const float d = distance_2d(dx, dy);
        if (best >= d) {
            best = d;
            sum_at_best = sum;
        }
        ++i;
        if (i >= field<int16_t>(path, 0x382)) break;
        const float cross = field<float>(record, 0xc) * dy - field<float>(record, 0x10) * dx;
        sum = field<float>(record, 0x14) * abs_bits(cross) + sum;
        record += 0x1c;
        if (!(title_global<float>(0x0083ebc8) >= sum)) break;
    }
    const uint8_t* last = path + (field<int16_t>(path, 0x380) - 2) * 0x1c;
    const float cross = field<float>(last, 0xc) * (field<float>(last, 4) - point[1]) -
                        field<float>(last, 0x10) * (field<float>(last, 0) - point[0]);
    const float trim = (field<float>(last, 0x14) - field<float>(path, 0x3c8)) * abs_bits(cross) + sum_at_best;
    field<float>(path, 0x3b4) = field<float>(path, 0x3b4) + trim;
}

PLATFORM_ASM_ENTRY(Path_AdvanceAlong_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Path_AdvanceAlong) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL Path_AdvanceAlong(uint8_t* path, const float* point) {
    int32_t k = field<int16_t>(path, 0x380) - 2;
    const int32_t start = field<int16_t>(path, 0x386);
    if (k < start) return path;
    const float dir_x = field<float>(path, 0x39c);
    const float dir_y = field<float>(path, 0x3a0);
    // From the last segment back: the first point ahead of `point` along the direction.
    bool first = true;
    const uint8_t* record = path + k * 0x1c;
    float dot;
    for (;;) {
        dot = (field<float>(record, 4) - point[1]) * dir_y + (field<float>(record, 0) - point[0]) * dir_x;
        if (!(0.0f >= dot)) break;
        --k;
        record -= 0x1c;
        first = false;
        if (k < field<int16_t>(path, 0x386)) return path;
    }
    const uint8_t* next = record + 0x1c;
    const float next_x = first ? field<float>(path, 0x3cc) : field<float>(next, 0);
    const float next_y = first ? field<float>(path, 0x3d0) : field<float>(next, 4);
    const float length = first ? field<float>(path, 0x3c8) : field<float>(record, 0x14);
    const float dot_next = (next_y - point[1]) * dir_y + (next_x - point[0]) * dir_x;
    const float t = dot_next / (dot_next - dot);
    const float step = t * length;
    if (step >= length) {
        if (k <= start) return path;
        std::memcpy(&field<float>(path, 0x3cc), record, 12);
        --k;
        field<float>(path, 0x3c8) = field<float>(path + k * 0x1c, 0x14);
    } else {
        const float base_x = first ? field<float>(path, 0x3cc) : field<float>(next, 0);
        field<float>(path, 0x3cc) = field<float>(record, 0xc) * step + base_x;
        const float base_y = first ? field<float>(path, 0x3d0) : field<float>(next, 4);
        field<float>(path, 0x3d0) = field<float>(record, 0x10) * step + base_y;
        const float base_z = first ? field<float>(path, 0x3d4) : field<float>(next, 8);
        const float remaining = first ? field<float>(path, 0x3c8) : field<float>(record, 0x14);
        field<float>(path, 0x3d4) = (field<float>(record, 8) - base_z) * t + base_z;
        field<float>(path, 0x3c8) = remaining - step;
    }
    const int16_t lookahead = field<int16_t>(path, 0x384);
    if (lookahead >= 0) {
        field<int16_t>(path, 0x384) = static_cast<int16_t>(lookahead - field<int16_t>(path, 0x380) + (k + 2));
        field<int16_t>(path, 0x380) = static_cast<int16_t>(k + 2);
        if (field<int16_t>(path, 0x384) < 0) field<int16_t>(path, 0x384) = 0;
    } else {
        field<int16_t>(path, 0x380) = static_cast<int16_t>(k + 2);
    }
    return path;  // EAX stays the path
}

PLATFORM_ASM_ENTRY(Actor_GetBoundaryPlanes_entry,
                   "pushl %esi\n"
                   "pushl %edx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Actor_GetBoundaryPlanes) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_GetBoundaryPlanes(const uint8_t* actor, float* normals, float* distances) {
    uint32_t count = 0;
    for (uint32_t plane = 0x12a8; plane <= 0x12f0; plane += 0x18) {
        if (!field<uint32_t>(actor, plane)) continue;
        std::memcpy(&normals[count * 2], &field<float>(actor, plane + 8), 8);
        distances[count] = field<float>(actor, plane + 0x10);
        ++count;
    }
    return count;
}

uint8_t* XB_REGS2 XB_CDECL Actor_PopBlockingStates(uint8_t* actor, uint32_t state) {
    // The states that `state` blocks: a row of five in the table 0x00830ea8 (rows from state 8 on).
    const uint32_t* blocked = &title_global<uint32_t>(0x00830ea8 + (state * 5 - 40) * 4);
    for (int32_t i = 1; i <= field<int32_t>(actor, 0xbc); ++i) {
        const uint32_t pushed = field<uint32_t>(actor, 0xa8 + (i - 1) * 4);
        for (int32_t j = 0; j < 5; ++j) {
            if (pushed != blocked[j]) continue;
            // Pop everything from there on: one queued pop (kind 3) per level.
            do {
                queue_transition(actor, 3, 0);
            } while (i <= --field<int32_t>(actor, 0xbc));
            return actor;
        }
    }
    return actor;  // EAX stays the actor
}

// The state callbacks run where the original's calls put them (four pushes) with its registers;
// the C frame sits below a gap.
PLATFORM_ASM_ENTRY(Actor_ApplyStateTransitions_entry,
                   "movl %esp, %ecx\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl %edi\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_ApplyStateTransitions) "addl $12, %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
                   "ret")

void XB_CDECL Actor_ApplyStateTransitions(uint32_t entry, uint8_t* actor, uint32_t edi) {
    const uint32_t top = entry - 0x10;  // push ebp, esi, ebx, edi
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor));
    // Slot `slot` of a state's nine callbacks (0 Start, 1 Finish, 2 Suspend, 3 Resume).
    const auto callback = [](uint32_t state, uint32_t slot) { return title_global<uint32_t>(0x00830c28 + (state * 9 + slot) * 4); };
    const auto state_at = [actor](int32_t depth) { return field<uint32_t>(actor, 8 + depth * 4); };
    for (int32_t i = 0; i < field<int32_t>(actor, 0xa0); ++i) {
        const uint32_t queued = self + 0x3c + 8 * i;  // EBX: the queued state
        const uint32_t kind = field<uint32_t>(actor, 0x38 + 8 * i);
        int32_t& depth = field<int32_t>(actor, 0x20);
        uint32_t next;
        uint32_t eax;
        uint32_t ecx = self;
        if (kind == 1 || kind == 2) {
            // Set: finish the top state; push: suspend it. Then start the queued state.
            const uint32_t current = state_at(depth);
            const TitleRegs regs = {current * 9, queued, self, field<uint32_t>(actor, 0x3c + 8 * i), self, edi};
            eax = title_call_at_regs(top, callback(current, kind), &regs, 0, nullptr);
            if (kind == 2) ++depth;
            next = field<uint32_t>(actor, 0x3c + 8 * i);
            ecx = next;
        } else if (kind == 3) {
            // Pop: finish the top state, resume the one below; a state Resume returns replaces it.
            const uint32_t current = state_at(depth);
            edi = current * 9;
            const TitleRegs finish = {static_cast<uint32_t>(depth), queued, self, state_at(depth - 1), self, edi};
            title_call_at_regs(top, callback(current, 1), &finish, 0, nullptr);
            --depth;
            const uint32_t below = state_at(depth);
            const TitleRegs resume = {below * 9, queued, self, state_at(depth + 1), self, edi};
            edi = title_call_at_regs(top, callback(below, 3), &resume, 0, nullptr);
            if (!edi) continue;
            const uint32_t now = state_at(depth);
            const TitleRegs replace = {now * 9, queued, self, edi, self, edi};
            eax = title_call_at_regs(top, callback(now, 1), &replace, 0, nullptr);
            next = edi;
        } else {
            continue;
        }
        const TitleRegs start = {eax, queued, ecx, 0, self, edi};
        title_call_at_regs(top, 0x001fc020, &start, 1, &next);  // Actor_StartState (ESI = actor)
    }
    field<uint32_t>(actor, 0xa0) = 0;
    field<int32_t>(actor, 0xbc) = field<int32_t>(actor, 0x20);
    for (int32_t c = 0; c <= field<int32_t>(actor, 0x20); ++c) field<uint32_t>(actor, 0xa4 + c * 4) = state_at(c);
}

PLATFORM_ASM_ENTRY(Actor_RotateOrient_entry,
                   "subl $8, %esp\n"
                   "movss %xmm5, 4(%esp)\n"
                   "movss %xmm4, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_RotateOrient) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Actor_RotateOrient(uint8_t* actor, float yaw, float pitch) {
    const float to_short = title_global<float>(0x0083ebd8), to_angle = title_global<float>(0x0083ebd4);
    // An angle turned by `delta`, through a 16-bit angle.
    const auto turn = [to_short, to_angle](float angle, float delta) {
        return static_cast<float>(static_cast<int32_t>((angle + delta) * to_short) & 0xffff) * to_angle;
    };
    static const uint32_t kPairs[] = {0x130, 0x144};
    for (const uint32_t base : kPairs) {
        const float new_yaw = turn(field<float>(actor, base + 8), yaw);
        const float new_pitch = turn(field<float>(actor, base), pitch);
        field<float>(actor, base + 4) = new_yaw;
        field<float>(actor, base + 8) = new_yaw;
        field<float>(actor, base) = new_pitch;
    }
    uint8_t* ent = field<uint8_t*>(actor, 0);
    field<float>(ent, 0x150) = 0.0f;
    field<float>(ent, 0x154) = turn(field<float>(ent, 0x154), yaw);
    field<float>(ent, 0x158) = 0.0f;
    Actor_SetDesiredAngles(actor, field<float>(actor, 0x100) + pitch, field<float>(actor, 0x104) + yaw);
}

PLATFORM_ASM_ENTRY(Actor_AdvanceMoveHistory_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Actor_AdvanceMoveHistory) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_AdvanceMoveHistory(uint8_t* actor) {
    // The ten 2D moves (+0x1398) from the oldest (after the head +0x1394) to the newest are scaled
    // by 1/2, 2/3, 3/4, ...; the head moves on.
    const int32_t head = field<int32_t>(actor, 0x1394);
    const float one = title_global<float>(0x0083eb20);
    float n = one;
    const auto scale = [actor, one, &n](int32_t k) {
        const float previous = n;
        n = n + one;
        const float f = previous / n;
        field<float>(actor, 0x1398 + k * 8) = field<float>(actor, 0x1398 + k * 8) * f;
        field<float>(actor, 0x139c + k * 8) = field<float>(actor, 0x139c + k * 8) * f;
    };
    for (int32_t k = head + 1; k < 10; ++k) scale(k);
    for (int32_t k = 0; k < head; ++k) scale(k);
    field<int32_t>(actor, 0x1394) = (head + 1) % 10;
}

PLATFORM_ASM_ENTRY(Path_IsPointNearPath_entry,
                   "subl $4, %esp\n"
                   "movss %xmm4, (%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Path_IsPointNearPath) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL Path_IsPointNearPath(const uint8_t* path, const float* origin, const float* point, float radius_sq) {
    // Near a segment (start, unit direction, length) when the perpendicular distance is below the
    // radius and the point lies along it, or near one of its ends.
    const auto near_segment = [radius_sq](float dx, float dy, float dir_x, float dir_y, float length) {
        const float cross = dir_x * dy - dir_y * dx;
        if (!(radius_sq > cross * cross)) return false;
        const float along = dir_y * dy + dir_x * dx;
        if (0.0f > along) return radius_sq > dy * dy + dx * dx;
        if (length >= along) return true;
        const float ex = dir_x * length + dx;
        const float ey = dir_y * length + dy;
        return radius_sq > ey * ey + ex * ex;
    };
    const float dir_x = field<float>(path, 0x39c), dir_y = field<float>(path, 0x3a0);
    const float lookahead = field<float>(path, 0x3b0);
    // The lookahead segment, measured back from its end.
    {
        const float ax = dir_x * lookahead + origin[0];
        const float ay = dir_y * lookahead + origin[1];
        const float dx = point[0] - ax, dy = point[1] - ay;
        const float cross = dir_x * dy - dir_y * dx;
        if (radius_sq > cross * cross) {
            const float along = dir_y * dy + dir_x * dx;
            if (0.0f > along) {
                if (radius_sq > dy * dy + dx * dx) return 1;
            } else {
                if (lookahead >= along) return 1;
                const float ox = origin[0] - point[0], oy = origin[1] - point[1];
                if (radius_sq > oy * oy + ox * ox) return 1;
            }
        }
    }
    const int32_t current = field<int16_t>(path, 0x388);
    const float remaining = field<float>(path, 0x3b8);
    if (remaining != 0.0f) {
        // The current segment, its remaining length from its start point.
        const uint8_t* r = path + current * 0x1c;
        const float dx = point[0] - field<float>(r, 0), dy = point[1] - field<float>(r, 4);
        if (near_segment(dx, dy, field<float>(r, 0xc), field<float>(r, 0x10), remaining)) return 1;
    }
    for (int32_t k = current - 1; k >= 0; --k) {
        const uint8_t* r = path + k * 0x1c;
        const float dx = point[0] - field<float>(r, 0), dy = point[1] - field<float>(r, 4);
        const float r_x = field<float>(r, 0xc), r_y = field<float>(r, 0x10);
        const float cross = r_x * dy - dx * r_y;
        if (cross * cross >= radius_sq) continue;
        const float along = r_x * dx + dy * r_y;
        if (0.0f > along) {
            if (radius_sq > dy * dy + dx * dx) return 1;
            continue;
        }
        const float length = field<float>(r, 0x14);
        if (length >= along) return 1;
        const float ex = r_x * length + dx;
        const float ey = length * r_y + dy;
        if (radius_sq > ey * ey + ex * ex) return 1;
    }
    return 0;
}

void XB_REGS3 XB_CDECL Actor_Noncombat_Finish(uint32_t, uint32_t, uint8_t* actor) {
    Sentient_SetClaimedNode(field<uint8_t*>(actor, 4), 0);
}

void XB_REGS3 XB_CDECL Actor_Cover_Finish(uint32_t, uint32_t next, uint8_t* actor) {
    field<uint8_t>(actor, 0x1364) = 0;
    field<int32_t>(actor, 0x1368) = level_time();
    if (next != 9) Sentient_SetClaimedNode(field<uint8_t*>(actor, 4), 0);
}

uint32_t XB_REGS3 XB_CDECL Actor_Cover_NodeClaimRevoked(uint32_t, uint32_t, uint8_t* actor) {
    Sentient_SetClaimedNode(field<uint8_t*>(actor, 4), 0);
    uint32_t& top = field<uint32_t>(actor, 0xa4 + field<int32_t>(actor, 0xbc) * 4);
    if (top == 1) {
        queue_transition(actor, 1, 1);  // set state 1 (and merge)
        field<uint32_t>(actor, 0xa4 + field<int32_t>(actor, 0xbc) * 4) = 1;
    }
    return 1;
}

uint32_t XB_REGS2 XB_CDECL Actor_IsAtClaimedNode(uint32_t, const uint8_t* actor) {
    if (field<uint32_t>(actor, 0x140)) return 1;
    const uint8_t* node = path_node(field<uint16_t>(field<const uint8_t*>(actor, 4), 0x48));
    if (!node) return 0;
    const uint8_t* ent = field<const uint8_t*>(actor, 0);
    const float dz = field<float>(node, 0x4c) - field<float>(ent, 0x148);
    if (!(title_global<float>(0x0083eb6c) > dz * dz)) return 0;
    const float dy = field<float>(ent, 0x144) - field<float>(node, 0x48);
    const float dx = field<float>(ent, 0x140) - field<float>(node, 0x44);
    return title_global<float>(0x0083100c) > dy * dy + dx * dx ? 1 : 0;
}

PLATFORM_ASM_ENTRY(Actor_GrenadeTargetClear_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Actor_GrenadeTargetClear) "addl $12, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Actor_GrenadeTargetClear(uint32_t weapon, const float* target, const uint8_t* actor) {
    const uint8_t* def = title_global<uint8_t* const*>(0x0095f4d8)[weapon];  // bg_weaponDefs
    const float limit = static_cast<float>(field<int32_t>(def, 0x724)) * title_global<float>(0x00830f60);
    // Every sentient not on the team the actor's team maps to (0 -> 0, 1 -> 2, 2 -> 1, 3/4 -> 0).
    static const int32_t kTeamBit[] = {0, 2, 1, 0, 0};
    const uint32_t team = field<uint32_t>(field<const uint8_t*>(actor, 4), 4);
    const uint32_t mask = ~(1u << kTeamBit[team]);
    for (uint8_t* sentient = Sentient_FirstOfTeams(mask); sentient; sentient = Sentient_NextOfTeams(sentient, mask)) {
        const uint8_t* ent = field<const uint8_t*>(sentient, 0);
        const float dx = target[0] - field<float>(ent, 0x140);
        const float dy = target[1] - field<float>(ent, 0x144);
        const float dz = target[2] - field<float>(ent, 0x148);
        if (limit >= (dz * dz + dy * dy) + dx * dx) return 0;
    }
    return 1;
}

int32_t XB_CDECL Actor_MainCharacterPriority(const uint8_t* ent) {
    if (!ent) return 0;
    int32_t priority = 0;
    const uint8_t* actor = field<const uint8_t*>(ent, 0x240);
    if (actor) {
        const char* script_name = field<const char*>(actor, 0xec);
        if (script_name && script_name[6]) {
            // The name after the first ' ' (0x0082d6e4) against the main characters' names.
            const char* name = script_name + 6;
            using StrStr = const char* XB_CDECL(const char*, const char*);
            using StrICmp = int32_t XB_CDECL(const char*, const char*);
            if (const char* sep = title_function<StrStr>(0x001efb10)(name, &title_global<const char>(0x0082d6e4))) name = sep + 1;
            for (const char* const* main = &title_global<const char* const>(0x008db468); *main; ++main) {
                if (title_function<StrICmp>(0x001f1443)(*main, name) == 0) {
                    priority = 5000;
                    break;
                }
            }
        }
        if (field<uint8_t>(field<const uint8_t*>(ent, 0x240), 0x7df)) priority += 1000;
    }
    if (!field<uint32_t>(ent, 0x29c)) priority += 1000;
    const uint8_t* sentient = field<const uint8_t*>(ent, 0x244);
    if (sentient) {
        if (field<int32_t>(sentient, 4) == 2) priority += 600;
        if (field<uint8_t>(sentient, 0x5b)) priority -= 1000;
        priority -= static_cast<int32_t>(field<float>(sentient, 0x18));
    }
    return priority;
}

PLATFORM_ASM_ENTRY(Path_StepBack_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Path_StepBack) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Path_StepBack(uint8_t* path, const float* point) {
    // Written close to the original's control flow: acc (the weighted |cross| sum, xmm0), the
    // current segment's |cross| (xmm7), `start` = segment start record (ECX), `end` (EDX).
    const auto rec = [path](int32_t i) { return path + i * 0x1c; };
    const auto f = [](const uint8_t* r, uint32_t offset) { return field<float>(r, offset); };
    const float dir_x = field<float>(path, 0x39c), dir_y = field<float>(path, 0x3a0);
    const float limit_dist = title_global<float>(0x0083eb40);
    int32_t i = field<int16_t>(path, 0x380) - 1;
    const float limit = field<float>(path, 0x3b4) * title_global<float>(0x0083ece0);
    const uint8_t* end = rec(i);
    const uint8_t* start = end - 0x1c;
    float dy = f(start, 4) - point[1];
    float dx = f(start, 0) - point[0];
    float abs_cross = abs_bits(f(start, 0xc) * dy - f(start, 0x10) * dx);
    float acc = 0.0f - field<float>(path, 0x3c8) * abs_cross;
    dy = f(end, 4) - point[1];
    dx = f(end, 0) - point[0];
    float t, step, x4;
    if (dy * dy + dx * dx > limit_dist) goto too_far;
    for (;;) {
        {
            const float along = dy * dir_y + dx * dir_x;
            if (!(along > 0.0f)) {
                // The point is behind the end of this segment: where along changes sign on it.
                const float along_start = (f(start, 4) - point[1]) * dir_y + (f(start, 0) - point[0]) * dir_x;
                if (!(along_start > 0.0f)) goto set_end;
                t = along / (along - along_start);
                goto interpolate;
            }
            x4 = f(start, 0x14) * abs_cross;
            acc = x4 + acc;
            if (acc >= limit) {
                t = (acc - limit) / x4;
                acc = acc - x4;
                goto interpolate;
            }
        }
        ++i;
        if (i >= field<int16_t>(path, 0x382)) {
            // Ran off the path's end: keep its last point.
            field<float>(path, 0x3c8) = f(start, 0x14);
            std::memcpy(&field<float>(path, 0x3cc), end, 12);
            field<int16_t>(path, 0x380) = field<int16_t>(path, 0x382);
            goto accumulate;
        }
        {
            const uint8_t* next = rec(i);
            const float cross = f(end, 0xc) * dy - f(end, 0x10) * dx;
            dx = f(next, 0) - point[0];
            dy = f(next, 4) - point[1];
            start = end;
            abs_cross = abs_bits(cross);
            end = next;
            if (dy * dy + dx * dx > limit_dist) goto too_far;
        }
    }
too_far:
    field<uint32_t>(path, 0x3bc) |= 0x20;
set_end:
    std::memcpy(&field<float>(path, 0x3cc), start, 12);
    field<float>(path, 0x3c8) = i <= field<int16_t>(path, 0x386) + 1 ? 0.0f : f(rec(i - 2), 0x14);
    acc = f(start, 0x14) * abs_cross + acc;
    field<int16_t>(path, 0x380) = static_cast<int16_t>(i);
    goto accumulate;
interpolate:
    step = f(start, 0x14) * t;
    if (step >= f(start, 0x14)) {
        --i;
        if (i <= field<int16_t>(path, 0x386)) return;
        std::memcpy(&field<float>(path, 0x3cc), start, 12);
        field<float>(path, 0x3c8) = f(rec(i - 1), 0x14);
    } else {
        const float remaining = f(start, 0x14) - step;
        field<float>(path, 0x3c8) = remaining;
        acc = remaining * abs_cross + acc;
        field<float>(path, 0x3cc) = f(start, 0xc) * step + f(end, 0);
        field<float>(path, 0x3d0) = f(start, 0x10) * step + f(end, 4);
        field<float>(path, 0x3d4) = (f(start, 8) - f(end, 8)) * t + f(end, 8);
    }
    ++i;
    field<int16_t>(path, 0x380) = static_cast<int16_t>(i);
accumulate:
    if (0.0f >= acc) return;
    if (!(field<uint8_t>(path, 0x3bc) & 2)) acc = acc * title_global<float>(0x0083eb58);
    field<float>(path, 0x3b4) = field<float>(path, 0x3b4) + acc;
    if (field<float>(path, 0x3b4) > limit_dist) field<float>(path, 0x3b4) = limit_dist;
}

namespace {

// path +0x3a8 = (x, y) / its length (__CIsqrt, stored as float), unless the length is 0.
void set_look_direction(uint8_t* path, float x, float y) {
    field<float>(path, 0x3a8) = x;
    field<float>(path, 0x3ac) = y;
    const float length = title_ci_sqrt_ext(static_cast<float>(x * x + y * y));
    if (length == title_global<float>(0x0083eafc)) return;
    const float inverse = title_global<float>(0x0083eb20) / length;
    field<float>(path, 0x3a8) = field<float>(path, 0x3a8) * inverse;
    field<float>(path, 0x3ac) = inverse * field<float>(path, 0x3ac);
}

}  // namespace

PLATFORM_ASM_ENTRY(Path_RefreshForwardLookahead_entry,
                   "pushl %ecx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Path_RefreshForwardLookahead) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Path_RefreshForwardLookahead(uint8_t* path, const float* origin) {
    const auto rec = [path](int32_t i) { return path + i * 0x1c; };
    const auto f = [](const uint8_t* r, uint32_t offset) { return field<float>(r, offset); };
    const float dir_x = field<float>(path, 0x39c), dir_y = field<float>(path, 0x3a0);
    const auto keep_direction = [path, dir_x, dir_y]() {
        field<float>(path, 0x3a8) = dir_x;
        field<float>(path, 0x3ac) = dir_y;
    };
    const int16_t count = field<int16_t>(path, 0x380);
    if (count <= 1 || (field<uint8_t>(path, 0x3bc) & 1)) return keep_direction();
    // A: up to 60 units along the lookahead from the origin.
    const float lookahead = field<float>(path, 0x3b0), cap = title_global<float>(0x0083eb3c);
    float ax, ay;
    if (lookahead > cap) {
        ax = dir_x * cap + origin[0];
        ay = dir_y * cap + origin[1];
    } else {
        ax = lookahead * dir_x + origin[0];
        ay = lookahead * dir_y + origin[1];
    }
    float ex = field<float>(path, 0x3cc) - ax, ey = field<float>(path, 0x3d0) - ay;
    float along = dir_x * ex + ey * dir_y;
    if (along > 0.0f) return keep_direction();
    const int32_t start = field<int16_t>(path, 0x386);
    int32_t k = count - 2;
    bool first = true;
    for (;;) {
        if (k < field<int16_t>(path, 0x386)) return keep_direction();
        const uint8_t* r = rec(k);
        along = dir_x * (f(r, 0) - ax) + (f(r, 4) - ay) * dir_y;
        if (!(0.0f >= along)) break;
        --k;
        first = false;
    }
    const uint8_t* r = rec(k);
    float length;
    if (first) {
        length = field<float>(path, 0x3c8);
    } else {
        ex = f(rec(k + 1), 0) - ax;
        ey = f(rec(k + 1), 4) - ay;
        length = f(r, 0x14);
    }
    const float along_next = dir_x * ex + ey * dir_y;
    const float budget = field<float>(path, 0x3b4);
    const float t = along_next / (along_next - along);
    float abs_cross = abs_bits(f(r, 0xc) * (f(r, 4) - ay) - f(r, 0x10) * (f(r, 0) - ax));
    float weight = (length - t * length) * abs_cross;
    if (!(weight >= budget)) {
        // Walk on back until the weighted |cross| sum reaches the budget.
        for (;;) {
            if (k <= start) return set_look_direction(path, f(rec(start), 0) - ax, f(rec(start), 4) - ay);
            const uint8_t* prev = rec(k - 1);
            abs_cross = abs_bits((f(prev, 4) - ay) * f(prev, 0xc) - f(prev, 0x10) * (f(prev, 0) - ax));
            weight = f(prev, 0x14) * abs_cross + weight;
            --k;
            if (!(budget > weight)) break;
        }
        r = rec(k);
    }
    const float s = (weight - budget) / abs_cross;
    set_look_direction(path, f(r, 0) - f(r, 0xc) * s - ax, f(r, 4) - f(r, 0x10) * s - ay);
}

uint32_t XB_REGS3 XB_CDECL Actor_AskActorsToMoveAside(uint32_t, uint32_t, uint8_t* actor) {
    const float radius_sq = title_global<float>(0x0083ee3c);
    uint32_t clear = 1;
    for (uint32_t slot = 0x00985384; slot < 0x00985404; slot += 4) {
        uint8_t* other = title_global<uint8_t*>(slot);
        if (!other || field<int32_t>(other, 0x1360) < 0 || other == actor) continue;
        const uint8_t* other_ent = field<const uint8_t*>(other, 0);
        if (other_ent == field<const uint8_t*>(actor, 0x1390)) continue;
        // Standing at our goal (within 30 units in xy) but not right next to us.
        float dy = field<float>(other_ent, 0x144) - field<float>(actor, 0x294);
        float dx = field<float>(other_ent, 0x140) - field<float>(actor, 0x290);
        if (dy * dy + dx * dx >= radius_sq) continue;
        uint8_t* ent = field<uint8_t*>(actor, 0);
        dy = field<float>(other_ent, 0x144) - field<float>(ent, 0x144);
        dx = field<float>(other_ent, 0x140) - field<float>(ent, 0x140);
        if (radius_sq > dy * dy + dx * dx) continue;
        if (!field<uint8_t*>(other, 0x1390)) field<uint8_t*>(other, 0x1390) = ent;
        clear = 0;
    }
    return clear;
}

namespace {

// The title's inline asin series for s in [0, 1): s + s^3/6 + 0.075 s^5 + 0.054 s^7 below 0.5, else
// pi/2 minus the series on sqrt(|(1 - s) / 2|) (__CIsqrt); `complement` returns pi/2 minus it.
float cover_asin(float s, bool complement) {
    const auto g = [](uint32_t address) { return title_global<float>(address); };
    const float half_pi = g(0x0083ed0c);
    if (g(0x0083eb24) > s) {
        const float s2 = s * s;
        const float s3 = s2 * s;
        const float s5 = s3 * s2;
        const float s7 = s5 * s2;
        const float r = (s3 * g(0x0083ee9c) + (s5 * g(0x0083ee98) + s7 * g(0x0083ee94))) + s;
        return complement ? half_pi - r : r;
    }
    const float u = abs_bits((g(0x0083eb20) - s) * g(0x0083eb24));
    const float v = title_ci_sqrt_ext(u);
    const float v2 = v * v;
    const float v3 = v2 * v;
    const float v5 = v3 * v2;
    const float v7 = v5 * v2;
    const float r = (((v7 * g(0x0083ee8c) - v5 * g(0x0083ed2c)) - v3 * g(0x0083ee90)) - v * g(0x0083eba8)) + g(0x0083ee88);
    return complement ? half_pi - r : r;
}

struct CoverKind {
    uint32_t script;
    uint32_t min_yaw, max_yaw, min_distance_sq;  // addresses of the title's float constants
};

// Node types 2..11: the cover anim script, the allowed yaw range (min > max wraps through 0) and
// the squared distance the threat must be beyond.
const CoverKind kCoverKinds[] = {
    {0x00987b18, 0x0083efac, 0x0083ef1c, 0x0083efa8}, {0x00987ad8, 0x0083efac, 0x0083ef1c, 0x0083efa8},
    {0x00987af8, 0x0083efb4, 0x0083ee38, 0x0083efb0}, {0x00987b08, 0x0083efac, 0x0083ee38, 0x0083efa8},
    {0x00987ae8, 0x0083efb4, 0x0083ef1c, 0x0083efa8}, {0x00987b38, 0x0083efac, 0x0083ee38, 0x0083efa8},
    {0x00987b28, 0x0083efb4, 0x0083ef1c, 0x0083efa8}, {0x00987ac8, 0x0083efac, 0x0083ef1c, 0x0083efa8},
    {0x00987aa8, 0x0083efac, 0x0083ef1c, 0x0083efa8}, {0x00987ab8, 0x0083efb4, 0x0083ee38, 0x0083efa8},
};

}  // namespace

PLATFORM_ASM_ENTRY(Actor_IsCoverValidAgainst_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_IsCoverValidAgainst) "addl $16, %esp\n"
                   "ret $8")

uint32_t XB_CDECL Actor_IsCoverValidAgainst(const uint8_t* node, const uint8_t* actor, const uint8_t* threat, uint32_t* script) {
    uint32_t chosen = 0;
    uint32_t valid = 0;
    if (!node) node = path_node(field<uint16_t>(field<const uint8_t*>(actor, 4), 0x48));
    if (node && field<uint32_t>(node, 0x28) - 2 <= 9) {
        const CoverKind& kind = kCoverKinds[field<uint32_t>(node, 0x28) - 2];
        chosen = kind.script;
        const float min_yaw = title_global<float>(kind.min_yaw), max_yaw = title_global<float>(kind.max_yaw);
        const float* pos = nullptr;
        if (threat) {
            pos = &field<float>(field<const uint8_t*>(threat, 0), 0x140);
        } else if (const uint8_t* enemy = field<const uint8_t*>(actor, 0x1310)) {
            pos = &field<float>(enemy, 0x2c0);
        }
        if (pos) {
            const float dx = pos[0] - field<float>(node, 0x44);
            const float dy = pos[1] - field<float>(node, 0x48);
            const float dz = pos[2] - field<float>(node, 0x4c);
            const float dx2 = dx * dx, dy2 = dy * dy;
            if (!(title_global<float>(kind.min_distance_sq) > (dz * dz + dx2) + dy2)) {
                // The threat's direction from the node: atan2 through the asin series.
                const float ax = abs_bits(dx), ay = abs_bits(dy);
                float angle = 0.0f;
                // The title tests |dx| + |dy| against a double that is not 0 (0x0083ed20): never equal.
                if (static_cast<long double>(ay) + static_cast<long double>(ax) != static_cast<long double>(title_global<double>(0x0083ed20))) {
                    const float inverse = title_global<float>(0x0083eb20) / title_ci_sqrt_ext(dx2 + dy2);
                    angle = ay > ax ? cover_asin(inverse * ax, true) : cover_asin(inverse * ay, false);
                    if (0.0f > dx) angle = title_global<float>(0x0083ed00) - angle;
                    if (0.0f > dy) angle = 0.0f - angle;
                }
                const float degrees = angle * title_global<float>(0x0083efa4) - field<float>(node, 0x50);
                const float yaw = static_cast<float>(static_cast<int32_t>(degrees * title_global<float>(0x0083ebd8)) & 0xffff) *
                                  title_global<float>(0x0083ebd4);
                if (min_yaw > max_yaw)
                    valid = (min_yaw > yaw && yaw > max_yaw) ? 0 : 1;
                else
                    valid = (min_yaw > yaw || yaw > max_yaw) ? 0 : 1;
            }
        }
    }
    if (script) *script = chosen;
    return valid;
}

}  // namespace game
