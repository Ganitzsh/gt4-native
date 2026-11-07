#pragma once
// The host's view of one frame's scene records (docs/modern/CONTRACTS.md a.7, a.8): what the SceneDecoder
// (scene_decoder.h) builds from the kScene records the game sends with its batches, for lanes B, E and F.
//
// Matrices here are GT4's: row vectors, p' = p.x * m[0] + p.y * m[1] + p.z * m[2] + p.w * m[3], and `a * b` applies
// a first (the scene records' convention, CONTRACTS.md a.3). platform/render/vecmath.h's Mat4 is column-major with
// column vectors: convert explicitly where the two meet (a transpose), never by copying the floats.
#include "gt4/gfx.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace gt4::scene {

// --- Matrices (GT4's row-vector convention) --------------------------------------------------------------------------
struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    static Mat4 from(const float (&a)[4][4]) {
        Mat4 r;
        std::memcpy(r.m, a, sizeof r.m);
        return r;
    }
    // a * b: a applied first (row r of the result = row r of a through b)
    friend Mat4 operator*(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 4; ++k)
                r.m[i][k] = a.m[i][0] * b.m[0][k] + a.m[i][1] * b.m[1][k] + a.m[i][2] * b.m[2][k] + a.m[i][3] * b.m[3][k];
        return r;
    }
};
// The inverse of a general 4 x 4 (false when singular: r left as identity).
bool inverse(const Mat4& a, Mat4& r);
// The largest absolute entry, and the largest absolute difference.
float largest(const Mat4& a);
float largestDifference(const Mat4& a, const Mat4& b);

// --- Keys and ids shared with the scene cache (lane B, CONTRACTS.md a.8) ----------------------------------------------
struct MeshKey {             // a Cmd's geometry: welded once
    uint64_t packets = 0;    // hash of the items' batch contents, in order
    uint32_t renderer = 0;   // the first item's renderer (ModelJob::renderer): the decode depends on it
    uint32_t chain = 0;      // the first item's lighting chain (ModelJob::chain)
    uint32_t chainUnpack = 0;   // the first item's decompression routine (the normals' format)
    bool operator==(const MeshKey& o) const {
        return packets == o.packets && renderer == o.renderer && chain == o.chain && chainUnpack == o.chainUnpack;
    }
};
struct MeshKeyHash {
    size_t operator()(const MeshKey& k) const {
        return static_cast<size_t>(k.packets ^ (uint64_t(k.renderer) << 32) ^ k.chain ^ (uint64_t(k.chainUnpack) << 48));
    }
};
struct MaterialKey {         // GT4's material: what the batch's pixels depend on besides geometry
    uint64_t texture = 0;    // texture set content hash ^ texture index; 0 untextured
    uint32_t palette = 0;    // CLUT source id/version
    uint32_t body = 0;       // ModelJob::chain >> 16 (lighting body)
    uint32_t dot = 0;        // ModelJob::dot
    uint32_t state = 0;      // hash of the DrawState fields that matter (blend, tests, fog, filter)
    uint64_t colours = 0;    // hash of the material colour qwords (0049E228)
};
using MeshId = uint32_t;      // 0 = none
using MaterialId = uint32_t;  // 0 = none

