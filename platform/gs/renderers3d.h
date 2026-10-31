#pragma once
// GT4's 3D renderers, as native code. On the PS2 they are VU1 microprograms: the game unpacks a batch of
// vertices and constants for one of them (vif1.h) and starts it at an entry of a table; it transforms,
// lights, culls and clips the vertices and hands GS primitives to the GIF. Here each renderer reads the
// batch, computes in plain float arithmetic and gives the front end primitives with their GS state
// (gs_primitives.h). docs/VU1_PROGRAMS.md describes each one; where the VU's number rules shape the picture
// (division by zero, a clamp of "infinity", float-to-fixed truncation, the ADC bit, clip flags) the code
// states the rule.
//
// The entry table at 0x1B08 (24 groups of five instructions): [setup], [lighting chain], one of six strip
// renderers, [GS state header]. The setup's constants stay for the entries that skip it.
#include "gs_primitives.h"
#include "vif1.h"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace gt4::ps2 {

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

class Renderers3d {
public:
    // Runs the renderer `batch.entry` selects (8-byte units, as MSCAL gives it). False when the entry is
    // not one of GT4's (nothing drawn). `continueAt`: where the program ends, for a later MSCNT.
    bool run(const Batch3d& batch, PrimitiveSink& out, uint32_t& continueAt);

    // The setup's constants (qwords 193..200 of the last batch whose entry ran the setup).
    struct Constants {
        Vec4 matrix[4];        // the transform to clip space, by columns
        Vec4 scale, offset;    // viewport scale and offset (x, y, z), fog scale and offset (w)
        Vec4 clamp;            // limit of the clip-space point before the projection; w: fog start
        Vec4 guard;            // the guard band, as a scale of the clip-space point
    } constants;

    // Motion vectors (not the PS2's): each setup is an object drawn with its own transform. Matched with the
    // previous frame's by what it draws (its first batch's GIF tag, strips and GS state header) and its order
    // among the setups that draw the same, its vertices also get where that transform put them then
    // (GsVertex::motion). The frame ends at the game's VSYNC.
    void endFrame();

    struct Stats {
        uint64_t batches = 0, skipped = 0;
        uint64_t clipped = 0;          // triangles through the polygon clipper
        uint64_t largeClips = 0;       // clipped polygons of more than 9 vertices (only a ring made non-convex by rounding)
        uint64_t unexpectedTags = 0;   // a batch's GIF tag that is not PACKED ST, RGBAQ, XYZ: not drawn
        uint64_t countMismatch = 0;    // the tag's NLOOP is not the number of vertices of the strips
        uint64_t oversized = 0;        // strips longer than a batch can hold: not drawn
        std::map<uint32_t, uint64_t> parts;   // runs per renderer, lighting body and unpack routine (byte address)
        uint64_t setups = 0, matched = 0;     // objects (setups), and those found in the previous frame
    } stats;

private:
    std::unordered_map<uint64_t, std::vector<Constants>> lastFrame, thisFrame;
    bool hasPrevious = false;
    Constants previous;   // the current object's constants in the previous frame (when hasPrevious)
};

// A saved state's VU1 registers vf23..vf31 (GT4_REC_STATE version 1) as the setup's constants.
Renderers3d::Constants constantsFromRegisters(const uint32_t vf[32][4]);

}  // namespace gt4::ps2
