#include "cm.h"

#include <cstring>

#include "lanes.h"
#include "g_script.h"
#include "qcommon.h"
#include "sv.h"
#include "title_data.h"

namespace game {

namespace {

uint8_t* clip_map() { return title_global<uint8_t*>(0x0095f6b8); }  // cm

template <typename T>
T& cm_field(uint32_t offset) {
    return *reinterpret_cast<T*>(clip_map() + offset);
}

// Area record {floodnum, floodvalid} (index clamped to 0 like the original).
int32_t* cm_area(uint32_t index) {
    if (index >= cm_field<uint32_t>(0x38)) index = 0;
    return cm_field<int32_t*>(0x3c) + index * 2;
}

uint8_t* cm_leaf(uint32_t index) {
    if (index >= cm_field<uint32_t>(8)) index = 0;
    return cm_field<uint8_t*>(0xc) + index * 0x10;
}

}  // namespace

int32_t XB_REGS3 XB_CDECL CM_AreasConnected(uint32_t, int32_t area1, int32_t area2) {
    if (area1 < 0 || area2 < 0) return 0;
    return cm_area(static_cast<uint32_t>(area1))[0] == cm_area(static_cast<uint32_t>(area2))[0];
}

const uint8_t* XB_THISCALL CM_ClusterPVS(int32_t cluster) {
    const uint8_t* vis = title_global<const uint8_t*>(0x0093565c);
    if (cluster < 0 || cluster >= cm_field<int32_t>(0x58) || !title_global<uint32_t>(0x00935660)) return vis;
    return vis + title_global<int32_t>(0x00935658) * cluster;
}

uint8_t* XB_CDECL CM_StoreLeafs(uint8_t* list, uint32_t leafnum) {
    int32_t* ll = reinterpret_cast<int32_t*>(list);
    if (*reinterpret_cast<int32_t*>(cm_leaf(leafnum) + 4) != -1) ll[0xc] = static_cast<int32_t>(leafnum);  // lastLeaf
    if (ll[0] >= ll[1]) {
        ll[2] = 1;  // overflowed
        return list;
    }
    reinterpret_cast<int32_t*>(uintptr_t{static_cast<uint32_t>(ll[3])})[ll[0]] = static_cast<int32_t>(leafnum);
    ++ll[0];
    return list;
}

int32_t XB_REGS1 XB_CDECL CM_LeafArea(uint32_t leafnum) { return *reinterpret_cast<int16_t*>(cm_leaf(leafnum) + 8); }

int32_t XB_REGS1 XB_CDECL CM_LeafCluster(uint32_t leafnum) {
    return *reinterpret_cast<int32_t*>(cm_leaf(leafnum) + 4);
}

int32_t XB_REGS2 XB_CDECL CM_PointLeafnum_r(uint32_t num, const float* point) {
    const uint8_t* base = cm_field<uint8_t*>(0xc);
    const uint8_t* node = cm_leaf(num);
    while (*reinterpret_cast<const uint16_t*>(node) == 0xffff) {
        const float* plane = *reinterpret_cast<const float* const*>(node + 4);
        const float d = ((point[2] * plane[2]) + (point[1] * plane[1])) + (point[0] * plane[0]) - plane[3];
        node = *reinterpret_cast<const uint8_t* const*>(node + (0.0f > d ? 0xc : 8));
    }
    return static_cast<int32_t>(node - base) >> 4;
}

void XB_CDECL CM_FloodArea_r(uint32_t area_num, int32_t flood_num) {
    int32_t* area = cm_area(area_num);
    if (area[1] == cm_field<int32_t>(0x5c)) {
        if (area[0] == flood_num) return;
        title_function<void XB_CDECL(uint32_t, const char*)>(0x002938f0)(  // __break
            1, reinterpret_cast<const char*>(uintptr_t{0x00833d9c}));  // "FloodArea_r: reflooded"
    }
    area[0] = flood_num;
    area[1] = cm_field<int32_t>(0x5c);
    const uint32_t row = cm_field<uint32_t>(0x38) * area_num;
    for (uint32_t i = 0; i < cm_field<uint32_t>(0x38); ++i) {
        uint32_t index = i + row;
        if (index >= cm_field<uint32_t>(0x40)) index = 0;
        if (cm_field<int32_t*>(0x44)[index] > 0) CM_FloodArea_r(i, flood_num);
    }
}

void XB_CDECL CM_FloodAreaConnections() {
    ++cm_field<int32_t>(0x5c);
    int32_t flood_num = 0;
    for (uint32_t i = 0; i < cm_field<uint32_t>(0x38); ++i) {
        if (cm_area(i)[1] == cm_field<int32_t>(0x5c)) continue;
        CM_FloodArea_r(i, ++flood_num);
    }
}

void XB_CDECL CM_BoxLeafnums_r(uint8_t* list, uint32_t nodenum, float* min_distance) {
    const uint8_t* base = cm_field<uint8_t*>(0xc);
    const float* mins = reinterpret_cast<const float*>(list + 0x10);
    const float* maxs = reinterpret_cast<const float*>(list + 0x20);
    for (;;) {
        const uint8_t* node = cm_leaf(nodenum);
        if (*reinterpret_cast<const uint16_t*>(node) != 0xffff) {
            title_function<void XB_CDECL(uint8_t*, uint32_t)>(*reinterpret_cast<uint32_t*>(list + 0x34))(list, nodenum);
            return;
        }
        const Lanes plane = lanes(*reinterpret_cast<const float* const*>(node + 4));
        Lanes near_corner, far_corner;  // CMPLEPS masks: 0 <= p picks maxs, p <= 0 picks maxs for the second
        for (int32_t i = 0; i < 4; ++i) {
            const bool positive = 0.0f <= plane.v[i];
            const bool negative = plane.v[i] <= 0.0f;
            far_corner.v[i] = positive ? maxs[i] : mins[i];
            near_corner.v[i] = negative ? maxs[i] : mins[i];
        }
        const Lanes p1 = mul(far_corner, plane);
        const float d1 = (p1.v[0] + p1.v[1]) + p1.v[2];
        platform_keep(p1);
        const Lanes p2 = mul(near_corner, plane);
        const float d2 = (p2.v[0] + p2.v[1]) + p2.v[2];
        platform_keep(p2);
        const float dist = plane.v[3];
        int32_t side = 0;
        float distance = 0.0f;
        if (dist > d1)
            distance = dist - d1;
        else
            side = 1;
        if (d2 > dist)
            distance = d2 - dist;
        else
            side |= 2;
        if (distance > 0.0f && *min_distance > distance) *min_distance = distance;
        if (side == 3) {
            CM_BoxLeafnums_r(list, static_cast<uint32_t>((*reinterpret_cast<const uint8_t* const*>(node + 8) - base) >> 4),
                             min_distance);
            nodenum = static_cast<uint32_t>((*reinterpret_cast<const uint8_t* const*>(node + 0xc) - base) >> 4);
        } else {
            nodenum = static_cast<uint32_t>((*reinterpret_cast<const uint8_t* const*>(node + 4 + side * 4) - base) >> 4);
        }
    }
}

PLATFORM_ASM_ENTRY(CM_AdjustAreaPortalState_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CM_AdjustAreaPortalState) "addl $12, %esp\n"
                   "ret")

void XB_CDECL CM_AdjustAreaPortalState(int32_t area1, int32_t area2, uint32_t open) {
    using Break = void XB_CDECL(uint32_t, const char*);
    if (area1 < 0 || area2 < 0) return;
    const uint32_t a1 = static_cast<uint32_t>(area1);
    const uint32_t a2 = static_cast<uint32_t>(area2);
    if (a1 >= cm_field<uint32_t>(0x38) || a2 >= cm_field<uint32_t>(0x38))
        title_function<Break>(0x002938f0)(1, reinterpret_cast<const char*>(uintptr_t{0x00833d70}));  // "CM_AdjustAreaPortalState: bad area number"
    auto portal = [](uint32_t from, uint32_t to) -> int32_t& {
        uint32_t index = cm_field<uint32_t>(0x38) * from + to;
        if (index >= cm_field<uint32_t>(0x40)) index = 0;
        return cm_field<int32_t*>(0x44)[index];
    };
    if (open) {
        ++portal(a1, a2);
        ++portal(a2, a1);
    } else if (portal(a2, a1)) {
        --portal(a1, a2);
        --portal(a2, a1);
        if (portal(a2, a1) < 0)
            title_function<Break>(0x002938f0)(1, reinterpret_cast<const char*>(uintptr_t{0x00833d3c}));  // "CM_AdjustAreaPortalState: negative reference count"
    }
    CM_FloodAreaConnections();
}

PLATFORM_ASM_ENTRY(CM_BoxLeafnums_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_BoxLeafnums) "addl $24, %esp\n"
                   "ret")

int32_t XB_CDECL CM_BoxLeafnums(const float* maxs, const float* mins, uint8_t* cache, int32_t* list,
                                int32_t list_size, int32_t* last_leaf) {
    ++cm_field<int32_t>(0x60);  // checkcount
    // leafList_t: count, maxcount, overflowed, list, bounds[2] (16-byte rows), lastLeaf, storeLeafs.
    struct alignas(16) LeafList {
        int32_t count, maxcount, overflowed;
        int32_t* list;
        float bounds[2][4];
        int32_t last_leaf;
        uint32_t store;
    } ll;
    ll.count = 0;
    ll.maxcount = list_size;
    ll.overflowed = 0;
    ll.list = list;
    std::memcpy(ll.bounds[0], mins, 16);
    std::memcpy(ll.bounds[1], maxs, 16);
    ll.last_leaf = 0;
    ll.store = 0x002aafc0;  // CM_StoreLeafs
    float* cached_center = reinterpret_cast<float*>(cache + 0x120);
    const float* center = reinterpret_cast<const float*>(cache + 0x140);
    float distance = cached_center[3] - title_global<float>(0x0083ece8);  // radius - 5
    if (distance > 0.0f) {
        const Lanes d = sub(lanes(cached_center), lanes(center));
        const Lanes squared = mul(d, d);
        platform_keep(squared);
        const float distance_squared = (squared.v[0] + squared.v[1]) + squared.v[2];
        if (distance * distance > distance_squared) {
            CM_StoreLeafs(reinterpret_cast<uint8_t*>(&ll), *reinterpret_cast<uint32_t*>(cache + 0x130));
            *last_leaf = ll.last_leaf;
            return ll.count;
        }
    }
    distance = title_global<float>(0x0082eed0);  // FLT_MAX
    CM_BoxLeafnums_r(reinterpret_cast<uint8_t*>(&ll), 0, &distance);
    const float radius = ll.count == 1 ? distance : 0.0f;
    cached_center[3] = radius;
    if (radius > title_global<float>(0x0083eafc)) {  // 0
        *reinterpret_cast<int32_t*>(cache + 0x130) = list[0];
        std::memcpy(cached_center, center, 16);
        cached_center[3] = radius;
    }
    *last_leaf = ll.last_leaf;
    return ll.count;
}

uint8_t* XB_REGS2 XB_CDECL CM_TempBoxModel(const float* maxs, uint32_t capsule, const float* mins, uint32_t contents) {
    uint8_t* box = title_global<uint8_t*>(0x0095f504);
    const Lanes lo = {{mins[0], mins[1], mins[2], 0.0f}};
    const Lanes hi = {{maxs[0], maxs[1], maxs[2], 0.0f}};
    std::memcpy(box + 0x10, lo.v, 16);
    std::memcpy(box + 0x20, hi.v, 16);
    uint8_t* leaf = *reinterpret_cast<uint8_t**>(box + 4);
    const float half = title_global<float>(0x0083eb24);  // 0.5
    const Lanes center = mul(add(lo, hi), {{half, half, half, half}});
    std::memcpy(leaf + 0x10, center.v, 12);
    const Lanes extent = sub(hi, center);
    std::memcpy(leaf + 0x20, extent.v, 16);
    const Lanes squared = mul(extent, extent);
    platform_keep(squared);
    const float length_squared = (squared.v[0] + squared.v[1]) + squared.v[2];
    float radius = length_squared;
    if (length_squared == length_squared && length_squared != 0.0f) {  // UCOMISS + JNE: NaN counts as equal
        const float r = platform_rsqrt_estimate(length_squared);
        const float scaled = length_squared * r;
        radius = ((title_global<float>(0x0082e660) - scaled * r) * scaled) * title_global<float>(0x0082e670);  // 3, 0.5
    }
    *reinterpret_cast<uint32_t*>(leaf + 8) = contents;
    *reinterpret_cast<float*>(leaf + 0x1c) = radius;
    *reinterpret_cast<uint32_t*>(box + 0x48) = capsule ? 0x1fe : 0x1ff;
    return box;
}

void XB_REGS1 XB_CDECL CM_LoadMap(const char* name, uint32_t client_load) {
    using Strncpy = char* XB_CDECL(char*, const char*, uint32_t);
    using CvarGet = uint8_t* XB_CDECL(const char*, const char*, uint32_t);
    if (!name || !*name)
        title_function<void XB_CDECL(uint32_t, const char*)>(0x002938f0)(  // __break
            1, reinterpret_cast<const char*>(uintptr_t{0x00833f64}));  // "CM_LoadMap: NULL name"
    char copy[0x80];
    title_function<Strncpy>(0x001eed80)(copy, name, 0x7f);
    copy[0x7f] = 0;
    title_global<uint8_t*>(0x00928360) = title_function<CvarGet>(0x0029c2d0)(  // Cvar_Get
        reinterpret_cast<const char*>(uintptr_t{0x00833f58}), reinterpret_cast<const char*>(uintptr_t{0x0082e098}),
        0x200);  // "cm_noCurves", "0"
    title_global<uint8_t*>(0x0092effc) = title_function<CvarGet>(0x0029c2d0)(
        reinterpret_cast<const char*>(uintptr_t{0x00833f44}), reinterpret_cast<const char*>(uintptr_t{0x0082e058}),
        0x201);  // "cm_playerCurveClip", "1"
    if (client_load) return;
    title_function<Strncpy>(0x001eed80)(reinterpret_cast<char*>(uintptr_t{0x009355d8}), copy, 0x7f);
    title_global<char>(0x00935657) = 0;
}

uint8_t XB_REGS3 XB_CDECL CM_BrushPointTest(uint32_t, uint32_t, const uint8_t* brush, const float* point) {
    const Lanes center = lanes(reinterpret_cast<const float*>(brush + 0x10));
    const Lanes half = lanes(reinterpret_cast<const float*>(brush + 0x20));
    const Lanes p = lanes(point);
    const Lanes high = add(center, half);
    const Lanes below = sub(sub(center, half), p);
    const Lanes above = sub(p, high);
    int32_t inside = 0;
    for (int32_t i = 0; i < 4; ++i) {
        const float m = below.v[i] > above.v[i] ? below.v[i] : above.v[i];  // MAXPS: the second operand on NaN
        if (m < 0.0f && i < 3) inside |= 1 << i;  // CMPLTPS on every lane
    }
    if (inside != 7) return 1;
    const uint32_t count = *reinterpret_cast<const uint32_t*>(brush + 0x30);
    const float* side = *reinterpret_cast<const float* const*>(brush + 0x34);
    for (uint32_t i = 0; i < count; ++i, side += 4) {
        const Lanes product = mul(p, lanes(side));
        platform_keep(product);
        const float d = ((product.v[0] + product.v[1]) + product.v[2]) - side[3];
        if (d >= 0.0f) return 0;
    }
    return 1;
}

const uint8_t* XB_THISCALL CM_QuadTreeFindLeaf(const uint8_t* tree, const float* point, float* center,
                                               float* half_size) {
    std::memcpy(center, tree + 0x10, 16);
    const float root = *reinterpret_cast<const float*>(tree + 0x20);
    const float half = title_global<float>(0x0083eb24);  // 0.5
    Lanes size = {{root, root, root, root}};
    const int16_t* children = *reinterpret_cast<const int16_t* const*>(tree + 4);
    int32_t index = 0;
    if (children[0] > 0) {
        do {
            const bool below_x = point[0] < center[0];  // COMISS + JB: NaN counts as below
            index = children[index];
            size = mul(size, {{half, half, half, half}});
            if (!below_x) index += 2;
            if (!(point[1] < center[1])) index += 1;
            const Lanes c = lanes(center);
            const Lanes d = sub(lanes(point), c);
            Lanes step;
            for (int32_t i = 0; i < 4; ++i) {
                uint32_t bits, size_bits;
                std::memcpy(&bits, &d.v[i], 4);
                std::memcpy(&size_bits, &size.v[i], 4);
                bits = (bits & 0x80000000u) ^ size_bits;
                std::memcpy(&step.v[i], &bits, 4);
            }
            const Lanes moved = add(c, step);
            std::memcpy(center, moved.v, 16);
            children = *reinterpret_cast<const int16_t* const*>(tree + 4);
        } while (children[index] > 0);
    }
    *half_size = size.v[0];
    return *reinterpret_cast<const uint8_t* const*>(tree + 0xc) - children[index] * 8;
}

namespace {

// MAXPS(a, b) < 0 (CMPLTPS) on every lane; true when lanes 0-2 are all negative.
bool inside_xyz(const Lanes& a, const Lanes& b) {
    int32_t mask = 0;
    for (int32_t i = 0; i < 4; ++i) {
        const float m = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
        if (m < 0.0f && i < 3) mask |= 1 << i;
    }
    return mask == 7;
}

}  // namespace

uint32_t XB_REGS3 XB_CDECL CM_PointContents(const float* point, uint32_t, const uint32_t* model) {
    uint32_t contents = 0;
    if (model) {
        alignas(16) float p[4] = {point[0], point[1], point[2], 0.0f};  // w: never-set stack (0 in tests)
        const int32_t count = static_cast<int32_t>(model[0]);
        if (count <= 0) return 0;
        const uint8_t* brush = reinterpret_cast<const uint8_t*>(uintptr_t{model[1]});
        for (int32_t i = count; i; --i, brush += 0x40)
            if (CM_BrushPointTest(0, 0, brush, p)) contents |= *reinterpret_cast<const uint32_t*>(brush + 8);
        return contents;
    }
    alignas(16) float p[4] = {point[0], point[1], point[2], point[0]};
    const Lanes pl = lanes(p);
    const uint8_t* world = title_global<const uint8_t*>(0x0095f4fc);
    for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(world + 0xc); ++k) {
        const uint8_t* tree = *reinterpret_cast<const uint8_t* const*>(world + 0x10 + k * 4);
        if (!inside_xyz(sub(lanes(reinterpret_cast<const float*>(tree + 0x40)), pl),
                        sub(pl, lanes(reinterpret_cast<const float*>(tree + 0x50)))))
            continue;
        alignas(16) float center[4];
        float half;
        const uint8_t* leaf = CM_QuadTreeFindLeaf(tree, p, center, &half);
        const int32_t n = *reinterpret_cast<const int32_t*>(leaf);
        for (int32_t j = 0; j < n; ++j) {
            const uint32_t index = (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[j];
            const uint32_t split = *reinterpret_cast<const uint32_t*>(tree + 0x30);
            uint8_t* object = index < split
                                  ? *reinterpret_cast<uint8_t* const*>(tree + 0x34) + index * 0x50
                                  : *reinterpret_cast<uint8_t* const*>(tree + 0x3c) + (index - split) * 0x40;
            const uint32_t slot = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<uint8_t* const*>(object) + 0x20);
            if (title_function<int32_t XB_THISCALL(uint8_t*)>(slot)(object) != 9) continue;
            const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
            const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
            if (!inside_xyz(sub(sub(c, h), pl), sub(pl, add(c, h)))) continue;
            const int32_t sides = *reinterpret_cast<const int32_t*>(object + 0x30);
            const float* side = *reinterpret_cast<const float* const*>(object + 0x34);
            int32_t i = 0;
            for (; i < sides; ++i, side += 4) {
                const Lanes product = mul(pl, lanes(side));
                platform_keep(product);
                if ((product.v[0] + product.v[1]) + product.v[2] > side[3]) break;
            }
            if (i == sides) contents |= *reinterpret_cast<const uint32_t*>(object + 8);
        }
    }
    return contents;
}

PLATFORM_ASM_ENTRY(CM_TransformedPointContents_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_TransformedPointContents) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL CM_TransformedPointContents(const float* origin, const float* angles, const uint32_t* model,
                                              const float* point) {
    float local[3] = {point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    const bool box = reinterpret_cast<const uint32_t*>(model)[0x48 / 4] == 0x1ff;
    // UCOMISS + JP: rotate when an angle is non-zero or NaN.
    if (!box && (!(angles[0] == 0.0f) || !(angles[1] == 0.0f) || !(angles[2] == 0.0f))) {
        float forward[3], right[3], up[3];
        uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles)),
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward))};
        AnglesToAxis(args, right, up);
        const float x = local[0], y = local[1], z = local[2];
        const float tx = (z * forward[2] + y * forward[1]) + x * forward[0];
        const float s = (z * right[2] + y * right[1]) + right[0] * x;
        const float tz = (z * up[2] + up[1] * y) + up[0] * x;
        local[1] = 0.0f - s;
        local[0] = tx;
        local[2] = tz;
    }
    return CM_PointContents(local, 0, model);
}

void XB_REGS3 XB_CDECL Mat34_Transpose3(float* out, uint32_t, const float* rows) {
    for (int32_t i = 0; i < 3; ++i) {
        std::memcpy(&out[i * 4 + 0], &rows[i], 4);
        std::memcpy(&out[i * 4 + 1], &rows[4 + i], 4);
        std::memcpy(&out[i * 4 + 2], &rows[8 + i], 4);
    }
}

float* XB_REGS3 XB_CDECL Mat34_RotateVector(float* v, uint32_t, const float* rows) {
    const Lanes in = lanes(v);
    for (int32_t i = 0; i < 3; ++i) {
        const Lanes product = mul(lanes(rows + i * 4), in);
        platform_keep(product);
        v[i] = (product.v[0] + product.v[1]) + product.v[2];
    }
    return v;  // EAX is never written
}

namespace {

float sum_xyz(const Lanes& product) {
    platform_keep(product);
    return (product.v[0] + product.v[1]) + product.v[2];
}

const float* tw_row(const uint8_t* tw, uint32_t offset) { return reinterpret_cast<const float*>(tw + offset); }

}  // namespace

// The original never touches ECX; its title callers read the trace work from ECX after the call.
PLATFORM_ASM_ENTRY(CM_TestBoxInBrush_entry,
                   "pushl %ecx\n"
                   "pushl 8(%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(CM_TestBoxInBrush) "addl $8, %esp\n"
                   "popl %ecx\n"
                   "ret")

void XB_CDECL CM_TestBoxInBrush(uint8_t* tw, const uint8_t* brush) {
    const Lanes c = lanes(reinterpret_cast<const float*>(brush + 0x10));
    const Lanes h = lanes(reinterpret_cast<const float*>(brush + 0x20));
    if (!inside_xyz(sub(sub(c, h), lanes(tw_row(tw, 0x30))), sub(lanes(tw_row(tw, 0x20)), add(c, h)))) return;
    const uint32_t sides = *reinterpret_cast<const uint32_t*>(brush + 0x30);
    const float* side = *reinterpret_cast<const float* const*>(brush + 0x34);
    if (*reinterpret_cast<const uint32_t*>(tw + 0x160)) {
        const Lanes offset = lanes(tw_row(tw, 0x140));
        for (uint32_t i = 0; i < sides; ++i, side += 4) {
            const Lanes n = lanes(side);
            const float limit = side[3] + *reinterpret_cast<const float*>(tw + 0x164);
            const Lanes start = lanes(tw_row(tw, 0));
            const Lanes v = sum_xyz(mul(n, offset)) > 0.0f ? sub(start, offset) : add(start, offset);
            if (sum_xyz(mul(v, n)) - limit > 0.0f) return;
        }
    } else {
        const Lanes start = lanes(tw_row(tw, 0));
        for (uint32_t i = 0; i < sides; ++i, side += 4) {
            int32_t corner = 0;
            if (0.0f > side[0]) corner = 1;
            if (0.0f > side[1]) corner |= 2;
            if (0.0f > side[2]) corner |= 4;
            const Lanes n = lanes(side);
            const float dist = side[3] - sum_xyz(mul(lanes(tw_row(tw, 0x60 + corner * 0x10)), n));
            if (sum_xyz(mul(start, n)) - dist > 0.0f) return;
        }
    }
    tw[0x13c] = 1;
    tw[0x13d] = 1;
    *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
    *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(brush + 8);
}

PLATFORM_ASM_ENTRY(CM_TestInBrushes_entry,
                   "pushl %ecx\n"
                   "pushl %ecx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(CM_TestInBrushes) "addl $8, %esp\n"
                   "popl %ecx\n"
                   "ret")

void XB_CDECL CM_TestInBrushes(const uint32_t* model, uint8_t* tw) {
    if (!model) return;
    const uint8_t* brushes = reinterpret_cast<const uint8_t*>(uintptr_t{model[1]});
    for (int32_t i = 0; i < static_cast<int32_t>(model[0]); ++i) {
        CM_TestBoxInBrush(tw, brushes + i * 0x40);
        if (tw[0x13c]) return;
    }
}

PLATFORM_ASM_ENTRY(CM_TestCapsuleInTempBox_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CM_TestCapsuleInTempBox) "addl $4, %esp\n"
                   "movl %esi, %ecx\n"
                   "ret")

void XB_CDECL CM_TestCapsuleInTempBox(uint8_t* tw) {
    const uint8_t* box = title_global<const uint8_t*>(0x0095f504);
    const float h = title_global<float>(0x0083eb24);  // 0.5
    const Lanes maxs = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes center = mul(add(lanes(reinterpret_cast<const float*>(box + 0x10)), maxs), {{h, h, h, h}});
    const Lanes half = sub(maxs, center);
    float* start = reinterpret_cast<float*>(tw);
    float* end = reinterpret_cast<float*>(tw + 0x10);
    const Lanes moved_start = sub(lanes(start), center);
    std::memcpy(start, moved_start.v, 16);
    const Lanes moved_end = sub(lanes(end), center);
    std::memcpy(end, moved_end.v, 16);
    const float half_z = half.v[2];
    *reinterpret_cast<uint32_t*>(tw + 0x160) = 1;
    const float radius = half.v[0] > half_z ? half_z : half.v[0];
    *reinterpret_cast<float*>(tw + 0x164) = radius;
    *reinterpret_cast<float*>(tw + 0x168) = half_z;
    *reinterpret_cast<float*>(tw + 0x140) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x144) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x148) = half_z - *reinterpret_cast<const float*>(tw + 0x164);
    const uint8_t* leaf = *reinterpret_cast<uint8_t* const*>(box + 4);
    CM_TempBoxModel(reinterpret_cast<const float*>(tw + 0x50), 0, reinterpret_cast<const float*>(tw + 0x40),
                    *reinterpret_cast<const uint32_t*>(leaf + 8));
    CM_TestBoxInBrush(tw, *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f504) + 4));
}

