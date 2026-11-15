#include "acoustic_scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace game {

namespace {

constexpr int kBins = 12;
constexpr uint32_t kLeafSize = 4;

struct Box {
    float mins[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
    float maxs[3] = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                     -std::numeric_limits<float>::max()};
    void add(const float p[3]) {
        for (int k = 0; k < 3; ++k) {
            mins[k] = std::min(mins[k], p[k]);
            maxs[k] = std::max(maxs[k], p[k]);
        }
    }
    void add(const Box& b) {
        for (int k = 0; k < 3; ++k) {
            mins[k] = std::min(mins[k], b.mins[k]);
            maxs[k] = std::max(maxs[k], b.maxs[k]);
        }
    }
    float area() const {
        const float d[3] = {maxs[0] - mins[0], maxs[1] - mins[1], maxs[2] - mins[2]};
        if (d[0] < 0) return 0;
        return 2 * (d[0] * d[1] + d[1] * d[2] + d[2] * d[0]);
    }
};

struct BuildItem {
    Box box;
    float centre[3];
    uint32_t index;
};

void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Ray against a slab box: the entry distance, or a value > max_t when missed.
inline float slab(const float mins[3], const float maxs[3], const float origin[3], const float inv[3], float max_t) {
    float t0 = 0, t1 = max_t;
    for (int k = 0; k < 3; ++k) {
        float a = (mins[k] - origin[k]) * inv[k];
        float b = (maxs[k] - origin[k]) * inv[k];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return std::numeric_limits<float>::max();
    }
    return t0;
}

}  // namespace

