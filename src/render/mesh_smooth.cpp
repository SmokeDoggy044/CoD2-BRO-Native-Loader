#include "mesh_smooth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace render::smooth {

namespace {

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalized(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-12f ? a * (1.0f / l) : V3{0, 0, 1};
}

int32_t sign_extend(uint32_t v, int bits) {
    const uint32_t m = 1u << (bits - 1);
    v &= (1u << bits) - 1;
    return static_cast<int32_t>(v ^ m) - static_cast<int32_t>(m);
}

int component_count(Format f) {
    switch (f) {
        case Format::Float1: return 1;
        case Format::Float2: case Format::Short2: case Format::Short2N: return 2;
        case Format::Float3: case Format::PackedNormal: return 3;
        default: return 4;
    }
}

void read(const uint8_t* p, Format f, float out[4]) {
    switch (f) {
        case Format::Float1: case Format::Float2: case Format::Float3: case Format::Float4:
            std::memcpy(out, p, 4 * component_count(f));
            break;
        case Format::Color: case Format::UByte4N:
            for (int i = 0; i < 4; ++i) out[i] = p[i];
            break;
        case Format::Short2: case Format::Short4: case Format::Short2N: case Format::Short4N:
            for (int i = 0; i < component_count(f); ++i) {
                int16_t s;
                std::memcpy(&s, p + 2 * i, 2);
                out[i] = s;
            }
            break;
        case Format::PackedNormal: {
            uint32_t w;
            std::memcpy(&w, p, 4);
            unpack_normal(w, out);
            break;
        }
    }
}

void write(uint8_t* p, Format f, const float in[4]) {
    switch (f) {
        case Format::Float1: case Format::Float2: case Format::Float3: case Format::Float4:
            std::memcpy(p, in, 4 * component_count(f));
            break;
        case Format::Color: case Format::UByte4N:
            for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(std::clamp(std::lround(in[i]), 0l, 255l));
            break;
        case Format::Short2: case Format::Short4: case Format::Short2N: case Format::Short4N:
            for (int i = 0; i < component_count(f); ++i) {
                const int16_t s = static_cast<int16_t>(std::clamp(std::lround(in[i]), -32768l, 32767l));
                std::memcpy(p + 2 * i, &s, 2);
            }
            break;
        case Format::PackedNormal: {
            const V3 n = normalized({in[0], in[1], in[2]});
            const float v[3] = {n.x, n.y, n.z};
            const uint32_t w = pack_normal(v);
            std::memcpy(p, &w, 4);
            break;
        }
    }
}

struct EdgeKey {
    uint32_t a, b;  // position ids, a < b
    bool operator==(const EdgeKey&) const = default;
};
struct EdgeHash {
    size_t operator()(const EdgeKey& k) const { return (uint64_t{k.a} << 32 | k.b) * 0x9E3779B97F4A7C15ull >> 16; }
};

struct Edge {
    uint32_t triangles = 0;
    V3 normal_a, normal_b;     // the first triangle's normals at a and b
    V3 face;                   // the first triangle's face normal
    V3 sum_c1, sum_c2;         // the PN control points (near a, near b), summed over the triangles
    bool straight = false;     // no PN curvature (normals interpolated linearly)
    bool feature = false;      // straight because the surface folds or creases here (an outline)
    bool rounded = false;      // a feature edge curved along its outline (round_outlines)
    V3 c1, c2;                 // final (after averaging)
};

struct Smoother {
    const Layout& layout;
    const uint8_t* source;
    uint32_t vertex_count;
    const Options& options;
    Result& result;

    const Attribute* position = nullptr;
    const Attribute* normal = nullptr;
    const Attribute* bone_indices = nullptr;
    const Attribute* bone_weights = nullptr;

    std::vector<V3> positions, normals;
    std::vector<V3> shape_normals;  // what the curvature follows: from the faces (geometry_normals) or `normals`
    std::vector<uint8_t> normal_ok;
    std::vector<uint32_t> position_id;
    std::vector<std::array<uint32_t, 3>> triangles;
    std::unordered_map<EdgeKey, Edge, EdgeHash> edges;
    std::vector<V3> shift;  // per position id: where volume keeping moves it
    std::unordered_map<uint64_t, uint16_t> edge_vertices;  // (low vertex, high vertex, step from low)

