#include "ngl_debug.h"

#include <cstring>

#include "lanes.h"
#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

const uint8_t* system_font() { return title_global<const uint8_t*>(0x009028d4); }

float line_width() { return title_global<float>(0x008a2f04); }

// x / sqrt(x) of the title's inlined vector length: one RSQRTSS estimate and a Newton
// step, ((3 - (x r) r) (x r)) / 2.
float rsqrt_length(float x) {
    const float r = platform_rsqrt_estimate(x);
    const float t = x * r;
    return ((3.0f - t * r) * t) * 0.5f;
}

// 1 / sqrt(|x|) of the inlined normalize: ((3 - r r |x|) r) / 2.
float rsqrt_newton(float x) {
    const float a = abs_bits(x);
    const float r = platform_rsqrt_estimate(a);
    return ((3.0f - (r * r) * a) * r) * 0.5f;
}

Lanes make_lanes(float x, float y, float z, float w) { return {{x, y, z, w}}; }

uint8_t* material_section(const uint32_t* material) {
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(material[1]));
}

uint32_t& section_word(uint8_t* section, uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(section + offset);
}

// Vertex `index` (12 bytes: x y z) of the section's locked vertex buffer.
float* section_vertex(uint8_t* section, uint32_t index) {
    return reinterpret_cast<float*>(
        static_cast<uintptr_t>(section_word(section, 0x54) + index * 12 + section_word(section, 0x44)));
}

void set_vertex(uint8_t* section, uint32_t index, float x, float y, float z) {
    float* v = section_vertex(section, index);
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

// The mesh builder's allocator (0x00902928) for the 8-byte material {vtable 0x007be7f4,
// section}, then the mesh's one section (format 0x00902cac, 12-byte vertices) locked.
uint32_t* begin_debug_mesh(uint32_t vertex_count, uint32_t unused) {
    using Alloc = uint32_t* XB_CDECL(uint32_t, uint32_t, uint32_t);
    nglMeshBuilder_Begin(0x80000, 1, 0, nullptr);
    uint32_t* material = title_function<Alloc>(title_global<uint32_t>(0x00902928))(8, 4, 0);
    if (material) material[0] = 0x007be7f4;
    nglMeshBuilder_AddSection(material, 0x00902cac, vertex_count, unused, 0, nullptr, 0xc);
    uint8_t* section = material_section(material);
    if (!(section_word(section, 0x24) & 0x40000)) {
        section_word(material_section(material), 0x44) =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(nglLockVertexBuffer(section + 0x44, 0, 0)));
    }
    return material;
}

uint32_t* end_debug_mesh(uint32_t* material, float radius) {
    uint8_t* section = material_section(material);
    if (!(section_word(section, 0x24) & 0x40000)) {
        nglVertexBuffer_Unlock(reinterpret_cast<uint32_t*>(section + 0x44));
    }
    alignas(16) float center[4];
    std::memcpy(center, &title_global<float>(0x007be720), sizeof(center));  // (0, 0, 0, 1)
    nglMeshBuilder_SetBounds(center, radius);
    return nglMeshBuilder_End();
}

// The title's packed polynomial sine (one lane, like scene_sine_poly) with the rounding
// constant of the vector at 0x008a2f30 (1.5 * 2^23).
float debug_sine(float angle, float magic) {
    const float y = -abs_bits(angle) * from_bits(0x3e22f983);  // 1 / (2 pi)
    const float z = abs_bits(((y - magic) + magic) - y - 0.5f) - 0.25f;
    const float z2 = z * z;
    const float z3 = z * z2;
    const float z4 = z2 * z2;
    const float z5 = z * z4;
    float sum = z5 * z4 * from_bits(0x421ed7b7) + z3 * z4 * from_bits(0xc2992661);
    sum = sum + z5 * from_bits(0x42a33457);
    sum = sum + z3 * from_bits(0xc2255de0);
    return sum + z * from_bits(0x40c90fda);
}

}  // namespace

void XB_CDECL nglDrawFpsCounter() {
    char text[0x800];
    if (title_global<uint8_t>(0x00902c00) == 1) {
        const uint32_t free_memory = nglGetFreeMemory();
        const auto f = [](uint32_t address) { return static_cast<double>(title_global<float>(address)); };
        const auto u = [](uint32_t address) { return title_global<uint32_t>(address); };
        // The format has one %d more (ALLOCS COUNT) than arguments: it prints the
        // never-written word above them, zero in tests.
        title_function<int32_t XB_CDECL(char*, const char*, ...)>(0x001ee901)(  // _sprintf
            text, &title_global<const char>(0x007bcc58), &title_global<const char>(0x007bcd68), f(0x00902c60),
            f(0x00902c6c), f(0x00902c70), f(0x00902c7c), f(0x00902c78), f(0x00902c1c), f(0x00902c20), u(0x00902c84),
            u(0x00902c80), u(0x00902c88), u(0x00902c10), u(0x009025ac), u(0x00902c14), u(0x00903c44), u(0x00902c18),
            u(0x00903c1c), free_memory, 0u);
    } else {
        title_function<int32_t XB_CDECL(char*, const char*, ...)>(0x001ee901)(  // _sprintf
            text, &title_global<const char>(0x007bcc40), static_cast<double>(title_global<float>(0x00902c60)),
            static_cast<double>(title_global<float>(0x00902c6c)));
    }
    int32_t width = 0;
    int32_t height = 0;
    nglGetTextExtent(system_font(), text, &width, &height, 1.0f, 1.0f);
    NglQuad quad;
    nglInitQuad(&quad);
    const float bottom = platform_x87_unsigned_to_float(static_cast<uint32_t>(height) + 10);
    const float left = platform_x87_unsigned_to_float(0x26c - static_cast<uint32_t>(width));
    nglSetQuadRect(&quad, left, 10.0f, 640.0f, bottom);
    nglSetQuadColor(&quad, 0xc0000000);
    nglSetQuadZ(&quad, from_bits(0xc61c3c00));  // -9999
    nglListAddQuad(&quad);
    const float x = platform_x87_unsigned_to_float(0x276 - static_cast<uint32_t>(width));
    nglListAddText(system_font(), text, x, 20.0f, from_bits(0xc61c3c00), 0xffffffff, 1.0f, 1.0f);
}

