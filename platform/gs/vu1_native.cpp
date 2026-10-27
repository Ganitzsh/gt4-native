// GT4's VU1 microprograms in C++ (see vu1_native.h). Addresses in comments are byte addresses in the
// micro image; "qword N" is TOP + N, the batch the VIF unpacked before MSCAL.
#include "vu1_native.h"

#include "vu_float.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gt4::ps2 {

namespace {

// FNV-1a of GT4's micro image (NTSC-U v1.01), as GT4_VU1_INVENTORY reports it.
constexpr uint64_t kGt4Image = 0x8B6E3B5747FAFFF5ull;

uint64_t fnv(const uint8_t* data, size_t bytes) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t k = 0; k < bytes; ++k) h = (h ^ data[k]) * 0x100000001B3ull;
    return h;
}

// The batch layout every renderer reads (qwords from TOP).
enum : uint32_t {
    kPositions = 0,     // x, y, z (w unused)
    kSt = 64,           // s, t in x, y; the w words hold the strip count (qword 64) and lengths (65...)
    kColors = 128,      // RGBA as four integers
    kTag = 192,         // GIFtag of the output packet
    kConstants = 193,   // the setup's constants (below)
    kHeader = 217,      // optional GIF packet kicked before the strips (GS state), when the entry asks for it
    kOutput = 242,      // the packet the renderers build: tag, then ST, RGBA, XYZF2 per vertex
    kSnapshotQwords = 440,   // what a program may write: the batch (lit in place) and its packet (64 vertices at most)
};

// Renderers, by their address in the micro image.
enum class Renderer : uint16_t { Strip = 0x1EC8, CulledBack = 0x2058, CulledFront = 0x2070, R2300 = 0x2300, R27A8 = 0x27A8, R2AD8 = 0x2AD8 };

struct Entry {
    bool setup;      // calls the setup at 0x0000 first
    bool lighting;   // runs the lighting chain at 0x0048 (ITOP selects the routines)
    bool header;     // kicks the GS state packet at qword 217 before the strips
    Renderer renderer;
};

// The entry table at 0x1B08: 24 groups of five instructions. Group g starts with BAL to the setup; entering
// one instruction later skips it. Groups 0-11 go straight to a renderer, 12-23 through the lighting chain;
// even groups kick the header packet, odd ones do not. Two consecutive groups share a renderer.
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

struct Batch {
    Vu1& vu;
    uint32_t top;   // qword address of the batch

    uint8_t* qword(uint32_t q) const { return vu.data + ((top + q) & 0x3FF) * 16; }
    VuVec load(uint32_t q) const {
        VuVec v;
        std::memcpy(&v, qword(q), 16);
        return v;
    }
    void store(uint32_t q, const VuVec& v) const { std::memcpy(qword(q), &v, 16); }
    uint16_t word(uint32_t q, int lane) const {   // ILW: the low 16 bits of a word
        uint32_t w;
        std::memcpy(&w, qword(q) + lane * 4, 4);
        return static_cast<uint16_t>(w);
    }
    VuVec reg(int index) const {
        VuVec v;
        std::memcpy(&v, &vu.vf[index], 16);
        return v;
    }
};

// Setup (0x0000): the transform and the viewport/fog constants into the registers every renderer reads.
//   vf28..vf31  qwords 193..196  the transform (columns), clip space
//   vf25        qword 197        viewport scale (x, y, z) and fog scale (w)
//   vf26        qword 198        viewport offset (x, y, z) and fog offset (w)
//   vf24        qword 199        clamp for the projected point (x, y, z) and the fog start (w)
//   vf23        qword 200        guard band (read by the clipper)
void setup(Batch& b) {
    static const int registers[8] = {28, 29, 30, 31, 25, 26, 24, 23};
    for (int k = 0; k < 8; ++k) std::memcpy(&b.vu.vf[registers[k]], b.qword(kConstants + k), 16);
}

struct Constants {
    VuVec matrix[4], scale, offset, clamp;
    explicit Constants(const Batch& b) : scale(b.reg(25)), offset(b.reg(26)), clamp(b.reg(24)) {
        for (int k = 0; k < 4; ++k) matrix[k] = b.reg(28 + k);
    }
    // MULAx/MADDAy/MADDAz/MADDw with vf0.w: the position's own w is not used.
    VuVec transform(const VuVec& p) const {
        VuVec r;
        for (int i = 0; i < 4; ++i) r[i] = ((matrix[0][i] * p.x + matrix[1][i] * p.y) + matrix[2][i] * p.z) + matrix[3][i] * kVuOne;
        return r;
    }
};

// One vertex after the projection: the clamped point times 1/w (clip space, before the viewport) and q.
struct Projected {
    VuVec s;   // MINI with vf24, then MULq
    VuF q;     // DIV q, vf0w, vf3w
};

Projected project(const Constants& c, const VuVec& position) {
    const VuVec p = c.transform(position);
    const VuF q = vdiv(kVuOne, p.w);
    return {vmin(p, c.clamp) * q, q};
}

void storeFixed4(Batch& b, uint32_t q, const VuVec& v) {
    uint32_t words[4] = {ftoi4(v.x), ftoi4(v.y), ftoi4(v.z), ftoi4(v.w)};
    std::memcpy(b.qword(q), words, 16);
}

// ADC: the w lane of the viewport offset plus 2048 (I = 0x45000000): FTOI4 then sets bit 15 of the F word,
// the GS's "do not draw the triangle this vertex completes".
VuVec adcOffset(const VuVec& offset) {
    VuVec r = offset * kVuOne;   // MULw vf27, vf26, vf0w
    r.w = r.w + VuF::bits(0x45000000u);
    return r;
}

// Strips the renderers walk: qword 64 .w is the count, qwords 65.. .w the length of each strip as the
// distance in output quadwords (3 per vertex) from its first to its second-to-last vertex: the loops run
// two vertices before the first end test and one after the last. Returns the vertex count of strip k.
uint32_t stripVertices(const Batch& b, uint32_t k) { return static_cast<uint16_t>(b.word(kSt + 1 + k, 3)) / 3u + 2; }

// Renderer 0x1EC8: textured strips, no culling. Per vertex: ST times q, the colour as given, XYZF2 = the
// projected point through the viewport (fog in w); the first two vertices of each strip carry ADC.
void stripRenderer(Batch& b) {
    const Constants c(b);
    const VuVec adc = adcOffset(c.offset);
    uint32_t out = kOutput;
    b.store(out++, b.load(kTag));
    const uint32_t strips = b.word(kSt, 3);
    uint32_t v = 0;
    for (uint32_t k = 0; k < strips; ++k) {
        const uint32_t n = stripVertices(b, k);
        for (uint32_t j = 0; j < n; ++j, ++v) {
            const Projected p = project(c, b.load(kPositions + v));
            VuVec st = {VuF{}, VuF{}, kVuOne, VuF{}};
            const VuVec given = b.load(kSt + v);
            st.x = given.x;
            st.y = given.y;
            b.store(out++, st * p.q);
            b.store(out++, b.load(kColors + v));
            const VuVec& base = j < 2 ? adc : c.offset;
            VuVec xyzf;
            for (int i = 0; i < 4; ++i) xyzf[i] = base[i] * kVuOne + p.s[i] * c.scale[i];   // MULAw then MADD
            storeFixed4(b, out++, xyzf);
        }
    }
}

