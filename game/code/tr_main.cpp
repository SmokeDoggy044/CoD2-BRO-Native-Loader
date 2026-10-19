#include "tr_main.h"

#include <cstring>

#include "title_data.h"

namespace game {

namespace {

struct Cvar;  // Q3's cvar_t; only `value` is needed so far
float cvar_value(const Cvar* cvar) {
    return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(cvar) + 0x1c);
}

// Scene lists: counts filled by the RE_Add*ToScene side and where the current
// scene's entries start in each (three pairs, which list is which is not known yet).
uint32_t& scene_count_a() { return title_global<uint32_t>(0x0095e628); }
uint32_t& scene_count_b() { return title_global<uint32_t>(0x0095e60c); }
uint32_t& scene_count_c() { return title_global<uint32_t>(0x0095e738); }
uint32_t& scene_first_a() { return title_global<uint32_t>(0x0095e6bc); }
uint32_t& scene_first_b() { return title_global<uint32_t>(0x0095e710); }
uint32_t& scene_first_c() { return title_global<uint32_t>(0x0095e6f4); }

}  // namespace

void XB_REGS3 myGLMultMatrix(const float* a, float* out, const float* b) {
    // Same operation order as the original (its float sums are not associative),
    // reading `a` after each store as it does, in case out == a.
    for (int i = 0; i < 4; ++i, a += 4, out += 4) {
        out[0] = a[2] * b[8] + b[12] * a[3] + b[4] * a[1] + a[0] * b[0];
        out[1] = a[3] * b[13] + a[1] * b[5] + b[9] * a[2] + a[0] * b[1];
        out[2] = a[3] * b[14] + a[1] * b[6] + a[0] * b[2] + b[10] * a[2];
        out[3] = b[11] * a[2] + a[3] * b[15] + a[1] * b[7] + b[3] * a[0];
    }
}

void XB_CDECL RE_ClearScene() {
    scene_first_a() = scene_count_a();
    scene_first_b() = scene_count_b();
    scene_first_c() = scene_count_c();
}

void XB_CDECL R_ClearFrameLists() {
    RFrameList* lists = title_global<RFrameList*>(0x008db128);
    for (int i = 0; i < 2; ++i) {
        lists[i].count = 0;
        lists[i].head = &lists[i].tail;
        lists[i].tail = nullptr;
        lists[i].tail_prev = &lists[i].head;
    }
    title_global<uint32_t>(0x0095e708) = 0;
    title_global<uint32_t>(0x0095e6e8) = 0;
    scene_count_a() = 0;
    scene_count_b() = 0;
    scene_count_c() = 0;
    scene_first_a() = 0;
    scene_first_b() = 0;
    scene_first_c() = 0;
}

void XB_CDECL RE_SetFarCullDist(float distance) {
    // A NaN is stored as given, as in the original.
    title_global<float>(0x0095e2cc) = 0.0f >= distance ? 0.0f : distance;
}

float XB_CDECL RE_GetFarCullDist() {
    const float zfar = cvar_value(title_global<const Cvar*>(0x0095e378));
    const float far_cull = title_global<float>(0x0095e2cc);
    return far_cull > zfar ? far_cull : zfar;
}

void XB_CDECL GLimp_Init() {
    // glconfig_t; the strings are the title's own (pointers, not Q3's arrays).
    auto set = [](uint32_t address, uint32_t value) { title_global<uint32_t>(address) = value; };
    set(0x0095e760, 24);  // colorBits
    set(0x0095e764, 24);  // depthBits
    title_global<float>(0x0095e774) = title_global<float>(0x0083eb20);  // 1.0
    set(0x0095e740, 0x0083c0e8);  // "porting to NGL"
    set(0x0095e744, 0x0083c0d8);
    set(0x0095e748, 0x0083c0cc);  // "whatever"
    set(0x0095e74c, 0x0083c0a8);
    set(0x0095e750, 0x0083c0a0);
    set(0x0095e754, 0x200);  // maxTextureSize
    set(0x0095e758, 2);      // maxActiveTextures
    set(0x0095e75c, 8);
    set(0x0095e768, 8);      // stencilBits
    set(0x0095e76c, 0);
    set(0x0095e770, 0);
    for (uint32_t address = 0x0095e778; address <= 0x0095e7c0; address += 4) set(address, 0);
    set(0x0095e7c4, 640);         // vidWidth
    set(0x0095e7c8, 480);         // vidHeight
    set(0x0095e7cc, 0x3faaaaab);  // windowAspect 4/3
    set(0x0095e7d0, 60);          // displayFrequency
    set(0x0095e7d4, 0);
    set(0x0095e7d8, 0);
    set(0x0095e7dc, 0);
}

