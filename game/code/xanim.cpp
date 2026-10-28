#include "xanim.h"

#include <cstring>

#include "dobj.h"
#include "g_scr_runtime.h"
#include "nal_anim.h"
#include "nal_math.h"
#include "qcommon.h"
#include "script_string.h"
#include "stream.h"
#include "title_data.h"
#include "tr_main.h"

namespace game {

namespace {

// Anim data (an entry of an anim-tree table, the tree's +8): +4 node count, +8 nodes (0x38
// bytes each: +0x20 child count, +0x22 parent, +0x28 notetracks, +0x34 flags, +0x36 first
// child). Indexes past the count read node 0, as the title does.
uint8_t* anim_node(const uint8_t* anims, uint32_t index) {
    const uint32_t count = *reinterpret_cast<const uint32_t*>(anims + 4);
    const uint32_t nodes = *reinterpret_cast<const uint32_t*>(anims + 8);
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(nodes + (index < count ? index : 0) * 0x38));
}

uint8_t* tree_anims(const uint8_t* tree) {
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(tree + 8)));
}

// The tree's anim info of node `index` (+0x18: one 16-bit info index per node, 0 = none) in
// the info table 0x00965cf0 (0x30 bytes each: +0 notetrack node, +2 next notetrack, +4 anim,
// +0x10 time, +0x14 previous time, +0x18/+0x1a time and previous time as frames, +0x20 goal
// weight, +0x24 weight, +0x2c track).
uint16_t info_index(const uint8_t* tree, uint32_t index) { return *reinterpret_cast<const uint16_t*>(tree + 0x18 + index * 2); }

uint8_t* info_at(uint32_t info) { return &title_global<uint8_t>(0x00965cf0 + info * 0x30); }

float& info_float(uint8_t* info, uint32_t offset) { return *reinterpret_cast<float*>(info + offset); }

uint16_t u16_at(const uint8_t* p, uint32_t offset) { return *reinterpret_cast<const uint16_t*>(p + offset); }

uint8_t* current_tree() { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00928588))); }

// 0x00022df0 Scr_NotifyWithString: entity in EBX, (notify, string) on the stack, callee pops 8.
extern "C" PLATFORM_HIDDEN void XB_CDECL anim_notify_with_string_call(uint32_t ent, uint32_t notify, uint32_t string);
PLATFORM_ASM_ENTRY(anim_notify_with_string_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "movl 16(%esp), %eax\n"
                   "movl 12(%esp), %edx\n"
                   "pushl %eax\n"
                   "pushl %edx\n"
                   "movl $0x00022df0, %ecx\n"
                   "call *%ecx\n"
                   "popl %ebx\n"
                   "ret")

}  // namespace

uint32_t XB_REGS3 XB_CDECL AnimTree_IsLeafOrSynced(uint32_t index, uint32_t, const uint8_t* anims) {
    const uint8_t* node = anim_node(anims, index);
    return (!u16_at(node, 0x20) || (node[0x34] & 3)) ? 1 : 0;
}

uint8_t XB_CDECL AnimTree_HasActiveLeaf_r(const uint8_t* tree, uint32_t index) {
    const uint16_t info = info_index(tree, index);
    if (!info) return 0;
    if (info_float(info_at(info), 0x24) == 0.0f) return 0;  // NaN counts as active
    const uint8_t* node = anim_node(tree_anims(tree), index);
    const int32_t count = u16_at(node, 0x20);
    if (!count) return 1;
    for (int32_t i = 0; i < count; ++i)
        if (AnimTree_HasActiveLeaf_r(tree, u16_at(node, 0x36) + i)) return 1;
    return 0;
}

PLATFORM_ASM_ENTRY(AnimTree_ParentsActive_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(AnimTree_ParentsActive) "addl $8, %esp\n"
                   "ret")

uint8_t XB_CDECL AnimTree_ParentsActive(uint32_t index, const uint8_t* tree) {
    while (index) {
        const uint8_t* node = anim_node(tree_anims(tree), index);
        index = u16_at(node, 0x22);
        if (info_float(info_at(info_index(tree, index)), 0x24) == 0.0f) return 0;
    }
    return 1;
}

PLATFORM_ASM_ENTRY(AnimTree_CheckLeafNotes_entry,
                   "subl $4, %esp\n" PLATFORM_ASM_CALL(AnimTree_CheckLeafNotes) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL AnimTree_CheckLeafNotes() {
    // The original tests 0x0095f51c and the anim's +4 and loads its stack argument, then
    // returns 1 whatever they hold: the notetrack check compiled out of the retail build.
    return title_global<float>(0x0083eb20);
}

PLATFORM_ASM_ENTRY(AnimData_FindNotetrack_entry,
                   "subl $4, %esp\n"
                   "movss %xmm2, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimData_FindNotetrack) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL AnimData_FindNotetrack(const uint8_t* node, float time) {
    // Notetracks: 12-byte records {?, name, time}, ended by a zero name. The first one at or
    // after `time` (the earliest of them); none = index -1 (only AX is set).
    const uint32_t notes = *reinterpret_cast<const uint32_t*>(node + 0x28);
    if (!notes) return (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node)) & 0xffff0000u) | 0xffffu;
    float best = title_global<float>(0x0083eba8);
    uint32_t found = 0;
    for (uint32_t note = notes; *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(note + 4)); note += 0xc) {
        const float t = *reinterpret_cast<const float*>(static_cast<uintptr_t>(note + 8));
        if (time > t) continue;
        if (best > t) {
            best = t;
            found = note;
        }
    }
    return static_cast<uint32_t>(static_cast<int32_t>(found - notes) / 12);
}

void XB_REGS2 XB_CDECL AnimTree_UpdateNoteIndex(uint32_t, uint32_t index) {
    const uint8_t* tree = current_tree();
    uint8_t* info = info_at(info_index(tree, index));
    if (!*reinterpret_cast<const uint32_t*>(info + 4)) return;
    if (info_float(info, 0x10) == title_global<float>(0x0083eb20)) {
        *reinterpret_cast<uint16_t*>(info + 2) = 0xffff;
        return;
    }
    const uint8_t* anims = tree_anims(tree);
    const uint8_t* node = anim_node(anims, index);
    if (u16_at(node, 0x20)) {
        const uint16_t note_node = u16_at(info, 0);
        if (!note_node) return;
        node = anim_node(anims, note_node);
    }
    *reinterpret_cast<uint16_t*>(info + 2) = static_cast<uint16_t>(AnimData_FindNotetrack(node, info_float(info, 0x10)));
}

uint32_t XB_REGS3 XB_CDECL AnimTree_HasAnimEnded(uint32_t, uint32_t, const uint8_t* tree, uint32_t index) {
    const uint16_t info_id = info_index(tree, index & 0xffff);
    if (!info_id) return 1;
    uint8_t* info = info_at(info_id);
    if (info_float(info, 0x14) > info_float(info, 0x10)) return 1;  // wrapped around
    if (info_float(info, 0x10) == title_global<float>(0x0083eb20)) return 1;
    if (static_cast<int16_t>(u16_at(info, 0x18)) > static_cast<int16_t>(u16_at(info, 0x1a))) return 1;
    return 0;
}

void XB_REGS3 XB_CDECL AnimTree_NotifyNotetrack(uint32_t string, uint8_t* ent, const uint32_t* notify, const uint32_t* plain) {
    // The notetrack's notify with its name as the argument, then the plain one.
    anim_notify_with_string_call(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent)), *notify, string);
    Scr_Notify(ent, *plain);
}

void XB_CDECL AnimTree_SetTime_r(uint32_t index) {
    const uint8_t* tree = current_tree();
    const uint16_t info_id = info_index(tree, index);
    if (!info_id) return;
    uint8_t* info = info_at(info_id);
    const uint32_t time = title_global<uint32_t>(0x00927f58);
    const uint16_t frame = title_global<uint16_t>(0x0093ef64);
    *reinterpret_cast<uint32_t*>(info + 0x10) = time;
    *reinterpret_cast<uint16_t*>(info + 0x18) = frame;
    const uint8_t* node = anim_node(tree_anims(tree), index);
    for (int32_t i = 0; i < u16_at(node, 0x20); ++i) AnimTree_SetTime_r(u16_at(node, 0x36) + i);
}

uint32_t XB_CDECL AnimTree_FindHeaviestLeaf_r(uint32_t index) {
    const uint8_t* node = anim_node(tree_anims(current_tree()), index);
    if (!u16_at(node, 0x20)) return index;
    float best = 0.0f;
    uint32_t result = 0;
    for (int32_t i = 0; i < u16_at(node, 0x20); ++i) {
        const uint32_t child = u16_at(node, 0x36) + i;
        const float weight = info_float(info_at(info_index(current_tree(), child)), 0x20);
        if (best >= weight) continue;
        if (const uint32_t leaf = AnimTree_FindHeaviestLeaf_r(child)) {
            best = weight;
            result = leaf;
        }
    }
    return result;
}

uint8_t XB_CDECL XAnim_HasNotetrack(uint32_t handle, const uint32_t* name) {
    // rm_animTrees (0x0095f524) +4: {count, 12-byte anim data entries}; the handle's high
    // half picks the entry, the low half the node.
    const uint8_t* table = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
        *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f524) + 4))));
    const uint32_t count = *reinterpret_cast<const uint32_t*>(table);
    const uint32_t entry = handle >> 16 < count ? handle >> 16 : 0;
    const uint8_t* anims = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
        *reinterpret_cast<const uint32_t*>(table + 4) + entry * 12));
    const uint8_t* node = anim_node(anims, handle & 0xffff);
    const uint32_t notes = *reinterpret_cast<const uint32_t*>(node + 0x28);
    if (!notes) return 0;
    for (uint32_t note = notes + 4; *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(note)); note += 0xc)
        if (*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(note)) == *name) return 1;
    return 0;
}

}  // namespace game

namespace game {

namespace {

// 0x002938f0 __break (int3; ret): the title's assertion trap.
template <typename... Args>
void anim_break(Args... args) {
    title_function<void XB_CDECL(Args...)>(0x002938f0)(args...);
}

}  // namespace

void XB_CDECL AnimTree_SetSyncFlag_r(const uint8_t* anims, uint32_t index, uint32_t flag) {
    uint8_t* node = anim_node(anims, index);
    const int32_t count = u16_at(node, 0x20);
    if (!count) return;
    if (node[0x34] & 3) {
        // A synced node inside a synced subtree: reported with the anim data's name, the
        // depth and the first leaf's name, and the flag then goes on that leaf.
        const uint32_t node_count = *reinterpret_cast<const uint32_t*>(anims + 4);
        uint32_t depth = 0;
        do {
            const uint32_t child = u16_at(node, 0x36);
            ++depth;
            node = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(anims + 8) +
                                                                     (child < node_count ? child : 0) * 0x38));
        } while (u16_at(node, 0x20));
        const uint32_t anim = *reinterpret_cast<const uint32_t*>(node + 0x24);
        anim_break(1u, 0x008329e0u, *reinterpret_cast<const uint32_t*>(anims), depth, anim ? anim + 0xc : 0x00832aecu);
    }
    *reinterpret_cast<uint16_t*>(node + 0x34) |= static_cast<uint16_t>((static_cast<uint8_t>(flag) == 0) + 1);
    for (int32_t i = 0; i < count; ++i) AnimTree_SetSyncFlag_r(anims, u16_at(node, 0x36) + i, flag);
}

void XB_CDECL AnimTree_MarkSync_r(const uint8_t* anims, uint32_t index) {
    if (!*reinterpret_cast<const uint32_t*>(anims + 4)) return;
    uint8_t* node = anim_node(anims, index);
    const int32_t count = u16_at(node, 0x20);
    if (!count) return;
    const uint32_t sync = node[0x34] & 3;
    if (!sync) {
        for (int32_t i = 0; i < count; ++i) AnimTree_MarkSync_r(anims, u16_at(node, 0x36) + i);
        return;
    }
    if (sync == 3) anim_break(1u, 0x008329a8u);
    node[0x34] |= 4;
    // The flag word is the anim data's argument slot with its low byte replaced (only the
    // byte is read).
    const uint32_t flag = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(anims)) & 0xffffff00u) | (sync == 1 ? 1u : 0u);
    for (int32_t i = 0; i < count; ++i) AnimTree_SetSyncFlag_r(anims, u16_at(node, 0x36) + i, flag);
}