std::shared_ptr<const AcousticScene> AcousticScene::build(std::vector<AcousticTriangle> input) {
    auto scene = std::make_shared<AcousticScene>();
    scene->triangles_.reserve(input.size());
    for (const AcousticTriangle& t : input) {
        Tri tri;
        for (int k = 0; k < 3; ++k) {
            tri.v0[k] = t.v[0][k];
            tri.e1[k] = t.v[1][k] - t.v[0][k];
            tri.e2[k] = t.v[2][k] - t.v[0][k];
        }
        cross(tri.e1, tri.e2, tri.n);
        const float length = std::sqrt(dot(tri.n, tri.n));
        if (!(length > 1e-4f)) continue;  // degenerate (or not a number)
        for (float& c : tri.n) c /= length;
        tri.surface = t.surface;
        tri.contents = t.contents;
        tri.brush = t.brush;
        scene->triangles_.push_back(tri);
    }
    input.clear();
    input.shrink_to_fit();

    std::vector<BuildItem> items(scene->triangles_.size());
    for (uint32_t i = 0; i < items.size(); ++i) {
        const Tri& t = scene->triangles_[i];
        float p1[3], p2[3];
        for (int k = 0; k < 3; ++k) {
            p1[k] = t.v0[k] + t.e1[k];
            p2[k] = t.v0[k] + t.e2[k];
        }
        items[i].box.add(t.v0);
        items[i].box.add(p1);
        items[i].box.add(p2);
        for (int k = 0; k < 3; ++k) items[i].centre[k] = (items[i].box.mins[k] + items[i].box.maxs[k]) * 0.5f;
        items[i].index = i;
    }
    scene->nodes_.reserve(items.size() / 2 + 1);

    // Binned surface-area heuristic, depth first (left child right after its parent).
    struct Task {
        uint32_t begin, end, node;
    };
    std::vector<Task> stack;
    scene->nodes_.push_back({});
    stack.push_back({0, uint32_t(items.size()), 0});
    while (!stack.empty()) {
        const Task task = stack.back();
        stack.pop_back();
        Box box, centres;
        for (uint32_t i = task.begin; i < task.end; ++i) {
            box.add(items[i].box);
            centres.add(items[i].centre);
        }
        Node& node = scene->nodes_[task.node];
        std::memcpy(node.mins, box.mins, sizeof(node.mins));
        std::memcpy(node.maxs, box.maxs, sizeof(node.maxs));
        const uint32_t count = task.end - task.begin;
        auto make_leaf = [&] {
            Node& n = scene->nodes_[task.node];
            n.first = task.begin;
            n.count = uint16_t(count);
        };
        if (count <= kLeafSize) {
            make_leaf();
            continue;
        }
        int best_axis = -1;
        float best_cost = std::numeric_limits<float>::max(), best_split = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const float lo = centres.mins[axis], hi = centres.maxs[axis];
            if (!(hi - lo > 1e-3f)) continue;
            Box bins[kBins];
            uint32_t counts[kBins] = {};
            const float scale = kBins / (hi - lo);
            for (uint32_t i = task.begin; i < task.end; ++i) {
                const int b = std::min(kBins - 1, int((items[i].centre[axis] - lo) * scale));
                bins[b].add(items[i].box);
                ++counts[b];
            }
            float right_area[kBins];
            uint32_t right_count[kBins];
            Box acc;
            uint32_t n = 0;
            for (int b = kBins - 1; b > 0; --b) {
                acc.add(bins[b]);
                n += counts[b];
                right_area[b] = acc.area();
                right_count[b] = n;
            }
            Box left;
            uint32_t left_n = 0;
            for (int b = 0; b < kBins - 1; ++b) {
                left.add(bins[b]);
                left_n += counts[b];
                if (!left_n || !right_count[b + 1]) continue;
                const float cost = left.area() * float(left_n) + right_area[b + 1] * float(right_count[b + 1]);
                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = axis;
                    best_split = lo + float(b + 1) / scale;
                }
            }
        }
        if (best_axis < 0 || (count <= 16 && best_cost >= box.area() * float(count))) {
            if (count <= 0xffff) {
                make_leaf();
                continue;
            }
        }
        uint32_t mid = task.begin;
        if (best_axis >= 0) {
            mid = uint32_t(std::partition(items.begin() + task.begin, items.begin() + task.end,
                                          [&](const BuildItem& it) { return it.centre[best_axis] < best_split; }) -
                           items.begin());
        }
        if (mid == task.begin || mid == task.end) {  // all centres equal: split by count
            best_axis = best_axis < 0 ? 0 : best_axis;
            mid = task.begin + count / 2;
            std::nth_element(items.begin() + task.begin, items.begin() + mid, items.begin() + task.end,
                             [&](const BuildItem& a, const BuildItem& b) {
                                 return a.centre[best_axis] < b.centre[best_axis];
                             });
        }
        const uint32_t left = uint32_t(scene->nodes_.size());
        scene->nodes_.push_back({});
        const uint32_t right = uint32_t(scene->nodes_.size());
        scene->nodes_.push_back({});
        Node& parent = scene->nodes_[task.node];
        parent.first = right;
        parent.count = 0;
        parent.axis = uint8_t(best_axis);
        (void)left;
        // Left (task.node + 1 only for the first child made right after): children are stored as a pair.
        stack.push_back({mid, task.end, right});
        stack.push_back({task.begin, mid, left});
    }
    // Triangles in leaf order.
    std::vector<Tri> ordered(scene->triangles_.size());
    for (uint32_t i = 0; i < items.size(); ++i) ordered[i] = scene->triangles_[items[i].index];
    scene->triangles_ = std::move(ordered);
    return scene;
}

