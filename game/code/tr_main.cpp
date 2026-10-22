#include "tr_main.h"

#include <cstring>

#include "dobj.h"
#include "lanes.h"
#include "ngl.h"
#include "qcommon.h"
#include "shaders.h"
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

void XB_CDECL RE_StretchPicRotate(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                                  float angle, void* shader) {
    if (!title_global<NglScene*>(0x0090259c)) return;
    float sine, cosine;
    FastSinCos(&sine, 0, &cosine, angle * title_global<float>(0x0083ed00) * title_global<float>(0x0083ecfc));
    const float half = title_global<float>(0x0083eb24);
    const float half_w = w * half;
    const float half_h = h * half;
    const float cx = half_w + x;
    const float cy = half_h + y;
    NglQuad quad;
    nglInitQuad(&quad);
    nglSetQuadUV(&quad, s0, t0, s1, t1);
    nglSetQuadColor(&quad, title_global<uint32_t>(0x0095e6d0));
    // The corners rotated about the centre, grouped as the original adds them.
    const float a = 0.0f - half_h * sine;
    const float b = half_w * cosine;
    const float c = half_w * sine;
    const float d = half_h * cosine;
    quad.verts[0].x = (cx - b) - a;
    quad.verts[0].y = (cy - c) - d;
    quad.verts[1].x = (b + cx) - a;
    quad.verts[1].y = (c + cy) - d;
    quad.verts[2].x = (cx - b) + a;
    quad.verts[2].y = (cy - c) + d;
    quad.verts[3].x = (a + b) + cx;
    quad.verts[3].y = (d + c) + cy;
    quad.texture = static_cast<NglTexture*>(shader);
    nglListAddQuad(&quad);
}

void XB_CDECL RE_AddPolyToScene(void* shader, int32_t vertex_count, const uint8_t* verts) {
    if (vertex_count != 4) return;
    // The PCUV material from the frame arena (16-aligned, 0x28 bytes).
    uint32_t& arena = title_global<uint32_t>(0x009025a8);
    const uint32_t material_address = (arena + 0xf) & ~0xfu;
    arena = material_address + 0x28;
    uint8_t* material = material_address
                            ? ShaderPCUV_InitMaterial(reinterpret_cast<uint8_t*>(uintptr_t{material_address}),
                                                      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shader)), 3, 2)
                            : nullptr;
    nglMeshBuilder_Begin(0x40000, 1, 0, nullptr);
    using Alloc = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t);
    uint32_t* owner = reinterpret_cast<uint32_t*>(uintptr_t{title_function<Alloc>(title_global<uint32_t>(0x00902928))(8, 4, 0)});
    if (owner) owner[0] = 0x0083be78;  // vtable
    nglMeshBuilder_AddSection(owner, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(material)), 4, 1, 0, nullptr,
                              0x18);
    const auto section = [owner] { return reinterpret_cast<uint8_t*>(uintptr_t{owner[1]}); };
    const auto gpu_frame = [&] { return *reinterpret_cast<const uint32_t*>(section() + 0x24) & 0x40000; };
    if (!gpu_frame()) {
        uint8_t* buffer = section() + 0x44;
        *reinterpret_cast<uint32_t*>(buffer) =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(nglLockVertexBuffer(buffer, 0, 0)));
    }
    uint32_t builder[3] = {0x0083be3c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)), 0};
    nglMeshBuilder_AddStrip(4, 0x18);
    // polyVert_t (0x20 bytes): xyz, st, then the modulate bytes r g b a as ARGB. The
    // corners go in strip order 0, 1, 3, 2.
    const auto word = [verts](int vertex, uint32_t offset) {
        return *reinterpret_cast<const uint32_t*>(verts + vertex * 0x20 + offset);
    };
    const auto color = [verts](int vertex) {
        const uint8_t* rgba = verts + vertex * 0x20 + 0x1c;
        return (uint32_t{rgba[3]} << 24) | (uint32_t{rgba[0]} << 16) | (uint32_t{rgba[1]} << 8) | rgba[2];
    };
    static const int kVertex[4] = {0, 1, 3, 2};
    for (uint32_t i = 0; i < 4; ++i) {
        const int v = kVertex[i];
        builder[2] = i;
        nglPolyBuilder_SetVertex(builder, word(v, 0), word(v, 4), word(v, 8), color(v), word(v, 0xc), word(v, 0x10));
    }
    if (!gpu_frame()) nglVertexBuffer_Unlock(reinterpret_cast<uint32_t*>(section() + 0x44));
    // The identity rows (0x0082e160/170/180, each times 1.0f) and the origin row 0x0082e290.
    const float one = title_global<float>(0x0082e140);
    alignas(16) float matrix[16];
    for (int row = 0; row < 3; ++row)
        for (int lane = 0; lane < 4; ++lane) matrix[row * 4 + lane] = title_global<float>(0x0082e160 + row * 0x10 + lane * 4) * one;
    std::memcpy(matrix + 12, &title_global<float>(0x0082e290), 4 * sizeof(float));
    nglListAddMesh(reinterpret_cast<uint8_t*>(nglMeshBuilder_End()), matrix, nullptr, nullptr);
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
    R_AddFogPass();
}

void XB_CDECL RE_Shutdown(int32_t destroy_window) {
    title_function<void XB_CDECL(int32_t, const char*, ...)>(title_global<uint32_t>(0x0095e630))(
        0, "RE_Shutdown( %i )\n", destroy_window);
    static const char* const kCommands[] = {
        "modellist", "screenshotHigh", "screenshot", "imagelist",   "shaderlist",  "skinlist",  "gfxinfo",
        "modelist",  "shaderstate",    "taginfo",    "cropimages",  "r_meminfo",   "r_vc_stats", "r_smc_stats",
        "r_smc_flush", "r_loadsun",    "r_savesun",  "r_sunhelp",   "r_vbo_refresh",
    };
    const auto remove_command = title_function<void XB_CDECL(const char*)>(title_global<uint32_t>(0x0095e660));
    for (const char* name : kCommands) remove_command(name);
    NglTexture*& texture = title_global<NglTexture*>(0x0095f6c8);
    if (texture) {
        nglFreeTexture(texture);
        texture = nullptr;
    }
    title_global<uint32_t>(0x0095e390) = 0;
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

PLATFORM_ASM_ENTRY(R_ParseTextIcon_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl %edx\n"
                   "pushl %ebx\n"
                   "pushl 0x1c(%esp)\n" PLATFORM_ASM_CALL(R_ParseTextIcon) "addl $36, %esp\n"
                   "ret")

void XB_CDECL R_ParseTextIcon(float scale, const uint16_t* text, int32_t* consumed, int32_t* count, float* width,
                              float* height, uint32_t* shader, float* colour) {
    const float size = scale * title_global<float>(0x0083ecd8);
    *width = size;
    *height = size;
    *shader = 0;
    char name[0x200];
    std::memset(name, 0, 0x80);
    for (int32_t i = 0; i < *count; ++i) {
        const uint32_t kind = text[i] >> 8;
        const uint32_t value = text[i] & 0xff;
        switch (kind) {
        case 0x0d:
        case 0x0e:
        case 0x0f:
            if (colour) colour[kind - 0x0d] = static_cast<float>(static_cast<int32_t>(value)) * title_global<float>(0x0083eb30);
            break;
        case 0x10:
        case 0x11: {
            float* extent = kind == 0x10 ? width : height;
            *extent = static_cast<float>(static_cast<int32_t>(value)) * title_global<float>(0x0083f004) * *extent;
            break;
        }
        case 0x12: {
            // The shader name: characters of kind 0x12 up to a zero or the last one.
            char* out = name;
            uint32_t c = value;
            while (c && i < *count - 1) {
                *out++ = static_cast<char>(c);
                *consumed += 2;
                const uint16_t next = text[i + 1];
                ++i;
                c = next & 0xff;
                if ((next & 0xff00) != 0x1200) break;
            }
            *shader = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(RE_RegisterShader(0, name)));
            *count -= i;
            return;
        }
        default:
            continue;
        }
        *consumed += 2;
    }
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
            R_SampleLightGrid(index, static_cast<const RLightGrid*>(grid), position, &model->lighting);
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

void XB_REGS1 XB_STDCALL R_SampleLightGrid(int32_t cell, const RLightGrid* grid, const float* point,
                                           RCachedLighting* out) {
    const RLightGridArea* area = nullptr;
    for (int32_t i = 0; i < grid->area_count; ++i) {
        if (grid->areas[i].cell == cell) {
            area = &grid->areas[i];
            break;
        }
    }
    if (!area) {
        nglSetAmbientColor(0.0f, 0.0f, 1.0f);
        if (out) {
            out->ambient[0] = 0.0f;
            out->ambient[1] = 0.0f;
            out->count = 0;
            out->ambient[2] = title_global<float>(0x0083eb20);
        }
        return;
    }

    // The sample cell and the point's place in it.
    const Lanes offset = sub(lanes(point), lanes(area->origin));
    platform_keep(offset);
    float fx = offset.v[0] / area->cell_size[0];
    float fy = offset.v[1] / area->cell_size[1];
    int32_t x = static_cast<int32_t>(fx);
    int32_t y = static_cast<int32_t>(fy);
    if (x < 0) {
        fx = 0.0f;
        x = 0;
    }
    if (x >= area->width - 1) {
        x = area->width - 2;
        fx = static_cast<float>(x);
    }
    if (y < 0) {
        fy = 0.0f;
        y = 0;
    }
    if (y >= area->height - 1) {
        y = area->height - 2;
        fy = static_cast<float>(y);
    }
    const float one = title_global<float>(0x0083eb20);
    fx = fx - static_cast<float>(x);
    fy = fy - static_cast<float>(y);
    const int32_t index = area->width * y + area->first + x;
    const uint32_t* corners[4] = {grid->samples + index, grid->samples + index + 1,
                                  grid->samples + area->width + index, grid->samples + area->width + index + 1};
    const float gx = one - fx;
    const float weights[4] = {gx * (one - fy), (one - fy) * fx, gx * fy, fy * fx};

    // The ambient: the corners' 5-bit colours (r, g, b, r) / 31, blended.
    const float to_unit = title_global<float>(0x0083f040);
    Lanes colors[4];
    for (int i = 0; i < 4; ++i) {
        const uint32_t sample = *corners[i];
        const float r = platform_x87_int_mul(static_cast<int32_t>(sample & 0x1f), to_unit);
        const float b = platform_x87_int_mul(static_cast<int32_t>((sample >> 10) & 0x1f), to_unit);
        const float g = platform_x87_int_mul(static_cast<int32_t>((sample >> 5) & 0x1f), to_unit);
        colors[i] = {{r, g, b, r}};
    }
    RCachedLighting local;
    if (!out) out = &local;
    const Lanes ambient = add(add(add(mul(colors[0], weights[0]), mul(colors[1], weights[1])), mul(colors[2], weights[2])),
                              mul(colors[3], weights[3]));
    platform_keep(ambient);
    out->ambient[0] = ambient.v[0];
    out->ambient[1] = ambient.v[1];
    out->count = 0;
    out->ambient[2] = ambient.v[2];

    // The corners' light references, merged: {light, weight} with blended weights.
    struct Reference {
        uint16_t light;
        uint16_t weight;
    } merged[12];
    int32_t merged_count = 0;
    for (int c = 0; c < 4; ++c) {
        const uint32_t sample = *corners[c];
        const uint32_t count = (sample >> 15) & 3;
        if (count == 0) continue;
        const uint16_t* reference = grid->references + (sample >> 17) * 2;
        for (uint32_t r = 0; r < count; ++r, reference += 2) {
            int32_t i = 0;
            for (; i < merged_count; ++i) {
                if (merged[i].light == reference[0]) {
                    merged[i].weight = static_cast<uint16_t>(
                        platform_x87_mul_add_ftol(reference[1], weights[c], merged[i].weight, true));
                    break;
                }
            }
            if (i != merged_count) continue;
            merged[merged_count].light = reference[0];
            merged[merged_count].weight =
                static_cast<uint16_t>(platform_x87_mul_add_ftol(reference[1], weights[c], 0, false));
            ++merged_count;
        }
    }

    // The three strongest as directional lights.
    for (int32_t n = 0; n < (merged_count < 3 ? merged_count : 3); ++n) {
        int32_t best = 0;
        int32_t strongest = 0;
        for (int32_t i = 0; i < merged_count; ++i) {
            if (merged[i].weight > strongest) {
                strongest = merged[i].weight;
                best = i;
            }
        }
        merged[best].weight = 0;
        const RGridLight& light = grid->lights[merged[best].light];
        float* color = out->colors[out->count];
        float* direction = out->directions[out->count];
        const float weight = static_cast<float>(strongest) * title_global<float>(0x0083f03c);  // / 65535
        color[0] = light.color[0] * weight;
        color[1] = light.color[1] * weight;
        color[2] = light.color[2] * weight;
        color[3] = weight;
        alignas(16) float light_color[4];
        std::memcpy(light_color, color, sizeof(light_color));
        alignas(16) float to_light[4];
        if (light.point != 0.0f) {
            // From the light to the point, normalized (RSQRTSS + one Newton step).
            const Lanes d = sub(lanes(point), Lanes{{light.origin[0], light.origin[1], light.origin[2], light.origin[0]}});
            const Lanes sq = mul(d, d);
            platform_keep(sq);
            const float length_sq = (sq.v[0] + sq.v[1]) + sq.v[2];
            float scale;
            if (length_sq == 0.0f) {
                scale = title_global<float>(0x0082e6a0);
            } else {
                const float a = abs_bits(length_sq);
                const float r = platform_rsqrt_estimate(a);
                scale = ((title_global<float>(0x0082e660) - r * r * a) * r) * title_global<float>(0x0082e670);
            }
            const Lanes scaled = mul(d, scale);
            platform_keep(scaled);
            std::memcpy(to_light, scaled.v, sizeof(to_light));
        } else {
            const float origin[4] = {light.origin[0], light.origin[1], light.origin[2], light.origin[0]};
            std::memcpy(to_light, origin, sizeof(to_light));
        }
        nglAddDirectionalLight(0xffffffff, to_light, light_color);
        direction[0] = to_light[0];
        direction[1] = to_light[1];
        direction[2] = to_light[2];
        const float ambient_scale = title_global<float>(0x008db118);
        const Lanes add_ambient = {{color[0] * ambient_scale, ambient_scale * color[1], color[2] * ambient_scale,
                                    color[0] * ambient_scale}};
        // The fourth lane is the first colour's red (the word after the ambient).
        const Lanes sum = add(Lanes{{out->ambient[0], out->ambient[1], out->ambient[2], out->colors[0][0]}}, add_ambient);
        platform_keep(sum);
        out->ambient[0] = sum.v[0];
        out->ambient[1] = sum.v[1];
        out->ambient[2] = sum.v[2];
        ++out->count;
    }
    nglSetAmbientColor(out->ambient[0], out->ambient[1], out->ambient[2]);
}

void XB_CDECL RE_BeginFrame() {
    if (!title_global<uint32_t>(0x0095e390)) return;
    alignas(16) const float color[4] = {title_global<float>(0x0095e41c), title_global<float>(0x0095e420),
                                        title_global<float>(0x0095e424), title_global<float>(0x0095e41c)};
    ++title_global<uint32_t>(0x0095e398);
    title_function<void XB_CDECL()>(0x00064690)();  // nglBeginFrame
    nglAdvanceShaderTime(color, title_global<float>(0x008ddaa0));
    nglSetClearFlags(3);
    alignas(16) float fog[4];
    nglGetSceneFogColor(fog);
    nglSetClearColor(fog[0], fog[1], fog[2], 0.0f);
    nglSetClearDepth(1.0f);
    const float one = title_global<float>(0x0083eb20);
    const float bottom = static_cast<float>(nglGetScreenHeight()) - one;
    const float right = static_cast<float>(nglGetScreenWidth()) - one;
    nglSetViewportRect(0.0f, 0.0f, right, bottom);
}

PLATFORM_ASM_ENTRY(R_SetupEntityLighting_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(R_SetupEntityLighting) "addl $8, %esp\n"
                   "ret")

void XB_CDECL R_SetupEntityLighting(REntityLighting* cache, const float* origin) {
    const float timer = cache->timer - title_global<float>(0x008ddaa0);
    alignas(16) float position[4];
    std::memcpy(position, origin, sizeof(position));
    cache->timer = timer;
    if (cache->valid && (timer > title_global<float>(0x0083eafc) ||
                         (cache->origin[0] == origin[0] && cache->origin[1] == origin[1] &&
                          cache->origin[2] == origin[2]))) {
        R_ApplyCachedLighting(&cache->lighting);
        return;
    }
    int32_t cell;
    void* grid = R_GetCellLighting(position, 0, &cell);
    if (!grid || cell < 0) {
        R_SetDefaultLighting();
        return;
    }
    R_SampleLightGrid(cell, static_cast<const RLightGrid*>(grid), position, &cache->lighting);
    cache->valid = 1;
    uint32_t& seed = title_global<uint32_t>(0x008d5a70);
    seed = seed * 0x343fd + 0x269ec3;
    cache->origin[0] = origin[0];
    cache->origin[1] = origin[1];
    cache->origin[2] = origin[2];
    // The next look-up in 0.14 + 0.08 * random seconds, on the x87.
    cache->timer = platform_x87_mul_div_add(platform_x87_unsigned_to_float(seed >> 17), title_global<float>(0x0083f0d4),
                                            title_global<float>(0x0083ebc8), title_global<float>(0x0083f0d0));
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

void XB_CDECL R_AddEntityToCells_r(const uint8_t* node, void* entity, const float* mins, const float* maxs) {
    // R_LinkEntityToCells_r's walk, linking the entity's bounds instead of a model.
    const RCellNode* n = reinterpret_cast<const RCellNode*>(node);
    while (n->cell == -2) {
        const float* plane = n->plane;
        const Lanes zero = lanes(&title_global<float>(0x0082e680));
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
            R_AddEntityToCells_r(reinterpret_cast<const uint8_t*>(n->children[0]), entity, mins, maxs);
            n = n->children[1];
        } else {
            n = reinterpret_cast<const RCellNode* const*>(&n->plane)[sides];
        }
    }
    if (n->cell < 0) return;
    const uint8_t* world = *reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095e608) + 0x100);
    uint32_t index = static_cast<uint32_t>(n->cell);
    if (index >= *reinterpret_cast<const uint32_t*>(world + 0x18)) index = 0;
    R_AddCellEntityLink(*reinterpret_cast<uint8_t* const*>(world + 0x1c) + index * 0x50, mins, maxs, entity);
}

