// The course's lamps from its meshes (course_lamps.h, PLAN.md F2b).
#include "course_lamps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>

namespace gt4::render {

namespace {

struct Quad {
    float centre[3] = {};
    float diagonal = 0.0f;
};

float distance(const float* a, const float* b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The quad (or lone triangle) over these distinct vertices: their mean, and the largest distance between two of them.
Quad quadOf(const scene::Mesh& mesh, const uint32_t* v, int n) {
    Quad q;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k) q.centre[k] += mesh.vertices[v[i]].position[k] / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) q.diagonal = std::max(q.diagonal, distance(mesh.vertices[v[i]].position, mesh.vertices[v[j]].position));
    return q;
}

}  // namespace

std::vector<CourseLamp> extractLamps(const scene::Mesh& mesh, uint32_t item, float merge, LampStats* stats) {
    LampStats s;
    std::vector<CourseLamp> lamps;
    if (item >= mesh.items.size()) {
        if (stats) *stats = s;
        return lamps;
    }
    const scene::MeshItem& it = mesh.items[item];
    const uint32_t first = it.firstIndex, end = std::min<uint32_t>(it.firstIndex + it.indexCount, static_cast<uint32_t>(mesh.indices.size()));
    auto P = [&](uint32_t v) { return mesh.vertices[v].position; };
    // The item's triangles, without the degenerate ones (a strip's restart or a repeated vertex: two equal indices, or
    // no area), which would pair along a non-edge and pull a centre.
    std::vector<std::array<uint32_t, 3>> tris;
    for (uint32_t i = first; i + 3 <= end; i += 3) {
        const std::array<uint32_t, 3> t = {mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]};
        ++s.triangles;
        bool degenerate = t[0] == t[1] || t[1] == t[2] || t[0] == t[2];
        if (!degenerate) {
            const float* a = P(t[0]);
            const float* b = P(t[1]);
            const float* c = P(t[2]);
            const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float n[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
            const float area2 = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            const float scale = std::max(distance(a, b), std::max(distance(b, c), distance(a, c)));
            degenerate = !(area2 > 1e-6f * scale * scale);
        }
        if (degenerate) ++s.degenerate;
        else tris.push_back(t);
    }
    // Edges (low, high) -> the triangles that have them (the mesh is welded: a shared edge is two shared indices).
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> edges;
    for (uint32_t t = 0; t < tris.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            uint32_t a = tris[t][k], b = tris[t][(k + 1) % 3];
            if (a > b) std::swap(a, b);
            edges[{a, b}].push_back(t);
        }
    // Two triangles across edge (a, b), with opposite vertices c and d, make a rectangle when c + d = a + b (a
    // parallelogram) and |c - d| = |a - b| (equal diagonals). A glow quad is one; across the edge two quads of a row
    // share, the triangles make a sheared parallelogram, which this refuses.
    auto rectangle = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        const float* pa = P(a);
        const float* pb = P(b);
        const float* pc = P(c);
        const float* pd = P(d);
        const float edge = distance(pa, pb), tolerance = 0.01f * std::max(edge, distance(pc, pd));
        float off = 0.0f;
        for (int k = 0; k < 3; ++k) off = std::max(off, std::fabs(pc[k] + pd[k] - pa[k] - pb[k]));
        return off <= tolerance && std::fabs(distance(pc, pd) - edge) <= tolerance;
    };
    auto opposite = [&](uint32_t t, uint32_t a, uint32_t b) {
        for (uint32_t v : tris[t])
            if (v != a && v != b) return v;
        return a;
    };
    std::vector<int32_t> mate(tris.size(), -1);
    std::vector<uint8_t> skew(tris.size(), 0);
    // First the pairs that make rectangles, then (a mesh of other shapes) any unpaired neighbour, counted as skew.
    for (int pass = 0; pass < 2; ++pass)
        for (uint32_t t = 0; t < tris.size(); ++t)
            for (int k = 0; k < 3 && mate[t] < 0; ++k) {
                uint32_t a = tris[t][k], b = tris[t][(k + 1) % 3];
                if (a > b) std::swap(a, b);
                for (uint32_t u : edges[{a, b}]) {
                    if (u == t || mate[u] >= 0) continue;
                    const bool rect = rectangle(a, b, opposite(t, a, b), opposite(u, a, b));
                    if (pass == 0 && !rect) continue;
                    mate[t] = static_cast<int32_t>(u);
                    mate[u] = static_cast<int32_t>(t);
                    skew[t] = skew[u] = rect ? 0 : 1;
                    break;
                }
            }
    std::vector<Quad> quads;
    for (uint32_t t = 0; t < tris.size(); ++t) {
        if (mate[t] >= 0 && static_cast<uint32_t>(mate[t]) < t) continue;   // (the pair is made at its first triangle)
        uint32_t v[6];
        int n = 0;
        auto add = [&](uint32_t x) {
            if (std::find(v, v + n, x) == v + n) v[n++] = x;
        };
        for (uint32_t x : tris[t]) add(x);
        if (mate[t] >= 0) {
            for (uint32_t x : tris[static_cast<uint32_t>(mate[t])]) add(x);
            if (skew[t]) ++s.skewQuads;
        } else {
            ++s.loneTriangles;
        }
        quads.push_back(quadOf(mesh, v, n));
    }
    s.quads = static_cast<uint32_t>(quads.size());
    // Merge quads whose centres coincide (union-find over the pairs within reach). O(quads^2) per item: about 116 quads
    // on Route 5's lamp item, once per mesh.
    std::vector<uint32_t> parent(quads.size());
    std::iota(parent.begin(), parent.end(), 0u);
    auto root = [&](uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (size_t i = 0; i < quads.size(); ++i)
        for (size_t j = i + 1; j < quads.size(); ++j)
            if (distance(quads[i].centre, quads[j].centre) <= merge * std::min(quads[i].diagonal, quads[j].diagonal))
                parent[root(static_cast<uint32_t>(i))] = root(static_cast<uint32_t>(j));
    std::map<uint32_t, size_t> lampOf;
    for (size_t i = 0; i < quads.size(); ++i) {
        const uint32_t r = root(static_cast<uint32_t>(i));
        auto [at, fresh] = lampOf.emplace(r, lamps.size());
        if (fresh) lamps.emplace_back();
        CourseLamp& l = lamps[at->second];
        for (int k = 0; k < 3; ++k) l.position[k] += quads[i].centre[k];
        l.radius = std::max(l.radius, 0.5f * quads[i].diagonal);
        ++l.quads;
    }
    for (CourseLamp& l : lamps)
        for (float& p : l.position) p /= static_cast<float>(l.quads);
    s.lamps = static_cast<uint32_t>(lamps.size());
    if (stats) *stats = s;
    return lamps;
}

}  // namespace gt4::render