// Renderers 0x2058 / 0x2070: as 0x1EC8, plus back-face culling. A vertex completes the triangle it forms
// with the two before it; its ADC is set when that triangle faces the wrong way: the z of the cross product
// of the edges old = s[-2] - s[-1] and new = s[-1] - s[0] (projected x, y), whose operand order alternates
// with the vertex's index in its strip (the loop handles two vertices per pass), is tested by its zero and
// sign flags (MAC & 0x22) against 0 (0x2058: keep z > 0) or 0x20 (0x2070: keep z < 0). The first two
// vertices of each strip always carry ADC (the second reads a dummy flag result that always culls).
void culledRenderer(Batch& b, bool front) {
    const Constants c(b);
    const VuVec adc = adcOffset(c.offset);
    const uint32_t keep = front ? 0x20u : 0u;
    uint32_t out = kOutput;
    b.store(out++, b.load(kTag));
    const uint32_t strips = b.word(kSt, 3);
    uint32_t v = 0;
    VuVec s[3];   // projected points of the vertex two before, one before and this one
    for (uint32_t k = 0; k < strips; ++k) {
        const uint32_t n = stripVertices(b, k);
        for (uint32_t j = 0; j < n; ++j, ++v) {
            const Projected p = project(c, b.load(kPositions + v));
            s[0] = s[1];
            s[1] = s[2];
            s[2] = p.s;
            VuVec st = {VuF{}, VuF{}, kVuOne, VuF{}};
            const VuVec given = b.load(kSt + v);
            st.x = given.x;
            st.y = given.y;
            b.store(out++, st * p.q);
            b.store(out++, b.load(kColors + v));
            bool culled = j < 2;
            if (!culled) {
                const VuVec older = s[0] - s[1], newer = s[1] - s[2];
                const VuF z = (j & 1) ? newer.x * older.y - older.x * newer.y : older.x * newer.y - newer.x * older.y;
                uint32_t mac = 0;
                if (!(z.u & 0x7F800000u)) mac |= 0x2u;
                if (z.u & 0x80000000u) mac |= 0x20u;
                culled = mac != keep;
            }
            const VuVec scaled = p.s * c.scale;   // MUL, then ADD with the offset
            storeFixed4(b, out++, scaled + (culled ? adc : c.offset));
        }
    }
}

VuF itof0(uint32_t w) { return {vufp::from_fixed(static_cast<int32_t>(w), 0)}; }
VuF itof12(uint32_t w) { return {vufp::from_fixed(static_cast<int32_t>(w), 12)}; }

uint32_t vertexCount(const Batch& b) { return static_cast<uint16_t>(b.word(kTag, 0) * 2u) / 2u; }   // ILW, then IADD vi9, vi9, vi9

// The word view of a quadword, for the integer lanes (colours, unconverted positions).
struct Words {
    uint32_t w[4];
};
Words loadWords(const Batch& b, uint32_t q) {
    Words r;
    std::memcpy(r.w, b.qword(q), 16);
    return r;
}
void storeLanes(Batch& b, uint32_t q, const VuVec& v, unsigned lanes) {   // lanes: bit 0 = x ... bit 3 = w
    uint8_t* p = b.qword(q);
    for (int i = 0; i < 4; ++i)
        if (lanes & (1u << i)) std::memcpy(p + i * 4, &v[i].u, 4);
}
void storeIntegers(Batch& b, uint32_t q, const VuVec& v) {
    uint32_t words[4] = {ftoi0(v.x), ftoi0(v.y), ftoi0(v.z), ftoi0(v.w)};
    std::memcpy(b.qword(q), words, 16);
}
constexpr unsigned kXyz = 7, kXy = 3, kXyzw = 15;
const VuF kNormalScale = VuF::bits(0x3C010204u);   // 1/127: normals are stored as signed bytes

// --- Renderers with clipping: 0x2300 (no culling), 0x27A8 and 0x2AD8 (back-face culling) ----------------
//
// Per vertex as 0x1EC8 (ADC offset: 2048 for 0x2300, 3072 for the others), plus a clip test for every
// vertex j >= 2 of a strip on its triangle (j-2, j-1, j), made before its XYZF2:
// - no vertex outside the frustum (CLIP of the clip-space point against its w): drawn as it is;
// - all three outside the same plane: not drawn (the ADC offset);
// - else the guard band decides (the points scaled by vf23, CLIP again): inside it the GS clips the
//   triangle itself (drawn as it is); outside, 0x27A8/0x2AD8 first repeat the back-face test in homogeneous
//   coordinates (culled: not drawn), and the rest goes to the polygon clipper at 0x2FA8/0x2FE0, which builds
//   extra packets. That clipper is not ported: such a batch runs in the interpreter.
// 0x27A8/0x2AD8 then cull as 0x2058 (the operand order of 0x2AD8's cross product is swapped): a culled vertex
// gets 3072 added to its fog (ST's w, set to 3072 from the second vertex of the packet on), which sets ADC.

enum class Clipped { Plain2300, Back27A8, Front2AD8 };

// CLIP: bit 0 x > |w|, bit 1 x < -|w|, bits 2-3 y, bits 4-5 z (as the VU compares: flushed bit patterns).
uint32_t clipFlags(const VuVec& s, VuF w) {
    const int32_t limit = vufp::key(w.u & vufp::kAbs);
    uint32_t f = 0;
    for (int i = 0; i < 3; ++i) {
        const int32_t x = vufp::key(s[i].u);
        if (x > limit) f |= 1u << (i * 2);
        if (x < -limit) f |= 2u << (i * 2);
    }
    return f;
}
uint32_t clipFlags(const VuVec& p) { return clipFlags(p, p.w); }

bool macCulls(VuF z) { return !(z.u & 0x7F800000u) || (z.u & 0x80000000u); }   // MAC & 0x22: z zero or negative

// The clipper prelude's back-face test (0x2EA0..0x2F50) in homogeneous (x, y, w) coordinates. `variantB`:
// the operand order of the cross product (the prelude picks it from its return address).
bool homogeneousCulled(const VuVec p[3], const VuVec& guard, bool variantB) {
    const VuVec g = p[0] * guard;
    VuVec a = {p[0].x, p[0].y, VuF{} + g.w, g.w};
    VuVec e1 = p[1] - p[0], e2 = p[2] - p[0];
    e1.z = VuF{} + e1.w;
    e2.z = VuF{} + e2.w;
    VuVec cross = e1;
    if (variantB) {
        cross.x = e1.y * e2.z - e2.y * e1.z;
        cross.y = e1.z * e2.x - e2.z * e1.x;
        cross.z = e1.x * e2.y - e2.x * e1.y;
    } else {
        cross.x = e2.y * e1.z - e1.y * e2.z;
        cross.y = e2.z * e1.x - e1.z * e2.x;
        cross.z = e2.x * e1.y - e1.x * e2.y;
    }
    const VuF px = a.x * cross.x, py = a.y * cross.y;
    const VuF z = ((a.z * cross.z) + kVuOne * px) + kVuOne * py;
    return macCulls(z);
}

