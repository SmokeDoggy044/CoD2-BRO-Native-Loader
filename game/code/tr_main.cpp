#include "tr_main.h"

#include <cstring>

#include "dobj.h"
#include "lanes.h"
#include "ngl.h"
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

void XB_CDECL R_SetupFog() {
    if (title_global<int32_t>(0x008db100) == 0) return;
    nglSetFog(title_global<float>(0x0095f698), title_global<float>(0x008db104), title_global<float>(0x0095f69c),
              title_global<float>(0x008db108) * 0.5f);
    nglSetFogColor(title_global<float>(0x008db10c), title_global<float>(0x008db110), title_global<float>(0x008db114));
}

void XB_CDECL RE_SetColor(const float* rgba) {
    uint32_t& color = title_global<uint32_t>(0x0095e6d0);
    if (!rgba) {
        color = 0xffffffff;
        return;
    }
    uint32_t argb = static_cast<uint32_t>(platform_ftol(rgba[3] * 255.0f));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(rgba[0] * 255.0f));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(rgba[1] * 255.0f));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(rgba[2] * 255.0f));
    color = argb;
}

void XB_CDECL RE_StretchPic(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                            void* shader) {
    if (!title_global<NglScene*>(0x0090259c)) return;
    NglQuad quad;
    nglInitQuad(&quad);
    nglSetQuadRect(&quad, x, y, x + w, y + h);
    nglSetQuadUV(&quad, s0, t0, s1, t1);
    nglSetQuadColor(&quad, title_global<uint32_t>(0x0095e6d0));
    quad.texture = static_cast<NglTexture*>(shader);
    nglListAddQuad(&quad);
}

void XB_CDECL RE_DrawQuadPic(const float* points, const float* coords, void* shader) {
    if (!title_global<NglScene*>(0x0090259c)) return;
    NglQuad quad;
    nglInitQuad(&quad);
    nglSetQuadColor(&quad, title_global<uint32_t>(0x0095e6d0));
    static const int kVertex[4] = {0, 1, 3, 2};  // clockwise corners -> quad order
    for (int i = 0; i < 4; ++i) {
        NglQuadVertex& vertex = quad.verts[kVertex[i]];
        vertex.x = points[2 * i];
        vertex.y = points[2 * i + 1];
    }
    for (int i = 0; i < 4; ++i) {
        NglQuadVertex& vertex = quad.verts[kVertex[i]];
        vertex.u = coords[2 * i];
        vertex.v = coords[2 * i + 1];
    }
    quad.texture = static_cast<NglTexture*>(shader);
    nglListAddQuad(&quad);
}

void XB_CDECL R_DrawPic(float x, float y, float w, float h, void* shader) {
    title_function<void XB_CDECL(float*, float*, float*, float*)>(title_global<uint32_t>(0x0095e6b0))(&x, &y, &w, &h);
    RE_StretchPic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, shader);
}

void XB_CDECL R_ClearAlpha() {
    // Bit 7 of the shader disable mask turns the pass off.
    if (title_global<int8_t>(0x00901e8c) < 0) return;
    // Alpha 0 everywhere, then alpha 0x80 where the depth test passes at 0.9 * zfar.
    for (int pass = 0; pass < 2; ++pass) {
        nglPushScene(1);
        nglSetClearFlags(0);
        nglSetColorWriteMask(0x01000000);
        nglSetSceneDepthEnable(0);
        nglSetSceneDepthTest(pass);
        NglQuad quad;
        nglInitQuad(&quad);
        const float height = static_cast<float>(nglGetScreenHeight());
        const float width = static_cast<float>(nglGetScreenWidth());
        nglSetQuadRect(&quad, 0.0f, 0.0f, width, height);
        if (pass == 1) nglSetQuadZ(&quad, title_global<float>(0x008db194) * title_global<float>(0x0095e514));
        nglSetQuadColor(&quad, pass == 0 ? 0x00000000u : 0x80000000u);
        nglSetQuadBlend(&quad, 0, 0);
        nglListAddQuad(&quad);
        nglPopScene();
    }
    title_function<void XB_CDECL()>(0x00045ef0)();
}

uint32_t XB_CDECL RE_TextHeight(uint32_t font, float scale) {
    const float size = title_global<float>(0x008db12c) * scale;
    const uint32_t handle = title_function<uint32_t XB_CDECL(uint32_t, float)>(title_global<uint32_t>(0x0095e6b4))(font, size);
    uint32_t width;
    uint32_t height;
    title_function<void XB_CDECL(uint32_t, uint32_t*, uint32_t*, float, float, const char*, ...)>(0x00066310)(
        handle, &width, &height, size, size, "LltygI");
    return height;
}

PLATFORM_ASM_ENTRY(R_CullPointAndRadius_entry,
                   "subl $4, %esp\n"
                   "movss %xmm2, (%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(R_CullPointAndRadius) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL R_CullPointAndRadius(const float* point, float radius) {
    const uint8_t* nocull = title_global<const uint8_t*>(0x0095e6c8);
    if (*reinterpret_cast<const int32_t*>(nocull + 0x20)) return 1;
    const float negative = 0.0f - radius;
    bool clipped = false;
    const float* plane = &title_global<float>(0x0095e518);  // normal, dist, type/signbits
    for (int i = 0; i < 4; ++i, plane += 5) {
        float dist = plane[0] * point[0] + plane[2] * point[2];
        dist = dist + point[1] * plane[1];
        dist = dist - plane[3];
        if (negative > dist) return 2;
        if (radius >= dist) clipped = true;
    }
    return clipped;
}

