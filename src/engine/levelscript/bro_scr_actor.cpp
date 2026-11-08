#include "bro_scr_actor.h"

#include <cstring>

#include "bro_scr_common.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

namespace {


float& AnimStateFloat(uint32_t offset) {
    return *reinterpret_cast<float*>(title_global<uint32_t>(0x00922efc) + offset);
}

uint32_t addr32(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

bool NodeValid(uint32_t node) {
    return node != 0 && node != 0xffffffff;
}

// Engine slots returning a pointer to their out vector.
using VecOfNode = const uint32_t* XB_CDECL(uint32_t*, uint32_t);

}  // namespace

// Entries that move the C frame below a gap and hand over the entry ESP: GScr_ActorUpdateNearestNode
// keeps a frame address in 0x00929b40, so the call chain down to it runs at the original's ESP.
#define BRO_SCR_ESP_ENTRY(name)                                                                     \
    PLATFORM_ASM_ENTRY(name##_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"     \
                                     PLATFORM_ASM_CALL(name##_c) "addl $4, %esp\n popl %esp\n ret")

BRO_SCR_ESP_ENTRY(BroScr_GetNodeAtPosition)
BRO_SCR_ESP_ENTRY(BroScr_GetNodeOrActorYaw)
BRO_SCR_ESP_ENTRY(BroScr_GetCoverNodeForward)

namespace {

template <typename T>
T& at(uint32_t address) {
    return *reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A title call at the original's ESP (`top`: ESP before the argument pushes); the return address is
// written back into the dead-stack slot as the original's call leaves it.
uint32_t call_at(uint32_t top, uint32_t ret, uint32_t function, uint32_t count, const uint32_t* args) {
    const TitleRegs regs{};
    const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
    at<uint32_t>(top - count * 4 - 4) = ret;
    return result;
}

// BroScr_GetNodeAtPosition(out = the caller's argument slot E+8, actor) at the caller's ESP.
uint32_t node_at(uint32_t top, uint32_t ret, uint32_t entry_esp, uint32_t actor) {
    const uint32_t args[2] = {entry_esp + 8, actor};
    call_at(top, ret, 0x00184230, 2, args);
    return at<uint32_t>(entry_esp + 8);
}

}  // namespace

uint32_t XB_CDECL BroScr_GetNodeAtPosition_c(uint32_t entry_esp) {
    const uint32_t E = entry_esp;
    uint32_t* const out = at<uint32_t*>(E + 4);
    const uint32_t actor = at<uint32_t>(E + 8);
    uint32_t ref[4];
    const uint32_t first = *scr_slot<const uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922de0)(ref, actor);  // GetRef_48
    const uint32_t second = *scr_slot<const uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922de0)(ref, actor);
    uint32_t node = first;
    if (!NodeValid(second)) {
        const uint32_t args[1] = {actor};
        node = call_at(E - 0x3c, 0x00184268, title_global<uint32_t>(0x00922784), 1, args);  // GScr_ActorUpdateNearestNode
        if (node == 0xffffffff) node = 0;
    }
    if (NodeValid(node)) {
        uint32_t temp1[3], temp2[3], actor_origin[3], node_origin[3];
        const uint32_t* v = scr_slot<VecOfNode>(0x00922b20)(temp1, actor);  // ScrEnt_GetOrigin
        actor_origin[0] = v[0];
        actor_origin[1] = v[1];
        actor_origin[2] = v[2];
        v = scr_slot<VecOfNode>(0x00922ab8)(temp2, node);  // ScrPathNode_GetOrigin
        node_origin[0] = v[0];
        node_origin[1] = v[1];
        node_origin[2] = v[2];
        // Scr_Distance's x87 result is compared unrounded with 16 (FCOMIP).
        const long double distance =
            scr_slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(node_origin, actor_origin);
        if (distance > 16.0L) node = 0;
    }
    *out = node;
    return addr32(out);
}

uint32_t XB_CDECL BroScr_GetNodeOrActorYaw_c(uint32_t entry_esp) {
    const uint32_t E = entry_esp;
    float* const out = at<float*>(E + 4);
    const uint32_t actor = at<uint32_t>(E + 8);
    const uint32_t node = node_at(E - 0x1c, 0x00184327, E, actor);  // the node lands in the actor's slot
    if (NodeValid(node)) {
        uint32_t temp[3];
        const uint32_t* angles = scr_slot<VecOfNode>(0x00922ac0)(temp, node);  // ScrPathNode_GetAngles
        std::memcpy(out, &angles[1], 4);
    } else {
        *out = scr_slot<float XB_CDECL(uint32_t)>(0x00922d00)(actor);  // ScrActor_GetFloat_154 (FSTP)
    }
    return addr32(out);
}

uint32_t XB_CDECL BroScr_GetCoverNodeForward_c(uint32_t entry_esp) {
    const uint32_t E = entry_esp;
    uint32_t* const out = at<uint32_t*>(E + 4);
    const uint32_t node = node_at(E - 0x24, 0x001843a6, E, at<uint32_t>(E + 8));
    if (NodeValid(node)) {
        uint32_t temp[3], angles[3];
        const uint32_t* v = scr_slot<VecOfNode>(0x00922ac0)(temp, node);  // ScrPathNode_GetAngles
        angles[0] = v[0];
        angles[1] = v[1];
        angles[2] = v[2];
        const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
        uint32_t forward[3] = {lane, lane, lane};
        scr_slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, angles);  // Scr_AnglesToForward
        out[0] = forward[0];
        out[1] = forward[1];
        out[2] = forward[2];
    } else {
        out[0] = title_global<uint32_t>(0x00922ec4);  // the undefined vector
        out[1] = title_global<uint32_t>(0x00922ec8);
        out[2] = title_global<uint32_t>(0x00922ecc);
    }
    return addr32(out);
}

uint32_t* XB_CDECL BroScr_YawToDirectionName(uint32_t* out, float yaw) {
    const float f = static_cast<float>(static_cast<int32_t>(yaw) % 360);
    if (45.0f > f || f > 315.0f)
        *out = 0x0658b1b4;  // "front"
    else if (135.0f > f)
        *out = 0x003798b4;  // "left"
    else if (225.0f > f)
        *out = 0x0030880b;  // "back"
    else
        *out = 0x07151174;  // "right"
    return out;
}

void XB_CDECL BroScr_SetDirectionBlendWeights(uint32_t, float yaw) {
    float angle = static_cast<float>(static_cast<int32_t>(yaw) % 360);
    if (angle > 180.0f) angle = angle - 360.0f;
    float a = 0, b = 0;  // SinCosDegrees(angle, &a, &b): b (the argument slot in the original) first
    scr_slot<void XB_CDECL(float, float*, float*)>(0x0092223c)(angle, &a, &b);
    if (b > 0.0f) {
        AnimStateFloat(0x13c) = b;
        AnimStateFloat(0x140) = 0.0f;
    } else {
        AnimStateFloat(0x140) = platform_mulss(b, -1.0f);
        AnimStateFloat(0x13c) = 0.0f;
    }
    if (a > 0.0f) {
        AnimStateFloat(0x148) = a;
        AnimStateFloat(0x144) = 0.0f;
    } else {
        AnimStateFloat(0x144) = platform_mulss(a, -1.0f);
        AnimStateFloat(0x148) = 0.0f;
    }
}

uint32_t* XB_CDECL BroScr_GetPlayerStanceOffset(uint32_t* out) {
    ScriptString stance;
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d5d0)(&stance, 0);  // ScriptString_FromRep
    scr_slot<void XB_CDECL(uint32_t, ScriptString*)>(0x00922600)(title_global<uint32_t>(0x009236dc), &stance);  // GScr_GetStance
    using Equals = bool XB_CDECL(const ScriptString*, uint32_t);
    uint32_t z;
    if (title_function<Equals>(0x0011d7c0)(&stance, 0x008367fc)) {  // "prone"
        out[0] = 0;
        out[1] = 0;
        z = 0x40a00000;  // 5
    } else {
        const bool crouch = title_function<Equals>(0x0011d7c0)(&stance, 0x0083ae7c);  // "crouch"
        out[1] = 0;
        out[0] = 0;
        z = crouch ? 0x41c80000 : 0x42480000;  // 25 : 50
    }
    out[2] = z;
    scr_release(&stance);
    return out;
}

