#pragma once

// Our own acoustic scene (engine improvements phase 3.1): the level's collision as triangles in a bounding volume
// hierarchy, for every ray the sound path casts (occlusion, transmission, reverb probes, paths). Built once per
// level on a worker thread; read-only afterwards, so any number of threads can query it at once. Plain geometry:
// nothing here reads or changes the game (the level's brushes and meshes come from snd_scene.cpp).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace game {

struct AcousticTriangle {
    float v[3][3];
    uint8_t surface = 0;    // the game's surface type (bits 20-24 of the surface flags)
    uint32_t contents = 0;  // the object's contents (1 solid, 0x10 glass, ...)
    bool brush = false;     // a brush face (facing out of the solid); else a mesh triangle (one-sided, facing up/out)
};

class AcousticScene {
public:
    // Builds the hierarchy (the caller's thread: a worker). Degenerate triangles are dropped.
    static std::shared_ptr<const AcousticScene> build(std::vector<AcousticTriangle> triangles);

    struct Hit {
        bool hit = false;
        float distance = 0;         // along the ray, world units
        float normal[3] = {0, 0, 0};  // the triangle's, facing the ray's origin
        uint8_t surface = 0;
        uint32_t contents = 0;
        bool brush = false;
    };
    // Rays as the game's world ray trace: triangles only from their front (meshes face up/out, brush faces out of
    // the brush: a ray starting inside a brush does not hit it).
    // The nearest triangle in `mask` along origin + t dir, 0 <= t <= max_distance (dir need not be unit length:
    // distances are in units of |dir|... callers pass unit directions).
    Hit nearest(const float origin[3], const float dir[3], float max_distance, uint32_t mask) const;
    // Any triangle in `mask` between a and b (cheaper than nearest).
    bool blocked(const float a[3], const float b[3], uint32_t mask) const;

    // Every surface crossed between a and b, nearest first (at most `max`; returns the count found up to it).
    struct Crossing {
        float t;          // share of a -> b (0..1)
        bool entering;    // the ray enters the solid here (the triangle faces a), else leaves
        uint8_t surface;
        uint32_t contents;
    };
    size_t crossings(const float a[3], const float b[3], uint32_t mask, Crossing* out, size_t max) const;

    size_t triangle_count() const { return triangles_.size(); }
    size_t node_count() const { return nodes_.size(); }
    void bounds(float mins[3], float maxs[3]) const;

private:
    struct Tri {
        float v0[3], e1[3], e2[3];
        float n[3];  // unit normal (e1 x e2), the solid's outside for brush faces
        uint8_t surface;
        bool brush;
        uint32_t contents;
    };
    struct Node {
        float mins[3], maxs[3];
        uint32_t first;  // leaf: first triangle; node: right child (left = this + 1)
        uint16_t count;  // leaf: triangle count; node: 0
        uint8_t axis;
    };
    template <typename Visit>
    void walk(const float origin[3], const float dir[3], float& max_t, uint32_t mask, bool all_sides, Visit&& visit) const;

    std::vector<Tri> triangles_;
    std::vector<Node> nodes_;
};

}  // namespace game