// Children of an inner node: `first` - 1 (left) and `first` (right), made as a pair.
template <typename Visit>
void AcousticScene::walk(const float origin[3], const float dir[3], float& max_t, uint32_t mask, bool all_sides,
                         Visit&& visit) const {
    if (nodes_.empty() || triangles_.empty()) return;
    float inv[3];
    for (int k = 0; k < 3; ++k) inv[k] = dir[k] != 0.0f ? 1.0f / dir[k] : std::numeric_limits<float>::max();
    uint32_t stack[64];
    int depth = 0;
    stack[depth++] = 0;
    while (depth) {
        const Node& node = nodes_[stack[--depth]];
        if (slab(node.mins, node.maxs, origin, inv, max_t) > max_t) continue;
        if (node.count) {
            for (uint32_t i = node.first, end = node.first + node.count; i < end; ++i) {
                const Tri& t = triangles_[i];
                if (!(t.contents & mask)) continue;
                // As the game's ray trace: a mesh only from its front; a brush only where the ray enters it (a ray
                // starting inside a brush leaves it unhit, Q3's startsolid without allsolid).
                if (!all_sides && dot(t.n, dir) > 0) continue;
                // Moller-Trumbore, both sides.
                float p[3], q[3], s[3];
                cross(dir, t.e2, p);
                const float det = dot(t.e1, p);
                if (std::fabs(det) < 1e-9f) continue;
                const float inv_det = 1.0f / det;
                for (int k = 0; k < 3; ++k) s[k] = origin[k] - t.v0[k];
                const float u = dot(s, p) * inv_det;
                if (u < 0.0f || u > 1.0f) continue;
                cross(s, t.e1, q);
                const float v = dot(dir, q) * inv_det;
                if (v < 0.0f || u + v > 1.0f) continue;
                const float hit_t = dot(t.e2, q) * inv_det;
                if (hit_t < 0.0f || hit_t > max_t) continue;
                if (visit(t, hit_t)) return;
            }
            continue;
        }
        const uint32_t left = node.first - 1, right = node.first;
        if (depth > 62) continue;  // cannot happen with a sane tree; never overflow
        // Nearer child last (popped first).
        if (dir[node.axis] < 0) {
            stack[depth++] = left;
            stack[depth++] = right;
        } else {
            stack[depth++] = right;
            stack[depth++] = left;
        }
    }
}

AcousticScene::Hit AcousticScene::nearest(const float origin[3], const float dir[3], float max_distance,
                                          uint32_t mask) const {
    Hit hit;
    float max_t = max_distance;
    const Tri* best = nullptr;
    walk(origin, dir, max_t, mask, false, [&](const Tri& t, float hit_t) {
        if (hit_t <= max_t) {
            max_t = hit_t;
            best = &t;
        }
        return false;
    });
    if (!best) return hit;
    hit.hit = true;
    hit.distance = max_t;
    const float side = dot(best->n, dir) > 0 ? -1.0f : 1.0f;
    for (int k = 0; k < 3; ++k) hit.normal[k] = best->n[k] * side;
    hit.surface = best->surface;
    hit.contents = best->contents;
    hit.brush = best->brush;
    return hit;
}

bool AcousticScene::blocked(const float a[3], const float b[3], uint32_t mask) const {
    const float dir[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float max_t = 1.0f;
    bool any = false;
    walk(a, dir, max_t, mask, false, [&](const Tri&, float) {
        any = true;
        return true;
    });
    return any;
}

size_t AcousticScene::crossings(const float a[3], const float b[3], uint32_t mask, Crossing* out, size_t max) const {
    const float dir[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float max_t = 1.0f;
    std::vector<Crossing> all;
    walk(a, dir, max_t, mask, true, [&](const Tri& t, float hit_t) {
        all.push_back({hit_t, dot(t.n, dir) < 0, t.surface, t.contents});
        return false;
    });
    std::sort(all.begin(), all.end(), [](const Crossing& x, const Crossing& y) { return x.t < y.t; });
    // Faces shared by touching brushes (a wall built of several) cross twice at the same spot: one leave, one
    // enter. They cancel.
    std::vector<Crossing> kept;
    for (size_t i = 0; i < all.size(); ++i) {
        if (i + 1 < all.size() && all[i + 1].t - all[i].t < 1e-5f && all[i].entering != all[i + 1].entering) {
            ++i;
            continue;
        }
        kept.push_back(all[i]);
    }
    const size_t n = std::min(max, kept.size());
    std::copy(kept.begin(), kept.begin() + long(n), out);
    return kept.size();
}

void AcousticScene::bounds(float mins[3], float maxs[3]) const {
    for (int k = 0; k < 3; ++k) {
        mins[k] = nodes_.empty() ? 0 : nodes_[0].mins[k];
        maxs[k] = nodes_.empty() ? 0 : nodes_[0].maxs[k];
    }
}

}  // namespace game