void XB_REGS1 XB_CDECL CM_TestCapsuleInCapsule(uint8_t* tw) {
    const Lanes offset = lanes(tw_row(tw, 0x140));
    const Lanes start = lanes(tw_row(tw, 0));
    const Lanes top = add(start, offset);
    const Lanes bottom = sub(start, offset);
    const uint8_t* box = title_global<const uint8_t*>(0x0095f504);
    const Lanes maxs = lanes(reinterpret_cast<const float*>(box + 0x20));
    const float h = title_global<float>(0x0083eb24);  // 0.5
    const Lanes center = mul(add(lanes(reinterpret_cast<const float*>(box + 0x10)), maxs), {{h, h, h, h}});
    const Lanes half = sub(maxs, center);
    const float radius = half.v[0] > half.v[2] ? half.v[2] : half.v[0];
    const float half_height = half.v[2] - radius;
    float r = *reinterpret_cast<const float*>(tw + 0x164) + radius;
    auto hit = [&](const Lanes& a, const Lanes& b) {
        const Lanes d = sub(a, b);
        if (r * r > sum_xyz(mul(d, d))) {
            tw[0x13c] = 1;
            tw[0x13d] = 1;
            *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
        }
    };
    Lanes upper = center;
    upper.v[2] = center.v[2] + half_height;
    hit(upper, top);
    hit(upper, bottom);
    Lanes lower = center;
    lower.v[2] = center.v[2] - half_height;
    hit(lower, top);
    hit(lower, bottom);
    const float reach = (*reinterpret_cast<const float*>(tw + 0x168) + half_height) - *reinterpret_cast<const float*>(tw + 0x164);
    const float dz = *reinterpret_cast<const float*>(tw + 8) - center.v[2];
    if (!(reach >= dz)) return;  // COMISS + JB: NaN skips
    if (!(dz >= 0.0f - reach)) return;
    Lanes flat_top = top;
    flat_top.v[2] = 0.0f;
    upper.v[2] = 0.0f;
    hit(flat_top, upper);
}

PLATFORM_ASM_ENTRY(CM_TestInWorldTrees_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CM_TestInWorldTrees) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CM_TestInWorldTrees(uint8_t* tw) {
    const Lanes one = lanes(reinterpret_cast<const float*>(uintptr_t{0x0082e640}));
    const Lanes start = lanes(tw_row(tw, 0));
    const Lanes lo = sub(add(start, lanes(tw_row(tw, 0x40))), one);
    const Lanes hi = add(add(start, lanes(tw_row(tw, 0x50))), one);
    for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0095f4fc) + 0xc); ++k) {
        const uint8_t* tree =
            *reinterpret_cast<const uint8_t* const*>(title_global<const uint8_t*>(0x0095f4fc) + 0x10 + k * 4);
        if (!inside_xyz(sub(lanes(reinterpret_cast<const float*>(tree + 0x40)), hi),
                        sub(lo, lanes(reinterpret_cast<const float*>(tree + 0x50)))))
            continue;
        alignas(16) float center[4];
        float half;
        const uint8_t* leaf = CM_QuadTreeFindLeaf(tree, tw_row(tw, 0), center, &half);
        title_function<void XB_CDECL(uint8_t*, const uint8_t*, const uint8_t*)>(0x002da4d0)(tw, tree, leaf);
        if (tw[0x13c]) return;
    }
}

namespace {

uint16_t* box_stack() { return reinterpret_cast<uint16_t*>(uintptr_t{0x0090a020}); }
uint16_t& box_stack_depth() { return title_global<uint16_t>(0x0090a620); }

}  // namespace

void XB_CDECL CM_BoxTreeStack_Push(uint16_t node, uint16_t count, uint16_t axis) {
    uint16_t depth = box_stack_depth();
    box_stack()[depth++] = node;
    box_stack()[depth++] = count;
    box_stack()[depth++] = axis;
    box_stack_depth() = depth;
}

uint16_t XB_THISCALL CM_BoxTreeStack_Next(const uint8_t* tree, const float* mins, const float* maxs) {
    uint16_t* stack = box_stack();
    uint16_t depth = box_stack_depth();
    for (;;) {
        if (!depth) {
            box_stack_depth() = depth;
            return 0xffff;
        }
        const uint16_t axis = stack[--depth];
        const uint16_t count = stack[--depth];
        const uint16_t node = stack[--depth];
        const uint8_t* counts = *reinterpret_cast<uint8_t* const*>(tree + 0x4c) + node * 2;
        const uint16_t below = counts[0];
        const uint16_t above = counts[1];
        const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(tree + 0x3c))[node];
        const float* split = reinterpret_cast<const float*>(*reinterpret_cast<uint8_t* const*>(tree + 0x34) + (flags & 0x3f) * 16);
        const uint16_t rest = static_cast<uint16_t>(count - below - above - 1);
        const uint16_t next_axis = static_cast<uint16_t>((axis + 1) % 3);
        if (rest) {
            stack[depth++] = static_cast<uint16_t>(above + below + node + 1);
            stack[depth++] = rest;
            stack[depth++] = next_axis;
        }
        if (split[axis] > mins[axis] && below) {
            stack[depth++] = static_cast<uint16_t>(node + 1);
            stack[depth++] = below;
            stack[depth++] = next_axis;
        }
        if (maxs[axis] > split[axis]) {
            box_stack_depth() = depth;
            if (above) CM_BoxTreeStack_Push(static_cast<uint16_t>(below + node + 1), above, next_axis);
            return node;
        }
    }
}

namespace {

Lanes splat(float x) { return {{x, x, x, x}}; }

}  // namespace

float* XB_CDECL ClosestPointOnTriangle(float* out, const float* a, const float* b, const float* c, const float* p) {
    const Lanes la = lanes(a), lb = lanes(b), lc = lanes(c), lp = lanes(p);
    const Lanes ap = sub(lp, la);
    const Lanes ab = sub(lb, la);
    const float d1 = sum_xyz(mul(ap, ab));
    const Lanes ca = sub(la, lc);  // a - c
    const float d2 = sum_xyz(mul(ap, ca));
    if (d2 >= 0.0f && 0.0f >= d1) {
        std::memcpy(out, a, 16);
        return out;
    }
    const float acac = sum_xyz(mul(ca, ca));
    const float e = acac + d2;
    const float abab = sum_xyz(mul(ab, ab));
    const Lanes bp = sub(lp, lb);
    const Lanes cb = sub(lc, lb);
    const float d3 = sum_xyz(mul(bp, cb));
    if (d1 >= abab && 0.0f >= d3) {
        std::memcpy(out, b, 16);
        return out;
    }
    const float cbcb = sum_xyz(mul(cb, cb));
    if (d3 >= cbcb && 0.0f >= e) {
        std::memcpy(out, c, 16);
        return out;
    }
    const float abac = sum_xyz(mul(ca, ab));
    const Lanes n = sub(mul(ca, splat(abab)), mul(splat(abac), ab));
    const float vc = sum_xyz(mul(ap, n));
    if (vc >= 0.0f && d1 >= 0.0f && abab >= d1) {
        const Lanes r = add(la, mul(ab, splat(d1 / abab)));
        std::memcpy(out, r.v, 16);
        return out;
    }
    const float s = sum_xyz(mul(ab, cb));
    const Lanes m = sub(mul(ab, splat(cbcb)), mul(cb, splat(s)));
    const float va = sum_xyz(mul(bp, m));
    if (va >= 0.0f && d3 >= 0.0f && cbcb >= d3) {
        const Lanes r = add(lb, mul(cb, splat(d3 / cbcb)));
        std::memcpy(out, r.v, 16);
        return out;
    }
    Lanes neg_ab;
    for (int32_t i = 0; i < 4; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &ab.v[i], 4);
        bits ^= 0x80000000u;
        std::memcpy(&neg_ab.v[i], &bits, 4);
    }
    const Lanes k = add(mul(neg_ab, splat(acac)), mul(splat(abac), ca));
    const float vb = sum_xyz(mul(ap, k));
    if (vb >= 0.0f && e >= 0.0f && acac >= e) {
        const Lanes r = add(lc, mul(ca, splat(e / acac)));
        std::memcpy(out, r.v, 16);
        return out;
    }
    std::memcpy(out, p, 16);
    return out;
}

namespace {

Lanes yzx(const Lanes& a) { return {{a.v[1], a.v[2], a.v[0], a.v[3]}}; }

// The edge test: (a * yzx(b) - yzx(a) * b) shuffled to yzx, dotted with the normal.
float edge_side(const Lanes& a, const Lanes& b, const Lanes& normal) {
    return sum_xyz(mul(yzx(sub(mul(a, yzx(b)), mul(yzx(a), b))), normal));
}

}  // namespace

PLATFORM_ASM_ENTRY(CM_SphereTouchesTriangle_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_SphereTouchesTriangle) "addl $24, %esp\n"
                   "ret")

uint8_t XB_CDECL CM_SphereTouchesTriangle(const float* v2, const float* v1, const float* center, const float* v0,
                                          float radius, const float* plane) {
    const Lanes n = lanes(plane);
    const Lanes c = lanes(center);
    const Lanes product = mul(n, c);
    Lanes d4 = product;
    d4.v[0] = (product.v[0] + product.v[1]) + product.v[2];
    d4 = add(d4, splat(n.v[3]));  // ADDPS with the broadcast distance: every lane
    platform_keep(d4);
    const float d = d4.v[0];
    if (platform_x87_greater(abs_bits(d), radius)) return 0;
    const Lanes p = sub(c, mul(n, splat(d)));
    const Lanes a = lanes(v0), b = lanes(v1), e = lanes(v2);
    const float s1 = edge_side(sub(a, b), sub(p, b), n);
    const float s2 = edge_side(sub(b, e), sub(p, e), n);
    const float s3 = edge_side(sub(e, a), sub(p, a), n);
    const float order[4] = {s3, s2, s1, s1};  // SHUFPS lanes; CMPLTPS on all four
    int32_t mask = 0;
    for (int32_t i = 0; i < 4; ++i)
        if (order[i] < 0.0f) mask |= 1 << i;
    if (!(mask & 7)) return 1;
    alignas(16) float closest[4];
    alignas(16) float projected[4];
    std::memcpy(projected, p.v, 16);
    ClosestPointOnTriangle(closest, v0, v1, v2, projected);
    const Lanes delta = sub(lanes(closest), c);
    return radius * radius > sum_xyz(mul(delta, delta)) ? 1 : 0;
}

namespace {

uint8_t* leaf_object(uint8_t* tree, uint32_t index) {
    const uint32_t split = *reinterpret_cast<const uint32_t*>(tree + 0x30);
    if (index < split) return *reinterpret_cast<uint8_t* const*>(tree + 0x34) + index * 0x50;
    return *reinterpret_cast<uint8_t* const*>(tree + 0x3c) + (index - split) * 0x40;
}

int32_t object_type(uint8_t* object) {
    const uint32_t slot = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<uint8_t* const*>(object) + 0x20);
    return title_function<int32_t XB_THISCALL(uint8_t*)>(slot)(object);
}

}  // namespace

void XB_CDECL CM_TestInLeafObjects(uint8_t* tw, uint8_t* tree, const uint8_t* leaf) {
    int32_t& checkcount = cm_field<int32_t>(0x60);
    ++checkcount;
    const int32_t n = *reinterpret_cast<const int32_t*>(leaf);
    const uint16_t* indexes = *reinterpret_cast<const uint16_t* const*>(leaf + 4);
    for (int32_t j = 0; j < n; ++j) {
        uint8_t* object = leaf_object(tree, indexes[j]);
        if (object_type(object) != 9) continue;
        int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
        if (mark == checkcount) continue;
        mark = checkcount;
        if (!(*reinterpret_cast<const uint32_t*>(object + 8) & *reinterpret_cast<const uint32_t*>(tw + 0xf8))) continue;
        CM_TestBoxInBrush(tw, object);
        if (tw[0x13c]) return;
    }
    if (*reinterpret_cast<const uint32_t*>(tw + 0xfc)) return;
    const float radius = *reinterpret_cast<const float*>(tw + 0x164);
    alignas(16) float center[4];
    std::memcpy(center, tw, 16);
    const Lanes start = lanes(center);
    alignas(16) float lo[4], hi[4];
    const Lanes lo_lanes = sub(start, splat(radius));
    const Lanes hi_lanes = add(start, splat(radius));
    std::memcpy(lo, lo_lanes.v, 16);
    std::memcpy(hi, hi_lanes.v, 16);
    for (int32_t j = 0; j < n; ++j) {
        uint8_t* object = leaf_object(tree, (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[j]);
        if (object_type(object) != 8) continue;
        int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
        if (mark == checkcount) continue;
        if (!(*reinterpret_cast<const uint32_t*>(tw + 0xf8) & *reinterpret_cast<const uint32_t*>(object + 8))) continue;
        mark = checkcount;
        const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
        const Lanes half = lanes(reinterpret_cast<const float*>(object + 0x20));
        const Lanes below = sub(lo_lanes, c);
        const Lanes above = sub(c, hi_lanes);
        int32_t inside = 0;
        for (int32_t i = 0; i < 4; ++i) {
            const float m = above.v[i] > below.v[i] ? above.v[i] : below.v[i];  // MAXPS(above, below)
            if (m < half.v[i] && i < 3) inside |= 1 << i;
        }
        if (inside != 7) {
            const Lanes reach = add(sub(hi_lanes, c), half);
            platform_keep(reach);
            if (!(0.0f <= reach.v[0])) return;
            continue;
        }
        uint16_t* stack = reinterpret_cast<uint16_t*>(uintptr_t{0x0090a020});
        stack[0] = 0;
        stack[2] = 0;
        stack[1] = *reinterpret_cast<const uint16_t*>(object + 0x40);
        title_global<uint16_t>(0x0090a620) = 3;
        for (uint16_t node = CM_BoxTreeStack_Next(object, lo, hi); node != 0xffff;
             node = CM_BoxTreeStack_Next(object, lo, hi)) {
            const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(object + 0x34);
            const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(object + 0x3c))[node];
            const float* v0 = reinterpret_cast<const float*>(rows + (flags & 0x3f) * 16);
            const float* v1 = reinterpret_cast<const float*>(rows + ((flags >> 6) & 0x3f) * 16);
            const float* v2 = reinterpret_cast<const float*>(rows + ((flags >> 12) & 0x3f) * 16);
            const float* plane = reinterpret_cast<const float*>(*reinterpret_cast<uint8_t* const*>(object + 0x44) + node * 16);
            if (CM_SphereTouchesTriangle(v2, v1, center, v0, radius, plane)) {
                tw[0x13c] = 1;
                tw[0x13d] = 1;
                *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
                return;
            }
        }
    }
}

PLATFORM_ASM_ENTRY(CM_TraceCapsuleThroughBrush_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(CM_TraceCapsuleThroughBrush) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CM_TraceCapsuleThroughBrush(uint8_t* tw, const uint8_t* brush) {
    const float eps = title_global<float>(0x0083ec68);  // 0.125
    alignas(16) float plane[4];
    std::memcpy(plane, reinterpret_cast<const void*>(uintptr_t{0x0082e110}), 16);  // zeros
    const Lanes start = lanes(tw_row(tw, 0));
    const Lanes end = lanes(tw_row(tw, 0x10));
    const Lanes offset = lanes(tw_row(tw, 0x140));
    const float* size = tw_row(tw, 0x150);
    float enter = 0.0f;
    float leave = *reinterpret_cast<const float*>(tw + 0x120);
    bool hit = false;
    bool gets_out = true;
    const Lanes center = lanes(reinterpret_cast<const float*>(brush + 0x10));
    const Lanes half = lanes(reinterpret_cast<const float*>(brush + 0x20));
    for (int32_t pass = 0; pass < 2; ++pass) {
        const Lanes bound = pass ? add(center, half) : sub(center, half);
        const float sign = title_global<float>(pass ? 0x0083eb20 : 0x0083eb34);  // 1 / -1
        for (int32_t a = 0; a < 3; ++a) {
            const float f = (start.v[a] - bound.v[a]) * sign - size[a];
            const float g = (end.v[a] - bound.v[a]) * sign - size[a];
            if (f > 0.0f) {
                const float d = f - g;
                if (g > 0.0f) {
                    if (0.0f >= d) return;
                    if (g >= eps) return;
                    gets_out = false;
                }
                const float f_eps = f - eps;
                if (f_eps > d * enter) {
                    enter = f_eps / d;
                    if (enter >= leave) return;
                    hit = true;
                    plane[a] = sign;
                } else if (!hit) {
                    hit = true;
                    plane[a] = sign;
                }
            } else if (g > 0.0f) {
                const float d = f - g;
                gets_out = false;
                if (f > d * leave) {
                    leave = f / d;
                    if (enter >= leave) return;
                }
            }
        }
    }
    const uint32_t sides = *reinterpret_cast<const uint32_t*>(brush + 0x30);
    const float* side = *reinterpret_cast<const float* const*>(brush + 0x34);
    for (uint32_t i = 0; i < sides; ++i, side += 4) {
        const Lanes n = lanes(side);
        const float dist = side[3] + *reinterpret_cast<const float*>(tw + 0x164);
        const bool toward = sum_xyz(mul(n, offset)) > 0.0f;
        const Lanes s0 = toward ? sub(start, offset) : add(start, offset);
        const Lanes e0 = toward ? sub(end, offset) : add(end, offset);
        const float f = sum_xyz(mul(s0, n)) - dist;
        const float g = sum_xyz(mul(e0, n)) - dist;
        if (f > 0.0f) {
            const float d = f - g;
            if (g > 0.0f) {
                if (0.0f >= d) return;
                if (g >= eps) return;
                gets_out = false;
            }
            const float f_eps = f - eps;
            if (f_eps > d * enter) {
                enter = f_eps / d;
                if (enter >= leave) return;
            } else if (hit) {
                continue;
            }
            std::memcpy(plane, side, 16);
            hit = true;
        } else if (g > 0.0f) {
            const float d = f - g;
            gets_out = false;
            if (f > d * leave) {
                leave = f / d;
                if (enter >= leave) return;
            }
        }
    }
    if (!hit) {
        tw[0x13d] = 1;
        if (!gets_out) return;
        tw[0x13c] = 1;
        *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
        return;
    }
    *reinterpret_cast<float*>(tw + 0x120) = enter;
    std::memcpy(tw + 0x110, plane, 16);
}

uint8_t XB_REGS1 XB_CDECL CM_TraceBoxThroughBrush(uint8_t* tw, const uint8_t* brush) {
    const float eps = title_global<float>(0x0083ec68);  // 0.125
    alignas(16) float plane[4];
    std::memcpy(plane, reinterpret_cast<const void*>(uintptr_t{0x0082e110}), 16);  // zeros
    const Lanes start = lanes(tw_row(tw, 0));
    const Lanes end = lanes(tw_row(tw, 0x10));
    const float* size = tw_row(tw, 0x50);
    float enter = 0.0f;
    float leave = *reinterpret_cast<const float*>(tw + 0x120);
    bool hit = false;
    bool gets_out = true;
    const Lanes center = lanes(reinterpret_cast<const float*>(brush + 0x10));
    const Lanes half = lanes(reinterpret_cast<const float*>(brush + 0x20));
    for (int32_t pass = 0; pass < 2; ++pass) {
        const Lanes bound = pass ? add(center, half) : sub(center, half);
        const float sign = title_global<float>(pass ? 0x0083eb20 : 0x0083eb34);  // 1 / -1
        for (int32_t a = 0; a < 3; ++a) {
            const float f = (start.v[a] - bound.v[a]) * sign - size[a];
            const float g = (end.v[a] - bound.v[a]) * sign - size[a];
            if (f > 0.0f) {
                const float d = f - g;
                if (g > 0.0f) {
                    if (0.0f >= d) return 0;
                    if (g >= eps) return 0;
                    gets_out = false;
                }
                const float f_eps = f - eps;
                if (f_eps > d * enter) {
                    enter = f_eps / d;
                    if (!(enter < leave)) return 0;
                    hit = true;
                    plane[a] = sign;
                } else if (!hit) {
                    hit = true;
                    plane[a] = sign;
                }
            } else if (g > 0.0f) {
                const float d = f - g;
                gets_out = false;
                if (f > d * leave) {
                    leave = f / d;
                    if (enter >= leave) return 0;
                }
            }
        }
    }
    const uint32_t sides = *reinterpret_cast<const uint32_t*>(brush + 0x30);
    const float* side = *reinterpret_cast<const float* const*>(brush + 0x34);
    for (uint32_t i = 0; i < sides; ++i, side += 4) {
        int32_t corner = 0;
        if (0.0f > side[0]) corner = 1;
        if (0.0f > side[1]) corner |= 2;
        if (0.0f > side[2]) corner |= 4;
        const Lanes n = lanes(side);
        const float* c = tw_row(tw, 0x60 + corner * 0x10);
        const Lanes packed = {{c[0], c[1], c[2], c[0]}};
        const float dist = side[3] - sum_xyz(mul(packed, n));
        const float f = sum_xyz(mul(start, n)) - dist;
        const float g = sum_xyz(mul(end, n)) - dist;
        if (f > 0.0f) {
            const float d = f - g;
            if (g > 0.0f) {
                if (0.0f >= d) return 0;
                if (g >= eps) return 0;
                gets_out = false;
            }
            const float f_eps = f - eps;
            if (f_eps > d * enter) {
                enter = f_eps / d;
                if (enter >= leave) return 0;
            } else if (hit) {
                continue;
            }
            std::memcpy(plane, side, 16);
            hit = true;
        } else if (g > 0.0f) {
            const float d = f - g;
            gets_out = false;
            if (f > d * leave) {
                leave = f / d;
                if (enter >= leave) return 0;
            }
        }
    }
    if (!hit) {
        tw[0x13d] = 1;
        if (!gets_out) return 1;
        tw[0x13c] = 1;
        *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
        return 1;
    }
    *reinterpret_cast<float*>(tw + 0x120) = enter;
    std::memcpy(tw + 0x110, plane, 16);
    return 1;
}

PLATFORM_ASM_ENTRY(CM_TraceThroughBrushes_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(CM_TraceThroughBrushes) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CM_TraceThroughBrushes(const uint32_t* model, uint8_t* tw) {
    if (!model) return;
    for (int32_t i = 0; i < static_cast<int32_t>(model[0]); ++i) {
        const uint8_t* brush = reinterpret_cast<const uint8_t*>(uintptr_t{model[1]}) + i * 0x40;
        if (!(*reinterpret_cast<const uint32_t*>(tw + 0xf8) & *reinterpret_cast<const uint32_t*>(brush + 8))) continue;
        if (*reinterpret_cast<const uint32_t*>(tw + 0x160))
            CM_TraceCapsuleThroughBrush(tw, brush);
        else
            CM_TraceBoxThroughBrush(tw, brush);
        // UCOMISS + JNP: stop on an exact 0 (a NaN fraction goes on).
        const float fraction = *reinterpret_cast<const float*>(tw + 0x120);
        if (fraction == title_global<float>(0x0083eafc)) return;
    }
}

PLATFORM_ASM_ENTRY(CM_TraceCapsuleThroughTempBox_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CM_TraceCapsuleThroughTempBox) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CM_TraceCapsuleThroughTempBox(uint8_t* tw) {
    const uint8_t* box = title_global<const uint8_t*>(0x0095f504);
    const float h = title_global<float>(0x0083eb24);  // 0.5
    const Lanes maxs = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes center = mul(add(lanes(reinterpret_cast<const float*>(box + 0x10)), maxs), {{h, h, h, h}});
    const Lanes half = sub(maxs, center);
    float* start = reinterpret_cast<float*>(tw);
    float* end = reinterpret_cast<float*>(tw + 0x10);
    const Lanes moved_start = sub(lanes(start), center);
    std::memcpy(start, moved_start.v, 16);
    const Lanes moved_end = sub(lanes(end), center);
    std::memcpy(end, moved_end.v, 16);
    const float half_z = half.v[2];
    *reinterpret_cast<uint32_t*>(tw + 0x160) = 1;
    const float radius = half.v[0] > half_z ? half_z : half.v[0];
    *reinterpret_cast<float*>(tw + 0x164) = radius;
    *reinterpret_cast<float*>(tw + 0x168) = half_z;
    *reinterpret_cast<float*>(tw + 0x140) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x144) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x148) = half_z - *reinterpret_cast<const float*>(tw + 0x164);
    std::memcpy(tw + 0x150, tw + 0x164, 4);
    std::memcpy(tw + 0x154, tw + 0x164, 4);
    std::memcpy(tw + 0x158, tw + 0x168, 4);
    const uint8_t* leaf = *reinterpret_cast<uint8_t* const*>(box + 4);
    CM_TempBoxModel(reinterpret_cast<const float*>(tw + 0x50), 0, reinterpret_cast<const float*>(tw + 0x40),
                    *reinterpret_cast<const uint32_t*>(leaf + 8));
    const uint8_t* brush = *reinterpret_cast<uint8_t* const*>(box + 4);
    if (*reinterpret_cast<const uint32_t*>(tw + 0x160))
        CM_TraceCapsuleThroughBrush(tw, brush);
    else
        CM_TraceBoxThroughBrush(tw, brush);
}

PLATFORM_ASM_ENTRY(Vec4Normalize_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vec4Normalize) "addl $8, %esp\n"
                   "ret")