// The polygon clipper (0x2FE0..0x3D48): Sutherland-Hodgman against the guard band's six planes, on a ring
// of vertex records, then one strip packet. The records live where the VU keeps them, because the strip
// packet can overrun them: five qwords each (position, ST + flags, colour, plus + next link, minus + previous
// link), the triangle's at 933, 938 and 943, new ones from 948, a dummy at 928 (931 .w: the first record,
// 932 .w: the last). The packet goes to 903 or 878, and from 903 a polygon of nine vertices or more runs into
// 928..: the emit loop then reads what it wrote, which the code below reproduces by reading and writing VU
// memory in the program's order.
struct ClipVertex {
    VuVec pos;           // clip space
    VuVec st, colour;    // ST (x, y used), colour as floats
    VuVec plus, minus;   // the guard-scaled point g plus and minus g.w: the signed distances to the planes
    uint32_t flags = 0;  // CLIP of g against g.w
    uint16_t address = 0;
};

ClipVertex clipVertex(const VuVec& pos, const VuVec& g, const VuVec& st, const VuVec& colour) {
    ClipVertex r;
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

void setWord(Batch& absolute, uint32_t q, int lane, uint32_t value) { std::memcpy(absolute.qword(q) + lane * 4, &value, 4); }

// A record as the VU stores it (its flags word holds 12 bits of CLIP history there; only the low 6 matter).
void storeRecord(Batch& absolute, const ClipVertex& v) {
    absolute.store(v.address, v.pos);
    storeLanes(absolute, v.address + 1u, v.st, kXy);
    setWord(absolute, v.address + 1u, 3, v.flags);
    absolute.store(v.address + 2u, v.colour);
    storeLanes(absolute, v.address + 3u, v.plus, kXyz);
    storeLanes(absolute, v.address + 4u, v.minus, kXyz);
}

// Clips the triangle and writes its strip packet; kicks the packet before it (`pending`: the batch's state
// packet before the first clipped polygon, then the previous polygon). False: the record allocator (never
// reset within the call) would run past qword 1023 and wrap into the batch; the caller leaves such a batch
// to the interpreter.
bool clipPolygon(Batch& b, ClipVertex triangle[3], uint32_t& pending, const VuVec& tag, const VuVec& adc, const Constants& c) {
    Batch m{b.vu, 0};
    static const uint16_t initial[3] = {933, 938, 943};
    for (int k = 0; k < 3; ++k) {
        triangle[k].address = initial[k];
        storeRecord(m, triangle[k]);
    }
    setWord(m, 931, 3, 933);
    setWord(m, 946, 3, 933);
    setWord(m, 936, 3, 938);
    setWord(m, 941, 3, 943);
    std::vector<ClipVertex> ring(triangle, triangle + 3), next;
    uint16_t allocated = 948;
    // Plane bit, which record (plus: the -w side, minus: the +w side) and lane, in the program's order.
    static const struct { uint32_t mask; bool plus; int lane; } planes[6] = {{32, true, 2}, {16, false, 2}, {8, true, 1}, {4, false, 1}, {2, true, 0}, {1, false, 0}};
    const VuVec guard = b.reg(23);
    uint32_t any = ring[0].flags | ring[1].flags | ring[2].flags;
    for (const auto& plane : planes) {
        if (!(any & plane.mask)) continue;
        next.clear();
        any = 0;
        uint16_t previous = 928;
        auto push = [&](const ClipVertex& v) {
            setWord(m, previous + 3u, 3, v.address);   // previous.next
            setWord(m, v.address + 4u, 3, previous);   // v.previous
            previous = v.address;
            next.push_back(v);
            any |= v.flags;
        };
        for (size_t k = 0; k < ring.size(); ++k) {
            const ClipVertex a = ring[k];
            const ClipVertex& e = ring[(k + 1) % ring.size()];
            const bool aOut = (a.flags & plane.mask) != 0, eOut = (e.flags & plane.mask) != 0;
            if (!aOut) push(a);
            if (aOut == eOut) continue;
            // The intersection: (e * dA - a * dE) / (dA - dE), with dA, dE the distances on this plane.
            const VuF da = (plane.plus ? a.plus : a.minus)[plane.lane], de = (plane.plus ? e.plus : e.minus)[plane.lane];
            const VuF q = vdiv(kVuOne, (plane.plus ? a.plus - e.plus : a.minus - e.minus)[plane.lane]);
            VuVec pos, st, colour;
            for (int i = 0; i < 4; ++i) {
                pos[i] = e.pos[i] * da - a.pos[i] * de;
                st[i] = e.st[i] * da - a.st[i] * de;
                colour[i] = e.colour[i] * da - a.colour[i] * de;
            }
            if (allocated + 5u > 1024u) return false;
            ClipVertex v = clipVertex(pos * q, (pos * guard) * q, st * q, colour * q);
            v.address = allocated;
            allocated = static_cast<uint16_t>(allocated + 5);
            storeRecord(m, v);
            push(v);
        }
        setWord(m, 932, 3, previous);   // in the branch's delay slot: written even when the polygon vanished
        ring.swap(next);
        if (ring.size() < 3) return true;   // nothing left
        setWord(m, previous + 3u, 3, ring.front().address);
    }
    // The strip, from both ends of the ring: first, last, second, second to last, ... (reads and writes in
    // the program's order: the packet can overrun the records it reads).
    if (pending) b.vu.kick(pending);
    uint16_t left = m.word(931, 3);
    const uint16_t slot = static_cast<uint16_t>(25 - m.word(870, 1));
    VuVec leftPos = m.load(left);
    setWord(m, 870, 1, slot);
    uint16_t right = m.word(932, 3);
    VuVec leftSt = {VuF{}, VuF{}, (VuF{} - VuF{}) + kVuOne, kVuOne - kVuOne};
    VuVec rightSt = leftSt;
    auto loadSt = [&](VuVec& st, uint16_t record) {
        const VuVec v = m.load(record + 1u);
        st.x = v.x;
        st.y = v.y;
    };
    loadSt(leftSt, left);
    VuVec leftColour = m.load(left + 2u);
    const uint16_t packet = static_cast<uint16_t>(slot + 878);
    VuVec rightPos = m.load(right);
    uint32_t out = packet;
    setWord(m, out, 0, static_cast<uint16_t>(ring.size() | 0x8000u));
    storeLanes(m, out++, tag, 0xE);
    const VuF half = VuF::bits(0x3F000000u);
    // One output qword of a vertex: 0 ST times q, 1 the colour rounded, 2 XYZF2.
    auto emit = [&](const VuVec& pos, const VuVec& st, const VuVec& colour, bool adcBase, int part) {
        const VuF w = vdiv(kVuOne, pos.w);
        if (part == 0) {
            m.store(out++, st * w);
        } else if (part == 1) {
            storeIntegers(m, out++, {colour.x + half, colour.y + half, colour.z + half, colour.w + half});
        } else {
            const VuVec s = vmin(pos, c.clamp) * w;
            const VuVec& base = adcBase ? adc : c.offset;
            VuVec xyzf;
            for (int i = 0; i < 4; ++i) xyzf[i] = base[i] * kVuOne + s[i] * c.scale[i];
            storeFixed4(m, out++, xyzf);
        }
    };
    bool first = true;
    for (;;) {
        const uint16_t emitted = right;
        const uint16_t leftNext = m.word(left + 3u, 3);
        loadSt(rightSt, emitted);
        const VuVec rightColour = m.load(emitted + 2u);
        emit(leftPos, leftSt, leftColour, first, 0);
        const VuVec leftNextPos = m.load(leftNext);
        emit(leftPos, leftSt, leftColour, first, 1);
        bool done = left == emitted;
        emit(leftPos, leftSt, leftColour, first, 2);
        first = false;
        if (done) break;
        left = leftNext;
        leftPos = leftNextPos;
        const uint16_t rightPrevious = m.word(emitted + 4u, 3);
        loadSt(leftSt, left);
        leftColour = m.load(left + 2u);
        emit(rightPos, rightSt, rightColour, false, 0);
        const VuVec rightPreviousPos = m.load(rightPrevious);
        emit(rightPos, rightSt, rightColour, false, 1);
        done = left == emitted;
        emit(rightPos, rightSt, rightColour, false, 2);
        if (done) break;
        right = rightPrevious;
        rightPos = rightPreviousPos;
    }
    pending = packet;
    return true;
}

// `pending`: the packet the next XGKICK sends (the state packet, then the last clipped polygon).
bool clippedRenderer(Batch& b, Clipped kind, uint32_t& pending) {
    const Constants c(b);
    const VuVec guard = b.reg(23);
    const bool culling = kind != Clipped::Plain2300;
    VuVec adc = c.offset * kVuOne;
    adc.w = adc.w + VuF::bits(culling ? 0x45400000u : 0x45000000u);
    const VuF cullFog = VuF::bits(0x45400000u);   // vf9.w
    if (culling) {   // ISW: the packet start for the final XGKICK, and the clipper's packet count
        const uint32_t start = (b.top + kOutput) & 0xFFFF, zero = 0;
        Batch absolute{b.vu, 0};
        std::memcpy(absolute.qword(870), &start, 4);
        std::memcpy(absolute.qword(870) + 4, &zero, 4);
    } else {
        const uint32_t zero = 0;
        Batch absolute{b.vu, 0};
        std::memcpy(absolute.qword(870) + 4, &zero, 4);
    }
    uint32_t out = kOutput;
    b.store(out++, b.load(kTag));
    const uint32_t strips = b.word(kSt, 3);
    uint32_t v = 0;
    VuVec p[3], s[3];   // clip-space and projected points of the vertex two before, one before and this one
    uint32_t flags[3] = {};
    for (uint32_t k = 0; k < strips; ++k) {
        const uint32_t n = stripVertices(b, k);
        for (uint32_t j = 0; j < n; ++j, ++v) {
            p[0] = p[1];
            p[1] = p[2];
            p[2] = c.transform(b.load(kPositions + v));
            flags[0] = flags[1];
            flags[1] = flags[2];
            flags[2] = clipFlags(p[2]);
            const VuF q = vdiv(kVuOne, p[2].w);
            s[0] = s[1];
            s[1] = s[2];
            s[2] = vmin(p[2], c.clamp) * q;
            VuVec st = {VuF{}, VuF{}, kVuOne, (culling && v > 0) ? cullFog : VuF{}};
            const VuVec given = b.load(kSt + v);
            st.x = given.x;
            st.y = given.y;
            b.store(out++, st * q);
            b.store(out++, b.load(kColors + v));
            bool hidden = j < 2;
            if (!hidden && (flags[0] | flags[1] | flags[2])) {
                if (flags[0] & flags[1] & flags[2]) {
                    hidden = true;
                } else if (clipFlags(p[0] * guard) | clipFlags(p[1] * guard) | clipFlags(p[2] * guard)) {
                    const bool variantB = (kind == Clipped::Back27A8) == ((j & 1) == 0);
                    if (!culling || !homogeneousCulled(p, guard, variantB)) {
                        ClipVertex triangle[3];
                        for (uint32_t t = 0; t < 3; ++t) {
                            const Words colour = loadWords(b, kColors + v - 2 + t);
                            triangle[t] = clipVertex(p[t], p[t] * guard, b.load(kSt + v - 2 + t),
                                                     {itof0(colour.w[0]), itof0(colour.w[1]), itof0(colour.w[2]), itof0(colour.w[3])});
                        }
                        if (!clipPolygon(b, triangle, pending, b.load(kTag), adc, c)) return false;
                    }
                    hidden = true;
                }
            }
            const VuVec& base = hidden ? adc : c.offset;
            VuVec xyzf;
            for (int i = 0; i < 4; ++i) xyzf[i] = base[i] * kVuOne + s[2][i] * c.scale[i];   // MULAw, MADD
            if (culling && j >= 2) {
                const VuVec older = s[0] - s[1], newer = s[1] - s[2];
                const bool odd = (j & 1) != (kind == Clipped::Front2AD8);
                const VuF z = odd ? newer.x * older.y - older.x * newer.y : older.x * newer.y - newer.x * older.y;
                if (macCulls(z)) xyzf.w = xyzf.w + cullFog;
            }
            storeFixed4(b, out++, xyzf);
        }
    }
    return true;
}

// --- The lighting chain (0x0048) ---------------------------------------------------------------------
//
// The dispatcher jumps to ITOP, an instruction inside one of 15 blocks. A block is a table of five
// entries (16 bytes each), then its body. Entry i runs one vertex-decompression routine and continues
// with the body; ITOP may also point at the body itself. Every body ends with LQI vf2 (the first position)
// and returns to the renderer. All of them work in place on the batch, for NLOOP vertices (the GIFtag's
// count, qword 192 .x).

// Decompression routines (byte address). Positions are integers; texture coordinates 4.12 fixed point;
// normals bytes scaled to 1/127, in the colour slot (lit models) or the ST slot (untextured ones).
void unpackPositionsSt(Batch& b) {   // 0x1810
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const Words p = loadWords(b, kPositions + k), st = loadWords(b, kSt + k);
        storeLanes(b, kPositions + k, {itof0(p.w[0]), itof0(p.w[1]), itof0(p.w[2]), VuF{}}, kXyz);
        storeLanes(b, kSt + k, {itof12(st.w[0]), itof12(st.w[1]), VuF{}, VuF{}}, kXy);
    }
}
void unpackPositionsStNormals(Batch& b) {   // 0x1878
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const Words p = loadWords(b, kPositions + k), st = loadWords(b, kSt + k), nr = loadWords(b, kColors + k);
        storeLanes(b, kPositions + k, {itof0(p.w[0]), itof0(p.w[1]), itof0(p.w[2]), VuF{}}, kXyz);
        storeLanes(b, kSt + k, {itof12(st.w[0]), itof12(st.w[1]), VuF{}, VuF{}}, kXy);
        storeLanes(b, kColors + k, {itof0(nr.w[0]) * kNormalScale, itof0(nr.w[1]) * kNormalScale, itof0(nr.w[2]) * kNormalScale, VuF{}}, kXyz);
    }
}
void unpackPositionsNormalsInSt(Batch& b) {   // 0x1908
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const Words p = loadWords(b, kPositions + k), nr = loadWords(b, kSt + k);
        storeLanes(b, kPositions + k, {itof0(p.w[0]), itof0(p.w[1]), itof0(p.w[2]), VuF{}}, kXyz);
        storeLanes(b, kSt + k, {itof0(nr.w[0]) * kNormalScale, itof0(nr.w[1]) * kNormalScale, itof0(nr.w[2]) * kNormalScale, VuF{}}, kXyz);
    }
}
// 0x1990 / 0x1A38: blend two key frames 32 quadwords apart by t = qword 242 .w: a * (1 - t) + b * t, for the
// positions (xyz), texture coordinates (xyz) and colours (xyzw; 0x1A38 blends them as integers).
void blendKeyFrames(Batch& b, bool integerColours) {
    const VuF t = b.load(kOutput).w, rest = kVuOne - t;
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        storeLanes(b, kPositions + k, b.load(kPositions + k) * rest + b.load(kPositions + 32 + k) * t, kXyz);
        storeLanes(b, kSt + k, b.load(kSt + k) * rest + b.load(kSt + 32 + k) * t, kXyz);
        if (integerColours) {
            const Words a = loadWords(b, kColors + k), c = loadWords(b, kColors + 32 + k);
            VuVec r;
            for (int i = 0; i < 4; ++i) r[i] = itof0(a.w[i]) * rest + itof0(c.w[i]) * t;
            storeIntegers(b, kColors + k, r);
        } else {
            storeLanes(b, kColors + k, b.load(kColors + k) * rest + b.load(kColors + 32 + k) * t, kXyzw);
        }
    }
}

