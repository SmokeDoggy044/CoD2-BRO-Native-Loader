#include "g_pathnode.h"

#include <cstring>

#include "actor.h"
#include "g_utils.h"
#include "qcommon.h"
#include "qcommon_fs.h"
#include "sv.h"
#include "title_data.h"
#include "g_script.h"
#include "script_string.h"

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

namespace {

uint8_t* ptr(uint32_t word) { return reinterpret_cast<uint8_t*>(uintptr_t{word}); }
uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }
float& float_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<float*>(base + offset); }
uint16_t& half_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint16_t*>(base + offset); }
// Path data: +8 the node blocks (one pointer per zone), +0x194 the info record (+8 sorted node count,
// +0xc block count, +0x14 the sorted node handles).
uint8_t* path_info(uint8_t* world) { return ptr(word_at(world, 0x194)); }
uint8_t* path_block(uint8_t* world, uint32_t index) { return ptr(word_at(world, 8 + index * 4)); }
// A node handle: bits 9-15 = block + 1, bits 0-8 = the node in the block.
uint32_t handle_block(uint32_t handle) { return static_cast<uint16_t>((handle >> 9) - 1); }
bool type_in_mask(uint32_t mask, uint32_t type) { return (mask & (1u << (type & 31))) != 0; }

// The cylinder query PathNode_ListInCylinder fills in for PathNode_ListInCylinder_r.
constexpr uint32_t kQueryOrigin = 0x00929b24;
constexpr uint32_t kQueryRadius = 0x00929b30;
constexpr uint32_t kQueryRadiusSq = 0x00929b34;
constexpr uint32_t kQueryHeightSq = 0x00929b38;
constexpr uint32_t kQueryMask = 0x00929b3c;
constexpr uint32_t kQueryList = 0x00929b40;
constexpr uint32_t kQueryMax = 0x00929b44;
constexpr uint32_t kQueryCount = 0x00929b48;

}  // namespace

void XB_THISCALL PathNode_ListInCylinder_r(uint8_t* world, const uint32_t* tree) {
    int32_t axis = static_cast<int32_t>(tree[0]);
    if (axis >= 0) {
        float radius = title_global<float>(kQueryRadius);
        do {
            const float d = title_global<float>(kQueryOrigin + axis * 4) - reinterpret_cast<const float*>(tree)[1];
            if (radius >= d) {
                PathNode_ListInCylinder_r(world, reinterpret_cast<const uint32_t*>(ptr(tree[2])));
                radius = title_global<float>(kQueryRadius);
            }
            if (!(d >= 0.0f - radius)) return;
            tree = reinterpret_cast<const uint32_t*>(ptr(tree[3]));
            axis = static_cast<int32_t>(tree[0]);
        } while (axis >= 0);
    }
    const int32_t first_count = static_cast<int32_t>(tree[3]);
    const uint32_t* const nodes = reinterpret_cast<const uint32_t*>(ptr(tree[2]));
    if (first_count <= 0) return;
    const float height_sq = title_global<float>(kQueryHeightSq);
    const float oz = title_global<float>(kQueryOrigin + 8);
    const float radius_sq = title_global<float>(kQueryRadiusSq);
    const float oy = title_global<float>(kQueryOrigin + 4);
    const float ox = title_global<float>(kQueryOrigin);
    uint32_t count = title_global<uint32_t>(kQueryCount);
    for (int32_t i = 0;;) {
        uint8_t* const node = ptr(nodes[i]);
        const float dx = float_at(node, 0x44) - ox;
        const float dy = float_at(node, 0x48) - oy;
        const float dist = dy * dy + dx * dx;
        if (radius_sq >= dist) {
            const uint32_t mask = title_global<uint32_t>(kQueryMask);
            if (type_in_mask(mask, word_at(node, 0x28)) &&
                !(title_global<uint32_t>(0x008db4a0) && (node[0x2c] & 1) && !(mask & 1))) {
                const float dz = float_at(node, 0x4c) - oz;
                if (!(dz * dz > height_sq)) {
                    if (count == title_global<uint32_t>(kQueryMax)) break;
                    uint8_t* const list = title_global<uint8_t*>(kQueryList);
                    word_at(list, count * 8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
                    float_at(list, count * 8 + 4) = dist;
                    ++count;
                }
            }
        }
        if (++i >= static_cast<int32_t>(tree[3])) break;
    }
    title_global<uint32_t>(kQueryCount) = count;
}

PLATFORM_ASM_ENTRY(PathNode_ReleaseClaimsBy_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(PathNode_ReleaseClaimsBy) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL PathNode_ReleaseClaimsBy(uint32_t claimer, uint8_t* world) {
    if (!path_info(world)) return;
    for (int32_t b = 0; b < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++b) {
        uint8_t* const block = path_block(world, b);
        if (!block) continue;
        int32_t left = static_cast<int32_t>(word_at(block, 4));
        if (left <= 0) continue;
        uint32_t offset = 0;
        do {
            uint32_t& owner = word_at(ptr(word_at(path_block(world, b), 0x10)), offset + 4);
            if (owner == claimer) owner = 0;
            offset += 0x80;
        } while (--left);
    }
}

uint8_t* XB_REGS1 XB_STDCALL PathNode_Next(const uint16_t* handle, uint8_t* world, uint32_t mask) {
    const uint32_t h = *handle;
    const uint32_t first = handle_block(h);
    uint32_t i = (h & 0x1ff) + 1;
    uint8_t* const block = path_block(world, first);
    const int32_t first_count = static_cast<int32_t>(word_at(block, 4));
    if (static_cast<int32_t>(i) < first_count) {
        uint8_t* const nodes = ptr(word_at(block, 0x10));
        for (; static_cast<int32_t>(i) < first_count; ++i)
            if (type_in_mask(mask, word_at(nodes, i * 0x80 + 0x28))) return nodes + i * 0x80;
    }
    // The later blocks are walked with the first block's node count (as the original does).
    const int32_t blocks = static_cast<int32_t>(word_at(path_info(world), 0xc));
    for (int32_t b = static_cast<int32_t>(first) + 1; b < blocks; ++b) {
        uint8_t* const next = path_block(world, b);
        if (!next || !word_at(next, 4) || first_count <= 0) continue;
        uint8_t* const nodes = ptr(word_at(next, 0x10));
        for (uint32_t j = 0; static_cast<int32_t>(j) < first_count; ++j)
            if (type_in_mask(mask, word_at(nodes, j * 0x80 + 0x28)))
                return ptr(word_at(path_block(world, b), 0x10)) + j * 0x80;
    }
    return nullptr;
}

uint8_t* XB_STDCALL PathNode_First(uint8_t* world, uint32_t mask) {
    const int32_t blocks = static_cast<int32_t>(word_at(path_info(world), 0xc));
    for (int32_t b = 0; b < blocks; ++b) {
        uint8_t* const block = path_block(world, b);
        if (!block) continue;
        const int32_t count = static_cast<int32_t>(word_at(block, 4));
        if (count <= 0) continue;
        uint8_t* const nodes = ptr(word_at(block, 0x10));
        for (uint32_t i = 0; static_cast<int32_t>(i) < count; ++i)
            if (type_in_mask(mask, word_at(nodes, i * 0x80 + 0x28)))
                return ptr(word_at(path_block(world, b), 0x10)) + i * 0x80;
    }
    return nullptr;
}

int32_t XB_THISCALL PathNode_FindSortedIndex(uint8_t* world, const uint16_t* handle) {
    uint8_t* const info = path_info(world);
    const int32_t count = static_cast<int32_t>(word_at(info, 8));
    if (count > 0) {
        const uint16_t h = *handle;
        const uint16_t* const sorted = reinterpret_cast<const uint16_t*>(ptr(word_at(info, 0x14)));
        for (int32_t i = 0; i < count; ++i)
            if (h == sorted[i]) return i;
    }
    return -1;
}

PLATFORM_ASM_ENTRY(Path_FindNearestNodeInList_entry,
                   "pushl %ebx\n"
                   "pushl %edx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Path_FindNearestNodeInList) "addl $12, %esp\n"
                   "ret")

uint8_t* XB_CDECL Path_FindNearestNodeInList(const float* origin, uint8_t* ent, uint8_t* world) {
    uint8_t* best = nullptr;
    const uint16_t zone = ent ? half_at(ent, 0x5c) : 0;
    uint8_t* const info = path_info(world);
    const int32_t count = static_cast<int32_t>(word_at(info, 8));
    float best_dist = title_global<float>(0x0082eed0);  // FLT_MAX
    const uint16_t* const sorted = reinterpret_cast<const uint16_t*>(ptr(word_at(info, 0x14)));
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t h = sorted[i];
        uint8_t* const block = path_block(world, handle_block(h));
        if (!block) continue;
        uint8_t* const node = ptr(word_at(block, 0x10)) + (h & 0x1ff) * 0x80;
        const uint16_t node_zone = half_at(node, 0x5c);
        const float dx = float_at(node, 0x44) - origin[0];
        const float dy = float_at(node, 0x48) - origin[1];
        const float dz = float_at(node, 0x4c) - origin[2];
        if (node_zone != zone) continue;
        const float dist = dz * dz + dy * dy + dx * dx;
        if (best_dist > dist) {
            best_dist = dist;
            best = node;
        }
    }
    return best;
}

void XB_STDCALL PathNode_ResetAllRuntimeState(uint8_t* world) {
    for (int32_t b = 0; b < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++b) {
        uint8_t* const block = path_block(world, b);
        if (!block) continue;
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(word_at(block, 4)); ++i) {
            uint8_t* const node = ptr(word_at(block, 0x10)) + i * 0x80;
            word_at(node, 8) = 0;
            word_at(node, 0x68) = 0xfffffffe;
        }
    }
}