long double XB_CDECL Vec4Normalize(const float* v, float* out) {
    const Lanes in = lanes(v);
    const float length_squared = sum_xyz(mul(in, in));
    if (length_squared == 0.0f) {  // UCOMISS + JNP: only an exact 0 (a NaN goes on)
        out[2] = 0.0f;
        out[1] = 0.0f;
        out[0] = 0.0f;
        return title_global<float>(0x0083eafc);
    }
    long double root;
    __asm__ volatile("call *%2"
                     : "=t"(root)
                     : "0"(static_cast<long double>(length_squared)), "r"(uintptr_t{0x0033acf4})  // __CIsqrt
                     : "eax", "ecx", "edx", "memory");
    const float length = static_cast<float>(root);
    const float inverse = title_global<float>(0x0083eb20) / length;  // 1
    const Lanes r = mul(in, splat(inverse));
    std::memcpy(out, r.v, 16);
    return length;
}

PLATFORM_ASM_ENTRY(CM_TraceThroughSphere_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_TraceThroughSphere) "addl $20, %esp\n"
                   "ret")

int32_t XB_CDECL CM_TraceThroughSphere(const float* start, const float* origin, uint8_t* tw, const float* end,
                                       float extra_radius) {
    const Lanes o = lanes(origin);
    const Lanes dir = sub(lanes(start), o);
    const float radius = *reinterpret_cast<const float*>(tw + 0x164) + extra_radius;
    const float radius_squared = radius * radius;
    const Lanes squares = mul(dir, dir);
    platform_keep(squares);
    const float length_squared = (squares.v[0] + squares.v[1]) + squares.v[2];
    const float c = length_squared - radius_squared;
    if (0.0f >= c) {
        *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
        tw[0x13d] = 1;
        float length = length_squared;  // 0 or NaN: copied as is
        if (length_squared == length_squared && length_squared != 0.0f) {  // UCOMISS + JNE
            const float r = platform_rsqrt_estimate(length_squared);
            const float scaled = length_squared * r;
            length = ((title_global<float>(0x0082e660) - scaled * r) * scaled) * title_global<float>(0x0082e670);
        }
        Lanes normal = dir;
        if (!(length == 0.0f)) {  // UCOMISS + JNP: divide unless exactly 0 (all four lanes)
            const Lanes divided = {{dir.v[0] / length, dir.v[1] / length, dir.v[2] / length, dir.v[3] / length}};
            normal = divided;
        }
        std::memcpy(tw + 0x110, normal.v, 16);
        const Lanes to_end = sub(lanes(end), o);
        if (radius_squared >= sum_xyz(mul(to_end, to_end))) tw[0x13c] = 1;
        return 0;
    }
    const float b = sum_xyz(mul(lanes(tw_row(tw, 0xe0)), dir));
    if (b >= 0.0f) return 1;
    const float a = *reinterpret_cast<const float*>(tw + 0xf0);
    const float d = b * b - c * a;
    if (0.0f > d) return 1;
    alignas(16) float in[4];
    alignas(16) float normal[4];
    std::memcpy(in, dir.v, 16);
    long double x = Vec4Normalize(in, normal);
    const float eps = title_global<float>(0x0083ec68);  // 0.125
    const float fraction = *reinterpret_cast<const float*>(tw + 0x120);
    float frac;
    bool hit;
    // x = len * eps / b + (-b - sqrt(d)) / a on the x87 (stored as float); hit when the current fraction is
    // above the unrounded sum.
    __asm__ volatile(
        "fmuls %[eps]\n\t"
        "fdivs %[b]\n\t"
        "flds %[b]\n\t"
        "fchs\n\t"
        "flds %[d]\n\t"
        "fsqrt\n\t"
        ".byte 0xde, 0xe9\n\t"  // fsubp st(1), st (Intel): st1 = st1 - st0
        "fdivs %[a]\n\t"
        ".byte 0xde, 0xc1\n\t"  // faddp st(1), st
        "fsts %[frac]\n\t"
        "flds %[fraction]\n\t"
        "fcomip %%st(1), %%st\n\t"
        "seta %[hit]"
        : "+t"(x), [frac] "=m"(frac), [hit] "=q"(hit)
        : [eps] "m"(eps), [b] "m"(b), [d] "m"(d), [a] "m"(a), [fraction] "m"(fraction)
        : "cc");
    if (!hit) return 1;
    const float stored = frac > 0.0f ? frac : 0.0f;
    std::memcpy(tw + 0x110, normal, 16);
    *reinterpret_cast<float*>(tw + 0x120) = stored;
    const uint8_t* leaf = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f504) + 4);
    *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(leaf + 8);
    return 0;
}

PLATFORM_ASM_ENTRY(CM_TraceThroughVerticalCylinder_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(CM_TraceThroughVerticalCylinder) "addl $16, %esp\n"
                   "ret")

int32_t XB_CDECL CM_TraceThroughVerticalCylinder(uint8_t* tw, const float* origin, float height, float extra_radius) {
    Lanes dir = sub(lanes(tw_row(tw, 0)), lanes(origin));
    const float radius = *reinterpret_cast<const float*>(tw + 0x164) + extra_radius;
    const float dx = dir.v[0], dy = dir.v[1];
    const float c = (dy * dy + dx * dx) - radius * radius;
    auto half_height = [&] {
        return (*reinterpret_cast<const float*>(tw + 0x168) - *reinterpret_cast<const float*>(tw + 0x164)) + height;
    };
    const uint8_t* leaf = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f504) + 4);
    if (0.0f >= c) {
        const float hh = half_height();
        const float dz = dir.v[2];
        if (dz > hh) return 1;
        const float low = 0.0f - hh;
        if (low > dz) return 1;
        dir.v[2] = 0.0f;
        const Lanes squares = mul(dir, dir);
        platform_keep(squares);
        const float length_squared = (squares.v[0] + squares.v[1]) + squares.v[2];
        *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
        tw[0x13d] = 1;
        float length = length_squared;
        if (length_squared == length_squared && length_squared != 0.0f) {  // UCOMISS + JNE
            const float r = platform_rsqrt_estimate(length_squared);
            const float scaled = length_squared * r;
            length = ((title_global<float>(0x0082e660) - scaled * r) * scaled) * title_global<float>(0x0082e670);
        }
        if (!(length == 0.0f)) {
            const Lanes divided = {{dir.v[0] / length, dir.v[1] / length, dir.v[2] / length, dir.v[3] / length}};
            dir = divided;
        }
        std::memcpy(tw + 0x110, dir.v, 16);
        *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(leaf + 8);
        const Lanes to_end = sub(lanes(tw_row(tw, 0x10)), lanes(origin));
        const float end_z = to_end.v[2];
        if (hh >= end_z && end_z >= low) tw[0x13c] = 1;
        return 0;
    }
    const float* delta = tw_row(tw, 0xe0);
    const float b = delta[1] * dy + delta[0] * dx;
    if (b >= 0.0f) return 1;
    const float a = delta[0] * delta[0] + delta[1] * delta[1];
    const float d = b * b - a * c;
    if (0.0f > d) return 1;
    alignas(16) float in[4];
    alignas(16) float normal[4];
    dir.v[2] = 0.0f;
    std::memcpy(in, dir.v, 16);
    long double x = Vec4Normalize(in, normal);
    const float eps = title_global<float>(0x0083ec68);  // 0.125
    const float fraction = *reinterpret_cast<const float*>(tw + 0x120);
    float nudge, frac;
    bool hit;
    // nudge = (float)(len * eps / b); x = (-b - sqrt(d)) / a + nudge on the x87; hit when the current fraction is
    // above the unrounded x.
    __asm__ volatile(
        "fmuls %[eps]\n\t"
        "fdivs %[b]\n\t"
        "fstps %[nudge]\n\t"
        "flds %[b]\n\t"
        "fchs\n\t"
        "flds %[d]\n\t"
        "fsqrt\n\t"
        ".byte 0xde, 0xe9\n\t"  // fsubp st(1), st (Intel): st1 = st1 - st0
        "fdivs %[a]\n\t"
        "fadds %[nudge]\n\t"
        "fsts %[frac]\n\t"
        "flds %[fraction]\n\t"
        "fcomip %%st(1), %%st\n\t"
        "seta %[hit]"
        : "+t"(x), [nudge] "+m"(nudge), [frac] "=m"(frac), [hit] "=q"(hit)
        : [eps] "m"(eps), [b] "m"(b), [d] "m"(d), [a] "m"(a), [fraction] "m"(fraction)
        : "cc");
    if (!hit) return 1;
    const float hh = half_height();
    const float z = (((frac - nudge) * delta[2]) + *reinterpret_cast<const float*>(tw + 8)) - origin[2];
    if (z > hh) return 1;
    if (0.0f - hh > z) return 1;
    const float stored = frac > 0.0f ? frac : 0.0f;
    std::memcpy(tw + 0x110, normal, 16);
    *reinterpret_cast<float*>(tw + 0x120) = stored;
    *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(leaf + 8);
    return 0;
}

void XB_REGS1 XB_CDECL CM_TraceCapsuleThroughCapsule(uint8_t* tw) {
    const uint8_t* box = title_global<const uint8_t*>(0x0095f504);
    const Lanes one = splat(title_global<float>(0x0083eb20));
    const Lanes mins = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes maxs = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes abs_min = sub(lanes(tw_row(tw, 0x20)), one);
    const Lanes abs_max = add(lanes(tw_row(tw, 0x30)), one);
    if (!inside_xyz(sub(mins, abs_max), sub(abs_min, maxs))) return;
    const Lanes offset = lanes(tw_row(tw, 0x140));
    const Lanes start = lanes(tw_row(tw, 0));
    const Lanes end = lanes(tw_row(tw, 0x10));
    alignas(16) float top_start[4], bottom_start[4], top_end[4], bottom_end[4];
    std::memcpy(top_start, add(start, offset).v, 16);
    std::memcpy(bottom_start, sub(start, offset).v, 16);
    std::memcpy(top_end, add(end, offset).v, 16);
    std::memcpy(bottom_end, sub(end, offset).v, 16);
    const float h = title_global<float>(0x0083eb24);  // 0.5
    const Lanes center_lanes = mul(add(mins, maxs), splat(h));
    const Lanes half = sub(maxs, center_lanes);
    const float radius = half.v[0] > half.v[2] ? half.v[2] : half.v[0];
    const float half_height = half.v[2] - radius;
    alignas(16) float center[4], top[4], bottom[4];
    std::memcpy(center, center_lanes.v, 16);
    std::memcpy(top, center, 16);
    std::memcpy(bottom, center, 16);
    top[2] = center[2] + half_height;
    bottom[2] = center[2] - half_height;
    const float delta_z = *reinterpret_cast<const float*>(tw + 0xe8);
    if (bottom_start[2] > top[2]) {
        if (!CM_TraceThroughSphere(bottom_start, top, tw, bottom_end, radius)) return;
        if (delta_z >= title_global<float>(0x0083eafc)) return;
    } else if (bottom[2] > top_start[2]) {
        if (!CM_TraceThroughSphere(top_start, bottom, tw, top_end, radius)) return;
        if (0.0f >= delta_z) return;
    }
    if (!CM_TraceThroughVerticalCylinder(tw, center, half_height, radius)) return;
    if (bottom_end[2] > top[2]) {
        if (!(top[2] >= bottom_start[2])) return;  // COMISS + JB: NaN returns too
        CM_TraceThroughSphere(bottom_start, top, tw, bottom_end, radius);
    } else if (bottom[2] > top_end[2]) {
        if (!(top_start[2] >= bottom[2])) return;
        CM_TraceThroughSphere(top_start, bottom, tw, top_end, radius);
    }
}

PLATFORM_ASM_ENTRY(CM_ModelBrushesBlockSight_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(CM_ModelBrushesBlockSight) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL CM_ModelBrushesBlockSight(const uint32_t* model, uint8_t* tw) {
    if (!model) return 0;
    for (int32_t i = 0; i < static_cast<int32_t>(model[0]); ++i) {
        const uint8_t* brush = reinterpret_cast<const uint8_t*>(model[1]) + i * 0x40;
        if (!(*reinterpret_cast<const uint32_t*>(tw + 0xf8) & *reinterpret_cast<const uint32_t*>(brush + 8))) continue;
        const uint32_t hit = CM_TraceBoxThroughBrush(tw, brush);
        if (hit) return hit;
    }
    return 0;
}

PLATFORM_ASM_ENTRY(CM_TempBoxBlocksSight_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CM_TempBoxBlocksSight) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL CM_TempBoxBlocksSight(uint8_t* tw) {
    const uint8_t* box = title_global<const uint8_t*>(0x0095f504);
    const float h = title_global<float>(0x0083eb24);  // 0.5
    const Lanes maxs = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes center = mul(add(lanes(reinterpret_cast<const float*>(box + 0x10)), maxs), {{h, h, h, h}});
    const Lanes half = sub(maxs, center);
    float* start = reinterpret_cast<float*>(tw);
    float* end = reinterpret_cast<float*>(tw + 0x10);
    const Lanes moved_start = sub(lanes(start), center);
    std::memcpy(start, moved_start.v, 16);
    const Lanes moved_end = sub(lanes(end), center);
    std::memcpy(end, moved_end.v, 16);
    const float half_z = half.v[2];
    *reinterpret_cast<uint32_t*>(tw + 0x160) = 1;
    const float radius = half.v[0] > half_z ? half_z : half.v[0];
    *reinterpret_cast<float*>(tw + 0x164) = radius;
    *reinterpret_cast<float*>(tw + 0x168) = half_z;
    *reinterpret_cast<float*>(tw + 0x140) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x144) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x148) = half_z - *reinterpret_cast<const float*>(tw + 0x164);
    std::memcpy(tw + 0x150, tw + 0x164, 4);
    std::memcpy(tw + 0x154, tw + 0x164, 4);
    std::memcpy(tw + 0x158, tw + 0x168, 4);
    const uint8_t* leaf = *reinterpret_cast<uint8_t* const*>(box + 4);
    CM_TempBoxModel(reinterpret_cast<const float*>(tw + 0x50), 0, reinterpret_cast<const float*>(tw + 0x40),
                    *reinterpret_cast<const uint32_t*>(leaf + 8));
    return CM_TraceBoxThroughBrush(tw, *reinterpret_cast<uint8_t* const*>(box + 4));
}

PLATFORM_ASM_ENTRY(CM_SphereBlocksSight_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_SphereBlocksSight) "addl $16, %esp\n"
                   "ret")

namespace {

// nudge = (float)(b * 0.125) on the x87, as the original stores it.
float sight_nudge(float b) {
    const float eps = title_global<float>(0x0083ec68);  // 0.125
    float nudge;
    __asm__ volatile("flds %[b]\n\t"
                     "fmuls %[eps]\n\t"
                     "fstps %[nudge]"
                     : [nudge] "=m"(nudge)
                     : [b] "m"(b), [eps] "m"(eps));
    return nudge;
}

}  // namespace

int32_t XB_CDECL CM_SphereBlocksSight(const float* origin, const float* start, uint8_t* tw, float extra_radius) {
    const Lanes dir = sub(lanes(start), lanes(origin));
    const Lanes squares = mul(dir, dir);
    platform_keep(squares);
    const float length_squared = (squares.v[0] + squares.v[1]) + squares.v[2];
    const float radius = *reinterpret_cast<const float*>(tw + 0x164) + extra_radius;
    const float c = length_squared - radius * radius;
    if (0.0f >= c) return 0;
    const float b = sum_xyz(mul(lanes(tw_row(tw, 0xe0)), dir));
    if (b >= 0.0f) return 1;
    const float a = *reinterpret_cast<const float*>(tw + 0xf0);
    const float d = b * b - c * a;
    if (0.0f > d) return 1;
    const float nudge = sight_nudge(b);
    alignas(16) float in[4];
    alignas(16) float normal[4];
    std::memcpy(in, dir.v, 16);
    long double x = Vec4Normalize(in, normal);
    const float fraction = *reinterpret_cast<const float*>(tw + 0x120);
    bool hit;
    // x = nudge / len + (-b - sqrt(d)) / a on the x87; blocked when the current fraction is above it.
    __asm__ volatile(
        "fdivrs %[nudge]\n\t"
        "flds %[b]\n\t"
        "fchs\n\t"
        "flds %[d]\n\t"
        "fsqrt\n\t"
        ".byte 0xde, 0xe9\n\t"  // fsubp st(1), st (Intel): st1 = st1 - st0
        "fdivs %[a]\n\t"
        ".byte 0xde, 0xc1\n\t"  // faddp st(1), st
        "flds %[fraction]\n\t"
        "fcomip %%st(1), %%st\n\t"
        "seta %[hit]"
        : "+t"(x), [hit] "=q"(hit)
        : [nudge] "m"(nudge), [b] "m"(b), [d] "m"(d), [a] "m"(a), [fraction] "m"(fraction)
        : "cc");
    return hit ? 0 : 1;
}

PLATFORM_ASM_ENTRY(CM_VerticalCylinderBlocksSight_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(CM_VerticalCylinderBlocksSight) "addl $16, %esp\n"
                   "ret")

int32_t XB_CDECL CM_VerticalCylinderBlocksSight(uint8_t* tw, const float* origin, float height, float extra_radius) {
    Lanes dir = sub(lanes(tw_row(tw, 0)), lanes(origin));
    const float dx = dir.v[0], dy = dir.v[1];
    const float radius = *reinterpret_cast<const float*>(tw + 0x164) + extra_radius;
    const float c = (dx * dx + dy * dy) - radius * radius;
    auto half_height = [&] {
        return (*reinterpret_cast<const float*>(tw + 0x168) - *reinterpret_cast<const float*>(tw + 0x164)) + height;
    };
    if (0.0f >= c) {
        const float hh = half_height();
        const float dz = dir.v[2];
        if (dz > hh) return 1;
        if (0.0f - hh > dz) return 1;
        return 0;
    }
    const float* delta = tw_row(tw, 0xe0);
    const float b = delta[0] * dx + delta[1] * dy;
    if (b >= 0.0f) return 1;
    const float a = *reinterpret_cast<const float*>(tw + 0xf0);
    const float d = b * b - a * c;
    if (0.0f > d) return 1;
    const float nudge = sight_nudge(b);
    alignas(16) float in[4];
    alignas(16) float normal[4];
    dir.v[2] = 0.0f;
    std::memcpy(in, dir.v, 16);
    long double x = Vec4Normalize(in, normal);
    const float fraction = *reinterpret_cast<const float*>(tw + 0x120);
    float scaled, frac;
    bool hit;
    // scaled = (float)(nudge / len); x = (-b - sqrt(d)) / a + scaled on the x87, stored as frac; blocked when the
    // current fraction is above the unrounded x and the hit height is within the half height.
    __asm__ volatile(
        "fdivrs %[nudge]\n\t"
        "fstps %[scaled]\n\t"
        "flds %[b]\n\t"
        "fchs\n\t"
        "flds %[d]\n\t"
        "fsqrt\n\t"
        ".byte 0xde, 0xe9\n\t"  // fsubp st(1), st (Intel): st1 = st1 - st0
        "fdivs %[a]\n\t"
        "fadds %[scaled]\n\t"
        "fsts %[frac]\n\t"
        "flds %[fraction]\n\t"
        "fcomip %%st(1), %%st\n\t"
        "seta %[hit]"
        : "+t"(x), [scaled] "+m"(scaled), [frac] "=m"(frac), [hit] "=q"(hit)
        : [nudge] "m"(nudge), [b] "m"(b), [d] "m"(d), [a] "m"(a), [fraction] "m"(fraction)
        : "cc");
    if (!hit) return 1;
    const float z = (((frac - scaled) * delta[2]) + *reinterpret_cast<const float*>(tw + 8)) - origin[2];
    const float hh = half_height();
    if (z > hh) return 1;
    if (0.0f - hh > z) return 1;
    return 0;
}

int32_t XB_REGS1 XB_CDECL CM_CapsuleBlocksSight(uint8_t* tw) {
    const uint8_t* box = title_global<const uint8_t*>(0x0095f504);
    const Lanes one = splat(title_global<float>(0x0083eb20));
    const Lanes mins = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes maxs = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes abs_min = sub(lanes(tw_row(tw, 0x20)), one);
    const Lanes abs_max = add(lanes(tw_row(tw, 0x30)), one);
    if (!inside_xyz(sub(mins, abs_max), sub(abs_min, maxs))) return 0;
    const Lanes offset = lanes(tw_row(tw, 0x140));
    const Lanes start = lanes(tw_row(tw, 0));
    const Lanes end = lanes(tw_row(tw, 0x10));
    alignas(16) float top_start[4], bottom_start[4], top_end[4], bottom_end[4];
    std::memcpy(top_start, add(start, offset).v, 16);
    std::memcpy(bottom_start, sub(start, offset).v, 16);
    std::memcpy(top_end, add(end, offset).v, 16);
    std::memcpy(bottom_end, sub(end, offset).v, 16);
    const float h = title_global<float>(0x0083eb24);  // 0.5
    const Lanes center_lanes = mul(add(mins, maxs), splat(h));
    const Lanes half = sub(maxs, center_lanes);
    const float radius = half.v[0] > half.v[2] ? half.v[2] : half.v[0];
    const float half_height = half.v[2] - radius;
    alignas(16) float center[4], top[4], bottom[4];
    std::memcpy(center, center_lanes.v, 16);
    std::memcpy(top, center, 16);
    std::memcpy(bottom, center, 16);
    top[2] = center[2] + half_height;
    bottom[2] = center[2] - half_height;
    const float delta_z = *reinterpret_cast<const float*>(tw + 0xe8);
    if (bottom_start[2] > top[2]) {
        if (!CM_SphereBlocksSight(top, bottom_start, tw, radius)) return -1;
        if (delta_z >= title_global<float>(0x0083eafc)) return 0;
    } else if (bottom[2] > top_start[2]) {
        if (!CM_SphereBlocksSight(bottom, top_start, tw, radius)) return -1;
        if (0.0f >= delta_z) return 0;
    }
    if (!CM_VerticalCylinderBlocksSight(tw, center, half_height, radius)) return -1;
    if (bottom_end[2] > top[2]) {
        if (!(top[2] >= bottom_start[2])) return 0;  // COMISS + JB: NaN returns too
        return CM_SphereBlocksSight(top, bottom_start, tw, radius) ? 0 : -1;
    }
    if (!(bottom[2] > top_end[2])) return 0;
    if (!(top_start[2] >= bottom[2])) return 0;
    return CM_SphereBlocksSight(bottom, top_start, tw, radius) ? 0 : -1;
}

uint8_t XB_REGS3 XB_CDECL CM_SegmentOverlapsBox(const float* b, const float* c, const float* a, const float* d) {
    const Lanes la = lanes(a), lb = lanes(b), lc = lanes(c), ld = lanes(d);
    float enter = 0.0f;
    float leave = title_global<float>(0x0083eb20);  // 1
    // Pass 1: the low planes (a).
    const Lanes s0 = sub(la, lb);
    const Lanes enter0 = sub(s0, splat(title_global<float>(0x0083ec68)));  // 0.125
    const Lanes e0 = sub(la, lc);
    const Lanes d0 = sub(s0, e0);
    platform_keep(enter0);
    for (int32_t i = 0; i < 3; ++i) {
        if (s0.v[i] > 0.0f) {
            if (!(enter0.v[i] > d0.v[i] * enter)) continue;
            enter = enter0.v[i] / d0.v[i];
        } else {
            if (!(e0.v[i] > 0.0f)) continue;
            if (!(s0.v[i] > d0.v[i] * leave)) continue;
            leave = s0.v[i] / d0.v[i];
        }
        if (enter >= leave) return 0;
    }
    // Pass 2: the high planes (d).
    const Lanes s1 = sub(lb, ld);
    const Lanes e1 = sub(lc, ld);
    const Lanes d1 = sub(s1, e1);
    for (int32_t i = 0; i < 3; ++i) {
        if (s1.v[i] > 0.0f) {
            if (!(s1.v[i] > d1.v[i] * enter)) continue;
            enter = s1.v[i] / d1.v[i];
        } else {
            if (!(e1.v[i] > 0.0f)) continue;
            if (!(s1.v[i] > d1.v[i] * leave)) continue;
            leave = s1.v[i] / d1.v[i];
        }
        if (enter >= leave) return 0;
    }
    return 1;
}

namespace {

// plane.v + plane.w with the packed ADDPS of the broadcast distance (every lane).
float plane_distance(const Lanes& plane, const Lanes& v) {
    const Lanes product = mul(plane, v);
    Lanes d4 = product;
    d4.v[0] = (product.v[0] + product.v[1]) + product.v[2];
    d4 = add(d4, splat(plane.v[3]));
    platform_keep(d4);
    return d4.v[0];
}

}  // namespace