void XB_CDECL R_AddEntitiesToCells(const RCullPlane* planes, int32_t count) {
    const int32_t enabled = *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0095e234) + 0x20);
    const auto cell_at = [](uint32_t index) {
        const uint8_t* world = *reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095e608) + 0x100);
        if (index >= *reinterpret_cast<const uint32_t*>(world + 0x18)) index = 0;
        return *reinterpret_cast<uint8_t* const*>(world + 0x1c) + index * 0x50;
    };
    for (uint32_t i = 0;
         i < *reinterpret_cast<const uint32_t*>(
                 *reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095e608) + 0x100) + 0x18);
         ++i)
        *reinterpret_cast<uint32_t*>(cell_at(i) + 0x38) = 0;
    title_global<int32_t>(0x0095e2d4) = 0;
    uint8_t* list = title_global<uint8_t*>(0x008db128);
    uint8_t* entity = *reinterpret_cast<uint8_t**>(list + 4);
    uint8_t* next = entity ? *reinterpret_cast<uint8_t**>(entity) : nullptr;
    if (entity == list + 8 || !next) return;
    // Like the original's stack locals, the box and the model bounds carry over from the
    // previous entity when this one has no model (or no bounds).
    float box[6] = {};     // mins, maxs
    float bounds[6] = {};  // the model's, before scaling
    for (;;) {
        entity[0xec] = 0;
        if (*reinterpret_cast<const int32_t*>(entity + 0x80) == 1 && enabled) {
            // Radius: the longest of the three axis vectors.
            const float* axis = reinterpret_cast<const float*>(entity + 0x94);
            const float a = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
            const float b = axis[3] * axis[3] + axis[4] * axis[4] + axis[5] * axis[5];
            const float c = axis[6] * axis[6] + axis[7] * axis[7] + axis[8] * axis[8];
            float longest = a;
            if (b > longest) longest = b;
            if (c > longest) longest = c;
            const float radius = platform_x87_sqrt(longest);
            const uint8_t* model = *reinterpret_cast<uint8_t* const*>(entity + 0xd4);
            if (model) {
                const float* model_bounds = *reinterpret_cast<const float* const*>(model + 0x38);
                if (model_bounds) {
                    for (int k = 0; k < 3; ++k) {
                        bounds[k] = model_bounds[k];
                        bounds[3 + k] = model_bounds[4 + k];
                    }
                }
                const float* origin = reinterpret_cast<const float*>(entity + 0xbc);
                for (int k = 0; k < 6; ++k) box[k] = bounds[k] * radius + origin[k % 3];
            }
            bool culled = false;
            const auto dist = [&box](const RCullPlane& plane, bool reversed) {
                const auto at = [&box](uint8_t offset) { return box[offset / 4]; };
                if (!reversed)
                    return at(plane.box_offset[0]) * plane.normal[0] + at(plane.box_offset[1]) * plane.normal[1] +
                           at(plane.box_offset[2]) * plane.normal[2];
                return at(plane.box_offset[2]) * plane.normal[2] + at(plane.box_offset[1]) * plane.normal[1] +
                       at(plane.box_offset[0]) * plane.normal[0];
            };
            for (int32_t p = 0; p < count && !culled; ++p) culled = planes[p].dist > dist(planes[p], false);
            if (!culled) {
                const RCullPlane& near_plane = *title_global<const RCullPlane*>(0x0095e2c4);
                culled = near_plane.dist > dist(near_plane, true);
            }
            if (!culled) {
                const RCullPlane* far_plane = title_global<const RCullPlane*>(0x0095e2c8);
                culled = far_plane && far_plane->dist > dist(*far_plane, true);
            }
            if (culled) {
                entity[0xec] = 2;
            } else {
                // Boxes over the limit along two axes are not linked.
                const float limit = title_global<float>(0x0083eb84);
                const bool wide_x = box[3] - box[0] > limit;
                const bool wide_y = box[4] - box[1] > limit;
                bool skip;
                if (wide_x)
                    skip = wide_y || box[5] - box[2] > limit;
                else
                    skip = wide_y && box[5] - box[2] > limit;
                if (!skip) {
                    alignas(16) float mins[4] = {box[0], box[1], box[2], box[0]};
                    alignas(16) float maxs[4] = {box[3], box[4], box[5], box[3]};
                    const uint8_t* light = *reinterpret_cast<uint8_t* const*>(entity + 0xd8);
                    const float range = *reinterpret_cast<const float*>(light + 0x12c) - title_global<float>(0x0083ece8);
                    bool linked = false;
                    if (range > 0.0f) {
                        const Lanes d = sub(lanes(reinterpret_cast<const float*>(light + 0x120)),
                                            lanes(reinterpret_cast<const float*>(light + 0x140)));
                        const Lanes d2 = mul(d, d);
                        platform_keep(d2);
                        if (range * range > d2.v[0] + d2.v[1] + d2.v[2]) {
                            // Within range of its light's origin: just the light's cell.
                            const uint8_t* tree = title_global<const uint8_t*>(0x0095f6b8);
                            uint32_t index = *reinterpret_cast<const uint32_t*>(light + 0x130);
                            if (index >= *reinterpret_cast<const uint32_t*>(tree + 8)) index = 0;
                            const int16_t cell = *reinterpret_cast<const int16_t*>(
                                *reinterpret_cast<uint8_t* const*>(tree + 0xc) + index * 16 + 2);
                            if (cell != -2) {
                                if (cell >= 0) R_AddCellEntityLink(cell_at(static_cast<uint32_t>(cell)), mins, maxs, entity);
                                linked = true;
                            }
                        }
                    }
                    if (!linked) {
                        const uint8_t* world = *reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095e608) + 0x100);
                        R_AddEntityToCells_r(*reinterpret_cast<uint8_t* const*>(world + 0xc), entity, mins, maxs);
                    }
                }
                entity[0xec] = 0;
            }
        }
        // The last entity (its next is null) is not visited.
        entity = next;
        next = *reinterpret_cast<uint8_t**>(entity);
        if (!next) break;
    }
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
        float a_max = title_global<float>(0x0083eb34);  // -1
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

namespace {

// The plane's box corner offsets: 4 * axis, + 12 when the component's bits as an
// integer are > 0 (R_CopyFrustumPlanes does the same).
void set_box_offsets(RCullPlane& plane) {
    for (int axis = 0; axis < 3; ++axis) {
        int32_t bits;
        std::memcpy(&bits, &plane.normal[axis], sizeof bits);
        plane.box_offset[axis] = static_cast<uint8_t>((bits > 0 ? 12 : 0) + 4 * axis);
    }
}

}  // namespace

void XB_CDECL R_SetupFrustum() {
    // Q3's R_SetupFrustum: the side planes from the view axes (0x0095e3ac, three rows)
    // and the half field of view angles (0x0095e4c4 x, 0x0095e4c8 y, degrees).
    const auto sin_cos = title_function<void XB_REGS3(float*, uint32_t, float*, float)>(0x000110f0);  // FastSinCos
    const float* axis = &title_global<float>(0x0095e3ac);
    float* frustum = &title_global<float>(0x0095e518);  // 4 planes, 0x14 bytes apart
    const float to_half_radians = title_global<float>(0x0083ed0c);  // pi / 2
    float s, c;
    sin_cos(&s, 0, &c, platform_x87_div_mul(title_global<float>(0x0095e4c4), 180.0f, to_half_radians));
    for (int i = 0; i < 3; ++i) frustum[i] = axis[3 + i] * c + axis[i] * s;
    float neg = 0.0f - c;
    for (int i = 0; i < 3; ++i) frustum[5 + i] = neg * axis[3 + i] + axis[i] * s;
    sin_cos(&s, 0, &c, platform_x87_div_mul(title_global<float>(0x0095e4c8), 180.0f, to_half_radians));
    for (int i = 0; i < 3; ++i) frustum[10 + i] = axis[6 + i] * c + axis[i] * s;
    neg = 0.0f - c;
    for (int i = 0; i < 3; ++i) frustum[15 + i] = neg * axis[6 + i] + axis[i] * s;
    const float* origin = &title_global<float>(0x0095e3a0);
    for (int i = 0; i < 4; ++i) {
        float* plane = frustum + i * 5;
        uint8_t* bytes = reinterpret_cast<uint8_t*>(plane + 4);
        bytes[0] = 3;  // PLANE_NON_AXIAL
        plane[3] = plane[2] * origin[2] + plane[1] * origin[1] + origin[0] * plane[0];
        uint8_t bits = 0;
        for (int axis_index = 0; axis_index < 3; ++axis_index)
            if (0.0f > plane[axis_index]) bits |= static_cast<uint8_t>(1 << axis_index);
        bytes[1] = bits;
    }
}

void XB_CDECL R_RotateForViewer() {
    // Q3's R_RotateForViewer: tr.or (0x0095e568, 31 floats) is the identity at the view
    // origin, its model matrix the view axes transposed with -origin, times the flip matrix
    // (0x008db150); viewParms.world (0x0095e41c) gets a copy.
    uint32_t* orientation = &title_global<uint32_t>(0x0095e568);
    for (int i = 0; i < 31; ++i) orientation[i] = 0;
    float* o = reinterpret_cast<float*>(orientation);
    const float one = title_global<float>(0x0083eb20);
    o[3] = one;
    o[7] = one;
    o[11] = one;
    const float* view_origin = &title_global<float>(0x0095e3a0);
    for (int i = 0; i < 3; ++i) o[12 + i] = view_origin[i];
    const float origin[3] = {view_origin[0], view_origin[1], view_origin[2]};
    // The axes as three 16-byte rows (each read 4 floats from its start), adjusted by
    // 0x002c9270 (0x00964b80 as `this`).
    alignas(16) float axis[3][4];
    for (int row = 0; row < 3; ++row)
        std::memcpy(axis[row], &title_global<float>(0x0095e3ac + row * 12), sizeof axis[row]);
    title_function<void XB_THISCALL(void*, float*)>(0x002c9270)(&title_global<uint8_t>(0x00964b80), axis[0]);
    alignas(16) float viewer[16];
    for (int row = 0; row < 3; ++row) {
        viewer[row] = axis[row][0];
        viewer[4 + row] = axis[row][1];
        viewer[8 + row] = axis[row][2];
        viewer[12 + row] = 0.0f - (axis[row][2] * origin[2] + axis[row][1] * origin[1] + axis[row][0] * origin[0]);
    }
    viewer[3] = 0.0f;
    viewer[7] = 0.0f;
    viewer[11] = 0.0f;
    viewer[15] = one;
    myGLMultMatrix(viewer, o + 15, &title_global<float>(0x008db150));
    std::memcpy(&title_global<uint32_t>(0x0095e41c), orientation, 31 * 4);
}

