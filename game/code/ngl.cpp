#include "ngl.h"

#include <cstdarg>
#include <cmath>
#include <cstring>

#include "lanes.h"
#include "qcommon.h"
#include "shaders.h"
#include "title_data.h"

namespace game {

namespace {

NglScene*& current_scene() { return title_global<NglScene*>(0x0090259c); }
NglLightSet*& current_light_set() { return title_global<NglLightSet*>(0x00902e44); }
NglScene*& root_scene() { return title_global<NglScene*>(0x009025a0); }
uint32_t scene_slot_count() { return title_global<uint32_t>(0x00902900); }

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

NglScene* XB_CDECL nglInitScene(NglScene* scene, int32_t mode) {
    NglScene* previous = current_scene();
    current_scene() = scene;
    if (mode == 0) {
        std::memset(scene, 0, sizeof(*scene));
        title_function<void XB_CDECL()>(0x00062a70)();  // nglSetDefaultSceneParams
    } else if (mode == 1) {
        std::memcpy(scene, previous, sizeof(*scene));
    } else if (mode == 2) {
        std::memcpy(scene, root_scene(), sizeof(*scene));
    }
    const uint32_t slot_bytes = scene_slot_count() * 4 + 8;
    auto* slots = static_cast<uint32_t*>(alloc_frame(title_global<uintptr_t>(0x009025a8), slot_bytes, 8));
    slots[0] = 0;
    slots[1] = 0;
    scene->slots = slots;
    if (mode == 1) {
        std::memcpy(scene->slots, previous->slots, slot_bytes);
    } else if (mode == 2) {
        std::memcpy(scene->slots, root_scene()->slots, slot_bytes);
    }
    scene->next_sibling = nullptr;
    scene->first_child = nullptr;
    scene->last_child = nullptr;
    scene->render_targets = nullptr;
    std::memset(scene->callbacks, 0, sizeof(scene->callbacks));
    uint8_t* raw = reinterpret_cast<uint8_t*>(scene);
    std::memset(raw + 0x304, 0, 0x10);  // the draw lists and their counts
    scene->parent = previous;
    return previous;
}

NglScene* XB_CDECL nglPushScene(int32_t mode) {
    uintptr_t& arena = title_global<uintptr_t>(0x009025a8);
    auto* scene = static_cast<NglScene*>(alloc_frame(arena, sizeof(NglScene), 16));
    if (!scene) return nullptr;
    auto* slots = static_cast<uint32_t*>(alloc_frame(arena, scene_slot_count() * 4 + 8, 8));
    scene->slots = slots;
    slots[0] = 0;
    slots[1] = 0;
    NglScene* parent = current_scene();
    if (parent) {
        if (parent->last_child) {
            parent->last_child->next_sibling = scene;
        } else {
            parent->first_child = scene;
        }
        current_scene()->last_child = scene;
    } else {
        root_scene() = scene;
    }
    nglInitScene(scene, mode);
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

float* XB_CDECL nglMatrixOrthoOffCenter(float* out, float left, float bottom, float right, float top) {
    const float height = top - bottom;
    const float width = right - left;
    set_rows(out, {2.0f / width, 0, 0, 0,
                   0, 2.0f / height, 0, 0,
                   0, 0, 1, 0,
                   0.0f - (left + right) / width, 0.0f - (bottom + top) / height, 0, 1});
    return out;
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

namespace {

// a * m as row vector times matrix, four packed lanes, added in the title's order.
Lanes row_times(const Lanes& a, const float* m) {
    Lanes r = add(mul(lanes(m), a.v[0]), mul(lanes(m + 4), a.v[1]));
    r = add(r, mul(lanes(m + 8), a.v[2]));
    return add(r, mul(lanes(m + 12), a.v[3]));
}

void store(float* out, const Lanes& a) { std::memcpy(out, a.v, 16); }

}  // namespace

void XB_CDECL nglUpdateScene(uint8_t mark_stale) {
    if (mark_stale) {
        current_scene()->stale = 1;
        return;
    }
    NglScene* scene = current_scene();
    if (!scene->stale) return;
    scene->stale = 0;

    // The clamped viewport mapped into the viewport rectangle's -1..1.
    float x0 = scene->viewport_rect[0];
    float y0 = scene->viewport_rect[1];
    float x1 = scene->viewport_rect[2];
    float y1 = scene->viewport_rect[3];
    const float* clamped = scene->viewport_clamped;
    float inv = 1.0f / (x1 - x0);
    const float sum_x = x1 + x0;
    float offset = 0.0f - sum_x * inv;
    float scale = inv * 2.0f;
    const float ndc2 = clamped[2] * scale + offset;
    const float ndc0 = clamped[0] * scale + offset;
    inv = 1.0f / (y1 - y0);
    const float sum_y = y1 + y0;
    offset = 0.0f - sum_y * inv;
    scale = inv * 2.0f;
    const float ndc1 = clamped[1] * scale + offset;
    const float ndc3 = clamped[3] * scale + offset;
    scene->viewport_ndc[0] = ndc0;
    scene->viewport_ndc[2] = ndc2;
    scene->viewport_ndc[1] = ndc1;
    scene->viewport_ndc[3] = ndc3;

    alignas(16) float m[16];
    Lanes planes[6];  // view space: left, right, two in y, near, far
    if (scene->projection_kind == 1) {
        // f = cos / sin of half the field of view (degrees), from the polynomial.
        const float angle = scene->fov * from_bits(0x3c0efa35);  // pi / 360
        const float f = scene_sine_poly(angle + from_bits(0x4096cbe2)) / scene_sine_poly(angle + 0.0f);
        const float af = scene->aspect * f;
        const float ax = abs_bits(ndc0) > abs_bits(ndc2) ? abs_bits(ndc0) : abs_bits(ndc2);
        const float ay = abs_bits(ndc1) > abs_bits(ndc3) ? abs_bits(ndc1) : abs_bits(ndc3);
        const float sx = ax * af;
        const float sy = f * ay;
        scene->frustum_scale[0] = sx;
        scene->frustum_scale[1] = sy;
        // The rectangle widened by the same extents around its centre.
        const float mid_x = sum_x * 0.5f;
        x0 = (x0 - mid_x) * ax + mid_x;
        x1 = (x1 - mid_x) * ax + mid_x;
        const float mid_y = sum_y * 0.5f;
        y0 = (y0 - mid_y) * ay + mid_y;
        y1 = (y1 - mid_y) * ay + mid_y;
        nglMatrixPerspective(m, 1.0f / sx, 1.0f / sy, scene->near_z, scene->far_z);
        std::memcpy(scene->base_projection, m, 64);
        planes[4] = {{0.0f, 0.0f, 1.0f, scene->near_z}};
        planes[5] = {{0.0f, 0.0f, -1.0f, 0.0f - scene->far_z}};
        const float v0 = af * ndc0;
        float s = platform_x87_rcp_sqrt_sq_plus_one(v0);
        planes[0] = {{s, 0.0f, 0.0f - s * v0, 0.0f}};
        const float v2 = af * ndc2;
        s = platform_x87_rcp_sqrt_sq_plus_one(v2);
        planes[1] = {{0.0f - s, 0.0f, s * v2, 0.0f}};
        const float v1 = f * ndc1;
        s = platform_x87_rcp_sqrt_sq_plus_one(v1);
        planes[2] = {{0.0f, 0.0f - s, 0.0f - s * v1, 0.0f}};
        const float v3 = f * ndc3;
        s = platform_x87_rcp_sqrt_sq_plus_one(v3);
        planes[3] = {{0.0f, s, s * v3, 0.0f}};
    } else {
        nglMatrixOrtho(m, 1.0f / scene->aspect, 1.0f, scene->near_z, scene->far_z);
        std::memcpy(scene->base_projection, m, 64);
        planes[4] = {{0.0f, 0.0f, 1.0f, scene->near_z}};
        planes[5] = {{0.0f, 0.0f, -1.0f, 0.0f - scene->far_z}};
        planes[0] = {{1.0f, 0.0f, 0.0f, ndc0 * scene->aspect}};
        planes[1] = {{-1.0f, 0.0f, 0.0f, 0.0f - ndc2 * scene->aspect}};
        planes[2] = {{0.0f, -1.0f, 0.0f, ndc1}};
        planes[3] = {{0.0f, 1.0f, 0.0f, 0.0f - ndc3}};
    }
    nglMatrixViewport(m, x0, y0, x1, y1);
    std::memcpy(scene->viewport, m, 64);
    std::memcpy(scene->target_scale, nglGetSceneTargetScale(m), 64);

    // projection = base rows times the viewport (lane 3 kept from the base row), then
    // times the target scale.
    for (int i = 0; i < 4; ++i) {
        const Lanes base = lanes(scene->base_projection + i * 4);
        Lanes t = row_times(base, scene->viewport);
        platform_keep(t);
        t.v[3] = base.v[3];
        store(scene->projection + i * 4, row_times(t, scene->target_scale));
    }

    // The camera: the view matrix inverted as a rigid transform.
    const float* v = scene->view_matrix;
    float camera[16] = {
        v[0], v[4], v[8], v[8],
        v[1], v[5], v[9], v[9],
        v[2], v[6], v[10], v[10],
    };
    {
        const Lanes t = neg(lanes(v + 12));
        Lanes row = add(mul(lanes(camera), t.v[0]), mul(lanes(camera + 4), t.v[1]));
        store(camera + 12, add(row, mul(lanes(camera + 8), t.v[2])));
    }
    std::memcpy(scene->matrix0d0, camera, 64);

    // view_projection = view * projection (rows 0..2 without, row 3 with translation).
    const float* p = scene->projection;
    for (int i = 0; i < 3; ++i) {
        Lanes row = add(mul(lanes(p), v[i * 4]), mul(lanes(p + 4), v[i * 4 + 1]));
        store(scene->view_projection + i * 4, add(row, mul(lanes(p + 8), v[i * 4 + 2])));
    }
    {
        Lanes row = add(lanes(p + 12), mul(lanes(p), v[12]));
        row = add(row, mul(lanes(p + 4), v[13]));
        store(scene->view_projection + 12, add(row, mul(lanes(p + 8), v[14])));
    }

    nglMatrixSceneClipToScreen(m);
    std::memcpy(scene->clip_to_screen, m, 64);

    // The frustum planes into world space: normal through the camera rotation (lane 3
    // of the rows 0), distance plus the camera position's offset.
    const float* c = scene->matrix0d0;
    const Lanes r0 = {{c[0], c[1], c[2], 0.0f}};
    const Lanes r1 = {{c[4], c[5], c[6], 0.0f}};
    const Lanes r2 = {{c[8], c[9], c[10], 0.0f}};
    const Lanes pos = {{c[12], c[13], c[14], 1.0f}};
    for (int i = 0; i < 6; ++i) {
        const Lanes& n = planes[i];
        Lanes world = add(mul(r0, n.v[0]), mul(r1, n.v[1]));
        world = add(world, mul(r2, n.v[2]));
        Lanes q = {{world.v[0] * pos.v[0], world.v[1] * pos.v[1], world.v[2] * pos.v[2], world.v[3] * pos.v[3]}};
        q.v[0] = q.v[0] + q.v[1] + q.v[2];
        Lanes d = {{q.v[0] + n.v[3], q.v[1] + n.v[3], q.v[2] + n.v[3], q.v[3] + n.v[3]}};
        platform_keep(d);
        world.v[3] = d.v[0];
        store(scene->frustum[i], world);
    }
    std::memcpy(scene->camera_position, c + 12, 16);
    std::memcpy(scene->camera_forward, c + 8, 16);

    nglMatrixScreenToClip(m);
    for (int i = 0; i < 4; ++i) store(scene->screen_to_target + i * 4, row_times(lanes(m + i * 4), scene->target_scale));
}

void XB_CDECL nglMatrixSceneClipToScreen(float* out) {
    const NglScene* scene = current_scene();
    const float* vp = scene->viewport;
    const float* proj = scene->base_projection;
    // The original divides whole rows (DIVPS) and uses one lane of each quotient.
    auto divide_rows = [](const float* a, const float* b) {
        Lanes q;
        for (int i = 0; i < 4; ++i) q.v[i] = -a[i] / b[i];
        platform_keep(q);
        return q;
    };
    const Lanes vp_offset = divide_rows(vp + 12, vp + 4);
    // The viewport's inverse (only the columns the product reads).
    const Lanes inv_vp[4] = {
        {{1.0f / vp[0], 0.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f / vp[5], 0.0f, 0.0f}},
        {{0.0f, 0.0f, 1.0f, 0.0f}},
        {{-vp[12] / vp[0], vp_offset.v[1], 0.0f, 0.0f}},
    };
    Lanes inv_proj[4];
    inv_proj[0] = {{1.0f / proj[0], 0.0f, 0.0f, 0.0f}};
    inv_proj[1] = {{0.0f, 1.0f / proj[5], 0.0f, 0.0f}};
    if (scene->projection_kind == 1) {
        inv_proj[2] = {{0.0f, 0.0f, 0.0f, 1.0f / proj[14]}};
        inv_proj[3] = {{0.0f, 0.0f, 1.0f, divide_rows(proj + 8, proj + 12).v[2]}};
    } else {
        inv_proj[2] = {{0.0f, 0.0f, 1.0f / proj[10], 0.0f}};
        inv_proj[3] = {{0.0f, 0.0f, divide_rows(proj + 12, proj + 8).v[2], 1.0f}};
    }
    Lanes left[4];
    for (int i = 0; i < 4; ++i) {
        const float* r = inv_vp[i].v;
        Lanes row = i == 3 ? add(inv_proj[3], mul(inv_proj[0], r[0])) : mul(inv_proj[0], r[0]);
        row = add(row, mul(inv_proj[1], r[1]));
        left[i] = add(row, mul(inv_proj[2], r[2]));
    }
    const float* m = scene->matrix0d0;
    for (int i = 0; i < 4; ++i) {
        const float* r = left[i].v;
        Lanes row = add(mul(lanes(m), r[0]), mul(lanes(m + 4), r[1]));
        row = add(row, mul(lanes(m + 8), r[2]));
        row = add(row, mul(lanes(m + 12), r[3]));
        out[i * 4 + 0] = row.v[0];
        out[i * 4 + 1] = row.v[1];
        out[i * 4 + 2] = row.v[2];
        out[i * 4 + 3] = r[3];
    }
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
    for (NglLight*& list : set->projectors) list = reinterpret_cast<NglLight*>(&set->projectors);
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

void XB_CDECL nglSetSceneDepthEnable(uint8_t enable) { current_scene()->depth_enable = enable; }

void XB_CDECL nglSetSceneDepthTest(uint8_t test) { current_scene()->depth_test = test; }

void XB_CDECL nglSetSceneTime(float time) { current_scene()->time = time; }

void XB_CDECL nglSetPreSwapCallback(void* function, void* arg) {
    title_global<void*>(0x009025c0) = function;
    title_global<void*>(0x009025c4) = arg;
}

void XB_CDECL nglSetEndFrameCallback(void* function, void* arg) {
    title_global<void*>(0x009025c8) = function;
    title_global<void*>(0x009025cc) = arg;
}

void XB_CDECL nglSetMeshFileManager(void* manager) { title_global<void*>(0x00902a84) = manager; }

void* XB_CDECL nglGetMeshFileManager() { return title_global<void*>(0x00902a84); }

void XB_CDECL nglSetMeshManager(void* manager) { title_global<void*>(0x00902a80) = manager; }

void* XB_CDECL nglGetMeshManager() { return title_global<void*>(0x00902a80); }

namespace {

template <typename T>
T& field(const uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(const_cast<uint8_t*>(base) + offset);
}

void relocate(uint8_t*& pointer, intptr_t delta) {
    if (pointer) pointer += delta;
}

}  // namespace

uint8_t* XB_CDECL nglMeshFileFirstMesh(const uint8_t* file) {
    return file ? field<uint8_t*>(file, 0x138) : nullptr;
}

uint8_t* XB_CDECL nglMeshNext(const uint8_t* mesh) { return mesh ? field<uint8_t*>(mesh, 0x38) : nullptr; }

void XB_CDECL nglRelocateMesh(uint8_t* new_base, uint8_t* old_base, uint8_t* mesh) {
    const intptr_t delta = new_base - old_base;
    relocate(field<uint8_t*>(mesh, 0x14), delta);
    relocate(field<uint8_t*>(mesh, 0x1c), delta);
    relocate(field<uint8_t*>(mesh, 0x0c), delta);
    for (uint32_t i = 0; i < field<uint32_t>(mesh, 0x18); ++i)
        relocate(field<uint8_t**>(mesh, 0x1c)[i * 2], delta);
    for (uint32_t i = 0; i < field<uint32_t>(mesh, 0x08); ++i) {
        relocate(field<uint8_t**>(mesh, 0x0c)[i * 2 + 1], delta);
        nglRelocateMeshSection(new_base, old_base, field<uint8_t**>(mesh, 0x0c)[i * 2 + 1]);
    }
}

void XB_CDECL nglRelocateMeshSection(uint8_t* new_base, uint8_t* old_base, uint8_t* section) {
    const intptr_t delta = new_base - old_base;
    // Not a range-for over a braced list: at -O0 that calls std::initializer_list's
    // members out of line, and the loader's copies of them win (host code in a test).
    static const uint32_t kPointers[] = {0x0c, 0x44, 0x30, 0x38, 0x04, 0x5c};
    for (uint32_t i = 0; i < 6; ++i) relocate(field<uint8_t*>(section, kPointers[i]), delta);
    uint8_t** table = field<uint8_t**>(section, 0x5c);
    if (table) relocate(*table, delta);
}

void XB_CDECL nglVertexShader_Init(uint32_t* shader, uint32_t declaration, uint32_t program) {
    shader[0] = program;
    shader[1] = declaration;
}

void XB_CDECL nglPixelShader_Init(uint32_t* shader, uint32_t definition) { shader[0] = definition; }

uint8_t XB_THISCALL nglIndexBuffer_Init(uint8_t** self, uint8_t* data, uint32_t /*size*/) {
    *self = data;
    return 1;
}

void XB_THISCALL nglIndexBuffer_Clear(uint8_t** self) { *self = nullptr; }

uint8_t* XB_THISCALL nglIndexBuffer_Lock(uint8_t** self, uint32_t offset, uint32_t /*flags*/) {
    return *self + (offset >> 1);
}

void XB_THISCALL nglIndexBuffer_Write(uint8_t** self, const void* source, uint32_t size, uint32_t offset) {
    std::memcpy(*self + (offset >> 1), source, size);
}

uint8_t XB_CDECL nglEvalPointLight(float* out, const float* light, const float* position) {
    float d[4];
    for (int i = 0; i < 4; ++i) d[i] = position[i] - light[i];
    const float dist2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (1e-6f > dist2) {
        for (int i = 0; i < 4; ++i) out[4 + i] = light[4 + i];
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
        out[3] = 0.0f;
        return 0;
    }
    const float estimate = platform_rsqrt_estimate(dist2);
    const float inv_dist = (3.0f - estimate * estimate * dist2) * estimate * 0.5f;
    const float dist = dist2 * inv_dist;
    float falloff = 1.0f - (dist - light[8]) / (light[9] - light[8]);
    uint8_t lit = 1;
    if (0.0f > falloff) {
        falloff = 0.0f;
        lit = 0;
    } else if (falloff > 1.0f) {
        falloff = 1.0f;
    }
    for (int i = 0; i < 4; ++i) out[4 + i] = light[4 + i] * falloff;
    for (int i = 0; i < 4; ++i) out[i] = d[i] * inv_dist;
    return lit;
}

namespace {

bool less(const NglTranslucentNode& a, const NglTranslucentNode& b) {
    if (a.key > b.key) return true;
    if (b.key > a.key) return false;
    return a.value < b.value;
}

template <typename Node>
void swap_nodes(Node& a, Node& b) {
    const Node t = a;
    a = b;
    b = t;
}

}  // namespace

void XB_CDECL std_PushHeap_OpaqueNodes(NglOpaqueNode* first, int32_t hole, int32_t top, NglOpaqueNode value) {
    for (int32_t parent = (hole - 1) / 2; top < hole && first[parent].key < value.key; parent = (hole - 1) / 2) {
        first[hole] = first[parent];
        hole = parent;
    }
    first[hole] = value;
}

void XB_CDECL std_PushHeap_TranslucentNodes(NglTranslucentNode* first, int32_t hole, int32_t top,
                                            NglTranslucentNode value) {
    for (int32_t parent = (hole - 1) / 2; top < hole && less(first[parent], value); parent = (hole - 1) / 2) {
        first[hole] = first[parent];
        hole = parent;
    }
    first[hole] = value;
}

void XB_CDECL std_Rotate_NodeKeys(NglOpaqueNode* first, NglOpaqueNode* mid, NglOpaqueNode* last) {
    const int32_t shift = static_cast<int32_t>(mid - first);
    const int32_t count = static_cast<int32_t>(last - first);
    int32_t factor = count;
    for (int32_t b = shift; b != 0;) {
        const int32_t r = factor % b;
        factor = b;
        b = r;
    }
    if (factor >= count || factor <= 0) return;
    for (; factor > 0; --factor) {
        NglOpaqueNode* hole = first + factor;
        const NglOpaqueNode value = *hole;
        NglOpaqueNode* next1 = hole;
        NglOpaqueNode* next2 = hole + shift == last ? first : hole + shift;
        while (next2 != hole) {
            *next1 = *next2;
            next1 = next2;
            if (shift < last - next2)
                next2 += shift;
            else
                next2 = first + (shift - (last - next2));
        }
        *next1 = value;
    }
}

void XB_CDECL std_Med3_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* mid, NglOpaqueNode* last) {
    if (mid->key < first->key) swap_nodes(*mid, *first);
    if (last->key < mid->key) swap_nodes(*last, *mid);
    if (mid->key < first->key) swap_nodes(*mid, *first);
}

void XB_CDECL std_Med3_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* mid,
                                        NglTranslucentNode* last) {
    if (less(*mid, *first)) swap_nodes(*mid, *first);
    if (less(*last, *mid)) swap_nodes(*last, *mid);
    if (less(*mid, *first)) swap_nodes(*mid, *first);
}

namespace {

// The rest of MSVC's std::sort (Dinkumware, VC7) over both node types; each template
// calls the per-type title-named helpers, as the instantiations do.
bool node_less(const NglOpaqueNode& a, const NglOpaqueNode& b) { return a.key < b.key; }
bool node_less(const NglTranslucentNode& a, const NglTranslucentNode& b) { return less(a, b); }

void push_heap(NglOpaqueNode* first, int32_t hole, int32_t top, NglOpaqueNode value) {
    std_PushHeap_OpaqueNodes(first, hole, top, value);
}
void push_heap(NglTranslucentNode* first, int32_t hole, int32_t top, NglTranslucentNode value) {
    std_PushHeap_TranslucentNodes(first, hole, top, value);
}
void med3(NglOpaqueNode* first, NglOpaqueNode* mid, NglOpaqueNode* last) { std_Med3_OpaqueNodes(first, mid, last); }
void med3(NglTranslucentNode* first, NglTranslucentNode* mid, NglTranslucentNode* last) {
    std_Med3_TranslucentNodes(first, mid, last);
}
void adjust_heap(NglOpaqueNode* first, int32_t hole, int32_t bottom, NglOpaqueNode value, uint32_t pred) {
    std_AdjustHeap_OpaqueNodes(first, hole, bottom, value, pred);
}
void adjust_heap(NglTranslucentNode* first, int32_t hole, int32_t bottom, NglTranslucentNode value, uint32_t pred) {
    std_AdjustHeap_TranslucentNodes(first, hole, bottom, value, pred);
}
void make_heap(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t pred) { std_MakeHeap_OpaqueNodes(first, last, pred); }
void make_heap(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred) {
    std_MakeHeap_TranslucentNodes(first, last, pred);
}
void sort_heap(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t pred) { std_SortHeap_OpaqueNodes(first, last, pred); }
void sort_heap(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred) {
    std_SortHeap_TranslucentNodes(first, last, pred);
}
void insertion_sort(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t pred) {
    std_InsertionSort_OpaqueNodes(first, last, pred);
}
void insertion_sort(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred) {
    std_InsertionSort_TranslucentNodes(first, last, pred);
}
void median(NglOpaqueNode* first, NglOpaqueNode* mid, NglOpaqueNode* last, uint32_t pred) {
    std_Median_OpaqueNodes(first, mid, last, pred);
}
void median(NglTranslucentNode* first, NglTranslucentNode* mid, NglTranslucentNode* last, uint32_t pred) {
    std_Median_TranslucentNodes(first, mid, last, pred);
}
template <typename Node>
NglNodeRange<Node>* unguarded_partition(NglNodeRange<Node>* result, Node* first, Node* last, uint32_t pred);
template <>
NglNodeRange<NglOpaqueNode>* unguarded_partition(NglNodeRange<NglOpaqueNode>* result, NglOpaqueNode* first,
                                                 NglOpaqueNode* last, uint32_t pred) {
    return std_UnguardedPartition_OpaqueNodes(result, first, last, pred);
}
template <>
NglNodeRange<NglTranslucentNode>* unguarded_partition(NglNodeRange<NglTranslucentNode>* result,
                                                      NglTranslucentNode* first, NglTranslucentNode* last,
                                                      uint32_t pred) {
    return std_UnguardedPartition_TranslucentNodes(result, first, last, pred);
}
void sort_r(NglOpaqueNode* first, NglOpaqueNode* last, int32_t ideal, uint32_t pred) {
    nglSortOpaqueNodes_r(first, last, ideal, pred);
}
void sort_r(NglTranslucentNode* first, NglTranslucentNode* last, int32_t ideal, uint32_t pred) {
    nglSortTranslucentNodes_r(first, last, ideal, pred);
}

template <typename Node>
void adjust_heap_impl(Node* first, int32_t hole, int32_t bottom, Node value, uint32_t pred) {
    const int32_t top = hole;
    int32_t child = 2 * hole + 2;
    for (; child < bottom; child = 2 * child + 2) {
        if (node_less(first[child], first[child - 1])) --child;
        first[hole] = first[child];
        hole = child;
    }
    if (child == bottom) {
        first[hole] = first[bottom - 1];
        hole = bottom - 1;
    }
    (void)pred;
    push_heap(first, hole, top, value);
}

template <typename Node>
void make_heap_impl(Node* first, Node* last, uint32_t pred) {
    const int32_t bottom = static_cast<int32_t>(last - first);
    for (int32_t hole = bottom / 2; hole > 0;) {
        --hole;
        adjust_heap(first, hole, bottom, first[hole], pred);
    }
}

template <typename Node>
void sort_heap_impl(Node* first, Node* last, uint32_t pred) {
    for (; last - first > 1; --last) {
        const Node value = last[-1];
        last[-1] = first[0];
        adjust_heap(first, 0, static_cast<int32_t>(last - 1 - first), value, pred);
    }
}

// With std::rotate instead of copy_backward, as this STL does.
template <typename Node>
void insertion_sort_impl(Node* first, Node* last) {
    if (first == last) return;
    for (Node* next = first + 1; next != last; ++next) {
        Node* hole;
        if (node_less(*next, *first)) {
            hole = first;
        } else {
            hole = next;
            for (Node* previous = next - 1; node_less(*next, *previous); --previous) hole = previous;
        }
        if (hole != next) std_Rotate_NodeKeys(reinterpret_cast<NglOpaqueNode*>(hole), reinterpret_cast<NglOpaqueNode*>(next),
                                              reinterpret_cast<NglOpaqueNode*>(next + 1));
    }
}

template <typename Node>
void median_impl(Node* first, Node* mid, Node* last) {
    const int32_t count = static_cast<int32_t>(last - first);
    if (count > 40) {
        const int32_t step = (count + 1) / 8;
        med3(first, first + step, first + 2 * step);
        med3(mid - step, mid, mid + step);
        med3(last - 2 * step, last - step, last);
        med3(first + step, mid, last - step);
    } else {
        med3(first, mid, last);
    }
}

template <typename Node>
void iter_swap(Node* a, Node* b) {
    const Node t = *a;
    *a = *b;
    *b = t;
}

template <typename Node>
NglNodeRange<Node>* unguarded_partition_impl(NglNodeRange<Node>* result, Node* first, Node* last, uint32_t pred) {
    Node* mid = first + (last - first) / 2;
    median(first, mid, last - 1, pred);
    Node* pfirst = mid;
    Node* plast = pfirst + 1;
    while (first < pfirst && !node_less(pfirst[-1], *pfirst) && !node_less(*pfirst, pfirst[-1])) --pfirst;
    while (plast < last && !node_less(*plast, *pfirst) && !node_less(*pfirst, *plast)) ++plast;
    Node* gfirst = plast;
    Node* glast = pfirst;
    for (;;) {
        for (; gfirst < last; ++gfirst) {
            if (node_less(*pfirst, *gfirst)) continue;
            if (node_less(*gfirst, *pfirst)) break;
            iter_swap(plast++, gfirst);
        }
        for (; first < glast; --glast) {
            if (node_less(glast[-1], *pfirst)) continue;
            if (node_less(*pfirst, glast[-1])) break;
            iter_swap(--pfirst, glast - 1);
        }
        if (glast == first && gfirst == last) {
            result->first = pfirst;
            result->second = plast;
            return result;
        }
        if (glast == first) {
            if (plast != gfirst) iter_swap(pfirst, plast);
            ++plast;
            iter_swap(pfirst++, gfirst++);
        } else if (gfirst == last) {
            if (--glast != --pfirst) iter_swap(glast, pfirst);
            iter_swap(pfirst, --plast);
        } else {
            iter_swap(gfirst++, --glast);
        }
    }
}

template <typename Node>
void sort_r_impl(Node* first, Node* last, int32_t ideal, uint32_t pred) {
    int32_t count = static_cast<int32_t>(last - first);
    for (; count > 32 && ideal > 0; count = static_cast<int32_t>(last - first)) {
        NglNodeRange<Node> range;
        unguarded_partition(&range, first, last, pred);
        ideal = ideal / 2 + ideal / 2 / 2;
        // Recurse into the smaller side (byte distances rounded down to whole nodes).
        const int32_t low = static_cast<int32_t>(reinterpret_cast<uintptr_t>(range.first) - reinterpret_cast<uintptr_t>(first)) & ~7;
        const int32_t high = static_cast<int32_t>(reinterpret_cast<uintptr_t>(last) - reinterpret_cast<uintptr_t>(range.second)) & ~7;
        if (low < high) {
            sort_r(first, range.first, ideal, pred);
            first = range.second;
        } else {
            sort_r(range.second, last, ideal, pred);
            last = range.first;
        }
    }
    if (count > 32) {
        if ((static_cast<int32_t>(reinterpret_cast<uintptr_t>(last) - reinterpret_cast<uintptr_t>(first)) & ~7) > 8)
            make_heap(first, last, pred);
        sort_heap(first, last, pred);
    } else if (count > 1) {
        insertion_sort(first, last, pred);
    }
}

}  // namespace

void XB_CDECL std_AdjustHeap_OpaqueNodes(NglOpaqueNode* first, int32_t hole, int32_t bottom, NglOpaqueNode value,
                                         uint32_t pred) {
    adjust_heap_impl(first, hole, bottom, value, pred);
}
void XB_CDECL std_AdjustHeap_TranslucentNodes(NglTranslucentNode* first, int32_t hole, int32_t bottom,
                                              NglTranslucentNode value, uint32_t pred) {
    adjust_heap_impl(first, hole, bottom, value, pred);
}
void XB_CDECL std_MakeHeap_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t pred) {
    make_heap_impl(first, last, pred);
}
void XB_CDECL std_MakeHeap_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred) {
    make_heap_impl(first, last, pred);
}
void XB_CDECL std_SortHeap_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t pred) {
    sort_heap_impl(first, last, pred);
}
void XB_CDECL std_SortHeap_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred) {
    sort_heap_impl(first, last, pred);
}
void XB_CDECL std_InsertionSort_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t) {
    insertion_sort_impl(first, last);
}
void XB_CDECL std_InsertionSort_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t) {
    insertion_sort_impl(first, last);
}
void XB_CDECL std_Median_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* mid, NglOpaqueNode* last, uint32_t) {
    median_impl(first, mid, last);
}
void XB_CDECL std_Median_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* mid, NglTranslucentNode* last,
                                          uint32_t) {
    median_impl(first, mid, last);
}
NglNodeRange<NglOpaqueNode>* XB_CDECL std_UnguardedPartition_OpaqueNodes(NglNodeRange<NglOpaqueNode>* result,
                                                                         NglOpaqueNode* first, NglOpaqueNode* last,
                                                                         uint32_t pred) {
    return unguarded_partition_impl(result, first, last, pred);
}
NglNodeRange<NglTranslucentNode>* XB_CDECL std_UnguardedPartition_TranslucentNodes(
    NglNodeRange<NglTranslucentNode>* result, NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred) {
    return unguarded_partition_impl(result, first, last, pred);
}
void XB_CDECL nglSortOpaqueNodes_r(NglOpaqueNode* first, NglOpaqueNode* last, int32_t ideal, uint32_t pred) {
    sort_r_impl(first, last, ideal, pred);
}
void XB_CDECL nglSortTranslucentNodes_r(NglTranslucentNode* first, NglTranslucentNode* last, int32_t ideal,
                                        uint32_t pred) {
    sort_r_impl(first, last, ideal, pred);
}

const uint32_t* XB_THISCALL nglSkipList_Find(const NglSkipList* self, const uint32_t* key) {
    const NglSkipListNode* current = self->head;
    if (!current) return nullptr;
    int32_t level = self->level;
    const NglSkipListNode* node;
    do {
        node = current->next[level];
        while (node) {
            int i = 0;
            while (node->key[i] == key[i]) {
                if (++i >= 8) return node->key;
            }
            if (key[i] < node->key[i]) break;  // down a level
            current = node;
            node = current->next[level];
        }
    } while (--level >= 0);
    // The original compares the node it last stopped at once more.
    if (node) {
        for (int i = 0; node->key[i] == key[i];) {
            if (++i >= 8) return node->key;
        }
    }
    return nullptr;
}

uint8_t* XB_CDECL nglMeshFileFindMesh(const uint32_t* key, const uint8_t* file) {
    for (uint8_t* mesh = *reinterpret_cast<uint8_t* const*>(file + 0x13c); mesh;
         mesh = *reinterpret_cast<uint8_t**>(mesh + 0xc)) {
        const uint32_t* name = *reinterpret_cast<const uint32_t* const*>(mesh);
        for (int i = 0; name[i] == key[i];) {
            if (++i >= 8) return mesh;
        }
    }
    return nullptr;
}

NglVideoMode* XB_THISCALL nglVideoMode_Init(NglVideoMode* self, uint32_t width, uint32_t height,
                                            uint8_t progressive, uint8_t widescreen,
                                            uint8_t unknown0a, uint32_t u0, uint32_t u1,
                                            uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5,
                                            uint32_t u6, uint32_t u7, uint32_t u8) {
    self->width = width;
    self->height = height;
    self->progressive = progressive;
    self->widescreen = widescreen;
    self->unknown0a = unknown0a;
    self->unknown0b = 0;
    const uint32_t values[9] = {u0, u1, u2, u3, u4, u5, u6, u7, u8};
    for (int i = 0; i < 9; ++i) self->unknown0c[i] = values[i];
    self->unknown30 = 0;
    self->unknown34 = 1.0f;
    self->unknown38 = 1.0f;
    return self;
}

