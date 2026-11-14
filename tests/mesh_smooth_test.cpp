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

    // An octahedron, every face with its own vertices (unwelded) and smooth normals (= direction):
    // curved, and edge vertices of neighbouring faces on exactly the same points.
    {
        const float P[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const int F[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
        std::vector<CharVertex> v;
        for (const auto& f : F)
            for (int k : f) v.push_back(make(P[k][0], P[k][1], P[k][2], P[k][0], P[k][1], P[k][2]));
        Result r;
        CHECK(run(v, {}, r));
        CHECK(r.straight_edges == 0 && r.curved_edges == 12, "%u %u", r.straight_edges, r.curved_edges);
        // Distinct positions: 6 corners + 12 edges * 2 + 8 faces * 1.
        std::set<std::tuple<float, float, float>> distinct;
        for (uint32_t i = 0; i < r.vertex_count; ++i) {
            const float* p = at(r, i).p;
            distinct.insert({p[0], p[1], p[2]});
        }
        CHECK(distinct.size() == 6 + 24 + 8, "%zu distinct positions", distinct.size());
        // Flat faces would put the edge thirds at radius ~0.745 and the face centres at ~0.577; curved:
        // further out (a coarse octahedron's PN patches are not spheres: centres ~0.77), inside the sphere.
        for (uint32_t i = 24; i < r.vertex_count; ++i) {
            const float* p = at(r, i).p;
            const float radius = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            CHECK(radius > 0.76f && radius < 1.01f, "vertex %u radius %g", i, radius);
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
