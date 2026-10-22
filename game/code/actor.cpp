#include "actor.h"

#include "bg_misc.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
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

void XB_REGS3 XB_CDECL Sentient_GetOrigin(float* out, uint32_t, uint8_t* const* sentient) {
    uint32_t* words = reinterpret_cast<uint32_t*>(out);
    words[0] = field<uint32_t>(*sentient, 0x140);
    words[1] = field<uint32_t>(*sentient, 0x144);
    words[2] = field<uint32_t>(*sentient, 0x148);
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

void XB_REGS1 XB_STDCALL Actor_AnimRun(uint8_t* actor, uint32_t script) {
    set_move_script(actor, script, 0x00987b98);  // g_animScript_run
}

void XB_REGS1 XB_STDCALL Actor_AnimWalk(uint8_t* actor, uint32_t script) {
    set_move_script(actor, script, 0x00987be8);  // g_animScript_walk
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

void XB_REGS2 XB_CDECL Path_CopyToActor(const uint8_t* path, uint8_t* actor) {
    copy_path(path, actor + 0x3c0, field<int16_t>(path, 0x382));
}

void XB_REGS2 XB_CDECL Path_CopyFromActor(const uint8_t* actor, uint8_t* path) {
    copy_path(actor + 0x3c0, path, field<int16_t>(actor, 0x742));
}

void XB_REGS3 XB_CDECL Actor_CopySentientInfo(uint8_t* to, uint32_t, const uint8_t* from) {
    field<uint32_t>(to, 0x14) = 0;
    field<uint32_t>(to, 0x1c) = 0;
    for (uint32_t i = 0; i < 5; ++i)
        field<uint32_t>(to, kSentientInfoWords[i]) = field<uint32_t>(from, kSentientInfoWords[i]);
    field<uint16_t>(to, 0x30) = field<uint16_t>(from, 0x30);
    for (uint32_t offset = 0x40; offset <= 0x48; offset += 4) field<uint32_t>(to, offset) = field<uint32_t>(from, offset);
}

void XB_REGS1 XB_CDECL Actor_AnimReacquire(uint8_t* actor) {
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

void XB_REGS1 XB_CDECL Path_ShrinkLookaheadBy(uint8_t* path) {
    const uint32_t flags = field<uint32_t>(path, 0x3bc);
    float& distance = field<float>(path, 0x3b4);
    if (flags & 2)
        distance = (distance - title_global<float>(0x0083eb44)) * title_global<float>(0x0083eb58);  // 6.4, 0.75
    else
        distance = (distance - title_global<float>(0x0083eb54)) * title_global<float>(0x0083eb50);  // 12.8, 0.5625
    const float minimum = title_global<float>(0x0083eb4c);  // 0.001
    if (minimum > distance) distance = minimum;
    field<uint32_t>(path, 0x3bc) = flags & ~3u;
}

void XB_REGS1 XB_CDECL Actor_ClearLookAt(uint8_t* actor) {
    field<uint32_t>(actor, 0x1e8) = 0;
    for (uint32_t offset = 0x1ec; offset <= 0x1fc; offset += 4) field<uint32_t>(actor, offset) = 0;  // 0.0f
    field<float>(actor, 0x200) = title_global<float>(0x0083eb68);                                    // 120
    field<uint32_t>(actor, 0x204) = 0;
    field<float>(actor, 0x208) = title_global<float>(0x0083eb64);  // 90
    field<float>(actor, 0x20c) = title_global<float>(0x0083eb64);
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

void XB_REGS2 XB_CDECL Actor_ClearSentientSlots(uint8_t* actor, uint32_t sentient) {
    for (uint32_t slot = 0x12a8; slot < 0x1308; slot += 0x18) {
        if (field<uint32_t>(actor, slot + 4) != sentient) continue;
        field<uint32_t>(actor, slot) = 0;
        field<uint32_t>(actor, slot + 4) = 0;
        field<uint32_t>(actor, slot + 0x14) = 0;
    }
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

void XB_REGS1 XB_CDECL Actor_ClearMoveHistory(uint8_t* actor) {
    for (uint32_t offset = 0x1398; offset <= 0x13e4; offset += 4) field<uint32_t>(actor, offset) = 0;  // 0.0f
    field<uint32_t>(actor, 0x1394) = 0;
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

void XB_REGS1 XB_CDECL Actor_ExpirePathPlanes(uint8_t* actor) {
    for (uint32_t slot = 0x12a8; slot < 0x1308; slot += 0x18) {
        const int32_t since = field<int32_t>(actor, slot);
        if (since <= 0) continue;
        const int32_t age = level_time() - since;
        if ((field<uint32_t>(actor, slot + 0x14) != 0 && age >= 300) || age >= field<int32_t>(actor, 0x1308)) {
            field<int32_t>(actor, slot) = 0;
            field<uint8_t*>(actor, 4)[0x5a] = 0;
        }
    }
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

void XB_REGS1 XB_CDECL Actor_MergeStateTransitions(uint8_t* actor) {
    int32_t& count = field<int32_t>(actor, 0xa0);
    if (count < 2) return;
    do {
        if (!field<uint32_t>(actor, 0x2d0)) {
            count = 1;
            return;
        }
        int32_t next = count;
        const uint32_t a = field<uint32_t>(actor, 0x28 + next * 8);
        const uint32_t b = field<uint32_t>(actor, 0x30 + next * 8);
        const int32_t merged = title_global<int32_t>(0x00830e00 + (b + a * 4) * 4);  // g_aiTransitionMerge
        if (merged == -1) return;
        if (merged == 0) {
            next -= 2;
        } else {
            field<int32_t>(actor, 0x28 + next * 8) = merged;
            field<uint32_t>(actor, 0x2c + count * 8) = field<uint32_t>(actor, 0x34 + count * 8);
            next = count - 1;
        }
        count = next;
    } while (count >= 2);
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

void XB_REGS1 XB_STDCALL Actor_AnimMoveRun(uint8_t* actor, uint32_t script) { anim_move(actor, script, false); }
void XB_REGS1 XB_STDCALL Actor_AnimMoveWalk(uint8_t* actor, uint32_t script) { anim_move(actor, script, true); }

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
PLATFORM_ASM_ENTRY(Actor_ForceState_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_ForceState) "movl %ebp, %esp\n"
                   "popl %ebp\n"
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

void XB_CDECL Actor_ClearAll() {
    for (uint32_t address = 0x00985384; address <= 0x00985400; address += 4) title_global<uint32_t>(address) = 0;
    title_global<uint32_t>(0x0095ee0c) = title_global<uint32_t>(0x00987a94);
    title_global<int32_t>(0x0095f44c) = -1;
}

void XB_REGS1 XB_CDECL Actor_InitDefaults(uint8_t* actor) {
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

void XB_REGS3 XB_CDECL Actor_SumMoveHistory(float* out, uint32_t, const uint8_t* actor) {
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
}

}  // namespace game