void XB_CDECL AnimTree_SyncChildTimes_r(uint32_t index, const uint8_t* src) {
    const uint8_t* node = anim_node(tree_anims(current_tree()), index);
    for (int32_t i = 0; i < u16_at(node, 0x20); ++i) {
        const uint32_t child = u16_at(node, 0x36) + i;
        const uint16_t id = info_index(current_tree(), child);
        if (!id) continue;
        uint8_t* info = info_at(id);
        *reinterpret_cast<uint32_t*>(info + 0x10) = *reinterpret_cast<const uint32_t*>(src + 0x10);
        *reinterpret_cast<uint16_t*>(info + 0x18) = u16_at(src, 0x18);
        *reinterpret_cast<uint32_t*>(info + 0x14) = *reinterpret_cast<const uint32_t*>(src + 0x14);
        *reinterpret_cast<uint16_t*>(info + 0x1a) = u16_at(src, 0x1a);
        AnimTree_UpdateNoteIndex(0, child);
        AnimTree_SyncChildTimes_r(child, src);
    }
}

#define XANIM_XMM0_ENTRY(name, body, args)                                                  \
    PLATFORM_ASM_ENTRY(name##_entry,                                                        \
                       "subl $0x64, %esp\n"                                                 \
                       "movups %xmm2, 0x00(%esp)\n"                                         \
                       "movups %xmm3, 0x10(%esp)\n"                                         \
                       "movups %xmm4, 0x20(%esp)\n"                                         \
                       "movups %xmm5, 0x30(%esp)\n"                                         \
                       "movups %xmm6, 0x40(%esp)\n"                                         \
                       "movups %xmm7, 0x50(%esp)\n"                                         \
                       "leal 0x60(%esp), %eax\n"                                            \
                       "pushl %eax\n" body PLATFORM_ASM_CALL(name) "addl $" args ", %esp\n" \
                       "movss 0x60(%esp), %xmm0\n"                                          \
                       "movups 0x00(%esp), %xmm2\n"                                         \
                       "movups 0x10(%esp), %xmm3\n"                                         \
                       "movups 0x20(%esp), %xmm4\n"                                         \
                       "movups 0x30(%esp), %xmm5\n"                                         \
                       "movups 0x40(%esp), %xmm6\n"                                         \
                       "movups 0x50(%esp), %xmm7\n"                                         \
                       "addl $0x64, %esp\n"                                                 \
                       "ret")

// (info, node, &result): the entry hands the result over in XMM0 and keeps XMM2-7.
XANIM_XMM0_ENTRY(AnimTree_CheckNotes_r, "pushl 0x70(%esp)\n pushl 0x70(%esp)\n", "12")

void XB_CDECL AnimTree_CheckNotes_r(const uint8_t* info, const uint8_t* node, float* result) {
    (void)info;  // the original only loads its +4 under 0x0095f51c: compiled-out notetrack checks
    float least = title_global<float>(0x0083eb20);
    for (int32_t i = 0; i < u16_at(node, 0x20); ++i) {
        const uint32_t child = u16_at(node, 0x36) + i;
        const uint8_t* tree = current_tree();
        const uint16_t id = info_index(tree, child);
        if (!id) continue;
        uint8_t* child_info = info_at(id);
        if (info_float(child_info, 0x24) == 0.0f) continue;
        if (info_float(child_info, 0x20) == 0.0f) continue;
        float r;
        AnimTree_CheckNotes_r(child_info, anim_node(tree_anims(tree), child), &r);
        if (platform_x87_greater(least, r)) least = r;
    }
    *result = least;
}

// (index, &result).
XANIM_XMM0_ENTRY(AnimTree_GetAvgFrequency_r, "pushl 0x6c(%esp)\n", "8")

void XB_CDECL AnimTree_GetAvgFrequency_r(uint32_t index, float* result) {
    const uint8_t* node = anim_node(tree_anims(current_tree()), index);
    const int32_t count = u16_at(node, 0x20);
    if (!count) {
        // A leaf: 1 / its anim's length (+0x38), 1 without an anim, 0 for length 0.
        const uint8_t* anim = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(node + 0x24)));
        if (!anim) {
            *result = title_global<float>(0x0083eb20);
            return;
        }
        const float length = *reinterpret_cast<const float*>(anim + 0x38);
        *result = length == 0.0f ? 0.0f : title_global<float>(0x0083eb20) / length;
        return;
    }
    // The children's frequencies times their rate (+0x28), averaged by weight (+0x24).
    float total_weight = 0.0f;
    float sum = 0.0f;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t child = u16_at(node, 0x36) + i;
        const uint16_t id = info_index(current_tree(), child);
        if (!id) continue;
        const uint8_t* info = info_at(id);
        const float weight = *reinterpret_cast<const float*>(info + 0x24);
        if (weight == 0.0f) continue;
        float frequency;
        AnimTree_GetAvgFrequency_r(child, &frequency);
        const float zero = title_global<float>(0x0083eafc);
        if (platform_x87_greater_equal(frequency, zero) && platform_x87_greater_equal(zero, frequency)) continue;
        total_weight = weight + total_weight;
        sum = *reinterpret_cast<const float*>(info + 0x28) * frequency * weight + sum;
    }
    *result = total_weight == 0.0f ? 0.0f : sum / total_weight;
}

void XB_CDECL AnimTree_ResetTime(uint32_t index, uint32_t keep_active) {
    uint8_t* tree = current_tree();
    // The nearest ancestor (or the node itself) with children and the synced flag (4).
    uint32_t sync = index;
    uint8_t* sync_info = nullptr;
    while (sync) {
        const uint8_t* anims = tree_anims(tree);
        sync_info = info_at(info_index(tree, sync));
        const uint8_t* node = anim_node(anims, sync);
        if (u16_at(node, 0x20) && (node[0x34] & 4)) break;
        sync = u16_at(anim_node(anims, sync), 0x22);
    }
    if (!sync) {
        if (u16_at(anim_node(tree_anims(tree), index), 0x20)) return;
        if (!keep_active && AnimTree_ParentsActive(index, tree) && AnimTree_HasActiveLeaf_r(tree, index)) return;
        uint8_t* info = info_at(info_index(tree, index));
        info_float(info, 0x10) = 0.0f;
        *reinterpret_cast<uint16_t*>(info + 0x18) = 0;
        info_float(info, 0x14) = 0.0f;
        *reinterpret_cast<uint16_t*>(info + 0x1a) = 0;
        return;
    }
    // Synced: the ancestor restarts unless it is still playing, and the nodes from `index` up
    // to it take its time.
    if (keep_active || !AnimTree_ParentsActive(sync, tree) || !AnimTree_HasActiveLeaf_r(tree, sync)) {
        info_float(sync_info, 0x10) = 0.0f;
        *reinterpret_cast<uint16_t*>(sync_info + 0x18) = 0;
        info_float(sync_info, 0x14) = 0.0f;
        *reinterpret_cast<uint16_t*>(sync_info + 0x1a) = 0;
    }
    AnimTree_SyncChildTimes_r(index, sync_info);
    for (uint32_t i = index; i != sync; i = u16_at(anim_node(tree_anims(tree), i), 0x22)) {
        uint8_t* info = info_at(info_index(tree, i));
        *reinterpret_cast<uint32_t*>(info + 0x10) = *reinterpret_cast<const uint32_t*>(sync_info + 0x10);
        *reinterpret_cast<uint16_t*>(info + 0x18) = u16_at(sync_info, 0x18);
        *reinterpret_cast<uint32_t*>(info + 0x14) = *reinterpret_cast<const uint32_t*>(sync_info + 0x14);
        *reinterpret_cast<uint16_t*>(info + 0x1a) = u16_at(sync_info, 0x1a);
        AnimTree_UpdateNoteIndex(0, i);
    }
}

PLATFORM_ASM_ENTRY(AnimTree_ClearGoalWeight_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimTree_ClearGoalWeight) "addl $12, %esp\n"
                   "ret")

void XB_CDECL AnimTree_ClearGoalWeight(const uint8_t* tree, uint32_t index, uint32_t blend_time) {
    const uint16_t id = info_index(tree, index);
    if (!id) return;
    uint8_t* info = info_at(id);
    if (!AnimTree_ParentsActive(index, tree) || !AnimTree_HasActiveLeaf_r(tree, index)) {
        // Not playing: weight, goal weight and blend time all 0.
        info_float(info, 0x24) = 0.0f;
        info_float(info, 0x20) = 0.0f;
        info_float(info, 0x1c) = 0.0f;
    } else if (info_float(info, 0x20) == 0.0f) {
        // Already fading out: the shorter of the two blend times.
        float time;
        std::memcpy(&time, &blend_time, 4);
        if (info_float(info, 0x1c) > time) *reinterpret_cast<uint32_t*>(info + 0x1c) = blend_time;
    } else {
        info_float(info, 0x20) = 0.0f;
        *reinterpret_cast<uint32_t*>(info + 0x1c) = blend_time;
    }
    if (*reinterpret_cast<const uint32_t*>(info + 4)) *reinterpret_cast<uint32_t*>(info + 4) = 0;
    *reinterpret_cast<uint16_t*>(info + 2) = 0xffff;
}

}  // namespace game

// Calls a title function that takes EAX, ESI and EDI (tr_main.cpp).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);

namespace game {

namespace {

// MemContext_Pop (0x0028c690): the memory system in EDX (EDI = the context id, as the
// title leaves it).
extern "C" PLATFORM_HIDDEN void XB_CDECL xanim_memcontext_pop(uint32_t system, uint32_t context);
PLATFORM_ASM_ENTRY(xanim_memcontext_pop,
                   "pushl %edi\n"
                   "movl 8(%esp), %edx\n"
                   "movl 12(%esp), %edi\n"
                   "movl $0x0028c690, %eax\n"
                   "call *%eax\n"
                   "popl %edi\n"
                   "ret")

}  // namespace

PLATFORM_ASM_ENTRY(AnimTree_FreeInfo_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(AnimTree_FreeInfo) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL AnimTree_FreeInfo(uint8_t* tree, uint32_t info_arg) {
    const uint16_t id = static_cast<uint16_t>(info_arg);
    uint8_t* info = info_at(id);
    if (const uint32_t track = *reinterpret_cast<const uint32_t*>(info + 0x2c)) {
        // The track is deleted inside the tree's memory context (+0x10, -1 = none).
        const int32_t context = *reinterpret_cast<const int32_t*>(tree + 0x10);
        uint8_t saved = 0;
        if (context != -1) {
            saved = title_global<uint8_t>(0x0095f5d0);
            const uint32_t system = title_global<uint32_t>(0x0095f638);
            title_global<uint8_t>(0x0095f5d1) = 0;
            title_global<uint8_t>(0x0095f5d0) = 1;
            title_call_regs(0x0028f6c0, (track & ~0xffu) | saved, system, static_cast<uint32_t>(context));  // MemContext_Push
        }
        if (const uint32_t object = *reinterpret_cast<const uint32_t*>(info + 0x2c)) {
            const uint32_t destroy = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(
                *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(object))));
            title_function<void XB_THISCALL(uint32_t, uint32_t)>(destroy)(object, 1);  // deleting destructor
        }
        *reinterpret_cast<uint32_t*>(info + 0x2c) = 0;
        if (context != -1) {
            const uint32_t system = title_global<uint32_t>(0x0095f638);
            title_global<uint8_t>(0x0095f5d1) = 0;
            title_global<uint8_t>(0x0095f5d0) = saved;
            xanim_memcontext_pop(system, static_cast<uint32_t>(context));
        }
    }
    // Back on the free list (info 0 +0xc = its head; +0xa previous, +0xc next).
    uint16_t& head = title_global<uint16_t>(0x00965cfc);
    *reinterpret_cast<uint16_t*>(info + 0xa) = 0;
    *reinterpret_cast<uint16_t*>(info + 0xc) = head;
    *reinterpret_cast<uint16_t*>(info_at(head) + 0xa) = id;
    head = id;
    uint32_t& used = *reinterpret_cast<uint32_t*>(tree + 0x14);
    return --used;
}

void XB_CDECL AnimTree_ClearGoalWeight_r(uint8_t* tree, uint32_t index, uint32_t blend_time) {
    if (!info_index(tree, index)) return;
    AnimTree_ClearGoalWeight(tree, index, blend_time);
    const uint8_t* node = anim_node(tree_anims(tree), index);
    const int32_t count = u16_at(node, 0x20);
    for (int32_t i = 0; i < count; ++i) AnimTree_ClearGoalWeight_r(tree, u16_at(node, 0x36) + i, blend_time);
}

void XB_REGS1 XB_CDECL AnimTree_ClearChildGoalWeights(uint32_t index, uint8_t* tree, uint32_t blend_time) {
    float time;
    std::memcpy(&time, &blend_time, 4);
    if (title_global<float>(0x0083eb4c) > time) blend_time = 0;  // under 1 ms: at once
    const uint8_t* node = anim_node(tree_anims(tree), index);
    const int32_t count = u16_at(node, 0x20);
    for (int32_t i = 0; i < count; ++i) AnimTree_ClearGoalWeight_r(tree, u16_at(node, 0x36) + i, blend_time);
}