uint8_t XB_REGS3 XB_CDECL CM_ClipSphereSweepToTriangle(uint32_t, const float* start, const float* v0, float* end,
                                                     const float* dir, float radius, const float* v1, const float* v2,
                                                     const float* plane, uint8_t* startsolid) {
    *startsolid = 0;
    const Lanes e = lanes(end);
    const Lanes s = lanes(start);
    const Lanes sweep = sub(e, s);
    if (title_global<float>(0x0083effc) > sum_xyz(mul(sweep, sweep))) {  // 1e-5
        if (!CM_SphereTouchesTriangle(v2, v1, start, v0, radius, plane)) return 0;
        *startsolid = 1;
        return 1;
    }
    const Lanes n = lanes(plane);
    const float dist_start = plane_distance(n, s);
    const float dist_end = plane_distance(n, e);
    if (radius >= dist_start && dist_start >= dist_end) {
        if (CM_SphereTouchesTriangle(v2, v1, start, v0, radius, plane)) {
            *startsolid = 1;
            return 1;
        }
    }
    const Lanes d = lanes(dir);
    const float dn = sum_xyz(mul(d, n));
    if (dn >= 0.0f) return 0;
    if (0.0f - radius >= dist_start) return 0;
    const Lanes q = sub(s, mul(d, splat(title_global<float>(0x0083ec30))));  // 50
    const float dist_q = plane_distance(n, q);
    alignas(16) float p[4];
    if (dist_q > title_global<float>(0x0083eafc)) {  // 0
        Lanes x;
        if (dist_q > radius) {
            const Lanes q2 = sub(q, mul(n, splat(radius)));
            const float dist_q2 = plane_distance(n, q2);
            const float t = (title_global<float>(0x0083eb34) / dn) * dist_q2;  // -1
            x = add(q2, mul(d, splat(t)));
        } else {
            x = sub(q, mul(n, splat(dist_q)));
        }
        const Lanes a = lanes(v0), b = lanes(v1), c = lanes(v2);
        const float s1 = edge_side(sub(a, b), sub(x, b), n);
        const float s2 = edge_side(sub(b, c), sub(x, c), n);
        const float s3 = edge_side(sub(c, a), sub(x, a), n);
        const float order[4] = {s3, s2, s1, s1};  // SHUFPS lanes; CMPLTPS on all four
        int32_t mask = 0;
        for (int32_t i = 0; i < 4; ++i)
            if (order[i] < 0.0f) mask |= 1 << i;
        if (!(mask & 7)) {
            const Lanes r = add(x, mul(n, splat(radius + title_global<float>(0x0083ed50))));  // 1e-4
            std::memcpy(end, r.v, 16);
            return 1;
        }
        std::memcpy(p, x.v, 16);
    } else {
        const Lanes moved = add(q, mul(n, splat(dist_q)));
        std::memcpy(p, moved.v, 16);
    }
    const float r2 = radius * radius;
    const Lanes w = sub(q, lanes(end));
    const float a = sum_xyz(mul(w, w));
    alignas(16) float closest[4];
    ClosestPointOnTriangle(closest, v0, v1, v2, p);
    const Lanes u = sub(lanes(closest), q);
    const float b = sum_xyz(mul(w, u));
    const float cc = sum_xyz(mul(u, u));
    const float disc = b * b + (r2 - cc) * a;
    if (0.0f >= disc) return 0;
    const float root = platform_x87_sqrt(disc);
    const float nb = b * title_global<float>(0x0083eb34);  // -1
    const float t0 = nb - root;
    if (!(root + nb >= 0.0f)) return 0;
    if (!(a >= t0)) return 0;
    if (!(a * a + title_global<float>(0x0083ed50) > t0)) return 0;
    const float f = 0.0f - t0 / a;
    if (0.0f > f) {
        const Lanes r = add(q, mul(splat(f), w));
        std::memcpy(end, r.v, 16);
    } else {
        std::memcpy(end, start, 16);
    }
    return 1;
}

namespace {

Lanes div_lanes(const Lanes& a, const Lanes& b) {
    return {{a.v[0] / b.v[0], a.v[1] / b.v[1], a.v[2] / b.v[2], a.v[3] / b.v[3]}};
}

// 2 * !(p.x <= c.x) (CMPLESS) + (c.y < p.y) (CMPLTPS on every lane).
int32_t quadtree_quadrant(const Lanes& c, const Lanes& p) {
    int32_t q = !(p.v[0] <= c.v[0]) ? 2 : 0;
    int32_t mask = 0;
    for (int32_t i = 0; i < 4; ++i)
        if (c.v[i] < p.v[i]) mask |= 1 << i;
    return q | ((mask >> 1) & 1);
}

void quadtree_child_center(float* out, const Lanes& c, int32_t quadrant, const Lanes& half) {
    const Lanes offset = lanes(reinterpret_cast<const float*>(uintptr_t{0x0095ff30}) + quadrant * 4);
    const Lanes r = add(c, mul(offset, half));
    std::memcpy(out, r.v, 16);
}

}  // namespace

int32_t XB_CDECL CM_QuadTreeTraceLeafs_r(const uint8_t* tree, int32_t node, const float* p1, const float* p2,
                                         const float* center, float half) {
    const int16_t child = (*reinterpret_cast<const int16_t* const*>(tree + 4))[node];
    if (child <= 0) {
        int32_t& count = title_global<int32_t>(0x00960030);
        uint8_t* record = title_global<uint8_t*>(0x00960034) + count * 0x30;
        std::memcpy(record + 0x10, center, 16);
        *reinterpret_cast<float*>(record + 0x20) = half;
        *reinterpret_cast<const uint8_t**>(record) = *reinterpret_cast<const uint8_t* const*>(tree + 0xc) - child * 8;
        return ++count;
    }
    const Lanes c = lanes(center);
    const Lanes a = lanes(p1);
    const Lanes b = lanes(p2);
    const int32_t q1 = quadtree_quadrant(c, a);
    const int32_t q2 = quadtree_quadrant(c, b);
    const float h = half * title_global<float>(0x0083eb24);  // 0.5
    const Lanes hh = splat(h);
    alignas(16) float child_center[4];
    if (q1 == q2) {
        quadtree_child_center(child_center, c, q1, hh);
        return CM_QuadTreeTraceLeafs_r(tree, child + q1, p1, p2, child_center, h);
    }
    const Lanes d = sub(b, a);
    alignas(16) float mid[4];
    if (!((q1 ^ q2) & 2) || !((q1 ^ q2) & 1)) {
        float t;
        if (!((q1 ^ q2) & 2)) {  // only y differs: packed divide, the y lane
            const Lanes t4 = div_lanes(sub(c, a), d);
            platform_keep(t4);
            t = t4.v[1];
        } else {
            t = (c.v[0] - a.v[0]) / (b.v[0] - a.v[0]);
        }
        const Lanes m = add(a, mul(d, splat(t)));
        std::memcpy(mid, m.v, 16);
        quadtree_child_center(child_center, c, q1, hh);
        CM_QuadTreeTraceLeafs_r(tree, child + q1, p1, mid, child_center, h);
        quadtree_child_center(child_center, c, q2, hh);
        return CM_QuadTreeTraceLeafs_r(tree, child + q2, mid, p2, child_center, h);
    }
    const float tx = (c.v[0] - a.v[0]) / (b.v[0] - a.v[0]);
    const Lanes t4 = div_lanes(sub(c, a), d);
    platform_keep(t4);
    const float ty = t4.v[1];
    const float t1 = ty > tx ? tx : ty;
    const float t2 = (ty + tx) - t1;
    const Lanes m1 = add(a, mul(d, splat(t1)));
    std::memcpy(mid, m1.v, 16);
    quadtree_child_center(child_center, c, q1, hh);
    CM_QuadTreeTraceLeafs_r(tree, child + q1, p1, mid, child_center, h);
    if (!(title_global<float>(0x0083eb20) > t2)) {  // 1
        quadtree_child_center(child_center, c, q2, hh);
        return CM_QuadTreeTraceLeafs_r(tree, child + q2, mid, p2, child_center, h);
    }
    alignas(16) float mid2[4];
    const Lanes m2 = add(lanes(p1), mul(sub(lanes(p2), lanes(p1)), splat(t2)));
    std::memcpy(mid2, m2.v, 16);
    const int32_t qm = t1 == tx ? q1 ^ 2 : q1 ^ 1;  // UCOMISS: equal crosses x first; NaN crosses y
    quadtree_child_center(child_center, c, qm, hh);
    CM_QuadTreeTraceLeafs_r(tree, child + qm, mid, mid2, child_center, h);
    quadtree_child_center(child_center, c, q2, hh);
    return CM_QuadTreeTraceLeafs_r(tree, child + q2, mid2, p2, child_center, h);
}

namespace {

Lanes min_lanes(const Lanes& a, const Lanes& b) {  // MINPS: the second operand on NaN
    Lanes r;
    for (int32_t i = 0; i < 4; ++i) r.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i];
    return r;
}

Lanes max_lanes(const Lanes& a, const Lanes& b) {  // MAXPS: the second operand on NaN
    Lanes r;
    for (int32_t i = 0; i < 4; ++i) r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
    return r;
}

}  // namespace

uint8_t XB_CDECL CM_TraceRayThroughWorldTrees(uint8_t* tw, const float* start, const float* end, float* out,
                                              uint32_t* surface_flags, uint32_t* contents) {
    const Lanes s = lanes(start);
    const Lanes e = lanes(end);
    const Lanes d = sub(e, s);
    float best = sum_xyz(mul(d, d));
    if (title_global<float>(0x0083eb4c) > best) return 0;  // 0.001
    std::memcpy(out, end, 16);
    Lanes lo = min_lanes(s, e);
    Lanes hi = max_lanes(s, e);
    uint8_t hit = 0;
    int32_t& checkcount = cm_field<int32_t>(0x60);
    alignas(16) float mesh_out[8] = {};
    for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0095f4fc) + 0xc); ++k) {
        uint8_t* tree = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f4fc) + 0x10 + k * 4);
        const float* tree_mins = reinterpret_cast<const float*>(tree + 0x40);
        const float* tree_maxs = reinterpret_cast<const float*>(tree + 0x50);
        if (!inside_xyz(sub(lanes(tree_mins), hi), sub(lo, lanes(tree_maxs)))) continue;
        if (!CM_SegmentOverlapsBox(start, end, tree_mins, tree_maxs)) continue;
        title_global<int32_t>(0x00960030) = 0;
        title_global<uint32_t>(0x00960034) = 0x009638c0;
        CM_QuadTreeTraceLeafs_r(tree, 0, start, end, reinterpret_cast<const float*>(tree + 0x10),
                                *reinterpret_cast<const float*>(tree + 0x20));
        for (uint32_t j = 0; j < title_global<uint32_t>(0x00960030); ++j) {
            const uint8_t* record = title_global<const uint8_t*>(0x00960034) + j * 0x30;
            const float cell_half = *reinterpret_cast<const float*>(record + 0x20);
            const float* cell_center = reinterpret_cast<const float*>(record + 0x10);
            const uint8_t* leaf = *reinterpret_cast<const uint8_t* const*>(record);
            const int32_t n = *reinterpret_cast<const int32_t*>(leaf);
            for (int32_t i = 0; i < n; ++i) {
                const uint32_t index = (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[i];
                uint8_t* object = leaf_object(tree, index);
                int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
                if (mark == checkcount) continue;
                if (!(*reinterpret_cast<const uint32_t*>(object + 8) & *reinterpret_cast<const uint32_t*>(tw + 0xf8)))
                    continue;
                mark = checkcount;
                const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
                const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
                alignas(16) float omin[4], omax[4];
                const Lanes lo_o = sub(c, h);
                const Lanes hi_o = add(c, h);
                std::memcpy(omin, lo_o.v, 16);
                std::memcpy(omax, hi_o.v, 16);
                if (!inside_xyz(sub(lo_o, hi), sub(lo, hi_o))) {
                    const Lanes reach = sub(hi, lo_o);
                    platform_keep(reach);
                    if (!(0.0f <= reach.v[0])) break;  // CMPLESS on the x lane
                    continue;
                }
                if (!CM_SegmentOverlapsBox(start, end, omin, omax)) continue;
                const uint32_t split = *reinterpret_cast<const uint32_t*>(tree + 0x30);
                if (index >= split) {
                    if (CM_TraceBoxThroughBrush(tw, object)) {
                        const Lanes ts = lanes(tw_row(tw, 0));
                        const Lanes p = add(ts, mul(sub(lanes(tw_row(tw, 0x10)), ts), splat(*reinterpret_cast<const float*>(tw + 0x120))));
                        const Lanes to_p = sub(ts, p);
                        const float distance = sum_xyz(mul(to_p, to_p));
                        if (best > distance) {
                            std::memcpy(out, p.v, 16);
                            std::memcpy(out + 4, tw + 0x110, 16);
                            best = distance;
                            *surface_flags = *reinterpret_cast<const uint32_t*>(object + 4);
                            *contents = *reinterpret_cast<const uint32_t*>(object + 8);
                            const Lanes hit_point = lanes(out);
                            lo = min_lanes(lanes(start), hit_point);
                            hi = max_lanes(lanes(start), hit_point);
                            const Lanes normal = lanes(out + 4);
                            int32_t zero = 0;
                            for (int32_t a = 0; a < 4; ++a)
                                if (normal.v[a] == 0.0f) zero |= 1 << a;  // CMPEQPS
                            if ((zero & 7) != 7) hit = 1;
                        }
                    }
                    // UCOMISS + JP: only an exact 0 ends the trace.
                    if (*reinterpret_cast<const float*>(tw + 0x120) == title_global<float>(0x0083eafc)) return 1;
                    continue;
                }
                uint32_t unused = 0;
                const uint32_t slot = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<uint8_t* const*>(object) + 0x14);
                using MeshTrace = uint8_t XB_THISCALL(uint8_t*, const float*, const float*, float*, uint32_t*, uint32_t);
                if (!title_function<MeshTrace>(slot)(object, start, end, mesh_out, &unused, 0)) continue;
                const Lanes to_p = sub(lanes(start), lanes(mesh_out));
                const float distance = sum_xyz(mul(to_p, to_p));
                if (!(best > distance)) continue;
                std::memcpy(out, mesh_out, 32);
                *surface_flags = *reinterpret_cast<const uint32_t*>(object + 4);
                *contents = *reinterpret_cast<const uint32_t*>(object + 8);
                best = distance;
                const Lanes hit_point = lanes(out);
                lo = min_lanes(lanes(start), hit_point);
                hi = max_lanes(lanes(start), hit_point);
                hit = 1;
            }
            if (!hit) continue;
            const Lanes cc = lanes(cell_center);
            const Lanes ch = splat(cell_half);
            const Lanes p = lanes(out);
            const Lanes below = sub(sub(cc, ch), p);
            const Lanes above = sub(p, add(cc, ch));
            int32_t mask = 0;
            for (int32_t a = 0; a < 4; ++a) {
                const float m = below.v[a] > above.v[a] ? below.v[a] : above.v[a];
                if (m < 0.0f) mask |= 1 << a;
            }
            if ((mask & 3) == 3) break;  // the hit is inside this cell (x, y): the next tree
        }
    }
    return hit;
}

PLATFORM_ASM_ENTRY(CM_TraceThroughWorldTrees_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CM_TraceThroughWorldTrees) "addl $4, %esp\n"
                   "ret")

namespace {

// RSQRTSS with one Newton step; 0 and NaN are copied (UCOMISS + JNE).
float length_from_squared(float squared) {
    if (!(squared == squared && squared != 0.0f)) return squared;
    const float r = platform_rsqrt_estimate(squared);
    const float scaled = squared * r;
    return ((title_global<float>(0x0082e660) - scaled * r) * scaled) * title_global<float>(0x0082e670);  // 3, 0.5
}

}  // namespace

uint8_t XB_CDECL CM_TraceThroughWorldTrees(uint8_t* tw) {
    alignas(16) float start[4], end[4];
    std::memcpy(start, tw, 16);
    std::memcpy(end, tw + 0x10, 16);
    const Lanes s = lanes(start);
    const Lanes e = lanes(end);
    const Lanes sweep = sub(e, s);
    const float length = platform_x87_sqrt(sum_xyz(mul(sweep, sweep)));
    int32_t& checkcount = cm_field<int32_t>(0x60);
    const Lanes extent = lanes(tw_row(tw, 0x150));
    const Lanes lo = sub(min_lanes(s, e), extent);
    const Lanes hi = add(max_lanes(s, e), extent);
    uint8_t hit = 0;
    float* const tw_start_z = reinterpret_cast<float*>(tw + 8);
    float* const tw_end_z = reinterpret_cast<float*>(tw + 0x18);
    const uint8_t* world = title_global<const uint8_t*>(0x0095f4fc);
    for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(world + 0xc); ++k, world = title_global<const uint8_t*>(0x0095f4fc)) {
        uint8_t* tree = *reinterpret_cast<uint8_t* const*>(world + 0x10 + k * 4);
        if (!inside_xyz(sub(lanes(reinterpret_cast<const float*>(tree + 0x40)), hi),
                        sub(lo, lanes(reinterpret_cast<const float*>(tree + 0x50)))))
            continue;
        title_global<int32_t>(0x00960030) = 0;
        title_global<uint32_t>(0x00960034) = 0x009638c0;
        CM_QuadTreeTraceLeafs_r(tree, 0, start, end, reinterpret_cast<const float*>(tree + 0x10),
                                *reinterpret_cast<const float*>(tree + 0x20));
        for (uint32_t j = 0; j < title_global<uint32_t>(0x00960030); ++j) {
            const uint8_t* leaf = *reinterpret_cast<const uint8_t* const*>(title_global<const uint8_t*>(0x00960034) + j * 0x30);
            const int32_t n = *reinterpret_cast<const int32_t*>(leaf);
            // Pass 1: brushes (type 9).
            for (int32_t i = 0; i < n; ++i) {
                uint8_t* object = leaf_object(tree, (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[i]);
                if (object_type(object) != 9) continue;
                int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
                if (mark == checkcount) continue;
                if (!(*reinterpret_cast<const uint32_t*>(object + 8) & *reinterpret_cast<const uint32_t*>(tw + 0xf8))) continue;
                mark = checkcount;
                const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
                const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
                const Lanes below = sub(lo, c);
                const Lanes above = sub(c, hi);
                int32_t inside = 0;
                for (int32_t a = 0; a < 4; ++a) {
                    const float m = above.v[a] > below.v[a] ? above.v[a] : below.v[a];  // MAXPS(above, below)
                    if (m < h.v[a]) inside |= 1 << a;
                }
                if ((inside & 7) != 7) {
                    const Lanes reach = add(sub(hi, c), h);
                    platform_keep(reach);
                    if (!(0.0f <= reach.v[0])) break;  // CMPLESS on the x lane
                    continue;
                }
                if (*reinterpret_cast<const uint32_t*>(tw + 0x160))
                    CM_TraceCapsuleThroughBrush(tw, object);
                else
                    CM_TraceBoxThroughBrush(tw, object);
                // UCOMISS + JNP: an exact fraction 0 ends the trace.
                if (*reinterpret_cast<const float*>(tw + 0x120) == title_global<float>(0x0083eafc)) {
                    *reinterpret_cast<uint32_t*>(tw + 0x124) = *reinterpret_cast<const uint32_t*>(object + 4);
                    *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(object + 8);
                    return 1;
                }
            }
            // Pass 2: triangle meshes (every other type), the sphere moved down to the capsule's lower end.
            const float radius = *reinterpret_cast<const uint32_t*>(tw + 0x160) ? *reinterpret_cast<const float*>(tw + 0x164)
                                                                                 : *reinterpret_cast<const float*>(tw + 0x50);
            const float saved_start_z = *tw_start_z;
            const float saved_end_z = *tw_end_z;
            const float drop = *reinterpret_cast<const float*>(tw + 0x168) - radius;
            *tw_start_z = *tw_start_z - drop;
            *tw_end_z = *tw_end_z - drop;
            const Lanes ss = lanes(tw_row(tw, 0));
            const Lanes se = lanes(tw_row(tw, 0x10));
            const Lanes rr4 = splat(radius);
            const Lanes lo2 = sub(min_lanes(ss, se), rr4);
            const Lanes hi2 = add(max_lanes(ss, se), rr4);
            for (int32_t i = 0; i < n; ++i) {
                uint8_t* object = leaf_object(tree, (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[i]);
                if (object_type(object) == 9) continue;
                int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
                if (mark == checkcount) continue;
                if (!(*reinterpret_cast<const uint32_t*>(object + 8) & *reinterpret_cast<const uint32_t*>(tw + 0xf8))) continue;
                mark = checkcount;
                const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
                const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
                const Lanes below = sub(lo2, c);
                const Lanes above = sub(c, hi2);
                int32_t inside = 0;
                for (int32_t a = 0; a < 4; ++a) {
                    const float m = above.v[a] > below.v[a] ? above.v[a] : below.v[a];
                    if (m < h.v[a]) inside |= 1 << a;
                }
                if ((inside & 7) != 7) {
                    const Lanes reach = add(sub(hi2, c), h);
                    platform_keep(reach);
                    if (!(0.0f <= reach.v[0])) break;
                    continue;
                }
                alignas(16) float a_pt[4], b_pt[4], dir[4], walk_lo[4], walk_hi[4];
                std::memcpy(a_pt, tw, 16);
                std::memcpy(b_pt, tw + 0x10, 16);
                const Lanes a4 = lanes(a_pt);
                Lanes d = sub(lanes(b_pt), a4);
                {
                    const Lanes dn = div_lanes(d, splat(platform_x87_sqrt(sum_xyz(mul(d, d)))));
                    platform_keep(dn);
                    std::memcpy(dir, dn.v, 16);
                }
                uint16_t* stack = reinterpret_cast<uint16_t*>(uintptr_t{0x0090a020});
                stack[1] = *reinterpret_cast<const uint16_t*>(object + 0x40);
                stack[0] = 0;
                stack[2] = 0;
                title_global<uint16_t>(0x0090a620) = 3;
                const Lanes wl = sub(min_lanes(a4, lanes(b_pt)), rr4);
                const Lanes wh = add(max_lanes(a4, lanes(b_pt)), rr4);
                std::memcpy(walk_lo, wl.v, 16);
                std::memcpy(walk_hi, wh.v, 16);
                uint16_t node = CM_BoxTreeStack_Next(object, walk_lo, walk_hi);
                if (node == 0xffff) continue;
                const float rr = radius + title_global<float>(0x0083eb4c);  // 0.001
                for (; node != 0xffff; node = CM_BoxTreeStack_Next(object, walk_lo, walk_hi)) {
                    alignas(16) float plane[4];
                    std::memcpy(plane, *reinterpret_cast<uint8_t* const*>(object + 0x44) + node * 16, 16);
                    const Lanes p = lanes(plane);
                    const Lanes ap = lanes(a_pt);
                    const Lanes bp = lanes(b_pt);
                    const float dist_a = plane_distance(p, ap);
                    const float dist_b = plane_distance(p, bp);
                    if (dist_a > rr && dist_b > rr) continue;
                    const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(object + 0x34);
                    const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(object + 0x3c))[node];
                    alignas(16) float v0[4], v1[4], v2[4];
                    std::memcpy(v0, rows + (flags & 0x3f) * 16, 16);
                    std::memcpy(v1, rows + ((flags >> 6) & 0x3f) * 16, 16);
                    std::memcpy(v2, rows + ((flags >> 12) & 0x3f) * 16, 16);
                    const Lanes rrr = splat(rr);
                    const Lanes blo = sub(min_lanes(ap, bp), rrr);
                    const Lanes bhi = add(max_lanes(ap, bp), rrr);
                    const Lanes l0 = lanes(v0), l1 = lanes(v1), l2 = lanes(v2);
                    const Lanes tmin = min_lanes(l0, min_lanes(l1, l2));
                    const Lanes tmax = max_lanes(l0, max_lanes(l1, l2));
                    if (!inside_xyz(sub(tmin, bhi), sub(blo, tmax))) continue;
                    uint8_t solid;
                    if (!CM_ClipSphereSweepToTriangle(0, a_pt, v0, b_pt, dir, rr, v1, v2, plane, &solid)) continue;
                    if (solid) {
                        std::memcpy(tw + 0x110, plane, 16);
                        *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
                        tw[0x13d] = 1;
                        *reinterpret_cast<uint32_t*>(tw + 0x124) = *reinterpret_cast<const uint32_t*>(object + 4);
                        *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(object + 8);
                        *tw_start_z = saved_start_z;
                        *tw_end_z = saved_end_z;
                        return 1;
                    }
                    d = sub(lanes(b_pt), lanes(a_pt));
                    const float frac = length_from_squared(sum_xyz(mul(d, d))) / length;
                    const float min_frac = title_global<float>(0x0083effc);  // 1e-5
                    if (min_frac >= frac) {
                        std::memcpy(tw + 0x110, plane, 16);
                        *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
                        tw[0x13d] = 1;
                        *reinterpret_cast<uint32_t*>(tw + 0x124) = *reinterpret_cast<const uint32_t*>(object + 4);
                        *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(object + 8);
                        *tw_start_z = saved_start_z;
                        *tw_end_z = saved_end_z;
                        return 1;
                    }
                    if (frac > *reinterpret_cast<const float*>(tw + 0x120)) continue;
                    *reinterpret_cast<float*>(tw + 0x120) = frac - min_frac;
                    const float new_length = platform_x87_sqrt(sum_xyz(mul(d, d)));
                    std::memcpy(tw + 0x110, plane, 16);
                    *reinterpret_cast<uint32_t*>(tw + 0x124) = *reinterpret_cast<const uint32_t*>(object + 4);
                    *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(object + 8);
                    hit = 1;
                    const Lanes dn = div_lanes(d, splat(new_length));
                    platform_keep(dn);
                    std::memcpy(dir, dn.v, 16);
                }
            }
            *tw_start_z = saved_start_z;
            *tw_end_z = saved_end_z;
        }
    }
    return hit;
}


PLATFORM_ASM_ENTRY(CM_TraceThroughWorld_entry,
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_TraceThroughWorld) "addl $12, %esp\n"
                   "ret")

void XB_CDECL CM_TraceThroughWorld(uint8_t* tw, const float* start, const float* end) {
    ++cm_field<int32_t>(0x60);  // checkcount
    if (!*reinterpret_cast<const uint32_t*>(tw + 0xfc)) {
        CM_TraceThroughWorldTrees(tw);
        return;
    }
    const Lanes s = lanes(start);
    const Lanes d = sub(lanes(end), s);
    const float length = length_from_squared(sum_xyz(mul(d, d)));
    alignas(16) float out[8] = {};  // never-set words: zeroed dead stack in tests
    uint32_t surface_flags = 0, contents = 0;
    if (!CM_TraceRayThroughWorldTrees(tw, start, end, out, &surface_flags, &contents)) return;
    *reinterpret_cast<uint32_t*>(tw + 0x124) = surface_flags;
    *reinterpret_cast<uint32_t*>(tw + 0x128) = contents;
    const Lanes p = lanes(out);
    const Lanes to_hit = sub(p, s);
    const float fraction = length_from_squared(sum_xyz(mul(to_hit, to_hit))) / length;
    if (!(*reinterpret_cast<const float*>(tw + 0x120) > fraction)) return;
    const Lanes n = lanes(out + 4);
    uint32_t bits;
    const float dist = sum_xyz(mul(p, n));
    std::memcpy(&bits, &dist, 4);
    const float neg_dist = from_bits(bits ^ 0x80000000u);  // XORPS sign
    const float ds = sum_xyz(mul(s, n)) + neg_dist;
    const float de = sum_xyz(mul(lanes(end), n)) + neg_dist;
    *reinterpret_cast<float*>(tw + 0x120) = (ds - title_global<float>(0x0083ec68)) / (ds - de);  // 0.125
    std::memcpy(tw + 0x110, out + 4, 16);
}