void XB_CDECL nglDrawFrameTimeBar() {
    const int32_t screen_width = nglGetScreenWidth();
    const int32_t intervals = title_global<int32_t>(0x008a2f00);
    const float count = static_cast<float>(intervals);
    const float length = static_cast<float>(screen_width) - 36.0f;
    const float step = length / count;
    NglQuad quad;
    nglInitQuad(&quad);
    const float right = length + 18.0f;
    nglSetQuadRect(&quad, 18.0f, 18.0f, right, 28.0f);
    nglSetQuadColor(&quad, 0xe0001020);
    nglSetQuadZ(&quad, -1000.0f);
    nglListAddQuad(&quad);
    // The two times as bars, at most the bar's length.
    float frame = nglIsPAL50() ? 20.0f : from_bits(0x41855555);  // 16.6667
    float bar = title_global<float>(0x00902bd4) / frame * step;
    if (!(length > bar)) bar = length;
    nglSetQuadRect(&quad, 18.0f, 18.0f, bar + 18.0f, 23.0f);
    nglSetQuadColor(&quad, 0xffffff00);
    nglListAddQuad(&quad);
    frame = nglIsPAL50() ? 20.0f : from_bits(0x41855555);
    bar = title_global<float>(0x00902bd8) / frame * step;
    if (!(length > bar)) bar = length;
    nglSetQuadRect(&quad, 18.0f, 23.0f, bar + 18.0f, 28.0f);
    nglSetQuadColor(&quad, 0xff0000ff);
    nglListAddQuad(&quad);
    float x = 18.0f;
    nglSetQuadColor(&quad, 0xffffffff);
    if (intervals > 0) {
        // Per interval a full tick, then half, quarter and three-quarter ticks.
        const float half = step * 0.5f;
        const float half_end = half + 1.25f;
        const float quarter = step * 0.25f;
        const float quarter_end = quarter + 1.25f;
        const float three = step * 0.75f;
        const float three_end = three + 1.25f;
        for (int32_t i = 0; i < title_global<int32_t>(0x008a2f00); ++i) {
            nglSetQuadRect(&quad, x, 18.0f, x + 1.25f, 28.0f);
            nglListAddQuad(&quad);
            nglSetQuadRect(&quad, half + x, 18.0f, half_end + x, 23.0f);
            nglListAddQuad(&quad);
            nglSetQuadRect(&quad, quarter + x, 18.0f, quarter_end + x, 20.5f);
            nglListAddQuad(&quad);
            nglSetQuadRect(&quad, three + x, 18.0f, three_end + x, 20.5f);
            nglListAddQuad(&quad);
            x = x + step;
        }
    }
    nglSetQuadRect(&quad, right, 18.0f, length + 19.25f, 28.0f);
    nglListAddQuad(&quad);
}

PLATFORM_ASM_ENTRY(nglDebugMesh_AddBox_entry,
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(nglDebugMesh_AddBox) "addl $12, %esp\n"
                   "ret")

void XB_CDECL nglDebugMesh_AddBox(const float* size, NglDebugMeshWriter* writer, const float* center) {
    const Lanes half = mul(lanes(size), 0.5f);
    const auto emit = [&](const Lanes& corner) {
        const Lanes v = add(lanes(center), mul(half, corner));
        platform_keep(v);
        uint8_t* section = material_section(writer->material);
        set_vertex(section, writer->vertex_count, v.v[0], v.v[1], v.v[2]);
        ++writer->vertex_count;
    };
    float s = 1.0f;
    for (int32_t side = 0; side < 2; ++side) {
        // One face pair per axis: y = s, x = s, z = s (the w lanes as the title has them).
        nglMeshBuilder_AddStrip(4, 0xc);
        emit(make_lanes(-1.0f, s, 1.0f, -1.0f));
        emit(make_lanes(1.0f, s, 1.0f, 1.0f));
        emit(make_lanes(-1.0f, s, -1.0f, -1.0f));
        emit(make_lanes(1.0f, s, -1.0f, 1.0f));
        nglMeshBuilder_AddStrip(4, 0xc);
        emit(make_lanes(s, -1.0f, 1.0f, s));
        emit(make_lanes(s, 1.0f, 1.0f, s));
        emit(make_lanes(s, -1.0f, -1.0f, s));
        emit(make_lanes(s, 1.0f, -1.0f, s));
        nglMeshBuilder_AddStrip(4, 0xc);
        emit(make_lanes(-1.0f, 1.0f, s, -1.0f));
        emit(make_lanes(1.0f, 1.0f, s, 1.0f));
        emit(make_lanes(-1.0f, -1.0f, s, -1.0f));
        emit(make_lanes(1.0f, -1.0f, s, 1.0f));
        s = s * -1.0f;
    }
}