void XB_REGS3 XB_CDECL PathNode_DisableLink(uint8_t* node, uint32_t, uint8_t* link) {
    const uint8_t disabled = static_cast<uint8_t>(link[2] + 1);
    link[2] = disabled;
    if (disabled != 1) return;
    int16_t& count = *reinterpret_cast<int16_t*>(node + 0x24);
    count = static_cast<int16_t>(count - 1);
    const int32_t last = count;
    uint32_t* const words = reinterpret_cast<uint32_t*>(link);
    const uint32_t a = words[0];
    const uint32_t b = words[1];
    const uint32_t c = words[2];
    const uint32_t* const from = reinterpret_cast<const uint32_t*>(ptr(word_at(node, 0x64)) + last * 12);
    words[0] = from[0];
    words[1] = from[1];
    words[2] = from[2];
    uint32_t* const to =
        reinterpret_cast<uint32_t*>(ptr(word_at(node, 0x64)) + *reinterpret_cast<int16_t*>(node + 0x24) * 12);
    to[0] = a;
    to[1] = b;
    to[2] = c;
}

uint32_t XB_REGS2 XB_STDCALL PathNode_CanSee_2(const uint16_t* from, uint8_t* world, const uint16_t* to) {
    const uint32_t ha = *from;
    const uint32_t hb = *to;
    const uint32_t block_a = handle_block(ha);
    const uint32_t node_a = ha & 0x1ff;
    const uint32_t block_b = handle_block(hb);
    const uint32_t node_b = hb & 0x1ff;
    const uint8_t* bits;
    int32_t bit;
    if (block_a < block_b) {
        uint8_t* const a = path_block(world, block_a);
        bits = ptr(word_at(ptr(word_at(a, 0x28)), block_b * 8));
        bit = static_cast<int32_t>((node_a + word_at(a, 4)) * word_at(path_block(world, block_b), 4) + node_b);
    } else {
        uint8_t* const b = path_block(world, block_b);
        bits = ptr(word_at(ptr(word_at(b, 0x28)), block_a * 8));
        bit = static_cast<int32_t>((node_b + word_at(b, 4)) * word_at(path_block(world, block_a), 4) + node_a);
    }
    if (bits && (bits[bit >> 3] & (1u << (bit & 7)))) return 1;
    return 0;
}

void XB_REGS1 XB_STDCALL PathNodeBlock_ResetLinkState(uint8_t* world, uint32_t index) {
    if (!path_info(world)) return;
    uint8_t* const block = path_block(world, index);
    auto node = [&](uint32_t i) { return ptr(word_at(block, 0x10)) + i * 0x80; };
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(word_at(block, 4)); ++i) {
        half_at(node(i), 0x24) = half_at(node(i), 0x62);
        for (int32_t j = 0; j < *reinterpret_cast<const int16_t*>(node(i) + 0x62); ++j)
            for (uint32_t k = 4; k < 8; ++k) ptr(word_at(node(i), 0x64))[j * 12 + k] = 0;
    }
}

void XB_REGS3 XB_CDECL PathNode_ParseScriptKeys(uint8_t* world, uint32_t, uint32_t index) {
    using Stricmp = int32_t XB_CDECL(const char*, const char*);
    const auto stricmp = title_function<Stricmp>(0x001f1443);  // _stricmp
    auto is = [&](const char* key, uint32_t text) { return stricmp(key, reinterpret_cast<const char*>(ptr(text))) == 0; };
    auto import = [](uint32_t slot) { return word_at(ptr(title_global<uint32_t>(0x0095f49c)), slot); };
    uint8_t* const block = path_block(world, index);
    uint32_t offset = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(word_at(block, 0xc)); ++i, offset += 0xc2) {
        uint8_t* const pair = ptr(word_at(block, 0x20)) + offset;
        const char* const key = reinterpret_cast<const char*>(pair + 2);
        const char* const value = reinterpret_cast<const char*>(pair + 0x42);
        const uint32_t id = half_at(ptr(word_at(block, 0x10)) + (half_at(pair, 0) & 0x1ff) * 0x80, 0);
        uint32_t hash = 0;
        if (key) {
            for (const char* p = key; *p; ++p) {
                int8_t c = static_cast<int8_t>(*p);
                if (c >= 'A' && c <= 'Z') c = static_cast<int8_t>(c + 0x20);
                hash = static_cast<uint32_t>(static_cast<int32_t>(c)) ^ ((hash >> 27) | (hash << 5));
            }
        }
        const int32_t number = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(value);  // _atol
        const float real = title_function<float XB_CDECL(const char*)>(0x001ef056)(value);       // atof
        using SetInt = void XB_CDECL(uint32_t, uint32_t, int32_t);
        using SetFloat = void XB_CDECL(uint32_t, uint32_t, float);
        using SetString = void XB_CDECL(uint32_t, uint32_t, uint32_t);
        if (is(key, 0x00833a28)) title_function<SetFloat>(import(0x824))(id, hash, real);  // "script_delay"
        if (is(key, 0x00833a20)) continue;                                                 // "_color"
        if (is(key, 0x00833a14) || is(key, 0x00833a04)) {  // "script_mg42", "script_fallback"
            title_function<SetInt>(import(0x81c))(id, hash, number);
        } else if (is(key, 0x00833a28)) {  // "script_delay" (set a second time)
            title_function<SetFloat>(import(0x824))(id, hash, real);
        } else if (is(key, 0x008339f4) || is(key, 0x008339e4) || is(key, 0x008339d8)) {
            // "script_waittill", "script_chain", "script_door"
            const uint32_t fn = import(0x814);
            uint32_t text = 0;
            own_function<void XB_THISCALL(uint32_t*, const char*)>(&ScriptString_FromCString)(&text, value);  // ScriptString_FromCString
            title_function<SetString>(fn)(id, hash, text);
        }
    }
}

PLATFORM_ASM_ENTRY(PathNode_ListInCylinder_entry,
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl %edi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(PathNode_ListInCylinder) "addl $28, %esp\n"
                   "ret $20")

uint32_t XB_CDECL PathNode_ListInCylinder(const uint32_t* origin, uint8_t* world, float radius, float half_height,
                                          uint32_t list, uint32_t max, uint32_t mask) {
    uint8_t* const info = path_info(world);
    if (!info || !word_at(info, 4)) return 0;
    title_global<uint32_t>(kQueryOrigin) = origin[0];
    title_global<uint32_t>(kQueryOrigin + 4) = origin[1];
    title_global<uint32_t>(kQueryOrigin + 8) = origin[2];
    title_global<uint32_t>(kQueryMask) = mask;
    const float radius_sq = radius * radius;
    title_global<float>(kQueryRadius) = radius;
    title_global<float>(kQueryRadiusSq) = radius_sq;
    title_global<uint32_t>(kQueryMax) = max;
    title_global<float>(kQueryHeightSq) = half_height * half_height;
    title_global<uint32_t>(kQueryList) = list;
    title_global<uint32_t>(kQueryCount) = 0;
    for (int32_t b = 0; b < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++b) {
        uint8_t* const block = path_block(world, b);
        if (block && word_at(block, 0x18))
            PathNode_ListInCylinder_r(world, reinterpret_cast<const uint32_t*>(ptr(word_at(block, 0x18))));
    }
    return title_global<uint32_t>(kQueryCount);
}

uint8_t* XB_REGS2 XB_CDECL PathData_Construct(uint32_t, uint8_t* data) {
    word_at(data, 0) = 0x00838220;  // ResourceManager_vtable
    const int32_t managers = title_global<int32_t>(0x009719f8);
    if (managers < 0x14) {
        title_global<uint32_t>(0x009719fc + managers * 4) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        ++title_global<int32_t>(0x009719f8);
    }
    word_at(data, 0) = 0x00832780;
    word_at(data, 4) = 0;
    word_at(data, 0x194) = 0;
    title_global<uint8_t>(0x00986015) = 0;
    title_global<uint8_t>(0x00986016) = 0;
    title_global<uint8_t>(0x00986014) = 1;
    for (uint32_t i = 0; i < 0x63; ++i) word_at(data, 8 + i * 4) = 0;
    return data;
}