// Five stack arguments copied below the colour.
PLATFORM_ASM_ENTRY(R_DrawFilledRect_entry,
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(R_DrawFilledRect) "addl $24, %esp\n"
                   "ret")

void XB_CDECL R_DrawFilledRect(const float* colour, float x0, float y0, float x1, float y1, float z) {
    NglQuad quad;
    nglInitQuad(&quad);
    nglSetQuadZ(&quad, z);
    nglSetQuadRect(&quad, x0, y0, x1, y1);
    const float scale = title_global<float>(0x0083eb8c);  // 255
    uint32_t argb = static_cast<uint32_t>(platform_ftol(colour[3] * scale));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(colour[0] * scale));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(colour[1] * scale));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(colour[2] * scale));
    nglSetQuadColor(&quad, argb);
    nglListAddQuad(&quad);
}

PLATFORM_ASM_ENTRY(R_SetInstanceColor_entry,
                   "pushl %ecx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(R_SetInstanceColor) "addl $8, %esp\n"
                   "ret")

void XB_CDECL R_SetInstanceColor(uint32_t* const* params, const float* colour) {
    uintptr_t& next = title_global<uintptr_t>(0x009025a8);
    const uintptr_t block = (next + 15) & ~uintptr_t{15};
    next = block + 16;
    float* copy = nullptr;
    if (block) {
        copy = reinterpret_cast<float*>(block);
        std::memcpy(copy, colour, 16);
    }
    const uint32_t slot = title_global<uint32_t>(0x00902908);
    uint32_t* block_words = *params;
    block_words[0] |= 1u << (slot & 31);
    block_words[2 + slot] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(copy));
}

PLATFORM_ASM_ENTRY(R_ApplyCachedLighting_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(R_ApplyCachedLighting) "addl $4, %esp\n"
                   "ret")

void XB_CDECL R_ApplyCachedLighting(const RCachedLighting* lighting) {
    nglSetAmbientColor(lighting->ambient[0], lighting->ambient[1], lighting->ambient[2]);
    // The title copies four floats of each direction (12 bytes apart) to the stack: the
    // fourth is the next direction's x, or the count after the last.
    for (int32_t i = 0; i < lighting->count; ++i)
        nglAddDirectionalLight(0xffffffff, lighting->directions[i], lighting->colors[i]);
}

PLATFORM_ASM_ENTRY(R_AddRefEntityToScene_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(R_AddRefEntityToScene) "addl $8, %esp\n"
                   "ret")

void XB_CDECL R_AddRefEntityToScene(uint8_t* ref_entity, int32_t cell) {
    if (!title_global<uint32_t>(0x0095e390)) return;
    const int32_t type = *reinterpret_cast<const int32_t*>(ref_entity);
    if (type < 0 || type >= 16)
        title_function<void XB_CDECL(int32_t, const char*, ...)>(title_global<uint32_t>(0x0095e634))(
            1, reinterpret_cast<const char*>(0x0083bef8), type);
    uint8_t* entity = ref_entity - 0x80;
    auto* node = reinterpret_cast<void**>(entity);
    *reinterpret_cast<int32_t*>(entity + 0xf0) = cell;
    entity[0xec] = 0;
    RFrameList& list = *title_global<RFrameList*>(0x008db128);
    node[0] = &list.tail;
    node[1] = list.tail_prev;
    *static_cast<void**>(list.tail_prev) = node;
    list.tail_prev = node;
    ++list.count;
}

PLATFORM_ASM_ENTRY(R_AddCellEntityLink_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(R_AddCellEntityLink) "addl $16, %esp\n"
                   "ret")

void XB_CDECL R_AddCellEntityLink(uint8_t* cell, const float* mins, const float* maxs, void* entity) {
    int32_t& count = title_global<int32_t>(0x0095e2d4);
    if (count >= 256) {
        title_function<void XB_CDECL()>(0x00042460)();  // an assert that does nothing here
        return;
    }
    RCellEntityLink*& first = *reinterpret_cast<RCellEntityLink**>(cell + 0x38);
    for (RCellEntityLink* link = first; link; link = link->next) {
        if (link->entity != entity) continue;
        // Grow the box; NaN comparisons keep the old bound, as comiss/jbe does.
        if (link->mins[0] > mins[0]) link->mins[0] = mins[0];
        if (maxs[0] > link->maxs[0]) link->maxs[0] = maxs[0];
        if (link->mins[1] > mins[1]) link->mins[1] = mins[1];
        if (maxs[1] > link->maxs[1]) link->maxs[1] = maxs[1];
        if (link->mins[2] > mins[2]) link->mins[2] = mins[2];
        if (maxs[2] > link->maxs[2]) link->maxs[2] = maxs[2];
        return;
    }
    RCellEntityLink* link = title_global<RCellEntityLink*>(0x0095e2d0) + count;
    ++count;
    link->entity = entity;
    std::memcpy(link->mins, mins, sizeof(link->mins));
    std::memcpy(link->maxs, maxs, sizeof(link->maxs));
    link->next = first;
    first = link;
}

