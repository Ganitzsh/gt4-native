// GT4_DIFFTEST=NAME[,NAME...]|all (a gt4_game linked with `game_link.py --eeorig`): runs the native code and the
// original code of game functions on the same random inputs and compares everything they write and return; logs a
// line per test and exits (code 1 if any differs, unless tools/eeref/known.txt lists the function: an understood
// difference, reported as `known`; GT4_DIFFTEST_KNOWN=FILE reads another list). For native rewrites of functions no
// recorded scene reaches (docs/NATIVE_ONLY.md): each test below knows its function's arguments, the memory it reads
// and writes, and the shared vectors it reads. The buffers live in a scratch area of guest RAM the game has not used
// yet (the test runs before the game starts).
#include "difftest.h"

#include "gs/vif1.h"
#include "gt4/gfx.h"
#include "gt4/unpack.h"
#include "guest.h"
#include "image.h"
#include "log.h"
#include "shared_state.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" std::uint64_t gt4_orig_run_kinds(const std::uint32_t *args, std::uint32_t addr, const char *kinds, char ret);

namespace difftest {

std::uint64_t g_rng = 0x2545F4914F6CDD1Dull;
std::uint32_t rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return static_cast<std::uint32_t>(g_rng >> 16);
}
float rndf(float lo, float hi) { return lo + (hi - lo) * static_cast<float>(rnd() % 1000001) / 1000000.0f; }
std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
std::uint8_t *ram(std::uint32_t a) { return static_cast<std::uint8_t *>(gt4::rt::guest_ptr(a)); }
float *ramf(std::uint32_t a) { return reinterpret_cast<float *>(ram(a)); }
void fill(std::uint32_t at, std::uint32_t bytes) {
    for (std::uint32_t k = 0; k < bytes; ++k) ram(at)[k] = static_cast<std::uint8_t>(rnd());
}
void fill_floats(std::uint32_t at, std::uint32_t count, float lo, float hi) {
    for (std::uint32_t k = 0; k < count; ++k) ramf(at)[k] = rndf(lo, hi);
}
void random_vectors(int first, int last, float lo, float hi) {
    for (int r = first; r <= last; ++r)
        for (int k = 0; k < 4; ++k) gt4_shared_vectors[r][k] = rndf(lo, hi);
}
float *vec(int n) { return gt4_shared_vectors[n]; }

std::vector<Test> &tests() {
    static std::vector<Test> all;
    return all;
}
void add(const Test &t) { tests().push_back(t); }

}  // namespace difftest

using namespace difftest;

namespace {

void *native_fn(std::uint32_t addr) {
    for (std::uint32_t k = 0; k < gt4_native_function_count; ++k)
        if (gt4_native_functions[k].addr == addr) return gt4_native_functions[k].fn;
    return nullptr;
}

using Fn8w = std::uint64_t (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                               std::uint32_t, std::uint32_t);
using Fn8 = std::uint32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                              std::uint32_t, std::uint32_t);
struct Ret16 {
    std::uint32_t w[4];
};
// gcc 2.9's i386 code returns a 16-byte structure through a hidden first argument it pops itself
typedef Ret16(__attribute__((callee_pop_aggregate_return(1))) * Fn8Ret16)(std::uint32_t, std::uint32_t, std::uint32_t,
                                                                         std::uint32_t, std::uint32_t, std::uint32_t,
                                                                         std::uint32_t, std::uint32_t);


// image byte operations of the movie player (001C1850 ...): (dst, src[, src2], width, height[, value])
void image_mask(std::uint32_t a[8]) {
    a[0] = kScratch;
    a[1] = kScratch + 0x40000;
    a[2] = 1 + rnd() % 80;   // width
    a[3] = 1 + rnd() % 12;   // height
    a[4] = rnd();            // value (its low byte)
    fill(kScratch, 0x80000);
}
void image_two(std::uint32_t a[8]) {
    a[0] = kScratch;
    a[1] = kScratch + 0x40000;
    a[2] = kScratch + 0x60000;
    a[3] = 1 + rnd() % 80;
    a[4] = 1 + rnd() % 12;
    fill(kScratch, 0x80000);
}
void image_one(std::uint32_t a[8]) {
    a[0] = kScratch;
    a[1] = kScratch + 0x40000;
    a[2] = 1 + rnd() % 80;
    a[3] = 1 + rnd() % 12;
    fill(kScratch, 0x80000);
}

