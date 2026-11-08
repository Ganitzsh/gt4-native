#pragma once
// The host's view of one frame's scene records (docs/modern/CONTRACTS.md a.7, a.8): what the SceneDecoder
// (scene_decoder.h) builds from the kScene records the game sends with its batches, for lanes B, E and F.
//
// Matrices here are GT4's: row vectors, p' = p.x * m[0] + p.y * m[1] + p.z * m[2] + p.w * m[3], and `a * b` applies
// a first (the scene records' convention, CONTRACTS.md a.3). platform/render/vecmath.h's Mat4 is column-major with
// column vectors: convert explicitly where the two meet (a transpose), never by copying the floats.
#include "gt4/gfx.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
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
    uint64_t texture = 0;    // the texture's identity (the front end's textureIdentity(id): content hash, fields,
                             //   palette, TEXA), so a re-decode after eviction keeps the id; 0 untextured
    uint32_t palette = 0;    // CLUT source id/version
    uint32_t body = 0;       // ModelJob::chain >> 16 (lighting body)
    uint32_t dot = 0;        // ModelJob::dot
    uint32_t state = 0;      // hash of the DrawState fields that matter (blend, tests, fog, filter)
    uint64_t colours = 0;    // 0 (a.8, 51a14a9): the batch's colour rows are per-frame lighting, not identity;
                             //   reserved for an unscaled per-object colour
};
using MeshId = uint32_t;      // 0 = none
using MaterialId = uint32_t;  // 0 = none