uint32_t XB_REGS3 XB_CDECL PathNode_IsUnclaimedFor(uint8_t* node, uint32_t, uint8_t* sentient) {
    if (word_at(node, 4) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sentient))) return 1;
    const uint32_t team = word_at(sentient, 4);
    const int32_t time = title_global<int32_t>(0x0098540c);  // level_time
    if (time <= static_cast<int32_t>(word_at(node, 8 + team * 4))) return 0;
    return time > static_cast<int32_t>(word_at(node, 8)) ? 1 : 0;
}

uint32_t XB_CDECL PathNode_IsSentientClaimed(const uint16_t* handle) {
    const uint8_t* sentient = Sentient_FirstOfTeams(4);
    if (!sentient) return 0;
    const uint16_t h = *handle;
    do {
        if (h == *reinterpret_cast<const uint16_t*>(sentient + 0x50)) return 1;
        sentient = Sentient_NextOfTeams(sentient, 4);
    } while (sentient);
    return 0;
}

namespace {

uint8_t* node_of(uint8_t* world, uint32_t handle) {
    uint8_t* const block = path_block(world, handle_block(handle));
    if (!block) return nullptr;
    return ptr(word_at(block, 0x10)) + (handle & 0x1ff) * 0x80;
}
int32_t depth_of(const uint8_t* node) { return *reinterpret_cast<const int16_t*>(node + 0x5e); }
const uint16_t* sorted_handles(uint8_t* world) {
    return reinterpret_cast<const uint16_t*>(ptr(word_at(path_info(world), 0x14)));
}

}  // namespace

uint8_t* XB_REGS1 XB_STDCALL PathNodeTree_ScanUnclaimedInDepthRange(int32_t start, uint8_t* world, int32_t min_depth,
                                                                    int32_t max_depth, uint8_t* ref) {
    uint8_t* best = nullptr;
    int32_t best_score = 0x7fffffff;
    uint8_t* const info = path_info(world);
    const int32_t count = static_cast<int32_t>(word_at(info, 8));
    if (start >= count) return nullptr;
    const uint16_t* handle = reinterpret_cast<const uint16_t*>(ptr(word_at(info, 0x14))) + start;
    for (int32_t i = start; i < count; ++i, ++handle) {
        uint8_t* const node = node_of(world, *handle);
        if (!node) continue;
        if (half_at(node, 0x5c) != half_at(ref, 0x5c)) break;
        if (title_global<int32_t>(0x0098540c) > static_cast<int32_t>(word_at(node, 8))) continue;  // level_time
        const int32_t depth = depth_of(node);
        int32_t score;
        if (depth < min_depth)
            score = min_depth - depth;
        else if (depth > max_depth)
            score = depth - max_depth;
        else
            score = 0;
        if (score > best_score) continue;
        if (PathNode_IsSentientClaimed(reinterpret_cast<const uint16_t*>(node))) continue;
        best_score = score;
        best = node;
        if (max_depth < depth) break;
    }
    return best;
}

uint8_t* XB_REGS1 XB_STDCALL PathNodeTree_ScanAvailableInDepthRange(int32_t start, uint8_t* world, int32_t min_depth,
                                                                    int32_t max_depth, uint8_t* ref,
                                                                    const uint8_t* sentient) {
    uint8_t* best = nullptr;
    int32_t best_score = 0x7fffffff;
    uint8_t* const info = path_info(world);
    const int32_t count = static_cast<int32_t>(word_at(info, 8));
    if (start >= count) return nullptr;
    const uint16_t* handle = reinterpret_cast<const uint16_t*>(ptr(word_at(info, 0x14))) + start;
    for (int32_t i = start; i < count; ++i, ++handle) {
        uint8_t* const node = node_of(world, *handle);
        if (!node) continue;
        if (half_at(node, 0x5c) != half_at(ref, 0x5c)) break;
        const int32_t depth = depth_of(node);
        int32_t score;
        if (depth < min_depth)
            score = min_depth - depth;
        else if (depth <= max_depth)
            continue;  // nodes inside the range are skipped
        else
            score = depth - max_depth;
        if (score > best_score) continue;
        if (!PathNode_IsAvailableFor(node, sentient)) continue;
        best_score = score;
        best = node;
        if (max_depth < depth) break;
    }
    return best;
}

PLATFORM_ASM_ENTRY(PathNodeTree_FindAvailableAncestor_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PathNodeTree_FindAvailableAncestor) "addl $20, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL PathNodeTree_FindAvailableAncestor(uint8_t* node, int32_t max_depth, int32_t min_depth,
                                                     uint8_t* world, const uint8_t* sentient) {
    if (!node) return nullptr;
    if (depth_of(node) > max_depth) {
        uint8_t* const blocks = title_global<uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
        do {
            const uint16_t parent = half_at(node, 0x60);
            if (parent == 0 || parent == 0xffff) return nullptr;
            node = node_of(blocks, parent);
        } while (depth_of(node) > max_depth);
    }
    if (depth_of(node) < min_depth) return nullptr;
    for (;;) {
        if (PathNode_IsAvailableFor(node, sentient)) return node;
        const uint16_t parent = half_at(node, 0x60);
        if (parent == 0 || parent == 0xffff) return nullptr;
        node = node_of(world, parent);
        if (depth_of(node) < min_depth) return nullptr;
    }
}

uint8_t* XB_REGS1 XB_STDCALL PathNodeTree_PickAvailableInSubtree(uint8_t* root, int32_t min_depth, int32_t max_depth,
                                                                 const uint8_t* sentient) {
    const uint16_t handle = half_at(root, 0);
    const uint16_t zone = half_at(root, 0x5c);
    uint8_t* best = nullptr;
    uint32_t picks = 0;
    for (uint8_t* node = root + 0x80; half_at(node, 0x5c) == zone; node += 0x80) {
        const int16_t depth = static_cast<int16_t>(depth_of(node));
        if (depth > max_depth) break;
        if (handle == half_at(node, 0x60)) return nullptr;
        if (depth < min_depth) continue;
        if (!PathNode_IsAvailableFor(node, sentient)) continue;
        if (best) {
            const int16_t best_depth = static_cast<int16_t>(depth_of(best));
            if (depth == best_depth) {
                ++picks;
                const uint32_t r = static_cast<uint32_t>(title_function<int32_t XB_CDECL()>(0x001efcc0)());  // _rand
                if (static_cast<int32_t>(r * picks) > 0x7fff) continue;
            } else if (depth < best_depth) {
                continue;
            } else {
                picks = 1;
            }
        } else {
            picks = 1;
        }
        best = node;
    }
    return best;
}

uint8_t* XB_THISCALL PathNodeTree_PickAvailableDescendant_r(uint8_t* world, int32_t min_depth, int32_t max_depth,
                                                            int32_t start, const uint8_t* sentient) {
    uint8_t* const info = path_info(world);
    const uint32_t root_handle = reinterpret_cast<const uint16_t*>(ptr(word_at(info, 0x14)))[start];
    uint8_t* const root = node_of(world, root_handle);
    if (!root) return nullptr;
    const uint16_t zone = half_at(root, 0x5c);
    const int32_t child_depth = depth_of(root) + 1;
    const int32_t limit = max_depth >= child_depth ? child_depth : max_depth;
    const int32_t count = static_cast<int32_t>(word_at(info, 8));
    uint32_t picks = 0;
    uint32_t best = 0;
    for (int32_t i = start + 1; i < count; ++i) {
        uint8_t* const node = node_of(world, sorted_handles(world)[i]);
        if (!node) continue;
        if (half_at(node, 0x5c) != zone) break;
        if (depth_of(node) > limit) break;
        if (static_cast<uint16_t>(root_handle) != half_at(node, 0x60)) continue;
        if (depth_of(node) < max_depth) {
            uint8_t* const picked = PathNodeTree_PickAvailableDescendant_r(world, min_depth, max_depth, i, sentient);
            if (picked) PathNodeTree_ReservoirPickDeepest(&picks, picked, &best);
        }
        const int32_t depth = depth_of(node);
        if (depth != max_depth && (best || depth < min_depth)) continue;
        if (PathNode_IsAvailableFor(node, sentient)) PathNodeTree_ReservoirPickDeepest(&picks, node, &best);
    }
    return ptr(best);
}