PLATFORM_ASM_ENTRY(R_RotateForEntity_entry,
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(R_RotateForEntity) "addl $12, %esp\n"
                   "ret")

void XB_CDECL R_RotateForEntity(const uint8_t* entity, ROrientation* orientation, const uint8_t* view_parms) {
    // The view parameters start with the viewer's orientation; the world's follows at +0x7c.
    const float* view_origin = reinterpret_cast<const float*>(view_parms);
    const auto* world = reinterpret_cast<const ROrientation*>(view_parms + 0x7c);
    std::memcpy(orientation->origin, entity + 0xbc, 12);
    std::memcpy(orientation->axis, entity + 0x94, 36);
    const float(*axis)[3] = orientation->axis;
    const float* origin = orientation->origin;
    alignas(16) float matrix[16];
    matrix[0] = axis[0][0];
    matrix[4] = axis[1][0];
    matrix[8] = axis[2][0];
    matrix[12] = origin[0];
    matrix[1] = axis[0][1];
    matrix[5] = axis[1][1];
    matrix[9] = axis[2][1];
    matrix[13] = origin[1];
    matrix[2] = axis[0][2];
    matrix[6] = axis[1][2];
    matrix[10] = axis[2][2];
    matrix[14] = origin[2];
    matrix[3] = 0.0f;
    matrix[7] = 0.0f;
    matrix[11] = 0.0f;
    matrix[15] = title_global<float>(0x0083eb20);  // 1
    myGLMultMatrix(matrix, orientation->model_matrix, world->model_matrix);
    // The view origin in the entity's frame.
    const float d0 = view_origin[0] - origin[0];
    const float d2 = view_origin[2] - origin[2];
    const float d1 = view_origin[1] - origin[1];
    orientation->view_origin[0] = axis[0][0] * d0 + axis[0][2] * d2 + d1 * axis[0][1];
    orientation->view_origin[1] = axis[1][1] * d1 + axis[1][0] * d0 + d2 * axis[1][2];
    orientation->view_origin[2] = axis[2][2] * d2 + axis[2][1] * d1 + axis[2][0] * d0;
}

void XB_REGS3 R_SetupEntityOrientation(const uint8_t* entity, ROrientation* orientation, const uint8_t* view_parms) {
    const int32_t type = *reinterpret_cast<const int32_t*>(entity + 0x80);
    if (type == 1 || type == 2 || type == 0)
        R_RotateForEntity(entity, orientation, view_parms);
    else
        std::memcpy(orientation, view_parms + 0x7c, sizeof(ROrientation));  // the view's world orientation
}

// Three stack arguments copied below the two register ones.
PLATFORM_ASM_ENTRY(R_LayoutTextWithIcons_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(R_LayoutTextWithIcons) "addl $20, %esp\n"
                   "ret")

int32_t XB_CDECL R_LayoutTextWithIcons(char** out, float* colour, const uint16_t* text, int32_t* count,
                                       const uint16_t** rest) {
    char* buffer = &title_global<char>(0x0098b5d8);
    *out = buffer;
    if (*count > 255) *count = 255;
    buffer[0] = 0;
    int32_t pos = 0;
    int32_t trailing_space = -1;
    int32_t i = 0;
    uint32_t code = 7;
    int32_t stopped = 0;
    if (*count > 0) {
        const float scale = title_global<float>(0x0083eb30);
        do {
            const uint16_t ch = text[i];
            const uint32_t kind = ch >> 8;
            if (kind != code) {
                if (kind == 0x0d || kind == 0x10 || kind == 0x11 || kind == 0x12) {
                    // An icon: the caller draws it and continues after it.
                    *rest = &text[i];
                    trailing_space = -1;
                    stopped = 1;
                    break;
                }
                if (kind == 0x0a || kind == 0x0b || kind == 0x0c) {
                    // Red, green, blue of the text colour.
                    if (colour) colour[kind - 0x0a] = static_cast<float>(static_cast<int32_t>(ch & 0xff)) * scale;
                    continue;
                }
                buffer[pos++] = '^';
                code = kind;
                buffer[pos++] = static_cast<char>(kind + '0');
            }
            const char c = static_cast<char>(text[i]);
            buffer[pos] = c;
            if (c != ' ')
                trailing_space = -1;
            else if (trailing_space == -1)
                trailing_space = pos;
            ++pos;
        } while (++i < *count);
    }
    buffer[pos] = 0;
    if (trailing_space >= 0) buffer[trailing_space] = 0;
    *rest = i == *count ? nullptr : &text[i];
    *count -= i;
    return stopped;
}

namespace {

const XModelLod* first_lod(const XModel* model) {
    const XModelLod* const* lod = model->lods;
    while (!*lod) ++lod;
    return *lod;
}

}  // namespace