void XB_REGS3 MatrixInverse4x4(const float* m, uint32_t /*unused*/, float* dst) {
    // Cofactors in MSVC's evaluation order (lifted from the SSE code: each sum runs
    // differently from the textbook formula).
    dst[0] = ((((m[13] * (m[6] * m[11])) + (m[9] * (m[7] * m[14]))) + (m[5] * (m[15] * m[10]))) - (((m[13] * (m[7] * m[10])) + (m[9] * (m[6] * m[15]))) + (m[5] * (m[11] * m[14]))));
    dst[1] = ((((m[13] * (m[3] * m[10])) + (m[9] * (m[2] * m[15]))) + (m[1] * (m[11] * m[14]))) - (((m[13] * (m[2] * m[11])) + (m[9] * (m[3] * m[14]))) + (m[1] * (m[15] * m[10]))));
    dst[2] = ((((m[13] * (m[2] * m[7])) + (m[5] * (m[3] * m[14]))) + (m[1] * (m[6] * m[15]))) - (((m[13] * (m[3] * m[6])) + (m[5] * (m[2] * m[15]))) + (m[1] * (m[7] * m[14]))));
    dst[3] = ((((m[9] * (m[3] * m[6])) + (m[5] * (m[2] * m[11]))) + (m[1] * (m[7] * m[10]))) - (((m[9] * (m[2] * m[7])) + (m[5] * (m[3] * m[10]))) + (m[1] * (m[6] * m[11]))));
    dst[4] = ((((m[12] * (m[7] * m[10])) + (m[8] * (m[6] * m[15]))) + (m[4] * (m[11] * m[14]))) - (((m[12] * (m[6] * m[11])) + (m[8] * (m[7] * m[14]))) + (m[4] * (m[15] * m[10]))));
    dst[5] = ((((m[12] * (m[2] * m[11])) + (m[0] * (m[15] * m[10]))) + (m[8] * (m[3] * m[14]))) - (((m[12] * (m[3] * m[10])) + (m[0] * (m[11] * m[14]))) + (m[8] * (m[2] * m[15]))));
    dst[6] = ((((m[12] * (m[3] * m[6])) + (m[0] * (m[7] * m[14]))) + (m[4] * (m[2] * m[15]))) - (((m[12] * (m[2] * m[7])) + (m[0] * (m[6] * m[15]))) + (m[4] * (m[3] * m[14]))));
    dst[7] = ((((m[0] * (m[6] * m[11])) + (m[8] * (m[2] * m[7]))) + (m[4] * (m[3] * m[10]))) - (((m[0] * (m[7] * m[10])) + (m[8] * (m[3] * m[6]))) + (m[4] * (m[2] * m[11]))));
    dst[8] = (((((m[12] * m[5]) * m[11]) + (m[7] * (m[8] * m[13]))) + ((m[4] * m[9]) * m[15])) - (((m[7] * (m[12] * m[9])) + ((m[4] * m[13]) * m[11])) + ((m[8] * m[5]) * m[15])));
    dst[9] = ((((m[3] * (m[12] * m[9])) + ((m[0] * m[13]) * m[11])) + ((m[8] * m[1]) * m[15])) - ((((m[12] * m[1]) * m[11]) + (m[3] * (m[8] * m[13]))) + ((m[0] * m[9]) * m[15])));
    dst[10] = (((((m[12] * m[1]) * m[7]) + (m[3] * (m[4] * m[13]))) + ((m[0] * m[5]) * m[15])) - (((m[3] * (m[12] * m[5])) + ((m[0] * m[13]) * m[7])) + ((m[4] * m[1]) * m[15])));
    dst[11] = ((((m[3] * (m[8] * m[5])) + ((m[0] * m[9]) * m[7])) + ((m[4] * m[1]) * m[11])) - (((m[3] * (m[4] * m[9])) + ((m[8] * m[1]) * m[7])) + ((m[0] * m[5]) * m[11])));
    dst[12] = (((((m[8] * m[5]) * m[14]) + (m[6] * (m[12] * m[9]))) + ((m[4] * m[13]) * m[10])) - ((((m[4] * m[9]) * m[14]) + (m[6] * (m[8] * m[13]))) + ((m[12] * m[5]) * m[10])));
    dst[13] = (((((m[0] * m[9]) * m[14]) + (m[2] * (m[8] * m[13]))) + ((m[12] * m[1]) * m[10])) - ((((m[8] * m[1]) * m[14]) + (m[2] * (m[12] * m[9]))) + ((m[0] * m[13]) * m[10])));
    dst[14] = ((((m[2] * (m[12] * m[5])) + ((m[0] * m[13]) * m[6])) + ((m[4] * m[1]) * m[14])) - ((((m[12] * m[1]) * m[6]) + (m[2] * (m[4] * m[13]))) + ((m[0] * m[5]) * m[14])));
    dst[15] = ((((m[2] * (m[4] * m[9])) + ((m[8] * m[1]) * m[6])) + ((m[0] * m[5]) * m[10])) - (((m[2] * (m[8] * m[5])) + ((m[0] * m[9]) * m[6])) + ((m[4] * m[1]) * m[10])));
    const float det = dst[2] * m[8] + dst[3] * m[12] + m[0] * dst[0] + dst[1] * m[4];
    const float inv = title_global<float>(0x0083eb20) / det;  // 1
    for (int i = 0; i < 16; ++i) dst[i] = dst[i] * inv;
}

void XB_CDECL R_SetupPortalBevels() {
    // The world's model matrix (0x0095e458) times the projection (0x0095e4d4).
    const float* a = &title_global<float>(0x0095e458);
    const float* b = &title_global<float>(0x0095e4d4);
    float product[16];
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            product[i * 4 + j] = a[i * 4 + 3] * b[12 + j] + a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + b[8 + j] * a[i * 4 + 2];
    MatrixInverse4x4(product, 0, &title_global<float>(0x0095e318));
    float* transposed = &title_global<float>(0x0095e2d8);
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 4; ++i) transposed[k * 4 + i] = product[i * 4 + k];
}

void XB_CDECL R_SetupCullPlanes() {
    uint32_t& unlock = title_global<uint32_t>(0x008ff018);
    if (!unlock && *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0095e6cc) + 0x20)) return;  // r_lockpvs
    unlock = 0;
    R_CopyFrustumPlanes();
    const float* origin = &title_global<float>(0x0095e3a0);
    const float* forward = &title_global<float>(0x0095e3ac);
    float* view_origin = &title_global<float>(0x0095e240);
    for (int axis = 0; axis < 3; ++axis) view_origin[axis] = origin[axis];
    RCullPlane& near_plane = title_global<RCullPlane>(0x0095e29c);
    for (int axis = 0; axis < 3; ++axis) near_plane.normal[axis] = forward[axis];
    set_box_offsets(near_plane);
    near_plane.dist = forward[2] * origin[2] + forward[1] * origin[1] + forward[0] * origin[0] - 0.1f - 0.005f;
    title_global<uint32_t>(0x0095e2c4) = title_global<uint32_t>(0x0095e4a8) ? 0x0095e4b0 : 0x0095e29c;
    const float far_cull = title_global<float>(0x0095e2cc);
    float zfar = cvar_value(title_global<const Cvar*>(0x0095e378));
    if (far_cull > zfar) zfar = far_cull;
    if (zfar > 0.0f) {
        RCullPlane& far_plane = title_global<RCullPlane>(0x0095e2b0);
        for (int axis = 0; axis < 3; ++axis) far_plane.normal[axis] = forward[axis] * title_global<float>(0x0083eb34);  // -1, a MULSS
        set_box_offsets(far_plane);
        far_plane.dist = far_plane.normal[2] * origin[2] + far_plane.normal[1] * origin[1] +
                         far_plane.normal[0] * origin[0] - zfar - 0.005f;
        title_global<uint32_t>(0x0095e2c8) = 0x0095e2b0;
    } else {
        title_global<uint32_t>(0x0095e2c8) = 0;
    }
    if (cvar_value(title_global<const Cvar*>(0x0095e6e0)) > 0.0f)  // r_portalbevels
        R_SetupPortalBevels();
}

// The original reserves the cell entity link buffer (0x2000 bytes) right below its
// return address and stores its address in 0x0095e2d0: the entry does the same, so the
// global gets the same value.
PLATFORM_ASM_ENTRY(R_AddWorldSurfaces_entry,
                   "subl $0x2000, %esp\n"
                   "pushl %esp\n" PLATFORM_ASM_CALL(R_AddWorldSurfaces) "addl $0x2004, %esp\n"
                   "ret")

void XB_CDECL R_AddWorldSurfaces(void* link_buffer) {
    if (title_global<uint8_t>(0x0095e600) & 1) return;
    const uint32_t pass = (1u << title_global<uint8_t>(0x0095e604)) - 1;
    title_global<void*>(0x0095e2d0) = link_buffer;
    R_SetupCullPlanes();
    const uint8_t* planes = &title_global<uint8_t>(0x0095e24c);
    R_AddEntitiesToCells(reinterpret_cast<const RCullPlane*>(planes), 4);
    const auto world = [] { return *reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095e608) + 0x100); };
    const auto cell_at = [&](uint32_t index) {
        const uint8_t* w = world();
        if (index >= *reinterpret_cast<const uint32_t*>(w + 0x18)) index = 0;
        return *reinterpret_cast<uint8_t* const*>(w + 0x1c) + index * 0x50;
    };
    const int32_t view_cell = R_CellForViewOrigin();
    if (view_cell >= 0) {
        uint8_t* cell = cell_at(static_cast<uint32_t>(view_cell));
        if (*reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0095e6d4) + 0x20)) {  // r_singlecell
            title_global<uint32_t>(0x0095e2c8) = 0;
            R_AddCellSurfaces(cell, planes, 4);
            R_AddCellModels(cell);
        } else {
            R_VisitCell_r(cell, &title_global<uint8_t>(0x0095e29c), planes, 4, pass);
        }
        return;
    }
    // Outside the map: every cell, their models with outsideMapEnts.
    const bool models = *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0095e720) + 0x20);
    for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(world() + 0x18); ++i) {
        R_AddCellSurfaces(cell_at(i), planes, 4);
        if (models) R_AddCellModels(cell_at(i));
    }
}

void XB_CDECL R_AddEntitySurfaces() {
    if (!*reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0095e36c) + 0x20)) return;  // r_drawentities
    uint8_t* list = title_global<uint8_t*>(0x008db128);
    uint8_t* entity = *reinterpret_cast<uint8_t**>(list + 4);
    uint8_t* next = entity ? *reinterpret_cast<uint8_t**>(entity) : nullptr;
    alignas(16) float matrix[16];
    if (entity != list + 8 && next) {
        for (;;) {
            entity[0xed] &= 0xfe;
            ++title_global<int32_t>(0x0094d570);
            nglGetMatrix(matrix, 0);
            bool skip = false;
            if (title_global<uint32_t>(0x0095f550)) {
                // Too far from the view in the horizontal plane (squared, the camera's
                // translation row against the entity's origin).
                const Lanes d = sub(lanes(matrix + 12), lanes(reinterpret_cast<const float*>(entity + 0xbc)));
                const Lanes sq = mul(d, d);
                const float x2 = d.v[0] * d.v[0];
                const Lanes sum = {{x2 + sq.v[1], d.v[1] + sq.v[1], d.v[2] + sq.v[1], d.v[3] + sq.v[1]}};
                platform_keep(sq);
                platform_keep(sum);
                skip = sum.v[0] > title_global<float>(0x0083f150);
            }
            if (!skip) {
                ++title_global<int32_t>(0x00954e90);
                if (!(entity[0x84] & 4) || !title_global<uint32_t>(0x0095e4a4)) {
                    const uint32_t kind = *reinterpret_cast<const uint32_t*>(entity + 0x80);
                    if (kind == 1) {
                        reinterpret_cast<void XB_REGS1 XB_CDECL (*)(uint8_t*)>(&R_AddEntityModel_entry)(entity);
                        ++title_global<int32_t>(0x0094cb00);
                    } else if (kind == 2) {
                        title_function<void XB_CDECL()>(0x00042460)();  // breaks into a debugger when one is attached
                    } else if (kind == 3 || kind > 15) {
                        title_function<void XB_CDECL(int32_t, const char*, ...)>(title_global<uint32_t>(0x0095e634))(
                            1, reinterpret_cast<const char*>(uintptr_t{0x0083bf38}));
                    }
                }
            }
            // The last entity (its next is null) is not visited.
            entity = next;
            next = *reinterpret_cast<uint8_t**>(entity);
            if (!next) break;
        }
    }
    // The animated objects (handles: index in the low 12 bits, a use count above).
    for (int32_t i = 0; i < title_global<int32_t>(0x00960070); ++i) {
        ++title_global<int32_t>(0x0094cb98);
        const uint32_t handle = (&title_global<uint32_t>(0x00960074))[i];
        ++title_global<int32_t>(0x00954e90);
        const int32_t index = static_cast<int32_t>(handle & 0xfff);
        if (index >= 0x540) continue;
        const uint32_t* slot = &title_global<uint32_t>(0x0097a968 + index * 8);
        if ((handle >> 12) != slot[1]) continue;
        uint8_t* object = reinterpret_cast<uint8_t*>(uintptr_t{slot[0]});
        if (!object) continue;
        uint8_t* model = *reinterpret_cast<uint8_t**>(object + 0x22c);
        if (!model) continue;
        float* object_matrix = title_function<float* XB_STDCALL(uint8_t*, float*)>(0x002de120)(object, matrix);
        reinterpret_cast<uint32_t XB_CDECL (*)(uint8_t*, uint8_t*, float*, int32_t, int32_t, int32_t)>(
            &R_AddDObjSurfaces_entry)(model, object, object_matrix, 1, 0xff, 0);
    }
}

// Calls `function` with ESP at `top` minus its stack arguments (args[0] pushed last),
// EAX = eax and ESI = esi; returns its EAX. Callers that rebuild the original's frame
// use it so that each callee runs where the original's call puts it: some title
// functions store addresses of their stack locals, or copy uninitialized ones.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_at(uint32_t top, uint32_t function, uint32_t eax,
                                                          uint32_t esi, uint32_t count, const uint32_t* args);
PLATFORM_ASM_ENTRY(title_call_at,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "movl 8(%ebp), %edx\n"
                   "movl 24(%ebp), %ecx\n"
                   "movl 28(%ebp), %edi\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "decl %ecx\n"
                   "subl $4, %edx\n"
                   "movl (%edi,%ecx,4), %eax\n"
                   "movl %eax, (%edx)\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 16(%ebp), %eax\n"
                   "movl 20(%ebp), %esi\n"
                   "movl 12(%ebp), %ecx\n"
                   "movl %edx, %esp\n"
                   "call *%ecx\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %ebx\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebp\n"
                   "ret")

PLATFORM_ASM_ENTRY(title_call_at_regs,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "movl 8(%ebp), %edx\n"
                   "movl 20(%ebp), %ecx\n"
                   "movl 24(%ebp), %edi\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "decl %ecx\n"
                   "subl $4, %edx\n"
                   "movl (%edi,%ecx,4), %eax\n"
                   "movl %eax, (%edx)\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 16(%ebp), %eax\n"
                   "movl %edx, %esp\n"
                   "movl 4(%eax), %ebx\n"
                   "movl 8(%eax), %ecx\n"
                   "movl 12(%eax), %edx\n"
                   "movl 16(%eax), %esi\n"
                   "movl 20(%eax), %edi\n"
                   "movl 0(%eax), %eax\n"
                   "call *12(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %ebx\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebp\n"
                   "ret")

namespace {

// Calls from a rebuilt original frame: `frame` is the original's ESP after its
// prologue, `pending` the words its code still had pushed at that call.
struct OriginalFrame {
    uint32_t frame;
    template <typename F>
    static uint32_t address(F* function) {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(function));
    }
    uint32_t operator()(uint32_t pending, uint32_t function, uint32_t count = 0, const uint32_t* args = nullptr,
                        uint32_t eax = 0, uint32_t esi = 0) const {
        return title_call_at(frame - 4 * pending, function, eax, esi, count, args);
    }
    // Our own functions leave other values on the dead stack than the originals, and
    // title code called later may read uninitialized locals from there (the sky mesh's
    // nglUpdateScene, then a later caller: a mismatch until this). The stack they used
    // (16 KB below the call) is zeroed again, as the harness has it at a test's start.
    template <typename F>
    uint32_t operator()(uint32_t pending, F* function, uint32_t count = 0, const uint32_t* args = nullptr,
                        uint32_t eax = 0) const {
        const uint32_t top = frame - 4 * pending;
        const uint32_t result = title_call_at(top, address(function), eax, 0, count, args);
        constexpr uint32_t kScrub = 16 * 1024;
        const uint32_t below = top - 4 * count - 4;  // the return address's slot
        std::memset(reinterpret_cast<void*>(uintptr_t{below - kScrub}), 0, kScrub);
        return result;
    }
};