// 003E21D8 (out, in): in through rows 20..22. Unaligned three-float points.
void rotate_point(std::uint32_t a[8]) {
    fill(kScratch, 0x100);
    a[0] = kScratch + 0x10 + 4 * (rnd() % 4);
    a[1] = kScratch + 0x40 + 4 * (rnd() % 4);
    fill_floats(a[1], 3, -1000.0f, 1000.0f);
    random_vectors(20, 22, -2.0f, 2.0f);
}
// 003B7C98 (cam, view, screen, in, radius, near): a point through the current matrix, projected and tested.
void project_point(std::uint32_t a[8]) {
    fill(kScratch, 0x2400);
    a[0] = kScratch;   // the camera: m21B0, m21C4, m21D0, m21D4
    ramf(kScratch + 0x21B0)[0] = rndf(0.5f, 3.0f);
    ramf(kScratch + 0x21C4)[0] = rndf(0.5f, 3.0f);
    ramf(kScratch + 0x21D0)[0] = rndf(-0.5f, 0.5f);
    ramf(kScratch + 0x21D4)[0] = rndf(-0.5f, 0.5f);
    a[1] = kScratch + 0x2300;
    a[2] = kScratch + 0x2320;
    a[3] = kScratch + 0x2340 + 4 * (rnd() % 4);
    fill_floats(a[3], 3, -100.0f, 100.0f);
    a[4] = bits(rndf(0.0f, 20.0f));    // radius
    a[5] = bits(rndf(1.0f, 400.0f));   // near
    random_vectors(28, 31, -1.5f, 1.5f);
}

// a double's bits: mostly ordinary values, some zeros, infinities, NaNs, denormals and huge or tiny exponents
std::uint64_t random_double() {
    const std::uint32_t pick = rnd() % 16;
    const std::uint64_t sign = static_cast<std::uint64_t>(rnd() & 1) << 63;
    const std::uint64_t mant = (static_cast<std::uint64_t>(rnd()) << 32 | rnd()) & 0xFFFFFFFFFFFFFull;
    if (pick == 0) return sign;                                   // zero
    if (pick == 1) return sign | 0x7FF0000000000000ull;          // infinity
    if (pick == 2) return sign | 0x7FF0000000000000ull | (mant | 1);   // NaN
    if (pick == 3) return sign | mant;                            // denormal
    if (pick == 4) return sign | (static_cast<std::uint64_t>(1 + rnd() % 2046) << 52) | mant;   // any exponent
    return sign | (static_cast<std::uint64_t>(1023 - 40 + rnd() % 80) << 52) | mant;          // near 1
}
void one_double(std::uint32_t a[8]) {
    const std::uint64_t x = random_double();
    a[0] = static_cast<std::uint32_t>(x);
    a[1] = static_cast<std::uint32_t>(x >> 32);
}

const Register r0(Test{"001C1850", 0x001C1850, "iiiii", 's', false, image_mask});
const Register r1(Test{"001C18B0", 0x001C18B0, "iiiii", 'v', false, image_mask});
const Register r2(Test{"001C1C50", 0x001C1C50, "iiiii", 'v', false, image_two});
const Register r3(Test{"001C1CA8", 0x001C1CA8, "iiii", 'v', false, image_one});
const Register r4(Test{"003E21D8", 0x003E21D8, "ii", 'v', true, rotate_point});
const Register r5(Test{"003B7C98", 0x003B7C98, "iiiiff", 'i', true, project_point});
const Register r7(Test{"0057FB98", 0x0057FB98, "w", 'w', false, one_double});

// within float rounding (1e-5 relative), or within the drift of a long computation (1e-3 relative, 1e-5 absolute:
// the EE truncates where x87 rounds, and sums that cancel make the difference relative to the result large)
bool within(std::uint32_t a, std::uint32_t b, float rel, float abs) {
    float x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return std::fabs(x - y) <= rel * std::fmax(std::fabs(x), std::fabs(y)) + abs;
}
bool close(std::uint32_t a, std::uint32_t b) { return within(a, b, 1e-5f, 1e-30f); }
bool drift(std::uint32_t a, std::uint32_t b) { return within(a, b, 1e-3f, 1e-5f); }

// What a trial compares: the scratch area, the matrix stack (the original's VU0 data memory: the bridge hands it to
// the original code as such), the scratchpad and the test's own region of game data, as bytes (floats within
// tolerance when the test says so); then shared vectors 8..31, the matrix stack's top and the random state, the state
// the original carried between functions in VU0's registers (vectors 1..7 are temporaries a rewrite need not keep).
struct Region {
    const char *name;
    std::uint8_t *at;
    std::uint32_t addr, size;   // addr: the address reported
};

struct Snapshot {
    std::vector<std::uint8_t> mem[4];
    float vectors[32][4];
    std::uint16_t top;
    std::uint32_t random;
    void take(const Region *regions) {
        for (int r = 0; r < 4; ++r) mem[r].assign(regions[r].at, regions[r].at + regions[r].size);
        std::memcpy(vectors, gt4_shared_vectors, sizeof vectors);
        top = gt4_matrix_stack_top;
        random = gt4_random_state;
    }
    void put(const Region *regions) const {
        for (int r = 0; r < 4; ++r) std::memcpy(regions[r].at, mem[r].data(), regions[r].size);
        std::memcpy(gt4_shared_vectors, vectors, sizeof vectors);
        gt4_matrix_stack_top = top;
        gt4_random_state = random;
    }
};