void XB_REGS1 XB_CDECL R_AddStaticModel(RStaticModel* model) {
    const XModel* xmodel = model->xmodel;
    const XModelParts* parts = first_lod(xmodel)->parts;
    if (parts && static_cast<int32_t>(first_lod(xmodel)->parts->bone_count) > 32) return;

    // The bone matrices, a function-static array of 32 (constructed on first use).
    uint32_t& guards = title_global<uint32_t>(0x0098e420);
    float* bones = &title_global<float>(0x0098dc20);
    if (!(guards & 1)) {
        guards |= 1;
        title_function<void XB_STDCALL(void*, uint32_t, uint32_t, uint32_t)>(0x00011240)(bones, 0x40, 0x20,
                                                                                      0x0001b920);
    }
    title_function<void XB_CDECL(const XModel*, float*)>(0x00241ff0)(xmodel, bones);

    const float one = title_global<float>(0x0083eb20);
    const Lanes m0 = {{model->axis[0][0], model->axis[0][1], model->axis[0][2], 0.0f}};
    const Lanes m1 = {{model->axis[1][0], model->axis[1][1], model->axis[1][2], 0.0f}};
    const Lanes m2 = {{model->axis[2][0], model->axis[2][1], model->axis[2][2], 0.0f}};
    const Lanes m3 = {{model->origin[0], model->origin[1], model->origin[2], one}};

    NglLightSet* light_set = nglCreateLightSet();
    alignas(16) const float position[4] = {model->origin[0], model->origin[1], model->origin[2], model->origin[0]};
    if (model->lighting_state == 0) {
        model->lighting_state = 1;
        int32_t index;
        void* grid = R_GetCellLighting(position, 0, &index);
        // Not in a cell: try a little higher, then higher still.
        const uint32_t raises[2] = {0x0083f0d8, 0x0083ef04};
        for (int i = 0; !grid && i < 2; ++i) {
            const Lanes raise = {{0.0f, 0.0f, title_global<float>(raises[i]), 0.0f}};
            alignas(16) const Lanes raised = add(lanes(position), raise);
            grid = R_GetCellLighting(raised.v, 0, &index);
        }
        if (!grid) model->lighting_state = 0;
        if (grid && index >= 0) {
            title_function<void XB_REGS1 XB_STDCALL(int32_t, void*, const float*, RStaticModel*)>(0x00242e90)(
                index, grid, position, model);
            model->lighting_state = 2;
        } else {
            // No light grid there: full red ambient.
            model->lighting.ambient[0] = one;
            model->lighting.ambient[1] = 0.0f;
            model->lighting.ambient[2] = 0.0f;
        }
    } else if (model->lighting_state == 2) {
        R_ApplyCachedLighting(&model->lighting);
    }

    const uint32_t* scale_params = nullptr;
    if (model->scale != title_global<float>(0x0083eb20)) {
        // A function-static instance parameter block {2, ..., scale x4 at +0x10}.
        uint32_t* block = &title_global<uint32_t>(0x0098dbe0);
        if (!(guards & 2)) {
            guards |= 2;
            block[0] = 2;
        }
        float* scale = reinterpret_cast<float*>(block + 4);
        scale[0] = model->scale;
        scale[1] = model->scale;
        scale[2] = model->scale;
        scale[3] = model->scale;
        scale_params = block;
    }

    // The instance parameters: a 64-bit slot mask, then a word per slot (0x00902904
    // slots), from the frame allocator; the light set goes into slot 0x00902914.
    uintptr_t& next = title_global<uintptr_t>(0x009025a8);
    uint32_t* params = reinterpret_cast<uint32_t*>((next + 7) & ~uintptr_t{7});
    params[0] = 0;
    params[1] = 0;
    next = reinterpret_cast<uintptr_t>(params + 2 + title_global<uint32_t>(0x00902904));
    const uint32_t slot = title_global<uint32_t>(0x00902914);
    params[0] |= 1u << (slot & 31);
    params[1] |= 0;
    params[2 + slot] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(light_set));

    for (uint32_t i = 0;; ++i) {
        const XModelParts* p = first_lod(xmodel)->parts;
        if (static_cast<int32_t>(i) >= (p ? static_cast<int32_t>(p->bone_count) : 0)) return;
        p = first_lod(xmodel)->parts;
        uint8_t* mesh = p->meshes[i < p->mesh_count ? i : 0];
        if (!mesh) continue;
        // The bone's matrix times the model's (rows; the bone's w column unused).
        const float* bone = bones + i * 16;
        alignas(16) Lanes matrix[4];
        for (int r = 0; r < 3; ++r) {
            const float* b = bone + r * 4;
            matrix[r] = add(add(mul(m0, b[0]), mul(m1, b[1])), mul(m2, b[2]));
        }
        const float* b = bone + 12;
        matrix[3] = add(add(add(m3, mul(m0, b[0])), mul(m1, b[1])), mul(m2, b[2]));
        // The instance block is handed over by the address of the pointer to it.
        const uint32_t params_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
        nglListAddMesh(mesh, matrix[0].v, scale_params, &params_word);
    }
}

namespace {

// The world's cells (0x0095f6b8, or +0x100 of the client's world record): count at +0x18,
// 0x50-byte cells at +0x1c; an index past the end reads cell 0.
uint8_t* world_cell(const uint8_t* world, uint32_t index) {
    const uint32_t count = *reinterpret_cast<const uint32_t*>(world + 0x18);
    uint8_t* cells = *reinterpret_cast<uint8_t* const*>(world + 0x1c);
    return cells + (index < count ? index : 0) * 0x50;
}

// A cell's model links (+0x34), from the pool at 0x0095f6d4.
struct RCellModelLink {
    uint8_t* model;
    RCellModelLink* next;
};

// A BSP node of the cell tree: -1 at +0 with a negative +2 is a split, else +2 is the cell.
struct RCellNode {
    int16_t kind;
    int16_t cell;
    const float* plane;  // normal, dist (16-byte aligned)
    RCellNode* children[2];
};

}  // namespace