uint32_t float_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

}  // namespace

// Calls a title function that takes EAX, ESI and EDI (and no stack arguments); returns
// its EAX.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);
PLATFORM_ASM_ENTRY(title_call_regs,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 16(%ebp), %esi\n"
                   "movl 20(%ebp), %edi\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %ebx\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebp\n"
                   "ret")

namespace {

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename T>
T field(const void* base, uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof value);
    return value;
}

// Sets one word of an instance parameter block (a 64-bit slot mask, then a word per
// slot); the original rewrites the mask's high word unchanged.
void set_param_slot(uint32_t* params, uint32_t slot, uint32_t value) {
    params[0] |= 1u << (slot & 31);
    params[1] |= 0;
    params[2 + slot] = value;
}

// ((t + r0 * p.x) + r1 * p.y) + r2 * p.z over all four lanes (rows of 4 floats).
Lanes transform_point(const float* r0, const float* r1, const float* r2, const float* t, const Lanes& p) {
    return add(add(add(lanes(t), mul(lanes(r0), p.v[0])), mul(lanes(r1), p.v[1])), mul(lanes(r2), p.v[2]));
}

// A mesh's bounding sphere centre (+0x20) through `matrix`: the shadow copy is made
// only within the height band around the camera unless the entity is animated.
bool outside_shadow_band(const uint8_t* mesh, const float* matrix, float band_center, float band_half) {
    const Lanes center = transform_point(matrix, matrix + 4, matrix + 8, matrix + 12,
                                         lanes(reinterpret_cast<const float*>(mesh + 0x20)));
    platform_keep(center);
    const float offset = center.v[2] - band_center;
    return abs_bits(offset) > band_half;
}

// The parts of an LOD of a model; -1 = the first one it has.
const XModelParts* lod_parts(const XModel* model, int32_t lod) {
    if (lod < 0) {
        lod = 0;
        while (!model->lods[lod]) ++lod;
    }
    return model->lods[lod]->parts;
}

uint8_t* part_mesh(const XModelParts* parts, uint32_t index) {
    return parts->meshes[index < parts->mesh_count ? index : 0];
}

}  // namespace

// The original: push ebp, align to 16, 0x574 bytes of locals, then EBX/ESI/EDI. The C
// version runs below that frame (where the original's callees run).
PLATFORM_ASM_ENTRY(R_AddDObjSurfaces_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "andl $-16, %esp\n"
                   "subl $0x574, %esp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl %esp, %eax\n"
                   "pushl %eax\n"
                   "pushl 28(%ebp)\n"
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n" PLATFORM_ASM_CALL(R_AddDObjSurfaces) "addl $28, %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

uint32_t XB_CDECL R_AddDObjSurfaces(DObj* obj, uint8_t* entity, const float* matrix, uint8_t limit_depth,
                                    uint8_t alpha, uint8_t cast_shadow, uint8_t* frame) {
    const auto cvar_integer = [](uint32_t address) {
        return *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(address) + 0x20);
    };
    const auto scene = [] { return title_global<NglScene*>(0x0090259c); };
    if (obj->client_index != -1) {
        const uint8_t* client =
            *reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095f638) + 0x38 + obj->client_index * 4);
        if (!client || field<uint32_t>(client, 0xdc) != 0) return 0;
    }
    if (scene()->stale) nglUpdateScene(0);
    if (entity)
        title_function<void XB_CDECL(uint8_t*, DObj*, void*)>(title_global<uint32_t>(0x0095e6ac))(entity, obj,
                                                                                                frame + 0x570);
    uint32_t result = 0;

    // Lighting at the origin (or the parent's, for an entity linked to another).
    alignas(16) float origin[4] = {matrix[12], matrix[13], matrix[14], matrix[15]};
    NglLightSet* light_set = nglCreateLightSet();
    uint32_t flags = 0;
    if (entity) {
        flags = field<uint32_t>(entity, 0x2a4);
        uint8_t* const* parent = field<uint8_t* const*>(entity, 0x3cc);
        if (parent) {
            const uint8_t* p = *parent;
            flags = field<uint32_t>(p, 0x2a4);
            origin[0] = field<float>(p, 0x140);
            origin[1] = field<float>(p, 0x144);
            origin[2] = field<float>(p, 0x148);
        }
    }
    uintptr_t& arena = title_global<uintptr_t>(0x009025a8);
    uint32_t* params = reinterpret_cast<uint32_t*>((arena + 7) & ~uintptr_t{7});
    params[0] = 0;
    params[1] = 0;
    arena = reinterpret_cast<uintptr_t>(params + 2 + title_global<uint32_t>(0x00902904));
    const bool lit = !((flags >> 23) & 1);
    if (lit) {
        if (flags & 0x200000) {
            R_SetDefaultLighting();
        } else if (entity) {
            const uint32_t ref = title_call_regs(0x002be200, 0, address_of(entity), 0);  // CG_GetEntityRefEntity
            R_SetupEntityLighting(reinterpret_cast<REntityLighting*>(ref + 8), origin);
        } else {
            int32_t cell;
            void* grid = R_GetCellLighting(origin, 0, &cell);
            if (grid && cell >= 0)
                R_SampleLightGrid(cell, static_cast<const RLightGrid*>(grid), origin, nullptr);
            else
                R_SetDefaultLighting();
        }
    }
    for (uint8_t* light = field<uint8_t*>(scene()->light_set, 0x18);
         light != reinterpret_cast<uint8_t*>(scene()->light_set); light = field<uint8_t*>(light, 0x18))
        nglLightSetAddLight(field<uint32_t>(light, 0x28), field<float*>(light, 0x2c), 0xffffffff);
    set_param_slot(params, title_global<uint32_t>(0x00902914), address_of(light_set));

    const uint32_t extra_slot = title_global<uint32_t>(0x00902908);
    bool animated = false;
    if (entity && field<uint32_t>(entity, 0x248)) {
        animated = true;
        float time = static_cast<float>(field<int32_t>(entity, 0xa0));
        if (time != 0.0f) {
            time *= title_global<float>(0x0083eb4c);  // 0.001
            const float one = title_global<float>(0x0083eb20);
            const float w = title_global<float>(0x0082eca0);
            float* m = reinterpret_cast<float*>((arena + 15) & ~uintptr_t{15});
            const float rows[16] = {one, 0.0f, 0.0f, w,    0.0f, one,  0.0f, w,
                                    0.0f, 0.0f, one, w,    time, 0.0f, 0.0f, title_global<float>(0x0082ecb0)};
            std::memcpy(m, rows, sizeof rows);
            arena = reinterpret_cast<uintptr_t>(m + 16);
            set_param_slot(params, extra_slot, address_of(m));
        }
    }
    if (alpha != 0xff) {
        const float value = alpha == 0   ? title_global<float>(0x0083eb20)
                            : alpha == 1 ? title_global<float>(0x0083eb24)
                                         : static_cast<float>(static_cast<int32_t>(alpha)) * title_global<float>(0x0083eb30);
        set_param_slot(params, extra_slot, float_bits(value));
    }
    float band_center = 0.0f;
    float band_half = 0.0f;
    if (cast_shadow) {
        if (!lit || !cvar_integer(0x0095e700) || title_global<uint8_t>(0x0095f6b4)) {  // r_testshadow
            cast_shadow = 0;
        } else if (!animated) {
            const float z = scene()->camera_position[2];
            const float top = z + title_global<float>(0x008db120);
            band_center = ((z - title_global<float>(0x008db124)) + top) * title_global<float>(0x0083eb24);
            band_half = top - band_center;
        }
    }

    if (!cvar_integer(0x0095e7f0)) {  // r_showSkeletons
        auto& rigid = *reinterpret_cast<RDObjRigidParams*>(frame + 0xf0);
        auto& skinned = *reinterpret_cast<RDObjSkinnedParams*>(frame + 0x130);
        const uint32_t params_word = address_of(params);
        obj->cull_flags |= 1;
        const uint32_t owner_flags = obj->owner ? field<uint32_t>(obj->owner, 0x2a4) : 0;
        bool limited = false;
        if (!(owner_flags & 0x40000000) && obj->lod_limit) {
            const int32_t lod = obj->forced_lod >= 0 ? obj->forced_lod : obj->lod;
            const int32_t limit = field<int32_t>(obj->lod_limit, 4);
            if (lod < limit) {
                DObj_SetMinLod(limit, obj);
                limited = true;
            }
        }
        if (!limited) obj->min_lod = -1;

        for (int32_t m = 0; m < static_cast<int32_t>(obj->model_count); ++m) {
            const uint8_t* bones =
                obj->bone_matrices ? obj->bone_matrices + (&obj->matrix_offset)[m] * 0x40 + 0x30 : nullptr;
            const XModel* model = obj->models[m];
            int32_t lod = -1;
            if (m == 0) {
                lod = obj->forced_lod >= 0 ? obj->forced_lod : obj->min_lod >= 0 ? obj->min_lod : obj->lod;
                if (lod >= 5) {
                    // Too far for meshes: only the bounding sphere's visibility.
                    const Lanes mins = lanes(model->mins);
                    const Lanes maxs = lanes(model->maxs);
                    const Lanes center = add(mul(sub(maxs, mins), title_global<float>(0x0083eb24)), mins);
                    const Lanes d = sub(center, mins);
                    const Lanes sq = mul(d, d);
                    platform_keep(sq);
                    const float r2 = (sq.v[0] + sq.v[1]) + sq.v[2];
                    float radius = r2;
                    if (r2 == r2 && r2 != title_global<float>(0x0082e680)) {
                        const float y = platform_rsqrt_estimate(r2);
                        const float a = r2 * y;
                        radius = ((title_global<float>(0x0082e660) - a * y) * a) * title_global<float>(0x0082e670);
                    }
                    alignas(16) const Lanes world = transform_point(matrix, matrix + 4, matrix + 8, matrix + 12, center);
                    if (nglSphereInFrustum(world.v, radius)) obj->cull_flags &= ~1u;
                    continue;
                }
            }
            for (uint32_t i = 0;; ++i) {
                const XModelParts* parts = lod_parts(model, lod);
                if (static_cast<int32_t>(i) >= (parts ? static_cast<int32_t>(parts->bone_count) : 0)) break;
                uint8_t* mesh = part_mesh(lod_parts(model, lod), i);
                if (!mesh) continue;
                if (field<uint32_t>(mesh, 0x10)) {
                    // Skinned: the model's bone matrices go with it.
                    skinned.flags = 8;
                    skinned.skin = field<uint32_t>(mesh, 0x10);
                    skinned.bones = bones;
                    if (entity) {
                        const uint32_t tint = field<uint8_t*>(entity, 0x250)[0xee];
                        if (tint != 0xff) set_param_slot(params, title_global<uint32_t>(0x00902918), tint);
                    }
                    if (nglListAddMesh(mesh, matrix, &skinned.flags, &params_word) <= 0) continue;
                    obj->cull_flags &= ~1u;
                    result = 1;
                    if (!cast_shadow) continue;
                    if (!animated && outside_shadow_band(mesh, matrix, band_center, band_half)) continue;
                    NglScene* saved = nglSetCurrentScene(title_global<NglScene*>(0x0095f6cc));
                    const int32_t coarsest =
                        DObj_GetCoarsestLod(0, obj);
                    nglListAddMesh(part_mesh(lod_parts(model, coarsest), i), matrix, &skinned.flags, &params_word);
                    nglSetCurrentScene(saved);
                    continue;
                }

                // Rigid: the bone's matrix (rows of x y z; lane 3 repeats x) times `matrix`.
                const uintptr_t bone = reinterpret_cast<uintptr_t>(bones) + i * 0x40;
                Lanes r[4];
                for (int k = 0; k < 4; ++k) {
                    const float* row = reinterpret_cast<const float*>(bone + k * 0x10);
                    r[k] = {{row[0], row[1], row[2], row[0]}};
                }
                rigid.unknown04 = 0;
                rigid.unknown08 = 0;
                rigid.distance = title_global<float>(0x008da6d8);
                rigid.flags = 0x80;
                const Lanes m0 = lanes(matrix);
                const Lanes m1 = lanes(matrix + 4);
                const Lanes m2 = lanes(matrix + 8);
                alignas(16) Lanes world[4];
                for (int k = 0; k < 3; ++k)
                    world[k] = add(add(mul(m0, r[k].v[0]), mul(m1, r[k].v[1])), mul(m2, r[k].v[2]));
                world[3] = transform_point(matrix, matrix + 4, matrix + 8, matrix + 12, r[3]);
                if (limit_depth) {
                    // Pull the mesh in to end at the far distance, shrunk to look the same.
                    const float* c = reinterpret_cast<const float*>(mesh + 0x20);
                    const float radius = c[4];
                    const Lanes center = add(add(add(world[3], mul(world[0], c[0])), mul(world[1], c[1])),
                                             mul(world[2], c[2]));
                    const float* view = scene()->view_matrix;
                    const Lanes v = transform_point(view, view + 4, view + 8, view + 12, center);
                    const Lanes depth = add(Lanes{{radius, radius, radius, radius}},
                                            Lanes{{v.v[2], v.v[2], v.v[2], v.v[2]}});
                    platform_keep(depth);
                    const float far_z = title_global<float>(0x0095e514);
                    if (depth.v[0] > far_z) {
                        const float target = far_z - radius;
                        const float s = target / v.v[2];
                        const Lanes sv = mul(v, s);
                        platform_keep(sv);
                        // The view rotation transposed (lane 3 repeats lane 2).
                        const Lanes t0 = {{view[0], view[4], view[8], view[8]}};
                        const Lanes t1 = {{view[1], view[5], view[9], view[9]}};
                        const Lanes t2 = {{view[2], view[6], view[10], view[10]}};
                        const Lanes p = {{sv.v[0], sv.v[1], target, sv.v[0]}};
                        const Lanes d = sub(p, lanes(view + 12));
                        const Lanes offset =
                            mul(add(add(mul(world[0], c[0]), mul(world[1], c[1])), mul(world[2], c[2])), s);
                        world[3] = sub(add(add(mul(t0, d.v[0]), mul(t1, d.v[1])), mul(t2, d.v[2])), offset);
                        rigid.flags |= 2;
                        for (float& lane : rigid.scale) lane = s;
                    }
                }
                if (entity && !field<uint32_t>(entity, 0x248)) {
                    const float one = title_global<float>(0x0083eb20);
                    const uint8_t* ref = field<const uint8_t*>(entity, 0x250);
                    float scale = ref ? field<float>(ref, 0xf4) : one;
                    const uintptr_t info = 0x0096bd10 + uintptr_t{field<uint16_t>(entity, 6)} * 0x34;
                    bool scaled = scale != one;
                    if (info && field<int32_t>(reinterpret_cast<const void*>(info), 0x24) == 1 &&
                        field<uint32_t>(entity, 0x260) != title_global<uint32_t>(0x0097a788)) {
                        scale = title_global<float>(0x008ff01c);
                        scaled = true;
                    }
                    if (scaled) {
                        rigid.flags |= 2;
                        for (float& lane : rigid.scale) lane = scale;
                    }
                }
                if (nglListAddMesh(mesh, world[0].v, &rigid.flags, &params_word) <= 0) continue;
                obj->cull_flags &= ~1u;
                result = 1;
                if (!cast_shadow) continue;
                if (!animated && outside_shadow_band(mesh, matrix, band_center, band_half)) continue;
                NglScene* saved = nglSetCurrentScene(title_global<NglScene*>(0x0095f6cc));
                DObj_SetForcedLod(4, obj);
                const int32_t shadow_lod = m != 0                ? -1
                                           : obj->forced_lod >= 0 ? obj->forced_lod
                                           : obj->min_lod >= 0    ? obj->min_lod
                                                                  : obj->lod;
                rigid.flags |= 0x80;
                rigid.distance = title_global<float>(0x0083f0dc);  // 100000
                nglListAddMesh(part_mesh(lod_parts(model, shadow_lod), i), matrix, &rigid.flags, &params_word);
                obj->forced_lod = -1;
                nglSetCurrentScene(saved);
            }
        }
    }
    if (cvar_integer(0x0095e7f0)) {
        // The skeleton instead: `matrix` as rows of x y z, in the frame where the
        // skinned block lives.
        obj->cull_flags &= ~1u;
        float* axes = reinterpret_cast<float*>(frame + 0x130);
        for (int k = 0; k < 3; ++k) std::memcpy(axes + k * 3, matrix + k * 4, 12);
        std::memcpy(axes + 9, matrix + 12, 12);
        title_function<void XB_REGS1 XB_CDECL(float*, DObj*)>(0x00241b40)(axes, obj);  // R_DrawDObjSkeleton
    }
    return result;
}

