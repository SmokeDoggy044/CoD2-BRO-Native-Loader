#include "cm.h"

#include <cstring>

#include "lanes.h"
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

}  // namespace game
