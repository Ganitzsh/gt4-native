#pragma once
// F2b's lamp pass (PLAN.md F2b): the course's street lamps. GT4 draws their glows as course billboards
// (BillboardSet__render), and A7 step 1b sends each billboard as a kSrcBillboard candidate: its centre and size in the
// view's space, its id the serial of the kInstBillboard instance its flush drew. A candidate whose instance's material
// has the glow class (glow_classifier.h, set by B6's classifier) becomes a kSrcCourseGlow light; the others (trees,
// signs) stay candidates, which the gather never lights. Render thread; works on the frame's Light records in place.
#include "gt4/gfx.h"
#include "scene_frame.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace gt4::render {

class CourseLampPass {
public:
    struct Stats {
        uint32_t candidates = 0;   // the view's kSrcBillboard records
        uint32_t kept = 0;         // promoted to kSrcCourseGlow
        uint32_t dropped = 0;      // their instance's material is not a glow (left as candidates: not lit)
        uint32_t noInstance = 0;   // no instance with that serial in the frame, or it has no material (not lit)
        uint32_t merged = 0;       // promoted ones that were already a game kSrcCourseGlow record (entries2[]): one lamp
        uint32_t mergedAgain = 0;  // of them, into a record another candidate had already merged into this call
    };
    // In `records` (the frame's Lights): each kSrcBillboard candidate of view `view` is promoted or left (above). A
    // promoted candidate within its billboard's half size of a game kSrcCourseGlow record of the view (A7 step 1a's
    // entries2[]) is that lamp: the record keeps its position and blink and takes the billboard's size, and the
    // candidate goes back to being a candidate (not lit twice). `classes` reads MaterialTable::classes.
    void promote(const scene::SceneFrame& frame, uint16_t view, const std::function<uint32_t(scene::MaterialId)>& classes,
                 std::vector<gfx::scene::Light>& records, Stats* stats = nullptr);

private:
    std::unordered_map<scene::MaterialId, uint32_t> classesById;   // never change for an id (bounded: cleared past 100k)
    std::unordered_map<uint32_t, uint32_t> bySerial;              // this frame's instances by serial (rebuilt per call)
    std::vector<size_t> gameLamps;                                 // the game's kSrcCourseGlow records of the view (per call)
    std::vector<uint8_t> mergedInto;                               // records a candidate merged into (per call)
};

}  // namespace gt4::render