// --- Validation counters (CONTRACTS.md a.7): per frame and as running totals -------------------------------------------
struct SceneCounters {
    // decoding
    uint64_t records = 0, bad = 0, versionUnknown = 0, unknownKind = 0, outsideFrame = 0;
    // frames and views
    uint64_t frames = 0, framesUnclosed = 0, countsMismatch = 0;
    uint64_t framesTruncated = 0;   // still open when the stream ended (a capture's last list, a.5.1): reported
    uint64_t begins = 0;   // FrameEnd::begins: the FrameBegins the runtime merged (reported)
    uint64_t framesVehiclesOnly = 0;   // frames flagged kFrameVehiclesOnly (a.1 mode 2: Vehicles only, no view; reported)
    uint64_t views = 0, viewsNested = 0, viewEndWithoutBegin = 0, viewsUnclosed = 0;   // unclosed: open at FrameEnd
    // projections
    uint64_t projections = 0, projectionUnknown = 0;
    // instances and their batches
    uint64_t instances = 0, instancesWithoutView = 0, itemsMismatch = 0;
    uint64_t jobsTagged = 0, jobsUntaggedInView = 0;
    // runs after an instance closed with its `items` runs, before the next Instance / ViewEnd / Marker (a.5.3: other draw
    // paths), and those of them whose jobs carry the closed instance's matrix (a displaced own run): reported
    uint64_t runsAfterInstance = 0, runsAfterSameMatrix = 0;
    uint64_t matrixChecked = 0, matrixMismatch = 0;
    float matrixMaxError = 0.0f;   // largest relative difference seen
    uint64_t viewportMismatch = 0, guardUnexpected = 0;
    uint64_t cameraChecked = 0, cameraMismatch = 0;
    uint64_t courseObjects = 0;   // instances flagged kInstCourseObject (A11a: placed course objects, not camera-checked)
    uint64_t billboards = 0;      // instances flagged kInstBillboard (A7 step 1b: one per BillboardSet batch run)
    uint64_t lightMaps = 0;       // instances flagged kInstLightMap (A6: one per batch run of a car's light map)
    // lights, emitters, markers, vehicles; lights after their view's ViewEnd (a.5 rule 4: reported), naming a view this
    // frame has not begun (dropped)
    uint64_t lights = 0, lightsAfterViewEnd = 0, lightViewUnknown = 0, emitters = 0, markers = 0, vehicles = 0;
    // filled by the front end and the renderer (A10's front-end half, B lane)
    uint64_t viewSplitTargets = 0, viewsUnbound = 0;
    uint64_t viewAuxiliaryDraws = 0, viewAuxiliaryReads = 0;   // a.5.7: tagged draws into auxiliary targets; principal draws sampling one
    uint64_t cuts = 0, cutsAsync = 0, viewsCut = 0;   // partial FrameLists in a frame (a.8; async: readLater's), views cut
    uint64_t fallbackNotBound = 0, fallbackMeshPending = 0, fallbackPipelinePending = 0, fallbackValidation = 0,
             fallbackReadPending = 0;
    uint64_t meshesNew = 0, meshesPending = 0, materialsNew = 0;
    // B6: instances given mesh 0 because their geometry is this frame's only: skinned (kInstSkinned); per frame, a
    // billboard run (kInstBillboard) or an item with a key-frame blend (MeshCache's per-frame items). B7 leaves them on the
    // GS path
    uint64_t meshesSkinned = 0, meshesPerFrame = 0;
    void add(const SceneCounters& o);   // running totals
    // the gates of CONTRACTS.md a.7 that hold on any stream (not the ones that need the front end)
    bool errorFree() const {
        return bad == 0 && versionUnknown == 0 && unknownKind == 0 && framesUnclosed == 0 && countsMismatch == 0 &&
               viewsNested == 0 && viewEndWithoutBegin == 0 && viewsUnclosed == 0 && itemsMismatch == 0 && projectionUnknown == 0 &&
               matrixMismatch == 0 && viewportMismatch == 0 && lightViewUnknown == 0;
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
    bool splitTargets = false;                    // its tagged draws went to two colour targets (never substituted)
    bool cut = false;                             // it had tagged draws in a partial FrameList (viewsCut: never substituted)
    uint32_t firstInstance = 0, instanceCount = 0;
    uint32_t firstLight = 0, lightCount = 0;      // its lights in SceneFrame::lights, by id, grouped at FrameEnd (a.5 rule 4)
    bool closed = false;                          // its ViewEnd arrived (false: nested or left open at FrameEnd)
    bool valid = false;                           // its own checks passed at its ViewEnd (a.8): instances, items, matrices,
                                                  // viewports, projections (viewSplitTargets is the front end's)
};
struct InstanceData {
    uint16_t view = 0, flags = 0;
    uint32_t serial = 0;
    uint32_t index = 0;                           // its position in SceneFrame::instances: its jobs' tag is index + 1
                                                  //   (ModelJob::pad[0]); the serial is the record's, not trusted
    uint16_t projection = 0;                      // GT4's projection for this instance: parity and GS Z (B7, B8)
    uint64_t object = 0;                          // set << 32 ^ model << 8 ^ lod, or the Cmd address
    uint32_t cmd = 0, entrant = 0xFFFFFFFFu, set = 0;
    uint16_t model = 0, section = 0xFFFF;
    uint8_t lod = 0, lodCount = 0;
    Mat4 objectToView, objectToWorld, previousObjectToWorld;
    float axisScale[3] = {1, 1, 1}, shapeScale = 0;
    uint16_t items = 0, runs = 0;                 // runs: the kBatchRun records that followed it
    uint32_t jobCount = 0;                        // in FrameList::models, found by tag (ModelJob::pad[0] = this
                                                  //   instance's position + 1), not as a range: an item
                                                  //   callback's runs can land between them (a.5.3)
    MeshId mesh = 0;                              // copied at sceneFrame() from the views' ViewOnList::ids (B6)
    uint32_t firstMaterial = 0, materialCount = 0;   // in SceneFrame::materials: one per item, in the mesh's
                                                     //   item order (a car has more than four). In a SceneView
                                                     //   package these three stay 0: it is made before B6 runs
                                                     //   and const after; its ids are on ViewOnList
};
// B6: one instance's ids beside its package (FrameList::ViewOnList::ids, CONTRACTS.md a.8).
struct InstanceIds {
    MeshId mesh = 0;
    uint32_t firstMaterial = 0, materialCount = 0;   // in ViewOnList::materials
};
struct SceneFrame {
    uint32_t frame = 0, vblank = 0, flags = 0;
    uint16_t begins = 0;                          // FrameEnd::begins
    std::vector<ProjectionData> projections;      // by id - 1
    std::vector<ViewData> views;                  // by id - 1
    std::vector<InstanceData> instances;          // by serial - 1
    std::vector<MaterialId> materials;            // the instances' materials (InstanceData::firstMaterial), B6
    std::vector<gfx::scene::Light> lights;
    std::vector<gfx::scene::Emitter> emitters;
    std::vector<gfx::scene::Marker> markers;
    std::vector<uint32_t> markerCommands;         // the FrameList command index each marker arrived at (front end)
    std::vector<gfx::scene::Vehicle> vehicles;
    SceneCounters counters;                       // this frame's
    bool complete = false;                        // FrameEnd arrived and its counts match
};

// One view's package at its ViewEnd (CONTRACTS.md a.8, cuts at a read): what a partial FrameList needs to substitute
// the view. Copies (SceneFrame keeps its own; ModelJob::pad[0] indexes that). Pooled: sceneViewFromPool() takes one
// (its vectors emptied, their capacity kept), sceneViewToPool() gives back one no one else holds, so a steady
// frame allocates none.
struct SceneView {
    ViewData view;                                // view.valid: the verdict at ViewEnd
    std::vector<InstanceData> instances;          // copies, in arrival order
    std::vector<ProjectionData> projections;      // the ones the view and its instances name
    std::vector<gfx::scene::Light> lights;        // the view's, as far as its ViewEnd (later ones: a.5 rule 4)
    std::vector<gfx::scene::Emitter> emitters;    // frame-wide records that arrived before this ViewEnd
    std::vector<gfx::scene::Vehicle> vehicles;    //   (later ones are not available to a partial substitution)
};
namespace detail {
inline std::mutex& sceneViewPoolMutex() {
    static std::mutex m;
    return m;
}
inline std::vector<std::shared_ptr<SceneView>>& sceneViewPool() {
    static std::vector<std::shared_ptr<SceneView>> pool;
    return pool;
}
}  // namespace detail
inline std::shared_ptr<SceneView> sceneViewFromPool() {
    std::shared_ptr<SceneView> v;
    {
        std::lock_guard<std::mutex> lock(detail::sceneViewPoolMutex());
        auto& pool = detail::sceneViewPool();
        if (!pool.empty()) {
            v = std::move(pool.back());
            pool.pop_back();
        }
    }
    if (!v) return std::make_shared<SceneView>();
    v->view = ViewData{};
    v->instances.clear();
    v->projections.clear();
    v->lights.clear();
    v->emitters.clear();
    v->vehicles.clear();
    return v;
}
// Back to the pool when no one else holds it. (use_count() is a relaxed load: the fence orders every earlier read of
// the package by its last other holder before the decoder reuses it, once a consumer keeps copies on another thread.)
inline void sceneViewToPool(std::shared_ptr<const SceneView>& v) {
    if (!v) return;
    if (v.use_count() == 1) {
        std::atomic_thread_fence(std::memory_order_acquire);
        std::lock_guard<std::mutex> lock(detail::sceneViewPoolMutex());
        auto& pool = detail::sceneViewPool();
        if (pool.size() < 64) pool.push_back(std::const_pointer_cast<SceneView>(std::move(v)));
    }
    v.reset();
}

}  // namespace gt4::scene
