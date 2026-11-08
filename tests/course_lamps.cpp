// The course's lamps from a mesh (PLAN.md F2b, platform/render/modern/course_lamps.h), on a synthetic mesh: a star of
// two crossed quads is one lamp; two lamps a lamp-size apart stay two; a lone quad and a lone triangle are lamps; only
// the asked item's triangles count; an item past the mesh's gives none.
#include "course_lamps.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace gt4;
using namespace gt4::render;

namespace {

int failures = 0;
void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what.c_str());
        ++failures;
    }
}
bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

struct Builder {
    scene::Mesh mesh;
    uint32_t vertex(float x, float y, float z) {
        scene::MeshVertex v;
        v.position[0] = x;
        v.position[1] = y;
        v.position[2] = z;
        mesh.vertices.push_back(v);
        return static_cast<uint32_t>(mesh.vertices.size() - 1);
    }
    // A quad over four corners (welded: two triangles share an edge), in either winding.
    void quad(const float c[4][3]) {
        uint32_t i[4];
        for (int k = 0; k < 4; ++k) i[k] = vertex(c[k][0], c[k][1], c[k][2]);
        for (uint32_t x : {i[0], i[1], i[2], i[0], i[2], i[3]}) mesh.indices.push_back(x);
    }
    // A square of half-size h facing z (plane 'z') or x (plane 'x'), centred on (x, y, z).
    void square(float x, float y, float z, float h, char plane) {
        float c[4][3];
        const float u[4] = {-h, h, h, -h}, v[4] = {-h, -h, h, h};
        for (int k = 0; k < 4; ++k) {
            c[k][0] = plane == 'z' ? x + u[k] : x;
            c[k][1] = y + v[k];
            c[k][2] = plane == 'z' ? z : z + u[k];
        }
        quad(c);
    }
    void triangle(float x, float y, float z) {
        const uint32_t a = vertex(x - 0.3f, y, z), b = vertex(x + 0.3f, y, z), c = vertex(x, y + 0.6f, z);
        for (uint32_t i : {a, b, c}) mesh.indices.push_back(i);
    }
    void degenerate(float x, float y, float z) {   // a strip's restart: two equal indices
        const uint32_t a = vertex(x, y, z), b = vertex(x + 1.0f, y, z);
        for (uint32_t i : {a, a, b}) mesh.indices.push_back(i);
    }
    // Two unit quads in a row sharing the edge x = x0 + 1, their triangles ordered so that taking the first neighbour
    // across any edge would pair a triangle of the left quad with one of the right quad (a sheared parallelogram).
    void row(float x0, float y0) {
        const uint32_t v0 = vertex(x0, y0, 0), v1 = vertex(x0 + 1, y0, 0), v2 = vertex(x0 + 2, y0, 0);
        const uint32_t v3 = vertex(x0, y0 + 1, 0), v4 = vertex(x0 + 1, y0 + 1, 0), v5 = vertex(x0 + 2, y0 + 1, 0);
        for (uint32_t i : {v0, v1, v4, v1, v5, v4, v0, v4, v3, v1, v2, v5}) mesh.indices.push_back(i);
    }
    void item(uint32_t first) {
        scene::MeshItem it;
        it.firstIndex = first;
        it.indexCount = static_cast<uint32_t>(mesh.indices.size()) - first;
        it.texture = true;
        mesh.items.push_back(it);
    }
};

const CourseLamp* lampNear(const std::vector<CourseLamp>& lamps, float x, float y, float z) {
    for (const CourseLamp& l : lamps)
        if (std::fabs(l.position[0] - x) < 0.05f && std::fabs(l.position[1] - y) < 0.05f && std::fabs(l.position[2] - z) < 0.05f)
            return &l;
    return nullptr;
}

}  // namespace