    const uint8_t* vertex(uint32_t i) const { return source + size_t{i} * layout.stride; }

    bool find_roles() {
        for (uint32_t i = 0; i < layout.count; ++i) {
            const Attribute& a = layout.attributes[i];
            const bool bytes4 = a.format == Format::UByte4N || a.format == Format::Color;
            switch (a.role) {
                case Role::Position: if (a.format != Format::Float3) return false; position = &a; break;
                case Role::Normal:
                    if (a.format != Format::Float3 && a.format != Format::PackedNormal) return false;
                    normal = &a;
                    break;
                case Role::BlendIndices: if (!bytes4) return false; bone_indices = &a; break;
                case Role::BlendWeights: if (!bytes4) return false; bone_weights = &a; break;
                case Role::Other: break;
            }
        }
        if ((bone_indices == nullptr) != (bone_weights == nullptr)) return false;
        return position && normal;
    }

    bool collect_triangles(Topology topology, const uint16_t* indices, uint32_t count) {
        auto index = [&](uint32_t i) -> uint32_t { return indices ? indices[i] : i; };
        for (uint32_t i = 0; i < count; ++i)
            if (index(i) >= vertex_count) return false;
        auto add = [&](uint32_t a, uint32_t b, uint32_t c) {
            if (a == b || b == c || a == c) return;
            if (position_id[a] == position_id[b] || position_id[b] == position_id[c] || position_id[a] == position_id[c])
                return;
            triangles.push_back({a, b, c});
        };
        switch (topology) {
            case Topology::Triangles:
                for (uint32_t i = 0; i + 3 <= count; i += 3) add(index(i), index(i + 1), index(i + 2));
                break;
            case Topology::Strip:
                for (uint32_t i = 0; i + 3 <= count; ++i) {
                    if (i & 1) add(index(i + 1), index(i), index(i + 2));
                    else add(index(i), index(i + 1), index(i + 2));
                }
                break;
            case Topology::Quads:
                for (uint32_t i = 0; i + 4 <= count; i += 4) {
                    add(index(i), index(i + 1), index(i + 2));
                    add(index(i), index(i + 2), index(i + 3));
                }
                break;
        }
        return !triangles.empty();
    }

    void read_vertices() {
        positions.resize(vertex_count);
        normals.resize(vertex_count);
        normal_ok.resize(vertex_count);
        position_id.resize(vertex_count);
        std::unordered_map<uint64_t, std::vector<std::pair<std::array<uint32_t, 3>, uint32_t>>> ids;
        uint32_t next = 0;
        for (uint32_t i = 0; i < vertex_count; ++i) {
            float p[4], n[4];
            read(vertex(i) + position->offset, position->format, p);
            read(vertex(i) + normal->offset, normal->format, n);
            positions[i] = {p[0], p[1], p[2]};
            const V3 nv{n[0], n[1], n[2]};
            normal_ok[i] = dot(nv, nv) > 1e-6f;
            normals[i] = normalized(nv);
            std::array<uint32_t, 3> bits;
            for (int c = 0; c < 3; ++c) {
                const float f = p[c] == 0.0f ? 0.0f : p[c];  // -0 as 0
                std::memcpy(&bits[c], &f, 4);
            }
            auto& bucket = ids[(uint64_t{bits[0]} * 0x9E3779B1u) ^ (uint64_t{bits[1]} << 21) ^ (uint64_t{bits[2]} << 42)];
            uint32_t id = UINT32_MAX;
            for (const auto& [b, existing] : bucket)
                if (b == bits) id = existing;
            if (id == UINT32_MAX) bucket.push_back({bits, id = next++});
            position_id[i] = id;
        }
    }