int32_t XB_REGS2 CM_CellForPoint(uint32_t, const float* point) {
    const RCellNode* node = *reinterpret_cast<RCellNode* const*>(title_global<uint8_t*>(0x0095f6b8) + 0xc);
    if (node->kind == -1) {
        while (node->cell < 0) {
            const Lanes products = mul(lanes(point), lanes(node->plane));
            platform_keep(products);
            const float d = products.v[0] + products.v[1] + products.v[2] - node->plane[3];
            node = node->children[d > 0.0f ? 0 : 1];
            if (node->kind != -1) break;
        }
    }
    return node->cell;
}

int32_t XB_CDECL R_CellForViewOrigin() {
    const float* view = &title_global<float>(0x0095e240);
    alignas(16) const float point[4] = {view[0], view[1], view[2], view[0]};
    if (!title_global<uint32_t>(0x0095e608))
        title_function<void XB_CDECL(int32_t, const char*, ...)>(title_global<uint32_t>(0x0095e634))(
            1, reinterpret_cast<const char*>(0x0083c4dc));
    return CM_CellForPoint(0, point);
}

void XB_CDECL R_SetDefaultLighting() {
    // Function statics: a direction (0, a, a) and a colour, built on first use.
    uint32_t& guards = title_global<uint32_t>(0x0098bc80);
    float* direction = &title_global<float>(0x0098bc70);
    float* colour = &title_global<float>(0x0098bc60);
    if (!(guards & 1)) {
        guards |= 1;
        const float a = title_global<float>(0x0083ef70);
        direction[0] = 0.0f;
        direction[1] = a;
        direction[2] = a;
        direction[3] = 0.0f;
    }
    if (!(guards & 2)) {
        guards |= 2;
        colour[0] = title_global<float>(0x0083ef64);
        colour[1] = title_global<float>(0x0083ef68);
        colour[2] = title_global<float>(0x0083ef6c);
        colour[3] = 0.0f;
    }
    alignas(16) const Lanes light = mul(lanes(colour), title_global<float>(0x0083ef60));
    nglAddDirectionalLight(0xffffffff, direction, light.v);
    const Lanes ambient = mul(lanes(colour), title_global<float>(0x008db118));
    platform_keep(ambient);
    nglSetAmbientColor(ambient.v[0], ambient.v[1], ambient.v[2]);
}

void* XB_REGS3 R_GetCellLighting(const float* position, uint32_t, int32_t* cell_out) {
    int32_t local;
    int32_t* cell = cell_out ? cell_out : &local;
    const Lanes up = {{0.0f, 0.0f, title_global<float>(0x0083eb94), 0.0f}};
    alignas(16) Lanes point = add(lanes(position), up);
    *cell = CM_CellForPoint(0, point.v);
    if (*cell == -1) {
        // Not in a cell: try again a little higher.
        const Lanes higher = {{0.0f, 0.0f, title_global<float>(0x0083ec30), 0.0f}};
        point = add(point, higher);
        *cell = CM_CellForPoint(0, point.v);
    }
    if (*cell == -1) return nullptr;
    const uint8_t* bsp = title_global<uint8_t*>(0x0095f640);
    const uint8_t* level = *reinterpret_cast<uint8_t* const*>(bsp + 4 + *reinterpret_cast<const int32_t*>(bsp + 0x1cc) * 4);
    const uint32_t count = *reinterpret_cast<const uint32_t*>(level + 0x1c);
    const uint8_t* const* entries = *reinterpret_cast<uint8_t* const* const*>(level + 0x20);
    const uint8_t* entry = entries[static_cast<uint32_t>(*cell) < count ? *cell : 0];
    const uint8_t* info = *reinterpret_cast<uint8_t* const*>(*reinterpret_cast<uint8_t* const*>(entry + 0x2c) + 0x24);
    const int32_t grid_index = *reinterpret_cast<const int32_t*>(info + 0x28);
    if (grid_index == -1) return nullptr;
    void* grid = reinterpret_cast<void* const*>(title_global<uint8_t*>(0x0095f6a0) + 4)[grid_index];
    if (!grid) nglSetAmbientColor(0.0f, 0.0f, 1.0f);  // no grid loaded: blue ambient
    return grid;
}

void XB_CDECL R_UnlinkEntityFromCells(const void* owner) {
    const uint8_t* world = title_global<uint8_t*>(0x0095f6b8);
    if (!world) return;
    auto free_link = title_function<void XB_THISCALL(void*, void*)>(0x00044910);
    for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(world + 0x18); ++i) {
        RCellModelLink*& head = *reinterpret_cast<RCellModelLink**>(world_cell(world, i) + 0x34);
        RCellModelLink* prev = nullptr;
        for (RCellModelLink* link = head; link;) {
            // The pool is handed every link: null for the ones kept.
            RCellModelLink* freed = nullptr;
            if (*reinterpret_cast<void* const*>(link->model + 0xe0) == owner) {
                freed = link;
                (prev ? prev->next : head) = link->next;
            } else {
                prev = link;
            }
            RCellModelLink* next = link->next;
            free_link(title_global<void*>(0x0095f6d4), freed);
            link = next;
        }
        world = title_global<uint8_t*>(0x0095f6b8);
    }
}

