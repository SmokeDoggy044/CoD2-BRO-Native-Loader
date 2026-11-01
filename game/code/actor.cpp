#include "actor.h"

#include <cstring>

#include "bg_misc.h"
#include "cm.h"
#include "dobj.h"
#include "g_main.h"
#include "g_script.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "script_string.h"
#include "sv.h"
#include "title_data.h"
#include "xanim.h"

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
                   "pushl %eax\n"  // EAX is handed back unchanged
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Sentient_SetGoalYaw) "addl $8, %esp\n"
                   "popl %eax\n"
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

uint32_t XB_THISCALL Actor_Noncombat_Resume(const uint8_t*) { return 0; }

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
                   "pushl %eax\n"  // EAX is handed back unchanged
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_SetSentientVisibility) "addl $12, %esp\n"
                   "popl %eax\n"
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
                   GAME_GAP_ESP("0x20000")
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
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_ForceState) "addl $16, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
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

uint8_t* XB_REGS3 XB_CDECL Actor_PopBlockingStates(uint8_t* actor, uint32_t, uint32_t state) {
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
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl %edi\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_ApplyStateTransitions) "addl $12, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
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
                   "pushl %eax\n"  // EAX is handed back unchanged
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Path_StepBack) "addl $8, %esp\n"
                   "popl %eax\n"
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

namespace {

// The inlined path clear: trim against the entity's origin, then no points and +0x7c4 = 0.
void clear_actor_path(uint8_t* actor) {
    Path_AccumulateTrim(actor + 0x3c0, &field<float>(field<uint8_t*>(actor, 0), 0x140));
    field<int16_t>(actor, 0x3c0 + 0x380) = 0;
    field<int16_t>(actor, 0x3c0 + 0x382) = 0;
    field<uint32_t>(actor, 0x7c4) = 0;
}

}  // namespace

PLATFORM_ASM_ENTRY(Actor_ClearPath_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_ClearPath) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_ClearPath(uint8_t* actor) {
    clear_actor_path(actor);
    return 0;
}

uint32_t XB_REGS3 XB_CDECL Actor_Pain_Start(uint32_t, uint32_t, uint8_t* actor) {
    field<int32_t>(actor, 0xc0) = level_time();
    clear_actor_path(actor);
    field<uint32_t>(actor, 0x254) = 0x00987b78;  // g_animScript_pain
    field<uint32_t>(actor, 0x27c) = 4;
    field<uint8_t>(actor, 0x285) = 0;
    field<uint8_t>(actor, 0x26c) = 1;
    return 0;
}

uint32_t XB_REGS3 XB_CDECL Actor_Noncombat_Start(uint32_t, uint32_t, uint8_t* actor) {
    clear_actor_path(actor);
    field<int32_t>(actor, 0x28) = 500;
    field<int32_t>(actor, 0x2c) = level_time() + 1;
    field<int32_t>(actor, 0x34) = level_time() + 501;
    field<int32_t>(actor, 0x34) = level_time() + 2000;
    return 0;
}

uint32_t XB_REGS3 XB_CDECL Actor_AnimCustom_Start(uint32_t, uint32_t, uint8_t* actor) {
    clear_actor_path(actor);
    field<uint8_t>(actor, 0x26c) = 1;
    return 0;
}

uint32_t XB_REGS3 XB_CDECL Actor_AnimScripted_Start(uint32_t, uint32_t, uint8_t* actor) {
    clear_actor_path(actor);
    field<uint8_t>(field<uint8_t*>(actor, 4), 0x5a) = 0;
    field<uint8_t>(actor, 0x26c) = 1;
    return 0;
}

void XB_REGS3 XB_CDECL Actor_DefaultTouch(uint32_t, const uint8_t* other, uint8_t* actor) {
    const uint32_t handle = field<uint32_t>(actor, 0x39c);
    if (field<uint32_t>(other, 0x224) != handle) return;
    // The entity behind the handle (index & 0xfff, generation >> 12; table 0x0097a968).
    const uint32_t index = handle & 0xfff;
    const uint8_t* ent = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        ent = title_global<const uint8_t*>(0x0097a968 + index * 8);
    if (field<uint32_t>(ent, 0x23c)) clear_actor_path(actor);  // the original reads it even from a null entity
}

PLATFORM_ASM_ENTRY(Actor_UpdateCoverValidity_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Actor_UpdateCoverValidity) "addl $12, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Actor_UpdateCoverValidity(const uint8_t* threat, uint8_t* actor, const uint8_t* node) {
    uint32_t script;
    const uint32_t valid = Actor_IsCoverValidAgainst(node, actor, threat, &script);
    field<uint32_t>(actor, 0x820) = valid ? script : 0;
    return 1;
}

PLATFORM_ASM_ENTRY(Actor_PushState_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_PushState) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_PushState(uint8_t* actor, uint32_t state) {
    if (!Actor_IsStatePushAllowed(state, 0, actor)) return 0;
    Actor_PopBlockingStates(actor, 0, state);
    queue_transition(actor, 2, state);  // push (and merge)
    const int32_t depth = ++field<int32_t>(actor, 0xbc);
    field<uint32_t>(actor, 0xa4 + depth * 4) = state;
    return 1;
}

PLATFORM_ASM_ENTRY(Actor_RecordMoveHistory_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_RecordMoveHistory) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_RecordMoveHistory(uint8_t* actor) {
    // Every tenth frame a new history slot, else this frame's move added to the current one.
    const float tenth = title_global<float>(0x0083ebf8);
    if ((level_time() / title_global<int32_t>(0x008dda9c)) % 10 != 0) {
        const int32_t head = field<int32_t>(actor, 0x1394);
        field<float>(actor, 0x1398 + head * 8) = field<float>(actor, 0x75c) * tenth + field<float>(actor, 0x1398 + head * 8);
        field<float>(actor, 0x139c + head * 8) = field<float>(actor, 0x760) * tenth + field<float>(actor, 0x139c + head * 8);
        return;
    }
    Actor_AdvanceMoveHistory(actor);
    const int32_t head = field<int32_t>(actor, 0x1394);
    field<float>(actor, 0x1398 + head * 8) = field<float>(actor, 0x75c) * tenth;
    field<float>(actor, 0x139c + head * 8) = field<float>(actor, 0x760) * tenth;
}

PLATFORM_ASM_ENTRY(Actor_RecordMoveHistoryBlended_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_RecordMoveHistoryBlended) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL Actor_RecordMoveHistoryBlended(uint8_t* actor, float speed) {
    // The move direction blended from +0x75c (slow) to +0x768 (fast) by the speed (95..190 units/s).
    const float limit = title_global<float>(0x0083ee28);
    const float f = (title_global<float>(0x0083eb9c) / static_cast<float>(title_global<int32_t>(0x008dda9c)) * speed - limit) / limit;
    const int32_t head = field<int32_t>(actor, 0x1394);
    float* entry = &field<float>(actor, 0x1398 + head * 8);
    const float one = title_global<float>(0x0083eb20);
    if (0.0f >= f) {
        entry[0] = field<float>(actor, 0x75c);
        entry[1] = field<float>(actor, 0x760);
    } else if (f >= one) {
        entry[0] = field<float>(actor, 0x768);
        entry[1] = field<float>(actor, 0x76c);
    } else {
        const float rest = one - f;
        const float x = field<float>(actor, 0x75c) * rest + field<float>(actor, 0x768) * f;
        const float y = field<float>(actor, 0x760) * rest + field<float>(actor, 0x76c) * f;
        const float length = title_ci_sqrt_ext(y * y + x * x);
        if (length == title_global<float>(0x0083eafc)) {
            entry[0] = x;
            entry[1] = y;
        } else {
            const float inverse = one / length;
            entry[0] = inverse * x;
            entry[1] = inverse * y;
        }
    }
    Actor_AdvanceMoveHistory(actor);
}

uint32_t XB_REGS3 XB_CDECL Actor_Exposed_NodeClaimRevoked(uint32_t, uint32_t, uint8_t* actor) {
    Sentient_SetClaimedNode(field<uint8_t*>(actor, 4), 0);
    clear_actor_path(actor);
    if (field<uint32_t>(actor, 0xa4) == 2 && field<uint32_t>(actor, 0x28) == 0xd3) Actor_Exposed_StartReacquire(1, actor);
    return 1;
}

void XB_REGS2 XB_CDECL Sentient_ClearNodeClaim(uint32_t, uint8_t* sentient) {
    field<uint32_t>(sentient, 4) = 0;
    field<uint32_t>(sentient, 8) = 0;
    for (uint32_t offset = 0x58; offset <= 0x5a; offset += 2) {
        const uint16_t handle = field<uint16_t>(sentient, offset);
        if (!node_handle_set(handle)) return;
        uint8_t* node = path_node(handle);
        if (--field<int16_t>(node, 0x26) == 0) field<uint32_t>(node, 8) = 0;
    }
}

// Its callees run where the original's calls put them (one push: EDI); the C frame sits below a gap.
PLATFORM_ASM_ENTRY(Actor_ThinkStateMachine_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %ebx\n"
                   "pushl 4(%ebp)\n"
                   "pushl %esi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_ThinkStateMachine) "addl $16, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $4")

uint32_t XB_CDECL Actor_ThinkStateMachine(uint32_t entry, uint8_t* actor, uint32_t arg, uint32_t ebx) {
    const uint32_t top = entry - 4;  // push edi
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor));
    uint8_t* ent = field<uint8_t*>(actor, 0);
    const uint32_t ent_value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    title_global<uint8_t*>(0x0095f708) = ent;
    if (field<uint32_t>(actor, 0xa0)) {
        const TitleRegs regs = {self, ebx, 0, 0, self, ent_value};
        title_call_at_regs(top, 0x002001f0, &regs, 0, nullptr);  // Actor_ApplyStateTransitions
    }
    // The entity we asked to make way: done once it is 37.5 units away, or when we walk away from it.
    if (const uint8_t* other = field<const uint8_t*>(actor, 0x1390)) {
        const float dx = field<float>(other, 0x140) - field<float>(ent, 0x140);
        const float dy = field<float>(other, 0x144) - field<float>(ent, 0x144);
        const float d2 = dy * dy + dx * dx;
        if (d2 >= title_global<float>(0x0083eefc)) {
            uint8_t* other_actor = field<uint8_t*>(other, 0x240);
            if (other_actor && field<uint8_t*>(other_actor, 0x1390) == ent) field<uint32_t>(other_actor, 0x1390) = 0;
            field<uint32_t>(actor, 0x1390) = 0;
        } else if (d2 >= title_global<float>(0x0083ee3c) && !field<uint32_t>(actor, 0x7c8) &&
                   (field<uint32_t>(actor, 0x278) == 7 || field<uint32_t>(actor, 0x278) == 1) && field<int16_t>(actor, 0x740) > 0) {
            const float along = field<float>(actor, 0x760) * (field<float>(ent, 0x144) - field<float>(other, 0x144)) +
                                field<float>(actor, 0x75c) * (field<float>(ent, 0x140) - field<float>(other, 0x140));
            if (along > title_global<float>(0x0083eafc)) {
                field<uint32_t>(actor, 0x27c) = 1;
                field<uint32_t>(actor, 0x1390) = 0;
            }
        }
    }
    // Think (slot 4) of the current state: actor in ECX, the argument in EDX.
    const uint32_t state = field<uint32_t>(actor, 8 + field<int32_t>(actor, 0x20) * 4);
    const TitleRegs think = {state * 9, ebx, self, arg, self, ent_value};
    return title_call_at_regs(top, title_global<uint32_t>(0x00830c38 + state * 36), &think, 0, nullptr);
}

PLATFORM_ASM_ENTRY(Actor_FindNearbyActor_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Actor_FindNearbyActor) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_FindNearbyActor(uint8_t* actor) {
    if (field<uint32_t>(actor, 0x1390)) return;
    const uint8_t* ent = field<const uint8_t*>(actor, 0);
    const float origin[3] = {field<float>(ent, 0x140), field<float>(ent, 0x144), field<float>(ent, 0x148)};
    // Cowering from a grenade or standing on its node: only actors on the move (handle +0x39c to an
    // entity without a sentient) count.
    const bool settled = field<uint32_t>(actor, 0x250) == 0x00987bc8 || Actor_IsAtClaimedNode(0, actor);
    for (uint32_t slot = 0x00985384; slot < 0x00985404; slot += 4) {
        uint8_t* other = title_global<uint8_t*>(slot);
        if (!other || field<int32_t>(other, 0x1360) < 0 || other == actor) continue;
        const uint8_t* other_ent = field<const uint8_t*>(field<const uint8_t*>(other, 4), 0);
        const float dz = origin[2] - field<float>(other_ent, 0x148);
        if (dz * dz >= title_global<float>(0x0083eb6c)) continue;
        const float dy = field<float>(other_ent, 0x144) - origin[1];
        const float dx = field<float>(other_ent, 0x140) - origin[0];
        if (dy * dy + dx * dx >= title_global<float>(0x0083ee3c)) continue;
        if (settled) {
            if (!field<uint32_t>(other, 0x39c)) continue;
            if (field<uint32_t>(G_EntHandleToEnt(&field<uint32_t>(other, 0x39c)), 0x244)) continue;
        }
        field<uint32_t>(actor, 0x1390) = field<uint32_t>(other, 0);
        return;
    }
}

// The callback runs at the original's ESP (no pushes) with its registers; the C frame sits below a gap.
PLATFORM_ASM_ENTRY(Actor_CallStateSlot5_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_CallStateSlot5) "addl $20, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_CallStateSlot5(uint32_t entry, uint32_t handle, uint32_t ebx, uint32_t esi, uint32_t edi) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return 0;
    const uint8_t* ent = title_global<const uint8_t*>(0x0097a968 + index * 8);
    if (!ent || !field<uint32_t>(ent, 0x240)) return 0;
    uint8_t* actor = field<uint8_t*>(ent, 0x240);
    const int32_t depth = field<int32_t>(actor, 0x20);
    title_global<uint32_t>(0x0095f704) = field<uint32_t>(actor, 0);
    const uint32_t state = field<uint32_t>(actor, 8 + depth * 4);
    const TitleRegs regs = {state * 9, ebx, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)),
                            static_cast<uint32_t>(depth), esi, edi};
    title_call_at_regs(entry, title_global<uint32_t>(0x00830c3c + state * 36), &regs, 0, nullptr);
    title_global<uint32_t>(0x0095f704) = 0;
    return 0;
}

PLATFORM_ASM_ENTRY(PathNode_Construct_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(PathNode_Construct) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL PathNode_Construct(uint8_t* node) {
    *reinterpret_cast<uint32_t*>(node) = 0;
    *reinterpret_cast<uint16_t*>(node + 4) = 0;
    for (uint32_t offset = 8; offset <= 0x14; offset += 4)
        ScriptString_FromRep(reinterpret_cast<ScriptString*>(node + offset), nullptr);
    *reinterpret_cast<uint32_t*>(node + 0x18) = 0;
    *reinterpret_cast<float*>(node + 0x28) = 0.0f;
    *reinterpret_cast<float*>(node + 0x2c) = 0.0f;
    for (uint32_t offset = 0x30; offset <= 0x3a; offset += 2) *reinterpret_cast<uint16_t*>(node + offset) = 0;
    *reinterpret_cast<uint32_t*>(node + 0x3c) = 0;
    *reinterpret_cast<float*>(node + 0x24) = 0.0f;
    *reinterpret_cast<float*>(node + 0x20) = 0.0f;
    *reinterpret_cast<float*>(node + 0x1c) = 0.0f;
    return node;
}

PLATFORM_ASM_ENTRY(ActorBlockList_RemoveEntity_entry,
                   "pushl %ebx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(ActorBlockList_RemoveEntity) "addl $8, %esp\n"
                   "ret")

void XB_CDECL ActorBlockList_RemoveEntity(uint8_t* list, uint32_t ent) {
    int32_t& count = *reinterpret_cast<int32_t*>(list + 0x78);
    for (;;) {
        int32_t i = 0;
        uint32_t* row = reinterpret_cast<uint32_t*>(list);
        for (; i < count; ++i, row += 6) {
            const uint32_t* a = reinterpret_cast<const uint32_t*>(uintptr_t{row[0]});
            const uint32_t* b = reinterpret_cast<const uint32_t*>(uintptr_t{row[1]});
            if ((a && *a == ent) || (b && *b == ent)) break;
        }
        if (i >= count) return;
        std::memcpy(row, list + (count - 1) * 0x18, 0x18);
        --count;
    }
}

void XB_CDECL ActorMove_AddTouchEnt(uint32_t ent) {
    if (!ent) return;
    uint8_t* move = title_global<uint8_t*>(0x0095f450);  // g_actorMove
    int32_t& count = *reinterpret_cast<int32_t*>(move + 0x88);
    uint32_t* touched = reinterpret_cast<uint32_t*>(move + 0x8c);
    const int32_t n = count;
    if (n == 0x20) return;
    for (int32_t i = 0; i < count; ++i)
        if (touched[i] == ent) return;
    touched[n] = ent;
    ++count;
}

namespace {

uint8_t* path_node_from_handle(const uint16_t* handle) {
    const uint16_t h = *handle;
    if (!h || h == 0xffff) return nullptr;
    const uint32_t block_index = static_cast<uint16_t>((h >> 9) - 1);
    uint8_t* block = reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f4f4) + 8)[block_index];  // g_pathNodeBlocks
    if (!block) return nullptr;
    return *reinterpret_cast<uint8_t**>(block + 0x10) + (h & 0x1ff) * 0x80;
}

}  // namespace

uint8_t* XB_REGS1 XB_CDECL PathNode_FromHandle(const uint16_t* handle) { return path_node_from_handle(handle); }

uint8_t* XB_REGS1 XB_CDECL PathNode_FromHandle2(const uint16_t* handle) { return path_node_from_handle(handle); }

PLATFORM_ASM_ENTRY(PathNode_IsAvailableFor_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(PathNode_IsAvailableFor) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL PathNode_IsAvailableFor(const uint8_t* node, const uint8_t* sentient) {
    if (*reinterpret_cast<const uint8_t* const*>(node + 4) != sentient &&
        title_global<int32_t>(0x0098540c) <= *reinterpret_cast<const int32_t*>(node + 8))  // level_time
        return 0;
    for (uint8_t* other = Sentient_FirstOfTeams(4); other; other = Sentient_NextOfTeams(other, 4))
        if (other != sentient && *reinterpret_cast<const uint16_t*>(node) == *reinterpret_cast<const uint16_t*>(other + 0x50))
            return 0;
    return 1;
}

PLATFORM_ASM_ENTRY(ActorBlockList_Bump_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(ActorBlockList_Bump) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL ActorBlockList_Bump(uint8_t* list, uint32_t a, uint32_t b) {
    const float bump = title_global<float>(0x008ddaa0);
    int32_t& count = *reinterpret_cast<int32_t*>(list + 0x78);
    const int32_t initial = count;
    if (initial > 0) {
        for (int32_t i = 0; i < count; ++i) {
            uint8_t* row = list + i * 0x18;
            const uint32_t first = *reinterpret_cast<uint32_t*>(row);
            const uint32_t second = *reinterpret_cast<uint32_t*>(row + 4);
            if ((first == a && second == b) || (second == a && first == b)) {
                *reinterpret_cast<float*>(row + 8) = *reinterpret_cast<float*>(row + 8) + bump;
                *reinterpret_cast<int32_t*>(row + 0xc) = title_global<int32_t>(0x0098540c);  // level_time
                return;
            }
        }
    }
    if (initial >= 5) return;
    uint8_t* row = list + initial * 0x18;
    *reinterpret_cast<float*>(row + 8) = 0.0f;
    *reinterpret_cast<uint32_t*>(row) = a;
    *reinterpret_cast<uint32_t*>(row + 4) = b;
    *reinterpret_cast<int32_t*>(row + 0xc) = title_global<int32_t>(0x0098540c);
    *reinterpret_cast<uint16_t*>(row + 0x10) = 0;
    *reinterpret_cast<float*>(row + 0x14) = 120.0f;
    *reinterpret_cast<uint16_t*>(row + 0x12) = 0;
    ++count;
}

PLATFORM_ASM_ENTRY(PathNode_CanSee_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PathNode_CanSee) "addl $12, %esp\n"
                   "ret $4")

uint32_t XB_CDECL PathNode_CanSee(const uint8_t* blocks, const uint16_t* first, const uint16_t* second) {
    const uint16_t h1 = *first;
    const uint16_t h2 = *second;
    const uint16_t b1 = static_cast<uint16_t>((h1 >> 9) - 1);
    const uint16_t b2 = static_cast<uint16_t>((h2 >> 9) - 1);
    const int32_t n1 = h1 & 0x1ff;
    const int32_t n2 = h2 & 0x1ff;
    auto block = [&](uint32_t index) { return *reinterpret_cast<const uint8_t* const*>(blocks + 8 + index * 4); };
    const uint8_t* visibility;
    int32_t bit;
    if (b1 < b2) {
        visibility = *reinterpret_cast<const uint8_t* const*>(*reinterpret_cast<const uint8_t* const*>(block(b1) + 0x28) + b2 * 8);
        bit = n1 * *reinterpret_cast<const int32_t*>(block(b2) + 4) + n2;
    } else {
        visibility = *reinterpret_cast<const uint8_t* const*>(*reinterpret_cast<const uint8_t* const*>(block(b2) + 0x28) + b1 * 8);
        bit = n2 * *reinterpret_cast<const int32_t*>(block(b1) + 4) + n1;
    }
    if (!visibility) return 0;
    return (visibility[bit >> 3] & (1 << (bit & 7))) ? 1 : 0;
}

PLATFORM_ASM_ENTRY(Actor_AnimStationary_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_AnimStationary) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_AnimStationary(uint8_t* actor, uint32_t script) {
    Actor_FindNearbyActor(actor);
    uint32_t chosen = script;
    if (*reinterpret_cast<uint32_t*>(actor + 0x1390)) {
        const float x = *reinterpret_cast<float*>(actor + 0x2a0);
        const float y = *reinterpret_cast<float*>(actor + 0x2a4);
        if (!(1.0f > x * x + y * y)) chosen = 0x00987be8;  // g_animScript_walk
    }
    *reinterpret_cast<uint32_t*>(actor + 0x254) = chosen;
    *reinterpret_cast<uint32_t*>(actor + 0x27c) = 7;
    actor[0x285] = 0;
}

PLATFORM_ASM_ENTRY(PathNode_IsLinkInBadPlace_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PathNode_IsLinkInBadPlace) "addl $16, %esp\n"
                   "ret $8")

uint32_t XB_CDECL PathNode_IsLinkInBadPlace(const uint16_t* handle, const uint8_t* blocks, const uint16_t* target,
                                            int32_t index) {
    const uint16_t h = *handle;
    const uint8_t* node = nullptr;
    if (h && h != 0xffff) {
        const uint8_t* block = *reinterpret_cast<const uint8_t* const*>(blocks + 8 + static_cast<uint16_t>((h >> 9) - 1) * 4);
        if (block) node = *reinterpret_cast<const uint8_t* const*>(block + 0x10) + (h & 0x1ff) * 0x80;
    }
    const int32_t count = *reinterpret_cast<const int16_t*>(node + 0x62);
    if (count <= 0) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node)) & ~0xffu;
    const uint8_t* links = *reinterpret_cast<const uint8_t* const*>(node + 0x64);
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* link = links + i * 0xc;
        if (*reinterpret_cast<const uint16_t*>(link) == *target) return link[4 + index] ? 1 : 0;
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(links + count * 0xc)) & ~0xffu;
}

namespace {

uint8_t* path_node(uint32_t value, const uint8_t* table) {
    const uint16_t handle = uint16_t(value);
    if (handle == 0 || handle == 0xffff) return nullptr;
    const uint16_t block_index = uint16_t((handle >> 9) - 1);
    const uint8_t* const block = *reinterpret_cast<uint8_t* const*>(table + 8 + uint32_t(block_index) * 4);
    if (!block) return nullptr;
    return *reinterpret_cast<uint8_t* const*>(block + 0x10) + (value & 0x1ff) * 0x80;
}

}  // namespace

uint32_t XB_REGS1 XB_CDECL PathNode_HandleIsValid(const uint16_t* handle) {
    // The original keeps the pointer's upper half in EAX; only the low 9 bits reach the node offset.
    return path_node(*handle, title_global<const uint8_t*>(0x0095f4f4)) ? 1 : 0;
}

uint8_t* XB_REGS2 XB_CDECL PathNode_FromHandleInTable(const uint16_t* handle, const uint8_t* table) {
    return path_node(*handle, table);
}

PLATFORM_ASM_ENTRY(EntBits_IterNext_entry, "pushl %edx\n" PLATFORM_ASM_CALL(EntBits_IterNext) "addl $4, %esp\n"
                                           "ret")

void XB_CDECL EntBits_IterNext(uint32_t* iterator) {
    if (int32_t(iterator[2]) == -1) return;
    if (!iterator[1]) {
        const uint32_t* const bits = reinterpret_cast<const uint32_t*>(uintptr_t{iterator[0]});
        while (int32_t(iterator[2]) < 0x29) {
            ++iterator[2];
            iterator[1] = bits[iterator[2]];
            if (iterator[1]) break;
        }
    }
    if (!iterator[1]) {
        iterator[3] = 0xffffffff;
        iterator[2] = 0xffffffff;
        return;
    }
    uint32_t bit = 0;
    while (!(iterator[1] & (1u << bit))) ++bit;  // BSF
    iterator[1] &= ~(1u << bit);
    iterator[3] = (iterator[2] << 5) + bit;
}

PLATFORM_ASM_ENTRY(BadPlace_FindByName_entry, "pushl %ebx\n" PLATFORM_ASM_CALL(BadPlace_FindByName) "addl $4, %esp\n"
                                              "ret")

int32_t XB_CDECL BadPlace_FindByName(const ScriptString* name) {
    for (int32_t i = 0; i < 32; ++i) {
        const auto* const slot = &title_global<const ScriptString>(0x0098a574 + uint32_t(i) * 0x28);
        if (ScriptString_EqualsString(slot, name)) return i;
    }
    return -1;
}

PLATFORM_ASM_ENTRY(Sentient_ClaimGoalNode_entry, "pushl %edi\n" PLATFORM_ASM_CALL(Sentient_ClaimGoalNode) "addl $4, %esp\n"
                                                 "ret")

void XB_CDECL Sentient_ClaimGoalNode(uint8_t* sentient) {
    const uint16_t* const goal = reinterpret_cast<const uint16_t*>(sentient + 0x50);
    if (*reinterpret_cast<const uint16_t*>(sentient + 0x48) == *goal) return;
    if (!(PathNode_HandleIsValid(goal) & 0xff)) return;
    if (!PathNode_IsAvailableFor(PathNode_FromHandle2(goal), sentient)) return;
    Sentient_SetClaimedNode(sentient, *goal);
}

PLATFORM_ASM_ENTRY(Sentient_SetGoalNode_entry, "pushl 4(%esp)\n"
                                               "pushl %ebx\n" PLATFORM_ASM_CALL(Sentient_SetGoalNode) "addl $8, %esp\n"
                                               "ret $4")

void XB_CDECL Sentient_SetGoalNode(uint8_t* sentient, uint32_t handle) {
    const uint16_t goal = uint16_t(handle);
    uint16_t& current = *reinterpret_cast<uint16_t*>(sentient + 0x50);
    if (goal != current) {
        const uint16_t* const claimed = reinterpret_cast<const uint16_t*>(sentient + 0x48);
        if ((PathNode_HandleIsValid(claimed) & 0xff) && current == *claimed) {
            const uint8_t* const actor = field<uint8_t*>(*reinterpret_cast<uint8_t**>(sentient), 0x240);
            if (actor) {
                const int32_t state = field<int32_t>(const_cast<uint8_t*>(actor), 0xa4);
                if (state != 1 && state != 6) Sentient_SetClaimedNode(sentient, handle & 0xffff0000);
            }
        }
        current = goal;
    }
    const uint8_t* const node = path_node(handle, title_global<const uint8_t*>(0x0095f4f4));
    if (!node) return;
    float radius = *reinterpret_cast<const float*>(node + 0x54);
    if (radius == title_global<float>(0x0083eafc)) return;  // 0
    const float least = title_global<float>(0x0083100c);   // 4
    if (least > radius) radius = least;
    uint8_t* const goal_info = field<uint8_t*>(*reinterpret_cast<uint8_t**>(sentient), 0x244);
    field<float>(goal_info, 0x18) = radius;
    field<float>(goal_info, 0x1c) = radius * radius;
}