uint32_t XB_REGS3 XB_CDECL CM_BoxTrace(const uint8_t* capsule, uint32_t, const float* maxs, uint8_t* results,
                                       const float* start, const float* end, const float* mins, const uint32_t* model,
                                       uint32_t brushmask, uint32_t capsule_flag) {
    if (!capsule) *reinterpret_cast<float*>(results + 0x20) = title_global<float>(0x0083eb20);  // 1
    ++cm_field<int32_t>(0x60);  // checkcount
    alignas(16) uint8_t tw[0x170];
    Com_Memset(tw, 0, 0x170);
    std::memcpy(tw + 0x120, results + 0x20, 4);
    *reinterpret_cast<uint32_t*>(tw + 0xf8) = brushmask;
    const Lanes offset = mul(add(lanes(mins), lanes(maxs)), splat(title_global<float>(0x0083eb24)));  // 0.5
    const Lanes size0 = sub(lanes(mins), offset);
    const Lanes size1 = sub(lanes(maxs), offset);
    const Lanes s = add(lanes(start), offset);
    const Lanes e = add(lanes(end), offset);
    const Lanes delta = sub(e, s);
    std::memcpy(tw + 0xe0, delta.v, 16);
    const float length_squared = sum_xyz(mul(delta, delta));
    std::memcpy(tw + 0x40, size0.v, 16);
    std::memcpy(tw + 0x50, size1.v, 16);
    std::memcpy(tw, s.v, 16);
    std::memcpy(tw + 0x10, e.v, 16);
    std::memcpy(tw + 0xf0, &length_squared, 4);
    float radius;
    uint32_t flag;
    if (capsule) {
        std::memcpy(tw + 0x140, capsule, 0x30);
        std::memcpy(&radius, tw + 0x164, 4);
        std::memcpy(&flag, tw + 0x160, 4);
    } else {
        flag = capsule_flag;
        *reinterpret_cast<uint32_t*>(tw + 0x160) = flag;
        const float half_z = size1.v[2];
        radius = size1.v[0] > half_z ? half_z : size1.v[0];
        *reinterpret_cast<float*>(tw + 0x140) = 0.0f;
        *reinterpret_cast<float*>(tw + 0x144) = 0.0f;
        *reinterpret_cast<float*>(tw + 0x164) = radius;
        *reinterpret_cast<float*>(tw + 0x168) = half_z;
        *reinterpret_cast<float*>(tw + 0x148) = half_z - radius;
    }
    const float* sign = reinterpret_cast<const float*>(uintptr_t{0x0082e0b0});
    Lanes abs_offset;
    for (int32_t i = 0; i < 4; ++i) {  // ANDNPS with the sign mask
        uint32_t mask, bits;
        std::memcpy(&mask, sign + i, 4);
        std::memcpy(&bits, tw + 0x140 + i * 4, 4);
        abs_offset.v[i] = from_bits(~mask & bits);
    }
    const Lanes capsule_size = add(abs_offset, splat(radius));
    Lanes lo = min_lanes(s, e);
    Lanes hi = max_lanes(s, e);
    if (flag) {
        lo = sub(lo, capsule_size);
        hi = add(hi, capsule_size);
    } else {
        lo = add(lo, size0);
        hi = add(hi, size1);
    }
    std::memcpy(tw + 0x20, lo.v, 16);
    std::memcpy(tw + 0x30, hi.v, 16);
    for (int32_t corner = 0; corner < 8; ++corner) {  // +0x60: x/y/z from size1 when bit 0/1/2 is set (w stays 0)
        float* row = reinterpret_cast<float*>(tw + 0x60 + corner * 0x10);
        row[0] = (corner & 1) ? size1.v[0] : size0.v[0];
        row[1] = (corner & 2) ? size1.v[1] : size0.v[1];
        row[2] = (corner & 4) ? size1.v[2] : size0.v[2];
    }
    const uint8_t* temp_leaf = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f504) + 4);
    // UCOMISS + JP: a position test only when every component is exactly equal.
    if (start[0] == end[0] && start[1] == end[1] && start[2] == end[2]) {
        if (!model) {
            CM_TestInWorldTrees(tw);
        } else if (model[0x48 / 4] != 0x1fe) {
            CM_TestInBrushes(model, tw);
        } else if (*reinterpret_cast<const uint32_t*>(temp_leaf + 8) & brushmask) {
            if (flag)
                CM_TestCapsuleInCapsule(tw);
            else
                CM_TestCapsuleInTempBox(tw);
        }
    } else {
        *reinterpret_cast<uint32_t*>(tw + 0xfc) = (size1.v[1] + size1.v[2]) + size1.v[0] == title_global<float>(0x0083eafc);
        if (flag) std::memcpy(tw + 0x150, capsule_size.v, 16);
        if (!model) {
            CM_TraceThroughWorld(tw, reinterpret_cast<const float*>(tw), reinterpret_cast<const float*>(tw + 0x10));
        } else if (model[0x48 / 4] != 0x1fe) {
            CM_TraceThroughBrushes(model, tw);
        } else if (*reinterpret_cast<const uint32_t*>(temp_leaf + 8) & brushmask) {
            if (flag)
                CM_TraceCapsuleThroughCapsule(tw);
            else
                CM_TraceCapsuleThroughTempBox(tw);
        }
    }
    const Lanes endpos = add(lanes(start), mul(lanes(tw_row(tw, 0xe0)), splat(*reinterpret_cast<const float*>(tw + 0x120))));
    std::memcpy(tw + 0x100, endpos.v, 16);
    std::memcpy(results, tw + 0x100, 0x40);
    uint32_t z;
    std::memcpy(&z, start + 2, 4);
    return z;  // EAX = start z bits
}


const float* XB_REGS3 XB_CDECL CM_TransformedBoxTrace(const float* origin, uint32_t, const float* maxs, uint8_t* results,
                                                      const float* start, const float* end, const float* mins,
                                                      const uint32_t* model, uint32_t brushmask, const float* angles,
                                                      uint32_t capsule_flag) {
    alignas(16) uint8_t trace[0x40] = {};
    const Lanes offset = mul(add(lanes(mins), lanes(maxs)), splat(title_global<float>(0x0083eb24)));  // 0.5
    alignas(16) float size0[4], size1[4], local_start[4], local_end[4];
    const Lanes s0 = sub(lanes(mins), offset);
    const Lanes s1 = sub(lanes(maxs), offset);
    std::memcpy(size0, s0.v, 16);
    std::memcpy(size1, s1.v, 16);
    const Lanes o = lanes(origin);
    const Lanes ls = sub(add(lanes(start), offset), o);
    const Lanes le = sub(add(lanes(end), offset), o);
    std::memcpy(local_start, ls.v, 16);
    std::memcpy(local_end, le.v, 16);
    // UCOMISS + JP: rotate when an angle is non-zero or NaN.
    const bool rotate = model && model[0x48 / 4] != 0x1ff &&
                        (!(angles[0] == 0.0f) || !(angles[1] == 0.0f) || !(angles[2] == 0.0f));
    // The capsule block CM_BoxTrace copies to trace work +0x140 (axis offset, never-set size, flag, radius, half
    // height); its unset words are the zeroed dead stack in tests.
    alignas(16) float block[12] = {};
    alignas(16) float rows[12] = {};  // w words never set
    std::memcpy(&block[8], &capsule_flag, 4);
    const float radius = size1[0] > size1[2] ? size1[2] : size1[0];
    block[10] = size1[2];
    block[9] = radius;
    const float half_height = size1[2] - radius;
    if (rotate) {
        float forward[3], right[3], up[3];
        uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles)),
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward))};
        AnglesToAxis(args, right, up);
        const float neg_right_z = 0.0f - right[2];
        rows[0] = forward[0];
        rows[1] = forward[1];
        rows[2] = forward[2];
        rows[4] = 0.0f - right[0];
        rows[5] = 0.0f - right[1];
        rows[6] = neg_right_z;
        rows[8] = up[0];
        rows[9] = up[1];
        rows[10] = up[2];
        Mat34_RotateVector(local_start, 0, rows);
        Mat34_RotateVector(local_end, 0, rows);
        block[0] = forward[2] * half_height;
        block[1] = 0.0f - neg_right_z * half_height;
        block[2] = up[2] * half_height;
    } else {
        block[0] = 0.0f;
        block[1] = 0.0f;
        block[2] = half_height;
    }
    std::memcpy(trace + 0x20, results + 0x20, 4);
    CM_BoxTrace(reinterpret_cast<const uint8_t*>(block), 0, size1, trace, local_start, local_end, size0, model, brushmask,
                capsule_flag);
    float fraction;
    std::memcpy(&fraction, trace + 0x20, 4);
    if (rotate && *reinterpret_cast<const float*>(results + 0x20) > fraction) {
        Mat34_Transpose3(block, 0, rows);
        alignas(16) float normal[4];
        std::memcpy(normal, trace + 0x10, 16);
        Mat34_RotateVector(normal, 0, block);
        std::memcpy(trace + 0x10, normal, 16);
    }
    const Lanes s = lanes(start);
    const Lanes endpos = add(s, mul(splat(fraction), sub(lanes(end), s)));
    std::memcpy(trace, endpos.v, 16);
    std::memcpy(results, trace, 0x40);
    return end;
}

void XB_REGS1 XB_CDECL CM_TestInCandidates(uint8_t* tw, const uint8_t* triangles, const uint8_t* brushes) {
    const int32_t brush_count = *reinterpret_cast<const int32_t*>(brushes + 0x804);
    for (int32_t i = 0; i < brush_count; ++i) {
        CM_TestBoxInBrush(tw, (*reinterpret_cast<const uint8_t* const* const*>(brushes + 0x800))[i]);
        if (tw[0x13c]) return;
    }
    const int32_t n = *reinterpret_cast<const int32_t*>(triangles + 0x1004);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* entry = *reinterpret_cast<const uint8_t* const*>(triangles + 0x1000) + i * 8;
        const uint8_t* mesh = *reinterpret_cast<const uint8_t* const*>(entry);
        const uint32_t triangle = *reinterpret_cast<const uint32_t*>(entry + 4);
        const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(mesh + 0x34);
        const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(mesh + 0x3c))[triangle];
        alignas(16) float plane[4], v0[4], v1[4], v2[4];
        std::memcpy(plane, *reinterpret_cast<uint8_t* const*>(mesh + 0x44) + triangle * 16, 16);
        std::memcpy(v0, rows + (flags & 0x3f) * 16, 16);
        std::memcpy(v1, rows + ((flags >> 6) & 0x3f) * 16, 16);
        std::memcpy(v2, rows + ((flags >> 12) & 0x3f) * 16, 16);
        if (CM_SphereTouchesTriangle(v2, v1, reinterpret_cast<const float*>(tw), v0,
                                     *reinterpret_cast<const float*>(tw + 0x164), plane)) {
            tw[0x13c] = 1;
            tw[0x13d] = 1;
            *reinterpret_cast<float*>(tw + 0x120) = 0.0f;
            return;
        }
    }
}

PLATFORM_ASM_ENTRY(CM_TraceThroughCandidates_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(CM_TraceThroughCandidates) "addl $12, %esp\n"
                   "ret")

uint8_t XB_CDECL CM_TraceThroughCandidates(uint8_t* tw, const uint8_t* triangles, const uint8_t* brushes) {
    uint8_t hit = 0;
    float* const fraction = reinterpret_cast<float*>(tw + 0x120);
    const int32_t brush_count = *reinterpret_cast<const int32_t*>(brushes + 0x804);
    for (int32_t i = 0; i < brush_count; ++i) {
        const uint8_t* brush = (*reinterpret_cast<const uint8_t* const* const*>(brushes + 0x800))[i];
        const float saved = *fraction;
        CM_TraceCapsuleThroughBrush(tw, brush);
        // UCOMISS + JNP: an exact fraction 0 ends the trace.
        if (*fraction == title_global<float>(0x0083eafc)) {
            *reinterpret_cast<uint32_t*>(tw + 0x124) = *reinterpret_cast<const uint32_t*>(brush + 4);
            *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(brush + 8);
            return 1;
        }
        if (saved > *fraction) {
            *reinterpret_cast<uint32_t*>(tw + 0x124) = *reinterpret_cast<const uint32_t*>(brush + 4);
            *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(brush + 8);
        }
    }
    // Triangles: the sphere at the capsule's lower end (start/end z - (half height - radius)); the w lanes of the
    // original's start/end copies hold x.
    const float radius = *reinterpret_cast<const float*>(tw + 0x164);
    const float drop = *reinterpret_cast<const float*>(tw + 0x168) - radius;
    const float* t = reinterpret_cast<const float*>(tw);
    alignas(16) float start[4] = {t[0], t[1], t[2] - drop, t[0]};
    alignas(16) float end[4] = {t[4], t[5], t[6] - drop, t[4]};
    const Lanes s = lanes(start);
    const Lanes e = lanes(end);
    const Lanes d = sub(e, s);
    const float length = platform_x87_sqrt(sum_xyz(mul(d, d)));
    const float rr = radius + title_global<float>(0x0083eb4c);  // 0.001
    alignas(16) float dir[4];
    {
        const Lanes dn = div_lanes(d, splat(length));
        platform_keep(dn);
        std::memcpy(dir, dn.v, 16);
    }
    const int32_t n = *reinterpret_cast<const int32_t*>(triangles + 0x1004);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* entry = *reinterpret_cast<const uint8_t* const*>(triangles + 0x1000) + i * 8;
        const uint8_t* mesh = *reinterpret_cast<const uint8_t* const*>(entry);
        const uint32_t triangle = *reinterpret_cast<const uint32_t*>(entry + 4);
        alignas(16) float plane[4];
        std::memcpy(plane, *reinterpret_cast<uint8_t* const*>(mesh + 0x44) + triangle * 16, 16);
        const Lanes p = lanes(plane);
        const float dist_end = sum_xyz(mul(p, e));
        const float dist_start = sum_xyz(mul(p, s));
        const Lanes w_minus_r = sub(splat(plane[3]), splat(rr));
        const Lanes dists = add({{dist_start, dist_end, 0.0f, 0.0f}}, w_minus_r);
        platform_keep(dists);
        int32_t beyond = 0;  // CMPLTPS on every lane: both ends beyond the radius
        for (int32_t a = 0; a < 4; ++a)
            if (0.0f < dists.v[a]) beyond |= 1 << a;
        if ((beyond & 3) == 3) continue;
        const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(mesh + 0x34);
        const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(mesh + 0x3c))[triangle];
        alignas(16) float v0[4], v1[4], v2[4], clipped[4];
        std::memcpy(v0, rows + (flags & 0x3f) * 16, 16);
        std::memcpy(v1, rows + ((flags >> 6) & 0x3f) * 16, 16);
        std::memcpy(v2, rows + ((flags >> 12) & 0x3f) * 16, 16);
        std::memcpy(clipped, end, 16);
        uint8_t solid = 0;
        if (!CM_ClipSphereSweepToTriangle(0, start, v0, clipped, dir, rr, v1, v2, plane, &solid)) continue;
        const float min_frac = title_global<float>(0x0083effc);  // 1e-5
        float frac = 0.0f;
        bool stuck = solid != 0;
        if (!stuck) {
            const Lanes d2 = sub(lanes(clipped), s);
            frac = length_from_squared(sum_xyz(mul(d2, d2))) / length;
            stuck = min_frac >= frac;
        }
        if (stuck) {
            std::memcpy(tw + 0x110, plane, 16);
            *fraction = 0.0f;
            tw[0x13d] = 1;
            *reinterpret_cast<uint32_t*>(tw + 0x124) = *reinterpret_cast<const uint32_t*>(mesh + 4);
            *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(mesh + 8);
            return 1;
        }
        if (frac > *fraction) continue;
        std::memcpy(tw + 0x110, plane, 16);
        *fraction = frac - min_frac;
        *reinterpret_cast<uint32_t*>(tw + 0x124) = *reinterpret_cast<const uint32_t*>(mesh + 4);
        *reinterpret_cast<uint32_t*>(tw + 0x128) = *reinterpret_cast<const uint32_t*>(mesh + 8);
        hit = 1;
    }
    return hit;
}

PLATFORM_ASM_ENTRY(CM_CandidateCapsuleTrace_entry,
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_CandidateCapsuleTrace) "addl $32, %esp\n"
                   "ret")

uint32_t XB_CDECL CM_CandidateCapsuleTrace(const float* maxs, const float* mins, const float* start,
                                           const uint8_t* triangles, const uint8_t* brushes, uint8_t* results,
                                           const float* end, uint32_t brushmask) {
    *reinterpret_cast<float*>(results + 0x20) = title_global<float>(0x0083eb20);  // 1
    alignas(16) uint8_t tw[0x170];
    Com_Memset(tw, 0, 0x170);
    std::memcpy(tw + 0x120, results + 0x20, 4);
    *reinterpret_cast<uint32_t*>(tw + 0xf8) = brushmask;
    const Lanes offset = mul(add(lanes(mins), lanes(maxs)), splat(title_global<float>(0x0083eb24)));  // 0.5
    const Lanes size0 = sub(lanes(mins), offset);
    const Lanes size1 = sub(lanes(maxs), offset);
    const Lanes s = add(lanes(start), offset);
    const Lanes e = add(lanes(end), offset);
    const Lanes delta = sub(e, s);
    std::memcpy(tw + 0x40, size0.v, 16);
    std::memcpy(tw + 0x50, size1.v, 16);
    std::memcpy(tw + 0xe0, delta.v, 16);
    std::memcpy(tw, s.v, 16);
    std::memcpy(tw + 0x10, e.v, 16);
    const float length_squared = sum_xyz(mul(delta, delta));
    std::memcpy(tw + 0xf0, &length_squared, 4);
    *reinterpret_cast<uint32_t*>(tw + 0x160) = 1;
    const float half_z = size1.v[2];
    const float radius = size1.v[0] > half_z ? half_z : size1.v[0];
    *reinterpret_cast<float*>(tw + 0x148) = half_z - radius;
    *reinterpret_cast<float*>(tw + 0x164) = radius;
    *reinterpret_cast<float*>(tw + 0x140) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x144) = 0.0f;
    const float* sign = reinterpret_cast<const float*>(uintptr_t{0x0082e0b0});
    Lanes abs_offset;
    for (int32_t i = 0; i < 4; ++i) {  // ANDNPS with the sign mask
        uint32_t mask, bits;
        std::memcpy(&mask, sign + i, 4);
        std::memcpy(&bits, tw + 0x140 + i * 4, 4);
        abs_offset.v[i] = from_bits(~mask & bits);
    }
    const Lanes capsule_size = add(abs_offset, splat(radius));
    const Lanes lo = sub(min_lanes(s, e), capsule_size);
    const Lanes hi = add(max_lanes(s, e), capsule_size);
    *reinterpret_cast<float*>(tw + 0x168) = half_z;
    std::memcpy(tw + 0x150, capsule_size.v, 16);
    std::memcpy(tw + 0x20, lo.v, 16);
    std::memcpy(tw + 0x30, hi.v, 16);
    *reinterpret_cast<uint32_t*>(tw + 0xfc) = (size1.v[1] + size1.v[2]) + size1.v[0] == 0.0f;
    // CMPEQPS on x, y, z: a position test when start and end are equal.
    if (start[0] == end[0] && start[1] == end[1] && start[2] == end[2])
        CM_TestInCandidates(tw, triangles, brushes);
    else
        CM_TraceThroughCandidates(tw, triangles, brushes);
    const Lanes endpos = add(lanes(start), mul(lanes(tw_row(tw, 0xe0)), splat(*reinterpret_cast<const float*>(tw + 0x120))));
    std::memcpy(tw + 0x100, endpos.v, 16);
    std::memcpy(results, tw + 0x100, 0x40);
    uint32_t y;
    std::memcpy(&y, start + 1, 4);
    return y;  // EAX = start y bits
}

// edge_side (above, as in CM_SphereTouchesTriangle): rot(a * rot(b) - rot(a) * b) . plane.

uint8_t XB_REGS3 XB_CDECL CM_TraceRayThroughCandidates(uint32_t, uint32_t, uint8_t* tw, const uint8_t* triangles,
                                                       const uint8_t* brushes, const float* start, const float* end,
                                                       float* out, uint32_t* surface_flags, uint32_t* contents) {
    const Lanes d = sub(lanes(end), lanes(start));
    const float best = sum_xyz(mul(d, d));
    if (!(title_global<float>(0x0082e6a0) <= best)) return 0;  // CMPLESS on lane 0 (0): a NaN length stops
    uint8_t hit = 0;
    const int32_t brush_count = *reinterpret_cast<const int32_t*>(brushes + 0x804);
    for (int32_t i = 0; i < brush_count; ++i) {
        const uint8_t* brush = (*reinterpret_cast<const uint8_t* const* const*>(brushes + 0x800))[i];
        if (!CM_TraceBoxThroughBrush(tw, brush)) continue;
        std::memcpy(out + 4, tw + 0x110, 16);
        const Lanes s = lanes(start);
        const Lanes p = add(s, mul(splat(*reinterpret_cast<const float*>(tw + 0x120)), sub(lanes(end), s)));
        std::memcpy(out, p.v, 16);
        *surface_flags = *reinterpret_cast<const uint32_t*>(brush + 4);
        *contents = *reinterpret_cast<const uint32_t*>(brush + 8);
        hit = 1;
    }
    const float* zero = reinterpret_cast<const float*>(uintptr_t{0x0082e680});
    const int32_t n = *reinterpret_cast<const int32_t*>(triangles + 0x1004);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* entry = *reinterpret_cast<const uint8_t* const*>(triangles + 0x1000) + i * 8;
        const uint8_t* mesh = *reinterpret_cast<const uint8_t* const*>(entry);
        const uint32_t triangle = *reinterpret_cast<const uint32_t*>(entry + 4);
        const uint8_t* plane_ptr = *reinterpret_cast<uint8_t* const*>(mesh + 0x44) + triangle * 16;
        alignas(16) float plane_copy[4];
        std::memcpy(plane_copy, plane_ptr, 16);
        const Lanes plane = lanes(plane_copy);
        const Lanes s = lanes(start);
        const Lanes e = lanes(end);
        const float ds = sum_xyz(mul(plane, s)) + plane.v[3];
        const float de = sum_xyz(mul(plane, e)) + plane.v[3];
        if (!(ds > title_global<float>(0x0083ed50))) continue;  // 1e-4
        if (!(de <= title_global<float>(0x0082e6a0))) continue;  // CMPLESS on lane 0 (0)
        const Lanes crossing = div_lanes(sub(mul(s, splat(de)), mul(e, splat(ds))), splat(de - ds));
        platform_keep(crossing);
        const Lanes to_start = sub(s, crossing);
        if (!(best > sum_xyz(mul(to_start, to_start)))) continue;
        const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(mesh + 0x34);
        const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(mesh + 0x3c))[triangle];
        const Lanes v0 = lanes(reinterpret_cast<const float*>(rows + (flags & 0x3f) * 16));
        const Lanes v1 = lanes(reinterpret_cast<const float*>(rows + ((flags >> 6) & 0x3f) * 16));
        const Lanes v2 = lanes(reinterpret_cast<const float*>(rows + ((flags >> 12) & 0x3f) * 16));
        const float e0 = edge_side(sub(v0, v1), sub(crossing, v1), plane);
        const float e1 = edge_side(sub(v1, v2), sub(crossing, v2), plane);
        const float e2 = edge_side(sub(v2, v0), sub(crossing, v0), plane);
        const float sides[4] = {e2, e1, e0, e0};
        int32_t outside = 0;
        for (int32_t a = 0; a < 4; ++a)  // CMPLTPS on every lane
            if (sides[a] < zero[a]) outside |= 1 << a;
        if (outside & 7) continue;
        std::memcpy(out + 4, plane_ptr, 16);
        std::memcpy(out, crossing.v, 16);
        *surface_flags = *reinterpret_cast<const uint32_t*>(mesh + 4);
        *contents = *reinterpret_cast<const uint32_t*>(mesh + 8);
        hit = 1;
    }
    return hit;
}