int32_t XB_REGS2 Q_PrintStrlen(uint32_t, const char* string) {
    int32_t length = 0;
    if (!*string) return 0;
    char next;
    do {
        if (string[0] == '^' && string[1] && string[1] != '^' && string[1] >= '0' && string[1] <= '9')
            string += 2;
        else
            ++length;
        next = string[1];
        ++string;
    } while (next);
    return length;
}

void XB_CDECL RE_LoadWorldMap() {
    title_global<uint32_t>(0x0095e608) = 0x00988bd8;  // the world
    title_global<uint32_t>(0x00988cd8) = title_global<uint32_t>(0x0095f6b8);  // cm
}

void XB_CDECL R_AddViewModelRefEntity(uint8_t* entity) {
    if (!title_global<uint32_t>(0x0095e390)) return;
    // The list node sits 0x80 bytes before the entity.
    auto* node = reinterpret_cast<void**>(entity - 0x80);
    entity[0x6c] = 0;
    *reinterpret_cast<uint32_t*>(entity + 0x70) = 0;
    RFrameList& list = title_global<RFrameList*>(0x008db128)[1];
    node[0] = &list.tail;
    node[1] = list.tail_prev;
    *static_cast<void**>(list.tail_prev) = node;
    list.tail_prev = node;
    ++list.count;
}

int32_t XB_REGS3 R_CullPortal(const uint8_t* portal, const float* planes, int32_t count) {
    const float* points = *reinterpret_cast<const float* const*>(portal + 0x18);
    const int32_t point_count = *reinterpret_cast<const int32_t*>(portal + 0x1c);
    for (; count != 0; --count, planes += 5) {
        int32_t i = 0;
        for (; i != point_count; ++i) {
            const float* p = points + i * 4;
            if (p[2] * planes[2] + p[1] * planes[1] + p[0] * planes[0] > planes[3]) break;
        }
        if (i == point_count) return 1;
    }
    return 0;
}

void XB_CDECL R_SetupProjection() {
    float zfar;
    if (title_global<uint8_t>(0x0095e600) & 1) {
        zfar = 2048.0f;
    } else {
        zfar = cvar_value(title_global<const Cvar*>(0x0095e378));
        if (zfar == 0.0f) zfar = 524288.0f;
    }
    title_global<float>(0x0095e514) = zfar;

    float* m = &title_global<float>(0x0095e4d4);
    const float pi_over_360 = 0x1.1df46ap-7f;  // 0x3c0efa35
    const float xmax = platform_x87_tan(title_global<float>(0x0095e5f8) * pi_over_360);
    const float znear = cvar_value(title_global<const Cvar*>(0x0095e7e0));
    static const int kZero[] = {4, 12, 1, 13, 2, 6, 3, 7, 15};  // no braced-list range-for (ngl.cpp)
    for (int i = 0; i < 9; ++i) m[kZero[i]] = 0.0f;
    m[14] = znear * -2.0f;
    const float xmin = 0.0f - xmax;
    const float width = xmax - xmin;
    const float x_sum = xmin + xmax;
    const float ymax = platform_x87_tan(title_global<float>(0x0095e5f4) * pi_over_360);
    const float ymin = 0.0f - ymax;
    const float height = ymax - ymin;
    const float y_sum = ymin + ymax;
    const float inv_height = 1.0f / height;
    m[8] = y_sum * inv_height;
    const float inv_width = 1.0f / width;
    m[9] = x_sum * inv_width;
    m[0] = inv_height * 2.0f;
    m[5] = inv_width * 2.0f;
    m[10] = -1.0f;
    m[11] = -1.0f;
}

void XB_CDECL R_CopyFrustumPlanes() {
    const float* view_planes = &title_global<float>(0x0095e518);
    RCullPlane* planes = &title_global<RCullPlane>(0x0095e24c);
    for (int i = 0; i < 4; ++i) {
        const float* source = view_planes + i * 5;
        RCullPlane& plane = planes[i];
        for (int axis = 0; axis < 3; ++axis) plane.normal[axis] = source[axis];
        plane.dist = source[3] - 0.005f;
        for (int axis = 0; axis < 3; ++axis) {
            int32_t bits;
            std::memcpy(&bits, &plane.normal[axis], sizeof bits);
            plane.box_offset[axis] = static_cast<uint8_t>((bits > 0 ? 12 : 0) + 4 * axis);
        }
    }
}

