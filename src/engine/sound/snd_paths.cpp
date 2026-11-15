#include "snd_paths.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace game {

namespace {

// Sideways, diagonal and up/down neighbours; pairs (2k, 2k + 1) are opposites.
constexpr int kDirs[10][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}, {1, 1, 0},
                              {-1, -1, 0}, {1, -1, 0}, {-1, 1, 0}, {0, 0, 1},  {0, 0, -1}};
constexpr float kInfinity = std::numeric_limits<float>::max();
constexpr float kMaxDistance = 3000.0f;  // world units: farther ways are not followed
constexpr uint32_t kSolid = 1 | 0x10;     // solid and glass (as the occlusion lines)

uint64_t edge_key(int gx, int gy, int gz, int dir) {
    const auto c = [](int v) { return uint64_t(uint32_t(v + 2048) & 0xfff); };
    return (c(gx) << 28) | (c(gy) << 16) | (c(gz) << 4) | uint64_t(dir);
}

float length3(const float a[3], const float b[3]) {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

}  // namespace

void SoundField::centre(int x, int y, int z, float out[3]) const {
    out[0] = (float(origin_[0] + x) + 0.5f) * kCell;
    out[1] = (float(origin_[1] + y) + 0.5f) * kCell;
    out[2] = (float(origin_[2] + z) + 0.5f) * kCell;
}

bool SoundPaths::open_edge(const AcousticScene& scene, const SoundField& field, int x, int y, int z, int dir) {
    const int gx = field.origin_[0] + x, gy = field.origin_[1] + y, gz = field.origin_[2] + z;
    const uint64_t key = edge_key(gx, gy, gz, dir);
    const auto found = edges_.find(key);
    if (found != edges_.end()) return found->second == 1;
    float a[3], b[3];
    field.centre(x, y, z, a);
    field.centre(x + kDirs[dir][0], y + kDirs[dir][1], z + kDirs[dir][2], b);
    const uint8_t state = scene.blocked_any(a, b, kSolid) ? 2 : 1;
    edges_[key] = state;
    edges_[edge_key(gx + kDirs[dir][0], gy + kDirs[dir][1], gz + kDirs[dir][2], dir ^ 1)] = state;
    return state == 1;
}

std::shared_ptr<const SoundField> SoundPaths::build(const AcousticScene& scene, const float listener[3]) {
    using F = SoundField;
    constexpr int kNX = F::kNX, kNZ = F::kNZ, kHalfXY = F::kHalfXY, kHalfZ = F::kHalfZ;
    constexpr float kCell = F::kCell;
    auto field = std::make_shared<SoundField>();
    SoundField& f = *field;
    // Another scene (a new object may reuse a freed one's address: its size tells them apart too).
    if (&scene != scene_ || scene.triangle_count() != scene_triangles_ || edges_.size() > 4000000) {
        edges_.clear();
        scene_ = &scene;
        scene_triangles_ = scene.triangle_count();
    }
    for (int k = 0; k < 3; ++k) {
        f.listener_[k] = listener[k];
        f.origin_[k] = int(std::floor(listener[k] / kCell)) - (k < 2 ? kHalfXY : kHalfZ);
    }
    const size_t cells = size_t(kNX) * kNX * kNZ;
    f.distance_.assign(cells, kInfinity);
    f.previous_.assign(cells, -1);
    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    // Seeds: the cells round the listener whose centres it sees.
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = kHalfXY + dx, y = kHalfXY + dy, z = kHalfZ + dz;
                float c[3];
                f.centre(x, y, z, c);
                if (scene.blocked_any(listener, c, kSolid)) continue;
                const int32_t i = f.index(x, y, z);
                f.distance_[i] = length3(listener, c);
                f.previous_[i] = -2;  // seen from the listener
                queue.push({f.distance_[i], i});
            }
    while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        if (d > f.distance_[i] || d > kMaxDistance) continue;
        const int x = i % kNX, y = (i / kNX) % kNX, z = i / (kNX * kNX);
        for (int dir = 0; dir < 10; ++dir) {
            const int nx = x + kDirs[dir][0], ny = y + kDirs[dir][1], nz = z + kDirs[dir][2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= kNX || ny >= kNX || nz >= kNZ) continue;
            const int32_t n = f.index(nx, ny, nz);
            const float step = (dir >= 4 && dir < 8 ? 1.41421356f : 1.0f) * kCell;
            if (d + step >= f.distance_[n]) continue;
            if (!open_edge(scene, f, x, y, z, dir)) continue;
            f.distance_[n] = d + step;
            f.previous_[n] = i;
            queue.push({f.distance_[n], n});
        }
    }
    return field;
}

SoundField::Path SoundField::path_to(const AcousticScene& scene, const float source[3],
                                     std::vector<std::array<float, 3>>* waypoints_out) const {
    Path path;
    std::vector<std::array<float, 3>> waypoints_;
    int cell[3];
    for (int k = 0; k < 3; ++k) cell[k] = int(std::floor(source[k] / kCell)) - origin_[k];
    // The reached cell next to the source (that it sees) with the shortest way.
    int32_t best = -1;
    float best_total = kInfinity;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cell[0] + dx, y = cell[1] + dy, z = cell[2] + dz;
                if (x < 0 || y < 0 || z < 0 || x >= kNX || y >= kNX || z >= kNZ) continue;
                const int32_t i = index(x, y, z);
                if (distance_[i] == kInfinity) continue;
                float c[3];
                centre(x, y, z, c);
                const float total = distance_[i] + length3(c, source);
                if (total >= best_total || scene.blocked_any(c, source, kSolid)) continue;
                best_total = total;
                best = i;
            }
    if (best < 0) return path;
    // The way back: source, cell centres, listener; then string-pulled from the listener.
    std::vector<std::array<float, 3>> points;
    points.push_back({listener_[0], listener_[1], listener_[2]});
    std::vector<int32_t> chain;
    for (int32_t i = best; i >= 0; i = previous_[i]) chain.push_back(i);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const int32_t i = *it;
        std::array<float, 3> c;
        centre(i % kNX, (i / kNX) % kNX, i / (kNX * kNX), c.data());
        points.push_back(c);
    }
    points.push_back({source[0], source[1], source[2]});
    waypoints_.push_back(points[0]);
    size_t current = 0;
    while (current + 1 < points.size()) {
        size_t next = current + 1;
        while (next + 1 < points.size() &&
               !scene.blocked_any(points[current].data(), points[next + 1].data(), kSolid))
            ++next;
        waypoints_.push_back(points[next]);
        current = next;
    }
    path.found = true;
    for (size_t k = 1; k < waypoints_.size(); ++k) path.length += length3(waypoints_[k - 1].data(), waypoints_[k].data());
    path.bends = int(waypoints_.size()) - 2;
    const auto& opening = waypoints_.size() > 2 ? waypoints_[1] : waypoints_.back();
    for (int k = 0; k < 3; ++k) path.opening[k] = opening[k];
    if (waypoints_out) *waypoints_out = waypoints_;
    return path;
}

}  // namespace game