void XB_REGS1 XB_CDECL AnimTree_ActivateParents(uint32_t index, uint32_t blend_time) {
    // Every parent up to the first that has an anim info gets one: the root at goal weight
    // 1 at once, the others at goal 0 blending over |0 - weight| x blend time.
    while (index) {
        uint8_t* tree = current_tree();
        index = u16_at(anim_node(tree_anims(tree), index), 0x22);
        if (info_index(tree, index)) return;
        float time;
        std::memcpy(&time, &blend_time, 4);
        uint8_t* info = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
            title_function<uint32_t XB_REGS1 XB_CDECL(uint32_t, uint8_t*)>(0x002c3ed0)(index, tree)));  // AnimTree_AllocInfo
        float goal, rate;
        if (!index) {
            goal = title_global<float>(0x0083eb20);
            rate = 0.0f;
        } else {
            rate = time;
            goal = 0.0f;
        }
        const float difference = goal - info_float(info, 0x24);
        uint32_t bits;
        std::memcpy(&bits, &difference, 4);
        bits &= 0x7fffffffu;
        float magnitude;
        std::memcpy(&magnitude, &bits, 4);
        const float blend = magnitude * rate;
        info_float(info, 0x20) = goal;
        info_float(info, 0x1c) = blend;
        if (title_global<float>(0x0083eb4c) > blend) {
            info_float(info, 0x1c) = 0.0f;
            info_float(info, 0x24) = goal;
        } else if (info_float(info, 0x24) == 0.0f) {
            info_float(info, 0x24) = goal * title_global<float>(0x0083eb4c);
        }
        info_float(info, 0x28) = title_global<float>(0x0083eb20);
        *reinterpret_cast<uint16_t*>(info + 2) = 0xffff;
        *reinterpret_cast<uint16_t*>(info) = 0;
    }
}

}  // namespace game

namespace game {

void XB_CDECL AnimTree_ClearNode_r(uint8_t* tree, uint32_t index) {
    const uint16_t id = info_index(tree, index);
    if (!id) return;
    const uint8_t* node = anim_node(tree_anims(tree), index);
    const int32_t count = u16_at(node, 0x20);
    for (int32_t i = 0; i < count; ++i) AnimTree_ClearNode_r(tree, u16_at(node, 0x36) + i);
    AnimTree_FreeInfo(tree, id);
    *reinterpret_cast<uint16_t*>(tree + 0x18 + index * 2) = 0;
}

void XB_CDECL AnimTree_FlushClearLists() {
    // Trees queued for freeing (0x00987070 count, 0x00987074 list) and for clearing only
    // (0x00987874, 0x00987878).
    for (int32_t i = 0; i < title_global<int32_t>(0x00987070); ++i) {
        uint32_t* tree = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00987074 + i * 4)));
        AnimTree_ClearNode_r(reinterpret_cast<uint8_t*>(tree), 0);
        AnimTree_Free(tree);
    }
    title_global<int32_t>(0x00987070) = 0;
    for (int32_t i = 0; i < title_global<int32_t>(0x00987874); ++i)
        AnimTree_ClearNode_r(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00987878 + i * 4))), 0);
    title_global<int32_t>(0x00987874) = 0;
}

uint8_t XB_CDECL AnimTree_FreeInactive_r(uint32_t index) {
    uint8_t* tree = current_tree();
    const uint16_t id = info_index(tree, index);
    if (!id) return 0;
    uint8_t* info = info_at(id);
    // The weight jumps to the goal weight; nodes left without weight free their infos.
    const float goal = info_float(info, 0x20);
    info_float(info, 0x24) = goal;
    info_float(info, 0x1c) = 0.0f;
    const uint8_t* node = anim_node(tree_anims(tree), index);
    const int32_t count = u16_at(node, 0x20);
    if (!count) {
        if (!(goal == 0.0f) || !(info_float(info, 0x20) == 0.0f)) return 1;
        AnimTree_FreeInfo(tree, id);
        *reinterpret_cast<uint16_t*>(current_tree() + 0x18 + index * 2) = 0;
        return 0;
    }
    bool active = false;
    for (int32_t i = 0; i < count; ++i)
        if (AnimTree_FreeInactive_r(u16_at(node, 0x36) + i)) active = true;
    if (active) return 1;
    tree = current_tree();
    if (!(info_float(info, 0x24) == 0.0f) || !(info_float(info, 0x20) == 0.0f)) return 1;
    AnimTree_FreeInfo(tree, id);
    *reinterpret_cast<uint16_t*>(current_tree() + 0x18 + index * 2) = 0;
    return 0;
}

void XB_CDECL AnimTree_ProcessNotetracks_r(uint8_t* info, const uint8_t* node) {
    AnimTree_ProcessNotetracks(info, node);
    for (int32_t i = 0; i < u16_at(node, 0x20); ++i) {
        const uint32_t child = u16_at(node, 0x36) + i;
        const uint8_t* tree = current_tree();
        const uint16_t id = info_index(tree, child);
        if (!id) continue;
        uint8_t* child_info = info_at(id);
        if (info_float(child_info, 0x24) == 0.0f) continue;
        if (info_float(child_info, 0x20) == 0.0f) continue;
        AnimTree_ProcessNotetracks_r(child_info, anim_node(tree_anims(tree), child));
    }
}

}  // namespace game

namespace game {

namespace {

// A node's notetracks (+0x28): an array of 12-byte records with its count in the word
// before it (vector new); each record's script string released, last first, then freed.
void free_notetracks(uint8_t* node) {
    const uint32_t notes = *reinterpret_cast<const uint32_t*>(node + 0x28);
    const int32_t count = *reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(notes - 4));
    for (int32_t k = count - 1; k >= 0; --k)
        title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(notes + k * 12);  // ScriptString_Release
    title_function<void XB_CDECL(uint32_t)>(0x000428d0)(notes - 4);               // Z_Free
    *reinterpret_cast<uint32_t*>(node + 0x28) = 0;
}

// The anim data entry of anim-tree table handle >> 16 (rm_animTrees +4: {count, entries}).
const uint8_t* rm_anims(uint32_t entry) {
    const uint8_t* table = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
        *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f524) + 4))));
    const uint32_t count = *reinterpret_cast<const uint32_t*>(table);
    return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(table + 4) +
                                                                   (entry < count ? entry : 0) * 12));
}

}  // namespace

PLATFORM_ASM_ENTRY(AnimData_EnsureLoaded_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(AnimData_EnsureLoaded) "addl $4, %esp\n"
                   "ret")

void XB_CDECL AnimData_EnsureLoaded(uint8_t* node) {
    // Looked up again when the node has no clip (+0x24) and its stamp (+0x2c) is not the
    // current one (0x008d508c); the notetracks are rebuilt.
    if (*reinterpret_cast<const uint32_t*>(node + 0x24)) return;
    if (*reinterpret_cast<const uint32_t*>(node + 0x2c) == title_global<uint32_t>(0x008d508c)) return;
    if (*reinterpret_cast<const uint32_t*>(node + 0x28)) free_notetracks(node);
    *reinterpret_cast<uint32_t*>(node + 0x2c) = title_global<uint32_t>(0x008d508c);
    uint32_t clip = NAL_FindClip(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node)));
    if (clip && *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(clip)) != title_global<uint32_t>(0x00903f98)) clip = 0;
    *reinterpret_cast<uint32_t*>(node + 0x24) = clip;
    XAnim_BuildNotetracks(node);
}

XANIM_XMM0_ENTRY(AnimTree_EnsureAnimLoaded, "pushl 0x6c(%esp)\n", "8")

void XB_CDECL AnimTree_EnsureAnimLoaded(uint32_t handle, uint32_t* result) {
    // XMM0 = the clip's length (+0x38), 1 without a clip.
    uint8_t* node = anim_node(rm_anims(handle >> 16), handle & 0xffff);
    if (!*reinterpret_cast<const uint32_t*>(node + 0x24)) AnimData_EnsureLoaded(node);
    const uint32_t clip = *reinterpret_cast<const uint32_t*>(node + 0x24);
    *result = clip ? *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(clip + 0x38)) : title_global<uint32_t>(0x0083eb20);
}

void XB_REGS1 XB_CDECL AnimTree_FreeNotetracks(uint32_t entry) {
    // Every node of the entry drops its clip, child count and notetracks.
    const uint8_t* anims = rm_anims(entry);
    for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(anims + 4); ++i) {
        uint8_t* node = anim_node(anims, i);
        *reinterpret_cast<uint32_t*>(node + 0x24) = 0;
        *reinterpret_cast<uint16_t*>(node + 0x20) = 0;
        if (*reinterpret_cast<const uint32_t*>(node + 0x28)) free_notetracks(node);
        *reinterpret_cast<uint32_t*>(node + 0x2c) = 0;
    }
}

}  // namespace game

namespace game {

uint8_t* XB_CDECL AnimTree_Create(const uint8_t* owner, uint8_t* anims) {
    // Header 0x18 bytes + one 16-bit info index per node, from the owner's memory context
    // (+0x220, -1 = the system's default +0x34) or the heap; +8 anim data, +0xc the owner's
    // +0x224, +0x10 the context. Linked at the end of the live list (head 0x008dda44, tail
    // 0x008dda48, count 0x008dda3c).
    const uint32_t size = *reinterpret_cast<const uint32_t*>(anims + 4) * 2 + 0x18;
    const uint8_t* system = title_global<uint8_t*>(0x0095f638);
    int32_t context = -1;
    if (owner) context = *reinterpret_cast<const int32_t*>(owner + 0x220);
    if (context == -1) context = *reinterpret_cast<const int32_t*>(system + 0x34);
    uint8_t* tree = nullptr;
    if (context != -1) {
        const uint32_t memory =
            context >= 0 && context < 0x63 ? *reinterpret_cast<const uint32_t*>(system + 0x38 + context * 4) : 0;
        tree = title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x0028c0f0)(memory, 0x10, size);  // MemContext_Alloc_r
    }
    if (!tree) tree = title_function<uint8_t* XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x10, size);  // Heap_AllocAligned
    Com_Memset(tree, 0, size);
    *reinterpret_cast<uint32_t*>(tree + 8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(anims));
    *reinterpret_cast<int32_t*>(tree + 0x10) = context;
    *reinterpret_cast<uint32_t*>(tree + 0xc) = owner ? *reinterpret_cast<const uint32_t*>(owner + 0x224) : 0;
    for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(anims + 4); ++i) AnimData_EnsureLoaded(anim_node(anims, i));
    uint32_t*& tail = title_global<uint32_t*>(0x008dda48);
    reinterpret_cast<uint32_t*>(tree)[0] = 0x008dda44;
    reinterpret_cast<uint32_t*>(tree)[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tail));
    *tail = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    ++title_global<int32_t>(0x008dda3c);
    tail = reinterpret_cast<uint32_t*>(tree);
    return tree;
}

}  // namespace game

namespace game {

namespace {

float bits_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// |a - b| through the integer sign mask, as the title does it.
float abs_difference(float a, float b) {
    const float difference = a - b;
    uint32_t bits;
    std::memcpy(&bits, &difference, 4);
    return bits_float(bits & 0x7fffffffu);
}

uint8_t* alloc_info(uint32_t index, uint8_t* tree) {
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
        title_function<uint32_t XB_REGS1 XB_CDECL(uint32_t, uint8_t*)>(0x002c3ed0)(index, tree)));  // AnimTree_AllocInfo
}

}  // namespace

PLATFORM_ASM_ENTRY(AnimTree_SetGoalWeightInternal_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(AnimTree_SetGoalWeightInternal) "addl $24, %esp\n"
                   "ret")