PLATFORM_ASM_ENTRY(R_LinkEntityToCell_entry,
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(R_LinkEntityToCell) "addl $12, %esp\n"
                   "ret")

void XB_CDECL R_LinkEntityToCell(const uint8_t* owner, uint32_t cell, uint8_t* model) {
    uint8_t* c = world_cell(*reinterpret_cast<uint8_t* const*>(owner + 0x100), cell);
    RCellModelLink*& head = *reinterpret_cast<RCellModelLink**>(c + 0x34);
    if (head && head->model == model) return;  // only the first link is checked
    auto* link = static_cast<RCellModelLink*>(
        title_function<void* XB_THISCALL(void*, uint32_t, uint32_t)>(0x00044850)(title_global<void*>(0x0095f6d4), 8, 0));
    link->model = model;
    link->next = head;
    head = link;
}

void XB_CDECL R_LinkEntityToCells_r(const uint8_t* owner, const uint8_t* node, uint8_t* model, const float* mins,
                                    const float* maxs) {
    const RCellNode* n = reinterpret_cast<const RCellNode*>(node);
    while (n->cell == -2) {
        const float* plane = n->plane;
        const Lanes zero = lanes(&title_global<float>(0x0082e680));
        // The box corner furthest along the normal (CMPLEPS selects: NaN lanes take mins).
        Lanes far_corner;
        Lanes near_corner;
        for (int i = 0; i < 4; ++i) {
            far_corner.v[i] = zero.v[i] <= plane[i] ? maxs[i] : mins[i];
            near_corner.v[i] = plane[i] <= zero.v[i] ? maxs[i] : mins[i];
        }
        const Lanes fp = mul(far_corner, lanes(plane));
        const Lanes np = mul(near_corner, lanes(plane));
        platform_keep(fp);
        platform_keep(np);
        const float far_dist = fp.v[0] + fp.v[1] + fp.v[2];
        const float near_dist = np.v[0] + np.v[1] + np.v[2];
        uint32_t sides = 0;
        if (far_dist >= plane[3]) sides = 1;
        if (plane[3] > near_dist) sides |= 2;
        if (sides == 3) {
            R_LinkEntityToCells_r(owner, reinterpret_cast<const uint8_t*>(n->children[0]), model, mins, maxs);
            n = n->children[1];
        } else {
            // Side 0 (only with NaNs) reads the plane pointer as a node, as the title does.
            n = reinterpret_cast<const RCellNode* const*>(&n->plane)[sides];
        }
    }
    if (n->cell >= 0) R_LinkEntityToCell(owner, static_cast<uint32_t>(n->cell), model);
}

void XB_REGS1 XB_CDECL R_AddCellModels(const uint8_t* cell) {
    for (const RCellModelLink* link = *reinterpret_cast<RCellModelLink* const*>(cell + 0x34); link;
         link = link->next) {
        float& stamp = *reinterpret_cast<float*>(link->model + 0xe4);
        const float frame = static_cast<float>(title_global<int32_t>(0x0095e39c));
        if (stamp != frame) {
            stamp = frame;
            R_AddStaticModel(reinterpret_cast<RStaticModel*>(link->model));
            ++title_global<int32_t>(0x0095c190);
        }
    }
}

PLATFORM_ASM_ENTRY(R_AddCellSurfaces_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(R_AddCellSurfaces) "addl $12, %esp\n"
                   "ret")

void XB_CDECL R_AddCellSurfaces(uint8_t* cell, const uint8_t* planes, int32_t count) {
    int32_t& stamp = *reinterpret_cast<int32_t*>(cell + 0x30);
    if (stamp == title_global<int32_t>(0x0095e39c)) return;
    stamp = title_global<int32_t>(0x0095e39c);
    // The world matrix: three scaled rows and a fixed fourth.
    const float scale = title_global<float>(0x0082e140);
    alignas(16) float matrix[16];
    const uint32_t rows[3] = {0x0082e160, 0x0082e170, 0x0082e180};
    for (int r = 0; r < 3; ++r) {
        const Lanes row = mul(lanes(&title_global<float>(rows[r])), scale);
        std::memcpy(matrix + r * 4, row.v, 16);
    }
    std::memcpy(matrix + 12, &title_global<uint32_t>(0x0082e290), 16);
    const uint32_t& mesh_count = *reinterpret_cast<const uint32_t*>(cell + 0x40);
    for (uint32_t i = 0; i < mesh_count; ++i) {
        uint8_t* const* meshes = *reinterpret_cast<uint8_t* const* const*>(cell + 0x44);
        nglListAddMesh(meshes[i < mesh_count ? i : 0], matrix, nullptr, nullptr);
    }
    if (*reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0095e36c) + 0x20))
        R_AddCellEntities(cell, planes, count);
}

