// The native renderer's back end (see gsn_scene.h and docs/NATIVE_RENDERER.md).
#include "gsn_scene.h"

#include "render_graph.h"

#include "gt4_shaders.h"
#include "../gs/cpu_profile.h"
#include "../scene/scene_frame.h"
#include "lights_cluster.h"
#include "lights_gather.h"
#include "course_lamp_pass.h"   // (F2b)
#include "parity_view.h"
#include "seam_anchor.h"   // (B16c)

#include <algorithm>
#include <cassert>
#include <bit>
#include <chrono>
#include <bit>
#include <memory>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace gt4 {

namespace {

constexpr uint32_t kSlots = 2;   // frames in flight (renderer.cpp)
constexpr VkFormat kColour = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kDepth = VK_FORMAT_D32_SFLOAT_S8_UINT;

// The draw constants (gsn_common.glsl).
struct DrawConstants {
    float targetSize[2];
    float scale[2];
    float textureSize[2];
    float sourceOffset[2];
    float sourceSize[2];
    uint32_t flags;
    uint32_t tex, fogColour, alphaTest, blend;
    uint32_t clampU, clampV;   // WMS/WMT (0-1), MINU/MINV (2-11), MAXU/MAXV (12-21): CLAMP
    uint32_t lod;              // LCM (0), L (1-2), K (3-14, signed 7.4), MXL (16-18): TEX1
    float jitter[2];           // the scene stage's sub-pixel offset, GS pixels (temporal anti-aliasing)
    float sourceScale[2];      // a texture from a target: that target's render pixels per GS pixel (else scale's)
};
static_assert(sizeof(DrawConstants) == 88);
// B7's parity draws (modern_parity.vert, gsn_common.glsl under GSN_PARITY): the draw's constants, then the item's job and
// gsn_models.comp's t and tPrevious.
struct ParityConstants {
    DrawConstants draw;
    uint32_t job;
    float t, tPrevious;
    uint32_t side;   // B8's side-channel pass: the item's SideItem (gsn_common.glsl paritySide)
};
// B8 step 2: one per item the side-channel pass draws (modern_parity.vert GSN_SIDE): its instance's objectToView, as
// GT4 keeps it (row vectors: copied as they are, a GLSL mat4's columns are GT4's rows, so M x p is GT4's p x M), and its
// material.
struct SideItem {
    float objectToView[4][4];
    uint32_t material, pad[3];
};
static_assert(sizeof(SideItem) == 80);
static_assert(sizeof(ParityConstants) == 104);

// The temporal anti-aliasing's sub-pixel offsets: Halton (2, 3), 8 phases, in render pixels from the centre.
constexpr float kHalton[8][2] = {{0.0f, -1.0f / 6.0f},   {-0.25f, 1.0f / 6.0f}, {0.25f, -7.0f / 18.0f}, {-0.375f, -1.0f / 18.0f},
                                 {0.125f, 5.0f / 18.0f}, {-0.125f, -5.0f / 18.0f}, {0.375f, 1.0f / 18.0f}, {-0.4375f, 7.0f / 18.0f}};
constexpr VkFormat kVelocity = VK_FORMAT_R16G16_SFLOAT;

enum : uint32_t {
    kTextured = 1, kUv = 2, kFog = 4, kBlend = 8, kPabe = 16, kFba = 32, kFromTarget = 64, kTarget24 = 128, kDestAlphaBlend = 256,
    kNearest = 512, kSnap = 1024, kAlphaInvert = 2048, kMipmapped = 4096, kGeneratedMips = 8192, kSnapFiltered = 16384,
    kReplaced = 32768, kDateFlips = 65536, kDateKeeps = 131072, kDatm = 262144, kGsExact = 524288,
};

// B24 (GT4_GS_EXACT=1 while it is measured; render.gs_sampling later): texel coordinates and bilinear weights as the GS
// computes them, the same on every GPU (gsn.frag exactCoordinate, targetBilinear).
bool gsExact() {
    static const bool on = [] { const char* e = std::getenv("GT4_GS_EXACT"); return e && *e == '1'; }();
    return on;
}

// A fixed-function blend for C = Ad: the coefficients of Cs and Cd as (a * Ad + b), mapped onto factors.
// Returns false when Vulkan has no factor for it (a * Ad + b with a and b both set, both negative).
bool destAlphaBlend(uint32_t A, uint32_t B, uint32_t D, VkBlendFactor& src, VkBlendFactor& dst, VkBlendOp& op) {
    auto coefficient = [&](uint32_t which, int& a, int& b) {
        a = (A == which ? 1 : 0) - (B == which ? 1 : 0);
        b = D == which ? 1 : 0;
    };
    int sa, sb, da, db;
    coefficient(0, sa, sb);
    coefficient(1, da, db);
    auto factor = [](int a, int b, VkBlendFactor& f, bool& negative) {
        negative = false;
        if (a == 0 && b == 0) f = VK_BLEND_FACTOR_ZERO;
        else if (a == 0 && b == 1) f = VK_BLEND_FACTOR_ONE;
        else if (a == 1 && b == 0) f = VK_BLEND_FACTOR_DST_ALPHA;
        else if (a == -1 && b == 1) f = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        else if (a == -1 && b == 0) { f = VK_BLEND_FACTOR_DST_ALPHA; negative = true; }
        else return false;
        return true;
    };
    bool sn, dn;
    if (!factor(sa, sb, src, sn) || !factor(da, db, dst, dn) || (sn && dn)) return false;
    op = sn ? VK_BLEND_OP_REVERSE_SUBTRACT : dn ? VK_BLEND_OP_SUBTRACT : VK_BLEND_OP_ADD;
    return true;
}

}  // namespace

struct GsnScene::Impl {
    const vk::Device& device;
    VkCommandPool pool;
    SceneFormats formats;
    // Frame lists not drawn yet, in the game's order: all are executed (their draws make what later frames
    // and GS -> host reads see); the last displayed one is shown.
    std::vector<gsn::FrameList> pending;
    gsn::FrameList frame;   // the list being executed
    uint32_t listIndex = 0;   // its position in this submission (per-list buffers)
    bool any = false;
    VkExtent2D lastExtent{640, 448};   // the render extent of the last frame (the scale of reads between frames)
    // Render pixels per GS pixel, per axis: the anamorphic 16:9 frame gets more across than down.
    uint32_t scaleSetting = 0, sx = 1, sy = 1;
    // One vertical scale for every target (sy, from the displayed lines). Until 2026-11-05, while the display showed
    // fields, a target of 384 lines or more got half of it (for the field races' 448-line scene; races are progressive
    // since, so only the menus took that path): the menus pass their background through 1A40 and 2300 every frame,
    // buffers drawn as 224 and as 448 lines, and at mixed scales the loop lost the lower half of the picture (the black
    // band over the Sunday Cup banner after leaving a race entry, at any preset drawing above the PS2's resolution).
    uint32_t verticalScaleFor(const gsn::Target&) const { return sy; }
    // A target narrower than the screen (under 512 GS pixels: GT4's environment maps, the car paint's reflection,
    // the glare's levels) is a texture other draws read, never shown pixel for pixel: at most 2 render pixels per
    // GS pixel each way (a 128 x 256 environment map was drawn at 2 x 5 at 1080p, 0.44 ms of 4.5).
    std::pair<uint32_t, uint32_t> scaleFor(const gsn::Target& t) const {
        if (t.exact) return {1u, 1u};   // pixels the game reads back, or what they are drawn from (gsn::Target::exact)
        const uint32_t x = sx, y = verticalScaleFor(t);
        return t.bw * 64 < 512 ? std::make_pair(std::min(x, 2u), std::min(y, 2u)) : std::make_pair(x, y);
    }
    bool warnedScales = false;

    struct GpuTarget {
        vk::Image image;
        uint32_t width = 0, height = 0;   // GS pixels
        uint32_t scaleX = 1, scaleY = 1;  // render pixels per GS pixel (scaleFor)
        uint32_t psm = 0;
        bool depth = false;
        uint32_t generation = 0;
    };
    std::unordered_map<uint32_t, GpuTarget> targets;
    struct GpuTexture {
        vk::Image image;
        uint32_t version = 0, width = 0, height = 0, levels = 1;
        uint32_t generated = 1;   // mip levels made from level 0 for a texture without GS mipmaps (3D draws)
        bool replaced = false;    // a texture pack's image (gsn::TextureImage::replaced)
        bool stream = false;      // a stream's frame (gsn::TextureImage::stream): no generated mips
    };
    std::unordered_map<uint32_t, GpuTexture> textures;
    // Retired textures, destroyed once the frames that may still sample them are done.
    std::vector<std::pair<uint64_t, vk::Image>> graveyard;
    // Staging and upload buffers outgrown, destroyed the same way (growing one used to wait for the device idle: a
    // movie's first frames stalled once per frame slot, 111 ms on the desktop).
    std::vector<std::pair<uint64_t, vk::Buffer>> bufferGraveyard;
    // Retired texture images, reused by later textures of the same size and levels once the frames that may still
    // sample them are done: a race decodes 6 to 12 textures a frame (palettes the game animates), and an image made
    // and its memory allocated each time took 4 of the render thread's 6.5 ms a frame (0.4 ms an image).
    std::vector<std::pair<uint64_t, vk::Image>> textureGraveyard;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, std::vector<vk::Image>> texturePool;   // (width, height, levels)
    size_t pooledTextures = 0;
    static constexpr size_t kMaxPooledTextures = 256;
    static constexpr size_t kDestroysPerSweep = 64;
    vk::ImageHeap textureHeap;   // the textures' memory (vk.h); destroyed after them (~Impl)
    void retireTexture(vk::Image& image) {
        if (image.valid()) textureGraveyard.emplace_back(uploads, image);
        image = vk::Image{};
    }
    uint64_t uploads = 0;
    vk::Image blank;                     // 1 x 1 for untextured draws
    vk::Image feedback;                  // a copy of a target a draw samples while drawing into it
    vk::Image scratchDepth;              // stencil for destination-alpha tests without a Z buffer
    // Per frame slot and per list executed in it (list * kSlots + slot).
    std::vector<vk::Buffer> vertexBuffers, staging, clutBuffers, indexBuffers;
    std::vector<vk::Buffer> modelJobs, modelData;   // GT4's 3D batches on the GPU (gsn_models.comp)
    struct Pool {
        std::vector<std::unique_ptr<vk::Image>> images;
        size_t used = 0;
    };
    std::unordered_map<uint64_t, Pool> reinterpretPool;   // reinterpreted textures by size (gs_frontend.h)
    std::vector<vk::Image*> reinterpretImages;            // this frame's, per reinterpretation
    std::vector<bool> built;
    vk::Pipeline reinterpret;
    vk::Pipeline models;
    // Lighting of the game's 3D scenes (docs/LIGHTING.md, GsnScene::setLighting).
    struct Lighting {
        float occlusion = 0.0f;      // 0: no ambient occlusion
        uint32_t directions = 6;
        float grade = 0.0f;          // 0: the game's colours
        float tanX = 0.433f * 4.0f / 3.0f, tanY = 0.433f;
    } lighting;
    vk::Pipeline aoPass, lightPass;
    vk::Image aoImage, litImage;
    // lighting.night (PLAN.md F2, CONTRACTS.md c.3; GsnScene::setNightLighting): the view's lamps added in
    // gsn_light.comp, from the clustered light list (F1) and an albedo estimate (gsn_night_lum.comp).
    float night = 0.0f;                        // 0: off; else the added light's strength
    // N4 (GsnScene::setSkippedBrackets): the marked brackets whose draws are not drawn (bit gsn::kBracketFlare, kBracketGlare)
    uint8_t skippedBrackets = 0;
    // B7's parity path (render.scene = parity, GsnScene::setParity; render/modern/parity_view.h): the substituted views'
    // items drawn from the mesh cache by modern_parity.vert. The meshes' raw vertices and triangles live in device-local
    // blocks (parityBlocks, B8 step 1), uploaded through a staging ring before the list's draws; paritySpans the
    // current list's items, by draw, parityAt each draw's first.
    bool parity = false;
    render::ParityView parityView;
    std::vector<vk::Buffer> parityBlocks;            // (index = ParitySpan::block)
    std::vector<vk::Buffer> parityStagingRing;       // per list and slot (buffer())
    std::vector<uint8_t> parityStaging;              // this list's uploads, before they are copied
    std::vector<render::ParityCopy> parityCopies;
    std::vector<render::ParitySpan> paritySpans;
    std::vector<uint32_t> parityAt;   // per draw (FrameList::draws), and one past: [parityAt[d], parityAt[d + 1]) its spans
    // N3 step 1: the modeled vertices of skipped items (ViewOnList::skip, or GT4_N3_SKIP_ALL_GLOW), by draw as parityAt;
    // the draws are recorded around them, in the scene and in its motion vectors
    struct SkipSpan {
        uint32_t draw = 0, first = 0, count = 0;
    };
    std::vector<SkipSpan> skipSpans;
    std::vector<uint32_t> skipAt;
    // ... and where they would have drawn: an R8 image at the scene's render size (gsn_mask.frag), drawn at a Light's
    // relight, read by gsn_light at binding 10
    vk::Pipeline maskPass;
    vk::Image skipMask;
    VkExtent2D skipMaskExtent{};
    bool drawSkipMask(const gsn::FrameList::Lighting& l, size_t lightAt, vk::Image& colour, vk::Image& depth, uint32_t sx, uint32_t sy);
    uint64_t n3MaskDraws = 0;
    uint64_t n3ItemsSkipped = 0, n3Frames = 0, n3OverParity = 0;   // n3OverParity: B7 spans a skip left undrawn
    uint64_t n3ItemsSeen = 0;   // items of main views on frames that pass the gate (the verdicts' denominator)
    uint64_t n3Misaligned = 0;  // views whose skip list isn't one per material (ignored: it's read by index)
    void planSkips();
    std::unordered_map<uint32_t, vk::Pipeline> parityPipelines;
    uint64_t paritySpansOutside = 0;   // spans not inside their draw's vertices (drawn on the GS path; should be 0)
    bool parityPlanned = false;        // a list planned this frame (endPending ends the view's frame then)
    void planParity();
    // B8 step 2 (render.scene = hybrid, PLAN B8): the side channels of each substituted view's colour target (view-space
    // normal, material, view-space depth), written by a pass per GT4 depth partition: before each Z clear of the view's Z
    // buffer and at its Light, the partition's items redrawn with the depth test EQUAL (modern_side.frag), after a reset
    // where the partition wrote Z (gsn_side_reset.*: material 0, GT4's colour as it is).
    bool hybrid = false;
    struct SideImages {
        vk::Image normal, material, depth;
        VkExtent2D grown{};
    };
    std::unordered_map<uint32_t, SideImages> sideTargets;   // by the view's colour target
    struct SideBound {
        size_t command = 0, from = 0;   // the pass runs before `command`, for the view's items drawn from `from` on
        uint32_t view = 0;              // a view drawing there (FrameList::views): its target and Z buffer name the pass's;
                                        // the pass draws the items of every view on that target (the main view and
                                        // the mirror share one: R5), with one reset
        float clearDepth = 0.0f;        // the Z the partition's own Z clear left (gsn.vert's sprite depth)
        uint32_t clearRect[4] = {};     // ... over its scissor (GS pixels x0, y0, x1, y1 inclusive): the partition's region
                                        // (the mirror's clear covers its own rectangle only)
        bool cleared = false;           // the partition began with a Z clear (else no reset)
        bool light = false;             // the pass before the view's Light (its last: GT4_SIDE_VIEW shows it)
    };
    std::vector<SideBound> sideBounds;  // this list's, by command
    size_t sideNext = 0;
    std::vector<size_t> spanCommand;    // per span: its draw's command
    std::unordered_set<uint32_t> sideClearedThisList;
    std::vector<vk::Buffer> sideItemRing;
    vk::Buffer* sideItems = nullptr;
    vk::Pipeline sidePass, sideResetPass, sideViewPass, sideVeilPass;
    uint64_t sidePasses = 0, sideItemsDrawn = 0, sideResets = 0, sideVeils = 0;
    void planSides();
    void runSide(const SideBound& bound);
    vk::Pipeline& parityPipeline(uint32_t blendKey, uint32_t mask, bool depth, bool exports);
    vk::Pipeline nightLumPass;
    render::LightClusters clusters;
    vk::Image nightLocal;                      // (sum of log luminance, count) per 8 x 8 block
    vk::Buffer nightCells, nightIndices, nightStats;   // written by the cluster build, read by gsn_light.comp
    // Per Light command of a list (its ordinal), per (list, slot): the light list's header and lights and the cluster
    // grid, which the CPU writes (separate buffers: the cluster build binds each at offset 0).
    std::vector<std::vector<vk::Buffer>> nightHeaders, nightLists, nightGrids;
    uint32_t nightOrdinal = 0;                 // Light commands of the current list relit so far
    std::vector<gfx::scene::Light> nightRecords;
    size_t nightHostFrom = SIZE_MAX;           // nightRecords from here on are the host's (F2b's course lamps, test lamps)
    std::vector<render::GpuLight> nightLights;
    render::GatherStats nightStatsLast;
    // F2b: the course's street lamps from B6's meshes (course_lamp_pass.h), and its totals (printed at exit)
    render::CourseLampPass lampPass;
    struct LampTotals {
        uint64_t frames = 0, candidates = 0, kept = 0, dropped = 0, noInstance = 0, merged = 0, mergedAgain = 0;
        double microseconds = 0.0;   // the pass's time on the render thread
    } lampTotals;
    uint64_t nightSplitSkipped = 0;   // Lights not relit: their target holds more than one main view (split screen)
    // gsn_light.comp's push constants from `night` on (offset 40): mirrored field for field.
    struct NightPush {
        float night = 0.0f, skipMasked = 0.0f;   // skipMasked: 1 when binding 9 holds the Light's skip mask (N3)
        float focal[2] = {1.0f, 1.0f}, centre[2] = {0.0f, 0.0f};
        float distanceA = 0.0f, distanceB = 0.0f, maxDistance = 0.0f, pad1 = 0.0f;
        float inset[4] = {1.0f, 1.0f, 0.0f, 0.0f};   // x1 < x0: no mirror
    };
    static_assert(sizeof(NightPush) == 56);
    // gsn_light.comp's push constants, whole.
    struct LightPush {
        float size[2], occlusion, zScale, grade, contrast, vibrance, warmth;
        uint32_t view, aoStep;
        NightPush night;
    };
    static_assert(sizeof(LightPush) == 96 && offsetof(LightPush, night) == 40 && offsetof(LightPush, night.focal) == 48 &&
                  offsetof(LightPush, night.inset) == 80);
    struct NightBuffers {
        vk::Buffer* header = nullptr;
        vk::Buffer* lights = nullptr;
        VkDeviceSize lightBytes = sizeof(render::GpuLight);
    };
    static void computeBarrier(VkCommandBuffer command, VkAccessFlags2 src, VkAccessFlags2 dst) {
        VkMemoryBarrier2 m{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        m.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        m.srcAccessMask = src;
        m.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        m.dstAccessMask = dst;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &m;
        vkCmdPipelineBarrier2(command, &d);
    }
    vk::Image gradeImage;                      // the colour grading's result (gsn_grade.comp), copied back
    std::vector<vk::Buffer> curveBuffers;      // its curves, per frame
    // The scene stage's temporal anti-aliasing (docs/NATIVE_RENDERER.md): the 3D draws of a scene are jittered,
    // drawn again for their motion vectors (velocityPass) and resolved against the scene's history.
    bool temporal = false;
    uint64_t displayedFrames = 0;   // frames shown so far (the jitter's phase, the histories' age)
    uint64_t framesSkipped = 0;     // complete frames not drawn, behind the game (addFrames, GT4_FRAME_SKIP)
    GsnScene::Counters counters;     // the hitch log's (framesSkipped kept above)
    std::chrono::steady_clock::time_point skipUntil{};   // ... skipping until then
    gsn::FrameList heldBack;                           // GT4_FRAME_SKIP_TEST: the frame held back
    bool held = false;
    uint64_t skipTestCount = 0;
    size_t skipOlderFrames(size_t keepComplete);
    // GS -> host reads (GsnScene::read): how many, the frames skipped ahead of them, and GT4_READ_SKIP_TEST's frames
    // held back (the newest lists, drawn only at the next read: a backlog for a lockstep replay's reads).
    uint64_t reads = 0, readSkips = 0;
    // B16c's display seam (GsnScene::setDisplaySeam; gsn::findSeamAnchor, gsn_seam.comp). Each list's plan is made at
    // executeBegin; the plan's Light keeps its motion vectors past its scene stage (keepSeamVelocity, at the end of the
    // motion vectors' pass), and right after the anchor runSeam maps them and the Z buffer onto the made picture: one
    // velocity and one depth image per made target, valid while its latest picture had an anchor.
    bool seamOn = [] { const char* e = std::getenv("GT4_B16C_SEAM"); return e && *e == '1'; }();
    gsn::SeamAnchor seamPlan;
    bool seamVelocityKept = false, seamDone = false;
    float displayedGs[2] = {640.0f, 448.0f};   // the displayed rectangle's size, GS pixels (executeFinish)
    vk::Image seamLightVelocity;
    struct Seam {
        vk::Image velocity, depth;
        bool valid = false;
    };
    std::unordered_map<uint32_t, Seam> seams;
    vk::Pipeline seamPass, seamKeepPass, displaySeam;   // (made when the seam first runs)
    void makeDisplaySeam();
    uint64_t seamOutcomes[6] = {};   // displayed lists by gsn::SeamAnchor::Outcome
    uint64_t seamRuns = 0;
    void keepSeamVelocity(VkCommandBuffer command, vk::Image& velocity);
    void runSeam(VkCommandBuffer command);
    // A rectangle of a target copied into a host buffer (GsnScene::read, FrameList::readbacks), packed as the GS keeps
    // it once the GPU has done the copy.
    struct Copied {
        gsn::ReadRequest request;
        vk::Buffer pixels;
        uint32_t w = 0, h = 0, scaleX = 1, scaleY = 1;
        gsn::FrameList::Readback readback;   // (an asynchronous read's: who asked)
    };
    bool copyRect(VkCommandBuffer command, const gsn::ReadRequest& request, Copied& c);
    void pack(Copied& c, std::vector<uint8_t>& out);   // (frees the buffer)
    // Asynchronous reads (FrameList::readbacks): copied in the frame being recorded, then in flight with its slot, then
    // packed for GsHost::answerRead once that slot's fence is signalled (GsnScene::slotDone).
    std::vector<Copied> readbacksRecorded, readbacksInFlight[kSlots];
    std::vector<GsnScene::ReadAnswer> readAnswers;
    std::chrono::steady_clock::time_point readbacksWaitingSince{};   // a pending list's, not drawn yet (zero: none)
    void finishReadbacks(std::vector<Copied>& done) {
        for (Copied& c : done) {
            GsnScene::ReadAnswer a{c.readback.id, c.readback.qwc, {}};
            if (c.pixels.buffer) pack(c, a.pixels);
            readAnswers.push_back(std::move(a));
        }
        done.clear();
    }
    // Targets a frame was seen reading before writing them: their content is kept from frame to frame (an environment
    // map a later frame samples), so a frame that writes one is never skipped as if it were scratch (skipOlderFrames).
    std::unordered_set<uint32_t> carried;
    void noteCarried(const gsn::FrameList& list);
    std::vector<gsn::FrameList> readHeld;
    // Presentation decoupled from the game's 60 Hz (docs/NATIVE_V2.md, step 4): the last list drawn again at each
    // display refresh with its 3D batches' transforms blended from the previous frame's by modelT, the fraction of
    // a game frame since the list came (gsn_models.comp); modelTPrevious: the one shown before (motion vectors).
    bool interpolate = true;
    std::chrono::steady_clock::time_point arrival{};
    float modelT = 1.0f, modelTPrevious = 0.0f;
    // setFixedClock (gt4_host's main.cpp with --fixed-step: replays and sessions): the phase counts presents instead
    // of the wall clock, a present being one 60 Hz tick of the game's clock, so from the first present after a
    // list's arrival it is 1.0, the same in every run (the wall clock's phase there was the scheduler's timing: two
    // replays of race7 with interpolation on had 1 frame of 100 alike).
    bool fixedClock = false;
    float sinceArrival() const {
        if (fixedClock) return 1.0f;
        return std::clamp(std::chrono::duration<float>(std::chrono::steady_clock::now() - arrival).count() * 60.0f, 0.0f, 1.0f);
    }
    vk::Pipeline velocityPass, taaPass;
    vk::Image velocityImage;
    struct History {
        vk::Image image[2];
        uint32_t current = 0;    // the image holding the last resolve
        uint64_t used = 0;       // displayedFrames when last resolved
        bool valid = false;
    };
    std::unordered_map<uint32_t, History> histories;   // by scene target
    // The scene stage's other effects (GsnScene::setEffects), each a compute pass over the scene.
    SceneEffects effects;
    vk::Pipeline ssrPass, ssrComposite, fogPass, blurPass, dofPass, gradePass;
    vk::Image ssrImage;   // the reflections at half resolution (gsn_ssr.comp)
    // The scene stage's work images (velocityImage, aoImage, litImage, ssrImage) as the stage code finds them: these
    // members (executeLight: the GS -> host read, a list drawn again, a Light the render graph keeps in one pass), or
    // the render graph's transients (GsnScene::buildPrepass), which the graph synchronises (no barrier of the stage's).
    struct WorkImages {
        vk::Image* velocity = nullptr;
        vk::Image* ao = nullptr;
        vk::Image* lit = nullptr;
        vk::Image* ssr = nullptr;
        bool graph = false;
    };
    // Their sizes, grown as one image of each kind grows (ensureWork): kept at least as large as any area asked for,
    // made again exactly as large as the area that did not fit. The members are made at these; so are the graph's
    // transients, from the growth the graph's build plans for its frame.
    VkExtent2D velocityExtent{}, aoExtent{}, litExtent{}, ssrExtent{};
    static void grow(VkExtent2D& grown, VkExtent2D e) {
        if (grown.width && grown.height && grown.width >= e.width && grown.height >= e.height) return;
        grown = e;
    }
    void ensureWork(vk::Image& image, VkExtent2D& grown, VkExtent2D e, VkFormat format, VkImageUsageFlags usage, const char* name) {
        grow(grown, e);
        if (image.valid() && image.extent.width == grown.width && image.extent.height == grown.height) return;
        if (image.valid()) retire(image);
        vk::ImageDesc desc;
        desc.format = format;
        desc.extent = grown;
        desc.usage = usage;
        desc.name = name;
        image = vk::createImage(device, desc);
    }
    // A Z buffer's depth plane as a sampled view (kept with the image's other views, destroyed with it).
    VkImageView depthView(vk::Image& image) {
        if (!image.mipViews.empty()) return image.mipViews[0];
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image.image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = image.format;
        info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        vk::check(vkCreateImageView(device.device, &info, nullptr, &view), "vkCreateImageView(depth)");
        image.mipViews.push_back(view);
        return view;
    }
    // GT4_GSN_DUMP=VBLANK: every colour target after that frame, read back and written as gsn_<vblank>_<id>.ppm
    // (colour / 255 as 8-bit RGB) once the GPU is done with it.
    struct Dump {
        uint64_t vblank = 0;
        std::vector<std::pair<uint32_t, vk::Buffer>> images;   // target id, its pixels (RGBA16F; a Z buffer's depth: float)
        std::vector<VkExtent2D> extents;
        std::vector<uint32_t> bps;
        std::vector<uint8_t> depth;                            // 1: a Z buffer (GT4_GSN_DUMP_DEPTH=1)
    } dump;
    void writeDump();
    // GT4_GPU_PROFILE=1 (performance work): GPU timestamps between labelled points of the frames executed here
    // (each label takes the time since the point before it), and counts of what the GPU was asked to do,
    // averaged per frame and printed when the scene goes.
    struct Profile {
        bool on = false;
        int level = 1;   // 2: each render pass by its target, the reinterpretations, the stencil copies
        std::deque<std::string> labelStore;
        std::unordered_map<uint64_t, const char*> passLabels;
        VkQueryPool pool = VK_NULL_HANDLE;
        static constexpr uint32_t kPerSlot = 256;
        std::vector<const char*> labels[kSlots];
        std::map<std::string, double> milliseconds;
        std::map<std::string, uint64_t> counts;
        uint64_t frames = 0;
        std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    } profile;
    void tally(const char* what, uint64_t n = 1) {
        if (profile.on) profile.counts[what] += n;
    }
    int markSlot = -1;   // the slot of the frame being profiled (-1: none, e.g. a read's own submission)
    void mark(VkCommandBuffer command, const char* label) {
        if (!profile.on || markSlot < 0) return;
        auto& l = profile.labels[markSlot];
        if (l.size() + 1 >= Profile::kPerSlot) return;
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, profile.pool, uint32_t(markSlot) * Profile::kPerSlot + uint32_t(l.size()) + 1);
        l.push_back(label);
    }
    // At the start of a frame in `slot`: the results of the slot's last frame (done: the renderer waited for it),
    // then its queries reset and the first timestamp.
    void profileFrame(VkCommandBuffer command, uint32_t slot) {
        if (!profile.on) return;
        slot %= kSlots;
        auto& l = profile.labels[slot];
        if (!l.empty()) {
            std::vector<uint64_t> t(l.size() + 1);
            if (vkGetQueryPoolResults(device.device, profile.pool, slot * Profile::kPerSlot, uint32_t(t.size()), t.size() * 8, t.data(), 8,
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                const double period = device.properties.limits.timestampPeriod * 1e-6;
                for (size_t k = 1; k < t.size(); ++k) profile.milliseconds[l[k - 1]] += double(t[k] - t[k - 1]) * period;
                ++profile.frames;
            }
            l.clear();
            // Every 10 s: the frames since the last report, then counted afresh (a run's slow stretch on its own,
            // beside the host's perf lines).
            const auto now = std::chrono::steady_clock::now();
            if (now - profile.since >= std::chrono::seconds(10)) {
                printProfile();
                profile.milliseconds.clear();
                profile.counts.clear();
                profile.frames = 0;
                profile.since = now;
            }
        }
        vkCmdResetQueryPool(command, profile.pool, slot * Profile::kPerSlot, Profile::kPerSlot);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, profile.pool, slot * Profile::kPerSlot);
    }
    void printProfile() const {
        if (!profile.on || !profile.frames) return;
        const double f = double(profile.frames);
        double total = 0.0;
        for (const auto& [label, ms] : profile.milliseconds) total += ms;
        std::printf("GPU profile of the native renderer, %llu frames: %.3f ms a frame\n", static_cast<unsigned long long>(profile.frames), total / f);
        std::vector<std::pair<double, std::string>> rows;
        for (const auto& [label, ms] : profile.milliseconds) rows.emplace_back(ms / f, label);
        std::sort(rows.rbegin(), rows.rend());
        for (size_t k = 0; k < rows.size() && k < 16; ++k)   // the dearest 16
            std::printf("  %-28s %7.3f ms\n", rows[k].second.c_str(), rows[k].first);
        std::printf("  per frame:");
        for (const auto& [what, n] : profile.counts) std::printf(" %s %.1f,", what.c_str(), double(n) / f);
        std::printf("\n");
    }

    std::unordered_map<uint32_t, vk::Pipeline> pipelines;
    vk::Pipeline display, stencilPrep;
    VkSampler samplers[4] = {};   // [linear][repeat]
    VkSampler mipSamplers[4] = {};   // [repeat U * 2 + repeat V]: mipmapped textures, trilinear, anisotropic
    uint32_t displayTarget = 0;
    uint32_t gameShown = 0;   // interpolating: the buffer the display registers showed for the list before
    float displayRect[4] = {0, 0, 1, 1};
    // Field mode: the field shown before (its buffer and its rectangle's top) and how much of it the display mixes in.
    uint32_t lastFieldTarget = 0, lastFieldY = 0, blendTarget = 0;
    float otherFieldRect[4] = {0, 0, 1, 1};
    float fieldBlend = 0.0f;
    float fieldLine = 0.0f;    // one GS line of the displayed rectangle, normalised (the clean field mode)
    bool fieldClean = false;   // display.field_mode = clean (setFieldMode)
    bool warnedBlend = false;