uint32_t XB_CDECL AnimTree_SetGoalWeightInternal(uint32_t index, uint32_t anim, uint32_t goal_bits, uint32_t blend_bits,
                                                 uint32_t rate_bits, uint32_t create) {
    uint8_t* tree = current_tree();
    uint8_t* info;
    if (const uint16_t id = info_index(tree, index)) {
        info = info_at(id);
    } else {
        // A node without an info gets one unless the goal is 0 and `create` is clear.
        if (bits_float(goal_bits) == title_global<float>(0x0083eafc) && !static_cast<uint8_t>(create)) return 0;
        info = alloc_info(index, tree);
        tree = current_tree();
    }
    float goal, blend, rate;
    if (!index) {
        goal = title_global<float>(0x0083eb20);
        blend = 0.0f;
        rate = goal;
    } else {
        rate = bits_float(rate_bits);
        blend = bits_float(blend_bits);
        goal = bits_float(goal_bits);
    }
    const float time = abs_difference(goal, info_float(info, 0x24)) * blend;
    info_float(info, 0x20) = goal;
    info_float(info, 0x1c) = time;
    if (title_global<float>(0x0083eb4c) > time) {
        info_float(info, 0x1c) = 0.0f;
        info_float(info, 0x24) = goal;
    } else if (info_float(info, 0x24) == 0.0f) {
        info_float(info, 0x24) = goal * title_global<float>(0x0083eb4c);
    }
    info_float(info, 0x28) = rate;
    if (anim) *reinterpret_cast<uint32_t*>(info + 4) = anim;
    *reinterpret_cast<uint16_t*>(info + 2) = 0xffff;
    const uint8_t* node = anim_node(tree_anims(tree), index);
    if (anim && u16_at(node, 0x20) && (node[0x34] & 3)) {
        // A synced subtree follows its heaviest leaf; 2 when there is none.
        const uint16_t leaf = static_cast<uint16_t>(AnimTree_FindHeaviestLeaf_r(index));
        *reinterpret_cast<uint16_t*>(info) = leaf;
        return leaf ? 0 : 2;
    }
    *reinterpret_cast<uint16_t*>(info) = 0;
    return 0;
}

uint32_t XB_REGS3 XB_CDECL AnimTree_SetGoalWeightClamped(uint8_t* tree, uint32_t, uint32_t index, uint32_t goal_bits,
                                                         uint32_t blend_bits, uint32_t rate_bits, uint32_t anim, uint32_t,
                                                         uint32_t restart) {
    title_global<uint32_t>(0x00928588) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    if (title_global<float>(0x0083eb4c) > bits_float(goal_bits)) goal_bits = 0;  // goal under 0.001: 0
    const uint32_t result = AnimTree_SetGoalWeightInternal(index, anim, goal_bits, blend_bits, rate_bits, 0);
    AnimTree_ActivateParents(index, blend_bits);
    AnimTree_ResetTime(index, restart);
    AnimTree_UpdateNoteIndex(0, index);
    return result;
}

void XB_REGS1 XB_CDECL AnimTree_SetGoalWeight(uint32_t index, uint8_t* tree, uint32_t goal_bits, uint32_t blend_bits,
                                              uint32_t rate_bits, uint32_t anim, uint32_t, uint32_t restart) {
    title_global<uint32_t>(0x00928588) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    if (title_global<float>(0x0083eb4c) > bits_float(goal_bits)) goal_bits = 0;
    AnimTree_SetGoalWeightInternal(index, anim, goal_bits, blend_bits, rate_bits, 0);
    // The parents that have no goal weight blend to 1 over the blend time (the root at once).
    for (uint32_t node = index; node;) {
        node = u16_at(anim_node(tree_anims(tree), node), 0x22);
        if (const uint16_t id = info_index(tree, node))
            if (!(info_float(info_at(id), 0x20) == 0.0f)) continue;
        uint8_t* global_tree = current_tree();
        uint8_t* info;
        if (const uint16_t id = info_index(global_tree, node))
            info = info_at(id);
        else
            info = alloc_info(node, global_tree);
        const float blend = node ? bits_float(blend_bits) : 0.0f;
        const float one = title_global<float>(0x0083eb20);
        const float time = abs_difference(one, info_float(info, 0x24)) * blend;
        info_float(info, 0x20) = one;
        info_float(info, 0x1c) = time;
        if (title_global<float>(0x0083eb4c) > time) {
            info_float(info, 0x1c) = 0.0f;
            info_float(info, 0x24) = one;
        } else if (info_float(info, 0x24) == 0.0f) {
            info_float(info, 0x24) = title_global<float>(0x0083eb4c);
        }
        info_float(info, 0x28) = one;
        *reinterpret_cast<uint16_t*>(info + 2) = 0xffff;
        *reinterpret_cast<uint16_t*>(info) = 0;
    }
    AnimTree_ResetTime(index, restart);
    AnimTree_UpdateNoteIndex(0, index);
}

}  // namespace game

namespace game {

namespace {

// MemContextScope_Enter (0x0028fcc0): flag in AL, context in ECX, the scope {context, saved
// 0x0095f5d0} on the stack (callee pops 4); pushes the context unless it is -1.
extern "C" PLATFORM_HIDDEN void XB_CDECL xanim_scope_enter(uint32_t flag, int32_t context, uint32_t* scope);
PLATFORM_ASM_ENTRY(xanim_scope_enter,
                   "movl 4(%esp), %eax\n"
                   "movl 8(%esp), %ecx\n"
                   "pushl 12(%esp)\n"
                   "movl $0x0028fcc0, %edx\n"
                   "call *%edx\n"
                   "ret")

void build_notetracks(uint8_t* node) { XAnim_BuildNotetracks(node); }

uint32_t& u32_ref(uint8_t* p, uint32_t offset) { return *reinterpret_cast<uint32_t*>(p + offset); }
uint16_t& u16_ref(uint8_t* p, uint32_t offset) { return *reinterpret_cast<uint16_t*>(p + offset); }

}  // namespace

uint32_t XB_REGS1 XB_CDECL AnimTree_AllocInfo(uint32_t index, uint8_t* tree) {
    // The head of the free list (info 0 +0xc) becomes node `index`'s info.
    uint16_t& head = title_global<uint16_t>(0x00965cfc);
    const uint16_t id = head;
    if (!id) {
        title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
        return 0;
    }
    ++u32_ref(tree, 0x14);
    uint8_t* info = info_at(id);
    head = u16_at(info, 0xc);
    u16_ref(info_at(head), 0xa) = 0;
    u16_ref(tree, 0x18 + index * 2) = id;
    Com_Memset(info + 0x10, 0, 0x20);
    u16_ref(info, 0) = 0;
    u32_ref(info, 4) = 0;
    u16_ref(info, 2) = 0xffff;
    u16_ref(info, 8) = 0;
    uint8_t* node = anim_node(tree_anims(tree), index);
    if (u16_at(node, 0x20)) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info));
    // A leaf gets its first notetrack's name (+4) and a track over its clip, made in the
    // tree's memory context.
    if (!u32_ref(node, 0x24)) {
        AnimData_EnsureLoaded(node);
        if (!u32_ref(node, 0x24)) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info));
    }
    if (const uint32_t notes = u32_ref(node, 0x28))
        u32_ref(info, 4) = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(notes + 4));
    else
        build_notetracks(node);
    uint32_t scope[2] = {0, 0};
    xanim_scope_enter(1, static_cast<int32_t>(u32_ref(tree, 0x10)), scope);
    uint8_t* clip = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(node, 0x24)));
    uint8_t* skeleton = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(clip, 0x30)));
    uint8_t* track = static_cast<uint8_t*>(Mem_AlignedAlloc(0x30, 4, 0));
    if (track) track = NAL_AnimTrack_Construct(track, clip, skeleton);
    u32_ref(info, 0x2c) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(track));
    if (scope[0] != 0xffffffffu) {
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = static_cast<uint8_t>(scope[1]);
        MemContext_Pop(title_global<uint8_t*>(0x0095f638));
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info));
}

// (goal in XMM2, tree, index, blend time): XMM5-7 kept (AnimTree_SetKnob relies on them).
PLATFORM_ASM_ENTRY(AnimTree_ClearSiblingGoalWeights_entry,
                   "subl $0x60, %esp\n"
                   "movups %xmm2, 0x00(%esp)\n"
                   "movups %xmm3, 0x10(%esp)\n"
                   "movups %xmm4, 0x20(%esp)\n"
                   "movups %xmm5, 0x30(%esp)\n"
                   "movups %xmm6, 0x40(%esp)\n"
                   "movups %xmm7, 0x50(%esp)\n"
                   "pushl 0x6c(%esp)\n"
                   "pushl 0x6c(%esp)\n"
                   "pushl 0x6c(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm2, (%esp)\n" PLATFORM_ASM_CALL(AnimTree_ClearSiblingGoalWeights) "addl $16, %esp\n"
                   "movups 0x00(%esp), %xmm2\n"
                   "movups 0x10(%esp), %xmm3\n"
                   "movups 0x20(%esp), %xmm4\n"
                   "movups 0x30(%esp), %xmm5\n"
                   "movups 0x40(%esp), %xmm6\n"
                   "movups 0x50(%esp), %xmm7\n"
                   "addl $0x60, %esp\n"
                   "ret")

void XB_CDECL AnimTree_ClearSiblingGoalWeights(float goal, uint8_t* tree, uint32_t index, uint32_t blend_bits) {
    if (!index) return;
    // The siblings fade out over the largest weight change among them (node `index` counts
    // |goal - weight|) times the blend time; under 0.001 at once.
    const uint8_t* anims = tree_anims(tree);
    const uint8_t* parent = anim_node(anims, u16_at(anim_node(anims, index), 0x22));
    const int32_t count = u16_at(parent, 0x20);
    float largest = 0.0f;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t child = u16_at(parent, 0x36) + i;
        const uint16_t id = info_index(tree, child);
        float change = id ? info_float(info_at(id), 0x24) : 0.0f;
        if (child == index) change = abs_difference(goal, change);
        if (change > largest) largest = change;
    }
    float time = largest * bits_float(blend_bits);
    if (title_global<float>(0x0083eb4c) > time) time = 0.0f;
    uint32_t time_bits;
    std::memcpy(&time_bits, &time, 4);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t child = u16_at(parent, 0x36) + i;
        if (child != index) AnimTree_ClearGoalWeight(tree, child, time_bits);
    }
}

// (index in EAX, goal in XMM5; tree, blend time, rate, anim, unused, restart).
PLATFORM_ASM_ENTRY(AnimTree_SetKnob_entry,
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm5, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimTree_SetKnob) "addl $32, %esp\n"
                   "ret")

void XB_CDECL AnimTree_SetKnob(uint32_t index, float goal, uint8_t* tree, uint32_t blend_bits, uint32_t rate_bits,
                               uint32_t anim, uint32_t, uint32_t restart) {
    // The siblings fade out (ClearSiblingGoalWeights), then SetGoalWeightClamped's steps.
    if (title_global<float>(0x0083eb4c) > goal) goal = 0.0f;
    AnimTree_ClearSiblingGoalWeights(goal, tree, index, blend_bits);
    uint32_t goal_bits;
    std::memcpy(&goal_bits, &goal, 4);
    title_global<uint32_t>(0x00928588) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    AnimTree_SetGoalWeightInternal(index, anim, goal_bits, blend_bits, rate_bits, 0);
    AnimTree_ActivateParents(index, blend_bits);
    AnimTree_ResetTime(index, restart);
    AnimTree_UpdateNoteIndex(0, index);
}

// (index in EAX, tree in ESI; stop node, goal, blend time, rate, anim, unused, restart).
PLATFORM_ASM_ENTRY(AnimTree_SetKnobAll_entry,
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl 28(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimTree_SetKnobAll) "addl $36, %esp\n"
                   "ret")

uint32_t XB_CDECL AnimTree_SetKnobAll(uint32_t index, uint8_t* tree, uint32_t stop, uint32_t goal_bits, uint32_t blend_bits,
                                      uint32_t rate_bits, uint32_t anim, uint32_t, uint32_t restart) {
    // SetKnob on node `index`, then every parent up to `stop` (exclusive; 1 when the root is
    // reached first, else Internal's result) goes to goal 1 with its siblings faded out.
    title_global<uint32_t>(0x00928588) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    if (title_global<float>(0x0083eb4c) > bits_float(goal_bits)) goal_bits = 0;
    AnimTree_ClearSiblingGoalWeights(bits_float(goal_bits), tree, index, blend_bits);
    const uint32_t result = AnimTree_SetGoalWeightInternal(index, anim, goal_bits, blend_bits, rate_bits, 0);
    AnimTree_ActivateParents(index, blend_bits);
    AnimTree_ResetTime(index, restart);
    AnimTree_UpdateNoteIndex(0, index);
    if (!index) return 1;
    uint8_t* global_tree = current_tree();
    while (true) {
        index = u16_at(anim_node(tree_anims(tree), index), 0x22);
        if (index == stop) return result;
        const float one = title_global<float>(0x0083eb20);
        AnimTree_ClearSiblingGoalWeights(one, tree, index, blend_bits);
        uint8_t* info;
        if (const uint16_t id = info_index(global_tree, index)) {
            info = info_at(id);
        } else {
            info = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(AnimTree_AllocInfo(index, global_tree)));
            global_tree = current_tree();
        }
        const float blend = index ? bits_float(blend_bits) : 0.0f;
        const float time = abs_difference(one, info_float(info, 0x24)) * blend;
        info_float(info, 0x20) = one;
        info_float(info, 0x1c) = time;
        if (title_global<float>(0x0083eb4c) > time) {
            info_float(info, 0x1c) = 0.0f;
            info_float(info, 0x24) = one;
        } else if (info_float(info, 0x24) == 0.0f) {
            info_float(info, 0x24) = title_global<float>(0x0083eb4c);
        }
        info_float(info, 0x28) = one;
        u16_ref(info, 2) = 0xffff;
        u16_ref(info, 0) = 0;
        AnimTree_ResetTime(index, restart);
        AnimTree_UpdateNoteIndex(0, index);
        if (!index) return 1;
    }
}

