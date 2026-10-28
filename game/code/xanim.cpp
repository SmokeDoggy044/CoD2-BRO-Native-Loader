#include "xanim.h"

#include <cstring>

#include "g_scr_runtime.h"
#include "title_data.h"

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