NglVideoMode* XB_THISCALL nglVideoMode_Copy(NglVideoMode* self, const NglVideoMode* other) {
    self->width = other->width;
    self->height = other->height;
    self->progressive = other->progressive;
    self->widescreen = other->widescreen;
    self->unknown0a = other->unknown0a;
    self->unknown0b = other->unknown0b;
    for (int i = 0; i < 9; ++i) self->unknown0c[i] = other->unknown0c[i];
    self->unknown30 = other->unknown30;
    self->unknown34 = other->unknown34;
    self->unknown38 = other->unknown38;
    return self;
}

void XB_CDECL nglResetFrameStats() {
    static const uint32_t kCounters[] = {0x00902b60, 0x00902b68, 0x00902b6c, 0x00902b70,
                                         0x00902b74, 0x00902bf8, 0x00902bfc, 0x00902c00,
                                         0x00902c04, 0x00902c08, 0x00902c0c};
    for (uint32_t address : kCounters) title_global<uint32_t>(address) = 0;
    for (int i = 0; i < 0x20; ++i) {
        title_global<uint32_t>(0x00902b78 + i * 4) = 0;
        title_global<uint32_t>(0x00902c10 + i * 4) = 0;
    }
    title_global<uint32_t>(0x00902b64) = 0xff00;
}

namespace {

// Spreads the bits of `value` over the set bits of `mask` (lowest first), as the
// original's loop does.
uint32_t swizzle(uint32_t value, uint32_t mask) {
    uint32_t result = 0;
    for (uint32_t bit = 1; bit <= mask; bit <<= 1) {
        if (bit & mask)
            result |= bit & value;
        else
            value <<= 1;
    }
    return -(-result & mask) & mask;
}

}  // namespace

void XB_CDECL nglTextureSetPixel(const uint8_t* texture, uint32_t x, uint32_t y, uint8_t value) {
    uint8_t* pixels = *reinterpret_cast<uint8_t* const*>(texture + 0x20);
    const uint32_t offset_x = swizzle(x, title_global<uint32_t>(0x00902b44));
    const uint32_t offset_y = swizzle(y, title_global<uint32_t>(0x00902b48));
    title_global<uint32_t>(0x00902b54) = offset_y;
    title_global<uint32_t>(0x00902b50) = offset_x;
    pixels[offset_y | offset_x] = value;
}

NglLight* XB_CDECL nglLightSetAddProjector(uint32_t type, float* data, uint32_t flags) {
    NglLight* projector = alloc_scene_memory<NglLight>(0x30);
    if (!projector) return nullptr;
    projector->data = data;
    projector->type = type;
    projector->flags = flags;
    for (int list = 0; list < 8; ++list) {
        if (!(flags & (0x01000000u << list))) continue;
        projector->next[list] = current_light_set()->projectors[list];
        current_light_set()->projectors[list] = projector;
    }
    return projector;
}

float* XB_CDECL nglGetTargetSampleScale(float* out, const NglRenderTarget* target) {
    uint32_t& guard = title_global<uint32_t>(0x00902f00);
    float* fallback = &title_global<float>(0x00902ef0);
    if (!(guard & 1)) {
        guard |= 1;
        fallback[0] = 1.0f;
        fallback[1] = 1.0f;
        fallback[2] = 0.53125f;
        fallback[3] = 0.0f;
    }
    // Copied as words, like the original.
    const float* source = (target->flags & 4) ? &title_global<float>(0x00903c70) : fallback;
    std::memcpy(out, source, 16);
    return out;
}

void XB_THISCALL nglSetTextureAddressMode(uint8_t* self, uint32_t stage, uint8_t flags, uint32_t border) {
    uint32_t& cached = *reinterpret_cast<uint32_t*>(self + 0x20 + stage * 4);
    uint32_t& dirty = title_global<uint32_t>(0x003f8320);
    uint32_t* state = &title_global<uint32_t>(0x003f5c74 + (stage << 7));
    const uint32_t bit = 1u << (stage & 31);
    auto set = [&](uint32_t mode, uint32_t u, uint32_t v, uint32_t w) {
        state[0] = u;
        state[1] = v;
        state[2] = w;
        dirty |= bit;
        cached = mode;
    };
    if (flags & 2) {
        if (cached != 2) set(2, 2, 2, 1);
    } else if (flags & 4) {
        if (cached != 4) set(4, 2, 2, 2);
    } else if (flags & 8) {
        if (cached != 8) set(8, 3, 3, 2);
        dirty |= bit;
        state[5] = border;  // +0x14
    } else if (cached != 1) {
        set(1, 1, 1, 1);
    }
}

void XB_CDECL nglInitShaderConstantTable() {
    // {table entry, title word it is loaded from}; 0 = an address stored as is.
    static const uint32_t kLoaded[][2] = {
        {0x00903c9c, 0x008a362c}, {0x00903cd0, 0x008a36ac}, {0x00903ca8, 0x008a3664},
        {0x00903cbc, 0x008a3714}, {0x00903ce8, 0x008a3c14}, {0x00903cf0, 0x008a3c18},
        {0x00903cf8, 0x008a3c1c}, {0x00903d00, 0x008a3c20}, {0x00903cc4, 0x008a3d18},
        {0x00903cb4, 0x008a3e10}, {0x00903d08, 0x008a3f08}, {0x00903cd8, 0x008a45b8},
        {0x00903cdc, 0x008a45bc}, {0x00903ce0, 0x008a45c0}, {0x00903ce4, 0x008a45c4},
        {0x00903cb0, 0x008a4000}, {0x00903cb8, 0x008a40f8}, {0x00903ca4, 0x008a41f0},
    };
    static const uint32_t kStored[][2] = {
        {0x00903cd4, 0x008a33a0}, {0x00903cac, 0x008a33a0}, {0x00903ca0, 0x008a32a0},
        {0x00903cc0, 0x008a31a0}, {0x00903cec, 0}, {0x00903cf4, 0}, {0x00903cfc, 0},
        {0x00903d04, 0},
    };
    for (const auto& entry : kLoaded) title_global<uint32_t>(entry[0]) = title_global<uint32_t>(entry[1]);
    for (const auto& entry : kStored) title_global<uint32_t>(entry[0]) = entry[1];
}

uint8_t* XB_REGS1 XB_CDECL nglSelectMeshLod(uint8_t* table, int8_t flags, const uint8_t* params,
                                            const float* position) {
    // Lane z of row3 + row0 * x + row1 * y + row2 * z, summed in that order.
    const float* view = current_scene()->view_matrix;
    float depth = view[12 + 2] + view[0 + 2] * position[0];
    depth = depth + view[4 + 2] * position[1];
    depth = depth + view[8 + 2] * position[2];
    if (flags < 0) depth = *reinterpret_cast<const float*>(params + 0x20) * depth;
    const int32_t count = *reinterpret_cast<const int32_t*>(table + 0x18);
    const uint8_t* entries = *reinterpret_cast<uint8_t* const*>(table + 0x1c);
    for (int32_t i = count - 1; i >= 0; --i) {
        if (depth > *reinterpret_cast<const float*>(entries + i * 8 + 4))
            return *reinterpret_cast<uint8_t* const*>(entries + i * 8);
    }
    return table;
}

namespace {

// MAXPS: the second operand unless the first is greater (NaN picks the second).
float maxps(float a, float b) { return a > b ? a : b; }

}  // namespace

float* XB_CDECL nglScaleMeshMatrix(const float* matrix, const uint8_t* instance, float* max_scale) {
    const float* scale = reinterpret_cast<const float*>(instance + 0x10);
    const float x = abs_bits(scale[0]);
    const float y = abs_bits(scale[1]);
    const float z = abs_bits(scale[2]);
    *max_scale = maxps(maxps(z, y), x);
    title_global<uint32_t>(0x00902b30) |= 1;  // the static's construction guard
    float* out = &title_global<float>(0x00902af0);
    for (int row = 0; row < 3; ++row) {
        for (int i = 0; i < 4; ++i) out[row * 4 + i] = matrix[row * 4 + i] * scale[row];
    }
    std::memcpy(out + 12, matrix + 12, 16);
    return out;
}

namespace {

// The title's packed cross product: (a * yzx(b) - yzx(a) * b), rotated (w unused).
Lanes cross3(const Lanes& a, const Lanes& b) {
    const float* x = a.v;
    const float* y = b.v;
    return {{x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0], 0.0f}};
}

float dot3(const Lanes& a, const float* b) { return (a.v[0] * b[0] + a.v[1] * b[1]) + a.v[2] * b[2]; }

// 1 / |v| from RSQRTSS and one Newton step; 0 for a zero vector.
float inverse_length(const Lanes& v) {
    const float sum = (v.v[0] * v.v[0] + v.v[1] * v.v[1]) + v.v[2] * v.v[2];
    if (sum == 0.0f) return 0.0f;
    const float a = abs_bits(sum);
    const float r = platform_rsqrt_estimate(a);
    return ((3.0f - (r * r) * a) * r) * 0.5f;
}

}  // namespace

void XB_CDECL nglBuildProjectorPlanes(float* out, const float* points) {
    const Lanes p0 = lanes(points);
    const Lanes p1 = lanes(points + 4);
    const Lanes p2 = lanes(points + 8);
    const float* corner = points + 12;
    static const Lanes kW = {{0.0f, 0.0f, 0.0f, 1.0f}};
    // Plane (n, -n.corner) and the opposite one through `far`, as packed ops.
    auto store = [&](float* plane, float* opposite, const Lanes& raw, const float* far) {
        const Lanes n = mul(raw, inverse_length(raw));
        const Lanes front = sub({{n.v[0], n.v[1], n.v[2], 0.0f}}, mul(kW, dot3(n, corner)));
        const Lanes back = add(neg(front), mul(kW, dot3(n, far)));
        std::memcpy(plane, front.v, 16);
        std::memcpy(opposite, back.v, 16);
    };
    const Lanes n0 = cross3(p0, p1);
    const Lanes n1 = cross3(p1, p2);
    const Lanes n2 = cross3(p2, p0);
    store(out + 16, out + 20, n0, points + 8);
    store(out + 8, out + 12, n1, points);
    store(out + 4, out, n2, points + 4);
}

void XB_CDECL nglSetSceneViewportRect(float x0, float y0, float x1, float y1) {
    current_scene()->viewport_rect[0] = x0;
    current_scene()->viewport_rect[1] = y0;
    current_scene()->viewport_rect[2] = x1;
    current_scene()->viewport_rect[3] = y1;
    nglUpdateScene(1);
}

void XB_CDECL nglSetViewportRect(float x0, float y0, float x1, float y1) {
    NglScene* scene = current_scene();
    float width;
    float height;
    if (scene->render_target->flags & 4) {
        width = static_cast<float>(nglGetScreenWidth());
        height = static_cast<float>(nglGetScreenHeight());
    } else {
        // Unsigned to float as MSVC does it: signed conversion, plus 2^32 when negative.
        const int32_t w = static_cast<int32_t>(scene->render_target->width);
        width = static_cast<float>(w);
        if (w < 0) width += 4294967296.0f;
        const int32_t h = static_cast<int32_t>(scene->render_target->height);
        height = static_cast<float>(h);
        if (h < 0) height += 4294967296.0f;
    }
    const float inv_width = 1.0f / width;
    const float nx0 = inv_width * x0 * 2.0f - 1.0f;
    scene->viewport_rect[0] = nx0;
    const float inv_height = 1.0f / height;
    const float ny0 = inv_height * y0 * 2.0f - 1.0f;
    current_scene()->viewport_rect[1] = ny0;
    const float nx1 = (x1 + 1.0f) * inv_width * 2.0f - 1.0f;
    current_scene()->viewport_rect[2] = nx1;
    const float ny1 = (y1 + 1.0f) * inv_height * 2.0f - 1.0f;
    current_scene()->viewport_rect[3] = ny1;
    nglUpdateScene(1);
    nglSetViewportNormalized(nx0, ny0, nx1, ny1);
}

namespace {

float clamp_unit(float x) { return -1.0f > x ? -1.0f : (x > 1.0f ? 1.0f : x); }

// Unsigned to float as MSVC does it: signed conversion, plus 2^32 when negative.
float unsigned_to_float(uint32_t value) {
    const int32_t v = static_cast<int32_t>(value);
    float f = static_cast<float>(v);
    if (v < 0) f += 4294967296.0f;
    return f;
}

}  // namespace

void XB_CDECL nglSetViewportNormalized(float x0, float y0, float x1, float y1) {
    current_scene()->viewport_clamped[0] = clamp_unit(x0);
    current_scene()->viewport_clamped[1] = clamp_unit(y0);
    current_scene()->viewport_clamped[2] = clamp_unit(x1);
    current_scene()->viewport_clamped[3] = clamp_unit(y1);
    float width;
    float height;
    if (current_scene()->render_target->flags & 4) {
        width = static_cast<float>(nglGetScreenWidth());
        height = static_cast<float>(nglGetScreenHeight());
    } else {
        width = unsigned_to_float(current_scene()->render_target->width);
        height = unsigned_to_float(current_scene()->render_target->height);
    }
    // The unclamped values, rounded half up by truncation (CVTTSS2SI).
    NglScene* scene = current_scene();
    scene->viewport_pixels[0] = static_cast<int32_t>((x0 + 1.0f) * 0.5f * width + 0.5f);
    scene->viewport_pixels[2] = static_cast<int32_t>((x1 + 1.0f) * 0.5f * width + 0.5f);
    scene->viewport_pixels[1] = static_cast<int32_t>((y0 + 1.0f) * 0.5f * height + 0.5f);
    scene->viewport_pixels[3] = static_cast<int32_t>((y1 + 1.0f) * 0.5f * height + 0.5f);
    nglUpdateScene(1);
}

void XB_CDECL nglSetAspect(float aspect) {
    current_scene()->aspect = aspect;
    nglUpdateScene(1);
}

void XB_CDECL nglSetPerspective(float fov, float near_z, float far_z) {
    current_scene()->projection_kind = 1;
    current_scene()->fov = fov;
    current_scene()->near_z = near_z;
    current_scene()->far_z = far_z;
    nglUpdateScene(1);
}

void XB_CDECL nglSetOrthographic(float near_z, float far_z) {
    current_scene()->projection_kind = 0;
    current_scene()->fov = 0.0f;
    current_scene()->near_z = near_z;
    current_scene()->far_z = far_z;
    nglUpdateScene(1);
}

void XB_CDECL nglSetViewMatrix(const float* matrix) {
    std::memcpy(current_scene()->view_matrix, matrix, sizeof(current_scene()->view_matrix));
    nglUpdateScene(1);
}

void XB_CDECL nglSetCameraMatrix(const float* camera) {
    // The view matrix is the camera's inverse for an orthonormal rotation: rows 0..2
    // transposed, translation -t * R^T. As in the original (SHUFPS), each row's fourth
    // lane is a copy of its third, and the translation row computes all four lanes.
    const float* r0 = camera;
    const float* r1 = camera + 4;
    const float* r2 = camera + 8;
    const float* t = camera + 12;
    float view[16] = {
        r0[0], r1[0], r2[0], r2[0],
        r0[1], r1[1], r2[1], r2[1],
        r0[2], r1[2], r2[2], r2[2],
    };
    const float tx = -t[0];
    const float ty = -t[1];
    const float tz = -t[2];
    for (int i = 0; i < 4; ++i) view[12 + i] = view[i] * tx + view[4 + i] * ty + view[8 + i] * tz;
    std::memcpy(current_scene()->view_matrix, view, sizeof(view));
    nglUpdateScene(1);
}

const float* XB_CDECL nglGetViewMatrix() {
    if (current_scene()->stale) nglUpdateScene(0);
    return current_scene()->matrix0d0;
}

void XB_CDECL nglListAddQuad(const NglQuad* quad) {
    if (!quad) return;
    struct Node {
        uint32_t vtable;
        Node* next;
        uint32_t key;
        NglQuad quad;
    };
    Node* node = alloc_scene_memory<Node>(0x70);
    if (!node) return;
    node->vtable = 0x007bc410;
    if (current_scene()->stale) nglUpdateScene(0);
    std::memcpy(&node->quad, quad, sizeof(NglQuad));
    uint8_t* scene = reinterpret_cast<uint8_t*>(current_scene());
    if ((1u << (quad->blend_mode & 31)) & 3) {
        node->key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(quad->texture));
        node->next = *reinterpret_cast<Node**>(scene + 0x304);
        *reinterpret_cast<Node**>(scene + 0x304) = node;
        ++*reinterpret_cast<uint32_t*>(scene + 0x30c);
    } else {
        std::memcpy(&node->key, &quad->z, 4);
        node->next = *reinterpret_cast<Node**>(scene + 0x308);
        *reinterpret_cast<Node**>(scene + 0x308) = node;
        ++*reinterpret_cast<uint32_t*>(scene + 0x310);
    }
}

float* XB_CDECL nglGetMatrix(float* out, int32_t which) {
    if (current_scene()->stale) nglUpdateScene(0);
    const NglScene* scene = current_scene();
    auto affine = [out](const float* m) {
        for (int row = 0; row < 4; ++row) {
            out[row * 4 + 0] = m[row * 4 + 0];
            out[row * 4 + 1] = m[row * 4 + 1];
            out[row * 4 + 2] = m[row * 4 + 2];
            out[row * 4 + 3] = row == 3 ? 1.0f : 0.0f;
        }
    };
    switch (which) {
    case 0:
        affine(scene->matrix0d0);
        break;
    case 1:
        std::memcpy(out, scene->projection, 64);
        break;
    case 2:
        affine(scene->view_matrix);
        break;
    case 3:
        std::memcpy(out, reinterpret_cast<const uint8_t*>(scene) + 0x190, 64);
        break;
    default:
        for (int i = 0; i < 16; ++i) out[i] = i % 5 == 0 ? 1.0f : 0.0f;
        break;
    }
    return out;
}

float* XB_CDECL nglProjectPoint(float* out, const float* point) {
    nglUpdateScene(0);
    const NglScene* scene = current_scene();
    // Row-vector products in packed lanes, in the original's order of additions.
    const float* v = scene->view_matrix;
    Lanes eye = add(lanes(v + 12), mul(lanes(v), point[0]));
    eye = add(eye, mul(lanes(v + 4), point[1]));
    eye = add(eye, mul(lanes(v + 8), point[2]));
    const float* b = scene->base_projection;
    Lanes clip = add(lanes(b + 12), mul(lanes(b), eye.v[0]));
    clip = add(clip, mul(lanes(b + 4), eye.v[1]));
    clip = add(clip, mul(lanes(b + 8), eye.v[2]));
    const float* vp = scene->viewport;
    Lanes screen = add(mul(lanes(vp), clip.v[0]), mul(lanes(vp + 4), clip.v[1]));
    screen = add(screen, mul(lanes(vp + 8), clip.v[2]));
    screen = add(screen, mul(lanes(vp + 12), clip.v[3]));
    // (x, y, z, clip w) divided by clip w in all four lanes (DIVPS), then mapped to
    // pixels; lane 2 is scaled by 0 and dropped, but its flags count (MXCSR).
    const float w = clip.v[3];
    Lanes ndc = {{screen.v[0] / w, screen.v[1] / w, screen.v[2] / w, w / w}};
    float width;
    float height;
    if (scene->render_target->flags & 4) {
        height = static_cast<float>(nglGetScreenHeight());
        width = static_cast<float>(nglGetScreenWidth());
    } else {
        width = unsigned_to_float(scene->render_target->width);
        height = unsigned_to_float(scene->render_target->height);
    }
    Lanes pixels;
    const float scale[4] = {width, height, 0.0f, width};
    for (int i = 0; i < 4; ++i) pixels.v[i] = (ndc.v[i] * 0.5f + 0.5f) * scale[i];
    platform_keep(pixels);
    out[0] = pixels.v[0];
    out[1] = pixels.v[1];
    out[2] = eye.v[2];
    out[3] = pixels.v[3];
    return out;
}

float* XB_CDECL nglUnprojectPoint(float* out, const float* point) {
    nglUpdateScene(0);
    const NglScene* scene = current_scene();
    const Lanes xy = {{point[0], point[1], 0.0f, 0.0f}};
    // The depth through the projection's z and w rows, all four lanes (MXCSR flags).
    const Lanes t = sub(mul(neg(lanes(point)), lanes(scene->base_projection + 8)), lanes(scene->base_projection + 12));
    // w: the point's z for a perspective projection, 1 otherwise.
    const float w = scene->projection_kind == 1 ? point[2] : 1.0f;
    const Lanes clip_w = {{w, w, t.v[2], 0.0f - w}};
    Lanes size = {{0.0f, 0.0f, 1.0f, 1.0f}};
    if (scene->render_target->flags & 4) {
        size.v[1] = static_cast<float>(nglGetScreenHeight());
        size.v[0] = static_cast<float>(nglGetScreenWidth());
    } else {
        size.v[1] = platform_x87_unsigned_to_float(scene->render_target->height);
        size.v[0] = platform_x87_unsigned_to_float(scene->render_target->width);
    }
    // 1 / size by RCPPS and one Newton step: r * (2 - r * size).
    Lanes r;
    for (int i = 0; i < 4; ++i) r.v[i] = platform_rcp_estimate(size.v[i]);
    const Lanes inverse = mul(sub(Lanes{{2.0f, 2.0f, 2.0f, 2.0f}}, mul(r, size)), r);
    const Lanes v = sub(mul(mul(clip_w, add(xy, xy)), inverse), clip_w);
    const float* m = scene->clip_to_screen;
    Lanes result = add(mul(lanes(m), v.v[0]), mul(lanes(m + 4), v.v[1]));
    result = add(result, mul(lanes(m + 8), v.v[2]));
    result = add(result, mul(lanes(m + 12), v.v[3]));
    std::memcpy(out, result.v, 16);
    return out;
}

void XB_CDECL nglGetMatrixCopy(float* out, int32_t which) {
    alignas(16) float local[16];
    std::memcpy(out, nglGetMatrix(local, which), 64);
}

void XB_CDECL nglProjectPointCopy(float* out, const float* point) {
    alignas(16) float local[4];
    std::memcpy(out, nglProjectPoint(local, point), 16);
}

bool XB_CDECL nglSphereInFrustum(const float* center, float radius) {
    const float* plane = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(current_scene()) + 0x270);
    for (int i = 0; i < 6; ++i, plane += 4) {
        // MULPS over all four lanes (centre w times plane d too; its flags count).
        Lanes product = {{center[0] * plane[0], center[1] * plane[1], center[2] * plane[2],
                          center[3] * plane[3]}};
        platform_keep(product);
        const float distance = product.v[0] + product.v[1] + product.v[2] - plane[3] + radius;
        if (0.0f > distance) return false;
    }
    return true;
}

void XB_CDECL nglAddPointLight(uint32_t flags, const float* position, float inner, float radius,
                               const float* color) {
    if (!nglSphereInFrustum(position, radius)) return;
    float* data = alloc_scene_memory<float>(0x30);
    if (!data) return;
    data[0] = position[0];
    data[1] = position[1];
    data[2] = position[2];
    data[3] = position[3];
    data[4] = color[0];
    data[5] = color[1];
    data[6] = color[2];
    data[7] = 1.0f;
    data[8] = inner;
    data[9] = radius;
    nglLightSetAddLight(0, data, flags);
}

void XB_CDECL nglAddDirectionalLight(uint32_t flags, const float* direction, const float* color) {
    float* data = alloc_scene_memory<float>(0x20);
    if (!data) return;
    data[0] = direction[0];
    data[1] = direction[1];
    data[2] = direction[2];
    data[3] = direction[3];
    data[4] = color[0];
    data[5] = color[1];
    data[6] = color[2];
    data[7] = 1.0f;
    nglLightSetAddLight(1, data, flags);
}

const float* XB_CDECL nglEvalLight(float* out, const NglLight* light, const float* position) {
    if (light->type == 0) {
        nglEvalPointLight(out, light->data, position);
        return out;
    }
    if (light->type == 1) return light->data;
    return nullptr;
}

const float* XB_CDECL nglEvalFirstObjectLight(float* out, const float* position) {
    NglLightSet* set = current_light_set();
    const NglLight* light = set->object_lights;
    if (light == reinterpret_cast<const NglLight*>(set)) {
        static const float kNone[8] = {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        std::memcpy(out, kNone, sizeof(kNone));
        return out;
    }
    return nglEvalLight(out, light, position);
}

namespace {

// The resource managers (textures, meshes, mesh files, fonts) are title C++ objects;
// their vtable slots are called as the original does.
struct NglResourceManager {
    void** vtable;
};

template <typename F>
F* manager_slot(NglResourceManager* manager, int slot) {
    return reinterpret_cast<F*>(manager->vtable[slot]);
}

using ManagerFind = void* XB_THISCALL(NglResourceManager*, const char*);
using ManagerRelease = void XB_THISCALL(NglResourceManager*, void*, uint32_t, uint32_t);

NglResourceManager* texture_manager() { return title_global<NglResourceManager*>(0x009025f0); }
NglResourceManager* mesh_manager() { return title_global<NglResourceManager*>(0x00902a80); }
NglResourceManager* mesh_file_manager() { return title_global<NglResourceManager*>(0x00902a84); }
NglResourceManager* font_manager() { return title_global<NglResourceManager*>(0x009028d0); }

}  // namespace

void* XB_CDECL nglFindTexture(const char* name) {
    return manager_slot<ManagerFind>(texture_manager(), 2)(texture_manager(), name);
}

void* XB_CDECL nglFindTextureAddRef(const char* name) {
    uint32_t* texture = static_cast<uint32_t*>(manager_slot<ManagerFind>(texture_manager(), 2)(texture_manager(), name));
    if (texture) {
        ++texture[0];
        return texture;
    }
    return manager_slot<ManagerFind>(texture_manager(), 7)(texture_manager(), name);
}

void XB_CDECL nglReleaseTexture(void* texture) {
    manager_slot<ManagerRelease>(texture_manager(), 8)(texture_manager(), texture, 0, 0);
}

void XB_CDECL nglFreeTexture(NglTexture* texture) {
    using ManagerRemove = void XB_THISCALL(NglResourceManager*, void*);
    using Free = void XB_CDECL(void*);
    if (!texture) return;
    const uint8_t kind = static_cast<uint8_t>(texture->flags);
    if (kind == 0x10) return;
    if (!nglTextureIsStale(texture)) {
        title_global<uint32_t>(0x00903c88) = 0;
        title_global<uint32_t>(0x00903c8c) = 0;
        title_global<uint32_t>(0x00903c90) = 0;
        title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
        nglUnbindTextures();
        title_function<void XB_CDECL()>(0x003e6200)();
    }
    // +0x4c is a count here.
    if (kind == 0x0f) {
        for (uint32_t i = 0; i < reinterpret_cast<uintptr_t>(texture->target); ++i) nglReleaseTexture(texture->frames[i]);
        title_function<Free>(0x000939b0)(texture->frames);  // Mem_AlignedFree
        title_function<Free>(0x000939b0)(texture);
        return;
    }
    if (texture->target) {
        for (uint32_t i = 0; i < reinterpret_cast<uintptr_t>(texture->target); ++i) {
            NglTexture* item = reinterpret_cast<NglTexture*>(reinterpret_cast<uint8_t*>(texture->frames) + i * 0x74);
            nglFreePalette(reinterpret_cast<uint32_t*>(item->palette));
            item->palette = nullptr;
            manager_slot<ManagerRemove>(texture_manager(), 4)(texture_manager(), item);
        }
        title_function<Free>(0x000939b0)(texture->frames);
    }
    if (texture->palette) {
        nglFreePalette(reinterpret_cast<uint32_t*>(texture->palette));
        texture->palette = nullptr;
    }
    const bool owns_memory = !((texture->flags >> 10) & 1);
    if (owns_memory) nglReleaseTextureSurfaces(texture);
    if (*reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(texture) + 0xc) & 2) {
        title_function<uint32_t XB_STDCALL(void*)>(0x003e7ef0)(texture->d3d_texture);  // D3DResource_Release
        texture->d3d_texture = nullptr;
    }
    if (!(*reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(texture) + 0x28) & 1) && owns_memory)
        nglReleaseTextureMemory(texture);
    nglTexture_FreeHeaders(texture);
    if (texture->companion) {
        manager_slot<ManagerRemove>(texture_manager(), 4)(texture_manager(), texture->companion);
        nglFreeTexture(texture->companion);
        texture->companion = nullptr;
    }
    title_function<Free>(0x000939b0)(texture);
}

void* XB_CDECL nglFindMesh(const char* name) {
    return manager_slot<ManagerFind>(mesh_manager(), 2)(mesh_manager(), name);
}

void XB_CDECL nglReleaseMeshFile(void* file) {
    manager_slot<ManagerRelease>(mesh_file_manager(), 8)(mesh_file_manager(), file, 0, 0);
}

void* XB_CDECL nglFindFontAddRef(const char* name) {
    uint8_t* font = static_cast<uint8_t*>(manager_slot<ManagerFind>(font_manager(), 2)(font_manager(), name));
    if (font) {
        ++*reinterpret_cast<uint32_t*>(font + 0x20);
        return font;
    }
    return manager_slot<ManagerFind>(font_manager(), 7)(font_manager(), name);
}

void XB_CDECL nglReleaseFont(void* font) {
    manager_slot<ManagerRelease>(font_manager(), 8)(font_manager(), font, 0, 0);
}

void XB_STDCALL nglMaterial_Relocate(uint8_t* material, uint32_t base) {
    uint32_t& texture = *reinterpret_cast<uint32_t*>(material + 0x18);
    if (texture) texture += base;
}

void XB_STDCALL nglMaterial_Relocate2(uint8_t* material, uint32_t base) {
    uint32_t& first = *reinterpret_cast<uint32_t*>(material + 0x18);
    if (first) first += base;
    uint32_t& second = *reinterpret_cast<uint32_t*>(material + 0x20);
    if (second) second += base;
}

void XB_CDECL nglListAddNode(NglNode* node) {
    uint32_t key[2];
    reinterpret_cast<void XB_THISCALL (*)(NglNode*, uint32_t*)>(node->vtable[1])(node, key);
    node->sort_key = key[1];
    if (key[0] == 1) {
        node->next = current_scene()->translucent_nodes;
        current_scene()->translucent_nodes = node;
        ++current_scene()->translucent_count;
    } else {
        node->next = current_scene()->opaque_nodes;
        current_scene()->opaque_nodes = node;
        ++current_scene()->opaque_count;
    }
}

float XB_THISCALL nglNode_ViewDepth(const NglMeshNode* node) {
    const float* m = node->world_matrix;
    const float* center = node->section->center;
    const Lanes world = add(add(add(lanes(m + 12), mul(lanes(m), center[0])), mul(lanes(m + 4), center[1])),
                            mul(lanes(m + 8), center[2]));
    platform_keep(world);
    const float* v = current_scene()->view_matrix;
    const Lanes view = add(add(add(lanes(v + 12), mul(lanes(v), world.v[0])), mul(lanes(v + 4), world.v[1])),
                           mul(lanes(v + 8), world.v[2]));
    platform_keep(view);
    return view.v[2] + node->section->radius;
}

float* XB_THISCALL nglNode_CalcWorldMatrix(const uint8_t* self, float* out) {
    const uint8_t* instance = *reinterpret_cast<uint8_t* const*>(self + 0x90);
    if (!(instance[0] & 2)) {
        std::memcpy(out, self, 64);
        return out;
    }
    // 1 / scale: the RCPPS estimate and one Newton step, all four lanes.
    const float* scale = reinterpret_cast<const float*>(instance + 0x10);
    Lanes inv;
    for (int i = 0; i < 4; ++i) {
        const float r = platform_rcp_estimate(scale[i]);
        inv.v[i] = (2.0f - r * scale[i]) * r;
    }
    platform_keep(inv);
    const float* m = reinterpret_cast<const float*>(self);
    float result[16];
    store(result, mul(lanes(m), inv.v[0]));
    store(result + 4, mul(lanes(m + 4), inv.v[1]));
    store(result + 8, mul(lanes(m + 8), inv.v[2]));
    std::memcpy(result + 12, m + 12, 16);
    std::memcpy(out, result, 64);
    return out;
}

float* XB_CDECL nglMatrixTargetScale(float* out, const NglRenderTarget* target, const float* scale) {
    float width;
    float height;
    if (target->flags & 4) {
        width = static_cast<float>(nglGetScreenWidth());
        height = static_cast<float>(nglGetScreenHeight());
    } else {
        // Unsigned to float as MSVC does it (on the x87 here, rounded to float).
        const int32_t w = static_cast<int32_t>(target->width);
        width = static_cast<float>(w);
        if (w < 0) width += 4294967296.0f;
        const int32_t h = static_cast<int32_t>(target->height);
        height = static_cast<float>(h);
        if (h < 0) height += 4294967296.0f;
    }
    const float sy = scale[1] * height * 0.5f;
    const float sx = scale[0] * width * 0.5f;
    const float m[16] = {
        sx, 0.0f, 0.0f, 0.0f,
        0.0f, sy, 0.0f, 0.0f,
        0.0f, 0.0f, 16777215.0f, 0.0f,
        scale[2] + sx, scale[2] + sy, 0.0f, 1.0f,
    };
    std::memcpy(out, m, 64);
    return out;
}

float* XB_CDECL nglGetSceneTargetScale(float* out) {
    const NglRenderTarget* target = current_scene()->render_target;
    float scale[4];
    return nglMatrixTargetScale(out, current_scene()->render_target, nglGetTargetSampleScale(scale, target));
}