void XB_CDECL CM_TraceSegmentThroughCandidates(const uint8_t* triangles, const uint8_t* brushes, const float* start,
                                               const float* end, float* fraction, uint32_t* surface_flags,
                                               uint32_t* contents) {
    const Lanes d = sub(lanes(end), lanes(start));
    const float best = sum_xyz(mul(d, d));
    if (!(title_global<float>(0x0082e6a0) <= best)) return;  // CMPLESS on lane 0 (0)
    const int32_t brush_count = *reinterpret_cast<const int32_t*>(brushes + 0x804);
    for (int32_t i = 0; i < brush_count; ++i) {
        const uint8_t* brush = (*reinterpret_cast<const uint8_t* const* const*>(brushes + 0x800))[i];
        if (!CM_ClipSegmentToBrush(start, end, brush, fraction)) continue;
        *surface_flags = *reinterpret_cast<const uint32_t*>(brush + 4);
        *contents = *reinterpret_cast<const uint32_t*>(brush + 8);
        title_global<int32_t>(0x009507e0) = -1;
        if (*fraction == title_global<float>(0x0083eafc)) return;  // UCOMISS + JNP: exactly 0
    }
    const float* zero = reinterpret_cast<const float*>(uintptr_t{0x0082e680});
    const int32_t n = *reinterpret_cast<const int32_t*>(triangles + 0x1004);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* entry = *reinterpret_cast<const uint8_t* const*>(triangles + 0x1000) + i * 8;
        const uint8_t* mesh = *reinterpret_cast<const uint8_t* const*>(entry);
        const uint32_t triangle = *reinterpret_cast<const uint32_t*>(entry + 4);
        alignas(16) float plane_copy[4];
        std::memcpy(plane_copy, *reinterpret_cast<uint8_t* const*>(mesh + 0x44) + triangle * 16, 16);
        const Lanes plane = lanes(plane_copy);
        const Lanes s = lanes(start);
        const Lanes e = lanes(end);
        const float ds = sum_xyz(mul(plane, s)) + plane.v[3];
        const float de = sum_xyz(mul(plane, e)) + plane.v[3];
        if (!(ds > title_global<float>(0x0083ed50))) continue;  // 1e-4
        if (!(de <= title_global<float>(0x0082e6a0))) continue;  // CMPLESS on lane 0 (0)
        const Lanes crossing = div_lanes(sub(mul(s, splat(de)), mul(e, splat(ds))), splat(de - ds));
        platform_keep(crossing);
        const Lanes to_start = sub(s, crossing);
        if (!(best > sum_xyz(mul(to_start, to_start)))) continue;
        const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(mesh + 0x34);
        const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(mesh + 0x3c))[triangle];
        const Lanes v0 = lanes(reinterpret_cast<const float*>(rows + (flags & 0x3f) * 16));
        const Lanes v1 = lanes(reinterpret_cast<const float*>(rows + ((flags >> 6) & 0x3f) * 16));
        const Lanes v2 = lanes(reinterpret_cast<const float*>(rows + ((flags >> 12) & 0x3f) * 16));
        const float e0 = edge_side(sub(v0, v1), sub(crossing, v1), plane);
        const float e1 = edge_side(sub(v1, v2), sub(crossing, v2), plane);
        const float e2 = edge_side(sub(v2, v0), sub(crossing, v0), plane);
        const float sides[4] = {e2, e1, e0, e0};
        int32_t outside = 0;
        for (int32_t a = 0; a < 4; ++a)  // CMPLTPS on every lane
            if (sides[a] < zero[a]) outside |= 1 << a;
        if (outside & 7) continue;
        const float t = (sum_xyz(mul(to_start, plane)) - title_global<float>(0x0083ec68)) / (ds - de);  // 0.125
        if (!(*fraction > t)) continue;
        *fraction = t;
        *surface_flags = *reinterpret_cast<const uint32_t*>(mesh + 4);
        *contents = *reinterpret_cast<const uint32_t*>(mesh + 8);
        title_global<const uint8_t*>(0x0094cb9c) = mesh;
        title_global<uint32_t>(0x009507e0) = triangle;
    }
}

void XB_REGS1 XB_CDECL CM_CandidateRayTrace(const float* end, const uint8_t* triangles, const uint8_t* brushes,
                                            uint8_t* results, const float* start, uint32_t brushmask) {
    alignas(16) uint8_t tw[0x170];
    Com_Memset(tw, 0, 0x170);
    *reinterpret_cast<float*>(results + 0x20) = title_global<float>(0x0083eb20);  // 1
    const float* size = reinterpret_cast<const float*>(uintptr_t{0x0082e290});  // {0, 0, 0, 1}
    std::memcpy(tw + 0x40, size, 16);
    std::memcpy(tw + 0x50, size, 16);
    *reinterpret_cast<uint32_t*>(tw + 0xf8) = brushmask;
    std::memcpy(tw, start, 16);
    std::memcpy(tw + 0x10, end, 16);
    *reinterpret_cast<float*>(tw + 0x120) = title_global<float>(0x0083eb20);
    const Lanes s = lanes(start);
    const Lanes e = lanes(end);
    const Lanes delta = sub(e, s);
    std::memcpy(tw + 0xe0, delta.v, 16);
    const float length_squared = sum_xyz(mul(delta, delta));
    std::memcpy(tw + 0xf0, &length_squared, 4);
    *reinterpret_cast<float*>(tw + 0x164) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x168) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x140) = 0.0f;
    *reinterpret_cast<float*>(tw + 0x144) = 0.0f;
    std::memcpy(tw + 0x148, tw + 0x58, 4);
    const Lanes size_lanes = lanes(size);
    const Lanes lo = add(min_lanes(s, e), size_lanes);
    const Lanes hi = add(max_lanes(s, e), size_lanes);
    *reinterpret_cast<uint32_t*>(tw + 0x160) = 0;
    std::memcpy(tw + 0x20, lo.v, 16);
    std::memcpy(tw + 0x30, hi.v, 16);
    alignas(16) float start_copy[4], end_copy[4];
    std::memcpy(start_copy, start, 16);
    std::memcpy(end_copy, end, 16);
    const Lanes sc = lanes(start_copy);
    const Lanes d = sub(lanes(end_copy), sc);
    const float length = length_from_squared(sum_xyz(mul(d, d)));
    alignas(16) float out[8] = {};  // never-set words: zeroed dead stack in tests
    uint32_t surface_flags = 0, contents = 0;
    if (CM_TraceRayThroughCandidates(0, 0, tw, triangles, brushes, start_copy, end_copy, out, &surface_flags,
                                     &contents)) {
        *reinterpret_cast<uint32_t*>(tw + 0x124) = surface_flags;
        *reinterpret_cast<uint32_t*>(tw + 0x128) = contents;
        const Lanes p = lanes(out);
        const Lanes to_hit = sub(p, sc);
        const float fraction = length_from_squared(sum_xyz(mul(to_hit, to_hit))) / length;
        if (*reinterpret_cast<const float*>(tw + 0x120) > fraction) {
            const Lanes n = lanes(out + 4);
            uint32_t bits;
            const float dist = sum_xyz(mul(p, n));
            std::memcpy(&bits, &dist, 4);
            const float neg_dist = from_bits(bits ^ 0x80000000u);  // XORPS sign
            const float ds = sum_xyz(mul(sc, n)) + neg_dist;
            const float de = sum_xyz(mul(lanes(end_copy), n)) + neg_dist;
            *reinterpret_cast<float*>(tw + 0x120) = (ds - title_global<float>(0x0083ec68)) / (ds - de);  // 0.125
            std::memcpy(tw + 0x110, out + 4, 16);
        }
    }
    const Lanes endpos = add(lanes(start), mul(lanes(tw_row(tw, 0xe0)), splat(*reinterpret_cast<const float*>(tw + 0x120))));
    std::memcpy(tw + 0x100, endpos.v, 16);
    std::memcpy(results, tw + 0x100, 0x40);
}

PLATFORM_ASM_ENTRY(G_RayTraceCachedCandidates_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(G_RayTraceCachedCandidates) "addl $16, %esp\n"
                   "ret")

uint8_t XB_CDECL G_RayTraceCachedCandidates(const uint8_t* entity, float radius, const float* start,
                                                const float* end) {
    // The entity's record from its handle (+0x7c of the entity's first pointer): index low 12 bits, serial above.
    const uint32_t handle = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<uint8_t* const*>(entity) + 0x7c);
    const uint32_t index = handle & 0xfff;
    uint8_t* record = nullptr;
    if (static_cast<int32_t>(index) < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        record = title_global<uint8_t*>(0x0097a968 + index * 8);
    const Lanes margin = mul(splat(title_global<float>(0x0083eba8)), splat(radius));  // 2 * radius
    const Lanes s = lanes(start);
    const Lanes e = lanes(end);
    Lanes hi = add(max_lanes(s, e), margin);
    Lanes lo = sub(min_lanes(s, e), margin);
    lo.v[2] = lo.v[2] - title_global<float>(0x0083eb90);  // 100
    hi.v[2] = hi.v[2] + title_global<float>(0x0083eb90);
    float* cache_lo = reinterpret_cast<float*>(record + 0x430);
    float* cache_hi = reinterpret_cast<float*>(record + 0x440);
    if (!inside_xyz(sub(lanes(cache_lo), lo), sub(hi, lanes(cache_hi)))) {
        std::memcpy(cache_lo, lo.v, 16);
        std::memcpy(cache_hi, hi.v, 16);
        CM_GatherBoxCandidates(cache_lo, *reinterpret_cast<uint8_t* const*>(record + 0x450),
                               *reinterpret_cast<uint8_t* const*>(record + 0x454), cache_hi);
    }
    alignas(16) uint8_t brushes[0x810];
    alignas(16) uint8_t triangles[0x1010];
    *reinterpret_cast<uint8_t**>(triangles + 0x1000) = triangles;
    *reinterpret_cast<uint32_t*>(triangles + 0x1004) = 0;
    *reinterpret_cast<uint32_t*>(brushes + 0x804) = 0;
    *reinterpret_cast<uint8_t**>(brushes + 0x800) = brushes;
    alignas(16) float box_lo[4], box_hi[4];
    std::memcpy(box_lo, lo.v, 16);
    std::memcpy(box_hi, hi.v, 16);
    const uint32_t mask = *reinterpret_cast<const uint32_t*>(entity + 0x5c) & 0xfdff3fffu;
    CM_FilterBoxCandidates(brushes, 0, *reinterpret_cast<uint8_t* const*>(record + 0x454), box_lo, box_hi, mask,
                           *reinterpret_cast<uint8_t* const*>(record + 0x450), triangles);
    alignas(16) uint8_t results[0x40] = {};  // never-set words: zeroed dead stack in tests
    *reinterpret_cast<uint32_t*>(results + 0x30) = 0;
    *reinterpret_cast<uint32_t*>(results + 0x34) = 0;
    CM_CandidateRayTrace(end, triangles, brushes, results, start, mask);
    return title_global<float>(0x0083eb20) > *reinterpret_cast<const float*>(results + 0x20) ? 1 : 0;  // 1
}

namespace {

// ext - |d| on the x87 (FLD/FABS/FSUBR), stored as float; *negative = 0 > the unrounded value (FCOMIP).
float x87_extent_minus_abs(float d, float ext, bool* negative) {
    const float zero = title_global<float>(0x0083eafc);
    float stored;
    bool neg;
    __asm__("flds %[d]\n\tfabs\n\tfsubrs %[e]\n\tfsts %[out]\n\tflds %[zero]\n\t"
            "fcomip %%st(1), %%st\n\tfstp %%st(0)\n\tseta %[neg]"
            : [out] "=m"(stored), [neg] "=q"(neg)
            : [d] "m"(d), [e] "m"(ext), [zero] "m"(zero)
            : "st", "st(1)", "cc");
    *negative = neg;
    return stored;
}

}  // namespace

PLATFORM_ASM_ENTRY(CM_PushPointOutOfBrush_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm2, (%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_PushPointOutOfBrush) "addl $16, %esp\n"
                   "ret")

uint8_t XB_CDECL CM_PushPointOutOfBrush(const uint8_t* brush, const float* point, float radius, float* out) {
    const Lanes p = lanes(point);
    const Lanes ext = add(lanes(reinterpret_cast<const float*>(brush + 0x20)), splat(radius));
    const Lanes center = lanes(reinterpret_cast<const float*>(brush + 0x10));
    const Lanes d = sub(p, center);
    platform_keep(ext);
    platform_keep(d);
    bool negative;
    float best = x87_extent_minus_abs(d.v[0], ext.v[0], &negative);
    if (negative) return 0;
    int32_t axis = 0;
    for (int32_t i = 1; i < 3; ++i) {
        const float depth = x87_extent_minus_abs(d.v[i], ext.v[i], &negative);
        if (negative) return 0;
        if (best > depth) {
            best = depth;
            axis = i;
        }
    }
    const uint32_t plane_count = *reinterpret_cast<const uint32_t*>(brush + 0x30);
    const float* planes = *reinterpret_cast<const float* const*>(brush + 0x34);
    int32_t nearest = -1;
    for (uint32_t j = 0; j < plane_count; ++j) {
        const Lanes n = lanes(planes + j * 4);
        const float outside = (sum_xyz(mul(p, n)) - n.v[3]) - radius;
        uint32_t bits;
        std::memcpy(&bits, &outside, 4);
        const float depth = from_bits(bits ^ 0x80000000u);  // XORPS sign
        if (0.0f > depth) return 0;
        if (best > depth) {
            best = depth;
            nearest = static_cast<int32_t>(j);
        }
    }
    const float skin = title_global<float>(0x008fef1c);
    if (nearest != -1) {
        const float k = skin + best;
        const Lanes r = add(lanes(point), mul(lanes(planes + nearest * 4), splat(k)));
        std::memcpy(out, r.v, 16);
        return 1;
    }
    std::memcpy(out, point, 16);
    const float k = skin + ext.v[axis];
    const float neg_k = 0.0f - k;  // SUBSS before the branch
    const float offset = 0.0f > d.v[axis] ? neg_k : k;
    out[axis] = center.v[axis] + offset;
    return 1;
}

uint8_t XB_REGS3 XB_CDECL CM_PushSphereOutOfTriangle(const float* center, uint32_t, float* out, float radius,
                                                     const float* a, const float* b, const float* c, const float* normal) {
    const Lanes p = lanes(center);
    const Lanes n = lanes(normal);
    const Lanes va = sub(lanes(a), p);
    const float dist = sum_xyz(mul(va, n));
    if (dist > radius) return 0;
    if (0.0f - radius > dist) return 0;
    const Lanes vb = sub(lanes(b), p);
    const Lanes vc = sub(lanes(c), p);
    const float aa = sum_xyz(mul(va, va));
    const float ab = sum_xyz(mul(va, vb));
    const float ac = sum_xyz(mul(va, vc));
    const float one = title_global<float>(0x0083eb20);
    Lanes v;
    float length_squared;
    if (ab >= aa && ac >= aa) {  // vertex a
        v = va;
        length_squared = aa;
    } else {
        const float bb = sum_xyz(mul(vb, vb));
        const float bc = sum_xyz(mul(vb, vc));
        if (ab >= bb && bc >= bb) {  // vertex b
            v = vb;
            length_squared = bb;
        } else {
            const float cc = sum_xyz(mul(vc, vc));
            if (ac >= cc && bc >= cc) {  // vertex c
                v = vc;
                length_squared = cc;
            } else {
                bool edge = false;
                if (bb >= ab && aa >= ab) {  // edge ab
                    const float t1 = aa - ab;
                    const float t2 = bb - ab;
                    if (bc * t1 + ac * t2 >= bb * aa - ab * ab) {
                        const float inv = one / (t1 + t2);
                        v = add(mul(splat(inv * t2), va), mul(splat(inv * t1), vb));
                        edge = true;
                    }
                }
                if (!edge && cc >= ac && aa >= ac) {  // edge ac
                    const float u1 = aa - ac;
                    const float u2 = cc - ac;
                    if (bc * u1 + ab * u2 >= cc * aa - ac * ac) {
                        const float inv = one / (u1 + u2);
                        v = add(mul(splat(inv * u2), va), mul(splat(inv * u1), vc));
                        edge = true;
                    }
                }
                if (!edge && cc >= bc && bb >= bc) {  // edge bc
                    const float w1 = cc - bc;
                    const float w2 = bb - bc;
                    if (ac * w2 + ab * w1 >= bb * cc - bc * bc) {
                        const float inv = one / (w2 + w1);
                        v = add(mul(splat(inv * w1), vb), mul(splat(inv * w2), vc));
                        edge = true;
                    }
                }
                if (!edge) {  // the face: out along the normal
                    const Lanes r = add(lanes(center), mul(splat(dist + radius), n));
                    std::memcpy(out, r.v, 16);
                    return 1;
                }
                length_squared = sum_xyz(mul(v, v));
            }
        }
    }
    if (!(length_squared > title_global<float>(0x0083ed50))) return 0;  // 1e-4
    if (!(radius * radius >= length_squared)) return 0;
    if (!(0.0f > dist)) return 0;
    float k;
    // (radius - sqrt(len2)) / sqrt(len2) on the x87, stored as float.
    __asm__("flds %[l]\n\tfsqrt\n\tflds %[r]\n\t"
            ".byte 0xd8, 0xe1\n\t"  // fsub st, st(1) (Intel): st = st - st1
            ".byte 0xd8, 0xf1\n\t"  // fdiv st, st(1)
            "fstps %[k]\n\tfstp %%st(0)"
            : [k] "=m"(k)
            : [l] "m"(length_squared), [r] "m"(radius)
            : "st", "st(1)");
    const Lanes r = sub(p, mul(splat(k), v));
    std::memcpy(out, r.v, 16);
    return 1;
}

namespace {

// The entity record of a handle (low 12 bits index, serial above) in the table 0x0097a968, or null.
uint8_t* entity_from_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (static_cast<int32_t>(index) < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        return title_global<uint8_t*>(0x0097a968 + index * 8);
    return nullptr;
}

}  // namespace

uint8_t XB_CDECL G_PushCapsuleOutOfSolids(uint8_t* pm, float radius) {
    uint8_t* const ps = *reinterpret_cast<uint8_t* const*>(pm);
    const uint32_t clipmask = *reinterpret_cast<const uint32_t*>(pm + 0x5c);
    alignas(16) float p[4];
    std::memcpy(p, ps, 16);
    const float mid_z = ((*reinterpret_cast<const float*>(pm + 0x108) + *reinterpret_cast<const float*>(pm + 0x118)) *
                         title_global<float>(0x0083eb24)) +  // 0.5
                        p[2];
    uint8_t* const record = entity_from_handle(*reinterpret_cast<const uint32_t*>(ps + 0x7c));
    alignas(16) uint8_t brushes[0x810];
    alignas(16) uint8_t triangles[0x1010];
    *reinterpret_cast<uint8_t**>(triangles + 0x1000) = triangles;
    *reinterpret_cast<uint32_t*>(triangles + 0x1004) = 0;
    *reinterpret_cast<uint8_t**>(brushes + 0x800) = brushes;
    *reinterpret_cast<uint32_t*>(brushes + 0x804) = 0;
    uint8_t hit = 0;
    const Lanes margin = mul(splat(title_global<float>(0x0083eba8)), splat(radius));  // 2 * radius
    const Lanes mid = {{p[0], p[1], mid_z, p[3]}};
    Lanes hi = add(mid, margin);
    Lanes lo = sub(mid, margin);
    lo.v[2] = lo.v[2] - title_global<float>(0x0083eb90);  // 100
    hi.v[2] = hi.v[2] + title_global<float>(0x0083eb90);
    float* cache_lo = reinterpret_cast<float*>(record + 0x430);
    float* cache_hi = reinterpret_cast<float*>(record + 0x440);
    if (!inside_xyz(sub(lanes(cache_lo), lo), sub(hi, lanes(cache_hi)))) {
        std::memcpy(cache_lo, lo.v, 16);
        std::memcpy(cache_hi, hi.v, 16);
        CM_GatherBoxCandidates(cache_lo, *reinterpret_cast<uint8_t* const*>(record + 0x450),
                               *reinterpret_cast<uint8_t* const*>(record + 0x454), cache_hi);
    }
    alignas(16) float box_lo[4], box_hi[4];
    std::memcpy(box_lo, lo.v, 16);
    std::memcpy(box_hi, hi.v, 16);
    CM_FilterBoxCandidates(brushes, 0, *reinterpret_cast<uint8_t* const*>(record + 0x454), box_lo, box_hi, clipmask,
                           *reinterpret_cast<uint8_t* const*>(record + 0x450), triangles);
    // The entity list (function statics): zeroed once, box globals, SV_AreaEntities with the clip mask.
    if (!(title_global<uint8_t>(0x0098e690) & 1)) {
        title_global<uint32_t>(0x0098e690) |= 1;
        std::memset(reinterpret_cast<void*>(uintptr_t{0x0098e490}), 0, 0x200);
    }
    std::memcpy(reinterpret_cast<void*>(uintptr_t{0x0098e470}), lo.v, 16);
    std::memcpy(reinterpret_cast<void*>(uintptr_t{0x0098e480}), hi.v, 16);
    int32_t* const list = reinterpret_cast<int32_t*>(uintptr_t{0x0098e490});
    title_global<int32_t>(0x0098e460) =
        SV_AreaEntities(reinterpret_cast<const float*>(uintptr_t{0x0098e480}), 0,
                        reinterpret_cast<const float*>(uintptr_t{0x0098e470}), list, 0x80, clipmask);
    const float height = *reinterpret_cast<const float*>(pm + 0x118);
    float offsets[3];
    offsets[1] = height * title_global<float>(0x0083eb24);  // 0.5
    offsets[2] = height - radius;
    offsets[0] = radius;
    alignas(16) float mat[16] = {};  // G_GetEntityMatrix writes all 16 words
    for (int32_t i = 0; i < 3; ++i) {
        std::memcpy(p, ps, 16);
        p[2] = p[2] + offsets[i];
        for (int32_t j = 0; j < title_global<int32_t>(0x0098e460); ++j) {
            uint8_t* const ent = entity_from_handle(static_cast<uint32_t>(list[j]));
            if (!ent || ent == record) continue;
            const uint32_t* const model = *reinterpret_cast<const uint32_t* const*>(ent + 0xdc);
            if (model) {
                G_GetEntityMatrix(ent, mat);
                const Lanes r0 = lanes(mat), r1 = lanes(mat + 4), r2 = lanes(mat + 8), origin = lanes(mat + 12);
                const Lanes col0 = {{r0.v[0], r1.v[0], r2.v[0], r2.v[0]}};
                const Lanes col1 = {{r0.v[1], r1.v[1], r2.v[1], r2.v[1]}};
                const Lanes col2 = {{r0.v[2], r1.v[2], r2.v[2], r2.v[2]}};
                const Lanes d = sub(lanes(p), origin);
                const Lanes local = add(add(mul(col0, splat(d.v[0])), mul(col1, splat(d.v[1]))), mul(col2, splat(d.v[2])));
                alignas(16) float lp[4];
                std::memcpy(lp, local.v, 16);
                const uint8_t* const model_brushes = reinterpret_cast<const uint8_t*>(model[1]);
                for (uint32_t k = 0; k < model[0]; ++k) {
                    const uint8_t* brush = model_brushes + k * 0x40;
                    if (!(clipmask & *reinterpret_cast<const uint32_t*>(brush + 8))) continue;
                    hit |= CM_PushPointOutOfBrush(brush, lp, radius, lp);
                }
                const Lanes l = lanes(lp);
                const Lanes world = add(add(add(origin, mul(r0, splat(l.v[0]))), mul(r1, splat(l.v[1]))),
                                        mul(r2, splat(l.v[2])));
                std::memcpy(p, world.v, 16);
                continue;
            }
            const Lanes pl = lanes(p);
            const Lanes closest = min_lanes(max_lanes(pl, lanes(reinterpret_cast<const float*>(ent + 0x100))),
                                            lanes(reinterpret_cast<const float*>(ent + 0x110)));
            const Lanes d = sub(pl, closest);
            const float length_squared = sum_xyz(mul(d, d));
            if (!(radius * radius > length_squared)) continue;
            if (!(length_squared > title_global<float>(0x0083eb4c))) continue;  // 0.001
            hit = 1;
            const float skin = title_global<float>(0x0083eb94);  // 0.01
            float length, k;
            // sqrt(len2) stored as float; k = (radius - sqrt) + 0.01 on the x87, stored as float.
            __asm__("flds %[l]\n\tfsqrt\n\tfsts %[s]\n\tflds %[r]\n\t"
                    ".byte 0xd8, 0xe1\n\t"  // fsub st, st(1) (Intel): st = st - st1
                    "fadds %[skin]\n\tfstps %[k]\n\tfstp %%st(0)"
                    : [s] "=m"(length), [k] "=m"(k)
                    : [l] "m"(length_squared), [r] "m"(radius), [skin] "m"(skin)
                    : "st", "st(1)");
            const Lanes dir = div_lanes(d, splat(length));
            platform_keep(dir);
            const Lanes moved = add(pl, mul(splat(k), dir));
            std::memcpy(p, moved.v, 16);
        }
        const int32_t brush_count = *reinterpret_cast<const int32_t*>(brushes + 0x804);
        for (int32_t k = 0; k < brush_count; ++k)
            hit |= CM_PushPointOutOfBrush((*reinterpret_cast<const uint8_t* const* const*>(brushes + 0x800))[k], p,
                                          radius, p);
        for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(triangles + 0x1004); ++k) {
            const uint8_t* entry = *reinterpret_cast<const uint8_t* const*>(triangles + 0x1000) + k * 8;
            const uint8_t* mesh = *reinterpret_cast<const uint8_t* const*>(entry);
            const uint32_t triangle = *reinterpret_cast<const uint32_t*>(entry + 4);
            const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(mesh + 0x34);
            const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(mesh + 0x3c))[triangle];
            alignas(16) float n[4], v0[4], v1[4], v2[4];
            std::memcpy(n, *reinterpret_cast<uint8_t* const*>(mesh + 0x44) + triangle * 16, 16);
            std::memcpy(v0, rows + (flags & 0x3f) * 16, 16);
            std::memcpy(v1, rows + ((flags >> 6) & 0x3f) * 16, 16);
            std::memcpy(v2, rows + ((flags >> 12) & 0x3f) * 16, 16);
            hit |= CM_PushSphereOutOfTriangle(p, 0, p, radius, v0, v1, v2, n);
        }
        std::memcpy(ps, p, 16);
        *reinterpret_cast<float*>(ps + 8) = *reinterpret_cast<const float*>(ps + 8) - offsets[i];
    }
    return hit;
}

PLATFORM_ASM_ENTRY(G_PushAwayFromEntities_entry,
                   "subl $4, %esp\n"
                   "movss %xmm3, (%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_PushAwayFromEntities) "addl $8, %esp\n"
                   "ret")