// What a packet delivers (Test::stream): the batches it runs (each with its buffer as the renderer would read it)
// and the two batch buffers at the end, through the host's VIF1 decoder, which takes the original's VIF codes and the
// native's batch records alike; the vertices it draws, from the original's GIF data (DIRECT) and the native's draw
// records alike; and the other native records, as bytes (the original code calls native functions too, which send
// records). `problem`: what the packet holds that this cannot compare.
struct Delivered {
    struct Run {
        std::uint32_t entry, itop, top, buffer;
        std::vector<std::uint32_t> words;
    };
    struct Vertex {   // the GS's view: PRIM, then the registers a vertex kick takes
        std::uint32_t prim, xy, z, fog_kick, rgba, q, s, t, uv;   // fog_kick: fog | 0x100 when the vertex draws
    };
    std::vector<Run> runs;
    std::vector<std::uint32_t> buffers;
    std::vector<Vertex> vertices;
    std::vector<std::vector<std::uint8_t>> records;   // each: the batch runs before it (4 bytes), then the record
    std::uint32_t cursor = 0, start = 0, end = 0;     // the cursor's address, the packet from start to end
    bool holds(std::uint32_t a) const { return a == cursor || (a >= start && a < end); }
    std::string problem;
};

// The GIF data a packet sends through DIRECT, as GS register writes: a vertex at each XYZF2/XYZ2 (drawing) and
// XYZF3/XYZ3 (not drawing) with the PRIM, RGBAQ, ST, UV and FOG values set before it.
struct Draws {
    Delivered &d;
    Delivered::Vertex cur{};
    explicit Draws(Delivered &out) : d(out) {
        const float one = 1.0f;
        std::memcpy(&cur.q, &one, 4);
    }
    void problem(const char *what) {
        if (d.problem.empty()) d.problem = what;
    }
    void reg(std::uint32_t r, std::uint64_t v) {
        const std::uint32_t lo = static_cast<std::uint32_t>(v), hi = static_cast<std::uint32_t>(v >> 32);
        switch (r) {
        case 0x00: cur.prim = lo & 0x7FF; break;                       // PRIM
        case 0x01: cur.rgba = lo; cur.q = hi; break;                   // RGBAQ
        case 0x02: cur.s = lo; cur.t = hi; break;                      // ST
        case 0x03: cur.uv = lo & 0x3FFF3FFF; break;                    // UV
        case 0x04: case 0x0C:                                          // XYZF2, XYZF3
            cur.xy = lo;
            cur.z = hi & 0xFFFFFF;
            cur.fog_kick = (hi >> 24) | (r == 0x04 ? 0x100u : 0u);
            d.vertices.push_back(cur);
            break;
        case 0x05: case 0x0D:                                          // XYZ2, XYZ3
            cur.xy = lo;
            cur.z = hi;
            cur.fog_kick = (cur.fog_kick & 0xFF) | (r == 0x05 ? 0x100u : 0u);
            d.vertices.push_back(cur);
            break;
        case 0x0A: cur.fog_kick = (cur.fog_kick & 0x100) | (hi >> 24); break;   // FOG
        case 0x0F: break;                                                       // NOP
        default: {
            char why[64];
            std::snprintf(why, sizeof why, "GIF register %02x (not compared)", r);
            problem(why);
        }
        }
    }
    void gif(std::uint32_t, const std::uint8_t *p, std::size_t bytes) {
        for (std::size_t at = 0; at + 16 <= bytes;) {
            std::uint64_t tag[2];
            std::memcpy(tag, p + at, 16);
            at += 16;
            const std::uint32_t nloop = tag[0] & 0x7FFF, flg = (tag[0] >> 58) & 3;
            const std::uint32_t nreg = ((tag[0] >> 60) & 0xF) ? static_cast<std::uint32_t>((tag[0] >> 60) & 0xF) : 16;
            if (flg == 0 && ((tag[0] >> 46) & 1)) reg(0x00, (tag[0] >> 47) & 0x7FF);   // PRE (PACKED only): PRIM
            auto desc = [&](std::uint32_t k) { return static_cast<std::uint32_t>(tag[1] >> (4 * k)) & 0xF; };
            if (flg == 1) {   // REGLIST: 64 bits a register
                const std::size_t values = std::size_t(nloop) * nreg;
                if (at + values * 8 > bytes) return problem("a GIF REGLIST past its data");
                for (std::size_t k = 0; k < values; ++k) {
                    std::uint64_t v;
                    std::memcpy(&v, p + at + k * 8, 8);
                    reg(desc(static_cast<std::uint32_t>(k % nreg)), v);
                }
                at += (values * 8 + 15) & ~std::size_t(15);
            } else if (flg == 0) {   // PACKED: a qword a register
                if (at + std::size_t(nloop) * nreg * 16 > bytes) return problem("a GIF PACKED past its data");
                for (std::uint32_t l = 0; l < nloop; ++l)
                    for (std::uint32_t k = 0; k < nreg; ++k, at += 16) {
                        std::uint32_t w[4];
                        std::memcpy(w, p + at, 16);
                        const std::uint32_t r = desc(k);
                        const bool noKick = (w[3] >> 15) & 1;   // ADC
                        switch (r) {
                        case 0x00: reg(0x00, w[0]); break;
                        case 0x01:
                            reg(0x01, (w[0] & 0xFF) | (w[1] & 0xFF) << 8 | (w[2] & 0xFF) << 16 | (w[3] & 0xFFu) << 24 |
                                          std::uint64_t(cur.q) << 32);
                            break;
                        case 0x02: cur.q = w[2]; reg(0x02, w[0] | std::uint64_t(w[1]) << 32); break;
                        case 0x03: reg(0x03, (w[0] & 0x3FFF) | (w[1] & 0x3FFF) << 16); break;
                        case 0x04:
                            reg(noKick ? 0x0C : 0x04, (w[0] & 0xFFFF) | (w[1] & 0xFFFF) << 16 |
                                                          std::uint64_t((w[2] >> 4) & 0xFFFFFF) << 32 |
                                                          std::uint64_t((w[3] >> 4) & 0xFF) << 56);
                            break;
                        case 0x05:
                            reg(noKick ? 0x0D : 0x05,
                                (w[0] & 0xFFFF) | (w[1] & 0xFFFF) << 16 | std::uint64_t(w[2]) << 32);
                            break;
                        case 0x0A: reg(0x0A, std::uint64_t((w[3] >> 4) & 0xFF) << 56); break;
                        case 0x0E: reg(w[2] & 0xFF, w[0] | std::uint64_t(w[1]) << 32); break;   // A+D
                        default: reg(r, 0);
                        }
                    }
            } else {
                return problem("a GIF IMAGE transfer (not compared)");
            }
        }
    }
    // A native draw record (include/gt4/gfx.h DrawRecord): the same registers, set as its flags say.
    void draw(const gt4::gfx::DrawRecord &r, const std::uint8_t *vertices) {
        namespace gfx = gt4::gfx;
        if (r.flags & gfx::kDrawPrim) cur.prim = r.prim & 0x7FF;
        for (std::uint32_t k = 0; k < r.count; ++k) {
            gfx::Vertex x;
            std::memcpy(&x, vertices + k * sizeof x, sizeof x);
            if (r.flags & gfx::kDrawColor) {
                cur.rgba = x.rgba;
                std::memcpy(&cur.q, &x.q, 4);
            }
            if (r.flags & gfx::kDrawSt) {
                std::memcpy(&cur.s, &x.s, 4);
                std::memcpy(&cur.t, &x.t, 4);
                std::memcpy(&cur.q, &x.q, 4);
            }
            if (r.flags & gfx::kDrawUv) cur.uv = x.u | std::uint32_t(x.v) << 16;
            cur.xy = x.x | std::uint32_t(x.y) << 16;
            cur.z = r.flags & gfx::kDrawFog ? x.z & 0xFFFFFF : x.z;
            cur.fog_kick = (r.flags & gfx::kDrawFog ? x.fog : cur.fog_kick & 0xFF) | (x.kick ? 0x100u : 0u);
            if (!(r.flags & gfx::kDrawAttributesOnly)) d.vertices.push_back(cur);
        }
    }
};