    Impl(const vk::Device& d, VkCommandPool p) : device(d), pool(p) {
        if (const char* e = std::getenv("GT4_GPU_PROFILE"); e && *e && *e != '0') {
            profile.on = startProfile();
            profile.level = std::atoi(e);
        }
        for (int linear = 0; linear < 2; ++linear)
            for (int repeat = 0; repeat < 2; ++repeat)
                samplers[linear * 2 + repeat] = vk::createSampler(device, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                                                  repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        makeMipSamplers(16.0f);
    }

    // The GPU profile's timestamps (GT4_GPU_PROFILE).
    bool startProfile() {
        if (profile.pool) return true;
        VkQueryPoolCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        q.queryType = VK_QUERY_TYPE_TIMESTAMP;
        q.queryCount = Profile::kPerSlot * kSlots;
        return vkCreateQueryPool(device.device, &q, nullptr, &profile.pool) == VK_SUCCESS;
    }

    // The mipmapped textures' samplers at an anisotropy (texture.anisotropy; 1 = off).
    float mipAnisotropy = 0.0f;
    void makeMipSamplers(float anisotropy) {
        anisotropy = std::clamp(anisotropy, 1.0f, std::max(1.0f, device.maxAnisotropy));
        if (anisotropy == mipAnisotropy) return;
        if (mipSamplers[0]) vkDeviceWaitIdle(device.device);
        for (VkSampler& s : mipSamplers)
            if (s) { vkDestroySampler(device.device, s, nullptr); s = VK_NULL_HANDLE; }
        mipAnisotropy = anisotropy;
        for (int uv = 0; uv < 4; ++uv) {   // the address mode per axis (WMS, WMT)
            VkSamplerCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            info.magFilter = info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.addressModeU = (uv & 2) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeV = (uv & 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.anisotropyEnable = device.anisotropy && anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
            info.maxAnisotropy = anisotropy;
            info.maxLod = VK_LOD_CLAMP_NONE;
            vk::check(vkCreateSampler(device.device, &info, nullptr, &mipSamplers[uv]), "vkCreateSampler");
        }
    }
    ~Impl() {
        vkDeviceWaitIdle(device.device);
        printProfile();
        if (framesSkipped) std::printf("Frames skipped behind the game: %llu\n", static_cast<unsigned long long>(framesSkipped));
        if (lampTotals.frames) {   // F2b's course lamps (per execution: an interpolated present runs a list again)
            const LampTotals& t = lampTotals;
            std::printf("F2b course lamps: %llu night executions, %llu billboard candidates: %llu lamps (%.1f an execution), "
                        "%llu not glows, %llu without an instance or material, %llu already a game record (%llu of them a "
                        "second time); %.1f us an execution\n",
                        static_cast<unsigned long long>(t.frames), static_cast<unsigned long long>(t.candidates),
                        static_cast<unsigned long long>(t.kept), double(t.kept) / double(t.frames),
                        static_cast<unsigned long long>(t.dropped), static_cast<unsigned long long>(t.noInstance),
                        static_cast<unsigned long long>(t.merged), static_cast<unsigned long long>(t.mergedAgain),
                        t.microseconds / double(t.frames));
        }
        if (n3Frames)
            std::printf("N3: %llu frames with the lamps on and a main view (%llu items), %llu items skipped, %llu of them B7 "
                        "would have substituted%s\n",
                        static_cast<unsigned long long>(n3Frames), static_cast<unsigned long long>(n3ItemsSeen),
                        static_cast<unsigned long long>(n3ItemsSkipped), static_cast<unsigned long long>(n3OverParity),
                        std::getenv("GT4_N3_SKIP_TEST") ? " (GT4_N3_SKIP_TEST)" : "");
        if (n3Misaligned)
            std::printf("N3: %llu views ignored, their skip list not one per material\n",
                        static_cast<unsigned long long>(n3Misaligned));
        if (nightSplitSkipped)
            std::printf("Night relight: %llu Lights without our lamps, their target holding more than one main view "
                        "(split screen: one light pass per half not yet)\n",
                        static_cast<unsigned long long>(nightSplitSkipped));
        if (counters.flareDrawsSkipped || counters.glareDrawsSkipped)
            std::printf("N4 draws skipped onto the picture: flare %llu, glare %llu\n",
                        static_cast<unsigned long long>(counters.flareDrawsSkipped), static_cast<unsigned long long>(counters.glareDrawsSkipped));
        if (stagesOnTransients || stagesOnMembers)
            std::printf("Scene stages run on the members: %llu (the build's plan was wrong); on the render graph's transients: %llu\n",
                        static_cast<unsigned long long>(stagesOnMembers), static_cast<unsigned long long>(stagesOnTransients));
        vk::destroy(device, seamLightVelocity);
        for (auto& [id, seam] : seams)
            for (vk::Image* i : {&seam.velocity, &seam.depth}) vk::destroy(device, *i);
        vk::destroy(device, seamPass);
        vk::destroy(device, seamKeepPass);
        vk::destroy(device, displaySeam);
        if (seamOn)
            std::printf("Display seam (B16c): %llu displayed lists; chained to a Light %llu (seams made %llu), fields mode %llu, no "
                        "anchor %llu, ending at no Light %llu, capped %llu\n",
                        (unsigned long long)(seamOutcomes[0] + seamOutcomes[1] + seamOutcomes[3] + seamOutcomes[4] + seamOutcomes[5]),
                        (unsigned long long)seamOutcomes[0], (unsigned long long)seamRuns, (unsigned long long)seamOutcomes[1],
                        (unsigned long long)seamOutcomes[3], (unsigned long long)seamOutcomes[4], (unsigned long long)seamOutcomes[5]);
        if (readSkips) std::printf("... of them ahead of GS reads: %llu (of %llu reads)\n", static_cast<unsigned long long>(readSkips),
                                   static_cast<unsigned long long>(reads));
        if (profile.pool) vkDestroyQueryPool(device.device, profile.pool, nullptr);
        if (readGraph) readGraph->destroy();
        for (auto& [id, t] : targets) vk::destroy(device, t.image);
        for (auto& [id, t] : textures) vk::destroy(device, t.image);
        for (auto& [when, image] : graveyard) vk::destroy(device, image);
        for (auto& [when, buf] : bufferGraveyard) vk::destroy(device, buf);
        for (auto& [when, image] : textureGraveyard) vk::destroy(device, image);
        for (auto& [key, images] : texturePool)
            for (vk::Image& image : images) vk::destroy(device, image);
        textureHeap.destroy(device);
        vk::destroy(device, blank);
        vk::destroy(device, feedback);
        vk::destroy(device, scratchDepth);
        for (auto& b : vertexBuffers) vk::destroy(device, b);
        for (auto& b : staging) vk::destroy(device, b);
        for (auto& b : clutBuffers) vk::destroy(device, b);
        for (auto& b : modelJobs) vk::destroy(device, b);
        for (auto& b : modelData) vk::destroy(device, b);
        for (auto& b : indexBuffers) vk::destroy(device, b);
        for (auto& [size, pool] : reinterpretPool)
            for (auto& i : pool.images) vk::destroy(device, *i);
        vk::destroy(device, reinterpret);
        vk::destroy(device, models);
        vk::destroy(device, aoPass);
        vk::destroy(device, lightPass);
        vk::destroy(device, aoImage);
        vk::destroy(device, litImage);
        vk::destroy(device, nightLumPass);
        clusters.destroy(device);
        vk::destroy(device, nightLocal);
        vk::destroy(device, nightCells);
        vk::destroy(device, nightIndices);
        vk::destroy(device, nightStats);
        for (auto& ring : nightHeaders)
            for (auto& b : ring) vk::destroy(device, b);
        for (auto& ring : nightLists)
            for (auto& b : ring) vk::destroy(device, b);
        for (auto& ring : nightGrids)
            for (auto& b : ring) vk::destroy(device, b);
        vk::destroy(device, gradeImage);
        for (auto& b : curveBuffers) vk::destroy(device, b);
        vk::destroy(device, velocityPass);
        vk::destroy(device, maskPass);
        vk::destroy(device, skipMask);
        vk::destroy(device, taaPass);
        for (vk::Pipeline* p : {&ssrPass, &ssrComposite, &fogPass, &blurPass, &dofPass, &gradePass}) vk::destroy(device, *p);
        vk::destroy(device, ssrImage);
        vk::destroy(device, velocityImage);
        for (auto& [id, h] : histories)
            for (vk::Image& i : h.image) vk::destroy(device, i);
        for (auto& [key, p] : pipelines) vk::destroy(device, p);
        for (auto& [key, p] : parityPipelines) vk::destroy(device, p);
        for (vk::Buffer& b : parityBlocks) vk::destroy(device, b);
        for (vk::Buffer& b : parityStagingRing) vk::destroy(device, b);
        for (auto& [id, side] : sideTargets)
            for (vk::Image* i : {&side.normal, &side.material, &side.depth}) vk::destroy(device, *i);
        for (vk::Buffer& b : sideItemRing) vk::destroy(device, b);
        vk::destroy(device, sidePass);
        vk::destroy(device, sideResetPass);
        vk::destroy(device, sideViewPass);
        vk::destroy(device, sideVeilPass);
        if (hybrid)
            std::printf("Hybrid side channels: %llu passes (%llu with a reset), %llu items drawn, %llu blended draws marking veiled\n",
                        static_cast<unsigned long long>(sidePasses), static_cast<unsigned long long>(sideResets),
                        static_cast<unsigned long long>(sideItemsDrawn), static_cast<unsigned long long>(sideVeils));
        if (parity) {   // B7's fallbacks by reason (PLAN B7's acceptance lists them)
            const render::ParityStats& s = parityView.stats();
            std::printf("Parity path: %llu of %llu items drawn from the mesh cache; on the GS path: mesh 0 %llu, mesh evicted %llu, "
                        "items differ %llu, outside their draw %llu, over the budget %llu; views not substituted %llu, of a role kept %llu, light-map and reflection items %llu, on the GS "
                        "path for a game read %llu (cut for one) + %llu (asynchronous); meshes "
                        "uploaded %llu (%.1f MiB), retired %llu; %u blocks of %llu MiB%s\n",
                        static_cast<unsigned long long>(s.drawn), static_cast<unsigned long long>(s.items),
                        static_cast<unsigned long long>(s.meshNone), static_cast<unsigned long long>(s.meshGone),
                        static_cast<unsigned long long>(s.itemsDiffer), static_cast<unsigned long long>(paritySpansOutside),
                        static_cast<unsigned long long>(s.overBudget), static_cast<unsigned long long>(s.viewsNotSubstituted),
                        static_cast<unsigned long long>(s.viewsOfRoleKept), static_cast<unsigned long long>(s.itemsOutOfBinding),
                        static_cast<unsigned long long>(s.viewsForReadSync), static_cast<unsigned long long>(s.viewsForReadAsync),
                        static_cast<unsigned long long>(s.meshesPlaced), double(s.uploadBytes) / double(1 << 20),
                        static_cast<unsigned long long>(s.meshesRetired), parityView.blocks(),
                        static_cast<unsigned long long>(parityView.blockSize() >> 20),
                        device.provokingLast ? "" : " (triangles rotated: no last provoking vertex)");
        }
        vk::destroy(device, display);
        vk::destroy(device, stencilPrep);
        for (VkSampler s : samplers)
            if (s) vkDestroySampler(device.device, s, nullptr);
        for (VkSampler s : mipSamplers)
            if (s) vkDestroySampler(device.device, s, nullptr);
    }

    // Pipelines by blend, colour write mask, depth attachment, topology and whether the shader exports the stencil
    // (alpha bit 7: VK_EXT_shader_stencil_export).
    vk::Pipeline& pipeline(uint32_t blendKey, uint32_t mask, bool depth, gsn::Topology topology, bool exports = false) {
        const uint32_t key = blendKey | (mask << 16) | (depth ? 1u << 20 : 0) | (static_cast<uint32_t>(topology) << 21) |
                             (exports ? 1u << 24 : 0);
        auto it = pipelines.find(key);
        if (it != pipelines.end()) return it->second;
        const vk::GraphicsDesc desc = drawDesc(blendKey, mask, depth, topology, exports);
        ++counters.pipelines;
        return pipelines[key] = vk::createGraphics(device, desc);
    }
    // A GS draw's pipeline description (pipeline(); parityPipeline() changes its vertex stage).
    vk::GraphicsDesc drawDesc(uint32_t blendKey, uint32_t mask, bool depth, gsn::Topology topology, bool exports) const {
        vk::GraphicsDesc desc;
        // (B24: the GS-exact variants while GT4_GS_EXACT is on; the default modules are main's to the byte)
        desc.vertex = gsExact() ? shaders::gsn_vert_exact() : shaders::gsn_vert();
        desc.fragment = gsExact() ? (exports ? shaders::gsn_frag_export_exact() : shaders::gsn_frag_exact())
                                  : (exports ? shaders::gsn_frag_export() : shaders::gsn_frag());
        desc.colors = {kColour};
        desc.depth = depth ? kDepth : VK_FORMAT_UNDEFINED;
        desc.stencil = depth ? kDepth : VK_FORMAT_UNDEFINED;
        desc.topology = topology == gsn::Topology::Points ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                        : topology == gsn::Topology::Lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                           : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        desc.vertices.stride = sizeof(gsn::Vertex);
        desc.vertices.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(gsn::Vertex, x)},
                                    {1, 0, VK_FORMAT_R32_UINT, offsetof(gsn::Vertex, z)},
                                    {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(gsn::Vertex, rgba)},
                                    {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(gsn::Vertex, s)},
                                    {4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(gsn::Vertex, u)},
                                    {5, 0, VK_FORMAT_R32_UINT, offsetof(gsn::Vertex, fog)},
                                    {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(gsn::Vertex, minU)},
                                    {8, 0, VK_FORMAT_R32_SFLOAT, offsetof(gsn::Vertex, w)}};
        desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
        desc.pushSize = sizeof(DrawConstants);
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = mask;
        blend.blendEnable = VK_TRUE;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // the GS writes the source alpha as it is
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        if (blendKey & 0x8000u) {   // C = Ad: fixed factors
            blend.srcColorBlendFactor = static_cast<VkBlendFactor>(blendKey & 0x1F);
            blend.dstColorBlendFactor = static_cast<VkBlendFactor>((blendKey >> 5) & 0x1F);
            blend.colorBlendOp = static_cast<VkBlendOp>((blendKey >> 10) & 0x1F);
        } else {   // Cs * s from the shader, Cd * d (source 1)
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = device.dualSourceBlend ? VK_BLEND_FACTOR_SRC1_COLOR : VK_BLEND_FACTOR_ZERO;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
        }
        desc.blendStates = {blend};
        desc.dynamics = {VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
                         VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                         VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        desc.name = "gsn draw";
        return desc;
    }

    // Whether a draw maps its texture's texels 1:1 onto GS pixels (within 3 %) on both axes.
    bool oneToOne(const gsn::Draw& d, const DrawConstants& k) const {
        if (d.modeled) return false;   // 3D on the GPU: its vertices are not here
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        const bool uv = (k.flags & kUv) != 0;
        for (uint32_t i = 0; i < d.count; ++i) {
            const gsn::Vertex& v = frame.vertices[d.first + i];
            if (v.w != 0.0f) return false;   // 3D (homogeneous): not a texture laid on the screen
            const float q = v.q != 0.0f ? v.q : 1.0f;
            const float tu = uv ? v.u : v.s / q * k.textureSize[0], tv = uv ? v.v : v.t / q * k.textureSize[1];
            x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
            u0 = std::min(u0, tu); u1 = std::max(u1, tu); v0 = std::min(v0, tv); v1 = std::max(v1, tv);
        }
        const float dx = x1 - x0, dy = y1 - y0;
        if (dx < 1.0f || dy < 1.0f) return true;
        return std::fabs((u1 - u0) / dx - 1.0f) < 0.03f && std::fabs((v1 - v0) / dy - 1.0f) < 0.03f;
    }

    // A target as ensureTarget decides about it: its image's presence and extent and the rest of GpuTarget. The render
    // graph's build runs the same decisions on copies to know the scene stage's sizes ahead (GsnScene::buildPrepass).
    struct TargetShape {
        bool valid = false;
        VkExtent2D extent{};
        uint32_t width = 0, height = 0, scaleX = 1, scaleY = 1, psm = 0, generation = 0;
        bool depth = false;
    };
    static TargetShape shapeOf(const GpuTarget& g) {
        return {g.image.valid(), g.image.extent, g.width, g.height, g.scaleX, g.scaleY, g.psm, g.generation, g.depth};
    }
    // ensureTarget's decisions for `t` at `scale` (scaleFor): `dropped` when the slot holds a new buffer (the image
    // retired, the target made afresh); true when a new image of g.extent is made (one still held retired first).
    static bool reshapeTarget(TargetShape& g, const gsn::Target& t, std::pair<uint32_t, uint32_t> scale, bool& dropped) {
        dropped = g.valid && (g.generation != t.generation || g.depth != t.depth);   // the slot holds a new buffer
        if (dropped) g = TargetShape{};
        g.generation = t.generation;
        const auto [tx, ty] = scale;
        // Within Vulkan's guaranteed image size at this scale (a 2048-line target at 8x is 16384 tall).
        const uint32_t width = std::min(std::max<uint32_t>(64, t.bw * 64), 16384 / tx),
                       height = std::min(std::max<uint32_t>(32, t.height), 16384 / ty);
        if (g.valid && g.width >= width && g.height >= height && g.scaleX == tx && g.scaleY == ty &&
            g.extent.width == g.width * tx && g.extent.height == g.height * ty) {
            g.psm = t.psm;
            return false;
        }
        if (g.scaleX != tx || g.scaleY != ty) g.width = g.height = 0;   // another scale: sized afresh
        g.scaleX = tx;
        g.scaleY = ty;
        g.width = std::max(g.width, width);
        g.height = std::max(g.height, height);
        g.depth = t.depth;
        g.psm = t.psm;
        g.valid = true;
        g.extent = {g.width * tx, g.height * ty};
        return true;
    }
    // The scene stage's area at a Light (render pixels) over its colour target `c` and Z buffer `d`; false when they
    // are no scene (not a colour target and a Z buffer, either without an image, or at two scales).
    static bool sceneArea(const TargetShape& c, const TargetShape& d, const gsn::FrameList::Lighting& l, VkExtent2D& area) {
        if (c.depth || !d.depth) return false;
        if (!c.valid || !d.valid) return false;
        if (d.scaleX != c.scaleX || d.scaleY != c.scaleY) return false;   // (never seen: they pair by size)
        VkExtent2D e{std::min(c.extent.width, d.extent.width), std::min(c.extent.height, d.extent.height)};
        if (l.width && l.height) e = {std::min(e.width, l.width * c.scaleX), std::min(e.height, l.height * c.scaleY)};
        area = e;
        return true;
    }
    void ensureTarget(const gsn::Target& t) {
        GpuTarget& g = targets[t.id];
        TargetShape shape = shapeOf(g);
        bool dropped = false;
        const bool make = reshapeTarget(shape, t, scaleFor(t), dropped);
        if (dropped) {
            retire(g.image);
            g = GpuTarget{};
        }
        g.width = shape.width;
        g.height = shape.height;
        g.scaleX = shape.scaleX;
        g.scaleY = shape.scaleY;
        g.psm = shape.psm;
        g.generation = shape.generation;
        g.depth = shape.depth;
        if (!make) return;
        if (g.image.valid()) retire(g.image);   // still used by frames in flight or earlier lists of this one
        vk::ImageDesc desc;
        desc.format = t.depth ? kDepth : kColour;
        desc.extent = shape.extent;
        // (GT4_GSN_DUMP_DEPTH=1, B7's Z comparison: the Z buffers copied out too, so TRANSFER_SRC; not otherwise, which could
        // cost the depth compression)
        static const bool depthDumped = [] { const char* e = std::getenv("GT4_GSN_DUMP_DEPTH"); return e && *e == '1'; }();
        desc.usage = t.depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                   (depthDumped ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u)
                             : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.name = t.depth ? "gsn depth" : "gsn target";
        g.image = vk::createImage(device, desc);
    }

    void clearNew(VkCommandBuffer command, GpuTarget& g) {
        vk::Barriers b;
        b.image(g.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
        b.flush(command);
        if (g.depth) {
            VkClearDepthStencilValue v{0.0f, 0};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
            vkCmdClearDepthStencilImage(command, g.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
        } else {
            VkClearColorValue v{};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(command, g.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
        }
    }

    vk::Buffer& buffer(std::vector<vk::Buffer>& ring, uint32_t slot, VkDeviceSize bytes, VkBufferUsageFlags usage) {
        const size_t index = size_t(listIndex) * kSlots + slot % kSlots;
        if (ring.size() <= index) ring.resize(index + 1);
        vk::Buffer& b = ring[index];
        if (b.size < bytes) {
            if (b.buffer) bufferGraveyard.emplace_back(uploads, b);   // (a frame in flight may still read it)
            b = vk::createBuffer(device, std::max<VkDeviceSize>(bytes + bytes / 2, 1 << 20), usage, true);
        }
        return b;
    }

    // An image replaced while frames in flight, or lists recorded earlier in this command buffer, may still use
    // it: destroyed by sweepGraveyard once no submission can.
    void retire(vk::Image& image) {
        graveyard.emplace_back(uploads, image);
        image = vk::Image{};
    }

    // Once per submission: retired images the frames in flight can no longer sample are destroyed.
    void sweepGraveyard() {
        GT4_STAGE_SCOPE("render: retired images destroyed or pooled");
        ++uploads;
        for (auto it = graveyard.begin(); it != graveyard.end();) {
            if (it->first + kSlots + 1 <= uploads) {
                vk::destroy(device, it->second);
                it = graveyard.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = bufferGraveyard.begin(); it != bufferGraveyard.end();) {
            if (it->first + kSlots + 1 <= uploads) {
                vk::destroy(device, it->second);
                it = bufferGraveyard.erase(it);
            } else {
                ++it;
            }
        }
        // At most kDestroysPerSweep images destroyed a submission: a burst retired (a catch-up's textures, 1000 and
        // more) goes over the next frames instead of in one (one sweep took 3 s).
        size_t destroyed = 0;
        for (auto it = textureGraveyard.begin(); it != textureGraveyard.end();) {
            if (it->first + kSlots + 1 > uploads) {
                ++it;
                continue;
            }
            vk::Image& image = it->second;
            if (pooledTextures < kMaxPooledTextures) {
                texturePool[{image.extent.width, image.extent.height, image.mips}].push_back(image);
                ++pooledTextures;
            } else if (destroyed < kDestroysPerSweep) {
                vk::destroy(device, image);
                ++destroyed;
            } else {
                ++it;
                continue;
            }
            it = textureGraveyard.erase(it);
        }
    }

    std::unordered_map<uint32_t, size_t> uploadLast;   // uploadTextures: each texture's last entry in the list
    void uploadTextures(VkCommandBuffer command, uint32_t slot) {
        if (frame.resetTextures) {
            for (auto& [id, t] : textures) retireTexture(t.image);
            textures.clear();
        }
        VkDeviceSize bytes = 4;
        for (const auto& t : frame.textures) bytes += VkDeviceSize(t.pixels.size()) * 4;
        counters.textures += frame.textures.size();
        counters.textureBytes += bytes - 4;
        vk::Buffer* stagePtr;
        {
            GT4_STAGE_SCOPE("render: texture staging buffer");
            stagePtr = &buffer(staging, slot, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        }
        vk::Buffer& stage = *stagePtr;
        VkDeviceSize offset = 0;
        vk::Barriers b;
        std::vector<std::pair<gsn::TextureImage*, VkDeviceSize>> copies;
        // A list that a skipped list's textures were carried into (gsn::carryOver) can name a texture twice: only its
        // last version is uploaded, so each image is made, written and given its mips once (twice, the second
        // generateMips found level 0 already sampled: a layout error; a version of another size was copied into the
        // image made for the last). The pixels are the same: a list's uploads all come before its draws, so only the
        // last version was ever sampled.
        uploadLast.clear();
        for (size_t i = 0; i < frame.textures.size(); ++i) uploadLast[frame.textures[i].id] = i;
        for (size_t i = 0; i < frame.textures.size(); ++i) {
            auto& t = frame.textures[i];
            if (uploadLast[t.id] != i) continue;
            GpuTexture& g = textures[t.id];
            if (!g.image.valid() || g.width != t.width || g.height != t.height || g.levels != t.levels ||
                g.stream != t.stream) {
                GT4_STAGE_SCOPE("render: texture images made (pooled or new)");
                retireTexture(g.image);
                vk::ImageDesc desc;
                desc.format = VK_FORMAT_R8G8B8A8_UNORM;
                desc.extent = {t.width, t.height};
                // A stream's frames (a movie, each frame new) get no chain: only perspective draws read generated
                // levels (see `made` in the draws), and a movie under the menus is drawn flat, so a chain blitted from
                // every frame went unread.
                g.generated = t.levels == 1 && !t.stream && std::max(t.width, t.height) >= 8
                                  ? std::bit_width(std::max(t.width, t.height)) : 1;
                g.stream = t.stream;
                desc.mips = t.levels > 1 ? t.levels : g.generated;
                desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                desc.name = "gsn texture";
                desc.heap = &textureHeap;   // (a burst of new textures: no allocation each)
                desc.mipViews = false;      // (its mips are made by blits: no view of each)
                auto pooled = texturePool.find({t.width, t.height, desc.mips});
                if (pooled != texturePool.end() && !pooled->second.empty()) {
                    g.image = pooled->second.back();   // (its contents are discarded below)
                    pooled->second.pop_back();
                    --pooledTextures;
                } else {
                    g.image = vk::createImage(device, desc);
                }
                g.width = t.width;
                g.height = t.height;
                g.levels = t.levels;
            }
            g.replaced = t.replaced;
            g.version = t.version;
            {
                GT4_CPU_SCOPE("render: texture copy to staging");
                std::memcpy(static_cast<uint8_t*>(stage.mapped) + offset, t.pixels.data(), t.pixels.size() * 4);
            }
            copies.emplace_back(&t, offset);
            b.image(g.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
            offset += VkDeviceSize(t.pixels.size()) * 4;
        }
        b.flush(command);
        for (const auto& [t, at] : copies) {
            VkBufferImageCopy regions[7]{};
            VkDeviceSize offsetInImage = at;
            uint32_t w = t->width, h = t->height;
            for (uint32_t level = 0; level < t->levels && level < 7; ++level) {
                regions[level].bufferOffset = offsetInImage;
                regions[level].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                regions[level].imageExtent = {w, h, 1};
                offsetInImage += VkDeviceSize(w) * h * 4;
                w = std::max(w / 2, 1u);
                h = std::max(h / 2, 1u);
            }
            vkCmdCopyBufferToImage(command, stage.buffer, textures[t->id].image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   std::min(t->levels, 7u), regions);
        }
        GT4_CPU_SCOPE("render: texture mips");
        for (const auto& [t, at] : copies) {
            GpuTexture& g = textures[t->id];
            if (g.generated > 1) generateMips(command, g.image, g.generated);
            else vk::toSampled(b, g.image, vk::kFragment);
        }
        b.flush(command);
    }

    // The mip chain of an image whose level 0 was just written (all levels in TRANSFER_DST), by halving
    // blits; leaves every level sampled.
    static void generateMips(VkCommandBuffer command, vk::Image& image, uint32_t levels) {
        auto barrier = [&](uint32_t level, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                           VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
            VkImageMemoryBarrier2 m{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            m.srcStageMask = srcStage;
            m.srcAccessMask = srcAccess;
            m.dstStageMask = dstStage;
            m.dstAccessMask = dstAccess;
            m.oldLayout = from;
            m.newLayout = to;
            m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            m.image = image.image;
            m.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
            VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            d.imageMemoryBarrierCount = 1;
            d.pImageMemoryBarriers = &m;
            vkCmdPipelineBarrier2(command, &d);
        };
        int32_t w = static_cast<int32_t>(image.extent.width), h = static_cast<int32_t>(image.extent.height);
        for (uint32_t level = 1; level < levels; ++level) {
            barrier(level - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
            blit.srcOffsets[1] = {w, h, 1};
            w = std::max(w / 2, 1);
            h = std::max(h / 2, 1);
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            blit.dstOffsets[1] = {w, h, 1};
            vkCmdBlitImage(command, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                           VK_FILTER_LINEAR);
            barrier(level - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, vk::kTransfer,
                    VK_ACCESS_2_TRANSFER_READ_BIT, vk::kFragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
        barrier(levels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, vk::kTransfer,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, vk::kFragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image.stage = vk::kFragment;
        image.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }

    // After the frame's draws are recorded: its retired textures go to the graveyard.
    void retireTextures() {
        GT4_STAGE_SCOPE("render: textures retired");
        for (const uint32_t id : frame.retiredTextures) {
            auto it = textures.find(id);
            if (it == textures.end()) continue;
            retireTexture(it->second.image);
            textures.erase(it);
        }
    }

    // The scene stage (docs/NATIVE_RENDERER.md, docs/LIGHTING.md): a 3D scene complete in its target (the
    // front end's Light command), before GT4's post effects and HUD. Its colour and Z images, and its area.
    struct SceneImages {
        vk::Image* colour = nullptr;
        vk::Image* depth = nullptr;
        VkExtent2D area{};
        uint32_t sx = 1, sy = 1;   // the scene target's scale
    };
    // lighting.night for one scene: its main view (the largest scissor bound to the scene's target), the view's
    // projection and viewport, the GS origin of its 3D batches; the view's lamps gathered (with GT4_NIGHT_TEST_LIGHTS'
    // test lamps), clustered over the scene area at the render scale, and the local luminance computed. False (nothing
    // added) when the frame has no such view or no lights.
    bool nightPrepare(const gsn::FrameList::Lighting& l, const SceneImages& si, VkImageView colourView, VkImageView depthOnly,
                      NightPush& k, NightBuffers& out);
    // Its second half, once the view's parameters are in `k` and its records in nightRecords: the mirror's inset, the
    // gather, the buffers, the cluster build and the local luminance.
    bool nightFinish(const gsn::FrameList::Lighting& l, const SceneImages& si, VkImageView colourView, VkImageView depthOnly,
                     uint16_t viewId, NightPush& k, NightBuffers& out);
    // execute's state between its pieces (executeBegin, executeRange, executeLight, executeFinish): one list's
    // execution, made anew by executeBegin. The functions see the piece's command buffer as `command`.
    struct ExecuteState {
        VkCommandBuffer command = VK_NULL_HANDLE;   // the piece's being recorded
        uint32_t slot = 0;                          // the frame slot (executeBegin, executeLight)
        vk::Buffer* vertices = nullptr;   // the list's vertices (vertexBuffers)
        uint32_t modelBase = 0;           // where the GPU's vertices start
        vk::Buffer* jobs = nullptr;       // the list's 3D batches (modelJobs, modelData; B7's parity draws read them too)
        vk::Buffer* words = nullptr;
        vk::Barriers b;
        bool rendering = false;
        uint32_t boundColour = 0, boundDepth = 0;
        // The destination alpha test's stencil: stencilDepth's stencil holds stencilColour's alpha bit 7 over stencilRegion
        // (render pixels) since prepareStencil copied it. Draws keep it so: a DATE draw by its two passes, and with
        // VK_EXT_shader_stencil_export every draw that writes the target's alpha with stencilDepth bound (its shader
        // writes each pixel's new bit 7 to the stencil). Only an alpha write it cannot follow makes it stale: the target's
        // alpha written without stencilDepth or without export, another target's exported into stencilDepth, the scene
        // stage or an effect writing the target. (Without export, each car part's base draw made the next part's
        // reflection copy the stencil again: 20 copies, each ending the scene's render pass, in a far replay camera.)
        bool stencilFresh = false;
        uint32_t stencilColour = 0, stencilDepth = 0;
        VkRect2D stencilRegion{};
        VkPipeline boundPipeline = VK_NULL_HANDLE;
        bool perPass = false;   // GT4_GPU_PROFILE=2: a mark at each render pass's end
        // The draws' state as last set in this render pass: only changes are recorded (a draw's state commands cost the
        // GPU's command processor more than its few vertices). Forgotten when the pass ends or another pass runs.
        struct Recorded {
            int depthTest = -1, compare = -1, reference = -1, depthWrite = -1;
            int stencilMode = -1;   // 0 off; DATE: 1 test (kept), 2 test and invert where it passes; 3 alpha bit 7 written
            VkRect2D scissor{{-1, -1}, {0, 0}};
            VkImageView view = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            bool pushed = false;
            DrawConstants constants{};
        } recorded;
        std::vector<uint8_t> jittered;   // per command: a 3D draw of a scene lit later (temporal anti-aliasing)
        const float* phase = nullptr;    // this frame's sub-pixel offset
        // The reinterpreted textures': their count, palettes, indices and the scale each is built at.
        size_t count = 0;
        vk::Buffer* cluts = nullptr;
        std::vector<uint32_t> indexStart;
        size_t indexWords = 1;
        vk::Buffer* indexBuffer = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> builtScale;
        vk::Buffer* curves = nullptr;   // the colour gradings' curves (none: nullptr)
        long executed = 0;              // draws executed (GT4_GSN_LIMIT)
        bool limited = false;           // GT4_GSN_LIMIT reached: the list's later commands not executed
        // What execute's lambdas were (executeBegin makes them; each captures only the Impl).
        std::function<const char*(uint32_t)> passLabel;
        std::function<void()> end;
        std::function<void(uint32_t, uint32_t)> begin;
        std::function<void(uint32_t, uint32_t, VkRect2D)> prepareStencil;
        std::function<bool(const gsn::FrameList::Lighting&, SceneImages&)> sceneImages;
        std::function<void(vk::Image&, vk::Image&, VkExtent2D, const gsn::FrameList::Lighting&, uint32_t, uint32_t)> copyBack;
        std::function<bool(vk::Image&, VkExtent2D, VkFormat, VkImageUsageFlags, const char*)> ensureImage;
        // The scene stage's steps on the work images in `work`. `part` (kPartWhole: all) is one layout of the work
        // images each, the render graph's pass for it: kPartFirst the occlusion or the half-resolution reflections
        // written, kPartMain the stage's result written, kPartCopy it copied back.
        WorkImages work;
        bool moving = false;         // the Light's motion vectors drawn (the temporal resolve and motion blur need them)
        size_t lightAt = 0;          // the Light being staged (runStep), for its skip mask
        bool lightSkipped = false;   // the graph's passes of a Light: none of its scene stage runs (beginLight)
        std::function<void(const gsn::FrameList::Lighting&, int)> relight, reflect, haze, blur, focus;
        std::function<void(const gsn::FrameList::Lighting&)> antialias;
        std::function<bool(const gsn::FrameList::Lighting&, size_t)> drawVelocity;
        std::function<void(const gsn::FrameList::Lighting&, const SceneImages&, vk::Pipeline&, const void*, uint32_t, vk::Image*, int)> stagePass;
        std::function<vk::Image&(uint32_t)> imageFor;
        std::function<GpuTarget*(const gsn::Reinterpretation&)> sourceOf, paletteOf;
        std::function<vk::Image*(uint32_t, size_t)> build;
        std::function<void(uint32_t)> grade;
    } ex;
    // A list's commands split at its Light commands: runs of the others (Range, [from, to)) and each Light alone
    // (from: its index). Only the commands' kinds decide it (GsnScene::buildPrepass splits a list before it runs).
    struct Segment {
        enum class Kind { Range, Light, Effect } kind = Kind::Range;
        size_t from = 0, to = 0;
    };
    static void segments(const gsn::FrameList& list, std::vector<Segment>& out);
    void execute(VkCommandBuffer command, uint32_t slot);   // the four below, in order (as the graph's passes run them)
    void executeBegin(VkCommandBuffer command, uint32_t slot);   // before the commands: the 3D batches, ex made
    void executeRange(VkCommandBuffer command, uint32_t slot, size_t from, size_t to);   // the commands [from, to)
    void executeLight(VkCommandBuffer command, uint32_t slot, size_t at);   // the scene stage at Light command `at`
    // The scene stage's steps, in a modern renderer's order (executeLight runs them in turn on the members; the render
    // graph builds a pass or more for each that runs, on its transients). Each is followed by its profile mark.
    enum Step : int { kStepMotionVectors, kStepLighting, kStepReflections, kStepFog, kStepTemporalResolve, kStepMotionBlur, kStepFocus, kSteps };
    static constexpr const char* kStepMarks[kSteps] = {"motion vectors", "lighting", "reflections", "fog", "temporal resolve",
                                                       "motion blur", "depth of field"};
    enum Part : int { kPartWhole = -1, kPartFirst = 0, kPartMain = 1, kPartCopy = 2 };
    // Whether a list's scene records have views (`records` below): a vehicles-only frame (scene records mode 2, D14) has a
    // scene frame but no views, so nothing for the night relight to light. The same test at build and at run time.
    static bool viewRecords(const gsn::FrameList& list) { return list.scene && !list.scene->views.empty(); }
    // Whether a step runs at Light `l` of a list, from the settings (known when the graph is built) and whether that list
    // has scene records with views (`records`: viewRecords of the list's own scene frame, the one being planned or run,
    // not `frame`'s). The temporal resolve and the motion blur also need the motion vectors drawn (ex.moving: the Light
    // has a scene).
    bool stepRuns(int step, const gsn::FrameList::Lighting& l, bool records) const {
        switch (step) {
            case kStepMotionVectors: return temporal || effects.shutter > 0.0f || seamOn;   // (B16c: the seam's motion vectors)
            case kStepLighting:   // lighting.night needs the list's lamps and views (or the test lamps)
                return lighting.occlusion > 0.0f || lighting.grade > 0.0f || (night > 0.0f && (records || render::testLightCount()));
            case kStepReflections: return effects.reflections > 0.0f;
            case kStepFog: return effects.fogDensity > 0.0f && l.fogged;   // (haze does nothing for a Light without fog)
            case kStepTemporalResolve: return temporal;
            case kStepMotionBlur: return effects.shutter > 0.0f;
            case kStepFocus: return effects.aperture > 0.0f;
            default: return false;
        }
    }
    bool beginLight(VkCommandBuffer command, size_t at);   // before the steps; false: no scene stage (no light, GT4_GSN_LIMIT)
    void stageOnMembers(VkCommandBuffer command, size_t at);
    void runStep(int step, const gsn::FrameList::Lighting& l, size_t at, int part);
    // Scene stages GsnScene::buildPrepass planned wrong (the targets or the settings at run time not as it saw them),
    // run on the members instead: a gate that stays 0 (printed with the scene's summary); and those run as planned.
    uint64_t stagesOnTransients = 0, stagesOnMembers = 0;
    void planWrong(size_t at, const char* what);
    void checkOnePass(size_t at);
    // The scene stage at a Light as GsnScene::buildPrepass plans it: whether it has a scene, its area, the colour
    // target's extent, the steps that run (checked when it runs: the targets are made by then).
    struct LightPlan {
        bool scene = false;
        VkExtent2D area{}, colour{};
        bool runs[kSteps] = {};
    };
    // One of the graph's passes for the Light at `at`: `part` of `step` on `work`, the marks of steps that do not run
    // before and after it; `first` (the Light's first pass): the stage begun and the plan checked.
    void executeLightPart(VkCommandBuffer command, size_t at, int step, int part, const WorkImages& work,
                          const std::vector<const char*>& before, const std::vector<const char*>& after, const LightPlan* first);
    void executeFinish(VkCommandBuffer command, uint32_t slot);   // after the commands: the displayed buffer
    void snapSprites();
    // B24: each sprite's min corner and texture steps per pixel, (x0, y0, ds, dt), as PCSX2's software GS sets them up
    // (GSRasterizer::DrawSprite), from the front end's vertices before snapSprites moves the corners; written into the
    // sprite's own S, T, Q and U in the GPU's copy. Kept for the list (a list executed again reuses them).
    struct ExactSprite {
        uint32_t first;   // its six vertices
        float x0, y0, ds, dt;
    };
    std::vector<ExactSprite> exactSprites;
    uint64_t listSerial = 0;                   // lists made current so far (pendingTargets): a list executed again keeps it
    uint64_t exactSpritesOf = ~0ull;           // the list the records are for
    void makeExactSprites();
    // Executes every pending list in order into `command` (the scale from `extent`), then empties the queue.
    // `profiled`: a frame's own submission (GT4_GPU_PROFILE).
    void executePending(VkCommandBuffer command, uint32_t slot, VkExtent2D extent, bool profiled = false);
    // executePending's pieces, in order (the render graph's passes call them one by one: GsnScene::buildPrepass).
    // `scaled`: the scale already decided (decideScale: the render graph's build).
    void beginPending(VkCommandBuffer command, uint32_t slot, VkExtent2D extent, bool profiled, bool scaled = false);
    void decideScale(VkExtent2D extent);   // the pending lists' render scale; the targets dropped when it changes
    void pendingTargets(VkCommandBuffer command, uint32_t slot);   // list `listIndex`
    void pendingUploads(VkCommandBuffer command, uint32_t slot);   // (after pendingTargets)
    void pendingDraws(VkCommandBuffer command, uint32_t slot);
    void pendingListEnd(VkCommandBuffer command);
    void endPending(VkCommandBuffer command);
    size_t pendingShown = 0;   // the list shown now (pending.size(): none)
    size_t pendingBuilt = 0;   // the lists buildPrepass made passes for (checked when they run: the lists cannot change)
    // The pending lists as render graph passes (GsnScene::buildPrepass: a frame's, into the pipeline's graph, `slot` its
    // frame slot; GsnScene::read: a read's, "read: ..." passes into readGraph, slot 0, its readbacks left for the read).
    void buildPending(rg::Graph& graph, uint32_t slot, VkExtent2D extent, bool read);
    // GsnScene::read's own render graph (ticket B4 step 5): one slot, so one transient heap (the read waits for the
    // device first). Destroyed with the scene. Its transients stay allocated meanwhile, at the largest scene stage a read
    // needed: next to the frame graph's, up to twice the scene stage's work-image memory (a few tens of MiB at 1080p
    // ultra). A low-VRAM device (B6's budgets) can drop it after a burst of reads instead.
    std::unique_ptr<rg::Graph> readGraph;
    uint32_t readFrames = 0;
    bool reading = false;   // GsnScene::read's graph is being built or run: its passes' CPU time is the read's
    // The CPU scopes of a frame's GS pass (none in a read's: the read times itself, as before the graph). `commands`:
    // the native renderer's commands scope too.
    struct FrameScopes {
        std::optional<cpuprofile::Scope> stage, commands;
        FrameScopes(const Impl& s, bool withCommands = true) {
            static cpuprofile::Slot& stageSlot = cpuprofile::slot("render: the scene (game lists)");
            static cpuprofile::Slot& commandsSlot = cpuprofile::slot("render: native renderer's commands");
            if (s.reading) return;
            stage.emplace(cpuprofile::stagesMeasured() ? &stageSlot : nullptr);
            if (withCommands) commands.emplace(cpuprofile::enabled() ? &commandsSlot : nullptr);
        }
    };

    // The pixels the display shows: the circuit's size in video clocks over its clocks per pixel; interlaced in
    // field mode each field shows every line of a half-height buffer.
    static void displayedSize(const gsn::Display& d, uint32_t& w, uint32_t& h) {
        const gfx::DisplayCircuit& c = d.record.circuit[d.target2 ? 1 : 0];
        w = c.magH ? c.clocks / c.magH : 0;
        h = d.record.interlaced && d.record.fieldMode ? c.lines / 2u : c.lines;
        if (!w || !h || w > 1024 || h > 1024) {
            w = 640;
            h = 448;
        }
    }
};

// Sprites cover whole GS pixels at any scale. The GS covers pixel k when x0 <= k < x1 (its sample point is the
// pixel's corner); a render pixel j samples the GS at j / scale (gsnPosition), so above 1x a sprite edge at k - 0.5
// (GT4 fills its targets from -0.5 to size - 0.5) cut the last GS pixel's block in two: a target's last render row
// and column stayed unfilled, and draws sampling a target at its edge (the car pictures' reflection lookups,
// clamped) read them. Each edge moves to ceil(x) - 0.5 / scale, which covers the blocks of exactly the GS's pixels
// (and changes nothing at 1x), with the texture coordinates continued along the sprite. Idempotent: a list drawn
// again for presentation is unchanged.
void GsnScene::Impl::makeExactSprites() {
    if (exactSpritesOf == listSerial) return;   // (this list again: its corners are snapped by now; the records stand)
    exactSprites.clear();
    exactSpritesOf = listSerial;
    for (const gsn::Draw& d : frame.draws) {
        if (d.modeled || d.topology != gsn::Topology::Triangles || (d.state.prim & 7) != 6 || d.count % 6) continue;
        if (!((d.state.prim >> 4) & 1)) continue;   // untextured: no coordinate
        const bool fst = (d.state.prim >> 8) & 1;
        const gfx::Texture& tx = d.state.c.texture;
        // PCSX2's 16.16 units: FST UV (texels) x 0x10000; STQ S/Q x (0x10000 << TW): powers of two, so the steps below
        // are its float32 steps exactly, scaled back to texels
        const float scaleU = fst ? 65536.0f : 65536.0f * float(1u << std::min<uint32_t>(tx.log2Width, 10u));
        const float scaleV = fst ? 65536.0f : 65536.0f * float(1u << std::min<uint32_t>(tx.log2Height, 10u));
        for (uint32_t first = d.first; first + 6 <= d.first + d.count && first + 6 <= frame.vertices.size(); first += 6) {
            const gsn::Vertex& a = frame.vertices[first];       // the sprite's first vertex's corner (gs_frontend.cpp)
            const gsn::Vertex& b = frame.vertices[first + 4];   // the second's
            const float ua = fst ? a.u : a.s / a.q, ub = fst ? b.u : b.s / b.q;
            const float va = fst ? a.v : a.t / a.q, vb = fst ? b.v : b.t / b.q;
            // per axis, the min corner's coordinate with it (DrawSprite's blend on v0.p < v1.p)
            const bool ax = a.x < b.x, ay = a.y < b.y;
            const float x0 = ax ? a.x : b.x, x1 = ax ? b.x : a.x, y0 = ay ? a.y : b.y, y1 = ay ? b.y : a.y;
            const float u0 = (ax ? ua : ub) * scaleU, u1 = (ax ? ub : ua) * scaleU;
            const float v0 = (ay ? va : vb) * scaleV, v1 = (ay ? vb : va) * scaleV;
            const float ds = x1 != x0 ? (u1 - u0) / (x1 - x0) : 0.0f, dt = y1 != y0 ? (v1 - v0) / (y1 - y0) : 0.0f;
            exactSprites.push_back({first, x0, y0, ds / 65536.0f, dt / 65536.0f});   // (texels per pixel)
        }
    }
}

void GsnScene::Impl::snapSprites() {
    for (const gsn::Draw& d : frame.draws) {
        if (d.modeled || d.topology != gsn::Topology::Triangles || (d.state.prim & 7) != 6 || d.count % 6) continue;
        auto t = targets.find(d.target);
        if (t == targets.end()) continue;
        const float sx = 0.5f / float(t->second.scaleX), sy = 0.5f / float(t->second.scaleY);
        for (uint32_t first = d.first; first + 6 <= d.first + d.count; first += 6) {
            gsn::Vertex* v = &frame.vertices[first];   // corners: left top, right top, left bottom, right top, right bottom, left bottom
            const float x0 = v[0].x, x1 = v[1].x, y0 = v[0].y, y1 = v[2].y;
            const float nx0 = std::ceil(x0) - sx, nx1 = std::ceil(x1) - sx, ny0 = std::ceil(y0) - sy, ny1 = std::ceil(y1) - sy;
            if (nx0 == x0 && nx1 == x1 && ny0 == y0 && ny1 == y1) continue;
            const float dx = x1 - x0, dy = y1 - y0;
            // per unit of x and of y: U and S along x, V and T along y (a sprite's corners share them so)
            const float ku = dx != 0.0f ? (v[1].u - v[0].u) / dx : 0.0f, ks = dx != 0.0f ? (v[1].s - v[0].s) / dx : 0.0f;
            const float kv = dy != 0.0f ? (v[2].v - v[0].v) / dy : 0.0f, kt = dy != 0.0f ? (v[2].t - v[0].t) / dy : 0.0f;
            for (int k = 0; k < 6; ++k) {
                gsn::Vertex& p = v[k];
                const bool right = p.x == x1 && x1 != x0, bottom = p.y == y1 && y1 != y0;
                const float mx = (right ? nx1 : nx0) - p.x, my = (bottom ? ny1 : ny0) - p.y;
                p.x += mx;
                p.y += my;
                p.u += ku * mx;
                p.s += ks * mx;
                p.v += kv * my;
                p.t += kt * my;
            }
        }
    }
}

// One list's commands (`frame`): executeBegin, its runs of commands between Light commands (executeRange) and the
// scene stage at each Light (executeLight) in order, then executeFinish. The render graph runs the same pieces as
// passes of their own (GsnScene::buildPrepass).
void GsnScene::Impl::execute(VkCommandBuffer command, uint32_t slot) {
    executeBegin(command, slot);
    std::vector<Segment> split;
    segments(frame, split);
    for (const Segment& g : split) {
        if (g.kind == Segment::Kind::Light) executeLight(command, slot, g.from);
        else executeRange(command, slot, g.from, g.to);
    }
    executeFinish(command, slot);
}

void GsnScene::Impl::segments(const gsn::FrameList& list, std::vector<Segment>& out) {
    out.clear();
    const size_t n = list.commands.size();
    // (a Light naming no light, skipped by the commands' loop, stays inside its range: no pass of its own, and the
    // render pass open across it stays inside one graph pass, as the loop had it; an Effect naming no effect too)
    auto light = [&](size_t at) {
        return list.commands[at].kind == gsn::FrameList::Command::Kind::Light && list.commands[at].index < list.lights.size();
    };
    // An effect (the colour grading) is a segment of its own, for its own pass and timing (ticket B4 step 4): its first
    // act is the render pass's end, which the range before it now makes (executeRange ends its render pass).
    auto effect = [&](size_t at) {
        return list.commands[at].kind == gsn::FrameList::Command::Kind::Effect && list.commands[at].index < list.effects.size();
    };
    for (size_t at = 0; at < n;) {
        if (light(at) || effect(at)) {
            out.push_back({light(at) ? Segment::Kind::Light : Segment::Kind::Effect, at, at + 1});
            ++at;
            continue;
        }
        size_t to = at + 1;
        while (to < n && !light(to) && !effect(to)) ++to;
        out.push_back({Segment::Kind::Range, at, to});
        at = to;
    }
}

// N3 step 1: the skip mask of a Light: its target's skipped items drawn again, before the Light, into an R8 image (1 where
// they would have shown: each draw's own depth test against the scene's Z, through gsn_velocity.vert's positions, the
// same to the bit as the scene's), for the albedo's stand-in under them. False when nothing of this Light is skipped.
bool GsnScene::Impl::drawSkipMask(const gsn::FrameList::Lighting& l, size_t lightAt, vk::Image& colour, vk::Image& depth, uint32_t sx,
                                  uint32_t sy) {
    const VkCommandBuffer command = ex.command;
    vk::Barriers& b = ex.b;
    bool any = false;
    for (size_t at = 0; at < lightAt && at < frame.commands.size() && !any; ++at) {
        const auto& cmd = frame.commands[at];
        if (cmd.kind != gsn::FrameList::Command::Kind::Draw || cmd.index >= frame.draws.size()) continue;
        any = frame.draws[cmd.index].target == l.target && cmd.index + 1 < skipAt.size() && skipAt[cmd.index] != skipAt[cmd.index + 1];
    }
    if (!any || !maskPass.valid()) return false;
    ensureWork(skipMask, skipMaskExtent, colour.extent, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               "gsn skip mask");
    vk::toColor(b, skipMask, true);
    vk::toDepth(b, depth);
    b.flush(command);
    VkRenderingAttachmentInfo m{};
    m.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    m.imageView = skipMask.view;
    m.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    m.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ds.imageView = depth.view;
    ds.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    ds.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ds.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {std::min({colour.extent.width, depth.extent.width, skipMask.extent.width}),
                                std::min({colour.extent.height, depth.extent.height, skipMask.extent.height})}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &m;
    info.pDepthAttachment = &ds;
    info.pStencilAttachment = &ds;
    vkCmdBeginRendering(command, &info);
    VkViewport viewport{0, 0, static_cast<float>(colour.extent.width), static_cast<float>(colour.extent.height), 0, 1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vk::bind(command, maskPass);
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &ex.vertices->buffer, &zero);
    DrawConstants k{};
    k.targetSize[0] = static_cast<float>(colour.extent.width);
    k.targetSize[1] = static_cast<float>(colour.extent.height);
    k.scale[0] = static_cast<float>(sx);
    k.scale[1] = static_cast<float>(sy);
    k.sourceScale[0] = k.scale[0];
    k.sourceScale[1] = k.scale[1];
    // The draw's own sense, not the buffer's at the Light: a Z buffer GT4 inverts in place (its Z24 pass) is never inverted
    // here (Target::inverted: drawn as kept, each later draw's Z turned round, max - Z), so the GPU's buffer keeps one sense
    // all frame, and a draw's test in that sense is the one its Draw::depthInverted gives, whatever passes run after it.
    static const VkCompareOp zops[4] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_GREATER};
    static const VkCompareOp invertedZops[4] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_LESS};
    for (size_t at = 0; at < lightAt && at < frame.commands.size(); ++at) {
        const auto& cmd = frame.commands[at];
        if (cmd.kind != gsn::FrameList::Command::Kind::Draw || cmd.index >= frame.draws.size()) continue;
        if (cmd.index + 1 >= skipAt.size() || skipAt[cmd.index] == skipAt[cmd.index + 1]) continue;
        const gsn::Draw& d = frame.draws[cmd.index];
        if (d.target != l.target || !d.modeled) continue;
        if (d.depth && d.depth != l.depth) continue;   // (another Z buffer: not this scene's)
        const gfx::Context& c = d.state.c;
        const bool zt = d.depth && c.depthTest;
        vkCmdSetDepthTestEnable(command, zt ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(command, (d.depthInverted ? invertedZops : zops)[c.depthFunc & 3u]);
        const uint32_t x0 = c.scissorX0, y0 = c.scissorY0;
        const uint32_t right = std::min((uint32_t(c.scissorX1) + 1) * sx, info.renderArea.extent.width),
                       bottom = std::min((uint32_t(c.scissorY1) + 1) * sy, info.renderArea.extent.height);
        if (right <= x0 * sx || bottom <= y0 * sy) continue;
        const VkRect2D scissor{{static_cast<int32_t>(x0 * sx), static_cast<int32_t>(y0 * sy)}, {right - x0 * sx, bottom - y0 * sy}};
        vkCmdSetScissor(command, 0, 1, &scissor);
        k.jitter[0] = ex.jittered[at] ? ex.phase[0] / float(sx) : 0.0f;
        k.jitter[1] = ex.jittered[at] ? ex.phase[1] / float(sy) : 0.0f;
        vk::pushConstants(command, maskPass, k);
        for (uint32_t i = skipAt[cmd.index]; i < skipAt[cmd.index + 1]; ++i) {
            const SkipSpan& s = skipSpans[i];
            if (s.first < d.first || s.first + s.count > d.first + d.count) continue;
            vkCmdDraw(command, s.count, 1, ex.modelBase + s.first, 0);
            ++n3MaskDraws;
        }
    }
    vkCmdEndRendering(command);
    vk::toSampled(b, skipMask, vk::kCompute);
    vk::toSampled(b, depth, vk::kCompute);
    b.flush(command);
    return true;
}

// N3 step 1 (PLAN N3, lane B's back end): the items whose draws the GS path leaves out, the lit-road layer v4 judged
// (ViewOnList::skip, lane F's verdicts), in a kViewMain view on a kFrameLampsOn frame with lighting.night on; nothing
// else. GT4_N3_SKIP_TEST (debugging, until the verdicts land): 'glow', every item of a glow-class material; 'all',
// every item of the main view (the course and the cars go, the billboards stay), so the machinery shows. Each item is
// its job's vertices in its draw (ViewOnList::items, as B7's spans), so a draw merging several items keeps the others.
void GsnScene::Impl::planSkips() {
    skipSpans.clear();
    skipAt.assign(frame.draws.size() + 1, 0);
    if (!(night > 0.0f) || !frame.scene || !(frame.scene->flags & gfx::scene::kFrameLampsOn) || frame.views.empty()) return;
    static const int test = [] {
        const char* e = std::getenv("GT4_N3_SKIP_TEST");
        return !e ? 0 : std::string(e) == "glow" ? 1 : std::string(e) == "all" ? 2 : 0;
    }();
    const std::shared_ptr<const scene::SceneCache> cache = frame.sceneCache;
    bool mainView = false;
    for (const gsn::FrameList::ViewOnList& view : frame.views) {
        if (!view.package || view.package->view.role != gfx::scene::kViewMain) continue;
        mainView = true;
        for (const scene::InstanceIds& ids : view.ids) n3ItemsSeen += ids.mesh ? ids.materialCount : 0;
        if (view.skip.empty() && !test) continue;
        if (!view.skip.empty() && view.skip.size() != view.materials.size()) {   // (one per material, or none at all)
            ++n3Misaligned;
            continue;
        }
        for (const scene::InstanceIds& ids : view.ids) {
            if (!ids.mesh) continue;
            for (uint32_t j = 0; j < ids.materialCount; ++j) {
                const size_t at = size_t(ids.firstMaterial) + j;
                if (at >= view.items.size() || at >= view.materials.size()) break;
                bool skip = test == 2 || (at < view.skip.size() && view.skip[at] != 0);
                if (!skip && test == 1 && cache) skip = (cache->materials().classes(view.materials[at]) & 1u) != 0;
                const gsn::FrameList::ViewOnList::ItemRef& ref = view.items[at];
                if (!skip || ref.draw >= frame.draws.size() || !ref.modelVertexCount) continue;
                skipSpans.push_back({ref.draw, ref.firstModelVertex, ref.modelVertexCount});
            }
        }
    }
    if (mainView) ++n3Frames;
    if (skipSpans.empty()) return;
    std::sort(skipSpans.begin(), skipSpans.end(),
              [](const SkipSpan& a, const SkipSpan& b) { return a.draw != b.draw ? a.draw < b.draw : a.first < b.first; });
    for (const SkipSpan& s : skipSpans) ++skipAt[s.draw + 1];
    for (size_t d = 1; d < skipAt.size(); ++d) skipAt[d] += skipAt[d - 1];
    n3ItemsSkipped += skipSpans.size();
}

// B7's parity path: the list's items the mesh cache draws (paritySpans, parityAt by draw), and the uploads of the meshes
// it draws for the first time, copied into their device-local blocks here, before any of the list's draws.
void GsnScene::Impl::planParity() {
    paritySpans.clear();
    parityAt.assign(frame.draws.size() + 1, 0);
    if (!parity || frame.views.empty() || frame.models.empty() || !ex.jobs) return;
    parityView.setRotate(!device.provokingLast);
    // B8 step 4: GT4_SCENE_READS=modern lets the game's reads see the substituted picture (R9's per-reader checks)
    static const bool readsModern = [] { const char* e = std::getenv("GT4_SCENE_READS"); return e && std::string(e) == "modern"; }();
    parityView.setReadSwitch(!readsModern);
    parityStaging.clear();
    parityCopies.clear();
    parityView.plan(frame, paritySpans, parityStaging, parityCopies);
    parityPlanned = true;
    if (!parityCopies.empty()) {
        while (parityBlocks.size() < parityView.blocks())
            parityBlocks.push_back(vk::createBuffer(device, parityView.blockSize(),
                                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    false));
        vk::Buffer& staging = buffer(parityStagingRing, ex.slot, parityStaging.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(staging.mapped, parityStaging.data(), parityStaging.size());
        for (const render::ParityCopy& c : parityCopies) {
            const VkBufferCopy region{c.from, c.to, c.bytes};
            vkCmdCopyBuffer(ex.command, staging.buffer, parityBlocks[c.block].buffer, 1, &region);
        }
        VkMemoryBarrier2 m{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        m.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        m.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        m.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        m.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &m;
        vkCmdPipelineBarrier2(ex.command, &d);
        tally("parity mesh uploads", parityCopies.size());
    }
    for (const render::ParitySpan& s : paritySpans) ++parityAt[s.draw + 1];
    for (size_t d = 1; d < parityAt.size(); ++d) parityAt[d] += parityAt[d - 1];
    if (hybrid) planSides();
}

// B8 step 2: where this list's side-channel passes run (before each Z clear of a substituted view's Z buffer, and at its
// Light), and its items' SideItems. A Z clear is a draw into the view's Z buffer that writes Z unconditionally (the test
// off or ALWAYS): R5's clear sprites and its alpha-test-never Z-only fills.
void GsnScene::Impl::planSides() {
    sideBounds.clear();
    sideNext = 0;
    sideClearedThisList.clear();
    if (paritySpans.empty()) return;
    std::vector<size_t> drawCommand(frame.draws.size(), SIZE_MAX);
    for (size_t at = 0; at < frame.commands.size(); ++at)
        if (frame.commands[at].kind == gsn::FrameList::Command::Kind::Draw && frame.commands[at].index < drawCommand.size())
            drawCommand[frame.commands[at].index] = at;
    spanCommand.resize(paritySpans.size());
    for (size_t i = 0; i < paritySpans.size(); ++i) spanCommand[i] = drawCommand[paritySpans[i].draw];
    std::vector<std::pair<uint32_t, uint32_t>> planned;   // (target, Z buffer) pairs already bounded
    for (uint32_t v = 0; v < frame.views.size(); ++v) {
        const gsn::FrameList::ViewOnList& view = frame.views[v];
        if (view.ids.empty() || !view.target || !view.depth) continue;
        if (std::find(planned.begin(), planned.end(), std::make_pair(view.target, view.depth)) != planned.end()) continue;
        planned.emplace_back(view.target, view.depth);
        size_t from = 0;
        bool cleared = false;
        float clearDepth = 0.0f;
        uint32_t clearRect[4] = {};
        for (size_t at = 0; at < frame.commands.size(); ++at) {
            const auto& cmd = frame.commands[at];
            if (cmd.kind == gsn::FrameList::Command::Kind::Light) {
                if (cmd.index < frame.lights.size() && frame.lights[cmd.index].target == view.target) {
                    sideBounds.push_back({at, from, v, clearDepth, {clearRect[0], clearRect[1], clearRect[2], clearRect[3]}, cleared, true});
                    from = at;
                    cleared = false;   // (draws after the Light are no part of that clear's partition: no reset against it)
                }
                continue;
            }
            if (cmd.kind != gsn::FrameList::Command::Kind::Draw || cmd.index >= frame.draws.size()) continue;
            const gsn::Draw& d = frame.draws[cmd.index];
            const gfx::Context& c = d.state.c;
            if (d.modeled || d.depth != view.depth || !c.depthWrite || (c.depthTest && c.depthFunc != 1) || !d.count) continue;
            sideBounds.push_back({at, from, v, clearDepth, {clearRect[0], clearRect[1], clearRect[2], clearRect[3]}, cleared, false});
            from = at;
            // The next partition's reset: only for a clear of one Z (all its vertices), over what it covered (its scissor
            // and its vertices' bounds: a GS pixel is covered from floor(min) to ceil(max) - 1). A Z-only fill of shaped
            // geometry or of several Zs is a boundary without a reset.
            const gsn::Vertex* q = frame.vertices.data() + d.first;
            bool oneZ = d.first + d.count <= frame.vertices.size();
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
            for (uint32_t k = 0; oneZ && k < d.count; ++k) {
                oneZ = q[k].z == q[0].z;
                x0 = std::min(x0, q[k].x), x1 = std::max(x1, q[k].x), y0 = std::min(y0, q[k].y), y1 = std::max(y1, q[k].y);
            }
            const int64_t cx0 = std::max<int64_t>(c.scissorX0, int64_t(std::floor(x0))), cy0 = std::max<int64_t>(c.scissorY0, int64_t(std::floor(y0))),
                          cx1 = std::min<int64_t>(c.scissorX1, int64_t(std::ceil(x1)) - 1), cy1 = std::min<int64_t>(c.scissorY1, int64_t(std::ceil(y1)) - 1);
            cleared = oneZ && cx1 >= cx0 && cy1 >= cy0;
            clearDepth = float(q[0].z) * (1.0f / 4294967296.0f);   // (gsn.vert's sprite depth)
            clearRect[0] = cleared ? uint32_t(cx0) : 0;
            clearRect[1] = cleared ? uint32_t(cy0) : 0;
            clearRect[2] = cleared ? uint32_t(cx1) : 0;
            clearRect[3] = cleared ? uint32_t(cy1) : 0;
        }
    }
    std::sort(sideBounds.begin(), sideBounds.end(), [](const SideBound& a, const SideBound& b) { return a.command < b.command; });
    // the items' instances and materials
    vk::Buffer& items = buffer(sideItemRing, ex.slot, paritySpans.size() * sizeof(SideItem), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    SideItem* out = static_cast<SideItem*>(items.mapped);
    for (size_t i = 0; i < paritySpans.size(); ++i) {
        const render::ParitySpan& s = paritySpans[i];
        const gsn::FrameList::ViewOnList& view = frame.views[s.view];
        SideItem item{};
        if (view.package && s.instance < view.package->instances.size()) {   // diag(axisScale x k, 1) x objectToView (CONTRACTS
            // a.8's matrix check: k = shapeScale, or 1 when 0): the mesh's positions to view space
            const scene::InstanceData& in = view.package->instances[s.instance];
            std::memcpy(item.objectToView, in.objectToView.m, sizeof item.objectToView);
            const float k = in.shapeScale != 0.0f ? in.shapeScale : 1.0f;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c) item.objectToView[r][c] *= in.axisScale[r] * k;
        }
        item.material = s.material;
        out[i] = item;
    }
    sideItems = &items;
}

vk::Pipeline& GsnScene::Impl::parityPipeline(uint32_t blendKey, uint32_t mask, bool depth, bool exports) {
    const uint32_t key = blendKey | (mask << 16) | (depth ? 1u << 20 : 0) | (exports ? 1u << 24 : 0);
    auto it = parityPipelines.find(key);
    if (it != parityPipelines.end()) return it->second;
    vk::GraphicsDesc desc = drawDesc(blendKey, mask, depth, gsn::Topology::Triangles, exports);
    desc.vertex = shaders::modern_parity_vert();
    desc.fragment = exports ? shaders::gsn_frag_parity_export() : shaders::gsn_frag_parity();
    desc.vertices.stride = sizeof(scene::MeshRawVertex);
    desc.vertices.attributes = {{0, 0, VK_FORMAT_R32G32B32_UINT, offsetof(scene::MeshRawVertex, position)},
                                {1, 0, VK_FORMAT_R32G32B32_UINT, offsetof(scene::MeshRawVertex, st)},
                                {2, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(scene::MeshRawVertex, colour)},
                                {3, 0, VK_FORMAT_R32G32_UINT, offsetof(scene::MeshRawVertex, next)},
                                {4, 0, VK_FORMAT_R32_UINT, offsetof(scene::MeshRawVertex, flags)},
                                {5, 0, VK_FORMAT_R32G32B32_UINT, offsetof(scene::MeshRawVertex, before)},
                                {6, 0, VK_FORMAT_R32G32B32_UINT, offsetof(scene::MeshRawVertex, before) + 12}};
    desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
    desc.pushSize = sizeof(ParityConstants);
    desc.provokingLast = device.provokingLast;
    desc.name = "gsn parity draw";
    ++counters.pipelines;
    return parityPipelines[key] = vk::createGraphics(device, desc);
}

// Before the list's commands: its vertices, its 3D batches, and the state the commands use (ex).
void GsnScene::Impl::executeBegin(VkCommandBuffer command, uint32_t slot) {
    ex = ExecuteState{};
    ex.command = command;
    ex.slot = slot;
    nightOrdinal = 0;
    if (gsExact()) makeExactSprites();
    snapSprites();
    if (seamOn) {   // B16c: this list's display seam
        seamPlan = gsn::findSeamAnchor(frame);
        seamVelocityKept = seamDone = false;
        if (seamPlan.outcome != gsn::SeamAnchor::Outcome::NotShown) {
            ++seamOutcomes[static_cast<int>(seamPlan.outcome)];
            if (!seamPlan.found())
                if (auto it = seams.find(seamPlan.made); it != seams.end()) it->second.valid = false;
        }
    }
    const VkDeviceSize vertexBytes = std::max<VkDeviceSize>((frame.vertices.size() + frame.modelVertices) * sizeof(gsn::Vertex), 64);
    ex.vertices = &buffer(vertexBuffers, slot, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    vk::Buffer& vertices = *ex.vertices;
    ex.modelBase = static_cast<uint32_t>(frame.vertices.size());   // where the GPU's vertices start
    const uint32_t modelBase = ex.modelBase;
    std::memcpy(vertices.mapped, frame.vertices.data(), frame.vertices.size() * sizeof(gsn::Vertex));
    if (gsExact()) {   // B24: the sprites' (x0, y0, ds, dt) in their own S, T, Q and U, the GPU's copy only
        gsn::Vertex* gpu = static_cast<gsn::Vertex*>(vertices.mapped);
        for (const ExactSprite& e : exactSprites)
            for (uint32_t k = 0; k < 6; ++k) {
                gsn::Vertex& v = gpu[e.first + k];
                v.s = e.x0, v.t = e.y0, v.q = e.ds, v.u = e.dt;
            }
    }
    // GT4's 3D batches (docs/NATIVE_V2.md, step 2): gsn_models.comp writes their triangles into the ranges their
    // draws hold, before any draw reads them.
    if (!frame.models.empty()) {
        static_assert(sizeof(gsn::Vertex) == 18 * 4, "gsn_models.comp writes 18 words a vertex");
        vk::Buffer& jobs = buffer(modelJobs, slot, frame.models.size() * sizeof(gfx::ModelJob), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(jobs.mapped, frame.models.data(), frame.models.size() * sizeof(gfx::ModelJob));
        vk::Buffer& data = buffer(modelData, slot, frame.modelData.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(data.mapped, frame.modelData.data(), frame.modelData.size() * 4);
        ex.jobs = &jobs;
        ex.words = &data;
        vk::bind(command, models);
        vk::Descriptors()
            .storageBuffer(0, jobs.buffer, 0, frame.models.size() * sizeof(gfx::ModelJob))
            .storageBuffer(1, data.buffer, 0, frame.modelData.size() * 4)
            .storageBuffer(2, vertices.buffer, 0, vertexBytes)
            .push(command, models);
        const uint32_t constants[4] = {modelBase, std::bit_cast<uint32_t>(modelT), std::bit_cast<uint32_t>(modelTPrevious), 0};
        vk::pushConstants(command, models, constants);
        vkCmdDispatch(command, static_cast<uint32_t>(frame.models.size()), 1, 1);
        VkMemoryBarrier2 m{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        m.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        m.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        m.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        m.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &m;
        vkCmdPipelineBarrier2(command, &d);
        tally("3D batches on the GPU", frame.models.size());
    }
    planParity();   // (B7: which of the list's jobs the mesh cache draws; none unless render.scene = parity)
    planSkips();    // (N3 step 1: the lit-road layer's items, skipped)
    // (the commands' state: ex, made anew above)
    // GT4_GPU_PROFILE=2: a render pass's time under its target's name (block address, size in GS pixels, format).
    ex.passLabel = [this](uint32_t id) -> const char* {
        uint32_t bp = 0, psm = 0;
        for (const gsn::Target& t : frame.targets)
            if (t.id == id) bp = t.bp, psm = t.psm;
        const GpuTarget& g = targets[id];
        const uint64_t key = (uint64_t(bp) << 40) | (uint64_t(g.width) << 28) | (uint64_t(g.height) << 16) | psm;
        auto it = profile.passLabels.find(key);
        if (it != profile.passLabels.end()) return it->second;
        char name[64];
        std::snprintf(name, sizeof name, "pass bp%04x %ux%u psm%02x", bp, g.width, g.height, psm);
        profile.labelStore.emplace_back(name);
        return profile.passLabels[key] = profile.labelStore.back().c_str();
    };
    ex.perPass = profile.on && profile.level >= 2;
    ex.end = [this] {
        const VkCommandBuffer command = ex.command;
        const bool perPass = ex.perPass;
        auto& passLabel = ex.passLabel;
        bool& rendering = ex.rendering;
        const uint32_t boundColour = ex.boundColour;
        VkPipeline& boundPipeline = ex.boundPipeline;
        auto& recorded = ex.recorded;
        if (rendering) {
            vkCmdEndRendering(command);
            if (perPass) mark(command, passLabel(boundColour));
        }
        rendering = false;
        boundPipeline = VK_NULL_HANDLE;
        recorded = ExecuteState::Recorded{};
    };
    ex.begin = [this](uint32_t colourId, uint32_t depthId) {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        const bool perPass = ex.perPass;
        bool& rendering = ex.rendering;
        uint32_t& boundColour = ex.boundColour;
        uint32_t& boundDepth = ex.boundDepth;
        if (perPass) mark(command, "between passes");
        GpuTarget& c = targets[colourId];
        vk::toColor(b, c.image);
        vk::Image* depth = depthId == ~0u ? &scratchDepth : depthId ? &targets[depthId].image : nullptr;
        if (depth) vk::toDepth(b, *depth);
        b.flush(command);
        VkRenderingAttachmentInfo colour{};
        colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colour.imageView = c.image.view;
        colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingAttachmentInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        if (depth) {
            ds.imageView = depth->view;
            ds.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ds.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            ds.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea = {{0, 0}, c.image.extent};
        if (depth) {   // a Z buffer smaller than the frame bounds what can be drawn with it
            info.renderArea.extent.width = std::min(info.renderArea.extent.width, depth->extent.width);
            info.renderArea.extent.height = std::min(info.renderArea.extent.height, depth->extent.height);
        }
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &colour;
        info.pDepthAttachment = depth ? &ds : nullptr;
        info.pStencilAttachment = depth ? &ds : nullptr;
        vkCmdBeginRendering(command, &info);
        tally("render passes");
        rendering = true;
        boundColour = colourId;
        boundDepth = depthId;
        VkViewport viewport{0, 0, static_cast<float>(c.image.extent.width), static_cast<float>(c.image.extent.height), 0, 1};
        vkCmdSetViewport(command, 0, 1, &viewport);
    };
    // The stencil copy of a target's alpha bit 7, for the destination alpha test.
    // Only over `region` (render pixels): the draws that will test it (copying a whole 1280 x 1344 target 14 times a
    // frame for the cars' reflection pass was 0.3 ms).
    ex.prepareStencil = [this](uint32_t colourId, uint32_t depthId, VkRect2D region) {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        const bool perPass = ex.perPass;
        auto& end = ex.end;
        end();
        if (perPass) mark(command, "between passes");
        tally("stencil copies");
        GpuTarget& c = targets[colourId];
        vk::Image& depth = depthId == ~0u ? scratchDepth : targets[depthId].image;
        vk::toSampled(b, c.image, vk::kFragment);
        vk::toDepth(b, depth);
        b.flush(command);
        VkRenderingAttachmentInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ds.imageView = depth.view;
        ds.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        ds.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ds.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingAttachmentInfo stencil = ds;
        stencil.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        stencil.clearValue.depthStencil = {0.0f, 0};
        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        const VkExtent2D whole{std::min(c.image.extent.width, depth.extent.width), std::min(c.image.extent.height, depth.extent.height)};
        const uint32_t x0 = std::min(uint32_t(region.offset.x), whole.width), y0 = std::min(uint32_t(region.offset.y), whole.height);
        info.renderArea = {{int32_t(x0), int32_t(y0)}, {std::min(region.extent.width, whole.width - x0), std::min(region.extent.height, whole.height - y0)}};
        info.layerCount = 1;
        info.pDepthAttachment = &ds;
        info.pStencilAttachment = &stencil;
        vkCmdBeginRendering(command, &info);
        VkViewport viewport{0, 0, static_cast<float>(whole.width), static_cast<float>(whole.height), 0, 1};
        VkRect2D scissor = info.renderArea;
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        vk::bind(command, stencilPrep);
        vkCmdSetStencilTestEnable(command, VK_TRUE);
        vkCmdSetStencilOp(command, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
        vkCmdSetStencilCompareMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
        vkCmdSetStencilWriteMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
        vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
        vk::Descriptors().sampled(0, c.image.view, samplers[0]).push(command, stencilPrep);
        vkCmdDraw(command, 3, 1, 0, 0);
        vkCmdEndRendering(command);
        if (perPass) mark(command, "stencil copies");
    };

    // The scene stage (docs/NATIVE_RENDERER.md, docs/LIGHTING.md): its images (SceneImages).
    ex.sceneImages = [this](const gsn::FrameList::Lighting& l, SceneImages& out) {
        auto ct = targets.find(l.target), dt = targets.find(l.depth);
        if (ct == targets.end() || dt == targets.end()) return false;
        VkExtent2D area{};
        if (!sceneArea(shapeOf(ct->second), shapeOf(dt->second), l, area)) return false;
        out.colour = &ct->second.image;
        out.depth = &dt->second.image;
        out.sx = ct->second.scaleX;
        out.sy = ct->second.scaleY;
        out.area = area;
        return true;
    };
    // The stage's result copied back into the scene, except an inset view (another camera: the mirror).
    ex.copyBack = [this](vk::Image& from, vk::Image& colour, VkExtent2D e, const gsn::FrameList::Lighting& l, uint32_t sx, uint32_t sy) {
        const VkCommandBuffer command = ex.command;
        VkImageCopy regions[4]{};
        uint32_t n = 0;
        auto add = [&](uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {   // [x0, x1) x [y0, y1), clipped to e
            x1 = std::min(x1, e.width);
            y1 = std::min(y1, e.height);
            if (x0 >= x1 || y0 >= y1) return;
            VkImageCopy& c = regions[n++];
            c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.srcOffset = c.dstOffset = {static_cast<int32_t>(x0), static_cast<int32_t>(y0), 0};
            c.extent = {x1 - x0, y1 - y0, 1};
        };
        if (!l.hasInset) {
            add(0, 0, e.width, e.height);
        } else {
            const uint32_t ix0 = l.inset[0] * sx, iy0 = l.inset[1] * sy, ix1 = (l.inset[2] + 1u) * sx, iy1 = (l.inset[3] + 1u) * sy;
            add(0, 0, e.width, std::min(iy0, e.height));          // above
            add(0, iy1, e.width, e.height);                        // below
            add(0, iy0, std::min(ix0, e.width), iy1);              // left
            add(ix1, iy0, e.width, iy1);                           // right
        }
        if (n) vkCmdCopyImage(command, from.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colour.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, n, regions);
    };
    // An image of the stage at least `e` large (kept, grown).
    ex.ensureImage = [this](vk::Image& image, VkExtent2D e, VkFormat format, VkImageUsageFlags usage, const char* name) {
        if (image.valid() && image.extent.width >= e.width && image.extent.height >= e.height) return false;
        if (image.valid()) retire(image);
        vk::ImageDesc desc;
        desc.format = format;
        desc.extent = {std::max(e.width, image.extent.width), std::max(e.height, image.extent.height)};
        desc.usage = usage;
        desc.name = name;
        image = vk::createImage(device, desc);
        return true;
    };
    // Lighting: ambient occlusion from the Z buffer and the enhanced grade, computed into litImage and copied back.
    // Into the work images' ao and lit (ex.work); the members are sized here, the graph's transients by its plan, and
    // only the members' barriers are the stage's (the graph makes the transients' at each part's pass).
    ex.relight = [this](const gsn::FrameList::Lighting& l, int part) {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        auto& sceneImages = ex.sceneImages;
        auto& end = ex.end;
        auto& copyBack = ex.copyBack;
        const WorkImages& w = ex.work;
        const bool own = !w.graph;   // the members: the stage's own barriers
        SceneImages si;
        if (!sceneImages(l, si)) return;
        vk::Image& colour = *si.colour;
        vk::Image& depth = *si.depth;
        end();
        const VkExtent2D e = si.area;
        if (own) {
            ensureWork(*w.ao, aoExtent, e, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "gsn occlusion");
            ensureWork(*w.lit, litExtent, e, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "gsn lit");
        }
        const VkImageView depthOnly = depthView(depth);
        constexpr float kZScale = 1.6e6f;
        constexpr uint32_t kAoStep = 2;
        static const uint32_t lightView = std::getenv("GT4_LIGHT_VIEW") ? static_cast<uint32_t>(std::atoi(std::getenv("GT4_LIGHT_VIEW"))) : 0;   // GT4's 3D Z = 1.6e6 / view distance (24-bit Z; race captures)
        if (part == kPartWhole || part == kPartFirst) {
            vk::Image& ao = *w.ao;
            vk::toSampled(b, colour, vk::kCompute);
            vk::toSampled(b, depth, vk::kCompute);
            if (own) vk::toStorage(b, ao, true);
            b.flush(command);
            if (lighting.occlusion > 0.0f) {
                vk::bind(command, aoPass);
                vk::Descriptors().sampled(0, depthOnly, samplers[0]).storage(1, ao.view).push(command, aoPass);
                // At half resolution (the light pass's depth-aware blur reads it so): a quarter of the work.
                struct {
                    float size[2], tanHalf[2], zScale, radius, intensity, maxPixels;
                    uint32_t directions, step;
                } k{{float(e.width), float(e.height)}, {lighting.tanX, lighting.tanY}, kZScale, 1.2f, 1.4f, 40.0f * float(si.sy),
                    lighting.directions, kAoStep};
                vk::pushConstants(command, aoPass, k);
                vk::dispatch2D(command, (e.width + kAoStep - 1) / kAoStep, (e.height + kAoStep - 1) / kAoStep);
            }
        }
        if (part == kPartWhole || part == kPartMain) {
            vk::Image& ao = *w.ao;
            vk::Image& lit = *w.lit;
            // N3: where the skipped items would have drawn (before the light pass, which reads it); the Z buffer back in
            // its sampled layout after
            const bool masked = night > 0.0f && !skipSpans.empty() && drawSkipMask(l, ex.lightAt, colour, *si.depth, si.sx, si.sy);
            if (own) {
                vk::toSampled(b, ao, vk::kCompute);
                vk::toStorage(b, lit, true);
            }
            b.flush(command);
            // lighting.night: the view's lamps, clustered, and the local luminance (none: the light pass adds nothing and
            // its night bindings get stand-ins it does not read).
            NightPush nightPush;
            NightBuffers nightBuffers;
            const bool lamps = night > 0.0f && nightPrepare(l, si, colour.view, depthOnly, nightPush, nightBuffers);
            nightPush.skipMasked = masked ? 1.0f : 0.0f;
            vk::bind(command, lightPass);
            vk::Descriptors()
                .sampled(0, colour.view, samplers[0])
                .sampled(1, ao.view, samplers[0])
                .sampled(2, depthOnly, samplers[0])
                .storage(3, lit.view)
                .storageBuffer(4, lamps ? nightBuffers.header->buffer : nightCells.buffer, 0, sizeof(render::LightListHeader))
                .storageBuffer(5, lamps ? nightBuffers.lights->buffer : nightCells.buffer, 0,
                               lamps ? nightBuffers.lightBytes : sizeof(render::GpuLight))
                .storageBuffer(6, nightCells.buffer, 0, sizeof(render::ClusterCell) * render::kClusterCount)
                .storageBuffer(7, nightIndices.buffer, 0,
                               sizeof(uint32_t) * render::kClusterCount * render::kMaxLightsPerCluster)
                .sampled(8, lamps ? nightLocal.view : ao.view, samplers[0])
                .sampled(9, masked ? skipMask.view : ao.view, samplers[0])
                .push(command, lightPass);
            const LightPush k{{float(e.width), float(e.height)}, lighting.occlusion, kZScale, lighting.grade, 1.18f, 0.3f, 1.0f,
                              lightView, kAoStep, nightPush};
            vk::pushConstants(command, lightPass, k);
            vk::dispatch2D(command, e.width, e.height);
        }
        if (part == kPartWhole || part == kPartCopy) {
            vk::Image& lit = *w.lit;
            if (own) b.image(lit, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
            b.image(colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            b.flush(command);
            copyBack(lit, colour, e, l, si.sx, si.sy);
        }
    };

    // Temporal anti-aliasing: the 3D draws (depth-tested) into a scene's target before its Light command are
    // jittered by this frame's sub-pixel offset, the same for every scene of the frame.
    ex.jittered.assign(frame.commands.size(), 0);
    std::vector<uint8_t>& jittered = ex.jittered;
    ex.phase = kHalton[displayedFrames % 8];   // render pixels: over each target's own scale
    if (temporal) {
        std::vector<std::pair<uint32_t, uint32_t>> lit;   // (target, Z buffer) of the scenes lit later
        for (size_t at = frame.commands.size(); at-- > 0;) {
            const auto& cmd = frame.commands[at];
            if (cmd.kind == gsn::FrameList::Command::Kind::Light && cmd.index < frame.lights.size()) {
                lit.emplace_back(frame.lights[cmd.index].target, frame.lights[cmd.index].depth);
            } else if (cmd.kind == gsn::FrameList::Command::Kind::Draw && !lit.empty()) {
                const gsn::Draw& d = frame.draws[cmd.index];
                if (d.depth && d.state.c.depthTest && std::find(lit.begin(), lit.end(), std::make_pair(d.target, d.depth)) != lit.end())
                    jittered[at] = 1;
            }
        }
    }
    // The scene's motion vectors: its 3D draws again, with their previous positions (gsn_velocity.vert), each
    // where the Z buffer the scene left shows it (depth test EQUAL; alpha-tested cut-outs need no texture: where
    // a fence was not drawn, the Z is what is behind it). 2D and transparent draws (blended without a Z write)
    // leave the surface under them. Into velocityImage, GS pixels.
    ex.drawVelocity = [this](const gsn::FrameList::Lighting& l, size_t lightAt) {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        auto& sceneImages = ex.sceneImages;
        auto& end = ex.end;
        vk::Buffer& vertices = *ex.vertices;
        const uint32_t modelBase = ex.modelBase;
        const std::vector<uint8_t>& jittered = ex.jittered;
        const float* const phase = ex.phase;
        SceneImages si;
        if (!sceneImages(l, si)) return false;
        end();
        vk::Image& colour = *si.colour;
        vk::Image& depth = *si.depth;
        vk::Image& velocity = *ex.work.velocity;   // (a member, or the graph's transient, in COLOR_ATTACHMENT_OPTIMAL)
        if (!ex.work.graph) {
            ensureWork(velocity, velocityExtent, colour.extent, kVelocity, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       "gsn motion vectors");
            vk::toColor(b, velocity, true);
        }
        vk::toDepth(b, depth);
        b.flush(command);
        VkRenderingAttachmentInfo v{};
        v.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        v.imageView = velocity.view;
        v.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        v.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        v.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingAttachmentInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ds.imageView = depth.view;
        ds.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        ds.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ds.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea = {{0, 0}, {std::min({colour.extent.width, depth.extent.width, velocity.extent.width}),
                                    std::min({colour.extent.height, depth.extent.height, velocity.extent.height})}};
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &v;
        info.pDepthAttachment = &ds;
        info.pStencilAttachment = &ds;
        vkCmdBeginRendering(command, &info);
        VkViewport viewport{0, 0, static_cast<float>(colour.extent.width), static_cast<float>(colour.extent.height), 0, 1};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vk::bind(command, velocityPass);
        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &zero);
        vkCmdSetDepthCompareOp(command, VK_COMPARE_OP_EQUAL);
        DrawConstants k{};
        k.targetSize[0] = static_cast<float>(colour.extent.width);
        k.targetSize[1] = static_cast<float>(colour.extent.height);
        k.scale[0] = static_cast<float>(si.sx);
        k.scale[1] = static_cast<float>(si.sy);
        k.sourceScale[0] = k.scale[0];
        k.sourceScale[1] = k.scale[1];
        const uint32_t sx = si.sx, sy = si.sy;
        for (size_t at = 0; at < lightAt; ++at) {
            const auto& cmd = frame.commands[at];
            if (cmd.kind != gsn::FrameList::Command::Kind::Draw) continue;
            const gsn::Draw& d = frame.draws[cmd.index];
            if (d.target != l.target || d.topology != gsn::Topology::Triangles || !d.count) continue;
            if (d.bracket && ((skippedBrackets >> d.bracket) & 1)) continue;   // (N4: not drawn, so no motion of its own)
            if (!(d.moving || (!d.modeled && (frame.vertices[d.first].mw > 0.0f || frame.vertices[d.first + d.count - 1].mw > 0.0f)))) continue;
            const gfx::Context& c = d.state.c;
            const bool zt = d.depth && c.depthTest;
            if (d.depth && d.depth != l.depth) continue;
            if (c.colorKeep == 0xFFFFFFFFu) continue;                                    // Z only
            if (((d.state.prim >> 6) & 1) && !(zt && c.depthWrite)) continue;            // transparent
            vkCmdSetDepthTestEnable(command, zt ? VK_TRUE : VK_FALSE);
            const uint32_t x0 = c.scissorX0, y0 = c.scissorY0;
            const uint32_t right = std::min((uint32_t(c.scissorX1) + 1) * sx, info.renderArea.extent.width),
                           bottom = std::min((uint32_t(c.scissorY1) + 1) * sy, info.renderArea.extent.height);
            if (right <= x0 * sx || bottom <= y0 * sy) continue;
            const VkRect2D scissor{{static_cast<int32_t>(x0 * sx), static_cast<int32_t>(y0 * sy)}, {right - x0 * sx, bottom - y0 * sy}};
            vkCmdSetScissor(command, 0, 1, &scissor);
            k.jitter[0] = jittered[at] ? phase[0] / float(sx) : 0.0f;
            k.jitter[1] = jittered[at] ? phase[1] / float(sy) : 0.0f;
            vk::pushConstants(command, velocityPass, k);
            if (d.modeled && cmd.index + 1 < skipAt.size() && skipAt[cmd.index] != skipAt[cmd.index + 1]) {
                uint32_t cursor = d.first;   // (N3: a skipped item has no motion vectors, as it has no pixels)
                const uint32_t end = d.first + d.count;
                for (uint32_t i = skipAt[cmd.index]; i < skipAt[cmd.index + 1]; ++i) {
                    const SkipSpan& s = skipSpans[i];
                    if (s.first < cursor || s.first + s.count > end) continue;
                    if (s.first > cursor) vkCmdDraw(command, s.first - cursor, 1, modelBase + cursor, 0);
                    cursor = s.first + s.count;
                }
                if (end > cursor) vkCmdDraw(command, end - cursor, 1, modelBase + cursor, 0);
            } else {
                vkCmdDraw(command, d.count, 1, d.modeled ? modelBase + d.first : d.first, 0);
            }
        }
        vkCmdEndRendering(command);
        if (seamOn && seamPlan.found() && seamPlan.lightAt == lightAt) keepSeamVelocity(command, velocity);   // (B16c)
        return true;
    };
    // A pass of the scene stage over the scene's area: the scene (binding 0, linear), its Z (1), the motion
    // vectors (2: the scene when there are none) into the work images' lit (3), copied back.
    // `third`: the image at binding 2 (the motion vectors, the half-resolution reflections; else the scene again: a work
    // image, synchronised as lit is). `part`: kPartMain the pass, kPartCopy the copy (kPartWhole: both).
    ex.stagePass = [this](const gsn::FrameList::Lighting& l, const SceneImages& si, vk::Pipeline& pass, const void* push, uint32_t pushSize,
                         vk::Image* third, int part) {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        auto& end = ex.end;
        auto& copyBack = ex.copyBack;
        const WorkImages& w = ex.work;
        const bool own = !w.graph;   // the members: the stage's own barriers
        end();
        vk::Image& colour = *si.colour;
        const VkExtent2D e = si.area;
        vk::Image& lit = *w.lit;
        if (own) ensureWork(lit, litExtent, e, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "gsn lit");
        if (part == kPartWhole || part == kPartMain) {
            vk::toSampled(b, colour, vk::kCompute);
            vk::toSampled(b, *si.depth, vk::kCompute);
            if (third && own) vk::toSampled(b, *third, vk::kCompute);
            if (own) vk::toStorage(b, lit, true);
            b.flush(command);
            vk::bind(command, pass);
            vk::Descriptors()
                .sampled(0, colour.view, samplers[2])
                .sampled(1, depthView(*si.depth), samplers[0])
                .sampled(2, third ? third->view : colour.view, samplers[0])
                .storage(3, lit.view)
                .push(command, pass);
            vk::pushConstants(command, pass, push, pushSize);
            vk::dispatch2D(command, e.width, e.height);
        }
        if (part == kPartWhole || part == kPartCopy) {
            if (own) b.image(lit, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
            b.image(colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            b.flush(command);
            copyBack(lit, colour, e, l, si.sx, si.sy);
        }
    };
    constexpr float kZScale = 1.6e6f;   // GT4's 3D Z = 1.6e6 / view distance (docs/LIGHTING.md)
    static const uint32_t ssrView = std::getenv("GT4_SSR_VIEW") ? static_cast<uint32_t>(std::atoi(std::getenv("GT4_SSR_VIEW"))) : 0;
    // `part`: kPartFirst the trace, kPartMain the composite, kPartCopy its copy (kPartWhole: all).
    ex.reflect = [this](const gsn::FrameList::Lighting& l, int part) {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        auto& sceneImages = ex.sceneImages;
        auto& end = ex.end;
        auto& stagePass = ex.stagePass;
        const WorkImages& w = ex.work;
        const bool own = !w.graph;
        SceneImages si;
        if (!sceneImages(l, si)) return;
        struct {
            float size[2], tanHalf[2], zScale, strength;
            uint32_t steps, frame;
            float roughness;
            uint32_t view;
        } k{{float(si.area.width), float(si.area.height)}, {lighting.tanX, lighting.tanY}, kZScale, effects.reflections,
            effects.reflectionSteps, static_cast<uint32_t>(displayedFrames), temporal ? 0.06f : 0.0f, ssrView};
        // Traced at half resolution into the work images' ssr, then blended in at full resolution (gsn_ssr_composite.comp).
        end();
        const VkExtent2D half{(si.area.width + 1) / 2, (si.area.height + 1) / 2};
        if (part == kPartWhole || part == kPartFirst) {
            vk::Image& ssr = *w.ssr;
            if (own) ensureWork(ssr, ssrExtent, half, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "gsn reflections");
            vk::toSampled(b, *si.colour, vk::kCompute);
            vk::toSampled(b, *si.depth, vk::kCompute);
            if (own) vk::toStorage(b, ssr, true);
            b.flush(command);
            vk::bind(command, ssrPass);
            vk::Descriptors()
                .sampled(0, si.colour->view, samplers[0])
                .sampled(1, depthView(*si.depth), samplers[0])
                .sampled(2, si.colour->view, samplers[0])
                .storage(3, ssr.view)
                .push(command, ssrPass);
            vk::pushConstants(command, ssrPass, k);
            vk::dispatch2D(command, half.width, half.height);
        }
        if (part == kPartFirst) return;
        struct {
            float size[2], zScale;
            uint32_t view;
        } c{{float(si.area.width), float(si.area.height)}, kZScale, ssrView};
        stagePass(l, si, ssrComposite, &c, sizeof c, w.ssr, part);
    };
    ex.haze = [this](const gsn::FrameList::Lighting& l, int part) {
        auto& sceneImages = ex.sceneImages;
        auto& stagePass = ex.stagePass;
        SceneImages si;
        if (!sceneImages(l, si) || !l.fogged) return;
        const uint32_t f = l.fogColour;
        struct {
            float size[2], tanHalf[2], zScale, density, falloff, height;
            float colour[4];
        } k{{float(si.area.width), float(si.area.height)}, {lighting.tanX, lighting.tanY}, kZScale, effects.fogDensity, 0.02f, 1.5f,
            {float(f & 0xFF) / 255.0f, float((f >> 8) & 0xFF) / 255.0f, float((f >> 16) & 0xFF) / 255.0f, 1.0f}};
        stagePass(l, si, fogPass, &k, sizeof k, nullptr, part);
    };
    ex.blur = [this](const gsn::FrameList::Lighting& l, int part) {
        auto& sceneImages = ex.sceneImages;
        auto& stagePass = ex.stagePass;
        SceneImages si;
        if (!sceneImages(l, si)) return;
        struct {
            float size[2], scale[2], imageSize[2], shutter, maxBlur, zScale;
            uint32_t samples, frame;
        } k{{float(si.area.width), float(si.area.height)}, {float(si.sx), float(si.sy)},
            {float(si.colour->extent.width), float(si.colour->extent.height)}, effects.shutter, 0.05f * float(si.area.height), kZScale,
            effects.blurSamples, static_cast<uint32_t>(displayedFrames)};
        stagePass(l, si, blurPass, &k, sizeof k, ex.work.velocity, part);
    };
    ex.focus = [this](const gsn::FrameList::Lighting& l, int part) {
        auto& sceneImages = ex.sceneImages;
        auto& stagePass = ex.stagePass;
        SceneImages si;
        if (!sceneImages(l, si)) return;
        struct {
            float size[2], tanHalf[2], imageSize[2], zScale, focus, fNumber, maxCoc;
            uint32_t samples;
        } k{{float(si.area.width), float(si.area.height)}, {lighting.tanX, lighting.tanY},
            {float(si.colour->extent.width), float(si.colour->extent.height)}, kZScale, effects.focus, effects.aperture,
            0.02f * float(si.area.height), effects.dofSamples};
        stagePass(l, si, dofPass, &k, sizeof k, nullptr, part);
    };

    // The resolve: the scene against its history (gsn_taa.comp), into the history's other image, copied back.
    ex.antialias = [this](const gsn::FrameList::Lighting& l) {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        auto& sceneImages = ex.sceneImages;
        auto& ensureImage = ex.ensureImage;
        const float* const phase = ex.phase;
        SceneImages si;
        if (!sceneImages(l, si)) return;
        vk::Image& colour = *si.colour;
        const VkExtent2D e = si.area;
        History& h = histories[l.target];
        bool reset = !h.valid || displayedFrames > h.used + 1;
        for (vk::Image& i : h.image)
            reset = ensureImage(i, e, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                "gsn scene history") || reset;
        vk::Image& before = h.image[h.current];
        vk::Image& after = h.image[1 - h.current];
        vk::Image& velocity = *ex.work.velocity;   // (a work image: the graph's transient is synchronised by the graph)
        vk::toSampled(b, colour, vk::kCompute);
        if (!ex.work.graph) vk::toSampled(b, velocity, vk::kCompute);
        vk::toSampled(b, *si.depth, vk::kCompute);
        if (!reset) vk::toSampled(b, before, vk::kCompute);
        vk::toStorage(b, after, true);
        b.flush(command);
        vk::bind(command, taaPass);
        vk::Descriptors()
            .sampled(0, colour.view, samplers[0])
            .sampled(1, velocity.view, samplers[0])
            .sampled(2, depthView(*si.depth), samplers[0])
            .sampled(3, reset ? colour.view : before.view, samplers[2])
            .storage(4, after.view)
            .push(command, taaPass);
        static const uint32_t view = std::getenv("GT4_TAA_VIEW") ? static_cast<uint32_t>(std::atoi(std::getenv("GT4_TAA_VIEW"))) : 0;
        struct {
            float size[2], historySize[2], scale[2], jitter[2], reset;
            uint32_t view;
        } k{{float(e.width), float(e.height)}, {float(before.extent.width), float(before.extent.height)}, {float(si.sx), float(si.sy)},
            {phase[0], phase[1]}, reset ? 1.0f : 0.0f, view};
        vk::pushConstants(command, taaPass, k);
        vk::dispatch2D(command, e.width, e.height);
        b.image(after, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.image(colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        b.flush(command);
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {e.width, e.height, 1};
        vkCmdCopyImage(command, after.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colour.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        h.current = 1 - h.current;
        h.used = displayedFrames;
        h.valid = true;
    };

    // Reinterpreted textures: built from their target when a draw first needs them.
    ex.count = frame.reinterpretations.size();
    const size_t count = ex.count;
    built.assign(count, false);
    ex.cluts = &buffer(clutBuffers, slot, std::max<VkDeviceSize>(count * 1024, 1024), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    vk::Buffer& cluts = *ex.cluts;
    for (size_t i = 0; i < count; ++i) std::memcpy(static_cast<uint8_t*>(cluts.mapped) + i * 1024, frame.reinterpretations[i].clut.data(), 1024);
    // Indices from GS memory (target 0), one buffer for the frame: each one's start in words.
    ex.indexStart.assign(count, 0);
    std::vector<uint32_t>& indexStart = ex.indexStart;
    size_t& indexWords = ex.indexWords;   // (1)
    for (size_t i = 0; i < count; ++i) {
        indexStart[i] = static_cast<uint32_t>(indexWords);
        indexWords += (frame.reinterpretations[i].indices.size() + 3) / 4;
    }
    ex.indexBuffer = &buffer(indexBuffers, slot, indexWords * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    vk::Buffer& indexBuffer = *ex.indexBuffer;
    for (size_t i = 0; i < count; ++i) {
        const auto& indices = frame.reinterpretations[i].indices;
        if (!indices.empty()) std::memcpy(static_cast<uint8_t*>(indexBuffer.mapped) + indexStart[i] * 4, indices.data(), indices.size());
    }
    // Their images come from a pool by size (sizes change from frame to frame; no waiting on the GPU).
    for (auto& [size, pool] : reinterpretPool) pool.used = 0;
    reinterpretImages.assign(count, nullptr);
    // Each is built at the scale of the target its draw renders into (gsn.frag's kSnap reads it per render pixel).
    ex.builtScale.assign(count, {sx, sy});
    ex.imageFor = [this](uint32_t index) -> vk::Image& {
        const auto& builtScale = ex.builtScale;
        const gsn::Reinterpretation& r = frame.reinterpretations[index];
        const auto [bx, by] = builtScale[index];
        Pool& pool = reinterpretPool[(uint64_t(r.width * bx) << 32) | (r.height * by)];
        if (pool.used == pool.images.size()) {
            vk::ImageDesc desc;
            desc.format = VK_FORMAT_R8G8B8A8_UNORM;
            desc.extent = {r.width * bx, r.height * by};   // the drawing target's scale per GS texel
            desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            desc.name = "gsn reinterpreted";
            pool.images.push_back(std::make_unique<vk::Image>(vk::createImage(device, desc)));
        }
        return *pool.images[pool.used++];
    };
    ex.sourceOf = [this](const gsn::Reinterpretation& r) -> GpuTarget* {
        if (!r.target) return nullptr;
        auto it = targets.find(r.target);
        // (a Z buffer only for a Z texture, P2c)
        return it == targets.end() || !it->second.image.valid() || it->second.depth != r.depth ? nullptr : &it->second;
    };
    ex.paletteOf = [this](const gsn::Reinterpretation& r) -> GpuTarget* {   // a palette drawn into a target (clutWritten)
        if (!r.clutTarget) return nullptr;
        auto it = targets.find(r.clutTarget);
        return it == targets.end() || !it->second.image.valid() || it->second.depth ? nullptr : &it->second;
    };
    // Builds the reinterpretation a draw needs, with every later one whose targets no draw in between
    // writes (their contents are the same now): one compute batch instead of a pass break each (GT4's colour
    // grading needs about 600 a frame).
    ex.build = [this](uint32_t index, size_t at) -> vk::Image* {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        const bool perPass = ex.perPass;
        auto& end = ex.end;
        auto& sourceOf = ex.sourceOf;
        auto& paletteOf = ex.paletteOf;
        auto& imageFor = ex.imageFor;
        auto& builtScale = ex.builtScale;
        const size_t count = ex.count;
        vk::Buffer& cluts = *ex.cluts;
        const std::vector<uint32_t>& indexStart = ex.indexStart;
        vk::Buffer& indexBuffer = *ex.indexBuffer;
        const size_t indexWords = ex.indexWords;
        if (frame.reinterpretations[index].target && !sourceOf(frame.reinterpretations[index])) return nullptr;
        if (built[index]) return reinterpretImages[index];
        end();
        if (perPass) mark(command, "between passes");
        std::vector<uint32_t> batch{index};
        std::vector<uint32_t> written;
        auto drawnScale = [&](uint32_t target) {
            auto t = targets.find(target);
            return t == targets.end() ? std::make_pair(sx, sy) : std::make_pair(t->second.scaleX, t->second.scaleY);
        };
        builtScale[index] = drawnScale(frame.draws[frame.commands[at].index].target);
        for (size_t j = at + 1, seen = 0; j < frame.commands.size() && seen < 256; ++j, ++seen) {
            const auto& cmd = frame.commands[j];
            // A Light's scene stage writes its target, a colour grade its destination: a reinterpretation of either
            // after it must be built from what they leave, so it is not built ahead of them in this batch.
            if (cmd.kind == gsn::FrameList::Command::Kind::Light) {
                if (cmd.index < frame.lights.size()) written.push_back(frame.lights[cmd.index].target);
                continue;
            }
            if (cmd.kind == gsn::FrameList::Command::Kind::Effect) {
                if (cmd.index < frame.effects.size()) written.push_back(frame.effects[cmd.index].destination);
                continue;
            }
            if (cmd.kind != gsn::FrameList::Command::Kind::Draw) continue;
            const gsn::Draw& d = frame.draws[cmd.index];
            if (d.texture.kind == gsn::TextureSource::Kind::Reinterpreted && d.texture.id < count && !built[d.texture.id] &&
                std::find(batch.begin(), batch.end(), d.texture.id) == batch.end()) {
                const gsn::Reinterpretation& r = frame.reinterpretations[d.texture.id];
                const bool stale = std::find(written.begin(), written.end(), r.target) != written.end() ||
                                   std::find(written.begin(), written.end(), r.clutTarget) != written.end();
                if (!stale && (!r.target || sourceOf(r))) {
                    batch.push_back(d.texture.id);
                    builtScale[d.texture.id] = drawnScale(d.target);
                }
            }
            written.push_back(d.target);
            if (d.depth) written.push_back(d.depth);
        }
        for (const uint32_t i : batch) {
            const gsn::Reinterpretation& r = frame.reinterpretations[i];
            reinterpretImages[i] = &imageFor(i);
            if (GpuTarget* source = sourceOf(r)) vk::toSampled(b, source->image, vk::kCompute);
            if (GpuTarget* palette = paletteOf(r)) vk::toSampled(b, palette->image, vk::kCompute);
            vk::toStorage(b, *reinterpretImages[i], true);
        }
        b.flush(command);
        vk::bind(command, reinterpret);
        for (const uint32_t i : batch) {
            const gsn::Reinterpretation& r = frame.reinterpretations[i];
            GpuTarget* source = sourceOf(r);
            GpuTarget* palette = paletteOf(r);
            struct {
                uint32_t targetBp, targetBw, targetHeight, scaleX, tbp, tbw, psm, width, height, cpsm, csa, texa;
                uint32_t clutTargetBp, clutBw, clutHeight, cbp;
                uint32_t indexStart, scaleY, sourceScale, clutScale;
                uint32_t zmode, pad[3];   // P2c: a Z buffer read as a Z texture: 1 | inverted << 1 | the Z buffer's psm << 8
            } k{r.targetBp, source ? r.targetBw : 0, source ? source->height : 0, builtScale[i].first, r.texture.base,
                r.texture.width, r.texture.format, r.width, r.height,
                r.texture.clutFormat, r.texture.clutOffset,
                r.texAlpha0 | (static_cast<uint32_t>(r.texAlpha1) << 8) | (static_cast<uint32_t>(r.texBlackTransparent) << 16),
                palette ? r.clutTargetBp : 0, palette ? r.clutTargetBw : 0, palette ? palette->height : 0,
                r.texture.clutBase, source ? 0 : indexStart[i], builtScale[i].second,
                source ? source->scaleX | (source->scaleY << 8) : sx | (sy << 8), palette ? palette->scaleX | (palette->scaleY << 8) : sx | (sy << 8),
                r.depth ? 1u | (r.depthInverted ? 2u : 0u) | (r.targetPsm << 8) : 0u, {}};
            // (a Z buffer: its depth plane)
            const VkImageView fallback = source ? (r.depth ? depthView(source->image) : source->image.view) : palette ? palette->image.view : blank.view;
            vk::Descriptors()
                .sampled(0, fallback, samplers[0])
                .storage(1, reinterpretImages[i]->view)
                .storageBuffer(2, cluts.buffer, VkDeviceSize(i) * 1024, 1024)
                .sampled(3, palette ? palette->image.view : fallback, samplers[0])
                .storageBuffer(4, indexBuffer.buffer, 0, indexWords * 4)
                .push(command, reinterpret);
            vk::pushConstants(command, reinterpret, k);
            vk::dispatch2D(command, r.width * builtScale[i].first, r.height * builtScale[i].second);
            built[i] = true;
        }
        tally("reinterpret batches");
        tally("reinterpret dispatches", batch.size());
        for (const uint32_t i : batch) vk::toSampled(b, *reinterpretImages[i], vk::kFragment);
        b.flush(command);
        if (perPass) mark(command, "reinterpretations");
        return reinterpretImages[index];
    };

    // GT4's post effects as passes (docs/NATIVE_V2.md, step 3): the colour grading, computed into gradeImage over
    // the rectangle and copied back into the destination.
    vk::Buffer*& curves = ex.curves;   // (nullptr)
    if (!frame.effects.empty()) {
        curves = &buffer(curveBuffers, slot, frame.effects.size() * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        for (size_t i = 0; i < frame.effects.size(); ++i)
            std::memcpy(static_cast<uint8_t*>(curves->mapped) + i * 1024, frame.effects[i].curve.data(), 1024);
    }
    ex.grade = [this](uint32_t index) {
        const VkCommandBuffer command = ex.command;
        vk::Barriers& b = ex.b;
        auto& end = ex.end;
        auto& ensureImage = ex.ensureImage;
        vk::Buffer* const curves = ex.curves;
        const gsn::FrameList::Effect& e = frame.effects[index];
        auto s = targets.find(e.source), d = targets.find(e.destination);
        if (s == targets.end() || d == targets.end() || !curves) return;
        end();
        GpuTarget& source = s->second;
        GpuTarget& destination = d->second;
        const VkExtent2D full = destination.image.extent;
        const uint32_t x0 = std::min(e.x0 * destination.scaleX, full.width), y0 = std::min(e.y0 * destination.scaleY, full.height);
        const uint32_t x1 = std::min((e.x1 + 1) * destination.scaleX, full.width), y1 = std::min((e.y1 + 1) * destination.scaleY, full.height);
        if (x0 >= x1 || y0 >= y1) return;
        const VkExtent2D area{x1 - x0, y1 - y0};
        ensureImage(gradeImage, area, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "gsn graded");
        vk::toSampled(b, source.image, vk::kCompute);
        if (&destination != &source) vk::toSampled(b, destination.image, vk::kCompute);
        vk::toStorage(b, gradeImage, true);
        b.flush(command);
        vk::bind(command, gradePass);
        vk::Descriptors()
            .sampled(0, source.image.view, samplers[0])
            .sampled(1, destination.image.view, samplers[0])
            .storage(2, gradeImage.view)
            .storageBuffer(3, curves->buffer, VkDeviceSize(index) * 1024, 1024)
            .push(command, gradePass);
        struct {
            int32_t origin[2], size[2];
            float sourcePerDestination[2];
            uint32_t weights[3];
        } k{{int32_t(x0), int32_t(y0)}, {int32_t(area.width), int32_t(area.height)},
            {float(source.scaleX) / float(destination.scaleX), float(source.scaleY) / float(destination.scaleY)},
            {e.weights[0], e.weights[1], e.weights[2]}};
        vk::pushConstants(command, gradePass, &k, sizeof k);
        vk::dispatch2D(command, area.width, area.height);
        b.image(gradeImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.image(destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        b.flush(command);
        VkImageCopy c{};
        c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.dstOffset = {int32_t(x0), int32_t(y0), 0};
        c.extent = {area.width, area.height, 1};
        vkCmdCopyImage(command, gradeImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        tally("colour gradings");
        mark(command, "colour grading");
    };
}

// The list's commands [from, to): draws and effects (a Light among them: executeLight, as segments never puts one in a
// range). GT4_GSN_LIMIT's count and stop go on across the list's ranges and Lights (ex.executed, ex.limited).
void GsnScene::Impl::executeRange(VkCommandBuffer command, uint32_t slot, size_t from, size_t to) {
    ex.command = command;
    vk::Barriers& b = ex.b;
    bool& rendering = ex.rendering;
    uint32_t& boundColour = ex.boundColour;
    uint32_t& boundDepth = ex.boundDepth;
    bool& stencilFresh = ex.stencilFresh;
    uint32_t& stencilColour = ex.stencilColour;
    uint32_t& stencilDepth = ex.stencilDepth;
    VkRect2D& stencilRegion = ex.stencilRegion;
    VkPipeline& boundPipeline = ex.boundPipeline;
    auto& recorded = ex.recorded;
    auto& end = ex.end;
    auto& begin = ex.begin;
    auto& prepareStencil = ex.prepareStencil;
    auto& build = ex.build;
    auto& grade = ex.grade;
    const std::vector<uint8_t>& jittered = ex.jittered;
    const float* const phase = ex.phase;
    const size_t count = ex.count;
    vk::Buffer& vertices = *ex.vertices;
    const uint32_t modelBase = ex.modelBase;
    // GT4_GSN_LIMIT=K (debugging): only the first K draws of each frame.
    static const long limit = std::getenv("GT4_GSN_LIMIT") ? std::strtol(std::getenv("GT4_GSN_LIMIT"), nullptr, 10) : -1;
    long& executed = ex.executed;
    if (ex.limited) {   // (GT4_GSN_LIMIT reached earlier: nothing more is drawn; no render pass left open past this pass)
        end();
        return;
    }
    for (size_t at = from; at < to; ++at) {
        if (seamOn && seamPlan.found() && !seamDone && at == seamPlan.at + 1) runSeam(command);   // (B16c: right after the anchor)
        const auto& cmd = frame.commands[at];
        // B8 step 2: a side-channel pass before a substituted view's Z clear or Light (planSides)
        while (sideNext < sideBounds.size() && sideBounds[sideNext].command <= at) runSide(sideBounds[sideNext++]);
        if (cmd.kind == gsn::FrameList::Command::Kind::Light) {
            executeLight(command, slot, at);
            continue;
        }
        if (cmd.kind == gsn::FrameList::Command::Kind::Effect) {
            if (cmd.index < frame.effects.size()) {
                if (frame.effects[cmd.index].destination == stencilColour) stencilFresh = false;
                grade(cmd.index);
            }
            continue;
        }
        if (cmd.kind != gsn::FrameList::Command::Kind::Draw) continue;
        if (limit >= 0 && executed++ >= limit) {
            ex.limited = true;   // (no later command of the list: its ranges and Lights after this one too)
            break;
        }
        const gsn::Draw& d = frame.draws[cmd.index];
        if (d.bracket && ((skippedBrackets >> d.bracket) & 1)) {   // N4: the flare's (glare's) draws onto the picture
            ++(d.bracket == gsn::kBracketFlare ? counters.flareDrawsSkipped : counters.glareDrawsSkipped);
            continue;
        }
        auto targetIt = targets.find(d.target);
        if (targetIt == targets.end()) continue;
        const gsn::DrawState& s = d.state;
        const uint32_t prim = s.prim;
        DrawConstants k{};
        GpuTarget& colourTarget = targetIt->second;
        const uint32_t sx = colourTarget.scaleX, sy = colourTarget.scaleY;   // this draw's
        k.targetSize[0] = static_cast<float>(colourTarget.image.extent.width);
        k.targetSize[1] = static_cast<float>(colourTarget.image.extent.height);
        k.scale[0] = static_cast<float>(sx);
        k.scale[1] = static_cast<float>(sy);
        k.sourceScale[0] = k.scale[0];
        k.sourceScale[1] = k.scale[1];
        // The texture.
        VkImageView view = blank.view;
        VkSampler sampler = samplers[0];
        if ((prim >> 4) & 1) {
            k.flags |= kTextured;
            if ((prim >> 8) & 1) k.flags |= kUv;
            const gfx::Texture& tx = s.c.texture;
            const gfx::Sampler& sm = s.c.sampler;
            const uint32_t tw = tx.log2Width, th = tx.log2Height;
            k.textureSize[0] = static_cast<float>(1u << std::min(tw, 10u));
            k.textureSize[1] = static_cast<float>(1u << std::min(th, 10u));
            const bool linear = sm.magLinear;
            const bool repeat = sm.wrapU == 0;
            if (!linear) k.flags |= kNearest;
            if (gsExact()) k.flags |= kGsExact;
            k.clampU = sm.wrapU | (static_cast<uint32_t>(sm.minU) << 2) | (static_cast<uint32_t>(sm.maxU) << 12);
            k.clampV = sm.wrapV | (static_cast<uint32_t>(sm.minV) << 2) | (static_cast<uint32_t>(sm.maxV) << 12);
            k.lod = sm.lodFixed | (static_cast<uint32_t>(sm.lodL) << 1) | ((static_cast<uint32_t>(sm.lodK) & 0xFFF) << 3) |
                    (static_cast<uint32_t>(sm.mipMax) << 16);
            sampler = samplers[(linear ? 2 : 0) + (repeat ? 1 : 0)];
            if (d.texture.kind == gsn::TextureSource::Kind::Target) {
                auto source = targets.find(d.texture.id);
                if (source != targets.end() && source->second.image.valid() && !source->second.depth) {
                    vk::Image* image = &source->second.image;
                    if (d.texture.id == d.target) {   // drawing into what it samples: from a copy
                        end();
                        if (!feedback.valid() || feedback.extent.width < image->extent.width || feedback.extent.height < image->extent.height) {
                            if (feedback.valid()) retire(feedback);
                            vk::ImageDesc desc;
                            desc.format = kColour;
                            desc.extent = image->extent;
                            desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                            desc.name = "gsn feedback";
                            feedback = vk::createImage(device, desc);
                        }
                        b.image(*image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
                        b.image(feedback, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
                        b.flush(command);
                        VkImageCopy copy{};
                        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        copy.extent = {image->extent.width, image->extent.height, 1};
                        vkCmdCopyImage(command, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, feedback.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                        tally("feedback copies");
                        image = &feedback;
                        k.sourceSize[0] = static_cast<float>(feedback.extent.width) / sx;
                        k.sourceSize[1] = static_cast<float>(feedback.extent.height) / sy;
                    } else {
                        const uint32_t ox = source->second.scaleX, oy = source->second.scaleY;
                        k.sourceSize[0] = static_cast<float>(source->second.image.extent.width) / ox;
                        k.sourceSize[1] = static_cast<float>(source->second.image.extent.height) / oy;
                        k.sourceScale[0] = static_cast<float>(ox);
                        k.sourceScale[1] = static_cast<float>(oy);
                    }
                    if (image->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                        end();
                        vk::toSampled(b, *image, vk::kFragment);
                        b.flush(command);
                    }
                    view = image->view;
                    k.flags |= kFromTarget;
                    if (source->second.psm == gsn::PSMCT24) k.flags |= kTarget24;
                    k.sourceOffset[0] = static_cast<float>(d.texture.offsetX);
                    k.sourceOffset[1] = static_cast<float>(d.texture.offsetY);
                    sampler = samplers[(linear ? 2 : 0)];
                }
            } else if (d.texture.kind == gsn::TextureSource::Kind::Memory) {
                auto t = textures.find(d.texture.id);
                if (t != textures.end()) {
                    view = t->second.image.view;
                    // The GS's mipmaps, sampled as a modern GPU does; above 1x, a 3D draw's bilinear texture
                    // without them gets the levels made from it (the GS shows such textures aliased).
                    // (With made levels, gsn.frag takes the alpha the alpha test sees from level 0, as the GS
                    // without mipmaps: averaged levels would make cut-outs such as fences and foliage solid.)
                    // Not for sprites (2D, even with STQ: the dealers' logos) nor for the region modes of
                    // CLAMP, which the hardware sampler does not know (the logos' REGION_CLAMP keeps the rows
                    // under them out; sampled past it they drew a line of other texels).
                    const bool perspective = !((prim >> 8) & 1) && (prim & 7) != 6;
                    const bool plainWrap = sm.wrapU < 2 && sm.wrapV < 2;
                    const bool made = t->second.levels == 1 && t->second.generated > 1 && perspective && plainWrap && linear &&
                                      (sx > 1 || sy > 1);
                    if (t->second.replaced) {
                        // A texture pack's image: filtered and mipmapped by the sampler, in normalised coordinates
                        // (the region modes of CLAMP at the GS texture's texels, gsn.frag), whatever the draw's filter.
                        k.flags = (k.flags & ~kNearest) | kReplaced;
                        // the rectangle of the GS texture it stands for (a region PCSX2 named it by), in texels
                        k.sourceOffset[0] = static_cast<float>(d.texture.regionX);
                        k.sourceOffset[1] = static_cast<float>(d.texture.regionY);
                        k.sourceSize[0] = d.texture.regionWidth ? static_cast<float>(d.texture.regionWidth) : k.textureSize[0];
                        k.sourceSize[1] = d.texture.regionHeight ? static_cast<float>(d.texture.regionHeight) : k.textureSize[1];
                        const bool plain = sm.wrapU < 2 && sm.wrapV < 2;
                        sampler = mipSamplers[plain ? (sm.wrapU == 0 ? 2 : 0) + (sm.wrapV == 0 ? 1 : 0) : 0];
                    } else if (t->second.levels > 1 || made) {
                        k.flags = (k.flags & ~kNearest) | kMipmapped | (made ? kGeneratedMips : 0u);
                        sampler = mipSamplers[(sm.wrapU == 0 ? 2 : 0) + (sm.wrapV == 0 ? 1 : 0)];
                    }
                }
            } else if (d.texture.kind == gsn::TextureSource::Kind::Reinterpreted && d.texture.id < count) {
                // Bytes of a target read as indices. Read 1:1 (the colour grading's index ramps and palettes):
                // once per GS pixel, at this render pixel's sub-texel. Scaled with the bilinear filter (the
                // dealer's smoky backdrop squeezes the whole previous frame, as indices, into a smaller
                // rectangle): the GS's four-texel filter over GS texels, each the mean of its sub-texels.
                if (vk::Image* image = build(d.texture.id, at)) view = image->view;
                k.flags |= linear && !oneToOne(d, k) ? kSnapFiltered : kSnap;
            }
            k.tex = tx.function | (static_cast<uint32_t>(tx.useAlpha) << 2) | (static_cast<uint32_t>(s.texAlpha0) << 8) |
                    (static_cast<uint32_t>(s.texAlpha1) << 16) | (static_cast<uint32_t>(s.texBlackTransparent) << 24);
        }
        if ((prim >> 5) & 1) k.flags |= kFog;
        const gfx::Context& c = s.c;
        k.fogColour = s.fogColor;
        k.alphaTest = c.alphaTest | (static_cast<uint32_t>(c.alphaFunc) << 1) | (static_cast<uint32_t>(c.alphaRef) << 4) |
                      (static_cast<uint32_t>(c.alphaFail) << 12);
        if (c.forceAlpha) k.flags |= kFba;
        // The blend.
        uint32_t blendKey = 0;
        if ((prim >> 6) & 1) {
            k.flags |= kBlend;
            if (s.perPixelBlend) k.flags |= kPabe;
            const uint32_t A = c.blendA, B = c.blendB, C = c.blendC, D = c.blendD;
            k.blend = A | (B << 2) | (C << 4) | (D << 6) | (static_cast<uint32_t>(c.blendFixed) << 8);
            if (C == 1) {
                VkBlendFactor src, dst;
                VkBlendOp op;
                if (destAlphaBlend(A, B, D, src, dst, op)) {
                    k.flags |= kDestAlphaBlend;
                    blendKey = 0x8000u | static_cast<uint32_t>(src) | (static_cast<uint32_t>(dst) << 5) | (static_cast<uint32_t>(op) << 10);
                } else if (!warnedBlend) {
                    warnedBlend = true;
                    std::fprintf(stderr, "gsn: blend %u%u%u%u has no fixed-function form; drawn unblended\n", A, B, C, D);
                }
                if (!(k.flags & kDestAlphaBlend)) k.flags &= ~kBlend;
            }
        }
        // Colour mask: FBMSK bytes, and no alpha in a 24-bit frame.
        const uint32_t fbmsk = c.colorKeep;
        uint32_t mask = 0;
        if ((fbmsk & 0xFF) != 0xFF) mask |= VK_COLOR_COMPONENT_R_BIT;
        if (((fbmsk >> 8) & 0xFF) != 0xFF) mask |= VK_COLOR_COMPONENT_G_BIT;
        if (((fbmsk >> 16) & 0xFF) != 0xFF) mask |= VK_COLOR_COMPONENT_B_BIT;
        if (((fbmsk >> 24) & 0xFF) != 0xFF && c.colorFormat != gsn::PSMCT24) mask |= VK_COLOR_COMPONENT_A_BIT;
        // Destination alpha test: through the stencil copy.
        const bool date = c.destTest;
        uint32_t depthId = d.depth;
        if (date && !depthId) {
            depthId = ~0u;
            if (!scratchDepth.valid() || scratchDepth.extent.width < colourTarget.image.extent.width ||
                scratchDepth.extent.height < colourTarget.image.extent.height) {
                if (scratchDepth.valid()) retire(scratchDepth);
                if (stencilDepth == ~0u) stencilFresh = false;   // (a new image)
                vk::ImageDesc desc;
                desc.format = kDepth;
                desc.extent = colourTarget.image.extent;
                desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                desc.name = "gsn scratch depth";
                scratchDepth = vk::createImage(device, desc);
            }
        }
        if (date) {
            // The draw's extent in render pixels (its vertices, a pixel's margin, inside its scissor).
            auto box = [&](const gsn::Draw& e) {
                float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
                if (e.modeled) bx0 = by0 = -1e30f, bx1 = by1 = 1e30f;   // (its vertices are the GPU's: the scissor)
                for (uint32_t v = 0; v < (e.modeled ? 0u : e.count); ++v) {
                    const gsn::Vertex& p = frame.vertices[e.first + v];
                    float x = p.x, y = p.y;
                    if (p.w != 0.0f) {   // homogeneous: projected; behind the eye: the scissor
                        if (!(p.w > 0.0f)) {
                            bx0 = by0 = -1e30f;
                            bx1 = by1 = 1e30f;
                            break;
                        }
                        x /= p.w;
                        y /= p.w;
                    }
                    bx0 = std::min(bx0, x); by0 = std::min(by0, y); bx1 = std::max(bx1, x); by1 = std::max(by1, y);
                }
                const gfx::Context& ec = e.state.c;
                const float lx = std::max(std::floor(bx0) - 1.0f, float(ec.scissorX0)), ly = std::max(std::floor(by0) - 1.0f, float(ec.scissorY0));
                const float hx = std::min(std::ceil(bx1) + 1.0f, float(ec.scissorX1 + 1)), hy = std::min(std::ceil(by1) + 1.0f, float(ec.scissorY1 + 1));
                if (!(hx > lx && hy > ly)) return VkRect2D{{0, 0}, {0, 0}};
                return VkRect2D{{int32_t(lx) * int32_t(sx), int32_t(ly) * int32_t(sy)}, {uint32_t(hx - lx) * sx, uint32_t(hy - ly) * sy}};
            };
            auto unite = [](VkRect2D a, VkRect2D r) {
                if (!r.extent.width || !r.extent.height) return a;
                if (!a.extent.width || !a.extent.height) return r;
                const int32_t x0 = std::min(a.offset.x, r.offset.x), y0 = std::min(a.offset.y, r.offset.y);
                const int32_t x1 = std::max(a.offset.x + int32_t(a.extent.width), r.offset.x + int32_t(r.extent.width));
                const int32_t y1 = std::max(a.offset.y + int32_t(a.extent.height), r.offset.y + int32_t(r.extent.height));
                return VkRect2D{{x0, y0}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}};
            };
            const VkRect2D mine = box(d);
            const bool inside = mine.offset.x >= stencilRegion.offset.x && mine.offset.y >= stencilRegion.offset.y &&
                                mine.offset.x + int32_t(mine.extent.width) <= stencilRegion.offset.x + int32_t(stencilRegion.extent.width) &&
                                mine.offset.y + int32_t(mine.extent.height) <= stencilRegion.offset.y + int32_t(stencilRegion.extent.height);
            if (!stencilFresh || stencilColour != d.target || stencilDepth != depthId || !inside) {
                // This draw's extent and the destination-alpha draws into the target that follow it, joined.
                VkRect2D region = mine;
                for (size_t j = at + 1, seen = 0; j < frame.commands.size() && seen < 128; ++j, ++seen) {
                    const auto& next = frame.commands[j];
                    if (next.kind != gsn::FrameList::Command::Kind::Draw) continue;
                    const gsn::Draw& e = frame.draws[next.index];
                    if (e.target == d.target && e.state.c.destTest) region = unite(region, box(e));
                }
                prepareStencil(d.target, depthId, region);
                stencilRegion = region;
                stencilFresh = true;
                stencilColour = d.target;
                stencilDepth = depthId;
            }
        }
        // Keep the bound Z buffer for a draw without depth test on the same target, unless it is smaller than the
        // target: the pass's render area would cut the draw off (Photo Travel's help line under the menu's 416-line Z
        // buffer, in a 448-line frame).
        if (!depthId && rendering && boundColour == d.target) {
            const vk::Image* bound = boundDepth == ~0u ? &scratchDepth : boundDepth ? &targets[boundDepth].image : nullptr;
            if (!bound || (bound->extent.width >= colourTarget.image.extent.width && bound->extent.height >= colourTarget.image.extent.height))
                depthId = boundDepth;
        }
        if (!rendering || boundColour != d.target || boundDepth != depthId) {
            end();
            begin(d.target, depthId);
        }
        const bool hasDepth = depthId != 0;
        // Alpha written outside a DATE draw: with a stencil, its shader writes alpha bit 7 there (exports).
        const bool exports = device.stencilExport && hasDepth && !date && (mask & VK_COLOR_COMPONENT_A_BIT);
        if (!date && (mask & VK_COLOR_COMPONENT_A_BIT)) {
            const bool mirrored = exports && d.target == stencilColour && depthId == stencilDepth;
            if (!mirrored && (d.target == stencilColour || (exports && depthId == stencilDepth))) stencilFresh = false;
        }
        if (d.depth && !warnedScales) {
            const GpuTarget& z = targets[d.depth];
            if (z.scaleX != sx || z.scaleY != sy) {
                warnedScales = true;
                std::fprintf(stderr, "gsn: a %ux%u target drawn at %ux%u with a %ux%u Z buffer at %ux%u (scales differ)\n", colourTarget.width,
                             colourTarget.height, sx, sy, z.width, z.height, z.scaleX, z.scaleY);
            }
        }
        // Depth and stencil state.
        const bool zte = c.depthTest;
        const uint32_t ztst = c.depthFunc;
        static const VkCompareOp zops[4] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_GREATER};
        // (a Z buffer the game inverted in place: its Z turned round by the front end, the tests too; Draw::depthInverted)
        static const VkCompareOp invertedZops[4] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_LESS};
        const int compareKey = int(ztst) | (d.depthInverted ? 4 : 0);
        const bool zwrite = hasDepth && zte && d.depth && c.depthWrite;
        const int depthTest = hasDepth && zte && d.depth ? 1 : 0, stencilTest = date && hasDepth ? 1 : 0;
        if (recorded.depthTest != depthTest) vkCmdSetDepthTestEnable(command, depthTest ? VK_TRUE : VK_FALSE);
        if (recorded.compare != compareKey) vkCmdSetDepthCompareOp(command, (d.depthInverted ? invertedZops : zops)[ztst]);
        if (stencilTest && recorded.reference != int(c.destPass)) {
            vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, c.destPass);
            recorded.reference = int(c.destPass);
        }
        recorded.depthTest = depthTest;
        recorded.compare = compareKey;
        // Scissor, in render pixels.
        const uint32_t x0 = c.scissorX0, x1 = c.scissorX1;
        const uint32_t y0 = c.scissorY0, y1 = c.scissorY1;
        VkRect2D scissor{};
        scissor.offset = {static_cast<int32_t>(x0 * sx), static_cast<int32_t>(y0 * sy)};
        const uint32_t right = std::min((x1 + 1) * sx, colourTarget.image.extent.width), bottom = std::min((y1 + 1) * sy, colourTarget.image.extent.height);
        scissor.extent = {right > x0 * sx ? right - x0 * sx : 0, bottom > y0 * sy ? bottom - y0 * sy : 0};
        if (!scissor.extent.width || !scissor.extent.height) continue;
        if (std::memcmp(&scissor, &recorded.scissor, sizeof scissor) != 0) {
            vkCmdSetScissor(command, 0, 1, &scissor);
            recorded.scissor = scissor;
        }
        // The alpha test: pixels that pass are drawn as usual; those that fail, under AFAIL, update only the
        // frame buffer (FB_ONLY), only Z (ZB_ONLY) or only RGB (RGB_ONLY), or nothing (KEEP): a second pass
        // with the test inverted.
        const bool ate = c.alphaTest;
        const uint32_t atst = c.alphaFunc, afail = c.alphaFail;
        struct Pass {
            uint32_t mask;
            bool zwrite, inverted;
            uint32_t date = 0;   // 0, or kDateFlips / kDateKeeps
        } passes[4];
        uint32_t passCount = 0;
        if (!ate || atst != 0) passes[passCount++] = {mask, zwrite, false};
        if (ate && atst != 1 && afail != 0) {
            const uint32_t rgb = mask & (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT);
            passes[passCount++] = afail == 1 ? Pass{mask, false, true} : afail == 2 ? Pass{0, zwrite, true} : Pass{rgb, false, true};
        }
        // The destination alpha test of a draw that writes alpha: the GS tests each pixel as the draw's earlier
        // primitives left it (a reflection pass drawn over a decal's polygon and the body under it lights the pixel
        // once: the first sets bit 7, the second then fails). The stencil copy is kept in step: the pixels whose
        // written alpha changes bit 7 draw first and invert the stencil, then the others; each pass's pixels are
        // chosen in gsn.frag. (Exact unless one draw's overlapping primitives write alpha on both sides of bit 7.)
        if (stencilTest) {
            Pass split[4];
            uint32_t n = 0;
            for (uint32_t p = 0; p < passCount; ++p) {
                if (!(passes[p].mask & VK_COLOR_COMPONENT_A_BIT)) {
                    split[n++] = passes[p];
                    continue;
                }
                split[n] = passes[p], split[n++].date = kDateFlips;
                split[n] = passes[p], split[n++].date = kDateKeeps;
            }
            std::copy(split, split + n, passes);
            passCount = n;
        }
        for (uint32_t pass = 0; pass < passCount; ++pass) {
            const Pass& ps = passes[pass];
            if (!ps.mask && !ps.zwrite) continue;
            // The stencil: DATE's test (kept, or inverted where the pass's pixels flip bit 7), or alpha bit 7 written.
            const int stencilMode = stencilTest ? (ps.date == kDateFlips ? 2 : 1)
                                    : exports && (ps.mask & VK_COLOR_COMPONENT_A_BIT) ? 3 : 0;
            if (recorded.stencilMode != stencilMode) {
                vkCmdSetStencilTestEnable(command, stencilMode ? VK_TRUE : VK_FALSE);
                if (stencilMode) {
                    vkCmdSetStencilOp(command, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP,
                                      stencilMode == 2 ? VK_STENCIL_OP_INVERT : stencilMode == 3 ? VK_STENCIL_OP_REPLACE : VK_STENCIL_OP_KEEP,
                                      VK_STENCIL_OP_KEEP, stencilMode == 3 ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_EQUAL);
                    vkCmdSetStencilCompareMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
                    vkCmdSetStencilWriteMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, stencilMode >= 2 ? 1u : 0u);
                }
                recorded.stencilMode = stencilMode;
            }
            // Alpha bit 7 into the stencil (mode 3): gsn.frag's export variant writes the value itself
            // (gl_FragStencilRefARB, every fragment), but Vulkan still wants a reference set for the draw
            // (VUID-vkCmdDraw-None-07839): 0, which the export overrides, so no pixel changes.
            if (stencilMode == 3 && recorded.reference < 0) {
                vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
                recorded.reference = 0;
            }
            vk::Pipeline& p = pipeline(blendKey, ps.mask, hasDepth, d.topology, stencilMode == 3);
            if (p.pipeline != boundPipeline) {
                tally("pipeline binds");
                vk::bind(command, p);
                const VkDeviceSize zero = 0;
                vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &zero);
                boundPipeline = p.pipeline;
                recorded.view = VK_NULL_HANDLE;   // (the layouts are alike, but bindings are not assumed to survive)
                recorded.pushed = false;
            }
            if (recorded.depthWrite != int(ps.zwrite)) {
                vkCmdSetDepthWriteEnable(command, ps.zwrite ? VK_TRUE : VK_FALSE);
                recorded.depthWrite = int(ps.zwrite);
            }
            DrawConstants pk = k;
            if (ps.inverted) pk.flags |= kAlphaInvert;
            if (ps.date) pk.flags |= ps.date | (c.destPass ? uint32_t(kDatm) : 0u);
            if (jittered[at]) {
                pk.jitter[0] = phase[0] / float(sx);
                pk.jitter[1] = phase[1] / float(sy);
            }
            if (recorded.view != view || recorded.sampler != sampler) {
                vk::Descriptors().sampled(0, view, sampler).push(command, p);
                recorded.view = view;
                recorded.sampler = sampler;
                tally("descriptor pushes");
            }
            if (!recorded.pushed || std::memcmp(&pk, &recorded.constants, sizeof pk) != 0) {
                vk::pushConstants(command, p, pk);
                recorded.constants = pk;
                recorded.pushed = true;
                tally("constant pushes");
            }
            const bool skips = d.modeled && cmd.index + 1 < skipAt.size() && skipAt[cmd.index] != skipAt[cmd.index + 1];
            if (skips || (d.modeled && cmd.index + 1 < parityAt.size() && parityAt[cmd.index] != parityAt[cmd.index + 1])) {
                // B7: the draw's jobs drawn from the mesh cache where the plan has them, the rest from gsn_models.comp's
                // vertices, in the draw's own order (each job's vertices are contiguous, in the runs' order)
                uint32_t cursor = d.first;
                const uint32_t end = d.first + d.count;
                auto gsRange = [&](uint32_t from, uint32_t to) {
                    if (from >= to) return;
                    if (boundPipeline != p.pipeline) {   // (back from a parity draw: the GS draw's bindings again)
                        vk::bind(command, p);
                        const VkDeviceSize zero = 0;
                        vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &zero);
                        vk::Descriptors().sampled(0, view, sampler).push(command, p);
                        vk::pushConstants(command, p, pk);
                        boundPipeline = p.pipeline;
                    }
                    vkCmdDraw(command, to - from, 1, modelBase + from, 0);
                    tally("draws");
                };
                // N3: a skipped item's vertices drawn by neither path (its spans in the draw's order with B7's)
                uint32_t skipNext = skips ? skipAt[cmd.index] : 0;
                const uint32_t skipEnd = skips ? skipAt[cmd.index + 1] : 0;
                auto skipUpTo = [&](uint32_t limit) {
                    while (skipNext < skipEnd && skipSpans[skipNext].first < limit) {
                        const SkipSpan& s = skipSpans[skipNext++];
                        if (s.first < cursor || s.first + s.count > end) continue;   // (outside the draw: drawn)
                        gsRange(cursor, s.first);
                        cursor = s.first + s.count;
                    }
                };
                const uint32_t parityFrom = cmd.index + 1 < parityAt.size() ? parityAt[cmd.index] : 0;
                const uint32_t parityTo = cmd.index + 1 < parityAt.size() ? parityAt[cmd.index + 1] : 0;
                for (uint32_t i = parityFrom; i < parityTo; ++i) {
                    const render::ParitySpan& sp = paritySpans[i];
                    skipUpTo(sp.first);
                    // (an item both substituted and skipped: the skip wins, neither path draws it; lane A)
                    if (skipNext < skipEnd && skipSpans[skipNext].first == sp.first && sp.first >= cursor &&
                        sp.first + sp.count <= end) {
                        const SkipSpan& k = skipSpans[skipNext++];
                        gsRange(cursor, k.first);
                        cursor = std::max(k.first + k.count, sp.first + sp.count);
                        if (pass == 0) ++n3OverParity;
                        continue;
                    }
                    if (sp.first < cursor || sp.first + sp.count > end) {   // (not inside the draw: left to the GS path)
                        if (pass == 0) ++paritySpansOutside;
                        continue;
                    }
                    gsRange(cursor, sp.first);
                    vk::Pipeline& pp = parityPipeline(blendKey, ps.mask, hasDepth, stencilMode == 3);
                    if (boundPipeline != pp.pipeline) {
                        vk::bind(command, pp);
                        boundPipeline = pp.pipeline;
                    }
                    // (the item's mesh: its block, at the mesh's offsets)
                    const VkDeviceSize vertexAt = sp.vertexOffset;
                    vkCmdBindVertexBuffers(command, 0, 1, &parityBlocks[sp.block].buffer, &vertexAt);
                    vkCmdBindIndexBuffer(command, parityBlocks[sp.block].buffer, sp.indexOffset, VK_INDEX_TYPE_UINT32);
                    vk::Descriptors()
                        .sampled(0, view, sampler)
                        .storageBuffer(1, ex.jobs->buffer, 0, frame.models.size() * sizeof(gfx::ModelJob))
                        .storageBuffer(2, ex.words->buffer, 0, frame.modelData.size() * 4)
                        .push(command, pp);
                    // (the renderers' cull, hidden test and ADC: modern_parity.vert, per triangle as gsn_models.comp)
                    const ParityConstants pc{pk, sp.job, modelT, modelTPrevious, 0};
                    vk::pushConstants(command, pp, pc);
                    vkCmdDrawIndexed(command, sp.indexCount, 1, sp.firstIndex, 0, 0);   // (the mesh's own indices, from its offsets)
                    tally("parity draws");
                    cursor = sp.first + sp.count;
                }
                skipUpTo(end);
                gsRange(cursor, end);
                // (the parity pipeline's bindings and constants differ: the next draw binds and pushes its own)
                recorded.view = VK_NULL_HANDLE;
                recorded.pushed = false;
            } else {
                vkCmdDraw(command, d.count, 1, d.modeled ? modelBase + d.first : d.first, 0);
                tally("draws");
            }
        }
    }
    // A range ends with its render pass ended: a valid Light (the scene stage) or the list's end follows it (segments()
    // keeps a skipped Light inside its range), and their first act is end() anyway, so the commands are the same, and no
    // render pass stays open across a graph pass's boundary (lane A, B4 step 2b: barriers between passes must not land
    // inside one). execute() calls the pieces in segments()'s order too.
    // (B8 step 2: the pass before a Light that follows this range, as the Light is its own segment)
    while (sideNext < sideBounds.size() && sideBounds[sideNext].command <= to) runSide(sideBounds[sideNext++]);
    end();
    if (seamOn && seamPlan.found() && !seamDone && seamPlan.at + 1 == to && !ex.limited) runSeam(command);   // (B16c: ... the anchor last)
}

// B16c: the seam plan's Light keeps its motion vectors past its scene stage, copied at the end of the motion vectors'
// pass by a compute that samples them (gsn_seam_keep.comp): the stage's image (the members' work image or the graph's
// transient) is sampled anyway and has no transfer usage, so nothing about it changes with the seam on or off. It is
// given back as the pass left it, a colour attachment written, which the graph's next use of its transient expects.
void GsnScene::Impl::keepSeamVelocity(VkCommandBuffer command, vk::Image& velocity) {
    vk::Barriers& b = ex.b;
    if (!seamKeepPass.valid())
        seamKeepPass = vk::createCompute(device, shaders::gsn_seam_keep_comp(),
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, 0,
                                         "gsn seam: keep motion vectors");
    if (!seamLightVelocity.valid() || seamLightVelocity.extent.width != velocity.extent.width ||
        seamLightVelocity.extent.height != velocity.extent.height) {
        if (seamLightVelocity.valid()) retire(seamLightVelocity);
        vk::ImageDesc desc;
        desc.format = kVelocity;
        desc.extent = velocity.extent;
        desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.name = "gsn seam: the Light's motion vectors";
        seamLightVelocity = vk::createImage(device, desc);
    }
    vk::toSampled(b, velocity, vk::kCompute);
    vk::toStorage(b, seamLightVelocity, true);
    b.flush(command);
    vk::bind(command, seamKeepPass);
    vk::Descriptors().sampled(0, velocity.view, samplers[0]).storage(1, seamLightVelocity.view).push(command, seamKeepPass);
    vk::dispatch2D(command, velocity.extent.width, velocity.extent.height);
    b.image(velocity, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, vk::kColorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    b.flush(command);
    seamVelocityKept = true;
}

// B16c: the display pass's seam variant, made the first time a seamed frame is shown (the seam off makes nothing new):
// the same pass, also writing the shown picture's motion vectors and its depth (always).
void GsnScene::Impl::makeDisplaySeam() {
    vk::GraphicsDesc desc;
    desc.vertex = shaders::fullscreen_vert();
    desc.fragment = shaders::gsn_display_seam_frag();
    desc.colors = {formats.color, formats.velocity};
    desc.depth = formats.depth;
    desc.samples = formats.samples;
    desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    desc.pushSize = 64;
    desc.depthTest = desc.depthWrite = true;
    desc.depthCompare = VK_COMPARE_OP_ALWAYS;
    desc.name = "gsn display (seam)";
    displaySeam = vk::createGraphics(device, desc);
}

// B16c: right after the anchor, the made picture's motion vectors and depth over its displayed rectangle
// (gsn_seam.comp), sampled by the display pass while that picture is shown.
void GsnScene::Impl::runSeam(VkCommandBuffer command) {
    seamDone = true;
    const gsn::SeamAnchor& a = seamPlan;
    Seam& seam = seams[a.made];
    seam.valid = false;
    auto made = targets.find(a.made), zbuffer = targets.find(a.lightDepth), lit = targets.find(a.light);
    if (!seamVelocityKept || made == targets.end() || zbuffer == targets.end() || lit == targets.end() ||
        !made->second.image.valid() || !zbuffer->second.image.valid() || !zbuffer->second.depth)
        return;
    if (!seamPass.valid())
        seamPass = vk::createCompute(device, shaders::gsn_seam_comp(),
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                                     72, "gsn display seam");
    ex.end();
    vk::Barriers& b = ex.b;
    const VkExtent2D e = made->second.image.extent;
    for (auto [image, format, name] : {std::tuple{&seam.velocity, kVelocity, "gsn seam: motion vectors"},
                                       std::tuple{&seam.depth, VK_FORMAT_R32_SFLOAT, "gsn seam: depth"}}) {
        if (image->valid() && image->extent.width == e.width && image->extent.height == e.height) continue;
        if (image->valid()) retire(*image);
        vk::ImageDesc desc;
        desc.format = format;
        desc.extent = e;
        desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.name = name;
        *image = vk::createImage(device, desc);
    }
    GpuTarget& m = made->second;
    GpuTarget& z = zbuffer->second;
    const uint32_t x0 = std::min(uint32_t(std::max(a.rect[0], 0.0f)) * m.scaleX, e.width);
    const uint32_t y0 = std::min(uint32_t(std::max(a.rect[1], 0.0f)) * m.scaleY, e.height);
    const uint32_t x1 = std::min(uint32_t(std::max(a.rect[2], 0.0f)) * m.scaleX, e.width);
    const uint32_t y1 = std::min(uint32_t(std::max(a.rect[3], 0.0f)) * m.scaleY, e.height);
    if (x1 <= x0 || y1 <= y0) return;
    vk::toSampled(b, seamLightVelocity, vk::kCompute);
    vk::toSampled(b, z.image, vk::kCompute);
    vk::toStorage(b, seam.velocity, true);
    vk::toStorage(b, seam.depth, true);
    b.flush(command);
    vk::bind(command, seamPass);
    vk::Descriptors()
        .sampled(0, seamLightVelocity.view, samplers[0])
        .sampled(1, depthView(z.image), samplers[0])
        .storage(2, seam.velocity.view)
        .storage(3, seam.depth.view)
        .push(command, seamPass);
    // The Z format's largest value over 2^32 (an inverted buffer holds it less Z: GsFrontend's Z24 pass)
    const uint32_t zpsm = z.psm & 0xFu;   // (Z32 0x30, Z24 0x31, Z16 0x32 / 0x3A)
    const float most = zpsm == 0 ? 4294967295.0f : zpsm == 1 ? 16777215.0f : 65535.0f;
    struct {
        int32_t origin[2], size[2];
        float madeScale[2], lightScale[2], depthScale[2], mapScale[2], mapOffset[2], lightSize[2];
        float depthMost, inverted;
    } k{{int32_t(x0), int32_t(y0)}, {int32_t(x1 - x0), int32_t(y1 - y0)},
        {float(m.scaleX), float(m.scaleY)}, {float(lit->second.scaleX), float(lit->second.scaleY)},
        {float(z.scaleX), float(z.scaleY)}, {a.scale[0], a.scale[1]}, {a.offset[0], a.offset[1]},
        {float(seamLightVelocity.extent.width) / float(lit->second.scaleX), float(seamLightVelocity.extent.height) / float(lit->second.scaleY)},
        most / 4294967296.0f, a.depthInverted ? 1.0f : 0.0f};
    vk::pushConstants(command, seamPass, &k, sizeof k);
    vk::dispatch2D(command, x1 - x0, y1 - y0);
    vk::toSampled(b, seam.velocity, vk::kFragment);
    vk::toSampled(b, seam.depth, vk::kFragment);
    b.flush(command);
    seam.valid = true;
    ++seamRuns;
}
// B8 step 2: one side-channel pass (planSides' bound): the partition's reset, then its items with the depth test EQUAL.
void GsnScene::Impl::runSide(const SideBound& bound) {
    if (bound.view >= frame.views.size() || !sideItems) return;
    const gsn::FrameList::ViewOnList& view = frame.views[bound.view];
    auto targetIt = targets.find(view.target);
    auto depthIt = targets.find(view.depth);
    if (targetIt == targets.end() || depthIt == targets.end() || !targetIt->second.image.valid() || !depthIt->second.image.valid()) return;
    // (nothing to do: no item in the partition, and no channel of this list's to reset yet)
    const bool first = !sideClearedThisList.count(view.target);
    bool any = false;
    auto ours = [&](size_t i) {   // an item of a view drawing into this pass's target and Z buffer, in its partition
        const gsn::FrameList::ViewOnList& v = frame.views[paritySpans[i].view];
        return v.target == view.target && v.depth == view.depth && spanCommand[i] >= bound.from && spanCommand[i] < bound.command;
    };
    for (size_t i = 0; i < paritySpans.size() && !any; ++i) any = ours(i);
    if (!any && (first || !bound.cleared)) return;
    sideClearedThisList.insert(view.target);   // (the channels cleared at the list's first pass)
    const VkCommandBuffer command = ex.command;
    vk::Barriers& b = ex.b;
    ex.end();
    GpuTarget& colourTarget = targetIt->second;
    vk::Image& depth = depthIt->second.image;
    const VkExtent2D extent = colourTarget.image.extent;
    SideImages& side = sideTargets[view.target];
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ensureWork(side.normal, side.grown, extent, VK_FORMAT_R16G16B16A16_SFLOAT, usage, "gsn side normal");
    ensureWork(side.material, side.grown, extent, VK_FORMAT_R32_UINT, usage, "gsn side material");
    ensureWork(side.depth, side.grown, extent, VK_FORMAT_R32_SFLOAT, usage, "gsn side depth");
    for (vk::Image* i : {&side.normal, &side.material, &side.depth}) vk::toColor(b, *i, first);
    vk::toDepth(b, depth);
    b.flush(command);
    if (!sidePass.valid()) {
        vk::GraphicsDesc desc;
        desc.vertex = shaders::modern_side_vert();
        desc.fragment = shaders::modern_side_frag();
        desc.colors = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32_UINT, VK_FORMAT_R32_SFLOAT};
        desc.depth = kDepth;
        desc.stencil = kDepth;
        desc.depthTest = true;
        desc.depthWrite = false;
        desc.depthCompare = VK_COMPARE_OP_EQUAL;
        desc.vertices.stride = sizeof(scene::MeshRawVertex);
        desc.vertices.attributes = {{0, 0, VK_FORMAT_R32G32B32_UINT, offsetof(scene::MeshRawVertex, position)},
                                    {1, 0, VK_FORMAT_R32G32B32_UINT, offsetof(scene::MeshRawVertex, st)},
                                    {2, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(scene::MeshRawVertex, colour)},
                                    {3, 0, VK_FORMAT_R32G32_UINT, offsetof(scene::MeshRawVertex, next)},
                                    {4, 0, VK_FORMAT_R32_UINT, offsetof(scene::MeshRawVertex, flags)},
                                    {5, 0, VK_FORMAT_R32G32B32_UINT, offsetof(scene::MeshRawVertex, before)},
                                    {6, 0, VK_FORMAT_R32G32B32_UINT, offsetof(scene::MeshRawVertex, before) + 12}};
        desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
        desc.pushSize = sizeof(ParityConstants);
        desc.provokingLast = device.provokingLast;
        desc.name = "gsn side channels";
        sidePass = vk::createGraphics(device, desc);
        vk::GraphicsDesc reset;
        reset.vertex = shaders::gsn_side_reset_vert();
        reset.fragment = shaders::gsn_side_reset_frag();
        reset.colors = desc.colors;
        reset.depth = kDepth;
        reset.stencil = kDepth;
        reset.depthTest = true;
        reset.depthWrite = false;
        reset.depthCompare = VK_COMPARE_OP_NOT_EQUAL;
        reset.pushSize = sizeof(float);
        reset.name = "gsn side reset";
        sideResetPass = vk::createGraphics(device, reset);
        // the veiled marking: the GS path's own vertices (gsn.vert), depth test GREATER, only the normal channel's w
        vk::GraphicsDesc veil = drawDesc(0, 0, true, gsn::Topology::Triangles, false);
        veil.fragment = shaders::gsn_side_veil_frag();
        veil.colors = desc.colors;
        veil.depth = kDepth;
        veil.stencil = kDepth;
        // (one blend state for all three, as independentBlend is not required: alpha only, which the material and depth
        // channels, single-channel R32 formats, don't have, so only the normal channel's w is written)
        VkPipelineColorBlendAttachmentState mark{};
        mark.colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
        veil.blendStates = {mark, mark, mark};
        veil.dynamics.clear();
        veil.depthTest = true;
        veil.depthWrite = false;
        veil.depthCompare = VK_COMPARE_OP_GREATER;   // (strictly in front: GT4's coplanar blended layers, the lit road and
                                                     // tyre marks at the surface's own Z, are the surface, not a veil)
        veil.name = "gsn side veil";
        sideVeilPass = vk::createGraphics(device, veil);
        ++counters.pipelines;
    }
    VkRenderingAttachmentInfo colours[3]{};
    vk::Image* images[3] = {&side.normal, &side.material, &side.depth};
    for (int i = 0; i < 3; ++i) {
        colours[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colours[i].imageView = images[i]->view;
        colours[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colours[i].loadOp = first ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colours[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingAttachmentInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ds.imageView = depth.view;
    ds.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    ds.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ds.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {std::min(extent.width, depth.extent.width), std::min(extent.height, depth.extent.height)}};
    info.layerCount = 1;
    info.colorAttachmentCount = 3;
    info.pColorAttachments = colours;
    info.pDepthAttachment = &ds;
    info.pStencilAttachment = &ds;
    vkCmdBeginRendering(command, &info);
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &info.renderArea);
    ++sidePasses;
    const uint32_t sx = colourTarget.scaleX, sy = colourTarget.scaleY;
    if (bound.cleared) {   // the channels cleared where this partition wrote Z (the stored Z differs from the clear's), within
                           // the clear's own region
        const uint32_t x0 = std::min(bound.clearRect[0] * sx, info.renderArea.extent.width),
                       y0 = std::min(bound.clearRect[1] * sy, info.renderArea.extent.height),
                       x1 = std::min((bound.clearRect[2] + 1) * sx, info.renderArea.extent.width),
                       y1 = std::min((bound.clearRect[3] + 1) * sy, info.renderArea.extent.height);
        const VkRect2D region{{static_cast<int32_t>(x0), static_cast<int32_t>(y0)}, {x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0}};
        vkCmdSetScissor(command, 0, 1, &region);
        vk::bind(command, sideResetPass);
        vk::pushConstants(command, sideResetPass, &bound.clearDepth, sizeof(float));
        vkCmdDraw(command, 3, 1, 0, 0);
        ++sideResets;
    }
    // the partition's items, through the parity vertex stage (the same Z to the bit), depth test EQUAL
    vk::bind(command, sidePass);
    vk::Descriptors()
        .sampled(0, blank.view, samplers[0])
        .storageBuffer(1, ex.jobs->buffer, 0, frame.models.size() * sizeof(gfx::ModelJob))
        .storageBuffer(2, ex.words->buffer, 0, frame.modelData.size() * 4)
        .storageBuffer(3, sideItems->buffer, 0, paritySpans.size() * sizeof(SideItem))
        .push(command, sidePass);
    for (size_t i = 0; i < paritySpans.size(); ++i) {
        const render::ParitySpan& sp = paritySpans[i];
        if (!ours(i)) continue;
        const gsn::Draw& d = frame.draws[sp.draw];
        const gfx::Context& c = d.state.c;
        if (d.depth != view.depth || !c.depthTest || !c.depthWrite || c.colorKeep == 0xFFFFFFFFu) continue;   // (Z-tested and
        // Z-writing items only: a blended one without Z write is the veiled marking's, next)
        const uint32_t right = std::min((uint32_t(c.scissorX1) + 1) * sx, info.renderArea.extent.width),
                       bottom = std::min((uint32_t(c.scissorY1) + 1) * sy, info.renderArea.extent.height);
        if (right <= c.scissorX0 * sx || bottom <= c.scissorY0 * sy) continue;
        const VkRect2D scissor{{static_cast<int32_t>(c.scissorX0 * sx), static_cast<int32_t>(c.scissorY0 * sy)},
                               {right - c.scissorX0 * sx, bottom - c.scissorY0 * sy}};
        vkCmdSetScissor(command, 0, 1, &scissor);
        ParityConstants pc{};
        pc.draw.targetSize[0] = static_cast<float>(extent.width);
        pc.draw.targetSize[1] = static_cast<float>(extent.height);
        pc.draw.scale[0] = static_cast<float>(sx);
        pc.draw.scale[1] = static_cast<float>(sy);
        pc.draw.sourceScale[0] = pc.draw.scale[0];
        pc.draw.sourceScale[1] = pc.draw.scale[1];
        if (spanCommand[i] < ex.jittered.size() && ex.jittered[spanCommand[i]]) {
            pc.draw.jitter[0] = ex.phase[0] / float(sx);
            pc.draw.jitter[1] = ex.phase[1] / float(sy);
        }
        pc.job = sp.job;
        pc.t = modelT;
        pc.tPrevious = modelTPrevious;
        pc.side = static_cast<uint32_t>(i);
        vk::pushConstants(command, sidePass, pc);
        const VkDeviceSize vertexAt = sp.vertexOffset;
        vkCmdBindVertexBuffers(command, 0, 1, &parityBlocks[sp.block].buffer, &vertexAt);
        vkCmdBindIndexBuffer(command, parityBlocks[sp.block].buffer, sp.indexOffset, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, sp.indexCount, 1, sp.firstIndex, 0, 0);
        ++sideItemsDrawn;
    }
    // the veiled marking: the partition's blended draws into the target that write no Z (items' and untagged ones' alike,
    // from the GS path's vertices, which gsn_models.comp writes for every job), strictly in front of the surface
    bool veilBound = false;
    for (size_t at = bound.from; at < bound.command; ++at) {
        const auto& cmd = frame.commands[at];
        if (cmd.kind != gsn::FrameList::Command::Kind::Draw || cmd.index >= frame.draws.size()) continue;
        const gsn::Draw& d = frame.draws[cmd.index];
        const gfx::Context& c = d.state.c;
        if (d.target != view.target || d.depth != view.depth || d.topology != gsn::Topology::Triangles || !d.count) continue;
        // (blended, Z-tested against the surface (GEQUAL or GREATER: a draw with the test off or ALWAYS, as R5's full-screen
        // draws at 0.47 of the order, is a screen effect with no depth of its own), and writing no Z)
        // A view's own blended layers (GT4's road detail, decals: tagged runs, drawn slightly in front of their base) are its
        // surface, not a veil: only draws of no view's items (Draw::view 0: untagged runs, sprites; smoke, particles, glare)
        // mark it.
        if (d.view || !((d.state.prim >> 6) & 1) || !c.depthTest || c.depthFunc < 2 || c.depthWrite || c.colorKeep == 0xFFFFFFFFu) continue;
        if (d.bracket && ((skippedBrackets >> d.bracket) & 1)) continue;   // (N4: not drawn)
        const uint32_t right = std::min((uint32_t(c.scissorX1) + 1) * sx, info.renderArea.extent.width),
                       bottom = std::min((uint32_t(c.scissorY1) + 1) * sy, info.renderArea.extent.height);
        if (right <= c.scissorX0 * sx || bottom <= c.scissorY0 * sy) continue;
        if (!veilBound) {
            vk::bind(command, sideVeilPass);
            const VkDeviceSize zero = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &ex.vertices->buffer, &zero);
            veilBound = true;
        }
        const VkRect2D scissor{{static_cast<int32_t>(c.scissorX0 * sx), static_cast<int32_t>(c.scissorY0 * sy)},
                               {right - c.scissorX0 * sx, bottom - c.scissorY0 * sy}};
        vkCmdSetScissor(command, 0, 1, &scissor);
        DrawConstants k{};
        k.targetSize[0] = static_cast<float>(extent.width);
        k.targetSize[1] = static_cast<float>(extent.height);
        k.scale[0] = static_cast<float>(sx);
        k.scale[1] = static_cast<float>(sy);
        k.sourceScale[0] = k.scale[0];
        k.sourceScale[1] = k.scale[1];
        if (at < ex.jittered.size() && ex.jittered[at]) {
            k.jitter[0] = ex.phase[0] / float(sx);
            k.jitter[1] = ex.phase[1] / float(sy);
        }
        vk::pushConstants(command, sideVeilPass, k);
        vkCmdDraw(command, d.count, 1, d.modeled ? ex.modelBase + d.first : d.first, 0);
        ++sideVeils;
    }
    vkCmdEndRendering(command);
    // B8 step 3 (debugging): GT4_SIDE_VIEW=normal|material|depth|veiled shows that channel in place of the scene target's colour
    // at its Light (alpha kept), so the picture is what the scene stage will light with
    static const int sideView = [] {
        const char* e = std::getenv("GT4_SIDE_VIEW");
        if (!e) return -1;
        const std::string v = e;
        return v == "normal" ? 0 : v == "material" ? 1 : v == "depth" ? 2 : v == "veiled" ? 3 : -1;
    }();
    if (bound.light && sideView >= 0) {
        for (vk::Image* i : {&side.normal, &side.material, &side.depth}) vk::toSampled(b, *i, vk::kFragment);
        vk::toColor(b, colourTarget.image);
        b.flush(command);
        if (!sideViewPass.valid()) {
            vk::GraphicsDesc desc;
            desc.vertex = shaders::fullscreen_vert();
            desc.fragment = shaders::gsn_side_view_frag();
            desc.colors = {kColour};
            VkPipelineColorBlendAttachmentState write{};
            write.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
            desc.blendStates = {write};
            desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
            desc.pushSize = 8;
            desc.name = "gsn side view";
            sideViewPass = vk::createGraphics(device, desc);
        }
        VkRenderingAttachmentInfo target{};
        target.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        target.imageView = colourTarget.image.view;
        target.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        target.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        target.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo shown{};
        shown.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        shown.renderArea = info.renderArea;
        shown.layerCount = 1;
        shown.colorAttachmentCount = 1;
        shown.pColorAttachments = &target;
        vkCmdBeginRendering(command, &shown);
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &info.renderArea);
        vk::bind(command, sideViewPass);
        vk::Descriptors()
            .sampled(0, side.normal.view, samplers[0])
            .sampled(1, side.material.view, samplers[0])
            .sampled(2, side.depth.view, samplers[0])
            .push(command, sideViewPass);
        const struct { uint32_t channel; float depthRange; } constants{static_cast<uint32_t>(sideView), 300.0f};
        vk::pushConstants(command, sideViewPass, &constants, sizeof constants);
        vkCmdDraw(command, 3, 1, 0, 0);
        vkCmdEndRendering(command);
    }
    ex.rendering = false;
    ex.boundPipeline = VK_NULL_HANDLE;
    ex.recorded = ExecuteState::Recorded{};
}

// The scene stage at the list's Light command `at` (`frame.commands[at]`), as the commands' loop ran it.
// The scene stage, in a modern renderer's order: motion vectors, lighting, reflections and fog (before the resolve,
// which also integrates their noise), the resolve, motion blur, depth of field. On the members (ex.work), each step
// whole.
void GsnScene::Impl::executeLight(VkCommandBuffer command, uint32_t /*slot*/, size_t at) {
    if (!beginLight(command, at)) return;
    stageOnMembers(command, at);
}

// The steps after beginLight, each whole, on the members (they size themselves and make their own barriers).
void GsnScene::Impl::stageOnMembers(VkCommandBuffer command, size_t at) {
    ex.work = WorkImages{&velocityImage, &aoImage, &litImage, &ssrImage, false};
    const gsn::FrameList::Lighting& l = frame.lights[frame.commands[at].index];
    for (int step = 0; step < kSteps; ++step) {
        if (stepRuns(step, l, viewRecords(frame))) runStep(step, l, at, kPartWhole);
        mark(command, kStepMarks[step]);
    }
}

// A Light's scene stage that does not run as GsnScene::buildPrepass planned it (counted: GsnScene's summary prints it).
void GsnScene::Impl::planWrong(size_t at, const char* what) {
    ++stagesOnMembers;
    static uint64_t reported = 0;
    if (reported++ < 8)
        std::printf("gsn: the scene stage at Light %zu (list %u) %s: run on the member images\n", at, listIndex, what);
}

// The graph's one pass for a Light the build planned no transients for (no scene then, or no step): a scene with a
// step that runs now is a wrong plan. It runs correctly all the same (executeLight, on the members); only counted.
void GsnScene::Impl::checkOnePass(size_t at) {
    if (ex.limited || frame.commands[at].index >= frame.lights.size()) return;
    const gsn::FrameList::Lighting& l = frame.lights[frame.commands[at].index];
    SceneImages si;
    if (!ex.sceneImages(l, si)) return;
    for (int step = 0; step < kSteps; ++step)
        if (stepRuns(step, l, viewRecords(frame))) {
            planWrong(at, "was planned without a scene or a step, and has both");
            return;
        }
}

bool GsnScene::Impl::beginLight(VkCommandBuffer command, size_t at) {
    ex.command = command;
    ex.moving = false;
    if (ex.limited) return false;   // (GT4_GSN_LIMIT reached before it)
    const auto& cmd = frame.commands[at];
    if (cmd.index >= frame.lights.size()) return false;
    const gsn::FrameList::Lighting& l = frame.lights[cmd.index];
    if (l.target == ex.stencilColour) ex.stencilFresh = false;   // (the scene stage may write its alpha)
    ex.end();
    mark(command, "draws");
    tally("scenes");
    return true;
}

void GsnScene::Impl::runStep(int step, const gsn::FrameList::Lighting& l, size_t at, int part) {
    switch (step) {
        case kStepMotionVectors: ex.moving = ex.drawVelocity(l, at); break;
        case kStepLighting: ex.lightAt = at; ex.relight(l, part); break;
        case kStepReflections: ex.reflect(l, part); break;
        case kStepFog: ex.haze(l, part); break;
        case kStepTemporalResolve: if (ex.moving) ex.antialias(l); break;
        case kStepMotionBlur: if (ex.moving) ex.blur(l, part); break;
        case kStepFocus: ex.focus(l, part); break;
        default: break;
    }
}

void GsnScene::Impl::executeLightPart(VkCommandBuffer command, size_t at, int step, int part, const WorkImages& work,
                                      const std::vector<const char*>& before, const std::vector<const char*>& after,
                                      const LightPlan* first) {
    ex.command = command;
    if (first) {
        ex.lightSkipped = !beginLight(command, at);
        if (!ex.lightSkipped) {
            // The plan's guesses, made before the lists' targets were (GsnScene::buildPrepass): its transients' sizes and
            // passes came from them. Different (another area, another colour target's size, other steps), the
            // transients may be too small or missing: the whole stage runs here on the members, as executeLight runs
            // it, and the Light's planned passes record nothing (their transients are written and read by no command).
            const gsn::FrameList::Lighting& l = frame.lights[frame.commands[at].index];
            SceneImages si;
            const bool scene = ex.sceneImages(l, si);
            bool same = scene == first->scene;
            if (same && scene)
                same = si.area.width == first->area.width && si.area.height == first->area.height &&
                       si.colour->extent.width == first->colour.width && si.colour->extent.height == first->colour.height;
            for (int s = 0; s < kSteps; ++s) same = same && stepRuns(s, l, viewRecords(frame)) == first->runs[s];
            // GT4_WRONG_PLAN_TEST=1 (testing): every plan taken as wrong, so the fallback runs on every Light; the
            // frames must not change (lane A's suggestion, B4 step 3).
            static const bool wrongTest = [] { const char* e = std::getenv("GT4_WRONG_PLAN_TEST"); return e && *e && *e != '0'; }();
            if (wrongTest) same = false;
            if (same) ++stagesOnTransients;
            if (!same) {
                char what[160];
                std::snprintf(what, sizeof what, "%swas planned over %ux%u (colour %ux%u) but has %ux%u (colour %ux%u), or other steps",
                              wrongTest ? "(GT4_WRONG_PLAN_TEST) " : "", first->area.width, first->area.height, first->colour.width, first->colour.height,
                              scene ? si.area.width : 0u, scene ? si.area.height : 0u, scene ? si.colour->extent.width : 0u,
                              scene ? si.colour->extent.height : 0u);
                planWrong(at, what);
                stageOnMembers(command, at);
                ex.lightSkipped = true;
            }
        }
    }
    if (ex.lightSkipped) return;
    for (const char* m : before) mark(command, m);
    ex.work = work;
    runStep(step, frame.lights[frame.commands[at].index], at, part);
    for (const char* m : after) mark(command, m);
}

bool GsnScene::Impl::nightPrepare(const gsn::FrameList::Lighting& l, const SceneImages& si, VkImageView colourView,
                                  VkImageView depthOnly, NightPush& k, NightBuffers& out) {
    // Debugging only (GT4_NIGHT_TEST_LIGHTS without scene records, e.g. an older capture): the test lamps in a stand-in
    // view, GT4's monitor-preset field of view and its race-measured Z = 1.6e6 / distance (gsn_ao.comp's). The game's own
    // lights always come with their view's projection (CONTRACTS.md c.3: no fallback for them).
    if (!frame.scene) {
        if (!render::testLightCount()) return false;
        const VkExtent2D e = si.area;
        k.focal[0] = 0.5f * float(e.width) / lighting.tanX;
        k.focal[1] = -0.5f * float(e.height) / lighting.tanY;
        k.centre[0] = 0.5f * float(e.width);
        k.centre[1] = 0.5f * float(e.height);
        k.distanceA = 1.6e6f;
        k.distanceB = 0.0f;
        k.maxDistance = 1e4f;
        nightRecords = render::testLights(0, render::testLightCount());
        return nightFinish(l, si, colourView, depthOnly, 0, k, out);
    }
    const scene::SceneFrame& sf = *frame.scene;
    // Split screen (A12: each half a kViewMain into the one target, with its own scissor and camera): one light pass
    // over the target would light both halves with one half's projection and lamps, so the Light gets no lamps (GT4's
    // night look; the rest of its light pass as before), counted, until a pass per half (scissored to its rectangle)
    // exists.
    uint32_t mains = 0;
    for (const scene::ViewData& v : sf.views)
        if (v.target == l.target && v.role == gfx::scene::kViewMain) ++mains;
    if (mains > 1) {
        ++nightSplitSkipped;
        return false;
    }
    // The main view drawn into this scene's target: the largest scissor (the mirror's is the smaller one).
    const scene::ViewData* view = nullptr;
    uint32_t bestArea = 0;
    for (const scene::ViewData& v : sf.views) {
        if (v.target != l.target || !v.projection || v.projection > sf.projections.size()) continue;
        const uint32_t area = uint32_t(std::max(0, int(v.scissor[2]) - int(v.scissor[0]) + 1)) *
                              uint32_t(std::max(0, int(v.scissor[3]) - int(v.scissor[1]) + 1));
        if (!view || area > bestArea) view = &v, bestArea = area;
    }
    if (!view) return false;
    // The GS origin (XYOFFSET) of the view's 3D batches: from its first tagged job (ModelJob::pad[0] its instance,
    // pad[1] its view: CONTRACTS.md a.8, jobs by tag; InstanceData had no first job, B6). frame.models holds this list's
    // jobs only, so view ids from other frames can't match here.
    float originX = 0.0f, originY = 0.0f;
    bool origin = false;
    for (const gfx::ModelJob& job : frame.models)
        if (job.pad[0] && job.pad[1] == view->id) {
            originX = job.originX;
            originY = job.originY;
            origin = true;
            break;
        }
    if (!origin) return false;
    // The view's projection (GT4's row vectors: clip = p * m) and viewport (window = scale * ndc + offset), at the render
    // scale: render pixel = scale * (window - origin). See CONTRACTS.md c.3 and spike R1 for GT4's Z.
    const scene::ProjectionData& pd = sf.projections[view->projection - 1];
    const float (&m)[4][4] = pd.viewToClip.m;
    const float* vs = pd.viewportScale;
    const float* vo = pd.viewportOffset;
    const float sx = float(si.sx), sy = float(si.sy);
    k.focal[0] = sx * vs[0] * m[0][0];
    k.focal[1] = sy * vs[1] * m[1][1];
    k.centre[0] = sx * (vo[0] - originX - vs[0] * m[2][0]);
    k.centre[1] = sy * (vo[1] - originY - vs[1] * m[2][1]);
    // Z = vo.z + vs.z * (m22 z + m32) / -z, z = -d: d = vs.z m32 / (Z - vo.z + vs.z m22).
    k.distanceA = vs[2] * m[3][2];
    k.distanceB = vs[2] * m[2][2] - vo[2];
    const float far = std::fabs(m[2][2] + 1.0f) > 1e-6f ? m[3][2] / (m[2][2] + 1.0f) : 0.0f;
    k.maxDistance = far > 0.0f && std::isfinite(far) ? far * 1.01f : 1e4f;
    if (!(std::fabs(k.focal[0]) > 0.0f && std::fabs(k.focal[1]) > 0.0f && k.distanceA != 0.0f)) return false;
    // The lamps: the view's records, and the test lamps.
    nightRecords.assign(sf.lights.begin(), sf.lights.end());
    // (the test lamps follow the game's records: no kFromGame. kFromGame means 'came through a game record': F2b's
    // promoted billboard candidates keep it, as A7 step 1b sent them)
    nightHostFrom = nightRecords.size();
    // F2b: the main view's street lamps: A7 step 1b's billboard candidates whose instance's material has the glow class
    // become kSrcCourseGlow (the list's scene cache, shared with the front end, holds the classes)
    if (const std::shared_ptr<const scene::SceneCache> cache = frame.sceneCache) {
        render::CourseLampPass::Stats ls;
        const auto started = std::chrono::steady_clock::now();
        lampPass.promote(sf, view->id, [&cache](scene::MaterialId id) { return cache->materials().classes(id); }, nightRecords, &ls);
        LampTotals& t = lampTotals;
        t.microseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
        ++t.frames;
        t.candidates += ls.candidates;
        t.kept += ls.kept;
        t.dropped += ls.dropped;
        t.noInstance += ls.noInstance;
        t.merged += ls.merged;
        t.mergedAgain += ls.mergedAgain;
    }
    if (const uint32_t n = render::testLightCount()) {
        const std::vector<gfx::scene::Light> test = render::testLights(view->id, n);
        nightRecords.insert(nightRecords.end(), test.begin(), test.end());
    }
    return nightFinish(l, si, colourView, depthOnly, view->id, k, out);
}

bool GsnScene::Impl::nightFinish(const gsn::FrameList::Lighting& l, const SceneImages& si, VkImageView colourView,
                                 VkImageView depthOnly, uint16_t viewId, NightPush& k, NightBuffers& out) {
    if (l.hasInset) {
        k.inset[0] = float(l.inset[0]) * sx;
        k.inset[1] = float(l.inset[1]) * sy;
        k.inset[2] = float(l.inset[2] + 1) * sx - 1.0f;
        k.inset[3] = float(l.inset[3] + 1) * sy - 1.0f;
    }
    const render::LightListHeader header = render::gatherLights(nightRecords.data(), nightRecords.size(), viewId, 0.5f,
                                                                std::max(k.maxDistance, 1.0f), night, nightLights, nightStatsLast,
                                                                nightHostFrom);
    if (header.count == 0) return false;
    // The buffers of this Light command: per ordinal in the list (nightOrdinal, reset by executeBegin), and within an
    // ordinal's ring per (list, slot) (Impl::buffer keys by listIndex too), so every list of a submission has its own.
    const uint32_t ordinal = nightOrdinal++;
    if (nightHeaders.size() <= ordinal) {
        nightHeaders.resize(ordinal + 1);
        nightLists.resize(ordinal + 1);
        nightGrids.resize(ordinal + 1);
    }
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkDeviceSize lightBytes = sizeof(render::GpuLight) * nightLights.size();
    vk::Buffer& hb = buffer(nightHeaders[ordinal], ex.slot, sizeof(header), storage);
    vk::Buffer& lb = buffer(nightLists[ordinal], ex.slot, lightBytes, storage);
    vk::Buffer& gb = buffer(nightGrids[ordinal], ex.slot, sizeof(render::ClusterGrid), storage);
    std::memcpy(hb.mapped, &header, sizeof(header));
    std::memcpy(lb.mapped, nightLights.data(), lightBytes);
    const VkExtent2D e = si.area;
    const render::ClusterGrid grid = render::clusterGrid(
        header, render::clusterProjection(k.focal[0], k.focal[1], k.centre[0], k.centre[1], float(e.width), float(e.height)));
    std::memcpy(gb.mapped, &grid, sizeof(grid));
    // The cluster build (F1), after the earlier light pass read the cells, before this one does.
    const VkCommandBuffer command = ex.command;
    computeBarrier(command, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    clusters.record(command, hb.buffer, lb.buffer, lightBytes, gb.buffer, nightCells.buffer, nightIndices.buffer, nightStats.buffer);
    computeBarrier(command, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    // The local luminance: (sum of log luminance, count) per 8 x 8 block of the scene area.
    const VkExtent2D blocks{(e.width + 7) / 8, (e.height + 7) / 8};
    ex.ensureImage(nightLocal, blocks, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   "gsn night local luminance");
    vk::toStorage(ex.b, nightLocal, true);
    ex.b.flush(command);
    vk::bind(command, nightLumPass);
    vk::Descriptors()
        .sampled(0, colourView, samplers[0])
        .sampled(1, depthOnly, samplers[0])
        .storage(2, nightLocal.view)
        .push(command, nightLumPass);
    struct {
        float size[2], distanceA, distanceB, maxDistance;
    } lk{{float(e.width), float(e.height)}, k.distanceA, k.distanceB, k.maxDistance};
    vk::pushConstants(command, nightLumPass, lk);
    vkCmdDispatch(command, blocks.width, blocks.height, 1);
    vk::toSampled(ex.b, nightLocal, vk::kCompute);
    ex.b.flush(command);
    k.night = night;
    out.header = &hb;
    out.lights = &lb;
    out.lightBytes = lightBytes;
    tally("night lights", header.count);
    return true;
}

// After the list's commands: the last render pass ended, the displayed buffer's bookkeeping.
void GsnScene::Impl::executeFinish(VkCommandBuffer command, uint32_t /*slot*/) {
    ex.command = command;
    vk::Barriers& b = ex.b;
    auto& end = ex.end;
    end();
    mark(command, "draws");
    // The displayed buffer, for recordMain (a list cut for a GS -> host read displays nothing).
    if (!frame.displayed) return;
    ++displayedFrames;
    const uint32_t shownByGame = frame.display.target2 ? frame.display.target2 : frame.display.target1;
    const uint32_t shownBefore = displayTarget;
    displayTarget = shownByGame;
    // Interpolating: the buffer this list draws into, not the one it shows. The game draws into a back buffer while
    // the display shows the front one, which the list before drew: shown now, the back buffer at modelT is the
    // state between the frame on screen (0) and this one (1). (The last target drawn of the shown one's size and
    // format; a single buffer is the shown one.)
    if (interpolate) {
        bool backBuffer = false;
        const gsn::Target* shownTarget = nullptr;
        for (const gsn::Target& t : frame.targets)
            if (t.id == displayTarget) shownTarget = &t;
        for (size_t at = frame.commands.size(); shownTarget && at-- > 0;) {
            const auto& cmd = frame.commands[at];
            if (cmd.kind != gsn::FrameList::Command::Kind::Draw) continue;
            const uint32_t id = frame.draws[cmd.index].target;
            const gsn::Target* drawn = nullptr;
            for (const gsn::Target& t : frame.targets)
                if (t.id == id) drawn = &t;
            if (drawn && !drawn->depth && drawn->bw == shownTarget->bw && drawn->height == shownTarget->height &&
                (drawn->psm == shownTarget->psm || (drawn->psm <= 1 && shownTarget->psm <= 1))) {   // (CT32 and CT24: one layout)
                displayTarget = id;
                backBuffer = true;
                break;
            }
        }
        // Nothing drawn and no flip: a game frame taking several VBlanks (the race start: the first race frame drawn
        // into the back buffer, then three empty lists before the flip) goes on showing the buffer shown before.
        // Back to the display registers' buffer there, the picture went race, black, black, black, race.
        if (!backBuffer && shownByGame == gameShown && targets.count(shownBefore)) displayTarget = shownBefore;
    }
    gameShown = shownByGame;
    auto shown = targets.find(displayTarget);
    if (shown != targets.end()) {
        const gfx::DisplayCircuit& c = frame.display.record.circuit[frame.display.target2 ? 1 : 0];
        const uint32_t dbx = c.x, dby = c.y;
        uint32_t w, h;
        displayedSize(frame.display, w, h);
        displayedGs[0] = float(w ? w : 640), displayedGs[1] = float(h ? h : 448);
        const float tw = static_cast<float>(shown->second.image.extent.width) / shown->second.scaleX,
                    th = static_cast<float>(shown->second.image.extent.height) / shown->second.scaleY;
        displayRect[0] = dbx / tw;
        displayRect[1] = dby / th;
        displayRect[2] = (w ? w : 640) / tw;
        displayRect[3] = (h ? h : 448) / th;
        vk::toSampled(b, shown->second.image, vk::kFragment);
        // Field mode (the logo, the menus): the game draws each field into its own buffer, offset for a TV that
        // shows the two half a line apart, and the offsets differ from screen to screen. Shown one at a time over
        // the whole height the picture bobs; shown blended with the other field (the buffer shown before, whole at
        // the end of a list) it holds still. GT4_FIELD_BLEND=0: each field alone.
        static const bool blendFields = [] { const char* e = std::getenv("GT4_FIELD_BLEND"); return !e || std::strcmp(e, "0"); }();
        const bool fields = frame.display.record.interlaced && frame.display.record.fieldMode;
        fieldBlend = 0.0f;
        auto other = targets.find(lastFieldTarget);
        if (blendFields && fields && lastFieldTarget && lastFieldTarget != displayTarget && other != targets.end() &&
            other->second.image.extent.width == shown->second.image.extent.width &&
            other->second.image.extent.height == shown->second.image.extent.height) {
            std::copy(displayRect, displayRect + 4, otherFieldRect);
            otherFieldRect[1] = lastFieldY / th;
            fieldBlend = 0.5f;
            fieldLine = 1.0f / th;
            blendTarget = lastFieldTarget;
            vk::toSampled(b, other->second.image, vk::kFragment);
        }
        lastFieldTarget = fields ? displayTarget : 0;
        lastFieldY = dby;
        b.flush(command);
    }
}

void GsnScene::Impl::writeDump() {
    if (dump.images.empty()) return;
    vkDeviceWaitIdle(device.device);
    for (size_t k = 0; k < dump.images.size(); ++k) {
        auto& [id, buffer] = dump.images[k];
        const VkExtent2D e = dump.extents[k];
        char name[96];
        if (dump.depth[k]) {   // "GSZ1", width, height, then the floats (Z over 2^32, as gsn.vert writes it)
            std::snprintf(name, sizeof name, "gsn_%llu_%u_bp%04x_z.f32", static_cast<unsigned long long>(dump.vblank), id, dump.bps[k]);
            if (FILE* f = std::fopen(name, "wb")) {
                const uint32_t header[3] = {0x315A5347u, e.width, e.height};
                std::fwrite(header, sizeof header, 1, f);
                std::fwrite(buffer.mapped, 4, size_t(e.width) * e.height, f);
                std::fclose(f);
            }
            vk::destroy(device, buffer);
            continue;
        }
        std::snprintf(name, sizeof name, "gsn_%llu_%u_bp%04x.ppm", static_cast<unsigned long long>(dump.vblank), id, dump.bps[k]);
        if (FILE* f = std::fopen(name, "wb")) {
            std::fprintf(f, "P6\n%u %u\n255\n", e.width, e.height);
            const uint16_t* half = static_cast<const uint16_t*>(buffer.mapped);
            std::vector<uint8_t> row(size_t(e.width) * 3);
            auto toFloat = [](uint16_t h) {
                const uint32_t sign = (h >> 15) & 1, exponent = (h >> 10) & 31, mantissa = h & 1023;
                float v = exponent ? std::ldexp(static_cast<float>(mantissa | 1024), static_cast<int>(exponent) - 25)
                                   : std::ldexp(static_cast<float>(mantissa), -24);
                return sign ? -v : v;
            };
            for (uint32_t y = 0; y < e.height; ++y) {
                for (uint32_t x = 0; x < e.width; ++x)
                    for (int c = 0; c < 3; ++c)
                        row[x * 3 + c] = static_cast<uint8_t>(std::clamp(toFloat(half[(size_t(y) * e.width + x) * 4 + c]) * 255.0f + 0.5f, 0.0f, 255.0f));
                std::fwrite(row.data(), 1, row.size(), f);
            }
            std::fclose(f);
        }
        // The alpha as the GS keeps it (alpha x 128), as a grey image beside it.
        std::snprintf(name, sizeof name, "gsn_%llu_%u_bp%04x_alpha.pgm", static_cast<unsigned long long>(dump.vblank), id, dump.bps[k]);
        if (FILE* f = std::fopen(name, "wb")) {
            std::fprintf(f, "P5\n%u %u\n255\n", e.width, e.height);
            const uint16_t* half = static_cast<const uint16_t*>(buffer.mapped);
            std::vector<uint8_t> row(e.width);
            for (uint32_t y = 0; y < e.height; ++y) {
                for (uint32_t x = 0; x < e.width; ++x) {
                    const uint16_t h = half[(size_t(y) * e.width + x) * 4 + 3];
                    const uint32_t exponent = (h >> 10) & 31, mantissa = h & 1023;
                    const float v = exponent ? std::ldexp(static_cast<float>(mantissa | 1024), static_cast<int>(exponent) - 25)
                                             : std::ldexp(static_cast<float>(mantissa), -24);
                    row[x] = static_cast<uint8_t>(std::clamp(v * 128.0f + 0.5f, 0.0f, 255.0f));
                }
                std::fwrite(row.data(), 1, row.size(), f);
            }
            std::fclose(f);
        }
        vk::destroy(device, buffer);
    }
    dump.images.clear();
    dump.extents.clear();
    dump.bps.clear();
    dump.depth.clear();
}

GsnScene::GsnScene(const vk::Device& device, VkCommandPool pool) : impl(std::make_unique<Impl>(device, pool)) {}
GsnScene::~GsnScene() = default;

// The oldest waiting frames skipped (their draws dropped, their textures carried into the next list) while more than
// `keepComplete` complete frames wait, as long as the oldest is a complete frame that is safe to skip:
// - every target it writes is written again by a later waiting list (a frame buffer, any target at a shown buffer's
//   address in any format, and a Z buffer count as written again: drawn every other frame and only shown, or cleared
//   each frame);
// - no later waiting list reads one of them (a texture, a reinterpretation or its palette, an effect's or a relight's
//   source) before writing it itself: it would read the skipped frame's pixels. (GT4's race and replay frames,
//   measured: none does; each frame draws what it samples.)
// A target no later waiting list writes is no obstacle either when it is the frame's own scratch: the frame does not
// read it before writing it, no later waiting list reads it, and no frame so far was seen reading it before writing it
// (`carried`: content kept between frames, which a frame not queued yet may read, as the 256 x 256 environment map
// does). A race's small 64-pixel-wide targets, drawn by a frame when a car needs them: most of the frames the first
// rule kept (a replay, every other frame offered: 1026 skipped by it, 1191 with this). GT4_SKIP_STRICT=1: not.
// Returns how many were skipped. (A GS -> host read waiting: keepComplete 0, so its list waits only for itself.)
void GsnScene::Impl::noteCarried(const gsn::FrameList& l) {
    std::vector<uint32_t> written;
    auto read = [&](uint32_t id) {
        if (id && std::find(written.begin(), written.end(), id) == written.end()) carried.insert(id);
    };
    for (const auto& c : l.commands) {
        if ((c.kind == gsn::FrameList::Command::Kind::Draw || c.kind == gsn::FrameList::Command::Kind::Clear) && c.index < l.draws.size()) {
            const gsn::Draw& d = l.draws[c.index];
            if (d.texture.kind == gsn::TextureSource::Kind::Target) read(d.texture.id);
            if (d.texture.kind == gsn::TextureSource::Kind::Reinterpreted && d.texture.id < l.reinterpretations.size()) {
                read(l.reinterpretations[d.texture.id].target);
                read(l.reinterpretations[d.texture.id].clutTarget);
            }
            written.push_back(d.target);
            if (d.depth) written.push_back(d.depth);
        } else if (c.kind == gsn::FrameList::Command::Kind::Effect && c.index < l.effects.size()) {
            read(l.effects[c.index].source);
            written.push_back(l.effects[c.index].destination);
        } else if (c.kind == gsn::FrameList::Command::Kind::Light && c.index < l.lights.size()) {
            read(l.lights[c.index].target);
            read(l.lights[c.index].depth);
        }
    }
}

size_t GsnScene::Impl::skipOlderFrames(size_t keepComplete) {
    auto writes = [](const gsn::FrameList& l, std::vector<uint32_t>& out) {
        for (const auto& c : l.commands) {
            if (c.kind == gsn::FrameList::Command::Kind::Draw || c.kind == gsn::FrameList::Command::Kind::Clear) {
                if (c.index < l.draws.size()) {
                    out.push_back(l.draws[c.index].target);
                    if (l.draws[c.index].depth) out.push_back(l.draws[c.index].depth);
                }
            } else if (c.kind == gsn::FrameList::Command::Kind::Effect) {
                if (c.index < l.effects.size()) out.push_back(l.effects[c.index].destination);
            } else if (c.kind == gsn::FrameList::Command::Kind::Light) {
                if (c.index < l.lights.size()) out.push_back(l.lights[c.index].target);
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };
    // Whether `l` reads one of `targets` before writing it.
    auto readsFirst = [](const gsn::FrameList& l, const std::vector<uint32_t>& targets) {
        std::vector<uint32_t> written;
        auto stale = [&](uint32_t id) {
            return id && std::binary_search(targets.begin(), targets.end(), id) &&
                   std::find(written.begin(), written.end(), id) == written.end();
        };
        for (const auto& c : l.commands) {
            if (c.kind == gsn::FrameList::Command::Kind::Draw || c.kind == gsn::FrameList::Command::Kind::Clear) {
                if (c.index >= l.draws.size()) continue;
                const gsn::Draw& d = l.draws[c.index];
                if (d.texture.kind == gsn::TextureSource::Kind::Target && stale(d.texture.id)) return true;
                if (d.texture.kind == gsn::TextureSource::Kind::Reinterpreted && d.texture.id < l.reinterpretations.size()) {
                    const gsn::Reinterpretation& r = l.reinterpretations[d.texture.id];
                    if (stale(r.target) || stale(r.clutTarget)) return true;
                }
                written.push_back(d.target);
                if (d.depth) written.push_back(d.depth);
            } else if (c.kind == gsn::FrameList::Command::Kind::Effect && c.index < l.effects.size()) {
                if (stale(l.effects[c.index].source)) return true;
                written.push_back(l.effects[c.index].destination);
            } else if (c.kind == gsn::FrameList::Command::Kind::Light && c.index < l.lights.size()) {
                if (stale(l.lights[c.index].target) || stale(l.lights[c.index].depth)) return true;
            }
        }
        return false;
    };
    // Whether `l` reads `id` at all (as readsFirst, whatever it wrote before).
    auto readsAny = [](const gsn::FrameList& l, uint32_t id) {
        for (const auto& c : l.commands) {
            if ((c.kind == gsn::FrameList::Command::Kind::Draw || c.kind == gsn::FrameList::Command::Kind::Clear) && c.index < l.draws.size()) {
                const gsn::Draw& d = l.draws[c.index];
                if (d.texture.kind == gsn::TextureSource::Kind::Target && d.texture.id == id) return true;
                if (d.texture.kind == gsn::TextureSource::Kind::Reinterpreted && d.texture.id < l.reinterpretations.size() &&
                    (l.reinterpretations[d.texture.id].target == id || l.reinterpretations[d.texture.id].clutTarget == id))
                    return true;
            } else if (c.kind == gsn::FrameList::Command::Kind::Effect && c.index < l.effects.size()) {
                if (l.effects[c.index].source == id) return true;
            } else if (c.kind == gsn::FrameList::Command::Kind::Light && c.index < l.lights.size()) {
                if (l.lights[c.index].target == id || l.lights[c.index].depth == id) return true;
            }
        }
        return false;
    };
    static const bool strict = [] { const char* e = std::getenv("GT4_SKIP_STRICT"); return e && *e && *e != '0'; }();
    size_t complete = 0, skipped = 0;
    for (const auto& list : pending) complete += list.displayed ? 1 : 0;
    // The oldest frame: its lists cut at asynchronous reads (FrameList::readbacks, not displayed) and the displayed one
    // that ends it, skipped together (the flare pass reads every frame: a frame is two lists then).
    while (complete > keepComplete && pending.size() > 1) {
        size_t last = 0;
        while (last < pending.size() && !pending[last].displayed) ++last;
        if (last + 1 >= pending.size()) break;
        std::vector<uint32_t> first, later;
        for (size_t k = 0; k <= last; ++k) writes(pending[k], first);
        for (size_t i = last + 1; i < pending.size(); ++i) writes(pending[i], later);
        std::vector<uint32_t> shownBp;
        for (const auto& list : pending)
            for (const gsn::Target& t : list.targets)
                if (t.id && (t.id == list.display.target1 || t.id == list.display.target2)) shownBp.push_back(t.bp);
        for (size_t k = 0; k <= last; ++k)
            for (const gsn::Target& t : pending[k].targets)
                if (t.depth || std::find(shownBp.begin(), shownBp.end(), t.bp) != shownBp.end()) later.push_back(t.id);
        std::sort(later.begin(), later.end());
        bool again = true;
        for (uint32_t t : first) {
            if (!again) break;
            if (std::binary_search(later.begin(), later.end(), t)) continue;
            // the frame's own scratch: not read before its write here, never seen carried, read by no later list
            bool scratch = !strict && !carried.count(t);
            std::vector<uint32_t> written;
            for (size_t k = 0; k <= last && scratch; ++k) {
                if (!std::binary_search(written.begin(), written.end(), t) && readsFirst(pending[k], std::vector<uint32_t>{t})) scratch = false;
                writes(pending[k], written);
            }
            for (size_t i = last + 1; i < pending.size() && scratch; ++i) scratch = !readsAny(pending[i], t);
            again = scratch;
        }
        if (!again) break;
        bool read = false;
        for (size_t i = last + 1; i < pending.size() && !read; ++i) read = readsFirst(pending[i], first);
        if (read) break;
        for (size_t k = last + 1; k-- > 0;) gsn::carryOver(pending[k], pending[last + 1]);   // (the newest first: order kept)
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(last + 1));
        --complete;
        ++skipped;
        ++framesSkipped;
    }
    return skipped;
}

void GsnScene::addFrames(std::vector<gsn::FrameList>& lists) {
    Impl& s = *impl;
    for (const auto& list : lists)
        if (!list.readbacks.empty() && s.readbacksWaitingSince == std::chrono::steady_clock::time_point{})
            s.readbacksWaitingSince = std::chrono::steady_clock::now();
    for (const auto& list : lists)
        if (list.displayed) s.arrival = std::chrono::steady_clock::now();
    for (auto& list : lists) {
        s.noteCarried(list);
        s.pending.push_back(std::move(list));
    }
    lists.clear();
    // GT4_READ_SKIP_TEST=N (testing, a lockstep replay): the newest N complete frames are held back until the next GS ->
    // host read, which then finds them waiting ahead of it as a host behind the game would (GsnScene::read). Two runs,
    // GT4_READ_SKIP=0 and not, with GT4_READ_HASH=1, show whether skipping them changes what the reads return.
    static const size_t readTest = [] { const char* e = std::getenv("GT4_READ_SKIP_TEST"); return e ? size_t(std::atoi(e)) : size_t(0); }();
    if (readTest) {
        for (auto& list : s.pending) s.readHeld.push_back(std::move(list));
        s.pending.clear();
        size_t complete = 0;
        for (const auto& list : s.readHeld) complete += list.displayed ? 1 : 0;
        size_t release = 0;   // the lists released (drawn at the next present): the oldest, until N complete ones are left
        for (; release < s.readHeld.size() && complete > readTest; ++release)
            if (s.readHeld[release].displayed) --complete;
        for (size_t k = 0; k < release; ++k) s.pending.push_back(std::move(s.readHeld[k]));
        s.readHeld.erase(s.readHeld.begin(), s.readHeld.begin() + static_cast<std::ptrdiff_t>(release));
    }
    // Behind: every waiting list is drawn before the next present, so a GPU slower than the game for a frame draws
    // two the next time, then three, up to the 64 below (a picture a second while the game runs at 60: the Odin's
    // replay cameras, then F7's stop). Three complete frames waiting is behind (two happen at 60 Hz, a loop's jitter):
    // for a second from then, while more than one waits, the oldest is skipped (its draws dropped, its textures kept)
    // when it is a complete frame and every target it writes is written again by a later one: never a read's list,
    // nor one that draws something drawn once (car pictures, a menu's backdrop). GT4_FRAME_SKIP=0: every list drawn.
    // (With 18 ms a frame on a desktop, GT4_GPU_THROTTLE_MS=18: 0.8 pictures a second before, 42 with it.)
    static const bool frameSkip = [] { const char* e = std::getenv("GT4_FRAME_SKIP"); return !(e && *e == '0'); }();
    // GT4_FRAME_SKIP_TEST=1 (testing, a lockstep replay): every other frame held back and skipped as above, so a grid
    // against a plain replay shows what a skipped frame costs on screen (the frames drawn should match exactly).
    static const bool skipTest = [] { const char* e = std::getenv("GT4_FRAME_SKIP_TEST"); return e && *e && *e != '0'; }();
    if (skipTest) {
        if (s.held) {
            s.pending.insert(s.pending.begin(), std::move(s.heldBack));
            s.held = false;
            s.skipOlderFrames(1);
        } else if (s.pending.size() == 1 && s.pending[0].displayed && (s.skipTestCount++ & 1)) {
            s.heldBack = std::move(s.pending[0]);
            s.pending.clear();
            s.held = true;
        }
    }
    size_t waiting = 0;
    for (const auto& list : s.pending) waiting += list.displayed ? 1 : 0;
    const auto now = std::chrono::steady_clock::now();
    if (frameSkip && waiting >= 3) s.skipUntil = now + std::chrono::seconds(1);
    if (frameSkip && now < s.skipUntil) s.skipOlderFrames(1);
    // Not drawn for long (a minimised window draws nothing): the oldest lists' draws are dropped, their
    // texture uploads kept.
    constexpr size_t kMaxPending = 64;
    while (s.pending.size() > kMaxPending) {
        gsn::carryOver(s.pending[0], s.pending[1]);
        s.pending.erase(s.pending.begin());
    }
    for (const auto& list : s.pending) s.any = s.any || list.displayed;
}

bool GsnScene::hasFrame() const { return impl->any; }
std::string GsnScene::textureHeapSummary() const { return impl->textureHeap.summary(); }

GsnScene::Counters GsnScene::counters() const {
    Counters c = impl->counters;
    c.framesSkipped = impl->framesSkipped;
    return c;
}
void GsnScene::setScale(uint32_t scale) { impl->scaleSetting = scale; }
void GsnScene::setAnisotropy(int anisotropy) { impl->makeMipSamplers(static_cast<float>(anisotropy)); }

void GsnScene::setTemporal(bool on) { impl->temporal = on; }
void GsnScene::setParity(bool on) { impl->parity = on; }
void GsnScene::setHybrid(bool on) { impl->hybrid = on; }
void GsnScene::setFieldMode(bool clean) { impl->fieldClean = clean; }
// GT4_GPU_PROFILE=1's labels from the render graph's pass timings of one frame (ticket B4 step 4; lane C keeps
// --profile-json's "gs_passes" under these labels for one release, so a comparison across the change holds them to the
// gate, then drops it for the graph's own pass names). A label's time is the sum of its passes', -1 when one of them was
// not timed (perf_report leaves it out then). Close to the marks' but not the same: a mark took the time since the mark
// before it, so "colour grading" held the draws before the grading, which now go to "draws".
std::vector<std::pair<std::string, double>> GsnScene::stagesOf(const std::vector<rg::PassTiming>& passes) {
    static const std::pair<const char*, const char*> kLabels[] = {
        {"gs: frame", "new targets"}, {"gs: new targets", "new targets"}, {"gs: list end", "new targets"},
        {"gs: texture uploads", "texture uploads"},
        {"gs: 3D batches", "draws"}, {"gs: draws", "draws"}, {"gs: draws end", "draws"}, {"gs: scene stage", "draws"},
        {"gs: frame again", "draws"},
        {"gs: colour grading", "colour grading"},
        {"scene: motion vectors", "motion vectors"},
        {"scene: lighting (occlusion)", "lighting"}, {"scene: lighting", "lighting"}, {"scene: lighting (copy back)", "lighting"},
        {"scene: reflections (trace)", "reflections"}, {"scene: reflections", "reflections"},
        {"scene: reflections (copy back)", "reflections"},
        {"scene: fog", "fog"}, {"scene: fog (copy back)", "fog"},
        {"scene: temporal resolve", "temporal resolve"},
        {"scene: motion blur", "motion blur"}, {"scene: motion blur (copy back)", "motion blur"},
        {"scene: focus", "depth of field"}, {"scene: focus (copy back)", "depth of field"},
    };
    std::vector<std::pair<std::string, double>> out;
    for (const rg::PassTiming& p : passes) {
        const char* label = nullptr;
        for (const auto& [name, mapped] : kLabels)
            if (p.name == name) {
                label = mapped;
                break;
            }
        if (!label) continue;
        const double ms = p.gpuMicroseconds < 0.0 ? -1.0 : p.gpuMicroseconds / 1000.0;
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.first == label; });
        if (it == out.end()) out.emplace_back(label, ms);
        else if (it->second >= 0.0) it->second = ms < 0.0 ? -1.0 : it->second + ms;
    }
    return out;
}
void GsnScene::setEffects(const SceneEffects& effects) { impl->effects = effects; }
bool GsnScene::antiAliasesItself() const { return impl->temporal; }

void GsnScene::setLighting(float occlusion, uint32_t directions, float grade, float aspect) {
    Impl::Lighting& l = impl->lighting;
    l.occlusion = occlusion;
    l.directions = std::max(directions, 1u);
    l.grade = grade;
    l.tanY = 0.433f;   // GT4's monitor presets: about 23.4 degrees above and below the centre (docs/WIDESCREEN.md)
    l.tanX = l.tanY * (aspect > 0.0f ? aspect : 4.0f / 3.0f);
}

void GsnScene::setNightLighting(float strength) { impl->night = std::max(strength, 0.0f); }
void GsnScene::setDisplaySeam(bool on) {
    static const bool fromEnv = [] { const char* e = std::getenv("GT4_B16C_SEAM"); return e && *e == '1'; }();
    impl->seamOn = on || fromEnv;
}
void GsnScene::setSkippedBrackets(bool flare, bool glare) {
    impl->skippedBrackets = static_cast<uint8_t>((flare ? 1u << gsn::kBracketFlare : 0u) | (glare ? 1u << gsn::kBracketGlare : 0u));
}

void GsnScene::prepare(const SceneFormats& formats) {
    Impl& s = *impl;
    vkDeviceWaitIdle(s.device.device);
    vk::destroy(s.device, s.display);
    vk::destroy(s.device, s.displaySeam);
    vk::destroy(s.device, s.stencilPrep);
    s.formats = formats;
    vk::GraphicsDesc desc;
    desc.vertex = shaders::fullscreen_vert();
    desc.fragment = shaders::gsn_display_frag();
    desc.colors = {formats.color, formats.velocity};
    desc.depth = formats.depth;
    desc.samples = formats.samples;
    desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    desc.pushSize = 48;
    desc.name = "gsn display";
    s.display = vk::createGraphics(s.device, desc);
    vk::GraphicsDesc stencil;
    stencil.vertex = shaders::fullscreen_vert();
    stencil.fragment = shaders::gsn_stencil_frag();
    stencil.depth = kDepth;
    stencil.stencil = kDepth;
    stencil.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    stencil.name = "gsn stencil";
    stencil.dynamics = {VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    s.stencilPrep = vk::createGraphics(s.device, stencil);
    if (!s.aoPass.valid())
        s.aoPass = vk::createCompute(s.device, shaders::gsn_ao_comp(),
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, 40, "gsn occlusion");
    if (!s.lightPass.valid())
        s.lightPass = vk::createCompute(s.device, shaders::gsn_light_comp(),
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
                                        96, "gsn light");
    // lighting.night (PLAN.md F2): the local luminance pass, the cluster build and its buffers. The light pass reads
    // these bindings whether or not the night is on (the shader skips them), so they always exist.
    if (!s.nightLumPass.valid()) {
        s.nightLumPass = vk::createCompute(s.device, shaders::gsn_night_lum_comp(),
                                           {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                                           20, "gsn night luminance");
        s.clusters.create(s.device, shaders::lights_cluster_comp());
        const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        s.nightCells = vk::createBuffer(s.device, sizeof(render::ClusterCell) * render::kClusterCount, storage, false);
        s.nightIndices = vk::createBuffer(s.device, sizeof(uint32_t) * render::kClusterCount * render::kMaxLightsPerCluster,
                                          storage, false);
        s.nightStats = vk::createBuffer(s.device, sizeof(render::ClusterStats), storage, false);
    }
    if (!s.velocityPass.valid()) {
        vk::GraphicsDesc v;
        v.vertex = shaders::gsn_velocity_vert();
        v.fragment = shaders::gsn_velocity_frag();
        v.colors = {kVelocity};
        v.depth = kDepth;
        v.stencil = kDepth;
        v.depthTest = true;
        v.depthWrite = false;
        v.depthCompare = VK_COMPARE_OP_EQUAL;
        v.vertices.stride = sizeof(gsn::Vertex);
        v.vertices.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(gsn::Vertex, x)},
                                 {1, 0, VK_FORMAT_R32_UINT, offsetof(gsn::Vertex, z)},
                                 {7, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(gsn::Vertex, mx)},
                                 {8, 0, VK_FORMAT_R32_SFLOAT, offsetof(gsn::Vertex, w)}};
        v.pushSize = sizeof(DrawConstants);
        v.dynamics = {VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP};
        v.name = "gsn motion vectors";
        s.velocityPass = vk::createGraphics(s.device, v);
        // N3: the skip mask, the same positions into an R8 image, each draw's own depth test, no depth written
        v.fragment = shaders::gsn_mask_frag();
        v.colors = {VK_FORMAT_R8_UNORM};
        v.name = "gsn skip mask";
        s.maskPass = vk::createGraphics(s.device, v);
    }
    const auto stage = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    if (!s.ssrPass.valid()) s.ssrPass = vk::createCompute(s.device, shaders::gsn_ssr_comp(), stage, 48, "gsn reflections");
    if (!s.ssrComposite.valid())
        s.ssrComposite = vk::createCompute(s.device, shaders::gsn_ssr_composite_comp(), stage, 48, "gsn reflections composite");
    if (!s.fogPass.valid()) s.fogPass = vk::createCompute(s.device, shaders::gsn_fog_comp(), stage, 48, "gsn fog");
    if (!s.gradePass.valid())
        s.gradePass = vk::createCompute(s.device, shaders::gsn_grade_comp(),
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                        36, "gsn colour grading");
    if (!s.blurPass.valid()) s.blurPass = vk::createCompute(s.device, shaders::gsn_motionblur_comp(), stage, 48, "gsn motion blur");
    if (!s.dofPass.valid()) s.dofPass = vk::createCompute(s.device, shaders::gsn_dof_comp(), stage, 48, "gsn depth of field");
    if (!s.taaPass.valid())
        s.taaPass = vk::createCompute(s.device, shaders::gsn_taa_comp(),
                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                                      40, "gsn temporal anti-aliasing");
    if (!s.models.valid())
        s.models = vk::createCompute(s.device, shaders::gsn_models_comp(),
                                     {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, 16,
                                     "gsn 3D batches");
    if (!s.reinterpret.valid())
        s.reinterpret = vk::createCompute(s.device, shaders::gsn_reinterpret_comp(),
                                          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                          96, "gsn reinterpret");
    if (!s.blank.valid()) {
        vk::ImageDesc b;
        b.format = VK_FORMAT_R8G8B8A8_UNORM;
        b.extent = {1, 1};
        b.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        b.name = "gsn blank";
        s.blank = vk::createImage(s.device, b);
        vk::Uploader up(s.device, s.pool);
        vk::Barriers barriers;
        barriers.image(s.blank, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
        barriers.flush(up.command());
        const uint32_t white = 0xFFFFFFFFu;
        up.copyToImage(s.blank, &white, 4, 0, 0, {1, 1});
        vk::toSampled(barriers, s.blank, vk::kFragment);
        barriers.flush(up.command());
        up.submit();
    }
}

void GsnScene::Impl::executePending(VkCommandBuffer command, uint32_t slot, VkExtent2D extent, bool profiled) {
    beginPending(command, slot, extent, profiled);
    for (listIndex = 0; listIndex < pending.size(); ++listIndex) {
        pendingTargets(command, slot);
        pendingUploads(command, slot);
        pendingDraws(command, slot);
        pendingListEnd(command);
    }
    endPending(command);
}

// The pending lists' work before the first one: the scale, the dump of an earlier submission, the shown list.
void GsnScene::Impl::beginPending(VkCommandBuffer command, uint32_t slot, VkExtent2D extent, bool profiled, bool scaled) {
    sweepGraveyard();
    markSlot = profiled && profile.on ? static_cast<int>(slot % kSlots) : -1;
    if (markSlot >= 0) profileFrame(command, slot);
    if (!scaled) decideScale(extent);
    writeDump();   // a dump recorded in an earlier submission is complete now
    // Only the list shown now moves with the display's clock (interpolation); every other one is drawn as the game drew
    // it: what a later list samples or the game reads back, or keeps (a car picture drawn once), is the frame's own.
    pendingShown = pending.size();
    for (size_t i = 0; i < pending.size(); ++i)
        if (pending[i].displayed) pendingShown = i;
    vk::beginLabel(command, "gsn frame");
}

// A CPU decision for the whole submission (the render graph's build makes it before any pass is built: the scene stage's
// sizes depend on it). The GPU is waited for when the targets go.
void GsnScene::Impl::decideScale(VkExtent2D extent) {
    // The nearest whole scale to the render extent per axis, from what the display shows (640 x 448, or 224
    // lines per field: GT4's race draws each field whole, so its lines get the full height).
    const gsn::FrameList* shown = nullptr;
    for (const auto& list : pending)
        if (list.displayed) shown = &list;
    // GT4_GSN_SCALE=N (comparisons with the GS back end): that scale on both axes.
    static const uint32_t forced = std::getenv("GT4_GSN_SCALE") ? static_cast<uint32_t>(std::strtoul(std::getenv("GT4_GSN_SCALE"), nullptr, 10)) : 0;
    if (forced) scaleSetting = forced;
    if (shown || scaleSetting) {
        uint32_t shownWidth = 640, shownHeight = 448;
        if (shown) displayedSize(shown->display, shownWidth, shownHeight);
        const uint32_t wantedX = scaleSetting ? scaleSetting
                                              : std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(extent.width / double(shownWidth))), 1, 8);
        const uint32_t wantedY = scaleSetting ? scaleSetting
                                              : std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(extent.height / double(shownHeight))), 1, 8);
        if (wantedX != sx || wantedY != sy) {
            GT4_STAGE_SCOPE("render: new scale (targets dropped)");
            vkDeviceWaitIdle(device.device);
            for (auto& [id, t] : targets) vk::destroy(device, t.image);
            targets.clear();
            sx = wantedX;
            sy = wantedY;
        }
    }
}

// List `listIndex`: made current (`frame`), its targets made or cleared.
void GsnScene::Impl::pendingTargets(VkCommandBuffer command, uint32_t slot) {
    std::swap(frame, pending[listIndex]);
    ++listSerial;
    // (a new list: from the previous frame's state, where the one before ended)
    modelTPrevious = 0.0f;
    modelT = interpolate && listIndex == pendingShown ? sinceArrival() : 1.0f;
    // GT4_GPU_THROTTLE_MS=N (testing): N ms more for each complete frame drawn, as a GPU that takes that long for
    // one (the Odin's in a replay's blurred far camera) costs the host for every frame it draws.
    static const int throttle = [] { const char* e = std::getenv("GT4_GPU_THROTTLE_MS"); return e ? std::atoi(e) : 0; }();
    if (throttle > 0 && frame.displayed) std::this_thread::sleep_for(std::chrono::milliseconds(throttle));
    {
        GT4_STAGE_SCOPE("render: the lists' targets");
        std::vector<uint32_t> created;
        for (const gsn::Target& t : frame.targets) {
            const bool existed = targets.count(t.id) && targets[t.id].image.valid();
            const VkImage before = existed ? targets[t.id].image.image : VK_NULL_HANDLE;
            ensureTarget(t);
            if (!existed || targets[t.id].image.image != before) created.push_back(t.id);
        }
        for (uint32_t id : created) clearNew(command, targets[id]);
        counters.targets += created.size();
    }
    mark(command, "new targets");
}

// The current list's textures uploaded.
void GsnScene::Impl::pendingUploads(VkCommandBuffer command, uint32_t slot) {
    {
        GT4_STAGE_SCOPE("render: texture uploads");
        uploadTextures(command, slot);
    }
    mark(command, "texture uploads");
    tally("lists");
}

// The current list's draws (its 3D batches, its commands, the scene stage at its Light commands).
void GsnScene::Impl::pendingDraws(VkCommandBuffer command, uint32_t slot) {
    GT4_STAGE_SCOPE("render: the lists' draws");
    execute(command, slot);
}

// The current list's end: its asynchronous reads copied, its textures retired, a dump made.
void GsnScene::Impl::pendingListEnd(VkCommandBuffer command) {
    // The game's asynchronous reads at this list's end (it was cut there): the rectangle copied now, answered when
    // the GPU is done (a target gone: answered with zeros).
    for (const gsn::FrameList::Readback& rb : frame.readbacks) {
        Copied c;
        if (!copyRect(command, rb.request, c)) c = Copied();
        c.readback = rb;
        readbacksRecorded.push_back(std::move(c));
    }
    retireTextures();
    // GT4_GSN_DUMP=VBLANK: the first frame drawn at or after that VBlank; GT4_GSN_DUMP_EVERY=N: then again every N VBlanks;
    // GT4_GSN_DUMP_DEPTH=1: the Z buffers too, as floats (B7's Z comparison: render.scene = parity against original).
    static const long dumpAt = std::getenv("GT4_GSN_DUMP") ? std::strtol(std::getenv("GT4_GSN_DUMP"), nullptr, 10) : -1;
    static const long dumpEvery = std::getenv("GT4_GSN_DUMP_EVERY") ? std::strtol(std::getenv("GT4_GSN_DUMP_EVERY"), nullptr, 10) : 0;
    static const bool dumpDepth = [] { const char* e = std::getenv("GT4_GSN_DUMP_DEPTH"); return e && *e == '1'; }();
    // GT4_GSN_DUMP_DISPLAYED=1: only the displayed target (and its Z buffer with _DEPTH): a frame series stays small
    static const bool dumpDisplayed = [] { const char* e = std::getenv("GT4_GSN_DUMP_DISPLAYED"); return e && *e == '1'; }();
    static long dumpNext = dumpAt;   // (-1: done)
    // (not while an earlier dump waits to be written: its copies may be in this submission still)
    if (frame.displayed && dump.images.empty() && (frame.dump || (dumpNext >= 0 && static_cast<long>(frame.vblank) >= dumpNext))) {
        if (!frame.dump) dumpNext = dumpEvery > 0 ? static_cast<long>(frame.vblank) + dumpEvery : -1;
        dump.vblank = frame.vblank;
        vk::Barriers b;
        for (auto& [id, t] : targets) {
            if (!t.image.valid()) continue;
            if (dumpDisplayed && id != displayTarget && !(t.depth && dumpDepth)) continue;
            if (t.depth) {
                if (!dumpDepth) continue;
                vk::Buffer buffer = vk::createBuffer(device, VkDeviceSize(t.image.extent.width) * t.image.extent.height * 4,
                                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
                b.image(t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
                b.flush(command);
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
                copy.imageExtent = {t.image.extent.width, t.image.extent.height, 1};
                vkCmdCopyImageToBuffer(command, t.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &copy);
                dump.images.emplace_back(id, buffer);
                dump.extents.push_back(t.image.extent);
                dump.bps.push_back(0);
                dump.depth.push_back(1);
                continue;   // (vk::toDepth moves it back before its next use)
            }
            vk::Buffer buffer = vk::createBuffer(device, VkDeviceSize(t.image.extent.width) * t.image.extent.height * 8,
                                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
            b.image(t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
            b.flush(command);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {t.image.extent.width, t.image.extent.height, 1};
            vkCmdCopyImageToBuffer(command, t.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &copy);
            vk::toSampled(b, t.image, vk::kFragment);
            b.flush(command);
            dump.images.emplace_back(id, buffer);
            dump.extents.push_back(t.image.extent);
            dump.bps.push_back(0);
            dump.depth.push_back(0);
        }
        for (const gsn::Target& t : frame.targets)
            for (size_t k = 0; k < dump.images.size(); ++k)
                if (dump.images[k].first == t.id) dump.bps[k] = t.bp;
    }
}

// After the last list: histories retired, the lists recycled, the displayed target made sampled.
void GsnScene::Impl::endPending(VkCommandBuffer command) {
    // B8 step 1: the parity meshes' frame ends (unused ones retired, ranges no frame in flight reads freed)
    if (parityPlanned) {
        parityView.endFrame();
        parityPlanned = false;
    }
    // Histories of scenes not seen for 10 seconds go (a scene's target left behind with its screen).
    for (auto it = histories.begin(); it != histories.end();) {
        if (displayedFrames > it->second.used + 600) {
            for (vk::Image& i : it->second.image)
                if (i.valid()) retire(i);
            it = histories.erase(it);
        } else {
            ++it;
        }
    }
    // The executed lists go back to the front end for reuse (gsn::recycleFrameList); the last one stays in `frame`.
    for (gsn::FrameList& list : pending) gsn::recycleFrameList(std::move(list));
    pending.clear();
    readbacksWaitingSince = {};
    listIndex = 0;
    // The displayed buffer is sampled by recordMain whatever the lists left it as (a read's copy, a draw).
    if (auto it = targets.find(displayTarget); it != targets.end() && it->second.image.valid() && !it->second.depth) {
        vk::Barriers b;
        vk::toSampled(b, it->second.image, vk::kFragment);
        b.flush(command);
    }
    vk::endLabel(command);
}

void GsnScene::recordPrepass(VkCommandBuffer command, const SceneView& view) {
    GT4_CPU_SCOPE("render: native renderer's commands");
    Impl& s = *impl;
    s.lastExtent = view.renderExtent;
    // Each list is executed once (its blends would apply again)...
    if (!s.pending.empty()) {
        s.executePending(command, view.slot, view.renderExtent, true);
        auto& inFlight = s.readbacksInFlight[view.slot % kSlots];
        for (auto& c : s.readbacksRecorded) inFlight.push_back(std::move(c));
        s.readbacksRecorded.clear();
        return;
    }
    // ... except the last one, drawn again whole between two of the game's frames with its moving 3D a step further
    // (a list that blends onto what a target held before, a feedback effect, blends once more each time).
    if (!s.interpolate || !s.frame.displayed || s.frame.models.empty()) return;
    const float t = s.sinceArrival();
    if (t <= s.modelT + 1e-3f) return;   // nothing moved since
    s.modelTPrevious = s.modelT;
    s.modelT = t;
    vk::beginLabel(command, "gsn frame again");
    // (its own slot's timestamps: the marks of the last executePending point at that frame's slot, maybe still in flight)
    s.markSlot = s.profile.on ? static_cast<int>(view.slot % kSlots) : -1;
    if (s.markSlot >= 0) s.profileFrame(command, view.slot);
    s.execute(command, view.slot);
    if (auto it = s.targets.find(s.displayTarget); it != s.targets.end() && it->second.image.valid() && !it->second.depth) {
        vk::Barriers b;
        vk::toSampled(b, it->second.image, vk::kFragment);
        b.flush(command);
    }
    vk::endLabel(command);
}

void GsnScene::setInterpolation(bool on) { impl->interpolate = on; }
void GsnScene::setFixedClock(bool on) { impl->fixedClock = on; }

bool GsnScene::Impl::copyRect(VkCommandBuffer command, const gsn::ReadRequest& request, Copied& c) {
    auto it = targets.find(request.target);
    if (it == targets.end() || !it->second.image.valid() || it->second.depth) return false;
    vk::Image& image = it->second.image;
    const uint32_t scaleX = it->second.scaleX, scaleY = it->second.scaleY;
    const uint32_t x0 = std::min(request.x * scaleX, image.extent.width), y0 = std::min(request.y * scaleY, image.extent.height);
    const uint32_t w = std::min(request.width * scaleX, image.extent.width - x0), h = std::min(request.height * scaleY, image.extent.height - y0);
    vk::Buffer pixels = vk::createBuffer(device, std::max<VkDeviceSize>(VkDeviceSize(w) * h * 8, 8), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
    vk::Barriers b;
    if (w && h) {
        b.image(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.flush(command);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {static_cast<int32_t>(x0), static_cast<int32_t>(y0), 0};
        copy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(command, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pixels.buffer, 1, &copy);
        VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        host.srcStageMask = vk::kTransfer;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &host;
        vkCmdPipelineBarrier2(command, &dependency);
    }
    vk::toSampled(b, image, vk::kFragment);
    b.flush(command);
    c.request = request;
    c.pixels = pixels;
    c.w = w;
    c.h = h;
    c.scaleX = scaleX;
    c.scaleY = scaleY;
    return true;
}

void GsnScene::Impl::pack(Copied& c, std::vector<uint8_t>& out) {
    // Each GS pixel is the render pixel at its own sample point (j / scale = x: the first of its sx x sy),
    // stored as colour / 255 and alpha / 128 (gsn targets), packed in the target's format as the GS keeps it.
    auto toFloat = [](uint16_t v) {
        const uint32_t sign = (v >> 15) & 1, exponent = (v >> 10) & 31, mantissa = v & 1023;
        const float f = exponent ? std::ldexp(static_cast<float>(mantissa | 1024), static_cast<int>(exponent) - 25)
                                 : std::ldexp(static_cast<float>(mantissa), -24);
        return sign ? -f : f;
    };
    const auto* half = static_cast<const uint16_t*>(c.pixels.mapped);
    const gsn::ReadRequest& request = c.request;
    const uint32_t w = c.w, h = c.h, scaleX = c.scaleX, scaleY = c.scaleY;
    const uint32_t bytes = request.psm == gsn::PSMCT32 ? 4 : request.psm == gsn::PSMCT24 ? 3 : 2;
    out.assign(size_t(request.width) * request.height * bytes, 0);
    for (uint32_t y = 0; y < request.height; ++y)
        for (uint32_t x = 0; x < request.width; ++x) {
            const uint32_t px = x * scaleX, py = y * scaleY;
            if (px >= w || py >= h) continue;
            const uint16_t* p = half + (size_t(py) * w + px) * 4;
            auto channel = [&](int c, float range) {
                return static_cast<uint32_t>(std::clamp(toFloat(p[c]) * range + 0.5f, 0.0f, 255.0f));
            };
            const uint32_t r = channel(0, 255.0f), g = channel(1, 255.0f), bl = channel(2, 255.0f), a = channel(3, 128.0f);
            uint8_t* o = out.data() + (size_t(y) * request.width + x) * bytes;
            if (bytes == 2) {
                const uint32_t v = (r >> 3) | ((g >> 3) << 5) | ((bl >> 3) << 10) | ((a >> 7) << 15);
                o[0] = static_cast<uint8_t>(v);
                o[1] = static_cast<uint8_t>(v >> 8);
            } else {
                o[0] = static_cast<uint8_t>(r);
                o[1] = static_cast<uint8_t>(g);
                o[2] = static_cast<uint8_t>(bl);
                if (bytes == 4) o[3] = static_cast<uint8_t>(a);
            }
        }
    vk::destroy(device, c.pixels);
}

void GsnScene::answerUndrawnReadbacks(std::chrono::milliseconds after) {
    Impl& s = *impl;
    if (s.readbacksWaitingSince == std::chrono::steady_clock::time_point{} || std::chrono::steady_clock::now() - s.readbacksWaitingSince < after)
        return;
    size_t n = 0;
    for (auto& list : s.pending) {
        for (const auto& rb : list.readbacks) s.readAnswers.push_back({rb.id, rb.qwc, {}});
        n += list.readbacks.size();
        list.readbacks.clear();
    }
    for (auto& list : s.readHeld) {
        for (const auto& rb : list.readbacks) s.readAnswers.push_back({rb.id, rb.qwc, {}});
        n += list.readbacks.size();
        list.readbacks.clear();
    }
    s.readbacksWaitingSince = {};
    static uint64_t total = 0;
    total += n;
    std::printf("gsn: %zu asynchronous read(s) answered with zeros: their lists not drawn for %lld ms (%llu so far)\n", n,
                static_cast<long long>(after.count()), static_cast<unsigned long long>(total));
}

void GsnScene::abandonReadbacks() {
    Impl& s = *impl;
    vkDeviceWaitIdle(s.device.device);
    for (auto& inFlight : s.readbacksInFlight) s.finishReadbacks(inFlight);
    s.finishReadbacks(s.readbacksRecorded);
    s.readbacksWaitingSince = std::chrono::steady_clock::time_point{std::chrono::steady_clock::duration{1}};
    answerUndrawnReadbacks(std::chrono::milliseconds(0));
}

void GsnScene::slotDone(uint32_t slot) { impl->finishReadbacks(impl->readbacksInFlight[slot % kSlots]); }

void GsnScene::takeReadAnswers(std::vector<ReadAnswer>& out) {
    for (auto& a : impl->readAnswers) out.push_back(std::move(a));
    impl->readAnswers.clear();
}

bool GsnScene::read(const gsn::ReadRequest& request, std::vector<uint8_t>& out) {
    Impl& s = *impl;
    if (!s.stencilPrep.valid() || !s.blank.valid()) return false;   // not prepared yet (no frame shown)
    for (auto& list : s.readHeld) s.pending.push_back(std::move(list));   // (GT4_READ_SKIP_TEST)
    s.readHeld.clear();
    // The game waits for these pixels: the complete frames ahead of the read's list are skipped by skipOlderFrames'
    // rule, so a host behind the game answers now instead of after drawing its backlog (the Odin's blurred replay
    // cameras: reads of 4 to 25 s, then F7's stop). A frame is drawn all the same when a target it writes is not
    // written again by a later list, the read's own included (what the read returns is then that frame's), or when a
    // later list reads one of its targets before writing it. GT4_READ_SKIP=0: every list drawn first.
    static const bool readSkip = [] { const char* e = std::getenv("GT4_READ_SKIP"); return !(e && *e == '0'); }();
    // Where a slow read's time goes (the game waits for it), printed for a read over 16 ms.
    using Clock = std::chrono::steady_clock;
    const Clock::time_point started = Clock::now();
    const Counters before = counters();
    ++s.reads;
    const size_t skipped = readSkip ? s.skipOlderFrames(0) : 0;
    s.readSkips += skipped;
    const size_t lists = s.pending.size();
    vkDeviceWaitIdle(s.device.device);   // the frames in flight are done: slot 0's buffers are free
    for (auto& inFlight : s.readbacksInFlight) s.finishReadbacks(inFlight);
    const Clock::time_point idle = Clock::now();
    vk::Uploader up(s.device, s.pool);
    VkCommandBuffer command = up.command();
    // the pixels as the game drew them: no frame interpolation (its moving 3D a step behind, by the display's clock)
    const bool interpolating = s.interpolate;
    s.interpolate = false;
    // The lists and the copy as passes of the read's own render graph (ticket B4 step 5; the frames' are in the
    // pipeline's graph, GsnScene::buildPrepass): the scene stage's work images are its transients. Its order rule: this
    // runs from the main loop's read branch only, never inside Renderer::draw, after vkDeviceWaitIdle above (CONTRACTS
    // b.6), so slot 0's buffers and the one transient heap are free, and no frame's graph is between build and execute.
    Impl::Copied copied;
    bool copiedOk = false;
    if (!s.readGraph) {
        s.readGraph = std::make_unique<rg::Graph>(s.device);
        s.readGraph->setPassScopes(false);   // (the read is one stage already: "main: a read the game waits for")
    }
    rg::FrameInfo info;
    info.slot = 0;
    info.frameIndex = ++s.readFrames;
    info.render = info.output = s.lastExtent;
    s.reading = true;
    s.readGraph->begin(info);
    s.buildPending(*s.readGraph, 0, s.lastExtent, true);
    s.readGraph->pass("read: copy", [](rg::PassBuilder& b) { b.keep(); }, [&](rg::PassContext& c) {
        copiedOk = s.copyRect(c.command, request, copied);
    });
    s.readGraph->compile();
    if (s.readGraph->plan().ok()) {
        s.readGraph->execute(command, VK_NULL_HANDLE);
    } else {
        // (as before the graph: the lists, then the copy; told once)
        static bool told = false;
        if (!told)
            for (const std::string& error : s.readGraph->plan().errors)
                std::fprintf(stderr, "gsn: a read's render graph did not compile (recorded without it): %s\n", error.c_str());
        told = true;
        s.executePending(command, 0, s.lastExtent);
        copiedOk = s.copyRect(command, request, copied);
    }
    s.reading = false;
    s.interpolate = interpolating;
    const Clock::time_point recorded = Clock::now();
    up.submit();
    const Clock::time_point drawn = Clock::now();
    s.finishReadbacks(s.readbacksRecorded);   // (asynchronous reads in the lists drawn: done too)
    if (copiedOk) s.pack(copied, out);
    const Clock::time_point done = Clock::now();
    auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    if (ms(started, done) > 16.0) {
        const Counters after = counters();
        std::printf("gsn read %llu took %.1f ms: GPU idle first %.1f, recording %zu lists %.1f (pipelines +%llu, textures +%llu "
                    "of %.1f MiB, targets +%llu), submit and GPU %.1f, readbacks and pack %.1f; %zu frames skipped ahead; target %u "
                    "%ux%u\n",
                    static_cast<unsigned long long>(s.reads), ms(started, done), ms(started, idle), lists, ms(idle, recorded),
                    static_cast<unsigned long long>(after.pipelines - before.pipelines),
                    static_cast<unsigned long long>(after.textures - before.textures),
                    static_cast<double>(after.textureBytes - before.textureBytes) / 1048576.0,
                    static_cast<unsigned long long>(after.targets - before.targets), ms(recorded, drawn), ms(drawn, done), skipped,
                    request.target, request.width, request.height);
    }
    if (!copiedOk) return false;
    // GT4_READ_HASH=1 (testing): each read's bytes as a hash (FNV-1a), to compare runs.
    static const bool readHash = [] { const char* e = std::getenv("GT4_READ_HASH"); return e && *e && *e != '0'; }();
    if (readHash) {
        uint64_t hash = 1469598103934665603ull;
        for (uint8_t v : out) hash = (hash ^ v) * 1099511628211ull;
        std::printf("gsn read %llu: target %u (%u,%u %ux%u psm %u), %zu frames skipped ahead, hash %016llx\n",
                    static_cast<unsigned long long>(s.reads), request.target, request.x, request.y, request.width, request.height,
                    request.psm, skipped, static_cast<unsigned long long>(hash));
    }
    return true;
}

// The prepass as passes of the pipeline's render graph (ticket B4, docs/modern/CONTRACTS.md b.6): the pending lists'
// work, each list its own passes, in the order executePending runs them (Impl::buildPending). The GS targets are not
// graph resources: each pass records its barriers from their vk::Image state, as before. The scene stage's work images
// are transients the graph makes and synchronises (the TAA histories stay the scene's). The lists are known now and do
// not change before the graph executes: frames are added between draws, and GsnScene::read runs only from the main
// loop's read branch, never inside Renderer::draw (CONTRACTS b.6's read order), and waits for the device first.
bool GsnScene::buildPrepass(rg::Graph& graph, const SceneView& view) {
    Impl& s = *impl;
    if (s.pending.empty()) {   // the last list drawn again, or nothing
        graph.pass("gs: frame again", [](rg::PassBuilder& b) { b.keep(); }, [this, view](rg::PassContext& c) { recordPrepass(c.command, view); });
        return true;
    }
    s.buildPending(graph, view.slot, view.renderExtent, false);
    return true;
}

void GsnScene::Impl::buildPending(rg::Graph& graph, uint32_t slot, VkExtent2D extent, bool read) {
    Impl& s = *this;
    auto keep = [](rg::PassBuilder& b) { b.keep(); };
    {
        // The render scale first, before any pass (it drops the targets when it changes): the scene stage's work images
        // are sized from the targets at that scale (beginPending, in the frame's first pass, does the rest).
        FrameScopes scopes(s, false);
        s.decideScale(extent);
    }
    s.lastExtent = extent;
    s.pendingBuilt = s.pending.size();
    graph.pass(read ? "read: frame" : "gs: frame", keep, [&s, slot, extent, read](rg::PassContext& c) {
        FrameScopes scopes(s);
        s.beginPending(c.command, slot, extent, !read, true);
    });
    // The targets as each list's uploads will leave them (Impl::reshapeTarget: ensureTarget's decisions, on copies), list
    // by list: the scene stage's area at each Light, so the sizes its work images grow to, known before the passes run.
    std::unordered_map<uint32_t, Impl::TargetShape> shapes;
    auto shape = [&](uint32_t id) -> Impl::TargetShape& {
        auto it = shapes.find(id);
        if (it == shapes.end()) {
            auto t = s.targets.find(id);
            it = shapes.emplace(id, t == s.targets.end() ? Impl::TargetShape{} : Impl::shapeOf(t->second)).first;
        }
        return it->second;
    };
    // GT4_GSN_LIMIT (debugging): a list's draws stop anywhere, and the scene stages after with them: each Light in one pass.
    static const bool drawLimit = [] {
        const char* e = std::getenv("GT4_GSN_LIMIT");
        return e && std::strtol(e, nullptr, 10) >= 0;
    }();
    // The scene stage at Light command `at` of `list`: a pass or more for each step that runs (Impl::Step), each on the
    // render graph's transients for its work images, in the layout that part needs (the graph's barriers; the GS targets'
    // are the stage's own, as before). One pass on the members (executeLight) when no step runs, the Light has no scene
    // (nothing runs then either), or GT4_GSN_LIMIT is set.
    auto sceneStage = [&](const gsn::FrameList& list, size_t at) {
        // (`check`: the plan saw no scene or no step; a scene with a step at run time is counted as a wrong plan)
        auto inOnePass = [&](bool check) {
            graph.pass(read ? "read: scene stage" : "gs: scene stage", keep, [&s, slot, at, check](rg::PassContext& c) {
                FrameScopes scopes(s);
                GT4_STAGE_SCOPE("render: the lists' draws");
                if (check) s.checkOnePass(at);
                s.executeLight(c.command, slot, at);
            });
        };
        const gsn::FrameList::Lighting& l = list.lights[list.commands[at].index];
        Impl::LightPlan plan;
        plan.scene = Impl::sceneArea(shape(l.target), shape(l.depth), l, plan.area);
        plan.colour = shape(l.target).extent;
        bool any = false;
        for (int step = 0; step < Impl::kSteps; ++step) any = (plan.runs[step] = s.stepRuns(step, l, Impl::viewRecords(list))) || any;
        if (drawLimit || !plan.scene || !any) {
            inOnePass(!drawLimit);
            return;
        }
        // The work images grown as the members would be at this Light (Impl::ensureWork's rule): the motion vectors over
        // the colour target, the reflections at half the area, the rest over the area. The transients are made at these.
        // They grow now, at the build, also for a frame that then never executes (its graph does not compile, or it is
        // dropped): later work images may then be larger than the old code's. No pixel depends on that: every stage
        // shader reads its work images with texelFetch at coordinates clamped to the push constants' area (the light
        // pass's occlusion, the composite's half-resolution reflections, the resolve's and the blur's motion vectors),
        // none takes an image's own size (no textureSize or imageSize; the blur's and focus's `imageSize` is the colour
        // target's), and the motion vectors' render area is the colour target and Z buffer's whenever the image covers
        // them, which a grown extent always does.
        const VkExtent2D area = plan.area, half{(area.width + 1) / 2, (area.height + 1) / 2};
        if (plan.runs[Impl::kStepMotionVectors]) Impl::grow(s.velocityExtent, plan.colour);
        if (plan.runs[Impl::kStepLighting]) {
            Impl::grow(s.aoExtent, area);
            Impl::grow(s.litExtent, area);
        }
        if (plan.runs[Impl::kStepReflections]) {
            Impl::grow(s.ssrExtent, half);
            Impl::grow(s.litExtent, area);
        }
        for (int step : {Impl::kStepFog, Impl::kStepMotionBlur, Impl::kStepFocus})
            if (plan.runs[step]) Impl::grow(s.litExtent, area);
        auto desc = [](VkFormat format, VkExtent2D extent, const char* name) {
            rg::TextureDesc d;
            d.format = format;
            d.size = rg::Size::Absolute;
            d.extent = extent;
            d.name = name;
            return d;
        };
        const rg::TextureDesc velocityDesc = desc(kVelocity, s.velocityExtent, "gsn motion vectors"),
                              aoDesc = desc(VK_FORMAT_R8G8B8A8_UNORM, s.aoExtent, "gsn occlusion"),
                              litDesc = desc(kColour, s.litExtent, "gsn lit"), ssrDesc = desc(kColour, s.ssrExtent, "gsn reflections");
        // The passes: each step's parts (one layout of its work images each), with the marks of the steps that do not run
        // where executeLight makes them (the profile's stages).
        struct StagePass {
            const char* name;
            int step, part;
            std::vector<const char*> before, after;
        };
        std::vector<StagePass> passes;
        std::vector<const char*> waiting;   // the marks before the first step that runs
        for (int step = 0; step < Impl::kSteps; ++step) {
            if (!plan.runs[step]) {
                (passes.empty() ? waiting : passes.back().after).push_back(Impl::kStepMarks[step]);
                continue;
            }
            switch (step) {
                case Impl::kStepMotionVectors: passes.push_back({read ? "read: motion vectors" : "scene: motion vectors", step, Impl::kPartWhole}); break;
                case Impl::kStepLighting:
                    passes.push_back({read ? "read: lighting (occlusion)" : "scene: lighting (occlusion)", step, Impl::kPartFirst});
                    passes.push_back({read ? "read: lighting" : "scene: lighting", step, Impl::kPartMain});
                    passes.push_back({read ? "read: lighting (copy back)" : "scene: lighting (copy back)", step, Impl::kPartCopy});
                    break;
                case Impl::kStepReflections:
                    passes.push_back({read ? "read: reflections (trace)" : "scene: reflections (trace)", step, Impl::kPartFirst});
                    passes.push_back({read ? "read: reflections" : "scene: reflections", step, Impl::kPartMain});
                    passes.push_back({read ? "read: reflections (copy back)" : "scene: reflections (copy back)", step, Impl::kPartCopy});
                    break;
                case Impl::kStepFog:
                    passes.push_back({read ? "read: fog" : "scene: fog", step, Impl::kPartMain});
                    passes.push_back({read ? "read: fog (copy back)" : "scene: fog (copy back)", step, Impl::kPartCopy});
                    break;
                case Impl::kStepTemporalResolve: passes.push_back({read ? "read: temporal resolve" : "scene: temporal resolve", step, Impl::kPartWhole}); break;
                case Impl::kStepMotionBlur:
                    passes.push_back({read ? "read: motion blur" : "scene: motion blur", step, Impl::kPartMain});
                    passes.push_back({read ? "read: motion blur (copy back)" : "scene: motion blur (copy back)", step, Impl::kPartCopy});
                    break;
                case Impl::kStepFocus:
                    passes.push_back({read ? "read: focus" : "scene: focus", step, Impl::kPartMain});
                    passes.push_back({read ? "read: focus (copy back)" : "scene: focus (copy back)", step, Impl::kPartCopy});
                    break;
                default: break;
            }
            passes.back().after.push_back(Impl::kStepMarks[step]);
        }
        passes.front().before = std::move(waiting);
        // The Light's transients at their current versions (the setups run as each pass is added). The motion vectors,
        // the occlusion and the reflections are written by their step's first pass and read by later ones; each step's
        // result has a lit of its own (written whole, then copied from), as the member is made anew at each step.
        struct Used { rg::Texture velocity, ao, lit, ssr; };
        rg::Texture velocity, ao, lit, ssr;
        for (size_t p = 0; p < passes.size(); ++p) {
            const StagePass& sp = passes[p];
            auto used = std::make_shared<Used>();
            graph.pass(
                sp.name,
                [&](rg::PassBuilder& b) {
                    b.keep();   // (it writes the GS colour target, outside the graph)
                    if (sp.part == Impl::kPartCopy) {
                        used->lit = lit = b.use(lit, rg::Use::TransferSrc, vk::kTransfer);
                        return;
                    }
                    switch (sp.step) {
                        case Impl::kStepMotionVectors:
                            used->velocity = velocity = b.use(b.create(velocityDesc), rg::Use::ColorWrite, vk::kColorOut);   // (cleared)
                            return;
                        case Impl::kStepTemporalResolve: used->velocity = b.use(velocity, rg::Use::Sampled, vk::kCompute); return;
                        case Impl::kStepLighting:
                            if (sp.part == Impl::kPartFirst) {
                                used->ao = ao = b.use(b.create(aoDesc), rg::Use::StorageWrite, vk::kCompute);
                                return;
                            }
                            used->ao = b.use(ao, rg::Use::Sampled, vk::kCompute);
                            break;
                        case Impl::kStepReflections:
                            if (sp.part == Impl::kPartFirst) {
                                used->ssr = ssr = b.use(b.create(ssrDesc), rg::Use::StorageWrite, vk::kCompute);
                                return;
                            }
                            used->ssr = b.use(ssr, rg::Use::Sampled, vk::kCompute);
                            break;
                        case Impl::kStepMotionBlur: used->velocity = b.use(velocity, rg::Use::Sampled, vk::kCompute); break;
                        default: break;   // (fog, focus: the scene and its Z only)
                    }
                    used->lit = lit = b.use(b.create(litDesc), rg::Use::StorageWrite, vk::kCompute);
                },
                [&s, at, step = sp.step, part = sp.part, before = sp.before, after = sp.after, used, plan, first = p == 0](rg::PassContext& c) {
                    FrameScopes scopes(s);
                    GT4_STAGE_SCOPE("render: the lists' draws");
                    Impl::WorkImages w;
                    w.graph = true;
                    if (used->velocity) w.velocity = &c.image(used->velocity);
                    if (used->ao) w.ao = &c.image(used->ao);
                    if (used->lit) w.lit = &c.image(used->lit);
                    if (used->ssr) w.ssr = &c.image(used->ssr);
                    s.executeLightPart(c.command, at, step, part, w, before, after, first ? &plan : nullptr);
                });
        }
    };
    for (size_t i = 0; i < s.pending.size(); ++i) {
        graph.pass(read ? "read: new targets" : "gs: new targets", keep, [&s, slot, i](rg::PassContext& c) {
            FrameScopes scopes(s);
            assert(s.pending.size() == s.pendingBuilt && i < s.pending.size());   // (lane A: nothing changes the lists in between)
            s.listIndex = i;
            s.pendingTargets(c.command, slot);
        });
        graph.pass(read ? "read: texture uploads" : "gs: texture uploads", keep, [&s, slot](rg::PassContext& c) {
            FrameScopes scopes(s);
            s.pendingUploads(c.command, slot);
        });
        for (const gsn::Target& t : s.pending[i].targets) {   // (as pendingTargets will)
            bool dropped = false;
            Impl::reshapeTarget(shape(t.id), t, s.scaleFor(t), dropped);
        }
        // The list's draws (pendingDraws' execute), split at its Light commands: its 3D batches, a pass for each run of
        // commands between them and the scene stage at each, then the end of its draws. The split is the list's own
        // (Impl::segments: its commands' kinds), made now; the list is made current by its uploads pass.
        std::vector<Impl::Segment> split;
        Impl::segments(s.pending[i], split);
        graph.pass(read ? "read: 3D batches" : "gs: 3D batches", keep, [&s, slot](rg::PassContext& c) {
            FrameScopes scopes(s);
            GT4_STAGE_SCOPE("render: the lists' draws");
            s.executeBegin(c.command, slot);
        });
        for (const Impl::Segment& g : split) {
            if (g.kind == Impl::Segment::Kind::Light) {
                sceneStage(s.pending[i], g.from);
            } else if (g.kind == Impl::Segment::Kind::Effect) {
                graph.pass(read ? "read: colour grading" : "gs: colour grading", keep, [&s, slot, from = g.from, to = g.to](rg::PassContext& c) {
                    FrameScopes scopes(s);
                    GT4_STAGE_SCOPE("render: the lists' draws");
                    s.executeRange(c.command, slot, from, to);
                });
            } else {
                graph.pass(read ? "read: draws" : "gs: draws", keep, [&s, slot, from = g.from, to = g.to](rg::PassContext& c) {
                    FrameScopes scopes(s);
                    GT4_STAGE_SCOPE("render: the lists' draws");
                    s.executeRange(c.command, slot, from, to);
                });
            }
        }
        graph.pass(read ? "read: draws end" : "gs: draws end", keep, [&s, slot](rg::PassContext& c) {
            FrameScopes scopes(s);
            GT4_STAGE_SCOPE("render: the lists' draws");
            s.executeFinish(c.command, slot);
        });
        graph.pass(read ? "read: list end" : "gs: list end", keep, [&s](rg::PassContext& c) {
            FrameScopes scopes(s);
            s.pendingListEnd(c.command);
        });
    }
    graph.pass(read ? "read: frame end" : "gs: frame end", keep, [&s, slot, read](rg::PassContext& c) {
        FrameScopes scopes(s);
        s.listIndex = s.pending.size();
        s.endPending(c.command);
        if (read) return;   // (a read's asynchronous readbacks: finished after its submission, GsnScene::read)
        auto& inFlight = s.readbacksInFlight[slot % kSlots];   // (as recordPrepass)
        for (auto& r : s.readbacksRecorded) inFlight.push_back(std::move(r));
        s.readbacksRecorded.clear();
    });
}

void GsnScene::recordMain(VkCommandBuffer command, const SceneView& view) {
    Impl& s = *impl;
    auto shown = s.targets.find(s.displayTarget);
    if (shown == s.targets.end() || !s.display.valid()) return;
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(view.renderExtent.width), static_cast<float>(view.renderExtent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, view.renderExtent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    // B16c: with the seam on, the pass that also writes motion vectors and depth (the shown picture's seam, else none)
    if (s.seamOn && !s.displaySeam.valid()) s.makeDisplaySeam();
    const bool seamed = s.seamOn && s.displaySeam.valid();
    vk::Pipeline& pass = seamed ? s.displaySeam : s.display;
    vk::bind(command, pass);
    auto other = s.fieldBlend > 0.0f ? s.targets.find(s.blendTarget) : s.targets.end();
    const VkImageView otherView = other != s.targets.end() ? other->second.image.view : shown->second.image.view;
    vk::Descriptors descriptors;
    descriptors.sampled(0, shown->second.image.view, s.samplers[2]).sampled(1, otherView, s.samplers[2]);
    const Impl::Seam* seam = nullptr;
    if (seamed) {
        auto it = s.seams.find(s.displayTarget);
        if (it != s.seams.end() && it->second.valid && it->second.velocity.extent.width == shown->second.image.extent.width &&
            it->second.velocity.extent.height == shown->second.image.extent.height)
            seam = &it->second;
        descriptors.sampled(2, seam ? seam->velocity.view : s.blank.view, s.samplers[0])
            .sampled(3, seam ? seam->depth.view : s.blank.view, s.samplers[0]);
    }
    descriptors.push(command, pass);
    float constants[16] = {};
    std::copy(s.displayRect, s.displayRect + 4, constants);
    std::copy(s.otherFieldRect, s.otherFieldRect + 4, constants + 4);
    constants[8] = other != s.targets.end() ? s.fieldBlend : 0.0f;
    constants[9] = s.fieldClean ? 1.0f : 0.0f;
    constants[10] = s.fieldLine;
    constants[12] = 1.0f / s.displayedGs[0];   // (b.11's velocity: a fraction of the displayed rectangle)
    constants[13] = 1.0f / s.displayedGs[1];
    constants[14] = seam ? 1.0f : 0.0f;
    static const bool seamView = [] { const char* e = std::getenv("GT4_SEAM_VIEW"); return e && *e == '1'; }();
    constants[15] = seamView ? float(shown->second.scaleX) : 0.0f;   // (debugging: the seam's motion vectors shown)
    vk::pushConstants(command, pass, constants, seamed ? 64 : 48);
    vkCmdDraw(command, 3, 1, 0, 0);
}

SceneCamera GsnScene::camera() const {
    SceneCamera camera;
    camera.previousViewProjection = camera.projection * camera.view;
    return camera;
}

}  // namespace gt4
