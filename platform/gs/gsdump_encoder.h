#pragma once
// The front end's GS dump (PLAN.md C12 step 3, GT4_GSDUMP=PATH,FROM,COUNT): the host's GS input, in the front end's
// order, as the GIF a PS2's GS would have taken, written by GsDumpWriter as a PCSX2 .gs dump that PCSX2's player renders.
// Most of that input arrives as records, not GIF, so each record becomes GIF here: a state record's groups and the
// transfer registers as A+D; vertices as REGLIST, each register's whole value (RGBAQ with the vertex's own Q,
// XYZ2/XYZF2 or XYZ3/XYZF3 by the kick bit); uploads and texture sets as image transfers; GS -> host reads as their
// transfer and a read; the CPU renderers' primitives (GT4_GPU_MODELS=0) as REGLIST too. Raw GIF passes through as it
// came. Every transfer on path 3 but raw GIF's own (PCSX2's GT4 dumps put all of theirs on path 3: lane C).
//
// The encoder sees every record from the run's start and keeps a register shadow (a GsDumpState), so a dump's starting
// registers are what the stream set; before FROM it only updates the shadow. At the VBlank FROM (a VBlank boundary,
// never inside a frame) the dump opens with the shadow and the GS memory the caller gives (the drawn targets read back
// where the host can, else the front end's memory, said in the summary), then writes until COUNT VSyncs. Not dumped,
// counted: the GPU's 3D batches (model jobs), the native effect passes (kGrade), uploads whose bytes weren't there.
// Counted too: draws that depth-test against a Z buffer not drawn since FROM, which a dump's start memory doesn't hold
// (no Z in it, PLAN.md B25/B26), so the dump isn't exact for them. No GPU.
#include "gsdump_writer.h"
#include "gs_primitives.h"
#include "gt4/gfx.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace gt4::gs {

class GsDumpEncoder {
public:
    struct Options {
        std::string path;
        uint64_t from = 0;    // the game's VBlank count the dump starts at (the VSync records' count, as GT4_GSN_AT and
                              // the host log's 'vblank N' have it: a mid-run capture's start high), 0: before anything
        uint64_t count = 1;   // VSyncs written
    };
    // "PATH,FROM,COUNT" (FROM and COUNT optional: 0 and 1); false for an empty or unreadable value.
    static bool parse(const char* value, Options& out);
    explicit GsDumpEncoder(Options options);
    ~GsDumpEncoder();

    // The input, in the front end's order.
    void gif(uint32_t path, const uint8_t* data, size_t bytes);
    void state(const gfx::StateRecord& r);
    void upload(const gfx::UploadRecord& u, const uint8_t* data, size_t bytes);
    void read(const gfx::ReadRecord& r);
    void draw(const gfx::DrawRecord& d, const gfx::Vertex* vertices);
    void primitives(const ps2::GsPrimitives& p);
    // A texture set and its bytes (the stream's blob); `kept` gives a content's bytes by hash when the record didn't
    // carry them (sent = 0: the host kept them), nullptr when it has none.
    void texSet(const uint8_t* payload, size_t bytes, const std::function<const std::vector<uint8_t>*(uint64_t)>& kept);
    void model() { if (writer.isOpen()) ++counts.models; }
    void effect(const gfx::EffectRecord& r);
    void display(const gfx::DisplayRecord& d) { shown = d; }
    // GS memory replaced whole (the host's writeVram: a mid-run capture's snapshot). Before the dump opens, nothing to
    // do (open() takes memory then); open with nothing written yet, it opens again on `memory`; after that, counted,
    // and the dump isn't exact.
    void memoryReplaced(const uint8_t* memory);

    // At a VBlank (`vblank`: the VBlank count the host passes its target): true when the dump opens here, before
    // vsync(); the caller then calls open() with GS memory as it stands.
    bool opensAt(uint64_t vblank) {
        firstSeen = std::min(firstSeen, vblank);
        lastSeen = std::max(lastSeen, vblank);
        return !opened && !failed && options.from && vblank == options.from;
    }
    // FROM 0: the dump opens before anything reaches the front end (GS memory zero, as the console boots).
    bool opensAtStart() const { return !opened && !failed && !options.from; }
    void open(const uint8_t* memory, const char* memorySource);
    void vsync(uint32_t field);
    bool writing() const { return writer.isOpen(); }
    // The summary line's entry ("" when no dump ran): records, transfers, what wasn't dumped, the depth limit.
    std::string summary() const;

    // The register shadow (tests).
    const GsDumpState& shadow() const { return registers; }

private:
    void put(uint32_t path);                                       // `out` as a transfer, then cleared
    void apply(uint32_t reg, uint64_t value);                      // into the shadow
    void addRegisters(const std::vector<std::pair<uint32_t, uint64_t>>& writes);   // A+D, and the shadow
    void image(const uint8_t* data, size_t bytes);                 // IMAGE packets (and padding to a quadword)
    void transfer(uint32_t dbp, uint32_t dbw, uint32_t dpsm, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  const uint8_t* data, size_t bytes);
    void parseGif(uint32_t path, const uint8_t* data, size_t bytes);   // raw GIF into the shadow
    void beforeDraw(uint32_t prim);                                // the depth limit's count
    void tag(uint32_t nloop, bool eop, uint32_t flg, uint32_t nreg, uint64_t regs, bool pre = false, uint32_t prim = 0);
    void word(uint64_t v);

    Options options;
    GsDumpWriter writer;
    GsDumpState registers;
    gfx::DisplayRecord shown{};
    std::vector<uint8_t> out;   // the transfer being built
    bool opened = false, failed = false, closed = false;
    uint64_t vsyncs = 0;
    uint64_t firstSeen = ~uint64_t(0), lastSeen = 0;   // the game's VBlank counts seen (a FROM never reached is said)
    float internalQ = 1.0f;   // the GS's Q (PACKED ST sets it; RGBAQ's Q is what reaches the vertex)
    // the raw GIF paths' parse state (a packet can span calls)
    struct Path {
        uint32_t nloop = 0, nreg = 0, flg = 0, reg = 0;
        uint64_t regs = 0;
        bool active = false;
        size_t imageLeft = 0;   // quadwords
    } paths[4];
    // the texture sets' uploads the stream delivers again as upload records with no bytes (dumped with the set)
    std::vector<std::pair<uint32_t, uint32_t>> setUploads;   // (source, base)
    std::unordered_set<uint32_t> zWritten;   // Z buffer bases (blocks) drawn since FROM
    std::string memorySource;
    struct Counts {
        uint64_t records = 0, transfers = 0, bytes = 0, draws = 0, vertices = 0;
        uint64_t models = 0, effects = 0, uploadsWithoutBytes = 0, setsWithoutBytes = 0, depthUnwritten = 0;
        uint64_t memoryReplaced = 0;   // GS memory replaced under an open dump that had written packets
    } counts;
};

}  // namespace gt4::gs