bool readable(std::uint32_t at, std::uint32_t bytes) {
    if (at >= 0x70000000u && at < 0x70004000u) return bytes <= 0x70004000u - at;   // the scratchpad
    return gt4::rt::in_guest_ram(at, bytes);
}

std::vector<std::uint32_t> batch_words(const gt4::ps2::BatchBuffer &b) {
    return std::vector<std::uint32_t>(&b.words[0][0], &b.words[0][0] + gt4::ps2::BatchBuffer::kQwords * 4);
}

// The packet from `start` to the cursor as a DMA chain of CNT and REF tags (END and REFE end it), each tag's upper
// half and its data taken as VIF1 codes: DIRECT's GIF data to Draws, native records to their decoders, and every
// other command to the host's batch decoder (Vif1, one command at a time through batchModel, which decodes UNPACK,
// the state codes, ITOP and MSCAL), starting as the frame head leaves VIF1 (Vif1::batchFrame).
Delivered deliver(const Test &t, std::uint32_t cursor, std::uint32_t start, const char *side, bool raw_gif = false) {
    namespace gfx = gt4::gfx;
    Delivered d;
    d.cursor = cursor;
    d.start = start;
    std::memcpy(&d.end, ram(cursor), 4);
    char why[160];
    auto problem = [&](const char *what) {
        if (d.problem.empty()) d.problem = what;
    };
    gt4::ps2::Vif1 v;
    Draws draws(d);
    v.batchFrame();
    if (t.stream_mask) std::memcpy(&v.mask, ram(t.stream_mask), 4);
    v.onBatch = [&](const gt4::ps2::Batch3d &b) {
        d.runs.push_back({b.entry, b.itop, b.top, b.buffer == &v.buffers[1] ? 1u : 0u, batch_words(*b.buffer)});
    };
    auto records = [&](const std::uint8_t *p, std::size_t bytes) {
        for (std::size_t at = 0; at + sizeof(gfx::Header) <= bytes;) {
            gfx::Header h;
            std::memcpy(&h, p + at, sizeof h);
            const std::uint8_t *payload = p + at + sizeof h;
            if (h.bytes() > bytes - at - sizeof h) {
                problem("a record runs past its block");
                return;
            }
            switch (h.type()) {
            case gfx::kNop: break;
            case gfx::kBatchFrame: v.batchFrame(); break;
            case gfx::kBatchWrite: {
                gfx::BatchWrite w;
                if (h.bytes() < sizeof w) return problem("a short batch write record");
                std::memcpy(&w, payload, sizeof w);
                if (h.bytes() < sizeof w + std::size_t(w.count) * 16) return problem("a short batch write record");
                std::vector<std::uint32_t> words(std::size_t(w.count) * 4);
                if (w.count) std::memcpy(words.data(), payload + sizeof w, words.size() * 4);
                v.batchWrite(w.qword, words.data(), w.count, w.lanes);
                break;
            }
            case gfx::kBatchRun: {
                gfx::BatchRun r;
                if (h.bytes() < sizeof r) return problem("a short batch run record");
                std::memcpy(&r, payload, sizeof r);
                v.batchRun(r.entry, r.itop);
                break;
            }
            case gfx::kDraw: {
                gfx::DrawRecord r;
                if (h.bytes() < sizeof r) return problem("a short draw record");
                std::memcpy(&r, payload, sizeof r);
                if (h.bytes() < sizeof r + std::size_t(r.count) * sizeof(gfx::Vertex))
                    return problem("a short draw record");
                draws.draw(r, payload + sizeof r);
                break;
            }
            default: {   // compared as bytes, padding included (ef5764a zeroes it), with the batches run before it
                const std::uint32_t runs = static_cast<std::uint32_t>(d.runs.size());
                std::vector<std::uint8_t> r(4 + sizeof h + h.bytes());
                std::memcpy(r.data(), &runs, 4);
                std::memcpy(r.data() + 4, p + at, sizeof h + h.bytes());
                d.records.push_back(std::move(r));
            }
            }
            at += std::size_t(gfx::recordQwords(h.bytes())) * 16;
        }
    };
    std::vector<std::uint32_t> codes;   // the VIF1 words along the chain
    auto take = [&](std::uint32_t from, std::uint32_t bytes) {
        const std::size_t old = codes.size();
        codes.resize(old + bytes / 4);
        std::memcpy(codes.data() + old, ram(from), bytes / 4 * 4);
    };
    std::uint32_t at = start;
    if (raw_gif) {   // the original's GIF data, inside a DIRECT its caller opened (Test::stream_gif)
        if (d.end < start || !readable(start, d.end - start)) problem("the GIF data does not end at the cursor");
        else draws.gif(2, ram(start), d.end - start);
        at = d.end;
    }
    for (int tags = 0; d.problem.empty() && at != d.end; ++tags) {
        if (tags == 4096 || at > d.end || !readable(at, 16)) {
            std::snprintf(why, sizeof why, "the DMA chain from %08x does not end at the cursor (%08x)", start, d.end);
            problem(why);
            break;
        }
        std::uint32_t tag[4];
        std::memcpy(tag, ram(at), 16);
        const std::uint32_t qwc = tag[0] & 0xFFFF, id = (tag[0] >> 28) & 7;
        const std::uint32_t addr = tag[1] & 0x80000000u ? 0x70000000u | (tag[1] & 0x3FF0) : tag[1] & 0x0FFFFFF0u;
        take(at + 8, 8);   // the tag's upper half: VIF codes
        const bool here = id == 1 || id == 7;   // CNT, END: the data follows the tag; REF, REFS, REFE: at addr
        if (!here && id != 3 && id != 4 && id != 0) {
            std::snprintf(why, sizeof why, "DMA tag %08x at %08x (only CNT, REF, REFS, END, REFE are followed)",
                          tag[0], at);
            problem(why);
            break;
        }
        const std::uint32_t data = here ? at + 16 : addr;
        if (!readable(data, qwc * 16)) {
            std::snprintf(why, sizeof why, "DMA tag at %08x: %u qwords at %08x outside guest memory", at, qwc, data);
            problem(why);
            break;
        }
        take(data, qwc * 16);
        at += here ? 16 + qwc * 16 : 16;
        if ((id == 7 || id == 0) && at != d.end) problem("the DMA chain ends before the cursor");
        if (id == 7 || id == 0) break;
    }
    for (std::size_t i = 0; d.problem.empty() && i < codes.size();) {
        const std::uint32_t code = codes[i], cmd = (code >> 24) & 0x7F, imm = code & 0xFFFF;
        std::size_t words = 1;   // the command's, its data included
        if (cmd >= 0x60) words += gfx::unpackWords(cmd, (code >> 16) & 0xFF ? (code >> 16) & 0xFF : 256, v.cl, v.wl);
        else if (cmd == 0x20) words = 2;                                   // STMASK
        else if (cmd == 0x30 || cmd == 0x31) words = 5;                    // STROW, STCOL
        else if (cmd == 0x50 || cmd == 0x51) words += (imm ? imm : 65536) * 4;   // DIRECT, DIRECTHL
        else if (cmd == 0x08) words += std::size_t(imm) * 4;               // native records (include/gt4/gfx.h)
        else if (cmd == 0x4A) {
            problem("an MPG (not compared)");
            break;
        }
        if (i + words > codes.size()) {
            std::snprintf(why, sizeof why, "VIF code %08x runs past the packet", code);
            problem(why);
            break;
        }
        const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(codes.data() + i);
        if (cmd == 0x50 || cmd == 0x51) draws.gif(2, bytes + 4, (words - 1) * 4);
        else if (cmd == 0x08) records(bytes + 4, (words - 1) * 4);
        else v.batchModel(bytes, words * 4, v.mask);   // keeps the mask as it is (STMASK in it changes it)
        i += words;
    }
    if (v.stats.unknownCodes) problem("unknown VIF codes");
    if (v.stats.outsideBatch) problem("unpacks outside both batch buffers");
    // GT4_DIFFTEST_DUMP=1: the packets of a test's first two trials that cannot be compared, in the log (=all: the
    // first two trials' packets)
    static const char *dumping = nullptr;
    static int dumps = 0;
    const char *dump = getenv("GT4_DIFFTEST_DUMP");
    if (dumping != t.name) {
        dumping = t.name;
        dumps = dump ? 4 : 0;
    }
    if ((!d.problem.empty() || (dump && !std::strcmp(dump, "all"))) && dumps > 0 && dumps--) {
        std::string words;
        const std::uint32_t stop = d.end > start && d.end - start < 0x400 ? d.end : start + 0x100;
        for (std::uint32_t a = start & ~3u; a < stop && readable(a, 4); a += 4) {
            std::uint32_t w;
            std::memcpy(&w, ram(a), 4);
            std::snprintf(why, sizeof why, "%s%08x", (a - start) % 16 ? " " : "\n  ", w);
            words += why;
        }
        gt4::rt::log_info("difftest %s: %s packet %08x..%08x (%s):%s", t.name, side, start, d.end, d.problem.c_str(),
                          words.c_str());
    }
    for (const gt4::ps2::BatchBuffer &b : v.buffers) {
        const std::vector<std::uint32_t> w = batch_words(b);
        d.buffers.insert(d.buffers.end(), w.begin(), w.end());
    }
    return d;
}