void XB_CDECL nglAddShadowProjector(uint32_t flags, const float* matrix, const float* scale, uint32_t param_a,
                                    uint32_t param_b, uint32_t param_c) {
    float* p = alloc_scene_memory<float>(0x140);
    if (!p) return;
    uint32_t* words = reinterpret_cast<uint32_t*>(p);
    words[0x20] = param_a;  // +0x80
    words[0x21] = param_b;
    if (param_c) words[0x22] = param_c;

    const Lanes r0 = lanes(matrix);
    const Lanes r1 = lanes(matrix + 4);
    const Lanes r2 = lanes(matrix + 8);
    const Lanes r3 = lanes(matrix + 12);
    const Lanes s = lanes(scale);
    store(p + 0x24, s);   // +0x90
    store(p + 0x28, r3);  // +0xa0
    store(p + 0x2c, r0);  // +0xb0, the matrix again (+0xb0..+0xef)
    store(p + 0x30, r1);
    store(p + 0x34, r2);

    const Lanes half = {{0.5f, 0.5f, 0.0f, 0.5f}};
    const Lanes neg_half = neg(half);
    const Lanes neg_r3 = neg(r3);
    // The rotation transposed, lane 3 repeating lane 2 (SHUFPS).
    const Lanes c0 = {{r0.v[0], r1.v[0], r2.v[0], r2.v[0]}};
    const Lanes c1 = {{r0.v[1], r1.v[1], r2.v[1], r2.v[1]}};
    const Lanes c2 = {{r0.v[2], r1.v[2], r2.v[2], r2.v[2]}};
    // 1 / scale: RCPPS and one Newton step.
    Lanes inv;
    for (int i = 0; i < 4; ++i) {
        const float r = platform_rcp_estimate(s.v[i]);
        inv.v[i] = (2.0f - r * s.v[i]) * r;
    }

    // +0x00: world to projector texture space (inverse rotation over the scale, then
    // the half offset).
    store(p, mul(c0, inv));
    store(p + 4, mul(c1, inv));
    store(p + 8, mul(c2, inv));
    {
        Lanes t = add(add(mul(c0, neg_r3.v[0]), mul(c1, neg_r3.v[1])), mul(c2, neg_r3.v[2]));
        store(p + 12, add(mul(t, inv), half));
    }

    // +0x40: the projector box, edges = rows scaled, corner = r3 - half extent.
    store(p + 0x10, mul(r0, s.v[0]));
    store(p + 0x14, mul(r1, s.v[1]));
    store(p + 0x18, mul(r2, s.v[2]));
    {
        const Lanes t = mul(neg_half, s);
        platform_keep(t);
        store(p + 0x1c, add(add(add(mul(r0, t.v[0]), mul(r1, t.v[1])), mul(r2, t.v[2])), r3));
    }

    nglBuildProjectorPlanes(p + 0x38, p + 0x10);
    nglLightSetAddProjector(2, p, flags);
}

int32_t XB_CDECL nglListAddMesh(uint8_t* mesh, const float* matrix, const uint32_t* params, const uint32_t* extra) {
    if (!mesh) return -1;
    const uint32_t flags = params ? params[0] : 0;
    float scale = 1.0f;
    float radius = *reinterpret_cast<const float*>(mesh + 0x30);
    if (flags & 2) {
        matrix = nglScaleMeshMatrix(matrix, reinterpret_cast<const uint8_t*>(params), &scale);
        radius = scale * radius;
    }
    // The bounding sphere's centre in world space (all four lanes, as packed).
    const float* c = reinterpret_cast<const float*>(mesh + 0x20);
    alignas(16) float center[4];
    store(center, add(add(add(lanes(matrix + 12), mul(lanes(matrix), c[0])), mul(lanes(matrix + 4), c[1])),
                      mul(lanes(matrix + 8), c[2])));
    if (current_scene()->stale) nglUpdateScene(0);
    if (*reinterpret_cast<const uint32_t*>(mesh + 0x18))
        mesh = nglSelectMeshLod(mesh, static_cast<int8_t>(flags), reinterpret_cast<const uint8_t*>(params), center);

    uintptr_t& arena = title_global<uintptr_t>(0x009025a8);
    const uintptr_t arena_start = arena;
    auto* node = static_cast<NglMeshDrawNode*>(alloc_frame(arena, sizeof(NglMeshDrawNode), 64));
    node->mesh = mesh;
    std::memcpy(node->world, matrix, 64);

    // world * view_projection (row vectors), stored transposed.
    const float* vp = current_scene()->view_projection;
    const Lanes s0 = lanes(vp), s1 = lanes(vp + 4), s2 = lanes(vp + 8), s3 = lanes(vp + 12);
    Lanes r[4];
    for (int i = 0; i < 3; ++i) {
        const float* m = matrix + i * 4;
        r[i] = add(add(mul(s0, m[0]), mul(s1, m[1])), mul(s2, m[2]));
    }
    r[3] = add(add(add(s3, mul(s0, matrix[12])), mul(s1, matrix[13])), mul(s2, matrix[14]));
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) node->world_view_projection[i * 4 + j] = r[j].v[i];
    }
    node->world_inverse = nullptr;
    node->scaled_world_inverse = nullptr;
    node->scale = scale;
    if (!params) {
        node->params = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(0x00902a90));
    } else if (static_cast<int32_t>(flags) < 0) {
        node->params = params;  // flag bit 31: the caller's block lives through the frame
    } else {
        auto* copy = static_cast<uint32_t*>(alloc_frame(arena, 0x30, 16));
        std::memcpy(copy, params, 0x30);
        node->params = copy;
    }
    node->extra = extra ? extra[0] : 0x009028f8;

    if (!(flags & 0x40) && !nglSphereInFrustum(center, radius)) {
        arena = arena_start;
        return 0;
    }
    for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(mesh + 0x8); ++i) {
        const NglMeshSectionEntry* entries = *reinterpret_cast<NglMeshSectionEntry* const*>(mesh + 0xc);
        uint8_t* section = reinterpret_cast<uint8_t*>(entries[i].section);
        title_global<uint32_t>(0x00902bec) += *reinterpret_cast<const uint32_t*>(section + 0x40);
        uint8_t* material = *reinterpret_cast<uint8_t**>(section + 4);
        void* shader = *reinterpret_cast<void**>(material + 4);
        using AddSection = void XB_THISCALL (*)(void*, NglMeshDrawNode*, uint8_t*, uint8_t*);
        reinterpret_cast<AddSection>((*static_cast<void***>(shader))[2])(shader, node, section, material);
    }
    title_global<uint32_t>(0x00902be8) += *reinterpret_cast<const uint32_t*>(mesh + 0x3c);
    if (uint8_t* owner = *reinterpret_cast<uint8_t**>(mesh + 0x34))
        *reinterpret_cast<int32_t*>(owner + 0x144) = title_global<int32_t>(0x009028e0);
    return 1;
}

float* XB_THISCALL nglNode_GetWorldInverse(NglMeshDrawNode* self, float* out) {
    if (!self->world_inverse) {
        self->world_inverse = static_cast<float*>(alloc_frame(title_global<uintptr_t>(0x009025a8), 64, 64));
        float world[16];
        const float* m = nglNode_CalcWorldMatrix(reinterpret_cast<const uint8_t*>(self), world);
        // The rotation transposed, lane 3 repeating lane 2 (SHUFPS), then -t * R^T.
        const Lanes c0 = {{m[0], m[4], m[8], m[8]}};
        const Lanes c1 = {{m[1], m[5], m[9], m[9]}};
        const Lanes c2 = {{m[2], m[6], m[10], m[10]}};
        const Lanes t = neg(lanes(m + 12));
        float* inverse = self->world_inverse;
        store(inverse, c0);
        store(inverse + 4, c1);
        store(inverse + 8, c2);
        store(inverse + 12, add(add(mul(c0, t.v[0]), mul(c1, t.v[1])), mul(c2, t.v[2])));
    }
    std::memcpy(out, self->world_inverse, 64);
    return out;
}

float* XB_THISCALL nglNode_GetScaledWorldInverse(NglMeshDrawNode* self, float* out) {
    if (!self->scaled_world_inverse) {
        self->scaled_world_inverse = static_cast<float*>(alloc_frame(title_global<uintptr_t>(0x009025a8), 64, 64));
        const float* m = self->world;
        const Lanes c0 = {{m[0], m[4], m[8], m[8]}};
        const Lanes c1 = {{m[1], m[5], m[9], m[9]}};
        const Lanes c2 = {{m[2], m[6], m[10], m[10]}};
        const Lanes t = neg(lanes(m + 12));
        const Lanes p = add(add(mul(c0, t.v[0]), mul(c1, t.v[1])), mul(c2, t.v[2]));
        float* inverse = self->scaled_world_inverse;
        if (*reinterpret_cast<const uint8_t*>(self->params) & 2) {
            // 1 / scale per lane: RCPPS and one Newton step (2.0 from 0x007b6710).
            const Lanes scale = lanes(reinterpret_cast<const float*>(self->params + 4));
            Lanes estimate;
            for (int i = 0; i < 4; ++i) estimate.v[i] = platform_rcp_estimate(scale.v[i]);
            const Lanes reciprocal = mul(sub(lanes(&title_global<float>(0x007b6710)), mul(estimate, scale)), estimate);
            const Lanes squared = mul(reciprocal, reciprocal);
            store(inverse, mul(c0, squared));
            store(inverse + 4, mul(c1, squared));
            store(inverse + 8, mul(c2, squared));
            store(inverse + 12, mul(p, squared));
        } else {
            store(inverse, c0);
            store(inverse + 4, c1);
            store(inverse + 8, c2);
            store(inverse + 12, p);
        }
    }
    std::memcpy(out, self->scaled_world_inverse, 64);
    return out;
}

void XB_CDECL nglSelectMeshLightSet(NglMeshDrawNode* node) {
    const uint32_t slot = title_global<uint32_t>(0x00902914);
    const uint32_t* params = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(node->extra));
    // The 64-bit slot mask, the bit as a sign-extended 32-bit shift (cdq).
    const auto has_slot = [params, slot] {
        const uint32_t low = 1u << (slot & 31);
        const uint32_t high = static_cast<int32_t>(low) < 0 ? 0xffffffff : 0;
        return (params[0] & low) == low && (params[1] & high) == high;
    };
    NglLightSet* set;
    if (has_slot()) {
        // The title tests the mask twice; the second test's failure (impossible) would
        // read an uninitialized stack slot.
        set = has_slot() ? reinterpret_cast<NglLightSet*>(static_cast<uintptr_t>(params[2 + slot])) : nullptr;
    } else {
        set = current_scene()->light_set;
    }
    current_light_set() = set;
    NglLight* const end = reinterpret_cast<NglLight*>(set);
    set->object_lights = end;
    const uint8_t* mesh = node->mesh;
    const int32_t list = static_cast<int32_t>(mesh[7]) - 1;
    if (list == -1) return;
    const float* local = reinterpret_cast<const float*>(mesh + 0x20);
    Lanes center = add(lanes(node->world + 12), mul(lanes(node->world), local[0]));
    center = add(center, mul(lanes(node->world + 4), local[1]));
    center = add(center, mul(lanes(node->world + 8), local[2]));
    const float mesh_radius = *reinterpret_cast<const float*>(mesh + 0x30);
    // The list links sit at word `list` of the set and of each light (past the eight
    // lists too, as in the original).
    NglLight* light = reinterpret_cast<NglLight* const*>(set)[list];
    while (light != end) {
        bool add_light = false;
        if (light->type == 0) {
            const float radius = light->data[9] + mesh_radius;
            const Lanes d = sub(lanes(light->data), center);
            const Lanes sq = mul(d, d);
            platform_keep(sq);
            float dist = sq.v[0] + sq.v[1];
            dist = dist + sq.v[2];
            add_light = radius * radius >= dist;
        } else if (light->type == 1) {
            add_light = true;
        }
        if (add_light) {
            light->next_object = set->object_lights;
            set->object_lights = light;
        }
        light = reinterpret_cast<NglLight* const*>(light)[list];
    }
}

void XB_CDECL nglComputeObjectLights(NglMeshDrawNode* node, float* directions, float* colors) {
    const float* m = node->world;
    const float* c = reinterpret_cast<const float*>(node->mesh + 0x20);
    alignas(16) float center[4];
    store(center, add(add(add(lanes(m + 12), mul(lanes(m), c[0])), mul(lanes(m + 4), c[1])), mul(lanes(m + 8), c[2])));
    NglLightSet* set = current_light_set();
    int count = 0;
    for (const NglLight* light = set->object_lights; light && light != reinterpret_cast<const NglLight*>(set);) {
        alignas(16) float evaluated[8];
        const float* data = nglEvalLight(evaluated, light, center);
        const Lanes toward = neg(lanes(data));
        float inverse[16];
        const float* w = nglNode_GetWorldInverse(node, inverse);
        // The light direction in object space; lane 3 replaced by 0.
        const Lanes d = add(add(mul(lanes(w), toward.v[0]), mul(lanes(w + 4), toward.v[1])),
                            mul(lanes(w + 8), toward.v[2]));
        platform_keep(d);
        directions[count * 4 + 0] = d.v[0];
        directions[count * 4 + 1] = d.v[1];
        directions[count * 4 + 2] = d.v[2];
        directions[count * 4 + 3] = 0.0f;
        colors[count] = data[4];
        colors[4 + count] = data[5];
        colors[8 + count] = data[6];
        if (++count == 4) break;
        light = light->next_object;
    }
    for (; count < 4; ++count) {
        std::memset(directions + count * 4, 0, 16);
        colors[count] = 0.0f;
        colors[4 + count] = 0.0f;
        colors[8 + count] = 0.0f;
    }
    std::memcpy(colors + 12, set->ambient, 16);
}

void XB_THISCALL nglSetDecalZBias(uint8_t* self, uint32_t bias) {
    using SetRenderStateSimple = void XB_FASTCALL(uint32_t, uint32_t);
    const float b = platform_x87_unsigned_to_float(bias);
    float& cached_bias = *reinterpret_cast<float*>(self + 0x80);
    float& cached_factor = *reinterpret_cast<float*>(self + 0x84);
    const float offset_bias = 0.0f - b;
    if (!(cached_bias == offset_bias)) {
        uint32_t bits;
        std::memcpy(&bits, &offset_bias, 4);
        title_function<SetRenderStateSimple>(0x003e89f0)(0x00040388, bits);  // polygon offset bias
        cached_bias = offset_bias;
    }
    const float factor = b * -0.25f;
    if (!(cached_factor == factor)) {
        uint32_t bits;
        std::memcpy(&bits, &factor, 4);
        title_function<SetRenderStateSimple>(0x003e89f0)(0x00040384, bits);  // polygon offset factor
        cached_factor = factor;
    }
}

void* XB_CDECL nglMaterialFindTexture(const char* name) {
    if (*reinterpret_cast<const uint32_t*>(name) == 0) return nullptr;
    return nglFindTextureAddRef(name);
}

void* XB_CDECL nglMaterialFindTextureOrDefault(const char* name) {
    void* texture = *reinterpret_cast<const uint32_t*>(name) ? nglFindTextureAddRef(name) : nullptr;
    return texture == title_global<void*>(0x009026f8) ? nullptr : texture;
}

namespace {

// Material fields: texture names (32 bytes each, offsets to them once relocated) at
// +0x18 / +0x20, the textures found for them at +0x1c / +0x24.
const char*& material_name(uint8_t* material, uint32_t offset) {
    return *reinterpret_cast<const char**>(material + offset);
}
void*& material_texture(uint8_t* material, uint32_t offset) { return *reinterpret_cast<void**>(material + offset); }

}  // namespace

void XB_STDCALL nglMaterial_LoadTextureRef(uint8_t* material) {
    material_texture(material, 0x1c) = nglFindTextureAddRef(material_name(material, 0x18));
}

void XB_STDCALL nglMaterial_ReleaseTexture(uint8_t* material) {
    nglReleaseTexture(material_texture(material, 0x1c));
    material_texture(material, 0x1c) = nullptr;
}

void XB_STDCALL nglMaterial_ReleaseTwoTextures(uint8_t* material) {
    nglReleaseTexture(material_texture(material, 0x1c));
    nglReleaseTexture(material_texture(material, 0x24));
}

void XB_STDCALL nglMaterial_LoadBaseAndOptional(uint8_t* material) {
    material_texture(material, 0x1c) = nglMaterialFindTexture(material_name(material, 0x18));
    void* second = nglMaterialFindTextureOrDefault(material_name(material, 0x20));
    material_texture(material, 0x24) = second;
    if (!second || second == title_global<void*>(0x009026f8)) material_texture(material, 0x24) = nullptr;
}

void XB_STDCALL nglMaterial_ReleaseBaseAndOptional(uint8_t* material) {
    nglReleaseTexture(material_texture(material, 0x1c));
    if (void* second = material_texture(material, 0x24)) nglReleaseTexture(second);
}

void XB_STDCALL nglMaterial_LoadTexture(uint8_t* material) {
    material_texture(material, 0x1c) = nglMaterialFindTexture(material_name(material, 0x18));
}

void XB_STDCALL nglMaterial_LoadTwoTextures(uint8_t* material) {
    material_texture(material, 0x1c) = nglMaterialFindTexture(material_name(material, 0x18));
    material_texture(material, 0x24) = nglMaterialFindTexture(material_name(material, 0x20));
}

void XB_STDCALL cdWorldBlend_LoadTextures(uint8_t* material) {
    material_texture(material, 0x1c) = nglMaterialFindTexture(material_name(material, 0x18));
    material_texture(material, 0x24) = nglMaterialFindTexture(material_name(material, 0x20));
    void* third = nglMaterialFindTextureOrDefault(material_name(material, 0x28));
    material_texture(material, 0x2c) = third;
    if (!third || third == title_global<void*>(0x009026f8)) material_texture(material, 0x2c) = nullptr;
}

void XB_STDCALL cdWorldBlend_ReleaseTextures(uint8_t* material) {
    nglReleaseTexture(material_texture(material, 0x1c));
    nglReleaseTexture(material_texture(material, 0x24));
    if (void* third = material_texture(material, 0x2c)) nglReleaseTexture(third);
}

void XB_STDCALL cdGlass_LoadTextures(uint8_t* material) {
    material_texture(material, 0x1c) = nglMaterialFindTexture(material_name(material, 0x18));
    void* second = nglMaterialFindTexture(material_name(material, 0x20));
    material_texture(material, 0x24) = second;
    if (second && second != title_global<void*>(0x009026f8)) *reinterpret_cast<uint32_t*>(material + 0x30) |= 2;
}

void XB_STDCALL nglMaterial_ReleaseTwoTexturesClear(uint8_t* material) {
    nglReleaseTexture(material_texture(material, 0x1c));
    material_texture(material, 0x1c) = nullptr;
    nglReleaseTexture(material_texture(material, 0x24));
    material_texture(material, 0x24) = nullptr;
}

namespace {

// The D3D functions the state code calls (replaced by the port's own in route B).
void set_render_state_simple(uint32_t method, uint32_t value) {
    title_function<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(method, value);
}

uint32_t address_of(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

template <typename T>
void set_cached(uint8_t* self, uint32_t offset, uint32_t method, T value) {
    T& cached = *reinterpret_cast<T*>(self + offset);
    if (cached == value) return;
    set_render_state_simple(method, value);
    cached = value;
}

}  // namespace

void XB_THISCALL nglStateCache_SetAlphaTestEnable(uint8_t* self, uint8_t enable) {
    set_cached<uint8_t>(self, 0x50, 0x00040300, enable);
}

void XB_THISCALL nglStateCache_SetFogEnable(uint8_t* self, uint8_t enable) {
    if (self[0x8b] == enable) return;
    title_global<uint32_t>(0x003f8320) |= 0x2000;
    title_global<uint32_t>(0x003f8498) = enable;
    self[0x8b] = enable;
}

void XB_THISCALL nglStateCache_SetAlphaFunc(uint8_t* self, uint32_t func) {
    set_cached<uint32_t>(self, 0x54, 0x0004033c, func);
}

void XB_THISCALL nglStateCache_SetAlphaRef(uint8_t* self, uint32_t ref) {
    set_cached<uint32_t>(self, 0x58, 0x00040340, ref);
}

void XB_THISCALL nglStateCache_SetBlendEnable(uint8_t* self, uint8_t enable) {
    set_cached<uint8_t>(self, 0x5c, 0x00040304, enable);
}

void XB_THISCALL nglStateCache_SetSrcBlend(uint8_t* self, uint32_t factor) {
    set_cached<uint32_t>(self, 0x60, 0x00040344, factor);
}

void XB_THISCALL nglStateCache_SetDestBlend(uint8_t* self, uint32_t factor) {
    set_cached<uint32_t>(self, 0x64, 0x00040348, factor);
}

void XB_THISCALL nglStateCache_SetBlendOp(uint8_t* self, uint32_t op) {
    set_cached<uint32_t>(self, 0x70, 0x00040350, op);
}

void XB_THISCALL nglStateCache_SetBlendColor(uint8_t* self, uint32_t color) {
    uint32_t& cached = *reinterpret_cast<uint32_t*>(self + 0x6c);
    if (cached == color) return;
    set_render_state_simple(0x0004034c, color);
    title_global<uint32_t>(0x003f8454) = color;  // D3D's D3DRS_BLENDCOLOR
    cached = color;
}

void XB_THISCALL nglSetBlendMode(uint8_t* self, uint32_t mode, uint32_t alpha, uint32_t alpha_ref) {
    uint32_t& cached_mode = *reinterpret_cast<uint32_t*>(self + 0xe8);
    uint32_t& cached_alpha = *reinterpret_cast<uint32_t*>(self + 0xec);
    if (cached_mode == mode && cached_alpha == alpha && mode != 1) return;
    constexpr uint32_t kGreaterEqual = 0x204, kAdd = 0x8006, kReverseSubtract = 0x800b;
    constexpr uint32_t kSrcAlpha = 0x302, kInvSrcAlpha = 0x303, kOne = 1, kZero = 0;
    constexpr uint32_t kConstantAlpha = 0x8003, kInvConstantAlpha = 0x8004;
    // Every blending mode: alpha test on (>= 0), blending on.
    const auto blending = [self] {
        nglStateCache_SetAlphaTestEnable(self, 1);
        nglStateCache_SetAlphaFunc(self, kGreaterEqual);
        nglStateCache_SetAlphaRef(self, 0);
        nglStateCache_SetBlendEnable(self, 1);
    };
    const auto blend = [self](uint32_t op, uint32_t src, uint32_t dst) {
        nglStateCache_SetBlendOp(self, op);
        nglStateCache_SetSrcBlend(self, src);
        nglStateCache_SetDestBlend(self, dst);
    };
    switch (mode) {
    case 0:
        nglStateCache_SetAlphaTestEnable(self, 0);
        nglStateCache_SetBlendEnable(self, 0);
        break;
    case 1:
        nglStateCache_SetAlphaTestEnable(self, 1);
        nglStateCache_SetAlphaFunc(self, kGreaterEqual);
        nglStateCache_SetAlphaRef(self, alpha_ref);
        nglStateCache_SetBlendEnable(self, 0);
        break;
    case 2:
        blending();
        blend(kAdd, kSrcAlpha, kInvSrcAlpha);
        break;
    case 3:
        blending();
        blend(kAdd, kSrcAlpha, kOne);
        break;
    case 4:
        blending();
        blend(kReverseSubtract, kSrcAlpha, kOne);
        break;
    case 5:
        blending();
        blend(kAdd, kConstantAlpha, kInvConstantAlpha);
        nglStateCache_SetBlendColor(self, alpha << 24);
        break;
    case 6:
        blending();
        blend(kAdd, kConstantAlpha, kOne);
        nglStateCache_SetBlendColor(self, alpha << 24);
        break;
    case 7:
        blending();
        blend(kReverseSubtract, kConstantAlpha, kOne);
        nglStateCache_SetBlendColor(self, alpha << 24);
        break;
    case 8:
        blending();
        blend(kAdd, kZero, kSrcAlpha);
        break;
    default:
        break;
    }
    *reinterpret_cast<uint32_t*>(self + 0xf0) = alpha_ref;
    cached_mode = mode;
    cached_alpha = alpha;
}

void XB_CDECL nglSetPixelShaderProgramCached(const NglShaderRef* shader) {
    uint32_t& cached = title_global<uint32_t>(0x00903c10);
    if (shader->program == cached) return;
    title_function<void XB_STDCALL(uint32_t)>(0x003e77e0)(shader->program);  // SetPixelShaderProgram
    cached = shader->program;
}

void XB_CDECL nglSetVertexShaderCached(const NglShaderRef* shader) {
    using LoadProgram = void XB_STDCALL(uint32_t, uint32_t);
    using SelectDirect = void XB_STDCALL(uint32_t, uint32_t);
    uint32_t& cached_program = title_global<uint32_t>(0x00903c08);
    uint32_t& cached_format = title_global<uint32_t>(0x00903c0c);
    if (shader->program == cached_program && shader->format == cached_format) return;
    title_function<LoadProgram>(0x003e6ea0)(shader->program, 0);  // LoadVertexShaderProgram
    if (shader->format == cached_format) {
        title_function<SelectDirect>(0x003e6f40)(0, 0);  // SelectVertexShaderDirect
    } else {
        title_function<SelectDirect>(0x003e6f40)(shader->format, 0);
        cached_format = shader->format;
    }
    cached_program = shader->program;
}

void XB_CDECL nglSetPaletteCached(uint32_t stage, uint8_t* palette) {
    uint8_t*& cached = title_global<uint8_t* [4]>(0x00903d48)[stage];
    if (palette == cached) return;
    title_function<void XB_STDCALL(uint32_t, uint32_t)>(0x003e6160)(stage, *reinterpret_cast<uint32_t*>(palette + 0xc));
    cached = palette;
}

void XB_CDECL nglUnbindTextures() {
    using SetStageObject = void XB_STDCALL(uint32_t, uint32_t);
    for (uint32_t stage = 0; stage < 4; ++stage) title_function<SetStageObject>(0x003e6160)(stage, 0);  // SetPalette
    uint32_t* palettes = title_global<uint32_t[4]>(0x00903d48);
    for (uint32_t stage = 0; stage < 4; ++stage) palettes[stage] = 0;
    for (uint32_t stage = 0; stage < 4; ++stage) title_function<SetStageObject>(0x003e5f70)(stage, 0);  // SetTexture
    uint32_t* textures = title_global<uint32_t[4]>(0x008a2e60);
    uint32_t* formats = title_global<uint32_t[4]>(0x008a2e70);
    uint32_t* address_modes = title_global<uint32_t[4]>(0x008a2e80);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        textures[stage] = 0;
        formats[stage] = 0xffffffff;
        address_modes[stage] = 0xffffffff;
    }
}

void XB_CDECL nglSetTexture(uint32_t stage, NglTexture* texture, uint8_t address_flags, uint32_t border) {
    NglTexture* t = texture;
    if (static_cast<uint8_t>(texture->flags) == 0x0f) {
        const uint32_t frame_count = address_of(texture->target);
        t = texture->frames[title_global<uint32_t>(0x009026fc) % frame_count];
    }
    if (static_cast<uint8_t>(t->flags) == 0x10) t = t->target;
    t->last_used_frame = title_global<int32_t>(0x009028e0);
    uint32_t* textures = title_global<uint32_t[4]>(0x008a2e60);
    uint32_t* formats = title_global<uint32_t[4]>(0x008a2e70);
    if (textures[stage] != address_of(t->d3d_texture)) {
        const uint32_t format = t->flags & 0xf0000f00;
        const uint8_t* d3d = t->d3d_texture;
        if ((t->flags & 0x100) && format == formats[stage]) {
            // Same format: only the texture's data and format words change.
            using SwitchTexture = void XB_FASTCALL(uint32_t, uint32_t, uint32_t);
            title_function<SwitchTexture>(0x003e6130)(title_global<uint32_t[4]>(0x00831600)[stage],
                                                     *reinterpret_cast<const uint32_t*>(d3d + 4),
                                                     *reinterpret_cast<const uint32_t*>(d3d + 0xc));
        } else {
            title_function<void XB_STDCALL(uint32_t, uint32_t)>(0x003e5f70)(stage, address_of(d3d));  // SetTexture
        }
        formats[stage] = format;
        textures[stage] = address_of(t->d3d_texture);
    }
    if (texture->palette) nglSetPaletteCached(stage, texture->palette);
    nglSetTextureAddressMode(&title_global<uint8_t>(0x008a2e60), stage, address_flags, border);
}

void XB_CDECL nglSetVertexStreams(const uint8_t* vertex_buffer, uint32_t stride, uint32_t start) {
    struct Stream {
        uint32_t buffer, stride, offset;
    };
    const uint32_t offset = stride * start;
    const uint32_t& buffer = *reinterpret_cast<const uint32_t*>(vertex_buffer + 8);
    uint32_t& cached_buffer = title_global<uint32_t>(0x00903c88);
    uint32_t& cached_stride = title_global<uint32_t>(0x00903c8c);
    uint32_t& cached_offset = title_global<uint32_t>(0x00903c90);
    if (buffer == cached_buffer && stride == cached_stride && offset == cached_offset) return;
    const Stream stream = {buffer, stride, offset};
    using InputDirect = void XB_STDCALL(uint32_t, uint32_t, const Stream*);
    title_function<InputDirect>(0x003e75e0)(title_global<uint32_t>(0x00903c0c), 1, &stream);  // SetVertexShaderInputDirect
    cached_buffer = buffer;
    cached_stride = stride;
    cached_offset = offset;
}

void XB_CDECL nglDrawMeshSection(const NglMeshSection* section) {
    using Draw = void XB_STDCALL(uint32_t, uint32_t, uint32_t);
    nglSetVertexStreams(section->vertex_buffer, section->stride, 0);
    if (!section->index_count) {
        title_function<Draw>(0x003e98c0)(section->primitive, 0, section->vertex_count);  // DrawVertices
    } else if (section->has_push_buffer) {
        title_function<void XB_STDCALL(uint32_t, uint32_t)>(0x003ebf10)(section->push_buffer, 0);  // RunPushBuffer
    } else {
        title_function<Draw>(0x003e9990)(section->primitive, section->index_count, section->indices);  // DrawIndexedVertices
    }
}

void XB_CDECL nglSetTransformConstants(int32_t slot, NglMeshDrawNode* node, float fog0, float fog1, float fog2,
                                       float fog3) {
    alignas(16) float constants[8];
    constants[3] = fog3 - fog2;
    constants[2] = 1.0f / (fog1 - fog0);
    constants[1] = fog0;
    constants[0] = fog2;
    alignas(16) float buffer[16];
    const float* inverse = nglNode_GetWorldInverse(node, buffer);
    // The camera position (the camera matrix's row 3) in the node's object space.
    const float* camera = current_scene()->matrix0d0 + 12;
    const Lanes p = add(add(add(lanes(inverse + 12), mul(lanes(inverse), camera[0])), mul(lanes(inverse + 4), camera[1])),
                        mul(lanes(inverse + 8), camera[2]));
    platform_keep(p);
    constants[4] = p.v[0];
    constants[5] = p.v[1];
    constants[6] = p.v[2];
    constants[7] = 1.0f;
    using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
    title_function<SetConstants>(0x003e7490)(static_cast<uint32_t>(slot + 0x60), constants, 8);  // NotInlineFast
}

void XB_CDECL nglSetObjectLightConstants(int32_t slot, NglMeshDrawNode* node) {
    alignas(16) float constants[32];  // directions[16], colours[16]
    nglComputeObjectLights(node, constants, constants + 16);
    using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
    title_function<SetConstants>(0x003e7490)(static_cast<uint32_t>(slot + 0x60), constants, 32);  // NotInlineFast
}

namespace {

// A row vector times a matrix (rows m[0..3]): ((m0 x + m1 y) + m2 z), with the
// translation row first for a point, all four lanes as the title's MULPS/ADDPS.
Lanes row_times(const Lanes& v, const float* m, bool point) {
    Lanes sum = mul(lanes(m), v.v[0]);
    if (point) sum = add(lanes(m + 12), sum);
    sum = add(sum, mul(lanes(m + 4), v.v[1]));
    return add(sum, mul(lanes(m + 8), v.v[2]));
}

// a * b (4x4, rows 0..2 without translation, row 3 with).
void matrix_times(const float* a, const float* b, Lanes* out) {
    for (int i = 0; i < 4; ++i) {
        out[i] = row_times(lanes(a + i * 4), b, i == 3);
        platform_keep(out[i]);
    }
}

// The first three columns of a row-vector matrix as rows.
void store_transposed(const Lanes* m, float* out) {
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 4; ++i) out[r * 4 + i] = m[i].v[r];
}

}  // namespace

void XB_CDECL nglSetSkinningConstants(int32_t slot, NglMeshDrawNode* node, const NglMeshSection* section) {
    // A function-static array of 64 matrices (3 rows), constructed on first use.
    uint32_t& guards = title_global<uint32_t>(0x00903c00);
    float* const matrices = &title_global<float>(0x00903000);
    if (!(guards & 1)) {
        guards |= 1;
        for (int i = 0; i < 64; ++i)
            title_function<void XB_STDCALL(void*, uint32_t, uint32_t, uint32_t)>(0x00011240)(matrices + i * 12, 0x10, 3,
                                                                                          0x0001b920);
    }
    const float* mesh_bones = *reinterpret_cast<float* const*>(node->mesh + 0x14);
    const uint32_t* params = node->params;
    const float* instance_bones = reinterpret_cast<const float*>(static_cast<uintptr_t>(params[2]));
    alignas(16) const float info[4] = {title_global<float>(0x007bf6d4), static_cast<float>(slot),
                                       title_global<float>(0x00903c70), title_global<float>(0x00903c74)};
    using SetConstant1 = void XB_FASTCALL(uint32_t, const float*);
    title_function<SetConstant1>(0x003e7440)(0, info);  // SetVertexShaderConstant1Fast
    const uint32_t flags = params[0];
    for (uint32_t i = 0; i < section->bone_count; ++i) {
        float* out = matrices + i * 12;
        const uint32_t bone = section->bones[i];
        const float* a = mesh_bones + bone * 16;
        const float* b = instance_bones + bone * 16;
        if (flags & 4) {
            alignas(16) float buffer[16];
            const float* inverse = nglNode_GetWorldInverse(node, buffer);
            Lanes ab[4];
            matrix_times(a, b, ab);
            Lanes abw[4];
            for (int r = 0; r < 4; ++r) {
                abw[r] = row_times(ab[r], inverse, r == 3);
                platform_keep(abw[r]);
            }
            store_transposed(abw, out);
        } else if (flags & 8) {
            Lanes ab[4];
            matrix_times(a, b, ab);
            store_transposed(ab, out);
        } else if (flags & 0x10) {
            const Lanes rows[4] = {lanes(b), lanes(b + 4), lanes(b + 8), lanes(b + 12)};
            store_transposed(rows, out);
        } else {
            static const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            std::memcpy(out, identity, sizeof(identity));
        }
    }
    const uint32_t count = section->bone_count * 3;
    if (count == 1) {
        title_function<SetConstant1>(0x003e7440)(static_cast<uint32_t>(slot + 0x60), matrices);
    } else {
        using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
        title_function<SetConstants>(0x003e7490)(static_cast<uint32_t>(slot + 0x60), matrices, count * 4);  // NotInlineFast
    }
}

void XB_CDECL nglAdvanceShaderTime(const float* color, float seconds) {
    float& clock = title_global<float>(0x00901e90);
    clock = platform_x87_add(clock, seconds);
    std::memcpy(&title_global<float>(0x00901ea0), color, 16);
    nglSetSceneTime(clock);
}

void XB_CDECL nglGetSceneFogColor(float* out) {
    float* kept = &title_global<float>(0x00901eb0);
    std::memcpy(kept, current_scene()->fog_color, 16);
    std::memcpy(out, kept, 16);
}

void* XB_CDECL nglAllocSceneMemory(uint32_t size, uint32_t align) {
    return alloc_frame(title_global<uintptr_t>(0x009025a8), size, align);
}

void XB_CDECL nglListAddTranslucent(NglNode* node) {
    node->next = current_scene()->translucent_nodes;
    current_scene()->translucent_nodes = node;
    ++current_scene()->translucent_count;
}