void XB_REGS3 XB_STDCALL Path_RecordDisconnectedLink(uint8_t* world, uint32_t, uint8_t* link, uint8_t* ent, uint8_t* node) {
    // Disconnected-link records: 8 bytes {+0 node handle, +2 link target, +4 prev, +6 next} in the pool at info
    // +0x10; record 0 heads the free list, the entity's ring starts at +0x410.
    auto pool = [&] { return ptr(word_at(path_info(world), 0x10)); };
    const uint16_t index = half_at(pool(), 6);
    if (!index) title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, reinterpret_cast<const char*>(ptr(0x008339ac)));  // __break
    uint8_t* const first = pool();
    uint8_t* const record = first + index * 8;
    half_at(first, 6) = half_at(record, 6);
    half_at(pool() + half_at(record, 6) * 8, 4) = 0;
    half_at(record, 0) = half_at(node, 0);
    half_at(record, 2) = half_at(link, 0);
    const uint16_t head = half_at(ent, 0x410);
    if (!head) {
        half_at(ent, 0x410) = index;
        half_at(record, 6) = index;
        half_at(record, 4) = index;
    } else {
        half_at(record, 4) = head;
        const uint16_t next = half_at(pool() + half_at(ent, 0x410) * 8, 6);
        half_at(record, 6) = next;
        half_at(pool() + next * 8, 4) = index;
        half_at(pool() + half_at(record, 4) * 8, 6) = index;
    }
    PathNode_DisableLink(node, 0, link);
}

void XB_THISCALL Path_ResetLinkList2048(uint8_t* world) {
    if (!path_info(world)) return;
    for (uint32_t i = 0; i < 0x800; ++i) {
        auto record = [&] { return ptr(word_at(path_info(world), 0x10)) + i * 8; };
        half_at(record(), 2) = 0;
        half_at(record(), 0) = 0;
        half_at(record(), 4) = static_cast<uint16_t>(i == 0 ? 0x7ff : i - 1);
        half_at(record(), 6) = static_cast<uint16_t>(i == 0x7ff ? 0 : i + 1);
    }
}

PLATFORM_ASM_ENTRY(PathNode_ParseAllScriptKeys_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(PathNode_ParseAllScriptKeys) "addl $4, %esp\n"
                   "ret")

void XB_CDECL PathNode_ParseAllScriptKeys(uint8_t* world) {
    if (!path_info(world)) return;
    for (int32_t b = 0; b < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++b)
        if (path_block(world, b)) PathNode_ParseScriptKeys(world, 0, b);
}

int32_t XB_REGS1 XB_STDCALL PathNode_FindByKey(const ScriptString* key, uint8_t* world, const ScriptString* value,
                                               uint32_t* out) {
    int32_t found = 0;
    const uint32_t* const key_rep = reinterpret_cast<const uint32_t*>(key);
    if (!*reinterpret_cast<const uint32_t*>(value) || !*key_rep) {
        if (out) {
            for (int32_t b = 0; b < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++b) {
                for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(word_at(path_block(world, b), 4)); ++i)
                    out[found++] = half_at(ptr(word_at(path_block(world, b), 0x10)), i * 0x80);
            }
            return found;
        }
    }
    // The node field the key names (text at the script string's rep + 6).
    static const uint32_t kKeys[9][2] = {
        {0x0082dda8, 0x30},  // "targetname"
        {0x00833ad0, 0x38},  // "target"
        {0x00833ac4, 0x3c},  // "animscript"
        {0x00833ab0, 0x34},  // "script_noteworthy"
        {0x00833aa8, 0x44},  // "origin"
        {0x00833aa0, 0x50},  // "angles"
        {0x00833a98, 0x54},  // "radius"
        {0x00833a8c, 0x2c},  // "spawnflags"
        {0x00833a84, 0x28},  // "type"
    };
    const auto stricmp = title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443);  // _stricmp
    uint32_t field = 0;
    bool known = false;
    for (uint32_t k = 0; k < 9; ++k) {
        const char* const text = *key_rep ? reinterpret_cast<const char*>(ptr(*key_rep + 6)) : nullptr;
        if (stricmp(reinterpret_cast<const char*>(ptr(kKeys[k][0])), text) == 0) {
            field = kKeys[k][1];
            known = true;
            break;
        }
    }
    if (!known) return -1;
    for (int32_t b = 0; b < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++b) {
        uint8_t* const block = path_block(world, b);
        if (!block) continue;
        for (uint8_t* node = ptr(word_at(block, 0x10));
             node < ptr(word_at(block, 0x10)) + word_at(block, 4) * 0x80; node += 0x80) {
            if (!word_at(node, field)) continue;
            if (!ScriptString_EqualsString(value, reinterpret_cast<const ScriptString*>(node + field))) continue;
            if (!out) return half_at(node, 0);
            out[found++] = half_at(node, 0);
        }
    }
    if (out) return found;
    return -1;
}

void XB_REGS2 XB_CDECL Actor_SetStanceLimitsFromNode(const uint8_t* actor, uint8_t* limits) {
    const uint16_t h = *reinterpret_cast<const uint16_t*>(actor + 0x18);
    if (h == 0 || h == 0xffff) return;
    uint8_t* const node = node_of(title_global<uint8_t*>(0x0095f4f4), h);  // g_pathNodeBlocks
    if (!node) return;
    uint8_t& flags = limits[0x46];
    if (node[0x2c] & 0x10)
        flags |= 1;
    else
        flags &= 0xfe;
    if (node[0x2c] & 0x8)
        flags |= 2;
    else
        flags &= 0xfd;
    if (node[0x2c] & 0x4)
        flags |= 4;
    else
        flags &= 0xfb;
}

namespace {

// Clears a node's claim time (+8) and the claimer (+4) when it is of the team, for a node not disabled (+0x26)
// whose claim is not permanent.
void revoke_linked_claim(uint8_t* node, uint32_t team) {
    if (half_at(node, 0x26) || static_cast<int32_t>(word_at(node, 8)) >= 0x7fffffff) return;
    uint8_t* const claimer = ptr(word_at(node, 4));
    if (claimer && word_at(claimer, 4) == team) word_at(node, 4) = 0;
    word_at(node, 8) = 0;
}

}  // namespace

PLATFORM_ASM_ENTRY(PathNode_RevokeClaim_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(PathNode_RevokeClaim) "addl $8, %esp\n"
                   "ret")

void XB_CDECL PathNode_RevokeClaim(uint8_t* node, uint32_t team) {
    uint8_t* const claimer = ptr(word_at(node, 4));
    if (claimer && word_at(claimer, 4) == team) word_at(node, 4) = 0;
    word_at(node, 8 + team * 4) = title_global<uint32_t>(0x0098540c) + 5000;  // level_time
    if (half_at(node, 0x26)) return;
    word_at(node, 8) = 0;
    const uint16_t first = half_at(node, 0x58);
    if (first == 0 || first == 0xffff) return;
    uint8_t* const blocks = title_global<uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
    revoke_linked_claim(node_of(blocks, first), team);
    const uint16_t second = half_at(node, 0x5a);
    if (second == 0 || second == 0xffff) return;
    revoke_linked_claim(PathNode_FromHandleInTable(reinterpret_cast<const uint16_t*>(node + 0x5a), blocks), team);
}

void XB_THISCALL G_FriendlyChainNode(uint8_t* ent, uint8_t* world, const ScriptString* name) {
    if (!title_global<uint32_t>(0x009550f4)) return;
    uint16_t* const current = reinterpret_cast<uint16_t*>(ent + 0x4a);
    if ((PathNode_HandleIsValid(current) & 0xff) &&
        ScriptString_EqualsString(reinterpret_cast<const ScriptString*>(PathNode_FromHandle(current) + 0x30), name))
        return;
    if (word_at(path_info(world), 8)) {
        int32_t i = 0;
        do {
            uint8_t* const node = node_of(world, sorted_handles(world)[i]);
            if (node && ScriptString_EqualsString(reinterpret_cast<const ScriptString*>(node + 0x30), name)) {
                *current = half_at(node, 0);
                return;
            }
            ++i;
        } while (i != static_cast<int32_t>(word_at(path_info(world), 8)));
    }
    auto text = [&] { return *reinterpret_cast<const uint32_t*>(name) ? reinterpret_cast<const char*>(ptr(*reinterpret_cast<const uint32_t*>(name) + 6)) : nullptr; };
    using Error = void XB_CDECL(const char*, const char*);
    for (int32_t b = 0; b < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++b) {
        uint8_t* const block = path_block(world, b);
        if (!block) continue;
        int32_t left = static_cast<int32_t>(word_at(block, 4));
        if (left <= 0) continue;
        uint32_t offset = 0;
        do {
            uint8_t* const nodes = ptr(word_at(path_block(world, b), 0x10));
            if (ScriptString_EqualsString(reinterpret_cast<const ScriptString*>(nodes + offset + 0x30), name))
                own_function<Error>(&G_Error_entry)(reinterpret_cast<const char*>(ptr(0x00833980)), text());  // G_Error
            offset += 0x80;
        } while (--left);
    }
    own_function<Error>(&G_Error_entry)(reinterpret_cast<const char*>(ptr(0x00833954)), text());  // G_Error
}