void XB_REGS3 XB_CDECL AnimTree_CopyInfos(const uint8_t* src, uint32_t, uint8_t* dst) {
    // dst gets src's anim infos node by node (whole 0x30-byte copies); nodes without one
    // in src lose theirs.
    const int32_t count = static_cast<int32_t>(*reinterpret_cast<const uint32_t*>(tree_anims(src) + 4));
    for (int32_t i = 0; i < count; ++i) {
        const uint16_t from = info_index(src, i);
        const uint16_t to = info_index(dst, i);
        if (!from) {
            if (to) {
                AnimTree_FreeInfo(dst, to);
                u16_ref(dst, 0x18 + i * 2) = 0;
            }
            continue;
        }
        uint8_t* info = to ? info_at(to) : reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(AnimTree_AllocInfo(i, dst)));
        platform_rep_movs(info, info_at(from), 0x30);
    }
}

void XB_CDECL AnimTree_BlendWeights_r(uint32_t index) {
    uint8_t* tree = current_tree();
    const uint16_t id = info_index(tree, index);
    if (!id) return;
    uint8_t* info = info_at(id);
    // The weight moves toward the goal by the frame time 0x0092f550 over the blend time
    // left (+0x1c); the previous time and frame (+0x14/+0x1a) take the current ones.
    const float frame_time = title_global<float>(0x0092f550);
    const float epsilon = title_global<float>(0x0083eb4c);
    u32_ref(info, 0x14) = u32_ref(info, 0x10);
    const uint16_t frame = u16_at(info, 0x18);
    const bool done = frame_time + epsilon > info_float(info, 0x1c);
    u16_ref(info, 0x1a) = frame;
    if (done) {
        u32_ref(info, 0x24) = u32_ref(info, 0x20);
        info_float(info, 0x1c) = 0.0f;
    } else {
        const float weight = (info_float(info, 0x20) - info_float(info, 0x24)) / info_float(info, 0x1c) * frame_time +
                             info_float(info, 0x24);
        info_float(info, 0x24) = weight;
        if (title_global<float>(0x0083efdc) > weight) info_float(info, 0x24) = info_float(info, 0x20) * epsilon;
        info_float(info, 0x1c) = info_float(info, 0x1c) - frame_time;
    }
    const uint8_t* node = anim_node(tree_anims(tree), index);
    const int32_t count = u16_at(node, 0x20);
    const bool stopped = info_float(info, 0x24) == 0.0f && info_float(info, 0x20) == 0.0f;
    if (!count) {
        if (!stopped) return;
        AnimTree_FreeInfo(current_tree(), id);
        u16_ref(current_tree(), 0x18 + index * 2) = 0;
        return;
    }
    if (!stopped) {
        for (int32_t i = 0; i < count; ++i) AnimTree_BlendWeights_r(u16_at(node, 0x36) + i);
        return;
    }
    // A stopped parent frees its info once nothing below it stays active.
    bool active = false;
    for (int32_t i = 0; i < count; ++i)
        if (AnimTree_FreeInactive_r(u16_at(node, 0x36) + i)) active = true;
    if (active) return;
    AnimTree_FreeInfo(current_tree(), id);
    u16_ref(current_tree(), 0x18 + index * 2) = 0;
}

// (model in EDI, frame time in XMM0).
PLATFORM_ASM_ENTRY(AnimTree_BlendWeightsForModel_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(AnimTree_BlendWeightsForModel) "addl $8, %esp\n"
                   "ret")

void XB_CDECL AnimTree_BlendWeightsForModel(const uint8_t* model, float frame_time) {
    // The model's trees (+0, count byte +0x2e) blend their weights by the frame time.
    if (!u32_ref(const_cast<uint8_t*>(model), 0)) return;
    title_global<float>(0x0092f550) = frame_time;
    for (int32_t i = 0; i < model[0x2e]; ++i) {
        const uint32_t tree = *reinterpret_cast<const uint32_t*>(model + i * 4);
        title_global<uint32_t>(0x00928588) = tree;
        if (tree) AnimTree_BlendWeights_r(0);
    }
}

// (index, time step): XMM0 = the result; XMM2-7 kept.
XANIM_XMM0_ENTRY(AnimTree_AdvanceTime_r_2, "pushl 0x70(%esp)\n pushl 0x70(%esp)\n", "12")

void XB_CDECL AnimTree_AdvanceTime_r_2(uint32_t index, uint32_t dt_bits, float* result) {
    // The notetrack check of a time step (the notes the step would pass are only looked at
    // in builds with them): 1 unless a leaf or synced node actually moves; its new time and
    // frames go to 0x00927f58/0x00935114.. for the check, nothing in the info changes.
    const float one = title_global<float>(0x0083eb20);
    uint8_t* tree = current_tree();
    const uint16_t id = info_index(tree, index);
    *result = one;
    if (!id) return;
    uint8_t* info = info_at(id);
    if (info_float(info, 0x24) == 0.0f || info_float(info, 0x20) == 0.0f) return;
    const uint8_t* node = anim_node(tree_anims(tree), index);
    const int32_t count = u16_at(node, 0x20);
    const float dt = bits_float(dt_bits);
    float step;
    bool wrap;
    if (!count) {
        const uint8_t* clip = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(u32_ref(const_cast<uint8_t*>(node), 0x24)));
        float frequency = one;
        if (clip) {
            const float length = *reinterpret_cast<const float*>(clip + 0x38);
            frequency = length == 0.0f ? 0.0f : one / length;
        }
        step = info_float(info, 0x28) * frequency * dt;
        if (step == 0.0f) return;
        wrap = clip && (clip[0x34] & 1);
    } else if (node[0x34] & 3) {
        float frequency;
        AnimTree_GetAvgFrequency_r(index, &frequency);
        step = frequency * info_float(info, 0x28) * dt;
        if (step == 0.0f) return;
        wrap = !(node[0x34] & 2);
    } else {
        // The least result over the children, at the step times the rate.
        const float scaled = info_float(info, 0x28) * dt;
        if (scaled == 0.0f) return;
        uint32_t scaled_bits;
        std::memcpy(&scaled_bits, &scaled, 4);
        float least = one;
        for (int32_t i = 0; i < count; ++i) {
            float r;
            AnimTree_AdvanceTime_r_2(u16_at(node, 0x36) + i, scaled_bits, &r);
            if (platform_x87_greater(least, r)) least = r;
        }
        *result = least;
        return;
    }
    float time = info_float(info, 0x14) + step;
    const uint16_t previous_frame = u16_at(info, 0x1a);
    uint16_t frame = previous_frame;
    if (!wrap) {
        if (time >= one) time = one;
    } else {
        while (time >= one) {
            time = time - one;
            ++frame;
        }
    }
    const int16_t current_frame = static_cast<int16_t>(u16_at(info, 0x18));
    const float frames = static_cast<float>(static_cast<int16_t>(frame) - current_frame);
    if (info_float(info, 0x10) - time > frames) return;
    title_global<uint32_t>(0x0093e754) = u32_ref(info, 0x14);
    title_global<uint16_t>(0x0092f090) = static_cast<uint16_t>(current_frame);
    title_global<float>(0x0093510c) = step;
    title_global<uint16_t>(0x00935114) = previous_frame;
    title_global<float>(0x00927f58) = time;
    title_global<uint32_t>(0x0093424c) = u32_ref(info, 0x10);
    if (!count)
        *result = AnimTree_CheckLeafNotes();
    else
        AnimTree_CheckNotes_r(info, node, result);
}

void XB_CDECL AnimTree_AdvanceTime_r(uint32_t index, uint32_t dt_bits, uint32_t notify_arg) {
    // Moves node `index` and its subtree on by the time step: leaves by rate / anim length
    // (looping anims wrap and count frames, others stop at 1), synced nodes by their
    // children's average frequency (then SetTime_r), the rest pass the step times their
    // rate down. Notetracks passed are processed when `notify` is set and the goal is not 0.
    uint8_t* tree = current_tree();
    const uint16_t id = info_index(tree, index);
    if (!id) return;
    uint8_t* info = info_at(id);
    if (info_float(info, 0x24) == 0.0f) return;
    uint8_t notify = static_cast<uint8_t>(notify_arg);
    if (info_float(info, 0x20) == 0.0f) notify = 0;
    uint8_t* node = anim_node(tree_anims(tree), index);
    const int32_t count = u16_at(node, 0x20);
    const float one = title_global<float>(0x0083eb20);
    const float dt = bits_float(dt_bits);
    if (count && !(node[0x34] & 3)) {
        const float scaled = info_float(info, 0x28) * dt;
        uint32_t scaled_bits;
        std::memcpy(&scaled_bits, &scaled, 4);
        for (int32_t i = 0; i < count; ++i) AnimTree_AdvanceTime_r(u16_at(node, 0x36) + i, scaled_bits, notify);
        return;
    }
    float time;
    bool wrap;
    if (!count) {
        if (!u32_ref(node, 0x24)) AnimData_EnsureLoaded(node);
        const uint8_t* clip = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(u32_ref(node, 0x24)));
        float frequency = one;
        if (clip) {
            const float length = *reinterpret_cast<const float*>(clip + 0x38);
            frequency = length == 0.0f ? 0.0f : one / length;
        }
        time = info_float(info, 0x28) * frequency * dt + info_float(info, 0x14);
        wrap = clip && (clip[0x34] & 1);
    } else {
        float frequency;
        AnimTree_GetAvgFrequency_r(index, &frequency);
        time = info_float(info, 0x14) + frequency * info_float(info, 0x28) * dt;
        wrap = !(node[0x34] & 2);
    }
    uint16_t frame = u16_at(info, 0x1a);
    if (!wrap) {
        if (time >= one)
            time = one;
        else if (0.0f >= time)
            return;
    } else {
        while (0.0f > time) {
            time = time + one;
            --frame;
        }
        while (time >= one) {
            time = time - one;
            ++frame;
        }
    }
    const uint16_t current_frame = u16_at(info, 0x18);
    const float frames = static_cast<float>(static_cast<int16_t>(frame) - static_cast<int16_t>(current_frame));
    if (info_float(info, 0x10) - time > frames) return;
    if (!count) {
        if (notify) {
            title_global<float>(0x00927f58) = time;
            title_global<uint32_t>(0x0093e754) = u32_ref(info, 0x10);
            title_global<uint16_t>(0x00935114) = current_frame;
            AnimTree_ProcessNotetracks(info, node);
        }
        u16_ref(info, 0x18) = frame;
        info_float(info, 0x10) = time;
        return;
    }
    title_global<float>(0x00927f58) = time;
    if (notify) {
        title_global<uint32_t>(0x0093e754) = u32_ref(info, 0x10);
        title_global<uint16_t>(0x00935114) = current_frame;
        AnimTree_ProcessNotetracks_r(info, node);
    }
    title_global<uint16_t>(0x0093ef64) = frame;
    AnimTree_SetTime_r(index);
}

// (index in ESI; model, time step, until-done byte).
PLATFORM_ASM_ENTRY(AnimTree_AdvanceTimeForModel_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(AnimTree_AdvanceTimeForModel) "addl $16, %esp\n"
                   "ret")

uint8_t XB_CDECL AnimTree_AdvanceTimeForModel(uint32_t index, const uint8_t* model, uint32_t dt_bits, uint32_t until_done) {
    // Each of the model's trees advances by the time step; with `until_done` a tree whose
    // notetrack check gives r < 1 advances only to r x step + 0.001 (if that is within the
    // step) and the result is 1 when the last such tree did.
    if (!*reinterpret_cast<const uint32_t*>(model)) return 0;
    title_global<uint32_t>(0x0095f51c) = *reinterpret_cast<const uint32_t*>(model + 0x88);
    uint8_t stopped = 0;
    for (int32_t i = 0; i < model[0x2e]; ++i) {
        const uint32_t tree = *reinterpret_cast<const uint32_t*>(model + i * 4);
        title_global<uint32_t>(0x00928588) = tree;
        if (!tree) continue;
        if (!static_cast<uint8_t>(until_done)) {
            AnimTree_AdvanceTime_r(index, dt_bits, 0);
            stopped = 0;
            continue;
        }
        float r;
        AnimTree_AdvanceTime_r_2(index, dt_bits, &r);
        if (r == title_global<float>(0x0083eb20)) {
            AnimTree_AdvanceTime_r(index, dt_bits, 1);
            stopped = 0;
            continue;
        }
        const float dt = bits_float(dt_bits);
        const float step = r * dt + title_global<float>(0x0083eb4c);
        if (!(dt >= step)) continue;  // NaN too
        uint32_t step_bits;
        std::memcpy(&step_bits, &step, 4);
        AnimTree_AdvanceTime_r(index, step_bits, 1);
        stopped = 1;
    }
    return stopped;
}