void XB_CDECL nglListAddText(const uint8_t* font, const char* text, float x, float y, float z, uint32_t color,
                             float scale_x, float scale_y) {
    if (current_scene()->stale) nglUpdateScene(0);
    if (!text || !*text || !font || !*reinterpret_cast<const uint32_t*>(font + 0x24)) return;
    auto* node = alloc_scene_memory<uint32_t>(0x2c);
    if (!node) return;
    node[0] = 0x007bd0cc;  // vtable
    const uint32_t size = static_cast<uint32_t>(std::strlen(text)) + 1;
    char* copy = static_cast<char*>(nglAllocSceneMemory(size, 0x10));
    node[3] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(copy));
    std::memcpy(copy, text, size);
    auto set_float = [node](int index, float value) { std::memcpy(&node[index], &value, 4); };
    set_float(5, x);
    set_float(6, y);
    set_float(8, scale_x);
    node[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(font));
    node[10] = color;
    set_float(7, z);
    set_float(9, scale_y);
    set_float(2, z);
    nglListAddTranslucent(reinterpret_cast<NglNode*>(node));
}

void XB_THISCALL nglRenderTextNode(NglTextNode* node) {
    if (!node->text) return;
    NglFont* font = node->font;
    const uint32_t texture_flags = font->texture_flags;
    if (!font->texture) return;
    uint32_t& cull_mode = title_global<uint32_t>(0x00902fac);
    if (cull_mode) {
        title_function<void XB_STDCALL(uint32_t)>(0x003e80f0)(0);  // D3DDevice_SetRenderState_CullMode
        cull_mode = 0;
    }
    nglSetBlendMode(&title_global<uint8_t>(0x00902f08), font->blend_mode, font->blend_alpha, 0x80);
    // D3D's deferred texture stage states, stage 0 (dirty bit 1).
    title_global<uint32_t>(0x003f8320) |= 1;
    title_global<uint32_t>(0x003f5c68) = (texture_flags & 0x40) ? 3 : 1;
    title_global<uint32_t>(0x003f8320) |= 1;
    title_global<uint32_t>(0x003f5c6c) = (texture_flags & 0x80) ? 3 : 1;
    nglSetTexture(0, font->texture, static_cast<uint8_t>(texture_flags), 3);
    nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00903c9c));
    nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00903cc4));
    float x = node->x;
    float y = node->y;
    const float z = nglDepthToZValue(node->z);
    if (title_global<uint8_t>(0x009028c0) && (current_scene()->render_target->flags & 4)) {
        x = title_global<float>(0x009028c4) * x;
        y = title_global<float>(0x009028c8) * y;
    }
    NglTextRun*& runs = title_global<NglTextRun*>(0x00903d18);
    uint32_t run_count;
    const uint32_t glyphs = nglBuildTextGlyphs(reinterpret_cast<const uint8_t*>(node->font), &runs, x, y,
                                               node->scale_x, node->scale_y, node->color, node->text, &run_count);
    NglTextRun* run = runs;
    title_global<uint32_t>(0x00903c88) = 0;
    title_global<uint32_t>(0x00903c8c) = 0;
    title_global<uint32_t>(0x00903c90) = 0;
    // D3DDevice_SetVertexShaderInputDirect
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);
    // 24 dwords per glyph, a begin/end pair and one header per run.
    const uint32_t dwords = ((glyphs * 3) << 5 >> 2) + run_count + 5;
    auto* push = reinterpret_cast<uint32_t*>(
        static_cast<uintptr_t>(title_function<uint32_t XB_STDCALL(uint32_t)>(0x003eba40)(dwords)));  // BeginPush
    push[0] = 0x000417fc;  // begin: quads
    push[1] = 8;
    push += 2;
    for (; run; run = run->next) {
        const uint32_t color = run->color;
        float pen_x = run->x;
        const float pen_y = run->y;
        const float scale_x = run->scale_x;
        const float scale_y = run->scale_y;
        *push++ = run->glyph_count * 0x600000 + 0x40001818;  // vertex data, 24 dwords a glyph
        for (uint32_t left = run->length; left; --left) {
            const uint8_t code = static_cast<uint8_t>(*run->text);
            font = node->font;
            const NglFontGlyph glyph = font->glyphs[code - font->first_glyph];
            const float* uv = node->font->uvs[static_cast<uint8_t>(code - static_cast<uint8_t>(node->font->first_glyph))];
            const float x0 = static_cast<float>(glyph.offset_x) * scale_x + pen_x;
            const float y0 = static_cast<float>(glyph.offset_y) * scale_y + pen_y;
            const float x1 = static_cast<float>(glyph.width) * scale_x + x0;
            const float y1 = static_cast<float>(glyph.height) * scale_y + y0;
            const float u0 = uv[0];
            const float v0 = uv[1];
            const float v1 = (uv[3] - uv[1]) + v0;
            const float u1 = (uv[2] - u0) + u0;
            const float vertices[4][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
            for (const auto& v : vertices) {
                std::memcpy(&push[0], &v[0], 4);
                std::memcpy(&push[1], &v[1], 4);
                std::memcpy(&push[2], &z, 4);
                push[3] = color;
                std::memcpy(&push[4], &v[2], 4);
                std::memcpy(&push[5], &v[3], 4);
                push += 6;
            }
            const NglFontGlyph& original = node->font->glyphs[code - node->font->first_glyph];
            ++run->text;
            pen_x = platform_x87_unsigned_mul_add(static_cast<uint32_t>(original.advance), run->scale_x, pen_x);
        }
    }
    push[0] = 0x000417fc;  // end
    push[1] = 0;
    title_function<void XB_STDCALL(uint32_t)>(0x003ebaa0)(  // EndPush
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(push + 2)));
    runs = nullptr;
}

void XB_CDECL nglListAddCustomNode(uint32_t a, uint32_t b, const uint32_t* data) {
    nglUpdateScene(0);
    auto* node = alloc_scene_memory<uint32_t>(0x20);
    if (!node) return;
    node[0] = 0x007bc6a0;  // vtable
    node[6] = data[0];
    node[7] = data[1];
    node[3] = 1;
    node[4] = a;
    node[5] = b;
    nglListAddNode(reinterpret_cast<NglNode*>(node));
}

namespace {

template <typename Entry>
void render_list(NglNode* nodes, uint32_t count, uint32_t pred,
                 void XB_CDECL (*sort)(Entry*, Entry*, int32_t, uint32_t)) {
    Entry* const first = alloc_scene_memory<Entry>(count * sizeof(Entry));
    Entry* const last = first + count;
    Entry* entry = first;
    for (NglNode* node = nodes; node; node = node->next, ++entry) {
        entry->value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
        std::memcpy(&entry->key, &node->sort_key, 4);
    }
    sort(first, last, static_cast<int32_t>(last - first), pred);
    NglNode* head = nullptr;
    entry = last;
    for (uint32_t i = count; i != 0; --i) {
        --entry;
        NglNode* node = reinterpret_cast<NglNode*>(static_cast<uintptr_t>(entry->value));
        node->next = head;
        head = node;
    }
    for (NglNode* node = head; node; node = node->next) {
        reinterpret_cast<void XB_THISCALL (*)(NglNode*)>(node->vtable[0])(node);
        title_global<NglNode*>(0x009025b4) = node;
    }
}

}  // namespace

void XB_CDECL nglRenderOpaqueList(NglNode* nodes, uint32_t count, uint32_t pred) {
    render_list<NglOpaqueNode>(nodes, count, pred, &nglSortOpaqueNodes_r);
}

void XB_CDECL nglRenderTranslucentList(NglNode* nodes, uint32_t count, uint32_t pred) {
    render_list<NglTranslucentNode>(nodes, count, pred, &nglSortTranslucentNodes_r);
}

void XB_CDECL nglRenderSceneTree() {
    uintptr_t& frame_memory = title_global<uintptr_t>(0x00903c34);
    const uintptr_t saved_frame_memory = frame_memory;
    title_function<void XB_CDECL()>(0x0006ab10)();  // nglAllocSceneRenderTargets
    NglScene* const scene = current_scene();
    for (NglScene* child = scene->first_child; child; child = child->next_sibling) {
        current_scene() = child;
        nglRenderSceneTree();
    }
    current_scene() = scene;
    if (scene->stale) nglUpdateScene(0);
    nglBeginSceneRender(current_scene());
    using Callback = void XB_CDECL(void*);
    auto run_callback = [](int which) {
        const NglScene::Callback& callback = current_scene()->callbacks[which];
        if (callback.function) reinterpret_cast<Callback*>(callback.function)(callback.arg);
    };
    run_callback(0);
    // Depth writes (render-state cache +0x74, the pushed state at 0x003f8428).
    uint8_t& z_write = title_global<uint8_t>(0x00902f7c);
    const uint8_t depth_write = current_scene()->depth_enable;
    if (z_write != depth_write) {
        title_function<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(0x0004035c, depth_write);  // SetRenderState_Simple
        title_global<uint32_t>(0x003f8428) = depth_write;
        z_write = depth_write;
    }
    nglRenderOpaqueList(current_scene()->opaque_nodes, current_scene()->opaque_count, 0);
    run_callback(1);
    if (current_scene()->translucent_count != 0) {
        if (z_write != 0) {
            title_function<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(0x0004035c, 0);  // SetRenderState_Simple
            title_global<uint32_t>(0x003f8428) = 0;
            z_write = 0;
        }
        nglRenderTranslucentList(current_scene()->translucent_nodes, current_scene()->translucent_count, 0);
    }
    run_callback(2);
    nglFreeSceneRenderTargets();
    frame_memory = saved_frame_memory;
    NglRenderTarget* target = current_scene()->render_target;
    if (target->levels > 1) title_function<void XB_CDECL(NglRenderTarget*)>(0x00063870)(target);  // nglGenerateRenderTargetMips
}

const uint32_t* XB_THISCALL nglResourceManager_Find(const uint8_t* manager, const uint32_t* name) {
    return nglSkipList_Find(reinterpret_cast<const NglSkipList*>(manager + 4), name);
}

uint8_t* XB_THISCALL nglLockVertexBuffer(const uint8_t* buffer, uint32_t offset, uint32_t) {
    using Lock = uint8_t* XB_STDCALL(uint32_t, uint32_t);
    // D3DVertexBuffer_Lock2(D3DLOCK_NOOVERWRITE)
    return title_function<Lock>(0x003ebb30)(*reinterpret_cast<const uint32_t*>(buffer + 8), 0x20) + offset;
}

void XB_THISCALL nglLockPalette(uint8_t* palette) {
    using Lock = uint32_t XB_STDCALL(uint32_t, uint32_t);
    // D3DPalette_Lock2; the entries pointer goes to +0.
    *reinterpret_cast<uint32_t*>(palette) = title_function<Lock>(0x003ebbc0)(*reinterpret_cast<uint32_t*>(palette + 0xc), 0);
}

uint32_t XB_CDECL nglGetFreeMemory() {
    // MEMORYSTATUS, left uninitialized like the original; dwAvailPhys at +0xc.
    uint32_t status[8];
    title_function<void XB_STDCALL(uint32_t*)>(0x001e7c35)(status);  // GlobalMemoryStatus
    return status[3];
}

NglTexture* XB_CDECL nglGetFrontBufferTexture() {
    // The texture's D3D surface slot (0x00902d0c) gets the current front buffer.
    uint32_t* slot = title_global<uint32_t*>(0x00902d0c);
    title_function<uint32_t XB_STDCALL(uint32_t)>(0x003e7ef0)(*slot);  // D3DResource_Release
    *title_global<uint32_t*>(0x00902d0c) = title_function<uint32_t XB_STDCALL(int32_t)>(0x003e5720)(-1);  // GetBackBuffer2
    return &title_global<NglTexture>(0x00902cd8);
}

NglTexture* XB_CDECL nglGetBackBufferTexture() {
    uint32_t* slot = title_global<uint32_t*>(0x00902d84);
    title_function<uint32_t XB_STDCALL(uint32_t)>(0x003e7ef0)(*slot);  // D3DResource_Release
    *title_global<uint32_t*>(0x00902d84) = title_function<uint32_t XB_STDCALL(int32_t)>(0x003e5720)(0);  // GetBackBuffer2
    return &title_global<NglTexture>(0x00902d50);
}

void XB_CDECL nglResetDeviceBindings() {
    title_global<uint32_t>(0x00903c88) = 0;
    title_global<uint32_t>(0x00903c8c) = 0;
    title_global<uint32_t>(0x00903c90) = 0;
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
    nglUnbindTextures();
    title_function<void XB_CDECL()>(0x003e6200)();
    title_global<uint32_t>(0x009028e0) += 2;  // ngl_frameCount
}

void XB_THISCALL nglVertexBuffer_Destroy(uint32_t* buffer) {
    // {memory, size, D3D vertex buffer}
    title_function<void XB_STDCALL(uint32_t)>(0x003e8000)(buffer[2]);  // D3DResource_BlockUntilNotBusy
    title_function<void XB_CDECL(uint32_t)>(0x000939b0)(buffer[2]);    // Mem_AlignedFree
    title_function<void XB_CDECL(uint32_t)>(0x000939b0)(buffer[0]);
    buffer[0] = 0;
    buffer[1] = 0;
    buffer[2] = 0;
}

void XB_CDECL nglTexture_Lock(NglTexture* texture, uint32_t flags) {
    uint8_t* t = reinterpret_cast<uint8_t*>(texture);
    using LockRect = void XB_STDCALL(uint8_t*, uint32_t, uint8_t*, uint32_t, uint32_t);
    title_function<LockRect>(0x003ebd40)(texture->d3d_texture, 0, t + 0x1c, 0, flags);  // D3DTexture_LockRect
    if (texture->flags & 0x100) {
        // Swizzled: set up the swizzler (0x00902b38) for width (+0x10) x height (+0x14).
        using SwizzlerInit = void XB_THISCALL(uint32_t, uint32_t, uint32_t, uint32_t);
        title_function<SwizzlerInit>(0x003f9a3e)(0x00902b38, *reinterpret_cast<uint32_t*>(t + 0x10),
                                                 *reinterpret_cast<uint32_t*>(t + 0x14), 0);
    }
}

void XB_THISCALL nglVertexBuffer_Free(uint32_t* buffer, uint32_t protect) {
    // Frees the D3D object only; the memory gets the protection `protect` when nonzero.
    title_function<void XB_STDCALL(uint32_t)>(0x003e8000)(buffer[2]);  // D3DResource_BlockUntilNotBusy
    if (protect) title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x001e76aa)(buffer[0], buffer[1], protect);  // XPhysicalProtect
    title_function<void XB_CDECL(uint32_t)>(0x000939b0)(buffer[2]);  // Mem_AlignedFree
    buffer[1] = 0;
    buffer[2] = 0;
}

void XB_CDECL nglFreeFileBuffer(uint32_t* buffer) {
    // An installed free hook (0x009098dc) takes the whole buffer; else the count of
    // live buffers drops and the memory goes to the second hook (0x009098ec), or to
    // XPhysicalFree (physical memory: the address's top bit set) or _aligned_free.
    using Free = void XB_CDECL(uint32_t);
    if (const uint32_t hook = title_global<uint32_t>(0x009098dc)) {
        title_function<void XB_CDECL(uint32_t*)>(hook)(buffer);
        return;
    }
    const uint32_t memory = *buffer;
    --title_global<int32_t>(0x009098c4);
    if (const uint32_t hook = title_global<uint32_t>(0x009098ec)) {
        title_function<Free>(hook)(memory);
        return;
    }
    if (static_cast<int32_t>(memory) < 0) {
        title_function<void XB_STDCALL(uint32_t)>(0x001e76ba)(memory);  // XPhysicalFree
        return;
    }
    title_function<Free>(0x001f113a)(memory);  // _aligned_free
}

void XB_CDECL nglFreePalette(uint32_t* palette) {
    // {entries, -, borrowed entries flag (byte +8), D3D palette}
    if (!palette) return;
    title_function<void XB_STDCALL(uint32_t)>(0x003e8000)(palette[3]);  // D3DResource_BlockUntilNotBusy
    nglUnbindTextures();
    using Free = void XB_CDECL(uint32_t);
    if (!reinterpret_cast<uint8_t*>(palette)[8]) {
        title_function<Free>(0x000939b0)(palette[0]);  // Mem_AlignedFree
        palette[0] = 0;
    }
    title_function<Free>(0x000939b0)(palette[3]);
    palette[3] = 0;
    title_function<Free>(0x000939b0)(address_of(palette));
}

void XB_THISCALL nglRegisterTexture(NglTexture* texture, uint32_t data) {
    // Points the D3D resource (+0x34 for a surface, flag 0x2000; else the texture at
    // +0x30) at `data`, with a reference count of 1 and its type bits.
    uint8_t* t = reinterpret_cast<uint8_t*>(texture);
    const bool surface = texture->flags & 0x2000;
    uint32_t* resource = *reinterpret_cast<uint32_t**>(t + (surface ? 0x34 : 0x30));
    resource[2] = 0;
    title_function<void XB_STDCALL(uint32_t*, uint32_t)>(0x003e7fd0)(resource, data);  // D3DResource_Register
    resource = *reinterpret_cast<uint32_t**>(t + (surface ? 0x34 : 0x30));
    resource[0] = surface ? 0x00050001 : 0x00040001;
    *reinterpret_cast<uint32_t*>(t + 0x24) = data;
}

void XB_THISCALL nglReleaseTextureMemory(NglTexture* texture) {
    // Frees the pixel memory (+0x24) unless borrowed (flag 0x400) and detaches it from
    // the D3D resource (surface +0x34 with flag 0x2000, else the texture +0x30).
    uint8_t* t = reinterpret_cast<uint8_t*>(texture);
    uint32_t& memory = *reinterpret_cast<uint32_t*>(t + 0x24);
    if (!memory) return;
    const uint32_t resource_offset = (texture->flags & 0x2000) ? 0x34 : 0x30;
    nglUnbindTextures();
    if (!(texture->flags & 0x400)) title_function<void XB_CDECL(uint32_t)>(0x000939b0)(memory);  // Mem_AlignedFree
    uint32_t* resource = *reinterpret_cast<uint32_t**>(t + resource_offset);
    memory = 0;
    resource[1] = 0;
}

void XB_CDECL nglSetRenderTargetCached(NglTexture* target, uint32_t level, NglTexture* depth, uint32_t face) {
    // The colour surface: level `level` of the target's surface list (+0x34), or of
    // its face list `face` (cube maps; 6 = not a cube). The depth surface: `depth`'s,
    // else the target's own depth texture's (+0x38), else none.
    uint8_t* t = reinterpret_cast<uint8_t*>(target);
    const uint32_t* surfaces = face == 6 ? *reinterpret_cast<uint32_t* const*>(t + 0x34)
                                         : (*reinterpret_cast<uint32_t* const* const*>(t + 0x34))[face];
    const uint32_t color = surfaces[level];
    uint32_t depth_surface = 0;
    if (depth) {
        depth_surface = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(depth) + 0x34);
    } else if (const uint8_t* own = *reinterpret_cast<uint8_t* const*>(t + 0x38)) {
        depth_surface = *reinterpret_cast<const uint32_t*>(own + 0x34);
    }
    uint32_t& cached_color = title_global<uint32_t>(0x00903c80);
    uint32_t& cached_depth = title_global<uint32_t>(0x00903c84);
    if (cached_color == color && cached_depth == depth_surface) {
        title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e55b0)(cached_color, cached_depth, 0);  // SetRenderTargetFast
    } else {
        title_function<void XB_STDCALL(uint32_t, uint32_t)>(0x003e52b0)(color, depth_surface);  // SetRenderTarget
    }
    cached_color = color;
    cached_depth = depth_surface;
}

uint32_t XB_CDECL nglTextureGetPixel(const NglTexture* texture, uint32_t x, uint32_t y) {
    // 32-bit pixels at +0x20: linear (flag 0x200) with the pitch at +0x1c, else
    // swizzled through the swizzler (0x00902b38) and the masks of nglTexture_Lock.
    const uint8_t* t = reinterpret_cast<const uint8_t*>(texture);
    const uint8_t* pixels = *reinterpret_cast<uint8_t* const*>(t + 0x20);
    if (texture->flags & 0x200)
        return reinterpret_cast<const uint32_t*>(pixels + *reinterpret_cast<const uint32_t*>(t + 0x1c) * y)[x];
    title_global<uint32_t>(0x00902b50) = 0;
    title_global<uint32_t>(0x00902b54) = 0;
    using Swizzle = uint32_t XB_THISCALL(uint32_t, uint32_t);
    uint32_t v = title_function<Swizzle>(0x003f9ac8)(0x00902b38, y);  // Swizzler_SwizzleV
    uint32_t u = title_function<Swizzle>(0x003f9a9f)(0x00902b38, x);  // Swizzler_SwizzleU
    const uint32_t mask_x = title_global<uint32_t>(0x00902b44), mask_y = title_global<uint32_t>(0x00902b48);
    u = (0u - ((0u - u) & mask_x)) & mask_x;
    v = (0u - ((0u - v) & mask_y)) & mask_y;
    title_global<uint32_t>(0x00902b54) = v;
    title_global<uint32_t>(0x00902b50) = u;
    return reinterpret_cast<const uint32_t*>(pixels)[v | u];
}

void XB_CDECL nglInitDepthTextureView() {
    // A linear A8R8G8B8 (0x12) texture header (0x00902e50) over the depth buffer's
    // memory, the back buffer's size; the view texture (0x00902e88) gets the same
    // size fields as the back-buffer texture (0x00902d60/64) and the header.
    const uint32_t width = title_global<uint32_t>(0x00902dd8);
    using SetHeader = void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<SetHeader>(0x003f9994)(width, title_global<uint32_t>(0x00902ddc), 1, 0, 0x12, 0, 0x00902e50,
                                          title_global<uint32_t*>(0x00902dfc)[1], width * 4);  // XGSetTextureHeader
    title_global<uint32_t>(0x00902ea8) = 0x00902e50;
    title_global<uint32_t>(0x00902e88) = title_global<uint32_t>(0x00902d60);
    title_global<uint32_t>(0x00902e8c) = title_global<uint32_t>(0x00902d64);
}

void XB_CDECL nglListAddTextf(const uint8_t* font, float x, float y, float z, uint32_t color, float scale_x,
                              float scale_y, const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    title_function<int XB_CDECL(char*, const char*, va_list)>(0x001eea95)(text, format, args);  // _vsprintf
    va_end(args);
    nglListAddText(font, text, x, y, z, color, scale_x, scale_y);
}

void XB_CDECL nglQuadBatch_Draw() {
    // Draws the quads written since the last draw (count 0x00909f30) from the current
    // buffer (index 0x00909f2c: vertex bases 0x00909f20, starts 0x00909f18), then
    // advances the index start (0x00909f38), the write pointer (0x00909f3c, 0x60 bytes
    // a quad) and the frame's quad count (0x00909f48), and keeps the high-water mark
    // of quads used in a buffer (0x00909f40).
    const uint32_t buffer = title_global<uint32_t>(0x00909f2c);
    nglSetVertexStreams(title_global<const uint8_t* [2]>(0x00909f20)[buffer], 0x18, 0);
    const uint8_t* indices = *title_global<uint8_t**>(0x00909f28) + title_global<int32_t>(0x00909f38) * 2;
    using Draw = void XB_STDCALL(uint32_t, uint32_t, const uint8_t*);
    title_function<Draw>(0x003e9990)(5, title_global<int32_t>(0x00909f30) * 6, indices);  // DrawIndexedVertices, triangle list
    const int32_t quads = title_global<int32_t>(0x00909f30);
    title_global<int32_t>(0x00909f38) += quads * 6;
    title_global<int32_t>(0x00909f48) += quads;
    int32_t& write = title_global<int32_t>(0x00909f3c);
    write += quads * 0x60;
    const int32_t used = (write - title_global<int32_t[2]>(0x00909f18)[title_global<uint32_t>(0x00909f2c)]) / 0x18 / 4;
    if (used > title_global<int32_t>(0x00909f40)) title_global<int32_t>(0x00909f40) = used;
}

void XB_CDECL nglQuadBatch_SetMaterial(NglTexture* texture, uint32_t blend, uint8_t fog, uint32_t alpha_ref) {
    // The particle quads' states through the state cache (0x00902f08): alpha test with
    // `alpha_ref` unless 0x00909f50 is set (then alpha test and blending off), culling
    // off, fog, and a blend: 0/3 alpha, 1 additive (src alpha, one), 2 reverse subtract.
    using SetRenderStateSimple = void XB_FASTCALL(uint32_t, uint32_t);
    uint8_t* cache = &title_global<uint8_t>(0x00902f08);
    auto set_byte = [cache](uint32_t offset, uint8_t value, uint32_t method) {
        if (cache[offset] != value) {
            title_function<SetRenderStateSimple>(0x003e89f0)(method, value);
            cache[offset] = value;
        }
    };
    auto set = [cache](uint32_t offset, uint32_t value, uint32_t method) {
        uint32_t& field = *reinterpret_cast<uint32_t*>(cache + offset);
        if (field != value) {
            title_function<SetRenderStateSimple>(0x003e89f0)(method, value);
            field = value;
        }
    };
    nglSetTexture(0, texture, 2, 3);
    title_global<uint32_t>(0x003f8320) |= 1;
    title_global<uint32_t>(0x003f5c68) = 3;
    title_global<uint32_t>(0x003f5c6c) = 3;
    if (title_global<uint32_t>(0x00909f50) != 0) {
        set_byte(0x5c, 0, 0x00040304);  // alpha blend enable
        set_byte(0x50, 0, 0x00040300);  // alpha test enable
    } else {
        set_byte(0x5c, 1, 0x00040304);
        set_byte(0x50, 1, 0x00040300);
        set(0x54, 0x204, 0x0004033c);       // alpha func GREATER
        set(0x58, alpha_ref, 0x00040340);   // alpha ref
    }
    uint32_t& cull_mode = *reinterpret_cast<uint32_t*>(cache + 0xa4);
    if (cull_mode != 0) {
        title_function<void XB_STDCALL(uint32_t)>(0x003e80f0)(0);  // SetRenderState_CullMode
        cull_mode = 0;
    }
    *reinterpret_cast<int32_t*>(cache + 0xec) = -1;
    *reinterpret_cast<int32_t*>(cache + 0xf0) = -1;
    *reinterpret_cast<uint32_t*>(cache + 0xe8) = 9;
    if (cache[0x8b] != fog) {
        title_global<uint32_t>(0x003f8320) |= 0x2000;  // D3DRS_FOGENABLE
        title_global<uint32_t>(0x003f8498) = fog;
        cache[0x8b] = fog;
    }
    switch (blend) {
    case 0:
    case 3:
        set(0x70, 0x8006, 0x00040350);  // blend op ADD
        set(0x60, 0x302, 0x00040344);   // src SRCALPHA
        set(0x64, 0x303, 0x00040348);   // dest INVSRCALPHA
        break;
    case 1:
        set(0x70, 0x8006, 0x00040350);
        set(0x60, 0x302, 0x00040344);
        set(0x64, 1, 0x00040348);       // dest ONE
        break;
    case 2:
        set(0x70, 0x800b, 0x00040350);  // REVSUBTRACT
        set(0x60, 0x302, 0x00040344);
        nglStateCache_SetDestBlend(cache, 1);
        break;
    default:
        break;
    }
}

void XB_CDECL nglSetDefaultSceneParams() {
    nglSetRenderTarget(reinterpret_cast<NglRenderTarget*>(nglGetBackBufferTexture()));
    const float bottom = static_cast<float>(nglGetScreenHeight() - 1);
    const float right = static_cast<float>(nglGetScreenWidth() - 1);
    nglSetViewportRect(0.0f, 0.0f, right, bottom);
    current_scene()->aspect = title_global<uint8_t>(0x00902899)  // ngl_widescreen
                                  ? title_global<float>(0x0083f254)   // 16/9
                                  : title_global<float>(0x0083eb80);  // 4/3
    nglUpdateScene(1);
    NglScene* scene = current_scene();
    scene->projection_kind = 1;
    scene->fov = title_global<float>(0x0083ec30);    // 50
    scene->near_z = title_global<float>(0x0083ebf8); // 0.1
    scene->far_z = title_global<float>(0x0083f010);  // 10000
    nglUpdateScene(1);
    // An identity camera: rows 0-2 scaled by a broadcast 1 (mulps), row 3 copied.
    const float scale = title_global<float>(0x007bc4b0);
    float camera[16];
    for (int row = 0; row < 3; ++row) {
        const float* source = &title_global<float>(0x007bc4d0 + row * 0x10);
        for (int i = 0; i < 4; ++i) camera[row * 4 + i] = source[i] * scale;
    }
    std::memcpy(camera + 12, &title_global<float>(0x007bc600), 16);
    nglSetCameraMatrix(camera);
    scene = current_scene();
    scene->clear_flags = 3;
    for (float& c : scene->clear_color) c = 0.0f;
    scene->clear_depth = title_global<float>(0x0083eb20);  // 1
    nglSetSceneFlags(0);
    scene = current_scene();
    scene->color_write_mask = 0x01010101;
    scene->depth_enable = 1;
    scene->depth_test = 1;
    uint8_t* raw = reinterpret_cast<uint8_t*>(scene);
    raw[0x386] = 0;
    *reinterpret_cast<uint32_t*>(raw + 0x2f0) = 0;
    *reinterpret_cast<uint32_t*>(raw + 0x2f4) = 0;
    scene->fog[0] = 0.0f > scene->near_z ? 0.0f : scene->near_z;
    scene->fog[1] = title_global<float>(0x0083f010);  // 10000
    scene->fog[2] = 0.0f;
    scene->fog[3] = title_global<float>(0x0083eb20);  // 1
    for (float& c : scene->fog_color) c = title_global<float>(0x0083eb20);
    raw[0x3b0] = 0;
    *reinterpret_cast<uint32_t*>(raw + 0x2f0) = 0;
    *reinterpret_cast<uint32_t*>(raw + 0x2f4) = 0;
    *reinterpret_cast<float*>(raw + 0x3b4) = 0.0f;
    scene->time = 0.0f;
    nglSetSceneLightSet(title_global<NglLightSet*>(0x00902e40));
    nglUpdateScene(1);
}

void XB_THISCALL nglRestoreRenderStates(uint8_t* self) {
    using SetState = void XB_STDCALL(uint32_t);
    using SetRenderStateSimple = void XB_FASTCALL(uint32_t, uint32_t);
    auto field = [self](uint32_t offset) { return *reinterpret_cast<uint32_t*>(self + offset); };
    // Simple states also go into D3D's render-state shadow (title memory).
    auto simple = [](uint32_t method, uint32_t value, uint32_t shadow) {
        title_function<SetRenderStateSimple>(0x003e89f0)(method, value);
        title_global<uint32_t>(shadow) = value;
    };
    title_function<SetState>(0x003e88a0)(field(0xa8));  // SetRenderState_FillMode
    title_function<SetState>(0x003e80f0)(field(0xa4));  // SetRenderState_CullMode
    simple(0x00040310, self[0xc9], 0x003f842c);
    simple(0x00040300, self[0x50], 0x003f8418);
    simple(0x00040304, self[0x5c], 0x003f8414);
    title_function<SetState>(0x003e82b0)(self[0]);      // SetRenderState_StencilEnable
    title_function<SetState>(0x003e8800)(field(0x4c));  // SetRenderState_MultiSampleMask
    simple(0x00040358, field(0xa0), 0x003f8434);
    title_global<uint32_t>(0x003f8320) |= 0x2000;       // D3DRS_FOGENABLE dirty
    title_global<uint32_t>(0x003f8498) = self[0x8b];
    title_function<SetState>(0x003e8010)(self[0xc8]);   // SetRenderState_EdgeAntiAlias
    title_function<SetState>(0x003e88f0)(field(0xb0));  // SetRenderState_BackFillMode
    title_function<SetState>(0x003e8840)(field(0x68));  // SetRenderState_LogicOp
    title_function<SetState>(0x003e8530)(self[0xb4]);   // SetRenderState_YuvEnable
    simple(0x00040384, field(0x84), 0x003f845c);        // polygon offset factor
    simple(0x00040388, field(0x80), 0x003f8460);        // polygon offset bias
    // Forget the blend mode (nglStateCache_SetBlendMode's +0xe8/+0xec/+0xf0).
    *reinterpret_cast<int32_t*>(self + 0xec) = -1;
    *reinterpret_cast<int32_t*>(self + 0xf0) = -1;
    *reinterpret_cast<uint32_t*>(self + 0xe8) = 9;
    title_global<uint32_t>(0x00903c08) = 0;
    title_global<uint32_t>(0x00903c0c) = 0;
    title_global<uint32_t>(0x00903c10) = 0;
}

void XB_CDECL nglInitShaderConstants(uint8_t init_table) {
    using SetConstant1 = void XB_FASTCALL(uint32_t, const float*);
    title_function<void XB_STDCALL(uint32_t)>(0x003e7050)(0x11);  // SetShaderConstantMode
    auto f = [](uint32_t address) { return title_global<float>(address); };
    // c1: 0, 0.5, 1, 2; c2: pi, 0.5, 2pi, 1/2pi; c3/c4: the cos/sin Taylor coefficients.
    alignas(16) float c[4] = {0.0f, f(0x0083eb24), f(0x0083eb20), f(0x0083eba8)};
    title_function<SetConstant1>(0x003e7440)(1, c);  // SetVertexShaderConstant1Fast
    c[0] = f(0x0083ed00); c[1] = f(0x0083eb24); c[2] = f(0x0083edac); c[3] = f(0x0083f118);
    title_function<SetConstant1>(0x003e7440)(2, c);
    c[0] = f(0x0083eb20); c[1] = f(0x0083eb28); c[2] = f(0x007bf6cc); c[3] = f(0x007bf6d0);
    title_function<SetConstant1>(0x003e7440)(3, c);
    c[0] = f(0x0083eb20); c[1] = f(0x007bf6c4); c[2] = f(0x0083ed98); c[3] = f(0x007bf6c8);
    title_function<SetConstant1>(0x003e7440)(4, c);
    if (init_table) nglInitShaderConstantTable();
}

void XB_THISCALL nglReleaseTextureSurfaces(NglTexture* texture) {
    // Releases the per-level surfaces (+0x34, +0x18 levels; cube maps 0x10000000: six
    // face lists) and frees the lists; textures with flag 0x2000 hold memory instead.
    if (texture->flags & 0x2000) {
        nglReleaseTextureMemory(texture);
        return;
    }
    uint8_t* t = reinterpret_cast<uint8_t*>(texture);
    uint32_t*& surfaces = *reinterpret_cast<uint32_t**>(t + 0x34);
    const uint32_t& levels = *reinterpret_cast<uint32_t*>(t + 0x18);
    using Release = uint32_t XB_STDCALL(uint32_t);
    using Free = void XB_CDECL(void*);
    if (texture->flags & 0x10000000) {
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t level = 0; level < levels; ++level) {
                uint32_t*& list = reinterpret_cast<uint32_t**>(surfaces)[face];
                if (list[level]) {
                    title_function<Release>(0x003e7ef0)(list[level]);  // D3DResource_Release
                    reinterpret_cast<uint32_t**>(surfaces)[face][level] = 0;
                }
            }
            title_function<Free>(0x000939b0)(reinterpret_cast<uint32_t**>(surfaces)[face]);  // Mem_AlignedFree
            reinterpret_cast<uint32_t**>(surfaces)[face] = nullptr;
        }
    } else {
        for (uint32_t level = 0; level < levels; ++level) {
            if (surfaces[level]) {
                title_function<Release>(0x003e7ef0)(surfaces[level]);
                surfaces[level] = 0;
            }
        }
    }
    title_function<Free>(0x000939b0)(surfaces);
    surfaces = nullptr;
}