void XB_REGS3 XB_CDECL Actor_Exposed_Finish(uint32_t, int32_t kind, const uint8_t* state) {
    uint8_t* const sentient = *reinterpret_cast<uint8_t* const*>(state + 4);
    const uint16_t* const claimed = reinterpret_cast<const uint16_t*>(sentient + 0x48);
    const uint8_t* const node = path_node(*claimed, title_global<const uint8_t*>(0x0095f4f4));
    if (!node) return;
    if (kind == 1) {
        const uint32_t type = field<uint32_t>(PathNode_FromHandle(claimed), 0x28);
        if ((1u << (type & 31)) & 0x1ffc) return;
    }
    Sentient_SetClaimedNode(sentient, uint32_t(uintptr_t(node)) & 0xffff0000);
}

void XB_REGS3 XB_CDECL ActorMove_ProjectVelocity(uint32_t, float* out, const float* normal, const float* velocity,
                                                 float overbounce) {
    if (normal[2] >= title_global<float>(0x0083ed28)) {  // 0.7
        const float horizontal = velocity[0] * velocity[0] + velocity[1] * velocity[1];
        if (horizontal >= velocity[2] * velocity[2]) {
            out[2] = (0.0f - velocity[0] * normal[0]) - normal[1] * velocity[1];
            out[0] = velocity[0] * normal[2];
            out[1] = velocity[1] * normal[2];
            return;
        }
    }
    float backoff = normal[1] * velocity[1] + velocity[0] * normal[0] + velocity[2] * normal[2];
    if (0.0f > backoff) {
        backoff *= overbounce;
    } else {
        backoff /= overbounce;
    }
    out[0] = velocity[0] - normal[0] * backoff;
    out[1] = velocity[1] - normal[1] * backoff;
    out[2] = velocity[2] - backoff * normal[2];
}

uint32_t XB_REGS3 XB_CDECL Actor_Noncombat_NodeClaimRevoked(uint32_t, uint32_t, uint8_t* actor) {
    if (field<int16_t>(actor, 0x740) > 0) {
        const float x = field<float>(actor, 0x750);
        const float y = field<float>(actor, 0x754);
        const float z = field<float>(actor, 0x758);
        const uint16_t* const claimed = reinterpret_cast<const uint16_t*>(field<uint8_t*>(actor, 4) + 0x48);
        const uint8_t* const node = path_node(*claimed, title_global<const uint8_t*>(0x0095f4f4));
        if (x == *reinterpret_cast<const float*>(node + 0x44) &&
            y == *reinterpret_cast<const float*>(PathNode_FromHandle(claimed) + 0x48) &&
            z == *reinterpret_cast<const float*>(PathNode_FromHandle(claimed) + 0x4c))
            Actor_ClearPath(actor);
    }
    Sentient_SetClaimedNode(field<uint8_t*>(actor, 4), uint32_t(uintptr_t(actor)) & 0xffff0000);
    return 1;
}

PLATFORM_ASM_ENTRY(Actor_RegisterSuppression_entry, "movl %esp, %edx\n"
                                                    GAME_GAP_ESP("0x20000")
                                                    "pushl %ebp\n"
                                                    "movl %edx, %ebp\n"
                                                    "pushl 4(%ebp)\n"
                                                    "pushl %ebx\n"
                                                    "pushl %eax\n"
                                                    "pushl %ecx\n"
                                                    "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_RegisterSuppression) "addl $20, %esp\n"
                                                    "popl %ebp\n"
                                                    "popl %esp\n"
                                                    "ret $4")

void XB_CDECL Actor_RegisterSuppression(uint32_t entry, uint8_t* actor, const float* start, const float* end,
                                        uint8_t* sentient) {
    if (float(field<int32_t>(actor, 0x1308)) == 0.0f) return;
    uint8_t* const shooter = field<uint8_t*>(sentient, 0);
    uint8_t* const own = field<uint8_t*>(actor, 4);
    if (!field<uint32_t>(shooter, 0x23c) &&
        field<int32_t>(sentient, 4) != Sentient_EnemyTeam(field<int32_t>(own, 4)))
        return;
    const uint32_t slot = Actor_FindSentientSlot(actor, uint32_t(uintptr_t(sentient)));
    own[0x5a] = 0;
    field<int32_t>(actor, (slot * 3 + 0x255) * 8) = title_global<int32_t>(0x0098540c);  // level time
    field<uint8_t*>(actor, slot * 24 + 0x12ac) = sentient;
    float dy = start[1] - end[1];
    float dx = end[0] - start[0];
    float dist = start[1] * dx + start[0] * dy;
    const uint8_t* const entity = field<uint8_t*>(actor, 0);
    const float self = field<float>(const_cast<uint8_t*>(entity), 0x144) * dx + field<float>(const_cast<uint8_t*>(entity), 0x140) * dy;
    if (self > dist) {
        dy = 0.0f - dy;
        dx = 0.0f - dx;
        dist = 0.0f - dist;
    }
    uint8_t* const record = actor + slot * 24;
    field<float>(record, 0x12b0) = dy;
    field<float>(record, 0x12b4) = dx;
    field<float>(record, 0x12b8) = dist;
    if (field<int32_t>(own, 4) == field<int32_t>(sentient, 4)) {
        field<uint32_t>(record, 0x12bc) = 1;
        if (field<int16_t>(actor, 0x740) > 0) Actor_ClearPath(actor);
        Scr_Notify(field<uint8_t*>(actor, 0), title_global<uint32_t>(0x0097a730));  // "movesuppressed"
        return;
    }
    // The original passes the address of its argument slot holding the shooter's entity number.
    const uint32_t number_slot = entry + 4;
    title_global<uint32_t>(number_slot) = field<uint32_t>(shooter, 0x224);
    TitleRegs regs{};
    regs.eax = number_slot;
    regs.ecx = title_global<uint32_t>(0x0097a7f4);  // "suppressed"
    regs.edx = field<uint32_t>(shooter, 0x224);
    regs.ebx = uint32_t(uintptr_t(end));
    regs.esi = uint32_t(uintptr_t(start));
    regs.edi = uint32_t(uintptr_t(field<uint8_t*>(actor, 0)));
    const uint32_t args[2] = {title_global<uint32_t>(0x0097a7f4), number_slot};
    title_call_at_regs(entry - 0x20, 0x000265a0, &regs, 2, args);  // G_NotifyWithEntity
}

// Bare `ret` state callbacks (1 byte: too short to hook; the C is kept).
void XB_CDECL Actor_Noncombat_Suspend() {}

PLATFORM_ASM_ENTRY(Path_GetRemainingLength_entry, "pushl %ecx\n" PLATFORM_ASM_CALL(Path_GetRemainingLength) "fstps (%esp)\n"
                                                  "movss (%esp), %xmm0\n"
                                                  "addl $4, %esp\n"
                                                  "ret")

float XB_CDECL Path_GetRemainingLength(const uint8_t* path) {
    // The lookahead pieces (+0x3b8, +0x3b0), the last segment (+0x3c8) when the lookahead
    // ends on the next-to-last point, then the point lengths (+0x14) down to the start index.
    const int16_t count = field<int16_t>(path, 0x380);
    if (!count) return 0.0f;
    const int16_t end = field<int16_t>(path, 0x388);
    const int16_t start = field<int16_t>(path, 0x386);
    float length = field<float>(path, 0x3b8) + field<float>(path, 0x3b0);
    if (end == start) return length;
    int32_t i = end - 1;
    if (i == count - 2) {
        length = field<float>(path, 0x3c8) + length;
        --i;
    }
    for (; i >= start; --i) length = field<float>(path, 0x14 + uint32_t(i) * 0x1c) + length;
    return length;
}

void XB_CDECL Actor_Lmg_Finish() {}

// `ret 0x14`: the default Pain callback ignores its five arguments.
void XB_STDCALL Actor_DefaultPain(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}

// The default Resume (slot 3) and Suspend (slot 2) tail-jump to the current state's Start
// (slot 0) / Finish (slot 1) with ECX = actor, EDX as given and EAX = state * 9. The C
// version returns the table index; the entry jumps through the table at the entry ESP.
PLATFORM_ASM_ENTRY(Actor_DefaultResume_entry,
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Actor_DefaultResume) "addl $4, %esp\n"
                   "popl %ecx\n"
                   "popl %edx\n"
                   "popl %esp\n"
                   "jmp *0x00830c28(,%eax,4)")

uint32_t XB_CDECL Actor_DefaultResume(const uint8_t* actor) {
    return field<uint32_t>(actor, 8 + field<uint32_t>(actor, 0x20) * 4) * 9;
}

PLATFORM_ASM_ENTRY(Actor_DefaultSuspend_entry,
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Actor_DefaultSuspend) "addl $4, %esp\n"
                   "popl %ecx\n"
                   "popl %edx\n"
                   "popl %esp\n"
                   "jmp *0x00830c2c(,%eax,4)")

uint32_t XB_CDECL Actor_DefaultSuspend(const uint8_t* actor) {
    return field<uint32_t>(actor, 8 + field<uint32_t>(actor, 0x20) * 4) * 9;
}

void XB_CDECL Actor_DefaultFinish() {}

uint32_t XB_CDECL BadPlace_ClearAll() {
    // The 32 bad places (0x28 bytes from 0x0098a570): each name (+4) set to the empty
    // string through a temporary, then the whole table zeroed (REP STOSD, EAX = 0).
    using Assign = void XB_THISCALL(ScriptString*, const ScriptString*);
    for (uint32_t i = 0; i < 32; ++i) {
        ScriptString empty;
        ScriptString_FromRep(&empty, nullptr);
        title_function<Assign>(0x0011d600)(&title_global<ScriptString>(0x0098a574 + i * 0x28), &empty);  // ScriptString_Assign
        title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&empty);                           // ScriptString_Release
    }
    for (uint32_t offset = 0; offset < 0x500; offset += 4) title_global<uint32_t>(0x0098a570 + offset) = 0;
    return 0;
}

void XB_CDECL Actor_Callback348Stub() {}

// The entity's Touch: tail jump to slot 6 of the actor's current state with ECX = actor,
// EDX = other (the second argument), EAX = state * 9; the stack arguments stay in place.
PLATFORM_ASM_ENTRY(Actor_Touch_entry,
                   GAME_GAP_ESP("0x10000")
                   "movl (%esp), %eax\n"
                   "pushl 4(%eax)\n" PLATFORM_ASM_CALL(Actor_Touch) "addl $4, %esp\n"
                   "popl %esp\n"
                   "movl %edx, %ecx\n"
                   "movl 8(%esp), %edx\n"
                   "jmp *0x00830c40(,%eax,4)")

uint64_t XB_CDECL Actor_Touch(const uint8_t* ent) {
    const uint8_t* actor = field<const uint8_t*>(ent, 0x240);
    const uint32_t index = field<uint32_t>(actor, 8 + field<uint32_t>(actor, 0x20) * 4) * 9;
    return (uint64_t{uint32_t(uintptr_t(actor))} << 32) | index;  // EDX:EAX
}

PLATFORM_ASM_ENTRY(Actor_PathAllowsFacingEnemy_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_PathAllowsFacingEnemy) "addl $12, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Actor_PathAllowsFacingEnemy(const uint8_t* path, const float* origin, const float* dir) {
    // 1 = the actor may turn to its enemy (dir) while following the path (ESI) from origin (EDI):
    // a fresh (< 300 ms) or short (<= 128) path always allows it; else the path must not turn
    // toward dir within a reach that shrinks as dir lines up with the path's direction
    // (+0x39c/+0x3a0), nor climb or drop more than 12 - 6 * alignment on the way.
    const int32_t age = level_time() - field<int32_t>(path, 0x3c0);
    if (age < 300) return 1;
    if (!Path_IsLongerThan(path, title_global<float>(0x0083ebe8))) return 1;  // 128
    float align = dir[1] * field<float>(path, 0x3a0);
    align = align + field<float>(path, 0x39c) * dir[0];
    align = (align + title_global<float>(0x0083eb20)) * title_global<float>(0x0083eb24);  // (+1) * 0.5
    const float reach = title_global<float>(0x0083ecdc) - align * title_global<float>(0x0083ecd8);  // 72 - 48a
    if (reach > field<float>(path, 0x3b0)) return 0;
    const float time_limit = align * title_global<float>(0x0083ecd4) + title_global<float>(0x0083ebec);  // 4500a + 500
    if (float(age) >= time_limit) return 0;
    const int32_t last = field<int16_t>(path, 0x380) - 1;
    const int32_t stop = field<int16_t>(path, 0x388);
    const float height = title_global<float>(0x0083eccc) - align * title_global<float>(0x0083ecd0);  // 12 - 6a
    float dz = field<float>(path, 0x3d4) - origin[2];
    const float height_sq = height * height;
    if (stop == last) {
        dz = ((dz / field<float>(path, 0x3b0)) * reach + origin[2]) - origin[2];
        return dz * dz > height_sq ? 0 : 1;
    }
    if (dz * dz > height_sq) return 0;
    const float zero = 0.0f;
    float previous;
    {
        const uint8_t* point = path + uint32_t(last) * 0x1c;
        const float dy = field<float>(point, 4) - origin[1];
        const float dx = field<float>(point, 0) - origin[0];
        previous = field<float>(path, 0x3a0) * dy;
        previous = previous + field<float>(path, 0x39c) * dx;
        previous = previous - reach;
        if (previous > zero) return 1;
    }
    for (int32_t i = last - 1; i >= stop; --i) {
        const uint8_t* point = path + uint32_t(i) * 0x1c;
        const float dy = field<float>(point, 4) - origin[1];
        const float dx = field<float>(point, 0) - origin[0];
        float along = field<float>(path, 0x3a0) * dy;
        along = along + field<float>(path, 0x39c) * dx;
        along = along - reach;
        if (along > zero) {
            // The reach crosses between points i and i + 1: the height there.
            float z = field<float>(point, 8) - field<float>(point, 0x24);
            z = z * previous;
            z = z / (previous - along);
            z = z + field<float>(point, 0x24);
            z = z - origin[2];
            return z * z > height_sq ? 0 : 1;
        }
        const float dzi = field<float>(point, 8) - origin[2];
        if (dzi * dzi > height_sq) return 0;
        previous = along;
    }
    return 1;
}

uint32_t XB_THISCALL Path_NeedsEndRefresh(const uint8_t* path) {
    // 1 when the path ends on a node point (+0x18 not 0/-1) after a plain point, flag 0x20 is
    // clear, the length (+0x3b4) is above 0.001 and the point count (+0x382 = the count it was
    // built with) or the end position (+0x3cc) no longer match.
    const int16_t count = field<int16_t>(path, 0x380);
    const int32_t last = count - 1;
    if (last <= field<int16_t>(path, 0x386)) return 0;
    const uint8_t* const end = path + uint32_t(last) * 0x1c;
    const int16_t node = field<int16_t>(end, 0x18);
    if (node == 0 || node == -1) return 0;
    const int16_t previous = field<int16_t>(end - 0x1c, 0x18);
    if (previous != 0 && previous != -1) return 0;
    if (path[0x3bc] & 0x20) return 0;
    if (title_global<float>(0x0083eb4c) >= field<float>(path, 0x3b4)) return 0;  // 0.001
    if (count != field<int16_t>(path, 0x382)) return 1;
    if (field<float>(path, 0x3cc) != field<float>(end, 0)) return 1;
    if (field<float>(path, 0x3d0) != field<float>(end, 4)) return 1;
    if (field<float>(path, 0x3d4) != field<float>(end, 8)) return 1;
    return 0;
}

void XB_CDECL Actor_DefaultSlot5() {}

// Callers keep XMM3 across the call.
PLATFORM_ASM_ENTRY(Actor_IsInWeaponRange_entry,
                   "subl $16, %esp\n"
                   "movups %xmm3, (%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Sentient_GetRangeAccuracyScale) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $8, %esp\n"
                   "movups (%esp), %xmm3\n"
                   "addl $16, %esp\n"
                   "ret")

float XB_CDECL Sentient_GetRangeAccuracyScale(const uint8_t* sentient, float distance) {
    // A range factor for `distance` (XMM0): actors use their weapon's near/far ranges
    // (+0x800/+0x804; full inside near, 0 beyond far, linear between) times the sentient's
    // +0x2c; others blend two global values (0x0094d6a8 at 0x0094cb10, 0x0095c108 at
    // 0x0095c1a0), times 5.
    const uint8_t* const actor = field<const uint8_t*>(field<const uint8_t*>(sentient, 0), 0x240);
    if (!actor) {
        const float near = title_global<float>(0x0094cb10);
        if (near > distance) return title_global<float>(0x0094d6a8) * title_global<float>(0x0083ece8);
        const float far = title_global<float>(0x0095c1a0);
        if (distance > far) return title_global<float>(0x0095c108) * title_global<float>(0x0083ece8);
        float t = distance - near;
        t = t / (far - near);
        const float near_value = title_global<float>(0x0094d6a8);
        t = t * (title_global<float>(0x0095c108) - near_value);
        return (t + near_value) * title_global<float>(0x0083ece8);
    }
    // The weapon index is kept in AL: the def of (index & 0xff), 0 when not found.
    const int32_t last = title_global<int32_t>(0x0095f4d4);                         // bg_lastWeaponIndex
    const uint8_t* const* const defs = title_global<const uint8_t* const*>(0x0095f4d8);  // bg_weaponDefs
    uint32_t index = 0;
    for (int32_t i = 0; i <= last; ++i) {
        if (field<uint32_t>(actor, 0xf0) == field<uint32_t>(defs[i], 4)) {
            index = uint32_t(i) & 0xff;
            break;
        }
    }
    const uint8_t* const def = defs[index];
    if (field<float>(def, 0x800) > distance) return field<float>(sentient, 0x2c) * title_global<float>(0x0083eb20);
    if (distance > field<float>(def, 0x804)) {
        const float zero = 0.0f;
        return field<float>(sentient, 0x2c) * zero;
    }
    float t = distance - field<float>(def, 0x800);
    t = t / (field<float>(def, 0x804) - field<float>(def, 0x800));
    return field<float>(sentient, 0x2c) * (title_global<float>(0x0083eb20) - t);
}

void XB_THISCALL Actor_UpdateAvoidLeader(uint8_t* actor) {
    // Only in modes 1 and 7 (+0x278): follows the +0x7c8 links to the actor that links to
    // itself (the leader) and keeps it (+0x7c8, its entity +0x7cc) while the leader's
    // +0x7c0 time (+ 0x008dda9c once the leader entity's +0x328 time has passed) lies ahead
    // and the leader's entity is not our own; else both are cleared.
    const uint32_t mode = field<uint32_t>(actor, 0x278);
    if (mode == 1 || mode == 7) {
        if (field<uint8_t*>(actor, 0x7c8) == actor) return;
        uint8_t* leader = actor;
        for (;;) {
            leader = field<uint8_t*>(leader, 0x7c8);
            if (!leader) break;
            if (field<uint8_t*>(leader, 0x7c8) != leader) continue;
            int32_t until = field<int32_t>(leader, 0x7c0);
            const int32_t now = level_time();
            if (now >= field<int32_t>(field<const uint8_t*>(leader, 0), 0x328)) until += title_global<int32_t>(0x008dda9c);
            if (now < until && field<uint32_t>(actor, 0) != field<uint32_t>(leader, 0x7cc)) {
                field<uint8_t*>(actor, 0x7c8) = leader;
                field<uint32_t>(actor, 0x7cc) = field<uint32_t>(leader, 0x7cc);
                return;
            }
            break;
        }
    }
    field<uint32_t>(actor, 0x7c8) = 0;
    field<uint32_t>(actor, 0x7cc) = 0;
}

PLATFORM_ASM_ENTRY(Actor_ClampLethalDamage_entry, "pushl %eax\n"
                                                  "pushl %edi\n" PLATFORM_ASM_CALL(Actor_ClampLethalDamage) "addl $8, %esp\n"
                                                  "ret")

int32_t XB_CDECL Actor_ClampLethalDamage(uint8_t* actor, int32_t damage) {
    // Damage that would kill (>= the entity's health +0x350) while no flag +0xcc is set and a
    // pending transition (+0xa4[0..+0xbc]) is state 10, or the actor is in state 12: the
    // excess + 1 (marks +0xd0); else 0.
    const int32_t health = field<int32_t>(field<const uint8_t*>(actor, 0), 0x350);
    if (damage < health) return 0;
    if (field<uint32_t>(actor, 0xcc)) return 0;
    bool lethal = false;
    if (field<int32_t>(actor, 0xbc) >= 0) {
        int32_t i = 0;
        do {
            if (field<uint32_t>(actor, 0xa4 + uint32_t(i) * 4) == 10) {
                lethal = true;
                break;
            }
        } while (++i <= field<int32_t>(actor, 0xbc));
    }
    if (!lethal && !Actor_IsInState(actor, 12)) return 0;
    field<uint32_t>(actor, 0xd0) = 1;
    return damage - health + 1;
}

PLATFORM_ASM_ENTRY(Actor_ForgetSentient_entry, "pushl %edi\n"
                                               "pushl %esi\n" PLATFORM_ASM_CALL(Actor_ForgetSentient) "addl $8, %esp\n"
                                               "ret")

void XB_CDECL Actor_ForgetSentient(uint8_t* actor, uint8_t* sentient) {
    // Clears what the actor knows of the sentient (its 0x50-byte record at +0x850 + index * 0x50),
    // its enemy (Sentient_SetEnemy(null)) and the two references at +0x12a0/+0x12a4.
    const int32_t index = int32_t(uintptr_t(sentient) - uintptr_t(sentients())) / 0x60;
    field<uint32_t>(actor, 0x850 + uint32_t(index) * 0x50) = 0;
    Actor_ClearSentientSlots(actor, uint32_t(uintptr_t(sentient)));
    uint8_t* const own = field<uint8_t*>(actor, 4);
    if (field<uint8_t*>(own, 0x3c) == sentient) {
        using SetEnemy = void XB_REGS3 XB_STDCALL(uint32_t, uint32_t, uint8_t*, uint32_t);
        title_function<SetEnemy>(0x002184f0)(0, 0, own, 0);  // Sentient_SetEnemy
    }
    if (field<uint8_t*>(actor, 0x12a0) == sentient) field<uint32_t>(actor, 0x12a0) = 0;
    if (field<uint8_t*>(actor, 0x12a4) == sentient) field<uint32_t>(actor, 0x12a4) = 0;
}

PLATFORM_ASM_ENTRY(Scr_QueueAnimEndNotify_entry, "pushl %edi\n" PLATFORM_ASM_CALL(Scr_QueueAnimEndNotify) "addl $4, %esp\n"
                                                 "ret")

void XB_CDECL Scr_QueueAnimEndNotify(uint8_t* record) {
    // A 0x14-byte node from the small heap (0x0095f56c): +0xc = record +8, +8 = hash of the
    // name at record +0x18, appended to the list at 0x008dd200 (tail 0x008dd204, count
    // 0x008dd1f8); the record is marked (+0x10). A failed allocation links a null node.
    uint8_t* node = SmallHeap_Alloc(title_global<uint8_t*>(0x0095f56c), 0x14, 0);
    if (node) {
        const char* const name = field<const char*>(record, 0x18);
        const uint32_t value = field<uint32_t>(record, 8);
        const uint32_t hash = Com_HashKeyNoCase(0, name);
        field<uint32_t>(node, 0xc) = value;
        field<uint32_t>(node, 0) = 0;
        field<uint32_t>(node, 4) = 0;
        field<uint32_t>(node, 8) = hash;
        field<uint32_t>(node, 0x10) = 0;
    } else {
        node = nullptr;
    }
    field<uint32_t>(node, 0) = 0x008dd200;
    uint8_t* const tail = title_global<uint8_t*>(0x008dd204);
    field<uint8_t*>(node, 4) = tail;
    field<uint8_t*>(tail, 0) = node;
    const int32_t count = title_global<int32_t>(0x008dd1f8) + 1;
    title_global<uint8_t*>(0x008dd204) = node;
    title_global<int32_t>(0x008dd1f8) = count;
    record[0x10] = 1;
}

float* XB_REGS3 XB_CDECL Sentient_GetVelocity(float* out, uint32_t, const uint8_t* sentient) {
    // Actors: the actor's +0x2a0 vector; others: the entity's client (+0x23c) +0x10 vector.
    // Every word is read again through the sentient, as in the original.
    uint32_t* const words = reinterpret_cast<uint32_t*>(out);
    if (field<const uint8_t*>(field<const uint8_t*>(sentient, 0), 0x240)) {
        for (uint32_t i = 0; i < 3; ++i)
            words[i] = field<uint32_t>(field<const uint8_t*>(field<const uint8_t*>(sentient, 0), 0x240), 0x2a0 + i * 4);
    } else {
        for (uint32_t i = 0; i < 3; ++i)
            words[i] = field<uint32_t>(field<const uint8_t*>(field<const uint8_t*>(sentient, 0), 0x23c), 0x10 + i * 4);
    }
    return out;  // EAX is never written
}

void XB_REGS3 XB_CDECL Actor_ForgetEnemyFor(uint32_t delay, uint32_t, uint8_t* actor) {
    // Marks the current enemy's record (+0x86c) with level time + delay, then forgets it.
    const uint32_t until = uint32_t(level_time()) + delay;
    const int32_t index = int32_t(uintptr_t(field<uint8_t*>(field<uint8_t*>(actor, 4), 0x3c)) - uintptr_t(sentients())) / 0x60;
    field<uint32_t>(actor, 0x86c + uint32_t(index) * 0x50) = until;
    Actor_ForgetSentient(actor, field<uint8_t*>(field<uint8_t*>(actor, 4), 0x3c));
}

PLATFORM_ASM_ENTRY(Path_FailCache_Add_entry, "pushl 4(%esp)\n"
                                             "pushl %edi\n"
                                             "pushl %esi\n" PLATFORM_ASM_CALL(Path_FailCache_Add) "addl $12, %esp\n"
                                             "ret $4")

void XB_CDECL Path_FailCache_Add(uint8_t* cache, const float* goal, const float* start) {
    // Cache: +0 adds, +4 entries used (int16, at most 15), +8 entries of 0x1c bytes
    // (start, goal, time as float). When full, the entry whose time is farthest from now
    // (x87 |now - time| stored as float; the first on ties, entry 0 if all are 0) is reused.
    uint8_t* slot;
    const int16_t used = field<int16_t>(cache, 4);
    if (used < 15) {
        slot = cache + 8 + uint32_t(used) * 0x1c;
        field<int16_t>(cache, 4) = int16_t(used + 1);
    } else {
        float best = 0.0f;
        const float now = float(level_time());
        uint32_t index = 0;
        for (uint32_t i = 0; i < 15; ++i) {
            const float age = float(platform_x87_abs_ext(static_cast<long double>(now) -
                                                         static_cast<long double>(field<float>(cache, 0x20 + i * 0x1c))));
            if (platform_x87_greater(age, best)) {
                best = age;
                index = i;
            }
        }
        slot = cache + 8 + index * 0x1c;
    }
    for (uint32_t i = 0; i < 3; ++i) field<uint32_t>(slot, i * 4) = reinterpret_cast<const uint32_t*>(start)[i];
    for (uint32_t i = 0; i < 3; ++i) field<uint32_t>(slot, 0xc + i * 4) = reinterpret_cast<const uint32_t*>(goal)[i];
    field<float>(slot, 0x18) = float(level_time());
    ++field<int32_t>(cache, 0);
}

PLATFORM_ASM_ENTRY(Path_FailCache_Find_entry, "pushl 4(%esp)\n"
                                              "pushl %edx\n"
                                              "pushl %edi\n" PLATFORM_ASM_CALL(Path_FailCache_Find) "addl $12, %esp\n"
                                              "ret $4")

uint32_t XB_CDECL Path_FailCache_Find(uint8_t* cache, const float* goal, const float* start) {
    // 1 (and +6 counted) for an entry with exactly this goal and a start within 2 units on
    // each axis (x87 |a - b| < 2).
    const float limit = title_global<float>(0x0083eba8);  // 2
    for (int32_t i = 0; i < field<int16_t>(cache, 4); ++i) {
        const uint8_t* const entry = cache + 8 + uint32_t(i) * 0x1c;
        if (goal[0] != field<float>(entry, 0xc) || goal[1] != field<float>(entry, 0x10) ||
            goal[2] != field<float>(entry, 0x14))
            continue;
        bool near = true;
        for (uint32_t axis = 0; axis < 3 && near; ++axis) {
            const long double d = platform_x87_abs_ext(static_cast<long double>(start[axis]) -
                                                       static_cast<long double>(field<float>(entry, axis * 4)));
            near = static_cast<long double>(limit) > d;
        }
        if (near) {
            ++field<int16_t>(cache, 6);
            return 1;
        }
    }
    return 0;
}