void XB_CDECL nglDebugDrawLine(const float* from, const float* to, uint32_t color, uint8_t arrow) {
    const Lanes d = sub(lanes(to), lanes(from));
    const Lanes sq = mul(d, d);
    const float length_sq = (sq.v[0] + sq.v[1]) + sq.v[2];
    // UCOMISS/JNE: zero and NaN keep the squared length.
    const float length = (length_sq == 0.0f || length_sq != length_sq) ? length_sq : rsqrt_length(length_sq);
    if (0.001f > length) return;
    const float inv = length_sq == 0.0f ? 0.0f : rsqrt_newton(length_sq);
    const Lanes dir = mul(d, inv);
    // A vector across the line: from (x, z, y) or (z, y, x) of the direction.
    Lanes side;
    if (dir.v[1] > 0.9f) {
        side = mul(make_lanes(dir.v[0], dir.v[2], dir.v[1], dir.v[3]), make_lanes(0.0f, -1.0f, 1.0f, 0.0f));
    } else {
        side = mul(make_lanes(dir.v[2], dir.v[1], dir.v[0], dir.v[3]), make_lanes(1.0f, 0.0f, -1.0f, 0.0f));
    }
    const Lanes side_sq = mul(side, side);
    const float side_len_sq = (side_sq.v[0] + side_sq.v[1]) + side_sq.v[2];
    const float side_inv = side_len_sq == 0.0f ? 0.0f : rsqrt_newton(side_len_sq);
    const Lanes perp = mul(side, side_inv);
    // dir x perp, lane by lane as the shuffles do it.
    const Lanes a = mul(dir, make_lanes(perp.v[1], perp.v[2], perp.v[0], perp.v[3]));
    const Lanes b = mul(make_lanes(dir.v[1], dir.v[2], dir.v[0], dir.v[3]), perp);
    const Lanes t = sub(a, b);
    const Lanes mid = mul(add(lanes(from), lanes(to)), 0.5f);
    platform_keep(t);
    platform_keep(mid);
    alignas(16) float matrix[16] = {perp.v[0], perp.v[1], perp.v[2], perp.v[3], t.v[1],   t.v[2],
                                    t.v[0],    t.v[3],    dir.v[0],  dir.v[1],  dir.v[2], dir.v[3],
                                    mid.v[0],  mid.v[1],  mid.v[2],  mid.v[3]};
    const float w = line_width();
    alignas(16) float scale[4] = {w, w, length, w};
    nglDebugDrawBoxMesh(matrix, reinterpret_cast<const uint32_t*>(scale), color, 1);
    if (arrow) {
        const float w2 = line_width() * 2.0f;
        std::memcpy(matrix + 12, to, 16);
        scale[0] = w2;
        scale[1] = w2;
        scale[2] = w2;
        scale[3] = w2;
        nglDebugDrawPyramidMesh(matrix, reinterpret_cast<const uint32_t*>(scale), color);
    }
}

void XB_CDECL nglDebugDrawAxes(const float* matrix, float scale, uint32_t color_x, uint32_t color_y,
                               uint32_t color_z) {
    const float* origin = matrix + 12;
    alignas(16) float end[4];
    const uint32_t colors[3] = {color_x, color_y, color_z};
    for (int32_t axis = 0; axis < 3; ++axis) {
        const Lanes e = add(lanes(origin), mul(lanes(matrix + axis * 4), scale));
        std::memcpy(end, e.v, sizeof(end));
        nglDebugDrawLine(origin, end, colors[axis], 1);
    }
}

void XB_THISCALL nglDebugLineBatch_Draw(uint8_t* self) {
    const auto field = [self](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(self + offset); };
    uint32_t& cull_mode = title_global<uint32_t>(0x00902fac);
    if (cull_mode != 0) {
        title_function<void XB_STDCALL(uint32_t)>(0x003e80f0)(0);  // SetRenderState_CullMode
        cull_mode = 0;
    }
    nglSetBlendMode(&title_global<uint8_t>(0x00902f08), 0, 0, 0);
    uint8_t* vertices = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(field(0x10)));
    if (field(0x18)) {
        // Screen space: only the depths go through the Z mapping.
        for (uint32_t i = 0; i < field(0x14); ++i) {
            float* z = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(field(0x10))) +
                                                i * 16 + 8);
            *z = nglDepthToZValue(*z);
        }
        nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00903c9c));
    } else {
        const float* s = reinterpret_cast<const float*>(title_global<const uint8_t*>(0x0090259c) + 0x190);
        const float* m = reinterpret_cast<const float*>(static_cast<uintptr_t>(field(0xc)));
        Lanes rows[4];
        if (m) {
            for (int32_t r = 0; r < 3; ++r) {
                rows[r] = add(add(mul(lanes(s), m[r * 4]), mul(lanes(s + 4), m[r * 4 + 1])),
                              mul(lanes(s + 8), m[r * 4 + 2]));
            }
            rows[3] = add(add(add(lanes(s + 12), mul(lanes(s), m[12])), mul(lanes(s + 4), m[13])),
                          mul(lanes(s + 8), m[14]));
        } else {
            for (int32_t r = 0; r < 4; ++r) rows[r] = lanes(s + r * 4);
        }
        // Transposed into constants 6-9.
        alignas(16) float constants[16];
        for (int32_t c = 0; c < 4; ++c) {
            for (int32_t r = 0; r < 4; ++r) constants[c * 4 + r] = rows[r].v[c];
        }
        using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
        title_function<SetConstants>(0x003e7490)(6, constants, 16);  // NotInlineFast
        nglSetVertexShaderCached(&title_global<NglShaderRef>(0x00903cc8));
    }
    nglSetPixelShaderProgramCached(&title_global<NglShaderRef>(0x00903cb4));
    uint32_t primitive;
    uint32_t primitives;
    if (field(0x1c) == 1) {
        primitive = 4;  // line strip
        primitives = field(0x14) - 1;
    } else {
        primitive = 2;  // line list
        primitives = field(0x14) >> 1;
    }
    vertices = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(field(0x10)));
    title_global<uint32_t>(0x00903c88) = 0;
    title_global<uint32_t>(0x00903c8c) = 0;
    title_global<uint32_t>(0x00903c90) = 0;
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e75e0)(0, 0, 0);  // SetVertexShaderInputDirect
    // D3D's primitive-to-vertex count table {multiplier, offset}.
    const uint32_t* table = &title_global<uint32_t>(0x00831780);
    const uint32_t vertex_count = table[primitive * 2] * primitives + table[primitive * 2 + 1];
    title_function<void XB_STDCALL(uint32_t, uint32_t, const void*, uint32_t)>(0x003e9710)(
        primitive, vertex_count, vertices, 16);  // DrawVerticesUP
}