uint8_t XB_CDECL G_PushAwayFromEntities(uint8_t* pm, float radius) {
    uint8_t* const ps = *reinterpret_cast<uint8_t* const*>(pm);
    uint8_t* const record = entity_from_handle(*reinterpret_cast<const uint32_t*>(ps + 0x7c));
    uint8_t hit = 0;
    const float two = title_global<float>(0x0083eba8);
    const float mid_z = (*reinterpret_cast<const float*>(pm + 0x108) + *reinterpret_cast<const float*>(pm + 0x118)) *
                        title_global<float>(0x0083eb24);  // 0.5
    alignas(16) float p[4];
    std::memcpy(p, ps, 16);
    Lanes mid = lanes(p);
    mid.v[2] = mid_z + mid.v[2];
    const Lanes r2 = mul(splat(two), splat(radius));
    Lanes lo = sub(mid, r2);
    Lanes hi = add(mid, r2);
    lo.v[2] = lo.v[2] - title_global<float>(0x0083eb90);  // 100
    hi.v[2] = hi.v[2] + title_global<float>(0x0083eb90);
    if (!(title_global<uint8_t>(0x0098c390) & 1)) {
        title_global<uint32_t>(0x0098c390) |= 1;
        std::memset(reinterpret_cast<void*>(uintptr_t{0x0098c190}), 0, 0x200);
    }
    std::memcpy(reinterpret_cast<void*>(uintptr_t{0x0098c170}), lo.v, 16);
    std::memcpy(reinterpret_cast<void*>(uintptr_t{0x0098c180}), hi.v, 16);
    int32_t* const list = reinterpret_cast<int32_t*>(uintptr_t{0x0098c190});
    title_global<int32_t>(0x0098c160) =
        SV_AreaEntities(reinterpret_cast<const float*>(uintptr_t{0x0098c180}), 0,
                        reinterpret_cast<const float*>(uintptr_t{0x0098c170}), list, 0x80,
                        *reinterpret_cast<const uint32_t*>(pm + 0x5c));
    for (int32_t j = 0; j < title_global<int32_t>(0x0098c160); ++j) {
        const uint8_t* const ent = entity_from_handle(static_cast<uint32_t>(list[j]));
        if (!ent || ent == record) continue;
        const Lanes o = lanes(reinterpret_cast<const float*>(ent + 0x140));
        const float top = (*reinterpret_cast<const float*>(pm + 0x118) + p[2]) + two;
        if (o.v[2] > top) continue;
        if (p[0] > o.v[0] + title_global<float>(0x0083ecdc)) continue;  // 72 (the original compares x here)
        Lanes d = sub(o, lanes(p));
        d.v[2] = 0.0f;
        const float length_squared = sum_xyz(mul(d, d));
        if (!((radius * radius) * title_global<float>(0x0083100c) > length_squared)) continue;  // 4
        float length;
        __asm__("flds %[l]\n\tfsqrt\n\tfstps %[s]" : [s] "=m"(length) : [l] "m"(length_squared) : "st");
        Lanes neg;
        for (int32_t a = 0; a < 4; ++a) {  // XORPS with the sign mask
            uint32_t bits;
            std::memcpy(&bits, &d.v[a], 4);
            neg.v[a] = from_bits(bits ^ 0x80000000u);
        }
        Lanes dir;
        if (platform_x87_greater(length, title_global<float>(0x0083eb94))) {  // 0.01
            dir = div_lanes(neg, splat(length));
            platform_keep(dir);
        } else {
            const float one = title_global<float>(0x0083eb20);
            dir = {{one, 0.0f, 0.0f, one}};
        }
        const float push = radius * two + title_global<float>(0x008fef50);
        const Lanes moved = add(o, mul(splat(push), dir));
        std::memcpy(p, moved.v, 16);
        hit = 1;
    }
    std::memcpy(ps, p, 16);
    return hit;
}

void XB_CDECL G_SeparateFromSolids() {
    uint8_t* pm = title_global<uint8_t*>(0x0092eff4);
    uint8_t* ps = *reinterpret_cast<uint8_t* const*>(pm);
    alignas(16) float saved[4];
    std::memcpy(saved, ps, 16);
    const uint32_t clipmask = *reinterpret_cast<const uint32_t*>(pm + 0x5c);
    *reinterpret_cast<uint32_t*>(pm + 0x5c) = 0x200c000;
    if (G_PushAwayFromEntities(pm, title_global<float>(0x008fef4c))) {
        for (int32_t n = 0;;) {
            const uint8_t again = G_PushAwayFromEntities(pm, title_global<float>(0x008fef4c));
            if (++n > 4 || !again) break;
        }
    }
    *reinterpret_cast<uint32_t*>(pm + 0x5c) = clipmask & 0xfdff3fffu;
    if (G_PushCapsuleOutOfSolids(pm, title_global<float>(0x008fef4c))) {
        for (int32_t n = 0;;) {
            const uint8_t again = G_PushCapsuleOutOfSolids(title_global<uint8_t*>(0x0092eff4), title_global<float>(0x008fef4c));
            if (++n > 4 || !again) break;
        }
    }
    pm = title_global<uint8_t*>(0x0092eff4);
    ps = *reinterpret_cast<uint8_t* const*>(pm);
    alignas(16) float now[4];
    std::memcpy(now, ps, 16);
    const Lanes d = sub(lanes(now), lanes(saved));
    const float radius = title_global<float>(0x008fef4c);
    if (!(sum_xyz(mul(d, d)) > radius * radius)) {
        *reinterpret_cast<uint32_t*>(pm + 0x5c) = clipmask;
        return;
    }
    const float half = (*reinterpret_cast<const float*>(pm + 0x118) + *reinterpret_cast<const float*>(pm + 0x108)) *
                       title_global<float>(0x0083eb24);  // 0.5
    alignas(16) float start[4], end[4];
    std::memcpy(start, saved, 16);
    std::memcpy(end, now, 16);
    start[2] = start[2] + half;
    end[2] = end[2] + half;
    // G_RayTraceCachedCandidates takes the move state in EBX.
    uint8_t blocked;
    {
        const float r = title_global<float>(0x008fef4c);
        blocked = G_RayTraceCachedCandidates(pm, r, start, end);
    }
    pm = title_global<uint8_t*>(0x0092eff4);
    if (blocked) std::memcpy(*reinterpret_cast<uint8_t* const*>(pm), saved, 16);
    *reinterpret_cast<uint32_t*>(pm + 0x5c) = clipmask;
}

PLATFORM_ASM_ENTRY(CM_PushSphereOutOfListedObjects_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CM_PushSphereOutOfListedObjects) "addl $12, %esp\n"
                   "ret")

uint8_t XB_CDECL CM_PushSphereOutOfListedObjects(const float* point, float* out, float radius) {
    std::memcpy(out, point, 16);
    uint8_t hit = 0;
    const uint8_t* const* brushes = title_global<const uint8_t* const*>(0x008fcee0);
    const uint8_t* const* brushes_end = brushes + title_global<int32_t>(0x008fcee4);
    for (const uint8_t* const* b = brushes; b != brushes_end; ++b) hit |= CM_PushPointOutOfBrush(*b, out, radius, out);
    const float* const* tris = title_global<const float* const*>(0x008fd3f0);
    const float* const* tris_end = tris + title_global<int32_t>(0x008fd3f4) * 4;
    for (const float* const* t = tris; t != tris_end; t += 4)
        hit |= CM_PushSphereOutOfTriangle(out, 0, out, radius, t[0], t[1], t[2], t[3]);
    // Entities (0x50-byte records: axis rows +0, +0x10, +0x20, origin +0x30, entity +0x40): the point moved into the
    // entity's frame, pushed out of its brush model's brushes, moved back.
    const uint8_t* records = title_global<const uint8_t*>(0x008fd680);
    const uint8_t* records_end = records + title_global<int32_t>(0x008fd684) * 0x50;
    for (const uint8_t* r = records; r != records_end; r += 0x50) {
        const uint8_t* ent = *reinterpret_cast<uint8_t* const*>(r + 0x40);
        const uint32_t* model = *reinterpret_cast<const uint32_t* const*>(ent + 0xdc);
        const float* m = reinterpret_cast<const float*>(r);
        const Lanes r0 = lanes(m), r1 = lanes(m + 4), r2 = lanes(m + 8), origin = lanes(m + 12);
        const Lanes col0 = {{r0.v[0], r1.v[0], r2.v[0], r2.v[0]}};
        const Lanes col1 = {{r0.v[1], r1.v[1], r2.v[1], r2.v[1]}};
        const Lanes col2 = {{r0.v[2], r1.v[2], r2.v[2], r2.v[2]}};
        const Lanes d = sub(lanes(out), origin);
        const Lanes local = add(add(mul(col0, splat(d.v[0])), mul(col1, splat(d.v[1]))), mul(col2, splat(d.v[2])));
        std::memcpy(out, local.v, 16);
        const uint8_t* model_brushes = reinterpret_cast<const uint8_t*>(model[1]);
        for (uint32_t k = 0; k < model[0]; ++k)
            hit |= CM_PushPointOutOfBrush(model_brushes + k * 0x40, out, radius, out);
        const Lanes l = lanes(out);
        const Lanes world =
            add(add(add(origin, mul(r0, splat(l.v[0]))), mul(r1, splat(l.v[1]))), mul(r2, splat(l.v[2])));
        std::memcpy(out, world.v, 16);
    }
    return hit;
}

void XB_REGS1 XB_CDECL Phys_MoveAttachedPoint(uint8_t* obj) {
    float* const push = reinterpret_cast<float*>(obj + 0x110);
    push[0] = push[1] = push[2] = push[3] = 0.0f;
    const Lanes v = lanes(reinterpret_cast<const float*>(obj + 0x90));
    uint8_t* const owner = *reinterpret_cast<uint8_t* const*>(obj + 0x124);
    const float* rows = reinterpret_cast<const float*>(owner + 0x40);
    const Lanes world = add(add(add(lanes(rows + 12), mul(lanes(rows), splat(v.v[0]))), mul(lanes(rows + 4), splat(v.v[1]))),
                            mul(lanes(rows + 8), splat(v.v[2])));
    float* const pos = reinterpret_cast<float*>(obj + 0x100);
    const Lanes d = sub(world, lanes(pos));
    const float length_squared = sum_xyz(mul(d, d));
    const float half = *reinterpret_cast<const float*>(obj + 0x120) * title_global<float>(0x0083eb24);  // 0.5
    if (length_squared > half * half) {
        float k;
        // (half + sqrt(len2)) / sqrt(len2) on the x87, stored as float.
        __asm__("flds %[l]\n\tfsqrt\n\tflds %[h]\n\t"
                ".byte 0xd8, 0xc1\n\t"  // fadd st, st(1)
                ".byte 0xd8, 0xf1\n\t"  // fdiv st, st(1)
                "fstps %[k]\n\tfstp %%st(0)"
                : [k] "=m"(k)
                : [l] "m"(length_squared), [h] "m"(half)
                : "st", "st(1)");
        alignas(16) float delta[4];
        std::memcpy(delta, mul(d, splat(k)).v, 16);
        float fraction;
        if (CM_RayHitsListedObjects(&fraction, pos, delta)) {
            const Lanes back = mul(splat(fraction - title_global<float>(0x0083eb20)), lanes(delta));
            std::memcpy(push, back.v, 16);
        }
    }
    const Lanes t = lanes(push);
    float* const origin = reinterpret_cast<float*>(owner + 0x70);
    std::memcpy(origin, add(lanes(origin), t).v, 16);
    std::memcpy(pos, add(world, t).v, 16);
}

void XB_REGS1 XB_CDECL Phys_UndoAttachedPushes(uint8_t* obj) {
    uint8_t* const* points = reinterpret_cast<uint8_t* const*>(obj + 0xbf0);
    const int32_t count = *reinterpret_cast<const int32_t*>(obj + 0xc44);
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* point = points[i];
        const Lanes push = lanes(reinterpret_cast<const float*>(point + 0x110));
        Lanes back;
        for (int32_t a = 0; a < 3; ++a) {  // XORPS with the sign mask
            uint32_t bits;
            std::memcpy(&bits, &push.v[a], 4);
            back.v[a] = from_bits(bits ^ 0x80000000u);
        }
        back.v[3] = 0.0f;  // the original's w is a never-written stack word (zeroed dead stack in tests)
        float* origin = reinterpret_cast<float*>(*reinterpret_cast<uint8_t* const*>(point + 0x124) + 0x70);
        std::memcpy(origin, add(lanes(origin), back).v, 16);
    }
}

uint8_t XB_REGS1 XB_CDECL G_FilterCachedCandidates(const uint8_t* record, const float* start, const float* end,
                                                   float radius, uint32_t content_mask, uint8_t* out_triangles,
                                                   uint8_t* out_brushes) {
    if (!record) return 0;
    const uint8_t* in_triangles = *reinterpret_cast<uint8_t* const*>(record + 0x450);
    if (!in_triangles) return 0;
    const uint8_t* in_brushes = *reinterpret_cast<uint8_t* const*>(record + 0x454);
    if (!in_brushes) return 0;
    const Lanes s = lanes(start), e = lanes(end);
    alignas(16) float lo[4], hi[4];
    std::memcpy(lo, sub(min_lanes(s, e), Lanes{{radius, radius, 0.0f, radius}}).v, 16);
    const float r2 = radius * title_global<float>(0x0083eba8);  // 2
    std::memcpy(hi, add(max_lanes(s, e), Lanes{{radius, radius, r2, radius}}).v, 16);
    if (!inside_xyz(sub(lanes(reinterpret_cast<const float*>(record + 0x430)), lanes(lo)),
                    sub(lanes(hi), lanes(reinterpret_cast<const float*>(record + 0x440)))))
        return 0;
    CM_FilterBoxCandidates(out_brushes, 0, in_brushes, lo, hi, content_mask, in_triangles, out_triangles);
    return 1;
}

uint8_t XB_REGS3 XB_CDECL CM_RayHitsTriangle(const float* a, uint32_t, const float* c, const float* origin,
                                             const float* dir, const float* b, float max_fraction, float* fraction) {
    const Lanes la = lanes(a);
    const float* sign = reinterpret_cast<const float*>(uintptr_t{0x0082e0b0});
    const Lanes d = lanes(dir);
    Lanes neg_dir;
    for (int32_t i = 0; i < 4; ++i) {  // XORPS with the sign mask
        uint32_t mask, bits;
        std::memcpy(&mask, sign + i, 4);
        std::memcpy(&bits, &d.v[i], 4);
        neg_dir.v[i] = from_bits(mask ^ bits);
    }
    const Lanes e1 = sub(lanes(b), la);
    const Lanes e2 = sub(lanes(c), la);
    const Lanes n = yzx(sub(mul(e2, yzx(e1)), mul(yzx(e2), e1)));
    const float det = sum_xyz(mul(n, neg_dir));
    uint32_t det_bits;
    std::memcpy(&det_bits, &det, 4);
    // FLD/FABS/FCOMIP: parallel when |det| < 1e-4 (no MXCSR flags).
    if (platform_x87_greater(title_global<float>(0x0083ed50), from_bits(det_bits & 0x7fffffffu))) return 0;
    const Lanes to_origin = sub(lanes(origin), la);
    const float t = sum_xyz(mul(n, to_origin)) / det;
    if (0.0f > t) return 0;
    if (t > max_fraction) return 0;
    const Lanes q = yzx(sub(mul(to_origin, yzx(neg_dir)), mul(yzx(to_origin), neg_dir)));
    const float qe1 = sum_xyz(mul(q, e1));
    uint32_t qbits;
    std::memcpy(&qbits, &qe1, 4);
    const float u = from_bits(qbits ^ 0x80000000u) / det;  // XORPS sign
    if (0.0f > u) return 0;
    const float v = sum_xyz(mul(q, e2)) / det;
    if (!(v >= 0.0f)) return 0;
    if (!(title_global<float>(0x0083eb20) >= u + v)) return 0;
    *fraction = t;
    return 1;
}

uint8_t XB_CDECL CM_ClipSegmentToBrush(const float* start, const float* end, const uint8_t* brush, float* fraction) {
    const float zero = 0.0f;
    const float eps = title_global<float>(0x0083ec68);  // 0.125
    float enter = zero;
    float leave = *fraction;
    bool hit_plane = false;
    bool inside = true;
    // One plane: d1 / d2 = start / end distances. Returns false when the segment misses the brush.
    auto plane = [&](float d1, float d2) -> bool {
        if (d1 > zero) {
            const float f = d1 - d2;
            if (d2 > zero) {
                if (zero >= f) return false;
                if (d2 >= eps) return false;
                inside = false;
            }
            const float near = d1 - eps;
            if (near > f * enter) {
                enter = near / f;
                if (enter >= leave) return false;
                hit_plane = true;
            } else {
                hit_plane = true;
            }
        } else if (d2 > zero) {
            const float f = d1 - d2;
            inside = false;
            if (d1 > f * leave) {
                leave = d1 / f;
                if (enter >= leave) return false;
            }
        }
        return true;
    };
    const Lanes center = lanes(reinterpret_cast<const float*>(brush + 0x10));
    const Lanes half = lanes(reinterpret_cast<const float*>(brush + 0x20));
    for (int32_t pass = 0; pass < 2; ++pass) {  // the box faces: mins with normal -1, then maxs with +1
        const Lanes bound = pass ? add(center, half) : sub(center, half);
        const float sign = pass ? title_global<float>(0x0083eb20) : title_global<float>(0x0083eb34);
        for (int32_t a = 0; a < 3; ++a) {
            const float d1 = (start[a] - bound.v[a]) * sign;
            const float d2 = (end[a] - bound.v[a]) * sign;
            if (!plane(d1, d2)) return 0;
        }
    }
    const uint32_t count = *reinterpret_cast<const uint32_t*>(brush + 0x30);
    const float* planes = *reinterpret_cast<const float* const*>(brush + 0x34);
    const Lanes s = lanes(start), e = lanes(end);
    for (uint32_t j = 0; j < count; ++j) {
        const Lanes n = lanes(planes + j * 4);
        const float d1 = sum_xyz(mul(s, n)) - n.v[3];
        const float d2 = sum_xyz(mul(e, n)) - n.v[3];
        if (!plane(d1, d2)) return 0;
    }
    if (hit_plane) {
        *fraction = enter;
    } else if (inside) {
        *fraction = zero;
    }
    return 1;
}

PLATFORM_ASM_ENTRY(CM_RayHitsListedObjects_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(CM_RayHitsListedObjects) "addl $12, %esp\n"
                   "ret")

uint8_t XB_CDECL CM_RayHitsListedObjects(float* fraction, const float* start, const float* delta) {
    *fraction = title_global<float>(0x0083eb20);  // 1
    alignas(16) float end[4];
    std::memcpy(end, add(lanes(start), lanes(delta)).v, 16);
    uint8_t hit = 0;
    const uint8_t* const* brushes = title_global<const uint8_t* const*>(0x008fcee0);
    const uint8_t* const* brushes_end = brushes + title_global<int32_t>(0x008fcee4);
    for (const uint8_t* const* b = brushes; b != brushes_end; ++b)
        if (CM_ClipSegmentToBrush(start, end, *b, fraction)) hit = 1;
    const float* const* tris = title_global<const float* const*>(0x008fd3f0);
    const float* const* tris_end = tris + title_global<int32_t>(0x008fd3f4) * 4;
    for (const float* const* t = tris; t != tris_end; t += 4) {
        if (!(0.0f > sum_xyz(mul(lanes(delta), lanes(t[3]))))) continue;  // facing the segment
        hit |= CM_RayHitsTriangle(t[0], 0, t[1], start, delta, t[2], *fraction, fraction);
    }
    return hit;
}


uint8_t XB_CDECL CM_RayHitsWorldMeshes(const float* start, const float* delta, float* plane_out, float* fraction) {
    const Lanes s = lanes(start);
    const Lanes e = add(s, lanes(delta));
    const Lanes hi = max_lanes(s, e);
    const Lanes lo = min_lanes(s, e);
    alignas(16) float center[4];
    {
        const Lanes c = mul(splat(title_global<float>(0x0083eb24)), add(s, e));  // 0.5
        std::memcpy(center, c.v, 16);
    }
    *fraction = title_global<float>(0x0083eb20);  // 1
    uint8_t hit = 0;
    for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0095f4fc) + 0xc); ++k) {
        uint8_t* tree = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f4fc) + 0x10 + k * 4);
        if (!inside_xyz(sub(lanes(reinterpret_cast<const float*>(tree + 0x40)), hi),
                        sub(lo, lanes(reinterpret_cast<const float*>(tree + 0x50)))))
            continue;
        alignas(16) float cell_center[4];
        float half;
        const uint8_t* leaf = CM_QuadTreeFindLeaf(tree, center, cell_center, &half);
        const int32_t n = *reinterpret_cast<const int32_t*>(leaf);
        for (int32_t j = 0; j < n; ++j) {
            uint8_t* object = leaf_object(tree, (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[j]);
            if (object_type(object) == 9) continue;
            const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
            const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
            if (!inside_xyz(sub(sub(c, h), hi), sub(lo, add(c, h)))) continue;
            alignas(16) float walk_lo[4], walk_hi[4];  // MINPS/MAXPS(start, end) again
            std::memcpy(walk_lo, lo.v, 16);
            std::memcpy(walk_hi, hi.v, 16);
            uint16_t* stack = reinterpret_cast<uint16_t*>(uintptr_t{0x0090a020});
            stack[0] = 0;
            stack[2] = 0;
            stack[1] = *reinterpret_cast<const uint16_t*>(object + 0x40);
            title_global<uint16_t>(0x0090a620) = 3;
            for (uint16_t node = CM_BoxTreeStack_Next(object, walk_lo, walk_hi); node != 0xffff;
                 node = CM_BoxTreeStack_Next(object, walk_lo, walk_hi)) {
                const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(object + 0x34);
                const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(object + 0x3c))[node];
                const float* v0 = reinterpret_cast<const float*>(rows + (flags & 0x3f) * 16);
                const float* v1 = reinterpret_cast<const float*>(rows + ((flags >> 6) & 0x3f) * 16);
                const float* v2 = reinterpret_cast<const float*>(rows + ((flags >> 12) & 0x3f) * 16);
                const float* plane = reinterpret_cast<const float*>(*reinterpret_cast<uint8_t* const*>(object + 0x44) + node * 16);
                if (sum_xyz(mul(lanes(plane), lanes(delta))) >= title_global<float>(0x0083eafc)) continue;  // facing away
                if (!CM_RayHitsTriangle(v0, 0, v1, start, delta, v2, *fraction, fraction)) continue;
                std::memcpy(plane_out, plane, 16);
                hit = 1;
            }
        }
    }
    return hit;
}

int32_t XB_REGS3 XB_CDECL CM_SightTrace(const float* end, uint32_t, const float* start, const float* mins,
                                        const float* maxs, const uint32_t* model, uint32_t brushmask,
                                        uint32_t capsule_flag, const uint8_t* capsule) {
    ++cm_field<int32_t>(0x60);  // checkcount
    alignas(16) uint8_t tw[0x170];
    Com_Memset(tw, 0, 0x170);
    *reinterpret_cast<uint32_t*>(tw + 0xf8) = brushmask;
    *reinterpret_cast<float*>(tw + 0x120) = title_global<float>(0x0083eb20);  // 1
    const Lanes offset = mul(add(lanes(mins), lanes(maxs)), splat(title_global<float>(0x0083eb24)));  // 0.5
    const Lanes size0 = sub(lanes(mins), offset);
    const Lanes size1 = sub(lanes(maxs), offset);
    const Lanes s = add(lanes(start), offset);
    const Lanes e = add(lanes(end), offset);
    const Lanes delta = sub(e, s);
    std::memcpy(tw + 0xe0, delta.v, 16);
    std::memcpy(tw + 0x40, size0.v, 16);
    std::memcpy(tw + 0x50, size1.v, 16);
    std::memcpy(tw, s.v, 16);
    std::memcpy(tw + 0x10, e.v, 16);
    const float length_squared = sum_xyz(mul(delta, delta));
    std::memcpy(tw + 0xf0, &length_squared, 4);
    float radius;
    uint32_t flag;
    if (capsule) {
        std::memcpy(tw + 0x140, capsule, 0x30);
        std::memcpy(&radius, tw + 0x164, 4);
        std::memcpy(&flag, tw + 0x160, 4);
    } else {
        flag = capsule_flag;
        *reinterpret_cast<uint32_t*>(tw + 0x160) = flag;
        const float half_z = size1.v[2];
        radius = size1.v[0] > half_z ? half_z : size1.v[0];
        *reinterpret_cast<float*>(tw + 0x140) = 0.0f;
        *reinterpret_cast<float*>(tw + 0x144) = 0.0f;
        *reinterpret_cast<float*>(tw + 0x164) = radius;
        *reinterpret_cast<float*>(tw + 0x168) = half_z;
        *reinterpret_cast<float*>(tw + 0x148) = half_z - radius;
    }
    Lanes lo = min_lanes(s, e);
    Lanes hi = max_lanes(s, e);
    if (flag) {
        const float* sign = reinterpret_cast<const float*>(uintptr_t{0x0082e0b0});
        Lanes abs_offset;
        for (int32_t i = 0; i < 4; ++i) {  // ANDNPS with the sign mask
            uint32_t mask, bits;
            std::memcpy(&mask, sign + i, 4);
            std::memcpy(&bits, tw + 0x140 + i * 4, 4);
            abs_offset.v[i] = from_bits(~mask & bits);
        }
        const Lanes capsule_size = add(abs_offset, splat(radius));
        std::memcpy(tw + 0x150, capsule_size.v, 16);
        lo = sub(lo, capsule_size);
        hi = add(hi, capsule_size);
    } else {
        lo = add(lo, size0);
        hi = add(hi, size1);
    }
    std::memcpy(tw + 0x20, lo.v, 16);
    std::memcpy(tw + 0x30, hi.v, 16);
    for (int32_t corner = 0; corner < 8; ++corner) {  // +0x60: x/y/z from size1 when bit 0/1/2 is set (w stays 0)
        float* row = reinterpret_cast<float*>(tw + 0x60 + corner * 0x10);
        row[0] = (corner & 1) ? size1.v[0] : size0.v[0];
        row[1] = (corner & 2) ? size1.v[1] : size0.v[1];
        row[2] = (corner & 4) ? size1.v[2] : size0.v[2];
    }
    // UCOMISS + LAHF/JNP: point-sized only when the sum is exactly 0.
    *reinterpret_cast<uint32_t*>(tw + 0xfc) = (size1.v[1] + size1.v[0]) + size1.v[2] == title_global<float>(0x0083eafc);
    if (model) {
        if (model[0x48 / 4] != 0x1fe) return static_cast<int32_t>(CM_ModelBrushesBlockSight(model, tw));
        const uint8_t* temp_leaf = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f504) + 4);
        if (!(*reinterpret_cast<const uint32_t*>(temp_leaf + 8) & brushmask)) return 0;
        if (flag) return CM_CapsuleBlocksSight(tw);
        return static_cast<int32_t>(CM_TempBoxBlocksSight(tw));
    }
    ++cm_field<int32_t>(0x60);  // checkcount again for the world trees
    uint8_t blocked;
    if (*reinterpret_cast<const uint32_t*>(tw + 0xfc))
        blocked = CM_SightTraceRayThroughWorldTrees(tw, reinterpret_cast<const float*>(tw),
                                                    reinterpret_cast<const float*>(tw + 0x10));
    else
        blocked = CM_SightTraceThroughWorldTrees(tw);
    return blocked ? 1 : 0;
}