namespace {

// Script-string fields stored as offsets in a loaded block: targetname, target, script_noteworthy, animscript.
constexpr uint32_t kStringFields[] = {0x30, 0x38, 0x34, 0x3c};

// The sorted index of the node's handle, or -1 (a linear scan of the info record's sorted handles).
int32_t sorted_index_of(uint8_t* world, uint8_t* node) {
    uint8_t* const info = path_info(world);
    const int32_t count = static_cast<int32_t>(word_at(info, 8));
    const uint16_t* const handles = reinterpret_cast<const uint16_t*>(ptr(word_at(info, 0x14)));
    const uint16_t handle = half_at(node, 0);
    for (int32_t i = 0; i < count; ++i)
        if (handle == handles[i]) return i;
    return -1;
}

}  // namespace

uint8_t* XB_STDCALL PathNodeTree_FindNodeInDepthRange(uint8_t* world, uint8_t* ref, int32_t min_offset,
                                                      int32_t max_offset, uint8_t* node, const uint8_t* sentient,
                                                      uint32_t allow_ancestor) {
    const uint16_t zone = half_at(ref, 0x5c);
    const int32_t min_depth = depth_of(ref) + min_offset;
    const int32_t max_depth = depth_of(ref) + max_offset;
    const bool available = node && PathNode_IsAvailableFor(node, sentient);
    if (node && half_at(node, 0x5c) == zone) {
        if (available && depth_of(node) == max_depth) return node;
        if (depth_of(node) < max_depth) {
            const int32_t index = sorted_index_of(world, node);
            if (uint8_t* found = PathNodeTree_PickAvailableDescendant_r(world, min_depth, max_depth, index, sentient))
                return found;
            if (uint8_t* found = PathNodeTree_PickAvailableInSubtree(node, min_depth, max_depth, sentient)) return found;
            if (available && depth_of(node) >= min_depth) return node;
        }
        if (!allow_ancestor) return node;
        if (depth_of(node) >= min_depth) {
            if (uint8_t* found = PathNodeTree_FindAvailableAncestor(node, max_depth, min_depth, world, sentient))
                return found;
        }
    }
    int32_t index = sorted_index_of(world, ref);
    if (max_offset > 0) {
        if (uint8_t* found = PathNodeTree_PickAvailableDescendant_r(world, min_depth, max_depth, index, sentient))
            return found;
    }
    if (min_offset <= 0) {
        if (uint8_t* found = PathNodeTree_FindAvailableAncestor(ref, max_depth, min_depth, world, sentient))
            return found;
    }
    // Climb to the tree's root (through g_pathNodeBlocks).
    uint8_t* const blocks = title_global<uint8_t*>(0x0095f4f4);
    for (;;) {
        const uint16_t parent = half_at(ref, 0x60);
        if (parent == 0 || parent == 0xffff) break;
        ref = node_of(blocks, parent);
        if (!ref) return nullptr;
    }
    const uint16_t* const handles = sorted_handles(world);
    if (half_at(ref, 0) != handles[index]) index = PathNode_FindSortedIndex(world, reinterpret_cast<const uint16_t*>(ref));
    if (depth_of(ref) < max_depth) {
        if (uint8_t* found = PathNodeTree_PickAvailableDescendant_r(world, min_depth, max_depth, index, sentient))
            return found;
    }
    if (available && half_at(node, 0x5c) == zone) return node;
    if (uint8_t* found = PathNodeTree_ScanAvailableInDepthRange(index, world, min_depth, max_depth, ref, sentient))
        return found;
    if (PathNode_HandleIsValid(reinterpret_cast<const uint16_t*>(sentient + 0x50)) & 0xff) return nullptr;
    return PathNodeTree_ScanUnclaimedInDepthRange(index, world, min_depth, max_depth, ref);
}

void XB_REGS1 XB_STDCALL PathNode_EnableLink(uint8_t* node, const uint16_t* target) {
    int32_t i = static_cast<int16_t>(half_at(node, 0x24));
    if (i >= static_cast<int16_t>(half_at(node, 0x62))) return;
    const uint16_t handle = *target;
    uint8_t* link = ptr(word_at(node, 0x64)) + i * 12;
    while (handle != half_at(link, 0)) {
        ++i;
        link += 12;
        if (i >= static_cast<int16_t>(half_at(node, 0x62))) return;
    }
    if (--link[2]) return;
    // The last disable is gone: swap the link with the first disabled one and count it enabled.
    const uint32_t a = word_at(link, 0), b = word_at(link, 4), c = word_at(link, 8);
    uint8_t* const first = ptr(word_at(node, 0x64)) + static_cast<int16_t>(half_at(node, 0x24)) * 12;
    word_at(link, 0) = word_at(first, 0);
    word_at(link, 4) = word_at(first, 4);
    word_at(link, 8) = word_at(first, 8);
    uint8_t* const slot = ptr(word_at(node, 0x64)) + static_cast<int16_t>(half_at(node, 0x24)) * 12;
    word_at(slot, 0) = a;
    word_at(slot, 4) = b;
    word_at(slot, 8) = c;
    ++half_at(node, 0x24);
}

void XB_REGS3 XB_CDECL PathNode_ResolveAnimScripts(uint8_t* world, uint32_t, uint32_t index) {
    char text[0x80];
    uint8_t* const block = path_block(world, index);
    if (static_cast<int32_t>(word_at(block, 4)) <= 0) return;
    uint8_t* node = ptr(word_at(block, 0x10));
    int32_t i = 0;
    do {
        const uint32_t name = word_at(node, 0x3c);  // animscript
        if (name) {
            const char* const string = reinterpret_cast<const char*>(ptr(name + 6));
            const uint32_t hash = Com_HashKeyNoCase(0, string);
            uint32_t function = 0;
            int32_t f = 0;
            for (; f < 0x34; ++f)
                if (hash == title_global<uint32_t>(0x00927170 + f * 8)) break;  // g_broFuncs {hash, function}
            if (f < 0x34)
                function = title_global<uint32_t>(0x00927174 + f * 8);
            else
                title_function<int32_t XB_CDECL(char*, const char*, ...)>(0x001ee901)(  // _sprintf
                    text, reinterpret_cast<const char*>(ptr(0x00832320)), string);   // "BroFunc: %s, NOT FOUND"
            word_at(node, 0x40) = function;
        }
        ++i;
        node += 0x80;
    } while (i < static_cast<int32_t>(word_at(block, 4)));
}

uint8_t XB_STDCALL Path_RelocateNodeBlock(uint8_t* world, uint32_t index) {
    uint8_t* const block = path_block(world, index);
    if (word_at(block, 0) != 2) return 0;
    // Links (+0x64) from indices to pointers into the block's link array (+0x14, 12 bytes each).
    for (int32_t i = 0; i < static_cast<int32_t>(word_at(block, 4)); ++i) {
        uint8_t* const links = ptr(word_at(block, 0x10)) + i * 0x80 + 0x64;
        word_at(links, 0) = word_at(block, 0x14) + word_at(links, 0) * 12;
    }
    // The node list (+0x1c) from indices to node pointers.
    for (int32_t i = 0; i < static_cast<int32_t>(word_at(block, 4)); ++i) {
        uint32_t* const entry = reinterpret_cast<uint32_t*>(ptr(word_at(block, 0x1c))) + i;
        *entry = (*entry << 7) + word_at(block, 0x10);
    }
    if (word_at(block, 4) == 0) {
        word_at(block, 0x18) = 0;
        word_at(block, 0x1c) = 0;
    } else {
        // The node tree (+0x18, 16-byte cells) linked over the node list (PathNodeTree_Link_r from its root).
        uint32_t* const root = reinterpret_cast<uint32_t*>(ptr(word_at(block, 0x18)));
        const uint32_t base = word_at(block, 0x1c);
        uint32_t** const cursor = reinterpret_cast<uint32_t**>(block + 0x18);
        *cursor = root + 4;
        const uint32_t world_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(world));
        if (static_cast<int32_t>(root[0]) >= 0) {
            root[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PathNodeTree_Link_r(world_word, base, cursor)));
            root[3] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PathNodeTree_Link_r(world_word, base, cursor)));
        } else {
            root[2] = base + root[2] * 4;
        }
        *cursor = root;
    }
    // Script strings from offsets into the block's string data (+0x24): targetname, target, script_noteworthy,
    // animscript.
    using FromCString = void XB_THISCALL(uint32_t*, const char*);
    for (int32_t i = 0; i < static_cast<int32_t>(word_at(block, 4)); ++i) {
        uint8_t* const node = ptr(word_at(block, 0x10)) + i * 0x80;
        for (const uint32_t field : kStringFields) {
            const uint32_t offset = word_at(node, field);
            if (offset)
                own_function<FromCString>(&ScriptString_FromCString)(  // ScriptString_FromCString
                    &word_at(node, field), reinterpret_cast<const char*>(ptr(word_at(block, 0x24) + offset - 1)));
        }
    }
    if (title_global<uint8_t>(0x00986015)) {
        PathNode_ParseScriptKeys(world, 0, index);
        PathNode_ResolveAnimScripts(world, 0, index);
    }
    PathNodeBlock_ResetLinkState(world, index);
    return 1;
}