// The original: push ebp, align to 16, 0x108 bytes of locals, then ESI/EDI; the C
// version runs 64 KB below, R_AddDObjSurfaces where the original calls it.
PLATFORM_ASM_ENTRY(R_AddEntityModel_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "andl $-16, %esp\n"
                   "subl $0x108, %esp\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl %esp, %ecx\n"
                   "subl $0x10000, %esp\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(R_AddEntityModel) "addl $0x10008, %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL R_AddEntityModel(uint8_t* entity, uint8_t* frame) {
    DObj* obj = field<DObj*>(entity, 0xd4);
    uint8_t* owner = field<uint8_t*>(entity, 0xd8);
    if (owner && (field<uint32_t>(owner, 0x2a4) & 0x200000)) return;
    if (!obj) return;
    // The axis (+0x94, rows of 3) and origin (+0xbc) as rows (lane 3 repeats x), at the
    // original's +0x60.
    float* matrix = reinterpret_cast<float*>(frame + 0x60);
    for (int k = 0; k < 4; ++k) {
        const float* row = reinterpret_cast<const float*>(entity + (k < 3 ? 0x94 + k * 0xc : 0xbc));
        matrix[k * 4 + 0] = row[0];
        matrix[k * 4 + 1] = row[1];
        matrix[k * 4 + 2] = row[2];
        matrix[k * 4 + 3] = row[0];
    }
    const uint8_t before = entity[0xed];
    const uint32_t args[] = {address_of(obj), address_of(owner), address_of(matrix), 0, entity[0xef],
                             static_cast<uint32_t>(!(before & 2))};
    const uint8_t drawn = static_cast<uint8_t>(
        title_call_at(address_of(frame), address_of(reinterpret_cast<const void*>(&R_AddDObjSurfaces_entry)),
                      address_of(obj), address_of(entity), 6, args));
    const uint8_t after = entity[0xed];
    entity[0xed] = static_cast<uint8_t>(after ^ ((after ^ drawn) & 1));
    if (entity[0xed] & 2) return;
    if (!*reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0095e700) + 0x20)) return;  // r_testshadow
    if (!title_global<uint8_t>(0x0095f6b4)) return;

    // A red quad at the origin's screen position, in the shadow scene.
    NglScene* saved = nglSetCurrentScene(title_global<NglScene*>(0x0095f6cc));
    const NglScene* s = title_global<NglScene*>(0x0090259c);
    const float* vm = s->view_matrix;
    const Lanes v1 = transform_point(vm, vm + 4, vm + 8, vm + 12, lanes(matrix + 12));
    const float* pm = s->base_projection;
    const Lanes v2 = transform_point(pm, pm + 4, pm + 8, pm + 12, v1);
    const float* sm = s->viewport;
    const Lanes v3 = add(add(add(mul(lanes(sm), v2.v[0]), mul(lanes(sm + 4), v2.v[1])), mul(lanes(sm + 8), v2.v[2])),
                         mul(lanes(sm + 12), v2.v[3]));
    const uint8_t* target = reinterpret_cast<const uint8_t*>(s->render_target);
    const float width = platform_x87_unsigned_to_float(field<uint32_t>(target, 0x10));
    const float height = platform_x87_unsigned_to_float(field<uint32_t>(target, 0x14));
    const float* half = &title_global<float>(0x00830160);
    const float k = title_global<float>(0x0082ecb0);
    const Lanes q = {{v3.v[0], v3.v[1], v3.v[2], v2.v[3]}};
    const Lanes scale = {{half[0], half[1], half[2], k}};
    const Lanes bias = {{half[0], half[1], half[2], title_global<float>(0x00830190)}};
    const Lanes size = {{width, height, title_global<float>(0x0083eafc), k}};
    const Lanes p = mul(add(mul(q, scale), bias), size);
    platform_keep(p);
    NglQuad quad;
    nglInitQuad(&quad);
    const float r = title_global<float>(0x0083ebb8);
    nglSetQuadRect(&quad, p.v[0] - r, p.v[1] - r, p.v[0] + r, p.v[1] + r);
    nglSetQuadColor(&quad, 0xffff0000);
    nglListAddQuad(&quad);
    nglSetCurrentScene(saved);
}

namespace {

// 1 / |v.xyz| as RSQRTSS and one Newton step (3.0 and 0.5 from 0x0082e660/0x0082e670);
// 0 for a zero vector (the zero vector 0x0082e6a0). Every lane of the squares is
// computed (MXCSR flags).
float unit_scale(const Lanes& v) {
    const Lanes squares = mul(v, v);
    platform_keep(squares);
    const float sum = (squares.v[0] + squares.v[1]) + squares.v[2];
    if (sum == 0.0f) return 0.0f;
    const float a = abs_bits(sum);
    const float r = platform_rsqrt_estimate(a);
    return ((title_global<float>(0x0082e660) - (r * r) * a) * r) * title_global<float>(0x0082e670);
}

// The lanes {y, z, x, w} (SHUFPS 0xc9).
Lanes yzxw(const Lanes& a) { return {{a.v[1], a.v[2], a.v[0], a.v[3]}}; }

void store_lanes(float* out, const Lanes& v) {
    for (int i = 0; i < 4; ++i) out[i] = v.v[i];
}

}  // namespace

void XB_CDECL R_RenderTestShadow() {
    NglTexture*& texture = title_global<NglTexture*>(0x0095f6c8);
    if (!texture) texture = nglCreateTexture(0x1101, 0x200, 0x200, 0, 0);
    float view[16];
    std::memcpy(view, nglGetViewMatrix(), sizeof view);
    // The camera: the view's rows 2 and 0 flattened (w lane = x, as in the original).
    const Lanes forward = {{view[8], view[9], 0.0f, view[8]}};
    const Lanes f = mul(forward, unit_scale(forward));
    const Lanes side = {{view[0], view[1], 0.0f, view[0]}};
    const Lanes g = mul(side, unit_scale(side));
    const Lanes h = add(neg(g), f);
    const Lanes down = {{0.0f, 0.0f, title_global<float>(0x0083eb34), 0.0f}};  // -1
    float* camera = &title_global<float>(0x00988730);
    store_lanes(camera + 8, down);
    const Lanes up = mul(h, unit_scale(h));
    store_lanes(camera + 4, up);
    // cross(up, down) in the packed order: (up * down.yzx - up.yzx * down).yzx.
    const Lanes across = yzxw(sub(mul(up, yzxw(down)), mul(yzxw(up), down)));
    store_lanes(camera, across);
    const float reach = title_global<float>(0x008db19c) * title_global<float>(0x0083f034);  // 0.45
    const Lanes lift = {{0.0f, 0.0f, title_global<float>(0x008db1a0), 0.0f}};
    store_lanes(camera + 12, add(add(sub(lanes(view + 12), mul(across, reach)), mul(up, reach)), lift));

    // Clear the texture.
    nglSceneAddRenderTarget(texture);
    nglPushScene(0);
    nglSetSceneDepthEnable(0);
    nglSetSceneDepthTest(0);
    nglSetRenderTarget(reinterpret_cast<NglRenderTarget*>(texture));
    nglSetViewportRect(0.0f, 0.0f, platform_x87_unsigned_to_float(texture->width),
                       platform_x87_unsigned_to_float(texture->height));
    nglSetClearFlags(0xf0);
    nglSetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    nglPopScene();

    // The shadow scene, filled by the entities drawn after this.
    title_global<NglScene*>(0x0095f6cc) = nglPushScene(0);
    nglSetSceneDepthEnable(0);
    nglSetSceneDepthTest(0);
    nglSetRenderTarget(reinterpret_cast<NglRenderTarget*>(texture));
    nglSetViewportRect(1.0f, 1.0f, platform_x87_unsigned_to_float(texture->width - 2),
                       platform_x87_unsigned_to_float(texture->height - 2));
    nglSetClearFlags(0);
    nglSetColorWriteMask(0x1000000);
    nglSetAspect(1.0f);
    nglSetCameraMatrix(camera);
    nglSetOrthographic(0.1f, title_global<float>(0x008db1a4));
    const float inverse = title_global<float>(0x0083eb20) / title_global<float>(0x008db19c);
    const float extent = inverse * title_global<float>(0x0083eba8);           // 2
    const float negative_extent = inverse * title_global<float>(0x0083ec5c);  // -2
    nglSetSceneViewportRect(negative_extent, negative_extent, extent, extent);
    nglUpdateScene(0);
    if (title_global<uint8_t>(0x0095f6d0)) {
        NglQuad quad;
        nglInitQuad(&quad);
        nglSetQuadRect(&quad, 192.0f, 192.0f, 320.0f, 320.0f);
        nglSetQuadColor(&quad, 0xff000000);
        nglListAddQuad(&quad);
    }
    nglPopScene();

    const float radius = title_global<float>(0x008db19c);
    alignas(16) const float scale[4] = {radius, 0.0f - radius, title_global<float>(0x0083ebec), radius};  // 500
    nglAddShadowProjector(0xffffffff, camera, scale, 2, 0, address_of(texture));
}

// The original's frame (aligned to 16, 0xa0 bytes: its locals - the view matrix copy at
// +0x60, the sky matrix at +0x20 - and two saved registers) is rebuilt, with 128 KB
// below it kept free: every call runs at the original's stack position from there.
// RE_RenderScene (0x002476b0): the view parameters live at the original's frame offset
// and R_RenderView runs at the original's ESP (it rebuilds its own frame from there).
PLATFORM_ASM_ENTRY(RE_RenderScene_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "leal 4(%ebp), %ecx\n"
                   "subl $0x20000, %esp\n"
                   "pushl 8(%ebp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(RE_RenderScene) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL RE_RenderScene(uint32_t entry, const uint8_t* refdef) {
    if (!title_global<uint32_t>(0x0095e390)) return;
    if (*reinterpret_cast<const int32_t*>(title_global<const uint8_t*>(0x0095e238) + 0x20)) return;  // r_norefresh
    // Every call runs at the original's ESP (push ebp; and esp,-8; sub esp,0x1cc; three
    // pushes): what the clock read leaves below it is read later as stale locals in
    // R_RenderView's subtree.
    const uint32_t top = ((entry - 4) & ~7u) - 0x1cc - 12;
    const TitleRegs no_regs{};
    title_call_at_regs(top, title_global<uint32_t>(0x0095e638), &no_regs, 0, nullptr);  // ri.Milliseconds
    const auto word = [refdef](uint32_t offset) { return *reinterpret_cast<const uint32_t*>(refdef + offset); };
    if (!title_global<uint32_t>(0x0095e608) && !(refdef[0x58] & 1)) {
        const uint32_t args[] = {1, 0x0083bed8};
        title_call_at_regs(top, title_global<uint32_t>(0x0095e634), &no_regs, 2, args);  // ri.Error
    }
    for (uint32_t i = 0; i < 6; ++i) title_global<uint32_t>(0x0095e5e4 + i * 4) = word(i * 4);  // tr.refdef x/y/w/h, fov
    title_global<uint32_t>(0x0095e5fc) = word(0x54);
    title_global<uint32_t>(0x0095e600) = word(0x58);
    if (refdef[0x58] & 8) title_global<uint32_t>(0x0095e7e8) = 1;
    title_global<uint32_t>(0x0095e364) = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(refdef[0x58])) & 0x10) >> 4;
    uint16_t lights = static_cast<uint16_t>(title_global<uint32_t>(0x0095e628) - title_global<uint32_t>(0x0095e6bc));
    title_global<uint16_t>(0x0095e604) = lights;
    title_global<uint16_t>(0x0095e606) = lights;
    const uint8_t* dynamic_light = title_global<const uint8_t*>(0x0095e6ec);
    const auto mode = [dynamic_light] { return *reinterpret_cast<const int32_t*>(dynamic_light + 0x20); };
    if (!mode()) title_global<uint16_t>(0x0095e606) = 0;
    if (mode() != 1) {
        lights = 0;
        title_global<uint16_t>(0x0095e604) = 0;
    }
    if (mode() == 3) {
        const uint32_t args[] = {0, 0x0083bebc, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(lights)))};
        title_call_at_regs(top, title_global<uint32_t>(0x0095e630), &no_regs, 3, args);  // ri.Printf
    }

    // The view parameters (0x1c8 bytes) at the original's ESP + 0x10.
    uint8_t* parms = reinterpret_cast<uint8_t*>(uintptr_t{top + 0x10});
    std::memset(parms, 0, 0x72 * 4);
    const auto field = [parms](uint32_t offset) -> float& { return *reinterpret_cast<float*>(parms + offset); };
    const auto cvar_value = [](uint32_t cvar) { return *reinterpret_cast<const float*>(title_global<const uint8_t*>(cvar) + 0x1c); };
    const float fov_x = title_global<float>(0x0095e5f4);
    const float fov_y = title_global<float>(0x0095e5f8);
    field(0x124) = fov_x;
    field(0x128) = fov_y;
    const float lod_scale = cvar_value(0x0095e620);
    const float lod_limit = title_global<float>(0x0083100c);
    field(0x130) = lod_scale > lod_limit ? lod_limit : lod_scale;
    const float lod_bias = cvar_value(0x0095e384);
    field(0x12c) = lod_bias > 0.0f ? 0.0f : lod_bias;
    field(0x174) = cvar_value(0x0095e378);  // r_zfar
    const float fov = fov_y >= fov_x ? fov_x : fov_y;
    std::memcpy(parms, refdef + 0x20, 3 * 4);       // origin
    std::memcpy(parms + 0xc, refdef + 0x30, 9 * 4);  // axis
    std::memcpy(parms + 0xf8, refdef + 0x20, 3 * 4);
    // The LOD scale and bias by tan(fov / 2) / tan(c), the ratio kept on the x87.
    const long double ratio = platform_x87_tan_ext(fov * title_global<float>(0x0083ecf0)) /
                              platform_x87_tan_ext(title_global<double>(0x0083f260));
    field(0x130) = static_cast<float>(field(0x130) * ratio);
    field(0x12c) = static_cast<float>(ratio * field(0x12c));

    const TitleRegs regs{0, top + 0x10, 0, 0, 0, 0};
    // By its title address: R_RenderView is ours in the game (the hook), the original in
    // tests, where ours still mismatches on stale-stack lanes.
    title_call_at_regs(top, 0x00247350, &regs, 0, nullptr);  // R_RenderView
    title_global<uint32_t>(0x0095e6bc) = title_global<uint32_t>(0x0095e628);
    title_global<uint32_t>(0x0095e6f4) = title_global<uint32_t>(0x0095e738);
}