void XB_CDECL nglInitDebugMeshes() {
    NglDebugMeshWriter writer;
    writer.vtable = 0x007be7d8;
    alignas(16) float size[4];
    alignas(16) float center[4];
    const auto box = [&](float sx, float sy, float sz, float sw, float cx, float cy, float cz, float cw) {
        size[0] = sx;
        size[1] = sy;
        size[2] = sz;
        size[3] = sw;
        center[0] = cx;
        center[1] = cy;
        center[2] = cz;
        center[3] = cw;
        nglDebugMesh_AddBox(size, &writer, center);
    };
    const float radius = from_bits(0x3f3504f3);  // (float)sqrt(0.5) through FSQRT

    // The solid unit box.
    writer.material = begin_debug_mesh(0x18, 6);
    writer.vertex_count = 0;
    std::memcpy(size, &title_global<float>(0x007be560), sizeof(size));      // (1, 1, 1, 1)
    std::memcpy(center, &title_global<float>(0x007be5a0), sizeof(center));  // 0
    nglDebugMesh_AddBox(size, &writer, center);
    title_global<uint32_t*>(0x00902c90) = end_debug_mesh(writer.material, radius);

    // The wire box: its 12 edges as boxes one line width thick.
    const float w = line_width();
    const float h = 0.5f - w * 0.5f;
    writer.material = begin_debug_mesh(0x120, 0x48);
    const float n = 0.0f - h;
    writer.vertex_count = 0;
    box(w, 1.0f, w, w, n, 0.0f, h, n);
    box(w, 1.0f, w, w, h, 0.0f, h, h);
    box(w, 1.0f, w, w, h, 0.0f, n, h);
    box(w, 1.0f, w, w, n, 0.0f, n, n);
    const float l = 1.0f - w * 2.0f;
    box(w, w, l, w, n, n, 0.0f, n);
    box(w, w, l, w, h, n, 0.0f, h);
    box(w, w, l, w, n, h, 0.0f, n);
    box(w, w, l, w, h, h, 0.0f, h);
    box(l, w, w, l, 0.0f, n, h, 0.0f);
    box(l, w, w, l, 0.0f, n, n, 0.0f);
    box(l, w, w, l, 0.0f, h, h, 0.0f);
    box(l, w, w, l, 0.0f, h, n, 0.0f);
    title_global<uint32_t*>(0x00902c94) = end_debug_mesh(writer.material, radius);

    // The sphere: bands of 20 degrees from -90 to 90 and 18 segments, quads (triangles at
    // the bottom pole) of unit vectors (sin a sin b, sin a cos b, cos a) through the
    // packed sine polynomial (cos x = the polynomial at x + 3 pi / 2).
    uint32_t sphere_vertices = 0;
    for (int32_t lat = -90; lat <= 70; lat += 20) {
        for (int32_t i = 0; i < 18; ++i) sphere_vertices += (lat > -90 && lat < 90) ? 4 : 3;
    }
    uint32_t* material = begin_debug_mesh(sphere_vertices, 0xa2);
    const float deg = from_bits(0x3c8efa35);  // pi / 180
    uint32_t index = 0;
    for (int32_t lat = -90; lat <= 70; lat += 20) {
        const float lat_next = static_cast<float>(lat + 20);
        const float lat_this = static_cast<float>(lat);
        for (int32_t lon = 0; lon <= 340; lon += 20) {
            const float* magic = &title_global<float>(0x008a2f30);
            const Lanes angles =
                mul(make_lanes(lat_this, lat_next, static_cast<float>(lon), static_cast<float>(lon + 20)), deg);
            Lanes s;
            Lanes c;
            const Lanes shifted = add(angles, make_lanes(from_bits(0x4096cbe4), from_bits(0x4096cbe4),
                                                         from_bits(0x4096cbe4), from_bits(0x4096cbe4)));
            for (int32_t k = 0; k < 4; ++k) s.v[k] = debug_sine(angles.v[k], magic[k]);
            for (int32_t k = 0; k < 4; ++k) c.v[k] = debug_sine(shifted.v[k], magic[k]);
            const Lanes s_s2 = mul(s, s.v[2]);
            const Lanes s_c2 = mul(s, c.v[2]);
            const Lanes s_s3 = mul(s, s.v[3]);
            const Lanes s_c3 = mul(s, c.v[3]);
            platform_keep(s_s2);
            platform_keep(s_c2);
            platform_keep(s_s3);
            platform_keep(s_c3);
            uint8_t* section = material_section(material);
            if (lat > -90 && lat < 90) {
                nglMeshBuilder_AddStrip(4, 0xc);
                section = material_section(material);
                set_vertex(section, index++, s_s2.v[0], s_c2.v[0], c.v[0]);
                set_vertex(material_section(material), index++, s_s2.v[1], s_c2.v[1], c.v[1]);
                set_vertex(material_section(material), index++, s_s3.v[0], s_c3.v[0], c.v[0]);
            } else {
                nglMeshBuilder_AddStrip(3, 0xc);
                section = material_section(material);
                set_vertex(section, index++, s_s2.v[0], s_c2.v[0], c.v[0]);
                set_vertex(material_section(material), index++, s_s2.v[1], s_c2.v[1], c.v[1]);
            }
            set_vertex(material_section(material), index++, s_s3.v[1], s_c3.v[1], c.v[1]);
        }
    }
    title_global<uint32_t*>(0x00902c98) = end_debug_mesh(material, 1.0f);

    // The pyramid: base square at z = -1, tip at (0, 0, 1).
    material = begin_debug_mesh(0x10, 5);
    static const float kPyramid[16][3] = {
        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f},  {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 1.0f},  {1.0f, -1.0f, -1.0f},  {-1.0f, 1.0f, -1.0f},
        {0.0f, 0.0f, 1.0f},    {-1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f},
        {0.0f, 0.0f, 1.0f},    {1.0f, 1.0f, -1.0f},  {1.0f, -1.0f, -1.0f},  {0.0f, 0.0f, 1.0f}};
    static const uint32_t kStrips[5][2] = {{0, 4}, {4, 3}, {7, 3}, {10, 3}, {13, 3}};
    for (const auto& strip : kStrips) {
        nglMeshBuilder_AddStrip(strip[1], 0xc);
        for (uint32_t i = strip[0]; i < strip[0] + strip[1]; ++i) {
            float* v = reinterpret_cast<float*>(static_cast<uintptr_t>(
                section_word(material_section(material), 0x54) + section_word(material_section(material), 0x44) +
                i * 12));
            v[0] = kPyramid[i][0];
            v[1] = kPyramid[i][1];
            v[2] = kPyramid[i][2];
        }
    }
    title_global<uint32_t*>(0x00902c9c) = end_debug_mesh(material, from_bits(0x3fb504f3));  // sqrt(2)
}

