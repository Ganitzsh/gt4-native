// N3's lit-road rule (lit_road.h).
#include "lit_road.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <vector>

namespace gt4::scene {

bool litRoadState(const ItemSource& item) {
    const gfx::Context* c = item.context;
    if (!c || !item.batch.job) return false;
    return ((item.prim >> 4) & 1) && ((item.prim >> 6) & 1) &&                      // textured, alpha-blended
           c->blendA == 0 && c->blendB == 1 && c->blendC == 0 && c->blendD == 1 &&   // (Cs - Cd) As + Cd
           !c->alphaTest && c->depthTest && c->depthFunc == 2 && !c->depthWrite &&   // z GEQUAL, no z write
           (item.batch.job->chain >> 16) == 0;                                        // lighting body 0
}

namespace {

using Vec3 = std::array<double, 3>;

Vec3 toWorld(const float (&p)[3], const float (&m)[4][4]) {
    Vec3 w;
    for (int q = 0; q < 3; ++q) w[q] = p[0] * m[0][q] + p[1] * m[1][q] + p[2] * m[2][q] + m[3][q];
    return w;
}

}  // namespace

LitRoadMeasure measureLitRoad(const DecodedBatch& b, const float (&m)[4][4], const TextureSummary* texture) {
    LitRoadMeasure out;
    const size_t nv = b.vertices.size();
    if (!nv) return out;
    // The vertex colour: its mean over the item (R in the low byte).
    double sum[4] = {};
    for (const MeshVertex& v : b.vertices)
        for (int k = 0; k < 4; ++k) sum[k] += double((v.colour >> (8 * k)) & 0xFF);
    out.vertexLuminance = float((0.2126 * sum[0] + 0.7152 * sum[1] + 0.0722 * sum[2]) / double(nv));
    out.vertexAlpha = float(sum[3] / double(nv));
    const bool known = texture && texture->known && texture->width && texture->height;
    if (known) out.centreToRim = texture->centreToRim;

    // The vertices in the world, welded at 1 mm (a strip's pieces share positions, not indices).
    std::vector<Vec3> P(nv);
    std::vector<uint32_t> rep(nv), parent(nv);
    std::map<std::array<long long, 3>, uint32_t> weld;
    for (size_t i = 0; i < nv; ++i) {
        P[i] = toWorld(b.vertices[i].position, m);
        const std::array<long long, 3> key{std::llround(P[i][0] * 1000), std::llround(P[i][1] * 1000), std::llround(P[i][2] * 1000)};
        rep[i] = weld.emplace(key, uint32_t(i)).first->second;
    }
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    struct UpTriangle {
        uint32_t v[3];
        double area;
    };
    std::vector<UpTriangle> up;
    double total = 0.0, upArea = 0.0, sAcc = 0.0, tAcc = 0.0, uvArea = 0.0;
    for (size_t t = 0; t + 2 < b.triangles.size(); t += 3) {
        const uint32_t i0 = b.triangles[t], i1 = b.triangles[t + 1], i2 = b.triangles[t + 2];
        if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
        const Vec3 e1{P[i1][0] - P[i0][0], P[i1][1] - P[i0][1], P[i1][2] - P[i0][2]};
        const Vec3 e2{P[i2][0] - P[i0][0], P[i2][1] - P[i0][1], P[i2][2] - P[i0][2]};
        const Vec3 n{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(len > 0.0) || !std::isfinite(len)) continue;
        const double area = len / 2.0;
        total += area;
        if (std::fabs(n[1]) / len < 0.9) continue;   // up-facing only from here on
        upArea += area;
        up.push_back({{rep[i0], rep[i1], rep[i2]}, area});
        parent[find(rep[i1])] = find(rep[i0]);
        parent[find(rep[i2])] = find(rep[i0]);
        // dP/ds and dP/dt (world units per unit of S and T), from the triangle's edges in S and T
        const float* s0 = b.vertices[i0].st;
        const float* s1 = b.vertices[i1].st;
        const float* s2 = b.vertices[i2].st;
        const double ds1 = s1[0] - s0[0], dt1 = s1[1] - s0[1], ds2 = s2[0] - s0[0], dt2 = s2[1] - s0[1];
        const double det = ds1 * dt2 - ds2 * dt1;
        if (std::fabs(det) > 1e-12) {
            double ps = 0.0, pt = 0.0;
            for (int q = 0; q < 3; ++q) {
                const double gs = (dt2 * e1[q] - dt1 * e2[q]) / det, gt = (-ds2 * e1[q] + ds1 * e2[q]) / det;
                ps += gs * gs;
                pt += gt * gt;
            }
            sAcc += area * std::sqrt(ps);
            tAcc += area * std::sqrt(pt);
            uvArea += area;
        }
    }
    out.upShare = total > 0.0 ? float(upArea / total) : 0.0f;

    // The footprint: each connected up-facing piece's 2 x area / perimeter (its edges used by one triangle, measured
    // across the ground), weighted by its area.
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (const UpTriangle& u : up)
        for (int k = 0; k < 3; ++k) {
            uint32_t x = u.v[k], y = u.v[(k + 1) % 3];
            if (x > y) std::swap(x, y);
            ++edges[{x, y}];
        }
    struct Piece {
        double area = 0.0, perimeter = 0.0;
    };
    std::map<uint32_t, Piece> pieces;
    for (const UpTriangle& u : up) pieces[find(u.v[0])].area += u.area;
    for (const auto& [e, count] : edges)
        if (count == 1) {
            const double dx = P[e.first][0] - P[e.second][0], dz = P[e.first][2] - P[e.second][2];
            pieces[find(e.first)].perimeter += std::sqrt(dx * dx + dz * dz);
        }
    double width = 0.0;
    for (const auto& [root, piece] : pieces)
        if (piece.perimeter > 0.0) width += piece.area * (2.0 * piece.area / piece.perimeter);
    out.width = upArea > 0.0 ? float(width / upArea) : 0.0f;
    if (known && uvArea > 0.0)
        out.texel = float(std::max(sAcc / uvArea / texture->width, tAcc / uvArea / texture->height));
    return out;
}

bool litRoadVerdict(const LitRoadMeasure& m) {
    return m.vertexLuminance > 16.0f && m.vertexAlpha >= 100.0f && m.centreToRim >= 0.0f && m.centreToRim < 3.0f &&
           m.upShare >= 0.9f && m.width >= 0.9f && m.texel >= 0.03f;
}

bool litRoadGeometryFails(const LitRoadMeasure& m) {
    return !(m.vertexLuminance > 16.0f && m.vertexAlpha >= 100.0f && m.upShare >= 0.9f && m.width >= 0.9f);
}

}  // namespace gt4::scene
