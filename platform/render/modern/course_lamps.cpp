// The course's lamps from its meshes (course_lamps.h, PLAN.md F2b).
#include "course_lamps.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>

namespace gt4::render {

namespace {

struct Quad {
    float centre[3] = {};
    float size = 0.0f;
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
        for (int j = i + 1; j < n; ++j) q.size = std::max(q.size, distance(mesh.vertices[v[i]].position, mesh.vertices[v[j]].position));
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
    const uint32_t triangles = end > first ? (end - first) / 3 : 0;
    s.triangles = triangles;
    // Pair triangles into quads: two triangles sharing an edge (the mesh is welded, so a shared edge is two shared
    // indices). Each triangle pairs once, with the first unpaired neighbour across any of its edges.
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> openEdge;   // edge (low, high) -> the unpaired triangle that has it
    std::vector<int32_t> mate(triangles, -1);
    auto index = [&](uint32_t t, int k) { return mesh.indices[first + 3 * t + k]; };
    for (uint32_t t = 0; t < triangles; ++t) {
        for (int k = 0; k < 3 && mate[t] < 0; ++k) {
            uint32_t a = index(t, k), b = index(t, (k + 1) % 3);
            if (a > b) std::swap(a, b);
            auto e = openEdge.find({a, b});
            if (e != openEdge.end() && mate[e->second] < 0 && e->second != t) {
                mate[t] = static_cast<int32_t>(e->second);
                mate[e->second] = static_cast<int32_t>(t);
            }
        }
        if (mate[t] < 0)
            for (int k = 0; k < 3; ++k) {
                uint32_t a = index(t, k), b = index(t, (k + 1) % 3);
                if (a > b) std::swap(a, b);
                openEdge.emplace(std::make_pair(a, b), t);
            }
    }
    std::vector<Quad> quads;
    for (uint32_t t = 0; t < triangles; ++t) {
        if (mate[t] >= 0 && static_cast<uint32_t>(mate[t]) < t) continue;   // (the pair is made at its first triangle)
        uint32_t v[6];
        int n = 0;
        auto add = [&](uint32_t x) {
            if (std::find(v, v + n, x) == v + n) v[n++] = x;
        };
        for (int k = 0; k < 3; ++k) add(index(t, k));
        if (mate[t] >= 0)
            for (int k = 0; k < 3; ++k) add(index(static_cast<uint32_t>(mate[t]), k));
        else
            ++s.loneTriangles;
        quads.push_back(quadOf(mesh, v, n));
    }
    s.quads = static_cast<uint32_t>(quads.size());
    // Merge quads whose centres coincide (union-find over the pairs within reach).
    std::vector<uint32_t> parent(quads.size());
    std::iota(parent.begin(), parent.end(), 0u);
    auto root = [&](uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (size_t i = 0; i < quads.size(); ++i)
        for (size_t j = i + 1; j < quads.size(); ++j)
            if (distance(quads[i].centre, quads[j].centre) <= merge * std::min(quads[i].size, quads[j].size))
                parent[root(static_cast<uint32_t>(i))] = root(static_cast<uint32_t>(j));
    std::map<uint32_t, size_t> lampOf;
    for (size_t i = 0; i < quads.size(); ++i) {
        const uint32_t r = root(static_cast<uint32_t>(i));
        auto [at, fresh] = lampOf.emplace(r, lamps.size());
        if (fresh) lamps.emplace_back();
        CourseLamp& l = lamps[at->second];
        for (int k = 0; k < 3; ++k) l.position[k] += quads[i].centre[k];
        l.size = std::max(l.size, quads[i].size);
        ++l.quads;
    }
    for (CourseLamp& l : lamps)
        for (float& p : l.position) p /= static_cast<float>(l.quads);
    s.lamps = static_cast<uint32_t>(lamps.size());
    if (stats) *stats = s;
    return lamps;
}

}  // namespace gt4::render
