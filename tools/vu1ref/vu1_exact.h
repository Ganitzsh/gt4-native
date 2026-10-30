#pragma once
// Test reference (GT4_VU1_INTERPRETER builds only): GT4's VU1 microprograms in C++ over a model of VU1 data
// memory, computing with the VU's arithmetic (VuF, vu_float.h), so that their PATH1 packets and the memory
// they leave are the interpreter's, bit for bit (tests/vu1_native_fuzz.cpp). Until 2026-10-30 this was the
// host's 3D path; the host now runs platform/gs/renderers3d.cpp (plain floats, primitives to the front end),
// which tools/vu1ref/vu1_reference.cpp compares with this (GT4_VU1_DIFF=1).
//
// GT4 uploads one micro image at boot (2,046 instructions, one MPG burst) and calls it through a table of
// entries at 0x1B08: each entry optionally runs the setup at 0x0000 (matrix, viewport and fog constants
// from the batch), optionally the lighting chain at 0x0048 (routines chosen per batch by ITOP), then one of
// six strip renderers, which transform, light, cull and clip the batch's vertices and kick the GIF packet
// (PATH1). A micro image that is not GT4's returns false (the interpreter runs it). docs/VU1_PROGRAMS.md
// lists what each part does.
#include "vu1.h"

#include <cstdint>
#include <map>
#include <string>

namespace gt4::ps2 {

class Vu1Exact {
public:
    // Runs the program the VIF starts at `pc` (8-byte units) with the VU's current TOP/ITOP. True when it
    // ran natively: the VU's data memory, the registers later programs read (the setup constants) and
    // the PATH1 output are what the interpreter would have produced. False: nothing was changed.
    bool run(Vu1& vu, uint32_t pc);
    // The micro memory changed (VIF1 MPG): check the image again before the next run.
    void microWritten() { imageKnown = false; }

    struct Stats {
        uint64_t native = 0, interpreted = 0;
        uint64_t clipperWraps = 0;   // clipped polygons whose records wrapped past qword 1023 (vu1_exact.cpp)
        std::map<uint32_t, uint64_t> parts;   // native runs per renderer, lighting body and unpack routine (byte address)
    } stats;

private:
    bool imageKnown = false, gt4Image = false;
};

}  // namespace gt4::ps2
