#include "xanim.h"

#include <cstring>

#include "dobj.h"
#include "g_scr_runtime.h"
#include "nal_anim.h"
#include "qcommon.h"
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
    title_function<void XB_CDECL(uint8_t*, const uint8_t*)>(0x002c45f0)(info, node);  // AnimTree_ProcessNotetracks
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
    title_function<void XB_CDECL(uint8_t*)>(0x002b7e40)(node);  // XAnim_BuildNotetracks
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