// Raised to 2^squarings by repeated squaring, as the VU does it.
VuF power(VuF x, int squarings) {
    for (int k = 0; k < squarings; ++k) x = x * x;
    return x;
}

// max(m0 * n.x + m1 * n.y + m2 * n.z, 0) per lane: four N.L terms of the lights in the matrix's columns.
VuVec dots(const VuVec m[3], const VuVec& n) {
    VuVec r;
    for (int i = 0; i < 4; ++i) r[i] = vmax(((m[0][i] * n.x + m[1][i] * n.y) + m[2][i] * n.z), VuF{});
    return r;
}

// The N.L model of most bodies. colour = ambient (qword 204) + c205 * d.x + c206 * d.y + c207 * d.z^p +
// c208 * d.w^p [+ c212..c215 * e^q], clamped to [0, clamp] and stored as integers over the normals
// (colour slot). d: lights of qwords 201..203; e: a second set in 209..211 (then the clamp is qword 216,
// else 209). `squaringsA`: 0 keeps d.z, d.w linear. `nextNormal`: body 0x290 starts its sum with MADDA, so
// the accumulator still holds the first two products of the next vertex's N.L.
struct DotModel {
    int squaringsA;
    bool second;
    int squaringsB;
    bool nextNormal;
};

void dotLighting(Batch& b, const DotModel& model) {
    VuVec a[3], e[3], c[8];
    for (int k = 0; k < 3; ++k) a[k] = b.load(201 + k);
    for (int k = 0; k < 3; ++k) e[k] = b.load(209 + k);
    const VuVec ambient = b.load(204);
    for (int k = 0; k < 4; ++k) c[k] = b.load(205 + k);
    for (int k = 0; k < 4; ++k) c[4 + k] = b.load(212 + k);
    const VuVec clamp = b.load(model.second ? 216 : 209);
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const VuVec normal = b.load(kColors + k);
        VuVec d = dots(a, normal), f{};
        d.z = power(d.z, model.squaringsA);
        d.w = power(d.w, model.squaringsA);
        if (model.second) {
            f = dots(e, normal);
            for (int i = 0; i < 4; ++i) f[i] = power(f[i], model.squaringsB);
        }
        const VuVec next = b.load(kColors + k + 1);
        VuVec colour;
        for (int i = 0; i < 4; ++i) {
            VuF acc = model.nextNormal ? (a[0][i] * next.x + a[1][i] * next.y) + ambient[i] * kVuOne : ambient[i] * kVuOne;
            acc = acc + c[0][i] * d.x;
            acc = acc + c[1][i] * d.y;
            acc = acc + c[2][i] * d.z;
            acc = acc + c[3][i] * d.w;
            if (model.second)
                for (int j = 0; j < 4; ++j) acc = acc + c[4 + j][i] * f[j];
            colour[i] = vmin(vmax(acc, VuF{}), clamp[i]);
        }
        storeIntegers(b, kColors + k, colour);
    }
}