PLATFORM_ASM_ENTRY(Path_InitAfterLoad_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Path_InitAfterLoad) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Path_InitAfterLoad(uint8_t* world) {
    Path_ResetLinkList2048(world);
    title_global<uint8_t>(0x00986015) = 0;
    title_global<uint8_t>(0x00986014) = 1;
    title_global<uint8_t>(0x00986016) = 0;
    uint8_t* const cvar = Cvar_Get("g_connectpaths", reinterpret_cast<const char*>(ptr(0x0082e098)), 0);  // "0"
    // UCOMISS + LAHF/TEST 0x44/JP: on only for an exact 0 (unordered and unequal both leave).
    if (!(float_at(cvar, 0x1c) == title_global<float>(0x0083eafc))) return 0;  // 0.0
    uint8_t* const info = path_info(world);
    if (!info || word_at(info, 0) != 9) return 0;
    for (int32_t i = 0; i < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++i) {
        uint8_t* const record = path_info(world);
        uint32_t* const names = reinterpret_cast<uint32_t*>(ptr(word_at(record, 0x18)));
        names[i] += word_at(record, 0x1c);
    }
    title_global<uint8_t>(0x00986014) = 0;
    return 1;
}

int32_t XB_STDCALL Path_FindZoneBlockIndex(uint8_t* world, const char* name) {
    const uint32_t zone = Pak_FindZoneByName(name, title_global<uint8_t*>(0x0095f640));
    if (!zone) return -1;
    uint8_t* const info = path_info(world);
    const int32_t count = static_cast<int32_t>(word_at(info, 0xc));
    const uint32_t* const names = reinterpret_cast<const uint32_t*>(ptr(word_at(info, 0x18)));
    const char* const zone_name = reinterpret_cast<const char*>(ptr(word_at(ptr(zone), 0x20)));
    for (int32_t i = 0; i < count; ++i)
        if (!std::strcmp(reinterpret_cast<const char*>(ptr(names[i])), zone_name)) return i;
    return -1;
}

PLATFORM_ASM_ENTRY(Path_ConnectPathsForEntity_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Path_ConnectPathsForEntity) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Path_ConnectPathsForEntity(uint8_t* ent, uint8_t* world) {
    const uint16_t first = half_at(ent, 0x410);
    word_at(ent, 0x2a4) |= 0x4000;
    if (!first) return;
    half_at(ent, 0x410) = 0;
    // Disconnected-link records (info +0x10, 8 bytes: node handle, link target, prev, next): the entity's ring is
    // opened and spliced into the free ring of record 0.
    auto record = [&](uint32_t i) { return ptr(word_at(path_info(world), 0x10)) + i * 8; };
    half_at(record(half_at(record(first), 4)), 6) = 0;
    half_at(record(half_at(record(0), 4)), 6) = first;
    const uint16_t free_prev = half_at(record(0), 4);
    half_at(record(0), 4) = half_at(record(first), 4);
    half_at(record(first), 4) = free_prev;
    uint32_t i = first;
    do {
        uint8_t* const entry = record(i);
        const uint16_t handle = half_at(entry, 0);
        if (path_block(world, handle_block(handle))) {
            uint8_t* const node = handle == 0 || handle == 0xffff ? nullptr : node_of(world, handle);
            PathNode_EnableLink(node, reinterpret_cast<const uint16_t*>(entry + 2));
        }
        i = half_at(record(i), 6);
    } while (i);
}

PLATFORM_ASM_ENTRY(PathNode_ResolveAllAnimScripts_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(PathNode_ResolveAllAnimScripts) "addl $4, %esp\n"
                   "ret")

void XB_CDECL PathNode_ResolveAllAnimScripts(uint8_t* world) {
    if (!path_info(world)) return;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(word_at(path_info(world), 0xc)); ++i)
        if (path_block(world, i)) PathNode_ResolveAnimScripts(world, 0, i);
}

namespace {

// A node's claimer (+4) other than `claimer`, holding a permanent claim (+8 = 0x7fffffff): its actor drops the node.
void drop_other_claim(uint8_t* node, uint8_t* claimer) {
    uint8_t* const holder = ptr(word_at(node, 4));
    if (!holder || holder == claimer || word_at(node, 8) != 0x7fffffff) return;
    Actor_DropClaimedNode(ptr(word_at(ptr(word_at(holder, 0)), 0x240)));
}

}  // namespace

PLATFORM_ASM_ENTRY(PathNode_SetClaim_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PathNode_SetClaim) "addl $8, %esp\n"
                   "ret")

void XB_CDECL PathNode_SetClaim(uint8_t* claimer, uint8_t* node) {
    drop_other_claim(node, claimer);
    // The two linked nodes (+0x58, +0x5a): the second only when the first exists.
    const uint16_t first = half_at(node, 0x58);
    if (first != 0 && first != 0xffff) {
        uint8_t* const linked = node_of(title_global<uint8_t*>(0x0095f4f4), first);  // g_pathNodeBlocks
        if (linked) {
            drop_other_claim(linked, claimer);
            const uint16_t second = half_at(node, 0x5a);
            if (second != 0 && second != 0xffff) {
                uint8_t* const other = node_of(title_global<uint8_t*>(0x0095f4f4), second);
                if (other) drop_other_claim(other, claimer);
            }
        }
    }
    word_at(node, 4) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(claimer));
    word_at(node, 8) = 0x7fffffff;
    // Linked nodes count the claim (+0x26) and lose their own claimer (a missing block writes at address 0, as the
    // original does).
    const uint16_t a = half_at(node, 0x58);
    if (a == 0 || a == 0xffff) return;
    uint8_t* const blocks = title_global<uint8_t*>(0x0095f4f4);
    uint8_t* const na = node_of(blocks, a);
    ++half_at(na, 0x26);
    word_at(na, 4) = 0;
    word_at(na, 8) = 0x7fffffff;
    const uint16_t b = half_at(node, 0x5a);
    if (b == 0 || b == 0xffff) return;
    uint8_t* const nb = node_of(blocks, b);
    ++half_at(nb, 0x26);
    if (!nb) {
        word_at(nb, 8) = 0x7fffffff;
        word_at(nb, 4) = 0;
        return;
    }
    word_at(nb, 4) = 0;
    word_at(nb, 8) = 0x7fffffff;
}

// The original's frame (ESP aligned to 16, 0x470 bytes) is rebuilt at its position: PathNode_ListInCylinder keeps
// the list's address in the query (0x00929b40), so the list and the trace vectors sit at the original offsets.
PLATFORM_ASM_ENTRY(Path_DisconnectPathsForEntity_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Path_DisconnectPathsForEntity) "addl $12, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $8")

void XB_CDECL Path_DisconnectPathsForEntity(uint32_t entry, uint8_t* world, uint8_t* ent) {
    uint8_t* const frame = ptr(((entry - 4) & ~0xfu) - 0x470);
    if (!word_at(world, 0x194)) return;
    Path_ConnectPathsForEntity(ent, world);
    word_at(ent, 0x2a4) &= ~0x4000u;
    word_at(ent, 0x414) = title_global<uint32_t>(0x0098540c);  // level_time
    // The nodes within 306 units (256 high) of the bounds' centre (four lanes: the fourth is kept for its flags).
    float* const centre = reinterpret_cast<float*>(frame + 0x10);
    const float* const mins = reinterpret_cast<const float*>(ent + 0x100);
    const float* const maxs = reinterpret_cast<const float*>(ent + 0x110);
    for (uint32_t i = 0; i < 4; ++i) centre[i] = (mins[i] + maxs[i]) * title_global<float>(0x0083eb24);  // 0.5f
    platform_keep(centre[3]);
    uint32_t* const list = reinterpret_cast<uint32_t*>(frame + 0x70);
    const int32_t count = static_cast<int32_t>(PathNode_ListInCylinder(
        reinterpret_cast<const uint32_t*>(centre), title_global<uint8_t*>(0x0095f4f4), 306.0f, 256.0f,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list)), 0x80, 0xffffffffu));
    float* const start = reinterpret_cast<float*>(frame + 0x50);
    float* const end = reinterpret_cast<float*>(frame + 0x60);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const node = ptr(list[i * 2]);
        // Each link whose target node is seen through the entity (a blocked sight trace) is recorded and cut.
        for (int32_t j = *reinterpret_cast<const int16_t*>(node + 0x62) - 1; j >= 0; --j) {
            uint8_t* const link = ptr(word_at(node, 0x64)) + j * 12;
            const uint16_t target = half_at(link, 0);
            if (!target || target == 0xffff) continue;
            uint8_t* const block = path_block(world, handle_block(target));
            if (!block) continue;
            uint8_t* const other = ptr(word_at(block, 0x10)) + (target & 0x1ff) * 0x80;
            if (!other) continue;
            // Both points as four lanes, the first repeated.
            end[0] = float_at(other, 0x44);
            end[1] = float_at(other, 0x48);
            end[2] = float_at(other, 0x4c);
            end[3] = float_at(other, 0x44);
            start[0] = float_at(node, 0x44);
            start[1] = float_at(node, 0x48);
            start[2] = float_at(node, 0x4c);
            start[3] = float_at(node, 0x44);
            if (SV_EntitySightTrace(end, &title_global<float>(0x0095fe60), start, &title_global<float>(0x00960060),
                                    word_at(ent, 0x224), 0x2820011, 1))
                Path_RecordDisconnectedLink(world, 0, link, ent, node);
        }
    }
}