void XB_CDECL nglGenerateRenderTargetMips(NglTexture* texture) {
    // Render targets with mips (flag 0x1000): draws each level from the texture with a
    // screen-sized quad of {x, y, z, u, v}, the texture coordinates scaled for
    // multisampling.
    uint8_t* t = reinterpret_cast<uint8_t*>(texture);
    const uint32_t& levels = *reinterpret_cast<uint32_t*>(t + 0x18);
    if (levels <= 1 || !(texture->flags & 0x1000)) return;
    uint32_t& cull_mode = title_global<uint32_t>(0x00902fac);
    if (cull_mode != 0) {
        title_function<void XB_STDCALL(uint32_t)>(0x003e80f0)(0);  // SetRenderState_CullMode
        cull_mode = 0;
    }
    nglSetBlendMode(&title_global<uint8_t>(0x00902f08), 0, 0, 0);
    nglSetTexture(0, texture, 2, 3);
    // D3D's texture stage shadows (title memory): stage 0 address modes, then +0x7c.
    title_global<uint32_t>(0x003f5c68) = 3;
    title_global<uint32_t>(0x003f8320) |= 1;
    title_global<uint32_t>(0x003f5c6c) = 3;
    nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00903ca8));
    nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00903d08));
    title_global<uint32_t>(0x003f8320) |= 1;
    title_global<uint32_t>(0x003f5c7c) = 0;
    const float u = title_global<float>(0x00903c70);  // ngl_multisampleScale
    const float v = title_global<float>(0x00903c74);
    float quad[4][5] = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                        {0.0f, 0.0f, 0.0f, u, 0.0f},
                        {0.0f, 0.0f, 0.0f, 0.0f, v},
                        {0.0f, 0.0f, 0.0f, u, v}};
    for (uint32_t level = 1; level < levels; ++level) {
        nglSetRenderTargetCached(texture, level, nullptr, 6);
        const float width = platform_x87_unsigned_to_float(*reinterpret_cast<uint32_t*>(t + 0x10) >> level);
        const float height = platform_x87_unsigned_to_float(*reinterpret_cast<uint32_t*>(t + 0x14) >> level);
        quad[1][0] = width;
        quad[2][1] = height;
        quad[3][0] = width;
        quad[3][1] = height;
        title_global<uint32_t>(0x00903c88) = 0;
        title_global<uint32_t>(0x00903c8c) = 0;
        title_global<uint32_t>(0x00903c90) = 0;
        title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
        title_function<void XB_STDCALL(uint32_t, uint32_t, const void*, uint32_t)>(0x003e9710)(
            6, 4, quad, sizeof(quad[0]));  // DrawVerticesUP: a quad list
    }
    // Forget the cached texture stage states.
    for (uint32_t i = 0; i < 4; ++i) {
        title_global<uint32_t>(0x008a2e60 + i * 4) = 0;
        title_global<int32_t>(0x008a2e70 + i * 4) = -1;
        title_global<int32_t>(0x008a2e80 + i * 4) = -1;
    }
}

void XB_THISCALL nglSetDefaultRenderStates(uint8_t* self) {
    // Sends every state of the cache to D3D (nglInitRenderStates' tail).
    using SetState = void XB_STDCALL(uint32_t);
    using SetRenderStateSimple = void XB_FASTCALL(uint32_t, uint32_t);
    auto field = [self](uint32_t offset) { return *reinterpret_cast<uint32_t*>(self + offset); };
    auto simple = [](uint32_t method, uint32_t value, uint32_t shadow) {
        title_function<SetRenderStateSimple>(0x003e89f0)(method, value);
        title_global<uint32_t>(shadow) = value;
    };
    title_function<SetState>(0x003e82b0)(self[0x0]);  // SetRenderState_StencilEnable
    simple(0x00040374, field(0x4), 0x003f8438);
    simple(0x00040378, field(0x8), 0x003f843c);
    simple(0x00040364, field(0xc), 0x003f8440);
    title_function<SetState>(0x003e8360)(field(0x10));  // SetRenderState_StencilFail
    simple(0x00040368, field(0x14), 0x003f8444);
    simple(0x0004036c, field(0x18), 0x003f8448);
    simple(0x00040360, field(0x1c), 0x003f844c);
    // Inlined setters: D3D's dirty bits (0x003f8320) and render-state shadows.
    title_global<uint32_t>(0x003f8320) |= 0x900;
    title_global<uint32_t>(0x003f8500) = self[0x20];
    title_global<uint32_t>(0x003f8504) = self[0x21];
    title_global<uint32_t>(0x003f84f8) = field(0x24);
    title_global<uint32_t>(0x003f84fc) = field(0x28);
    title_global<uint32_t>(0x003f8508) = field(0x2c);
    title_global<uint32_t>(0x003f850c) = field(0x30);
    title_global<uint32_t>(0x003f8510) = field(0x34);
    title_global<uint32_t>(0x003f8514) = field(0x38);
    title_function<SetState>(0x003e87c0)(self[0x3c]);  // SetRenderState_MultiSampleAntiAlias
    title_function<SetState>(0x003e8800)(field(0x4c));  // SetRenderState_MultiSampleMask
    simple(0x00040300, self[0x50], 0x003f8418);
    simple(0x0004033c, field(0x54), 0x003f8410);
    simple(0x00040340, field(0x58), 0x003f841c);
    simple(0x00040304, self[0x5c], 0x003f8414);
    simple(0x00040344, field(0x60), 0x003f8420);
    simple(0x00040348, field(0x64), 0x003f8424);
    simple(0x00040350, field(0x70), 0x003f8450);
    simple(0x0004035c, self[0x74], 0x003f8428);
    title_function<SetState>(0x003e8210)(field(0x78));  // SetRenderState_ZEnable
    simple(0x00040354, field(0x7c), 0x003f840c);
    simple(0x00040384, field(0x84), 0x003f845c);
    simple(0x00040388, field(0x80), 0x003f8460);
    title_global<uint32_t>(0x003f8320) |= 0x2000;  // D3DRS_FOGENABLE
    title_global<uint32_t>(0x003f8498) = self[0x8b];
    title_function<SetState>(0x003e8090)(field(0x8c));  // SetRenderState_FogColor
    simple(0x00040358, field(0xa0), 0x003f8434);
    title_function<SetState>(0x003e80f0)(field(0xa4));  // SetRenderState_CullMode
    title_function<SetState>(0x003e88a0)(field(0xa8));  // SetRenderState_FillMode
    simple(0x0004037c, field(0xac), 0x003f8430);
    title_function<SetState>(0x003e8010)(self[0xc8]);  // SetRenderState_EdgeAntiAlias
    simple(0x00040310, self[0xc9], 0x003f842c);
    title_global<uint32_t>(0x003f8320) |= 0x3000;
    title_global<uint32_t>(0x003f84c4) = self[0xe6];
    simple(0x00040338, self[0x90], 0x003f846c);
    title_global<uint32_t>(0x00903c08) = 0;
    title_global<uint32_t>(0x00903c0c) = 0;
    title_global<uint32_t>(0x00903c10) = 0;
    *reinterpret_cast<uint32_t*>(self + 0xe8) = 9;
    *reinterpret_cast<int32_t*>(self + 0xec) = -1;
    *reinterpret_cast<int32_t*>(self + 0xf0) = -1;
}

uint32_t XB_CDECL nglBuildTextGlyphs(const uint8_t* font, NglTextRun** runs, float x, float y, float scale_x,
                                     float scale_y, uint32_t color, const char* text, uint32_t* run_count) {
    // Splits the text into runs of glyphs that share position, scale and colour. Codes:
    // 1 colour (hex RRGGBBAA, then one character), 2 scale (one number), 3 scale x and
    // y, 9 tab (4 spaces), 10 newline (the line advance uses the largest y scale of the
    // line), 32 space; a run also ends after 85 glyphs (its 8 KB of vertices).
    using StrToUl = uint32_t XB_CDECL(const char*, const char**, int32_t);
    using StrToD = float XB_CDECL(const char*, const char**);  // the double, stored by FSTP
    const uint8_t* glyphs = *reinterpret_cast<const uint8_t* const*>(font + 0x28);  // 0x1c bytes each
    const int32_t first_char = *reinterpret_cast<const int32_t*>(font + 0x38);
    auto glyph_width = [&](uint32_t c) {
        return *reinterpret_cast<const uint32_t*>(glyphs + (static_cast<int32_t>(c) - first_char) * 0x1c + 0x18);
    };
    float line_scale = scale_y;
    const float space_width = platform_x87_unsigned_to_float(glyph_width(' '));
    const float start_x = x;
    const float line_height = static_cast<float>(*reinterpret_cast<const int32_t*>(font + 0x30));
    uint32_t total = 0;
    *run_count = 0;
    const char* p = text;
    if (*p == 0) {
        *runs = nullptr;
        return 0;
    }
    NglTextRun** link = runs;
    NglTextRun* run = nullptr;
    uint8_t c = static_cast<uint8_t>(*p);
    for (;;) {
        ++p;
        // Control codes, then a run from the first other character.
        while (c >= 1 && c <= 32) {
            if (c == 1) {
                const uint32_t v = title_function<StrToUl>(0x001f0ee6)(p + 1, &p, 16);  // strtoul
                ++p;
                color = (v << 24) | (v >> 8);
            } else if (c == 2) {
                scale_x = title_function<StrToD>(0x001f0efd)(p + 1, &p);  // strtod
                ++p;
                scale_y = scale_x;
                if (scale_y > line_scale) line_scale = scale_y;
            } else if (c == 3) {
                scale_x = title_function<StrToD>(0x001f0efd)(p + 1, &p);
                scale_y = title_function<StrToD>(0x001f0efd)(p + 1, &p);
                ++p;
                if (scale_y > line_scale) line_scale = scale_y;
            } else if (c == 9) {
                x = space_width * scale_x * title_global<float>(0x007bd0c8) + x;
            } else if (c == 10) {
                x = start_x;
                y = line_height * line_scale + y;
                line_scale = scale_y;
            } else if (c == 32) {
                x = space_width * scale_x + x;
            } else {
                break;  // 4-8, 11-31: drawn as glyphs
            }
            c = static_cast<uint8_t>(*p);
            ++p;
        }
        --p;
        run = alloc_scene_memory<NglTextRun>(sizeof(NglTextRun));
        *link = run;
        ++*run_count;
        run->x = x;
        run->y = y;
        run->scale_x = scale_x;
        run->text = p;
        run->scale_y = scale_y;
        run->color = color;
        link = &run->next;
        uint32_t count = 0;
        uint32_t vertex_bytes = 0x60;
        for (;;) {
            const uint8_t g = static_cast<uint8_t>(*p++);
            if (g <= 3 || g == 9 || g == 10 || g == 32) {
                --p;
                break;
            }
            x = platform_x87_mul_add(platform_x87_unsigned_to_float(glyph_width(g)), scale_x, x);
            vertex_bytes += 0x60;
            ++count;
            if ((vertex_bytes & ~3u) > 0x1ffc) break;
        }
        run->length = static_cast<uint32_t>(p - run->text);
        run->glyph_count = count;
        total += count;
        c = static_cast<uint8_t>(*p);
        if (c == 0) break;
    }
    run->next = nullptr;
    return total;
}

void XB_CDECL nglIndexBuffer_Unlock() {}

void XB_STDCALL nglShader_Nop(uint32_t) {}

uint32_t XB_CDECL WaitTilOutputInst1_str_VFunc2() { return 1; }

void XB_CDECL nglFreeFogTextures() {
    uint8_t& made = title_global<uint8_t>(0x00902e6c);
    if (!made) return;
    static const uint32_t kFogTextures[2] = {0x00902e64, 0x00902e68};
    for (const uint32_t address : kFogTextures) {
        NglTexture* texture = title_global<NglTexture*>(address);
        nglReleaseTextureSurfaces(texture);
        nglReleaseTextureMemory(texture);
    }
    made = 0;
}

namespace {

// A cached render state set through SetRenderState_Simple, its D3D shadow written too.
template <typename T>
void set_cached_simple_state(uint32_t cache, uint32_t method, uint32_t shadow, T value) {
    T& cached = title_global<T>(cache);
    if (cached == value) return;
    title_function<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(method, value);  // SetRenderState_Simple
    title_global<uint32_t>(shadow) = value;
    cached = value;
}

}  // namespace

void XB_CDECL nglDrawFogPass() {
    using SetState = void XB_STDCALL(uint32_t);
    nglAllocFogTextures();
    nglUpdateFogTable();
    uint32_t& z_enable = title_global<uint32_t>(0x00902f80);
    if (z_enable != 0) {
        title_function<SetState>(0x003e8210)(0);  // SetRenderState_ZEnable
        z_enable = 0;
    }
    const uint8_t saved_z_write = title_global<uint8_t>(0x00902f7c);
    if (saved_z_write) set_cached_simple_state<uint8_t>(0x00902f7c, 0x0004035c, 0x003f8428, 0);
    // Alpha test (greater than 0), blending one + (1 - source alpha) * destination.
    set_cached_simple_state<uint8_t>(0x00902f58, 0x00040300, 0x003f8418, 1);
    set_cached_simple_state<uint32_t>(0x00902f60, 0x00040340, 0x003f841c, 0);
    set_cached_simple_state<uint32_t>(0x00902f5c, 0x0004033c, 0x003f8410, 0x204);
    set_cached_simple_state<uint8_t>(0x00902f64, 0x00040304, 0x003f8414, 1);
    set_cached_simple_state<uint32_t>(0x00902f78, 0x00040350, 0x003f8450, 0x8006);
    set_cached_simple_state<uint32_t>(0x00902f68, 0x00040344, 0x003f8420, 1);
    set_cached_simple_state<uint32_t>(0x00902f6c, 0x00040348, 0x003f8424, 0x303);
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
    nglSetTexture(0, &title_global<NglTexture>(0x00902e78), 1, 3);
    nglSetTexture(1, title_global<NglTexture*>(0x00902e70), 1, 3);
    // D3D's texture stage shadows (title memory): stages 0 and 1 clamp in u and v.
    title_global<uint32_t>(0x003f5c68) = 3;
    title_global<uint32_t>(0x003f5c6c) = 3;
    title_global<uint32_t>(0x003f5ce8) = 3;
    title_global<uint32_t>(0x003f8320) |= 3;
    title_global<uint32_t>(0x003f5cec) = 3;
    nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00903c9c));
    nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00903cb8));
    const float width = platform_x87_unsigned_to_float(title_global<uint32_t>(0x00902e88));
    const float height = platform_x87_unsigned_to_float(title_global<uint32_t>(0x00902e8c));
    const float u = platform_x87_mul(title_global<float>(0x008a3510), width);
    const float v = platform_x87_mul(title_global<float>(0x008a3514), height);
    const Lanes color = mul(lanes(current_scene()->fog_color), title_global<float>(0x0083eb8c));  // 255
    uint32_t argb = static_cast<uint32_t>(platform_ftol(color.v[3]));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(color.v[0]));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(color.v[1]));
    argb = (argb << 8) | static_cast<uint32_t>(platform_ftol(color.v[2]));
    // {x, y, z, colour, u, v} as a strip.
    struct Vertex {
        float x, y, z;
        uint32_t color;
        float u, v;
    };
    const Vertex strip[4] = {{0.0f, 0.0f, 0.0f, argb, 0.0f, 0.0f},
                             {width, 0.0f, 0.0f, argb, u, 0.0f},
                             {0.0f, height, 0.0f, argb, 0.0f, v},
                             {width, height, 0.0f, argb, u, v}};
    title_global<uint32_t>(0x00903c88) = 0;
    title_global<uint32_t>(0x00903c8c) = 0;
    title_global<uint32_t>(0x00903c90) = 0;
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
    title_function<void XB_STDCALL(uint32_t, uint32_t, const void*, uint32_t)>(0x003e9710)(
        6, 4, strip, sizeof(strip[0]));  // DrawVerticesUP
    set_cached_simple_state<uint8_t>(0x00902f7c, 0x0004035c, 0x003f8428, saved_z_write);
    if (z_enable != 1) {
        title_function<SetState>(0x003e8210)(1);
        z_enable = 1;
    }
    title_global<uint32_t>(0x00902ff0) = 9;
    title_global<int32_t>(0x00902ff4) = -1;
    title_global<int32_t>(0x00902ff8) = -1;
}

void XB_CDECL nglFreeSceneRenderTargets() {
    for (NglRenderTargetLink* link = current_scene()->render_targets; link; link = link->next) {
        NglTexture* texture = link->texture;
        if (!texture) continue;
        nglReleaseTextureSurfaces(texture);
        nglReleaseTextureMemory(texture);
        if ((texture->flags & 0x1000) && (texture->flags & 0x4000))
            nglReleaseTextureMemory(*reinterpret_cast<NglTexture**>(reinterpret_cast<uint8_t*>(texture) + 0x38));
    }
}

void XB_THISCALL nglInitRenderStates(uint8_t* self) {
    auto u32 = [self](uint32_t offset, uint32_t value) { *reinterpret_cast<uint32_t*>(self + offset) = value; };
    auto u8 = [self](uint32_t offset, uint8_t value) { self[offset] = value; };
    auto f32 = [self](uint32_t offset, float value) { *reinterpret_cast<float*>(self + offset) = value; };
    const float one = title_global<float>(0x0083eb20);
    static const uint32_t kAllOnes[4] = {0x18, 0x1c, 0x4c, 0x8c};
    static const uint32_t kKeep[3] = {0x4, 0x8, 0x10};
    for (const uint32_t offset : kAllOnes) u32(offset, 0xffffffff);
    for (const uint32_t offset : kKeep) u32(offset, 0x1e00);  // D3DSTENCILOP_KEEP
    u32(0xb0, 0x1b02);
    u32(0xa8, 0x1b02);
    u8(0x0, 0);
    u32(0xc, 0x207);
    u32(0x14, 0);
    u8(0x20, 0);
    f32(0x24, one);
    f32(0x28, 0.0f);
    f32(0x38, one);
    u8(0x21, 0);
    f32(0x2c, one);
    f32(0x30, one);
    f32(0x34, one);
    u8(0x3c, 0);
    u32(0x40, 0);
    u32(0x44, 0);
    u32(0x48, 0);
    u8(0x50, 0);
    u32(0x54, 0x204);
    u32(0x58, 0x80);
    u8(0x5c, 1);
    u32(0x60, 0x302);
    u32(0x64, 0x303);
    u32(0x68, 0);
    u32(0x6c, 0);
    u32(0x70, 0x8006);
    u8(0x74, 0);
    u32(0x78, 1);
    u32(0x7c, 0x203);
    f32(0x84, 0.0f);
    f32(0x80, 0.0f);
    for (uint32_t offset = 0x88; offset < 0x8c; ++offset) u8(offset, 0);
    u32(0x94, 1);
    u32(0xa0, 0x1010101);
    u8(0xb4, 0);
    u32(0xa4, 0);
    u32(0xac, 0x1d01);
    u32(0xcc, 0x900);
    u32(0xd0, 0x200);
    u32(0xd4, 1);
    f32(0xd8, one);
    u32(0xdc, 4);
    u8(0xe0, 1);
    u8(0xe1, 0);
    u8(0xe2, 1);
    u8(0xe3, 0);
    u8(0xe4, 0);
    u8(0xe5, 1);
    u8(0xc8, 0);
    u8(0xc9, 1);
    u8(0xe6, 1);
    u8(0x90, 1);
    nglSetDefaultRenderStates(self);
}

void XB_CDECL nglGetTextExtent(const uint8_t* font, const char* text, int32_t* width, int32_t* height,
                               float scale_x, float scale_y) {
    using StrToUl = uint32_t XB_CDECL(const char*, const char**, int32_t);
    using StrToD = float XB_CDECL(const char*, const char**);  // the double, stored by FSTP
    const uint8_t* glyphs = *reinterpret_cast<const uint8_t* const*>(font + 0x28);  // 0x1c bytes each
    const int32_t first_char = *reinterpret_cast<const int32_t*>(font + 0x38);
    const int32_t line_height = *reinterpret_cast<const int32_t*>(font + 0x30);
    auto field = [&](uint32_t c, uint32_t offset) {
        return *reinterpret_cast<const uint32_t*>(glyphs + (static_cast<int32_t>(c) - first_char) * 0x1c + offset);
    };
    // A line's last glyph counts with its drawn extent (+0x10 - +0x18 + +0x8) instead of
    // its advance (+0x18).
    auto finish_line = [&](uint8_t last, float x) {
        const int32_t extent = static_cast<int32_t>(field(last, 0x10) - field(last, 0x18) + field(last, 0x8));
        return static_cast<float>(extent) * scale_x + x;
    };
    float x = 0.0f;
    float widest = 0.0f;
    float total = 0.0f;
    float line_scale = scale_y;
    uint8_t last = 0;
    const char* p = text;
    if (*p != 0) {
        do {
            const uint8_t c = static_cast<uint8_t>(*p++);
            switch (c) {
            case 1:  // colour: no size
                title_function<StrToUl>(0x001f0ee6)(p + 1, &p, 16);  // strtoul
                ++p;
                break;
            case 2:
                scale_x = title_function<StrToD>(0x001f0efd)(p + 1, &p);  // strtod
                ++p;
                scale_y = scale_x;
                if (scale_x > line_scale) line_scale = scale_x;
                break;
            case 3:
                scale_x = title_function<StrToD>(0x001f0efd)(p + 1, &p);
                scale_y = title_function<StrToD>(0x001f0efd)(p + 1, &p);
                ++p;
                if (scale_y > line_scale) line_scale = scale_y;
                break;
            case 9:  // tab: four spaces
                x = platform_x87_mul_mul_add(platform_x87_unsigned_to_float(field(' ', 0x18)), scale_x,
                                             title_global<float>(0x007bd0c8), x);
                last = ' ';
                break;
            case 10:
                if (last) x = finish_line(last, x);
                if (x > widest) widest = x;
                total = static_cast<float>(line_height) * line_scale + total;
                line_scale = scale_y;
                x = 0.0f;
                last = 0;
                break;
            default:
                x = platform_x87_mul_add(platform_x87_unsigned_to_float(field(c, 0x18)), scale_x, x);
                last = c;
                break;
            }
        } while (*p != 0);
        if (last) x = finish_line(last, x);
    }
    if (width) *width = static_cast<int32_t>(static_cast<int64_t>(x > widest ? x : widest));
    if (height)
        *height = static_cast<int32_t>(
            static_cast<int64_t>(platform_x87_add(platform_x87_int_mul(line_height, line_scale), total)));
}

void XB_CDECL nglBeginSceneRender(NglScene* target_scene) {
    using SetState = void XB_STDCALL(uint32_t);
    using SetRenderStateSimple = void XB_FASTCALL(uint32_t, uint32_t);
    // A render state through the title's cache (cache) and D3D's shadow table (shadow).
    auto simple = [](uint32_t method, uint32_t value, uint32_t shadow, uint32_t cache) {
        if (title_global<uint32_t>(cache) == value) return;
        title_function<SetRenderStateSimple>(0x003e89f0)(method, value);
        title_global<uint32_t>(shadow) = value;
        title_global<uint32_t>(cache) = value;
    };
    auto f = [](uint32_t address) { return title_global<float>(address); };
    NglScene* scene = current_scene();
    if (scene->time == 0.0f) {
        const float frame = static_cast<uint8_t>(nglIsPAL50()) ? f(0x0083eb5c) : f(0x007bc670);  // ms per field
        scene->frame_time = platform_x87_unsigned_mul_mul(title_global<uint32_t>(0x009028e8), frame, f(0x0083eb4c));
    } else {
        scene->frame_time = scene->time;
    }

    uint32_t& z_enable = title_global<uint32_t>(0x00902f80);
    uint8_t& stencil_enable = title_global<uint8_t>(0x00902f08);
    if (!target_scene->depth_enable && !target_scene->depth_test) {
        if (z_enable != 0) {
            title_function<SetState>(0x003e8210)(0);  // SetRenderState_ZEnable
            z_enable = 0;
        }
    } else {
        if (z_enable != 1) {
            title_function<SetState>(0x003e8210)(1);
            z_enable = 1;
        }
        if (target_scene->depth_test) {
            simple(0x00040354, 0x203, 0x003f840c, 0x00902f84);  // depth function
            title_function<SetState>(0x003e8650)(0);  // SetRenderState_RopZCmpAlwaysRead
            title_global<uint8_t>(0x00902feb) = 0;
        } else {
            if (!stencil_enable) {
                title_function<SetState>(0x003e82b0)(1);  // SetRenderState_StencilEnable
                stencil_enable = 1;
                simple(0x00040364, 0x207, 0x003f8440, 0x00902f14);  // stencil function
                simple(0x0004036c, 0xff, 0x003f8448, 0x00902f20);
                simple(0x00040378, 0, 0x003f843c, 0x00902f10);
            } else {
                title_function<SetState>(0x003e82b0)(0);
                stencil_enable = 0;
            }
            simple(0x00040354, 0x207, 0x003f840c, 0x00902f84);
            title_function<SetState>(0x003e8650)(1);
            title_global<uint8_t>(0x00902feb) = 1;
        }
    }

    scene = current_scene();
    NglRenderTarget* target = scene->render_target;
    nglSetRenderTargetCached(reinterpret_cast<NglTexture*>(target), scene->render_target_level, nullptr,
                             scene->unknown008);
    float target_width;
    float target_height;
    if (static_cast<uint8_t>(target->flags) & 4) {
        target_width = static_cast<float>(nglGetScreenWidth());
        target_height = static_cast<float>(nglGetScreenHeight());
    } else {
        target_width = platform_x87_unsigned_to_float(target->width);
        target_height = platform_x87_unsigned_to_float(target->height);
    }
    scene = current_scene();
    const float one = f(0x0083eb20);
    const float half = f(0x0083eb24);
    const float* clamped = scene->viewport_clamped;
    const int32_t x0 = platform_x87_add_mul_mul_add_ftol(clamped[0], one, half, target_width, half);
    const int32_t y0 = platform_x87_add_mul_mul_add_ftol(clamped[1], one, half, target_height, half);
    const int32_t x1 = platform_x87_add_mul_mul_add_ftol(clamped[2], one, half, target_width, half);
    const int32_t y1 = platform_x87_add_mul_mul_add_ftol(clamped[3], one, half, target_height, half);
    struct {
        int32_t x, y, width, height;
        float min_z, max_z;
    } viewport;
    if (reinterpret_cast<NglTexture*>(target)->flags & 0x100) {
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = static_cast<int32_t>(static_cast<int64_t>(target_width));
        viewport.height = static_cast<int32_t>(static_cast<int64_t>(target_height));
    } else {
        viewport.x = x0;
        viewport.y = y0;
        viewport.width = x1 - x0;
        viewport.height = y1 - y0;
    }
    viewport.min_z = 0.0f;
    viewport.max_z = one;
    title_function<void XB_STDCALL(const void*)>(0x003e5cc0)(&viewport);  // SetViewport

    scene = current_scene();
    if (scene->clear_flags) {
        simple(0x00040358, 0x1010101, 0x003f8434, 0x00902fa8);  // colour write mask: all
        scene = current_scene();
        const float scale = f(0x0083eb8c);
        float c[4];
        for (int32_t i = 0; i < 4; ++i) c[i] = scene->clear_color[i] * scale;
        auto ftol = [](float v) { return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(v))); };
        uint32_t color = ftol(c[3]) << 8;
        color = (color | ftol(c[0])) << 8;
        color = (color | ftol(c[1])) << 8;
        color |= ftol(c[2]);
        title_function<void XB_STDCALL(uint32_t, const void*, uint32_t, uint32_t, float, uint32_t)>(0x003ead90)(
            0, nullptr, scene->clear_flags, color, scene->clear_depth, scene->flags);  // Clear
        scene = current_scene();
    }
    simple(0x00040358, scene->color_write_mask, 0x003f8434, 0x00902fa8);
    const float frames = current_scene()->frame_time * title_global<float>(0x008a2978);
    current_scene()->frame_counter = static_cast<uint32_t>(platform_x87_round_to_int(frames));
}

void XB_CDECL nglGetTextExtentf(const uint8_t* font, int32_t* width, int32_t* height, const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    title_function<int XB_CDECL(char*, const char*, va_list)>(0x001eea95)(text, format, args);  // _vsprintf
    va_end(args);
    nglGetTextExtent(font, text, width, height, 1.0f, 1.0f);
}

void XB_CDECL nglGetTextSizef(const uint8_t* font, int32_t* width, int32_t* height, float scale_x, float scale_y,
                              const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    title_function<int XB_CDECL(char*, const char*, va_list)>(0x001eea95)(text, format, args);  // _vsprintf
    va_end(args);
    nglGetTextExtent(font, text, width, height, scale_x, scale_y);
}

namespace {

using SetVertexBufferHeader = void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using ResourceRegister = void XB_STDCALL(uint32_t, uint32_t);

// The D3D vertex buffer header (self[2]) over self[0], with a reference count of 1.
void register_vertex_buffer(uint32_t* self, uint32_t data) {
    title_function<SetVertexBufferHeader>(0x003f99e6)(0, 0, 0, 0, self[2], 0);  // XGSetVertexBufferHeader
    title_function<ResourceRegister>(0x003e7fd0)(self[2], data);                // D3DResource_Register
    *reinterpret_cast<uint32_t*>(self[2]) = 1;
}

}  // namespace

uint8_t XB_THISCALL nglCreateVertexBuffer(uint32_t* self, uint32_t size, uint32_t) {
    self[1] = size;
    self[0] = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(size, 1, 0x30000));
    if (!self[0]) return 0;
    self[2] = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(0xc, 4, 0));
    if (!self[2]) {
        title_function<void XB_CDECL(uint32_t)>(0x000939b0)(self[0]);  // Mem_AlignedFree
        return 0;
    }
    register_vertex_buffer(self, self[0]);
    return 1;
}

uint8_t XB_THISCALL nglCreateVertexBufferPhysical(uint32_t* self, uint32_t data, uint32_t size, uint32_t protect) {
    self[0] = data;
    self[1] = size;
    self[2] = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(0xc, 4, 0));
    if (!self[2]) return 0;
    if (protect) title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x001e76aa)(data, size, protect);  // XPhysicalProtect
    register_vertex_buffer(self, data);
    return 1;
}

void XB_THISCALL nglInitTextureHeader(NglTexture* self) {
    if (self->flags & 0x2000) {
        const uint32_t surface = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(0x18, 4, 0));
        self->surfaces = reinterpret_cast<uint32_t*>(surface);
        using SetSurfaceHeader = void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        title_function<SetSurfaceHeader>(0x003f9953)(self->width, self->height, self->format, surface, 0, 0);  // XGSetSurfaceHeader
    } else if (self->flags & 0x10000000) {
        const uint32_t cube = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(0x14, 4, 0));
        self->d3d_texture = reinterpret_cast<uint8_t*>(cube);
        using SetCubeHeader = void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        title_function<SetCubeHeader>(0x003f99be)(self->width, self->levels, 0, self->format, 0, cube, 0, 0);  // XGSetCubeTextureHeader
    } else {
        const uint32_t texture = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(0x14, 4, 0));
        self->d3d_texture = reinterpret_cast<uint8_t*>(texture);
        using SetTextureHeader =
            void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        title_function<SetTextureHeader>(0x003f9994)(self->width, self->height, self->levels, 0, self->format, 0,
                                                     texture, 0, 0);  // XGSetTextureHeader
    }
}

void XB_THISCALL nglTextureGetSurfaces(NglTexture* self) {
    if (self->flags & 0x2000) return;
    const uint32_t texture = reinterpret_cast<uint32_t>(self->d3d_texture);
    self->levels = title_function<uint32_t XB_STDCALL(uint32_t)>(0x003ebcc0)(texture);  // D3DBaseTexture_GetLevelCount
    if (self->flags & 0x10000000) {
        self->surfaces = static_cast<uint32_t*>(Mem_AlignedAlloc(0x18, 4, 0));
        for (uint32_t face = 0; face < 6; ++face) {
            self->surfaces[face] = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(self->levels * 4, 4, 0));
            for (uint32_t level = 0; level < self->levels; ++level) {
                uint32_t* slot = reinterpret_cast<uint32_t*>(self->surfaces[face]) + level;
                *slot = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003ebd70)(
                    reinterpret_cast<uint32_t>(self->d3d_texture), face, level);  // D3DCubeTexture_GetCubeMapSurface2
            }
        }
    } else {
        self->surfaces = static_cast<uint32_t*>(Mem_AlignedAlloc(self->levels * 4, 4, 0));
        for (uint32_t level = 0; level < self->levels; ++level) {
            uint32_t* slot = self->surfaces + level;
            *slot = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t)>(0x003ebcf0)(
                reinterpret_cast<uint32_t>(self->d3d_texture), level);  // D3DTexture_GetSurfaceLevel2
        }
    }
}

void* XB_THISCALL nglAllocTextureMemory(const NglTexture* self) {
    const uint32_t flags = self->flags;
    if (flags & 0x2000) {
        const uint32_t size = self->height * self->width * 4;
        if (flags & 0x400) return nglAllocFrameScratch(size, 0x40);
        return Mem_AlignedAlloc(size, 0x40, 0x30000);
    }
    const uint32_t bytes_per_pixel =
        (title_function<uint32_t XB_STDCALL(uint32_t)>(0x003fa187)(self->format) << 3) >> 3;  // XGBytesPerPixelFromFormat
    uint32_t size = 0;
    if (flags & 0x10000000) {
        for (int face = 6; face; --face) {
            uint32_t width = self->width, height = self->height;
            for (uint32_t level = self->levels; level; --level) {
                size += height * width * bytes_per_pixel;
                width >>= 1;
                height >>= 1;
            }
            size += size & 0x7f;
        }
    } else {
        uint32_t width = self->width, height = self->height;
        for (uint32_t level = self->levels; level; --level) {
            size += height * width * bytes_per_pixel;
            width >>= 1;
            height >>= 1;
        }
    }
    if (flags & 0x400) return nglAllocFrameScratch(size, 0x80);
    return Mem_AlignedAlloc(size, 0x80, 0x30000);
}

