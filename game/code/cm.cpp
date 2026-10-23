#include "cm.h"

#include <cstring>

#include "lanes.h"
#include "qcommon.h"
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

}  // namespace game