PLATFORM_ASM_ENTRY(R_RenderView_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "andl $-16, %esp\n"
                   "subl $0xa0, %esp\n"
                   "movl %esp, %eax\n"
                   "subl $0x20000, %esp\n"
                   "pushl %eax\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(R_RenderView) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL R_RenderView(const uint8_t* parms, uint8_t* frame) {
    const OriginalFrame at{static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame))};
    int32_t& view_count = title_global<int32_t>(0x0095e39c);
    ++view_count;
    std::memcpy(&title_global<uint8_t>(0x0095e3a0), parms, 0x72 * 4);  // tr.viewParms
    title_global<uint32_t>(0x0095e4ac) = title_global<uint32_t>(0x0095e398);
    ++view_count;
    at(0, &R_RotateForViewer);
    at(0, &R_SetupFrustum);
    static const uint32_t kOne[] = {1};
    static const uint32_t kZero[] = {0};
    at(0, &nglPushScene, 1, kOne);
    at(0, &R_SetupFog);
    const uint32_t aspect[] = {float_bits(title_global<uint32_t>(0x00948f64) ? title_global<float>(0x0083f254)
                                                                             : title_global<float>(0x0083eb80))};
    at(0, &nglSetAspect, 1, aspect);
    float far_z = *reinterpret_cast<const float*>(parms + 0x174);
    if (far_z == 0.0f) far_z = title_global<float>(0x00830bd8);
    const uint32_t fov = *reinterpret_cast<const uint32_t*>(parms + 0x128);
    const uint32_t perspective[] = {fov, float_bits(cvar_value(title_global<const Cvar*>(0x0095e7e0))),  // r_znear
                                    float_bits(far_z)};
    at(0, &nglSetPerspective, 3, perspective);
    float* view_matrix = reinterpret_cast<float*>(frame + 0x60);
    std::memcpy(view_matrix, &title_global<float>(0x0095e458), 64);  // viewParms.world's
    const uint32_t view_matrix_arg[] = {at.frame + 0x60};
    at(3, &nglSetViewMatrix, 1, view_matrix_arg);
    if (*reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095e608) + 0x108) &&
        title_global<uint32_t>(0x008db138)) {
        // The sky mesh around the view origin, without depth.
        at(0, &nglPushScene, 1, kOne);
        at(1, &nglSetSceneDepthEnable, 1, kZero);
        at(2, &nglSetSceneDepthTest, 1, kZero);
        const float* scale = &title_global<float>(0x0082e140);
        float* matrix = reinterpret_cast<float*>(frame + 0x20);
        for (int row = 0; row < 3; ++row)
            for (int i = 0; i < 4; ++i) matrix[row * 4 + i] = (&title_global<float>(0x0082e160 + row * 16))[i] * scale[0];
        const float* origin = &title_global<float>(0x0095e3a0);
        float* scratch = reinterpret_cast<float*>(frame + 0x10);  // the original builds row 3 here first
        scratch[0] = origin[0];
        scratch[1] = origin[1];
        scratch[2] = origin[2];
        scratch[3] = origin[0];
        std::memcpy(matrix + 12, scratch, 16);  // its w lane is x, as in the original
        const uint32_t mesh_args[] = {
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                *reinterpret_cast<uint8_t* const*>(title_global<uint8_t*>(0x0095e608) + 0x108))),
            at.frame + 0x20, 0, 0};
        at(3, &nglListAddMesh, 4, mesh_args);
        at(0, &nglPopScene);
    }
    at(0, &nglPushScene, 1, kOne);
    static const uint32_t kOpaqueCallback[] = {0, 0x00045690, 0};
    at(1, &nglSetSceneCallback, 3, kOpaqueCallback);
    const auto cvar_integer = [](uint32_t address) {
        return *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(address) + 0x20);
    };
    if (cvar_integer(0x0095e700)) at(0, &R_RenderTestShadow);
    static const uint32_t kFogCallback[] = {1, 0x00073380, 0};  // nglDrawFogPass
    at(0, &nglSetSceneCallback, 3, kFogCallback);
    if (title_global<uint32_t>(0x008db13c)) at(0, 0x00282040);  // CG_AddLocalEntities
    at(0, 0x0028a760);                                           // CG_AddPacketEntities
    if (title_global<uint32_t>(0x008db134)) at(0, 0x00246810);  // FX_DrawEffects
    if (title_global<uint32_t>(0x008db130)) {
        at(0, &R_SetupProjection);
        const uint8_t* world = title_global<const uint8_t*>(0x0095e608);
        if (world && *reinterpret_cast<const uint32_t*>(world + 0x100)) at(0, &R_AddWorldSurfaces_entry);
        if (title_global<uint32_t>(0x008db148)) at(0, &R_AddEntitySurfaces);
    }
    const uint32_t effects_arg[] = {title_global<uint32_t>(0x0095f4f8)};
    at(0, 0x002a8d30, 1, effects_arg);
    if (title_global<uint32_t>(0x008db144)) {
        const uint32_t* callbacks = &title_global<uint32_t>(0x00988784);
        const uint32_t* end = callbacks + title_global<int32_t>(0x00988780);
        for (; callbacks != end; ++callbacks) at(1, *callbacks);
    }
    at(1, &nglPopScene);
    at(1, &R_ClearAlpha);
    // The view model (gun) scene: entities of the list at 0x008db128 with the cvar at
    // 0x0095e700 cleared and 0x0095f6d2 set.
    uint8_t* shadow_cvar = title_global<uint8_t*>(0x0095e700);
    const int32_t saved = *reinterpret_cast<const int32_t*>(shadow_cvar + 0x20);
    *reinterpret_cast<int32_t*>(shadow_cvar + 0x20) = 0;
    title_global<uint8_t>(0x0095f6d2) = 1;
    at(1, &nglPushScene, 1, kOne);
    const uint32_t view_model_perspective[] = {fov, 0x3f000000, 0x43000000};  // 0.5, 128
    at(2, &nglSetPerspective, 3, view_model_perspective);
    static const uint32_t kClearFlags[] = {3};
    at(5, &nglSetClearFlags, 1, kClearFlags);
    uint8_t* list = title_global<uint8_t*>(0x008db128);
    uint8_t* node = *reinterpret_cast<uint8_t**>(list + 0x14);
    uint8_t* next = node ? *reinterpret_cast<uint8_t**>(node) : nullptr;
    if (node != list + 0x18 && next) {
        // The last node (its next is null) is not visited.
        for (;;) {
            if (*reinterpret_cast<const int32_t*>(node + 0x80) != 1) {
                static const uint32_t kMessage[] = {1, 0x0083bf20};
                at(0, title_global<uint32_t>(0x0095e634), 2, kMessage);
            } else {
                at(0, &R_AddEntityModel_entry, 0, nullptr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node)));
            }
            if (!next) break;
            node = next;
            next = *reinterpret_cast<uint8_t**>(next);
            if (!next) break;
        }
    }
    *reinterpret_cast<int32_t*>(title_global<uint8_t*>(0x0095e700) + 0x20) = saved;
    title_global<uint8_t>(0x0095f6d2) = 0;
    at(0, &nglPopScene);
    at(0, &nglPopScene);
    at(0, 0x003154b0, 0, nullptr, 0, 0x0095f8d0);  // ESI = 0x0095f8d0
    uint32_t* table = title_global<uint32_t*>(0x00909908);
    const int32_t count = static_cast<int32_t>(table[0]);
    for (int32_t i = 0; i < count; ++i) {
        if (*reinterpret_cast<const int32_t*>(table[1] + i * 0x104) == 2) {
            at(0, 0x00042460);  // breaks into a debugger when one is attached
            table = title_global<uint32_t*>(0x00909908);
        }
    }
    table[0] = 0;
}

PLATFORM_ASM_ENTRY(R_CheckCvarRange_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(R_CheckCvarRange) "addl $16, %esp\n"
                   "ret")

void XB_CDECL R_CheckCvarRange(uint32_t integral, const uint8_t* cvar, float minimum, float maximum) {
    using Printf = void XB_CDECL(int32_t, const char*, ...);
    using Va = const char* XB_CDECL(const char*, ...);
    using CvarSet = void XB_CDECL(const char*, const char*);
    const auto printf = title_function<Printf>(title_global<uint32_t>(0x0095e630));  // ri.Printf
    const auto va = title_function<Va>(0x002a93d0);
    const auto cvar_set = [](const char* name, const char* value) {
        title_function<CvarSet>(title_global<uint32_t>(0x0095e658))(name, value);  // ri.Cvar_Set
    };
    const char* const& name = *reinterpret_cast<const char* const*>(cvar);
    const float& value = *reinterpret_cast<const float*>(cvar + 0x1c);
    const int32_t& integer = *reinterpret_cast<const int32_t*>(cvar + 0x20);
    if (integral && static_cast<int32_t>(value) != integer) {
        printf(2, "WARNING: cvar '%s' must be integral (%f)\n", name, static_cast<double>(value));
        cvar_set(name, va("%d", integer));
    }
    if (minimum > value) {
        printf(2, "WARNING: cvar '%s' out of range (%f < %f)\n", name, static_cast<double>(value),
               static_cast<double>(minimum));
        cvar_set(name, va("%f", static_cast<double>(minimum)));
    } else if (value > maximum) {
        printf(2, "WARNING: cvar '%s' out of range (%f > %f)\n", name, static_cast<double>(value),
               static_cast<double>(maximum));
        cvar_set(name, va("%f", static_cast<double>(maximum)));
    }
}

uint32_t XB_CDECL RE_RegisterModel(uint32_t handle, const char* name, uint32_t flags) {
    using RegisterModel = void XB_REGS1 XB_STDCALL(const char*, uint32_t, uint32_t, uint32_t);
    title_function<RegisterModel>(0x00241f40)(name, title_global<uint32_t>(0x0095f6ac), handle, flags);  // R_RegisterModel
    return handle;
}

uint32_t XB_CDECL R_Register() {
    using CvarGet = uint32_t XB_CDECL(const char*, const char*, uint32_t);
    const auto Cvar_Get = title_function<CvarGet>(0x0029c2d0);
    const auto cvar_get = title_function<CvarGet>(title_global<uint32_t>(0x0095e650));  // ri.Cvar_Get
    const auto add_command = [](const char* name, uint32_t function) {
        title_function<void XB_CDECL(const char*, uint32_t)>(title_global<uint32_t>(0x0095e65c))(  // ri.Cmd_AddCommand
            name, function);
    };
    const auto text = [](uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); };
    uint32_t eax = 0;
    title_global<uint32_t>(0x0095e704) = Cvar_Get(text(0x008340d0), text(0x0082e098), 0x48);  // r_sv_cheats
    title_global<uint32_t>(0x0095e718) = cvar_get(text(0x0083c3c8), text(0x0082e098), 0x21);  // r_texturebits
    title_global<uint32_t>(0x0095e610) = cvar_get(text(0x0083c3c0), text(0x008398e0), 0x21);  // r_mode
    title_global<uint32_t>(0x0095e368) = cvar_get(text(0x0083c3b0), text(0x0082e058), 0x21);  // r_fullscreen
    title_global<uint32_t>(0x0095e6dc) = cvar_get(text(0x0083c3a0), text(0x0082e098), 0x20);  // r_weightMipMaps
    title_global<uint32_t>(0x0095e6c0) = cvar_get(text(0x0083c390), text(0x0082e098), 0x0);  // r_uifullscreen
    title_global<uint32_t>(0x0095e624) = cvar_get(text(0x0083c380), text(0x0082e098), 0x220);  // r_fullbright
    title_global<uint32_t>(0x0095e384) = cvar_get(text(0x0083c374), text(0x0082e098), 0x1);  // r_lodbias
    title_global<uint32_t>(0x0095e35c) = cvar_get(text(0x0083c368), text(0x0082e058), 0x1);  // r_flares
    eax = cvar_get(text(0x0083c35c), text(0x0083c364), 0x100);
    const uint32_t znear = eax;
    title_global<uint32_t>(0x0095e7e0) = znear;  // r_znear
    R_CheckCvarRange(1, reinterpret_cast<const uint8_t*>(uintptr_t{znear}), from_bits(0x3a83126f), from_bits(0x43480000));
    title_global<uint32_t>(0x0095e378) = cvar_get(text(0x0083c34c), text(0x0083c354), 0x200);  // r_zfar
    title_global<uint32_t>(0x0095e6ec) = cvar_get(text(0x0083c33c), text(0x0082e058), 0x1);  // r_dynamiclight
    title_global<uint32_t>(0x0095e714) = cvar_get(text(0x0083c330), text(0x0082e098), 0x1);  // r_finish
    title_global<uint32_t>(0x0095e380) = cvar_get(text(0x0083c304), text(0x0083c314), 0x1);  // r_textureMode
    title_global<uint32_t>(0x0095e734) = cvar_get(text(0x0083c2f8), text(0x0082e098), 0x1);  // r_swapDelay
    title_global<uint32_t>(0x0095e388) = cvar_get(text(0x0083c2e8), text(0x00839850), 0x100);  // r_LightScale
    title_global<uint32_t>(0x0095e61c) = cvar_get(text(0x0083c2d8), text(0x00839454), 0x1);  // r_maxEntLights
    title_global<uint32_t>(0x0095e6fc) = cvar_get(text(0x0083c2c0), text(0x00832818), 0x201);  // r_minEntLightIntensity
    title_global<uint32_t>(0x0095e6b8) = cvar_get(text(0x0083c2b0), text(0x0082e098), 0x20);  // r_vc_makelog
    title_global<uint32_t>(0x0095e728) = cvar_get(text(0x0083c2a0), text(0x0082e098), 0x0);  // r_vc_showlog
    title_global<uint32_t>(0x0095e6f8) = cvar_get(text(0x0083c290), text(0x0082e098), 0x20);  // r_vc_compile
    title_global<uint32_t>(0x0095e7ec) = cvar_get(text(0x0083c288), text(0x0082e058), 0x200);  // r_fog
    title_global<uint32_t>(0x0095e37c) = cvar_get(text(0x0083c278), text(0x0082e098), 0x200);  // r_portalOnly
    title_global<uint32_t>(0x0095e620) = cvar_get(text(0x0083c26c), text(0x0082e058), 0x1);  // r_lodscale
    title_global<uint32_t>(0x0095e238) = cvar_get(text(0x0083c260), text(0x0082e098), 0x200);  // r_norefresh
    title_global<uint32_t>(0x0095e36c) = cvar_get(text(0x0083c250), text(0x0082e058), 0x200);  // r_drawentities
    title_global<uint32_t>(0x0095e6c8) = cvar_get(text(0x0083c244), text(0x0082e098), 0x200);  // r_nocull
    title_global<uint32_t>(0x0095e720) = cvar_get(text(0x0083c234), text(0x0082e098), 0x200);  // r_outsideMapEnts
    title_global<uint32_t>(0x0095e724) = cvar_get(text(0x0083c228), text(0x0082e098), 0x200);  // r_speeds
    title_global<uint32_t>(0x0095e230) = cvar_get(text(0x0083c21c), text(0x0082e098), 0x0);  // r_verbose
    title_global<uint32_t>(0x0095e72c) = cvar_get(text(0x0083c210), text(0x0082e098), 0x0);  // r_logFile
    title_global<uint32_t>(0x0095e370) = cvar_get(text(0x0083c1f8), text(0x0082e098), 0x200);  // r_profileDrawElements
    title_global<uint32_t>(0x0095e374) = cvar_get(text(0x0083c1ec), text(0x0082e098), 0x200);  // r_showtris
    title_global<uint32_t>(0x0095e614) = cvar_get(text(0x0083c1dc), text(0x0082e098), 0x200);  // r_showtricounts
    title_global<uint32_t>(0x0095e360) = cvar_get(text(0x0083c1c8), text(0x0082e098), 0x200);  // r_showsurfcounts
    title_global<uint32_t>(0x0095e6c4) = cvar_get(text(0x0083c1c0), text(0x0082e098), 0x200);  // r_clear
    title_global<uint32_t>(0x0095e618) = cvar_get(text(0x0083c1b0), text(0x00832c8c), 0x200);  // r_offsetfactor
    title_global<uint32_t>(0x0095e6e4) = cvar_get(text(0x0083c19c), text(0x0083c1ac), 0x200);  // r_offsetunits
    title_global<uint32_t>(0x0095e6cc) = cvar_get(text(0x0083c190), text(0x0082e098), 0x200);  // r_lockpvs
    title_global<uint32_t>(0x0095e6d8) = cvar_get(text(0x0083c184), text(0x0082e098), 0x200);  // r_noportals
    title_global<uint32_t>(0x0095e70c) = cvar_get(text(0x008393c0), text(0x0082e058), 0x0);  // r_cg_skybox
    title_global<uint32_t>(0x0095e38c) = cvar_get(text(0x0083c174), text(0x0082e098), 0x200);  // r_showportals
    title_global<uint32_t>(0x0095e730) = cvar_get(text(0x0083c164), text(0x0082e058), 0x0);  // r_cullBModels
    title_global<uint32_t>(0x0095e234) = cvar_get(text(0x0083c154), text(0x0082e058), 0x0);  // r_cullXModels
    title_global<uint32_t>(0x0095e7f0) = cvar_get(text(0x0083c144), text(0x0082e098), 0x200);  // r_showSkeletons
    title_global<uint32_t>(0x0095e6d4) = cvar_get(text(0x0083c134), text(0x0082e098), 0x200);  // r_singlecell
    title_global<uint32_t>(0x0095e6e0) = cvar_get(text(0x0083c124), text(0x008342cc), 0x1);  // r_portalbevels
    add_command(text(0x0083c118), 0x002373f0);
    add_command(text(0x0083c108), 0x00235c20);
    title_global<uint32_t>(0x0095e700) = cvar_get(text(0x0083c0f8), text(0x0082e058), 0x100);  // r_testshadow
    return title_global<uint32_t>(0x0095e700);
}