namespace {

void* z_malloc_debug(int32_t size, uint32_t file, uint32_t text, int32_t line) {
    return title_function<void* XB_CDECL(int32_t, const char*, const char*, int32_t)>(0x00042c20)(
        size, reinterpret_cast<const char*>(file), reinterpret_cast<const char*>(text), line);
}

// Q3's Q_rsqrt: one Newton step from the 0x5f3759df guess.
float q_rsqrt(float x) {
    int32_t i;
    std::memcpy(&i, &x, 4);
    const float y = from_bits(static_cast<uint32_t>(0x5f3759df - (i >> 1)));
    return (title_global<float>(0x0083eaf4) - y * y * (x * title_global<float>(0x0083eb24))) * y;
}

// The byte offsets of a box corner's coordinates along a plane normal: +0xc (the
// maxs) where the component's bits are a positive integer, i.e. above +0.
uint8_t corner_offset(float v, uint8_t base) {
    int32_t i;
    std::memcpy(&i, &v, 4);
    return static_cast<uint8_t>((i > 0 ? 0xc : 0) + base);
}

void cross(float* out, const float* a, const float* b) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void set_plane(RPlane* plane, float dist) {
    plane->dist = dist - title_global<float>(0x0083eb88);  // 0.005
    plane->corner[0] = corner_offset(plane->normal[0], 0);
    plane->corner[1] = corner_offset(plane->normal[1], 4);
    plane->corner[2] = corner_offset(plane->normal[2], 8);
}

}  // namespace

// MSVC unrolled the loops four times (with a one-at-a-time loop for the rest) and
// summed in a different order in some of the copies: `unrolled_first(i, n)` is true for
// the first point of each unrolled group, `unrolled(i, n)` for every point in a group.
static bool unrolled(int32_t i, int32_t n) { return n >= 4 && i < (((n - 4) >> 2) + 1) * 4; }
static bool unrolled_first(int32_t i, int32_t n) { return unrolled(i, n) && (i & 3) == 0; }

RPlane* XB_REGS1 XB_CDECL R_ClipPortalWinding(int32_t n, int32_t* count, const float* points) {
    const float* view = &title_global<float>(0x0095e240);
    const float threshold = cvar_value(title_global<const Cvar*>(0x0095e6e0));
    const float zero = title_global<float>(0x0083eafc);
    const float one = title_global<float>(0x0083eb20);
    RPlane* planes;
    float corners[4][3];
    if (threshold > zero) {
        planes = static_cast<RPlane*>(z_malloc_debug((n + 4) * 20, 0x0083c4f8, 0x0083c504, 0x17f));
        // The winding's bounds in the projected view (rows at 0x0095e2d8, w row +0x30).
        const float* m = &title_global<float>(0x0095e2d8);
        float a_min = one;
        float a_max = title_global<float>(0x0083eb34);  // -100
        float b_min = one;
        float b_max = a_max;
        for (int32_t i = 0; i < n; ++i) {
            const float* p = points + i * 4;
            const bool yzx = !unrolled(i, n) || unrolled_first(i, n);
            auto dot = [p, yzx](const float* r) {
                return yzx ? p[1] * r[1] + p[2] * r[2] + p[0] * r[0] + r[3] : p[0] * r[0] + p[2] * r[2] + p[1] * r[1] + r[3];
            };
            const float w = dot(m + 12);
            if (w > zero) continue;
            const float inv = one / w;
            const float a = dot(m) * inv;
            const float b = dot(m + 4) * inv;
            if (a_min > a) a_min = a;
            if (a > a_max) a_max = a;
            if (b_min > b) b_min = b;
            if (b > b_max) b_max = b;
        }
        // Its corners back in the world (inverse at 0x0095e318).
        const float* q = &title_global<float>(0x0095e318);
        for (int i = 0; i < 4; ++i) {
            const float s = i >= 2 ? a_max : a_min;
            const float t = i == 1 || i == 2 ? b_min : b_max;
            const float x = q[4] * t + q[0] * s + q[12];
            const float y = q[5] * t + q[1] * s + q[13];
            const float z = q[6] * t + q[2] * s + q[14];
            const float inv = one / (q[7] * t + q[3] * s + q[15]);
            corners[i][0] = x * inv;
            corners[i][1] = y * inv;
            corners[i][2] = z * inv;
        }
    } else {
        planes = static_cast<RPlane*>(z_malloc_debug(n * 20, 0x0083534c, 0x0083c504, 0x1b4));
    }

    // The winding's edges seen from the view origin.
    float d[33][3];
    for (int32_t i = 0; i < n; ++i) {
        d[i][0] = points[i * 4] - view[0];
        d[i][1] = points[i * 4 + 1] - view[1];
        d[i][2] = points[i * 4 + 2] - view[2];
    }
    std::memcpy(d[n], d[0], 12);
    float normals[32][3];
    for (int32_t i = 0; i < n; ++i) {
        float* nm = normals[i];
        cross(nm, d[i + 1], d[i]);
        const float len2 = unrolled_first(i, n) ? nm[2] * nm[2] + nm[1] * nm[1] + nm[0] * nm[0]
                                                : nm[0] * nm[0] + nm[2] * nm[2] + nm[1] * nm[1];
        const float r = q_rsqrt(len2);
        nm[0] *= r;
        nm[1] *= r;
        nm[2] *= r;
    }

    int32_t out = 0;
    if (threshold > zero) {
        // The projected bounds' edges, unless one is within the threshold of an edge plane.
        float e[5][3];
        for (int i = 0; i < 4; ++i) {
            e[i][0] = corners[i][0] - view[0];
            e[i][1] = corners[i][1] - view[1];
            e[i][2] = corners[i][2] - view[2];
        }
        std::memcpy(e[4], e[0], 12);
        for (int j = 0; j < 4; ++j) {
            // Written in place: a rejected plane stays in the next slot.
            RPlane* plane = &planes[out];
            float* pn = plane->normal;
            cross(pn, e[j + 1], e[j]);
            const float r = q_rsqrt(pn[1] * pn[1] + pn[0] * pn[0] + pn[2] * pn[2]);
            pn[0] = r * pn[0];
            pn[1] = r * pn[1];
            pn[2] = r * pn[2];
            bool rejected = false;
            for (int32_t s = 0; s < n; ++s) {
                const float* nm = normals[s];
                if (nm[0] * pn[0] + nm[2] * pn[2] + nm[1] * pn[1] > threshold) {
                    rejected = true;
                    break;
                }
            }
            if (rejected) continue;
            set_plane(plane, view[2] * pn[2] + view[1] * pn[1] + view[0] * pn[0]);
            ++out;
        }
    }
    for (int32_t s = 0; s < n; ++s) {
        const float* nm = normals[s];
        const bool middle = unrolled(s, n) && !unrolled_first(s, n);
        const float len2 = unrolled_first(s, n) ? nm[2] * nm[2] + nm[0] * nm[0] + nm[1] * nm[1]
                                                : nm[2] * nm[2] + nm[1] * nm[1] + nm[0] * nm[0];
        if (len2 == 0.0f) continue;
        RPlane* plane = &planes[out];
        std::memcpy(plane->normal, nm, 12);
        set_plane(plane, middle ? nm[2] * view[2] + nm[1] * view[1] + nm[0] * view[0]
                                : nm[2] * view[2] + nm[0] * view[0] + nm[1] * view[1]);
        ++out;
    }
    *count = out;
    return planes;
}