void XB_REGS1 XB_CDECL R_DrawDObjSkeleton(const float* transform, const uint8_t* obj) {
    // Three axis end points (origin + 12 axis) the title computes and drops (a compiled-out
    // debug line); the fourth lane of the first is a never-written stack word (zero in tests),
    // of the others the origin's x.
    const Lanes origin = lanes(transform + 9);
    const Lanes twelve = make_lanes(12.0f, 12.0f, 12.0f, 12.0f);
    const Lanes e0 = add(origin, mul(twelve, make_lanes(transform[0], transform[1], transform[2], 0.0f)));
    const Lanes e1 = add(origin, mul(twelve, make_lanes(transform[3], transform[4], transform[5], transform[9])));
    const Lanes e2 = add(origin, mul(twelve, make_lanes(transform[6], transform[7], transform[8], transform[9])));
    platform_keep(e0);
    platform_keep(e1);
    platform_keep(e2);
    const int32_t models = obj[0x2e];
    if (models <= 0) return;
    // The transform as a 4x4 matrix; the w lanes are what the title's unaligned row copies
    // pick up from its neighbouring stack words.
    alignas(16) const float matrix[16] = {transform[0], transform[1], transform[2],  transform[6],
                                          transform[3], transform[4], transform[5],  transform[0],
                                          transform[6], transform[7], transform[8],  transform[12],
                                          transform[9], transform[10], transform[11], transform[12]};
    const uint32_t bones = *reinterpret_cast<const uint32_t*>(obj + 0x20);
    const uint32_t* model_list = reinterpret_cast<const uint32_t*>(obj + 0x38);
    using DrawSkeleton = void XB_CDECL(uint32_t, uint32_t, uint32_t, const float*);
    for (int32_t i = 0; i < models; ++i) {
        const uint32_t* lods = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(model_list[i] + 0x24));
        uint32_t lod = 0;
        while (lods[lod] == 0) ++lod;
        const uint32_t skeleton = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(lods[lod] + 8));
        title_function<DrawSkeleton>(0x00240930)(skeleton, bones + 0x30 + (uint32_t{obj[0x60 + i]} << 6), 0, matrix);
    }
}