uint32_t XB_CDECL BroScr_LoadGmiInitLevelVars() {
    // The level object (0x00922ef8) is re-read before each store.
    auto field = [](uint32_t offset) -> uint32_t& {
        return *reinterpret_cast<uint32_t*>(title_global<uint32_t>(0x00922ef8) + offset);
    };
    field(0x88) = 10;
    field(0x200) = 0;
    field(0xe0) = 0;
    field(0x198) = 0;
    field(0xdc) = 0;
    field(0xf4) = 0;
    *reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x00922ef8) + 0xf0) = 0;
    return 0;
}

uint32_t* XB_THISCALL BroScr_GetSentientRef3c(const uint32_t* self, uint32_t* out) {
    uint32_t value;
    const uint32_t* ref = scr_slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922e00)(&value, *self);  // ScrSentient_GetRef_3c
    *out = *ref;
    return out;
}

uint32_t* XB_CDECL BroScr_GetEyePos(uint32_t* out, const uint32_t* ent) {
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t eye[3] = {lane, lane, lane};
    scr_slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922528)(*ent, eye);  // GScr_GetEye
    out[0] = eye[0];
    out[1] = eye[1];
    out[2] = eye[2];
    return out;
}

void XB_CDECL BroScr_DropWeaponRight(uint32_t ent) {
    uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t origin[3] = {lane, lane, lane};
    uint32_t velocity[3] = {0, 0, title_global<uint32_t>(0x0083eb98)};  // (0, 0, 10)
    ScriptString tag;
    scr_string_from(&tag, 0x007f6438);  // "TAG_WEAPON_RIGHT"
    lane = title_global<uint32_t>(0x00922eb8);
    uint32_t pos[3] = {lane, lane, lane};
    scr_slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x0092261c)(ent, pos, &tag);  // GScr_GetTagOrigin
    origin[0] = pos[0];
    origin[1] = pos[1];
    origin[2] = pos[2];
    scr_release(&tag);
    scr_slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t*, uint32_t)>(0x0092260c)(ent, origin, velocity,
                                                                                    0x40400000);  // GScr_MagicGrenadeManual(.., 3.0f)
}

}  // namespace game
