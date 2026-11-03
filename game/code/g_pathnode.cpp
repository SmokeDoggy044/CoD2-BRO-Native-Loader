#include "g_pathnode.h"

#include "actor.h"
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
            title_function<void XB_THISCALL(uint32_t*, const char*)>(0x0011eb50)(&text, value);  // ScriptString_FromCString
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
                title_function<Error>(0x00248af0)(reinterpret_cast<const char*>(ptr(0x00833980)), text());  // G_Error
            offset += 0x80;
        } while (--left);
    }
    title_function<Error>(0x00248af0)(reinterpret_cast<const char*>(ptr(0x00833954)), text());  // G_Error
}

}  // namespace game