// (anim-tree table entry in EBX; flag byte: keep the notetracks and sync flags).
PLATFORM_ASM_ENTRY(AnimTree_Setup_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(AnimTree_Setup) "addl $8, %esp\n"
                   "ret")

void XB_CDECL AnimTree_Setup(uint32_t entry, uint32_t keep) {
    // Every node of the entry is announced through the function at 0x0095f49c +0x858
    // (entry name hash, entry, the node's name, index) and loses its clip and stamp;
    // without `keep` its notetracks are rebuilt and the sync flags marked from the root.
    const uint8_t* table = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
        *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f524) + 4))));
    const uint32_t entries = *reinterpret_cast<const uint32_t*>(table);
    uint8_t* anims = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(table + 4) +
                                                                       (entry < entries ? entry : 0) * 12));
    uint32_t name[8];
    title_function<uint32_t* XB_THISCALL(uint32_t*, uint32_t)>(0x00011060)(name, u32_ref(anims, 0));  // HashStr_Init
    for (uint32_t i = 0; i < u32_ref(anims, 4); ++i) {
        uint8_t* node = anim_node(anims, i);
        const uint32_t announce = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f49c) + 0x858));
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(announce)(name[0], entry, u32_ref(node, 0), i);
        u32_ref(node, 0x2c) = 0;
        u32_ref(node, 0x24) = 0;
        if (!static_cast<uint8_t>(keep)) build_notetracks(node);
    }
    if (!static_cast<uint8_t>(keep)) AnimTree_MarkSync_r(anims, 0);
}

}  // namespace game

namespace game {

namespace {

// A notetrack record (12 bytes): +0 the note's string, +4 its name (0 ends the list), +8 time.
float note_time(uint32_t note) { return *reinterpret_cast<const float*>(static_cast<uintptr_t>(note + 8)); }
uint32_t note_name(uint32_t note) { return *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(note + 4)); }
const uint32_t* note_name_ptr(uint32_t note) { return reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(note + 4)); }
uint8_t* notify_entity() { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f51c))); }

}  // namespace

void XB_CDECL AnimTree_ProcessNotetracks(uint8_t* info, const uint8_t* node) {
    // The notetracks the step from 0x0093e754 to 0x00927f58 passed are sent to the notify
    // entity 0x0095f51c (a "weapsound" track goes to 0x0095f534's +4 and every note of it
    // as the notify "weapsound"); the info's notify (+4) with "end" (0x00965ce8) when the
    // anim ends. Wrapped steps send the tail of the list, then the head.
    uint8_t weapsound = 0;
    const uint32_t notes = u32_ref(const_cast<uint8_t*>(node), 0x28);
    if (notes) {
        const uint32_t first = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(notes));
        if (first && first + 6 && *reinterpret_cast<const char*>(static_cast<uintptr_t>(first + 6))) {
            const char* text = reinterpret_cast<const char*>(static_cast<uintptr_t>(first ? first + 6 : 0));
            if (title_function<const char* XB_CDECL(const char*, const char*)>(0x001efb10)(text, &title_global<char>(0x00832af8))) {  // strstr
                title_global<uint32_t>(0x0095f51c) = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f534) + 4));
                weapsound = 1;
            }
        }
    }
    uint8_t* ent = notify_entity();
    if (!ent) return;
    uint32_t* notify = &u32_ref(info, 4);
    if (!*notify) return;
    const uint32_t entity = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    const float previous = title_global<float>(0x0093e754);
    const float one = title_global<float>(0x0083eb20);
    if (previous == one) {
        anim_notify_with_string_call(entity, *notify, 0x00965ce8);
        return;
    }
    if (u16_at(node, 0x20)) {
        const uint16_t note_node = u16_at(info, 0);
        if (!note_node) {
            const float time = title_global<float>(0x00927f58);
            if (previous > time || time == one) anim_notify_with_string_call(entity, *notify, 0x00965ce8);
            return;
        }
        node = anim_node(tree_anims(current_tree()), note_node);
    }
    const int16_t next = static_cast<int16_t>(u16_at(info, 2));
    if (next < 0) return;
    const float time = title_global<float>(0x00927f58);
    uint32_t note = u32_ref(const_cast<uint8_t*>(node), 0x28) + next * 12;
    if (previous > time) {
        if (time > note_time(note)) {
            do {
                AnimTree_NotifyNotetrack(note, notify_entity(), notify, note_name_ptr(note));
                note += 12;
            } while (note_name(note) && title_global<float>(0x00927f58) > note_time(note));
        } else {
            if (previous > note_time(note)) return;
            do {
                AnimTree_NotifyNotetrack(note, notify_entity(), notify, note_name_ptr(note));
                note += 12;
            } while (note_name(note));
            for (note = u32_ref(const_cast<uint8_t*>(node), 0x28); title_global<float>(0x00927f58) > note_time(note); note += 12)
                AnimTree_NotifyNotetrack(note, notify_entity(), notify, note_name_ptr(note));
        }
        u16_ref(info, 2) = static_cast<uint16_t>(AnimData_FindNotetrack(node, title_global<float>(0x00927f58)));
        return;
    }
    if (time == one) {
        if (previous > note_time(note)) return;
        do {
            AnimTree_NotifyNotetrack(note, notify_entity(), notify, note_name_ptr(note));
            note += 12;
        } while (note_name(note));
        return;
    }
    const float first_time = note_time(note);
    if (first_time >= time) return;
    if (previous > first_time) return;
    do {
        uint32_t hash;
        const uint32_t* sent = notify;
        if (weapsound) {
            hash = Com_HashKeyNoCase(0, &title_global<char>(0x00832af8));
            sent = &hash;
        }
        AnimTree_NotifyNotetrack(note, notify_entity(), sent, note_name_ptr(note));
        note += 12;
    } while (note_name(note) && title_global<float>(0x00927f58) > note_time(note));
    u16_ref(info, 2) = static_cast<uint16_t>(AnimData_FindNotetrack(node, title_global<float>(0x00927f58)));
}

}  // namespace game

namespace game {

void XB_CDECL AnimData_Purge(uint32_t clip) {
    // Every node of the anim-tree table (entries 1..) that uses `clip` loses it: the live
    // trees over that entry (list 0x008dda40, ended by 0x008dda44) free the node's info, and
    // the node drops its clip, child count, notetracks and stamp.
    const uint8_t* table = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
        *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f524) + 4))));
    for (uint32_t e = 1; e < *reinterpret_cast<const uint32_t*>(table); ++e) {
        const uint32_t entries = *reinterpret_cast<const uint32_t*>(table);
        uint8_t* anims = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(table + 4) +
                                                                           (e < entries ? e : 0) * 12));
        for (uint32_t i = 0; i < u32_ref(anims, 4); ++i) {
            if (u32_ref(anim_node(anims, i), 0x24) != clip) continue;
            uint8_t* tree = title_global<uint8_t*>(0x008dda40);
            uint8_t* next = tree ? reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(tree, 0))) : nullptr;
            if (tree != &title_global<uint8_t>(0x008dda44) && next) {
                while (true) {
                    if (u32_ref(tree, 8) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(anims)))
                        if (const uint16_t id = u16_at(tree, 0x18 + i * 2)) {
                            AnimTree_FreeInfo(tree, id);
                            u16_ref(tree, 0x18 + i * 2) = 0;
                        }
                    if (!next) break;
                    tree = next;
                    next = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(next, 0)));
                    if (!next) break;
                }
            }
            uint8_t* node = anim_node(anims, i);
            u32_ref(node, 0x24) = 0;
            u16_ref(node, 0x20) = 0;
            if (u32_ref(node, 0x28)) free_notetracks(node);
            u32_ref(node, 0x2c) = 0;
        }
    }
}

namespace {

uint32_t& stack_word(uint32_t address) { return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(address)); }

// A rebuilt original frame: `s` is the original's ESP after its prologue. Each call runs
// at that ESP with the original's registers, and the original's return address is put back
// where the call left ours, so the dead stack below looks as the original leaves it.
struct SampleFrame {
    uint32_t s;
    uint32_t call(uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count, const uint32_t* args) const {
        const uint32_t result = title_call_at_regs(s, function, &regs, count, args);
        stack_word(s - count * 4 - 4) = ret;
        return result;
    }
};

// The root transform {quaternion, position} of the node's clip between `previous` and
// `time`, concatenated onto *out (a temporary track and state over the clip). Both
// samplers share this body (0x002e4570 / 0x002e4650, `base` = the function): push ebp;
// and esp,-16; sub esp,0x54; push ebx/esi/edi. Every callee runs at the original's ESP
// with its registers: NAL_AnimTrack_Sample's channel code copies a never-written stack
// word (the position's w lane) into the state data (+0x5c), and with our C frame there
// that word was ours.
void sample_root(uint32_t entry, uint32_t base, uint8_t* node, float* out, uint32_t time, uint32_t previous,
                 const TitleRegs& in) {
    const SampleFrame f{((entry - 4) & ~15u) - 0x60};
    const bool between = base == 0x002e4650;
    const uint32_t r = between ? 0xe0 : 0;  // the return addresses after the shared prefix
    stack_word(f.s + 8) = in.ebx;
    stack_word(f.s + 4) = in.esi;
    stack_word(f.s + 0) = in.edi;
    const uint32_t state = f.s + 0x10;    // AnimState
    const uint32_t root = f.s + 0x20;     // the clip's root record
    const uint32_t product = f.s + 0x40;  // QuatTrans_Concat's result
    const uint32_t node_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    uint32_t eax = u32_ref(node, 0x24);
    if (!eax)
        eax = f.call(0x002e458a + r, 0x002bfb90, TitleRegs{0, in.ebx, in.ecx, in.edx, in.esi, node_a}, 0,
                     nullptr);  // AnimData_EnsureLoaded
    const uint32_t clip = u32_ref(node, 0x24);
    if (!clip) return;
    const uint32_t skeleton = stack_word(clip + 0x30);
    const uint32_t alloc_args[] = {0x30, 4, 0};
    const uint32_t memory = f.call(0x002e45a3 + r, 0x00093950, TitleRegs{eax, in.ebx, in.ecx, in.edx, clip, skeleton}, 3,
                                   alloc_args);  // Mem_AlignedAlloc
    if (!memory) return;
    const uint32_t construct_args[] = {clip, skeleton};
    const uint32_t track = f.call(0x002e45b7 + r, 0x00087dd0, TitleRegs{memory, in.ebx, memory, 0, clip, skeleton}, 2,
                                  construct_args);  // NAL_AnimTrack_Construct
    if (!track) return;
    uint32_t generic = stack_word(track + 0xc);
    eax = track;
    if (generic) {
        eax = stack_word(generic);
        if (eax != title_global<uint32_t>(0x00903fdc)) generic = 0;
    }
    const uint32_t init_args[] = {generic, 0};
    f.call(0x002e45e0 + r, 0x00087ad0, TitleRegs{eax, track, state, 0, generic, skeleton}, 2,
           init_args);  // NAL_AnimState_Init
    if (between) {
        // A step that wrapped (from > to) runs to to + 1, kept in the `to` argument slot.
        const float from = bits_float(previous);
        float to = bits_float(time);
        if (from > to) {
            to = to + title_global<float>(0x0083eb20);
            std::memcpy(&time, &to, 4);
            stack_word(entry + 0xc) = time;
        }
    }
    const uint32_t count = stack_word(generic + 0x60) - 1;
    const uint32_t sample_args[] = {time, previous, state, generic + 0xc8, count, 0};
    const uint32_t sampled = f.call(between ? 0x002e46fe : 0x002e4600, 0x000881c0,
                                    TitleRegs{between ? previous : time, track, track, state, generic + 0xc8, skeleton}, 6,
                                    sample_args);  // NAL_AnimTrack_Sample
    const uint32_t root_args[] = {state, root};
    f.call(between ? 0x002e4711 : 0x002e4613, 0x00086d30,
           TitleRegs{sampled, track, stack_word(state), state, generic + 0xc8, skeleton}, 2,
           root_args);  // NAL_GenericAnim_GetRootTransform
    const uint32_t out_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out));
    // QuatTrans_Concat leaves the position's w lane unwritten and the whole product is
    // copied out: the original's word there is never set (zero in tests; with a C caller
    // above, it held that caller's leftovers).
    stack_word(product + 0x1c) = 0;
    const uint32_t concat_args[] = {product, out_a, root};
    const uint32_t result = f.call(between ? 0x002e4724 : 0x002e4626, 0x0001cae0,
                                   TitleRegs{root, track, product, 0, generic + 0xc8, out_a}, 3,
                                   concat_args);  // QuatTrans_Concat
    platform_rep_movs(out, reinterpret_cast<const float*>(static_cast<uintptr_t>(result)), 0x20);
    const uint32_t vtable = stack_word(track);
    const uint32_t one[] = {1};
    f.call(between ? 0x002e4738 : 0x002e463a, stack_word(vtable),
           TitleRegs{result, track, track, vtable, result + 0x20, out_a + 0x20}, 1, one);  // deleting destructor
    f.call(between ? 0x002e4741 : 0x002e4643, 0x00086da0,
           TitleRegs{0, track, state, 0, result + 0x20, out_a + 0x20}, 0, nullptr);  // NAL_AnimState_Destroy
}

}  // namespace

