#pragma once
// GT4_VU1_INTERPRETER builds only (a development option; the default host has none of this): the VU1
// reference beside the native 3D path. It keeps a model of VU1 data and micro memory, fed by what VIF1
// decodes (Vif1::Observer), and runs GT4's programs on it the PS2 way: the bit-exact C++ port
// (vu1_exact.cpp), the interpreter (vu1.cpp) or the second interpreter (vu1_ref.cpp), whose XGKICKs give
// GIF packets (PATH1).
//
//   GT4_VU1=exact|interp|ref   draw with that reference instead of platform/gs/renderers3d.cpp
//   GT4_VU1_DIFF=1             every batch also runs in the reference (exact; =interp or =ref for the
//                              interpreters); the two outputs are compared as GS register writes and
//                              vertices (the frame list's draws and vertices are made of these), with a
//                              table at exit; the frame is drawn from the native output
//   GT4_VU1_INVENTORY=DIR      the interpreter runs everything and records each program (vu1_inventory.h)
//
// Programs of a micro image that is not GT4's always run in the interpreter (the tests' toy programs).
#include "renderers3d.h"
#include "vif1.h"
#include "vu1.h"
#include "vu1_exact.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt4::ps2 {

struct Vu1Ref;

class Vu1Reference final : public Vif1::Observer {
public:
    static bool wanted() { return true; }
    explicit Vu1Reference(Vif1& vif);
    ~Vu1Reference() override;
    Vu1Reference(const Vu1Reference&) = delete;
    Vu1Reference& operator=(const Vu1Reference&) = delete;

    // True: the batch was drawn here (a reference mode, or not GT4's image); false: draw it natively (after
    // the comparison, GT4_VU1_DIFF, which then has drawn it already and returns true).
    bool run(const Batch3d& batch, Renderers3d& renderers, PrimitiveSink& out, uint32_t& continueAt, bool gt4Program);
    // A saved state restored: VU1's data memory, registers vf, and the micro image.
    void restore(const uint8_t* data, const uint32_t vf[32][4], const std::vector<uint8_t>& micro);

    void unpacked(uint32_t address, const uint32_t value[4], unsigned lanes) override;
    void mpg(uint32_t address, const uint32_t* words, uint32_t instructions) override;

    // A GS register write or a vertex: what the outputs are compared as.
    struct Event {
        bool vertex = false;
        uint32_t reg = 0;
        uint64_t value = 0;
        GsVertex v;
        bool fog = false;
    };
    // The GS register writes and vertices of GIF packets (several may follow each other).
    static void decodeGif(const uint8_t* data, size_t bytes, std::vector<Event>& out);

    // Two outputs compared: identical, the same within float rounding (an eighth of a pixel plus 1/4096 of the
    // distance from the window centre, Z by 64 plus 1/8192 of Z, colours by 2, ST by 1e-3, the same ADC), or
    // different. `printFar`: a label to print far vertices with.
    struct Count {
        uint64_t runs = 0, identical = 0, close = 0, differ = 0, vertices = 0, verticesChanged = 0, far = 0, farDrawn = 0, wraps = 0;
        uint32_t maxXy = 0, maxZ = 0, maxColour = 0, maxFog = 0;
        double maxSt = 0;
    };
    enum class Verdict { Identical, Close, Differ };
    // `colours`: false leaves colours out (random test data whose colour words overflow when lit: the GS keeps
    // the low byte of a large integer, which rounding decides).
    static Verdict classify(const std::vector<Event>& reference, const std::vector<Event>& native, Count& c, size_t& first,
                            const char* printFar = nullptr, bool colours = true);

private:
    enum class Mode { Native, Exact, Interp, Ref };
    Vif1& vif;
    Vu1 unit;
    Vu1Exact exact;
    std::unique_ptr<Vu1Ref> ref;
    Mode mode = Mode::Native, diff = Mode::Native;
    bool inventory = false;

    // Runs the program on the memory model with `how`, its kicks to `sink`; returns where MSCNT goes on.
    uint32_t runReference(const Batch3d& batch, Mode how, GifSink* sink);

    std::map<uint32_t, Count> counts;   // by entry << 16 | lighting body
    uint64_t shown = 0;
    bool detail = false;   // GT4_VU1_DIFF_DETAIL: print the first differences
    void compare(const Batch3d& batch, const std::vector<Event>& reference, const std::vector<Event>& native);
};

}  // namespace gt4::ps2