float* XB_REGS1 XB_CDECL R_ClipPolygonToPlane(const float* plane, float* in, int32_t* count, float* out) {
    enum { kFront, kBack, kOn };
    int32_t sides[64];
    float dists[64];
    int32_t front = 0;
    int32_t back = 0;
    int32_t i = 0;
    for (; i < *count; ++i) {
        const float* p = in + i * 4;
        const float d = p[2] * plane[2] + p[0] * plane[0] + p[1] * plane[1] - plane[3];
        dists[i] = d;
        sides[i] = kOn;
        if (-0.005f > d) {
            sides[i] = kBack;
            ++back;
        } else if (d > 0.005f) {
            sides[i] = kFront;
            ++front;
        }
    }
    if (front == 0) {
        *count = 0;
        return nullptr;
    }
    if (back == 0) return in;
    sides[i] = sides[0];
    dists[i] = dists[0];

    int32_t out_count = 0;
    float* o = out;
    for (i = 0; i < *count; ++i) {
        const float* p = in + i * 4;
        const int32_t side = sides[i];
        if (side == kOn) {
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o += 4;
            ++out_count;
            continue;
        }
        if (side == kFront) {
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o += 4;
            ++out_count;
        }
        if (sides[i + 1] == kOn || sides[i + 1] == side) continue;
        const float* q = in + ((i + 1) % *count) * 4;
        const float t = dists[i] / (dists[i] - dists[i + 1]);
        o[0] = (q[0] - p[0]) * t + p[0];
        o[1] = (q[1] - p[1]) * t + p[1];
        o[2] = (q[2] - p[2]) * t + p[2];
        o += 4;
        if (++out_count == 32) break;
    }
    *count = out_count;
    return out;
}

namespace {

// The box corner picked by the plane's byte offsets (+0x10..0x12, into the link's
// mins/maxs at +4), in the original's operation order for portal planes...
float portal_plane_distance(const uint8_t* link, const uint8_t* plane) {
    auto corner = [&](int axis) { return *reinterpret_cast<const float*>(link + 4 + plane[0x10 + axis]); };
    const float* normal = reinterpret_cast<const float*>(plane);
    return corner(0) * normal[0] + corner(1) * normal[1] + corner(2) * normal[2];
}

// ...and for the view's near and far planes (summed from z down).
float view_plane_distance(const uint8_t* link, const uint8_t* plane) {
    auto corner = [&](int axis) { return *reinterpret_cast<const float*>(link + 4 + plane[0x10 + axis]); };
    const float* normal = reinterpret_cast<const float*>(plane);
    return corner(2) * normal[2] + corner(1) * normal[1] + corner(0) * normal[0];
}

bool behind(const uint8_t* plane, float distance) {
    return reinterpret_cast<const float*>(plane)[3] > distance;
}

}  // namespace

void XB_CDECL R_AddCellEntities(const uint8_t* cell, const uint8_t* planes, int32_t count) {
    for (const uint8_t* link = *reinterpret_cast<uint8_t* const*>(cell + 0x38); link;
         link = *reinterpret_cast<uint8_t* const*>(link + 0x1c)) {
        uint8_t* entity = *reinterpret_cast<uint8_t* const*>(link);
        if (!entity[0xec]) continue;
        bool culled = false;
        for (int32_t i = 0; i < count && !culled; ++i) {
            const uint8_t* plane = planes + i * 20;
            culled = behind(plane, portal_plane_distance(link, plane));
        }
        if (culled) continue;
        const uint8_t* near_plane = title_global<const uint8_t*>(0x0095e2c4);
        const uint8_t* far_plane = title_global<const uint8_t*>(0x0095e2c8);
        if (behind(near_plane, view_plane_distance(link, near_plane))) continue;
        if (far_plane && behind(far_plane, view_plane_distance(link, far_plane))) continue;
        entity[0xec] = 0;
        const uint8_t* world = *reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095e608) + 0x100);
        const uint8_t* cells = *reinterpret_cast<uint8_t* const*>(world + 0x1c);
        *reinterpret_cast<int32_t*>(entity + 0xf0) =
            static_cast<int32_t>(reinterpret_cast<intptr_t>(cell) - reinterpret_cast<intptr_t>(cells)) / 0x50;
    }
}

}  // namespace game
