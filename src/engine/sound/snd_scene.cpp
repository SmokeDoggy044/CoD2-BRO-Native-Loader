#include "snd_scene.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kCopied = kSceneSolid | kSceneGlass | kSceneFoliage | kSceneWater;

struct BrushCopy {
    float center[3], half[3];
    std::vector<float> sides;  // {normal, dist} each
    uint8_t surface;
    uint32_t contents;
};

struct LevelCopy {
    std::vector<AcousticTriangle> triangles;  // from the meshes
    std::vector<BrushCopy> brushes;
};

// A brush's faces: each plane's polygon clipped by every other plane (Q3's windings), as a triangle fan.
void brush_triangles(const BrushCopy& b, std::vector<AcousticTriangle>& out) {
    struct Plane {
        double n[3], d;
    };
    std::vector<Plane> planes;
    for (int a = 0; a < 3; ++a) {
        Plane lo{{0, 0, 0}, 0}, hi{{0, 0, 0}, 0};
        lo.n[a] = -1;
        lo.d = -(b.center[a] - b.half[a]);
        hi.n[a] = 1;
        hi.d = b.center[a] + b.half[a];
        planes.push_back(lo);
        planes.push_back(hi);
    }
    for (size_t i = 0; i + 3 < b.sides.size(); i += 4) {
        Plane p{{b.sides[i], b.sides[i + 1], b.sides[i + 2]}, double(b.sides[i + 3])};
        bool same = false;
        for (const Plane& q : planes)
            if (p.n[0] * q.n[0] + p.n[1] * q.n[1] + p.n[2] * q.n[2] > 0.9999f && std::fabs(p.d - q.d) < 0.01f)
                same = true;
        if (!same) planes.push_back(p);
    }
    using Point = std::array<double, 3>;
    std::vector<Point> poly, next;
    for (size_t i = 0; i < planes.size(); ++i) {
        const Plane& p = planes[i];
        // A big square on the plane.
        int major = 0;
        for (int k = 1; k < 3; ++k)
            if (std::fabs(p.n[k]) > std::fabs(p.n[major])) major = k;
        double up[3] = {0, 0, 0};
        up[major == 2 ? 0 : 2] = 1;
        const double dn = up[0] * p.n[0] + up[1] * p.n[1] + up[2] * p.n[2];
        for (int k = 0; k < 3; ++k) up[k] -= dn * p.n[k];
        const double ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        for (double& c : up) c /= ul;
        const double right[3] = {up[1] * p.n[2] - up[2] * p.n[1], up[2] * p.n[0] - up[0] * p.n[2],
                                up[0] * p.n[1] - up[1] * p.n[0]};
        constexpr double kSize = 131072.0;
        Point origin{p.n[0] * p.d, p.n[1] * p.d, p.n[2] * p.d};
        poly.clear();
        for (const double s : {1.0, -1.0}) {
            for (const double t : {1.0, -1.0}) {
                const double tt = s > 0 ? t : -t;  // around the square
                poly.push_back({origin[0] + (right[0] * s + up[0] * tt) * kSize,
                                origin[1] + (right[1] * s + up[1] * tt) * kSize,
                                origin[2] + (right[2] * s + up[2] * tt) * kSize});
            }
        }
        for (size_t j = 0; j < planes.size() && poly.size() >= 3; ++j) {
            if (j == i) continue;
            const Plane& c = planes[j];
            next.clear();
            for (size_t k = 0; k < poly.size(); ++k) {
                const Point& a = poly[k];
                const Point& e = poly[(k + 1) % poly.size()];
                const double da = a[0] * c.n[0] + a[1] * c.n[1] + a[2] * c.n[2] - c.d;
                const double de = e[0] * c.n[0] + e[1] * c.n[1] + e[2] * c.n[2] - c.d;
                if (da <= 0.01f) next.push_back(a);
                if ((da < -0.01f && de > 0.01f) || (da > 0.01f && de < -0.01f)) {
                    const double t = da / (da - de);
                    next.push_back({a[0] + (e[0] - a[0]) * t, a[1] + (e[1] - a[1]) * t, a[2] + (e[2] - a[2]) * t});
                }
            }
            poly.swap(next);
        }
        if (poly.size() < 3) continue;
        for (size_t k = 1; k + 1 < poly.size(); ++k) {
            AcousticTriangle t;
            const Point* v[3] = {&poly[0], &poly[k], &poly[k + 1]};
            for (int m = 0; m < 3; ++m)
                for (int c = 0; c < 3; ++c) t.v[m][c] = float((*v[m])[c]);
            // Facing out of the brush (the plane's normal): reorder when the fan winds the other way.
            const float e1[3] = {t.v[1][0] - t.v[0][0], t.v[1][1] - t.v[0][1], t.v[1][2] - t.v[0][2]};
            const float e2[3] = {t.v[2][0] - t.v[0][0], t.v[2][1] - t.v[0][1], t.v[2][2] - t.v[0][2]};
            const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            if (double(n[0]) * p.n[0] + double(n[1]) * p.n[1] + double(n[2]) * p.n[2] < 0)
                for (int c = 0; c < 3; ++c) std::swap(t.v[1][c], t.v[2][c]);
            t.surface = b.surface;
            t.contents = b.contents;
            t.brush = true;
            out.push_back(t);
        }
    }
}