    // A model's normals can lean where its shape does not (an artist's averaged normals on a flat slab: the
    // bumper in front of a licence plate). With geometry_normals the curvature follows the faces instead: per
    // position and smoothing group (the vertices there with the same normal), the area-weighted normal of the
    // faces around it (each turned to the vertex normal's side: two-sided materials). Flat stays flat.
    void find_shape_normals() {
        shape_normals = normals;
        if (!options.geometry_normals) return;
        struct Group {
            V3 sum;
        };
        std::unordered_map<uint64_t, std::vector<std::pair<std::array<uint32_t, 3>, Group>>> groups;
        auto group_of = [&](uint32_t v) -> Group& {
            std::array<uint32_t, 3> bits;
            std::memcpy(bits.data(), &normals[v], 12);
            auto& bucket = groups[uint64_t{position_id[v]} * 0x9E3779B97F4A7C15ull ^ bits[0] ^ uint64_t{bits[1]} << 32];
            for (auto& [b, g] : bucket)
                if (b == bits) return g;
            bucket.push_back({bits, Group{}});
            return bucket.back().second;
        };
        for (const auto& t : triangles) {
            const V3 e1 = positions[t[1]] - positions[t[0]], e2 = positions[t[2]] - positions[t[0]];
            const V3 cross{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            for (const uint32_t v : t) {
                if (!normal_ok[v]) continue;
                Group& g = group_of(v);
                g.sum = g.sum + (dot(cross, normals[v]) < 0 ? cross * -1.0f : cross);
            }
        }
        for (uint32_t v = 0; v < vertex_count; ++v) {
            if (!normal_ok[v]) continue;
            const V3 sum = group_of(v).sum;
            if (dot(sum, sum) > 1e-24f) shape_normals[v] = normalized(sum);
        }
    }

    // The edge from vertex u to vertex v, in its canonical direction (lower position id first).
    Edge& edge_of(uint32_t u, uint32_t v, bool& flipped) {
        flipped = position_id[u] > position_id[v];
        return edges[flipped ? EdgeKey{position_id[v], position_id[u]} : EdgeKey{position_id[u], position_id[v]}];
    }

    static V3 control(V3 a, V3 b, V3 na) { return (a * 2.0f + b - na * dot(b - a, na)) * (1.0f / 3.0f); }

    void build_edges() {
        for (const auto& t : triangles) {
            const V3 e1 = positions[t[1]] - positions[t[0]], e2 = positions[t[2]] - positions[t[0]];
            const V3 cross{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            const bool has_face = dot(cross, cross) > 1e-20f;
            const V3 face = normalized(cross);
            for (int e = 0; e < 3; ++e) {
                uint32_t u = t[e], v = t[(e + 1) % 3];
                bool flipped;
                Edge& edge = edge_of(u, v, flipped);
                if (flipped) std::swap(u, v);  // u = the canonical a
                if (!normal_ok[u] || !normal_ok[v]) edge.straight = true;
                // Either winding (two-sided materials need not match their normals).
                if (has_face && (std::fabs(dot(normals[u], face)) < options.lean_cos ||
                                 std::fabs(dot(normals[v], face)) < options.lean_cos))
                    edge.straight = true;
                if (edge.triangles == 0) {
                    edge.normal_a = normals[u], edge.normal_b = normals[v];
                    edge.face = has_face ? face : V3{};
                } else if (dot(edge.normal_a, normals[u]) < options.crease_cos ||
                           dot(edge.normal_b, normals[v]) < options.crease_cos) {
                    edge.straight = edge.feature = true;
                } else if (edge.triangles == 1 && has_face && dot(edge.face, edge.face) > 0 &&
                           std::fabs(dot(edge.face, face)) < options.fold_cos) {
                    edge.straight = edge.feature = true;
                }
                ++edge.triangles;
                edge.sum_c1 = edge.sum_c1 + control(positions[u], positions[v], shape_normals[u]);
                edge.sum_c2 = edge.sum_c2 + control(positions[v], positions[u], shape_normals[v]);
            }
        }
        // The canonical end positions: the first vertex found with each id.
        std::vector<uint32_t> any_vertex(vertex_count, UINT32_MAX);
        for (uint32_t i = 0; i < vertex_count; ++i)
            if (any_vertex[position_id[i]] == UINT32_MAX) any_vertex[position_id[i]] = i;
        auto at = [&](uint32_t id) { return positions[any_vertex[id]]; };
        // Open borders and non-manifold edges: straight, and their ends stay where they are (pinned).
        std::vector<uint8_t> pinned(vertex_count, 0);
        for (auto& [key, edge] : edges) {
            if (edge.triangles > 2 || (edge.triangles == 1 && !options.smooth_open_edges)) {
                edge.straight = true;
                edge.feature = false;
                pinned[key.a] = pinned[key.b] = 1;
            }
        }
        // Outlines (B): chains of feature edges. A position on exactly two of them that turn gently there
        // (a tire's rim, a barrel's end) gets a tangent: the direction from one chain neighbour to the other.
        std::vector<std::array<uint32_t, 2>> chain(vertex_count, {UINT32_MAX, UINT32_MAX});
        std::vector<uint8_t> chain_count(vertex_count, 0);
        if (options.round_outlines)
            for (const auto& [key, edge] : edges) {
                if (!edge.feature) continue;
                for (const auto& [from, to] : {std::pair{key.a, key.b}, std::pair{key.b, key.a}}) {
                    if (chain_count[from] < 2) chain[from][chain_count[from]] = to;
                    ++chain_count[from];
                }
            }
        // The tangent at `id` for its chain edge towards `toward` (null: none).
        auto tangent = [&](uint32_t id, uint32_t toward, V3& out) {
            if (chain_count[id] != 2) return false;
            const uint32_t other = chain[id][0] == toward ? chain[id][1] : chain[id][0];
            const V3 in = normalized(at(id) - at(other)), next = normalized(at(toward) - at(id));
            if (dot(in, next) < options.outline_turn_cos) return false;  // a corner, not a round outline
            out = normalized(at(toward) - at(other));
            return true;
        };
        for (auto& [key, edge] : edges) {
            const V3 a = at(key.a), b = at(key.b);
            const V3 line1 = a + (b - a) * (1.0f / 3.0f), line2 = a + (b - a) * (2.0f / 3.0f);
            edge.c1 = line1, edge.c2 = line2;
            if (!edge.straight) {
                const float share = 1.0f / static_cast<float>(edge.triangles);
                edge.c1 = line1 + (edge.sum_c1 * share - line1) * options.strength;
                edge.c2 = line2 + (edge.sum_c2 * share - line2) * options.strength;
                ++result.curved_edges;
                continue;
            }
            ++result.straight_edges;
            if (!edge.feature) continue;
            const float third = std::sqrt(dot(b - a, b - a)) * (1.0f / 3.0f);
            V3 t;
            if (tangent(key.a, key.b, t)) edge.c1 = a + t * third, edge.rounded = true;
            if (tangent(key.b, key.a, t)) edge.c2 = b + t * third, edge.rounded = true;  // t points towards a
            if (edge.rounded) ++result.rounded_edges;
        }
        // Volume (A): the curved surface passes through the original corners, so a convex model only grows.
        // Each position moves back by `keep_volume` of the average bulge (cubic minus straight midpoint) of its
        // curved edges; pinned positions stay. Per position id, so shared points stay shared.
        shift.assign(vertex_count, V3{});
        if (options.keep_volume > 0) {
            std::vector<V3> sum(vertex_count);
            std::vector<uint32_t> count(vertex_count, 0);
            for (const auto& [key, edge] : edges) {
                if (edge.straight && !edge.rounded) continue;
                const V3 a = at(key.a), b = at(key.b);
                const V3 bulge = (a + (edge.c1 + edge.c2) * 3.0f + b) * 0.125f - (a + b) * 0.5f;
                for (const uint32_t id : {key.a, key.b}) sum[id] = sum[id] + bulge, ++count[id];
            }
            for (uint32_t id = 0; id < vertex_count; ++id)
                if (count[id] && !pinned[id])
                    shift[id] = sum[id] * (-options.keep_volume / static_cast<float>(count[id]));
        }
    }

    // The edge's control points seen from u (near u first).
    void controls(uint32_t u, uint32_t v, V3& near_u, V3& near_v, bool& straight) {
        bool flipped;
        const Edge& edge = edge_of(u, v, flipped);
        near_u = flipped ? edge.c2 : edge.c1;
        near_v = flipped ? edge.c1 : edge.c2;
        straight = edge.straight;
    }

    // The PN quadratic normal's edge coefficient (Vlachos: normalize(n0 + n1 - v (p1 - p0))).
    V3 mid_normal(uint32_t u, uint32_t v, bool straight) const {
        const V3 d = positions[v] - positions[u];
        const V3 sum = normals[u] + normals[v];
        if (straight) return normalized(sum);
        return normalized(sum - d * (2.0f * dot(d, sum) / dot(d, d)));
    }

    uint8_t* new_vertex(uint32_t copy_from) {
        const size_t at = result.vertices.size();
        result.vertices.resize(at + layout.stride);
        std::memcpy(result.vertices.data() + at, vertex(copy_from), layout.stride);
        ++result.vertex_count;
        return result.vertices.data() + at;
    }

    // Linear attributes and merged bones for weights w[] of corners c[] (n of them).
    void fill_linear(uint8_t* out, const uint32_t* c, const float* w, int n) {
        for (uint32_t i = 0; i < layout.count; ++i) {
            const Attribute& a = layout.attributes[i];
            if (a.role != Role::Other) continue;
            float sum[4] = {0, 0, 0, 0}, v[4];
            for (int k = 0; k < n; ++k) {
                read(vertex(c[k]) + a.offset, a.format, v);
                for (int j = 0; j < component_count(a.format); ++j) sum[j] += v[j] * w[k];
            }
            write(out + a.offset, a.format, sum);
        }
        if (!bone_indices) return;
        uint8_t bone[12];
        float weight[12];
        int bones = 0;
        for (int k = 0; k < n; ++k) {
            const uint8_t* ids = vertex(c[k]) + bone_indices->offset;
            const uint8_t* ws = vertex(c[k]) + bone_weights->offset;
            for (int s = 0; s < 4; ++s) {
                if (!ws[s]) continue;
                int found = 0;
                while (found < bones && bone[found] != ids[s]) ++found;
                if (found == bones) bone[bones] = ids[s], weight[bones++] = 0;
                weight[found] += w[k] * ws[s];
            }
        }
        // The four strongest (stable order by first appearance among equals).
        int order[12];
        for (int i = 0; i < bones; ++i) order[i] = i;
        std::stable_sort(order, order + bones, [&](int x, int y) { return weight[x] > weight[y]; });
        const int kept = std::min(bones, 4);
        float total = 0;
        for (int i = 0; i < kept; ++i) total += weight[order[i]];
        uint8_t* ids = out + bone_indices->offset;
        uint8_t* ws = out + bone_weights->offset;
        if (kept == 0 || total <= 0) return;  // corner 0's bytes stay
        int bytes[4] = {0, 0, 0, 0}, given = 0;
        for (int i = 0; i < kept; ++i) given += bytes[i] = static_cast<int>(std::lround(weight[order[i]] / total * 255.0f));
        bytes[0] += 255 - given;  // the strongest takes the rounding
        for (int s = 0; s < 4; ++s) {
            ids[s] = s < kept ? bone[order[s]] : bone[order[0]];
            ws[s] = static_cast<uint8_t>(s < kept ? std::clamp(bytes[s], 0, 255) : 0);
        }
    }

    void write_position_normal(uint8_t* out, V3 p, V3 n) {
        const float pv[4] = {p.x, p.y, p.z, 0}, nv[4] = {n.x, n.y, n.z, 0};
        write(out + position->offset, position->format, pv);
        write(out + normal->offset, normal->format, nv);
    }

    // The vertex `step` of `segments` along u -> v (shared by every triangle with this vertex pair).
    uint16_t edge_vertex(uint32_t u, uint32_t v, uint32_t step) {
        const uint32_t segments = options.segments;
        if (u > v) std::swap(u, v), step = segments - step;
        const uint64_t key = (uint64_t{u} << 40) | (uint64_t{v} << 8) | step;
        if (auto it = edge_vertices.find(key); it != edge_vertices.end()) return it->second;
        // Position: the cubic in the edge's canonical direction (a = lower position id).
        bool flipped;
        const Edge& edge = edge_of(u, v, flipped);
        const uint32_t a = flipped ? v : u, b = flipped ? u : v;
        const uint32_t from_a = flipped ? segments - step : step;
        const float t = static_cast<float>(from_a) / static_cast<float>(segments);
        const float s = static_cast<float>(segments - from_a) / static_cast<float>(segments);
        const V3 p = positions[a] * (s * s * s) + edge.c1 * (3 * s * s * t) + edge.c2 * (3 * s * t * t) +
                     positions[b] * (t * t * t) + shift[position_id[a]] * s + shift[position_id[b]] * t;
        // Normal and the rest from u's side.
        const float tu = static_cast<float>(step) / static_cast<float>(segments);
        const float su = static_cast<float>(segments - step) / static_cast<float>(segments);
        const V3 n = normalized(normals[u] * (su * su) + mid_normal(u, v, edge.straight) * (su * tu) +
                                normals[v] * (tu * tu));
        uint8_t* out = new_vertex(su >= tu ? u : v);
        const uint32_t c[2] = {u, v};
        const float w[2] = {su, tu};
        fill_linear(out, c, w, 2);
        write_position_normal(out, p, n);
        const uint16_t index = static_cast<uint16_t>(result.vertex_count - 1);
        edge_vertices.emplace(key, index);
        return index;
    }

    uint16_t interior_vertex(const std::array<uint32_t, 3>& t, const V3 (&b)[10], const V3 (&mid)[3], uint32_t i,
                             uint32_t j) {
        const float L = static_cast<float>(options.segments);
        const float w1 = static_cast<float>(i) / L, w2 = static_cast<float>(j) / L,
                    w0 = static_cast<float>(options.segments - i - j) / L;
        // b: 300 (corner 0), 030 (1), 003 (2), 210, 120, 021, 012, 102, 201, 111.
        const V3 p = b[0] * (w0 * w0 * w0) + b[1] * (w1 * w1 * w1) + b[2] * (w2 * w2 * w2) +
                     b[3] * (3 * w0 * w0 * w1) + b[4] * (3 * w0 * w1 * w1) + b[5] * (3 * w1 * w1 * w2) +
                     b[6] * (3 * w1 * w2 * w2) + b[7] * (3 * w0 * w2 * w2) + b[8] * (3 * w0 * w0 * w2) +
                     b[9] * (6 * w0 * w1 * w2) + shift[position_id[t[0]]] * w0 + shift[position_id[t[1]]] * w1 +
                     shift[position_id[t[2]]] * w2;
        const V3 n = normalized(normals[t[0]] * (w0 * w0) + normals[t[1]] * (w1 * w1) + normals[t[2]] * (w2 * w2) +
                                mid[0] * (w0 * w1) + mid[1] * (w1 * w2) + mid[2] * (w2 * w0));
        const uint32_t nearest = w0 >= w1 && w0 >= w2 ? t[0] : w1 >= w2 ? t[1] : t[2];
        uint8_t* out = new_vertex(nearest);
        const float w[3] = {w0, w1, w2};
        fill_linear(out, t.data(), w, 3);
        write_position_normal(out, p, n);
        return static_cast<uint16_t>(result.vertex_count - 1);
    }

    bool run(Topology topology, const uint16_t* indices, uint32_t index_count) {
        if (!find_roles() || options.segments < 1 || options.segments > 8) return false;
        read_vertices();
        if (!collect_triangles(topology, indices, index_count)) return false;
        const uint32_t L = options.segments;
        // Vertices needed (an upper bound: no sharing assumed for edges).
        const uint64_t estimate = uint64_t{vertex_count} + uint64_t{triangles.size()} * (3 * (L - 1) + (L - 1) * (L - 2) / 2);
        if (L > 1 && estimate > options.max_vertices) {
            // Count shared edges exactly before giving up.
            std::unordered_map<uint64_t, int> pairs;
            for (const auto& t : triangles)
                for (int e = 0; e < 3; ++e) {
                    const uint32_t u = std::min(t[e], t[(e + 1) % 3]), v = std::max(t[e], t[(e + 1) % 3]);
                    pairs[uint64_t{u} << 32 | v] = 1;
                }
            const uint64_t exact =
                uint64_t{vertex_count} + pairs.size() * (L - 1) + uint64_t{triangles.size()} * ((L - 1) * (L - 2) / 2);
            if (exact > options.max_vertices) return false;
        }
        find_shape_normals();
        build_edges();
        result.triangles_in = static_cast<uint32_t>(triangles.size());
        result.vertices.assign(source, source + size_t{vertex_count} * layout.stride);
        result.vertex_count = vertex_count;
        for (uint32_t i = 0; i < vertex_count; ++i) {
            const V3 d = shift[position_id[i]];
            if (d.x == 0 && d.y == 0 && d.z == 0) continue;
            const V3 p = positions[i] + d;
            const float pv[4] = {p.x, p.y, p.z, 0};
            write(result.vertices.data() + size_t{i} * layout.stride + position->offset, position->format, pv);
        }
        result.indices.clear();
        result.indices.reserve(triangles.size() * 3 * L * L);
        std::vector<uint16_t> grid((L + 1) * (L + 2) / 2);
        auto at = [L](uint32_t i, uint32_t j) { return j * (L + 1) - j * (j - 1) / 2 + i; };  // i + j <= L
        for (const auto& t : triangles) {
            V3 b[10];
            V3 mid[3];
            bool straight[3];
            b[0] = positions[t[0]], b[1] = positions[t[1]], b[2] = positions[t[2]];
            controls(t[0], t[1], b[3], b[4], straight[0]);  // 210, 120
            controls(t[1], t[2], b[5], b[6], straight[1]);  // 021, 012
            controls(t[2], t[0], b[7], b[8], straight[2]);  // 102, 201
            const V3 e = (b[3] + b[4] + b[5] + b[6] + b[7] + b[8]) * (1.0f / 6.0f);
            const V3 v = (b[0] + b[1] + b[2]) * (1.0f / 3.0f);
            b[9] = e + (e - v) * 0.5f;
            mid[0] = mid_normal(t[0], t[1], straight[0]);
            mid[1] = mid_normal(t[1], t[2], straight[1]);
            mid[2] = mid_normal(t[2], t[0], straight[2]);
            // Grid point (i, j): weight i/L on corner 1, j/L on corner 2.
            for (uint32_t j = 0; j <= L; ++j)
                for (uint32_t i = 0; i + j <= L; ++i) {
                    uint16_t index;
                    if (i == 0 && j == 0) index = static_cast<uint16_t>(t[0]);
                    else if (i == L) index = static_cast<uint16_t>(t[1]);
                    else if (j == L) index = static_cast<uint16_t>(t[2]);
                    else if (j == 0) index = edge_vertex(t[0], t[1], i);
                    else if (i == 0) index = edge_vertex(t[0], t[2], j);
                    else if (i + j == L) index = edge_vertex(t[1], t[2], j);
                    else index = interior_vertex(t, b, mid, i, j);
                    grid[at(i, j)] = index;
                }
            for (uint32_t j = 0; j < L; ++j)
                for (uint32_t i = 0; i + j < L; ++i) {
                    result.indices.insert(result.indices.end(), {grid[at(i, j)], grid[at(i + 1, j)], grid[at(i, j + 1)]});
                    if (i + j + 1 < L)
                        result.indices.insert(result.indices.end(),
                                              {grid[at(i + 1, j)], grid[at(i + 1, j + 1)], grid[at(i, j + 1)]});
                }
            if (result.vertex_count > options.max_vertices) return false;
        }
        return true;
    }
};

}  // namespace

void unpack_normal(uint32_t packed, float out[3]) {
    out[0] = std::max(static_cast<float>(sign_extend(packed, 11)) / 1023.0f, -1.0f);
    out[1] = std::max(static_cast<float>(sign_extend(packed >> 11, 11)) / 1023.0f, -1.0f);
    out[2] = std::max(static_cast<float>(sign_extend(packed >> 22, 10)) / 511.0f, -1.0f);
}

uint32_t pack_normal(const float in[3]) {
    auto q = [](float v, float scale, int bits) {
        const long limit = (1l << (bits - 1)) - 1;
        const long n = std::clamp(std::lround(v * scale), -limit, limit);
        return static_cast<uint32_t>(n) & ((1u << bits) - 1);
    };
    return q(in[0], 1023.0f, 11) | q(in[1], 1023.0f, 11) << 11 | q(in[2], 511.0f, 10) << 22;
}

bool smooth(const Layout& layout, const uint8_t* vertices, uint32_t vertex_count, Topology topology,
            const uint16_t* indices, uint32_t index_count, const Options& options, Result& result) {
    result = Result();
    if (!vertices || !vertex_count || vertex_count > options.max_vertices) return false;
    Smoother s{layout, vertices, vertex_count, options, result};
    return s.run(topology, indices, index_count);
}

}  // namespace render::smooth