// 0x0EA0: specular from the eye. v = position - eye (qword 204), r = v - 2 (v.n) n (the reflection, its
// products in the program's order), s = max(r . L * rsqrt(v.v), 0)^16 with L = (201.x, 202.x, 203.x);
// colour = c205 * s, alpha 128.
void specularLighting(Batch& b) {
    const VuVec eye = b.load(204), coefficient = b.load(205);
    const VuF lx = b.load(201).x, ly = b.load(202).x, lz = b.load(203).x;
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const VuVec position = b.load(kPositions + k), normal = b.load(kColors + k);
        const VuF vx = position.x - eye.x, vy = position.y - eye.y, vz = position.z - eye.z;
        const VuF tx = vx * normal.x, ty = vy * normal.y, tz = vz * normal.z;
        const VuF twoX = tx + tx, twoY = ty + ty, twoZ = tz + tz;
        const VuF rx = ((vx * kVuOne - normal.x * twoX) - normal.x * twoY) - normal.x * twoZ;
        const VuF ry = ((vy * kVuOne - normal.y * twoX) - normal.y * twoY) - normal.y * twoZ;
        const VuF rz = ((vz * kVuOne - normal.z * twoX) - normal.z * twoY) - normal.z * twoZ;
        const VuF inverseLength{vufp::rsqrt(kVuOne.u, (((vx * vx) + (vy * vy)) + (vz * vz)).u).value};
        const VuF s = power(vmax((((lx * rx) + (ly * ry)) + (lz * rz)) * inverseLength, VuF{}), 4);
        const VuVec colour = {coefficient.x * s, coefficient.y * s, coefficient.z * s, kVuOne + VuF::bits(0x42FE0000u)};
        storeIntegers(b, kColors + k, colour);
    }
}