// What identifies the loaded collision: the world record and its trees.
std::vector<uintptr_t> level_key() {
    std::vector<uintptr_t> key;
    const uint8_t* world = title_global<const uint8_t*>(0x0095f4fc);
    if (!world) return key;
    const int32_t trees = *reinterpret_cast<const int32_t*>(world + 0xc);
    if (trees <= 0 || trees > 4096) return key;
    for (int32_t i = 0; i < trees; ++i) {
        const uint8_t* tree = *reinterpret_cast<const uint8_t* const*>(world + 0x10 + i * 4);
        key.push_back(reinterpret_cast<uintptr_t>(tree));
        if (tree) {
            key.push_back(*reinterpret_cast<const uint32_t*>(tree + 0x30));
            key.push_back(*reinterpret_cast<const uint32_t*>(tree + 0x38));
        }
    }
    return key;
}

LevelCopy copy_level() {
    LevelCopy copy;
    const uint8_t* world = title_global<const uint8_t*>(0x0095f4fc);
    const int32_t trees = *reinterpret_cast<const int32_t*>(world + 0xc);
    for (int32_t i = 0; i < trees; ++i) {
        const uint8_t* tree = *reinterpret_cast<const uint8_t* const*>(world + 0x10 + i * 4);
        if (!tree) continue;
        const uint32_t meshes = *reinterpret_cast<const uint32_t*>(tree + 0x30);
        const uint8_t* mesh_base = *reinterpret_cast<const uint8_t* const*>(tree + 0x34);
        for (uint32_t m = 0; m < meshes; ++m) {
            const uint8_t* mesh = mesh_base + m * 0x50;
            const uint32_t flags = *reinterpret_cast<const uint32_t*>(mesh + 4);
            const uint32_t contents = *reinterpret_cast<const uint32_t*>(mesh + 8);
            if (!(contents & kCopied)) continue;
            const uint32_t vertices = *reinterpret_cast<const uint32_t*>(mesh + 0x30);
            const float* rows = *reinterpret_cast<const float* const*>(mesh + 0x34);
            const uint32_t count = *reinterpret_cast<const uint32_t*>(mesh + 0x38);
            const uint32_t* words = *reinterpret_cast<const uint32_t* const*>(mesh + 0x3c);
            const float* planes = *reinterpret_cast<const float* const*>(mesh + 0x44);  // per triangle {normal, dist}
            const uint32_t plane_count = *reinterpret_cast<const uint32_t*>(mesh + 0x40);
            if (!rows || !words) continue;
            for (uint32_t t = 0; t < count; ++t) {
                const uint32_t w = words[t];
                const uint32_t index[3] = {w & 0x3f, (w >> 6) & 0x3f, (w >> 12) & 0x3f};
                if (index[0] >= vertices || index[1] >= vertices || index[2] >= vertices) continue;
                AcousticTriangle tri;
                for (int k = 0; k < 3; ++k) std::memcpy(tri.v[k], rows + index[k] * 4, 12);
                // Facing as the triangle's plane (the side the game's ray trace hits).
                if (planes && t < plane_count) {
                    const float* pl = planes + t * 4;
                    const float e1[3] = {tri.v[1][0] - tri.v[0][0], tri.v[1][1] - tri.v[0][1], tri.v[1][2] - tri.v[0][2]};
                    const float e2[3] = {tri.v[2][0] - tri.v[0][0], tri.v[2][1] - tri.v[0][1], tri.v[2][2] - tri.v[0][2]};
                    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                        e1[0] * e2[1] - e1[1] * e2[0]};
                    if (n[0] * pl[0] + n[1] * pl[1] + n[2] * pl[2] < 0)
                        for (int c = 0; c < 3; ++c) std::swap(tri.v[1][c], tri.v[2][c]);
                }
                tri.surface = uint8_t((flags >> 20) & 31);
                tri.contents = contents;
                copy.triangles.push_back(tri);
            }
        }
        const uint32_t brushes = *reinterpret_cast<const uint32_t*>(tree + 0x38);
        const uint8_t* brush_base = *reinterpret_cast<const uint8_t* const*>(tree + 0x3c);
        for (uint32_t b = 0; b < brushes; ++b) {
            const uint8_t* brush = brush_base + b * 0x40;
            const uint32_t contents = *reinterpret_cast<const uint32_t*>(brush + 8);
            if (!(contents & kCopied)) continue;
            BrushCopy c;
            std::memcpy(c.center, brush + 0x10, 12);
            std::memcpy(c.half, brush + 0x20, 12);
            const uint32_t sides = *reinterpret_cast<const uint32_t*>(brush + 0x30);
            const float* planes = *reinterpret_cast<const float* const*>(brush + 0x34);
            if (sides && planes) c.sides.assign(planes, planes + sides * 4);
            c.surface = uint8_t((*reinterpret_cast<const uint32_t*>(brush + 4) >> 20) & 31);
            c.contents = contents;
            copy.brushes.push_back(std::move(c));
        }
    }
    return copy;
}