uint32_t XB_REGS2 XB_CDECL Path_GetAllowedStances(uint32_t, const uint8_t* path) {
    // Stance mask (1 stand, 2 crouch, 4 prone) of the last node point on the path at or
    // after the start (+0x386), skipping the end point when the path's end (+0x3cc) is not
    // on it; the end point's node when none; 7 when that is no node either, 0 for a bad handle.
    const int32_t last = field<int16_t>(path, 0x380) - 1;
    int32_t i = last;
    const uint8_t* end = path + uint32_t(last) * 0x1c;
    if (field<float>(path, 0x3cc) != field<float>(end, 0) || field<float>(path, 0x3d0) != field<float>(end, 4) ||
        field<float>(path, 0x3d4) != field<float>(end, 8))
        --i;
    const int32_t start = field<int16_t>(path, 0x386);
    for (; i >= start; --i) {
        const int16_t node = field<int16_t>(path, uint32_t(i) * 0x1c + 0x18);
        if (node != 0 && node != -1) break;
    }
    if (i < start) {
        const int16_t node = field<int16_t>(path, uint32_t(last) * 0x1c + 0x18);
        if (node == 0 || node == -1) return 7;
        i = last;
    }
    const uint16_t handle = field<uint16_t>(path, uint32_t(i) * 0x1c + 0x18);
    if (handle == 0 || handle == 0xffff) return 0;
    const uint8_t* const blocks = title_global<const uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
    const uint8_t* const block = field<const uint8_t*>(blocks, 8 + uint32_t(uint16_t((handle >> 9) - 1)) * 4);
    if (!block) return 0;
    const uint8_t* const node = field<const uint8_t*>(block, 0x10) + (handle & 0x1ffu) * 0x80;
    if (!node) return 0;
    const uint16_t flags = field<uint16_t>(node, 0x2c);
    uint32_t mask = (flags & 4) ? 6 : 7;
    if (flags & 8) mask &= ~2u;
    if (flags & 0x10) mask &= ~4u;
    return mask;
}

PLATFORM_ASM_ENTRY(G_GrenadeThrowPoints_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_GrenadeThrowPoints) "addl $20, %esp\n"
                   "ret $8")

void XB_CDECL G_GrenadeThrowPoints(const float* from, const float* offset, float* release, const float* target,
                                   float* aim) {
    // release = target + offset turned into the (from -> target) heading in 2D (unit length
    // unless 0), z added; aim = release + 0.75 * (from - release).
    const float dy = target[1] - from[1];
    const float dx = target[0] - from[0];
    const float length = title_ci_sqrt_ext(static_cast<float>(dy * dy + dx * dx));
    float fx = dx, fy = dy;
    if (length != title_global<float>(0x0083eafc)) {
        const float inverse = title_global<float>(0x0083eb20) / length;
        fx = inverse * dx;
        fy = inverse * dy;
    }
    release[0] = (offset[0] * fx + target[0]) - offset[1] * fy;
    const float y = (offset[1] * fx + offset[0] * fy) + target[1];
    release[1] = y;
    const float z = target[2] + offset[2];
    release[2] = z;
    const float scale = title_global<float>(0x0083eb58);  // 0.75
    const float ax = (from[0] - release[0]) * scale;
    const float ay = (from[1] - y) * scale;
    const float az = (from[2] - z) * scale;
    aim[0] = release[0] + ax;
    aim[1] = release[1] + ay;
    aim[2] = release[2] + az;
}

PLATFORM_ASM_ENTRY(Actor_ComputeShotDir_entry, "pushl %esi\n"
                                               "pushl %eax\n" PLATFORM_ASM_CALL(Actor_ComputeShotDir) "addl $8, %esp\n"
                                               "ret")

void XB_CDECL Actor_ComputeShotDir(const float* target, float* shot) {
    // shot[0..2] = target - shot start (+0x24), normalized unless its length is 0.
    shot[0] = target[0] - shot[9];
    shot[1] = target[1] - shot[10];
    const float x = shot[0], y = shot[1];
    const float z = target[2] - shot[11];
    const float length_sq = (x * x + y * y) + z * z;
    shot[2] = z;
    const float length = title_ci_sqrt_ext(length_sq);
    if (length == title_global<float>(0x0083eafc)) return;
    const float inverse = title_global<float>(0x0083eb20) / length;
    shot[0] = shot[0] * inverse;
    shot[1] = shot[1] * inverse;
    shot[2] = shot[2] * inverse;
}

PLATFORM_ASM_ENTRY(Actor_SetQuantizedPitchYaw_entry,
                   "pushl %eax\n"  // EAX is handed back unchanged
                   "pushl 8(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_SetQuantizedPitchYaw) "addl $12, %esp\n"
                   "popl %eax\n"
                   "ret $4")

void XB_CDECL Actor_SetQuantizedPitchYaw(uint8_t* angles, float pitch, float yaw) {
    // +4 = pitch, +8 and +0xc = yaw, each through a 16-bit angle.
    const float to_short = title_global<float>(0x0083ebd8), to_angle = title_global<float>(0x0083ebd4);
    field<float>(angles, 4) = static_cast<float>(static_cast<int32_t>(pitch * to_short) & 0xffff) * to_angle;
    const float y = static_cast<float>(static_cast<int32_t>(yaw * to_short) & 0xffff) * to_angle;
    field<float>(angles, 8) = y;
    field<float>(angles, 0xc) = y;
}

namespace {

// Title callees with EBX/EDI inputs (the C frame's EBX is saved around them).
// Path_FindNearestNodeInList (0x002b0820): ECX position, EDX start node, EBX node blocks.
uint8_t* find_nearest_node_in_list(const float* position, uint8_t* node, uint32_t blocks) {
    uint32_t value = blocks;
    __asm__ volatile("pushl %%ebx\n\tmovl %%eax, %%ebx\n\tcall *%%esi\n\tpopl %%ebx"
                     : "+a"(value), "+c"(position), "+d"(node)
                     : "S"(uintptr_t{0x002b0820})
                     : "memory", "cc");
    return reinterpret_cast<uint8_t*>(uintptr_t{value});
}

// PathNode_ReleaseClaimsBy (0x002b0600): EBX sentient, one stack word (ret 4).
void release_claims_by(uint8_t* sentient, uint32_t blocks) {
    uint32_t value = uint32_t(uintptr_t(sentient)), word = blocks, scratch = 0;
    __asm__ volatile("pushl %%ebx\n\tmovl %%eax, %%ebx\n\tpushl %%edx\n\tcall *%%esi\n\tpopl %%ebx"
                     : "+a"(value), "+d"(word), "+c"(scratch)
                     : "S"(uintptr_t{0x002b0600})
                     : "memory", "cc");
}

// Path_TrimAtGoal (0x00200e90): EAX path, EDX goal, XMM4 radius, one stack float (ret 4).
uint32_t trim_at_goal(uint8_t* path, const float* goal, const float* radius, uint32_t limit) {
    uint32_t value = uint32_t(uintptr_t(path)), word = uint32_t(uintptr_t(goal)), scratch = limit;
    __asm__ volatile("movss (%%esi), %%xmm4\n\tpushl %%ecx\n\tmovl $0x00200e90, %%ecx\n\tcall *%%ecx"
                     : "+a"(value), "+d"(word), "+c"(scratch)
                     : "S"(radius)
                     : "memory", "cc", "xmm4");
    return value;
}

// The node a handle names (low 9 bits index, the rest block + 1 in g_pathNodeBlocks), or null.
const uint8_t* node_of(uint16_t handle) {
    if (handle == 0 || handle == 0xffff) return nullptr;
    const uint8_t* const blocks = title_global<const uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
    const uint8_t* const block = field<const uint8_t*>(blocks, 8 + uint32_t(uint16_t((handle >> 9) - 1)) * 4);
    if (!block) return nullptr;
    return field<const uint8_t*>(block, 0x10) + (handle & 0x1ffu) * 0x80;
}

}  // namespace

// The callbacks store stack addresses in a global (0x00929b40): the list sits at the
// original's frame address (entry - 0x800) and every title callee runs where the original's
// call puts it (frame E - 0x810 after `sub esp,0x804` and three pushes), with its registers
// and its return address written back. The C frame is below a gap; EBP is saved there too.
PLATFORM_ASM_ENTRY(Actor_GatherCoverCandidates_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %eax\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_GatherCoverCandidates) "addl $36, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $0x14")

void XB_CDECL Actor_GatherCoverCandidates(uint32_t entry, uint8_t* actor, uint32_t esi, uint32_t eax, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3, uint32_t filter, uint32_t score) {
    // Up to 256 nodes from PathNode_ListInCylinder, kept when the filter (if any) accepts them
    // and they are ours (+4 = our sentient) or free for our team and everyone (+8 + team * 4,
    // +8 times before now), else replaced by the last one; each kept node is scored
    // (float in ST0), the list sorted (qsort, 0x001fbe60) and the first ten stored at +0x7f0
    // in reverse order (count +0x818).
    const uint32_t frame = entry - 0x810;
    uint32_t* const list = reinterpret_cast<uint32_t*>(uintptr_t{entry - 0x800});
    const uint32_t list_address = entry - 0x800;
    const uint32_t self = uint32_t(uintptr_t(actor));
    const uint32_t blocks = title_global<uint32_t>(0x0095f4f4);
    using CallFloat = float XB_CDECL(uint32_t, uint32_t, const TitleRegs*, uint32_t, const uint32_t*);
    CallFloat* const call_float = reinterpret_cast<CallFloat*>(reinterpret_cast<uintptr_t>(&title_call_at_regs));
    auto returned_to = [](uint32_t slot, uint32_t address) { title_global<uint32_t>(slot) = address; };
    int32_t count;
    {
        const uint32_t args[5] = {arg2, arg3, list_address, 0x100, eax};
        const TitleRegs regs = {arg2, self, arg1, arg3, esi, blocks};
        count = int32_t(title_call_at_regs(frame, 0x002b1060, &regs, 5, args));  // PathNode_ListInCylinder
        returned_to(frame - 0x18, 0x001ffe46);
    }
    int32_t kept = 0;
    uint32_t* entry_word = list;
    while (kept < count) {
        const uint32_t node_value = entry_word[0];
        const uint8_t* const node = reinterpret_cast<const uint8_t*>(uintptr_t{node_value});
        const TitleRegs regs = {0, self, self, node_value, uint32_t(uintptr_t(entry_word)), uint32_t(count)};
        bool keep = true;
        if (filter) {
            keep = title_call_at_regs(frame, filter, &regs, 0, nullptr) != 0;
            returned_to(frame - 4, 0x001ffe7b);
        }
        if (keep && field<const uint8_t*>(node, 4) != field<const uint8_t*>(actor, 4)) {
            const int32_t now = level_time();
            const uint32_t team = field<uint32_t>(field<const uint8_t*>(actor, 4), 4);
            keep = now > field<int32_t>(node, 8 + team * 4) && now > field<int32_t>(node, 8);
        }
        if (keep) {
            reinterpret_cast<float*>(entry_word)[1] = call_float(frame, score, &regs, 0, nullptr);
            returned_to(frame - 4, filter ? 0x001ffea5 : 0x001fff12);
            ++kept;
            entry_word += 2;
        } else {
            --count;
            entry_word[0] = list[count * 2];
            entry_word[1] = list[count * 2 + 1];
        }
    }
    {
        const uint32_t args[4] = {list_address, uint32_t(count), 8, 0x001fbe60};
        const TitleRegs regs = {0, self, list_address, 0, uint32_t(uintptr_t(entry_word)), uint32_t(count)};
        title_call_at_regs(frame, 0x001ef880, &regs, 4, args);  // qsort
        returned_to(frame - 0x14, 0x001fff3a);
    }
    if (uint32_t(count) > 10) count = 10;
    for (int32_t j = 0; j < count; ++j) field<uint32_t>(actor, 0x7f0 + uint32_t(j) * 4) = list[(count - 1 - j) * 2];
    field<int32_t>(actor, 0x818) = count;
}

void XB_THISCALL Sentient_UpdateNearestNode(uint8_t* sentient) {
    // Once per level time: the node nearest to the entity's origin, searched from the current
    // one (+0x4a); cleared while 0x00986014 is set.
    if (title_global<uint8_t>(0x00986014)) {
        field<uint16_t>(sentient, 0x4a) = 0;
        return;
    }
    const int32_t now = level_time();
    if (field<int32_t>(sentient, 0x4c) == now) return;
    field<int32_t>(sentient, 0x4c) = now;
    const uint8_t* const ent = field<const uint8_t*>(sentient, 0);
    uint32_t position[3];
    for (uint32_t i = 0; i < 3; ++i) position[i] = field<uint32_t>(ent, 0x140 + i * 4);
    uint16_t* const nearest = reinterpret_cast<uint16_t*>(sentient + 0x4a);
    uint8_t* const start = PathNode_FromHandle2(nearest);
    const uint8_t* const found = find_nearest_node_in_list(reinterpret_cast<const float*>(position), start,
                                                           title_global<uint32_t>(0x0095f4f4));
    if (found) *nearest = field<uint16_t>(found, 0);
}

void XB_CDECL Sentient_Dissociate(uint8_t* sentient) {
    // Before a sentient goes away: no enemy, its claimed node (+0x48) released, its claims,
    // team 4, and every other sentient forgets it (actors; +8 goal entity handed over to the
    // old team's sentients, else cleared; +0x3c enemy).
    using SetEnemy = void XB_REGS3 XB_STDCALL(uint32_t, uint32_t, uint8_t*, uint32_t);
    title_function<SetEnemy>(0x002184f0)(0, 0, sentient, 0);  // Sentient_SetEnemy
    const uint32_t blocks = title_global<uint32_t>(0x0095f4f4);
    if (node_of(field<uint16_t>(sentient, 0x48))) {
        // The handle is looked up again for the call.
        const uint8_t* const node = node_of(field<uint16_t>(sentient, 0x48));
        Sentient_ClearNodeClaim(uint32_t(uintptr_t(node)), const_cast<uint8_t*>(node));
        field<uint16_t>(sentient, 0x48) = 0;
    }
    release_claims_by(sentient, blocks);
    const uint32_t team = field<uint32_t>(sentient, 4);
    field<uint32_t>(sentient, 4) = 4;
    for (uint8_t* other = Sentient_FirstOfTeams(0xffffffff); other; other = Sentient_NextOfTeams(other, 0xffffffff)) {
        if (other == sentient) continue;
        if (uint8_t* const actor = field<uint8_t*>(field<uint8_t*>(other, 0), 0x240)) Actor_ForgetSentient(actor, sentient);
        const uint8_t* const goal = field<const uint8_t*>(other, 8);
        if (goal && field<const uint8_t*>(goal, 0x244) == sentient) {
            if (field<uint32_t>(other, 4) == team)
                field<uint32_t>(other, 8) = field<uint32_t>(sentient, 8);
            else
                field<uint32_t>(other, 8) = 0;
        }
        if (field<uint8_t*>(other, 0x3c) == sentient) field<uint32_t>(other, 0x3c) = 0;
    }
}

// The script's stop callback, the anim thread (+0x24c) marked to end (flags 8, 0x40, 4; no
// notify, unlike Actor_StopAnimScript), then a tail jump to 0x0029aba0 with the entity's
// +0x230 in ECX, when set (the C version returns that ECX).
PLATFORM_ASM_ENTRY(Actor_AbortAnimScript_entry, "pushl %esi\n" PLATFORM_ASM_CALL(Actor_AbortAnimScript) "addl $4, %esp\n"
                                                "testl %eax, %eax\n"
                                                "jz 1f\n"
                                                "movl %eax, %ecx\n"
                                                "movl $0x0029aba0, %eax\n"
                                                "jmp *%eax\n"
                                                "1: ret")

uint32_t XB_CDECL Actor_AbortAnimScript(uint8_t* actor) {
    const uint8_t* script = field<const uint8_t*>(actor, 0x250);
    if (script && field<uint32_t>(script, 0xc)) {
        const uint8_t* ent = field<const uint8_t*>(actor, 0);
        const uint32_t number = field<uint32_t>(ent, 0x224);
        title_function<void XB_REGS3 XB_CDECL(const uint8_t*, uint32_t, const uint8_t*, uint32_t)>(
            field<uint32_t>(script, 0xc))(field<const uint8_t*>(actor, 0x250), number, ent, number);
    }
    const uint32_t handle = field<uint32_t>(actor, 0x24c);
    if (handle) {
        const uint32_t index = handle & 0xff;
        if ((handle >> 8) == title_global<uint32_t>(0x008dd22c + index * 8)) {
            if (uint8_t* thread = title_global<uint8_t*>(0x008dd228 + index * 8)) {
                const uint32_t flags = field<uint32_t>(thread, 0x10);
                field<uint32_t>(thread, 0x10) = flags | 8;
                field<uint32_t>(thread, 0x10) = flags | 0x48;
                field<uint32_t>(thread, 0x10) = flags | 0x4c;
            }
        }
        field<uint32_t>(actor, 0x24c) = 0;
    }
    return field<uint32_t>(field<const uint8_t*>(actor, 0), 0x230);
}

PLATFORM_ASM_ENTRY(Actor_TrimPathAtGoal_entry, "pushl %edi\n" PLATFORM_ASM_CALL(Actor_TrimPathAtGoal) "addl $4, %esp\n"
                                               "ret")

uint32_t XB_CDECL Actor_TrimPathAtGoal(uint8_t* actor) {
    // Path_TrimAtGoal(goal +0xc, radius +0x18 of the sentient, 80); when it fails the path
    // is dropped (trim at the entity's origin, no points, +0x7c4 cleared). EAX = its result or 0.
    const uint8_t* const sentient = field<const uint8_t*>(actor, 4);
    uint8_t* const path = actor + 0x3c0;
    const uint32_t trimmed = trim_at_goal(path, reinterpret_cast<const float*>(sentient + 0xc),
                                          reinterpret_cast<const float*>(sentient + 0x18), 0x42a00000);  // 80.0f
    if (trimmed) return trimmed;
    Path_AccumulateTrim(path, reinterpret_cast<const float*>(field<uint8_t*>(actor, 0) + 0x140));
    field<uint16_t>(path, 0x380) = 0;
    field<uint16_t>(path, 0x382) = 0;
    field<uint32_t>(actor, 0x7c4) = 0;
    return 0;
}

uint32_t XB_CDECL BadPlace_ClearCrossingPaths() {
    // Every actor whose path (+0x3c0, points +0x740 > 0) crosses a bad place for its team drops
    // it (as Actor_TrimPathAtGoal's failure). EAX = 0 (tail-called from state Start functions).
    for (uint8_t* actor = Actor_FirstOfTeams(0xffffffff); actor; actor = Actor_NextOfTeams(actor, 0xffffffff)) {
        if (field<int16_t>(actor, 0x740) <= 0) continue;
        uint8_t* const path = actor + 0x3c0;
        if (!Path_CrossesBadPlace(path, field<uint32_t>(field<const uint8_t*>(actor, 4), 4))) continue;
        Path_AccumulateTrim(path, reinterpret_cast<const float*>(field<uint8_t*>(actor, 0) + 0x140));
        field<uint16_t>(path, 0x380) = 0;
        field<uint16_t>(path, 0x382) = 0;
        field<uint32_t>(actor, 0x7c4) = 0;
    }
    return 0;
}

PLATFORM_ASM_ENTRY(Actor_GetGoalRadius_entry, "pushl 4(%esp)\n"
                                              "pushl %edi\n" PLATFORM_ASM_CALL(Actor_GetGoalRadius) "fstps (%esp)\n"
                                              "movss (%esp), %xmm0\n"
                                              "addl $8, %esp\n"
                                              "ret $4")

float XB_CDECL Actor_GetGoalRadius(const uint8_t* actor, uint32_t exact) {
    // The radius the actor's goal counts as reached in (XMM0): the sentient's +0x18, at least 30
    // while making way (+0x1390) with an enemy; 0.5 when following an entity's sentient that is not
    // of the enemy team with a valid goal node (+0x50) and +0x7de clear; else +0x18 for exact == 1,
    // otherwise the actor's +0x79c.
    const uint8_t* const sentient = field<const uint8_t*>(actor, 4);
    if (field<uint32_t>(sentient, 0x3c) && field<uint32_t>(actor, 0xa4) != 5) {
        if (!field<uint32_t>(actor, 0x1390)) return field<float>(sentient, 0x18);
        const float minimum = title_global<float>(0x0083ee38);  // 30
        if (field<float>(sentient, 0x18) > minimum) return field<float>(sentient, 0x18);
        return minimum;
    }
    const uint8_t* const goal = field<const uint8_t*>(sentient, 8);
    if (goal && field<const uint8_t*>(goal, 0x244)) {
        const uint32_t enemy_team[5] = {0, 2, 1, 0, 0};
        if (field<uint32_t>(field<const uint8_t*>(goal, 0x244), 4) != enemy_team[field<uint32_t>(sentient, 4)] &&
            (PathNode_HandleIsValid(reinterpret_cast<const uint16_t*>(sentient + 0x50)) & 0xff) && !actor[0x7de])
            return title_global<float>(0x0083eb24);  // 0.5
    }
    if (exact == 1) return field<float>(sentient, 0x18);
    return field<float>(actor, 0x79c);
}

void XB_REGS1 XB_CDECL Actor_DropClaimedNode(uint8_t* actor) {
    // With a claimed node (+0x48 of the sentient): the NodeClaimRevoked callbacks (slot 8) of the
    // state stack from the top until one returns nonzero; then a valid goal node (+0x50) is
    // dropped: Sentient_SetGoalNode with the node pointer's low word cleared (handle 0).
    if (node_of(field<uint16_t>(field<const uint8_t*>(actor, 4), 0x48))) {
        for (int32_t i = field<int32_t>(actor, 0x20); i >= 0; --i) {
            const uint32_t state = field<uint32_t>(actor, 8 + uint32_t(i) * 4);
            const uint32_t revoked = title_global<uint32_t>(0x00830c48 + state * 9 * 4);
            if (revoked && title_function<uint32_t XB_THISCALL(uint8_t*)>(revoked)(actor)) break;
        }
    }
    uint8_t* const sentient = field<uint8_t*>(actor, 4);
    if (const uint8_t* goal = node_of(field<uint16_t>(sentient, 0x50)))
        Sentient_SetGoalNode(sentient, uint32_t(uintptr_t(goal)) & 0xffff0000u);
}

PLATFORM_ASM_ENTRY(Sentient_Free_entry, "pushl %eax\n" PLATFORM_ASM_CALL(Sentient_Free) "addl $4, %esp\n"
                                        "ret")

void XB_CDECL Sentient_Free(uint8_t* sentient) {
    // References to the entity cleared, dissociated, the entity's sentient link (+0x244) and the
    // sentient's script fields (0x0082fdc0) released; +0x5c = the count at 0x0095f420 minus one.
    G_ClearEntityReferences(uint32_t(uintptr_t(sentient)), field<uint8_t*>(sentient, 0));
    Sentient_Dissociate(sentient);
    field<uint32_t>(field<uint8_t*>(sentient, 0), 0x244) = 0;
    G_FreeFieldStrings(&title_global<const uint32_t>(0x0082fdc0), sentient);
    const int32_t count = title_global<int32_t>(0x0095f420) - 1;
    field<int32_t>(sentient, 0x5c) = count;
    title_global<int32_t>(0x0095f420) = count;
}

// The open list's head is a path node in the original's frame (entry - 0x80): heap nodes keep
// its address (start's parent, the first open node's prev), so it sits at that address and the
// title callees run where the original's calls put them (frame E - 0x94), return addresses
// written back. The C frame is below a gap.
PLATFORM_ASM_ENTRY(Path_AStarSearch_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl 32(%ebp)\n"
                   "pushl 28(%ebp)\n"
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Path_AStarSearch) "addl $36, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Path_AStarSearch(uint32_t entry, uint8_t* path, uint32_t team, uint32_t arg2, uint8_t* start,
                                   const float* goal, uint32_t arg5, uint32_t arg6, uint8_t* const* goal_node) {
    // A* from `start` over the node links (+0x64, 12 bytes each, count +0x24; skipped when
    // blocked for the team (+4 + team) or the target block is gone) until *goal_node is taken
    // from the open list. Node fields: +0x68 search frame, +0x6c/+0x70 open-list next/prev,
    // +0x74 parent, +0x78 cost so far, +0x7c estimate (2D distance to `goal`). The open list is
    // sorted by cost + estimate; inserting past 50000 entries gives up. Path_BuildFromNodes
    // (ECX path, EDX team) writes the path when one is asked for; else 1. 0 when none found.
    const uint32_t frame = entry - 0x94;
    uint8_t* const head = reinterpret_cast<uint8_t*>(uintptr_t{entry - 0x80});
    auto returned_to = [entry](uint32_t address) { title_global<uint32_t>(entry - 0x98) = address; };
    field<uint16_t>(head, 0) = 0;
    field<uint32_t>(head, 4) = 0;
    field<uint32_t>(head, 8) = 0;
    field<uint16_t>(head, 0x24) = 0;
    field<uint16_t>(head, 0x26) = 0;
    field<uint32_t>(head, 0x14) = 0;
    field<uint32_t>(head, 0x10) = 0;
    field<uint32_t>(head, 0xc) = 0;
    field<uint32_t>(head, 0x20) = 0;
    field<uint32_t>(head, 0x1c) = 0;
    field<uint32_t>(head, 0x18) = 0;
    {
        const TitleRegs regs = {0, 0, 0, 0, uint32_t(uintptr_t(head + 0x28)), 0};
        title_call_at_regs(frame, 0x0002bd50, &regs, 0, nullptr);  // PathNode_Construct
        returned_to(0x001fdad4);
    }
    const int32_t search = title_global<int32_t>(0x00985fdc) + 1;  // g_pathSearchFrame
    title_global<int32_t>(0x00985fdc) = search;
    field<int32_t>(start, 0x68) = search;
    field<uint8_t*>(start, 0x74) = head;
    field<uint32_t>(head, 0x68) = 0;
    field<uint32_t>(head, 0x70) = 0;
    field<uint32_t>(head, 0x74) = 0;
    field<float>(head, 0x78) = 0.0f;
    field<float>(head, 0x7c) = 0.0f;
    field<uint32_t>(start, 0x6c) = 0;
    field<uint8_t*>(start, 0x70) = head;
    field<float>(start, 0x78) = 0.0f;
    field<uint8_t*>(head, 0x6c) = start;
    auto release_all = [&](uint32_t returns) {
        for (uint32_t offset = 0x3c; offset >= 0x30; offset -= 4) {
            const TitleRegs regs = {0, 0, uint32_t(uintptr_t(head + offset)), 0, 0, 0};
            title_call_at_regs(frame, 0x0011d640, &regs, 0, nullptr);  // ScriptString_Release
            returned_to(returns + (0x3c - offset) / 4 * 9);
        }
    };
    uint8_t* current = start;
    for (;;) {
        if (current == *goal_node) break;
        uint8_t* const next = field<uint8_t*>(current, 0x6c);
        field<uint8_t*>(head, 0x6c) = next;
        if (next) field<uint8_t*>(next, 0x70) = head;
        for (int32_t i = 0; i < field<int16_t>(current, 0x24); ++i) {
            const uint8_t* const link = field<const uint8_t*>(current, 0x64) + i * 0xc;
            if (link[4 + team] != 0) continue;
            const uint8_t* const blocks = title_global<const uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
            const uint16_t handle = field<uint16_t>(link, 0);
            if (!field<const uint8_t*>(blocks, 8 + uint32_t(uint16_t((handle >> 9) - 1)) * 4)) continue;
            uint8_t* neighbor = nullptr;
            if (handle != 0 && handle != 0xffff) {
                const uint8_t* const block = field<const uint8_t*>(blocks, 8 + uint32_t(uint16_t((handle >> 9) - 1)) * 4);
                if (block) neighbor = field<uint8_t*>(block, 0x10) + (handle & 0x1ffu) * 0x80;
            }
            const float cost = field<float>(link, 8) + field<float>(current, 0x78);
            if (field<int32_t>(neighbor, 0x68) == title_global<int32_t>(0x00985fdc)) {
                if (cost >= field<float>(neighbor, 0x78)) continue;
                if (uint8_t* const prev = field<uint8_t*>(neighbor, 0x70)) {
                    field<uint8_t*>(prev, 0x6c) = field<uint8_t*>(neighbor, 0x6c);
                    if (uint8_t* const after = field<uint8_t*>(neighbor, 0x6c))
                        field<uint8_t*>(after, 0x70) = field<uint8_t*>(neighbor, 0x70);
                }
            } else {
                field<int32_t>(neighbor, 0x68) = title_global<int32_t>(0x00985fdc);
                // The 2D distance to the goal, all on the x87.
                const long double dx = static_cast<long double>(field<float>(neighbor, 0x44)) - goal[0];
                const long double dy = static_cast<long double>(field<float>(neighbor, 0x48)) - goal[1];
                field<float>(neighbor, 0x7c) = platform_x87_sqrt(static_cast<float>(dx * dx + dy * dy));
            }
            const float total = field<float>(neighbor, 0x7c) + cost;
            field<uint8_t*>(neighbor, 0x74) = current;
            field<float>(neighbor, 0x78) = cost;
            uint8_t* at = head;
            if (field<uint8_t*>(head, 0x6c)) {
                int32_t steps = 0;
                for (;;) {
                    uint8_t* const other = field<uint8_t*>(at, 0x6c);
                    if (field<float>(other, 0x7c) + field<float>(other, 0x78) >= total) break;
                    if (++steps >= 50000) {
                        release_all(0x001fdcd2);
                        return 0;
                    }
                    at = other;
                    if (!field<uint8_t*>(other, 0x6c)) break;
                }
            }
            field<uint8_t*>(neighbor, 0x70) = at;
            field<uint8_t*>(neighbor, 0x6c) = field<uint8_t*>(at, 0x6c);
            field<uint8_t*>(at, 0x6c) = neighbor;
            if (uint8_t* const after = field<uint8_t*>(neighbor, 0x6c)) field<uint8_t*>(after, 0x70) = neighbor;
        }
        field<uint32_t>(current, 0x70) = 0;
        current = field<uint8_t*>(head, 0x6c);
        if (!current) {
            release_all(0x001fdcd2);
            return 0;
        }
    }
    if (!path) {
        release_all(0x001fdd0e);
        return 1;
    }
    uint32_t result;
    {
        const uint32_t args[6] = {arg2, uint32_t(uintptr_t(goal)), uint32_t(uintptr_t(start)), uint32_t(uintptr_t(current)),
                                  arg5, arg6};
        const TitleRegs regs = {arg2, 0, uint32_t(uintptr_t(path)), team, arg5, uint32_t(uintptr_t(current))};
        result = title_call_at_regs(frame, 0x002144f0, &regs, 6, args);  // Path_BuildFromNodes
        title_global<uint32_t>(frame - 0x1c) = 0x001fdd67;
    }
    release_all(0x001fdd72);
    return result;
}

