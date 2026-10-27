// GT4's VU1 microprograms in C++ (see vu1_native.h). Addresses in comments are byte addresses in the
// micro image; "qword N" is TOP + N, the batch the VIF unpacked before MSCAL.
#include "vu1_native.h"

#include "vu_float.h"

#include <cstring>

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

}  // namespace

bool Vu1Native::run(Vu1& vu, uint32_t pc) {
    if (!imageKnown) {
        gt4Image = fnv(vu.micro, sizeof vu.micro) == kGt4Image;
        imageKnown = true;
    }
    Entry entry;
    if (!gt4Image || !decodeEntry(pc, entry) || entry.lighting) {
        ++stats.interpreted;
        return false;
    }
    if (entry.renderer != Renderer::Strip && entry.renderer != Renderer::CulledBack && entry.renderer != Renderer::CulledFront) {
        ++stats.interpreted;
        return false;
    }
    Batch b{vu, vu.top};
    if (entry.setup) setup(b);
    switch (entry.renderer) {
    case Renderer::Strip: stripRenderer(b); break;
    case Renderer::CulledBack: culledRenderer(b, false); break;
    case Renderer::CulledFront: culledRenderer(b, true); break;
    default: break;
    }
    if (entry.header) vu.xgkick(b.top + kHeader);
    vu.xgkick(b.top + kOutput);
    vu.tpc = (static_cast<uint32_t>(entry.renderer) == 0x1EC8 ? 0x2058 : 0x22D0) / 8;   // after the E-bit pair
    ++stats.native;
    return true;
}

}  // namespace gt4::ps2