struct State {
    std::mutex mutex;
    std::shared_ptr<const AcousticScene> scene;
    std::vector<uintptr_t> key;  // of the scene built or being built (game thread only)
    std::thread builder;
    std::atomic<uint32_t> generation{0};  // a newer build makes an older one's result stale
    ~State() {
        if (builder.joinable()) builder.join();
    }
};
State g;

}  // namespace

std::shared_ptr<const AcousticScene> SndScene_Current() {
    std::lock_guard lock(g.mutex);
    return g.scene;
}

void SndScene_Update() {
    std::vector<uintptr_t> key = level_key();
    if (key == g.key) return;
    g.key = key;
    const uint32_t generation = ++g.generation;
    if (key.empty()) {  // the world is gone; a changed one keeps the old scene until the new one is built
        std::lock_guard lock(g.mutex);
        g.scene.reset();
        return;
    }
    const auto copy_start = std::chrono::steady_clock::now();
    LevelCopy copy = copy_level();
    const double copy_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - copy_start).count();
    if (g.builder.joinable()) g.builder.join();
    g.builder = std::thread([copy = std::move(copy), generation, copy_ms]() mutable {
        const auto start = std::chrono::steady_clock::now();
        const size_t mesh_triangles = copy.triangles.size(), brushes = copy.brushes.size();
        for (const BrushCopy& b : copy.brushes) brush_triangles(b, copy.triangles);
        copy.brushes.clear();
        auto scene = AcousticScene::build(std::move(copy.triangles));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (generation != g.generation.load()) return;  // the level changed meanwhile
        SndScene_Log("acoustic scene: %zu triangles (%zu from meshes, %zu brushes), %zu nodes; copied in %.1f ms "
                 "(game thread), built in %.0f ms (worker)",
                 scene->triangle_count(), mesh_triangles, brushes, scene->node_count(), copy_ms, ms);
        std::lock_guard lock(g.mutex);
        g.scene = std::move(scene);
    });
}

namespace {
void (*g_log)(const char*) = nullptr;
}

void SndScene_SetLog(void (*log)(const char* line)) { g_log = log; }

void SndScene_Log(const char* format, ...) {
    if (!g_log) return;
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log(line);
}

void SndScene_Reset() {
    ++g.generation;
    if (g.builder.joinable()) g.builder.join();
    g.key.clear();
    std::lock_guard lock(g.mutex);
    g.scene.reset();
}

}  // namespace game