void XB_REGS1 XB_CDECL DebugDraw_AddLine(const float* end, const float* start, const float* color, uint32_t width,
                                         int32_t frames, uint32_t grouped, uint32_t fade) {
    if (!title_global<uint32_t>(0x0092be80)) return;
    uint32_t& max = title_global<uint32_t>(0x0092bf70);
    int32_t& count = title_global<int32_t>(0x0092bf74);
    max = 0x400;
    if (count + 1 > 0x400) return;
    const uint8_t group = grouped ? title_global<uint8_t>(0x0092bf88) : 2;
    uint8_t*& lines = title_global<uint8_t*>(0x0092bf78);
    uint8_t*& groups = title_global<uint8_t*>(0x0092bf7c);
    int32_t*& durations = title_global<int32_t*>(0x0092bf80);
    float*& fades = title_global<float*>(0x0092bf84);
    if (!lines) {
        using Alloc = uint8_t* XB_CDECL(uint32_t);
        uint8_t* block = title_function<Alloc>(0x00042c10)(0xb000);  // Z_Malloc
        Com_Memset(block, 0, 0xb000);
        lines = block;
        block = title_function<Alloc>(0x00042c10)(max);
        Com_Memset(block, 0, max);
        groups = block;
        block = title_function<Alloc>(0x00042c10)(max << 2);
        Com_Memset(block, 0, max << 2);
        durations = reinterpret_cast<int32_t*>(block);
        block = title_function<Alloc>(0x00042c10)(max << 2);
        Com_Memset(block, 0, max << 2);
        fades = reinterpret_cast<float*>(block);
        count = 0;
    }
    uint32_t* line = reinterpret_cast<uint32_t*>(lines + count * 0x2c);
    const uint32_t* s = reinterpret_cast<const uint32_t*>(start);
    const uint32_t* e = reinterpret_cast<const uint32_t*>(end);
    const uint32_t* c = reinterpret_cast<const uint32_t*>(color);
    line[0] = s[0];
    line[1] = s[1];
    line[2] = s[2];
    line[3] = e[0];
    line[4] = e[1];
    line[5] = e[2];
    line[6] = c[0];
    line[7] = c[1];
    line[8] = c[2];
    line[9] = c[3];
    line[10] = width;
    if (fade && frames) {
        fades[count] = 1.0f / static_cast<float>(frames);
    } else {
        fades[count] = 0.0f;
    }
    groups[count] = group;
    durations[count] = frames;
    ++count;
}

PLATFORM_ASM_ENTRY(DebugDraw_AddString_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(DebugDraw_AddString) "addl $20, %esp\n"
                   "ret")

void XB_CDECL DebugDraw_AddString(const float* color, const float* origin, float scale, const char* text,
                                  uint32_t grouped) {
    if (!title_global<uint32_t>(0x0092be80)) return;
    uint32_t& max = title_global<uint32_t>(0x0092bf60);
    int32_t& count = title_global<int32_t>(0x0092bf64);
    max = 0x100;
    if (count + 1 > 0x100) return;
    const uint8_t group = grouped ? title_global<uint8_t>(0x0092bf88) : 2;
    uint8_t*& strings = title_global<uint8_t*>(0x0092bf68);
    uint8_t*& groups = title_global<uint8_t*>(0x0092bf6c);
    if (!strings) {
        using Alloc = uint8_t* XB_CDECL(uint32_t);
        uint8_t* block = title_function<Alloc>(0x00042c10)(0x8400);  // Z_Malloc
        Com_Memset(block, 0, 0x8400);
        strings = block;
        block = title_function<Alloc>(0x00042c10)(max);
        Com_Memset(block, 0, max);
        groups = block;
        count = 0;
    }
    uint8_t* record = strings + count * 0x84;
    uint32_t* words = reinterpret_cast<uint32_t*>(record);
    const uint32_t* o = reinterpret_cast<const uint32_t*>(origin);
    const uint32_t* c = reinterpret_cast<const uint32_t*>(color);
    words[0] = o[0];
    words[1] = o[1];
    words[2] = o[2];
    words[3] = c[0];
    words[4] = c[1];
    words[5] = c[2];
    words[6] = c[3];
    std::memcpy(record + 0x1c, &scale, 4);
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(  // _strncpy
        reinterpret_cast<char*>(record + 0x20), text, 0x5f);
    record[0x7f] = 0;
    words[0x80 / 4] = 0;
    groups[count] = group;
    ++count;
}

namespace {

// Rows 0-2 of b times a (b's rows as weights of a's rows 0-2), and row 3 = a3 + b3 a.
void bone_rows(const float* a, const float* b, Lanes rows[4]) {
    for (int32_t r = 0; r < 3; ++r) {
        rows[r] = add(add(mul(lanes(a), b[r * 4]), mul(lanes(a + 4), b[r * 4 + 1])), mul(lanes(a + 8), b[r * 4 + 2]));
    }
    rows[3] = add(add(add(lanes(a + 12), mul(lanes(a), b[12])), mul(lanes(a + 4), b[13])), mul(lanes(a + 8), b[14]));
}

}  // namespace

