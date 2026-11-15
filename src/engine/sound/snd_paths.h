#pragma once

// Sound paths round corners and through openings (engine improvements phase 3.4): a coarse grid of the air round
// the listener (cells joined where our acoustic scene leaves the line between their centres open), the shortest
// way from the listener to every cell (Dijkstra), and per hidden sound its way back string-pulled into waypoints:
// the length, the bends, and the opening it reaches the listener through. Worker thread only (snd_env's ray worker).
// Our addition, not title code.

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "acoustic_scene.h"

namespace game {

// The distances over the grid from one listener position (immutable once built: shared with the ray worker).
class SoundField {
public:
    struct Path {
        bool found = false;
        float length = 0;              // listener -> waypoints -> source
        int bends = 0;                 // waypoints between
        float opening[3] = {0, 0, 0};  // the first waypoint from the listener (where the sound comes from)
    };
    // The way to `source` (its centre), when it is inside the grid and reachable; `waypoints` (optional) gets the
    // listener, the waypoints and the source.
    Path path_to(const AcousticScene& scene, const float source[3],
                 std::vector<std::array<float, 3>>* waypoints = nullptr) const;
    const float* listener() const { return listener_; }

    static constexpr int kHalfXY = 20, kHalfZ = 6;
    static constexpr int kNX = 2 * kHalfXY + 1, kNZ = 2 * kHalfZ + 1;
    static constexpr float kCell = 64.0f;

private:
    friend class SoundPaths;
    int index(int x, int y, int z) const { return (z * kNX + y) * kNX + x; }
    void centre(int x, int y, int z, float out[3]) const;
    int origin_[3] = {0, 0, 0};  // global cell of grid (0,0,0)
    float listener_[3] = {0, 0, 0};
    std::vector<float> distance_;
    std::vector<int32_t> previous_;
};

// Builds fields; keeps the edge tests while the scene is the same.
class SoundPaths {
public:
    std::shared_ptr<const SoundField> build(const AcousticScene& scene, const float listener[3]);

private:
    bool open_edge(const AcousticScene& scene, const SoundField& field, int x, int y, int z, int dir);
    const AcousticScene* scene_ = nullptr;
    size_t scene_triangles_ = 0;
    std::unordered_map<uint64_t, uint8_t> edges_;  // (global cell, direction) -> 1 open, 2 blocked
};

}  // namespace game