PLATFORM_ASM_ENTRY(AI_EventTypeFromName_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(AI_EventTypeFromName) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL AI_EventTypeFromName(const char* name) {
    // The index of `name` in the 19 AI event names (0x008fedb0, 8 bytes each), else 0.
    for (uint32_t i = 0; i < 0x13; ++i) {
        const char* const entry = title_global<const char*>(0x008fedb0 + i * 8);
        if (entry && name && Q_stricmpn(name, entry, 99999) == 0) return i;
    }
    return 0;
}

namespace {

// ((rand / 32768) * 2 - 1) * scale: a random factor in [-scale, scale).
float random_spread(float scale) {
    const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
    return ((static_cast<float>(r) / title_global<float>(0x0083ebc8)) * title_global<float>(0x0083eba8) -
            title_global<float>(0x0083eb20)) *
           scale;
}

// The turret-relative direction to `point` (point - origin +0x24), normalized unless 0.
void turret_aim_dir(const float* point, const uint8_t* turret, float* dir) {
    const float dz = point[2] - field<float>(turret, 0x2c);
    const float dy = point[1] - field<float>(turret, 0x28);
    const float dx = point[0] - field<float>(turret, 0x24);
    dir[0] = dx;
    dir[1] = dy;
    dir[2] = dz;
    const float length = title_ci_sqrt_ext(static_cast<long double>((dz * dz + dy * dy) + dx * dx));
    if (!(length == title_global<float>(0x0083eafc))) {
        const float inverse = title_global<float>(0x0083eb20) / length;
        dir[0] = inverse * dx;
        dir[1] = inverse * dy;
        dir[2] = inverse * dz;
    }
}

// Normalizes `out` in place (the length from the values just stored) unless it is 0.
void normalize_stored(float* out, float x, float y, float z) {
    const float length = title_ci_sqrt_ext(static_cast<long double>((x * x + y * y) + z * z));
    if (!(length == title_global<float>(0x0083eafc))) {
        const float inverse = title_global<float>(0x0083eb20) / length;
        out[0] = out[0] * inverse;
        out[1] = inverse * out[1];
        out[2] = out[2] * inverse;
    }
}

}  // namespace

PLATFORM_ASM_ENTRY(Turret_AimDirMiss_entry,
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Turret_AimDirMiss) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Turret_AimDirMiss(const float* target, const uint8_t* turret, float* out) {
    // EBX target, ESI turret, EDI out: a shot that misses - aimed past the target's side
    // (cross of the aim direction and the turret's +0x18 axis, a random sign) by the miss
    // distance (0x008ff044, sqrt(2 * r * r * 3) of 0x0098a554 while it is 6969), pulled back
    // along the aim by 0x00984d00 + 1, jittered along the axis by up to 12. Unit vector.
    if (title_global<float>(0x008ff044) == title_global<float>(0x0083ee24)) {
        const float radius = title_global<float>(0x0098a554);
        long double product = static_cast<long double>(radius) * radius;
        product = product + product;
        product = product * title_global<float>(0x0083ec9c);  // 3
        title_global<float>(0x008ff044) = title_ci_sqrt_ext(static_cast<float>(product));
    }
    float aim[3];
    turret_aim_dir(target, turret, aim);
    const float u0 = field<float>(turret, 0x18), u1 = field<float>(turret, 0x1c), u2 = field<float>(turret, 0x20);
    const float s0 = aim[1] * u2 - aim[2] * u1;
    const float s1 = u0 * aim[2] - aim[0] * u2;
    const float s2 = aim[0] * u1 - u0 * aim[1];
    float n0 = s0, n1 = s1, n2 = s2;
    const float length = title_ci_sqrt_ext(static_cast<long double>((s2 * s2 + s1 * s1) + s0 * s0));
    if (!(length == title_global<float>(0x0083eafc))) {
        const float inverse = title_global<float>(0x0083eb20) / length;
        n0 = inverse * s0;
        n1 = inverse * s1;
        n2 = inverse * s2;
    }
    uint32_t seed = title_global<uint32_t>(0x008db488) * 0x10dcd + 1;
    title_global<uint32_t>(0x008db488) = seed;
    const float fraction = platform_x87_int_mul(int32_t(seed & 0xffff), 1.0f);
    const int32_t sign =
        static_cast<long double>(fraction) / title_global<float>(0x0083eb40) > title_global<double>(0x0083ecb0) ? 1 : -1;
    const float back = 0.0f - (title_global<float>(0x00984d00) + title_global<float>(0x0083eb20));
    const float b0 = back * aim[0] + target[0];
    const float b1 = back * aim[1] + target[1];
    const float b2 = back * aim[2] + target[2];
    const float side = static_cast<float>(sign) * title_global<float>(0x008ff044);
    const float p0 = n0 * side + b0;
    const float p1 = n1 * side + b1;
    const float p2 = n2 * side + b2;
    const float jitter = random_spread(title_global<float>(0x0083eccc));  // 12
    const float a0 = field<float>(turret, 0x18), a1 = field<float>(turret, 0x1c), a2 = field<float>(turret, 0x20);
    const float x = (a0 * jitter + p0) - field<float>(turret, 0x24);
    out[0] = x;
    const float y = (a1 * jitter + p1) - field<float>(turret, 0x28);
    out[1] = y;
    const float z = (a2 * jitter + p2) - field<float>(turret, 0x2c);
    out[2] = z;
    normalize_stored(out, x, y, z);
}

PLATFORM_ASM_ENTRY(Turret_AimDirHit_entry,
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Turret_AimDirHit) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Turret_AimDirHit(const float* target, const uint8_t* turret, float* out) {
    // EBX target, ESI turret, EDI out: a shot at the target, jittered by up to 8 along the
    // side (cross of the aim direction and the turret's +0x18 axis) and up to 8 along the axis.
    float aim[3];
    turret_aim_dir(target, turret, aim);
    const float u0 = field<float>(turret, 0x18), u1 = field<float>(turret, 0x1c), u2 = field<float>(turret, 0x20);
    float s0 = u2 * aim[1] - u1 * aim[2];
    float s1 = u0 * aim[2] - u2 * aim[0];
    float s2 = u1 * aim[0] - u0 * aim[1];
    const float length = title_ci_sqrt_ext(static_cast<long double>((s2 * s2 + s1 * s1) + s0 * s0));
    if (!(length == title_global<float>(0x0083eafc))) {
        const float inverse = title_global<float>(0x0083eb20) / length;
        s0 = inverse * s0;
        s1 = inverse * s1;
        s2 = inverse * s2;
    }
    const float eight = title_global<float>(0x0083ed78);
    float p0 = random_spread(eight) * s0 + target[0];
    float p1 = random_spread(eight) * s1 + target[1];
    float p2 = random_spread(eight) * s2 + target[2];
    p0 = random_spread(eight) * field<float>(turret, 0x18) + p0;
    p1 = random_spread(eight) * field<float>(turret, 0x1c) + p1;
    p2 = random_spread(eight) * field<float>(turret, 0x20) + p2;
    const float x = p0 - field<float>(turret, 0x24);
    out[0] = x;
    const float y = p1 - field<float>(turret, 0x28);
    out[1] = y;
    const float z = p2 - field<float>(turret, 0x2c);
    out[2] = z;
    const float length2 = title_ci_sqrt_ext(static_cast<long double>((out[0] * out[0] + y * y) + z * z));
    if (!(length2 == title_global<float>(0x0083eafc))) {
        const float inverse = title_global<float>(0x0083eb20) / length2;
        out[0] = out[0] * inverse;
        out[1] = out[1] * inverse;
        out[2] = inverse * out[2];
    }
}

PLATFORM_ASM_ENTRY(Path_TrimAtGoal_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm4, (%esp)\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Path_TrimAtGoal) "addl $16, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Path_TrimAtGoal(uint8_t* path, const float* goal, float radius, float height) {
    // EAX path, EDX goal, XMM4 radius, stack height. 0 when the goal is outside the cylinder
    // (radius, height) around the path's end (+0x3cc); else the path (points of 0x1c bytes:
    // position, 2D direction +0xc, length +0x14, count +0x380) is cut where it enters the
    // cylinder: the last point outside it is found from the end, the fraction along its
    // segment where the cylinder starts is solved, the passed points dropped and the first
    // point moved there (lengths, lookahead +0x390, +0x3b8 distance and the 2-point case
    // fixed up). 1 then (also with no points).
    const float r2 = radius * radius;
    const int16_t count = field<int16_t>(path, 0x380);
    if (count == 0) return 1;
    const float zero = 0.0f;
    const float dy = goal[1] - field<float>(path, 0x3d0);
    const float dx = goal[0] - field<float>(path, 0x3cc);
    const float dz = goal[2] - field<float>(path, 0x3d4);
    if (dy * dy + dx * dx > r2) return 0;
    const float h2 = height * height;
    if (dz * dz > h2) return 0;
    int32_t i = count - 2;
    if (i < 0) return 1;
    float ex, ey, ez, e;
    for (;;) {
        const float* const point = reinterpret_cast<const float*>(path + i * 0x1c);
        ex = goal[0] - point[0];
        ey = goal[1] - point[1];
        ez = goal[2] - point[2];
        e = (ey * ey + ex * ex) - r2;
        if (!(zero >= e)) break;
        if (!(h2 >= ez * ez)) break;
        if (--i < 0) return 1;
    }
    uint8_t* const segment = path + i * 0x1c;
    float t;
    if (e > zero) {
        const float small = title_global<float>(0x0083eef4);  // -0.005
        const float along = field<float>(segment, 0x10) * ey + field<float>(segment, 0xc) * ex;
        float disc = along * along - e;
        if (zero > disc && disc > small) disc = zero;
        const float root = platform_x87_sqrt(disc);
        const float inverse = title_global<float>(0x0083eb20) / field<float>(segment, 0x14);
        t = ((zero - along) - root) * inverse;
        if (small > t) t = (root - along) * inverse;
    } else {
        t = zero;
    }
    float c;
    bool vertical = true;
    if (ez > height)
        c = ez - height;
    else if (zero - height > ez)
        c = ez + height;
    else
        vertical = false;
    if (vertical) {
        c = c / (field<float>(segment, 0x24) - field<float>(segment, 8));
        if (c > t) t = c;
    }
    if (title_global<float>(0x0083eb88) >= t) {  // 0.005
        Path_DropPassedPoints(uint32_t(i), path);
        return 1;
    }
    if (field<int16_t>(path, 0x388) == i) {
        const float left = field<float>(path, 0x3b8) - field<float>(segment, 0x14) * t;
        field<float>(path, 0x3b8) = left;
        if (zero > left) field<float>(path, 0x3b8) = zero;
    }
    if (t >= title_global<float>(0x0083eef0) || field<int16_t>(path, 0x386) - 1 == i) {  // 0.995
        Path_DropPassedPoints(uint32_t(i + 1), path);
        return 1;
    }
    Path_DropPassedPoints(uint32_t(i), path);
    const int16_t left_points = field<int16_t>(path, 0x380);
    field<uint16_t>(path, 0x18) = 0;
    if (left_points <= 1) return 1;
    float* const first = reinterpret_cast<float*>(path);
    const float* const second = first + 7;
    const float ax = second[0] - first[0];
    const float ay = second[1] - first[1];
    const float az = second[2] - first[2];
    const float nx = t * ax + first[0];
    first[0] = nx;
    const float ny = t * ay + first[1];
    const float nz = t * az + first[2];
    first[1] = ny;
    first[2] = nz;
    const float distance = Vec2Distance(second, 0, first);
    const float removed = static_cast<float>(static_cast<long double>(first[5]) - static_cast<long double>(distance));
    first[5] = distance;
    field<float>(path, 0x390) = nx;
    field<float>(path, 0x394) = ny;
    field<float>(path, 0x398) = nz;
    field<uint32_t>(path, 0x3bc) &= ~4u;
    field<uint32_t>(path, 0x3d8) = 0;
    if (field<int16_t>(path, 0x388) == 0) {
        const float left = field<float>(path, 0x3b8) - removed;
        field<float>(path, 0x3b8) = left;
        if (0.0f > left) field<float>(path, 0x3b8) = 0.0f;
    }
    if (left_points == 2) {
        const float rest = Vec2Distance(reinterpret_cast<const float*>(path + 0x3cc), 0, first);
        field<float>(path, 0x3c8) = rest;
        if (static_cast<long double>(rest) == static_cast<long double>(title_global<float>(0x0083eafc))) {
            field<float>(path, 0x3d4) = nz;
            field<int16_t>(path, 0x380) = 1;
            field<uint16_t>(path, 0x384) = 0;
            field<uint16_t>(path, 0x388) = 0;
            field<float>(path, 0x3b8) = 0.0f;
        }
    }
    return 1;
}


namespace {

// The entity behind a handle (index & 0xfff below 0x540, generation >> 12; table 0x0097a968), or null.
uint8_t* entity_of_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return title_global<uint8_t*>(0x0097a968 + index * 8);
}

void free_entity(uint8_t* ent) {
    title_function<void XB_CDECL(uint8_t*, uint32_t)>(0x002be420)(ent, 0);  // G_FreeEntity
}

}  // namespace

void XB_CDECL SP_info_grenade_hint(uint8_t* ent) {
    // Spawn: the entity's origin becomes a grenade hint (0x0095ee20, count 0x00985fd4, at most
    // 128), then the entity is freed.
    uint32_t count = title_global<uint32_t>(0x00985fd4);
    if (count >= 0x80) {
        title_function<void XB_CDECL(const char*, int32_t)>(0x00248af0)(&title_global<const char>(0x0083d0cc), 0x80);
        count = title_global<uint32_t>(0x00985fd4);
    }
    const uint32_t hint = 0x0095ee20 + count * 12;
    title_global<uint32_t>(hint) = field<uint32_t>(ent, 0x140);
    title_global<uint32_t>(hint + 4) = field<uint32_t>(ent, 0x144);
    title_global<uint32_t>(hint + 8) = field<uint32_t>(ent, 0x148);
    title_global<uint32_t>(0x00985fd4) = count + 1;
    free_entity(ent);
}

uint32_t XB_REGS3 XB_CDECL Actor_IsAtGoal(uint32_t exact, uint32_t, uint8_t* actor) {
    // 1 when the entity is within the goal's height (80) and radius (Actor_GetGoalRadius) and,
    // for a path that ends at the goal, the path is no longer than twice the goal radius +0x18.
    const float radius = Actor_GetGoalRadius(actor, exact);
    const uint8_t* const ent = field<const uint8_t*>(actor, 0);
    const uint8_t* const sentient = field<const uint8_t*>(actor, 4);
    const float dz = field<float>(ent, 0x148) - field<float>(sentient, 0x14);
    const float dx = field<float>(ent, 0x140) - field<float>(sentient, 0xc);
    const float dy = field<float>(ent, 0x144) - field<float>(sentient, 0x10);
    if (dz * dz > title_global<float>(0x0083eb6c)) return 0;  // 6400
    if (dy * dy + dx * dx > radius * radius) return 0;
    if (field<int16_t>(actor, 0x740) <= 0) return 1;
    const float ex = field<float>(actor, 0x750), ey = field<float>(actor, 0x754), ez = field<float>(actor, 0x758);
    if (ex == field<float>(sentient, 0xc) && ey == field<float>(sentient, 0x10) && ez == field<float>(sentient, 0x14)) {
        if (Path_IsLongerThan(actor + 0x3c0, field<float>(sentient, 0x18) * title_global<float>(0x0083eba8))) return 0;
    }
    return 1;
}

void XB_CDECL Actor_FreeAllCorpses() {
    // Frees every corpse entity (handles in g_actorCorpses 0x009881c8, 0x1c bytes each, count 0x00985418).
    for (int32_t i = 0; i < title_global<int32_t>(0x00985418); ++i) {
        const uint32_t handle = title_global<uint32_t>(0x009881c8 + uint32_t(i) * 0x1c);
        if (handle) free_entity(entity_of_handle(handle));
    }
}

PLATFORM_ASM_ENTRY(Sentient_SetGoalPos_entry, "pushl 4(%esp)\n"
                                              "pushl %ecx\n" PLATFORM_ASM_CALL(Sentient_SetGoalPos) "addl $8, %esp\n"
                                              "ret $4")

void XB_CDECL Sentient_SetGoalPos(const float* position, uint8_t* sentient) {
    // ECX position, stack sentient: no goal yaw (+0x20 = -1); a new goal position (+0xc) drops
    // the goal entity (+8) and node, and an actor's path (trimmed at the entity's origin) and
    // re-checks whether it is at the goal (+0x7df).
    field<uint32_t>(sentient, 0x20) = title_global<uint32_t>(0x0083eb34);  // -1.0f
    if (position[0] == field<float>(sentient, 0xc) && position[1] == field<float>(sentient, 0x10) &&
        position[2] == field<float>(sentient, 0x14))
        return;
    const uint32_t* const words = reinterpret_cast<const uint32_t*>(position);
    field<uint32_t>(sentient, 0xc) = words[0];
    field<uint32_t>(sentient, 0x10) = words[1];
    field<uint32_t>(sentient, 0x14) = words[2];
    field<uint32_t>(sentient, 8) = 0;
    // The handle word is the argument slot with its low half zeroed.
    Sentient_SetGoalNode(sentient, uint32_t(uintptr_t(sentient)) & 0xffff0000u);
    uint8_t* const actor = field<uint8_t*>(field<uint8_t*>(sentient, 0), 0x240);
    if (!actor) return;
    uint8_t* const path = actor + 0x3c0;
    Path_AccumulateTrim(path, reinterpret_cast<const float*>(field<uint8_t*>(actor, 0) + 0x140));
    field<uint16_t>(path, 0x380) = 0;
    field<uint16_t>(path, 0x382) = 0;
    field<uint32_t>(actor, 0x7c4) = 0;
    field<uint32_t>(actor, 0x834) = 0;
    field<uint8_t>(actor, 0x7df) = uint8_t(Actor_IsAtGoal(1, 0, actor));
}

// Actor_RegisterSuppression runs at the original's ESP (entry - 0x10, one word), its return
// address written back; the C frame is below a gap.
PLATFORM_ASM_ENTRY(Actor_EventBullet_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n"
                   "pushl %ecx\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_EventBullet) "addl $24, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $8")

void XB_CDECL Actor_EventBullet(uint32_t entry, uint8_t* actor, uint32_t end, const uint8_t* attacker, uint32_t start,
                                uint32_t no_suppression) {
    // ECX actor, EAX end, EDI attacker entity, stack start and a flag (ret 8): a bullet line
    // passed by. Unless the actor ignores events or the attacker (+0x224 a live entity, +0x244
    // sentient) is none: attacked-by and its position recorded; then, without the flag,
    // suppression (Actor_RegisterSuppression) when the actor has no enemy or the enemy is
    // farther than 180.
    const uint8_t* const ent = field<const uint8_t*>(actor, 0);
    if (ent && (field<uint32_t>(ent, 0x2a4) & 0x10000000)) return;
    if (!entity_of_handle(field<uint32_t>(attacker, 0x224))) return;
    if (!field<const uint8_t*>(attacker, 0x244)) return;
    Actor_WasAttackedBy(field<const uint8_t*>(attacker, 0x244), 0, actor);
    Actor_RecordSentientPosition(field<const uint8_t*>(attacker, 0x244), actor);
    if (no_suppression) return;
    uint32_t returns = 0x0020389c;
    const uint8_t* const sentient = field<const uint8_t*>(actor, 4);
    if (sentient && field<const uint8_t*>(sentient, 0x3c)) {
        const uint8_t* const enemy = field<const uint8_t*>(field<const uint8_t*>(sentient, 0x3c), 0);
        const uint8_t* const self = field<const uint8_t*>(actor, 0);
        const float dy = field<float>(enemy, 0x144) - field<float>(self, 0x144);
        const float dx = field<float>(enemy, 0x140) - field<float>(self, 0x140);
        const float dz = field<float>(enemy, 0x148) - field<float>(self, 0x148);
        if (!((dz * dz + dy * dy) + dx * dx > title_global<float>(0x0083efa0))) return;  // 32400
        returns = 0x00203886;
    }
    const uint32_t self = uint32_t(uintptr_t(actor));
    const uint32_t args[1] = {field<uint32_t>(attacker, 0x244)};
    const TitleRegs regs = {start, end, self, 0, self, uint32_t(uintptr_t(attacker))};
    title_call_at_regs(entry - 0xc, 0x00203610, &regs, 1, args);  // Actor_RegisterSuppression
    title_global<uint32_t>(entry - 0x14) = returns;
}

void XB_REGS1 XB_CDECL Actor_Grenade_Recover(uint8_t* actor) {
    field<uint32_t>(actor, 0x12c) = 1;
    Actor_AnimStationary(actor, 0x00987bd8);  // g_animScript_grenade_recover
}

void XB_REGS1 XB_CDECL Actor_Grenade_Cower(uint8_t* actor) {
    field<uint32_t>(actor, 0x12c) = 1;
    Actor_AnimStationary(actor, 0x00987bc8);  // g_animScript_grenade_cower
}

namespace {

// PathNode_SetClaim (0x002cabf0): EAX sentient, ESI node.
void path_node_set_claim(uint8_t* sentient, uint8_t* node) {
    uint32_t eax = uint32_t(uintptr_t(sentient));
    __asm__ volatile("call *%2"
                     : "+a"(eax)
                     : "S"(node), "D"(uintptr_t{0x002cabf0})
                     : "ecx", "edx", "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7");
}

}  // namespace

PLATFORM_ASM_ENTRY(Sentient_TakeNodeClaim_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Sentient_TakeNodeClaim) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Sentient_TakeNodeClaim(const uint8_t* other, uint8_t* sentient) {
    // EAX other sentient, EBX sentient: the sentient drops its own node claim and claims the
    // other's node (+0x48), which also becomes its +0x50.
    const uint16_t handle = field<uint16_t>(other, 0x48);
    if (handle == 0 || handle == 0xffff) return;
    const uint8_t* const blocks = title_global<const uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
    const uint8_t* const block = field<const uint8_t*>(blocks, 8 + uint32_t(uint16_t((handle >> 9) - 1)) * 4);
    if (!block) return;
    uint8_t* const node = field<uint8_t*>(block, 0x10) + (handle & 0x1ffu) * 0x80;
    if (!node) return;
    uint16_t* const claimed = reinterpret_cast<uint16_t*>(sentient + 0x48);
    if (PathNode_HandleIsValid(claimed) & 0xff) {
        uint8_t* const old = PathNode_FromHandle2(claimed);
        Sentient_ClearNodeClaim(uint32_t(uintptr_t(old)), old);
    }
    path_node_set_claim(sentient, node);
    *claimed = field<uint16_t>(node, 0);
    field<uint16_t>(sentient, 0x50) = field<uint16_t>(node, 0);
}

// The state's pain callback and Actor_PushState run at the original's ESP (entry - 0x10),
// their return addresses written back; the C frame is below a gap.
PLATFORM_ASM_ENTRY(Actor_Pain_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl 28(%ebp)\n"
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_Pain) "addl $32, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Actor_Pain(uint32_t entry, uint8_t* self, uint8_t* attacker, uint32_t damage, uint32_t arg4,
                         uint32_t arg5, const float* dir, uint32_t mod) {
    // Entity pain callback: +0x374 = attacker; attacked-by for a live attacker sentient; unless
    // the sentient's +0x34 is 1: damage (+0x22c), +0x228, the hit yaw relative to the entity
    // (+0x230, whole degrees in (-180, 180]), the direction (+0x234), the hit kind (+0x240 from
    // 0x0095c0b0[mod]); the current state's pain callback (stdcall: EAX state * 9, ECX actor,
    // EDX attacker, the five words) and state 8 pushed.
    field<uint8_t*>(self, 0x374) = attacker;
    uint8_t* const actor = field<uint8_t*>(self, 0x240);
    if (entity_of_handle(field<uint32_t>(attacker, 0x224)) && field<const uint8_t*>(attacker, 0x244))
        Actor_WasAttackedBy(field<const uint8_t*>(attacker, 0x244), 0, actor);
    if (field<int32_t>(field<const uint8_t*>(actor, 4), 0x34) == 1) return;
    field<uint32_t>(actor, 0x22c) = damage;
    field<uint32_t>(actor, 0x228) = arg5;
    const float yaw = vectoyaw(dir);
    const float turns = (yaw - field<float>(self, 0x154)) * title_global<float>(0x0083ebd8);
    const int32_t quantized = static_cast<int32_t>(turns) & 0xffff;
    float angle = static_cast<float>(quantized) * title_global<float>(0x0083ebd4);
    if (angle > title_global<float>(0x0083eb60)) angle = angle - title_global<float>(0x0083ebcc);  // 180, 360
    field<int32_t>(actor, 0x230) = static_cast<int32_t>(angle);
    const uint32_t* const words = reinterpret_cast<const uint32_t*>(dir);
    field<uint32_t>(actor, 0x234) = words[0];
    field<uint32_t>(actor, 0x238) = words[1];
    field<uint32_t>(actor, 0x23c) = words[2];
    const uint32_t depth = field<uint32_t>(actor, 0x20);
    field<uint32_t>(actor, 0x240) = title_global<uint32_t>(0x0095c0b0 + mod * 4);
    const uint32_t state = field<uint32_t>(actor, 8 + depth * 4);
    const uint32_t self_actor = uint32_t(uintptr_t(actor));
    const uint32_t frame = entry - 0x10;
    {
        const uint32_t args[5] = {damage, arg4, arg5, uint32_t(uintptr_t(dir)), mod};
        const TitleRegs regs = {state * 9, uint32_t(uintptr_t(attacker)), self_actor, uint32_t(uintptr_t(attacker)),
                                self_actor, uint32_t(uintptr_t(dir))};
        title_call_at_regs(frame, title_global<uint32_t>(0x00830c44 + state * 36), &regs, 5, args);
        title_global<uint32_t>(frame - 0x18) = 0x00204a43;
    }
    {
        const TitleRegs regs = {0, uint32_t(uintptr_t(attacker)), self_actor, 0, self_actor, 8};
        title_call_at_regs(frame, 0x00202770, &regs, 0, nullptr);  // Actor_PushState
        title_global<uint32_t>(frame - 4) = 0x00204a4d;
    }
}