void XB_CDECL nglBuildBoneMatrixPalette(float* out, uint8_t* node, const uint8_t* section) {
    const uint32_t* instance = *reinterpret_cast<uint32_t* const*>(node + 0x90);
    const uint32_t flags = instance[0];
    // The count and the bone list are read again each iteration (out may overlap section).
    const auto count = [section] { return *reinterpret_cast<const uint32_t*>(section + 8); };
    const auto bone = [section](uint32_t i) {
        return (*reinterpret_cast<const uint16_t* const*>(section + 0xc))[i];
    };
    const float* instance_matrices = reinterpret_cast<const float*>(static_cast<uintptr_t>(instance[2]));
    if (flags & 0x10) {
        for (uint32_t i = 0; i < count(); ++i) std::memcpy(out + i * 16, instance_matrices + bone(i) * 16, 64);
        return;
    }
    if (flags & 0x8 || flags & 0x4) {
        const uint8_t* model = *reinterpret_cast<uint8_t* const*>(node + 0x88);
        const float* bind = *reinterpret_cast<const float* const*>(model + 0x14);
        if (flags & 0x8) {
            for (uint32_t i = 0; i < count(); ++i) {
                Lanes rows[4];
                bone_rows(instance_matrices + bone(i) * 16, bind + bone(i) * 16, rows);
                std::memcpy(out + i * 16, rows, 48);
                std::memcpy(out + i * 16 + 12, rows[3].v, 16);
            }
            return;
        }
        alignas(16) float inverse[16];
        nglNode_GetWorldInverse(reinterpret_cast<NglMeshDrawNode*>(node), inverse);
        for (uint32_t i = 0; i < count(); ++i) {
            Lanes m[4];
            bone_rows(instance_matrices + bone(i) * 16, bind + bone(i) * 16, m);
            // Rows 0-2 and the translation through the inverse (row 3 with its own term).
            Lanes rows[3];
            for (int32_t r = 0; r < 3; ++r) {
                rows[r] = add(add(mul(lanes(inverse), m[r].v[0]), mul(lanes(inverse + 4), m[r].v[1])),
                              mul(lanes(inverse + 8), m[r].v[2]));
            }
            alignas(16) float block[12];
            title_function<void XB_STDCALL(void*, uint32_t, uint32_t, uint32_t)>(0x00011240)(  // eh_vector_constructor_iterator
                block, 0x10, 3, 0x0001b920);
            std::memcpy(block, rows, 48);
            std::memcpy(out + i * 16, block, 48);
            const Lanes t = add(add(add(lanes(inverse + 12), mul(lanes(inverse), m[3].v[0])),
                                    mul(lanes(inverse + 4), m[3].v[1])),
                                mul(lanes(inverse + 8), m[3].v[2]));
            std::memcpy(out + i * 16 + 12, t.v, 16);
        }
        return;
    }
    const float one = title_global<float>(0x007bdd70);
    for (uint32_t i = 0; i < count(); ++i) {
        const Lanes r0 = mul(lanes(&title_global<float>(0x007bdd90)), one);
        const Lanes r1 = mul(lanes(&title_global<float>(0x007bdda0)), one);
        const Lanes r2 = mul(lanes(&title_global<float>(0x007bddb0)), one);
        std::memcpy(out + i * 16, r0.v, 16);
        std::memcpy(out + i * 16 + 4, r1.v, 16);
        std::memcpy(out + i * 16 + 8, r2.v, 16);
        std::memcpy(out + i * 16 + 12, &title_global<float>(0x007bdec0), 16);  // (0, 0, 0, 1)
    }
}

namespace {

uint32_t* queue_slot(uint32_t data, int32_t index) {
    return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(data + static_cast<uint32_t>(index) * 0x30));
}

// The grow step of both push_backs: `words` dwords per element copied, the line's
// constructor loop (`clear_10`) over the first old count + 1 new slots.
void grow_queue(DebugDrawQueue* queue, int words, bool clear_10) {
    const int32_t count = queue->count;
    const int32_t capacity = count > 3 ? count + 4 : count + 1;
    const uint32_t data = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(Mem_AlignedAlloc(static_cast<uint32_t>(capacity) * 0x30, 4, 0)));
    if (clear_10) {
        const uint32_t end = data + static_cast<uint32_t>(count + 1) * 0x30;
        for (uint32_t slot = data; slot != end; slot += 0x30)
            if (slot) *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(slot + 0x10)) = 0;  // 0.0f
    }
    for (int32_t i = 0; i < queue->count; ++i) {
        const uint32_t* from = queue_slot(queue->data, i);
        uint32_t* to = queue_slot(data, i);
        for (int w = 0; w < words; ++w) to[w] = from[w];
    }
    if (queue->data) {
        Mem_AlignedFree(reinterpret_cast<void*>(static_cast<uintptr_t>(queue->data)));
        queue->data = 0;
    }
    queue->capacity = capacity;
    queue->data = data;
}

void push_queue(DebugDrawQueue* queue, const uint32_t* element, int words, bool clear_10) {
    if (queue->count >= queue->capacity) grow_queue(queue, words, clear_10);
    uint32_t* slot = queue_slot(queue->data, queue->count);
    ++queue->count;
    for (int w = 0; w < words; ++w) slot[w] = element[w];
}

}  // namespace

void XB_REGS1 XB_STDCALL DebugDraw_QueueLine(DebugDrawQueue* queue, const uint32_t* line) {
    push_queue(queue, line, 9, true);
}

void XB_REGS1 XB_STDCALL DebugDraw_QueueTriangle(DebugDrawQueue* queue, const uint32_t* triangle) {
    push_queue(queue, triangle, 12, false);
}

uint32_t* XB_REGS3 XB_STDCALL DebugDraw_MakeTriangle(uint32_t, const uint32_t* b, const uint32_t* c, uint32_t* out,
                                                     const uint32_t* a) {
    for (int w = 0; w < 4; ++w) out[w] = a[w];
    for (int w = 0; w < 4; ++w) out[4 + w] = b[w];
    for (int w = 0; w < 4; ++w) out[8 + w] = c[w];
    return out;
}

void XB_REGS2 XB_CDECL DebugDraw_AddTriangleIfEnabled(uint32_t, const uint32_t* b, const uint32_t* a,
                                                      const uint32_t* c) {
    if (!title_global<uint8_t>(0x0095f688)) return;
    alignas(16) uint32_t triangle[12];
    DebugDraw_QueueTriangle(&title_global<DebugDrawQueue>(0x0097a8b0), DebugDraw_MakeTriangle(0, b, c, triangle, a));
}

