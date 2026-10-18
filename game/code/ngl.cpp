#include "ngl.h"

#include <cstring>

#include "title_data.h"

namespace game {

namespace {

NglScene*& current_scene() { return title_global<NglScene*>(0x0090259c); }

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

void XB_CDECL nglSetClearFlags(uint32_t flags) { current_scene()->clear_flags = flags; }

void XB_CDECL nglSetClearDepth(float depth) { current_scene()->clear_depth = depth; }

void XB_CDECL nglSetColorWriteMask(uint32_t mask) { current_scene()->color_write_mask = mask; }

void XB_CDECL nglSetFog(float start, float value1, float value2, float value3) {
    NglScene* scene = current_scene();
    // A NaN start gives the minimum, as the original's comparison does.
    scene->fog[0] = start > scene->fog_start_min ? start : scene->fog_start_min;
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

int32_t XB_CDECL nglGetScreenWidth() { return 640; }

int32_t XB_CDECL nglGetScreenHeight() { return 480; }

void XB_CDECL nglColorToVec4(float* out, uint32_t color) {
    const float scale = 1.0f / 255.0f;
    out[0] = static_cast<float>((color >> 16) & 0xff) * scale;
    out[1] = static_cast<float>((color >> 8) & 0xff) * scale;
    out[2] = static_cast<float>(color & 0xff) * scale;
    out[3] = static_cast<float>(color >> 24) * scale;
}

void XB_CDECL nglSetRenderTarget(void* target) {
    current_scene()->render_target = target;
    current_scene()->unknown008 = 6;
}

void XB_CDECL nglSetSceneFlags(uint32_t flags) { current_scene()->flags = flags; }

void XB_CDECL nglSwapRedBlue(uint8_t* pixels, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t red = pixels[i * 4];
        pixels[i * 4] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = red;
    }
}

}  // namespace game