uint32_t* XB_CDECL nglCreatePalette(uint32_t, uint32_t count, uint32_t entries) {
    uint32_t* palette = static_cast<uint32_t*>(Mem_AlignedAlloc(0x10, 4, 0));
    palette[0] = palette[1] = palette[2] = palette[3] = 0;
    palette[1] = count;
    uint32_t size = 0;  // D3DPALETTE_256; also any other count
    switch (count) {
    case 0x20: size = 3; break;
    case 0x40: size = 2; break;
    case 0x80: size = 1; break;
    case 0x100: size = 0; break;
    }
    if (entries) {
        reinterpret_cast<uint8_t*>(palette)[8] = 1;
    } else {
        entries = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(count * 4, 0x40, 0x30000));
        reinterpret_cast<uint8_t*>(palette)[8] = 0;
    }
    palette[0] = entries;
    palette[3] = reinterpret_cast<uint32_t>(Mem_AlignedAlloc(0xc, 4, 0));
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003f99fe)(size, palette[3], 0);  // XGSetPaletteHeader
    title_function<ResourceRegister>(0x003e7fd0)(palette[3], palette[0]);                      // D3DResource_Register
    *reinterpret_cast<uint32_t*>(palette[3]) = 0x00030001;
    return palette;
}

void XB_CDECL nglMeshBuilder_Begin(uint32_t flags, uint32_t section_count, uint32_t material_count,
                                   const void* materials) {
    // Allocator (0x00902928): nglAllocFrameGpu or the Mem_AlignedAlloc wrapper at 0x00066af0.
    // Mesh: +4 flags | 0x20000, +8 section count, +0xc sections {1, 0x60-byte section},
    // +0x10 material count, +0x14 materials.
    using Alloc = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t);
    uint32_t& allocator = title_global<uint32_t>(0x00902928);
    allocator = (flags & 0x40000) ? 0x00066ad0 : 0x00066af0;
    uint32_t* mesh = reinterpret_cast<uint32_t*>(title_function<Alloc>(allocator)(0x40, 0x10, 0));
    std::memset(mesh, 0, 0x40);
    mesh[1] = flags | 0x20000;
    mesh[2] = section_count;
    mesh[3] = title_function<Alloc>(allocator)(section_count * 8, 0x10, 0);
    uint32_t* sections = reinterpret_cast<uint32_t*>(mesh[3]);
    for (uint32_t i = 0; i < section_count; ++i) {
        sections[i * 2 + 1] = title_function<Alloc>(allocator)(0x60, 0x10, 0);
        std::memset(reinterpret_cast<void*>(sections[i * 2 + 1]), 0, 0x60);
        sections[i * 2] = 1;
    }
    if (material_count) {
        mesh[4] = material_count;
        mesh[5] = title_function<Alloc>(allocator)(material_count << 6, 0x10, 0);
        std::memcpy(reinterpret_cast<void*>(mesh[5]), materials, material_count << 6);
    }
    alignas(16) float center[4];
    std::memcpy(center, &title_global<float>(0x007bda20), sizeof(center));
    title_global<uint32_t*>(0x00902920) = mesh;
    title_global<uint32_t>(0x00902924) = 0;
    nglMeshBuilder_SetBounds(center, from_bits(0x749dc5ae));
}

uint32_t XB_CDECL nglMeshBuilder_AddSection(uint32_t* material, uint32_t primitive_type, uint32_t vertex_count,
                                            uint32_t, uint32_t index_count, const uint16_t* indices,
                                            uint32_t stride) {
    uint32_t* mesh = title_global<uint32_t*>(0x00902920);
    uint32_t& current = title_global<uint32_t>(0x00902924);
    uint8_t* section = reinterpret_cast<uint8_t*>(reinterpret_cast<uint32_t*>(mesh[3])[current * 2 + 1]);
    auto field = [section](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(section + offset); };
    material[1] = reinterpret_cast<uint32_t>(section);
    field(0x5c) = reinterpret_cast<uint32_t>(material);
    field(0x4) = primitive_type;
    field(0x28) = 6;
    field(0x50) = stride;
    field(0x30) = title_global<uint32_t>(0x0090296c) + title_global<uint32_t>(0x00902964) * 2;
    if (title_global<uint32_t*>(0x00902920)[1] & 0x40000) {
        field(0x24) = 0x40000;
        uint32_t& offset = title_global<uint32_t>(0x0090295c);
        const uint32_t misalign = offset % stride;
        if (misalign) offset += stride - misalign;
        title_global<uint32_t>(0x00902960) = offset;
        field(0x54) = offset;
        field(0x44) = title_global<uint32_t>(0x00902970);
        field(0x48) = title_global<uint32_t>(0x00902974);
        field(0x4c) = title_global<uint32_t>(0x00902978);
        offset += vertex_count * stride;
    } else {
        nglCreateVertexBuffer(&field(0x44), vertex_count * stride, 0);
    }
    if (index_count) {
        using Alloc = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t);
        field(0xc) = title_function<Alloc>(title_global<uint32_t>(0x00902928))(index_count * 2, 0x10, 0);
        std::memcpy(reinterpret_cast<void*>(field(0xc)), indices, index_count * 2);
        field(0x8) = index_count;
    }
    title_global<uint8_t*>(0x00902930) = section;
    return ++current;
}

uint32_t XB_CDECL nglMeshBuilder_AddIndexRun(uint16_t* indices, uint32_t count, uint32_t base) {
    uint32_t i = 0;
    for (; i < count; ++i) {
        uint32_t& written = title_global<uint32_t>(0x00902964);
        indices[written] = static_cast<uint16_t>(base + i);
        ++written;
    }
    return i;
}

void XB_CDECL nglMeshBuilder_AddStrip(uint32_t count, uint32_t stride) {
    uint8_t* section = title_global<uint8_t*>(0x00902930);
    auto field = [section](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(section + offset); };
    const uint32_t base = title_global<uint32_t>(0x00902960) / stride + field(0x40);
    uint16_t* indices = title_global<uint16_t*>(0x0090296c);
    uint32_t added = count;
    if (field(0x2c)) {
        // Joined to the previous strip by degenerate triangles: repeat its last index and
        // this strip's first, once more after a strip of odd length (keeps the winding).
        const bool even = !(title_global<uint8_t>(0x00902958) & 1);
        added = even ? count + 2 : count + 3;
        uint32_t& written = title_global<uint32_t>(0x00902964);
        indices[written] = indices[written - 1];
        ++written;
        indices[written] = static_cast<uint16_t>(base);
        ++written;
        if (!even) {
            indices[written] = static_cast<uint16_t>(base);
            ++written;
        }
    }
    nglMeshBuilder_AddIndexRun(indices, count, base);
    title_global<uint32_t>(0x00902958) = count;
    field(0x2c) += added;
    field(0x40) += count;
}

PLATFORM_ASM_ENTRY(nglPolyBuilder_SetVertex_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x18(%esp)\n"
                   "pushl 0x14(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(nglPolyBuilder_SetVertex) "addl $32, %esp\n"
                   "ret $20")

void XB_CDECL nglPolyBuilder_SetVertex(const uint32_t* builder, uint32_t x, uint32_t y, uint32_t z, uint32_t color,
                                       uint32_t u, uint32_t v) {
    const uint8_t* section = reinterpret_cast<const uint8_t*>(
        uintptr_t{*reinterpret_cast<const uint32_t*>(uintptr_t{builder[1] + 4})});
    const auto field = [section](uint32_t offset) { return *reinterpret_cast<const uint32_t*>(section + offset); };
    uint32_t* vertex = reinterpret_cast<uint32_t*>(uintptr_t{field(0x54) + builder[2] * 0x18 + field(0x44)});
    vertex[0] = x;
    vertex[1] = y;
    vertex[2] = z;
    vertex[5] = color;
    vertex[3] = u;
    vertex[4] = v;
}

void XB_THISCALL nglVertexBuffer_Unlock(uint32_t*) {}

uint32_t XB_THISCALL nglVertexBuffer_GetD3D(const uint32_t* self) { return self[2]; }

void XB_THISCALL nglDynamicVB_BeginFrame(uint32_t* self) {
    const uint32_t index = self[0x30 / 4];
    uint32_t* current = self + 0x38 / 4;
    current[0] = self[index * 3];
    current[1] = self[index * 3 + 1];
    current[2] = self[index * 3 + 2];
    self[0x34 / 4] = self[0x18 / 4 + index];
    if (nglVertexBuffer_GetD3D(current))
        current[0] = reinterpret_cast<uint32_t>(nglLockVertexBuffer(reinterpret_cast<uint8_t*>(current), 0, 0));
}

void XB_THISCALL nglDynamicVB_EndFrame(uint32_t* self) {
    self[0x24 / 4] = 0;
    self[0x28 / 4] = 0;
    self[0x20 / 4] = 0;
    self[0x2c / 4] = 0;
    self[0x30 / 4] ^= 1;
    nglVertexBuffer_Unlock(self + 0x38 / 4);
}

uint8_t* XB_CDECL nglQuadBatch_Reserve(int32_t quads) {
    if (title_global<int32_t>(0x00909f48) + quads > title_global<int32_t>(0x00909f34)) {
        nglVertexBuffer_Unlock(title_global<uint32_t* [2]>(0x00909f20)[title_global<uint32_t>(0x00909f2c)]);
        title_global<uint32_t>(0x00909f44) = 1;
        return nullptr;
    }
    title_global<int32_t>(0x00909f30) = quads;
    return title_global<uint8_t*>(0x00909f3c);
}

uint8_t XB_THISCALL nglIndexBuffer_Alloc(uint8_t** self, uint32_t size) {
    *self = static_cast<uint8_t*>(Mem_AlignedAlloc(size, 4, 0));
    return *self != nullptr;
}

uint32_t* XB_CDECL nglCreateDDSPalette(const uint8_t* header, uint32_t** out, uint32_t entries) {
    uint32_t* palette = nglCreatePalette(0, *reinterpret_cast<const uint16_t*>(header + 0xc), entries);
    *out = palette;
    return palette;
}

int32_t XB_REGS2 XB_CDECL nglFontParseKeyInt(const char* key, char** cursor, int32_t base) {
    const uint32_t length = static_cast<uint32_t>(std::strlen(key));
    while (**cursor) {
        uint32_t i = 0;
        const char* text = *cursor;
        while (i < length && text[i] && text[i] == key[i]) ++i;
        if (i == length) {
            *cursor += length;
            return title_function<int32_t XB_CDECL(const char*, char**, int32_t)>(0x001f0ecf)(*cursor, cursor, base);  // _strtol
        }
        ++*cursor;
    }
    return 0;
}

void XB_CDECL nglTexture_Unlock() {}

void XB_CDECL nglPalette_Unlock() {}

void XB_CDECL nglBeginFrame_Nop() {}

namespace {

void alloc_texture(NglTexture* texture) {
    nglRegisterTexture(texture, reinterpret_cast<uint32_t>(nglAllocTextureMemory(texture)));
}

}  // namespace

void XB_CDECL nglAllocFogTextures() {
    title_global<uint8_t>(0x00902e6c) = 1;
    static const uint32_t kSlots[2] = {0x00902e64, 0x00902e68};
    for (int i = 0; i < 2; ++i) {
        NglTexture* texture = title_global<NglTexture*>(kSlots[i]);
        if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(texture) + 0x24)) continue;
        alloc_texture(texture);
        nglTextureGetSurfaces(texture);
    }
}

void XB_CDECL nglAllocSceneRenderTargets() {
    for (NglRenderTargetLink* link = current_scene()->render_targets; link; link = link->next) {
        NglTexture* texture = link->texture;
        if (!texture) continue;
        alloc_texture(texture);
        if ((texture->flags & 0x1000) && (texture->flags & 0x4000))
            alloc_texture(*reinterpret_cast<NglTexture**>(reinterpret_cast<uint8_t*>(texture) + 0x38));
        nglTextureGetSurfaces(texture);
    }
}

uint32_t* XB_CDECL nglMeshBuilder_End() {
    uint32_t* mesh = title_global<uint32_t*>(0x00902920);
    if (mesh[1] & 0x40000) return mesh;
    const uint32_t* sections = reinterpret_cast<uint32_t*>(mesh[3]);
    const uint16_t* base = *reinterpret_cast<uint16_t* const*>(reinterpret_cast<uint8_t*>(sections[1]) + 0x30);
    for (uint32_t i = 0; i < mesh[2]; ++i) {
        uint8_t* section = reinterpret_cast<uint8_t*>(reinterpret_cast<uint32_t*>(mesh[3])[i * 2 + 1]);
        const uint32_t count = *reinterpret_cast<uint32_t*>(section + 0x2c);
        uint8_t** buffer = reinterpret_cast<uint8_t**>(section + 0x30);
        const uint8_t* indices = *buffer;
        nglIndexBuffer_Alloc(buffer, count * 2);
        nglIndexBuffer_Lock(buffer, 0, 0);
        nglIndexBuffer_Write(buffer, indices, count * 2, 0);
        nglIndexBuffer_Unlock();
        uint16_t* out = reinterpret_cast<uint16_t*>(nglIndexBuffer_Lock(buffer, 0, 0));
        for (uint32_t j = 0; j < *reinterpret_cast<uint32_t*>(section + 0x2c); ++j) out[j] -= *base;
        nglIndexBuffer_Unlock();
    }
    return title_global<uint32_t*>(0x00902920);
}

PLATFORM_ASM_ENTRY(nglParseFontFile_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(nglParseFontFile) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL nglParseFontFile(NglFont* font, char* text) {
    font->version = nglFontParseKeyInt("version", &text, 0x10);
    font->cell_height = nglFontParseKeyInt("cellheight", &text, 10);
    font->ascent = nglFontParseKeyInt("ascent", &text, 10);
    font->first_glyph = nglFontParseKeyInt("firstglyph", &text, 10);
    font->glyph_count = nglFontParseKeyInt("numglyphs", &text, 10);
    font->glyphs = static_cast<NglFontGlyph*>(Mem_AlignedAlloc(font->glyph_count * sizeof(NglFontGlyph), 4, 0));
    font->uvs = static_cast<float(*)[4]>(Mem_AlignedAlloc(font->glyph_count * 16, 4, 0));
    const float one = title_global<float>(0x0083eb20);
    const float scale_u = platform_x87_div_by_unsigned(one, font->texture->width);
    const float scale_v = platform_x87_div_by_unsigned(one, font->texture->height);
    using Strtol = int32_t XB_CDECL(const char*, char**, int32_t);
    for (int32_t i = 0; i < font->glyph_count; ++i) {
        while (*text && (*text < '0' || *text > '9')) ++text;
        const int32_t code = title_function<Strtol>(0x001f0ecf)(text, &text, 10);  // _strtol
        NglFontGlyph* glyph = font->glyphs + (code - font->first_glyph);
        glyph->x = nglFontParseKeyInt("x", &text, 10);
        glyph->y = nglFontParseKeyInt("y", &text, 10);
        glyph->advance = nglFontParseKeyInt("w", &text, 10);
        glyph->offset_x = nglFontParseKeyInt("gx", &text, 10);
        glyph->offset_y = nglFontParseKeyInt("gy", &text, 10);
        glyph->width = nglFontParseKeyInt("gw", &text, 10);
        glyph->height = nglFontParseKeyInt("gh", &text, 10);
        float* uv = font->uvs[i];
        const float u0 = static_cast<float>(glyph->x) * scale_u;
        const float v0 = static_cast<float>(glyph->y) * scale_v;
        uv[0] = u0;
        uv[1] = v0;
        uv[2] = static_cast<float>(glyph->width) * scale_u + u0;
        uv[3] = static_cast<float>(glyph->height) * scale_v + v0;
    }
    return static_cast<uint32_t>(font->glyph_count);
}

NglFont* XB_CDECL nglLoadFontFile(const NglHashedName* name, char* text) {
    NglFont* font = static_cast<NglFont*>(Mem_AlignedAlloc(sizeof(NglFont), 4, 0));
    std::memset(font, 0, sizeof(NglFont));
    font->refs = 1;
    font->texture_flags = 2;
    font->blend_mode = 2;
    std::memcpy(font->name, name, sizeof(font->name));
    font->texture = static_cast<NglTexture*>(nglFindTextureAddRef(reinterpret_cast<const char*>(name)));
    nglParseFontFile(font, text);
    return font;
}

NglFont* XB_CDECL nglLoadFont(const NglHashedName* name, char* text) {
    using Add = void XB_THISCALL(NglResourceManager*, void*);
    NglFont* font = static_cast<NglFont*>(
        manager_slot<ManagerFind>(font_manager(), 2)(font_manager(), reinterpret_cast<const char*>(name)));
    if (font) {
        ++font->refs;
        return font;
    }
    font = nglLoadFontFile(name, text);
    if (!font) return title_global<NglFont*>(0x009028d4);  // ngl_sysFont
    manager_slot<Add>(font_manager(), 3)(font_manager(), font);
    return font;
}

uint8_t XB_CDECL nglParseDDSPalette(uint32_t, uint8_t* texture) {
    uint8_t* const data = *reinterpret_cast<uint8_t**>(texture + 0x128);
    uint8_t*& header = *reinterpret_cast<uint8_t**>(texture + 0x134);
    uint8_t*& entries = *reinterpret_cast<uint8_t**>(texture + 0x138);
    uint32_t*& palettes = *reinterpret_cast<uint32_t**>(texture + 0x13c);
    header = data;
    entries = data + 0x10;
    using Strncmp = int32_t XB_CDECL(const void*, const char*, uint32_t);
    if (title_function<Strncmp>(0x001eeaf0)(data, "DDSP", 4) || *reinterpret_cast<uint32_t*>(data + 4) != 1)
        return 0;
    const uint16_t& count = *reinterpret_cast<uint16_t*>(data + 8);
    palettes = static_cast<uint32_t*>(Mem_AlignedAlloc(count * 4u, 4, 0));
    uint32_t pad = reinterpret_cast<uintptr_t>(entries) & 0x3f;
    if (pad) pad = 0x40 - pad;
    if (!data[0xf]) {
        nglSwapRedBlue(entries + pad, static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(data + 0xc)) * count);
        data[0xf] = 1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t stride = *reinterpret_cast<uint16_t*>(data + 0xa);
        nglCreateDDSPalette(data, reinterpret_cast<uint32_t**>(palettes + i),
                            reinterpret_cast<uintptr_t>(entries + stride * i + pad));
    }
    return 1;
}

void XB_CDECL nglUpdateFogTable() {
    NglScene* scene = current_scene();
    float* const cached = &title_global<float>(0x008a30fc);
    // Only the first changed fog value is taken over; the rest follow on later frames.
    int changed = -1;
    for (int k = 0; k < 4; ++k) {
        if (!(cached[k] == scene->fog[k])) {
            changed = k;
            break;
        }
    }
    if (changed < 0) return;
    cached[changed] = scene->fog[changed];
    NglTexture* table = title_global<NglTexture*>(0x00902e70);
    const int32_t count = static_cast<int32_t>(table->height * table->width);
    const float start = scene->fog[0];
    const float end = scene->fog[1];
    uint32_t x = 0;
    uint32_t y = 0;
    nglTexture_Lock(table, 0);
    if (count > 0) {
        const float total = static_cast<float>(count);
        for (int32_t i = 0; i < count; ++i) {
            const NglScene* s = current_scene();
            const float t = (s->far_z - s->near_z) / s->far_z * (static_cast<float>(i) / total);
            const float z = s->near_z / (title_global<float>(0x0083eb20) - t);
            float density;
            if (start > z)
                density = s->fog[2];
            else if (z > end)
                density = s->fog[3];
            else
                density = (z - start) / (end - start) * (s->fog[3] - s->fog[2]) + s->fog[2];
            const uint8_t level = static_cast<uint8_t>(platform_x87_round_to_int(density * title_global<float>(0x0083eb8c)));
            nglTextureSetPixel(reinterpret_cast<const uint8_t*>(title_global<NglTexture*>(0x00902e70)), x, y,
                               static_cast<uint8_t>(0xff - level));
            ++y;
            if (y % title_global<NglTexture*>(0x00902e70)->height == 0) {  // the height (+0x14), as the title has it
                ++x;
                y = 0;
            }
        }
    }
    nglTexture_Unlock();
}

NglTexture* XB_CDECL nglCreateTexture(uint32_t flags, uint32_t width, uint32_t height, uint32_t mip_levels,
                                     uint32_t data) {
    NglTexture* texture = static_cast<NglTexture*>(Mem_AlignedAlloc(0x74, 4, 0));
    if (!texture) return nullptr;
    std::memset(texture, 0, 0x74);
    char text[0x80];
    title_function<int XB_CDECL(char*, const char*, uint32_t)>(0x001ee901)(  // _sprintf
        text, "created texture %d", title_global<uint32_t>(0x00902e3c)++);
    *reinterpret_cast<uint32_t*>(texture) = 1;
    uint8_t hashed[0x20];
    const uint8_t* name =
        title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, text);  // HashStr_Init
    std::memcpy(texture->name, name, 0x20);
    const bool bit12 = (flags >> 12) & 1;
    const bool bit14 = (flags >> 14) & 1;
    texture->width = width;
    texture->height = height;
    texture->flags = flags;
    texture->levels = mip_levels + 1;
    if (flags & 0x100)
        texture->format = title_global<uint32_t>(0x008a2f88 + (flags & 0xff) * 4);
    else if (flags & 0x200)
        texture->format = title_global<uint32_t>(0x008a2f48 + (flags & 0xff) * 4);
    nglInitTextureHeader(texture);
    const uint32_t render_target = flags & 0x400;
    if (!render_target) {
        if (!data) data = reinterpret_cast<uint32_t>(nglAllocTextureMemory(texture));
        nglRegisterTexture(texture, data);
        nglTextureGetSurfaces(texture);
    }
    if (bit12 && bit14) texture->companion = nglCreateTexture(render_target ? 0x260d : 0x220d, width, height, 0, 0);
    if ((flags & 0xff) == 7) {
        uint32_t* palette = nglCreatePalette(0, 0x100, 0);
        texture->palette = reinterpret_cast<uint8_t*>(palette);
        nglLockPalette(reinterpret_cast<uint8_t*>(palette));
        for (uint32_t i = 0; i < 0x100; ++i)
            nglPaletteSetEntry(reinterpret_cast<uint32_t**>(texture->palette), i, 0xffffffff);
        nglPalette_Unlock();
    }
    texture->last_used_frame = -1;
    return texture;
}

namespace {

uint8_t* load_builtin_texture(const char* name, uint32_t data, uint32_t size) {
    uint8_t* copy = static_cast<uint8_t*>(Mem_AlignedAlloc(size, 0x80, 0x30000));
    std::memcpy(copy, &title_global<uint8_t>(data), size);
    uint8_t hashed[0x20];
    title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, name);  // HashStr_Init
    return title_function<uint8_t* XB_CDECL(uint8_t*, uint32_t, uint8_t*, uint32_t)>(0x000635a0)(  // nglLoadTexture
        hashed, 0, copy, size);
}

}  // namespace

void XB_CDECL nglInitTextureManager() {
    title_global<uint8_t>(0x009025f8) = 0;
    const auto create_manager = [](uint32_t vtable) -> uint32_t {
        uint32_t* manager = static_cast<uint32_t*>(Mem_AlignedAlloc(0x14, 4, 0));
        if (!manager) return 0;
        manager[0] = vtable;
        manager[1] = static_cast<uint32_t>(title_function<int XB_CDECL()>(0x001efcc0)());  // _rand
        manager[2] = 7;
        manager[3] = 0;
        return reinterpret_cast<uintptr_t>(manager);
    };
    title_global<uint32_t>(0x009025f0) = create_manager(0x007bc940);
    title_global<uint32_t>(0x00903d10) = create_manager(0x007bc98c);
    uint8_t* texture = load_builtin_texture("ngldefault", 0x008a1e40, 0xb38);
    title_global<uint8_t*>(0x009026f8) = texture;
    *reinterpret_cast<uint32_t*>(texture + 0x28) |= 2;
    nglCreateWhiteTexture();
}

uint8_t* XB_CDECL nglCreateSysFont() { return load_builtin_texture("nglSysFont", 0x008a56e0, 0x155d6); }

void XB_CDECL nglCreateWhiteTexture() {
    NglTexture*& white = title_global<NglTexture*>(0x00902700);
    white = nglCreateTexture(0x109, 1, 1, 0, 0);
    nglTexture_Lock(white, 0);
    nglTextureSetPixel(reinterpret_cast<const uint8_t*>(white), 0, 0, 0xff);
    nglTexture_Unlock();
    uint8_t hashed[0x20];
    const uint8_t* name =
        title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, "nglwhite");  // HashStr_Init
    std::memcpy(white->name, name, 0x20);
    reinterpret_cast<uint32_t*>(white)[0x28 / 4] |= 2;
    using Add = void XB_THISCALL(NglResourceManager*, void*);
    manager_slot<Add>(texture_manager(), 3)(texture_manager(), white);
}

void XB_CDECL nglCreateFogTextures(uint32_t width, uint32_t height, uint32_t fog_width, uint32_t fog_height) {
    title_global<uint32_t>(0x00902e7c) = 0x201;
    title_global<NglTexture*>(0x00902e70) = nglCreateTexture(0x109, fog_width, fog_height, 0, 0);
    for (uint32_t slot = 0x00902e64; slot < 0x00902e6c; slot += 4) {
        NglTexture* texture = nglCreateTexture(0x1601, width, height, 0, 0);
        title_global<NglTexture*>(slot) = texture;
        reinterpret_cast<uint32_t*>(texture)[0x28 / 4] |= 2;
    }
    const uint32_t texture_width = title_global<uint32_t>(0x00902dd8);
    using SetTextureHeader =
        void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<SetTextureHeader>(0x003f9994)(texture_width, title_global<uint32_t>(0x00902ddc), 1, 0, 0x12, 0,
                                                 0x00902e50, title_global<uint32_t*>(0x00902dfc)[1],
                                                 texture_width * 4);  // XGSetTextureHeader
    title_global<uint32_t>(0x00902ea8) = 0x00902e50;
    title_global<uint32_t>(0x00902e88) = title_global<uint32_t>(0x00902d60);
    title_global<uint32_t>(0x00902e8c) = title_global<uint32_t>(0x00902d64);
}

uint8_t* XB_CDECL Map_EmptyKey(int32_t size) {
    if (size > 0x20) title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
    uint8_t& ready = title_global<uint8_t>(0x0090228c);
    if (!ready) {
        for (uint32_t offset = 0; offset < 0x20; offset += 4) title_global<uint32_t>(0x0090226c + offset) = 0;
        ready = 1;
    }
    return &title_global<uint8_t>(0x0090226c);
}

uint32_t* XB_STDCALL UintMap_Find(const uint32_t* map, const uint32_t* key) {
    // {count, entries {key, value}}: an implicit binary tree (children 2i+1 below, 2i+2 above);
    // an empty-key entry ends the search.
    uint32_t i = 0;
    while (i < map[0]) {
        uint32_t* entries = reinterpret_cast<uint32_t*>(uintptr_t{map[1]});
        if (entries[i * 2] == *key) return &entries[i * 2 + 1];
        i = entries[i * 2] >= *key ? i * 2 + 1 : i * 2 + 2;
        if (i >= map[0]) break;
        const uint32_t* empty = reinterpret_cast<const uint32_t*>(Map_EmptyKey(8));
        if (entries[i * 2] == empty[0] && entries[i * 2 + 1] == empty[1]) break;
    }
    return nullptr;
}

int32_t XB_THISCALL nglSkipList_RandomLevel(NglSkipList* self) {
    int32_t level = 0;
    int32_t pair;
    do {
        const int32_t bits = self->random_bits;
        pair = bits & 3;
        if (pair == 0) ++level;
        self->random_bits = bits >> 2;
        if (--self->bits_left == 0) {
            self->random_bits = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // rand
            self->bits_left = 7;
        }
    } while (pair == 0);
    return level > 15 ? 15 : level;
}

void XB_THISCALL nglSkipList_InitHead(NglSkipList* self) {
    self->level = 0;
    self->head = static_cast<NglSkipListNode*>(Mem_AlignedAlloc(0x44, 4, 0));
    for (int i = 0; i < 16; ++i) self->head->next[i] = nullptr;
}

void XB_CDECL nglFlushDebugLineBatch() {
    if (!title_global<uint8_t*>(0x00902ca0)) return;
    if (current_scene()->stale) nglUpdateScene(0);
    uint8_t* node = title_global<uint8_t*>(0x00902ca0);
    field<uint32_t>(node, 0x14) = title_global<uint32_t>(0x00902ca4);
    field<uint32_t>(node, 0x18) = title_global<uint32_t>(0x008a2f08);
    field<uint32_t>(node, 0x1c) = title_global<uint32_t>(0x008a2f0c);
    field<int32_t>(node, 0x8) = -1;
    field<NglNode*>(node, 0x4) = current_scene()->opaque_nodes;
    current_scene()->opaque_nodes = reinterpret_cast<NglNode*>(node);
    ++current_scene()->opaque_count;
    title_global<uint8_t*>(0x00902ca0) = nullptr;
    title_global<uint32_t>(0x00902ca4) = 0;
}

void XB_THISCALL nglTexture_FreeHeaders(NglTexture* texture) {
    using Free = void XB_CDECL(void*);
    if (texture->d3d_texture) {
        title_function<Free>(0x000939b0)(texture->d3d_texture);  // Mem_AlignedFree
        texture->d3d_texture = nullptr;
    }
    if (texture->surfaces) {
        title_function<Free>(0x000939b0)(texture->surfaces);  // Mem_AlignedFree
        texture->surfaces = nullptr;
    }
}

void XB_THISCALL nglIndexBuffer_Free(uint8_t** self) {
    title_function<void XB_CDECL(void*)>(0x000939b0)(*self);  // Mem_AlignedFree
    *self = nullptr;
}

// --- Texture loading (DDS files, animated lists) --------------------------------------

namespace {

uint32_t& texture_word(NglTexture* texture, uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(texture) + offset);
}

void title_free(uint32_t memory) { title_function<void XB_CDECL(uint32_t)>(0x000939b0)(memory); }  // Mem_AlignedFree

}  // namespace

void XB_CDECL nglSwizzleTextureLevels(NglTexture* texture, uint32_t* data) {
    const uint32_t format = texture->format;
    if (format == 0xc || format == 0xe || format == 0xf) return;  // DXT: stored as they are
    const uint32_t cube = texture->flags & 0x10000000;
    const uint32_t d3d_texture = address_of(texture->d3d_texture);
    for (uint32_t level = 0; level < texture->levels; ++level) {
        uint32_t surface;
        if (cube)
            surface = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003ebd70)(
                d3d_texture, 0, level);  // D3DCubeTexture_GetCubeMapSurface2
        else
            surface = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t)>(0x003ebcf0)(
                d3d_texture, level);  // D3DTexture_GetSurfaceLevel2
        uint32_t desc[7];  // D3DSURFACE_DESC: Format, Type, Usage, Size, MultiSampleType, Width, Height
        title_function<uint32_t XB_STDCALL(uint32_t, uint32_t*)>(0x003ebe30)(surface, desc);  // D3DSurface_GetDesc
        const uint32_t bytes_per_pixel =
            title_function<uint32_t XB_STDCALL(uint32_t)>(0x003fa187)(desc[0]);  // XGBytesPerPixelFromFormat
        title_function<uint32_t XB_STDCALL(uint32_t)>(0x003e7ef0)(surface);  // D3DResource_Release
        const uint32_t buffer = address_of(Mem_AlignedAlloc(desc[3] + 0x10, 4, 0));
        const uint32_t aligned = (buffer + 0xf) & ~0xfu;
        using SwizzleRect = void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                            uint32_t);
        title_function<SwizzleRect>(0x003fa203)(*data, 0, 0, aligned, desc[5], desc[6], 0,
                                                bytes_per_pixel);  // XGSwizzleRect
        std::memcpy(reinterpret_cast<void*>(uintptr_t{*data}), reinterpret_cast<const void*>(uintptr_t{aligned}),
                    desc[3]);
        title_free(buffer);
        *data += desc[3];
    }
}

bool XB_CDECL nglLoadDDS(NglTexture* texture, uint8_t* dds) {
    const auto* header = reinterpret_cast<const uint32_t*>(dds);
    const uint32_t magic = header[0];
    const bool multi = magic == 0x4d534444;  // "DDSM": a palette per frame
    if ((magic != 0x20534444 && !multi) || header[1] != 0x7c) return false;
    if (header[0x1c] & 0xfe00) {  // caps2: cube map faces
        texture->flags |= 0x10000000;
        if (header[0x1c] != 0xfe00) return false;
    }
    uint32_t data = address_of(dds) + 0x80;
    uint32_t palette_count = 0;
    if (multi) {
        palette_count = *reinterpret_cast<const uint16_t*>(dds + 0x80);
        uint32_t records = address_of(dds) + 0x82;
        records += 8 - ((records - address_of(dds)) & 7);
        const uint32_t end = records + palette_count * 0x20;
        data = end + (0x80 - ((end - address_of(dds)) & 0x7f));
        texture_word(texture, 0x50) = address_of(Mem_AlignedAlloc(palette_count * 0x74, 4, 0));
        for (uint32_t i = 0; i < palette_count; ++i) {
            if (dds[0x7f] != 'M') nglSwapRedBlue(reinterpret_cast<uint8_t*>(uintptr_t{data}), 0x100);
            auto* frame = reinterpret_cast<NglTexture*>(uintptr_t{texture_word(texture, 0x50) + i * 0x74});
            std::memcpy(frame, texture, 0x74);
            frame->flags = 0x10;
            std::memcpy(frame->name, reinterpret_cast<const void*>(uintptr_t{records + i * 0x20}), 0x20);
            texture_word(frame, 0x4c) = address_of(texture);
            texture_word(frame, 0x50) = i;
            frame->palette = reinterpret_cast<uint8_t*>(nglCreatePalette(0, 0x100, data));
            manager_slot<void XB_THISCALL(NglResourceManager*, void*)>(texture_manager(), 3)(texture_manager(), frame);
            data += 0x400;
        }
    } else {
        texture_word(texture, 0x4c) = 0;
        texture_word(texture, 0x50) = 0;
    }
    texture->width = header[4];
    texture->height = header[3];
    texture->levels = header[7];
    texture_word(texture, 0x4c) = palette_count & 0xffff;
    const uint32_t flags = texture->flags | 0x100;
    texture->format = 0xffffffff;
    texture->flags = flags;
    uint32_t bits = texture_word(texture, 0x28);
    bits ^= ((header[8] << 4) ^ bits) & 0x10;
    bits ^= ((header[8] << 4) ^ bits) & 0x20;
    texture_word(texture, 0x28) = bits;
    if (!texture->levels) texture->levels = 1;
    const uint32_t depth = (header[2] & 0x800000) ? header[6] : 0;
    bool compressed = false;
    if (!depth) {
        compressed = true;
        switch (header[0x15]) {  // FourCC
        case 0x31545844: texture->format = 0xc; break;  // DXT1
        case 0x32545844:                                // DXT2
        case 0x33545844: texture->format = 0xe; break;  // DXT3
        case 0x34545844:                                // DXT4
        case 0x35545844: texture->format = 0xf; break;  // DXT5
        default: compressed = false; break;
        }
        if (compressed) texture->flags = flags | 1;
    }
    if (!compressed) {
        const uint32_t pixel_flags = header[0x14];
        const uint32_t bit_count = header[0x16];
        if (pixel_flags == 0x41 && bit_count == 0x20 && header[0x1a] == 0xff000000) {
            texture->format = 6;
            texture->flags = flags | 1;
        } else if (pixel_flags == 0x40 && bit_count == 0x10 && header[0x18] == 0x7e0) {
            texture->format = 5;
            texture->flags = flags | 5;
        } else if (pixel_flags == 0x41 && bit_count == 0x10 && header[0x1a] == 0x8000) {
            texture->format = 2;
            texture->flags = flags | 3;
        } else if (pixel_flags == 0x41 && bit_count == 0x10 && header[0x1a] == 0xf000) {
            texture->format = 4;
            texture->flags = flags | 2;
        } else {
            if (bit_count != 8) return false;
            if (pixel_flags == 0x20000) {
                texture->format = 0;
                texture->flags = flags | 9;
            } else if (pixel_flags == 2) {
                texture->format = 0x19;
                texture->flags = flags | 0xa;
            } else if (pixel_flags == 0x20001) {
                texture->format = 1;
                texture->flags = flags | 0xb;
            } else {
                texture->flags = flags | 7;
                texture->format = 0xb;
                if (!multi) {
                    if (dds[0x7f] != 'M') nglSwapRedBlue(reinterpret_cast<uint8_t*>(uintptr_t{data}), 0x100);
                    texture->palette = reinterpret_cast<uint8_t*>(nglCreatePalette(0, 0x100, data));
                    data += 0x400;
                }
            }
        }
        if (depth) return false;
    }
    if (texture->flags & 0x10000000) {
        nglInitTextureHeader(texture);
        nglRegisterTexture(texture, data);
        if (dds[0x7f] != 'M') {
            for (int face = 6; face; --face) {
                nglSwizzleTextureLevels(texture, &data);
                data += data & 0x7f;
            }
        }
    } else {
        nglInitTextureHeader(texture);
        nglRegisterTexture(texture, data);
        if (dds[0x7f] != 'M') nglSwizzleTextureLevels(texture, &data);
    }
    dds[0x7f] = 'M';  // swizzled: a second load of the same file skips it
    return true;
}