PLATFORM_ASM_ENTRY(Path_DisconnectAllEntities_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Path_DisconnectAllEntities) "addl $4, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Path_DisconnectAllEntities(uint32_t entry) {
    // Every entity with path-blocking flags (+0x2a4 bits 0x1000 and 0x2000); each call where the original's
    // puts it (below its two saved registers), for the rebuilt frame above.
    uint32_t* const first = &title_global<uint32_t>(0x0097d370);
    const uint32_t* const last = first + title_global<uint32_t>(0x0097d36c);
    for (uint32_t* it = first; it != last; ++it) {
        uint8_t* const ent = ptr(*it);
        if (!ent) continue;
        const uint32_t flags = word_at(ent, 0x2a4);
        if (!(flags & 0x1000) || !(flags & 0x2000)) continue;
        const uint32_t world = title_global<uint32_t>(0x0095f4f4);
        const TitleRegs regs{world, 0, 0, 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(it)),
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(last))};
        const uint32_t args[2] = {world, *it};
        title_call_at_regs(entry - 8, own_address(&Path_DisconnectPathsForEntity_entry), &regs, 2, args);  // Path_DisconnectPathsForEntity
    }
}

// The original's frame (ESP aligned to 16, 0x890 bytes) is rebuilt at its position and its callees run at the
// original's ESP (G_SightTrace's subtree copies stack words into the trace work).
PLATFORM_ASM_ENTRY(PathNode_FindNearest_entry,
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
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %esi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(PathNode_FindNearest) "addl $40, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

namespace {

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// G_SightTrace (0x002e9840; end in EAX, start in EDI, maxs in EBX) from the origin to a node with the actor box,
// contents 0x820011, capsule, at the original's ESP; both points as four lanes (the first repeated) at its
// offsets. True when nothing blocks it.
bool node_visible(uint8_t* frame, const float* origin, const uint8_t* node, uint32_t end_offset,
                  uint32_t start_offset) {
    float* const end = reinterpret_cast<float*>(frame + end_offset);
    float* const start = reinterpret_cast<float*>(frame + start_offset);
    int32_t* const result = reinterpret_cast<int32_t*>(frame + 0x18);
    end[0] = *reinterpret_cast<const float*>(node + 0x44);
    end[1] = *reinterpret_cast<const float*>(node + 0x48);
    end[2] = *reinterpret_cast<const float*>(node + 0x4c);
    end[3] = *reinterpret_cast<const float*>(node + 0x44);
    start[0] = origin[0];
    start[1] = origin[1];
    start[2] = origin[2];
    start[3] = origin[0];
    *result = 0;
    const TitleRegs regs{address_of(end), address_of(frame + 0x50), 0, 0, address_of(origin), address_of(start)};
    const uint32_t args[6] = {address_of(result), address_of(frame + 0x60), 0, 0, 0x820011, 1};
    title_call_at_regs(address_of(frame), own_address(&G_SightTrace_entry), &regs, 6, args);  // G_SightTrace
    return !*result;
}

}  // namespace

uint8_t* XB_CDECL PathNode_FindNearest(uint32_t entry, const float* origin, uint32_t type_mask, uint32_t max_nodes,
                                       uint32_t* list, float radius, const float* planes, const float* plane_dists,
                                       int32_t plane_count, int32_t* count_out) {
    uint8_t* const frame = ptr(((entry - 4) & ~0xfu) - 0x890);
    uint32_t radius_bits;
    std::memcpy(&radius_bits, &radius, 4);
    TitleRegs regs{type_mask, address_of(list), address_of(origin), radius_bits, address_of(origin),
                   title_global<uint32_t>(0x0095f4f4)};
    const uint32_t list_args[5] = {radius_bits, 0x42800000, address_of(list), max_nodes, type_mask};  // 64.0f high
    const int32_t count = static_cast<int32_t>(
        title_call_at_regs(address_of(frame), own_address(&PathNode_ListInCylinder_entry), &regs, 5, list_args));  // PathNode_ListInCylinder
    word_at(frame, 0x4c) = static_cast<uint32_t>(count);
    regs.eax = static_cast<uint32_t>(count);
    regs.edi = static_cast<uint32_t>(count);
    const uint32_t sort_args[4] = {address_of(list), static_cast<uint32_t>(count), 8, 0x002aa110};
    title_call_at_regs(address_of(frame), 0x001ef880, &regs, 4, sort_args);  // qsort by distance
    // The actor box (g_actorMins/Maxs; mins z raised by 0x0083eed0); the fourth lanes are never set (zero in tests).
    const float* const actor_mins = &title_global<float>(0x0098a560);
    const float* const actor_maxs = &title_global<float>(0x0098a550);
    float* const mins = reinterpret_cast<float*>(frame + 0x60);
    float* const maxs = reinterpret_cast<float*>(frame + 0x50);
    mins[0] = actor_mins[0];
    maxs[0] = actor_maxs[0];
    mins[1] = actor_mins[1];
    mins[2] = actor_mins[2] + title_global<float>(0x0083eed0);
    maxs[1] = actor_maxs[1];
    maxs[2] = actor_maxs[2];
    *count_out = count;
    uint8_t** const outside = reinterpret_cast<uint8_t**>(frame + 0x90);
    int32_t outside_count = 0;
    const float one = title_global<float>(0x0083eb20);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const node = ptr(list[i * 2]);
        const float* const pos = reinterpret_cast<const float*>(node + 0x44);
        // A node whose actor box (1 unit larger) holds the origin is taken at once.
        if (origin[0] > (actor_mins[0] + pos[0]) - one && (actor_maxs[0] + pos[0]) + one > origin[0] &&
            origin[1] > (pos[1] + actor_mins[1]) - one && (pos[1] + actor_maxs[1]) + one > origin[1] &&
            origin[2] > (pos[2] + actor_mins[2]) - one && (pos[2] + actor_maxs[2]) + one > origin[2])
            return ptr(list[i * 2]);
        // Nodes outside one of the planes (x, y . node > dist) are kept for the second pass.
        bool out = false;
        for (int32_t k = 0; k < plane_count; ++k) {
            const float d = planes[k * 2] * pos[0] + planes[k * 2 + 1] * pos[1];
            if (d > plane_dists[k]) {
                out = true;
                break;
            }
        }
        if (out) {
            outside[outside_count++] = node;
            continue;
        }
        if (node_visible(frame, origin, node, 0x70, 0x80)) return ptr(list[i * 2]);
    }
    for (int32_t j = 0; j < outside_count; ++j) {
        if (node_visible(frame, origin, outside[j], 0x80, 0x70)) return outside[j];
    }
    return nullptr;
}

// count out in EAX, list in EDX, origin in ESI; cdecl max nodes, type mask, radius. PathNode_FindNearest runs at the
// original's ESP (entry - 0x18; its frame is rebuilt there), so the C frame sits below a gap.
PLATFORM_ASM_ENTRY(PathNode_FindNearestNoPlanes_entry,
                   "pushl %edx\n"
                   "leal 4(%esp), %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %esi\n"
                   "pushl -4(%ebp)\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(PathNode_FindNearestNoPlanes) "addl $28, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "leal 4(%esp), %esp\n"
                   "ret")

uint32_t XB_CDECL PathNode_FindNearestNoPlanes(uint32_t entry, int32_t* count_out, uint32_t* list, const float* origin,
                                               uint32_t max_nodes, uint32_t type_mask, uint32_t radius) {
    // PathNode_FindNearest without boundary planes.
    const TitleRegs regs{type_mask, 0, max_nodes, address_of(list), address_of(origin), 0};
    const uint32_t args[6] = {address_of(list), radius, 0, 0, 0, address_of(count_out)};
    const uint32_t node = title_call_at_regs(entry, own_address(&PathNode_FindNearest_entry), &regs, 6, args);  // PathNode_FindNearest
    *reinterpret_cast<uint32_t*>(uintptr_t{entry - 0x1c}) = 0x002eabca;  // its return address
    return node;
}