// (node in EAX; out, time / out, from, to): the body in C below a gap, with the entry's
// registers and ESP.
PLATFORM_ASM_ENTRY(AnimTree_SampleRootTransform_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %ebx\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl %eax\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimTree_SampleRootTransform) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL AnimTree_SampleRootTransform(uint32_t entry, uint8_t* node, float* out, uint32_t time_bits, uint32_t ebx,
                                           uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t edi) {
    sample_root(entry, 0x002e4570, node, out, time_bits, 0, TitleRegs{0, ebx, ecx, edx, esi, edi});
}

PLATFORM_ASM_ENTRY(AnimTree_SampleRootTransformBetween_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %ebx\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl %eax\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimTree_SampleRootTransformBetween) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL AnimTree_SampleRootTransformBetween(uint32_t entry, uint8_t* node, float* out, uint32_t from_bits,
                                                  uint32_t to_bits, uint32_t ebx, uint32_t ecx, uint32_t edx, uint32_t esi,
                                                  uint32_t edi) {
    sample_root(entry, 0x002e4650, node, out, to_bits, from_bits, TitleRegs{0, ebx, ecx, edx, esi, edi});
}

namespace {

// The two samplers by title address (asm entries; the root-delta getters copy out only the
// direction and position, so the depth they run at does not show).
void sample_call(uint8_t* node, float* out, uint32_t time) {
    title_function<void XB_REGS1 XB_CDECL(uint8_t*, float*, uint32_t)>(0x002e4570)(node, out, time);  // AnimTree_SampleRootTransform
}

void sample_between_call(uint8_t* node, float* out, uint32_t from, uint32_t to) {
    title_function<void XB_REGS1 XB_CDECL(uint8_t*, float*, uint32_t, uint32_t)>(0x002e4650)(node, out, from, to);  // AnimTree_SampleRootTransformBetween
}

}  // namespace

namespace {

// The root transform of node `index` (identity when it is not a leaf) as a forward
// direction (row 0's x, y) and a position.
void root_delta(uint32_t index, const uint8_t* anims, float* direction, float* position, bool between, uint32_t from_bits,
                uint32_t to_bits) {
    const uint32_t count = *reinterpret_cast<const uint32_t*>(anims + 4);
    uint8_t* node = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(anims + 8) +
                                                                      (index < count ? index : 0) * 0x38));
    alignas(16) float transform[8] = {0.0f, 0.0f, 0.0f, title_global<float>(0x0083eb20), 0.0f, 0.0f, 0.0f, 0.0f};
    if (!u16_at(node, 0x20)) {
        if (between)
            sample_between_call(node, transform, from_bits, to_bits);
        else
            sample_call(node, transform, from_bits);
    }
    alignas(16) float axis[16];
    Quat_ToMat33(axis, transform);
    std::memcpy(&direction[0], &axis[0], 4);
    std::memcpy(&direction[1], &axis[1], 4);
    std::memcpy(position, &transform[4], 12);
}

}  // namespace

// (index in EAX, anim data in ECX, direction out in EDI, position out in ESI; time).
PLATFORM_ASM_ENTRY(AnimTree_GetRootDelta_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimTree_GetRootDelta) "addl $20, %esp\n"
                   "ret")

void XB_CDECL AnimTree_GetRootDelta(uint32_t index, const uint8_t* anims, float* direction, float* position, uint32_t time) {
    root_delta(index, anims, direction, position, false, time, 0);
}

// (index in EAX, anim data in ECX, direction out in EDI, position out in ESI; from, to).
PLATFORM_ASM_ENTRY(AnimTree_GetRootDeltaBetween_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimTree_GetRootDeltaBetween) "addl $24, %esp\n"
                   "ret")

void XB_CDECL AnimTree_GetRootDeltaBetween(uint32_t index, const uint8_t* anims, float* direction, float* position,
                                           uint32_t from, uint32_t to) {
    root_delta(index, anims, direction, position, true, from, to);
}

}  // namespace game

namespace game {

// (generic clip in EDI; out, bone name, channel type; callee pops 12).
// The three never-set locals the title copies into out[1..3] are read from below the entry
// ESP, where the original's frame has them.
PLATFORM_ASM_ENTRY(NAL_Clip_FindChannelByName_entry,
                   "pushl -4(%esp)\n"
                   "pushl -4(%esp)\n"
                   "pushl -4(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(NAL_Clip_FindChannelByName) "addl $28, %esp\n"
                   "ret $12")

void XB_CDECL NAL_Clip_FindChannelByName(uint8_t* generic, uint32_t* out, const uint32_t* bone, const uint32_t* type,
                                         uint32_t stale1, uint32_t stale2, uint32_t stale3) {
    // The channels (+0x80, 0x28 bytes: +0 type name hash, +0x20 bone) of each group (+0x88,
    // 0x30 bytes: +0x20 the channel object, +0x28 count; group count +0x84) are matched on
    // their 32-byte type and bone name hashes (bones +0x74, 0x30 bytes). The match's
    // object gets its slot 0 call; out = {clip, group, channel in group}. The title copies
    // three never-set locals into out[1..3] first.
    out[0] = 0;
    out[1] = stale1;
    out[2] = stale2;
    out[3] = stale3;
    int32_t channel = 0;
    for (int32_t g = 0; g < static_cast<int32_t>(u32_ref(generic, 0x84)); ++g) {
        const int32_t count = static_cast<int32_t>(u32_ref(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(generic, 0x88) + g * 0x30)), 0x28));
        const uint8_t* record = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(u32_ref(generic, 0x80) + channel * 0x28));
        for (int32_t k = 0; k < count; ++k, ++channel, record += 0x28) {
            const uint32_t* words = reinterpret_cast<const uint32_t*>(record);
            int32_t i = 0;
            while (i < 8 && type[i] == words[i]) ++i;
            if (i < 8) continue;
            const uint32_t* name = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(u32_ref(generic, 0x74) + words[8] * 0x30));
            i = 0;
            while (i < 8 && bone[i] == name[i]) ++i;
            if (i < 8) continue;
            const uint32_t object = u32_ref(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(generic, 0x88) + g * 0x30)), 0x20);
            const uint32_t vtable = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(object));
            title_function<void XB_THISCALL(uint32_t)>(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(vtable)))(object);
            out[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(generic));
            out[1] = u32_ref(generic, 0x88) + g * 0x30;
            out[2] = static_cast<uint32_t>(k);
            return;
        }
    }
}

// (clip in EBX; channel from NAL_Clip_FindChannelByName; callee pops 4).
PLATFORM_ASM_ENTRY(NAL_Clip_FindChannelData_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(NAL_Clip_FindChannelData) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL NAL_Clip_FindChannelData(uint8_t* clip, const uint32_t* channel) {
    // Walks the clip's data (+0x50) group by group (slots 0x50/0x54/0x58 of the group's
    // object) and over each present channel (bit first + j of the mask +0x54; slot 0x5c
    // skips one) up to the wanted one: its data, 0 when it is not present.
    uint32_t data = u32_ref(clip, 0x50);
    const uint8_t* generic = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(u32_ref(clip, 0x30)));
    uint8_t* group = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(generic + 0x88)));
    const auto call = [&data](uint8_t* g, uint32_t slot) {
        const uint32_t object = u32_ref(g, 0x20);
        const uint32_t vtable = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(object));
        title_function<void XB_THISCALL(uint32_t, uint32_t*)>(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(vtable + slot)))(object, &data);
    };
    for (int32_t g = 0; g < static_cast<int32_t>(*reinterpret_cast<const uint32_t*>(generic + 0x84)); ++g, group += 0x30) {
        call(group, 0x50);
        call(group, 0x54);
        call(group, 0x58);
        for (int32_t j = 0; j < static_cast<int32_t>(u32_ref(group, 0x28)); ++j) {
            const int32_t bit = static_cast<int32_t>(u32_ref(group, 0x24)) + j;
            const uint32_t* mask = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(u32_ref(clip, 0x54)));
            if (!(mask[bit / 32] & (1u << (bit & 31)))) continue;
            if (channel[1] == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(group)) && channel[2] == static_cast<uint32_t>(j))
                return data;
            call(group, 0x5c);
        }
    }
    return 0;
}

namespace {

// The notetrack name hash: rotate left 5, xor the lower-cased (A-Z only) signed char.
uint32_t note_hash(const char* text) {
    uint32_t hash = 0;
    if (!text) return 0;
    for (; *text; ++text) {
        char c = *text;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 0x20);
        hash = ((hash >> 27) | (hash << 5)) ^ static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(c)));
    }
    return hash;
}

uint32_t* hash_str_init(uint32_t* out, uint32_t text) {
    return title_function<uint32_t* XB_THISCALL(uint32_t*, uint32_t)>(0x00011060)(out, text);  // HashStr_Init
}

void assign_cstring(uint32_t* note, const char* text) {
    title_function<void XB_THISCALL(uint32_t*, const char*)>(0x0011dd20)(note, text);  // ScriptString_AssignCString
}

}  // namespace

void XB_CDECL XAnim_BuildNotetracks(uint8_t* node) {
    // The clip's "COD_Note" channel (on "fakeroot", else "tag_origin"): +4 note count, +8
    // name count, then {name index, frame} pairs and the names. Records {string, name hash,
    // frame / clip frames (+0x44)} plus "end" at 1 and a terminator (name 0, time -666);
    // without notes only the last two.
    uint32_t cursor[4] = {0, 0, 0, 0};
    uint32_t* note = nullptr;
    const uint8_t* data = nullptr;
    if (u32_ref(node, 0x24)) {
        uint32_t type[8];
        uint32_t bone[8];
        hash_str_init(type, 0x00832b1c);  // "COD_Note"
        hash_str_init(bone, 0x00832b10);  // "fakeroot"
        uint8_t* clip = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(node, 0x24)));
        NAL_Clip_FindChannelByName(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(clip, 0x30))), cursor, bone, type, 0, 0, 0);
        if (!cursor[0]) {
            hash_str_init(type, 0x00832b1c);  // "COD_Note"
            hash_str_init(bone, 0x00832b04);  // "tag_origin"
            clip = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(node, 0x24)));
            NAL_Clip_FindChannelByName(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(clip, 0x30))), cursor, bone, type, 0, 0, 0);
        }
        if (cursor[0])
            data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
                NAL_Clip_FindChannelData(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(u32_ref(node, 0x24))), cursor)));
    }
    if (data) {
        const char* names[1024];
        const int32_t count = *reinterpret_cast<const int32_t*>(data + 4);
        const char* text = reinterpret_cast<const char*>(data + 0xc + count * 8);
        for (int32_t i = 0; i < *reinterpret_cast<const int32_t*>(data + 8); ++i) {
            names[i] = text;
            const char* end = text;
            while (*end++) {
            }
            text = end;
        }
        const uint32_t records = *reinterpret_cast<const uint32_t*>(data + 4) + 2;
        uint32_t* memory = title_function<uint32_t* XB_CDECL(uint32_t)>(0x00042c30)(records * 12 + 4);  // Heap_Alloc16
        if (memory) {
            memory[0] = records;
            note = memory + 1;
            for (uint32_t k = 0; k < records; ++k) ScriptString_FromRep(reinterpret_cast<ScriptString*>(note + k * 3), nullptr);
        }
        u32_ref(node, 0x28) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(note));
        if (*reinterpret_cast<const uint32_t*>(data + 8)) {
            const uint32_t* pairs = reinterpret_cast<const uint32_t*>(data + 0xc);
            for (int32_t i = 0; i < *reinterpret_cast<const int32_t*>(data + 4); ++i, note += 3) {
                assign_cstring(note, names[pairs[i * 2]]);
                note[1] = note_hash(names[pairs[i * 2]]);
                const uint8_t* clip = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(u32_ref(node, 0x24)));
                const float time = static_cast<float>(static_cast<int32_t>(pairs[i * 2 + 1])) /
                                   static_cast<float>(*reinterpret_cast<const int32_t*>(clip + 0x44));
                std::memcpy(&note[2], &time, 4);
            }
        }
    } else {
        uint32_t* memory = title_function<uint32_t* XB_CDECL(uint32_t)>(0x00042c30)(0x1c);  // Heap_Alloc16
        if (memory) {
            memory[0] = 2;
            note = memory + 1;
            title_function<void XB_STDCALL(uint32_t*, uint32_t, uint32_t, uint32_t)>(0x00011240)(note, 0xc, 2, 0x0001dc00);  // eh_vector_constructor_iterator
        }
        u32_ref(node, 0x28) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(note));
    }
    const char* end = &title_global<char>(0x00832b28);  // "end"
    assign_cstring(note, end);
    note[1] = note_hash(end);
    note[2] = title_global<uint32_t>(0x0083eb20);
    note += 3;
    if (const uint32_t rep = note[0]) {
        title_function<void XB_THISCALL(uint32_t)>(0x0011d5b0)(rep);  // ScriptStringRep_Release
        note[0] = 0;
    }
    note[1] = 0;
    note[2] = title_global<uint32_t>(0x0083ee54);
}

}  // namespace game