uint32_t XB_CDECL Actor_FreeFarthestCorpse() {
    // Frees the corpse farthest from the player (entity with +0x260 == "player"); returns its
    // index. A free slot (handle 0) on the way ends the search: its index is returned, nothing freed.
    const uint8_t* const player = G_FindEntityByIntField(&title_global<const uint8_t>(0x0097a8c0), 0x260,
                                                         title_global<int32_t>(0x0097a75c));  // scr_const_player
    const uint8_t* const origin = field<const uint8_t*>(field<const uint8_t*>(player, 0x244), 0) + 0x140;
    const float px = field<float>(origin, 0), py = field<float>(origin, 4), pz = field<float>(origin, 8);
    float farthest = title_global<float>(0x0083eb34);  // -1
    int32_t best = 0;
    for (int32_t i = 0; i < title_global<int32_t>(0x00985418); ++i) {
        const uint32_t handle = title_global<uint32_t>(0x009881c8 + uint32_t(i) * 0x1c);
        if (!handle) return uint32_t(i);
        const uint8_t* const ent = entity_of_handle(handle);
        // Packed: all four lanes (+0x14c minus x as the fourth) are computed.
        const float d0 = field<float>(ent, 0x140) - px, d1 = field<float>(ent, 0x144) - py;
        const float d2 = field<float>(ent, 0x148) - pz, d3 = field<float>(ent, 0x14c) - px;
        const float s0 = d0 * d0, s1 = d1 * d1, s2 = d2 * d2, s3 = d3 * d3;
        platform_keep(s3);
        const float distance = (s0 + s1) + s2;
        if (distance > farthest) {
            farthest = distance;
            best = i;
        }
    }
    free_entity(entity_of_handle(title_global<uint32_t>(0x009881c8 + uint32_t(best) * 0x1c)));
    return uint32_t(best);
}

uint32_t XB_REGS1 XB_STDCALL Path_GetNegotiationNodeInfo(const uint8_t* path, uint8_t* info, uint32_t* out) {
    // EAX path, stack info and out (ret 8): the node of the path's negotiation point (+0x386,
    // its node handle +0x18): info +8 = node +0x40, +0xc = 0, +4 (word) = the hashed node name
    // (+0x3c, added to the name map) or 0; *out = node +0x50. Returns the hash or 0.
    const uint16_t handle = field<uint16_t>(path, uint32_t(field<int16_t>(path, 0x386) * 0x1c + 0x18));
    const uint8_t* node = nullptr;
    if (handle != 0 && handle != 0xffff) {
        const uint8_t* const blocks = title_global<const uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
        const uint8_t* const block = field<const uint8_t*>(blocks, 8 + uint32_t(uint16_t((handle >> 9) - 1)) * 4);
        if (block) node = field<const uint8_t*>(block, 0x10) + (handle & 0x1ffu) * 0x80;
    }
    field<uint32_t>(info, 8) = field<uint32_t>(node, 0x40);
    field<uint32_t>(info, 0xc) = 0;
    const uint8_t* const name = field<const uint8_t*>(node, 0x3c);
    if (name) {
        const char* const text = reinterpret_cast<const char*>(name + 6);
        const uint32_t hash = title_function<uint32_t XB_CDECL(const char*)>(0x002f5470)(text);  // ScrIface_HashStringNoCase
        NameMap_Insert(hash, text);
        field<uint16_t>(info, 4) = uint16_t(hash);
        *out = field<uint32_t>(node, 0x50);
        return hash;
    }
    field<uint16_t>(info, 4) = 0;
    *out = field<uint32_t>(node, 0x50);
    return 0;
}

namespace {

// The grenade states' "no enemy" branch: cower for half a second.
void grenade_cower(uint8_t* actor) {
    field<uint32_t>(actor, 0x28) = 0x25a;
    field<int32_t>(actor, 0x2c) = level_time() + 1;
    field<int32_t>(actor, 0x34) = level_time() + 0x1f5;
    field<uint32_t>(actor, 0x12c) = 1;
    Actor_AnimStationary(actor, 0x00987bc8);  // g_animScript_grenade_cower
}

}  // namespace

void XB_REGS1 XB_CDECL Actor_Grenade_CoverAttack(uint8_t* actor) {
    // No enemy: cower. Else +0x12c = 3, the anim script +0x254 from +0x820 and the move mode
    // +0x27c from +0x280 (4 when unset).
    if (!field<uint32_t>(field<const uint8_t*>(actor, 4), 0x3c)) {
        grenade_cower(actor);
        return;
    }
    const uint32_t mode = field<uint32_t>(actor, 0x280);
    field<uint32_t>(actor, 0x12c) = 3;
    field<uint32_t>(actor, 0x254) = field<uint32_t>(actor, 0x820);
    field<uint32_t>(actor, 0x27c) = mode ? mode : 4;
    field<uint8_t>(actor, 0x285) = 1;
}

void XB_THISCALL Actor_Grenade_Combat(uint8_t* actor) {
    // ECX actor. No enemy: cower. Else +0x12c = 3 and the combat anim script: stationary
    // unless the actor has no partner (+0x1390) or the partner's actor moves; a partner pair
    // (each other's +0x1390) standing still more than 30 apart is split up first.
    if (!field<uint32_t>(field<const uint8_t*>(actor, 4), 0x3c)) {
        grenade_cower(actor);
        return;
    }
    const uint8_t* const partner = field<const uint8_t*>(actor, 0x1390);
    field<uint32_t>(actor, 0x12c) = 3;
    if (partner) {
        uint8_t* const other = field<uint8_t*>(partner, 0x240);
        if (!other) {
            Actor_AnimStationary(actor, 0x00987a98);  // g_animScript_combat
            return;
        }
        if (field<float>(other, 0x2a0) == 0.0f && field<float>(other, 0x2a4) == 0.0f) {
            const uint8_t* const ent = field<const uint8_t*>(actor, 0);
            if (field<const uint8_t*>(other, 0x1390) != ent) {
                Actor_AnimStationary(actor, 0x00987a98);
                return;
            }
            const float dy = field<float>(partner, 0x144) - field<float>(ent, 0x144);
            const float dx = field<float>(partner, 0x140) - field<float>(ent, 0x140);
            if (title_global<float>(0x0083ee3c) > dy * dy + dx * dx) {  // 900
                Actor_AnimStationary(actor, 0x00987a98);
                return;
            }
            field<uint32_t>(other, 0x1390) = 0;
            field<uint32_t>(actor, 0x1390) = 0;
        }
    }
    field<uint32_t>(actor, 0x254) = 0x00987a98;
    field<uint32_t>(actor, 0x27c) = 4;
    field<uint8_t>(actor, 0x285) = 0;
}

void XB_REGS1 XB_CDECL BadPlace_FreeIndex(int32_t index) {
    // EAX index (none when negative): a bad place of kind 1 (+8) takes its arc off the path
    // nodes (PathNode_AdjustBadPlaceArc, team +9, -1); the slot is freed and its name cleared.
    if (index < 0) return;
    const uint32_t slot = uint32_t(index) * 0x28;
    if (title_global<uint8_t>(0x0098a578 + slot) == 1) {
        using AdjustArc = void XB_STDCALL(uint32_t, uint32_t, uint32_t, int32_t);
        title_function<AdjustArc>(0x002d0db0)(title_global<uint32_t>(0x0095f4f4), 0x0098a57c + slot,
                                              title_global<uint8_t>(0x0098a579 + slot), -1);
    }
    title_global<uint8_t>(0x0098a578 + slot) = 0;
    title_function<void XB_THISCALL(uint32_t)>(0x0011d660)(0x0098a574 + slot);  // ScriptString_Clear
}

PLATFORM_ASM_ENTRY(BadPlace_Delete_entry, "pushl %ebx\n" PLATFORM_ASM_CALL(BadPlace_Delete) "addl $4, %esp\n"
                                          "ret")

void XB_CDECL BadPlace_Delete(const ScriptString* name) {
    // EBX name: the bad place of that name freed (BadPlace_FreeIndex inlined).
    BadPlace_FreeIndex(BadPlace_FindByName(name));
}

uint8_t* XB_REGS3 XB_CDECL BadPlace_Alloc(int32_t duration, uint32_t, const ScriptString* name) {
    // EAX duration, ECX name: an unnamed bad place frees the unnamed one first; the first free
    // slot of 32 (kind +8 = 0) gets the name and its end time +0 (level time + duration; a
    // named one with no duration never ends, an unnamed one gets none). The slot, or 0.
    const uint8_t* rep = *reinterpret_cast<const uint8_t* const*>(name);
    if (!rep || !rep[6]) BadPlace_FreeIndex(BadPlace_FindByName(name));
    int32_t end = level_time() + duration;
    if (duration <= 0) {
        rep = *reinterpret_cast<const uint8_t* const*>(name);
        if (!rep || !rep[6]) return nullptr;
        end = 0x7fffffff;
    }
    uint32_t i = 0;
    for (; i < 0x20; ++i)
        if (!title_global<uint8_t>(0x0098a578 + i * 0x28)) break;
    if (i == 0x20) {
        title_function<const char* XB_CDECL(const char*, ...)>(0x002a93d0)(&title_global<const char>(0x0083d354), 0x20);  // va
        return nullptr;
    }
    const uint32_t slot = i * 0x28;
    title_function<void XB_THISCALL(uint32_t, const ScriptString*)>(0x0011d600)(0x0098a574 + slot, name);  // ScriptString_Assign
    title_global<int32_t>(0x0098a570 + slot) = end;
    return &title_global<uint8_t>(0x0098a570 + slot);
}


PLATFORM_ASM_ENTRY(Scr_ProcessAnimNotifies_entry, "pushl %ebx\n" PLATFORM_ASM_CALL(Scr_ProcessAnimNotifies) "addl $4, %esp\n"
                                                  "ret")

void XB_CDECL Scr_ProcessAnimNotifies(uint8_t* list) {
    // EBX list (count +0, first node +4, the end node at +8; nodes: next +0, prev +4, done
    // flag +0x10): walks the nodes (stopping before the last), unlinking done ones (not
    // freed) and dispatching the rest (Scr_DispatchAnimNotetrack, EAX node).
    uint8_t* node = field<uint8_t*>(list, 4);
    uint8_t* next = node ? field<uint8_t*>(node, 0) : nullptr;
    if (node == list + 8 || !next) return;
    do {
        uint8_t* const current = node;
        node = next;
        next = field<uint8_t*>(next, 0);
        if (field<uint8_t>(current, 0x10)) {
            uint8_t* const after = field<uint8_t*>(current, 0);
            field<uint8_t*>(after, 4) = field<uint8_t*>(current, 4);
            uint8_t* const before = field<uint8_t*>(current, 4);
            field<uint8_t*>(before, 0) = field<uint8_t*>(current, 0);
            --field<int32_t>(list, 0);
        } else {
            uint32_t eax = uint32_t(uintptr_t(current));
            __asm__ volatile("call *%1"
                             : "+a"(eax)
                             : "r"(uintptr_t{0x00203280})  // Scr_DispatchAnimNotetrack
                             : "ecx", "edx", "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",
                               "xmm7");
        }
    } while (next);
}

PLATFORM_ASM_ENTRY(Actor_UpdateStatusNotifies_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_UpdateStatusNotifies) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_UpdateStatusNotifies(uint8_t* actor) {
    // EDI actor, once per think: "goal" when at the goal (+0x7df; in state 5 with no node and
    // no +0x140 only when facing within the goal arc), "enemy" + "enemyvisible"/"enemyhidden"
    // (+0x850 record of the enemy) + "combat" (unless state 2 with +0x28 in 201..210), or
    // "noenemy" (+ "noncombat" in state 5), "death" for team 4, "occupied" when within 1 of
    // the claimed node (+0x48).
    auto notify = [actor](uint32_t name) { Scr_Notify(field<uint8_t*>(actor, 0), title_global<uint32_t>(name)); };
    if (Actor_IsAtGoal(1, 0, actor)) {
        uint8_t* const sentient = field<uint8_t*>(actor, 4);
        field<uint8_t>(actor, 0x7df) = 1;
        bool reached = true;
        if (!(PathNode_HandleIsValid(reinterpret_cast<const uint16_t*>(sentient + 0x48)) & 0xff) &&
            !field<uint32_t>(actor, 0x140) && field<int32_t>(actor, 0x12c) == 5) {
            reached = Sentient_IsYawInGoalArc(sentient, field<float>(field<const uint8_t*>(actor, 0), 0x154)) != 0;
        }
        if (reached) {
            title_function<void XB_REGS3 XB_CDECL(uint8_t*, uint8_t*)>(0x002ba050)(actor + 0x3c0, sentient);  // Actor_SetStanceLimitsFromNode
            notify(0x0097a6ec);  // goal
        }
    } else {
        field<uint8_t>(actor, 0x7df) = 0;
    }
    if (field<const uint8_t*>(field<const uint8_t*>(actor, 4), 0x3c)) {
        notify(0x0097a69c);  // enemy
        const int32_t offset = int32_t(uintptr_t(field<const uint8_t*>(field<const uint8_t*>(actor, 4), 0x3c)) -
                                       uintptr_t(title_global<const uint8_t*>(0x00985378)));
        const int32_t index = offset / 0x60;
        notify(field<uint8_t>(actor, uint32_t(index * 0x50 + 0x850)) ? 0x0097a6a4 : 0x0097a6a0);  // enemyvisible/hidden
        const int32_t sub = field<int32_t>(actor, 0x28);
        if (!(field<int32_t>(actor, 8) == 2 && sub >= 0xc9 && sub <= 0xd2)) notify(0x0097a668);  // combat
    } else {
        notify(0x0097a73c);  // noenemy
        if (field<int32_t>(actor, 8) == 5) notify(0x0097a744);  // noncombat
    }
    if (field<int32_t>(field<const uint8_t*>(actor, 4), 4) == 4) notify(0x0097a684);  // death
    const uint16_t handle = field<uint16_t>(field<const uint8_t*>(actor, 4), 0x48);
    if (handle == 0 || handle == 0xffff) return;
    const uint8_t* const blocks = title_global<const uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
    const uint8_t* const block = field<const uint8_t*>(blocks, 8 + uint32_t(uint16_t((handle >> 9) - 1)) * 4);
    if (!block) return;
    const uint8_t* const node = field<const uint8_t*>(block, 0x10) + (handle & 0x1ffu) * 0x80;
    if (!node) return;
    // Packed: all four lanes (node x as the fourth) are computed.
    const uint8_t* const ent = field<const uint8_t*>(actor, 0);
    const float d0 = field<float>(ent, 0x140) - field<float>(node, 0x44);
    const float d1 = field<float>(ent, 0x144) - field<float>(node, 0x48);
    const float d2 = field<float>(ent, 0x148) - field<float>(node, 0x4c);
    const float d3 = field<float>(ent, 0x14c) - field<float>(node, 0x44);
    const float s0 = d0 * d0, s1 = d1 * d1, s2 = d2 * d2, s3 = d3 * d3;
    platform_keep(s3);
    if (title_global<float>(0x0083eb20) > (s0 + s1) + s2) notify(0x0097a74c);  // occupied
}

uint8_t XB_CDECL PathNode_FindFreeNeighbor_r(uint8_t* ent, const uint8_t* node) {
    // Up to 8 steps (depth 0x0095f70c) through the node links (+0x64, count +0x62): the first
    // neighbor nobody claims (+4) gets the entity moved onto it (origin, trajectory, an actor's
    // velocity and path cleared, +0x7df and the sentient's +0x5a reset) - 1; else the first
    // existing neighbor is searched next. 0 when none is found.
    if (title_global<int32_t>(0x0095f70c) > 8) return 0;
    const uint8_t* const blocks = title_global<const uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
    auto neighbor_of = [blocks](const uint8_t* link) -> const uint8_t* {
        const uint16_t handle = field<uint16_t>(link, 0);
        if (handle == 0 || handle == 0xffff) return nullptr;
        const uint8_t* const block = field<const uint8_t*>(blocks, 8 + uint32_t(uint16_t((handle >> 9) - 1)) * 4);
        if (!block) return nullptr;
        return field<const uint8_t*>(block, 0x10) + (handle & 0x1ffu) * 0x80;
    };
    for (;;) {
        ++title_global<int32_t>(0x0095f70c);
        const int32_t count = field<int16_t>(node, 0x62);
        const uint8_t* free_node = nullptr;
        for (int32_t i = 0; i < count; ++i) {
            const uint8_t* const other = neighbor_of(field<const uint8_t*>(node, 0x64) + i * 0xc);
            if (other && !field<uint32_t>(other, 4)) {
                free_node = other;
                break;
            }
        }
        if (free_node) {
            const uint32_t x = field<uint32_t>(free_node, 0x44), y = field<uint32_t>(free_node, 0x48),
                           z = field<uint32_t>(free_node, 0x4c);
            field<uint32_t>(ent, 0x20) = x;
            field<uint32_t>(ent, 0x24) = y;
            field<uint32_t>(ent, 0x28) = z;
            field<uint32_t>(ent, 0x14) = 0;
            field<uint32_t>(ent, 0x18) = 0;
            field<uint32_t>(ent, 0x1c) = 0;
            field<float>(ent, 0x34) = 0.0f;
            field<float>(ent, 0x30) = 0.0f;
            field<float>(ent, 0x2c) = 0.0f;
            field<uint32_t>(ent, 0x140) = x;
            field<uint32_t>(ent, 0x144) = y;
            field<uint32_t>(ent, 0x148) = z;
            if (field<uint8_t*>(ent, 0x240)) {
                static const uint32_t cleared[6] = {0x2a8, 0x2a4, 0x2a0, 0x2cc, 0x2c8, 0x2c4};
                for (uint32_t k = 0; k < 6; ++k) field<float>(field<uint8_t*>(ent, 0x240), cleared[k]) = 0.0f;
                uint8_t* const actor = field<uint8_t*>(ent, 0x240);
                uint8_t* const path = actor + 0x3c0;
                Path_AccumulateTrim(path, reinterpret_cast<const float*>(field<uint8_t*>(actor, 0) + 0x140));
                field<uint16_t>(path, 0x380) = 0;
                field<uint16_t>(path, 0x382) = 0;
                field<uint32_t>(actor, 0x7c4) = 0;
                field<uint8_t>(field<uint8_t*>(ent, 0x240), 0x7df) = 0;
            }
            if (uint8_t* const sentient = field<uint8_t*>(ent, 0x244)) field<uint8_t>(sentient, 0x5a) = 0;
            return 1;
        }
        const uint8_t* next = nullptr;
        for (int32_t i = 0; i < count; ++i) {
            next = neighbor_of(field<const uint8_t*>(node, 0x64) + i * 0xc);
            if (next) break;
        }
        if (!next) return 0;
        node = next;
        if (title_global<int32_t>(0x0095f70c) > 8) return 0;
    }
}

// PathNode_ListInCylinder and PathNode_FindFreeNeighbor_r run at the original's ESP with the
// centre and the node list at the original's frame addresses; the C frame is below a gap.
PLATFORM_ASM_ENTRY(Actor_FindStepAsideNode_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_FindStepAsideNode) "addl $16, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_FindStepAsideNode(uint32_t entry, uint8_t* a, uint8_t* b, float radius) {
    // EAX, ECX the two blocking entities, stack radius (at least 60, written back to the slot):
    // the lower main-character priority one steps aside to a free node next to the first node
    // found in the cylinder (radius, height 256) around its bounds' centre. AL 1 when moved.
    if (!b || !a) return 0;
    const float least = title_global<float>(0x0083eb3c);  // 60
    if (least > radius) {
        radius = least;
        title_global<float>(entry + 4) = radius;
    }
    const int32_t priority_b = Actor_MainCharacterPriority(b);
    const int32_t priority_a = Actor_MainCharacterPriority(a);
    uint8_t* const mover = priority_b <= priority_a ? b : a;
    const uint32_t frame = ((entry - 4) & ~0xfu) - 0x130;
    float* const centre = reinterpret_cast<float*>(uintptr_t{frame + 0x10});
    const uint32_t list = frame + 0x30;
    const float half = title_global<float>(0x0083eb24);
    float lanes[4];
    for (uint32_t i = 0; i < 4; ++i)
        lanes[i] = (field<float>(mover, 0x100 + i * 4) + field<float>(mover, 0x110 + i * 4)) * half;
    for (uint32_t i = 0; i < 4; ++i) {
        title_global<uint32_t>(frame + 0x20 + i * 4) = field<uint32_t>(mover, 0x110 + i * 4);  // its copy of the maxs
        centre[i] = lanes[i];
    }
    uint32_t radius_bits;
    std::memcpy(&radius_bits, &radius, 4);
    const uint32_t blocks = title_global<uint32_t>(0x0095f4f4);
    const uint32_t self = uint32_t(uintptr_t(mover));
    uint32_t found;
    {
        const uint32_t args[5] = {radius_bits, 0x43800000, list, 0x20, 0xffffffff};
        const TitleRegs regs = {list, uint32_t(priority_b), uint32_t(uintptr_t(centre)), field<uint32_t>(mover, 0x10c), self,
                                blocks};
        found = title_call_at_regs(frame, 0x002b1060, &regs, 5, args);  // PathNode_ListInCylinder
        title_global<uint32_t>(frame - 0x18) = 0x00203f30;
    }
    if (!found) return 0;
    const uint32_t first = title_global<uint32_t>(list);
    if (!first) return 0;
    title_global<int32_t>(0x0095f70c) = 0;
    const uint32_t args[2] = {self, first};
    const TitleRegs regs = {first, 0, 0, 0, self, blocks};
    const uint32_t moved = title_call_at_regs(frame, 0x00202ce0, &regs, 2, args);  // PathNode_FindFreeNeighbor_r
    title_global<uint32_t>(frame - 0xc) = 0x00203f4d;
    return moved;
}

// Actor_EventBullet runs at the original's ESP (entry - 0x24, two words), its return address
// written back; the C frame is below a gap.
PLATFORM_ASM_ENTRY(Actor_SendLineEvent_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_SendLineEvent) "addl $28, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $0x18")

void XB_CDECL Actor_SendLineEvent(uint32_t entry, const uint8_t* attacker, int32_t event, uint32_t mask,
                                  const float* start, const float* end, float range) {
    // stdcall, 6 words: an event along the line start-end to every actor of the teams in
    // `mask` (0: the attacker sentient's enemy team, none -> nothing) within `range` (squared
    // distance to the nearest point of the segment; 0: the event's default at 0x008fedb4 +
    // event * 8). Event 0x10: Actor_EventBullet; 0x11: attacked-by and position recorded.
    // The original keeps the mask, range and squared length in its argument slots.
    if (mask == 0) {
        const uint8_t* const sentient = field<const uint8_t*>(attacker, 0x244);
        if (!sentient) return;
        const uint32_t enemies[5] = {0, 2, 1, 0, 0};
        const uint32_t team = enemies[field<uint32_t>(sentient, 4)];
        if (!team) return;
        mask = 1u << (team & 31);
    }
    if (range == title_global<float>(0x0083eafc)) range = title_global<float>(0x008fedb4 + uint32_t(event) * 8);
    const float dx = end[0] - start[0];
    const float dy = end[1] - start[1];
    const float dz = end[2] - start[2];
    const float length2 = (dz * dz + dy * dy) + dx * dx;
    for (uint8_t* actor = Actor_FirstOfTeams(mask); actor; actor = Actor_NextOfTeams(actor, mask)) {
        const uint8_t* const ent = field<const uint8_t*>(actor, 0);
        const float pz = field<float>(ent, 0x148) - start[2];
        const float py = field<float>(ent, 0x144) - start[1];
        const float px = field<float>(ent, 0x140) - start[0];
        const float along = (pz * dz + py * dy) + px * dx;
        if (!(along >= title_global<float>(0x0083eafc))) continue;
        float cx, cy, cz;
        if (along > length2) {
            cx = end[0];
            cy = end[1];
            cz = end[2];
        } else {
            const float t = along / length2;
            cz = t * dz + start[2];
            cx = t * dx + start[0];
            cy = t * dy + start[1];
        }
        const float ex = field<float>(ent, 0x140) - cx;
        const float ey = field<float>(ent, 0x144) - cy;
        const float ez = field<float>(ent, 0x148) - cz;
        if (!(range >= (ez * ez + ey * ey) + ex * ex)) continue;
        if (event == 0x10) {
            const uint32_t args[2] = {uint32_t(uintptr_t(start)), 0};
            const TitleRegs regs = {uint32_t(uintptr_t(end)), 0, uint32_t(uintptr_t(actor)), 0, mask,
                                    uint32_t(uintptr_t(attacker))};
            title_call_at_regs(entry - 0x24, 0x00203780, &regs, 2, args);  // Actor_EventBullet
            title_global<uint32_t>(entry - 0x30) = 0x002045d5;
        } else if (event == 0x11) {
            if (ent && (field<uint32_t>(ent, 0x2a4) & 0x10000000)) continue;
            if (!entity_of_handle(field<uint32_t>(attacker, 0x224))) continue;
            if (!field<const uint8_t*>(attacker, 0x244)) continue;
            Actor_WasAttackedBy(field<const uint8_t*>(attacker, 0x244), 0, actor);
            Actor_RecordSentientPosition(field<const uint8_t*>(attacker, 0x244), actor);
        }
    }
}

PLATFORM_ASM_ENTRY(Actor_ClampShotDirToCone_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_ClampShotDirToCone) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_ClampShotDirToCone(float* shot) {
    // ESI: the shot direction (+0) and the aim forward (+0x30). Outside the 15 degree cone
    // (|dot| below cos 15 on the x87), the direction becomes the forward rotated by 15 degrees
    // around their (normalized) cross product.
    const float* const forward = shot + 12;
    long double sum = static_cast<long double>(forward[2]) * shot[2];
    sum = sum + static_cast<long double>(forward[1]) * shot[1];
    sum = sum + static_cast<long double>(forward[0]) * shot[0];
    const float dot = abs_bits(static_cast<float>(sum));
    if (!platform_x87_greater(title_global<float>(0x0083f024), dot)) return;  // cos 15
    float axis[3];
    axis[0] = shot[2] * forward[1] - forward[2] * shot[1];
    axis[1] = shot[0] * forward[2] - forward[0] * shot[2];
    axis[2] = forward[0] * shot[1] - shot[0] * forward[1];
    const float length =
        title_ci_sqrt_ext(static_cast<long double>((axis[2] * axis[2] + axis[1] * axis[1]) + axis[0] * axis[0]));
    if (!(length == title_global<float>(0x0083eafc))) {
        const float inverse = title_global<float>(0x0083eb20) / length;
        axis[0] = inverse * axis[0];
        axis[1] = axis[1] * inverse;
        axis[2] = axis[2] * inverse;
    }
    float rotated[3];
    RotatePointAroundVector(axis, forward, rotated, 15.0f);
    uint32_t* const out = reinterpret_cast<uint32_t*>(shot);
    const uint32_t* const words = reinterpret_cast<const uint32_t*>(rotated);
    out[0] = words[0];
    out[1] = words[1];
    out[2] = words[2];
}

void XB_REGS3 XB_CDECL Actor_OrientToMoveHistory(uint8_t* orient, uint32_t, const uint8_t* actor) {
    // EAX orientation, ECX actor: with a path, in move mode 1 (unless +0x77c bit 0 with
    // +0x770 under 60) or 7, faces along the summed move history (mode 7 only when it is more
    // than ~25 degrees off the current yaw +0xc).
    if (field<int16_t>(actor, 0x740) <= 0) return;
    const int32_t mode = field<int32_t>(actor, 0x278);
    if (mode != 1 && mode != 7) return;
    if (mode == 1 && (field<uint8_t>(actor, 0x77c) & 1) && title_global<float>(0x0083eb3c) > field<float>(actor, 0x770))
        return;
    float history[3] = {0.0f, 0.0f, 0.0f};
    Actor_SumMoveHistory(history, 0, actor);
    if (history[0] == 0.0f && history[1] == 0.0f) return;
    if (mode == 7) {
        Vec2Normalize(history);
        const float yaw = field<float>(orient, 0xc) * title_global<float>(0x0083edd8);  // degrees to radians
        float sine, cosine;
        FastSinCos(&sine, 0, &cosine, yaw);
        if (sine * history[1] + cosine * history[0] >= title_global<float>(0x0083ec18)) return;  // 0.9
    }
    Actor_SetOrientFromDir(orient, history, 0.0f);
}

PLATFORM_ASM_ENTRY(Actor_TurnTowardAngles_entry, "pushl 4(%esp)\n"
                                                 "pushl %esi\n" PLATFORM_ASM_CALL(Actor_TurnTowardAngles) "addl $8, %esp\n"
                                                 "ret $4")

