#pragma once

// Model smoothing (engine improvements phase 1g): a mesh section's triangles as curved PN triangles
// (Vlachos et al. 2001) cut into segments x segments smaller ones on the CPU, with PN-AEN's shared edge
// control points (McDonald & Kilgard 2010) so neighbouring triangles meet without cracks. Runs once per
// section (the caller keeps the result); back-end independent (D3D9 has no tessellation shaders).
//
// - Positions: the cubic patch. Each edge's control points come from the edge's two end positions and
//   normals only, averaged over the triangles on that edge and always computed in one direction, so every
//   triangle on an edge places its new vertices on exactly the same points (bit for bit).
// - Straight (linear) edges: creases (the triangles' normals at an end differ by more than the crease
//   angle), edges with more than two triangles, edges at a vertex without a usable normal and, unless
//   `smooth_open_edges`, open edges (a section's border: the next section may have other normals there).
//   A triangle with three straight edges stays flat.
// - Normals: the PN quadratic, straight edges interpolated linearly; all other attributes linearly
//   (texture coordinates, colours, tangent frames re-normalized), bone weights merged: the four
//   strongest of the corners' bones, renormalized.
// - The original vertices stay first, in order and unchanged; the new ones follow.

#include <cstdint>
#include <vector>

namespace render::smooth {

enum class Format : uint8_t {
    Float1, Float2, Float3, Float4,
    Color,         // 4 bytes, each 0..255 (D3DCOLOR; byte order kept)
    UByte4N,       // 4 bytes, each 0..255
    Short2, Short4, Short2N, Short4N,
    PackedNormal,  // Xbox NORMPACKED3: 11:11:10 signed normalized x, y, z
};

enum class Role : uint8_t {
    Other,         // interpolated by its format
    Position,      // Float3; required
    Normal,        // Float3 or PackedNormal; required
    BlendIndices,  // UByte4N/Color: four bone bytes, copied (never interpolated)
    BlendWeights,  // UByte4N/Color: their four weights
};

struct Attribute {
    uint16_t offset;
    Format format;
    Role role;
};

struct Layout {
    uint32_t stride;
    const Attribute* attributes;
    uint32_t count;
};

enum class Topology : uint8_t { Triangles, Strip, Quads };

struct Options {
    uint32_t segments = 3;         // per original edge (1: no change)
    float crease_cos = 0.5f;       // normals further apart than this (cos of the angle): a crease
    bool smooth_open_edges = false;
    uint32_t max_vertices = 65535; // 16-bit indices
};

struct Result {
    std::vector<uint8_t> vertices;  // vertex_count * stride
    std::vector<uint16_t> indices;  // a triangle list
    uint32_t vertex_count = 0;
    uint32_t triangles_in = 0, straight_edges = 0, curved_edges = 0;
};

// `vertices`: vertex_count vertices of the layout's stride. `indices` (null: 0, 1, 2, ...) has
// `index_count` entries in `topology`. False (result unspecified) when it cannot: no position or normal,
// an index past vertex_count, more than max_vertices, or no triangle to curve.
bool smooth(const Layout& layout, const uint8_t* vertices, uint32_t vertex_count, Topology topology,
            const uint16_t* indices, uint32_t index_count, const Options& options, Result& result);

// NORMPACKED3 as the vertex shaders read it (src/render/shaders/nv2a.hlsli nv_normpacked3), and back
// (rounded to nearest, clamped).
void unpack_normal(uint32_t packed, float out[3]);
uint32_t pack_normal(const float in[3]);

}  // namespace render::smooth