// --- Validation counters (CONTRACTS.md a.7): per frame and as running totals -------------------------------------------
struct SceneCounters {
    // decoding
    uint64_t records = 0, bad = 0, versionUnknown = 0, unknownKind = 0, outsideFrame = 0;
    // frames and views
    uint64_t frames = 0, framesUnclosed = 0, countsMismatch = 0;
    uint64_t views = 0, viewsNested = 0, viewEndWithoutBegin = 0, viewsUnclosed = 0;   // unclosed: open at FrameEnd
    // projections
    uint64_t projections = 0, projectionUnknown = 0;
    // instances and their batches
    uint64_t instances = 0, instancesWithoutView = 0, itemsMismatch = 0;
    uint64_t jobsTagged = 0, jobsUntaggedInView = 0;
    uint64_t matrixChecked = 0, matrixMismatch = 0;
    float matrixMaxError = 0.0f;   // largest relative difference seen
    uint64_t viewportMismatch = 0, guardUnexpected = 0;
    uint64_t cameraChecked = 0, cameraMismatch = 0;
    // lights, emitters, markers, vehicles
    uint64_t lights = 0, emitters = 0, markers = 0, vehicles = 0;
    // filled by the front end and the renderer (A10's front-end half, B lane)
    uint64_t viewSplitTargets = 0, viewsUnbound = 0;
    uint64_t fallbackNotBound = 0, fallbackMeshPending = 0, fallbackPipelinePending = 0, fallbackValidation = 0,
             fallbackReadPending = 0;
    uint64_t meshesNew = 0, meshesPending = 0, materialsNew = 0;
    void add(const SceneCounters& o);   // running totals
    // the gates of CONTRACTS.md a.7 that hold on any stream (not the ones that need the front end)
    bool errorFree() const {
        return bad == 0 && versionUnknown == 0 && unknownKind == 0 && framesUnclosed == 0 && countsMismatch == 0 &&
               viewsNested == 0 && viewEndWithoutBegin == 0 && viewsUnclosed == 0 && itemsMismatch == 0 && projectionUnknown == 0 &&
               matrixMismatch == 0 && viewportMismatch == 0;
    }
};

// --- One frame's scene (CONTRACTS.md a.8) ----------------------------------------------------------------------------------
struct ProjectionData {
    uint16_t id = 0, view = 0;
    Mat4 viewToClip;                              // GT4's GL projection as sent (its own near / far)
    float viewportScale[4] = {}, viewportOffset[4] = {}, clamp[4] = {};
};
struct ViewData {
    uint16_t id = 0;
    uint8_t role = 0, flags = 0;
    uint32_t pass = 0;
    uint16_t projection = 0;                      // the camera's (ViewBegin::projection)
    Mat4 worldToView;
    Mat4 previousWorldToView;                     // matched by role + pass from the last frame (identity: none)
    bool previousKnown = false;
    uint16_t scissor[4] = {};
    uint32_t target = 0, depth = 0;               // GS target ids, bound by the front end
    uint32_t firstInstance = 0, instanceCount = 0, firstLight = 0, lightCount = 0;
    bool closed = false;                          // its ViewEnd arrived (false: nested or left open at FrameEnd)
};
struct InstanceData {
    uint16_t view = 0, flags = 0;
    uint32_t serial = 0;
    uint16_t projection = 0;                      // GT4's projection for this instance: parity and GS Z (B7, B8)
    uint64_t object = 0;                          // set << 32 ^ model << 8 ^ lod, or the Cmd address
    uint32_t cmd = 0, entrant = 0xFFFFFFFFu, set = 0;
    uint16_t model = 0, section = 0xFFFF;
    uint8_t lod = 0, lodCount = 0;
    Mat4 objectToView, objectToWorld, previousObjectToWorld;
    float axisScale[3] = {1, 1, 1}, shapeScale = 0;
    uint16_t items = 0, runs = 0;                 // runs: the kBatchRun records that followed it
    uint32_t firstJob = 0, jobCount = 0;          // in FrameList::models (A10's front-end half)
    MeshId mesh = 0;
    MaterialId materials[4] = {};
};
struct SceneFrame {
    uint32_t frame = 0, vblank = 0, flags = 0;
    std::vector<ProjectionData> projections;      // by id - 1
    std::vector<ViewData> views;                  // by id - 1
    std::vector<InstanceData> instances;          // by serial - 1
    std::vector<gfx::scene::Light> lights;
    std::vector<gfx::scene::Emitter> emitters;
    std::vector<gfx::scene::Marker> markers;
    std::vector<uint32_t> markerCommands;         // the FrameList command index each marker arrived at (front end)
    std::vector<gfx::scene::Vehicle> vehicles;
    SceneCounters counters;                       // this frame's
    bool complete = false;                        // FrameEnd arrived and its counts match
};

}  // namespace gt4::scene