void XB_CDECL Actor_TurnTowardAngles(uint8_t* actor, int32_t max_turn) {
    // ESI actor, stack max turn (ret 4): the desired pitch/yaw (+0x100/+0x104) turned toward the
    // target angles (+0x140 when set, else +0x12c; +4 pitch, +8 yaw) by at most 0.8 * max_turn
    // each, the differences wrapped into [-180, 180] (__CIfmod 360); tail jump to
    // Actor_SetDesiredAngles.
    const float* const target = reinterpret_cast<const float*>(actor + (field<uint32_t>(actor, 0x140) ? 0x140 : 0x12c));
    const double full = title_global<double>(0x0083ed40);  // 360.0
    auto wrapped = [&](float to, float from) {
        float delta = static_cast<float>(title_ci_fmod(static_cast<long double>(to) - from, full));
        if (platform_x87_greater(delta, title_global<float>(0x0083eb60))) delta = delta - title_global<float>(0x0083ebcc);
        if (title_global<float>(0x0083ebd0) > delta) delta = delta + title_global<float>(0x0083ebcc);  // -180, 360
        return delta;
    };
    float pitch = wrapped(target[1], field<float>(actor, 0x100));
    const float limit = static_cast<float>(max_turn);
    const float most = limit * title_global<float>(0x0083ebc0);  // 0.8
    if (pitch > most) {
        pitch = most;
    } else {
        const float least = limit * title_global<float>(0x0083f0c4);  // -0.8
        if (least > pitch) pitch = least;
    }
    const float new_pitch = field<float>(actor, 0x100) + pitch;
    float yaw = wrapped(target[2], field<float>(actor, 0x104));
    if (yaw > most) {
        yaw = most;
    } else {
        const float least = limit * title_global<float>(0x0083f0c4);
        if (least > yaw) yaw = least;
    }
    Actor_SetDesiredAngles(actor, new_pitch, platform_x87_add(yaw, field<float>(actor, 0x104)));
}

PLATFORM_ASM_ENTRY(Path_TraceGatherCandidates_entry,
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Path_TraceGatherCandidates) "addl $32, %esp\n"
                   "ret")

uint8_t XB_CDECL Path_TraceGatherCandidates(uint8_t* cache, const float* box_a, const float* box_b, const float* start,
                                            const float* end, uint8_t* out_triangles, uint8_t* out_brushes,
                                            uint32_t contents) {
    // ESI trace cache (lists +0x450 triangles, +0x454 brushes; cached box +0x430/+0x440), EAX
    // and ECX the mover's bounds, stack start, end (aligned 4-lane vectors), output triangle and
    // brush lists, content mask. For moves shorter than 150 (0x008ff040): the swept box (the
    // smaller of the half x / half z extents around the segment, twice that upwards) is
    // filtered from the cached candidates (CM_FilterBoxCandidates), the cache refilled with
    // the box grown by 50 (CM_GatherBoxCandidates) when it is not inside the cached one.
    // AL 1, or 0 without a cache or for a long move. All packed lanes are computed.
    if (!cache || !field<uint32_t>(cache, 0x450) || !field<uint32_t>(cache, 0x454)) return 0;
    float squares[4];
    for (uint32_t i = 0; i < 4; ++i) {
        const float delta = end[i] - start[i];
        squares[i] = delta * delta;
    }
    const float reach = title_global<float>(0x008ff040);
    if (!(reach * reach > (squares[0] + squares[1]) + squares[2])) return 0;
    alignas(16) float low[4];
    alignas(16) float high[4];
    for (uint32_t i = 0; i < 4; ++i) {
        low[i] = start[i] < end[i] ? start[i] : end[i];
        high[i] = start[i] > end[i] ? start[i] : end[i];
    }
    const float half_factor = title_global<float>(0x0083eb24);  // 0.5
    float half[4];
    for (uint32_t i = 0; i < 4; ++i) half[i] = box_b[i] - (box_a[i] + box_b[i]) * half_factor;
    float extent = half[0];
    if (extent > half[2]) extent = half[2];
    const float down[4] = {extent, extent, 0.0f, extent};
    const float up[4] = {extent, extent, extent * title_global<float>(0x0083eba8), extent};
    uint32_t inside = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        low[i] = low[i] - down[i];
        high[i] = high[i] + up[i];
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const float above = high[i] - field<float>(cache, 0x440 + i * 4);
        const float below = field<float>(cache, 0x430 + i * 4) - low[i];
        const float most = below > above ? below : above;
        if (most < title_global<float>(0x0082e3c0 + i * 4)) inside |= 1u << i;
    }
    if ((inside & 7) != 7) {
        if (!(title_global<uint8_t>(0x0098bf30) & 1)) {
            title_global<uint32_t>(0x0098bf30) |= 1;
            const uint32_t pad = title_global<uint32_t>(0x0083ec30);  // 50
            for (uint32_t i = 0; i < 4; ++i) title_global<uint32_t>(0x0098bf20 + i * 4) = pad;
        }
        for (uint32_t i = 0; i < 4; ++i) {
            const float pad = title_global<float>(0x0098bf20 + i * 4);
            field<float>(cache, 0x430 + i * 4) = low[i] - pad;
            field<float>(cache, 0x440 + i * 4) = high[i] + pad;
        }
        CM_GatherBoxCandidates(reinterpret_cast<const float*>(cache + 0x430), field<uint8_t*>(cache, 0x450),
                               field<uint8_t*>(cache, 0x454), reinterpret_cast<const float*>(cache + 0x440));
    }
    CM_FilterBoxCandidates(out_brushes, 0, field<const uint8_t*>(cache, 0x454), low, high, contents,
                           field<const uint8_t*>(cache, 0x450), out_triangles);
    return 1;
}

void XB_STDCALL Actor_HandlePathArrival(uint8_t* actor) {
    // stdcall: arriving at the path's negotiation point (+0x746). With no arrival time
    // (+0x798), a moving actor (+0x2b0) with path flag 0x40 close enough for its speed
    // (squared distance within |velocity|^2 * 2e-6 + 1e-6) pushes state 12 and takes the
    // negotiation node's data (+0x258) and yaw, velocity = the offset, +0x12c = 1. Otherwise
    // (or flag 1 at the path's start) it eases in: within a 16th of its squared speed the
    // arrival is set 500 ms ahead; the velocity (+0x2c4) is the offset scaled by
    // (2 - f) * f / (+0x2bc when +0x2b8, else 1), f = frame time / time left; at the
    // arrival time the path is dropped and the scale is the plain one.
    // The original keeps the scale and the node's yaw in its argument slot.
    const int32_t arrival = field<int32_t>(actor, 0x798);
    const uint8_t* const ent = field<const uint8_t*>(actor, 0);
    if (arrival == 0) {
        if (!field<uint32_t>(actor, 0x2b0)) return;
        const uint32_t flags = field<uint32_t>(actor, 0x77c);
        if (!(flags & 0x40)) return;
        const int16_t index = field<int16_t>(actor, 0x746);
        if (index > 0) {
            const uint8_t* const point = actor + 0x3c0 + int32_t(index) * 0x1c;
            const float dy = field<float>(point, 4) - field<float>(ent, 0x144);
            const float dx = field<float>(point, 0) - field<float>(ent, 0x140);
            const float dz = field<float>(point, 8) - field<float>(ent, 0x148);
            const float distance2 = dy * dy + dx * dx;
            const float vx = field<float>(actor, 0x2a0), vy = field<float>(actor, 0x2a4), vz = field<float>(actor, 0x2a8);
            const float reach =
                ((vx * vx + vy * vy) + vz * vz) * title_global<float>(0x0083f030) + title_global<float>(0x0083f02c);
            if (!(reach >= distance2)) return;
            if (!Actor_PushState(actor, 0xc)) return;
            uint32_t yaw_bits = 0;
            Path_GetNegotiationNodeInfo(actor + 0x3c0, actor + 0x258, &yaw_bits);
            float yaw;
            std::memcpy(&yaw, &yaw_bits, 4);
            Actor_SetQuantizedPitchYaw(actor + 0x12c, 0.0f, yaw);
            field<float>(actor, 0x2c4) = dx;
            field<float>(actor, 0x2c8) = dy;
            field<float>(actor, 0x2cc) = dz;
            field<uint32_t>(actor, 0x12c) = 1;
            return;
        }
        if (!(flags & 1)) return;
    }
    const uint8_t* const point = actor + 0x3c0 + int32_t(field<int16_t>(actor, 0x746)) * 0x1c;
    const float dx = field<float>(point, 0) - field<float>(ent, 0x140);
    const float dy = field<float>(point, 4) - field<float>(ent, 0x144);
    const float dz = field<float>(point, 8) - field<float>(ent, 0x148);
    if (arrival == 0) {
        const float vx = field<float>(actor, 0x2a0), vy = field<float>(actor, 0x2a4);
        const float speed2 = (vx * vx + vy * vy) * title_global<float>(0x0083f028);  // 1/16
        if (dy * dy + dx * dx > speed2) return;
        field<int32_t>(actor, 0x798) = level_time() + 500;
    }
    float scale;
    if (field<uint32_t>(actor, 0x2b8))
        scale = title_global<float>(0x0083eb20) / field<float>(actor, 0x2bc);
    else
        scale = title_global<float>(0x0083eb20);
    const int32_t left = field<int32_t>(actor, 0x798) - level_time();
    if (left <= 0) {
        uint8_t* const path = actor + 0x3c0;
        Path_AccumulateTrim(path, reinterpret_cast<const float*>(ent + 0x140));
        field<float>(actor, 0x2c4) = scale * dx;
        field<uint16_t>(path, 0x380) = 0;
        field<uint16_t>(path, 0x382) = 0;
        field<float>(actor, 0x2c8) = scale * dy;
        field<float>(actor, 0x2cc) = scale * dz;
        field<uint32_t>(actor, 0x7c4) = 0;
        return;
    }
    const float fraction = static_cast<float>(title_global<int32_t>(0x008dda9c)) / static_cast<float>(left);
    const float k = ((title_global<float>(0x0083eba8) - fraction) * fraction) * scale;
    field<float>(actor, 0x2c4) = k * dx;
    field<float>(actor, 0x2c8) = k * dy;
    field<float>(actor, 0x2cc) = k * dz;
}


// The name buffer (entry - 0x100), its length (entry - 0x108) and the lookup's out word
// (entry - 0x104, also pushed as a stale-topped word) sit at the original's frame addresses;
// every callee runs where the original's call puts it, return addresses written back. The C
// frame is below a gap.
PLATFORM_ASM_ENTRY(AnimNotify_Init_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ecx\n"
                   "pushl %edi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(AnimNotify_Init) "addl $16, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $4")

uint8_t* XB_CDECL AnimNotify_Init(uint32_t entry, uint8_t* node, uint32_t name, uint32_t handle) {
    // EDI node, ECX name, stack entity handle (ret 4): a pending anim notify (list links +0/+4,
    // entity +8, +0xc, failed flag +0x10, "ANIM" trigger table +0x14, name +0x18, variant +0x1c
    // = -1). The table comes from the zone string map entry of the anim prefix (0x00988c58)
    // + ".anim"; without one the notify fails, else its first variant plays
    // (AnimNotify_PlayNext). EAX = node.
    field<uint32_t>(node, 8) = handle;
    field<uint32_t>(node, 0) = 0;
    field<uint32_t>(node, 4) = 0;
    field<uint32_t>(node, 0xc) = 0;
    field<uint8_t>(node, 0x10) = 0;
    field<uint32_t>(node, 0x14) = 0;
    field<uint32_t>(node, 0x18) = name;
    field<int32_t>(node, 0x1c) = -1;
    const uint32_t frame = entry - 0x110;
    const uint32_t text = entry - 0x100, length = entry - 0x108, out = entry - 0x104;
    const uint32_t self = uint32_t(uintptr_t(node));
    {
        const uint32_t args[4] = {text, length, 0xfe, 0x00988c58};
        const TitleRegs regs = {handle, 0, name, length, 0, self};
        title_call_at_regs(frame, 0x000442e0, &regs, 4, args);  // Q_strncpyLen
        title_global<uint32_t>(frame - 0x14) = 0x002028e5;
    }
    const uint16_t copied = title_global<uint16_t>(length);
    title_global<uint16_t>(entry - 2) = copied;
    title_global<uint32_t>(length) = copied;
    {
        const uint32_t args[4] = {text, length, 0xfe, 0x0083ce94};  // ".anim"
        const TitleRegs regs = {text, 0, copied, length, 0, self};
        title_call_at_regs(frame, 0x00044330, &regs, 4, args);  // Str_AppendBounded
        title_global<uint32_t>(frame - 0x14) = 0x00202911;
    }
    const uint16_t total = title_global<uint16_t>(length);
    title_global<uint8_t>(out) = 0;
    title_global<uint16_t>(entry - 2) = total;
    {
        const uint32_t zones = field<uint32_t>(title_global<const uint8_t*>(0x0095f638), 0x34);
        const uint32_t args[5] = {title_global<uint32_t>(0x0095f5a8), out, text, title_global<uint32_t>(out), 0};  // rm_db
        const TitleRegs regs = {title_global<uint32_t>(0x0095f638), 0, out, zones, 0, self};
        title_call_at_regs(frame, 0x0002bf80, &regs, 5, args);  // ZoneStringMap_Lookup
        title_global<uint32_t>(frame - 0x18) = 0x00202949;
    }
    const uint32_t entry_found = title_global<uint32_t>(out);
    if (entry_found) {
        const uint32_t args[1] = {0x0083846c};  // "ANIM"
        const TitleRegs regs = {0, 0, 0, 0, entry_found, self};
        field<uint32_t>(node, 0x14) = title_call_at_regs(frame, 0x00023640, &regs, 1, args);  // FXTriggerTable_Find
        title_global<uint32_t>(frame - 8) = 0x0020295b;
    }
    if (!field<uint32_t>(node, 0x14)) {
        field<uint8_t>(node, 0x10) = 1;
        return node;
    }
    {
        const uint32_t args[1] = {self};
        const TitleRegs regs = {field<uint32_t>(node, 0x14), 0, 0, 0, 0, self};
        title_call_at_regs(entry - 0x108, 0x00200450, &regs, 1, args);  // AnimNotify_PlayNext
        title_global<uint32_t>(entry - 0x110) = 0x0020297c;
    }
    return node;
}

namespace {

// The entity a handle names (index 12 bits, generation above), or 0.
uint32_t entity_from_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        return title_global<uint32_t>(0x0097a968 + index * 8);
    return 0;
}

// A one-time hash of a constant name (init bit `bit` of the word at `flags`).
uint32_t cached_hash(uint32_t flags, uint32_t bit, uint32_t hash, uint32_t name) {
    if (!(title_global<uint32_t>(flags) & bit)) {
        title_global<uint32_t>(flags) |= bit;
        title_global<uint32_t>(hash) = Com_HashKeyNoCase(0, &title_global<const char>(name));
    }
    return title_global<uint32_t>(hash);
}

void copy3(uint8_t* to, const uint8_t* from) {
    for (uint32_t i = 0; i < 12; i += 4) field<uint32_t>(to, i) = field<uint32_t>(from, i);
}

// Releases the script-string fields (type 0x10) of a field table (offset, type; ends at type 0).
void release_string_fields(uint8_t* base, uint32_t table) {
    uint32_t entry = table;
    do {
        if (title_global<int32_t>(entry + 4) == 0x10) {
            uint32_t& slot = field<uint32_t>(base, title_global<uint32_t>(entry));
            if (slot) {
                title_function<void XB_THISCALL(uint32_t)>(0x0011d5b0)(slot);  // ScriptStringRep_Release
                slot = 0;
            }
        }
        entry += 8;
    } while (title_global<uint32_t>(entry + 4));
}

// The anim script's start callback: EAX = the stack word (entity handle), EDX script, ECX handle.
uint32_t call_anim_script_start(uint32_t function, uint32_t script, uint32_t handle) {
    uint32_t eax, edx = script, ecx = handle;
    __asm__ volatile("pushl %%ecx\n"
                     "movl %%esp, %%eax\n"
                     "call *%3\n"
                     "addl $4, %%esp\n"
                     : "=&a"(eax), "+d"(edx), "+c"(ecx)
                     : "r"(function)
                     : "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7");
    return eax;
}

const uint8_t* dobj_of(const uint8_t* ent) { return field<const uint8_t*>(ent, 0x22c); }

}  // namespace

void XB_CDECL Actor_ReleaseTurret(uint8_t* actor) {
    // The actor gives up its turret (+0x136c): the turret's in-use flag (+0x297) and user
    // handle (+0x1a0) cleared, "turretownerchange" on both; an actor linked to the turret
    // goes back to the turret's own link parent (tag from +0x3cc +8) or is unlinked; the
    // turret info's flag 0x200 (+0x24c +2) cleared.
    uint8_t* const turret = field<uint8_t*>(actor, 0x136c);
    field<uint8_t>(turret, 0x297) = 0;
    field<uint32_t>(turret, 0x1a0) = 0;
    Scr_Notify(turret, title_global<uint32_t>(0x0097a864));
    Scr_Notify(field<uint8_t*>(actor, 0), title_global<uint32_t>(0x0097a864));
    uint8_t* const ent = field<uint8_t*>(actor, 0);
    const uint8_t* const link = field<const uint8_t*>(ent, 0x3cc);
    if (link && field<uint8_t*>(link, 0) == turret) {
        const uint8_t* const parent_link = field<const uint8_t*>(turret, 0x3cc);
        if (!parent_link) {
            title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x002b7900)(ent);  // G_EntUnlink
        } else {
            const int32_t tag = field<int32_t>(parent_link, 8);
            const uint32_t name = tag ? uint32_t(uintptr_t(IntHashMap_FindValue(tag))) : 0x0082dfeb;  // ""
            using LinkTo = uint32_t XB_REGS1 XB_CDECL(uint32_t, uint8_t*, uint32_t, uint32_t);
            if (title_function<LinkTo>(0x002df740)(field<uint32_t>(parent_link, 0), ent, name, 0))  // G_EntLinkTo
                title_function<void XB_CDECL(uint8_t*, uint32_t)>(0x002e3f10)(ent, 0);       // G_CalcTagLinkOffset
        }
    }
    field<uint8_t>(field<uint8_t*>(turret, 0x24c), 3) &= 0xfd;
}

uint32_t XB_REGS3 XB_CDECL Actor_Turret_Start(uint32_t, uint32_t previous, uint8_t* actor) {
    // Turret state start (ECX actor, EDX previous state): links the actor to its free turret
    // (tag ""), marks it used (+0x297, user handle +0x1a0), "turretownerchange", idles it and
    // sets the info flags (+0x24c +2: 0x100 on, 0xe8 off; 0x10 and the target +0x4c kept
    // only when coming from state 8); clears threat times and the path. 0, or 2 / 5 (no
    // turret: 2 when the sentient has +0x3c set).
    uint8_t* const turret = field<uint8_t*>(actor, 0x136c);
    if (turret && !field<uint8_t>(turret, 0x297)) {
        uint8_t* const ent = field<uint8_t*>(actor, 0);
        using LinkTo = uint32_t XB_REGS1 XB_CDECL(uint8_t*, uint8_t*, uint32_t, uint32_t);
        if (title_function<LinkTo>(0x002df740)(turret, ent, 0x0082dfeb, 0)) {  // G_EntLinkTo
            title_function<void XB_CDECL(uint8_t*, uint32_t)>(0x002e3f10)(ent, 0);  // G_CalcTagLinkOffset
            field<uint8_t>(turret, 0x297) = 1;
            field<uint32_t>(turret, 0x1a0) = field<uint32_t>(field<uint8_t*>(actor, 0), 0x224);
            Scr_Notify(turret, title_global<uint32_t>(0x0097a864));
            title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x0024fd90)(turret);  // Turret_SetIdle
            uint8_t* const info = field<uint8_t*>(turret, 0x24c);
            field<uint16_t>(info, 2) = uint16_t((field<uint16_t>(info, 2) & 0xff17) | 0x100);
            Actor_ClearSentientThreatTimes(actor);
            if (previous != 8) {
                field<uint8_t>(info, 2) &= 0xef;
                field<uint32_t>(info, 0x4c) = 0;
            }
            Actor_ClearPath(actor);
            return 0;
        }
    }
    return field<uint32_t>(field<const uint8_t*>(actor, 4), 0x3c) ? 2 : 5;
}

PLATFORM_ASM_ENTRY(Actor_UpdateAnimScript_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_UpdateAnimScript) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_UpdateAnimScript(uint8_t* actor) {
    // ESI actor: takes over the requested move mode (+0x27c -> +0x278; mode 1 clears the move
    // history and entity flags 0xc) and +0x285 -> +0x284. On a restart (+0x26c) or a new anim
    // script (+0x254 vs +0x250): clears the script state, pops a state-9 entry (pushing a
    // transition 3), stops the old script, starts the new one (its +8 callback with the
    // entity handle -> +0x24c) and advances the anims. Then the request moves to +0x270/+0x274
    // and is cleared. EAX = the request's script.
    const int32_t mode = field<int32_t>(actor, 0x27c);
    if (field<int32_t>(actor, 0x278) != mode) {
        field<int32_t>(actor, 0x278) = mode;
        if (mode == 1) {
            Actor_ClearMoveHistory(actor);
            field<uint32_t>(field<uint8_t*>(actor, 0), 0x2a4) &= 0xfffffff3;
        }
    }
    field<uint8_t>(actor, 0x284) = field<uint8_t>(actor, 0x285);
    if (field<uint8_t>(actor, 0x26c) || field<uint32_t>(actor, 0x250) != field<uint32_t>(actor, 0x254)) {
        uint8_t* const ent = field<uint8_t*>(actor, 0);
        field<uint8_t>(actor, 0x26c) = 0;
        field<uint32_t>(actor, 0x140) = 0;
        field<uint32_t>(actor, 0x1324) = 0;
        field<uint32_t>(actor, 0x280) = 0;
        field<uint32_t>(ent, 0x2a4) &= 0xfffeffff;
        const int32_t top = field<int32_t>(actor, 0xbc);
        if (field<int32_t>(actor, 0xa4 + top * 4) == 9) {
            field<uint32_t>(actor, 0x38 + field<int32_t>(actor, 0xa0) * 8) = 3;
            field<uint32_t>(actor, 0x3c + field<int32_t>(actor, 0xa0) * 8) = 0;
            field<int32_t>(actor, 0xa0) = field<int32_t>(actor, 0xa0) + 1;
            Actor_MergeStateTransitions(actor);
            --field<int32_t>(actor, 0xbc);
        }
        Actor_StopAnimScript(actor);
        const uint8_t* const script = field<const uint8_t*>(actor, 0x254);
        field<uint32_t>(actor, 0x250) = uint32_t(uintptr_t(script));
        if (field<uint32_t>(script, 8)) {
            const uint32_t handle = field<uint32_t>(field<const uint8_t*>(actor, 0), 0x224);
            const uint32_t start = field<uint32_t>(field<const uint8_t*>(actor, 0x254), 8);
            field<uint32_t>(actor, 0x24c) =
                call_anim_script_start(start, field<uint32_t>(actor, 0x254), handle);
        }
        uint32_t edi = field<uint32_t>(actor, 0);
        __asm__ volatile("call *%1"
                         : "+D"(edi)
                         : "r"(uintptr_t{0x0025b4d0})  // G_DObjAdvanceAnimsUntilDone
                         : "eax", "ecx", "edx", "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5",
                           "xmm6", "xmm7");
    }
    const uint32_t script = field<uint32_t>(actor, 0x254);
    const uint32_t requested = field<uint32_t>(actor, 0x27c);
    field<uint32_t>(actor, 0x254) = 0;
    field<uint32_t>(actor, 0x27c) = 0;
    field<uint32_t>(actor, 0x270) = script;
    field<uint32_t>(actor, 0x274) = requested;
    return script;
}

PLATFORM_ASM_ENTRY(Actor_LeaveTurret_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Actor_LeaveTurret) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_LeaveTurret(uint8_t* actor) {
    // ESI actor: releases the turret when this actor is its user; the turret pointer cleared.
    const uint8_t* const turret = field<const uint8_t*>(actor, 0x136c);
    if (turret && entity_from_handle(field<uint32_t>(turret, 0x1a0)) == field<uint32_t>(actor, 0))
        Actor_ReleaseTurret(actor);
    field<uint32_t>(actor, 0x136c) = 0;
}

void XB_REGS3 XB_STDCALL Actor_Turret_Pain(uint32_t, const uint8_t* attacker, uint8_t* actor, uint32_t, uint32_t,
                                           uint32_t, uint32_t, uint32_t) {
    // Turret state pain (ECX actor, EDX attacker entity): hit by a sentient of the enemy team
    // while using the turret - the first hit sets the info's flag 0x10 and target (+0x4c =
    // attacker's sentient), the next one makes the actor leave the turret.
    if (!field<const uint8_t*>(attacker, 0x244)) return;
    const uint8_t* const sentient = field<const uint8_t*>(actor, 4);
    if (field<int32_t>(sentient, 0x30) == 1) return;
    if (!Actor_IsUsingTurret(actor)) return;
    if (field<int32_t>(field<const uint8_t*>(attacker, 0x244), 4) != Sentient_EnemyTeam(field<int32_t>(sentient, 4)))
        return;
    uint8_t* const info = field<uint8_t*>(field<uint8_t*>(actor, 0x136c), 0x24c);
    const uint16_t flags = field<uint16_t>(info, 2);
    if (flags & 0x10) {
        Actor_LeaveTurret(actor);
        return;
    }
    field<uint16_t>(info, 2) = uint16_t(flags | 0x10);
    field<uint32_t>(info, 0x4c) = field<uint32_t>(attacker, 0x244);
}

void XB_THISCALL Actor_Turret_Finish(uint8_t* actor) {
    // Turret state finish (ECX actor): releases the turret when this actor is its user.
    const uint8_t* const turret = field<const uint8_t*>(actor, 0x136c);
    if (turret && entity_from_handle(field<uint32_t>(turret, 0x1a0)) == field<uint32_t>(actor, 0))
        Actor_ReleaseTurret(actor);
}

void XB_REGS2 XB_CDECL BadPlace_Create(int32_t duration, const uint32_t* arc, const ScriptString* name, uint32_t team) {
    // EAX duration, EDX the 7-word arc: a kind-1 bad place for `team` (BadPlace_Alloc), its arc
    // applied to the path nodes, then crossing paths cleared.
    uint32_t copy[7];
    for (uint32_t i = 0; i < 7; ++i) copy[i] = arc[i];
    uint8_t* const place = BadPlace_Alloc(duration, 0, name);
    if (!place) return;
    const uint32_t blocks = title_global<uint32_t>(0x0095f4f4);
    field<uint8_t>(place, 9) = uint8_t(team);
    field<uint8_t>(place, 8) = 1;
    for (uint32_t i = 0; i < 7; ++i) field<uint32_t>(place, 0xc + i * 4) = copy[i];
    using AdjustArc = void XB_STDCALL(uint32_t, uint32_t, uint32_t, int32_t);
    title_function<AdjustArc>(0x002d0db0)(blocks, uint32_t(uintptr_t(place)) + 0xc, uint8_t(team), 1);
    BadPlace_ClearCrossingPaths();
}

PLATFORM_ASM_ENTRY(Actor_GetMuzzleInfo_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_GetMuzzleInfo) "addl $12, %esp\n"
                   "ret $8")

uint32_t XB_CDECL Actor_GetMuzzleInfo(uint8_t* actor, uint8_t* origin, uint8_t* direction) {
    // EDI actor: the "tag_flash" origin (+0x1d0) and forward axis (+0x1dc), refreshed once per
    // level time (+0x1cc), copied out (direction optional). 0 without a model or tag.
    if (field<int32_t>(actor, 0x1cc) != level_time()) {
        const uint32_t hash = cached_hash(0x0098ebac, 1, 0x0098eba8, 0x008389c0);  // "tag_flash"
        uint8_t* const ent = field<uint8_t*>(actor, 0);
        if (!dobj_of(ent)) return 0;
        const int32_t tag = DObj_GetTagIndex(reinterpret_cast<const DObj*>(dobj_of(ent)), hash);
        if (tag < 0) return 0;
        alignas(16) uint8_t m[0x40];
        title_function<void XB_CDECL(uint8_t*, int32_t, uint8_t*)>(0x002e3780)(ent, tag, m);  // G_DObjGetWorldTagMatrix
        copy3(actor + 0x1d0, m + 0x30);
        copy3(actor + 0x1dc, m);
        field<int32_t>(actor, 0x1cc) = level_time();
    }
    copy3(origin, actor + 0x1d0);
    if (direction) copy3(direction, actor + 0x1dc);
    return 1;
}

