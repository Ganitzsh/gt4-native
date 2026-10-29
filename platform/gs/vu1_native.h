#pragma once
// GT4's VU1 microprograms, written in C++.
//
// GT4 uploads one micro image at boot (2,046 instructions, one MPG burst) and calls it through a table of
// entries at 0x1B08: each entry optionally runs the setup at 0x0000 (matrix, viewport and fog constants
// from the batch), optionally the lighting chain at 0x0048 (routines chosen per batch by ITOP), then one of
// six strip renderers, which transform, light, cull and clip the batch's vertices and kick the GIF packet
// (PATH1). This file runs those programs natively. They compute with VuF (vu_float.h), so their output and
// the data memory they leave are the interpreter's, bit for bit; GT4_VU1_DIFF=native checks it.
//
// Anything not ported yet returns false and runs in the interpreter: a micro image that is not GT4's, an
// entry or a lighting routine without a native version. docs/VU1_PROGRAMS.md lists what each part does.
#include "vu1.h"

#include <cstdint>
#include <map>
#include <string>

namespace gt4::ps2 {

class Vu1Native {
public:
    // Runs the program the VIF starts at `pc` (8-byte units) with the VU's current TOP/ITOP. True when it
    // ran natively: the VU's data memory, the registers later programs read (the setup constants) and
    // the PATH1 output are what the interpreter would have produced. False: nothing was changed.
    bool run(Vu1& vu, uint32_t pc);
    // The micro memory changed (VIF1 MPG): check the image again before the next run.
    void microWritten() { imageKnown = false; }

    struct Stats {
        uint64_t native = 0, interpreted = 0;
        uint64_t clipperWraps = 0;   // clipped polygons whose records wrapped past qword 1023 (vu1_native.cpp)
        std::map<uint32_t, uint64_t> parts;   // native runs per renderer, lighting body and unpack routine (byte address)
    } stats;

private:
    bool imageKnown = false, gt4Image = false;
};

}  // namespace gt4::ps2