bool run(const Test &t, int trials) {
    if (t.double_size && t.double_size < 8) {   // a region of doubles holds at least one
        gt4::rt::log_error("difftest %s: double_size %u is under 8 bytes", t.name, t.double_size);
        return false;
    }
    void *fn = t.native ? t.native : native_fn(t.addr);
    if (!fn) {
        gt4::rt::log_error("difftest %s: no native function", t.name);
        return false;
    }
    const Region regions[4] = {
        {"scratch", ram(kScratch), kScratch, kScratchSize},
        {"matrix stack", reinterpret_cast<std::uint8_t *>(gt4_matrix_stack), 0, sizeof gt4_matrix_stack},
        {"scratchpad", ram(0x70000000), 0x70000000, 0x4000},
        {"data", t.data_size ? ram(t.data_addr) : nullptr, t.data_addr, t.data_size},
    };
    Snapshot before, native, original;
    int differ = 0, rounding = 0, drifted = 0;
    for (int n = 0; n < trials; ++n) {
        std::uint32_t args[8] = {};
        t.setup(args);
        before.take(regions);
        const std::uint32_t native_cursor = t.stream_native ? t.stream_native : t.stream;
        std::uint32_t start = 0, native_start = 0;
        if (t.stream) {
            std::memcpy(&start, ram(t.stream), 4);
            std::memcpy(&native_start, ram(native_cursor), 4);
        }
        Delivered native_out, orig_out;
        std::uint64_t native_ret = 0;
        if (t.ret == 'w')
            native_ret = reinterpret_cast<Fn8w>(fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        else if (t.ret == 's')
            reinterpret_cast<Fn8Ret16>(fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        else
            native_ret = reinterpret_cast<Fn8>(fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        native.take(regions);
        if (t.stream) native_out = deliver(t, native_cursor, native_start, "native");
        before.put(regions);
        std::uint64_t orig_ret = gt4_orig_run_kinds(args, t.addr, t.kinds, t.ret == 's' ? 'v' : t.ret);
        original.take(regions);
        if (t.stream) orig_out = deliver(t, t.stream, start, "original", t.stream_gif);
        // the packets themselves and the cursors are not compared (each side's packet and cursor, in both runs'
        // memory): what they deliver is, below
        auto packet = [&](std::uint32_t a) { return native_out.holds(a) || orig_out.holds(a); };
        if (t.ret != 'w') {
            native_ret &= 0xFFFFFFFFu;
            orig_ret &= 0xFFFFFFFFu;
        }
        bool same = t.ret == 'v' || t.ret == 's' || native_ret == orig_ret ||
                    (t.ret == 'f' && t.floats && drift(static_cast<std::uint32_t>(native_ret), static_cast<std::uint32_t>(orig_ret)));
        const char *where = "";
        std::uint32_t at = 0, na = 0, oa = 0;
        auto word = [&](const char *region, std::uint32_t addr, std::uint32_t x, std::uint32_t y, bool exact = false) {
            if (x == y) return;
            if (t.floats && !exact && close(x, y)) {
                ++rounding;
                return;
            }
            if (t.floats && !exact && drift(x, y)) {
                ++drifted;
                return;
            }
            if (same) {
                where = region;
                at = addr;
                na = x;
                oa = y;
            }
            same = false;
        };
        for (int r = 0; r < 4; ++r)
            for (std::uint32_t k = 0; k < regions[r].size; k += 4) {
                const std::uint32_t a = regions[r].addr + k;
                if (t.stream && packet(a)) continue;
                if (t.double_size && a >= t.double_addr && a - t.double_addr <= t.double_size - 8 &&
                    ((a - t.double_addr) & 7) == 0 && k + 8 <= regions[r].size) {
                    std::uint64_t xb, yb;   // one double: compared whole, not as two unrelated words
                    std::memcpy(&xb, &native.mem[r][k], 8);
                    std::memcpy(&yb, &original.mem[r][k], 8);
                    if (xb != yb) {
                        double x, y;
                        std::memcpy(&x, &xb, 8);
                        std::memcpy(&y, &yb, 8);
                        const double m = std::fmax(std::fabs(x), std::fabs(y));
                        if (t.floats && std::fabs(x - y) <= 1e-12 * m) {
                            ++rounding;
                        } else if (t.floats && std::fabs(x - y) <= 1e-6 * m + 1e-9) {
                            ++drifted;
                        } else {
                            if (same) {
                                where = regions[r].name;
                                at = a;
                                na = static_cast<std::uint32_t>(xb >> 32);
                                oa = static_cast<std::uint32_t>(yb >> 32);
                            }
                            same = false;
                        }
                    }
                    k += 4;
                    continue;
                }
                std::uint32_t x, y;
                std::memcpy(&x, &native.mem[r][k], 4);
                std::memcpy(&y, &original.mem[r][k], 4);
                word(regions[r].name, regions[r].addr + k, x, y);
            }
        for (int v = 8; v < 32; ++v)
            for (int k = 0; k < 4; ++k)
                if (!(v == t.loose_vector && (t.loose_lanes & (1 << k))))
                    word("vector", static_cast<std::uint32_t>(v * 4 + k), bits(native.vectors[v][k]), bits(original.vectors[v][k]));
        word("matrix stack top", 0, native.top, original.top, true);
        word("random state", 0, native.random, original.random, true);
        std::string stream_note;
        if (t.stream) {
            if (!native_out.problem.empty() || !orig_out.problem.empty()) {
                stream_note = "; packet not compared: native " +
                              (native_out.problem.empty() ? "-" : native_out.problem) + ", original " +
                              (orig_out.problem.empty() ? "-" : orig_out.problem);
                same = false;
            } else {
                word("batch runs", 0, static_cast<std::uint32_t>(native_out.runs.size()),
                     static_cast<std::uint32_t>(orig_out.runs.size()), true);
                for (std::size_t i = 0; same && i < native_out.runs.size() && i < orig_out.runs.size(); ++i) {
                    const Delivered::Run &x = native_out.runs[i], &y = orig_out.runs[i];
                    const std::uint32_t id = static_cast<std::uint32_t>(i) << 16;   // reported: run << 16 | word
                    word("run entry", id, x.entry, y.entry, true);
                    word("run itop", id, x.itop, y.itop, true);
                    word("run top", id, x.top, y.top, true);
                    word("run buffer", id, x.buffer, y.buffer, true);
                    for (std::size_t k = 0; k < x.words.size(); ++k)
                        word("run's batch", id | static_cast<std::uint32_t>(k), x.words[k], y.words[k]);
                }
                for (std::size_t k = 0; k < native_out.buffers.size(); ++k)   // buffer << 10 | qword << 2 | lane
                    word("batch buffers", static_cast<std::uint32_t>(k), native_out.buffers[k], orig_out.buffers[k]);
                word("vertices", 0, static_cast<std::uint32_t>(native_out.vertices.size()),
                     static_cast<std::uint32_t>(orig_out.vertices.size()), true);
                for (std::size_t i = 0; same && i < native_out.vertices.size() && i < orig_out.vertices.size(); ++i) {
                    const Delivered::Vertex &x = native_out.vertices[i], &y = orig_out.vertices[i];
                    const std::uint32_t id = static_cast<std::uint32_t>(i);
                    word("vertex PRIM", id, x.prim, y.prim, true);
                    // a projected position or depth one unit apart (1/16 pixel) is rounding when `floats`: the VU's
                    // arithmetic against x87's before the conversion to fixed point
                    auto off_by_one = [](std::uint32_t a, std::uint32_t b) { return (a > b ? a - b : b - a) <= 1; };
                    const bool xy1 = off_by_one(x.xy & 0xFFFF, y.xy & 0xFFFF) && off_by_one(x.xy >> 16, y.xy >> 16);
                    if (x.xy != y.xy && t.floats && xy1) ++rounding;
                    else word("vertex XY", id, x.xy, y.xy, true);
                    if (x.z != y.z && t.floats && off_by_one(x.z, y.z)) ++rounding;
                    else word("vertex Z", id, x.z, y.z, true);
                    word("vertex fog, kick", id, x.fog_kick, y.fog_kick, true);
                    word("vertex RGBA", id, x.rgba, y.rgba, true);
                    word("vertex Q", id, x.q, y.q);
                    word("vertex S", id, x.s, y.s);
                    word("vertex T", id, x.t, y.t);
                    word("vertex UV", id, x.uv, y.uv, true);
                }
                word("records", 0, static_cast<std::uint32_t>(native_out.records.size()),
                     static_cast<std::uint32_t>(orig_out.records.size()), true);
                for (std::size_t i = 0; same && i < native_out.records.size() && i < orig_out.records.size(); ++i) {
                    const std::vector<std::uint8_t> &x = native_out.records[i], &y = orig_out.records[i];
                    word("record size", static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(x.size()),
                         static_cast<std::uint32_t>(y.size()), true);
                    for (std::size_t k = 0; same && k + 4 <= x.size() && k + 4 <= y.size(); k += 4) {
                        std::uint32_t a, b;
                        std::memcpy(&a, &x[k], 4);
                        std::memcpy(&b, &y[k], 4);
                        word("record", static_cast<std::uint32_t>(i << 16 | k), a, b, true);
                    }
                }
            }
        }
        if (!same && ++differ <= 3)
            gt4::rt::log_warn("difftest %s trial %d (args %08x %08x %08x %08x): return native %016llx original %016llx; first "
                              "differing word: %s %08x native %08x original %08x%s",
                              t.name, n, args[0], args[1], args[2], args[3], static_cast<unsigned long long>(native_ret),
                              static_cast<unsigned long long>(orig_ret), where, at, na, oa, stream_note.c_str());
    }
    if (t.teardown) t.teardown();
    gt4::rt::log_info("difftest %s: %d trials, %d differ (%d words within float rounding, %d within 1e-3)", t.name, trials,
                      differ, rounding, drifted);
    return differ == 0;
}

}  // namespace

// The understood differences (tools/eeref/known.txt, which replay.py reads too): address -> why. GT4_DIFFTEST_KNOWN,
// else the known.txt beside this source (game_link.py compiles it by its full path, so the run needs no particular
// working directory), else tools/eeref/known.txt under the working directory.
std::map<std::uint32_t, std::string> known_differences() {
    std::map<std::uint32_t, std::string> out;
    std::string path;
    std::FILE *f = nullptr;
    if (getenv("GT4_DIFFTEST_KNOWN")) {
        path = getenv("GT4_DIFFTEST_KNOWN");
        f = std::fopen(path.c_str(), "r");
    } else {
        path = __FILE__;
        const std::size_t slash = path.find_last_of("/\\");
        path = slash == std::string::npos ? "known.txt" : path.substr(0, slash + 1) + "known.txt";
        f = std::fopen(path.c_str(), "r");
        if (!f) {
            path = "tools/eeref/known.txt";
            f = std::fopen(path.c_str(), "r");
        }
    }
    if (!f) {
        gt4::rt::log_warn("difftest: no %s; every difference counts", path.c_str());
        return out;
    }
    gt4::rt::log_info("difftest: known differences from %s", path.c_str());
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        unsigned addr = 0;
        int used = 0;
        if (line[0] == '#' || std::sscanf(line, "%8x%n", &addr, &used) != 1 || used != 8) continue;
        std::string why = line + used;
        why.erase(0, why.find_first_not_of(" \t"));
        why.erase(why.find_last_not_of("\r\n") + 1);
        out[addr] = why;
    }
    std::fclose(f);
    return out;
}

// From gt4_orig_install: runs the tests GT4_DIFFTEST names ("all" for every one) and exits.
extern "C" void gt4_difftest(const char *names) {
    const int trials = getenv("GT4_DIFFTEST_TRIALS") ? atoi(getenv("GT4_DIFFTEST_TRIALS")) : 200;
    const std::string list = std::string(",") + names + ",";
    const std::map<std::uint32_t, std::string> known = known_differences();
    bool ok = true;
    for (const Test &t : tests()) {
        if (list != ",all," && list.find(std::string(",") + t.name + ",") == std::string::npos) continue;
        if (run(t, trials)) continue;
        const auto k = known.find(t.addr);
        if (k != known.end() && !t.native) {   // another native version is never the one known.txt describes
            gt4::rt::log_info("difftest %s: known (tools/eeref/known.txt): %s", t.name, k->second.c_str());
            continue;
        }
        ok = false;
    }
    std::exit(ok ? 0 : 1);
}