RPlane* XB_REGS3 R_ClipFrustumToPortal(int32_t* count, const uint8_t* portal, const RPlane* planes,
                                       const float* clip_plane, int32_t plane_count) {
    alignas(16) float buffers[2][128];
    auto other = [&buffers](const float* points) { return points == buffers[0] ? buffers[1] : buffers[0]; };
    *count = *reinterpret_cast<const int32_t*>(portal + 0x1c);
    float* points = R_ClipPolygonToPlane(clip_plane, *reinterpret_cast<float* const*>(portal + 0x18), count, buffers[0]);
    if (!*count) return nullptr;
    const float* view = &title_global<float>(0x0095e240);
    const float* normal = reinterpret_cast<const float*>(portal);
    const float d = normal[3] - (normal[2] * view[2] + normal[1] * view[1] + normal[0] * view[0]);
    if (title_global<float>(0x0083eb20) > d) {
        // The view is (nearly) in the portal: keep the frustum as it is.
        *count = plane_count;
        auto* copy = static_cast<RPlane*>(z_malloc_debug(plane_count * 20, 0x0083534c, 0x0083c504, 0x32e));
        std::memcpy(copy, planes, static_cast<size_t>(plane_count) * 20);
        return copy;
    }
    if (const float* near_plane = title_global<const float*>(0x0095e2c8)) {
        points = R_ClipPolygonToPlane(near_plane, points, count, other(points));
        if (!*count) return nullptr;
    }
    for (int32_t i = 0; i < plane_count; ++i) {
        points = R_ClipPolygonToPlane(planes[i].normal, points, count, other(points));
        if (!*count) return nullptr;
    }
    return R_ClipPortalWinding(*count, count, points);
}

void XB_CDECL R_VisitCell_r(uint8_t* cell, const uint8_t* from_portal, const uint8_t* planes, int32_t count,
                            uint32_t pass) {
    R_AddCellSurfaces(cell, planes, count);
    R_AddCellModels(cell);
    uint8_t* portal = *reinterpret_cast<uint8_t* const*>(cell + 0x20);
    const float* view = &title_global<float>(0x0095e240);
    for (int32_t n = *reinterpret_cast<const int32_t*>(cell + 0x24); n; --n, portal += 0x24) {
        int32_t& visiting = *reinterpret_cast<int32_t*>(portal + 0x20);
        if (visiting) continue;
        const float* plane = reinterpret_cast<const float*>(portal);
        const float d = plane[2] * view[2] + plane[1] * view[1] + plane[0] * view[0];
        if (d > plane[3]) continue;  // facing away
        if (R_CullPortal(portal, reinterpret_cast<const float*>(planes), count)) continue;
        int32_t clipped_count;
        RPlane* clipped = R_ClipFrustumToPortal(&clipped_count, portal, reinterpret_cast<const RPlane*>(planes),
                                                reinterpret_cast<const float*>(from_portal), count);
        if (!clipped_count) continue;
        visiting = 1;
        R_VisitCell_r(*reinterpret_cast<uint8_t* const*>(portal + 0x14), portal, reinterpret_cast<const uint8_t*>(clipped),
                      clipped_count, pass);
        title_function<void XB_CDECL(void*)>(0x000428d0)(clipped);  // Z_Free
        visiting = 0;
    }
}

}  // namespace game
