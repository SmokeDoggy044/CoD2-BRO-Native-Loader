// src/render/mesh_smooth.h: model smoothing (PN-AEN on the CPU).

#include "test_support.h"

#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "mesh_smooth.h"

using namespace render::smooth;

namespace {

// The character layout: position, packed normal, uv, bone indices, weights (stride 32).
struct CharVertex {
    float p[3];
    uint32_t n;
    float uv[2];
    uint8_t bones[4];
    uint8_t weights[4];
};
static_assert(sizeof(CharVertex) == 32);
const Attribute kChar[] = {{0, Format::Float3, Role::Position},
                           {12, Format::PackedNormal, Role::Normal},
                           {16, Format::Float2, Role::Other},
                           {24, Format::UByte4N, Role::BlendIndices},
                           {28, Format::UByte4N, Role::BlendWeights}};
const Layout kCharLayout{32, kChar, 5};

CharVertex make(float x, float y, float z, float nx, float ny, float nz) {
    CharVertex v{};
    v.p[0] = x, v.p[1] = y, v.p[2] = z;
    const float n[3] = {nx, ny, nz};
    v.n = pack_normal(n);
    v.uv[0] = x, v.uv[1] = y;
    v.weights[0] = 255;
    return v;
}

const CharVertex& at(const Result& r, uint32_t i) { return reinterpret_cast<const CharVertex*>(r.vertices.data())[i]; }

bool run(const std::vector<CharVertex>& v, const std::vector<uint16_t>& idx, Result& r, Topology t = Topology::Triangles,
         Options o = {}) {
    return smooth(kCharLayout, reinterpret_cast<const uint8_t*>(v.data()), static_cast<uint32_t>(v.size()), t,
                  idx.empty() ? nullptr : idx.data(), static_cast<uint32_t>(idx.empty() ? v.size() : idx.size()), o, r);
}

float face_z(const Result& r, size_t tri) {  // the z of a triangle's (unnormalized) normal
    const float* a = at(r, r.indices[tri * 3]).p;
    const float* b = at(r, r.indices[tri * 3 + 1]).p;
    const float* c = at(r, r.indices[tri * 3 + 2]).p;
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

}  // namespace

int main() {
    // Packed normals: the shader's decode, and back.
    for (const auto& n : std::vector<std::array<float, 3>>{{1, 0, 0}, {0, -1, 0}, {0, 0, 1}, {0.6f, 0, -0.8f}}) {
        float out[3];
        unpack_normal(pack_normal(n.data()), out);
        for (int i = 0; i < 3; ++i) CHECK(std::fabs(out[i] - n[i]) < 0.003f, "%g vs %g", out[i], n[i]);
    }

    // A flat square (normals up) stays flat; 2 triangles -> 18, 4 + 5 edges * 2 + 2 interior vertices.
    {
        std::vector<CharVertex> v = {make(0, 0, 0, 0, 0, 1), make(1, 0, 0, 0, 0, 1), make(1, 1, 0, 0, 0, 1),
                                     make(0, 1, 0, 0, 0, 1)};
        Options o;
        o.smooth_open_edges = true;
        Result r;
        CHECK(run(v, {0, 1, 2, 0, 2, 3}, r, Topology::Triangles, o));
        CHECK(r.indices.size() == 18 * 3 && r.vertex_count == 4 + 5 * 2 + 2, "%zu %u", r.indices.size(), r.vertex_count);
        for (uint32_t i = 0; i < r.vertex_count; ++i) {
            CHECK(at(r, i).p[2] == 0.0f);
            CHECK(std::fabs(at(r, i).uv[0] - at(r, i).p[0]) < 1e-6f);  // uv = linear
        }
        for (size_t t = 0; t < r.indices.size() / 3; ++t) CHECK(face_z(r, t) > 0, "winding of %zu", t);
        CHECK(std::memcmp(r.vertices.data(), v.data(), 4 * 32) == 0);  // originals first, unchanged

        // The same as a strip (0 1 3 2): same winding.
        Result s;
        CHECK(run(v, {0, 1, 3, 2}, s, Topology::Strip, o));
        for (size_t t = 0; t < s.indices.size() / 3; ++t) CHECK(face_z(s, t) > 0, "strip winding of %zu", t);
        Result q;
        CHECK(run(v, {0, 1, 2, 3}, q, Topology::Quads, o) && q.indices.size() == 18 * 3);
    }

    // An icosahedron, every face with its own vertices (unwelded) and smooth normals (= direction): curved
    // outwards (but not past the sphere), and edge vertices of neighbouring faces on exactly the same points.
    {
        const float g = 1.6180339887f;
        const float P[12][3] = {{-1, g, 0}, {1, g, 0}, {-1, -g, 0}, {1, -g, 0}, {0, -1, g}, {0, 1, g},
                                {0, -1, -g}, {0, 1, -g}, {g, 0, -1}, {g, 0, 1}, {-g, 0, -1}, {-g, 0, 1}};
        const int F[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                              {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                              {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
        const float r = std::sqrt(1 + g * g);
        std::vector<CharVertex> v;
        for (const auto& f : F)
            for (int k : f) v.push_back(make(P[k][0] / r, P[k][1] / r, P[k][2] / r, P[k][0] / r, P[k][1] / r, P[k][2] / r));
        Result r1, flat;
        Options none;
        none.strength = 0;
        CHECK(run(v, {}, r1) && run(v, {}, flat, Topology::Triangles, none));
        CHECK(r1.straight_edges == 0 && r1.curved_edges == 30, "%u %u", r1.straight_edges, r1.curved_edges);
        std::set<std::tuple<float, float, float>> distinct;
        for (uint32_t i = 0; i < r1.vertex_count; ++i) distinct.insert({at(r1, i).p[0], at(r1, i).p[1], at(r1, i).p[2]});
        CHECK(distinct.size() == 12 + 60 + 20, "%zu distinct positions", distinct.size());
        CHECK(r1.vertex_count == flat.vertex_count);
        for (uint32_t i = 60; i < r1.vertex_count; ++i) {
            auto radius = [](const float* p) { return std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]); };
            const float curved = radius(at(r1, i).p), plain = radius(at(flat, i).p);
            CHECK(curved > plain + 0.005f && curved < 1.005f, "vertex %u radius %g (flat %g)", i, curved, plain);
        }
    }

    // Volume keeping: the icosahedron's smoothed vertices sit around the flat ones (mean radius), not outside.
    {
        const float g = 1.6180339887f, r = std::sqrt(1 + g * g);
        const float P[12][3] = {{-1, g, 0}, {1, g, 0}, {-1, -g, 0}, {1, -g, 0}, {0, -1, g}, {0, 1, g},
                                {0, -1, -g}, {0, 1, -g}, {g, 0, -1}, {g, 0, 1}, {-g, 0, -1}, {-g, 0, 1}};
        const int F[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                              {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                              {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
        std::vector<CharVertex> v;
        for (int k = 0; k < 12; ++k) v.push_back(make(P[k][0] / r, P[k][1] / r, P[k][2] / r, P[k][0] / r, P[k][1] / r, P[k][2] / r));
        std::vector<uint16_t> idx;
        for (const auto& f : F)
            for (int k : f) idx.push_back(static_cast<uint16_t>(k));
        Options flat_options, full, kept;
        flat_options.strength = 0, flat_options.keep_volume = 0;
        full.keep_volume = 0;
        Result flat, grown, centred;
        CHECK(run(v, idx, flat, Topology::Triangles, flat_options) && run(v, idx, grown, Topology::Triangles, full) &&
              run(v, idx, centred, Topology::Triangles, kept));
        auto mean_radius = [](const Result& res) {
            double sum = 0;
            for (uint32_t i = 0; i < res.vertex_count; ++i) {
                const float* q = at(res, i).p;
                sum += std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
            }
            return sum / res.vertex_count;
        };
        const double f = mean_radius(flat), gr = mean_radius(grown), c = mean_radius(centred);
        CHECK(gr > f && std::fabs(c - f) < 0.25 * (gr - f), "mean radius flat %g, grown %g, centred %g", f, gr, c);
        CHECK(at(centred, 0).p[0] != v[0].p[0] || at(centred, 0).p[1] != v[0].p[1]);  // corners moved in
    }

    // Outlines: a 12-sided prism (a tire: caps with axial normals, tread with radial ones, split at the rims).
    // The rims are creases; rounded, their new points lie on the circle instead of on the chords.
    {
        const int n = 12;
        const float pi = 3.14159265f;
        std::vector<CharVertex> v;
        std::vector<uint16_t> idx;
        for (int side = 0; side < 2; ++side) {  // caps: centre + ring
            const float z = side ? 0.5f : -0.5f, nz = side ? 1.0f : -1.0f;
            const uint16_t centre = static_cast<uint16_t>(v.size());
            v.push_back(make(0, 0, z, 0, 0, nz));
            for (int k = 0; k < n; ++k)
                v.push_back(make(std::cos(2 * pi * k / n), std::sin(2 * pi * k / n), z, 0, 0, nz));
            for (int k = 0; k < n; ++k)
                idx.insert(idx.end(), {centre, uint16_t(centre + 1 + k), uint16_t(centre + 1 + (k + 1) % n)});
        }
        const uint16_t tread = static_cast<uint16_t>(v.size());
        for (int k = 0; k < n; ++k)
            for (int side = 0; side < 2; ++side) {
                const float c = std::cos(2 * pi * k / n), s = std::sin(2 * pi * k / n);
                v.push_back(make(c, s, side ? 0.5f : -0.5f, c, s, 0));
            }
        for (int k = 0; k < n; ++k) {
            const uint16_t a = uint16_t(tread + 2 * k), b = uint16_t(tread + 2 * ((k + 1) % n));
            idx.insert(idx.end(), {a, b, uint16_t(b + 1), a, uint16_t(b + 1), uint16_t(a + 1)});
        }
        Options round, square;
        round.keep_volume = 0;
        square.keep_volume = 0, square.round_outlines = false;
        Result rr, sq;
        CHECK(run(v, idx, rr, Topology::Triangles, round) && run(v, idx, sq, Topology::Triangles, square));
        CHECK(rr.rounded_edges == 2 * n, "%u rounded edges", rr.rounded_edges);
        int rim_points = 0;
        for (uint32_t i = static_cast<uint32_t>(v.size()); i < rr.vertex_count; ++i) {
            const float* q = at(rr, i).p;
            const float radius = std::sqrt(q[0] * q[0] + q[1] * q[1]);
            if (std::fabs(std::fabs(q[2]) - 0.5f) > 1e-6f || radius < 0.9f) continue;
            ++rim_points;
            CHECK(radius > 0.995f && radius < 1.005f, "rim point %u radius %g", i, radius);
            const float* chord = at(sq, i).p;
            CHECK(std::sqrt(chord[0] * chord[0] + chord[1] * chord[1]) < 0.98f, "unrounded rim point %u", i);
        }
        CHECK(rim_points == 2 * n * 2 * 2, "%d rim points", rim_points);  // 2 rims x n edges x 2 points, cap + tread side
    }

    // The bumper case: a thin slab (4 x 4 x 0.1) whose corner normals lean 25 degrees from its big faces (an
    // artist's averaging). With the model's normals its big faces bulge; with the faces' normals it stays thin.
    {
        std::vector<CharVertex> v;
        const float lean = std::tan(25.0f * 3.14159265f / 180.0f) / std::sqrt(2.0f);
        for (int k = 0; k < 8; ++k) {
            const float x = k & 1 ? 2.0f : -2.0f, y = k & 2 ? 2.0f : -2.0f, z = k & 4 ? 0.05f : -0.05f;
            v.push_back(make(x, y, z, x > 0 ? lean : -lean, y > 0 ? lean : -lean, z > 0 ? 1.0f : -1.0f));
        }
        const std::vector<uint16_t> idx = {0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4,
                                           2, 6, 7, 2, 7, 3, 0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5};
        Options model_normals;
        model_normals.geometry_normals = false;
        Result faces, model;
        CHECK(run(v, idx, faces) && run(v, idx, model, Topology::Triangles, model_normals));
        auto thickest = [](const Result& res) {
            float z = 0;
            for (uint32_t i = 0; i < res.vertex_count; ++i) z = std::max(z, std::fabs(at(res, i).p[2]));
            return z;
        };
        CHECK(thickest(model) > 0.1f, "model normals: the slab should bulge (%g)", thickest(model));
        CHECK(thickest(faces) < 0.06f, "face normals: the slab grew to %g", thickest(faces));
    }

    // The rifle case: a box with averaged (corner) normals, welded. Without the guards it balloons; it must
    // stay a box.
    {
        std::vector<CharVertex> v;
        for (int k = 0; k < 8; ++k) {
            const float x = k & 1 ? 1.0f : -1.0f, y = k & 2 ? 1.0f : -1.0f, z = k & 4 ? 1.0f : -1.0f;
            const float n = 1.0f / std::sqrt(3.0f);
            v.push_back(make(x, y, z, x * n, y * n, z * n));
        }
        const std::vector<uint16_t> idx = {0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4,
                                           2, 6, 7, 2, 7, 3, 0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5};
        Result r;
        CHECK(run(v, idx, r));
        CHECK(r.curved_edges == 0, "%u curved edges on a box", r.curved_edges);
        for (uint32_t i = 0; i < r.vertex_count; ++i) {
            const float* p = at(r, i).p;
            const float m = std::max({std::fabs(p[0]), std::fabs(p[1]), std::fabs(p[2])});
            CHECK(std::fabs(m - 1.0f) < 1e-5f, "box vertex %u off the box (%g)", i, m);
        }
    }

    // A cube with split (flat) normals: every edge a crease, the result stays on the faces.
    {
        std::vector<CharVertex> v;
        std::vector<uint16_t> idx;
        for (int axis = 0; axis < 3; ++axis)
            for (int sign = -1; sign <= 1; sign += 2) {
                const uint16_t base = static_cast<uint16_t>(v.size());
                const int u = (axis + 1) % 3, w = (axis + 2) % 3;
                for (int k = 0; k < 4; ++k) {
                    float p[3], n[3] = {0, 0, 0};
                    p[axis] = static_cast<float>(sign);
                    p[u] = (k == 1 || k == 2) ? 1.0f : -1.0f;
                    p[w] = (k >= 2) ? 1.0f : -1.0f;
                    n[axis] = static_cast<float>(sign);
                    v.push_back(make(p[0], p[1], p[2], n[0], n[1], n[2]));
                }
                idx.insert(idx.end(), {base, uint16_t(base + 1), uint16_t(base + 2), base, uint16_t(base + 2),
                                       uint16_t(base + 3)});
            }
        Result r;
        CHECK(run(v, idx, r));
        CHECK(r.curved_edges == 6, "face diagonals only: %u", r.curved_edges);  // flat normals: no bulge either
        for (uint32_t i = 0; i < r.vertex_count; ++i) {
            const float* p = at(r, i).p;
            const float m = std::max({std::fabs(p[0]), std::fabs(p[1]), std::fabs(p[2])});
            CHECK(std::fabs(m - 1.0f) < 1e-5f, "vertex %u off the cube (%g)", i, m);
        }
    }

    // Bones: merged, four strongest, weights summing to 255.
    {
        std::vector<CharVertex> v = {make(0, 0, 0, 0, 0, 1), make(1, 0, 0, 0, 0, 1), make(0, 1, 0, 0, 0, 1)};
        v[0].bones[0] = 3, v[0].weights[0] = 255;
        v[1].bones[0] = 6, v[1].bones[1] = 3, v[1].weights[0] = 200, v[1].weights[1] = 55;
        v[2].bones[0] = 9, v[2].weights[0] = 255;
        Options o;
        o.smooth_open_edges = true;
        Result r;
        CHECK(run(v, {}, r, Topology::Triangles, o));
        for (uint32_t i = 3; i < r.vertex_count; ++i) {
            const CharVertex& c = at(r, i);
            int sum = 0;
            std::set<int> bones;
            for (int s = 0; s < 4; ++s) {
                sum += c.weights[s];
                if (c.weights[s]) bones.insert(c.bones[s]);
            }
            CHECK(sum == 255, "vertex %u weights sum %d", i, sum);
            for (int b : bones) CHECK(b == 3 || b == 6 || b == 9, "bone %d", b);
        }
    }

    // Refusals: no normal; an index past the vertices.
    {
        const Attribute only_position[] = {{0, Format::Float3, Role::Position}};
        std::vector<CharVertex> v = {make(0, 0, 0, 0, 0, 1), make(1, 0, 0, 0, 0, 1), make(0, 1, 0, 0, 0, 1)};
        Result r;
        CHECK(!smooth({32, only_position, 1}, reinterpret_cast<const uint8_t*>(v.data()), 3, Topology::Triangles,
                      nullptr, 3, {}, r));
        CHECK(!run(v, {0, 1, 7}, r));
    }

    if (test::g_failures) return 1;
    std::printf("mesh_smooth_test: ok\n");
    return 0;
}