int main() {
    Builder b;
    // item 0: the glows
    b.square(0.0f, 5.0f, 0.0f, 0.5f, 'z');   // a star: two crossed quads on one centre
    b.square(0.0f, 5.0f, 0.0f, 0.5f, 'x');
    b.square(10.0f, 5.0f, 0.0f, 0.25f, 'z');   // two lamps 1.0 apart (each 0.71 across): kept apart
    b.square(11.0f, 5.0f, 0.0f, 0.25f, 'z');
    b.square(20.0f, 6.0f, 3.0f, 0.4f, 'z');   // a lone quad
    b.triangle(30.0f, 4.0f, -2.0f);           // a lone triangle (no partner)
    b.degenerate(40.0f, 0.0f, 0.0f);          // left out, counted
    b.item(0);
    // item 1: something else in the same mesh (not glows)
    const uint32_t second = static_cast<uint32_t>(b.mesh.indices.size());
    b.square(50.0f, 0.0f, 0.0f, 2.0f, 'z');
    b.item(second);
    // item 2: a row of two quads sharing an edge
    const uint32_t third = static_cast<uint32_t>(b.mesh.indices.size());
    b.row(70.0f, 0.0f);
    b.item(third);

    LampStats s;
    const std::vector<CourseLamp> lamps = extractLamps(b.mesh, 0, 0.25f, &s);
    expect(s.triangles == 12 && s.degenerate == 1 && s.quads == 6 && s.loneTriangles == 1 && s.skewQuads == 0,
           "item 0: 12 triangles, 1 degenerate left out, 5 rectangles and 1 lone triangle");
    expect(lamps.size() == 5 && s.lamps == 5, "item 0: 5 lamps (the star's two quads are one)");
    const CourseLamp* star = lampNear(lamps, 0.0f, 5.0f, 0.0f);
    expect(star && star->quads == 2 && near(star->radius, 0.5f * std::sqrt(2.0f)), "the star: one lamp of two quads, its radius half their diagonal");
    const CourseLamp* left = lampNear(lamps, 10.0f, 5.0f, 0.0f);
    const CourseLamp* right = lampNear(lamps, 11.0f, 5.0f, 0.0f);
    expect(left && right && left != right && left->quads == 1 && right->quads == 1, "two lamps a lamp-size apart stay two");
    const CourseLamp* lone = lampNear(lamps, 20.0f, 6.0f, 3.0f);
    expect(lone && lone->quads == 1 && near(lone->radius, 0.4f * std::sqrt(2.0f)), "a lone quad: one lamp at its centre");
    const CourseLamp* tri = lampNear(lamps, 30.0f, 4.2f, -2.0f);
    expect(tri && tri->quads == 1, "a lone triangle: one lamp at its centroid");
    expect(!lampNear(lamps, 50.0f, 0.0f, 0.0f), "item 1's quad is not in item 0's lamps");

    const std::vector<CourseLamp> other = extractLamps(b.mesh, 1);
    expect(other.size() == 1 && lampNear(other, 50.0f, 0.0f, 0.0f), "item 1 alone: its one quad");
    expect(extractLamps(b.mesh, 7).empty(), "an item past the mesh's: no lamps");

    LampStats rs;
    const std::vector<CourseLamp> rowLamps = extractLamps(b.mesh, 2, 0.25f, &rs);
    expect(rowLamps.size() == 2 && lampNear(rowLamps, 70.5f, 0.5f, 0.0f) && lampNear(rowLamps, 71.5f, 0.5f, 0.0f) && rs.skewQuads == 0,
           "a row of two quads sharing an edge: each quad's triangles pair together (two lamps at their centres)");

    // a wider merge joins the close pair; a merge of 0 keeps even the star's quads apart
    expect(extractLamps(b.mesh, 0, 2.0f).size() == 4, "merge 2.0: the close pair joins (4 lamps)");
    expect(extractLamps(b.mesh, 0, 0.0f).size() == 5, "merge 0: centres that coincide exactly still merge (distance 0)");

    std::printf("course_lamps: %s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
