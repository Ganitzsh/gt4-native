// The native renderer's back end (see gsn_scene.h and docs/NATIVE_RENDERER.md).
#include "gsn_scene.h"

#include "gt4_shaders.h"
#include "../gs/cpu_profile.h"

#include <algorithm>
#include <bit>
#include <memory>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>

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

// The temporal anti-aliasing's sub-pixel offsets: Halton (2, 3), 8 phases, in render pixels from the centre.
constexpr float kHalton[8][2] = {{0.0f, -1.0f / 6.0f},   {-0.25f, 1.0f / 6.0f}, {0.25f, -7.0f / 18.0f}, {-0.375f, -1.0f / 18.0f},
                                 {0.125f, 5.0f / 18.0f}, {-0.125f, -5.0f / 18.0f}, {0.375f, 1.0f / 18.0f}, {-0.4375f, 7.0f / 18.0f}};
constexpr VkFormat kVelocity = VK_FORMAT_R16G16_SFLOAT;

enum : uint32_t {
    kTextured = 1, kUv = 2, kFog = 4, kBlend = 8, kPabe = 16, kFba = 32, kFromTarget = 64, kTarget24 = 128, kDestAlphaBlend = 256,
    kNearest = 512, kSnap = 1024, kAlphaInvert = 2048, kMipmapped = 4096, kGeneratedMips = 8192, kSnapFiltered = 16384,
    kReplaced = 32768,
};

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
    // While the display shows fields (the race: 224 lines a field, sy from them), a target of 384 lines or more
    // (the 448-line 3D scene, which GT4's post effects take down to the field) gets half the vertical scale,
    // rounded up: about the output's rows rather than twice them (at 1080p, 2240 rows for 1080 shown).
    bool fieldMode = false;
    uint32_t verticalScaleFor(const gsn::Target& t) const { return fieldMode && t.height >= 384 ? (sy + 1) / 2 : sy; }
    // A target narrower than the screen (under 512 GS pixels: GT4's environment maps, the car paint's reflection,
    // the glare's levels) is a texture other draws read, never shown pixel for pixel: at most 2 render pixels per
    // GS pixel each way (a 128 x 256 environment map was drawn at 2 x 5 at 1080p, 0.44 ms of 4.5).
    std::pair<uint32_t, uint32_t> scaleFor(const gsn::Target& t) const {
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
    };
    std::unordered_map<uint32_t, GpuTexture> textures;
    // Retired textures, destroyed once the frames that may still sample them are done.
    std::vector<std::pair<uint64_t, vk::Image>> graveyard;
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
    vk::Image gradeImage;                      // the colour grading's result (gsn_grade.comp), copied back
    std::vector<vk::Buffer> curveBuffers;      // its curves, per frame
    // The scene stage's temporal anti-aliasing (docs/NATIVE_RENDERER.md): the 3D draws of a scene are jittered,
    // drawn again for their motion vectors (velocityPass) and resolved against the scene's history.
    bool temporal = false;
    uint64_t displayedFrames = 0;   // frames shown so far (the jitter's phase, the histories' age)
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
        std::vector<std::pair<uint32_t, vk::Buffer>> images;   // target id, its pixels (RGBA16F)
        std::vector<VkExtent2D> extents;
        std::vector<uint32_t> bps;
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
        for (const auto& [ms, label] : rows) std::printf("  %-28s %7.3f ms\n", label.c_str(), ms);
        std::printf("  per frame:");
        for (const auto& [what, n] : profile.counts) std::printf(" %s %.1f,", what.c_str(), double(n) / f);
        std::printf("\n");
    }

    std::unordered_map<uint32_t, vk::Pipeline> pipelines;
    vk::Pipeline display, stencilPrep;
    VkSampler samplers[4] = {};   // [linear][repeat]
    VkSampler mipSamplers[4] = {};   // [repeat U * 2 + repeat V]: mipmapped textures, trilinear, anisotropic
    uint32_t displayTarget = 0;
    float displayRect[4] = {0, 0, 1, 1};
    bool warnedBlend = false;

    Impl(const vk::Device& d, VkCommandPool p) : device(d), pool(p) {
        if (const char* e = std::getenv("GT4_GPU_PROFILE"); e && *e && *e != '0') {
            VkQueryPoolCreateInfo q{};
            q.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            q.queryType = VK_QUERY_TYPE_TIMESTAMP;
            q.queryCount = Profile::kPerSlot * kSlots;
            profile.on = vkCreateQueryPool(device.device, &q, nullptr, &profile.pool) == VK_SUCCESS;
            profile.level = std::atoi(e);
        }
        for (int linear = 0; linear < 2; ++linear)
            for (int repeat = 0; repeat < 2; ++repeat)
                samplers[linear * 2 + repeat] = vk::createSampler(device, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                                                  repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        makeMipSamplers(16.0f);
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
        if (profile.pool) vkDestroyQueryPool(device.device, profile.pool, nullptr);
        for (auto& [id, t] : targets) vk::destroy(device, t.image);
        for (auto& [id, t] : textures) vk::destroy(device, t.image);
        for (auto& [when, image] : graveyard) vk::destroy(device, image);
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
        vk::destroy(device, gradeImage);
        for (auto& b : curveBuffers) vk::destroy(device, b);
        vk::destroy(device, velocityPass);
        vk::destroy(device, taaPass);
        for (vk::Pipeline* p : {&ssrPass, &ssrComposite, &fogPass, &blurPass, &dofPass, &gradePass}) vk::destroy(device, *p);
        vk::destroy(device, ssrImage);
        vk::destroy(device, velocityImage);
        for (auto& [id, h] : histories)
            for (vk::Image& i : h.image) vk::destroy(device, i);
        for (auto& [key, p] : pipelines) vk::destroy(device, p);
        vk::destroy(device, display);
        vk::destroy(device, stencilPrep);
        for (VkSampler s : samplers)
            if (s) vkDestroySampler(device.device, s, nullptr);
        for (VkSampler s : mipSamplers)
            if (s) vkDestroySampler(device.device, s, nullptr);
    }

    // Pipelines by blend, colour write mask, depth attachment and topology.
    vk::Pipeline& pipeline(uint32_t blendKey, uint32_t mask, bool depth, gsn::Topology topology) {
        const uint32_t key = blendKey | (mask << 16) | (depth ? 1u << 20 : 0) | (static_cast<uint32_t>(topology) << 21);
        auto it = pipelines.find(key);
        if (it != pipelines.end()) return it->second;
        vk::GraphicsDesc desc;
        desc.vertex = shaders::gsn_vert();
        desc.fragment = shaders::gsn_frag();
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
        return pipelines[key] = vk::createGraphics(device, desc);
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

    void ensureTarget(const gsn::Target& t) {
        GpuTarget& g = targets[t.id];
        if (g.image.valid() && (g.generation != t.generation || g.depth != t.depth)) {   // the slot holds a new buffer
            retire(g.image);
            g = GpuTarget{};
        }
        g.generation = t.generation;
        const auto [tx, ty] = scaleFor(t);
        // Within Vulkan's guaranteed image size at this scale (a 2048-line target at 8x is 16384 tall).
        const uint32_t width = std::min(std::max<uint32_t>(64, t.bw * 64), 16384 / tx),
                       height = std::min(std::max<uint32_t>(32, t.height), 16384 / ty);
        if (g.image.valid() && g.width >= width && g.height >= height && g.scaleX == tx && g.scaleY == ty &&
            g.image.extent.width == g.width * tx && g.image.extent.height == g.height * ty) {
            g.psm = t.psm;
            return;
        }
        if (g.scaleX != tx || g.scaleY != ty) g.width = g.height = 0;   // another scale: sized afresh
        g.scaleX = tx;
        g.scaleY = ty;
        if (g.image.valid()) retire(g.image);   // still used by frames in flight or earlier lists of this one
        g.width = std::max(g.width, width);
        g.height = std::max(g.height, height);
        g.depth = t.depth;
        g.psm = t.psm;
        vk::ImageDesc desc;
        desc.format = t.depth ? kDepth : kColour;
        desc.extent = {g.width * tx, g.height * ty};
        desc.usage = t.depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
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
            if (b.buffer) {
                vkDeviceWaitIdle(device.device);
                vk::destroy(device, b);
            }
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
        ++uploads;
        for (auto it = graveyard.begin(); it != graveyard.end();) {
            if (it->first + kSlots + 1 <= uploads) {
                vk::destroy(device, it->second);
                it = graveyard.erase(it);
            } else {
                ++it;
            }
        }
    }

    void uploadTextures(VkCommandBuffer command, uint32_t slot) {
        if (frame.resetTextures) {
            for (auto& [id, t] : textures) graveyard.emplace_back(uploads, t.image);
            textures.clear();
        }
        VkDeviceSize bytes = 4;
        for (const auto& t : frame.textures) bytes += VkDeviceSize(t.pixels.size()) * 4;
        vk::Buffer& stage = buffer(staging, slot, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        VkDeviceSize offset = 0;
        vk::Barriers b;
        std::vector<std::pair<gsn::TextureImage*, VkDeviceSize>> copies;
        for (auto& t : frame.textures) {
            GpuTexture& g = textures[t.id];
            if (!g.image.valid() || g.width != t.width || g.height != t.height || g.levels != t.levels) {
                if (g.image.valid()) retire(g.image);
                vk::ImageDesc desc;
                desc.format = VK_FORMAT_R8G8B8A8_UNORM;
                desc.extent = {t.width, t.height};
                g.generated = t.levels == 1 && std::max(t.width, t.height) >= 8 ? std::bit_width(std::max(t.width, t.height)) : 1;
                desc.mips = t.levels > 1 ? t.levels : g.generated;
                desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                desc.name = "gsn texture";
                g.image = vk::createImage(device, desc);
                g.width = t.width;
                g.height = t.height;
                g.levels = t.levels;
            }
            g.replaced = t.replaced;
            g.version = t.version;
            std::memcpy(static_cast<uint8_t*>(stage.mapped) + offset, t.pixels.data(), t.pixels.size() * 4);
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
        for (const uint32_t id : frame.retiredTextures) {
            auto it = textures.find(id);
            if (it == textures.end()) continue;
            graveyard.emplace_back(uploads, it->second.image);
            textures.erase(it);
        }
    }

    void execute(VkCommandBuffer command, uint32_t slot);
    // Executes every pending list in order into `command` (the scale from `extent`), then empties the queue.
    // `profiled`: a frame's own submission (GT4_GPU_PROFILE).
    void executePending(VkCommandBuffer command, uint32_t slot, VkExtent2D extent, bool profiled = false);

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

void GsnScene::Impl::execute(VkCommandBuffer command, uint32_t slot) {
    const VkDeviceSize vertexBytes = std::max<VkDeviceSize>((frame.vertices.size() + frame.modelVertices) * sizeof(gsn::Vertex), 64);
    vk::Buffer& vertices = buffer(vertexBuffers, slot, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const uint32_t modelBase = static_cast<uint32_t>(frame.vertices.size());   // where the GPU's vertices start
    std::memcpy(vertices.mapped, frame.vertices.data(), frame.vertices.size() * sizeof(gsn::Vertex));
    // GT4's 3D batches (docs/NATIVE_V2.md, step 2): gsn_models.comp writes their triangles into the ranges their
    // draws hold, before any draw reads them.
    if (!frame.models.empty()) {
        static_assert(sizeof(gsn::Vertex) == 18 * 4, "gsn_models.comp writes 18 words a vertex");
        vk::Buffer& jobs = buffer(modelJobs, slot, frame.models.size() * sizeof(gfx::ModelJob), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(jobs.mapped, frame.models.data(), frame.models.size() * sizeof(gfx::ModelJob));
        vk::Buffer& data = buffer(modelData, slot, frame.modelData.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(data.mapped, frame.modelData.data(), frame.modelData.size() * 4);
        vk::bind(command, models);
        vk::Descriptors()
            .storageBuffer(0, jobs.buffer, 0, frame.models.size() * sizeof(gfx::ModelJob))
            .storageBuffer(1, data.buffer, 0, frame.modelData.size() * 4)
            .storageBuffer(2, vertices.buffer, 0, vertexBytes)
            .push(command, models);
        const uint32_t constants[4] = {modelBase, 0, 0, 0};
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
    vk::Barriers b;
    bool rendering = false;
    uint32_t boundColour = 0, boundDepth = 0;
    bool stencilFresh = false;   // the bound depth image's stencil holds the bound target's alpha bit 7
    VkRect2D stencilRegion{};    // ... over this rectangle (render pixels)
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    // GT4_GPU_PROFILE=2: a render pass's time under its target's name (block address, size in GS pixels, format).
    auto passLabel = [&](uint32_t id) -> const char* {
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
    const bool perPass = profile.on && profile.level >= 2;
    // The draws' state as last set in this render pass: only changes are recorded (a draw's state commands cost the
    // GPU's command processor more than its few vertices). Forgotten when the pass ends or another pass runs.
    struct Recorded {
        int depthTest = -1, compare = -1, stencilTest = -1, reference = -1, depthWrite = -1;
        bool stencilFixed = false;
        VkRect2D scissor{{-1, -1}, {0, 0}};
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        bool pushed = false;
        DrawConstants constants{};
    } recorded;
    auto end = [&] {
        if (rendering) {
            vkCmdEndRendering(command);
            if (perPass) mark(command, passLabel(boundColour));
        }
        rendering = false;
        boundPipeline = VK_NULL_HANDLE;
        recorded = Recorded{};
    };
    auto begin = [&](uint32_t colourId, uint32_t depthId) {
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
    auto prepareStencil = [&](uint32_t colourId, uint32_t depthId, VkRect2D region) {
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

    // The scene stage (docs/NATIVE_RENDERER.md, docs/LIGHTING.md): a 3D scene complete in its target (the
    // front end's Light command), before GT4's post effects and HUD. Its colour and Z images, and its area.
    struct SceneImages {
        vk::Image* colour = nullptr;
        vk::Image* depth = nullptr;
        VkExtent2D area{};
        uint32_t sx = 1, sy = 1;   // the scene target's scale
    };
    auto sceneImages = [&](const gsn::FrameList::Lighting& l, SceneImages& out) {
        auto ct = targets.find(l.target), dt = targets.find(l.depth);
        if (ct == targets.end() || dt == targets.end() || ct->second.depth || !dt->second.depth) return false;
        if (!ct->second.image.valid() || !dt->second.image.valid()) return false;
        out.colour = &ct->second.image;
        out.depth = &dt->second.image;
        out.sx = ct->second.scaleX;
        out.sy = ct->second.scaleY;
        if (dt->second.scaleX != out.sx || dt->second.scaleY != out.sy) return false;   // (never seen: they pair by size)
        VkExtent2D e{std::min(out.colour->extent.width, out.depth->extent.width), std::min(out.colour->extent.height, out.depth->extent.height)};
        if (l.width && l.height) e = {std::min(e.width, l.width * out.sx), std::min(e.height, l.height * out.sy)};
        out.area = e;
        return true;
    };
    // The stage's result copied back into the scene, except an inset view (another camera: the mirror).
    auto copyBack = [&](vk::Image& from, vk::Image& colour, VkExtent2D e, const gsn::FrameList::Lighting& l, uint32_t sx, uint32_t sy) {
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
    auto ensureImage = [&](vk::Image& image, VkExtent2D e, VkFormat format, VkImageUsageFlags usage, const char* name) {
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
    auto relight = [&](const gsn::FrameList::Lighting& l) {
        SceneImages si;
        if (!sceneImages(l, si)) return;
        vk::Image& colour = *si.colour;
        vk::Image& depth = *si.depth;
        end();
        const VkExtent2D e = si.area;
        auto ensure = [&](vk::Image& image, VkFormat format, VkImageUsageFlags usage, const char* name) {
            ensureImage(image, e, format, usage, name);
        };
        ensure(aoImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "gsn occlusion");
        ensure(litImage, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "gsn lit");
        const VkImageView depthOnly = depthView(depth);
        constexpr float kZScale = 1.6e6f;
        constexpr uint32_t kAoStep = 2;
        static const uint32_t lightView = std::getenv("GT4_LIGHT_VIEW") ? static_cast<uint32_t>(std::atoi(std::getenv("GT4_LIGHT_VIEW"))) : 0;   // GT4's 3D Z = 1.6e6 / view distance (24-bit Z; race captures)
        vk::toSampled(b, colour, vk::kCompute);
        vk::toSampled(b, depth, vk::kCompute);
        vk::toStorage(b, aoImage, true);
        b.flush(command);
        if (lighting.occlusion > 0.0f) {
            vk::bind(command, aoPass);
            vk::Descriptors().sampled(0, depthOnly, samplers[0]).storage(1, aoImage.view).push(command, aoPass);
            // At half resolution (the light pass's depth-aware blur reads it so): a quarter of the work.
            struct {
                float size[2], tanHalf[2], zScale, radius, intensity, maxPixels;
                uint32_t directions, step;
            } k{{float(e.width), float(e.height)}, {lighting.tanX, lighting.tanY}, kZScale, 1.2f, 1.4f, 40.0f * float(si.sy),
                lighting.directions, kAoStep};
            vk::pushConstants(command, aoPass, k);
            vk::dispatch2D(command, (e.width + kAoStep - 1) / kAoStep, (e.height + kAoStep - 1) / kAoStep);
        }
        vk::toSampled(b, aoImage, vk::kCompute);
        vk::toStorage(b, litImage, true);
        b.flush(command);
        vk::bind(command, lightPass);
        vk::Descriptors()
            .sampled(0, colour.view, samplers[0])
            .sampled(1, aoImage.view, samplers[0])
            .sampled(2, depthOnly, samplers[0])
            .storage(3, litImage.view)
            .push(command, lightPass);
        struct {
            float size[2], occlusion, zScale, grade, contrast, vibrance, warmth;
            uint32_t view, aoStep;
        } k{{float(e.width), float(e.height)}, lighting.occlusion, kZScale, lighting.grade, 1.18f, 0.3f, 1.0f, lightView, kAoStep};
        vk::pushConstants(command, lightPass, k);
        vk::dispatch2D(command, e.width, e.height);
        b.image(litImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.image(colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        b.flush(command);
        copyBack(litImage, colour, e, l, si.sx, si.sy);
    };

    // Temporal anti-aliasing: the 3D draws (depth-tested) into a scene's target before its Light command are
    // jittered by this frame's sub-pixel offset, the same for every scene of the frame.
    std::vector<uint8_t> jittered(frame.commands.size(), 0);
    const float* phase = kHalton[displayedFrames % 8];   // render pixels: over each target's own scale
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
    auto drawVelocity = [&](const gsn::FrameList::Lighting& l, size_t lightAt) {
        SceneImages si;
        if (!sceneImages(l, si)) return false;
        end();
        vk::Image& colour = *si.colour;
        vk::Image& depth = *si.depth;
        ensureImage(velocityImage, colour.extent, kVelocity, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "gsn motion vectors");
        vk::toColor(b, velocityImage, true);
        vk::toDepth(b, depth);
        b.flush(command);
        VkRenderingAttachmentInfo v{};
        v.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        v.imageView = velocityImage.view;
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
        info.renderArea = {{0, 0}, {std::min({colour.extent.width, depth.extent.width, velocityImage.extent.width}),
                                    std::min({colour.extent.height, depth.extent.height, velocityImage.extent.height})}};
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
            vkCmdDraw(command, d.count, 1, d.modeled ? modelBase + d.first : d.first, 0);
        }
        vkCmdEndRendering(command);
        return true;
    };
    // A pass of the scene stage over the scene's area: the scene (binding 0, linear), its Z (1), the motion
    // vectors (2: the scene when there are none) into litImage (3), copied back.
    // `third`: the image at binding 2 (the motion vectors, the half-resolution reflections; else the scene again).
    auto stagePass = [&](const gsn::FrameList::Lighting& l, const SceneImages& si, vk::Pipeline& pass, const void* push, uint32_t pushSize,
                         vk::Image* third) {
        end();
        vk::Image& colour = *si.colour;
        const VkExtent2D e = si.area;
        ensureImage(litImage, e, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "gsn lit");
        vk::toSampled(b, colour, vk::kCompute);
        vk::toSampled(b, *si.depth, vk::kCompute);
        if (third) vk::toSampled(b, *third, vk::kCompute);
        vk::toStorage(b, litImage, true);
        b.flush(command);
        vk::bind(command, pass);
        vk::Descriptors()
            .sampled(0, colour.view, samplers[2])
            .sampled(1, depthView(*si.depth), samplers[0])
            .sampled(2, third ? third->view : colour.view, samplers[0])
            .storage(3, litImage.view)
            .push(command, pass);
        vk::pushConstants(command, pass, push, pushSize);
        vk::dispatch2D(command, e.width, e.height);
        b.image(litImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.image(colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        b.flush(command);
        copyBack(litImage, colour, e, l, si.sx, si.sy);
    };
    constexpr float kZScale = 1.6e6f;   // GT4's 3D Z = 1.6e6 / view distance (docs/LIGHTING.md)
    static const uint32_t ssrView = std::getenv("GT4_SSR_VIEW") ? static_cast<uint32_t>(std::atoi(std::getenv("GT4_SSR_VIEW"))) : 0;
    auto reflect = [&](const gsn::FrameList::Lighting& l) {
        SceneImages si;
        if (!sceneImages(l, si)) return;
        struct {
            float size[2], tanHalf[2], zScale, strength;
            uint32_t steps, frame;
            float roughness;
            uint32_t view;
        } k{{float(si.area.width), float(si.area.height)}, {lighting.tanX, lighting.tanY}, kZScale, effects.reflections,
            effects.reflectionSteps, static_cast<uint32_t>(displayedFrames), temporal ? 0.06f : 0.0f, ssrView};
        // Traced at half resolution into ssrImage, then blended in at full resolution (gsn_ssr_composite.comp).
        end();
        const VkExtent2D half{(si.area.width + 1) / 2, (si.area.height + 1) / 2};
        ensureImage(ssrImage, half, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "gsn reflections");
        vk::toSampled(b, *si.colour, vk::kCompute);
        vk::toSampled(b, *si.depth, vk::kCompute);
        vk::toStorage(b, ssrImage, true);
        b.flush(command);
        vk::bind(command, ssrPass);
        vk::Descriptors()
            .sampled(0, si.colour->view, samplers[0])
            .sampled(1, depthView(*si.depth), samplers[0])
            .sampled(2, si.colour->view, samplers[0])
            .storage(3, ssrImage.view)
            .push(command, ssrPass);
        vk::pushConstants(command, ssrPass, k);
        vk::dispatch2D(command, half.width, half.height);
        struct {
            float size[2], zScale;
            uint32_t view;
        } c{{float(si.area.width), float(si.area.height)}, kZScale, ssrView};
        stagePass(l, si, ssrComposite, &c, sizeof c, &ssrImage);
    };
    auto haze = [&](const gsn::FrameList::Lighting& l) {
        SceneImages si;
        if (!sceneImages(l, si) || !l.fogged) return;
        const uint32_t f = l.fogColour;
        struct {
            float size[2], tanHalf[2], zScale, density, falloff, height;
            float colour[4];
        } k{{float(si.area.width), float(si.area.height)}, {lighting.tanX, lighting.tanY}, kZScale, effects.fogDensity, 0.02f, 1.5f,
            {float(f & 0xFF) / 255.0f, float((f >> 8) & 0xFF) / 255.0f, float((f >> 16) & 0xFF) / 255.0f, 1.0f}};
        stagePass(l, si, fogPass, &k, sizeof k, nullptr);
    };
    auto blur = [&](const gsn::FrameList::Lighting& l) {
        SceneImages si;
        if (!sceneImages(l, si)) return;
        struct {
            float size[2], scale[2], imageSize[2], shutter, maxBlur, zScale;
            uint32_t samples, frame;
        } k{{float(si.area.width), float(si.area.height)}, {float(si.sx), float(si.sy)},
            {float(si.colour->extent.width), float(si.colour->extent.height)}, effects.shutter, 0.05f * float(si.area.height), kZScale,
            effects.blurSamples, static_cast<uint32_t>(displayedFrames)};
        stagePass(l, si, blurPass, &k, sizeof k, &velocityImage);
    };
    auto focus = [&](const gsn::FrameList::Lighting& l) {
        SceneImages si;
        if (!sceneImages(l, si)) return;
        struct {
            float size[2], tanHalf[2], imageSize[2], zScale, focus, fNumber, maxCoc;
            uint32_t samples;
        } k{{float(si.area.width), float(si.area.height)}, {lighting.tanX, lighting.tanY},
            {float(si.colour->extent.width), float(si.colour->extent.height)}, kZScale, effects.focus, effects.aperture,
            0.02f * float(si.area.height), effects.dofSamples};
        stagePass(l, si, dofPass, &k, sizeof k, nullptr);
    };

    // The resolve: the scene against its history (gsn_taa.comp), into the history's other image, copied back.
    auto antialias = [&](const gsn::FrameList::Lighting& l) {
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
        vk::toSampled(b, colour, vk::kCompute);
        vk::toSampled(b, velocityImage, vk::kCompute);
        vk::toSampled(b, *si.depth, vk::kCompute);
        if (!reset) vk::toSampled(b, before, vk::kCompute);
        vk::toStorage(b, after, true);
        b.flush(command);
        vk::bind(command, taaPass);
        vk::Descriptors()
            .sampled(0, colour.view, samplers[0])
            .sampled(1, velocityImage.view, samplers[0])
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
    const size_t count = frame.reinterpretations.size();
    built.assign(count, false);
    vk::Buffer& cluts = buffer(clutBuffers, slot, std::max<VkDeviceSize>(count * 1024, 1024), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    for (size_t i = 0; i < count; ++i) std::memcpy(static_cast<uint8_t*>(cluts.mapped) + i * 1024, frame.reinterpretations[i].clut.data(), 1024);
    // Indices from GS memory (target 0), one buffer for the frame: each one's start in words.
    std::vector<uint32_t> indexStart(count, 0);
    size_t indexWords = 1;
    for (size_t i = 0; i < count; ++i) {
        indexStart[i] = static_cast<uint32_t>(indexWords);
        indexWords += (frame.reinterpretations[i].indices.size() + 3) / 4;
    }
    vk::Buffer& indexBuffer = buffer(indexBuffers, slot, indexWords * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    for (size_t i = 0; i < count; ++i) {
        const auto& indices = frame.reinterpretations[i].indices;
        if (!indices.empty()) std::memcpy(static_cast<uint8_t*>(indexBuffer.mapped) + indexStart[i] * 4, indices.data(), indices.size());
    }
    // Their images come from a pool by size (sizes change from frame to frame; no waiting on the GPU).
    for (auto& [size, pool] : reinterpretPool) pool.used = 0;
    reinterpretImages.assign(count, nullptr);
    // Each is built at the scale of the target its draw renders into (gsn.frag's kSnap reads it per render pixel).
    std::vector<std::pair<uint32_t, uint32_t>> builtScale(count, {sx, sy});
    auto imageFor = [&](uint32_t index) -> vk::Image& {
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
    auto sourceOf = [&](const gsn::Reinterpretation& r) -> GpuTarget* {
        if (!r.target) return nullptr;
        auto it = targets.find(r.target);
        return it == targets.end() || !it->second.image.valid() || it->second.depth ? nullptr : &it->second;
    };
    auto paletteOf = [&](const gsn::Reinterpretation& r) -> GpuTarget* {   // a palette drawn into a target (clutWritten)
        if (!r.clutTarget) return nullptr;
        auto it = targets.find(r.clutTarget);
        return it == targets.end() || !it->second.image.valid() || it->second.depth ? nullptr : &it->second;
    };
    // Builds the reinterpretation a draw needs, with every later one whose targets no draw in between
    // writes (their contents are the same now): one compute batch instead of a pass break each (GT4's colour
    // grading needs about 600 a frame).
    auto build = [&](uint32_t index, size_t at) -> vk::Image* {
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
                uint32_t indexStart, scaleY, pad[2];
            } k{r.targetBp, source ? r.targetBw : 0, source ? source->height : 0, builtScale[i].first, r.texture.base,
                r.texture.width, r.texture.format, r.width, r.height,
                r.texture.clutFormat, r.texture.clutOffset,
                r.texAlpha0 | (static_cast<uint32_t>(r.texAlpha1) << 8) | (static_cast<uint32_t>(r.texBlackTransparent) << 16),
                palette ? r.clutTargetBp : 0, palette ? r.clutTargetBw : 0, palette ? palette->height : 0,
                r.texture.clutBase, source ? 0 : indexStart[i], builtScale[i].second,
                {source ? source->scaleX | (source->scaleY << 8) : sx | (sy << 8), palette ? palette->scaleX | (palette->scaleY << 8) : sx | (sy << 8)}};
            const VkImageView fallback = source ? source->image.view : palette ? palette->image.view : blank.view;
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
    vk::Buffer* curves = nullptr;
    if (!frame.effects.empty()) {
        curves = &buffer(curveBuffers, slot, frame.effects.size() * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        for (size_t i = 0; i < frame.effects.size(); ++i)
            std::memcpy(static_cast<uint8_t*>(curves->mapped) + i * 1024, frame.effects[i].curve.data(), 1024);
    }
    auto grade = [&](uint32_t index) {
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

    // GT4_GSN_LIMIT=K (debugging): only the first K draws of each frame.
    static const long limit = std::getenv("GT4_GSN_LIMIT") ? std::strtol(std::getenv("GT4_GSN_LIMIT"), nullptr, 10) : -1;
    long executed = 0;
    for (size_t at = 0; at < frame.commands.size(); ++at) {
        const auto& cmd = frame.commands[at];
        if (cmd.kind == gsn::FrameList::Command::Kind::Light) {
            if (cmd.index >= frame.lights.size()) continue;
            // The scene stage, in a modern renderer's order: motion vectors, lighting, reflections and fog
            // (before the resolve, which also integrates their noise), the resolve, motion blur, depth of field.
            const gsn::FrameList::Lighting& l = frame.lights[cmd.index];
            end();
            mark(command, "draws");
            tally("scenes");
            const bool moving = (temporal || effects.shutter > 0.0f) && drawVelocity(l, at);
            mark(command, "motion vectors");
            if (lighting.occlusion > 0.0f || lighting.grade > 0.0f) relight(l);
            mark(command, "lighting");
            if (effects.reflections > 0.0f) reflect(l);
            mark(command, "reflections");
            if (effects.fogDensity > 0.0f) haze(l);
            mark(command, "fog");
            if (moving && temporal) antialias(l);
            mark(command, "temporal resolve");
            if (moving && effects.shutter > 0.0f) blur(l);
            mark(command, "motion blur");
            if (effects.aperture > 0.0f) focus(l);
            mark(command, "depth of field");
            continue;
        }
        if (cmd.kind == gsn::FrameList::Command::Kind::Effect) {
            if (cmd.index < frame.effects.size()) grade(cmd.index);
            continue;
        }
        if (cmd.kind != gsn::FrameList::Command::Kind::Draw) continue;
        if (limit >= 0 && executed++ >= limit) break;
        const gsn::Draw& d = frame.draws[cmd.index];
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
            if (!stencilFresh || boundColour != d.target || boundDepth != depthId || !inside) {
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
            }
        }
        if (!date && mask & VK_COLOR_COMPONENT_A_BIT) stencilFresh = false;
        // Keep the bound Z buffer for a draw without depth test on the same target.
        if (!depthId && rendering && boundColour == d.target) depthId = boundDepth;
        if (!rendering || boundColour != d.target || boundDepth != depthId) {
            end();
            begin(d.target, depthId);
            if (depthId == 0) stencilFresh = false;
        }
        const bool hasDepth = depthId != 0;
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
        const bool zwrite = hasDepth && zte && d.depth && c.depthWrite;
        const int depthTest = hasDepth && zte && d.depth ? 1 : 0, stencilTest = date && hasDepth ? 1 : 0;
        if (recorded.depthTest != depthTest) vkCmdSetDepthTestEnable(command, depthTest ? VK_TRUE : VK_FALSE);
        if (recorded.compare != int(ztst)) vkCmdSetDepthCompareOp(command, zops[ztst]);
        if (recorded.stencilTest != stencilTest) vkCmdSetStencilTestEnable(command, stencilTest ? VK_TRUE : VK_FALSE);
        if (!recorded.stencilFixed) {
            vkCmdSetStencilOp(command, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL);
            vkCmdSetStencilCompareMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
            vkCmdSetStencilWriteMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
        }
        if (recorded.reference != int(c.destPass)) vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, c.destPass);
        recorded.depthTest = depthTest;
        recorded.compare = int(ztst);
        recorded.stencilTest = stencilTest;
        recorded.stencilFixed = true;
        recorded.reference = int(c.destPass);
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
        } passes[2];
        uint32_t passCount = 0;
        if (!ate || atst != 0) passes[passCount++] = {mask, zwrite, false};
        if (ate && atst != 1 && afail != 0) {
            const uint32_t rgb = mask & (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT);
            passes[passCount++] = afail == 1 ? Pass{mask, false, true} : afail == 2 ? Pass{0, zwrite, true} : Pass{rgb, false, true};
        }
        for (uint32_t pass = 0; pass < passCount; ++pass) {
            const Pass& ps = passes[pass];
            if (!ps.mask && !ps.zwrite) continue;
            vk::Pipeline& p = pipeline(blendKey, ps.mask, hasDepth, d.topology);
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
            vkCmdDraw(command, d.count, 1, d.modeled ? modelBase + d.first : d.first, 0);
            tally("draws");
        }
    }
    end();
    mark(command, "draws");
    // The displayed buffer, for recordMain (a list cut for a GS -> host read displays nothing).
    if (!frame.displayed) return;
    ++displayedFrames;
    displayTarget = frame.display.target2 ? frame.display.target2 : frame.display.target1;
    auto shown = targets.find(displayTarget);
    if (shown != targets.end()) {
        const gfx::DisplayCircuit& c = frame.display.record.circuit[frame.display.target2 ? 1 : 0];
        const uint32_t dbx = c.x, dby = c.y;
        uint32_t w, h;
        displayedSize(frame.display, w, h);
        const float tw = static_cast<float>(shown->second.image.extent.width) / shown->second.scaleX,
                    th = static_cast<float>(shown->second.image.extent.height) / shown->second.scaleY;
        displayRect[0] = dbx / tw;
        displayRect[1] = dby / th;
        displayRect[2] = (w ? w : 640) / tw;
        displayRect[3] = (h ? h : 448) / th;
        vk::toSampled(b, shown->second.image, vk::kFragment);
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
}

GsnScene::GsnScene(const vk::Device& device, VkCommandPool pool) : impl(std::make_unique<Impl>(device, pool)) {}
GsnScene::~GsnScene() = default;

void GsnScene::addFrames(std::vector<gsn::FrameList>& lists) {
    Impl& s = *impl;
    for (auto& list : lists) s.pending.push_back(std::move(list));
    lists.clear();
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
void GsnScene::setScale(uint32_t scale) { impl->scaleSetting = scale; }
void GsnScene::setAnisotropy(int anisotropy) { impl->makeMipSamplers(static_cast<float>(anisotropy)); }

void GsnScene::setTemporal(bool on) { impl->temporal = on; }
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

void GsnScene::prepare(const SceneFormats& formats) {
    Impl& s = *impl;
    vkDeviceWaitIdle(s.device.device);
    vk::destroy(s.device, s.display);
    vk::destroy(s.device, s.stencilPrep);
    s.formats = formats;
    vk::GraphicsDesc desc;
    desc.vertex = shaders::fullscreen_vert();
    desc.fragment = shaders::gsn_display_frag();
    desc.colors = {formats.color, formats.velocity};
    desc.depth = formats.depth;
    desc.samples = formats.samples;
    desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    desc.pushSize = 16;
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
                                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                                        40, "gsn light");
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
                                          80, "gsn reinterpret");
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
    sweepGraveyard();
    markSlot = profiled && profile.on ? static_cast<int>(slot % kSlots) : -1;
    if (markSlot >= 0) profileFrame(command, slot);
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
        if (shown) fieldMode = shownHeight <= 256;
        const uint32_t wantedX = scaleSetting ? scaleSetting
                                              : std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(extent.width / double(shownWidth))), 1, 8);
        const uint32_t wantedY = scaleSetting ? scaleSetting
                                              : std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(extent.height / double(shownHeight))), 1, 8);
        if (wantedX != sx || wantedY != sy) {
            vkDeviceWaitIdle(device.device);
            for (auto& [id, t] : targets) vk::destroy(device, t.image);
            targets.clear();
            sx = wantedX;
            sy = wantedY;
        }
    }
    writeDump();   // a dump recorded in an earlier submission is complete now
    vk::beginLabel(command, "gsn frame");
    for (listIndex = 0; listIndex < pending.size(); ++listIndex) {
        std::swap(frame, pending[listIndex]);
        {
            std::vector<uint32_t> created;
            for (const gsn::Target& t : frame.targets) {
                const bool existed = targets.count(t.id) && targets[t.id].image.valid();
                const VkImage before = existed ? targets[t.id].image.image : VK_NULL_HANDLE;
                ensureTarget(t);
                if (!existed || targets[t.id].image.image != before) created.push_back(t.id);
            }
            for (uint32_t id : created) clearNew(command, targets[id]);
        }
        mark(command, "new targets");
        uploadTextures(command, slot);
        mark(command, "texture uploads");
        tally("lists");
        execute(command, slot);
        retireTextures();
        static const long dumpAt = std::getenv("GT4_GSN_DUMP") ? std::strtol(std::getenv("GT4_GSN_DUMP"), nullptr, 10) : -1;
        static bool dumped = false;   // the first frame drawn at or after that VBlank
        if (frame.displayed && (frame.dump || (!dumped && dumpAt >= 0 && static_cast<long>(frame.vblank) >= dumpAt))) {
            dumped = dumped || !frame.dump;
            dump.vblank = frame.vblank;
            vk::Barriers b;
            for (auto& [id, t] : targets) {
                if (t.depth || !t.image.valid()) continue;
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
            }
            for (const gsn::Target& t : frame.targets)
                for (size_t k = 0; k < dump.images.size(); ++k)
                    if (dump.images[k].first == t.id) dump.bps[k] = t.bp;
        }
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
    // The executed lists' buffers go back to the queue's vector for reuse; the last one stays in `frame`.
    pending.clear();
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
    // Each list is executed once (its blends would apply again).
    if (s.pending.empty()) return;
    s.executePending(command, view.slot, view.renderExtent, true);
}

bool GsnScene::read(const gsn::ReadRequest& request, std::vector<uint8_t>& out) {
    Impl& s = *impl;
    if (!s.stencilPrep.valid() || !s.blank.valid()) return false;   // not prepared yet (no frame shown)
    vkDeviceWaitIdle(s.device.device);   // the frames in flight are done: slot 0's buffers are free
    vk::Uploader up(s.device, s.pool);
    VkCommandBuffer command = up.command();
    s.executePending(command, 0, s.lastExtent);
    auto it = s.targets.find(request.target);
    if (it == s.targets.end() || !it->second.image.valid() || it->second.depth) {
        up.submit();
        return false;
    }
    vk::Image& image = it->second.image;
    const uint32_t scaleX = it->second.scaleX, scaleY = it->second.scaleY;
    const uint32_t x0 = std::min(request.x * scaleX, image.extent.width), y0 = std::min(request.y * scaleY, image.extent.height);
    const uint32_t w = std::min(request.width * scaleX, image.extent.width - x0), h = std::min(request.height * scaleY, image.extent.height - y0);
    vk::Buffer pixels = vk::createBuffer(s.device, std::max<VkDeviceSize>(VkDeviceSize(w) * h * 8, 8), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
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
    up.submit();
    // Each GS pixel is the render pixel at its own sample point (j / scale = x: the first of its sx x sy),
    // stored as colour / 255 and alpha / 128 (gsn targets), packed in the target's format as the GS keeps it.
    auto toFloat = [](uint16_t v) {
        const uint32_t sign = (v >> 15) & 1, exponent = (v >> 10) & 31, mantissa = v & 1023;
        const float f = exponent ? std::ldexp(static_cast<float>(mantissa | 1024), static_cast<int>(exponent) - 25)
                                 : std::ldexp(static_cast<float>(mantissa), -24);
        return sign ? -f : f;
    };
    const auto* half = static_cast<const uint16_t*>(pixels.mapped);
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
    vk::destroy(s.device, pixels);
    return true;
}

void GsnScene::recordMain(VkCommandBuffer command, const SceneView& view) {
    Impl& s = *impl;
    auto shown = s.targets.find(s.displayTarget);
    if (shown == s.targets.end() || !s.display.valid()) return;
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(view.renderExtent.width), static_cast<float>(view.renderExtent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, view.renderExtent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vk::bind(command, s.display);
    vk::Descriptors().sampled(0, shown->second.image.view, s.samplers[2]).push(command, s.display);
    vk::pushConstants(command, s.display, s.displayRect, 16);
    vkCmdDraw(command, 3, 1, 0, 0);
}

SceneCamera GsnScene::camera() const {
    SceneCamera camera;
    camera.previousViewProjection = camera.projection * camera.view;
    return camera;
}

}  // namespace gt4
