// GT4's 3D renderers as native code (see renderers3d.h). Byte addresses in comments are the entry points of
// the original VU1 programs in GT4's micro image (docs/VU1_PROGRAMS.md); "qword N" is qword N of the batch.
#include "renderers3d.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace gt4::ps2 {

namespace {

// --- The VU's number rules, where they shape the picture --------------------------------------------------

// The VU has no infinities, NaNs or denormals: exponent 255 is just a large exponent and denormals read as
// zero. Game data read as floats goes through this, so that plain float arithmetic sees the numbers the VU
// saw (the projection clamp of qword 199 holds 0x7FFFFFFF in lanes it leaves alone: "+max", not a NaN).
float vuFloat(uint32_t bits) {
    if (!(bits & 0x7F800000u)) bits &= 0x80000000u;
    if ((bits & 0x7F800000u) == 0x7F800000u) bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    return std::bit_cast<float>(bits);
}

// The VU saturates where host floats overflow (a point at w near 0, its 1/w).
float saturate(float f) {
    if (std::isnan(f)) return 0.0f;
    return std::clamp(f, -FLT_MAX, FLT_MAX);
}

// DIV: a quotient by zero is the largest float with the quotient's sign (vertices on the eye plane).
float divide(float a, float b) {
    if (b == 0.0f) return std::signbit(a) != std::signbit(b) ? -FLT_MAX : FLT_MAX;
    return saturate(a / b);
}
float reciprocal(float w) { return divide(1.0f, w); }

// RSQRT / ERLENG: a / sqrt(b); the root of zero gives the largest float.
float rsqrt(float a, float b) {
    b = std::fabs(b);
    if (b == 0.0f) return std::signbit(a) ? -FLT_MAX : FLT_MAX;
    return saturate(a / std::sqrt(b));
}

// FTOI0 / FTOI4: to fixed point by truncation toward zero, saturating at the 32-bit limits. The GS takes
// bit fields of these words: X and Y the low 16 bits (12.4), Z bits 4..27, fog bits 4..11 and ADC bit 15 of
// the w word (so fog 2048 or 3072 above the offset sets ADC, and both added together clear it again).
uint32_t fixedPoint(float v, int fractionBits) {
    if (std::isnan(v)) return 0;
    const double scaled = std::trunc(static_cast<double>(v) * static_cast<double>(1u << fractionBits));
    if (scaled >= 2147483647.0) return 0x7FFFFFFFu;
    if (scaled <= -2147483648.0) return 0x80000000u;
    return static_cast<uint32_t>(static_cast<int32_t>(scaled));
}

float itof0(uint32_t w) { return static_cast<float>(static_cast<int32_t>(w)); }
float itof12(uint32_t w) { return static_cast<float>(static_cast<int32_t>(w)) / 4096.0f; }

// The zero and sign flags the culling tests read: the VU flushes results below the smallest normal to
// zero, and reports zero for them.
bool positiveFlag(float z) { return z >= FLT_MIN; }
bool negativeFlag(float z) { return z <= -FLT_MIN; }

Vec4 operator+(const Vec4& a, const Vec4& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
Vec4 operator-(const Vec4& a, const Vec4& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
Vec4 operator*(const Vec4& a, const Vec4& b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
Vec4 operator*(const Vec4& a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
Vec4 saturate(const Vec4& v) { return {saturate(v.x), saturate(v.y), saturate(v.z), saturate(v.w)}; }
Vec4 minimum(const Vec4& a, const Vec4& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z), std::min(a.w, b.w)}; }

using Words = std::array<uint32_t, 4>;

// --- The batch ------------------------------------------------------------------------------------------

enum : uint32_t {
    kPositions = 0,     // x, y, z (w unused)
    kSt = 64,           // s, t in x, y; the w words hold the strip count (qword 64) and lengths (65...)
    kColors = 128,      // RGBA as four integers, or normals for lit models
    kTag = 192,         // GIF tag of the primitives
    kConstants = 193,   // the setup's constants
    kHeader = 217,      // the GS state header, a GIF packet the game wrote
    kWeight = 242,      // .w: the key-frame blend's weight
};

struct Reader {
    BatchBuffer& buffer;
    Words words(uint32_t q) const {
        Words w;
        std::memcpy(w.data(), std::as_const(buffer).qword(q), 16);
        return w;
    }
    void store(uint32_t q, int lane, uint32_t value) const { buffer.qword(q)[lane] = value; }
    void store(uint32_t q, int lane, float value) const { store(q, lane, std::bit_cast<uint32_t>(value)); }
    Vec4 floats(uint32_t q) const {
        const uint32_t* w = buffer.qword(q);
        return {vuFloat(w[0]), vuFloat(w[1]), vuFloat(w[2]), vuFloat(w[3])};
    }
    uint16_t half(uint32_t q, int lane) const { return static_cast<uint16_t>(buffer.qword(q)[lane]); }   // ILW
};

// The batch's GIF tag: what the GS is told about the vertices that follow.
struct Tag {
    Words words{};
    uint32_t nloop = 0;
    bool eop = false, pre = false, fog = true, adcAlways = false, drawable = false;
    uint32_t prim = 0;
    explicit Tag(const Words& w) : words(w) {
        nloop = w[0] & 0x7FFF;
        eop = (w[0] >> 15) & 1;
        pre = (w[1] >> 14) & 1;
        prim = (w[1] >> 15) & 0x7FF;
        const uint32_t flg = (w[1] >> 26) & 3, nreg = (w[1] >> 28) & 0xF;
        // GT4's renderers store ST, RGBAQ and XYZ per vertex: the tag must read them so (PACKED, three
        // registers, XYZF2 or XYZ2; the "3" forms always set ADC).
        const uint32_t xyz = (w[2] >> 8) & 0xF;
        drawable = flg == 0 && nreg == 3 && (w[2] & 0xFF) == 0x12 && (xyz == 0x4 || xyz == 0x5 || xyz == 0xC || xyz == 0xD);
        fog = xyz == 0x4 || xyz == 0xC;
        adcAlways = xyz == 0xC || xyz == 0xD;
    }
};

// One output vertex: ST and Q, the colour words, the XYZ words as the GS reads their fields.
GsVertex gsVertex(const Tag& tag, float s, float t, float q, const Words& colour, const Vec4& xyzf) {
    GsVertex v;
    v.s = s;
    v.t = t;
    v.q = q;
    v.rgba = (colour[0] & 0xFF) | (colour[1] & 0xFF) << 8 | (colour[2] & 0xFF) << 16 | (colour[3] & 0xFF) << 24;
    const uint32_t x = fixedPoint(xyzf.x, 4), y = fixedPoint(xyzf.y, 4), z = fixedPoint(xyzf.z, 4), w = fixedPoint(xyzf.w, 4);
    v.x = x & 0xFFFF;
    v.y = y & 0xFFFF;
    v.z = tag.fog ? (z >> 4) & 0xFFFFFF : z;
    v.fog = tag.fog ? (w >> 4) & 0xFF : 0;
    v.adc = ((w >> 15) & 1) || tag.adcAlways;
    return v;
}

// The length of the GIF packet at qword `start` (tags until EOP), as the GIF reads it.
uint32_t packetQwords(const Reader& r, uint32_t start) {
    uint32_t total = 0;
    while (total < 1024) {
        const Words t = r.words(start + total);
        const uint32_t nloop = t[0] & 0x7FFF, flg = (t[1] >> 26) & 3;
        uint32_t nreg = (t[1] >> 28) & 0xF;
        if (!nreg) nreg = 16;
        total += 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        if ((t[0] >> 15) & 1) break;
    }
    return std::min(total, 1024u);
}

std::vector<uint8_t> copyPacket(const Reader& r, uint32_t start) {
    const uint32_t n = packetQwords(r, start);
    std::vector<uint8_t> bytes(size_t(n) * 16);
    for (uint32_t k = 0; k < n; ++k) std::memcpy(bytes.data() + k * 16, r.buffer.qword(start + k), 16);
    return bytes;
}

// The working copy of a batch: what the renderers and the lighting chain read and compute, per vertex.
struct Vertices {
    std::vector<Vec4> position;   // object space (x, y, z)
    std::vector<Vec4> st;         // texture coordinates (x, y), or normals for untextured lit models
    std::vector<Words> colour;    // RGBA integers (what the renderers pass on)
    std::vector<Vec4> normal;     // the colour slot as floats: normals of lit models
    // What the lighting chain decoded, written back to the batch for a later draw of it (vif1.h BatchBuffer):
    // positions (x, y, z), the ST slot's lanes, the colour slot as normals (x, y, z) or as words (all four).
    bool positions = false;
    unsigned stLanes = 0;
    enum class ColourSlot : uint8_t { Given, Normals, Words } colourSlot = ColourSlot::Given;
    uint32_t colours = 0;         // how many colour slots were written (0x16F8 converts an even number)
    void resize(size_t n) {
        position.resize(n);
        st.resize(n);
        colour.resize(n);
        normal.resize(n);
    }
    void writeBack(const Reader& r, uint32_t n) const {
        for (uint32_t k = 0; k < n; ++k) {
            for (int i = 0; i < 4; ++i) {
                if (positions && i < 3) r.store(kPositions + k, i, position[k][i]);
                if (stLanes & (1u << i)) r.store(kSt + k, i, st[k][i]);
            }
        }
        for (uint32_t k = 0; k < colours; ++k)
            for (int i = 0; i < 4; ++i) {
                if (colourSlot == ColourSlot::Normals && i < 3) r.store(kColors + k, i, normal[k][i]);
                if (colourSlot == ColourSlot::Words) r.store(kColors + k, i, colour[k][i]);
            }
    }
};

// --- The entry table (0x1B08) ---------------------------------------------------------------------------

enum class Renderer : uint16_t { Strip = 0x1EC8, CulledBack = 0x2058, CulledFront = 0x2070, R2300 = 0x2300, R27A8 = 0x27A8, R2AD8 = 0x2AD8 };

struct Entry {
    bool setup;      // reads the setup constants first
    bool lighting;   // runs the lighting chain (ITOP selects it)
    bool header;     // sends the GS state header before the primitives
    Renderer renderer;
};

// 24 groups of five instructions. Group g starts with a call of the setup; entering one instruction later
// skips it. Groups 0-11 go straight to a renderer, 12-23 through the lighting chain; even groups send the
// header, odd ones do not. Two consecutive groups share a renderer.
bool decodeEntry(uint32_t pc, Entry& entry) {
    const uint32_t byte = pc * 8;
    if (byte < 0x1B08 || byte >= 0x1EC8) return false;
    const uint32_t offset = byte - 0x1B08, group = offset / 0x28, within = offset % 0x28;
    if (within > 8) return false;
    static const Renderer renderers[6] = {Renderer::Strip, Renderer::CulledBack, Renderer::CulledFront,
                                          Renderer::R2300, Renderer::R27A8, Renderer::R2AD8};
    entry.setup = within == 0;
    entry.lighting = group >= 12;
    entry.header = (group & 1) == 0;
    entry.renderer = renderers[(group % 12) / 2];
    return true;
}

// --- Projection -----------------------------------------------------------------------------------------

struct Projector {
    const Renderers3d::Constants& c;
    const Renderers3d::Constants* previous = nullptr;   // the object's constants in the previous frame (motion)
    // The transform: the position's w is taken as 1.
    Vec4 clip(const Vec4& p) const {
        Vec4 r;
        for (int i = 0; i < 4; ++i) r[i] = saturate(c.matrix[0][i] * p.x + c.matrix[1][i] * p.y + c.matrix[2][i] * p.z + c.matrix[3][i]);
        return r;
    }
    // The clip-space point clamped (qword 199), divided by w.
    Vec4 projected(const Vec4& p, float q) const { return saturate(minimum(p, c.clamp) * q); }
    // Window coordinates (x, y, z) and fog (w) before FTOI4: the offset plus the projected point scaled.
    Vec4 window(const Vec4& s, const Vec4& base) const {
        Vec4 r;
        for (int i = 0; i < 4; ++i) r[i] = base[i] + s[i] * c.scale[i];
        return r;
    }
    // Where the previous frame's transform put the point: homogeneous window coordinates (x w, y w, w); w = 0
    // when not known or behind the eye then.
    Vec4 history(const Vec4& p) const {
        if (!previous) return {};
        const Renderers3d::Constants& o = *previous;
        auto row = [&](int i) { return o.matrix[0][i] * p.x + o.matrix[1][i] * p.y + o.matrix[2][i] * p.z + o.matrix[3][i]; };
        const float x = row(0), y = row(1), w = row(3);
        if (!(w > 1e-6f) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w)) return {};
        return {o.offset.x * w + o.scale.x * x, o.offset.y * w + o.scale.y * y, w, 0.0f};
    }
    // A vertex's motion: its history over this frame's w (q = 1/w).
    static void withHistory(GsVertex& v, const Vec4& h, float q) {
        if (h.z == 0.0f || !(q > 0.0f) || q >= FLT_MAX) return;
        const float m[3] = {h.x * q, h.y * q, h.z * q};
        if (std::isfinite(m[0]) && std::isfinite(m[1]) && std::isfinite(m[2]) && m[2] > 0.0f)
            for (int i = 0; i < 3; ++i) v.motion[i] = m[i];
    }
    void motion(GsVertex& v, const Vec4& position, float q) const {
        if (previous) withHistory(v, history(position), q);
    }
};

// The offset with fog raised by 2048 (or 3072): FTOI4 then sets bit 15 of the fog word, ADC.
Vec4 adcOffset(const Vec4& offset, float raise) {
    Vec4 r = offset;
    r.w = r.w + raise;
    return r;
}
constexpr float kAdc2048 = 2048.0f, kAdc3072 = 3072.0f;

// CLIP: bit 0 x > |w|, bit 1 x < -|w|, bits 2-3 y, bits 4-5 z.
uint32_t clipFlags(const Vec4& p) {
    const float limit = std::fabs(p.w);
    uint32_t f = 0;
    for (int i = 0; i < 3; ++i) {
        if (p[i] > limit) f |= 1u << (i * 2);
        if (p[i] < -limit) f |= 2u << (i * 2);
    }
    return f;
}

// The strips: qword 64 .w is their count, qwords 65.. .w the length of each as the distance in output
// qwords (3 per vertex) from its first to its second-to-last vertex. A strip has at least three vertices.
uint32_t stripCount(const Reader& r) { return r.half(kSt, 3); }
uint32_t stripVertices(const Reader& r, uint32_t k) { return r.half(kSt + 1 + k, 3) / 3u + 2; }

// --- Strip renderers without clipping ------------------------------------------------------------------

struct Output {
    const Tag& tag;
    std::vector<GsVertex>& vertices;
};

// 0x1EC8: textured strips, no culling. The first two vertices of each strip carry ADC (the triangles that
// would join two strips are not drawn).
void stripRenderer(const Reader& r, const Projector& p, Vertices& in, Output& out) {
    const Vec4 adc = adcOffset(p.c.offset, kAdc2048);
    uint32_t v = 0;
    for (uint32_t k = 0, strips = stripCount(r); k < strips; ++k)
        for (uint32_t j = 0, n = stripVertices(r, k); j < n; ++j, ++v) {
            const Vec4 clip = p.clip(in.position[v]);
            const float q = reciprocal(clip.w);
            const Vec4 s = p.projected(clip, q);
            out.vertices.push_back(gsVertex(out.tag, in.st[v].x * q, in.st[v].y * q, q, in.colour[v], p.window(s, j < 2 ? adc : p.c.offset)));
            p.motion(out.vertices.back(), in.position[v], q);
        }
}

// The back-face test of 0x2058 / 0x2070: the z of the cross product of the projected edges old = s[-2] -
// s[-1] and new = s[-1] - s[0], whose operand order alternates with the vertex's index in its strip (the
// original handles two vertices per pass).
float facing(const Vec4 s[3], bool oddOrder) {
    const Vec4 older = s[0] - s[1], newer = s[1] - s[2];
    return oddOrder ? newer.x * older.y - older.x * newer.y : older.x * newer.y - newer.x * older.y;
}

// 0x2058 / 0x2070: as 0x1EC8, plus back-face culling: a vertex whose triangle faces away (0x2058 keeps
// z > 0, 0x2070 z < 0) gets ADC. The first two vertices of each strip always do.
void culledRenderer(const Reader& r, const Projector& p, Vertices& in, Output& out, bool front) {
    const Vec4 adc = adcOffset(p.c.offset, kAdc2048);
    uint32_t v = 0;
    Vec4 s[3];
    for (uint32_t k = 0, strips = stripCount(r); k < strips; ++k)
        for (uint32_t j = 0, n = stripVertices(r, k); j < n; ++j, ++v) {
            const Vec4 clip = p.clip(in.position[v]);
            const float q = reciprocal(clip.w);
            s[0] = s[1];
            s[1] = s[2];
            s[2] = p.projected(clip, q);
            bool culled = j < 2;
            if (!culled) {
                const float z = facing(s, (j & 1) != 0);
                culled = front ? !negativeFlag(z) : !positiveFlag(z);
            }
            out.vertices.push_back(gsVertex(out.tag, in.st[v].x * q, in.st[v].y * q, q, in.colour[v], p.window(s[2], culled ? adc : p.c.offset)));
            p.motion(out.vertices.back(), in.position[v], q);
        }
}

// --- Strip renderers with clipping: 0x2300 (no culling), 0x27A8 and 0x2AD8 (back-face culling) -----------
//
// For every vertex j >= 2 of a strip, its triangle (j-2, j-1, j):
// - no vertex outside the frustum (CLIP of the clip-space point against its w): drawn;
// - all three outside one plane: not drawn (ADC);
// - else, inside the guard band (the points scaled by the guard vector): drawn, the GS clips it;
// - else 0x27A8 / 0x2AD8 first repeat the back-face test in homogeneous coordinates (culled: not drawn),
//   and the polygon clipper draws the rest as a strip of its own, before the batch's strip; the triangle
//   itself is not drawn there (ADC).
// 0x27A8 / 0x2AD8 then cull as 0x2058 (0x2AD8 with the cross product's operand order swapped): a culled
// vertex gets 3072 added to its fog. Their ADC offset adds 3072 as well, 0x2300's 2048: a vertex both
// hidden and culled has 6144 added, which clears bit 15 again and draws its triangle, as on the PS2.

enum class Clipped { Plain2300, Back27A8, Front2AD8 };

// The homogeneous back-face test (0x2EA0..0x2F50) on (x, y, w); `variantB`: the cross product's operand
// order, which the original picks from its return address.
bool homogeneousCulled(const Vec4 p[3], const Vec4& guard, bool variantB) {
    const float gw = p[0].w * guard.w;
    const Vec4 a = {p[0].x, p[0].y, gw, gw};
    Vec4 e1 = p[1] - p[0], e2 = p[2] - p[0];
    e1.z = e1.w;
    e2.z = e2.w;
    Vec4 cross;
    if (variantB) {
        cross.x = e1.y * e2.z - e2.y * e1.z;
        cross.y = e1.z * e2.x - e2.z * e1.x;
        cross.z = e1.x * e2.y - e2.x * e1.y;
    } else {
        cross.x = e2.y * e1.z - e1.y * e2.z;
        cross.y = e2.z * e1.x - e1.z * e2.x;
        cross.z = e2.x * e1.y - e1.x * e2.y;
    }
    const float z = a.z * cross.z + a.x * cross.x + a.y * cross.y;
    return !positiveFlag(z);
}

// A vertex of the polygon clipper: clip-space position, ST, colour (floats), and the guard-scaled point g
// as its signed distances to the six planes (g + g.w for the -w side, g - g.w for the +w side).
struct ClipVertex {
    Vec4 pos, st, colour;
    Vec4 history;   // Projector::history (motion vectors; not part of the original)
    Vec4 plus, minus;
    uint32_t flags = 0;
};

ClipVertex clipVertex(const Vec4& pos, const Vec4& g, const Vec4& st, const Vec4& colour, const Vec4& history = {}) {
    ClipVertex r;
    r.history = history;
    r.pos = pos;
    r.st = st;
    r.colour = colour;
    for (int i = 0; i < 4; ++i) {
        r.plus[i] = g[i] + g.w;
        r.minus[i] = g[i] - g.w;
    }
    r.flags = clipFlags(g);
    return r;
}

// The polygon clipper (0x2FE0..0x3D48): Sutherland-Hodgman against the guard band's six planes, in the
// original's order, then one strip from both ends of the ring (first, last, second, second to last, ...),
// its first vertex with the ADC offset. A triangle gains at most six vertices this way (nine at most); the
// original keeps its ring in VU memory, which a polygon of ten or more (only a ring made non-convex by
// rounding gets there) would overrun: this one is drawn as clipped (counted in Stats::largeClips).
void clipPolygon(const ClipVertex triangle[3], const Projector& p, const Vec4& adc, const Tag& batchTag, PrimitiveSink& sink,
                 Renderers3d::Stats& stats) {
    static thread_local std::vector<ClipVertex> ring, next;
    ring.assign(triangle, triangle + 3);
    static const struct { uint32_t mask; bool plus; int lane; } planes[6] = {{32, true, 2}, {16, false, 2}, {8, true, 1},
                                                                             {4, false, 1}, {2, true, 0}, {1, false, 0}};
    const Vec4 guard = p.c.guard;
    uint32_t any = ring[0].flags | ring[1].flags | ring[2].flags;
    for (const auto& plane : planes) {
        if (!(any & plane.mask)) continue;
        next.clear();
        any = 0;
        for (size_t k = 0; k < ring.size(); ++k) {
            const ClipVertex& a = ring[k];
            const ClipVertex& e = ring[(k + 1) % ring.size()];
            const bool aOut = (a.flags & plane.mask) != 0, eOut = (e.flags & plane.mask) != 0;
            if (!aOut) {
                next.push_back(a);
                any |= a.flags;
            }
            if (aOut == eOut) continue;
            // The intersection: (e dA - a dE) / (dA - dE), dA and dE the distances to this plane.
            const float da = (plane.plus ? a.plus : a.minus)[plane.lane], de = (plane.plus ? e.plus : e.minus)[plane.lane];
            const float q = reciprocal(da - de);
            Vec4 pos, st, colour, history;
            for (int i = 0; i < 4; ++i) {
                pos[i] = e.pos[i] * da - a.pos[i] * de;
                st[i] = e.st[i] * da - a.st[i] * de;
                colour[i] = e.colour[i] * da - a.colour[i] * de;
                history[i] = e.history[i] * da - a.history[i] * de;
            }
            // (a history known at one end only is not known: its w would interpolate towards 0)
            if (a.history.z == 0.0f || e.history.z == 0.0f) history = {};
            next.push_back(clipVertex(saturate(pos * q), saturate((pos * guard) * q), saturate(st * q), saturate(colour * q), history * q));
            any |= next.back().flags;
        }
        ring.swap(next);
        if (ring.size() < 3) return;   // nothing left
    }
    ++stats.clipped;
    if (ring.size() > 9) ++stats.largeClips;
    if (!batchTag.drawable) return;   // counted with the batch (Stats::unexpectedTags)
    // The strip's tag: the batch's, with NLOOP the polygon's vertex count.
    static thread_local std::vector<GsVertex> strip;
    strip.clear();
    const size_t n = ring.size();
    for (size_t k = 0; k < n; ++k) {
        const ClipVertex& v = ring[(k & 1) ? n - 1 - k / 2 : k / 2];
        const float q = reciprocal(v.pos.w);
        // Colours are rounded to the nearest integer here (+0.5, then truncated).
        const Words colour = {fixedPoint(v.colour.x + 0.5f, 0), fixedPoint(v.colour.y + 0.5f, 0), fixedPoint(v.colour.z + 0.5f, 0),
                              fixedPoint(v.colour.w + 0.5f, 0)};
        strip.push_back(gsVertex(batchTag, v.st.x * q, v.st.y * q, q, colour, p.window(p.projected(v.pos, q), k == 0 ? adc : p.c.offset)));
        Projector::withHistory(strip.back(), v.history, q);
    }
    GsPrimitives prims;
    prims.setPrim = batchTag.pre;
    prims.prim = batchTag.prim;
    prims.fog = batchTag.fog;
    prims.vertices = strip.data();
    prims.count = strip.size();
    sink.primitives(prims);
}

void clippedRenderer(const Reader& r, const Projector& p, Vertices& in, Output& out, Clipped kind, PrimitiveSink& sink,
                     Renderers3d::Stats& stats) {
    const bool culling = kind != Clipped::Plain2300;
    const Vec4 adc = adcOffset(p.c.offset, culling ? kAdc3072 : kAdc2048);
    const Vec4 guard = p.c.guard;
    uint32_t v = 0;
    Vec4 clip[3], s[3];   // clip-space and projected points of the vertex two before, one before and this one
    uint32_t flags[3] = {};
    for (uint32_t k = 0, strips = stripCount(r); k < strips; ++k)
        for (uint32_t j = 0, n = stripVertices(r, k); j < n; ++j, ++v) {
            clip[0] = clip[1];
            clip[1] = clip[2];
            clip[2] = p.clip(in.position[v]);
            flags[0] = flags[1];
            flags[1] = flags[2];
            flags[2] = clipFlags(clip[2]);
            const float q = reciprocal(clip[2].w);
            s[0] = s[1];
            s[1] = s[2];
            s[2] = p.projected(clip[2], q);
            bool hidden = j < 2;
            if (!hidden && (flags[0] | flags[1] | flags[2])) {
                if (flags[0] & flags[1] & flags[2]) {
                    hidden = true;
                } else if (clipFlags(saturate(clip[0] * guard)) | clipFlags(saturate(clip[1] * guard)) | clipFlags(saturate(clip[2] * guard))) {
                    const bool variantB = (kind == Clipped::Back27A8) == ((j & 1) == 0);
                    if (!culling || !homogeneousCulled(clip, guard, variantB)) {
                        ClipVertex triangle[3];
                        for (uint32_t t = 0; t < 3; ++t) {
                            const Words& c = in.colour[v - 2 + t];
                            triangle[t] = clipVertex(clip[t], saturate(clip[t] * guard), in.st[v - 2 + t],
                                                     {itof0(c[0]), itof0(c[1]), itof0(c[2]), itof0(c[3])}, p.history(in.position[v - 2 + t]));
                        }
                        clipPolygon(triangle, p, adc, out.tag, sink, stats);
                    }
                    hidden = true;
                }
            }
            Vec4 xyzf = p.window(s[2], hidden ? adc : p.c.offset);
            if (culling && j >= 2) {
                const float z = facing(s, ((j & 1) != 0) != (kind == Clipped::Front2AD8));
                if (!positiveFlag(z)) xyzf.w = xyzf.w + kAdc3072;
            }
            out.vertices.push_back(gsVertex(out.tag, in.st[v].x * q, in.st[v].y * q, q, in.colour[v], xyzf));
            p.motion(out.vertices.back(), in.position[v], q);
        }
}

// --- The lighting chain (0x0048) -------------------------------------------------------------------------
//
// ITOP selects one of 15 blocks: a vertex-decompression routine (one of five, or none) and a lighting body.
// All work on the first NLOOP vertices (the tag's count).

constexpr float kNormalScale = 1.0f / 127.0f;   // normals are signed bytes

// Raises x to 2^squarings by repeated squaring, as the original does.
float power(float x, int squarings) {
    for (int k = 0; k < squarings; ++k) x = saturate(x * x);
    return x;
}

uint32_t toInteger(float v) { return fixedPoint(v, 0); }
Words toIntegers(const Vec4& v) { return {toInteger(v.x), toInteger(v.y), toInteger(v.z), toInteger(v.w)}; }

// Decompression routines (byte addresses). Positions as integers, texture coordinates in 4.12 fixed point,
// normals as bytes scaled by 1/127 (in the colour slot for lit models, the ST slot for untextured ones).
void unpackPositions(const Reader& r, Vertices& in, uint32_t n) {
    for (uint32_t k = 0; k < n; ++k) {
        const Words p = r.words(kPositions + k);
        in.position[k] = {itof0(p[0]), itof0(p[1]), itof0(p[2]), in.position[k].w};
    }
}
void unpackSt(const Reader& r, Vertices& in, uint32_t n) {
    for (uint32_t k = 0; k < n; ++k) {
        const Words st = r.words(kSt + k);
        in.st[k].x = itof12(st[0]);
        in.st[k].y = itof12(st[1]);
    }
}
Vec4 byteNormal(const Words& w, float keepW) { return {itof0(w[0]) * kNormalScale, itof0(w[1]) * kNormalScale, itof0(w[2]) * kNormalScale, keepW}; }

// 0x1990 / 0x1A38: two key frames 32 qwords apart blended by t = qword 242 .w, a (1 - t) + b t: positions
// and ST (x, y, z) and the colour slot (0x1990 as floats, 0x1A38 as integers).
void blendKeyFrames(const Reader& r, Vertices& in, uint32_t n, bool integerColours) {
    const float t = r.floats(kWeight).w, rest = 1.0f - t;
    for (uint32_t k = 0; k < n; ++k) {
        const Vec4 pa = r.floats(kPositions + k), pb = r.floats(kPositions + 32 + k);
        const Vec4 sa = r.floats(kSt + k), sb = r.floats(kSt + 32 + k);
        for (int i = 0; i < 3; ++i) {
            in.position[k][i] = saturate(pa[i] * rest + pb[i] * t);
            in.st[k][i] = saturate(sa[i] * rest + sb[i] * t);
        }
        if (integerColours) {
            const Words a = r.words(kColors + k), b = r.words(kColors + 32 + k);
            Vec4 c;
            for (int i = 0; i < 4; ++i) c[i] = itof0(a[i]) * rest + itof0(b[i]) * t;
            in.colour[k] = toIntegers(c);
            in.normal[k] = {vuFloat(in.colour[k][0]), vuFloat(in.colour[k][1]), vuFloat(in.colour[k][2]), vuFloat(in.colour[k][3])};
        } else {
            const Vec4 ca = r.floats(kColors + k), cb = r.floats(kColors + 32 + k);
            Vec4 c;
            for (int i = 0; i < 4; ++i) c[i] = saturate(ca[i] * rest + cb[i] * t);
            in.normal[k] = c;
            // A model drawn unlit after this blend passes the float bits on as its colour words.
            for (int i = 0; i < 4; ++i) in.colour[k][i] = std::bit_cast<uint32_t>(c[i]);
        }
    }
}

// max(m0 * n.x + m1 * n.y + m2 * n.z, 0) per lane: four N.L terms of the lights in the matrix's columns.
Vec4 dots(const Vec4 m[3], const Vec4& n) {
    Vec4 r;
    for (int i = 0; i < 4; ++i) r[i] = std::max(saturate(m[0][i] * n.x + m[1][i] * n.y + m[2][i] * n.z), 0.0f);
    return r;
}

// The N.L model of most bodies: colour = ambient (qword 204) + c205 d.x + c206 d.y + c207 d.z^p + c208 d.w^p
// [+ c212..c215 e^q], clamped to [0, clamp] (qword 216 with the second set, else 209). d: the lights of
// qwords 201..203; e: a second set in 209..211. `nextNormal`: 0x0290 starts its sum with MADDA, so the first
// two products of the next vertex's N.L are added too (a quirk of the original, kept); after the last vertex
// that "next normal" is the qword after the batch's normals as the buffer holds it (in practice an earlier
// batch's lit colours: small integers, denormal floats, which count as zero).
struct DotModel {
    int squaringsA;
    bool second;
    int squaringsB;
    bool nextNormal;
};

void dotLighting(const Reader& r, Vertices& in, uint32_t n, const DotModel& model) {
    Vec4 a[3], e[3], c[8];
    for (int k = 0; k < 3; ++k) a[k] = r.floats(201 + k);
    for (int k = 0; k < 3; ++k) e[k] = r.floats(209 + k);
    const Vec4 ambient = r.floats(204);
    for (int k = 0; k < 4; ++k) c[k] = r.floats(205 + k);
    for (int k = 0; k < 4; ++k) c[4 + k] = r.floats(212 + k);
    const Vec4 clamp = r.floats(model.second ? 216 : 209);
    for (uint32_t k = 0; k < n; ++k) {
        const Vec4& normal = in.normal[k];
        Vec4 d = dots(a, normal), f{};
        d.z = power(d.z, model.squaringsA);
        d.w = power(d.w, model.squaringsA);
        if (model.second) {
            f = dots(e, normal);
            for (int i = 0; i < 4; ++i) f[i] = power(f[i], model.squaringsB);
        }
        const Vec4 next = k + 1 < n ? in.normal[k + 1] : r.floats(kColors + n);
        Vec4 colour;
        for (int i = 0; i < 4; ++i) {
            float acc = model.nextNormal ? (a[0][i] * next.x + a[1][i] * next.y) + ambient[i] : ambient[i];
            acc += c[0][i] * d.x + c[1][i] * d.y + c[2][i] * d.z + c[3][i] * d.w;
            if (model.second)
                for (int j = 0; j < 4; ++j) acc += c[4 + j][i] * f[j];
            colour[i] = std::min(std::max(saturate(acc), 0.0f), clamp[i]);
        }
        in.colour[k] = toIntegers(colour);
    }
}

// 0x0EA0 / 0x0CA8: specular from the eye. v = position - eye (qword 204), r = v - 2 (v.n) n, L = (201.x,
// 202.x, 203.x); 0x0EA0: s = max(L.r / |v|, 0)^16; 0x0CA8: r normalised first, s = max(L.r, 0)^8. Colour
// c205 s, alpha 128.
void specularLighting(const Reader& r, Vertices& in, uint32_t n, bool normalised) {
    const Vec4 eye = r.floats(204), coefficient = r.floats(205);
    const Vec4 light = {r.floats(201).x, r.floats(202).x, r.floats(203).x, 0};
    for (uint32_t k = 0; k < n; ++k) {
        const Vec4& position = in.position[k];
        const Vec4& normal = in.normal[k];
        const Vec4 v = position - eye;
        const float twice = 2.0f * (v.x * normal.x + v.y * normal.y + v.z * normal.z);
        const float inverseLength = rsqrt(1.0f, v.x * v.x + v.y * v.y + v.z * v.z);
        Vec4 reflected = {v.x - normal.x * twice, v.y - normal.y * twice, v.z - normal.z * twice, 0};
        float dot;
        float s;
        if (normalised) {
            reflected = saturate(reflected * inverseLength);
            dot = light.x * reflected.x + light.y * reflected.y + light.z * reflected.z;
            s = power(std::max(saturate(dot), 0.0f), 3);
        } else {
            dot = (light.x * reflected.x + light.y * reflected.y + light.z * reflected.z) * inverseLength;
            s = power(std::max(saturate(dot), 0.0f), 4);
        }
        in.colour[k] = toIntegers({coefficient.x * s, coefficient.y * s, coefficient.z * s, 128.0f});
    }
}

// The reflection of the eye ray v about the normal n, rotated by qwords 201..203: (v - 2 (v.n) n) M.
Vec4 reflection(const Vec4 m[3], const Vec4& v, const Vec4& normal, float d) {
    const Vec4 rr = {v.x - 2.0f * normal.x * d, v.y - 2.0f * normal.y * d, v.z - 2.0f * normal.z * d, 0};
    Vec4 s;
    for (int i = 0; i < 3; ++i) s[i] = saturate(m[0][i] * rr.x + m[1][i] * rr.y + m[2][i] * rr.z);
    return s;
}

// The colour of the reflective bodies: itof(colour) (c205 + c206 min(v.n, 0) / |v|): darker where the
// surface turns away.
Words reflectiveColour(const Words& given, const Vec4& base, const Vec4& rim, float t) {
    Vec4 colour;
    for (int i = 0; i < 4; ++i) colour[i] = saturate(itof0(given[i]) * (base[i] + rim[i] * t));
    return toIntegers(colour);
}

// 0x10A8: reflective paint (normals in the ST slot): ST a sphere map of the reflection r / |v|: with
// k = 0.5 / |r / |v| + (0, 0, 1)|, ST = (0.5 + r.x k, 0.5 - r.y k).
void reflectiveLighting(const Reader& r, Vertices& in, uint32_t n) {
    const Vec4 eye = r.floats(204), base = r.floats(205), rim = r.floats(206);
    const Vec4 m[3] = {r.floats(201), r.floats(202), r.floats(203)};
    for (uint32_t k = 0; k < n; ++k) {
        const Vec4 normal = in.st[k];
        const Vec4 v = in.position[k] - eye;
        const float d = v.x * normal.x + v.y * normal.y + v.z * normal.z;
        const float e = rsqrt(1.0f, v.x * v.x + v.y * v.y + v.z * v.z);
        in.colour[k] = reflectiveColour(in.colour[k], base, rim, saturate(std::min(d, 0.0f) * e));
        Vec4 s = saturate(reflection(m, v, normal, d) * e);
        s.z = s.z + 1.0f;
        const float scale = rsqrt(0.5f, s.x * s.x + s.y * s.y + s.z * s.z);
        in.st[k].x = s.x * scale + 0.5f;
        in.st[k].y = 0.5f - s.y * scale;
    }
}

// 0x12C8: as 0x10A8, with ST from a map by quadrant: the signs of r.x - r.z and r.x + r.z pick a quadrant,
// u = quadrant (0, 2, 4, 6) + a / b with (a, b) = (r.z, r.x) or (-r.x, r.z), v = t^3 + 2t with t = r.y / |v|;
// ST = (202.w + 201.w u, 204.w + 203.w v).
void quadrantMapLighting(const Reader& r, Vertices& in, uint32_t n) {
    const Vec4 eye = r.floats(204), base = r.floats(205), rim = r.floats(206);
    const Vec4 m[3] = {r.floats(201), r.floats(202), r.floats(203)};
    const float uScale = m[0].w, uOffset = m[1].w, vScale = m[2].w, vOffset = eye.w;
    for (uint32_t k = 0; k < n; ++k) {
        const Vec4 normal = in.st[k];
        const Vec4 v = in.position[k] - eye;
        const float d = v.x * normal.x + v.y * normal.y + v.z * normal.z;
        const float e = rsqrt(1.0f, v.x * v.x + v.y * v.y + v.z * v.z);
        in.colour[k] = reflectiveColour(in.colour[k], base, rim, saturate(std::min(d, 0.0f) * e));
        const Vec4 s = reflection(m, v, normal, d);
        const bool a = std::signbit(s.x - s.z), c = std::signbit(s.x + s.z);
        float numerator, denominator, quadrant;
        if (a == c) {
            numerator = s.z;
            denominator = s.x;
            quadrant = a ? 4.0f : 0.0f;
        } else {
            numerator = -s.x;
            denominator = s.z;
            quadrant = a ? 2.0f : 6.0f;
        }
        const float u = saturate(quadrant + divide(numerator, denominator));
        const float t = saturate(e * s.y);
        const float vv = saturate(t * t * t + (t + t));
        in.st[k].x = saturate(uOffset + uScale * u);
        in.st[k].y = saturate(vOffset + vScale * vv);
    }
}

// 0x1608: a planar projection: colour c205, ST = c204 + c201 x + c202 y + c203 z.
void planarMapLighting(const Reader& r, Vertices& in, uint32_t n) {
    const Vec4 m[3] = {r.floats(201), r.floats(202), r.floats(203)}, offset = r.floats(204);
    const Words colour = toIntegers(r.floats(205));
    for (uint32_t k = 0; k < n; ++k) {
        const Vec4& p = in.position[k];
        in.colour[k] = colour;
        for (int i = 0; i < 2; ++i) in.st[k][i] = saturate(offset[i] + m[0][i] * p.x + m[1][i] * p.y + m[2][i] * p.z);
    }
}

// 0x1508: prelit colours: clamp(c203 + itof(colour) c201, 0, c202).
void modulateColours(const Reader& r, Vertices& in, uint32_t n) {
    const Vec4 scale = r.floats(201), clamp = r.floats(202), offset = r.floats(203);
    for (uint32_t k = 0; k < n; ++k) {
        Vec4 colour;
        for (int i = 0; i < 4; ++i) colour[i] = std::min(std::max(saturate(offset[i] + itof0(in.colour[k][i]) * scale[i]), 0.0f), clamp[i]);
        in.colour[k] = toIntegers(colour);
    }
}

// 0x16F8: fade. Qword 202 .x is the offset of a per-object table from the batch: the word at qword
// 203 + offset is the alpha, 0..255. Zero: the batch draws nothing (returns false). Otherwise colour =
// min(itof(colour) (c201.xyz, alpha / 255), 255).
bool fadeColours(const Reader& r, Vertices& in, uint32_t n, Renderers3d::Stats& stats) {
    const uint32_t table = (r.half(202, 0) + 203u) & 0x3FF;
    if (table >= BatchBuffer::kQwords) ++stats.oversized;   // outside the batch: not seen in GT4
    const uint32_t alphaWord = r.words(table)[0];
    if (!static_cast<uint16_t>(alphaWord)) return false;
    const Vec4 scale = r.floats(201);
    const float alpha = itof0(alphaWord) * (1.0f / 255.0f);
    in.colours = (n + 1) & ~1u;   // two vertices per pass: an odd count converts one more
    for (uint32_t k = 0; k < in.colours; ++k) {
        const Words& c = in.colour[k];
        in.colour[k] = toIntegers({std::min(itof0(c[0]) * scale.x, 255.0f), std::min(itof0(c[1]) * scale.y, 255.0f),
                                   std::min(itof0(c[2]) * scale.z, 255.0f), std::min(itof0(c[3]) * alpha, 255.0f)});
    }
    return true;
}

// What ITOP selects: a decompression routine (0..4: 0x1908, 0x1878, 0x1810, 0x1990, 0x1A38; 5: none) and a
// body (its address).
struct ChainPlan {
    bool known = false;
    uint32_t unpack = 5, body = 0;
    DotModel model{};
    bool dot = false;
};

ChainPlan planChain(uint32_t itop) {
    static const uint16_t blocks[15] = {0x060, 0x0C8, 0x240, 0x3C8, 0x560, 0x738, 0x910, 0xAF8, 0xC58, 0xE50, 0x1058, 0x1278, 0x14B8, 0x15B8, 0x16A8};
    ChainPlan plan;
    const uint32_t at = itop * 8;
    for (const uint16_t start : blocks) {
        if (at < start || at > start + 0x50u || (at - start) % 0x10) continue;
        plan.body = start + 0x50u;
        plan.unpack = (at - start) / 0x10;
    }
    plan.dot = true;
    switch (plan.body) {
    case 0x118: plan.model = {2, false, 0, false}; break;   // d.z, d.w to the 4th power
    case 0x290: plan.model = {3, false, 0, true}; break;    // 8th power, with the next normal's products
    case 0x418: plan.model = {4, false, 0, false}; break;   // 16th power
    case 0x5B0: plan.model = {0, true, 2, false}; break;    // four linear terms, four more to the 4th power
    case 0x788: plan.model = {0, true, 3, false}; break;    // ... to the 8th power
    case 0x960: plan.model = {0, true, 4, false}; break;    // ... to the 16th power
    case 0xB48: plan.model = {0, false, 0, false}; break;   // four linear terms
    case 0x0B0: case 0xCA8: case 0xEA0: case 0x10A8: case 0x12C8: case 0x1508: case 0x1608: case 0x16F8: plan.dot = false; break;
    default: return plan;   // ITOP outside the blocks
    }
    plan.known = true;
    return plan;
}

// Runs the chain. False: the body ends the batch with nothing drawn (0x16F8, alpha 0).
bool runChain(const Reader& r, Vertices& in, uint32_t n, const ChainPlan& plan, Renderers3d::Stats& stats) {
    using Slot = Vertices::ColourSlot;
    in.positions = plan.unpack < 5;
    in.colours = n;
    switch (plan.unpack) {
    case 0:   // 0x1908: positions, normals in the ST slot
        unpackPositions(r, in, n);
        for (uint32_t k = 0; k < n; ++k) in.st[k] = byteNormal(r.words(kSt + k), in.st[k].w);
        in.stLanes = 7;
        break;
    case 1:   // 0x1878: positions, ST, normals in the colour slot
        unpackPositions(r, in, n);
        unpackSt(r, in, n);
        for (uint32_t k = 0; k < n; ++k) {
            in.normal[k] = byteNormal(r.words(kColors + k), in.normal[k].w);
            // The colour slot holds the normals now: a body that reads it as colour words (or none) sees floats.
            for (int i = 0; i < 3; ++i) in.colour[k][i] = std::bit_cast<uint32_t>(in.normal[k][i]);
        }
        in.stLanes = 3;
        in.colourSlot = Slot::Normals;
        break;
    case 2:   // 0x1810: positions, ST
        unpackPositions(r, in, n);
        unpackSt(r, in, n);
        in.stLanes = 3;
        break;
    case 3:   // 0x1990
    case 4:   // 0x1A38
        blendKeyFrames(r, in, n, plan.unpack == 4);
        in.stLanes = 7;
        in.colourSlot = Slot::Words;
        break;
    default: break;
    }
    // The bodies light the colours (all but 0x00B0), and some compute texture coordinates.
    if (plan.body != 0x0B0) in.colourSlot = Slot::Words;
    if (plan.body == 0x10A8 || plan.body == 0x12C8 || plan.body == 0x1608) in.stLanes |= 3;
    if (plan.dot) dotLighting(r, in, n, plan.model);
    else if (plan.body == 0xCA8) specularLighting(r, in, n, true);
    else if (plan.body == 0xEA0) specularLighting(r, in, n, false);
    else if (plan.body == 0x12C8) quadrantMapLighting(r, in, n);
    else if (plan.body == 0x1608) planarMapLighting(r, in, n);
    else if (plan.body == 0x10A8) reflectiveLighting(r, in, n);
    else if (plan.body == 0x1508) modulateColours(r, in, n);
    else if (plan.body == 0x16F8 && !fadeColours(r, in, n, stats)) {
        in.colourSlot = plan.unpack == 3 || plan.unpack == 4 ? Slot::Words : Slot::Given;   // nothing lit
        if (plan.unpack == 1) in.colourSlot = Slot::Normals;
        return false;
    }
    return true;
}

// --- Screen grids: 0x3D50 and 0x3EA0 -----------------------------------------------------------------------
//
// Full-screen meshes for post effects: a GIF packet the game wrote (qword 0 .y: its offset in the batch) is
// sent piece by piece, fields of it stepped before each piece. The steps are float additions on fields that
// hold packed integers: the exponents stay fixed, so the mantissas count. (The original alternates between
// the template and a copy of it; every field is set again before each piece, so one copy does.)

struct Grid {
    std::vector<uint8_t> packet;
    void setWord(uint32_t qword, int lane, float value) {
        const size_t at = size_t(qword) * 16 + size_t(lane) * 4;
        if (at + 4 <= packet.size()) std::memcpy(packet.data() + at, &value, 4);
    }
};

Grid gridPacket(const Reader& r) { return {copyPacket(r, r.half(0, 1))}; }

// 0x3D50: rows of columns; per column the word at qword (1 .x) of the packet takes a counter (qword 2 .x,
// stepped by .y per column and .z per row), and eight vertex pairs from qword (1 .y) get their z: a ramp
// from qword 4 .z by 4 .w and one from qword 5 .z by 5 .w (starts and steps times 16 per row).
// Qword 3 .z + 1 columns, .w + 1 rows.
void gridRamps(const Reader& r, PrimitiveSink& sink) {
    Grid g = gridPacket(r);
    const Vec4 counter = r.floats(2), ramp = r.floats(4), second = r.floats(5);
    float field = counter.x, rampStart = ramp.z, rampStep = ramp.w, secondStart = second.z, secondStep = second.w;
    const uint32_t word = r.half(1, 0), firstVertex = r.half(1, 1);
    for (uint32_t row = 0, rows = r.half(3, 3); row <= rows; ++row) {
        float z2 = rampStart, z3 = secondStart;
        for (uint32_t column = 0, columns = r.half(3, 2); column <= columns; ++column) {
            g.setWord(word, 0, field);
            for (uint32_t k = 0, vertex = firstVertex; k < 8; ++k, vertex += 3) {
                g.setWord(vertex + 1, 2, z2);
                z2 = z2 + rampStep;
                g.setWord(vertex + 2, 2, z3);
                z3 = z3 + secondStep;
            }
            field = field + counter.y;
            sink.packet(g.packet.data(), g.packet.size());
        }
        field = field + counter.z;
        rampStart *= 16.0f;
        rampStep *= 16.0f;
        secondStart *= 16.0f;
        secondStep *= 16.0f;
    }
}

// 0x3EA0: qword 2 .z + 1 pieces; before each, four fields (word x of the qwords at the offsets in qword 1
// .x .. .w) take four counters (qwords 2..5 .x), each stepped by its .y.
void gridCounters(const Reader& r, PrimitiveSink& sink) {
    Grid g = gridPacket(r);
    float field[4], step[4];
    for (int k = 0; k < 4; ++k) {
        const Vec4 q = r.floats(2 + k);
        field[k] = q.x;
        step[k] = q.y;
    }
    for (uint32_t piece = 0, pieces = r.half(2, 2); piece <= pieces; ++piece) {
        for (int k = 0; k < 4; ++k) {
            g.setWord(r.half(1, k), 0, field[k]);
            field[k] = field[k] + step[k];
        }
        sink.packet(g.packet.data(), g.packet.size());
    }
}

}  // namespace

Renderers3d::Constants constantsFromRegisters(const uint32_t vf[32][4]) {
    auto reg = [&](int k) { return Vec4{vuFloat(vf[k][0]), vuFloat(vf[k][1]), vuFloat(vf[k][2]), vuFloat(vf[k][3])}; };
    Renderers3d::Constants c;
    for (int k = 0; k < 4; ++k) c.matrix[k] = reg(28 + k);
    c.scale = reg(25);
    c.offset = reg(26);
    c.clamp = reg(24);
    c.guard = reg(23);
    return c;
}

void Renderers3d::endFrame() {
    bool drew = false;
    for (const auto& [key, list] : thisFrame) drew = drew || !list.empty();
    if (!drew) return;   // a VSYNC inside a frame drawn over two (30 frames a second): its objects carry on
    lastFrame.swap(thisFrame);
    if (thisFrame.size() > 50000) thisFrame.clear();   // objects of courses left long ago
    for (auto& [key, list] : thisFrame) list.clear();
    hasPrevious = false;
}

bool Renderers3d::run(const Batch3d& batch, PrimitiveSink& sink, uint32_t& continueAt) {
    const Reader r{*batch.buffer};
    const uint32_t address = batch.entry * 8;
    if (address == 0x3D50 || address == 0x3EA0 || address == 0x3FE0) {   // the screen grids (0x3FE0 just ends)
        if (address == 0x3D50) gridRamps(r, sink);
        else if (address == 0x3EA0) gridCounters(r, sink);
        continueAt = (address == 0x3D50 ? 0x3EA0 : address == 0x3EA0 ? 0x3FE0 : 0x3FF0) / 8;
        ++stats.parts[address];
        ++stats.batches;
        return true;
    }
    Entry entry;
    ChainPlan chain;
    if (!decodeEntry(batch.entry, entry) || (entry.lighting && !(chain = planChain(batch.itop)).known)) {
        ++stats.skipped;
        return false;
    }
    ++stats.batches;
    ++stats.parts[static_cast<uint32_t>(entry.renderer)];
    if (entry.lighting) {
        static const uint32_t unpacks[5] = {0x1908, 0x1878, 0x1810, 0x1990, 0x1A38};
        ++stats.parts[chain.body];
        if (chain.unpack < 5) ++stats.parts[unpacks[chain.unpack]];
    }
    static const std::pair<Renderer, uint32_t> ends[6] = {{Renderer::Strip, 0x2058}, {Renderer::CulledBack, 0x22D0}, {Renderer::CulledFront, 0x22D0},
                                                          {Renderer::R2300, 0x25C0}, {Renderer::R27A8, 0x2A28}, {Renderer::R2AD8, 0x2D58}};
    for (const auto& [kind, end] : ends)
        if (kind == entry.renderer) continueAt = end / 8;

    if (entry.setup) {
        for (int k = 0; k < 4; ++k) constants.matrix[k] = r.floats(kConstants + k);
        constants.scale = r.floats(kConstants + 4);
        constants.offset = r.floats(kConstants + 5);
        constants.clamp = r.floats(kConstants + 6);
        constants.guard = r.floats(kConstants + 7);
        // The object, for its previous transform (motion vectors): what its first batch draws (FNV-1a over the
        // GIF tag, the strips' lengths and the GS state header) and how many setups drew the same before it.
        uint64_t key = 1469598103934665603ull;
        auto mix = [&key](uint32_t w) { key = (key ^ w) * 1099511628211ull; };
        for (const uint32_t w : r.words(kTag)) mix(w);
        const uint32_t strips = stripCount(r);
        mix(strips);
        for (uint32_t k = 0; k < strips && k < 64; ++k) mix(r.half(kSt + 1 + k, 3));
        if (entry.header)
            for (uint32_t q = 0, n = packetQwords(r, kHeader); q < n; ++q)
                for (const uint32_t w : r.words(kHeader + q)) mix(w);
        std::vector<Constants>& seen = thisFrame[key];
        const auto then = lastFrame.find(key);
        hasPrevious = then != lastFrame.end() && then->second.size() > seen.size();
        if (hasPrevious) previous = then->second[seen.size()];
        seen.push_back(constants);
        ++stats.setups;
        if (hasPrevious) ++stats.matched;
    }
    const Tag tag(r.words(kTag));
    // The vertices: the strips' total, and the tag's count, which the lighting chain processes.
    uint32_t total = 0;
    for (uint32_t k = 0, strips = stripCount(r); k < strips && total <= BatchBuffer::kQwords; ++k) total += stripVertices(r, k);
    const uint32_t n = tag.nloop;
    if (total > BatchBuffer::kQwords || n > BatchBuffer::kQwords) {
        ++stats.oversized;
        return true;
    }
    static thread_local Vertices in;
    const uint32_t count = std::max(total, n + 1);   // n + 1: 0x16F8 converts an even count
    in.resize(count);
    in.positions = false;
    in.stLanes = 0;
    in.colourSlot = Vertices::ColourSlot::Given;
    in.colours = 0;
    for (uint32_t k = 0; k < count; ++k) {
        in.position[k] = r.floats(kPositions + k);
        in.st[k] = r.floats(kSt + k);
        in.colour[k] = r.words(kColors + k);
        in.normal[k] = r.floats(kColors + k);
    }
    if (entry.header) {
        const std::vector<uint8_t> header = copyPacket(r, kHeader);
        sink.packet(header.data(), header.size());
    }
    if (entry.lighting && !runChain(r, in, n, chain, stats)) {
        in.writeBack(r, n);
        r.store(kWeight, 0, 0x8000u);   // the empty packet's tag (NLOOP 0, EOP), where the output goes
        continueAt = 0x1810 / 8;        // the fade ended the program
        return true;
    }
    in.writeBack(r, n);
    // The output packet starts at qword 242 with the batch's tag (a later key-frame batch that does not send
    // its weight there reads the tag's last word).
    for (int i = 0; i < 4; ++i) r.store(kWeight, i, tag.words[i]);
    static thread_local std::vector<GsVertex> vertices;
    vertices.clear();
    Output out{tag, vertices};
    const Projector p{constants, hasPrevious ? &previous : nullptr};
    switch (entry.renderer) {
    case Renderer::Strip: stripRenderer(r, p, in, out); break;
    case Renderer::CulledBack: culledRenderer(r, p, in, out, false); break;
    case Renderer::CulledFront: culledRenderer(r, p, in, out, true); break;
    case Renderer::R2300: clippedRenderer(r, p, in, out, Clipped::Plain2300, sink, stats); break;
    case Renderer::R27A8: clippedRenderer(r, p, in, out, Clipped::Back27A8, sink, stats); break;
    case Renderer::R2AD8: clippedRenderer(r, p, in, out, Clipped::Front2AD8, sink, stats); break;
    }
    if (!tag.drawable) {
        ++stats.unexpectedTags;
        return true;
    }
    // The GS reads NLOOP vertices (more than the strips hold would be stale VU memory: never in GT4).
    if (n != vertices.size()) ++stats.countMismatch;
    GsPrimitives prims;
    prims.setPrim = tag.pre;
    prims.prim = tag.prim;
    prims.fog = tag.fog;
    prims.vertices = vertices.data();
    prims.count = std::min<size_t>(n, vertices.size());
    sink.primitives(prims);
    return true;
}

}  // namespace gt4::ps2