bool XB_CDECL nglLoadDDSTexture(NglTexture* texture, uint8_t* dds) {
    if (!nglLoadDDS(texture, dds)) return false;
    nglTextureGetSurfaces(texture);
    texture->last_used_frame = -1;
    return true;
}

int32_t XB_THISCALL nglTextCursor_NextToken(NglTextCursor* self, char* out, int32_t size) {
    const auto isspace = title_function<int XB_CDECL(int)>(0x001eefa0);  // _isspace
    while (self->cursor - self->begin < self->length && isspace(*self->cursor) && *self->cursor != '\n')
        ++self->cursor;
    const char* start = self->cursor;
    while (self->cursor - self->begin < self->length && !isspace(*self->cursor)) ++self->cursor;
    int32_t length = static_cast<int32_t>(self->cursor - start);
    if (length >= size - 1) length = size - 1;
    title_function<char* XB_CDECL(char*, const char*, int32_t)>(0x001eed80)(out, start, length);  // _strncpy
    out[length] = 0;
    return length;
}

bool XB_CDECL nglLoadAnimatedTextureList(NglTexture* texture, const char* text, int32_t size) {
    uint8_t name[0x20] = {};
    auto* list = static_cast<uint32_t*>(Mem_AlignedAlloc(0x1000, 4, 0));
    uint8_t& busy = title_global<uint8_t>(0x00902704);
    if (busy) return false;
    busy = 1;
    int32_t count = 0;
    NglTextCursor cursor{text, text, size};
    if (size > 0) {
        do {
            char token[0x20];
            if (nglTextCursor_NextToken(&cursor, token, 0x1f)) {
                char* dot = title_function<char* XB_CDECL(const char*, int)>(0x001ef5e0)(token, '.');  // _strchr
                if (dot) *dot = 0;
                uint8_t hashed[0x20];
                std::memcpy(name,
                            title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, token),
                            0x20);  // HashStr_Init
                int32_t repeat = 1;
                char number[0x40];
                if (nglTextCursor_NextToken(&cursor, number, 0x20)) {
                    const int32_t value = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(number);  // _atol
                    repeat = value <= 0 ? 0 : value;
                }
                const int32_t total = repeat + count;
                if (total > 0x400) break;
                using Find = uint32_t* XB_THISCALL(NglResourceManager*, const uint8_t*);
                uint32_t* frame = manager_slot<Find>(texture_manager(), 2)(texture_manager(), name);
                if (frame)
                    ++frame[0];
                else
                    frame = manager_slot<Find>(texture_manager(), 7)(texture_manager(), name);
                if (repeat > 0) {
                    for (int32_t i = 0; i < repeat; ++i) list[count + i] = address_of(frame);
                    count = total;
                }
            }
            int32_t offset = static_cast<int32_t>(cursor.cursor - cursor.begin);
            while (offset < cursor.length && *cursor.cursor != '\n') {
                ++cursor.cursor;
                ++offset;
            }
            ++cursor.cursor;
        } while (cursor.cursor - cursor.begin < cursor.length);
        if (count) {
            texture->flags = 0xf;
            texture_word(texture, 0x4c) = static_cast<uint32_t>(count);
            auto* frames = static_cast<uint32_t*>(Mem_AlignedAlloc(static_cast<uint32_t>(count) * 4, 4, 0));
            texture_word(texture, 0x50) = address_of(frames);
            std::memcpy(frames, list, static_cast<uint32_t>(count) * 4);
            texture->width = reinterpret_cast<const NglTexture*>(uintptr_t{frames[0]})->width;
            texture->height = reinterpret_cast<const NglTexture*>(uintptr_t{frames[0]})->height;
        }
    }
    busy = 0;
    title_free(address_of(list));
    return count != 0;
}

NglTexture* XB_CDECL nglLoadTextureFile(const uint8_t* name, uint32_t kind, uint8_t* data, uint32_t size) {
    auto* texture = static_cast<NglTexture*>(Mem_AlignedAlloc(0x74, 4, 0));
    std::memset(texture, 0, 0x74);
    *reinterpret_cast<uint32_t*>(texture) = 1;
    std::memcpy(texture->name, name, 0x20);
    texture_word(texture, 0xc) = kind;
    bool loaded = false;
    switch (kind) {
    case 0:
    case 1: loaded = nglLoadDDSTexture(texture, data); break;
    case 2:
        title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*, uint32_t, uint8_t**)>(0x003acbbc)(
            title_global<uint32_t>(0x00902874), data, size, &texture->d3d_texture);  // D3DXCreateTextureFromFileInMemory
        return texture;
    case 3: loaded = nglLoadAnimatedTextureList(texture, reinterpret_cast<const char*>(data), size); break;
    default: break;
    }
    if (loaded) return texture;
    title_free(address_of(texture));
    return nullptr;
}

NglTexture* XB_CDECL nglLoadTexture(const uint8_t* name, uint32_t kind, uint8_t* data, uint32_t size) {
    using Find = NglTexture* XB_THISCALL(NglResourceManager*, const uint8_t*);
    NglTexture* texture = manager_slot<Find>(texture_manager(), 2)(texture_manager(), name);
    if (texture) {
        ++*reinterpret_cast<uint32_t*>(texture);
        texture_word(texture, 0x28) |= 1;
        return texture;
    }
    texture = nglLoadTextureFile(name, kind, data, size);
    if (!texture) return title_global<NglTexture*>(0x009026f8);  // ngl_defaultTexture
    manager_slot<void XB_THISCALL(NglResourceManager*, void*)>(texture_manager(), 3)(texture_manager(), texture);
    texture_word(texture, 0x28) |= 1;
    return texture;
}

uint32_t XB_THISCALL nglVec4ToColor(const float* color) {
    const auto channel = [](float x) -> uint32_t {
        if (x >= 1.0f) return 0xff;
        if (0.0f >= x) return 0;
        return static_cast<uint32_t>(platform_ftol(platform_x87_mul_add(x, 255.0f, 0.5f)));
    };
    const uint32_t r = channel(color[0]);
    const uint32_t g = channel(color[1]);
    const uint32_t b = channel(color[2]);
    const uint32_t a = channel(color[3]);
    return ((((a << 8) | r) << 8 | g) << 8) | b;
}

uint32_t* XB_THISCALL nglMaterial_Relocate4(uint8_t* material, uint32_t delta) {
    uint32_t value = 0;
    for (uint32_t offset = 0x1c; offset <= 0x34; offset += 8) {
        uint32_t& word = *reinterpret_cast<uint32_t*>(material + offset);
        value = word;
        if (value) word = value += delta;
    }
    return reinterpret_cast<uint32_t*>(uintptr_t{value});
}

uint32_t XB_THISCALL nglMaterial_LoadThreeTexturesAndOptional(uint8_t* material) {
    auto* words = reinterpret_cast<uint32_t*>(material);
    for (int i = 0; i < 3; ++i) {
        const uint32_t name = words[0x1c / 4 + 2 * i];
        words[0x20 / 4 + 2 * i] =
            name ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                       nglMaterialFindTexture(reinterpret_cast<const char*>(uintptr_t{name}))))
                 : 0;
    }
    const uint32_t name = words[0x34 / 4];
    const uint32_t texture = name ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(nglMaterialFindTextureOrDefault(
                                        reinterpret_cast<const char*>(uintptr_t{name}))))
                                  : 0;
    words[0x38 / 4] = texture;
    if (texture == title_global<uint32_t>(0x009026f8)) words[0x38 / 4] = 0;  // ngl_defaultTexture
    return texture;
}

void XB_THISCALL nglMaterial_ReleaseThreeTexturesAndOptional(uint8_t* material) {
    auto* words = reinterpret_cast<uint32_t*>(material);
    for (int i = 0; i < 3; ++i) {
        if (words[0x20 / 4 + 2 * i]) {
            nglReleaseTexture(reinterpret_cast<void*>(uintptr_t{words[0x20 / 4 + 2 * i]}));
            words[0x20 / 4 + 2 * i] = 0;
        }
    }
    if (words[0x38 / 4]) {
        nglReleaseTexture(reinterpret_cast<void*>(uintptr_t{words[0x38 / 4]}));
        words[0x38 / 4] = 0;
    }
}

void XB_CDECL nglInitMeshFileCache() {
    auto* cache = static_cast<uint32_t*>(Mem_AlignedAlloc(0x14, 4, 0));
    if (!cache) {
        title_global<uint32_t>(0x00903d40) = 0;
        return;
    }
    cache[0] = 0x007c1284;                                          // vtable
    cache[1] = title_function<uint32_t XB_CDECL()>(0x001efcc0)();  // _rand
    cache[2] = 7;
    cache[3] = 0;
    title_global<uint32_t>(0x00903d40) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cache));
}

void* XB_CDECL nglFindMeshPreferFile(const uint32_t* name, const uint8_t* file) {
    for (const uint8_t* mesh = *reinterpret_cast<uint8_t* const*>(file + 0x138); mesh;
         mesh = *reinterpret_cast<uint8_t* const*>(mesh + 0x38)) {
        const uint32_t* mesh_name = *reinterpret_cast<const uint32_t* const*>(mesh);
        int i = 0;
        while (i < 8 && mesh_name[i] == name[i]) ++i;
        if (i == 8) return const_cast<uint8_t*>(mesh);
    }
    return manager_slot<ManagerFind>(mesh_manager(), 2)(mesh_manager(), reinterpret_cast<const char*>(name));
}

namespace {

// nglSkipList_Insert/InsertTexture: the 32-byte name at `name_offset` in each object.
const uint32_t* skip_list_insert(NglSkipList* self, const uint32_t* object, uint32_t name_offset) {
    if (!self->head) nglSkipList_InitHead(self);
    const auto name_of = [name_offset](const uint32_t* o) {
        return reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(o) + name_offset);
    };
    const uint32_t* key = name_of(object);
    NglSkipListNode* update[17];
    NglSkipListNode* current = self->head;
    NglSkipListNode* node;
    int32_t level = self->level;
    do {
        node = current->next[level];
        while (node) {
            const uint32_t* other = name_of(node->key);
            int i = 0;
            while (i < 8 && other[i] == key[i]) ++i;
            if (i == 8 || key[i] < other[i]) break;
            current = node;
            node = current->next[level];
        }
        update[level] = current;
    } while (--level >= 0);
    if (node) {
        const uint32_t* other = name_of(node->key);
        for (int i = 0; other[i] == key[i];) {
            if (++i >= 8) return node->key;
        }
    }
    int32_t height = nglSkipList_RandomLevel(self);
    if (height > self->level) {
        height = self->level + 1;
        self->level = height;
        update[height] = self->head;
    }
    auto* fresh = static_cast<NglSkipListNode*>(Mem_AlignedAlloc(static_cast<uint32_t>(height) * 4 + 8, 4, 0));
    fresh->key = object;
    for (int32_t i = height; i >= 0; --i) {
        fresh->next[i] = update[i]->next[i];
        update[i]->next[i] = fresh;
    }
    return nullptr;
}

}  // namespace

const uint32_t* XB_THISCALL nglSkipList_Insert(NglSkipList* self, const uint32_t* object) {
    return skip_list_insert(self, object, 0);
}

const uint32_t* XB_THISCALL nglSkipList_InsertTexture(NglSkipList* self, const uint32_t* texture) {
    return skip_list_insert(self, texture, 0x54);
}

namespace {

// The name of a listed object: its first 32 bytes, at +0x54 (textures), or what its first
// word points to (indirect).
enum class SkipName { Direct, Texture, Indirect, At8, At16 };

const uint32_t* skip_name(const uint32_t* object, SkipName kind) {
    switch (kind) {
    case SkipName::Texture: return object + 0x54 / 4;
    case SkipName::At8: return object + 2;    // the anim managers' lists (AnimManager_SkipListRemove)
    case SkipName::At16: return object + 4;
    case SkipName::Indirect: return reinterpret_cast<const uint32_t*>(uintptr_t{object[0]});
    case SkipName::Direct: break;
    }
    return object;
}

bool skip_list_remove(NglSkipList* self, const uint32_t* object, SkipName kind) {
    if (!self->head) return false;
    const uint32_t* key = skip_name(object, kind);
    NglSkipListNode* update[17];
    NglSkipListNode* current = self->head;
    NglSkipListNode* node;
    const int32_t top = self->level;
    for (int32_t level = top; level >= 0; --level) {
        node = current->next[level];
        while (node) {
            const uint32_t* other = skip_name(node->key, kind);
            int i = 0;
            while (i < 8 && other[i] == key[i]) ++i;
            if (i == 8 || key[i] < other[i]) break;
            current = node;
            node = current->next[level];
        }
        update[level] = current;
    }
    if (!node || node->key != object) return false;
    for (int32_t i = 0; i <= top && update[i]->next[i] == node; ++i) update[i]->next[i] = node->next[i];
    title_function<void XB_CDECL(void*)>(0x000939b0)(node);  // Mem_AlignedFree
    int32_t level = top;
    while (level > 0 && !self->head->next[level]) --level;
    self->level = level;
    return true;
}

}  // namespace

bool XB_THISCALL nglSkipList_Remove(NglSkipList* self, const uint32_t* object) {
    return skip_list_remove(self, object, SkipName::Direct);
}

void XB_CDECL nglInitFontManager() {
    auto* manager = static_cast<uint32_t*>(Mem_AlignedAlloc(0x14, 4, 0));
    if (manager) {
        manager[0] = 0x007bd0f8;                                          // vtable
        manager[1] = title_function<uint32_t XB_CDECL()>(0x001efcc0)();  // _rand
        manager[2] = 7;
        manager[3] = 0;
    }
    title_global<uint32_t>(0x009028d0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(manager));
    *reinterpret_cast<uint32_t*>(nglCreateSysFont() + 0x28) |= 2;
    NglHashedName name;
    title_function<uint8_t* XB_THISCALL(NglHashedName*, const char*)>(0x00011060)(&name, "nglSysFont");  // HashStr_Init
    using Find = uint8_t* XB_THISCALL(NglResourceManager*, const NglHashedName*);
    uint8_t* font = manager_slot<Find>(font_manager(), 2)(font_manager(), &name);
    uint8_t*& sys_font = title_global<uint8_t*>(0x009028d4);
    if (font) {
        ++*reinterpret_cast<uint32_t*>(font + 0x20);
    } else {
        font = reinterpret_cast<uint8_t*>(nglLoadFontFile(&name, reinterpret_cast<char*>(uintptr_t{0x008a4690})));
        if (!font) {
            font = sys_font;
        } else {
            manager_slot<void XB_THISCALL(NglResourceManager*, void*)>(font_manager(), 3)(font_manager(), font);
        }
    }
    sys_font = font;
    font[0x50] = 1;
}

void XB_CDECL nglInitMeshManagers() {
    title_global<uint8_t>(0x00902980) = 0;
    // The three managers: skip lists with their own vtables, seeded from rand().
    static const uint32_t kManagers[3][2] = {
        {0x00902a84, 0x007bdf84},  // mesh files
        {0x00902a80, 0x007bdfd0},  // meshes
        {0x00902a88, 0x007be01c},  // section-3 records
    };
    for (const auto& manager : kManagers) {
        auto* object = static_cast<uint32_t*>(Mem_AlignedAlloc(0x14, 4, 0));
        if (object) {
            object[0] = manager[1];
            object[1] = title_function<uint32_t XB_CDECL()>(0x001efcc0)();  // _rand
            object[2] = 7;
            object[3] = 0;
        }
        title_global<uint32_t>(manager[0]) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
    }
    ShaderCache_Init(reinterpret_cast<ShaderCache*>(uintptr_t{0x00902ad4}));
    ShaderCache_Init(reinterpret_cast<ShaderCache*>(uintptr_t{0x00902ac0}));
    nglInitMeshFileCache();
}

namespace {

uint32_t& word_at(uint32_t address, uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(uintptr_t{address + offset});
}

// HashStrMap_Find (0x00093f10, thiscall map) with a stack copy of the 32-byte name.
uint32_t hash_map_find(uint32_t map, uint32_t name) {
    uint32_t key[8];
    for (int i = 0; i < 8; ++i) key[i] = word_at(name, i * 4);
    return title_function<uint32_t XB_THISCALL(uint32_t, const uint32_t*)>(0x00093f10)(map, key);
}

uint32_t vtable_slot(uint32_t object, int slot) { return word_at(word_at(object, 0), slot * 4); }

// nglParseMesh's bounds pass: each mesh's bone matrices (+0x14, count +0x10) inverted in
// place as rigid transforms (packed: rows 0..2 transposed with lane 3 = lane 2, then
// -t * R^T), the bounding box of the meshes' spheres (+0x20 centre, +0x30 radius), and
// the LOD list (+0x1c, count +0x18) resolved to meshes.
void parse_mesh_bounds(uint8_t* file) {
    Lanes mins = {{from_bits(0x749dc5ae), from_bits(0x749dc5ae), from_bits(0x749dc5ae), from_bits(0x749dc5ae)}};
    Lanes maxs;
    for (int k = 0; k < 4; ++k) maxs.v[k] = from_bits(0xf49dc5ae);
    bool any = false;
    for (uint32_t mesh = texture_word(reinterpret_cast<NglTexture*>(file), 0x138); mesh; mesh = word_at(mesh, 0x38)) {
        if (word_at(mesh, 4) & 0x10000) continue;
        if (!word_at(mesh, 0x10)) {
            word_at(mesh, 4) |= 0x10000;
        } else {
            for (uint32_t i = 0; i < word_at(mesh, 0x10); ++i) {
                float* m = reinterpret_cast<float*>(uintptr_t{word_at(mesh, 0x14) + i * 0x40});
                const Lanes t0 = {{m[0], m[4], m[8], m[8]}};
                const Lanes t1 = {{m[1], m[5], m[9], m[9]}};
                const Lanes t2 = {{m[2], m[6], m[10], m[10]}};
                Lanes nt;
                for (int k = 0; k < 4; ++k) {
                    uint32_t bits;
                    std::memcpy(&bits, &m[12 + k], 4);
                    nt.v[k] = from_bits(bits ^ 0x80000000u);
                }
                const Lanes row3 = add(add(mul(t0, nt.v[0]), mul(t1, nt.v[1])), mul(t2, nt.v[2]));
                for (int k = 0; k < 4; ++k) {
                    m[k] = t0.v[k];
                    m[4 + k] = t1.v[k];
                    m[8 + k] = t2.v[k];
                    m[12 + k] = row3.v[k];
                }
            }
            const float* sphere = reinterpret_cast<const float*>(uintptr_t{mesh + 0x20});
            const float radius = sphere[4];
            const Lanes r = {{radius, radius, radius, radius}};
            const Lanes low = sub(lanes(sphere), r);
            const Lanes high = add(r, lanes(sphere));
            for (int k = 0; k < 4; ++k) {
                mins.v[k] = low.v[k] < mins.v[k] ? low.v[k] : mins.v[k];    // MINPS
                maxs.v[k] = high.v[k] > maxs.v[k] ? high.v[k] : maxs.v[k];  // MAXPS
            }
            any = true;
        }
        for (uint32_t i = 0; i < word_at(mesh, 0x18); ++i) {
            const uint32_t lods = word_at(mesh, 0x1c);
            const uint32_t found = address_of(nglFindMeshPreferFile(
                reinterpret_cast<const uint32_t*>(uintptr_t{word_at(lods, i * 8)}), file));
            word_at(lods, i * 8) = found;
            if (!word_at(word_at(mesh, 0x1c), i * 8)) {
                --i;
                --word_at(mesh, 0x18);
            }
        }
    }
    if (!any) return;
    const Lanes center = mul(add(mins, maxs), from_bits(0x3f000000));
    const Lanes d = sub(center, mins);
    const Lanes dd = mul(d, d);
    const float s = (dd.v[0] + dd.v[1]) + dd.v[2];
    float radius = s;
    if (!(s == 0.0f || std::isunordered(s, 0.0f))) {
        const float y = platform_rsqrt_estimate(s);
        const float sy = s * y;
        radius = ((from_bits(0x40400000) - sy * y) * sy) * from_bits(0x3f000000);
    }
    for (uint32_t mesh = texture_word(reinterpret_cast<NglTexture*>(file), 0x138); mesh; mesh = word_at(mesh, 0x38)) {
        if (word_at(mesh, 4) & 0x10000) continue;
        float* sphere = reinterpret_cast<float*>(uintptr_t{mesh + 0x20});
        for (int k = 0; k < 4; ++k) sphere[k] = center.v[k];
        sphere[4] = radius;
        word_at(mesh, 4) |= 0x10000;
    }
}

}  // namespace

bool XB_CDECL nglParseMesh(const uint32_t* name, uint8_t* file, const char* extension) {
    (void)name;
    (void)extension;
    const uint32_t header = texture_word(reinterpret_cast<NglTexture*>(file), 0x124);
    texture_word(reinterpret_cast<NglTexture*>(file), 0x134) = header;
    texture_word(reinterpret_cast<NglTexture*>(file), 0x144) = 0xffffffff;
    using Strncmp = int XB_CDECL(const char*, const char*, uint32_t);
    if (title_function<Strncmp>(0x001eeaf0)(reinterpret_cast<const char*>(uintptr_t{header}),
                                            reinterpret_cast<const char*>(uintptr_t{0x007be06c}), 4) ||  // "XBXM"
        word_at(header, 4) != 0x601 || !word_at(header, 8))
        return false;
    const uint32_t file_address = address_of(file);
    const uint32_t delta = texture_word(reinterpret_cast<NglTexture*>(file), 0x124) - word_at(header, 0x10);
    if (word_at(header, 0xc)) word_at(header, 0xc) += delta;
    texture_word(reinterpret_cast<NglTexture*>(file), 0x138) = 0;
    texture_word(reinterpret_cast<NglTexture*>(file), 0x13c) = 0;
    texture_word(reinterpret_cast<NglTexture*>(file), 0x140) = 0;
    uint32_t previous_mesh = 0, previous_material = 0;
    if (word_at(header, 8)) {
        for (uint32_t i = 0; i < word_at(header, 8); ++i) {
            const uint32_t section = word_at(header, 0xc) + i * 0xc;
            if (word_at(section, 4)) word_at(section, 4) += delta;
            if (word_at(section, 8)) word_at(section, 8) += delta;
            const uint8_t type = *reinterpret_cast<const uint8_t*>(uintptr_t{section + 3});
            if (type == 1) {
                const uint32_t record = word_at(section, 4);
                if (word_at(record, 0)) word_at(record, 0) += delta;
                if (word_at(record, 4)) word_at(record, 4) += delta;
                word_at(record, 8) = file_address;
                if (!texture_word(reinterpret_cast<NglTexture*>(file), 0x13c))
                    texture_word(reinterpret_cast<NglTexture*>(file), 0x13c) = record;
                if (previous_material) word_at(previous_material, 0xc) = record;
                previous_material = record;
                if (!word_at(header, 0x10)) {
                    const uint32_t node = hash_map_find(0x00902ac0, word_at(record, 4));
                    if (node && title_function<uint8_t XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(word_at(node, 0x20), 6))(
                                    word_at(node, 0x20), record))
                        word_at(record, 4) = word_at(node, 0x20);
                    else
                        word_at(record, 4) = 0x008a2e20;
                }
                title_function<void XB_THISCALL(uint32_t, uint32_t, uint32_t)>(vtable_slot(word_at(record, 4), 5))(
                    word_at(record, 4), record, delta);
                title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(word_at(record, 4), 3))(
                    word_at(record, 4), record);
            } else if (type == 2) {
                const uint32_t mesh = word_at(section, 4);
                if (word_at(mesh, 0)) word_at(mesh, 0) += delta;
                manager_slot<void XB_THISCALL(NglResourceManager*, uint32_t)>(mesh_manager(), 3)(mesh_manager(), mesh);
                word_at(mesh, 0x34) = file_address;
                if (!texture_word(reinterpret_cast<NglTexture*>(file), 0x138))
                    texture_word(reinterpret_cast<NglTexture*>(file), 0x138) = mesh;
                if (previous_mesh) word_at(previous_mesh, 0x38) = mesh;
                previous_mesh = mesh;
                if (!(word_at(mesh, 4) & 0x10000))
                    nglRelocateMesh(reinterpret_cast<uint8_t*>(uintptr_t{delta}), nullptr,
                                    reinterpret_cast<uint8_t*>(uintptr_t{mesh}));
                for (uint32_t j = 0; j < word_at(mesh, 8); ++j) {
                    word_at(word_at(mesh, 0xc), j * 8) = 1;
                    const uint32_t part = word_at(word_at(mesh, 0xc), j * 8 + 4);
                    if (word_at(part, 0)) word_at(part, 0) += delta;
                    uint32_t key[8];
                    for (int k = 0; k < 8; ++k) key[k] = word_at(word_at(part, 0), k * 4);
                    const uint32_t found = address_of(nglMeshFileFindMesh(key, file));
                    word_at(part, 4) = found;
                    const uint32_t material = word_at(found, 4);
                    if (!title_function<uint8_t XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(material, 7))(material, part)) {
                        uint8_t scratch[0x40] = {};
                        const uint32_t object = word_at(word_at(part, 4), 4);
                        title_function<void XB_THISCALL(uint32_t, uint8_t*)>(vtable_slot(object, 1))(object, scratch);
                        word_at(word_at(part, 4), 4) = 0x008a2e20;
                    }
                    nglCreateVertexBufferPhysical(reinterpret_cast<uint32_t*>(uintptr_t{part + 0x44}), word_at(part, 0x44),
                                                  word_at(part, 0x48), 0x404);
                    if (word_at(part, 0x38)) {
                        title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x001e76aa)(  // XPhysicalProtect
                            word_at(part, 0x38), word_at(part, 0x3c), 0x404);
                        auto* resource = static_cast<uint32_t*>(Mem_AlignedAlloc(0x18, 4, 0));
                        word_at(part, 0x34) = address_of(resource);
                        for (int k = 0; k < 6; ++k) resource[k] = 0;
                        word_at(word_at(part, 0x34), 0) = 0x20001;
                        word_at(word_at(part, 0x34), 0xc) = word_at(part, 0x3c);
                        word_at(word_at(part, 0x34), 0x10) = word_at(part, 0x3c);
                        title_function<void XB_STDCALL(uint32_t, uint32_t)>(0x003e7fd0)(  // D3DResource_Register
                            word_at(part, 0x34), word_at(part, 0x38));
                    }
                    if (word_at(part, 0x5c)) {
                        const uint32_t node = hash_map_find(0x00902ad4, word_at(word_at(part, 0x5c), 0));
                        if (node) {
                            word_at(word_at(part, 0x5c), 4) = part;
                            title_function<void XB_CDECL(uint32_t)>(word_at(node, 0x20))(word_at(part, 0x5c));
                        } else {
                            word_at(part, 0x5c) = 0;
                        }
                    }
                    if (word_at(part, 4) && word_at(word_at(part, 4), 4)) {
                        const uint32_t object = word_at(word_at(part, 4), 4);
                        title_function<void XB_THISCALL(uint32_t, uint32_t)>(vtable_slot(object, 8))(object, part);
                    }
                }
            } else if (type == 3) {
                nglMeshFile_RelocateSection3(file, reinterpret_cast<const uint32_t*>(uintptr_t{section}), delta);
            }
        }
        if (previous_mesh) word_at(previous_mesh, 0x38) = 0;
        if (previous_material) word_at(previous_material, 0xc) = 0;
    }
    parse_mesh_bounds(file);
    word_at(header, 0x10) = texture_word(reinterpret_cast<NglTexture*>(file), 0x124);
    return true;
}

uint8_t* XB_CDECL nglCreateMeshFromFile(const uint32_t* name, uint32_t data, const char* extension) {
    auto* file = static_cast<uint8_t*>(Mem_AlignedAlloc(0x148, 4, 0));
    file[0x20] = 0;
    std::memcpy(file, name, 0x20);
    texture_word(reinterpret_cast<NglTexture*>(file), 0x120) = 1;  // reference count
    file[0x130] = 1;
    texture_word(reinterpret_cast<NglTexture*>(file), 0x124) = data;
    texture_word(reinterpret_cast<NglTexture*>(file), 0x128) = 0;
    texture_word(reinterpret_cast<NglTexture*>(file), 0x12c) = 0;
    using ParseMesh = uint8_t XB_CDECL(const uint32_t*, uint8_t*, const char*);
    if (title_function<ParseMesh>(0x00067950)(name, file, extension)) return file;  // nglParseMesh
    title_free(address_of(file));
    return nullptr;
}

uint8_t* XB_CDECL nglLoadMeshFile(const uint32_t* name, uint32_t data) {
    using Find = uint8_t* XB_THISCALL(NglResourceManager*, const uint32_t*);
    uint8_t* file = manager_slot<Find>(mesh_file_manager(), 2)(mesh_file_manager(), name);
    if (file) {
        ++texture_word(reinterpret_cast<NglTexture*>(file), 0x120);
        return file;
    }
    file = nglCreateMeshFromFile(name, data, reinterpret_cast<const char*>(uintptr_t{0x008386b8}));  // ".xbmesh"
    if (!file) return nullptr;
    manager_slot<void XB_THISCALL(NglResourceManager*, void*)>(mesh_file_manager(), 3)(mesh_file_manager(), file);
    return file;
}

uint32_t* XB_CDECL nglMeshFile_RelocateSection3(uint8_t* file, const uint32_t* section, uint32_t base) {
    auto* record = reinterpret_cast<uint32_t*>(uintptr_t{section[1]});
    for (int i = 0; i <= 4; i += 2)
        if (record[i]) record[i] += base;
    using Add = void XB_THISCALL(NglResourceManager*, void*);
    auto* manager = title_global<NglResourceManager*>(0x00902a88);
    manager_slot<Add>(manager, 3)(manager, record);
    record[3] = address_of(file);
    if (!texture_word(reinterpret_cast<NglTexture*>(file), 0x140))
        texture_word(reinterpret_cast<NglTexture*>(file), 0x140) = address_of(record);
    for (uint32_t i = 0; i < record[1]; ++i) {
        auto* group = reinterpret_cast<uint32_t*>(uintptr_t{record[2]}) + i * 3;  // {?, count, entries}
        if (group[2]) group[2] += base;
        for (uint32_t j = 0; j < group[1]; ++j) {
            auto* entry = reinterpret_cast<uint32_t*>(uintptr_t{group[2] + j * 0x88 + 8});
            for (int k = 0; k < 8; ++k, entry += 4) {
                for (int w = 0; w < 3; ++w)
                    if (entry[w]) entry[w] += base;
            }
        }
    }
    auto* first = reinterpret_cast<uint32_t*>(uintptr_t{record[2]});
    first[0] = 2;
    return first;
}

bool XB_THISCALL nglSkipList_RemoveTexture(NglSkipList* self, const uint32_t* texture) {
    return skip_list_remove(self, texture, SkipName::Texture);
}

bool XB_THISCALL nglSkipList_RemoveIndirect(NglSkipList* self, const uint32_t* object) {
    return skip_list_remove(self, object, SkipName::Indirect);
}

// ---- Day 15: the renderer's bottom-up ready list ----

void XB_CDECL nglShutdown() {}

void XB_CDECL nglUpdateScene_Nop() {}

void XB_CDECL nglReadBackBufferSize() {
    const uint32_t surface = **reinterpret_cast<uint32_t* const*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00902d84)));
    uint32_t desc[8];
    title_function<uint32_t XB_STDCALL(uint32_t, uint32_t*)>(0x003ebe30)(surface, desc);  // D3DSurface_GetDesc
    const uint32_t width = desc[5];
    const uint32_t height = desc[6];
    title_global<uint32_t>(0x00902d60) = width;
    title_global<uint32_t>(0x00902d64) = height;
    title_global<uint32_t>(0x00902dd8) = width;
    title_global<uint32_t>(0x00902ddc) = height;
    nglInitDepthTextureView();
}

void XB_CDECL nglResetDevice() {
    title_function<void XB_STDCALL()>(0x003e6720)();                    // D3DDevice_PersistDisplay
    title_function<void XB_STDCALL(uint32_t)>(0x003e51f0)(0x00902750);  // D3DDevice_Reset(presentation parameters)
    nglReadBackBufferSize();
}

const uint32_t* XB_THISCALL nglResourceManager_Add(uint8_t* self, const uint32_t* object) {
    return nglSkipList_Insert(reinterpret_cast<NglSkipList*>(self + 4), object);
}

namespace {

// The managers' skip list teardown: removes the first node's object until the list is empty,
// then frees the head node.
template <bool (XB_THISCALL* Remove)(NglSkipList*, const uint32_t*)>
void clear_skip_list(NglSkipList* self) {
    if (!self->head) return;
    while (NglSkipListNode* first = self->head->next[0]) Remove(self, first->key);
    title_function<void XB_CDECL(void*)>(0x000939b0)(self->head);  // Mem_AlignedFree
    self->head = nullptr;
}

// The managers' deleting destructors: the list at +4 cleared, the base vtable stored, the
// object freed when flags bit 0 is set.
template <void (XB_THISCALL* Clear)(NglSkipList*)>
uint8_t* delete_manager(uint8_t* self, uint32_t flags, uint32_t base_vtable) {
    Clear(reinterpret_cast<NglSkipList*>(self + 4));
    *reinterpret_cast<uint32_t*>(self) = base_vtable;
    if (flags & 1) title_function<void XB_CDECL(void*)>(0x000939b0)(self);  // Mem_AlignedFree
    return self;
}

}  // namespace