PLATFORM_ASM_ENTRY(R_InitDebugMeshes_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(R_InitDebugMeshes) "addl $4, %esp\n"
                   "ret")

void XB_CDECL R_InitDebugMeshes(uint32_t* out) {
    auto* material = static_cast<uint32_t*>(title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(0x18));  // Z_Malloc
    out[3] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(material ? cdDebug_InitMaterial(material) : nullptr));
    static const char* const kMeshes[3] = {"dbgsphr", "dbgcyl", "dbghemi"};
    for (int i = 0; i < 3; ++i) {
        uint8_t hashed[0x20];
        const auto* name =
            title_function<const char* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, kMeshes[i]);  // HashStr_Init
        out[i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(nglFindMesh(name)));
    }
}

uint32_t* XB_REGS1 XB_STDCALL R_RegisterModel(const char* name, uint32_t maps, uint32_t* out, uint32_t zone_set) {
    char text[0x80];
    int32_t length = 0;
    Q_strncpyLen(text, &length, 0x7f, name);
    text[0x7f] = static_cast<char>(length);
    char base[0x80];
    FixedStr_BaseName(text, base, 1);
    std::memcpy(text, base, 0x80);
    // ZoneStringMap_Lookup (EDX zone set; asset maps, out, key, a word it never reads, out
    // index). The original's out slot is its length local with the low byte cleared (0).
    uint32_t model = static_cast<uint32_t>(length) & ~0xffu;
    using Lookup = void XB_REGS2 XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t*, const char*, uint32_t, uint32_t*);
    title_function<Lookup>(0x0002bf80)(0, zone_set, maps, &model, text, model, nullptr);
    if (model) {
        auto** parts = reinterpret_cast<XModelParts**>(uintptr_t{model + 0x24});
        for (int i = 0; i < 5; ++i) {
            if (parts[i]) XModel_ResolveBoneNames(*reinterpret_cast<XModelParts**>(reinterpret_cast<uint8_t*>(parts[i]) + 8));
        }
    }
    *out = model;
    return out;
}

// RE_ParseButtonToken (0x00296350): stdcall, the text before the next "{name}" copied
// to EBX = out; *icon = the translated button name (null when none), *next = past '}'.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL RE_ParseButtonToken_call(uint32_t table, const char* text,
                                                                    const char** next, const char** icon, char* out);
PLATFORM_ASM_ENTRY(RE_ParseButtonToken_call,
                   "pushl %ebx\n"
                   "movl 24(%esp), %ebx\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "movl $0x00296350, %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

PLATFORM_ASM_ENTRY(RE_ParseButtonToken_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(RE_ParseButtonToken) "addl $20, %esp\n"
                   "ret $16")

uint8_t XB_CDECL RE_ParseButtonToken(char* out, uint32_t table, const char* text, const char** next,
                                     const char** icon) {
    const auto strchr = title_function<const char* XB_CDECL(const char*, int)>(0x001ef5e0);
    const auto strncpy = title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80);
    if (text) {
        const char* open = strchr(text, '{');
        if (open) {
            const char* close = strchr(open, '}');
            if (close && static_cast<uint32_t>(close - open) > 1) {
                const uint32_t length = static_cast<uint32_t>(close - open) - 1;
                strncpy(out, open + 1, length);
                out[length] = '\0';
                // Key_TranslateButtonName (name in EAX, table in ECX)
                const char* name =
                    title_function<const char* XB_REGS3(const char*, uint32_t, uint32_t)>(0x00292ee0)(out, 0, table);
                *icon = name;
                if (name) {
                    strncpy(out, text, static_cast<uint32_t>(open - text));
                    out[open - text] = '\0';
                    *next = close + 1;
                    return 1;
                }
            }
        }
    }
    *out = '\0';
    *next = text;
    *icon = nullptr;
    return 0;
}

namespace {

// One run of text at (x, y - its height), the text's height and width out.
void draw_text_run(const uint8_t* font, const char* text, float x, float y, float s, uint32_t color,
                   int32_t* width, int32_t* height) {
    nglGetTextExtent(font, text, width, height, s, s);
    nglListAddText(font, text, x, y - platform_x87_unsigned_to_float(static_cast<uint32_t>(*height)), 0, color, s,
                   s);
}

}  // namespace

void XB_CDECL RE_DrawTextExt(float x, float y, uint32_t font_name, float scale, const float* rgba, const char* text,
                             int32_t, uint32_t, uint32_t, uint32_t, uint32_t) {
    if (!text) return;
    char buffer[0x400];
    const float* colour = rgba;
    if (text[0] == '^' && text[1] - '0' < 10 && text[1] - '0' > 0) {
        const int index = text[1] - '0';
        std::strcpy(buffer, text);
        static const uint32_t kColours[11] = {0x0082eacc, 0x0082eadc, 0x0082eaec, 0x0082eb1c, 0x0082eb0c, 0x0082eb5c,
                                              0x0082eb4c, 0x0082eb9c, 0x0082ebdc, 0x0082eafc, 0x0082ebec};
        colour = &title_global<float>(kColours[index]);
        text = buffer + 2;
        char* end = buffer + std::strlen(buffer) - 2;
        if (end[0] == '^' && end[1] == '7') end[0] = 0;
    }
    uint32_t color = static_cast<uint32_t>(platform_ftol(colour[3] * 255.0f));
    color = (color << 8) | static_cast<uint32_t>(platform_ftol(colour[0] * 255.0f));
    color = (color << 8) | static_cast<uint32_t>(platform_ftol(colour[1] * 255.0f));
    color = (color << 8) | static_cast<uint32_t>(platform_ftol(colour[2] * 255.0f));
    const float size = title_global<float>(0x008db12c) * scale;
    using FindFont = const uint8_t* XB_CDECL(uint32_t, float);
    const uint8_t* font = title_function<FindFont>(title_global<uint32_t>(0x0095e6b4))(font_name, size);
    if (!font) return;
    int32_t width, height;
    const auto strchr = title_function<const char* XB_CDECL(const char*, int)>(0x001ef5e0);
    const char* open = strchr(text, '{');
    if (!open || !strchr(open, '}')) {
        draw_text_run(font, text, x, y, title_global<float>(0x008db12c) * scale, color, &width, &height);
        return;
    }
    // Icons are as high as a space of this font.
    const float s = title_global<float>(0x008db12c) * scale;
    nglGetTextExtent(font, " ", &width, &height, s, s);
    const uint32_t table = title_global<uint32_t>(0x0095f5e0);
    const char* cursor = text;
    const char* icon;
    if (RE_ParseButtonToken_call(table, text, &cursor, &icon, buffer)) {
        do {
            if (buffer[0]) {
                draw_text_run(font, buffer, x, y, title_global<float>(0x008db12c) * scale, color, &width, &height);
                x += platform_x87_unsigned_to_float(static_cast<uint32_t>(width));
            }
            if (icon) {
                using RegisterShader = const uint8_t* XB_REGS2 XB_CDECL(uint32_t, const char*);
                const uint8_t* shader = title_function<RegisterShader>(0x0023ec30)(0, icon);  // RE_RegisterShader
                if (shader) {
                    const float shader_height = platform_x87_unsigned_to_float(field<uint32_t>(shader, 0x14));
                    const float ratio =
                        platform_x87_unsigned_to_float(static_cast<uint32_t>(height)) / shader_height;
                    const float h = shader_height * ratio;
                    const float w = platform_x87_unsigned_to_float(field<uint32_t>(shader, 0x10)) * ratio;
                    R_DrawPic(x, y - h, w, h, const_cast<uint8_t*>(shader));
                    x = platform_x87_mul_add(platform_x87_unsigned_to_float(field<uint32_t>(shader, 0x10)), ratio, x);
                } else {
                    draw_text_run(font, icon, x, y, title_global<float>(0x008db12c) * scale, color, &width,
                                  &height);
                    x += platform_x87_unsigned_to_float(static_cast<uint32_t>(width));
                }
            }
        } while (RE_ParseButtonToken_call(table, cursor, &cursor, &icon, buffer));
    }
    if (cursor) draw_text_run(font, cursor, x, y, title_global<float>(0x008db12c) * scale, color, &width, &height);
}

int32_t XB_REGS1 XB_STDCALL R_FindFont(const char* name, uint32_t strip_extension) {
    // A 31-character name with its length in the last byte, as the font table has them.
    char key[0x20];
    int32_t length;
    Q_strncpyLen(key, &length, 0x1f, name);
    const uint8_t count = static_cast<uint8_t>(length);
    key[0x1f] = static_cast<char>(count);
    if (static_cast<uint8_t>(strip_extension)) {
        for (int32_t dot = 0; dot < count; ++dot) {
            if (key[dot] != '.') continue;
            char stripped[0x20];
            stripped[0x1f] = 0;
            stripped[0] = 0;
            Q_strncpySubLen(stripped, &length, key, 0, dot, 0x1f);
            stripped[0x1f] = static_cast<char>(length);
            std::memcpy(key, stripped, sizeof key);
            break;
        }
    }
    const auto stricmp = title_function<int XB_CDECL(const char*, const char*)>(0x001f1443);  // _stricmp
    for (int32_t i = 0; i < 7; ++i)
        if (stricmp(key, &title_global<char>(0x0095f7e8 + i * 0x20)) == 0) return i;
    return 8;
}

void XB_CDECL RE_DrawText(float x, float y, uint32_t font_name, float scale, const float* rgba, const char* text,
                          uint32_t a6, uint32_t a7, uint32_t a8) {
    RE_DrawTextExt(x, y, font_name, scale, rgba, text, -1, 0, a6, a7, a8);
}

int32_t XB_CDECL RE_TextWidth(const char* text, uint32_t font_name, float scale) {
    using FindFont = const uint8_t* XB_CDECL(uint32_t, float);
    const uint8_t* font =
        title_function<FindFont>(title_global<uint32_t>(0x0095e6b4))(font_name, title_global<float>(0x008db12c) * scale);
    int32_t width, height;
    const auto strchr = title_function<const char* XB_CDECL(const char*, int)>(0x001ef5e0);
    const char* open = strchr(text, '{');
    if (!open || !strchr(open, '}')) {
        const float s = title_global<float>(0x008db12c) * scale;
        nglGetTextSizef(font, &width, &height, s, s, text);
        return width;
    }
    int32_t total = 0;
    float s = title_global<float>(0x008db12c) * scale;
    nglGetTextExtent(font, " ", &width, &height, s, s);
    const uint32_t table = title_global<uint32_t>(0x0095f5e0);
    const char* cursor = text;
    const char* icon;
    char buffer[0x400];
    if (RE_ParseButtonToken_call(table, text, &cursor, &icon, buffer)) {
        do {
            if (buffer[0]) {
                s = title_global<float>(0x008db12c) * scale;
                nglGetTextExtent(font, buffer, &width, &height, s, s);
                total += width;
            }
            if (icon) {
                using RegisterShader = const uint8_t* XB_REGS2 XB_CDECL(uint32_t, const char*);
                const uint8_t* shader = title_function<RegisterShader>(0x0023ec30)(0, icon);  // RE_RegisterShader
                if (shader) {
                    const float ratio = platform_x87_unsigned_to_float(static_cast<uint32_t>(height)) /
                                        platform_x87_unsigned_to_float(field<uint32_t>(shader, 0x14));
                    total = platform_ftol(platform_x87_mul_add(platform_x87_unsigned_to_float(field<uint32_t>(shader, 0x10)),
                                                               ratio,
                                                               platform_x87_unsigned_to_float(static_cast<uint32_t>(total))));
                } else {
                    s = title_global<float>(0x008db12c) * scale;
                    nglGetTextExtent(font, icon, &width, &height, s, s);
                    total += width;
                }
            }
        } while (RE_ParseButtonToken_call(table, cursor, &cursor, &icon, buffer));
    }
    if (!cursor) return total;
    s = title_global<float>(0x008db12c) * scale;
    nglGetTextExtent(font, cursor, &width, &height, s, s);
    return total + width;
}