uint32_t XB_REGS3 XB_CDECL CM_TransformedSightTrace(const float* origin, uint32_t, const float* end, uint32_t,
                                                    const float* start, const float* mins, const float* maxs,
                                                    const uint32_t* model, uint32_t brushmask, const float* angles,
                                                    uint32_t capsule_flag) {
    const Lanes offset = mul(add(lanes(mins), lanes(maxs)), splat(title_global<float>(0x0083eb24)));  // 0.5
    alignas(16) float size0[4], size1[4], local_start[4], local_end[4];
    const Lanes s0 = sub(lanes(mins), offset);
    const Lanes s1 = sub(lanes(maxs), offset);
    std::memcpy(size0, s0.v, 16);
    std::memcpy(size1, s1.v, 16);
    const Lanes o = lanes(origin);
    const Lanes ls = sub(add(lanes(start), offset), o);
    const Lanes le = sub(add(lanes(end), offset), o);
    std::memcpy(local_start, ls.v, 16);
    std::memcpy(local_end, le.v, 16);
    const bool rotate = model && model[0x48 / 4] != 0x1ff &&
                        (!(angles[0] == 0.0f) || !(angles[1] == 0.0f) || !(angles[2] == 0.0f));
    alignas(16) float block[12] = {};  // as in CM_TransformedBoxTrace
    alignas(16) float rows[12] = {};
    std::memcpy(&block[8], &capsule_flag, 4);
    const float radius = size1[0] > size1[2] ? size1[2] : size1[0];
    block[10] = size1[2];
    block[9] = radius;
    const float half_height = size1[2] - radius;
    if (rotate) {
        float forward[3], right[3], up[3];
        uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles)),
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward))};
        AnglesToAxis(args, right, up);
        const float neg_right_z = 0.0f - right[2];
        rows[0] = forward[0];
        rows[1] = forward[1];
        rows[2] = forward[2];
        rows[4] = 0.0f - right[0];
        rows[5] = 0.0f - right[1];
        rows[6] = neg_right_z;
        rows[8] = up[0];
        rows[9] = up[1];
        rows[10] = up[2];
        Mat34_RotateVector(local_start, 0, rows);
        Mat34_RotateVector(local_end, 0, rows);
        block[0] = forward[2] * half_height;
        block[1] = 0.0f - neg_right_z * half_height;
        block[2] = up[2] * half_height;
    } else {
        block[0] = 0.0f;
        block[1] = 0.0f;
        block[2] = half_height;
    }
    return static_cast<uint32_t>(
        CM_SightTrace(local_end, 0, local_start, size0, size1, model, brushmask, capsule_flag,
                      reinterpret_cast<const uint8_t*>(block)));
}

namespace {

// MAXPS(center - maxs, mins - center) < half on x, y and z (CMPLTPS on every lane).
bool box_overlaps_object(const float* mins, const float* maxs, const uint8_t* object) {
    const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
    const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
    const Lanes below = sub(lanes(mins), c);
    const Lanes above = sub(c, lanes(maxs));
    int32_t inside = 0;
    for (int32_t a = 0; a < 4; ++a) {
        const float m = above.v[a] > below.v[a] ? above.v[a] : below.v[a];
        if (m < h.v[a]) inside |= 1 << a;
    }
    return (inside & 7) == 7;
}

// The bounds of a mesh triangle (vertex indexes in +0x3c flags, rows +0x34) against mins..maxs.
bool box_overlaps_triangle(const float* mins, const float* maxs, const uint8_t* mesh, uint32_t triangle) {
    const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(mesh + 0x34);
    const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(mesh + 0x3c))[triangle];
    const Lanes v0 = lanes(reinterpret_cast<const float*>(rows + (flags & 0x3f) * 16));
    const Lanes v1 = lanes(reinterpret_cast<const float*>(rows + ((flags >> 6) & 0x3f) * 16));
    const Lanes v2 = lanes(reinterpret_cast<const float*>(rows + ((flags >> 12) & 0x3f) * 16));
    const Lanes tmax = max_lanes(v0, max_lanes(v1, v2));
    const Lanes tmin = min_lanes(v0, min_lanes(v1, v2));
    return inside_xyz(sub(tmin, lanes(maxs)), sub(lanes(mins), tmax));
}

// Appends to a candidate list (count at +count_offset, at most 0x200, pointer just before it); the slot address
// is tested, not the pointer.
uint32_t* candidate_slot(uint8_t* list, uint32_t count_offset, uint32_t entry_size) {
    int32_t& count = *reinterpret_cast<int32_t*>(list + count_offset);
    if (count >= 0x200) return nullptr;
    const uint32_t slot = *reinterpret_cast<const uint32_t*>(list + count_offset - 4) + static_cast<uint32_t>(count) * entry_size;
    ++count;
    return reinterpret_cast<uint32_t*>(uintptr_t{slot});
}

}  // namespace

void XB_CDECL CM_GatherQuadTreeCandidates_r(uint8_t* tree, uint32_t tree_index, const int16_t* child, const float* center,
                                            float half, const float* mins, const float* maxs, uint8_t* triangles,
                                            uint8_t* brushes) {
    if (*child <= 0) {
        int32_t& checkcount = cm_field<int32_t>(0x60);
        const uint8_t* leaf = *reinterpret_cast<uint8_t* const*>(tree + 0xc) - *child * 8;
        const int32_t n = *reinterpret_cast<const int32_t*>(leaf);
        for (int32_t i = 0; i < n; ++i) {
            uint8_t* object = leaf_object(tree, (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[i]);
            int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
            if (mark == checkcount) continue;
            mark = checkcount;
            if (!box_overlaps_object(mins, maxs, object)) continue;
            if (object_type(object) == 9) {
                if (uint32_t* slot = candidate_slot(brushes, 0x804, 4)) slot[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
                continue;
            }
            uint16_t* stack = reinterpret_cast<uint16_t*>(uintptr_t{0x0090a020});
            stack[0] = 0;
            stack[2] = 0;
            stack[1] = *reinterpret_cast<const uint16_t*>(object + 0x40);
            title_global<uint16_t>(0x0090a620) = 3;
            for (uint16_t node = CM_BoxTreeStack_Next(object, mins, maxs); node != 0xffff;
                 node = CM_BoxTreeStack_Next(object, mins, maxs)) {
                if (!box_overlaps_triangle(mins, maxs, object, node)) continue;
                if (uint32_t* slot = candidate_slot(triangles, 0x1004, 8)) {
                    slot[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
                    slot[1] = node;
                }
            }
        }
        return;
    }
    const float child_half = half * title_global<float>(0x0083eb24);  // 0.5
    const Lanes h = splat(half);
    const Lanes c = lanes(center);
    for (int32_t q = 0; q < 4; ++q) {
        const Lanes cell_min = add(c, mul(lanes(reinterpret_cast<const float*>(uintptr_t{0x00963870}) + q * 4), h));
        const Lanes cell_max = add(c, mul(lanes(reinterpret_cast<const float*>(uintptr_t{0x00960120}) + q * 4), h));
        const Lanes below = sub(lanes(mins), cell_max);
        const Lanes above = sub(cell_min, lanes(maxs));
        int32_t inside = 0;
        for (int32_t a = 0; a < 4; ++a) {
            const float m = above.v[a] > below.v[a] ? above.v[a] : below.v[a];  // MAXPS(above, below)
            if (m < 0.0f) inside |= 1 << a;
        }
        if ((inside & 3) != 3) continue;  // x and y only
        alignas(16) float child_center[4];
        quadtree_child_center(child_center, c, q, splat(child_half));
        const int16_t* children = *reinterpret_cast<const int16_t* const*>(tree + 4);
        CM_GatherQuadTreeCandidates_r(tree, tree_index, children + *child + q, child_center, child_half, mins, maxs,
                                      triangles, brushes);
    }
}

PLATFORM_ASM_ENTRY(CM_GatherBoxCandidates_entry,
                   "pushl %edi\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n" PLATFORM_ASM_CALL(CM_GatherBoxCandidates) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL CM_GatherBoxCandidates(const float* mins, uint8_t* triangles, uint8_t* brushes, const float* maxs) {
    uint32_t result = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip_map()));  // EAX when there are no trees
    *reinterpret_cast<int32_t*>(triangles + 0x1004) = 0;
    *reinterpret_cast<int32_t*>(brushes + 0x804) = 0;
    ++cm_field<int32_t>(0x60);  // checkcount
    for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0095f4fc) + 0xc); ++k) {
        uint8_t* tree = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f4fc) + 0x10 + k * 4);
        if (!inside_xyz(sub(lanes(reinterpret_cast<const float*>(tree + 0x40)), lanes(maxs)),
                        sub(lanes(mins), lanes(reinterpret_cast<const float*>(tree + 0x50)))))
            continue;
        CM_GatherQuadTreeCandidates_r(tree, static_cast<uint32_t>(k), *reinterpret_cast<const int16_t* const*>(tree + 4),
                                      reinterpret_cast<const float*>(tree + 0x10), *reinterpret_cast<const float*>(tree + 0x20),
                                      mins, maxs, triangles, brushes);
    }
    if (*reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0095f4fc) + 0xc) > 0)
        result = *reinterpret_cast<const uint32_t*>(title_global<const uint8_t*>(0x0095f4fc) + 0xc);
    return result;
}

int32_t XB_REGS3 XB_CDECL CM_FilterBoxCandidates(uint8_t* out_brushes, uint32_t, const uint8_t* in_brushes, const float* mins,
                                                 const float* maxs, uint32_t contents, const uint8_t* in_triangles,
                                                 uint8_t* out_triangles) {
    *reinterpret_cast<int32_t*>(out_triangles + 0x1004) = 0;
    *reinterpret_cast<int32_t*>(out_brushes + 0x804) = 0;
    const int32_t n = *reinterpret_cast<const int32_t*>(in_brushes + 0x804);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* brush = (*reinterpret_cast<uint8_t* const* const*>(in_brushes + 0x800))[i];
        if (!(*reinterpret_cast<const uint32_t*>(brush + 8) & contents)) continue;
        if (!box_overlaps_object(mins, maxs, brush)) continue;
        if (uint32_t* slot = candidate_slot(out_brushes, 0x804, 4))
            slot[0] = (*reinterpret_cast<const uint32_t* const*>(in_brushes + 0x800))[i];
    }
    const int32_t m = *reinterpret_cast<const int32_t*>(in_triangles + 0x1004);
    for (int32_t j = 0; j < m; ++j) {
        const uint32_t* entry = *reinterpret_cast<const uint32_t* const*>(in_triangles + 0x1000) + j * 2;
        const uint8_t* mesh = reinterpret_cast<const uint8_t*>(uintptr_t{entry[0]});
        if (!(contents & *reinterpret_cast<const uint32_t*>(mesh + 8))) continue;
        if (!box_overlaps_object(mins, maxs, mesh)) continue;
        if (!box_overlaps_triangle(mins, maxs, mesh, entry[1])) continue;
        if (uint32_t* slot = candidate_slot(out_triangles, 0x1004, 8)) {
            slot[0] = entry[0];
            slot[1] = entry[1];
        }
    }
    return m;
}

PLATFORM_ASM_ENTRY(CM_SegmentCrossesMesh_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(CM_SegmentCrossesMesh) "addl $24, %esp\n"
                   "ret")

uint8_t XB_CDECL CM_SegmentCrossesMesh(const uint8_t* mesh, const float* maxs, const float* start, const float* end,
                                       const float* delta, const float* mins) {
    uint16_t* stack = reinterpret_cast<uint16_t*>(uintptr_t{0x0090a020});
    stack[1] = *reinterpret_cast<const uint16_t*>(mesh + 0x40);
    stack[0] = 0;
    stack[2] = 0;
    title_global<uint16_t>(0x0090a620) = 3;
    for (uint16_t node = CM_BoxTreeStack_Next(mesh, mins, maxs); node != 0xffff;
         node = CM_BoxTreeStack_Next(mesh, mins, maxs)) {
        const Lanes n = lanes(reinterpret_cast<const float*>(*reinterpret_cast<uint8_t* const*>(mesh + 0x44) + node * 16));
        const Lanes d = lanes(delta);
        if (!(0.0f > sum_xyz(mul(d, n)))) continue;
        const float dist_start = plane_distance(n, lanes(start));
        const float dist_end = plane_distance(n, lanes(end));
        if (!(0.0f > dist_start * dist_end)) continue;
        const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(mesh + 0x34);
        const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(mesh + 0x3c))[node];
        const Lanes v0 = lanes(reinterpret_cast<const float*>(rows + (flags & 0x3f) * 16));
        const Lanes v1 = lanes(reinterpret_cast<const float*>(rows + ((flags >> 6) & 0x3f) * 16));
        const Lanes v2 = lanes(reinterpret_cast<const float*>(rows + ((flags >> 12) & 0x3f) * 16));
        const Lanes tmax = max_lanes(max_lanes(v0, v1), v2);
        const Lanes tmin = min_lanes(min_lanes(v0, v1), v2);
        if (!inside_xyz(sub(tmin, lanes(maxs)), sub(lanes(mins), tmax))) continue;
        const float t = dist_start / (dist_start - dist_end);
        const Lanes p = add(lanes(start), mul(splat(t), d));
        const float s1 = edge_side(sub(v0, v1), sub(p, v1), n);
        const float s2 = edge_side(sub(v1, v2), sub(p, v2), n);
        const float s3 = edge_side(sub(v2, v0), sub(p, v0), n);
        const float order[4] = {s3, s2, s1, s1};  // SHUFPS lanes; CMPLTPS on all four
        int32_t mask = 0;
        for (int32_t i = 0; i < 4; ++i)
            if (order[i] < 0.0f) mask |= 1 << i;
        if (!(mask & 7)) return 1;
    }
    return 0;
}

uint8_t XB_CDECL CM_SightTraceRayThroughWorldTrees(uint8_t* tw, const float* start, const float* end) {
    const Lanes s = lanes(start);
    const Lanes e = lanes(end);
    alignas(16) float delta[4];
    const Lanes d = sub(e, s);
    std::memcpy(delta, d.v, 16);
    if (title_global<float>(0x0083eb94) > sum_xyz(mul(d, d))) return 0;  // 0.01
    const Lanes lo = min_lanes(s, e);
    const Lanes hi = max_lanes(s, e);
    int32_t& checkcount = cm_field<int32_t>(0x60);
    for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0095f4fc) + 0xc); ++k) {
        uint8_t* tree = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f4fc) + 0x10 + k * 4);
        const float* tree_mins = reinterpret_cast<const float*>(tree + 0x40);
        const float* tree_maxs = reinterpret_cast<const float*>(tree + 0x50);
        if (!inside_xyz(sub(lanes(tree_mins), hi), sub(lo, lanes(tree_maxs)))) continue;
        if (!CM_SegmentOverlapsBox(start, end, tree_mins, tree_maxs)) continue;
        const Lanes clip_lo = max_lanes(lo, lanes(tree_mins));
        const Lanes clip_hi = min_lanes(hi, lanes(tree_maxs));
        title_global<int32_t>(0x00960030) = 0;
        title_global<uint32_t>(0x00960034) = 0x009638c0;
        CM_QuadTreeTraceLeafs_r(tree, 0, start, end, reinterpret_cast<const float*>(tree + 0x10),
                                *reinterpret_cast<const float*>(tree + 0x20));
        for (uint32_t j = 0; j < title_global<uint32_t>(0x00960030); ++j) {
            const uint8_t* leaf = *reinterpret_cast<const uint8_t* const*>(title_global<const uint8_t*>(0x00960034) + j * 0x30);
            const int32_t n = *reinterpret_cast<const int32_t*>(leaf);
            for (int32_t i = 0; i < n; ++i) {
                uint8_t* object = leaf_object(tree, (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[i]);
                int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
                if (mark == checkcount) continue;
                if (!(*reinterpret_cast<const uint32_t*>(object + 8) & *reinterpret_cast<const uint32_t*>(tw + 0xf8))) continue;
                mark = checkcount;
                const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
                const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
                alignas(16) float omin[4], omax[4];
                const Lanes lo_o = sub(c, h);
                const Lanes hi_o = add(c, h);
                std::memcpy(omin, lo_o.v, 16);
                std::memcpy(omax, hi_o.v, 16);
                if (!inside_xyz(sub(lo_o, clip_hi), sub(clip_lo, hi_o))) {
                    const Lanes reach = sub(hi, lo_o);
                    platform_keep(reach);
                    if (!(0.0f <= reach.v[0])) break;  // CMPLESS on the x lane
                    continue;
                }
                if (!CM_SegmentOverlapsBox(start, end, omin, omax)) continue;
                if (object_type(object) == 9) {
                    CM_TraceBoxThroughBrush(tw, object);
                    if (!(title_global<float>(0x0083eb20) > *reinterpret_cast<const float*>(tw + 0x120))) continue;  // 1
                } else {
                    alignas(16) float box_lo[4], box_hi[4];
                    const Lanes bh = min_lanes(clip_hi, hi_o);
                    const Lanes bl = max_lanes(clip_lo, lo_o);
                    std::memcpy(box_hi, bh.v, 16);
                    std::memcpy(box_lo, bl.v, 16);
                    if (!CM_SegmentCrossesMesh(object, box_hi, start, end, delta, box_lo)) continue;
                }
                title_global<uint8_t*>(0x0094cba0) = object;  // the object hit
                return 1;
            }
        }
    }
    return 0;
}

PLATFORM_ASM_ENTRY(CM_SightTraceThroughWorldTrees_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CM_SightTraceThroughWorldTrees) "addl $4, %esp\n"
                   "ret")

uint8_t XB_CDECL CM_SightTraceThroughWorldTrees(uint8_t* tw) {
    alignas(16) float start[4], end[4];
    std::memcpy(start, tw, 16);
    std::memcpy(end, tw + 0x10, 16);
    const Lanes s = lanes(start);
    const Lanes e = lanes(end);
    int32_t& checkcount = cm_field<int32_t>(0x60);
    const Lanes extent = lanes(tw_row(tw, 0x150));
    const Lanes lo = sub(min_lanes(s, e), extent);
    const Lanes hi = add(max_lanes(s, e), extent);
    float* const tw_start_z = reinterpret_cast<float*>(tw + 8);
    float* const tw_end_z = reinterpret_cast<float*>(tw + 0x18);
    for (int32_t k = 0; k < *reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0095f4fc) + 0xc); ++k) {
        uint8_t* tree = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f4fc) + 0x10 + k * 4);
        if (!inside_xyz(sub(lanes(reinterpret_cast<const float*>(tree + 0x40)), hi),
                        sub(lo, lanes(reinterpret_cast<const float*>(tree + 0x50)))))
            continue;
        title_global<int32_t>(0x00960030) = 0;
        title_global<uint32_t>(0x00960034) = 0x009638c0;
        CM_QuadTreeTraceLeafs_r(tree, 0, start, end, reinterpret_cast<const float*>(tree + 0x10),
                                *reinterpret_cast<const float*>(tree + 0x20));
        for (uint32_t j = 0; j < title_global<uint32_t>(0x00960030); ++j) {
            const uint8_t* leaf = *reinterpret_cast<const uint8_t* const*>(title_global<const uint8_t*>(0x00960034) + j * 0x30);
            const int32_t n = *reinterpret_cast<const int32_t*>(leaf);
            // Pass 1: brushes (type 9); any fraction below 1 is a hit.
            for (int32_t i = 0; i < n; ++i) {
                uint8_t* object = leaf_object(tree, (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[i]);
                if (object_type(object) != 9) continue;
                int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
                if (mark == checkcount) continue;
                if (!(*reinterpret_cast<const uint32_t*>(object + 8) & *reinterpret_cast<const uint32_t*>(tw + 0xf8))) continue;
                mark = checkcount;
                const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
                const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
                const Lanes below = sub(lo, c);
                const Lanes above = sub(c, hi);
                int32_t inside = 0;
                for (int32_t a = 0; a < 4; ++a) {
                    const float m = above.v[a] > below.v[a] ? above.v[a] : below.v[a];
                    if (m < h.v[a]) inside |= 1 << a;
                }
                if ((inside & 7) != 7) {
                    const Lanes reach = add(sub(hi, c), h);
                    platform_keep(reach);
                    if (!(0.0f <= reach.v[0])) break;
                    continue;
                }
                if (*reinterpret_cast<const uint32_t*>(tw + 0x160))
                    CM_TraceCapsuleThroughBrush(tw, object);
                else
                    CM_TraceBoxThroughBrush(tw, object);
                if (title_global<float>(0x0083eb20) > *reinterpret_cast<const float*>(tw + 0x120)) return 1;  // 1
            }
            // Pass 2: mesh triangles with the sphere at the capsule's lower end.
            const float radius = *reinterpret_cast<const uint32_t*>(tw + 0x160) ? *reinterpret_cast<const float*>(tw + 0x164)
                                                                                 : *reinterpret_cast<const float*>(tw + 0x50);
            const float saved_start_z = *tw_start_z;
            const float saved_end_z = *tw_end_z;
            const float drop = *reinterpret_cast<const float*>(tw + 0x168) - radius;
            *tw_start_z = *tw_start_z - drop;
            const float rr = radius + title_global<float>(0x0083eb4c);  // 0.001
            *tw_end_z = *tw_end_z - drop;
            alignas(16) float a_pt[4], b_pt[4], dir[4], lo2[4], hi2[4];
            std::memcpy(a_pt, tw, 16);
            std::memcpy(b_pt, tw + 0x10, 16);
            const Lanes a4 = lanes(a_pt), b4 = lanes(b_pt);
            const Lanes rr4 = splat(rr);
            const Lanes l2 = sub(min_lanes(a4, b4), rr4);
            const Lanes h2 = add(max_lanes(a4, b4), rr4);
            std::memcpy(lo2, l2.v, 16);
            std::memcpy(hi2, h2.v, 16);
            const Lanes d = sub(b4, a4);
            const Lanes dn = div_lanes(d, splat(platform_x87_sqrt(sum_xyz(mul(d, d)))));
            platform_keep(dn);
            std::memcpy(dir, dn.v, 16);
            for (int32_t i = 0; i < n; ++i) {
                uint8_t* object = leaf_object(tree, (*reinterpret_cast<const uint16_t* const*>(leaf + 4))[i]);
                if (object_type(object) == 9) continue;
                int32_t& mark = *reinterpret_cast<int32_t*>(object + 0xc);
                if (mark == checkcount) continue;
                if (!(*reinterpret_cast<const uint32_t*>(object + 8) & *reinterpret_cast<const uint32_t*>(tw + 0xf8))) continue;
                mark = checkcount;
                const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
                const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
                const Lanes below = sub(l2, c);
                const Lanes above = sub(c, h2);
                int32_t inside = 0;
                for (int32_t a = 0; a < 4; ++a) {
                    const float m = above.v[a] > below.v[a] ? above.v[a] : below.v[a];
                    if (m < h.v[a]) inside |= 1 << a;
                }
                if ((inside & 7) != 7) {
                    const Lanes reach = add(sub(h2, c), h);
                    platform_keep(reach);
                    if (!(0.0f <= reach.v[0])) break;
                    continue;
                }
                uint16_t* stack = reinterpret_cast<uint16_t*>(uintptr_t{0x0090a020});
                stack[1] = *reinterpret_cast<const uint16_t*>(object + 0x40);
                stack[0] = 0;
                stack[2] = 0;
                title_global<uint16_t>(0x0090a620) = 3;
                for (uint16_t node = CM_BoxTreeStack_Next(object, lo2, hi2); node != 0xffff;
                     node = CM_BoxTreeStack_Next(object, lo2, hi2)) {
                    alignas(16) float plane[4];
                    std::memcpy(plane, *reinterpret_cast<uint8_t* const*>(object + 0x44) + node * 16, 16);
                    const Lanes p = lanes(plane);
                    const Lanes limit4 = sub(rr4, splat(p.v[3]));
                    platform_keep(limit4);
                    const float limit = limit4.v[0];
                    const float dist_b = sum_xyz(mul(p, b4));
                    const float dist_a = sum_xyz(mul(p, a4));
                    const float dists[4] = {dist_a, dist_b, 0.0f, 0.0f};
                    int32_t beyond = 0;
                    for (int32_t a = 0; a < 4; ++a)  // CMPLTPS on all four lanes
                        if (limit < dists[a]) beyond |= 1 << a;
                    if ((beyond & 3) == 3) continue;
                    const uint8_t* rows = *reinterpret_cast<uint8_t* const*>(object + 0x34);
                    const uint32_t flags = (*reinterpret_cast<const uint32_t* const*>(object + 0x3c))[node];
                    alignas(16) float v0[4], v1[4], v2[4];
                    std::memcpy(v0, rows + (flags & 0x3f) * 16, 16);
                    std::memcpy(v1, rows + ((flags >> 6) & 0x3f) * 16, 16);
                    std::memcpy(v2, rows + ((flags >> 12) & 0x3f) * 16, 16);
                    const Lanes l0 = lanes(v0), l1 = lanes(v1), lv2 = lanes(v2);
                    const Lanes tmax = max_lanes(l0, max_lanes(l1, lv2));
                    const Lanes tmin = min_lanes(l0, min_lanes(l1, lv2));
                    if (!inside_xyz(sub(tmin, h2), sub(l2, tmax))) continue;
                    alignas(16) float clipped_end[4];
                    std::memcpy(clipped_end, b_pt, 16);
                    uint8_t solid;
                    if (CM_ClipSphereSweepToTriangle(0, a_pt, v0, clipped_end, dir, rr, v1, v2, plane, &solid)) {
                        *tw_start_z = saved_start_z;
                        *tw_end_z = saved_end_z;
                        return 1;
                    }
                }
            }
            *tw_start_z = saved_start_z;
            *tw_end_z = saved_end_z;
        }
    }
    return 0;
}

}  // namespace game