PLATFORM_ASM_ENTRY(Actor_RefreshEyeOrigin_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_RefreshEyeOrigin) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Actor_RefreshEyeOrigin(uint8_t* actor) {
    // EDI actor, once per level time (+0x1b0): eye origin (+0x1b4) and flat look direction
    // (+0x1c0) from the "BIP01 HEAD" tag of a shown model; else the entity origin + 64 and the
    // actor's +0x108 direction.
    if (field<int32_t>(actor, 0x1b0) == level_time()) return;
    uint8_t* const ent = field<uint8_t*>(actor, 0);
    field<int32_t>(actor, 0x1b0) = level_time();
    const bool shown = dobj_of(ent) && !(field<uint8_t>(dobj_of(ent), 0x9c) & 1);
    const uint32_t hash = cached_hash(0x0098ebb4, 1, 0x0098ebb0, 0x0083d3e8);  // "BIP01 HEAD"
    if (shown && dobj_of(ent)) {
        const int32_t tag = DObj_GetTagIndex(reinterpret_cast<const DObj*>(dobj_of(ent)), hash);
        if (tag >= 0) {
            alignas(16) uint8_t m[0x40];
            title_function<void XB_CDECL(uint8_t*, int32_t, uint8_t*)>(0x002e3780)(ent, tag, m);  // G_DObjGetWorldTagMatrix
            copy3(actor + 0x1b4, m + 0x30);
            field<uint32_t>(actor, 0x1c0) = field<uint32_t>(m, 0x10);
            field<uint32_t>(actor, 0x1c4) = field<uint32_t>(m, 0x14);
            Vec2Normalize(&field<float>(actor, 0x1c0));
            field<float>(actor, 0x1c8) = 0.0f;
            return;
        }
    }
    field<uint32_t>(actor, 0x1b4) = field<uint32_t>(ent, 0x140);
    field<uint32_t>(actor, 0x1b8) = field<uint32_t>(ent, 0x144);
    field<float>(actor, 0x1bc) = field<float>(ent, 0x148) + title_global<float>(0x00830c1c);  // 64
    copy3(actor + 0x1c0, actor + 0x108);
}

void XB_CDECL Actor_ThinkLinked(uint8_t* ent) {
    // Think of a linked actor entity: next think next frame; follows its link (G_GeneralLink).
    field<int32_t>(ent, 0x328) = level_time() + 1;
    if (field<uint32_t>(ent, 0x3cc)) title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x002e6490)(ent);  // G_GeneralLink
}

void XB_REGS1 XB_CDECL Actor_Free(uint8_t* actor) {
    // EAX actor: leaves the turret, stops the anim script, frees a held grenade it owns, clears
    // other actors' references to it (+0x74c/+0x39c handles, +0x1390 entity, +0x7c8 actor),
    // dissociates and frees its sentient (string fields 0x0082fdc0, count 0x0095f420), releases
    // its own string fields (0x0082fb70) and script strings, unregisters it (count 0x0095f44c,
    // g_actors slot +0x1382) and frees the memory (its client's memory context, else Z_Free).
    uint8_t* const ent = field<uint8_t*>(actor, 0);
    Actor_LeaveTurret(actor);
    Actor_StopAnimScript(actor);
    uint8_t* const grenade = field<uint8_t*>(actor, 0x1310);
    if (grenade && field<uint8_t*>(grenade, 0x378) == ent && (field<uint8_t>(grenade, 0xd4) & 1)) {
        title_function<void XB_CDECL(uint8_t*, uint32_t)>(0x002be420)(grenade, 0);  // G_FreeEntity
        field<uint32_t>(actor, 0x1310) = 0;
    }
    const uint32_t handle = field<uint32_t>(ent, 0x224);
    for (uint32_t slot = 0x00985384; slot < 0x00985404; slot += 4) {
        uint8_t* const other = title_global<uint8_t*>(slot);
        if (!other || field<int32_t>(other, 0x1360) < 0) continue;
        if (field<uint32_t>(other, 0x74c) == handle) field<uint32_t>(other, 0x74c) = 0;
        if (field<uint32_t>(other, 0x39c) == handle) field<uint32_t>(other, 0x39c) = 0;
        if (field<uint8_t*>(other, 0x1390) == ent) field<uint32_t>(other, 0x1390) = 0;
        if (field<uint8_t*>(other, 0x7c8) == actor) {
            field<uint32_t>(other, 0x7c8) = 0;
            field<uint32_t>(other, 0x7cc) = 0;
        }
    }
    Sentient_Dissociate(field<uint8_t*>(actor, 4));
    field<uint32_t>(ent, 0x240) = 0;
    uint8_t* const sentient = field<uint8_t*>(actor, 4);
    G_ClearEntityReferences(0, field<uint8_t*>(sentient, 0));
    Sentient_Dissociate(sentient);
    field<uint32_t>(field<uint8_t*>(sentient, 0), 0x244) = 0;
    release_string_fields(sentient, 0x0082fdc0);
    const int32_t sentients_left = title_global<int32_t>(0x0095f420) - 1;
    field<int32_t>(sentient, 0x5c) = sentients_left;
    title_global<int32_t>(0x0095f420) = sentients_left;
    field<uint32_t>(actor, 4) = 0;
    release_string_fields(actor, 0x0082fb70);
    static const uint32_t kStrings[7] = {0x138c, 0x1388, 0x1384, 0x1320, 0xf8, 0xec, 0xc8};
    for (uint32_t i = 0; i < 7; ++i)
        title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(actor + kStrings[i]);  // ScriptString_Release
    const int32_t actors_left = title_global<int32_t>(0x0095f44c) - 1;
    const uint8_t* const clients = title_global<const uint8_t*>(0x0095f638);
    field<int32_t>(actor, 0x1360) = actors_left;
    title_global<int32_t>(0x0095f44c) = actors_left;
    title_global<uint32_t>(0x00985384 + field<int16_t>(actor, 0x1382) * 4) = 0;
    int32_t context = field<int32_t>(field<const uint8_t*>(actor, 0), 0x220);
    if (context == -1) context = field<int32_t>(clients, 0x34);
    if (context != -1) {
        const uint32_t owner = context >= 0 && context < 0x63 ? field<uint32_t>(clients, 0x38 + context * 4) : 0;
        if (MemContext_Free_r(reinterpret_cast<uint8_t*>(uintptr_t(owner)), actor)) return;
    }
    title_function<void XB_CDECL(uint8_t*)>(0x000428d0)(actor);  // Z_Free
}

PLATFORM_ASM_ENTRY(Actor_GetEyePosition_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_GetEyePosition) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_GetEyePosition(uint8_t* actor, uint8_t* out) {
    // EAX actor, ESI out: the refreshed eye origin.
    Actor_RefreshEyeOrigin(actor);
    copy3(out, actor + 0x1b4);
}

PLATFORM_ASM_ENTRY(Actor_ApplySpawnTemplate_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Actor_ApplySpawnTemplate) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL Actor_ApplySpawnTemplate(const uint8_t* spawn, uint8_t* actor) {
    // EBX spawn template, stack actor (ret 4): team (+4: 0/1/2 -> 1/2/3), +8 -> actor +0xd4,
    // +0xc -> entity +0x350, +0x1c -> sentient +0x2c, +0x20/+0x24 -> actor +0x7e4/+0x1328,
    // two names (+0x10/+0x14, hashed and registered -> +0xf0/+0xf4), weapon (+0x18 -> +0x131c),
    // then a random model of the list (+0x28 count, +0x2c {model, name}): G_SetModel, the name
    // into +0xf8, G_DObjUpdate, SV_LinkEntity.
    uint8_t* const sentient = field<uint8_t*>(actor, 4);
    uint8_t* const ent = field<uint8_t*>(actor, 0);
    switch (field<int32_t>(spawn, 4)) {
    case 0: field<uint32_t>(sentient, 4) = 1; break;
    case 1: field<uint32_t>(sentient, 4) = 2; break;
    case 2: field<uint32_t>(sentient, 4) = 3; break;
    default: break;
    }
    field<uint32_t>(actor, 0xd4) = field<uint32_t>(spawn, 8);
    field<uint32_t>(ent, 0x350) = field<uint32_t>(spawn, 0xc);
    field<uint32_t>(sentient, 0x2c) = field<uint32_t>(spawn, 0x1c);
    field<uint32_t>(actor, 0x7e4) = field<uint32_t>(spawn, 0x20);
    field<uint32_t>(actor, 0x1328) = field<uint32_t>(spawn, 0x24);
    for (uint32_t i = 0; i < 2; ++i) {
        const char* const name = field<const char*>(spawn, 0x10 + i * 4);
        if (!name) continue;
        const uint32_t hash = title_function<uint32_t XB_CDECL(const char*)>(0x002f5470)(name);  // ScrIface_HashStringNoCase
        NameMap_Insert(hash, name);
        field<uint32_t>(actor, 0xf0 + i * 4) = hash;
    }
    const char* const weapon = field<const char*>(spawn, 0x18);
    if (weapon) {
        uint8_t index = 0;
        if (*weapon) index = uint8_t(BG_FindWeaponIndexForHash(Com_HashKeyNoCase(0, weapon)));
        field<uint32_t>(actor, 0x131c) = index;
    }
    const uint32_t pick = uint32_t(title_function<int32_t XB_CDECL()>(0x001efcc0)()) % field<uint32_t>(spawn, 0x28);  // _rand
    const uint32_t model = pick < field<uint32_t>(spawn, 0x28) ? pick : 0;
    using SetModel = void XB_REGS3 XB_CDECL(int32_t, uint8_t*, uint32_t);
    title_function<SetModel>(0x002d7c60)(-1, ent, field<uint32_t>(field<const uint8_t*>(spawn, 0x2c), model * 8));  // G_SetModel
    const uint32_t name_index = pick < field<uint32_t>(spawn, 0x28) ? pick : 0;
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x0011dd20)(  // ScriptString_AssignCString
        actor + 0xf8, field<uint32_t>(field<const uint8_t*>(spawn, 0x2c), name_index * 8 + 4));
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
    SV_LinkEntity(ent);
}

void XB_REGS3 XB_CDECL Sentient_EyeOrigin(const uint8_t* sentient, uint32_t, uint8_t* out) {
    // EAX sentient, ECX out: an actor with a shown model gives its refreshed eye origin; else
    // the entity origin raised by the client's view height (+0x23c +0xac) and lean
    // (AddLeanToPosition, 16 / 20), or by 64.
    const uint8_t* const ent = field<const uint8_t*>(sentient, 0);
    const bool shown = dobj_of(ent) && !(field<uint8_t>(dobj_of(ent), 0x9c) & 1);
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (actor && shown) {
        Actor_RefreshEyeOrigin(actor);
        copy3(out, actor + 0x1b4);
        return;
    }
    field<uint32_t>(out, 0) = field<uint32_t>(ent, 0x140);
    field<uint32_t>(out, 4) = field<uint32_t>(field<const uint8_t*>(sentient, 0), 0x144);
    const float z = field<float>(field<const uint8_t*>(sentient, 0), 0x148);
    field<float>(out, 8) = z;
    const uint8_t* const client = field<const uint8_t*>(field<const uint8_t*>(sentient, 0), 0x23c);
    if (client) {
        field<float>(out, 8) = field<float>(client, 0xac) + z;
        const uint8_t* const ps = field<const uint8_t*>(field<const uint8_t*>(sentient, 0), 0x23c);
        AddLeanToPosition(reinterpret_cast<float*>(out), field<float>(ps, 0x4c), field<float>(ps, 0xa0), 16.0f, 20.0f);
        return;
    }
    field<float>(out, 8) = z + title_global<float>(0x00830c1c);  // 64
}


namespace {

uint32_t& frame_word(uint32_t address) { return title_global<uint32_t>(address); }

// Runs a title callee where the original's call puts it (`top` = the original's ESP before
// the call's pushes, args[0] pushed last) and writes the original's return address back.
uint32_t frame_call(uint32_t top, uint32_t function, uint32_t ret, const TitleRegs& regs, uint32_t count,
                    const uint32_t* args) {
    const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
    frame_word(top - count * 4 - 4) = ret;
    return result;
}

}  // namespace

// Frame rebuilt: the parameter set (+0x2b8 of the original's frame: vtable, trigger table,
// list), the name buffer (+0x2c), the trigger query result (+0xac count, +0x1b4 choices) and
// the entity/script-string slots (+8, +0xc) sit at the original's addresses; every callee runs
// at the original's ESP with its EBX/ESI/EDI. The C frame is below a gap.
PLATFORM_ASM_ENTRY(AnimNotify_PlayNext_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl (%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(AnimNotify_PlayNext) "addl $24, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $4")

void XB_CDECL AnimNotify_PlayNext(uint32_t entry, uint8_t* node, uint32_t ebx, uint32_t esi, uint32_t edi,
                                  uint32_t ebp) {
    // Stack node (ret 4): queries the node's "ANIM" trigger table (+0x14) with the entity's
    // anim name (+0x264 +6), the notify name (+0x18) and its variant (+0xc) and plays the next
    // matching choice (+0x1c, else the node is marked failed +0x10): choice values 2 (variant,
    // kept), 3 (anim set, default "generic_human") / 4 (anim) -> the entity plays it as
    // "single_anim"; 5 a face sound ("animscript facesound"); 6 a face anim (generic_human only,
    // "animscript_faceanim", 1 / 0.1 / 1); 7 / 8 a script object and its two callbacks fed the
    // entity's origin / angles.
    const uint32_t f = entry - 0x718, g = f - 8;
    const uint32_t self = uint32_t(uintptr_t(node));
    frame_word(entry - 0x714) = ebx;
    frame_word(f) = esi;
    frame_word(f + 0x2bc) = field<uint32_t>(node, 0x14);
    field<uint8_t>(node, 0x10) = 0;
    frame_word(f + 0xac) = 0;
    frame_word(f + 0x1b0) = 0;
    frame_word(f + 0x2b4) = 0;
    frame_word(f + 0x2b8) = 0x008376b8;
    frame_call(f, 0x00023690, 0x0020049a, {f + 0x2c0, 0, 0, 0, self, edi}, 0, nullptr);  // ParamList_Init
    title_global<uint8_t>(f + 0x510) = 0;
    frame_word(f + 0x714) = 0;
    frame_word(f + 8) = 0;
    frame_word(f + 8) = entity_from_handle(field<uint32_t>(node, 8));
    auto add_string = [&](uint32_t text, uint32_t id, uint32_t ret_copy, uint32_t ret_add) {
        const uint32_t copy_args[3] = {f + 0x2c, text, 0x7f};
        const uint32_t copied =
            frame_call(f, 0x001eed80, ret_copy, {f + 0x2c, 0, 0, 0, self, edi}, 3, copy_args);  // _strncpy
        title_global<uint8_t>(f + 0xab) = 0;
        const uint32_t add_args[3] = {id, f + 0x2c, 1};
        frame_call(f, 0x00024630, ret_add, {copied, 0, f + 0x2c, f + 0x2b8, self, edi}, 3, add_args);  // ParamSet_AddString
    };
    const uint32_t anim_name = field<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t(frame_word(f + 8))), 0x264);
    add_string(anim_name ? anim_name + 6 : 0, 0, 0x002004f8, 0x00200516);
    add_string(field<uint32_t>(node, 0x18), 1, 0x00200526, 0x00200545);
    if (field<uint32_t>(node, 0xc)) add_string(field<uint32_t>(node, 0xc), 2, 0x00200559, 0x00200578);
    {
        const uint32_t vtable = frame_word(f + 0x2b8);
        const uint32_t args[1] = {f + 0xac};
        frame_call(f, frame_word(vtable + 4), 0x00200591, {0, 0, f + 0x2b8, vtable, self, edi}, 1, args);
    }
    const uint32_t count = frame_word(f + 0xac);
    if (!count) {
        field<uint8_t>(node, 0x10) = 1;
        return;
    }
    frame_word(f - 4) = ebp;
    const uint32_t index = field<uint32_t>(node, 0x1c) + 1;
    field<uint32_t>(node, 0x1c) = index;
    if (count < index + 1) {
        field<uint8_t>(node, 0x10) = 1;
        return;
    }
    frame_word(g) = edi;
    const uint32_t choice = frame_word(f + 0x1b4 + index * 4);
    uint32_t esi_now = self;
    auto value = [&](uint32_t id, uint32_t ret) -> const uint32_t* {  // ParamChoice_GetValue
        return reinterpret_cast<const uint32_t*>(
            uintptr_t(frame_call(g, 0x00024940, ret, {0, id, 0, 0, esi_now, choice}, 0, nullptr)));
    };
    auto hash = [&](uint32_t text, uint32_t ret, uint32_t ebx_now, uint32_t count_now, const uint32_t* args) {
        return frame_call(g, 0x00292fe0, ret, {0, ebx_now, 0, text, esi_now, choice}, count_now, args);  // Com_HashKeyNoCase
    };
    const uint32_t gi = title_global<uint32_t>(0x0095f49c);
    auto slot_fn = [&](uint32_t slot) { return frame_word(gi + slot); };
    if (!field<uint32_t>(node, 0xc)) {
        const uint32_t* const variant = value(2, 0x002005e3);
        field<uint32_t>(node, 0xc) = variant ? *variant : 0;
    }
    const uint32_t* v = value(4, 0x002005fa);
    const uint32_t anim = hash(v ? *v : 0, 0x00200609, 4, 0, nullptr);
    v = value(3, 0x00200615);
    {
        const uint32_t pushed[1] = {anim};
        const uint32_t set = hash(v ? *v : 0x0083a894, 0x0020062a, 3, 1, pushed);  // "generic_human"
        const uint32_t args[2] = {set, anim};
        frame_call(g, slot_fn(0x854), 0x00200636, {gi, 3, 0, 0, esi_now, choice}, 2, args);
    }
    v = value(5, 0x00200643);
    frame_word(f + 0xc) = v ? *v : 0;
    v = value(6, 0x00200661);
    frame_word(f + 0x10) = hash(v ? *v : 0, 0x00200670, 6, 0, nullptr);
    v = value(7, 0x0020067e);
    const uint32_t object_name = v ? *v : 0;
    esi_now = object_name;
    v = value(8, 0x00200692);
    const uint32_t callback_name = v ? *v : 0;
    const uint8_t* const ent = reinterpret_cast<const uint8_t*>(uintptr_t(frame_word(f + 8)));
    {
        const uint32_t z = field<uint32_t>(ent, 0x148), y = field<uint32_t>(ent, 0x144), x = field<uint32_t>(ent, 0x140);
        frame_word(f + 0x20) = x;
        frame_word(f + 0x24) = y;
        frame_word(f + 0x28) = z;
        const uint32_t c = field<uint32_t>(ent, 0x158), b = field<uint32_t>(ent, 0x154), a = field<uint32_t>(ent, 0x150);
        frame_word(f + 0x14) = a;
        frame_word(f + 0x18) = b;
        frame_word(f + 0x1c) = c;
    }
    auto from_cstring = [&](uint32_t slot, uint32_t text, uint32_t ret, uint32_t ebx_now) {
        const uint32_t args[1] = {text};
        frame_call(g, 0x0011eb50, ret, {0, ebx_now, slot, 0, esi_now, choice}, 1, args);  // ScriptString_FromCString
    };
    auto release = [&](uint32_t slot, uint32_t ret, uint32_t ebx_now) {
        frame_call(g, 0x0011d640, ret, {0, ebx_now, slot, 0, esi_now, choice}, 0, nullptr);  // ScriptString_Release
    };
    if (object_name) {
        from_cstring(f + 8, object_name, 0x00200706, callback_name);
        const uint32_t kind = title_global<uint32_t>(0x0098a4d4);
        const uint32_t args[5] = {f + 8, kind, 0, 0, 0};
        const uint32_t object = frame_call(g, slot_fn(0x84), 0x00200724, {f + 8, callback_name, gi, kind, esi_now, choice}, 5, args);
        esi_now = object;
        release(f + 8, 0x00200732, callback_name);
        if (object && callback_name) {
            from_cstring(f + 8, callback_name, 0x00200744, callback_name);
            const uint32_t first[3] = {object, f + 0x20, f + 8};
            frame_call(g, slot_fn(0x4dc), 0x0020075b, {f + 0x20, callback_name, gi, f + 8, esi_now, choice}, 3, first);
            release(f + 8, 0x00200767, callback_name);
            from_cstring(f + 8, callback_name, 0x00200771, callback_name);
            const uint32_t second[3] = {object, f + 0x14, f + 8};
            frame_call(g, slot_fn(0x4e0), 0x00200788, {f + 0x14, callback_name, gi, f + 8, esi_now, choice}, 3, second);
            release(f + 8, 0x00200794, callback_name);
        }
    }
    uint32_t ebx_now = callback_name;
    if (anim) {
        v = value(3, 0x002007a6);
        ebx_now = 3;
        const uint32_t set = hash(v ? *v : 0x0083a894, 0x002007b8, 3, 0, nullptr);
        const uint32_t args[2] = {set, anim};
        const uint32_t played = frame_call(g, slot_fn(0x854), 0x002007c6, {set, 3, 0, gi, esi_now, choice}, 2, args);
        esi_now = played;
        if (!(title_global<uint32_t>(0x0098bc0c) & 1)) {
            title_global<uint32_t>(0x0098bc0c) |= 1;
            title_global<uint32_t>(0x0098bc08) = hash(0x0083ce88, 0x002007f2, 3, 0, nullptr);  // "single_anim"
        }
        const uint32_t single = title_global<uint32_t>(0x0098bc08);
        const uint32_t handle = field<uint32_t>(node, 8);
        const uint32_t play[5] = {handle, single, f + 0x20, f + 0x14, played};
        frame_call(g, slot_fn(0x380), 0x00200820, {handle, 3, gi, single, played, choice}, 5, play);
    }
    const uint32_t sound = frame_word(f + 0xc);
    esi_now = sound;
    if (sound) {
        if (!(title_global<uint32_t>(0x0098bc0c) & 2)) {
            title_global<uint32_t>(0x0098bc0c) |= 2;
            title_global<uint32_t>(0x0098bc04) = hash(0x0083ce70, 0x0020085b, ebx_now, 0, nullptr);  // "animscript facesound"
        }
        from_cstring(f + 0xc, sound, 0x0020086a, ebx_now);
        const uint32_t facesound = title_global<uint32_t>(0x0098bc04);
        const uint32_t handle = field<uint32_t>(node, 8);
        const uint32_t args[4] = {handle, f + 0xc, facesound, 0};
        frame_call(g, slot_fn(0xcc), 0x00200888, {f + 0xc, ebx_now, handle, gi, esi_now, choice}, 4, args);
        release(f + 0xc, 0x00200894, ebx_now);
    }
    const uint32_t face = frame_word(f + 0x10);
    if (!face) return;
    v = value(3, 0x002008aa);
    const uint32_t set = hash(v ? *v : 0x0083a894, 0x002008be, 3, 0, nullptr);
    esi_now = set;
    uint32_t flags = title_global<uint32_t>(0x0098bc0c);
    if (!(flags & 4)) {
        flags |= 4;
        title_global<uint32_t>(0x0098bc0c) = flags;
        title_global<uint32_t>(0x0098bc00) = hash(0x0083a894, 0x002008de, flags, 0, nullptr);  // "generic_human"
    }
    if (!(flags & 8)) {
        flags |= 8;
        title_global<uint32_t>(0x0098bc0c) = flags;
        title_global<uint32_t>(0x0098bbfc) = hash(0x0083ce5c, 0x002008fb, flags, 0, nullptr);  // "animscript_faceanim"
    }
    if (set != title_global<uint32_t>(0x0098bc00)) return;
    const uint32_t lookup[2] = {set, face};
    const uint32_t anim_face = frame_call(g, slot_fn(0x854), 0x0020091a, {face, flags, gi, 0, set, choice}, 2, lookup);
    const uint32_t faceanim = title_global<uint32_t>(0x0098bbfc);
    const uint32_t handle = field<uint32_t>(node, 8);
    const uint32_t args[7] = {handle, faceanim, anim_face, 0x3f800000, 0x3dcccccd, 0x3f800000, 1};
    frame_call(g - 8, slot_fn(0x4a0), 0x00200943, {handle, flags, gi, faceanim, set, choice}, 7, args);
}

// Frame rebuilt like AnimNotify_PlayNext's: the trigger query (+0xa8 count, +0x1b0 choices
// of the original's frame after its pushes), the key / loop words and the script-string slot
// at the original's addresses, every callee at the original's ESP.
PLATFORM_ASM_ENTRY(Scr_DispatchAnimNotetrack_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl (%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Scr_DispatchAnimNotetrack) "addl $24, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Scr_DispatchAnimNotetrack(uint32_t entry, uint8_t* node, uint32_t ebx, uint32_t esi, uint32_t edi,
                                        uint32_t ebp) {
    // EAX pending notify node: without its entity it ends (Scr_QueueAnimEndNotify). An entity
    // whose notetrack key list (+0x230) has "single_anim" and "end" or "finish" ends it and plays
    // the next variant (AnimNotify_PlayNext). Then the node's trigger table is queried with
    // the variant name (+0xc): each choice whose value 1 names a key of that list starts a new
    // notify (value 0 = a script object, AnimNotify_Init) appended to the list 0x008feba0.
    const uint32_t f = entry - 0x704, g = f - 8;
    const uint32_t self = uint32_t(uintptr_t(node));
    frame_word(entry - 0x700) = esi;
    frame_word(f) = edi;
    const uint32_t ent = entity_from_handle(field<uint32_t>(node, 8));
    if (!ent) {
        frame_call(f, 0x001fdda0, 0x002032bb, {ent, ebx, 0, 0, esi, self}, 0, nullptr);  // Scr_QueueAnimEndNotify
        return;
    }
    frame_word(f - 4) = ebx;
    frame_word(g) = ebp;
    const uint32_t list = field<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t(ent)), 0x230);
    frame_word(g + 0x1c) = list;
    uint32_t flags = title_global<uint32_t>(0x0098bd08);
    auto hash_once = [&](uint32_t bit, uint32_t out, uint32_t text, uint32_t ret) {
        if (flags & bit) return;
        flags |= bit;
        title_global<uint32_t>(0x0098bd08) = flags;
        title_global<uint32_t>(out) = frame_call(g, 0x00292fe0, ret, {ent, flags, 0, text, 0, self}, 0, nullptr);
    };
    hash_once(1, 0x0098bd04, 0x00832b28, 0x002032ee);  // "end"
    hash_once(2, 0x0098bd00, 0x0083ce54, 0x0020330b);  // "finish"
    hash_once(4, 0x0098bcfc, 0x0083ce88, 0x00203328);  // "single_anim"
    if (!list) return;
    auto contains = [&](uint32_t key, uint32_t ret) {
        frame_word(g + 0x10) = key;
        const uint32_t args[1] = {g + 0x10};
        return uint8_t(frame_call(g, 0x00013df0, ret, {0, flags, g + 0x10, list, 0, self}, 1, args));  // KeyList_Contains
    };
    if (!contains(title_global<uint32_t>(0x0098bcfc), 0x0020334a)) return;
    if (contains(title_global<uint32_t>(0x0098bd04), 0x00203368) || contains(title_global<uint32_t>(0x0098bd00), 0x00203382)) {
        frame_call(g, 0x001fdda0, 0x0020338b, {0, flags, 0, list, 0, self}, 0, nullptr);  // Scr_QueueAnimEndNotify
        const uint32_t args[1] = {self};
        frame_call(g, 0x00200450, 0x00203391, {0, flags, 0, list, 0, self}, 1, args);  // AnimNotify_PlayNext
    }
    frame_word(g + 0x2b0) = field<uint32_t>(node, 0x14);
    frame_word(g + 0xa0) = 0;
    frame_word(g + 0x1a4) = 0;
    frame_word(g + 0x2a8) = 0;
    frame_word(g + 0x2ac) = 0x008376b8;
    frame_call(g, 0x00023690, 0x002033c7, {g + 0x2b4, flags, 0, 0, 0, self}, 0, nullptr);  // ParamList_Init
    {
        title_global<uint8_t>(g + 0x504) = 0;
        frame_word(g + 0x708) = 0;
        const uint32_t args[3] = {g + 0x20, field<uint32_t>(node, 0xc), 0x7f};
        const uint32_t copied = frame_call(g, 0x001eed80, 0x002033e6, {0, flags, args[1], g + 0x20, 0, self}, 3, args);  // _strncpy
        title_global<uint8_t>(g + 0x9f) = 0;
        const uint32_t add[3] = {2, g + 0x20, 1};
        frame_call(g, 0x00024630, 0x00203406, {copied, flags, 0, g + 0x2ac, 0, self}, 3, add);  // ParamSet_AddString
        const uint32_t vtable = frame_word(g + 0x2ac);
        const uint32_t query[1] = {g + 0xa0};
        frame_call(g, frame_word(vtable + 4), 0x0020341f, {0, flags, g + 0x2ac, vtable, 0, self}, 1, query);
    }
    frame_word(g + 0x10) = 0;
    if (int32_t(frame_word(g + 0xa0)) <= 0) return;
    frame_word(g + 0x14) = g + 0x1a8;
    uint32_t esi_now = 0;
    do {
        const uint32_t choice = frame_word(frame_word(g + 0x14));
        const uint32_t* v = reinterpret_cast<const uint32_t*>(
            uintptr_t(frame_call(g, 0x00024940, 0x00203450, {0, 1, 0, 0, esi_now, choice}, 0, nullptr)));  // ParamChoice_GetValue
        const uint32_t name = v ? *v : 0;
        const uint32_t key = frame_call(g, 0x00292fe0, 0x00203461, {0, 1, 0, name, esi_now, choice}, 0, nullptr);  // Com_HashKeyNoCase
        const uint32_t keys = frame_word(g + 0x1c);
        esi_now = keys + 0x14;
        uint32_t item = frame_word(keys + 0x10);
        uint32_t next = item ? frame_word(item) : 0;
        bool found = false;
        if (item != keys + 0x14 && next) {
            do {
                if (frame_word(item + 8) == key) {
                    found = true;
                    break;
                }
                item = next;
                next = frame_word(next);
            } while (next);
        }
        if (found) {
            v = reinterpret_cast<const uint32_t*>(
                uintptr_t(frame_call(g, 0x00024940, 0x0020349e, {0, 0, 0, 0, keys + 0x14, choice}, 0, nullptr)));
            const uint32_t text = v ? *v : 0;
            const uint32_t from[1] = {text};
            frame_call(g, 0x0011eb50, 0x002034b2, {text, 0, g + 0x18, 0, keys + 0x14, choice}, 1, from);  // ScriptString_FromCString
            const uint32_t kind = title_global<uint32_t>(0x0098a4d4);
            const uint32_t gi = title_global<uint32_t>(0x0095f49c);
            const uint32_t args[5] = {g + 0x18, kind, 0, 0, 0};
            const uint32_t object = frame_call(g, frame_word(gi + 0x84), 0x002034d0, {g + 0x18, 0, gi, kind, keys + 0x14, choice}, 5, args);
            esi_now = object;
            frame_call(g, 0x0011d640, 0x002034de, {0, 0, g + 0x18, 0, object, choice}, 0, nullptr);  // ScriptString_Release
            const uint32_t size[1] = {0x20};
            const uint32_t block = frame_call(g, 0x00042c10, 0x002034e5, {0, 0, 0, 0, object, choice}, 1, size);  // Z_Malloc
            uint32_t added = 0;
            if (block) {
                const uint32_t init[1] = {object};
                added = frame_call(g, 0x002028a0, 0x002034f6, {block, 0, name, 0, object, block}, 1, init);  // AnimNotify_Init
            }
            frame_word(added) = 0x008feba8;
            const uint32_t tail = title_global<uint32_t>(0x008febac);
            frame_word(added + 4) = tail;
            frame_word(tail) = added;
            title_global<uint32_t>(0x008febac) = added;
            ++title_global<int32_t>(0x008feba0);
        }
        frame_word(g + 0x10) = frame_word(g + 0x10) + 1;
        frame_word(g + 0x14) = frame_word(g + 0x14) + 4;
    } while (int32_t(frame_word(g + 0x10)) < int32_t(frame_word(g + 0xa0)));
}

