#pragma once
// B16c (docs/modern/PLAN.md; CONTRACTS.md b.11): where a frame list's picture for the display stops being the 3D scene.
// GT4 draws its 3D into a scene target, and its post pass copies and grades that into the buffer the display shows;
// the HUD and text come after. The anchor is the last draw (or grade) into the picture the list makes that covers the
// displayed rectangle and samples a target, followed back through such passes (at most kSeamHops) to a target a Light
// drew into this list. The picture right after it is the reactive mask's reference, and the composed mapping takes a
// pixel of the made picture to the Light target's pixel whose motion vector and depth it shows.
#include "gs_frontend.h"

#include <cstddef>
#include <cstdint>

namespace gt4::gsn {

inline constexpr int kSeamHops = 4;
inline constexpr float kSeamCover = 0.98f;   // the share of the displayed rectangle the anchor covers

struct SeamAnchor {
    enum class Outcome : uint8_t {
        Found,      // chained to a Light
        Fields,     // a fields-mode frame: reactive everywhere, not looked at
        NotShown,   // the list ends at no VSync (cut for a GS read)
        NoAnchor,   // no covering draw samples a target (menus, transitions)
        NoLight,    // the chain ends at a target no Light drew into this list (a feedback blur, a menu backdrop)
        Capped,     // kSeamHops passes followed without reaching a Light
    } outcome = Outcome::NoAnchor;
    uint32_t made = 0;    // the target the list makes for the display (the back buffer; else the shown one)
    size_t at = 0;        // the anchor's last command: the reference is the picture after it
    size_t lightAt = 0;   // the Light command the chain ends at
    uint32_t light = 0, lightDepth = 0;   // its scene target and Z buffer
    // A made GS pixel (x, y) -> the Light target's GS pixel (x scale[0] + offset[0], y scale[1] + offset[1]).
    float scale[2] = {1.0f, 1.0f}, offset[2] = {0.0f, 0.0f};
    float rect[4] = {};             // the displayed rectangle in the made target, GS pixels (x0, y0, x1, y1)
    bool depthInverted = false;     // the Z buffer is inverted in place at the anchor (Draw::depthInverted)
    int hops = 0;                   // passes followed (1: the copy samples the Light target itself)
    bool lastCovering = true;       // the anchor is also the last pass covering the rectangle (any source)
    bool found() const { return outcome == Outcome::Found; }
};

SeamAnchor findSeamAnchor(const FrameList& list);
const char* seamOutcomeName(SeamAnchor::Outcome outcome);

}  // namespace gt4::gsn
