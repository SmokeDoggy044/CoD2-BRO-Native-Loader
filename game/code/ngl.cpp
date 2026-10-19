#include "ngl.h"

#include <cstring>

#include "lanes.h"
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

float from_bits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// |x| by clearing the sign bit (ANDNPS; std::fabs is called out of line at -O0).
float abs_bits(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    return from_bits(bits & 0x7fffffffu);
}

// The title's packed sine polynomial (one lane): the angle folded into a quarter
// period by rounding with 1.5 * 2^23, then an odd polynomial of degree 9.
float scene_sine_poly(float angle) {
    const float y = -abs_bits(angle) * from_bits(0x3e22f983);  // 1 / (2 pi)
    const float magic = from_bits(0x4b400000);
    const float z = abs_bits(((y - magic) + magic) - y - 0.5f) - 0.25f;
    const float z2 = z * z;
    const float z4 = z2 * z2;
    const float z3 = z * z2;
    const float z5 = z * z4;
    float sum = z5 * z4 * from_bits(0x421ed7b7) + z3 * z4 * from_bits(0xc2992661);
    sum = sum + z5 * from_bits(0x42a33457);
    sum = sum + z3 * from_bits(0xc2255de0);
    return sum + z * from_bits(0x40c90fda);
}

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

namespace {

Lanes mul(const Lanes& a, const Lanes& b) {
    return {{a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3]}};
}

}  // namespace

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
    node->unknown84 = 0;
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

}  // namespace game