namespace game {

namespace {

// The weight a root-delta blend uses for child `i` of `node`: goal weight (+0x20) when
// 0x0092980c is set, else weight (+0x24); null without an info.
const float* blend_weight(const uint8_t* tree, const uint8_t* node, int32_t i, uint32_t goal) {
    const uint16_t id = info_index(tree, u16_at(node, 0x36) + i);
    if (!id) return nullptr;
    return &info_float(info_at(id), goal ? 0x20 : 0x24);
}

}  // namespace

// The body in C below a gap: the samplers and the recursion run at the original's ESP
// (the samplers copy a never-written product word, the position's w, into *out: with our
// C frame above them it held our leftovers).
PLATFORM_ASM_ENTRY(AnimTree_CalcRootDelta_r_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimTree_CalcRootDelta_r) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL AnimTree_CalcRootDelta_r(uint32_t entry, uint8_t* tree, uint32_t index, float* out, uint32_t ebx,
                                       uint32_t esi, uint32_t edi) {
    // push ebp; and esp,-16; sub esp,0xb4; push ebx/esi/edi.
    const SampleFrame f{((entry - 4) & ~15u) - 0xc0};
    stack_word(f.s + 8) = ebx;
    stack_word(f.s + 4) = esi;
    stack_word(f.s + 0) = edi;
    float* const child = reinterpret_cast<float*>(static_cast<uintptr_t>(f.s + 0x60));  // the original's slot
    const uint32_t tree_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    const uint32_t out_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out));
    // Down single active children to a leaf (its root transform at the info's time, or
    // over the last step unless 0x00929cc8 is set) or to a node with several: those blend
    // their children's deltas by normalized weight (positions lerped, rotations slerped).
    const uint32_t goal = title_global<uint32_t>(0x0092980c);
    const float one = title_global<float>(0x0083eb20);
    const uint8_t* anims = tree_anims(tree);
    while (true) {
        uint8_t* node = anim_node(anims, index);
        const int32_t count = u16_at(node, 0x20);
        if (!count) {
            const uint16_t id = info_index(tree, index);
            if (!id) return;
            uint8_t* info = info_at(id);
            const uint32_t node_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
            const uint32_t nodes = *reinterpret_cast<const uint32_t*>(anims + 8);
            if (title_global<uint8_t>(0x00929cc8)) {
                const uint32_t args[] = {out_a, u32_ref(info, 0x10)};
                f.call(0x002e6911, 0x002e4570, TitleRegs{node_a, node_a, out_a, tree_a, goal, nodes}, 2,
                       args);  // AnimTree_SampleRootTransform
            } else {
                const uint32_t args[] = {out_a, u32_ref(info, 0x14), u32_ref(info, 0x10)};
                f.call(0x002e692a, 0x002e4650, TitleRegs{node_a, node_a, out_a, args[2], goal, nodes}, 3,
                       args);  // AnimTree_SampleRootTransformBetween
            }
            return;
        }
        int32_t i = 0;
        const float* w = nullptr;
        for (; i < count; ++i) {
            w = blend_weight(tree, node, i, goal);
            if (w && !(*w == 0.0f)) break;
        }
        if (i >= count) return;
        float weights[24];
        int32_t indices[24];
        weights[0] = *w;
        indices[0] = i;
        int32_t n = 1;
        for (int32_t j = i + 1; j < count; ++j) {
            const float* v = blend_weight(tree, node, j, goal);
            if (!v || *v == 0.0f) continue;
            weights[n] = *v;
            indices[n] = j;
            ++n;
        }
        if (n == 1) {
            index = u16_at(node, 0x36) + i;
            continue;
        }
        float total = 0.0f;
        int32_t a = 0;
        if (n >= 4) {
            do {
                total = (((weights[a] + total) + weights[a + 1]) + weights[a + 2]) + weights[a + 3];
                a += 4;
            } while (a < n - 3);
        }
        for (; a < n; ++a) total = weights[a] + total;
        a = 0;
        if (n >= 4) {
            const float inv = one / total;
            do {
                for (int32_t k = 0; k < 4; ++k) weights[a + k] = inv * weights[a + k];
                a += 4;
            } while (a < n - 3);
        }
        if (a < n) {
            const float inv = one / total;
            for (; a < n; ++a) weights[a] = inv * weights[a];
        }
        const uint32_t node_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
        const uint32_t first_args[] = {tree_a, u16_at(node, 0x36) + static_cast<uint32_t>(indices[0]), out_a, 1, 1};
        f.call(0x002e6be7, 0x002e67d0, TitleRegs{tree_a, static_cast<uint32_t>(n), node_a, first_args[1], goal, out_a}, 5,
               first_args);  // AnimTree_CalcRootDelta_r
        for (int32_t s = 1; s < n; ++s) {
            const float t = one / (weights[s - 1] / weights[s] + one);
            // The child starts as identity; its w lane (+0x1c) is never set.
            const float identity[7] = {0.0f, 0.0f, 0.0f, one, 0.0f, 0.0f, 0.0f};
            std::memcpy(child, identity, sizeof identity);
            const uint32_t child_args[] = {tree_a, u16_at(node, 0x36) + static_cast<uint32_t>(indices[s]), f.s + 0x60, 0, 1};
            f.call(0x002e6ca6, 0x002e67d0,
                   TitleRegs{tree_a, static_cast<uint32_t>(n), tree_a, child_args[1], static_cast<uint32_t>(s), out_a + 0x10},
                   5, child_args);  // AnimTree_CalcRootDelta_r
            const float keep = one - t;
            const float x = out[4] * keep + child[4] * t;
            const float y = out[5] * keep + child[5] * t;
            const float z = out[6] * keep + child[6] * t;
            std::memcpy(&out[4], &x, 4);
            std::memcpy(&out[5], &y, 4);
            std::memcpy(&out[6], &z, 4);
            alignas(16) float rotation[4];
            platform_rep_movs(out, Quat_Slerp(rotation, out, child, t), 16);
        }
        return;
    }
}

// (tree in EDX, index in ECX, direction out in EDI, position out in ESI).
PLATFORM_ASM_ENTRY(AnimTree_GetTreeRootDelta_entry,
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(AnimTree_GetTreeRootDelta) "addl $16, %esp\n"
                   "ret")

void XB_CDECL AnimTree_GetTreeRootDelta(uint8_t* tree, uint32_t index, float* direction, float* position) {
    // The blended root transform of the subtree at its current times, by goal weight.
    title_global<uint8_t>(0x00929cc8) = 1;
    title_global<uint32_t>(0x0092980c) = 1;
    alignas(16) float transform[8] = {0.0f, 0.0f, 0.0f, title_global<float>(0x0083eb20), 0.0f, 0.0f, 0.0f, 0.0f};
    title_function<void XB_CDECL(uint8_t*, uint32_t, float*, uint32_t, uint32_t)>(0x002e67d0)(tree, index, transform, 1,
                                                                                            0);  // AnimTree_CalcRootDelta_r
    alignas(16) float axis[16];
    Quat_ToMat33(axis, transform);
    std::memcpy(&direction[0], &axis[0], 4);
    std::memcpy(&direction[1], &axis[1], 4);
    std::memcpy(position, &transform[4], 12);
}

}  // namespace game

namespace game {

namespace {

// FLD DWORD / FSTP QWORD: a float widened on the x87 for a printf argument (no MXCSR flags).
double x87_widen(const float& value) {
    double result;
    __asm__("flds %1\n\tfstpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}

using VaFn = const char* XB_CDECL(uint32_t, ...);

}  // namespace

void XB_CDECL AnimTree_DebugDraw_r(const uint8_t* tree, uint32_t index, int32_t depth, int32_t* line) {
    // One line per node with weight: name (or index), weight:goal, for leaves and synced
    // nodes the time:previous time, for leaves also the step in seconds; children indented.
    const uint16_t id = info_index(tree, index);
    if (!id) return;
    const uint8_t* anims = tree_anims(tree);
    uint8_t* node = anim_node(anims, index);
    uint8_t* info = info_at(id);
    if (info_float(info, 0x24) == 0.0f) return;
    const int32_t count = u16_at(node, 0x20);
    const float one = title_global<float>(0x0083eb20);
    const char* text;
    if (!count) {
        float step = info_float(info, 0x10) - info_float(info, 0x14);
        if (0.0f > step) step = step + one;
        if (!u32_ref(node, 0x24)) AnimData_EnsureLoaded(node);
        const uint8_t* clip = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(u32_ref(node, 0x24)));
        if (clip) {
            float frequency = 0.0f;
            const float length = *reinterpret_cast<const float*>(clip + 0x38);
            if (!(length == 0.0f)) frequency = one / length;
            step = step / frequency;
        } else {
            step = 0.0f;
        }
        const uint32_t name = clip ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip + 0xc)) : 0x00832aec;  // "<unknown>"
        text = title_function<VaFn>(0x002a93d0)(0x00832ac8, name, x87_widen(info_float(info, 0x24)), x87_widen(info_float(info, 0x20)),
                                                x87_widen(info_float(info, 0x10)), x87_widen(info_float(info, 0x14)), x87_widen(step));
    } else {
        const uint32_t entries = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(
            *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f524) + 4)) + 4));
        const int32_t entry = (static_cast<int32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(anims)) - entries)) / 12;
        const uint32_t handle = (static_cast<uint32_t>(entry) << 16) + index;
        const bool timed = AnimTree_IsLeafOrSynced(index, 0, anims);
        const uint32_t names = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f49c) + 0x870));
        const uint32_t name = title_function<uint32_t XB_CDECL(uint32_t)>(names)(handle);
        if (timed)
            text = title_function<VaFn>(0x002a93d0)(name ? 0x00832aac : 0x00832a90, name ? name : index,
                                                    x87_widen(info_float(info, 0x24)), x87_widen(info_float(info, 0x20)),
                                                    x87_widen(info_float(info, 0x10)), x87_widen(info_float(info, 0x14)));
        else
            text = title_function<VaFn>(0x002a93d0)(name ? 0x00832a80 : 0x00832a70, name ? name : index,
                                                    x87_widen(info_float(info, 0x24)), x87_widen(info_float(info, 0x20)));
    }
    const float color[4] = {one, one, one, one};
    R_DrawTextWithShadow(color, text, depth * 12 + 0x10, (*line + 2) << 4, 0.0f, one);
    ++*line;
    for (int32_t i = 0; i < count; ++i) AnimTree_DebugDraw_r(tree, u16_at(node, 0x36) + i, depth + 1, line);
}


void XB_CDECL AtExit_ReleaseScriptStrings_965ce0() {
    // The 1024 script strings (4 bytes each) below 0x00965ce0, last first.
    for (uint32_t i = 1; i <= 0x400; ++i)
        title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(0x00965ce0 - i * 4);  // ScriptString_Release
}

void XB_CDECL AtExit_ReleaseScriptString_965ce8() {
    title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(0x00965ce8);  // ScriptString_Release
}

void XB_CDECL AtExit_HashStrMap_903ff4() {
    title_function<void XB_THISCALL(uint32_t)>(0x00094010)(0x00903ff4);  // HashStrMap_Clear
}

}  // namespace game
