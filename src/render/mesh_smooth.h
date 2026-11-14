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
//   angle), folds (the faces meet at a sharp angle), a vertex normal leaning far from its face, edges with
//   more than two triangles, edges at a vertex without a usable normal and, unless
//   `smooth_open_edges`, open edges (a section's border: the next section may have other normals there).
//   A triangle with three straight edges stays flat. Outlines (chains of fold/crease edges that turn gently,
//   a tire's rim) are curved along themselves (Catmull-Rom tangents from the chain neighbours).
// - Volume: corners move back by the average bulge of their curved edges (the patch moves with them,
//   interpolated), so a model does not swell; open-border positions stay.
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
    // Hard-surface guards (low-poly boxes with averaged normals bulge otherwise): an edge whose two faces meet
    // at more than this angle (cos) is straight, and so is a triangle edge at a vertex whose normal leans further
    // from the triangle's own face than `lean_cos`.
    float fold_cos = 0.42f;        // ~65 degrees
    float lean_cos = 0.766f;       // ~40 degrees
    float strength = 0.75f;        // 0: flat, 1: full PN curvature
    // The curvature follows normals made from the faces around each point (area-weighted, per smoothing group)
    // instead of the model's own: flat parts with leaning (averaged) normals stay flat. Shading keeps the
    // model's normals.
    bool geometry_normals = true;
    // Outlines: a chain of fold/crease edges (a tire's rim) is curved along itself where it turns by less than
    // this angle (cos) at each point; corners (a box's) stay.
    bool round_outlines = true;
    float outline_turn_cos = 0.643f;  // ~50 degrees
    // Volume: each corner moves back by this share of the average bulge (at the midpoint) of its curved edges, so
    // the smoothed model sits around the original instead of outside it (open borders stay; 0: off). 1: the
    // patches' interiors bulge about as much as the edge midpoints, so this keeps the mean surface in place.
    float keep_volume = 1.0f;
    bool smooth_open_edges = false;
    uint32_t max_vertices = 65535; // 16-bit indices
};

struct Result {
    std::vector<uint8_t> vertices;  // vertex_count * stride
    std::vector<uint16_t> indices;  // a triangle list
    uint32_t vertex_count = 0;
    uint32_t triangles_in = 0, straight_edges = 0, curved_edges = 0, rounded_edges = 0;  // rounded: of the straight
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