void XB_THISCALL nglSkipList_ClearTextures(NglSkipList* self) { clear_skip_list<nglSkipList_RemoveTexture>(self); }
void XB_THISCALL nglSkipList_Clear(NglSkipList* self) { clear_skip_list<nglSkipList_Remove>(self); }
void XB_THISCALL nglSkipList_ClearIndirect(NglSkipList* self) { clear_skip_list<nglSkipList_RemoveIndirect>(self); }

uint8_t* XB_THISCALL nglTextureManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return delete_manager<nglSkipList_ClearTextures>(self, flags, 0x008381cc);
}
uint8_t* XB_THISCALL nglPaletteManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return delete_manager<nglSkipList_Clear>(self, flags, 0x007bc91c);
}
uint8_t* XB_THISCALL nglFontManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return delete_manager<nglSkipList_Clear>(self, flags, 0x007bd0d4);
}
uint8_t* XB_THISCALL nglMeshFileManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return delete_manager<nglSkipList_Clear>(self, flags, 0x00838270);
}
uint8_t* XB_THISCALL nglMeshManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return delete_manager<nglSkipList_ClearIndirect>(self, flags, 0x00838228);
}
uint8_t* XB_THISCALL nglSection3Manager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return delete_manager<nglSkipList_ClearIndirect>(self, flags, 0x007bdf30);
}
uint8_t* XB_THISCALL nglMeshFileCache_DeletingDtor(uint8_t* self, uint32_t flags) {
    return delete_manager<nglSkipList_Clear>(self, flags, 0x007c1260);
}

NglHashedName* XB_STDCALL nglEmpty_GetName(NglHashedName* out) {
    return title_function<NglHashedName* XB_THISCALL(NglHashedName*, const char*)>(0x00011060)(out, "nglEmpty");
}

void XB_CDECL nglMeshSection_FreeBuffers(uint8_t* section) {
    nglVertexBuffer_Free(reinterpret_cast<uint32_t*>(section + 0x44), 4);
    uint32_t* buffers = reinterpret_cast<uint32_t*>(section + 0x34);
    if (!buffers[1]) return;
    title_function<void XB_STDCALL(uint32_t)>(0x003e8000)(buffers[0]);  // D3DResource_BlockUntilNotBusy
    title_function<void XB_CDECL(uint32_t)>(0x000939b0)(buffers[0]);    // Mem_AlignedFree
    title_function<void XB_CDECL(uint32_t)>(0x000939b0)(buffers[1]);    // Mem_AlignedFree
    buffers[0] = 0;
    buffers[1] = 0;
    buffers[2] = 0;
}

namespace {

// The debug meshes' draw: a one-entry light set in frame memory whose slot for the current
// category (0x00902908) is the colour as a float4 (frame memory; null when that allocation
// lands at 0), and an instance block {2 = scaled, ..., +0x10 the scale} on the stack (the
// words the original leaves unset are zero in tests).
int32_t debug_draw_mesh(uint8_t* mesh, const float* matrix, const uint32_t* scale, uint32_t color) {
    uintptr_t& arena = title_global<uintptr_t>(0x009025a8);
    auto* lights = static_cast<uint32_t*>(alloc_frame(arena, title_global<uint32_t>(0x00902904) * 4 + 8, 8));
    lights[0] = 0;
    lights[1] = 0;
    auto* vec = static_cast<float*>(alloc_frame(arena, 0x10, 0x10));
    uint32_t vec_word = 0;
    if (vec) {
        nglColorToVec4(vec, color);
        vec_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vec));
    }
    const uint32_t category = title_global<uint32_t>(0x00902908);
    lights[0] |= 1u << (category & 31);
    lights[category + 2] = vec_word;
    alignas(16) uint32_t block[12] = {};
    std::memcpy(&block[4], scale, 16);
    block[0] = 2;
    const uint32_t light_ref = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lights));
    return nglListAddMesh(mesh, matrix, block, &light_ref);
}

}  // namespace

int32_t XB_CDECL nglDebugDrawSphereMesh(const float* matrix, const uint32_t* scale, uint32_t color) {
    return debug_draw_mesh(title_global<uint8_t*>(0x00902c98), matrix, scale, color);
}

int32_t XB_CDECL nglDebugDrawBoxMesh(const float* matrix, const uint32_t* scale, uint32_t color, uint8_t solid) {
    return debug_draw_mesh(title_global<uint8_t*>(solid ? 0x00902c90 : 0x00902c94), matrix, scale, color);
}

int32_t XB_CDECL nglDebugDrawPyramidMesh(const float* matrix, const uint32_t* scale, uint32_t color) {
    return debug_draw_mesh(title_global<uint8_t*>(0x00902c9c), matrix, scale, color);
}

int32_t XB_CDECL nglSelectMeshLodWithMatrix(const uint8_t* table, const float* matrix) {
    // The bounding sphere's centre (+0x20) through the matrix, then the view matrix (all four
    // lanes, as packed); its view depth (lane z) against the LOD distances from the last one.
    const float* c = reinterpret_cast<const float*>(table + 0x20);
    const Lanes p = add(add(add(lanes(matrix + 12), mul(lanes(matrix), c[0])), mul(lanes(matrix + 4), c[1])),
                        mul(lanes(matrix + 8), c[2]));
    const float* v = current_scene()->view_matrix;
    const Lanes view = add(add(add(lanes(v + 12), mul(lanes(v), p.v[0])), mul(lanes(v + 4), p.v[1])),
                           mul(lanes(v + 8), p.v[2]));
    platform_keep(view);
    const float depth = view.v[2];
    const int32_t count = *reinterpret_cast<const int32_t*>(table + 0x18);
    const uint8_t* entries = *reinterpret_cast<uint8_t* const*>(table + 0x1c);
    for (int32_t i = count - 1; i >= 0; --i) {
        if (depth > *reinterpret_cast<const float*>(entries + i * 8 + 4)) return i + 1;
    }
    return 0;
}

void XB_CDECL nglSelectMeshProjectors(NglMeshDrawNode* node) {
    const uint32_t slot = title_global<uint32_t>(0x00902914);
    const uint32_t* params = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(node->extra));
    const auto has_slot = [params, slot] {
        const uint32_t low = 1u << (slot & 31);
        const uint32_t high = static_cast<int32_t>(low) < 0 ? 0xffffffff : 0;
        return (params[0] & low) == low && (params[1] & high) == high;
    };
    NglLightSet* set;
    if (has_slot()) {
        set = has_slot() ? reinterpret_cast<NglLightSet*>(static_cast<uintptr_t>(params[2 + slot])) : nullptr;
    } else {
        set = current_scene()->light_set;
    }
    current_light_set() = set;
    NglLight* const end = reinterpret_cast<NglLight*>(set->projectors);
    NglLight*& active = *reinterpret_cast<NglLight**>(reinterpret_cast<uint8_t*>(set) + 0x50);
    active = end;
    const uint8_t* mesh = node->mesh;
    const int32_t list = static_cast<int32_t>(mesh[7]) - 1;
    if (list == -1) return;
    const float* local = reinterpret_cast<const float*>(mesh + 0x20);
    Lanes center = add(lanes(node->world + 12), mul(lanes(node->world), local[0]));
    center = add(center, mul(lanes(node->world + 4), local[1]));
    center = add(center, mul(lanes(node->world + 8), local[2]));
    const Lanes q = {{center.v[0], center.v[1], center.v[2], title_global<float>(0x007beb50)}};
    set = current_light_set();
    // Projector boxes (type 2): the sphere must not be behind any of the six planes at data
    // +0xe0 (tested in the order +0x100, +0x110, +0x120, +0xe0, +0xf0, +0x130); each plane's
    // distance is (x a + y b) + (z c + d), as the packed sum pairs them.
    static const int kPlanes[6] = {0x20, 0x30, 0x40, 0x00, 0x10, 0x50};
    NglLight* item = set->projectors[list];
    while (item != end) {
        if (item->type == 2) {
            const float behind = 0.0f - *reinterpret_cast<const float*>(mesh + 0x30);
            const uint8_t* planes = reinterpret_cast<const uint8_t*>(item->data) + 0xe0;
            bool inside = true;
            for (int k = 0; k < 6 && inside; ++k) {
                const Lanes plane = lanes(reinterpret_cast<const float*>(planes + kPlanes[k]));
                const Lanes m = mul(q, plane);
                const Lanes pair = add(m, Lanes{{m.v[1], m.v[1], plane.v[3], plane.v[3]}});
                platform_keep(pair);
                const float distance = pair.v[0] + pair.v[2];
                if (behind > distance) inside = false;
            }
            if (inside) {
                item->next_object = active;
                active = item;
                set = current_light_set();
            }
        }
        item = reinterpret_cast<NglLight* const*>(item)[list];
    }
}

bool XB_THISCALL AnimManager_SkipListRemove(NglSkipList* self, const uint32_t* object) {
    return skip_list_remove(self, object, SkipName::At16);
}

bool XB_THISCALL NalManager_SkipListRemoveKey8(NglSkipList* self, const uint32_t* object) {
    return skip_list_remove(self, object, SkipName::At8);
}

void XB_CDECL nglDrawSingleQuadFrame() {
    title_function<void XB_CDECL()>(0x00064690)();  // nglBeginFrame
    uint8_t* scene = reinterpret_cast<uint8_t*>(current_scene());
    *reinterpret_cast<uint32_t*>(scene + 0x360) = 0;
    scene = reinterpret_cast<uint8_t*>(current_scene());
    scene[0x385] = 0;
    scene = reinterpret_cast<uint8_t*>(current_scene());
    scene[0x384] = 0;
    NglQuad quad;
    nglInitQuad(&quad);
    nglSetQuadTexture(&quad, nglGetFrontBufferTexture());
    const float height = static_cast<float>(nglGetScreenHeight());
    const float width = static_cast<float>(nglGetScreenWidth());
    nglSetQuadRect(&quad, 0.0f, 0.0f, width, height);
    nglListAddQuad(&quad);
    title_function<void XB_CDECL()>(0x00064ec0)();  // nglEndFrame
}

void XB_CDECL nglDrawQuad(NglQuad* quad) {
    uint32_t& cull_mode = title_global<uint32_t>(0x00902fac);
    if (cull_mode) {
        title_function<void XB_STDCALL(uint32_t)>(0x003e80f0)(0);  // D3DDevice_SetRenderState_CullMode
        cull_mode = 0;
    }
    nglSetBlendMode(&title_global<uint8_t>(0x00902f08), quad->blend_mode, quad->blend_arg, 0x80);
    nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00903c9c));
    NglQuadVertex* v = quad->verts;
    if (title_global<uint8_t>(0x009028c0) && (current_scene()->render_target->flags & 4)) {
        const float sx = title_global<float>(0x009028c4);
        const float sy = title_global<float>(0x009028c8);
        v[0].x = v[0].x * sx;
        v[0].y = v[0].y * sy;
        v[1].x = v[1].x * sx;
        v[1].y = v[1].y * sy;
        v[2].x = v[2].x * sx;
        v[2].y = sy * v[2].y;
        v[3].x = v[3].x * sx;
        v[3].y = v[3].y * sy;
    }
    bool yuv = false;
    NglTexture* texture = quad->texture;
    if (texture) {
        const uint32_t flags = quad->flags;
        // D3D's deferred texture stage states, stage 0 (dirty bit 1).
        title_global<uint32_t>(0x003f8320) |= 1;
        title_global<uint32_t>(0x003f5c68) = (flags & 0x40) ? 3 : 1;
        title_global<uint32_t>(0x003f8320) |= 1;
        title_global<uint32_t>(0x003f5c6c) = (flags & 0x80) ? 3 : 1;
        title_global<uint32_t>(0x009026fc) = *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const uint8_t*>(current_scene()) + 0x3d4);
        nglSetTexture(0, texture, static_cast<uint8_t>(quad->flags), 3);
        if (static_cast<uint8_t>(texture->flags) == 8) {
            yuv = true;
            if (title_global<uint8_t>(0x00902fbc) != 1) {
                title_function<void XB_STDCALL(uint32_t)>(0x003e8530)(1);  // D3DDevice_SetRenderState_YuvEnable
                title_global<uint8_t>(0x00902fbc) = 1;
            }
        }
        if (texture->flags & 0x200) {
            // Texel coordinates: u by the width, v by the height (x87, unsigned to float).
            const float w = platform_x87_unsigned_to_float(texture->width);
            const float h = platform_x87_unsigned_to_float(texture->height);
            v[0].u = platform_x87_mul(w, v[0].u);
            v[0].v = platform_x87_mul(h, v[0].v);
            v[1].u = platform_x87_mul(w, v[1].u);
            v[1].v = platform_x87_mul(h, v[1].v);
            v[2].u = platform_x87_mul(w, v[2].u);
            v[2].v = platform_x87_mul(h, v[2].v);
            v[3].u = platform_x87_mul(w, v[3].u);
            v[3].v = platform_x87_mul(h, v[3].v);
        }
        nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00903cc4));
    } else {
        nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00903cb4));
    }
    const float z = nglDepthToZValue(quad->z);
    title_global<uint32_t>(0x00903c88) = 0;
    title_global<uint32_t>(0x00903c8c) = 0;
    title_global<uint32_t>(0x00903c90) = 0;
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
    auto* push = reinterpret_cast<uint32_t*>(
        static_cast<uintptr_t>(title_function<uint32_t XB_STDCALL(uint32_t)>(0x003eba40)(0x1d)));  // BeginPush
    push[0] = 0x000417fc;  // begin: quads
    push[1] = 8;
    push[2] = 0x40601818;  // 24 dwords of vertex data
    push += 3;
    // Vertices 0, 1, 3, 2: x, y, z, colour, u, v.
    static const int kOrder[4] = {0, 1, 3, 2};
    for (const int i : kOrder) {
        std::memcpy(&push[0], &v[i].x, 4);
        std::memcpy(&push[1], &v[i].y, 4);
        std::memcpy(&push[2], &z, 4);
        push[3] = v[i].color;
        std::memcpy(&push[4], &v[i].u, 4);
        std::memcpy(&push[5], &v[i].v, 4);
        push += 6;
    }
    push[0] = 0x000417fc;  // end
    push[1] = 0;
    push += 2;
    title_global<uint32_t>(0x00903c98) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(push));
    title_function<void XB_STDCALL(uint32_t)>(0x003ebaa0)(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(push)));  // EndPush
    if (yuv && title_global<uint8_t>(0x00902fbc)) {
        title_function<void XB_STDCALL(uint32_t)>(0x003e8530)(0);  // D3DDevice_SetRenderState_YuvEnable
        title_global<uint8_t>(0x00902fbc) = 0;
    }
}

namespace {

// A title object's virtual method (thiscall).
template <typename F>
F* vmethod(const void* object, uint32_t slot) {
    const uint32_t vtable = *static_cast<const uint32_t*>(object);
    return title_function<F>(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(vtable + slot * 4)));
}

}  // namespace

void XB_THISCALL nglResourceManager_ForEachPass(uint8_t* self, uint8_t restart_on_zero, uint32_t arg, int32_t passes) {
    using Method = uint32_t XB_THISCALL(void*);
    auto* iter = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(vmethod<Method>(self, 5)(self)));  // NewIterator
    for (int32_t pass = 1; pass <= passes; ++pass) {
        vmethod<Method>(iter, 1)(iter);  // First
        while (static_cast<uint8_t>(vmethod<Method>(iter, 2)(iter))) {  // Valid
            void* object = reinterpret_cast<void*>(static_cast<uintptr_t>(vmethod<Method>(iter, 4)(iter)));  // Current
            const int32_t result = static_cast<int32_t>(
                vmethod<uint32_t XB_THISCALL(void*, void*, int32_t, uint32_t)>(self, 8)(self, object, pass, arg));
            if (result > 0)
                vmethod<Method>(iter, 3)(iter);  // Next
            else
                vmethod<Method>(iter, 1)(iter);  // First again
            if (result == 0 && restart_on_zero) vmethod<Method>(self, 1)(self);
        }
    }
    if (iter) vmethod<uint32_t XB_THISCALL(void*, uint32_t)>(iter, 0)(iter, 1);  // deleting destructor
}

uint8_t XB_CDECL nglReadFile(const char* name, uint32_t* out, uint32_t alignment, uint32_t flags) {
    using ReadHook = uint8_t XB_CDECL(const char*, uint32_t*, uint32_t, uint32_t);
    const uint32_t hook = title_global<uint32_t>(0x009098d8);
    if (hook) return title_function<ReadHook>(hook)(name, out, alignment, flags);
    // The data prefix (0x008bb660) goes in front unless the name starts with it.
    const char* prefix = &title_global<char>(0x008bb660);
    char path[0x200];
    if (title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001eeaf0)(name, prefix,
                                                                                       std::strlen(prefix)))
        std::strcpy(path, prefix);
    else
        path[0] = 0;
    std::strcat(path, name);
    using CreateFile = uint32_t XB_STDCALL(const char*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const uint32_t file = title_function<CreateFile>(0x001e7d56)(path, 0x80000000, 1, 0, 3, 0x08000080, 0);
    if (file == 0xffffffff) return 0;
    const uint32_t size = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t*)>(0x001e84e1)(file, nullptr);  // GetFileSize
    out[1] = size;
    void* data = Mem_AlignedAlloc(size, alignment, flags);
    out[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    uint32_t read = 0;
    using ReadFile = uint32_t XB_STDCALL(uint32_t, void*, uint32_t, uint32_t*, void*);
    title_function<ReadFile>(0x001e7f2a)(file, data, out[1], &read, nullptr);
    title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e7b0b)(file);  // CloseHandle
    return 1;
}

uint8_t XB_CDECL nglInitMultisample(uint32_t type) {
    // Multisample types (D3DMULTISAMPLE_*): 0x1021/0x1121 two samples, 0x1022/0x1222 four,
    // 0x11 none; anything else falls back to 0x11 and returns false. Not on a 1920-wide mode
    // (0x00902890) unless 0x11.
    if (title_global<uint32_t>(0x00902890) == 0x780 && type != 0x11) return 0;
    float z = title_global<float>(0x0083f004);  // 1/32: the depth sample offset
    float x_samples = title_global<float>(0x0083eba8);  // 2
    float y_samples = title_global<float>(0x0083eba8);
    bool antialias = true;
    uint8_t ok = 1;
    switch (type) {
    case 0x1021:
    case 0x1121:
        y_samples = title_global<float>(0x0083eb20);  // 1
        break;
    case 0x1022:
    case 0x1222:
        break;
    default:
        ok = 0;
        type = 0x11;
        [[fallthrough]];
    case 0x11:
        z = title_global<float>(0x007bf1c0);  // 0.53125
        x_samples = title_global<float>(0x0083eb20);
        y_samples = title_global<float>(0x0083eb20);
        antialias = false;
        break;
    }
    float* scale = &title_global<float>(0x00903c70);
    scale[0] = title_global<float>(0x0083eb20);
    scale[1] = title_global<float>(0x0083eb20);
    scale[2] = z;
    scale[3] = title_global<float>(0x007bf9b0);
    title_global<float>(0x008a3510) = x_samples;
    title_global<float>(0x008a3514) = y_samples;
    uint8_t& state = title_global<uint8_t>(0x00902f44);
    if (state != static_cast<uint8_t>(antialias)) {
        title_function<void XB_STDCALL(uint32_t)>(0x003e87c0)(antialias ? 1 : 0);  // SetRenderState_MultiSampleAntiAlias
        state = antialias ? 1 : 0;
    }
    title_global<uint32_t>(0x008a3518) = type;
    title_global<uint32_t>(0x00902760) = type;
    nglResetDevice();
    return ok;
}

namespace {

// One of the frame buffer texture records (0x74 bytes): flags 6 at +0x28, the hashed name at
// +0x54, kind (+4), 1 at +0x18, the D3D format (+8) and the depth link (+0x38); then added to
// the texture manager (vtable slot 3).
void init_frame_buffer_record(uint32_t record, const char* name, uint32_t kind, uint32_t format, uint32_t link) {
    title_global<uint32_t>(record + 0x28) |= 6;
    uint32_t hashed[8];
    title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(0x00011060)(hashed, name);  // HashStr_Init
    std::memcpy(&title_global<uint32_t>(record + 0x54), hashed, sizeof(hashed));
    title_global<uint32_t>(record + 0x04) = kind;
    title_global<uint32_t>(record + 0x18) = 1;
    title_global<uint32_t>(record + 0x08) = format;
    title_global<uint32_t>(record + 0x38) = link;
    uint8_t* manager = title_global<uint8_t*>(0x009025f0);
    const uint32_t add = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*reinterpret_cast<uint32_t*>(manager)))[3];
    title_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(add)(manager, record);
}

}  // namespace

void XB_CDECL nglInitFrameBuffers() {
    constexpr uint32_t kFront = 0x00902cd8;
    constexpr uint32_t kBack = 0x00902d50;
    constexpr uint32_t kDepth = 0x00902dc8;
    if (title_global<uint32_t>(0x008a2f40)) {
        std::memset(&title_global<uint32_t>(kFront), 0, 0x1d * 4);
        std::memset(&title_global<uint32_t>(kBack), 0, 0x1d * 4);
        std::memset(&title_global<uint32_t>(kDepth), 0, 0x1d * 4);
        title_global<uint32_t>(kFront + 0x34) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Mem_AlignedAlloc(4, 4, 0)));
        init_frame_buffer_record(kFront, "nglFrontBuffer", 0x201, title_global<uint32_t>(0x00902758), 0);
        title_global<uint32_t>(kBack + 0x34) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Mem_AlignedAlloc(4, 4, 0)));
        init_frame_buffer_record(kBack, "nglBackBuffer", 0x5201, title_global<uint32_t>(0x00902758), kDepth);
        init_frame_buffer_record(kDepth, "nglBackZBuffer", 0x2201, title_global<uint32_t>(0x00902774), 0);
        title_global<uint32_t>(0x008a2f40) = 0;
    }
    using GetBuffer = uint32_t XB_STDCALL(int32_t);
    auto* back_surface = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(kBack + 0x34)));
    *back_surface = title_function<GetBuffer>(0x003e5720)(0);  // D3DDevice_GetBackBuffer2
    title_global<uint32_t>(kDepth + 0x34) = title_function<uint32_t XB_STDCALL()>(0x003e5b70)();  // GetDepthStencilSurface2
    const uint32_t width = title_global<uint32_t>(0x00902890);
    const uint32_t height = title_global<uint32_t>(0x00902894);
    title_global<uint32_t>(kBack + 0x14) = height;
    title_global<uint32_t>(kDepth + 0x14) = height;
    title_global<uint32_t>(kBack + 0x10) = width;
    title_global<uint32_t>(kDepth + 0x10) = width;
    title_global<uint32_t>(kBack + 0x30) =
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(kBack + 0x34)));
    auto* front_surface = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(kFront + 0x34)));
    *front_surface = title_function<GetBuffer>(0x003e5720)(-1);  // the front buffer
    uint32_t desc[8];
    title_function<uint32_t XB_STDCALL(uint32_t, uint32_t*)>(0x003ebe30)(
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(kFront + 0x34))), desc);  // GetDesc
    title_global<uint32_t>(kFront + 0x10) = desc[5];
    title_global<uint32_t>(kFront + 0x14) = desc[6];
    title_global<uint32_t>(kFront + 0x30) =
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(kFront + 0x34)));
}

void XB_CDECL nglSetFrameMemorySize(uint32_t kind, uint32_t size, uint8_t reallocate) {
    const uint32_t bytes = size + (size & 3);
    if (title_global<uint32_t>(0x00902874)) {
        title_global<uint32_t>(0x00903c88) = 0;
        title_global<uint32_t>(0x00903c8c) = 0;
        title_global<uint32_t>(0x00903c90) = 0;
        title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
        nglUnbindTextures();
        title_function<void XB_CDECL()>(0x003e6200)();  // D3DDevice_BlockUntilIdle
    }
    uint32_t& scene_bytes = title_global<uint32_t>(0x009025ac);
    uint32_t& block1_bytes = title_global<uint32_t>(0x00903c44);
    uint32_t& block2_bytes = title_global<uint32_t>(0x00903c2c);
    uint32_t& vertex_bytes = title_global<uint32_t>(0x00903c1c);
    uint32_t& index_bytes = title_global<uint32_t>(0x00903c14);
    switch (kind) {
    case 0: scene_bytes = bytes; break;
    case 1: block1_bytes = bytes; break;
    case 3:
        if (vertex_bytes) {
            nglVertexBuffer_Free(&title_global<uint32_t>(0x00902938), 0);
            nglVertexBuffer_Free(&title_global<uint32_t>(0x00902944), 0);
        }
        vertex_bytes = bytes;
        break;
    case 2:
        if (index_bytes) {
            nglIndexBuffer_Clear(&title_global<uint8_t*>(0x00902950));
            nglIndexBuffer_Clear(&title_global<uint8_t*>(0x00902954));
        }
        index_bytes = bytes;
        break;
    case 4: block2_bytes = bytes; break;
    default: break;
    }
    // The CPU block: scene memory, block 1, two index buffers.
    uint32_t& total = title_global<uint32_t>(0x00903c40);
    total = scene_bytes + (index_bytes + block1_bytes) * 2;
    uint32_t& block = title_global<uint32_t>(0x00903c38);
    bool make = true;
    if (block) {
        if (total == title_global<uint32_t>(0x00903c3c) || !reallocate) {
            make = false;
        } else {
            title_function<void XB_CDECL(uint32_t)>(0x000939b0)(block);  // Mem_AlignedFree
            block = 0;
        }
    }
    if (make && total && bytes) {
        title_global<uint32_t>(0x00903c3c) = total;
        const uint32_t memory = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Mem_AlignedAlloc(total, 4, 0)));
        block = memory;
        title_global<uint32_t>(0x009025a4) = memory;
        const uint32_t block1 = memory + scene_bytes;
        title_global<uint32_t>(0x00903c48) = block1;
        const uint32_t block1_end = block1 + block1_bytes;
        title_global<uint32_t>(0x00903c4c) = block1_end;
        const uint32_t indices = block1_bytes + block1_end;
        const uint32_t count = index_bytes;
        nglIndexBuffer_Init(&title_global<uint8_t*>(0x00902950), reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(indices)),
                            count);
        nglIndexBuffer_Init(&title_global<uint8_t*>(0x00902954),
                            reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(count + indices)), count);
        title_global<uint32_t>(0x00903c54) = title_global<uint32_t>(0x00903c48);
        title_global<uint32_t>(0x00903c50) = title_global<uint32_t>(0x00903c48);
    }
    // The physical block: block 2 and two vertex buffers.
    uint32_t& physical_total = title_global<uint32_t>(0x00903c28);
    physical_total = block2_bytes + vertex_bytes * 2;
    uint32_t& physical = title_global<uint32_t>(0x00903c20);
    if (physical) {
        if (physical_total == title_global<uint32_t>(0x00903c24) || !reallocate) return;
        title_function<void XB_CDECL(uint32_t)>(0x000939b0)(physical);  // Mem_AlignedFree
        physical = 0;
    }
    if (!physical_total || !bytes) return;
    title_global<uint32_t>(0x00903c24) = physical_total;
    const uint32_t memory =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Mem_AlignedAlloc(physical_total, 0x80, 0x30000)));
    const uint32_t count = vertex_bytes;
    physical = memory;
    title_global<uint32_t>(0x00903c18) = memory;
    nglCreateVertexBufferPhysical(&title_global<uint32_t>(0x00902938), memory, count, 0);
    nglCreateVertexBufferPhysical(&title_global<uint32_t>(0x00902944), memory + count, count, 0);
    title_global<uint32_t>(0x00903c30) = memory + count * 2;
}

void XB_CDECL nglSelectVideoMode(const uint32_t* modes, uint32_t count) {
    // The first mode of the list (low byte: 0 NTSC, 1 PAL, 2 480p, 3 720p, 4 1080i) the console
    // supports (XGetVideoFlags: 1 widescreen, 2 720p, 4 1080i, 8 480p; PAL needs the PAL video
    // standard) becomes the current mode (0x00902890); the title always renders 640x480/576.
    const uint32_t flags = title_function<uint32_t XB_STDCALL()>(0x001e7ee1)();  // XGetVideoFlags
    const auto standard = [] { return title_function<uint32_t XB_STDCALL()>(0x001e75df)(); };  // XGetVideoStandard
    const bool wide = flags & 1;
    for (uint32_t i = 0; i < count; ++i) {
        const char* name = nullptr;
        uint32_t height = 0x1e0;
        uint8_t progressive = 0, widescreen = 0, field0a = 1;
        uint32_t index = 0;
        switch (modes[i] & 0xff) {
        case 0:
            name = wide ? "NTSC (640x480) 16x9" : "NTSC (640x480) 4x3";
            widescreen = wide;
            break;
        case 1:
            if (standard() == 3 && wide) {
                name = "PAL (640x576) 16x9";
                widescreen = 1;
            } else if (standard() == 3) {
                name = "PAL (640x576) 4x3";
            }
            height = 0x240;
            index = 1;
            break;
        case 2:
            if (flags & 8) {
                name = wide ? "480P (640x480) 16x9" : "480P (640x480) 4x3";
                progressive = 1;
                widescreen = wide;
            }
            index = 2;
            break;
        case 3:
            if (flags & 2) {
                name = "720P (1280x720) 16x9";
                progressive = 1;
                widescreen = 1;
                field0a = 0;
            }
            index = 3;
            break;
        case 4:
            if (flags & 4) {
                name = "1080I (1920x1080) 16x9";
                progressive = 1;
                widescreen = 1;
                field0a = 0;
            }
            index = 4;
            break;
        default: break;
        }
        if (!name) continue;
        uint32_t hashed[8];
        title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(0x00011060)(hashed, name);  // HashStr_Init
        NglVideoMode mode;
        nglVideoMode_Init(&mode, 0x280, height, progressive, widescreen, field0a, hashed[0], hashed[1], hashed[2],
                          hashed[3], hashed[4], hashed[5], hashed[6], hashed[7], index);
        nglVideoMode_Copy(&title_global<NglVideoMode>(0x00902890), &mode);
        title_global<uint8_t>(0x0090289b) = 1;
        return;
    }
}

void XB_CDECL nglDumpPrintf(uint32_t file, const char* format, ...) {
    char text[0x1000];
    va_list args;
    va_start(args, format);
    title_function<int32_t XB_CDECL(char*, const char*, va_list)>(0x001eea95)(text, format, args);  // _vsprintf
    va_end(args);
    if (!file) return;
    uint32_t written;
    using WriteFile = uint32_t XB_STDCALL(uint32_t, const void*, uint32_t, uint32_t*, void*);
    if (!title_function<WriteFile>(0x001e8017)(file, text, static_cast<uint32_t>(std::strlen(text)), &written, nullptr))
        title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e7b0b)(file);  // CloseHandle
}

uint8_t XB_CDECL nglWriteTGA(NglTexture* texture, const char* path) {
    // An 18-byte TGA header (uncompressed true colour, 32 bits), then the rows bottom-up as
    // nglTextureGetPixel words, through a row buffer in frame memory (or allocated when the
    // frame memory pointer is unset).
    uint8_t header[0x12] = {};
    header[2] = 2;
    const uint16_t width16 = static_cast<uint16_t>(texture->width);
    const uint16_t height16 = static_cast<uint16_t>(texture->height);
    std::memcpy(&header[0xc], &width16, 2);
    std::memcpy(&header[0xe], &height16, 2);
    header[0x10] = 0x20;
    using CreateFile = uint32_t XB_STDCALL(const char*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using WriteFile = uint32_t XB_STDCALL(uint32_t, const void*, uint32_t, uint32_t*, void*);
    const uint32_t file = title_function<CreateFile>(0x001e7d56)(path, 0x40000000, 0, 0, 2, 0x08000080, 0);
    if (file == 0xffffffff) return 0;
    uint32_t written;
    title_function<WriteFile>(0x001e8017)(file, header, 0x12, &written, nullptr);
    uintptr_t& arena = title_global<uintptr_t>(0x009025a8);
    auto* row = static_cast<uint32_t*>(alloc_frame(arena, texture->width * 4, 0x10));
    bool allocated = false;
    if (!row) {
        allocated = true;
        row = static_cast<uint32_t*>(Mem_AlignedAlloc(texture->width * 4, 4, 0));
        if (!row) return 0;
    }
    const uint32_t flags = (texture == nglGetBackBufferTexture() || texture == nglGetFrontBufferTexture()) ? 0xc0 : 0x80;
    nglTexture_Lock(texture, flags);
    for (int32_t y = static_cast<int32_t>(texture->height) - 1; y >= 0; --y) {
        uint32_t* out = row;
        for (int32_t x = 0; x < static_cast<int32_t>(texture->width); ++x)
            *out++ = nglTextureGetPixel(texture, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        title_function<WriteFile>(0x001e8017)(file, row, texture->width * 4, &written, nullptr);
    }
    title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e7b0b)(file);  // CloseHandle
    title_function<void XB_CDECL(NglTexture*)>(0x0006a860)(texture);  // nglTexture_Unlock (the argument is pushed)
    if (allocated) title_function<void XB_CDECL(void*)>(0x000939b0)(row);  // Mem_AlignedFree
    return 1;
}

uint8_t XB_CDECL nglSaveTextureTGA(NglTexture* texture, const char* name) {
    title_global<uint32_t>(0x00903c88) = 0;
    title_global<uint32_t>(0x00903c8c) = 0;
    title_global<uint32_t>(0x00903c90) = 0;
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
    nglUnbindTextures();
    title_function<void XB_CDECL()>(0x003e6200)();  // D3DDevice_BlockUntilIdle
    char path[0x400];
    using Sprintf = int32_t XB_CDECL(char*, const char*, ...);
    if (name) {
        title_function<Sprintf>(0x001ee901)(path, &title_global<char>(0x007be534), name);  // "D:\%s.tga"
    } else {
        uint32_t& counter = title_global<uint32_t>(0x008a2ec0);
        const uint32_t number = counter++;
        title_function<Sprintf>(0x001ee901)(path, &title_global<char>(0x007be520), number);  // "D:\savetex%4.4d.tga"
    }
    return nglWriteTGA(texture, path);
}

uint8_t XB_CDECL nglSaveScreenshot(const char* name) {
    NglTexture* front = nglGetFrontBufferTexture();
    if (name) return nglSaveTextureTGA(front, name);
    uint32_t& counter = title_global<uint32_t>(0x00902748);
    const uint32_t number = counter++;
    char* buffer = &title_global<char>(0x00902708);
    title_function<int32_t XB_CDECL(char*, const char*, ...)>(0x001ee901)(buffer, &title_global<char>(0x007bc90c),
                                                                           number);  // "screenshot%4.4d"
    return nglSaveTextureTGA(front, buffer);
}

}  // namespace game