// 0x10A8: reflective paint on models whose normals sit in the ST slot. v = position - eye (qword 204),
// d = v . n, e = ERLENG(v):
// - colour = itof(colour) * (c205 + c206 * min(d, 0) * e): darker where the surface turns away;
// - ST = a sphere map: r = (v - n d - n d) rotated by qwords 201..203 and scaled by e, k = 0.5 / |r + (0, 0, 1)|
//   (RSQRT), ST = (0.5 + r.x k, 0.5 - r.y k).
void reflectiveLighting(Batch& b) {
    const VuVec eye = b.load(204), base = b.load(205), rim = b.load(206);
    const VuVec m[3] = {b.load(201), b.load(202), b.load(203)};
    const VuF half = VuF{} + VuF::bits(0x3F000000u);
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const VuVec position = b.load(kPositions + k), normal = b.load(kSt + k);
        const VuF vx = position.x - eye.x, vy = position.y - eye.y, vz = position.z - eye.z;
        const VuF d = (vx * normal.x + vy * normal.y) + vz * normal.z;
        const VuF e{vufp::efu_length(vx.u, vy.u, vz.u, 3)};
        const VuF t = vmin(d, VuF{}) * e;
        const Words given = loadWords(b, kColors + k);
        VuVec colour;
        for (int i = 0; i < 4; ++i) colour[i] = itof0(given.w[i]) * (base[i] * kVuOne + rim[i] * t);
        storeIntegers(b, kColors + k, colour);
        const VuF ox = normal.x * d, oy = normal.y * d, oz = normal.z * d;
        const VuF rx = (vx * kVuOne - ox) - ox, ry = (vy * kVuOne - oy) - oy, rz = (vz * kVuOne - oz) - oz;
        const VuF sx = ((m[0].x * rx + m[1].x * ry) + m[2].x * rz) * e;
        const VuF sy = ((m[0].y * rx + m[1].y * ry) + m[2].y * rz) * e;
        const VuF sz = ((m[0].z * rx + m[1].z * ry) + m[2].z * rz) * e + kVuOne;
        const VuF scale{vufp::rsqrt(half.u, (((sx * sx) + (sy * sy)) + (sz * sz)).u).value};
        storeLanes(b, kSt + k, {sx * scale + half, half - sy * scale, VuF{}, VuF{}}, kXy);
    }
}

// 0x0CA8: as 0x0EA0, with the reflection normalised before the dot product and a lower power:
// s = max(L . (r * rsqrt(v.v)), 0)^8.
void specularNormalisedLighting(Batch& b) {
    const VuVec eye = b.load(204), coefficient = b.load(205);
    const VuF lx = b.load(201).x, ly = b.load(202).x, lz = b.load(203).x;
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const VuVec position = b.load(kPositions + k), normal = b.load(kColors + k);
        const VuF vx = position.x - eye.x, vy = position.y - eye.y, vz = position.z - eye.z;
        const VuF tx = vx * normal.x, ty = vy * normal.y, tz = vz * normal.z;
        const VuF twoX = tx + tx, twoY = ty + ty, twoZ = tz + tz;
        const VuF inverseLength{vufp::rsqrt(kVuOne.u, (((vx * vx) + (vy * vy)) + (vz * vz)).u).value};
        const VuF rx = (((vx * kVuOne - normal.x * twoX) - normal.x * twoY) - normal.x * twoZ) * inverseLength;
        const VuF ry = (((vy * kVuOne - normal.y * twoX) - normal.y * twoY) - normal.y * twoZ) * inverseLength;
        const VuF rz = (((vz * kVuOne - normal.z * twoX) - normal.z * twoY) - normal.z * twoZ) * inverseLength;
        const VuF s = power(vmax(((lx * rx) + (ly * ry)) + (lz * rz), VuF{}), 3);
        storeIntegers(b, kColors + k, {coefficient.x * s, coefficient.y * s, coefficient.z * s, kVuOne + VuF::bits(0x42FE0000u)});
    }
}

// 0x12C8: an environment map by quadrant (normals in the ST slot). The colour is 0x10A8's; the reflection
// r (v - n d - n d, rotated by qwords 201..203) picks a quadrant by the signs of r.x - r.z and r.x + r.z (the
// program reads them as MAC flags), u = quadrant (0, 2, 4, 6) + a / b with (a, b) = (r.z, r.x) or (-r.x, r.z),
// v = t^3 + 2t with t = r.y / |v|; ST = (202.w + 201.w u, 204.w + 203.w v).
void quadrantMapLighting(Batch& b) {
    const VuVec eye = b.load(204), base = b.load(205), rim = b.load(206);
    const VuVec m[3] = {b.load(201), b.load(202), b.load(203)};
    const VuF uScale = VuF{} + m[0].w, uOffset = VuF{} + m[1].w, vScale = VuF{} + m[2].w, vOffset = VuF{} + eye.w;
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const VuVec position = b.load(kPositions + k), normal = b.load(kSt + k);
        const VuF vx = position.x - eye.x, vy = position.y - eye.y, vz = position.z - eye.z;
        const VuF d = ((vx * normal.x) + (vy * normal.y)) + (vz * normal.z);
        const VuF e{vufp::efu_length(vx.u, vy.u, vz.u, 3)};
        const Words given = loadWords(b, kColors + k);
        const VuF t0 = vmin(d, VuF{}) * e;
        VuVec colour;
        for (int i = 0; i < 4; ++i) colour[i] = itof0(given.w[i]) * (base[i] * kVuOne + rim[i] * t0);
        storeIntegers(b, kColors + k, colour);
        const VuF ox = normal.x * d, oy = normal.y * d, oz = normal.z * d;
        const VuF rx = (vx * kVuOne - ox) - ox, ry = (vy * kVuOne - oy) - oy, rz = (vz * kVuOne - oz) - oz;
        const VuF sx = (m[0].x * rx + m[1].x * ry) + m[2].x * rz;
        const VuF sy = (m[0].y * rx + m[1].y * ry) + m[2].y * rz;
        const VuF sz = (m[0].z * rx + m[1].z * ry) + m[2].z * rz;
        const bool a = ((sx - sz).u & 0x80000000u) != 0, c = ((sx + sz).u & 0x80000000u) != 0;
        VuF numerator, denominator;
        uint32_t quadrant;
        if (a == c) {
            numerator = VuF{} + sz;
            denominator = VuF{} + sx;
            quadrant = a ? 0x40800000u : 0;   // 4 or 0
        } else {
            numerator = VuF{} - sx;
            denominator = VuF{} + sz;
            quadrant = a ? 0x40000000u : 0x40C00000u;   // 2 or 6
        }
        const VuF u = (VuF{} + VuF::bits(quadrant)) + vdiv(numerator, denominator);
        const VuF t = e * sy;
        const VuF v = ((t * t) * t) + (t + t);
        storeLanes(b, kSt + k, {uOffset * kVuOne + uScale * u, vOffset * kVuOne + vScale * v, VuF{}, VuF{}}, kXy);
    }
}

