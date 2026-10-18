#include "ngl.h"

#include <cstring>

#include "title_data.h"

namespace game {

namespace {

NglScene*& current_scene() { return title_global<NglScene*>(0x0090259c); }
NglLightSet*& current_light_set() { return title_global<NglLightSet*>(0x00902e44); }

// The frame's scene memory (16-byte aligned blocks, bumped; no end check here).
template <typename T>
T* alloc_scene_memory(uint32_t size) {
    uintptr_t& next = title_global<uintptr_t>(0x009025a8);
    const uintptr_t block = (next + 15) & ~uintptr_t{15};
    next = block + size;
    return reinterpret_cast<T*>(block);
}

void* alloc_frame(uintptr_t& next, uint32_t size, uint32_t align) {
    const uintptr_t block = (next + align - 1) & ~uintptr_t{align - 1};
    next = block + size;
    return reinterpret_cast<void*>(block);
}

const NglTexture* resolve(const NglTexture* texture) {
    return (texture->flags & 0xff) == 0x10 ? texture->target : texture;
}

}  // namespace

void XB_CDECL nglInitQuad(NglQuad* quad) {
    std::memset(quad, 0, sizeof(*quad));
    for (NglQuadVertex& vertex : quad->verts) vertex.color = 0xffffffff;
    quad->verts[0].u = 0.0f;
    quad->verts[1].u = 1.0f;
    quad->verts[2].u = 0.0f;
    quad->verts[3].u = 1.0f;
    quad->verts[0].v = 0.0f;
    quad->verts[1].v = 0.0f;
    quad->verts[2].v = 1.0f;
    quad->verts[3].v = 1.0f;
    quad->flags = 0xc2;
    quad->blend_mode = 2;
}

void XB_CDECL nglSetQuadTexture(NglQuad* quad, NglTexture* texture) { quad->texture = texture; }

void XB_CDECL nglSetQuadFlags(NglQuad* quad, uint32_t flags) { quad->flags = flags; }

void XB_CDECL nglSetQuadBlend(NglQuad* quad, uint32_t mode, uint32_t arg) {
    quad->blend_mode = mode;
    quad->blend_arg = arg;
}

void XB_CDECL nglSetQuadUV(NglQuad* quad, float u0, float v0, float u1, float v1) {
    quad->verts[0].u = u0;
    quad->verts[2].u = u0;
    quad->verts[0].v = v0;
    quad->verts[1].u = u1;
    quad->verts[1].v = v0;
    quad->verts[2].v = v1;
    quad->verts[3].u = u1;
    quad->verts[3].v = v1;
}

void XB_CDECL nglSetQuadColor(NglQuad* quad, uint32_t color) {
    for (NglQuadVertex& vertex : quad->verts) vertex.color = color;
}

void XB_CDECL nglSetQuadRect(NglQuad* quad, float x0, float y0, float x1, float y1) {
    quad->verts[0].x = x0;
    quad->verts[2].x = x0;
    quad->verts[0].y = y0;
    quad->verts[1].x = x1;
    quad->verts[1].y = y0;
    quad->verts[2].y = y1;
    quad->verts[3].x = x1;
    quad->verts[3].y = y1;
}

void XB_CDECL nglSetQuadZ(NglQuad* quad, float z) { quad->z = z; }

void XB_CDECL nglSetQuadVertexPos(NglQuad* quad, int32_t vertex, float x, float y) {
    quad->verts[vertex].x = x;
    quad->verts[vertex].y = y;
}

void XB_CDECL nglSetQuadVertexUV(NglQuad* quad, int32_t vertex, float u, float v) {
    quad->verts[vertex].u = u;
    quad->verts[vertex].v = v;
}

void XB_CDECL nglSetQuadVertexColor(NglQuad* quad, int32_t vertex, uint32_t color) {
    quad->verts[vertex].color = color;
}

namespace {

// The original takes sine and cosine on the x87 and does the rest in SSE.
void rotate_pairs(float* pair, float cx, float cy, float angle) {
    const float s = platform_x87_sin(angle);
    const float c = platform_x87_cos(angle);
    for (int i = 0; i < 4; ++i, pair += sizeof(NglQuadVertex) / sizeof(float)) {
        const float dx = pair[0] - cx;
        const float dy = pair[1] - cy;
        pair[0] = (c * dx - s * dy) + cx;
        pair[1] = (s * dx + c * dy) + cy;
    }
}

}  // namespace

void XB_CDECL nglRotateQuadVerts(NglQuad* quad, float cx, float cy, float angle) {
    rotate_pairs(&quad->verts[0].x, cx, cy, angle);
}

void XB_CDECL nglScaleQuadVerts(NglQuad* quad, float cx, float cy, float sx, float sy) {
    for (NglQuadVertex& vertex : quad->verts) {
        vertex.x = (vertex.x - cx) * sx + cx;
        vertex.y = (vertex.y - cy) * sy + cy;
    }
}

void XB_CDECL nglRotateQuad(NglQuad* quad, float cu, float cv, float angle) {
    rotate_pairs(&quad->verts[0].u, cu, cv, angle);
}

void XB_CDECL nglSetClearFlags(uint32_t flags) { current_scene()->clear_flags = flags; }

void XB_CDECL nglSetClearDepth(float depth) { current_scene()->clear_depth = depth; }

void XB_CDECL nglSetColorWriteMask(uint32_t mask) { current_scene()->color_write_mask = mask; }

void XB_CDECL nglSetFog(float start, float value1, float value2, float value3) {
    NglScene* scene = current_scene();
    // A NaN start gives the minimum, as the original's comparison does.
    scene->fog[0] = start > scene->near_z ? start : scene->near_z;
    scene->fog[1] = value1;
    scene->fog[2] = value2;
    scene->fog[3] = value3;
}

NglScene* XB_CDECL nglPopScene() {
    NglScene* scene = current_scene();
    current_scene() = scene->parent;
    return scene;
}

NglScene* XB_CDECL nglSetCurrentScene(NglScene* scene) {
    NglScene* previous = current_scene();
    current_scene() = scene;
    return previous;
}

void XB_CDECL nglSetClearColor(float r, float g, float b, float a) {
    float* color = current_scene()->clear_color;
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;
}

void XB_CDECL nglSetFogColor(float r, float g, float b) {
    float* color = current_scene()->fog_color;
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = 1.0f;
}

namespace {

void set_rows(float* out, const float (&rows)[16]) {
    for (int i = 0; i < 16; ++i) out[i] = rows[i];
}

}  // namespace

void XB_CDECL nglMatrixPerspective(float* out, float x_scale, float y_scale, float near_z, float far_z) {
    const float q = far_z / (far_z - near_z);
    // 0 - x, not -x: a zero near plane gives +0 as in the original.
    set_rows(out, {x_scale, 0, 0, 0,
                   0, y_scale, 0, 0,
                   0, 0, q, 1,
                   0, 0, 0.0f - q * near_z, 0});
}

void XB_CDECL nglMatrixOrtho(float* out, float x_scale, float y_scale, float near_z, float far_z) {
    const float q = 1.0f / (far_z - near_z);
    set_rows(out, {x_scale, 0, 0, 0,
                   0, y_scale, 0, 0,
                   0, 0, q, 0,
                   0, 0, 0.0f - q * near_z, 1});
}

void XB_CDECL nglMatrixScreenToClip(float* out) {
    const NglRenderTarget* target = current_scene()->render_target;
    float half_width = 320.0f;
    float half_height = 240.0f;
    if (!(target->flags & 4)) {
        half_width = static_cast<float>(target->width) * 0.5f;
        half_height = static_cast<float>(target->height) * 0.5f;
    }
    set_rows(out, {1.0f / half_width, 0, 0, 0,
                   0, 1.0f / half_height, 0, 0,
                   0, 0, 1, 0,
                   -1, -1, 0, 1});
}

void XB_CDECL nglMatrixViewport(float* out, float x0, float y0, float x1, float y1) {
    const float x_scale = (x1 - x0) * 0.5f;
    const float centre_x = (x0 + x1) * 0.5f;
    // The original puts x_scale and centre_x in the w column too (its rows 0 and 3
    // end in them instead of 0 and 1); kept.
    set_rows(out, {x_scale, 0, 0, x_scale,
                   0, (y0 - y1) * 0.5f, 0, 0,
                   0, 0, 1, 0,
                   centre_x, (y0 + y1) * 0.5f, 0, centre_x});
}

float XB_CDECL nglDepthToZValue(float depth) {
    const NglScene* scene = current_scene();
    if (scene->near_z > depth) depth = scene->near_z;
    if (depth > scene->far_z) depth = scene->far_z;
    // Row vector (0, 0, depth, 1) times the projection, divided by w.
    const float* row2 = &scene->projection[8];
    const float* row3 = &scene->projection[12];
    const float z = (row2[2] * depth + row3[2]) / (row2[3] * depth + row3[3]);
    // Comparisons as in the original: a NaN comes back unchanged.
    if (0.0f > z) return 0.0f;
    if (z > 16777215.0f) return 16777215.0f;
    return z;
}

void XB_CDECL nglSceneAddRenderTarget(NglTexture* texture) {
    if (!(texture->flags & 0x400)) return;
    NglTexture* target = (texture->flags & 0xff) == 0x10 ? texture->target : texture;
    auto* link = alloc_scene_memory<NglRenderTargetLink>(sizeof(NglRenderTargetLink));
    if (!link) return;
    link->texture = target;
    link->next = current_scene()->render_targets;
    current_scene()->render_targets = link;
}

void XB_CDECL nglSetSceneCallback(uint32_t which, void* function, void* arg) {
    if (which > 2) return;
    current_scene()->callbacks[which].function = function;
    current_scene()->callbacks[which].arg = arg;
}

void XB_CDECL nglSetTextureManager(void* manager) { title_global<void*>(0x009025f0) = manager; }

void* XB_CDECL nglGetTextureManager() { return title_global<void*>(0x009025f0); }

void XB_CDECL nglAddRef(uint32_t* refs) { ++*refs; }

bool XB_CDECL nglTextureIsStale(const NglTexture* texture) {
    // Wraps around like the original's add.
    const auto next = static_cast<int32_t>(static_cast<uint32_t>(resolve(texture)->last_used_frame) + 1);
    return next < title_global<int32_t>(0x009028e0);
}

uint32_t XB_CDECL nglIsPAL50() { return title_global<uint8_t>(0x009028bc) == 1 ? 1 : 0; }

void XB_CDECL nglSetVBlankMode(uint32_t mode) {
    if (mode > 4) return;
    title_global<uint32_t>(0x008a2cb0) = mode >= 3 ? mode - 2 : mode;  // vertical blanks
    title_global<uint32_t>(0x00902878) = mode >= 3 ? 1 : 0;
}

void* XB_CDECL nglAllocFrameGpu(uint32_t size, uint32_t align) {
    return alloc_frame(title_global<uintptr_t>(0x00903c50), size, align);
}

void* XB_CDECL nglAllocFrameScratch(uint32_t size, uint32_t align) {
    return alloc_frame(title_global<uintptr_t>(0x00903c34), size, align);
}

void XB_CDECL nglSetSceneLightSet(NglLightSet* set) { current_scene()->light_set = set; }

NglLight* XB_CDECL nglLightSetAddLight(uint32_t type, float* data, uint32_t flags) {
    NglLight* light = alloc_scene_memory<NglLight>(sizeof(NglLight));
    if (!light) return nullptr;
    light->data = data;
    light->type = type;
    light->flags = flags;
    for (int list = 0; list < 8; ++list) {
        if (!(flags & (0x01000000u << list))) continue;
        light->next[list] = current_light_set()->lists[list];
        current_light_set()->lists[list] = light;
    }
    return light;
}

NglLightSet* XB_CDECL nglCreateLightSet() {
    auto* set = alloc_scene_memory<NglLightSet>(sizeof(NglLightSet));
    if (!set) return nullptr;
    std::memset(set, 0, sizeof(*set));
    for (void*& entry : set->unknown30) entry = &set->unknown30;
    // An empty list ends at the set itself.
    for (NglLight*& list : set->lists) list = reinterpret_cast<NglLight*>(set);
    const float* white = &title_global<const float>(0x007beae0);
    for (int i = 0; i < 4; ++i) set->ambient[i] = white[i];
    current_light_set() = set;
    return set;
}

float* XB_CDECL nglLightVectorWorld(float* out, const NglLight* light, const float* position) {
    switch (light->type) {
    case 0:
        return light->data;
    case 1: {
        const float* direction = light->data;
        for (int i = 0; i < 4; ++i) out[i] = position[i] - direction[i] * 1e12f;
        out[9] = 1e24f;
        out[8] = 1e24f;
        for (int i = 0; i < 4; ++i) out[4 + i] = direction[4 + i];
        return out;
    }
    default:
        return nullptr;
    }
}

void XB_CDECL nglSetAmbientColor(float r, float g, float b) {
    // max then min in the order of the original's maxps/minps (a NaN becomes 0 by the
    // max, which returns its second operand then).
    const float color[4] = {r, g, b, 1.0f};
    float* ambient = current_light_set()->ambient;
    for (int i = 0; i < 4; ++i) {
        float c = color[i] > 0.0f ? color[i] : 0.0f;
        ambient[i] = c < 1.0f ? c : 1.0f;
    }
}

void XB_THISCALL nglPaletteSetEntry(uint32_t** self, uint32_t index, uint32_t color) {
    (*self)[index] = color;
}

int32_t XB_CDECL nglGetScreenWidth() { return 640; }

int32_t XB_CDECL nglGetScreenHeight() { return 480; }

void XB_CDECL nglColorToVec4(float* out, uint32_t color) {
    const float scale = 1.0f / 255.0f;
    out[0] = static_cast<float>((color >> 16) & 0xff) * scale;
    out[1] = static_cast<float>((color >> 8) & 0xff) * scale;
    out[2] = static_cast<float>(color & 0xff) * scale;
    out[3] = static_cast<float>(color >> 24) * scale;
}

void XB_CDECL nglSetRenderTarget(NglRenderTarget* target) {
    current_scene()->render_target = target;
    current_scene()->unknown008 = 6;
}

void XB_CDECL nglSetSceneFlags(uint32_t flags) { current_scene()->flags = flags; }

void XB_CDECL nglMeshBuilder_SetBounds(const float* center, float radius) {
    NglMeshBuilder*& builder = title_global<NglMeshBuilder*>(0x00902920);
    for (int i = 0; i < 4; ++i) builder->center[i] = center[i];
    builder->radius = radius;
    for (uint32_t i = 0; i < builder->section_count; ++i) {
        for (int j = 0; j < 4; ++j) builder->sections[i].section->center[j] = center[j];
        builder->sections[i].section->radius = radius;
    }
}

void XB_CDECL nglSwapRedBlue(uint8_t* pixels, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t red = pixels[i * 4];
        pixels[i * 4] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = red;
    }
}

}  // namespace game