void XB_CDECL RE_DrawTextWithIcons(float x, float y, uint32_t font_name, float scale, const float* rgba,
                                   const uint16_t* text, uint32_t arg7, int32_t count, uint32_t arg9) {
    float colour[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    float advance = 0.0f;
    while (text) {
        char* line = nullptr;
        const int32_t icon = R_LayoutTextWithIcons(&line, colour, text, &count, &text);
        if (line && *line) {
            RE_DrawTextExt(advance + x, y, font_name, scale, colour, line, -1, 0, arg7, 0, arg9);
            if (icon) advance = static_cast<float>(RE_TextWidth(line, font_name, scale)) + advance;
        }
        if (!icon) continue;
        float width, height;
        uint32_t shader;
        int32_t consumed = 0;  // the original steps the text pointer itself
        R_ParseTextIcon(scale, text, &consumed, &count, &width, &height, &shader, colour);
        text += consumed / 2;
        RE_SetColor(colour);
        // Centred on the line (38.4 * scale high), through the client's 640x480 adjust.
        float pic_x = advance + x;
        float pic_y = y - (scale * title_global<float>(0x0083f038) + height) * title_global<float>(0x0083eb24);
        float pic_w = width;
        float pic_h = height;
        title_function<void XB_CDECL(float*, float*, float*, float*)>(title_global<uint32_t>(0x0095e6b0))(
            &pic_x, &pic_y, &pic_w, &pic_h);
        RE_StretchPic(pic_x, pic_y, pic_w, pic_h, 0.0f, 0.0f, 1.0f, 1.0f, reinterpret_cast<void*>(uintptr_t{shader}));
        advance = width + advance;
    }
}

int32_t XB_CDECL RE_TextWidthWithIcons(const uint16_t* text, uint32_t font_name, float scale, float char_width,
                                       int32_t count) {
    float width = 0.0f;
    while (text) {
        char* line = nullptr;
        const int32_t icon = R_LayoutTextWithIcons(&line, nullptr, text, &count, &text);
        if (line && *line) {
            if (char_width == 0.0f)
                width = static_cast<float>(RE_TextWidth(line, font_name, scale)) + width;
            else
                width = static_cast<float>(Q_PrintStrlen(0, line)) * char_width + width;
        }
        if (icon) {
            // The original hands R_ParseTextIcon the text pointer itself as the byte count it
            // steps (2 per code).
            float icon_width, icon_height;
            uint32_t shader;
            int32_t consumed = 0;
            R_ParseTextIcon(scale, text, &consumed, &count, &icon_width, &icon_height, &shader, nullptr);
            text += consumed / 2;
            width = icon_width + width;
        }
    }
    return static_cast<int32_t>(width);
}

namespace {

// A packed alpha (top byte) from a 0..1 float: truncated times 128, at most 0xff.
uint32_t fog_pass_alpha(float alpha) {
    int32_t a = static_cast<int32_t>(alpha * title_global<float>(0x0083ebe8));  // 128
    if (a > 0xff) a = 0xff;
    return static_cast<uint32_t>(a) << 24;
}

// Vertex stream 0 from the vertices themselves, then 4 vertices.
void fog_pass_draw(uint32_t primitive, const void* vertices, uint32_t stride) {
    title_global<uint32_t>(0x00903c88) = 0;
    title_global<uint32_t>(0x00903c8c) = 0;
    title_global<uint32_t>(0x00903c90) = 0;
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
    title_function<void XB_STDCALL(uint32_t, uint32_t, const void*, uint32_t)>(0x003e9710)(
        primitive, 4, vertices, stride);  // DrawVerticesUP
}

// {x, y, z, colour, u, v}; texture coordinates in texels.
struct FogQuadVertex {
    float x, y, z;
    uint32_t color;
    float u, v;
};

// {x, y, z, colour}, then u, v for stages 0..3.
struct FogPassVertex {
    float x, y, z;
    uint32_t color;
    float uv[4][2];
};

}  // namespace

void XB_CDECL R_DrawFogPass() {
    if (!title_global<uint32_t>(0x00897950) || !title_global<int32_t>(0x00897960)) return;
    nglAllocFogTextures();
    NglTexture** fog = &title_global<NglTexture*>(0x00901e7c);
    fog[0] = title_global<NglTexture*>(0x00902e64);
    fog[1] = title_global<NglTexture*>(0x00902e68);
    const float size = platform_x87_unsigned_to_float(fog[0]->width);
    using SetState = void XB_STDCALL(uint32_t);
    uint32_t& fill_mode = title_global<uint32_t>(0x00902fb0);
    if (fill_mode != 0x1b02) {
        title_function<SetState>(0x003e88a0)(0x1b02);  // SetRenderState_FillMode: solid
        fill_mode = 0x1b02;
    }
    uint32_t& cull_mode = title_global<uint32_t>(0x00902fac);
    if (cull_mode != 0) {
        title_function<SetState>(0x003e80f0)(0);  // SetRenderState_CullMode
        cull_mode = 0;
    }
    NglTexture* back_buffer = nglGetBackBufferTexture();
    // D3D's texture stage shadows (title memory): stages 0..3 clamp in u and v.
    title_global<uint32_t>(0x003f8320) |= 0xf;
    for (uint32_t stage = 0; stage < 4; ++stage) {
        title_global<uint32_t>(0x003f5c68 + stage * 0x80) = 3;
        title_global<uint32_t>(0x003f5c6c + stage * 0x80) = 3;
    }
    uint8_t* const blend = &title_global<uint8_t>(0x00902f08);
    nglSetBlendMode(blend, 0, 0, 0);
    nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00903c9c));
    nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00901e70));
    nglSetTexture(0, back_buffer, 2, 3);
    nglSetRenderTargetCached(fog[0], 0, nullptr, 6);
    float width = platform_x87_unsigned_to_float(back_buffer->width);
    float height = platform_x87_unsigned_to_float(back_buffer->height);
    // The back buffer into the first fog texture (a quad list).
    const FogQuadVertex copy[4] = {{0.0f, 0.0f, 0.0f, 0xffffffff, 0.0f, 0.0f},
                                   {size, 0.0f, 0.0f, 0xffffffff, width, 0.0f},
                                   {size, size, 0.0f, 0xffffffff, width, height},
                                   {0.0f, size, 0.0f, 0xffffffff, 0.0f, height}};
    fog_pass_draw(8, copy, sizeof copy[0]);
    nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00901e84));
    nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00901e74));

    // Each pass samples the last result at four offsets (a strip over the texture).
    FogPassVertex quad[4] = {};
    quad[1].x = size;
    quad[2].y = size;
    quad[3].x = size;
    quad[3].y = size;
    NglTexture* last = nullptr;
    for (int32_t pass = 0; pass < title_global<int32_t>(0x00897960); ++pass) {
        NglTexture* target = fog[(pass + 1) & 1];
        last = target;
        nglSetRenderTargetCached(target, 0, nullptr, 6);
        const float d = static_cast<float>(pass + 1) * title_global<float>(0x00897958);
        const float n = 0.0f - d;
        const float dx[4] = {n, n, d, d};
        const float dy[4] = {n, d, n, d};
        const uint32_t color = fog_pass_alpha(title_global<float>(0x0089795c));
        for (uint32_t stage = 0; stage < 4; ++stage) {
            nglSetTexture(stage, fog[pass & 1], 2, 3);
            for (FogPassVertex& v : quad) v.color = color;
            quad[0].uv[stage][0] = dx[stage];
            quad[0].uv[stage][1] = dy[stage];
            quad[1].uv[stage][0] = dx[stage] + size;
            quad[1].uv[stage][1] = dy[stage];
            quad[2].uv[stage][0] = dx[stage];
            quad[2].uv[stage][1] = dy[stage] + size;
            quad[3].uv[stage][0] = dx[stage] + size;
            quad[3].uv[stage][1] = dy[stage] + size;
        }
        fog_pass_draw(6, quad, sizeof quad[0]);
    }

    // The result over the back buffer.
    nglSetBlendMode(blend, 6, 0xff, 0);
    nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00903c9c));
    nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00901e78));
    nglSetTexture(0, last, 2, 3);
    nglSetRenderTargetCached(back_buffer, 0, nullptr, 6);
    width = platform_x87_unsigned_to_float(back_buffer->width);
    height = platform_x87_unsigned_to_float(back_buffer->height);
    const uint32_t color = fog_pass_alpha(title_global<float>(0x00897954));
    const FogQuadVertex result[4] = {{0.0f, 0.0f, 0.0f, color, 0.0f, 0.0f},
                                     {width, 0.0f, 0.0f, color, size, 0.0f},
                                     {width, height, 0.0f, color, size, size},
                                     {0.0f, height, 0.0f, color, 0.0f, size}};
    fog_pass_draw(8, result, sizeof result[0]);
}

void XB_CDECL R_AddFogPass() {
    static const uint32_t kData[2] = {1, 0};  // {1, 0.0f}
    nglPushScene(0);
    nglSetClearFlags(0);
    nglSetSceneDepthTest(0);
    nglSetSceneDepthEnable(0);
    nglListAddCustomNode(0x00045850, 0, kData);  // R_DrawFogPass
    nglPopScene();
}

namespace {

// Stream_FindMeshFile (0x00290400): hashed name in EBX, then (streams, zone, 0, a flag
// it never reads) on the stack (stdcall).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Stream_FindMeshFile_call(const uint8_t* hashed, uint32_t streams,
                                                                       uint32_t zone, uint32_t unused);
PLATFORM_ASM_ENTRY(Stream_FindMeshFile_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "pushl 20(%esp)\n"
                   "pushl $0\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "movl $0x00290400, %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

}  // namespace

void XB_CDECL R_LoadCellMeshes(uint32_t cell_index, uint32_t release) {
    const uint8_t* cm = title_global<const uint8_t*>(0x0095f6b8);
    if (!cm) return;
    uint8_t* cell = field<uint8_t*>(cm, 0x1c) + (cell_index < field<uint32_t>(cm, 0x18) ? cell_index : 0) * 0x50;
    uint32_t& file_slot = *reinterpret_cast<uint32_t*>(cell + 0x3c);
    const auto mesh_slot = [cell](uint32_t i) -> uint32_t& {
        return field<uint32_t*>(cell, 0x44)[i < field<uint32_t>(cell, 0x40) ? i : 0];
    };
    if (static_cast<uint8_t>(release)) {
        file_slot = 0;
        for (uint32_t i = 0; i < field<uint32_t>(cell, 0x40); ++i) mesh_slot(i) = 0;
        return;
    }
    const uint8_t* world = title_global<const uint8_t*>(0x0095f640);
    const uint8_t* level = field<const uint8_t*>(world, 4 + field<uint32_t>(world, 0x1cc) * 4);
    const uint8_t* entry =
        field<const uint8_t**>(level, 0x20)[cell_index < field<uint32_t>(level, 0x1c) ? cell_index : 0];
    const uint32_t zone = field<uint32_t>(field<const uint8_t*>(field<const uint8_t*>(entry, 0x2c), 0x24), 0x28);
    const auto sprintf = title_function<int XB_CDECL(char*, const char*, ...)>(0x001ee901);
    const char* map = &title_global<char>(0x00988c58);
    char name[0xf0];
    uint8_t hashed[0x20];
    uint8_t* files[32];
    int32_t count = 0;
    sprintf(name, "%s_%03d", map, cell_index);
    title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, name);  // HashStr_Init
    uint8_t* file = Stream_FindMeshFile_call(hashed, title_global<uint32_t>(0x0095f630), zone, 0);
    file_slot = reinterpret_cast<uintptr_t>(file);
    if (file) {
        files[0] = file;
        count = 1;
    } else {
        for (int32_t split = 0; split < 0x20; ++split) {
            sprintf(name, "%s_%03d_SPLIT%d", map, cell_index, split);
            title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, name);  // HashStr_Init
            file = Stream_FindMeshFile_call(hashed, title_global<uint32_t>(0x0095f630), zone, 0);
            file_slot = reinterpret_cast<uintptr_t>(file);
            if (!file) break;
            if (count < 0x20) files[count++] = file;
        }
        file_slot = 1;
        if (!count) return;
    }
    uint32_t slot = 0;
    for (int32_t i = 0; i < count; ++i)
        for (uint8_t* mesh = nglMeshFileFirstMesh(files[i]); mesh; mesh = nglMeshNext(mesh))
            mesh_slot(slot++) = reinterpret_cast<uintptr_t>(mesh);
}

namespace {

// StringMap_Find (0x000249a0): map in EBX, a pointer to the key's pointer on the stack
// (stdcall); a pointer to the entry's value or null.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL StringMap_Find_call(const uint8_t* map, const char* const* key);
PLATFORM_ASM_ENTRY(StringMap_Find_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "pushl 12(%esp)\n"
                   "movl $0x000249a0, %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

}  // namespace

uint8_t* XB_REGS2 XB_CDECL RE_RegisterShader(uint32_t, const char* name) {
    char buffer[0x40];
    char* out = buffer;
    for (const char* in = name; *in && *in != '.'; ++in) *out++ = *in;
    *out = 0;
    const uint8_t* zones = title_global<const uint8_t*>(0x0095f638);
    const uint8_t* table = field<const uint8_t*>(zones, 0x18);
    const uint8_t* zone = nullptr;
    uint32_t bank = 0;
    bool have_bank = false;
    if (table) {
        const char* key = "FrontEnd";
        const uint32_t* found = StringMap_Find_call(table + 8, &key);
        if (found) {
            uint32_t index = *found;
            if (index >= field<uint32_t>(table, 0x10)) index = 0;
            zone = field<const uint8_t**>(table, 0x14)[index];
        }
        if (zone && field<uint32_t>(zone, 0x28) != 0xffffffff) {
            bank = field<uint32_t>(zone, 0x28);
            have_bank = true;
        }
    }
    if (!have_bank) bank = field<uint32_t>(zones, 0x2c);
    const char* base = buffer;
    for (uint32_t i = 0; i < std::strlen(buffer); ++i)
        if (buffer[i] == '@' || buffer[i] == '$') base = &buffer[i + 1];
    const char* file = base;
    for (const char* p = base; *p; ++p)
        if (*p == '/') file = p + 1;
    uint8_t hashed[0x20];
    using FindTexture = uint8_t* XB_STDCALL(uint32_t, uint32_t, const uint8_t*, uint32_t, uint32_t);
    if (zone) {
        title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, file);  // HashStr_Init
        uint8_t* texture = title_function<FindTexture>(0x00290620)(title_global<uint32_t>(0x0095f630), bank, hashed,
                                                                   0, 0);  // Stream_FindTexture
        if (texture) return texture;
    }
    title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, file);  // HashStr_Init
    uint8_t* texture = title_function<FindTexture>(0x00290620)(title_global<uint32_t>(0x0095f630),
                                                               field<uint32_t>(zones, 0x34), hashed, 0, 0);
    return texture ? texture : title_global<uint8_t*>(0x00902700);
}

}  // namespace game