namespace {

float x87_int_to_float(int32_t value) {
    float result;
    __asm__("fildl %1\n\tfstps %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
double x87_float_mul_double(float a, double b) {
    double result;
    __asm__("flds %1\n\tfmull %2\n\tfstpl %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}
float x87_double_to_float(double value) {
    float result;
    __asm__("fldl %1\n\tfstps %0" : "=m"(result) : "m"(value) : "st");
    return result;
}

}  // namespace

PLATFORM_ASM_ENTRY(Actor_GetWeaponFireOrigin_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_GetWeaponFireOrigin) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_GetWeaponFireOrigin(uint8_t* actor, uint8_t* out) {
    // EAX actor, ESI out: the actor's 9-word aim record (+0x108) at +0, the muzzle origin /
    // direction at +0x24 / +0x30; without a muzzle tag the eye origin and the record's first
    // vector.
    for (uint32_t i = 0; i < 0x24; i += 4) field<uint32_t>(out, i) = field<uint32_t>(actor, 0x108 + i);
    if (Actor_GetMuzzleInfo(actor, out + 0x24, out + 0x30)) return;
    Actor_RefreshEyeOrigin(actor);
    copy3(out + 0x24, actor + 0x1b4);
    const uint32_t x = field<uint32_t>(out, 0), y = field<uint32_t>(out, 4), z = field<uint32_t>(out, 8);
    field<uint32_t>(out, 0x30) = x;
    field<uint32_t>(out, 0x34) = y;
    field<uint32_t>(out, 0x38) = z;
}

PLATFORM_ASM_ENTRY(G_TouchTriggers_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_TouchTriggers) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL G_TouchTriggers(const float* origin, uint8_t* ent) {
    // ESI origin, stack entity: the box of half size 0x008ff04c around the origin collects
    // the trigger entities (touch type flag | 0x800000) into 0x0098ec20 (count 0x0098ebf0, the
    // list zeroed once); with any, the entity's bounds at the origin touch them
    // (G_TouchEntitiesInBox, then 0x0025d520).
    if (!(title_global<uint8_t>(0x0098ee20) & 1)) {
        title_global<uint32_t>(0x0098ee20) |= 1;
        for (uint32_t i = 0; i < 0x200; i += 4) title_global<uint32_t>(0x0098ec20 + i) = 0;
    }
    float* const mins = &title_global<float>(0x0098ec00);
    float* const maxs = &title_global<float>(0x0098ec10);
    const float* const half = &title_global<float>(0x008ff04c);
    for (int i = 0; i < 3; ++i) mins[i] = origin[i] - half[i];
    for (int i = 0; i < 3; ++i) maxs[i] = origin[i] + half[i];
    const uint32_t mask = G_GetTouchTypeFlag(ent) | 0x800000;
    const int32_t count =
        SV_AreaEntities(maxs, 0, mins, &title_global<int32_t>(0x0098ec20), 0x80, mask);
    title_global<int32_t>(0x0098ebf0) = count;
    if (!count) return 0;
    for (int i = 0; i < 3; ++i) mins[i] = field<float>(ent, 0xe0 + i * 4) + origin[i];
    for (int i = 0; i < 3; ++i) maxs[i] = field<float>(ent, 0xf0 + i * 4) + origin[i];
    const uint32_t from = uint32_t(uintptr_t(origin)), self = uint32_t(uintptr_t(ent));
    const TitleRegs regs = {0x0098ec10, self, from, 0, from, 0};
    const uint32_t args[2] = {self, 0x0098ebf0};
    g_ent_call(0x0025d230, &regs, 2, args);  // G_TouchEntitiesInBox
    const TitleRegs touch = {0, self, from, 0x0098ebf0, from, 0};
    return g_ent_call(0x0025d520, &touch, 0, nullptr);  // 0x0025d520
}

void XB_CDECL Actor_Die(uint8_t* self, uint32_t, uint32_t attacker, int32_t damage, int32_t mod, uint32_t,
                        const float* point, const float* dir, int32_t hit_location) {
    // Die callback: explosive damage (mod 3..7) launches the body along the hit direction
    // (z * 1.2 up / 0.8 down; speed damage * 2.2 clamped to 130..180 for mod 7, else * 1.4
    // to 100..300; location 4); other kinds only with 0x0095f700 (from the hit point, z * 1.2,
    // speed 40..100). Unless already dying (state 7): records damage / mod / direction, the
    // hit yaw relative to the entity (+0x230), the hit location's anim (+0x240 from
    // 0x0095c0b0), forces state 7, attacker -> entity +0x374, clears the entity's +0x264.
    uint8_t* const actor = field<uint8_t*>(self, 0x240);
    if (mod) {
        alignas(16) float base[4];
        alignas(16) float push[4];
        if (mod > 2 && mod <= 7) {
            const float z = dir[2];
            const float up = z > title_global<float>(0x0083eafc) ? z * title_global<float>(0x00830f60)   // 1.2
                                                                  : z * title_global<float>(0x0083ebc0);  // 0.8
            push[0] = dir[0];
            push[1] = dir[1];
            push[2] = up;
            push[3] = dir[0];
            for (int i = 0; i < 4; ++i) base[i] = title_global<float>(0x0082e290 + i * 4);  // 0 0 0 1
            float speed;
            if (mod == 7) {
                const double scaled = x87_float_mul_double(x87_int_to_float(damage), title_global<double>(0x0083eb08));
                if (title_global<double>(0x0083f170) > scaled)  // 130
                    speed = x87_double_to_float(title_global<double>(0x0083f170));
                else if (scaled > title_global<double>(0x0083f168))  // 180
                    speed = x87_double_to_float(title_global<double>(0x0083f168));
                else
                    speed = x87_double_to_float(scaled);
            } else {
                const float s = float(damage) * title_global<float>(0x0083ee1c);  // 1.4
                const float low = title_global<float>(0x0083eb90), high = title_global<float>(0x0083ed04);  // 100, 300
                speed = low > s ? low : (s > high ? high : s);
            }
            G_PhysicsLaunch(self, base, push, speed, 0, 4);
        } else if (title_global<uint32_t>(0x0095f700)) {
            push[0] = dir[0];
            push[1] = dir[1];
            push[2] = dir[2] * title_global<float>(0x00830f60);
            push[3] = dir[0];
            base[0] = point[0];
            base[1] = point[1];
            base[2] = point[2];
            base[3] = point[0];
            const float s = float(damage);
            const float low = title_global<float>(0x0083ec84), high = title_global<float>(0x0083eb90);  // 40, 100
            const float speed = low > s ? low : (s > high ? high : s);
            G_PhysicsLaunch(self, base, push, speed, 0, uint32_t(hit_location));
        }
    }
    if (field<int32_t>(actor, 8 + field<int32_t>(actor, 0x20) * 4) == 7) return;
    field<int32_t>(actor, 0x22c) = damage;
    field<int32_t>(actor, 0x228) = mod;
    copy3(actor + 0x234, reinterpret_cast<const uint8_t*>(dir));
    const float yaw = vectoyaw(dir);
    const float delta = (yaw - field<float>(self, 0x154)) * title_global<float>(0x0083ebd8);  // 65536 / 360
    float angle = float(int32_t(delta) & 0xffff) * title_global<float>(0x0083ebd4);  // 360 / 65536
    if (angle > title_global<float>(0x0083eb60)) angle = angle - title_global<float>(0x0083ebcc);  // 180, 360
    const int32_t relative = int32_t(angle);
    field<int32_t>(actor, 0x230) = relative;
    const uint32_t anim = title_global<uint32_t>(0x0095c0b0 + hit_location * 4);
    field<uint32_t>(actor, 0x240) = anim;
    const TitleRegs regs = {anim, uint32_t(uintptr_t(self)), uint32_t(relative), uint32_t(hit_location),
                            uint32_t(uintptr_t(actor)), 7};
    g_ent_call(0x002026f0, &regs, 0, nullptr);  // Actor_ForceState (ESI actor, EDI state)
    field<uint32_t>(actor, 0x2d0) = 0;
    field<uint32_t>(self, 0x374) = attacker;
    title_function<void XB_THISCALL(uint8_t*)>(0x0011d660)(self + 0x264);  // ScriptString_Clear
}

PLATFORM_ASM_ENTRY(Sentient_GetLaggedEyeOrigin_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Sentient_GetLaggedEyeOrigin) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Sentient_GetLaggedEyeOrigin(const uint8_t* sentient, uint8_t* out, int32_t* time) {
    // EAX sentient, ECX out, ESI out time: a client's eye one frame back from the eye history
    // (0x0095f428 index, origins 0x0095f42c, times 0x0095f444; slot 1 when the index is 0);
    // else the current eye origin at the level time.
    if (field<uint32_t>(field<const uint8_t*>(sentient, 0), 0x23c)) {
        int32_t index = title_global<int32_t>(0x0095f428) - 1;
        if (index < 0) index = 1;
        copy3(out, &title_global<uint8_t>(0x0095f42c + index * 12));
        *time = title_global<int32_t>(0x0095f444 + index * 4);
        return;
    }
    Sentient_EyeOrigin(sentient, 0, out);
    *time = level_time();
}

PLATFORM_ASM_ENTRY(Sentient_EyeOrigin_2_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Sentient_EyeOrigin_2) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Sentient_EyeOrigin_2(const uint8_t* sentient, uint8_t* out) {
    // EAX sentient, ESI out: Sentient_EyeOrigin through a stack copy.
    uint8_t eye[12];
    Sentient_EyeOrigin(sentient, 0, eye);
    copy3(out, eye);
}

namespace {

long double x87_abs(long double v) {
    __asm__("fabs" : "+t"(v));
    return v;
}

// An anim's current goal weight (0x00965d14 records of 0x30 bytes) through the tree's index
// map (+0x18), 0 for an unmapped one.
float anim_weight(uint32_t tree, uint32_t index) {
    const uint16_t anim = title_global<uint16_t>(tree + 0x18 + index * 2);
    return anim ? title_global<float>(0x00965d14 + anim * 0x30) : 0.0f;
}

void set_goal_weight(uint32_t index, uint32_t tree, float goal, float blend) {
    uint32_t goal_bits, blend_bits;
    std::memcpy(&goal_bits, &goal, 4);
    std::memcpy(&blend_bits, &blend, 4);
    AnimTree_SetGoalWeight(index, reinterpret_cast<uint8_t*>(uintptr_t(tree)), goal_bits, blend_bits, 0x3f800000, 0,
                           0, 0);
}

// Blends an anim towards `goal` over `time` scaled by the distance from its weight (x87).
void blend_to(uint32_t index, uint32_t tree, float goal, float time) {
    const float current = anim_weight(tree, index);
    if (goal == current) return;
    const float blend = float(static_cast<long double>(time) /
                              x87_abs(static_cast<long double>(goal) - static_cast<long double>(current)));
    set_goal_weight(index, tree, goal, blend);
}

}  // namespace

PLATFORM_ASM_ENTRY(Actor_UpdateLookAt_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Actor_UpdateLookAt) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_UpdateLookAt(uint8_t* actor, float dt) {
    // EBX actor, stack frame time: with look-at on (+0x204) and active (+0x1e8 with yaw limits
    // +0x208/+0x20c both set, or a yaw still turned +0x1f8): the yaw to the target (+0x1ec,
    // from the eye, relative to the entity, clamped to the limit +0x20c shrunk by +0x224 per
    // ms until +0x21c) is approached by the turn yaw +0x1f8 with speed +0x1fc (acceleration
    // +0x200, capped at 0.22 * +0x200, braking near the goal; snaps within 1 degree). The
    // turn yaw over the limit +0x208 (shrunk by +0x220) gives a weight in -1..1: the left /
    // right look anims (+0x218 / +0x214) blend to it (the other to 0) and the straight one
    // (+0x210) to 1 - |weight|, over 1.5 * dt.
    if (!field<uint32_t>(actor, 0x204)) return;
    const float zero = 0.0f;
    const bool limited =
        field<uint32_t>(actor, 0x1e8) && field<float>(actor, 0x208) != zero && field<float>(actor, 0x20c) != zero;
    if (!limited && field<float>(actor, 0x1f8) == zero) return;
    const float k180 = title_global<float>(0x0083eb60), minus_one = title_global<float>(0x0083eb34);
    int32_t left = field<int32_t>(actor, 0x21c) - level_time();
    if (left < 0) left = 0;
    const float shrunk = field<float>(actor, 0x20c) - float(left) * field<float>(actor, 0x224);
    float limit = shrunk;
    if (zero > shrunk)
        limit = zero;
    else if (shrunk > k180)
        limit = k180;
    float eye[3];
    Sentient_EyeOrigin(field<const uint8_t*>(actor, 4), 0, reinterpret_cast<uint8_t*>(eye));
    float delta[3];
    for (int i = 0; i < 3; ++i) delta[i] = field<float>(actor, 0x1ec + i * 4) - eye[i];
    vectoangles_call(delta, eye);
    float goal = zero;
    if (limited) {
        goal = Q_fmod(field<float>(field<const uint8_t*>(actor, 0), 0x154), eye[1]);
        const float low = zero - limit;
        if (low > goal)
            goal = low;
        else if (goal > limit)
            goal = limit;
    }
    const long double offset = static_cast<long double>(goal) - static_cast<long double>(field<float>(actor, 0x1f8));
    const float diff_stored = float(offset);
    bool snap = static_cast<long double>(title_global<float>(0x0083eb20)) > x87_abs(offset);
    if (!snap) {
        float speed = field<float>(actor, 0x1fc), d = goal, diff = diff_stored;
        bool flipped = false;
        if (zero > d || (!(d != zero) && zero > diff)) {
            speed = speed * minus_one;
            d = d * minus_one;
            diff = diff * minus_one;
            flipped = true;
        }
        const float k10 = title_global<float>(0x0083eb98), k01 = title_global<float>(0x0083ebf8);
        const float twice = dt * title_global<float>(0x0083eba8);  // 2
        float next;
        if (zero > diff) {
            const float reach = twice * speed * minus_one;
            if (!(diff >= reach)) {
                next = speed - field<float>(actor, 0x200) * dt;
            } else {
                next = (dt * k10 + title_global<float>(0x0083eb20)) * speed;
                if (next > title_global<float>(0x0083ed10)) {  // -10
                    next = title_global<float>(0x0083ed10);
                } else if (twice * next * minus_one > diff) {
                    next = diff / twice * minus_one - k01;
                }
            }
        } else {
            if (!(twice * speed >= diff)) {
                next = field<float>(actor, 0x200) * dt + speed;
            } else {
                next = speed - dt * k10 * speed;
                if (k10 > next)
                    next = k10;
                else if (diff > twice * next)
                    next = diff / twice + k01;
            }
        }
        const float cap = field<float>(actor, 0x200) * title_global<float>(0x0083f15c);  // 0.22
        if (next > cap) next = cap;
        if (flipped) {
            next = next * minus_one;
            d = d * minus_one;
        }
        const float old = field<float>(actor, 0x1f8);
        field<float>(actor, 0x1fc) = next;
        const float turned = next * dt + field<float>(actor, 0x1f8);
        field<float>(actor, 0x1f8) = turned;
        snap = d > old ? turned >= d : d >= turned;
        goal = d;
    }
    if (snap) {
        field<float>(actor, 0x1f8) = goal;
        field<float>(actor, 0x1fc) = zero;
    }
    const uint32_t tree = G_GetEntityAnimTree(field<const uint8_t*>(actor, 0));
    left = field<int32_t>(actor, 0x21c) - level_time();
    if (left < 0) left = 0;
    float range = field<float>(actor, 0x208) - float(left) * field<float>(actor, 0x220);
    if (zero > range)
        range = zero;
    else if (range > k180)
        range = k180;
    float weight = field<float>(actor, 0x1f8) / range * title_global<float>(0x0083eb24);  // 0.5
    const float one = title_global<float>(0x0083eb20);
    if (weight > one)
        weight = one;
    else if (minus_one > weight)
        weight = minus_one;
    const float time = dt * title_global<float>(0x0083eaf4);  // 1.5
    float straight;
    if (weight > zero) {
        blend_to(field<uint16_t>(actor, 0x218), tree, weight, time);
        const uint32_t other = field<uint16_t>(actor, 0x214);
        const float current = anim_weight(tree, other);
        if (title_global<uint16_t>(tree + 0x18 + other * 2) && current != zero)
            set_goal_weight(other, tree, 0.0f, time / current);
        straight = weight;
    } else {
        const float turned = weight * minus_one;
        blend_to(field<uint16_t>(actor, 0x214), tree, turned, time);
        const uint32_t other = field<uint16_t>(actor, 0x218);
        const float current = anim_weight(tree, other);
        if (title_global<uint16_t>(tree + 0x18 + other * 2) && current != zero)
            set_goal_weight(other, tree, 0.0f, time / current);
        straight = turned;
    }
    blend_to(field<uint16_t>(actor, 0x210), tree, one - straight, time);
}

namespace {

// The title CRT's __CIsqrt of a float, stored as float.
float ci_sqrt(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

int32_t shot_rand() { return title_function<int32_t XB_CDECL()>(0x001efcc0)(); }  // _rand

// The shot's sideways axis: the unit direction from the muzzle (+0x24) to `eye` crossed with
// the aim (+0x18), normalized (vectors of length 0 kept as they are). `dir` gets the first.
void shot_side_axis(const uint8_t* shot, const float* eye, float* dir, float* side) {
    const float z = eye[2] - field<float>(shot, 0x2c);
    const float y = eye[1] - field<float>(shot, 0x28);
    const float x = eye[0] - field<float>(shot, 0x24);
    const float len = ci_sqrt(z * z + y * y + x * x);
    float nx = x, ny = y, nz = z;
    if (len != 0.0f) {
        const float inv = title_global<float>(0x0083eb20) / len;
        nx = inv * x;
        ny = inv * y;
        nz = inv * z;
    }
    dir[0] = nx;
    dir[1] = ny;
    dir[2] = nz;
    const float rx = field<float>(shot, 0x18), ry = field<float>(shot, 0x1c), rz = field<float>(shot, 0x20);
    const float cx = ny * rz - nz * ry;
    const float cy = rx * nz - nx * rz;
    const float cz = nx * ry - rx * ny;
    const float side_len = ci_sqrt(cz * cz + cy * cy + cx * cx);
    side[0] = cx;
    side[1] = cy;
    side[2] = cz;
    if (side_len != 0.0f) {
        const float inv = title_global<float>(0x0083eb20) / side_len;
        side[0] = inv * cx;
        side[1] = inv * cy;
        side[2] = inv * cz;
    }
}

// The engagement distance: an actor's 0x0098a558 - 64, a player's 0x00984d08 - client +0x428.
float shot_range(const uint8_t* ent) {
    if (field<uint32_t>(ent, 0x240)) return title_global<float>(0x0098a558) - title_global<float>(0x00830c1c);
    return title_global<float>(0x00984d08) - float(field<int32_t>(field<const uint8_t*>(ent, 0x23c), 0x428));
}

// The shot direction: from the muzzle to `point` + aim * distance, normalized.
void shot_finish(uint8_t* shot, const float* point, float distance) {
    const float z = field<float>(shot, 0x20) * distance + point[2] - field<float>(shot, 0x2c);
    const float y = field<float>(shot, 0x1c) * distance + point[1] - field<float>(shot, 0x28);
    const float x = field<float>(shot, 0x18) * distance + point[0] - field<float>(shot, 0x24);
    field<float>(shot, 8) = z;
    field<float>(shot, 4) = y;
    field<float>(shot, 0) = x;
    const float len = ci_sqrt(z * z + y * y + x * x);
    if (len == 0.0f) return;
    const float inv = title_global<float>(0x0083eb20) / len;
    field<float>(shot, 0) = x * inv;
    field<float>(shot, 4) = y * inv;
    field<float>(shot, 8) = z * inv;
}

// A signed unit from the LCG 0x008db488 (x * 0x10dcd + 1, low 16 bits / 65536 > 0.5).
int32_t lcg_sign(uint32_t value) {
    return static_cast<long double>(x87_int_to_float(int32_t(value & 0xffff))) / 65536.0L >
                   static_cast<long double>(title_global<double>(0x0083ecb0))
               ? 1
               : -1;
}

}  // namespace

PLATFORM_ASM_ENTRY(Actor_ShotDirMissTarget_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_ShotDirMissTarget) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_ShotDirMissTarget(const uint8_t* target, uint8_t* shot) {
    // EDI target sentient, ESI shot record (aim +0x18, muzzle +0x24; direction out at +0): a
    // shot that misses - aimed past the target's eye (0x00984d00 + 1 back towards the muzzle)
    // and to one side: a player by 8..16 units, others by the cached spread 0x008ff048
    // (sqrt(6) * 0x0098a554), signs from the LCG 0x008db488; then along the aim by the
    // engagement distance minus 0..44 (player) or by -12..12.
    if (!(title_global<float>(0x008ff048) != title_global<float>(0x0083ee24))) {  // 6969 = not computed
        long double spread = static_cast<long double>(title_global<float>(0x0098a554)) * title_global<float>(0x0098a554);
        spread = spread + spread;
        spread = spread * static_cast<long double>(title_global<float>(0x0083ec9c));  // 3
        title_global<float>(0x008ff048) = ci_sqrt(float(spread));
    }
    const uint8_t* const ent = field<const uint8_t*>(target, 0);
    float eye[3];
    Sentient_EyeOrigin(target, 0, reinterpret_cast<uint8_t*>(eye));
    float dir[3], side[3];
    shot_side_axis(shot, eye, dir, side);
    const float one = title_global<float>(0x0083eb20);
    float point[3];
    float distance;
    uint32_t seed = title_global<uint32_t>(0x008db488);
    if (field<uint32_t>(ent, 0x23c)) {
        seed = seed * 0x10dcd + 1;
        const float sign = float(lcg_sign(seed));
        seed = seed * 0x10dcd + 1;
        title_global<uint32_t>(0x008db488) = seed;
        const float u = float(int32_t(seed & 0xffff)) / title_global<float>(0x0083eb40);
        const float offset = (u + one) * title_global<float>(0x0083ed78) * sign;  // 8
        const float back = 0.0f - (title_global<float>(0x00984d00) + one);
        const float px = back * dir[0] + eye[0];
        const float py = back * dir[1] + eye[1];
        const float pz = back * dir[2] + eye[2];
        point[0] = side[0] * offset + px;
        point[1] = side[1] * offset + py;
        point[2] = side[2] * offset + pz;
        const float range = shot_range(field<const uint8_t*>(target, 0));
        distance = range - float(shot_rand()) / title_global<float>(0x0083ebc8) * title_global<float>(0x0083f160);
    } else {
        seed = seed * 0x10dcd + 1;
        title_global<uint32_t>(0x008db488) = seed;
        const int32_t sign = lcg_sign(seed);
        const float back = 0.0f - (title_global<float>(0x00984d00) + one);
        const float px = back * dir[0] + eye[0];
        const float py = back * dir[1] + eye[1];
        const float pz = back * dir[2] + eye[2];
        const float offset = float(sign) * title_global<float>(0x008ff048);
        point[0] = offset * side[0] + px;
        point[1] = offset * side[1] + py;
        point[2] = offset * side[2] + pz;
        distance = (float(shot_rand()) / title_global<float>(0x0083ebc8) * title_global<float>(0x0083eba8) - one) *
                   title_global<float>(0x0083eccc);  // 12
    }
    shot_finish(shot, point, distance);
}

PLATFORM_ASM_ENTRY(Actor_ShotDirToTarget_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_ShotDirToTarget) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_ShotDirToTarget(const uint8_t* target, uint8_t* shot) {
    // EAX target sentient, ESI shot record: a shot at the target's eye scattered sideways (a
    // player by -8..8 units, others per axis by -1..1 * 0x0098a554), then along the aim by
    // the engagement distance minus 0..44.
    float eye[3];
    Sentient_EyeOrigin(target, 0, reinterpret_cast<uint8_t*>(eye));
    const uint8_t* const ent = field<const uint8_t*>(target, 0);
    const float range = shot_range(ent);
    float dir[3], side[3];
    shot_side_axis(shot, eye, dir, side);
    const float scale = title_global<float>(0x0083ebc8), two = title_global<float>(0x0083eba8);
    const float one = title_global<float>(0x0083eb20);
    if (field<uint32_t>(ent, 0x23c)) {
        const float f = (float(shot_rand()) / scale * two - one) * title_global<float>(0x0083ed78);  // 8
        eye[0] = side[0] * f + eye[0];
        eye[1] = side[1] * f + eye[1];
        eye[2] = side[2] * f + eye[2];
    } else {
        for (int i = 0; i < 3; ++i)
            eye[i] = (float(shot_rand()) / scale * two - one) * title_global<float>(0x0098a554) * side[i] + eye[i];
    }
    const float distance = range - float(shot_rand()) / scale * title_global<float>(0x0083f160);  // 44
    shot_finish(shot, eye, distance);
}

}  // namespace game