PLATFORM_ASM_ENTRY(DebugDraw_AddLineIfEnabled_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(DebugDraw_AddLineIfEnabled) "addl $12, %esp\n"
                   "ret")

void XB_CDECL DebugDraw_AddLineIfEnabled(const uint32_t* second, const uint32_t* first, float value) {
    if (!title_global<uint8_t>(0x0095f688)) return;
    alignas(16) uint32_t line[9];
    for (int w = 0; w < 4; ++w) line[w] = first[w];
    std::memcpy(&line[4], &value, 4);
    for (int w = 0; w < 4; ++w) line[5 + w] = second[w];
    DebugDraw_QueueLine(&title_global<DebugDrawQueue>(0x00984ce0), line);
}

void XB_CDECL DebugDraw_Flush(uint32_t toggle) {
    // Drops the queued strings and the lines of one group: with `toggle` the group flips
    // first (0x0092bf88 = !0x0092bf88) and that new group's entries go, else group 2's
    // (ungrouped). Lines lose one frame and their fade from the alpha (+0x24) and go once out
    // of frames. A dropped entry takes the last one's place. Both lists are handed to the
    // draw callbacks (0x00928568 strings, 0x0092856c lines).
    if (!title_global<uint32_t>(0x0092be80)) return;
    uint8_t group;
    if (toggle) {
        uint8_t& current = title_global<uint8_t>(0x0092bf88);
        current = current == 0;
        group = current;
    } else {
        group = 2;
    }
    using Draw = void XB_CDECL(uint8_t*, int32_t);
    uint8_t*& strings = title_global<uint8_t*>(0x0092bf68);
    if (strings) {
        int32_t& count = title_global<int32_t>(0x0092bf64);
        uint8_t*& groups = title_global<uint8_t*>(0x0092bf6c);
        int32_t n = count;
        for (int32_t i = 0; i < n;) {
            if (groups[i] == group) {
                --count;
                groups[i] = groups[count];
                platform_rep_movs(strings + i * 0x84, strings + count * 0x84, 0x84);
                n = count;
            } else {
                ++i;
            }
        }
        title_function<Draw>(title_global<uint32_t>(0x00928568))(strings, n);
    }
    uint8_t*& lines = title_global<uint8_t*>(0x0092bf78);
    if (lines) {
        int32_t& count = title_global<int32_t>(0x0092bf74);
        uint8_t*& groups = title_global<uint8_t*>(0x0092bf7c);
        int32_t*& frames = title_global<int32_t*>(0x0092bf80);
        float*& fades = title_global<float*>(0x0092bf84);
        for (int32_t i = 0; i < count;) {
            if (groups[i] == group) {
                --frames[i];
                float& alpha = *reinterpret_cast<float*>(lines + i * 0x2c + 0x24);
                alpha = alpha - fades[i];
                if (frames[i] <= 0) {
                    --count;
                    groups[i] = groups[count];
                    frames[i] = frames[count];
                    reinterpret_cast<uint32_t*>(fades)[i] = reinterpret_cast<const uint32_t*>(fades)[count];
                    platform_rep_movs(lines + i * 0x2c, lines + count * 0x2c, 0x2c);
                    continue;
                }
            }
            ++i;
        }
        title_function<Draw>(title_global<uint32_t>(0x0092856c))(lines, count);
    }
}

uint32_t XB_CDECL DebugDraw_Shutdown() {
    // Frees the line and string queues' arrays and clears their state (0x0092bf60, 11 words).
    const auto release = [](uint32_t address) {
        void*& block = title_global<void*>(address);
        if (block) {
            title_function<void XB_CDECL(void*)>(0x000428d0)(block);  // Z_Free
            block = nullptr;
        }
    };
    release(0x0092bf78);
    release(0x0092bf7c);
    release(0x0092bf80);
    release(0x0092bf84);
    release(0x0092bf68);
    release(0x0092bf6c);
    std::memset(&title_global<uint32_t>(0x0092bf60), 0, 11 * 4);
    return 0;
}

void XB_CDECL R_DrawSkeletonBones_r(const uint8_t* parts, const uint8_t* matrices, int32_t parent, const float* matrix) {
    for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(parts + 0x10); ++i) {
        const uint32_t count = *reinterpret_cast<const uint32_t*>(parts + 0x10);
        const uint8_t* bones = *reinterpret_cast<const uint8_t* const*>(parts + 0x14);
        if (*reinterpret_cast<const int32_t*>(bones + (i < count ? i : 0) * 12 + 8) != parent) continue;
        // Both end points through the matrix over four lanes (the w lane repeats x).
        const float* from = reinterpret_cast<const float*>(matrices + parent * 0x40 + 0x30);
        const float* to = reinterpret_cast<const float*>(matrices + i * 0x40 + 0x30);
        const Lanes start = add(add(add(lanes(matrix + 12), mul(lanes(matrix), from[0])), mul(lanes(matrix + 4), from[1])),
                                mul(lanes(matrix + 8), from[2]));
        const Lanes end = add(add(add(lanes(matrix + 12), mul(lanes(matrix), to[0])), mul(lanes(matrix + 4), to[1])),
                              mul(lanes(matrix + 8), to[2]));
        platform_keep(start);
        platform_keep(end);
        R_DrawSkeletonBones_r(parts, matrices, static_cast<int32_t>(i), matrix);
    }
}

}  // namespace game