namespace {

// The title CRT's __CIsin / __CIsqrt of an x87 value, stored as float.
float title_ci_sin_ext(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033ae60})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

float title_ci_sqrt_float(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

}  // namespace

void XB_STDCALL PathNode_AdjustBadPlaceArc(uint8_t* tree, const float* arc, uint32_t mask, int32_t delta) {
    // arc = {origin x, y, z, radius +0xc, half height +0x10, start yaw +0x14, end yaw +0x18} (degrees). Every
    // link (node +0x64, 0xc each, count +0x62; target word: block (>> 9) - 1 of tree +8, node & 0x1ff, 0x80 each)
    // of every node near the arc (squared xy distance under (radius + 256)^2, z under (half height + 128)^2)
    // whose segment is not farther from the arc's centroid than the node, not wholly above or below it, and
    // (both ends outside the circle) crosses the cylinder within its height, and lies on the arc's side of
    // the start/end lines (both for arcs under 180 degrees, either otherwise; not checked for 0..360) gets
    // `delta` added to each of its four team bytes (+4) selected by `mask`, stopping at the first that would
    // leave 0..255.
    const float deg = title_global<float>(0x0083edd8);
    const float zero = title_global<float>(0x0083eafc);
    float s1, c1, s2, c2, sm, cm;
    FastSinCos(&s1, 0, &c1, arc[5] * deg);
    const float c1n = 0.0f - c1;
    FastSinCos(&s2, 0, &c2, arc[6] * deg);
    const float s1n = 0.0f - s1;
    const float c1pp = 0.0f - c1n;
    const float c2n = 0.0f - c2;
    float d2 = arc[0] * s2;
    d2 = d2 + c2n * arc[1];
    float span = arc[6] - arc[5];
    float d1 = arc[0] * s1n;
    d1 = d1 + c1pp * arc[1];
    if (0.0f > span) span = span + title_global<float>(0x0083ebcc);  // 360
    const bool narrow = title_global<float>(0x0083eb60) > span;   // 180
    FastSinCos(&sm, 0, &cm, (span * title_global<float>(0x0083eb24) + arc[5]) * deg);
    const float half_sine =
        title_ci_sin_ext(platform_x87_ld(span) * platform_x87_ld(title_global<float>(0x0083ecf0)));
    const float r2 = arc[3] * arc[3];
    const float h2 = arc[4] * arc[4];
    float far2 = arc[3] + title_global<float>(0x0083ecf4);  // 256
    far2 = far2 * far2;
    float hfar2 = arc[4] + title_global<float>(0x0083ebe8);  // 128
    hfar2 = hfar2 * hfar2;
    // The centroid of the sector: 2 r sin(span / 2) / span (degrees) along the middle direction.
    const float k = static_cast<float>(platform_x87_ld(half_sine) / span *
                                       platform_x87_ld(title_global<float>(0x0083f00c)) * arc[3]);
    const float cx = static_cast<float>(platform_x87_ld(cm) * k + arc[0]);
    const float cy = static_cast<float>(platform_x87_ld(sm) * k + arc[1]);
    auto W = [](uint32_t a) -> uint32_t& { return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(a)); };
    auto Fl = [](uint32_t a) -> float { return *reinterpret_cast<const float*>(static_cast<uintptr_t>(a)); };
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    if (static_cast<int32_t>(W(W(base + 0x194) + 0xc)) <= 0) return;
    for (int32_t b = 0; b < static_cast<int32_t>(W(W(base + 0x194) + 0xc)); ++b) {
        const uint32_t slot = base + 8 + static_cast<uint32_t>(b) * 4;
        const uint32_t block = W(slot);
        if (!block || static_cast<int32_t>(W(block + 4)) <= 0) continue;
        for (uint32_t n = W(block + 4), off = 0; n; --n, off += 0x80) {
            const uint32_t node = W(W(slot) + 0x10) + off;
            const float dx = Fl(node + 0x44) - arc[0];
            float dy = Fl(node + 0x48) - arc[1];
            const float dz = Fl(node + 0x4c) - arc[2];
            const float dy_saved = dy;
            float near2 = dy * dy;
            near2 = near2 + dx * dx;
            if (near2 >= far2) continue;
            if (dz * dz >= hfar2) continue;
            const int16_t links = *reinterpret_cast<const int16_t*>(static_cast<uintptr_t>(node + 0x62));
            const float qy = cy - Fl(node + 0x48), qx = cx - Fl(node + 0x44);
            float q = qy * qy;
            q = q + qx * qx;
            if (links <= 0) continue;
            for (int32_t l = 0; l < *reinterpret_cast<const int16_t*>(static_cast<uintptr_t>(node + 0x62)); ++l) {
                const uint32_t link = W(node + 0x64) + static_cast<uint32_t>(l) * 0xc;
                const uint16_t target = *reinterpret_cast<const uint16_t*>(static_cast<uintptr_t>(link));
                uint32_t other = 0;
                if (target && target != 0xffff) {
                    const uint32_t list = W(base + 8 + static_cast<uint16_t>((target >> 9) - 1) * 4u);
                    if (list) other = W(list + 0x10) + (target & 0x1ffu) * 0x80;
                }
                const float oy = Fl(other + 0x48) - arc[1];
                const float ox = Fl(other + 0x44) - arc[0];
                const float oz = Fl(other + 0x4c) - arc[2];
                float o2 = oy * oy;
                o2 = o2 + ox * ox;
                const float px = cx - Fl(other + 0x44);
                const float py = cy - Fl(other + 0x48);
                float p = py * py;
                p = p + px * px;
                if (p > q) continue;
                if (!(dz < arc[4]) && dz == dz && arc[4] == arc[4]) {
                    if (oz >= arc[4]) continue;
                } else {
                    const float bottom = 0.0f - arc[4];
                    if (!(bottom < dz) && bottom == bottom && dz == dz && bottom >= oz) continue;
                }
                if (near2 > r2 && o2 > r2) {
                    // Both ends outside the circle: where the segment meets it, within the height.
                    const float ez = Fl(other + 0x4c) - Fl(node + 0x4c);
                    const float ex = Fl(other + 0x44) - Fl(node + 0x44);
                    const float ey = Fl(other + 0x48) - Fl(node + 0x48);
                    float dot = ey * dy_saved;
                    dot = dot + ex * dx;
                    const float nb = zero - dot;
                    if (zero >= nb) continue;
                    float a = ey * ey;
                    a = a + ex * ex;
                    const float disc = nb * nb - (near2 - r2) * a;
                    if (zero >= disc) continue;
                    const float root = title_ci_sqrt_float(disc);
                    const float one = title_global<float>(0x0083eb20);
                    const float t0 = (nb - root) / a;
                    if (t0 >= one) continue;
                    const float z0 = t0 * ez + dz;
                    if (z0 * z0 > h2) {
                        const float t1 = (root + nb) / a;
                        if (t1 >= one) continue;
                        const float z1 = t1 * ez + dz;
                        if (z1 * z1 > h2 && z1 * z0 >= title_global<float>(0x0083eafc)) continue;
                    }
                }
                if (arc[5] != 0.0f || arc[6] != title_global<float>(0x0083ebcc)) {
                    // Sides of the start (c1, -s1) and end (s2, -c2) lines.
                    float p1 = c1pp * Fl(node + 0x48);
                    p1 = p1 + s1n * Fl(node + 0x44);
                    float q1 = s2 * Fl(node + 0x44);
                    q1 = q1 + c2n * Fl(node + 0x48);
                    q1 = q1 - d2;
                    float p2 = Fl(other + 0x48) * c1pp;
                    float q2 = Fl(other + 0x48) * c2n;
                    p2 = p2 + Fl(other + 0x44) * s1n;
                    q2 = q2 + Fl(other + 0x44) * s2;
                    q2 = q2 - d2;
                    p1 = p1 - d1;
                    p2 = p2 - d1;
                    if (narrow) {
                        if (zero > p1 && zero > p2) continue;
                        if (zero > q1 && zero > q2) continue;
                    } else if (zero > p1 && zero > p2 && zero > q1 && zero > q2) {
                        continue;
                    }
                }
                for (uint32_t bit = 0; bit < 4; ++bit) {
                    if (!(mask & (1u << bit))) continue;
                    uint8_t& team = *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(link + 4 + bit));
                    const int32_t value = static_cast<int32_t>(team) + delta;
                    if (value < 0 || value > 0xff) break;
                    team = static_cast<uint8_t>(value);
                }
            }
        }
    }
}

}  // namespace game
