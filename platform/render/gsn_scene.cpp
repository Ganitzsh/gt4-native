// The native renderer's back end (see gsn_scene.h and docs/NATIVE_RENDERER.md).
#include "gsn_scene.h"

#include "gt4_shaders.h"

#include <algorithm>
#include <bit>
#include <memory>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
};
static_assert(sizeof(DrawConstants) == 72);

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

    struct GpuTarget {
        vk::Image image;
        uint32_t width = 0, height = 0;   // GS pixels
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
    struct Pool {
        std::vector<std::unique_ptr<vk::Image>> images;
        size_t used = 0;
    };
    std::unordered_map<uint64_t, Pool> reinterpretPool;   // reinterpreted textures by size (gs_frontend.h)
    std::vector<vk::Image*> reinterpretImages;            // this frame's, per reinterpretation
    std::vector<bool> built;
    vk::Pipeline reinterpret;
    // Lighting of the game's 3D scenes (docs/LIGHTING.md, GsnScene::setLighting).
    struct Lighting {
        float occlusion = 0.0f;      // 0: no ambient occlusion
        uint32_t directions = 6;
        float grade = 0.0f;          // 0: the game's colours
        float tanX = 0.433f * 4.0f / 3.0f, tanY = 0.433f;
    } lighting;
    vk::Pipeline aoPass, lightPass;
    vk::Image aoImage, litImage;
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
    std::unordered_map<uint32_t, vk::Pipeline> pipelines;
    vk::Pipeline display, stencilPrep;
    VkSampler samplers[4] = {};   // [linear][repeat]
    VkSampler mipSamplers[4] = {};   // [repeat U * 2 + repeat V]: mipmapped textures, trilinear, anisotropic
    uint32_t displayTarget = 0;
    float displayRect[4] = {0, 0, 1, 1};
    bool warnedBlend = false;

    Impl(const vk::Device& d, VkCommandPool p) : device(d), pool(p) {
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
        for (auto& [id, t] : targets) vk::destroy(device, t.image);
        for (auto& [id, t] : textures) vk::destroy(device, t.image);
        for (auto& [when, image] : graveyard) vk::destroy(device, image);
        vk::destroy(device, blank);
        vk::destroy(device, feedback);
        vk::destroy(device, scratchDepth);
        for (auto& b : vertexBuffers) vk::destroy(device, b);
        for (auto& b : staging) vk::destroy(device, b);
        for (auto& b : clutBuffers) vk::destroy(device, b);
        for (auto& b : indexBuffers) vk::destroy(device, b);
        for (auto& [size, pool] : reinterpretPool)
            for (auto& i : pool.images) vk::destroy(device, *i);
        vk::destroy(device, reinterpret);
        vk::destroy(device, aoPass);
        vk::destroy(device, lightPass);
        vk::destroy(device, aoImage);
        vk::destroy(device, litImage);
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
                                    {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(gsn::Vertex, minU)}};
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
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        const bool uv = (k.flags & kUv) != 0;
        for (uint32_t i = 0; i < d.count; ++i) {
            const gsn::Vertex& v = frame.vertices[d.first + i];
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
        // Within Vulkan's guaranteed image size at this scale (a 2048-line target at 8x is 16384 tall).
        const uint32_t width = std::min(std::max<uint32_t>(64, t.bw * 64), 16384 / sx),
                       height = std::min(std::max<uint32_t>(32, t.height), 16384 / sy);
        if (g.image.valid() && g.width >= width && g.height >= height && g.image.extent.width == g.width * sx &&
            g.image.extent.height == g.height * sy) {
            g.psm = t.psm;
            return;
        }
        if (g.image.valid()) retire(g.image);   // still used by frames in flight or earlier lists of this one
        g.width = std::max(g.width, width);
        g.height = std::max(g.height, height);
        g.depth = t.depth;
        g.psm = t.psm;
        vk::ImageDesc desc;
        desc.format = t.depth ? kDepth : kColour;
        desc.extent = {g.width * sx, g.height * sy};
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
    void executePending(VkCommandBuffer command, uint32_t slot, VkExtent2D extent);

    // The GS pixels the display shows: DISPLAY's size over MAGH; interlaced with FFMD = 1 (field mode) each
    // field shows every line of a half-height buffer.
    static void displayedSize(const gsn::Display& d, uint32_t& w, uint32_t& h) {
        const uint64_t disp = d.target2 ? d.display2 : d.display1;
        const uint32_t magh = static_cast<uint32_t>((disp >> 23) & 0xF) + 1;
        w = (static_cast<uint32_t>((disp >> 32) & 0xFFF) + 1) / magh;
        h = static_cast<uint32_t>((disp >> 44) & 0x7FF) + 1;
        if ((d.smode2 & 3) == 3) h /= 2;
        if (!w || !h || w > 1024 || h > 1024) {
            w = 640;
            h = 448;
        }
    }
};

void GsnScene::Impl::execute(VkCommandBuffer command, uint32_t slot) {
    vk::Buffer& vertices = buffer(vertexBuffers, slot, std::max<VkDeviceSize>(frame.vertices.size() * sizeof(gsn::Vertex), 64),
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    std::memcpy(vertices.mapped, frame.vertices.data(), frame.vertices.size() * sizeof(gsn::Vertex));
    vk::Barriers b;
    bool rendering = false;
    uint32_t boundColour = 0, boundDepth = 0;
    bool stencilFresh = false;   // the bound depth image's stencil holds the bound target's alpha bit 7
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    auto end = [&] {
        if (rendering) vkCmdEndRendering(command);
        rendering = false;
        boundPipeline = VK_NULL_HANDLE;
    };
    auto begin = [&](uint32_t colourId, uint32_t depthId) {
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
        rendering = true;
        boundColour = colourId;
        boundDepth = depthId;
        VkViewport viewport{0, 0, static_cast<float>(c.image.extent.width), static_cast<float>(c.image.extent.height), 0, 1};
        vkCmdSetViewport(command, 0, 1, &viewport);
    };
    // The stencil copy of a target's alpha bit 7, for the destination alpha test.
    auto prepareStencil = [&](uint32_t colourId, uint32_t depthId) {
        end();
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
        info.renderArea = {{0, 0}, {std::min(c.image.extent.width, depth.extent.width), std::min(c.image.extent.height, depth.extent.height)}};
        info.layerCount = 1;
        info.pDepthAttachment = &ds;
        info.pStencilAttachment = &stencil;
        vkCmdBeginRendering(command, &info);
        VkViewport viewport{0, 0, static_cast<float>(info.renderArea.extent.width), static_cast<float>(info.renderArea.extent.height), 0, 1};
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
    };

    // Lighting (docs/LIGHTING.md): a 3D scene complete in its target (the front end's Light command) gets its
    // ambient occlusion from its Z buffer and the enhanced grade, computed into litImage and copied back.
    auto relight = [&](const gsn::FrameList::Lighting& l) {
        auto ct = targets.find(l.target), dt = targets.find(l.depth);
        if (ct == targets.end() || dt == targets.end() || ct->second.depth || !dt->second.depth) return;
        vk::Image& colour = ct->second.image;
        vk::Image& depth = dt->second.image;
        if (!colour.valid() || !depth.valid()) return;
        end();
        VkExtent2D e{std::min(colour.extent.width, depth.extent.width), std::min(colour.extent.height, depth.extent.height)};
        if (l.width && l.height) e = {std::min(e.width, l.width * sx), std::min(e.height, l.height * sy)};
        auto ensure = [&](vk::Image& image, VkFormat format, VkImageUsageFlags usage, const char* name) {
            if (image.valid() && image.extent.width >= e.width && image.extent.height >= e.height) return;
            if (image.valid()) retire(image);
            vk::ImageDesc desc;
            desc.format = format;
            desc.extent = {std::max(e.width, image.extent.width), std::max(e.height, image.extent.height)};
            desc.usage = usage;
            desc.name = name;
            image = vk::createImage(device, desc);
        };
        ensure(aoImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "gsn occlusion");
        ensure(litImage, kColour, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "gsn lit");
        const VkImageView depthOnly = depthView(depth);
        constexpr float kZScale = 1.6e6f;
        static const uint32_t lightView = std::getenv("GT4_LIGHT_VIEW") ? static_cast<uint32_t>(std::atoi(std::getenv("GT4_LIGHT_VIEW"))) : 0;   // GT4's 3D Z = 1.6e6 / view distance (24-bit Z; race captures)
        vk::toSampled(b, colour, vk::kCompute);
        vk::toSampled(b, depth, vk::kCompute);
        vk::toStorage(b, aoImage, true);
        b.flush(command);
        if (lighting.occlusion > 0.0f) {
            vk::bind(command, aoPass);
            vk::Descriptors().sampled(0, depthOnly, samplers[0]).storage(1, aoImage.view).push(command, aoPass);
            struct {
                float size[2], tanHalf[2], zScale, radius, intensity, maxPixels;
                uint32_t directions, pad;
            } k{{float(e.width), float(e.height)}, {lighting.tanX, lighting.tanY}, kZScale, 1.2f, 1.4f, 40.0f * float(sy),
                lighting.directions, 0};
            vk::pushConstants(command, aoPass, k);
            vk::dispatch2D(command, e.width, e.height);
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
            uint32_t view;
        } k{{float(e.width), float(e.height)}, lighting.occlusion, kZScale, lighting.grade, 1.18f, 0.3f, 1.0f, lightView};
        vk::pushConstants(command, lightPass, k);
        vk::dispatch2D(command, e.width, e.height);
        b.image(litImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.image(colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        b.flush(command);
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {e.width, e.height, 1};
        vkCmdCopyImage(command, litImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colour.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
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
    auto imageFor = [&](uint32_t index) -> vk::Image& {
        const gsn::Reinterpretation& r = frame.reinterpretations[index];
        Pool& pool = reinterpretPool[(uint64_t(r.width * sx) << 32) | (r.height * sy)];
        if (pool.used == pool.images.size()) {
            vk::ImageDesc desc;
            desc.format = VK_FORMAT_R8G8B8A8_UNORM;
            desc.extent = {r.width * sx, r.height * sy};   // sx x sy per GS texel
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
        std::vector<uint32_t> batch{index};
        std::vector<uint32_t> written;
        for (size_t j = at + 1, seen = 0; j < frame.commands.size() && seen < 256; ++j, ++seen) {
            const auto& cmd = frame.commands[j];
            if (cmd.kind != gsn::FrameList::Command::Kind::Draw) continue;
            const gsn::Draw& d = frame.draws[cmd.index];
            if (d.texture.kind == gsn::TextureSource::Kind::Reinterpreted && d.texture.id < count && !built[d.texture.id] &&
                std::find(batch.begin(), batch.end(), d.texture.id) == batch.end()) {
                const gsn::Reinterpretation& r = frame.reinterpretations[d.texture.id];
                const bool stale = std::find(written.begin(), written.end(), r.target) != written.end() ||
                                   std::find(written.begin(), written.end(), r.clutTarget) != written.end();
                if (!stale && (!r.target || sourceOf(r))) batch.push_back(d.texture.id);
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
            } k{r.targetBp, source ? r.targetBw : 0, source ? source->height : 0, sx, r.texture.base,
                r.texture.width, r.texture.format, r.width, r.height,
                r.texture.clutFormat, r.texture.clutOffset,
                r.texAlpha0 | (static_cast<uint32_t>(r.texAlpha1) << 8) | (static_cast<uint32_t>(r.texBlackTransparent) << 16),
                palette ? r.clutTargetBp : 0, palette ? r.clutTargetBw : 0, palette ? palette->height : 0,
                r.texture.clutBase, source ? 0 : indexStart[i], sy, {}};
            const VkImageView fallback = source ? source->image.view : palette ? palette->image.view : blank.view;
            vk::Descriptors()
                .sampled(0, fallback, samplers[0])
                .storage(1, reinterpretImages[i]->view)
                .storageBuffer(2, cluts.buffer, VkDeviceSize(i) * 1024, 1024)
                .sampled(3, palette ? palette->image.view : fallback, samplers[0])
                .storageBuffer(4, indexBuffer.buffer, 0, indexWords * 4)
                .push(command, reinterpret);
            vk::pushConstants(command, reinterpret, k);
            vk::dispatch2D(command, r.width * sx, r.height * sy);
            built[i] = true;
        }
        for (const uint32_t i : batch) vk::toSampled(b, *reinterpretImages[i], vk::kFragment);
        b.flush(command);
        return reinterpretImages[index];
    };

    // GT4_GSN_LIMIT=K (debugging): only the first K draws of each frame.
    static const long limit = std::getenv("GT4_GSN_LIMIT") ? std::strtol(std::getenv("GT4_GSN_LIMIT"), nullptr, 10) : -1;
    long executed = 0;
    for (size_t at = 0; at < frame.commands.size(); ++at) {
        const auto& cmd = frame.commands[at];
        if (cmd.kind == gsn::FrameList::Command::Kind::Light) {
            if ((lighting.occlusion > 0.0f || lighting.grade > 0.0f) && cmd.index < frame.lights.size()) relight(frame.lights[cmd.index]);
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
        k.targetSize[0] = static_cast<float>(colourTarget.image.extent.width);
        k.targetSize[1] = static_cast<float>(colourTarget.image.extent.height);
        k.scale[0] = static_cast<float>(sx);
        k.scale[1] = static_cast<float>(sy);
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
                        image = &feedback;
                        k.sourceSize[0] = static_cast<float>(feedback.extent.width) / sx;
                        k.sourceSize[1] = static_cast<float>(feedback.extent.height) / sy;
                    } else {
                        k.sourceSize[0] = static_cast<float>(source->second.image.extent.width) / sx;
                        k.sourceSize[1] = static_cast<float>(source->second.image.extent.height) / sy;
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
        if (date && (!stencilFresh || boundColour != d.target || boundDepth != depthId)) {
            prepareStencil(d.target, depthId);
            stencilFresh = true;
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
        // Depth and stencil state.
        const bool zte = c.depthTest;
        const uint32_t ztst = c.depthFunc;
        static const VkCompareOp zops[4] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_GREATER};
        const bool zwrite = hasDepth && zte && d.depth && c.depthWrite;
        vkCmdSetDepthTestEnable(command, hasDepth && zte && d.depth ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(command, zops[ztst]);
        vkCmdSetStencilTestEnable(command, date && hasDepth ? VK_TRUE : VK_FALSE);
        vkCmdSetStencilOp(command, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL);
        vkCmdSetStencilCompareMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
        vkCmdSetStencilWriteMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
        vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, c.destPass);
        // Scissor, in render pixels.
        const uint32_t x0 = c.scissorX0, x1 = c.scissorX1;
        const uint32_t y0 = c.scissorY0, y1 = c.scissorY1;
        VkRect2D scissor{};
        scissor.offset = {static_cast<int32_t>(x0 * sx), static_cast<int32_t>(y0 * sy)};
        const uint32_t right = std::min((x1 + 1) * sx, colourTarget.image.extent.width), bottom = std::min((y1 + 1) * sy, colourTarget.image.extent.height);
        scissor.extent = {right > x0 * sx ? right - x0 * sx : 0, bottom > y0 * sy ? bottom - y0 * sy : 0};
        if (!scissor.extent.width || !scissor.extent.height) continue;
        vkCmdSetScissor(command, 0, 1, &scissor);
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
                vk::bind(command, p);
                const VkDeviceSize zero = 0;
                vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &zero);
                boundPipeline = p.pipeline;
            }
            vkCmdSetDepthWriteEnable(command, ps.zwrite ? VK_TRUE : VK_FALSE);
            DrawConstants pk = k;
            if (ps.inverted) pk.flags |= kAlphaInvert;
            vk::Descriptors().sampled(0, view, sampler).push(command, p);
            vk::pushConstants(command, p, pk);
            vkCmdDraw(command, d.count, 1, d.first, 0);
        }
    }
    end();
    // The displayed buffer, for recordMain (a list cut for a GS -> host read displays nothing).
    if (!frame.displayed) return;
    displayTarget = frame.display.target2 ? frame.display.target2 : frame.display.target1;
    auto shown = targets.find(displayTarget);
    if (shown != targets.end()) {
        const uint64_t dispfb = frame.display.target2 ? frame.display.dispfb2 : frame.display.dispfb1;
        const uint32_t dbx = static_cast<uint32_t>((dispfb >> 32) & 0x7FF), dby = static_cast<uint32_t>((dispfb >> 43) & 0x7FF);
        uint32_t w, h;
        displayedSize(frame.display, w, h);
        const float tw = static_cast<float>(shown->second.image.extent.width) / sx, th = static_cast<float>(shown->second.image.extent.height) / sy;
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
                                        36, "gsn light");
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

void GsnScene::Impl::executePending(VkCommandBuffer command, uint32_t slot, VkExtent2D extent) {
    sweepGraveyard();
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
        uploadTextures(command, slot);
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
    Impl& s = *impl;
    s.lastExtent = view.renderExtent;
    // Each list is executed once (its blends would apply again).
    if (s.pending.empty()) return;
    s.executePending(command, view.slot, view.renderExtent);
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
    const uint32_t x0 = std::min(request.x * s.sx, image.extent.width), y0 = std::min(request.y * s.sy, image.extent.height);
    const uint32_t w = std::min(request.width * s.sx, image.extent.width - x0), h = std::min(request.height * s.sy, image.extent.height - y0);
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
            const uint32_t px = x * s.sx, py = y * s.sy;
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
