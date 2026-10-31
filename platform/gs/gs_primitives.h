#pragma once
// What GT4's 3D renderers (renderers3d.h) give the GS front end: primitives with the GS state that goes with
// them, as values, with no GIF packet in between. The state comes from the batch's GIF tag (PRIM) and the
// packets the game wrote for the GS (a batch's state header, the screen grids), which pass unchanged.
#include <cstddef>
#include <cstdint>

namespace gt4::ps2 {

// One vertex as the GS takes it: the values the ST, RGBAQ and XYZF2 (or XYZ2) registers receive.
struct GsVertex {
    float s = 0, t = 0, q = 1;   // ST (S/Q-scaled texture coordinates) and Q
    uint32_t rgba = 0;           // R in the low byte, A in the high one (0x80 = 1.0)
    uint32_t x = 0, y = 0;       // window coordinates, 12.4 fixed point (16 bits each)
    uint32_t z = 0;              // 24 bits with fog (XYZF2), 32 without (XYZ2)
    uint32_t fog = 0;            // F, 0..255 (XYZF2 only)
    bool adc = false;            // the GS does not draw the triangle this vertex completes (XYZF3 / XYZ3)
    // Not the GS's: where the vertex was in the previous frame, for motion vectors (the renderer's object seen
    // then: Renderers3d::endFrame). Homogeneous window coordinates (x, y in pixels, as XYZ before FTOI4, times
    // w) over this frame's w, so that they interpolate linearly across the screen; w = 0: not known.
    float motion[3] = {0.0f, 0.0f, 0.0f};
    // Not the GS's: the position in homogeneous window coordinates, so that the GPU clips (docs/NATIVE_V2.md, step
    // 2): x, y (pixels, as XYZ before FTOI4) and Z (as the GS reads it) times w, and w (the clip-space w); w = 0:
    // none (x, y, z above).
    float h[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// A run of vertices drawn with one PRIM. The front end assembles them as the GS does (a strip continues
// across vertices; `adc` stops the triangles that would join two strips).
struct GsPrimitives {
    bool setPrim = false;   // the batch's GIF tag sets PRIM (its PRE bit); otherwise PRIM stays as it was
    uint32_t prim = 0;      // the PRIM register value
    bool fog = true;        // XYZF2 (Z 24 bits and F) or XYZ2 (Z 32 bits)
    const GsVertex* vertices = nullptr;
    size_t count = 0;
};

// Where the renderers send their output, in the order the GS must see it.
class PrimitiveSink {
public:
    virtual ~PrimitiveSink() = default;
    // A GIF packet the game wrote (a batch's GS state header; a screen grid's template with the fields the
    // grid program steps), whole.
    virtual void packet(const uint8_t* gif, size_t bytes) = 0;
    virtual void primitives(const GsPrimitives& p) = 0;
};

}  // namespace gt4::ps2