// 0x1608: a planar projection: colour = c205, ST = c204 + c201 x + c202 y + c203 z.
void planarMapLighting(Batch& b) {
    const VuVec m[3] = {b.load(201), b.load(202), b.load(203)}, offset = b.load(204), colour = b.load(205);
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const VuVec p = b.load(kPositions + k);
        storeIntegers(b, kColors + k, colour);
        VuVec st;
        for (int i = 0; i < 2; ++i) st[i] = ((offset[i] * kVuOne + m[0][i] * p.x) + m[1][i] * p.y) + m[2][i] * p.z;
        storeLanes(b, kSt + k, st, kXy);
    }
}

// 0x1508: prelit colours: clamp(c203 + itof(colour) * c201, 0, c202).
void modulateColours(Batch& b) {
    const VuVec scale = b.load(201), clamp = b.load(202), offset = b.load(203);
    for (uint32_t k = 0, n = vertexCount(b); k < n; ++k) {
        const Words given = loadWords(b, kColors + k);
        VuVec colour;
        for (int i = 0; i < 4; ++i) colour[i] = vmin(vmax(offset[i] * kVuOne + itof0(given.w[i]) * scale[i], VuF{}), clamp[i]);
        storeIntegers(b, kColors + k, colour);
    }
}

// 0x16F8: fade. A per-object table (qword 203 + the offset in qword 202 .x) gives the alpha, 0..255. Zero:
// the program kicks an empty packet and ends, so the batch draws nothing (returns false). Otherwise
// colour = min(itof(colour) * (c201.xyz, alpha / 255), 255), two vertices per pass (an odd count converts
// one vertex more). Qword 242 .x is set to 0x8000 (an empty, final GIFtag) either way.
bool fadeColours(Batch& b) {
    const uint32_t offset = b.word(202, 0);
    const uint32_t table = (b.top + offset) & 0xFFFF;   // IADD vi1, vi1, vi2
    Batch t{b.vu, 0};
    const uint32_t alphaWord = loadWords(t, table + 203).w[0];
    const uint16_t enabled = t.word(table + 203, 0);
    const uint32_t empty = 0x8000;
    std::memcpy(b.qword(kOutput), &empty, 4);
    if (!enabled) return false;
    const VuVec scale = b.load(201);
    const VuF limit = VuF::bits(0x437F0000u);   // 255
    const VuF alpha = (kVuOne * VuF::bits(0x3B808081u)) * itof0(alphaWord);
    const uint32_t n = vertexCount(b), converted = (n + 1) & ~1u;
    for (uint32_t k = 0; k < converted; ++k) {
        const Words given = loadWords(b, kColors + k);
        const VuVec colour = {vmin(itof0(given.w[0]) * scale.x, limit), vmin(itof0(given.w[1]) * scale.y, limit),
                              vmin(itof0(given.w[2]) * scale.z, limit), vmin(itof0(given.w[3]) * alpha, limit)};
        storeIntegers(b, kColors + k, colour);
    }
    return true;
}

// What ITOP selects: a decompression routine (0..4: 0x1908, 0x1878, 0x1810, 0x1990, 0x1A38; 5: none) and a
// body (its address). `ported` is false when a part has no native version yet.
struct ChainPlan {
    bool ported = false;
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
    default: return plan;   // not ported (or ITOP outside the blocks)
    }
    plan.ported = true;
    return plan;
}

// Runs a ported chain. False: the body ended the program after kicking an empty packet (0x16F8).
bool runChain(Batch& b, const ChainPlan& plan) {
    switch (plan.unpack) {
    case 0: unpackPositionsNormalsInSt(b); break;   // 0x1908
    case 1: unpackPositionsStNormals(b); break;     // 0x1878
    case 2: unpackPositionsSt(b); break;            // 0x1810
    case 3: blendKeyFrames(b, false); break;        // 0x1990
    case 4: blendKeyFrames(b, true); break;         // 0x1A38
    default: break;
    }
    if (plan.dot) dotLighting(b, plan.model);
    else if (plan.body == 0xCA8) specularNormalisedLighting(b);
    else if (plan.body == 0xEA0) specularLighting(b);
    else if (plan.body == 0x12C8) quadrantMapLighting(b);
    else if (plan.body == 0x1608) planarMapLighting(b);
    else if (plan.body == 0x10A8) reflectiveLighting(b);
    else if (plan.body == 0x1508) modulateColours(b);
    else if (plan.body == 0x16F8) return fadeColours(b);
    return true;
}

// --- Screen grids: 0x3D50 and 0x3EA0 ---------------------------------------------------------------
//
// Full-screen meshes (post effects), kicked piece by piece. Qword 0 of the batch: .y the offset of a GIF
// packet template from TOP, .z its length in qword pairs, .w which buffer goes first. The program copies the
// template after the batch (qword 242) and kicks the template and the copy alternately (as the VU keeps
// both addresses in four lanes of a register it rotates), patching fields in place before each kick. The
// patches step packed fields with float additions: the exponents stay fixed, so the mantissas count.

struct GridBuffers {
    uint16_t lanes[4];
    uint32_t next = 0;
    uint32_t take() { return lanes[next++ & 3]; }
    void store(Vu1& vu, int reg) const {   // the register the VU leaves (MFIR: sign-extended)
        for (int i = 0; i < 4; ++i) vu.vf[reg].s[i] = static_cast<int16_t>(lanes[(next + i) & 3]);
    }
};

GridBuffers gridCopy(Batch& b) {
    const uint16_t tmpl = static_cast<uint16_t>(b.top + b.word(0, 1)), copy = static_cast<uint16_t>(b.top + kOutput);
    GridBuffers buffers{{tmpl, copy, tmpl, copy}};
    if (b.word(0, 3)) buffers = {{copy, tmpl, copy, tmpl}};
    Batch absolute{b.vu, 0};
    uint16_t from = tmpl, to = copy, pairs = b.word(0, 2);
    do {   // the loop tests after the decrement: a count of 0 copies 65536 pairs, as the VU would
        for (int k = 0; k < 2; ++k) std::memcpy(absolute.qword(to++), absolute.qword(from++), 16);
    } while (--pairs);
    return buffers;
}

