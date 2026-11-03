#include "g_pathnode.h"

#include "title_data.h"

namespace game {

PLATFORM_ASM_ENTRY(PathNodeTree_ReservoirPickDeepest_entry,
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(PathNodeTree_ReservoirPickDeepest) "addl $12, %esp\n"
                   "ret")

void XB_CDECL PathNodeTree_ReservoirPickDeepest(uint32_t* count, const uint8_t* node, uint32_t* best) {
    const uint32_t node_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    if (*best) {
        const int16_t depth = *reinterpret_cast<const int16_t*>(node + 0x5e);
        const int16_t best_depth =
            *reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(uintptr_t{*best}) + 0x5e);
        if (depth == best_depth) {
            ++*count;
            const uint32_t r = static_cast<uint32_t>(title_function<int32_t XB_CDECL()>(0x001efcc0)());  // _rand
            if (static_cast<int32_t>(r * *count) <= 0x7fff) *best = node_word;
            return;
        }
        if (depth <= best_depth) return;
    }
    *best = node_word;
    *count = 1;
}

uint32_t* XB_THISCALL PathNodeTree_Link_r(uint32_t ecx, uint32_t base, uint32_t** cursor) {
    uint32_t* const node = *cursor;
    *cursor = node + 4;
    if (static_cast<int32_t>(node[0]) >= 0) {
        node[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PathNodeTree_Link_r(ecx, base, cursor)));
        node[3] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PathNodeTree_Link_r(ecx, base, cursor)));
        return node;
    }
    node[2] = base + node[2] * 4;
    return node;
}

uint32_t XB_THISCALL Actor_GetAllowedStanceMask(const uint8_t* node) {
    const uint16_t flags = *reinterpret_cast<const uint16_t*>(node + 0x46);
    uint32_t mask = (flags & 4) ? 6 : 7;
    if (flags & 2) mask &= ~2u;
    if (flags & 1) mask &= ~4u;
    return mask;
}

PLATFORM_ASM_ENTRY(Actor_GetProneBlend_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Actor_GetProneBlend) "addl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL Actor_GetProneBlend(uint32_t time_word, uint32_t* blend) {
    const int32_t time = static_cast<int32_t>(time_word);
    const int32_t start = static_cast<int32_t>(blend[1]);
    if (!start) return 0.0f;
    const int32_t duration = static_cast<int32_t>(blend[2]);
    if (!duration) return title_global<float>(0x0083eb20);
    if (duration < 0) {
        if (start - duration < time) {
            blend[1] = 0;
            return 0.0f;
        }
        const float span = static_cast<float>(-duration);
        const float part = static_cast<float>(time - start);
        return title_global<float>(0x0083eb20) - part / span;
    }
    if (duration + start < time) {
        blend[2] = 0;
        return title_global<float>(0x0083eb20);
    }
    const float part = static_cast<float>(time - start);
    const float span = static_cast<float>(duration);
    return part / span;
}

PLATFORM_ASM_ENTRY(ProneBlend_Update_entry,
                   "pushl %esi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(ProneBlend_Update) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL ProneBlend_Update(uint32_t* blend, int32_t time) {
    const int32_t start = static_cast<int32_t>(blend[1]);
    if (!start || blend[0]) return 0;
    const int32_t duration = static_cast<int32_t>(blend[2]);
    if (!duration) return 1;
    if (duration < 0) {
        if (start - duration >= time) return 1;
        blend[1] = 0;
        return 0;
    }
    if (duration + start < time) blend[2] = 0;
    return 1;
}

}  // namespace game