// 0x3D50: rows of columns; per column, word x of the qword at the template's offset (qword 1 .x) takes
// the counter in qword 2 .x (stepped by .y per column, .z per row), and eight vertex pairs from qword 1 .y
// get their z: a ramp from qword 4 .z by 4 .w and one from qword 5 .z by 5 .w (both steps and starts are
// scaled by 16 per row). Qword 3 .z + 1 columns, .w + 1 rows.
void gridRamps(Batch& b) {
    GridBuffers buffers = gridCopy(b);
    Batch absolute{b.vu, 0};
    const VuVec counter = b.load(2), ramp = b.load(4), second = b.load(5);
    VuF field = counter.x, rampStart = ramp.z, rampStep = ramp.w, secondStart = second.z, secondStep = second.w;
    const VuF sixteen = VuF::bits(0x41800000u);
    uint16_t rows = b.word(3, 3);
    for (;;) {
        VuF z2 = rampStart, z3 = VuF{} + secondStart;   // MOVE.z; ADD.z with vf0
        uint16_t columns = b.word(3, 2);
        for (;;) {
            const uint16_t buffer = static_cast<uint16_t>(buffers.take());
            const uint16_t word = static_cast<uint16_t>(b.word(1, 0) + buffer);
            uint16_t vertex = static_cast<uint16_t>(b.word(1, 1) + buffer);
            std::memcpy(absolute.qword(word), &field.u, 4);
            for (int k = 0; k < 8; ++k, vertex += 3) {
                std::memcpy(absolute.qword(vertex + 1) + 8, &z2.u, 4);
                z2 = z2 + rampStep;
                std::memcpy(absolute.qword(vertex + 2) + 8, &z3.u, 4);
                z3 = z3 + secondStep;
            }
            field = field + counter.y;
            b.vu.kick(buffer);
            if (!columns--) break;
        }
        field = field + counter.z;
        rampStart = rampStart * sixteen;
        rampStep = rampStep * sixteen;
        secondStart = secondStart * sixteen;
        secondStep = secondStep * sixteen;
        if (!rows--) break;
    }
    buffers.store(b.vu, 22);
}

// 0x3EA0: qword 2 .z + 1 kicks; before each, four fields (word x of the qwords at the template offsets in
// qword 1 .x .. .w) take four counters (qwords 2..5 .x), each stepped by its .y.
void gridCounters(Batch& b) {
    GridBuffers buffers = gridCopy(b);
    Batch absolute{b.vu, 0};
    VuF field[4], step[4];
    for (int k = 0; k < 4; ++k) {
        const VuVec q = b.load(2 + k);
        field[k] = q.x;
        step[k] = q.y;
    }
    uint16_t kicks = b.word(2, 2);
    for (;;) {
        const uint16_t buffer = static_cast<uint16_t>(buffers.take());
        for (int k = 0; k < 4; ++k) {
            std::memcpy(absolute.qword(static_cast<uint16_t>(b.word(1, k) + buffer)), &field[k].u, 4);
            field[k] = field[k] + step[k];
        }
        b.vu.kick(buffer);
        if (!kicks--) break;
    }
    buffers.store(b.vu, 7);
}

}  // namespace

bool Vu1Native::run(Vu1& vu, uint32_t pc) {
    if (!imageKnown) {
        gt4Image = fnv(vu.micro, sizeof vu.micro) == kGt4Image;
        imageKnown = true;
    }
    if (gt4Image && (pc * 8 == 0x3D50 || pc * 8 == 0x3EA0 || pc * 8 == 0x3FE0)) {
        Batch b{vu, vu.top};
        if (pc * 8 == 0x3D50) gridRamps(b);
        else if (pc * 8 == 0x3EA0) gridCounters(b);
        vu.tpc = (pc * 8 == 0x3D50 ? 0x3EA0 : pc * 8 == 0x3EA0 ? 0x3FE0 : 0x3FF0) / 8;   // 0x3FE0 just ends
        ++stats.parts[pc * 8];
        ++stats.native;
        return true;
    }
    Entry entry;
    if (!gt4Image || !decodeEntry(pc, entry)) {
        ++stats.interpreted;
        return false;
    }
    ChainPlan chain;
    if (entry.lighting) chain = planChain(vu.itop);
    if (entry.lighting && !chain.ported) {
        ++stats.interpreted;
        return false;
    }
    Batch b{vu, vu.top};
    // A renderer that clips can still give up (the clipper's record allocator overflowing, see clipPolygon):
    // keep the batch area and qword 870 to undo the run, and hold its packets until it completes.
    struct Held final : GifSink {
        GifSink* next = nullptr;
        std::vector<std::vector<uint8_t>> packets;
        void gif(uint32_t path, const uint8_t* data, size_t bytes) override {
            if (path == 1) packets.emplace_back(data, data + bytes);
            else if (next) next->gif(path, data, bytes);
        }
    };
    static uint8_t saved[kSnapshotQwords * 16], savedStart[16];
    Held held;
    const bool clipping = entry.renderer == Renderer::R2300 || entry.renderer == Renderer::R27A8 || entry.renderer == Renderer::R2AD8;
    if (clipping) {
        for (uint32_t q = 0; q < kSnapshotQwords; ++q) std::memcpy(saved + q * 16, b.qword(q), 16);
        std::memcpy(savedStart, vu.data + 870 * 16, 16);
        held.next = vu.sink();
        vu.setGifSink(&held);
    }
    ++stats.parts[static_cast<uint32_t>(entry.renderer)];
    if (entry.lighting) {
        static const uint32_t unpacks[5] = {0x1908, 0x1878, 0x1810, 0x1990, 0x1A38};
        ++stats.parts[chain.body];
        if (chain.unpack < 5) ++stats.parts[unpacks[chain.unpack]];
    }
    if (entry.setup) setup(b);
    if (entry.lighting && !runChain(b, chain)) {   // an empty batch: the program ends in the lighting body
        if (clipping) vu.setGifSink(held.next);
        if (entry.header) vu.kick(b.top + kHeader);
        vu.kick(b.top + kOutput);
        vu.tpc = 0x1810 / 8;
        ++stats.native;
        return true;
    }
    bool complete = true;
    uint32_t pending = entry.header ? (b.top + kHeader) & 0xFFFF : 0;   // vi14
    switch (entry.renderer) {
    case Renderer::Strip: stripRenderer(b); break;
    case Renderer::CulledBack: culledRenderer(b, false); break;
    case Renderer::CulledFront: culledRenderer(b, true); break;
    case Renderer::R2300: complete = clippedRenderer(b, Clipped::Plain2300, pending); break;
    case Renderer::R27A8: complete = clippedRenderer(b, Clipped::Back27A8, pending); break;
    case Renderer::R2AD8: complete = clippedRenderer(b, Clipped::Front2AD8, pending); break;
    }
    if (!complete) {   // undo; the interpreter runs the whole program (setup and lighting included)
        for (uint32_t q = 0; q < kSnapshotQwords; ++q) std::memcpy(b.qword(q), saved + q * 16, 16);
        std::memcpy(vu.data + 870 * 16, savedStart, 16);
        vu.setGifSink(held.next);
        ++stats.clipperFallbacks;
        ++stats.interpreted;
        return false;
    }
    if (pending) vu.kick(pending);
    vu.kick(b.top + kOutput);
    if (clipping) {
        vu.setGifSink(held.next);
        if (held.next)
            for (const auto& packet : held.packets) held.next->gif(1, packet.data(), packet.size());
    }
    static const std::pair<Renderer, uint32_t> ends[6] = {{Renderer::Strip, 0x2058}, {Renderer::CulledBack, 0x22D0}, {Renderer::CulledFront, 0x22D0},
                                                          {Renderer::R2300, 0x25C0}, {Renderer::R27A8, 0x2A28}, {Renderer::R2AD8, 0x2D58}};
    for (const auto& [r, end] : ends)
        if (r == entry.renderer) vu.tpc = end / 8;   // after the E-bit pair (MSCNT would continue there)
    ++stats.native;
    return true;
}

}  // namespace gt4::ps2
