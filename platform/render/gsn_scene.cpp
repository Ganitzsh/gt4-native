// The native renderer's back end (see gsn_scene.h and docs/NATIVE_RENDERER.md).
#include "gsn_scene.h"

#include "gt4_shaders.h"

#include <algorithm>
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
    float scale;
    uint32_t flags;
    float textureSize[2];
    float sourceOffset[2];
    float sourceSize[2];
    uint32_t tex, fogColour, alphaTest, blend;
    uint32_t clampU, clampV;   // WMS/WMT (0-1), MINU/MINV (2-11), MAXU/MAXV (12-21): CLAMP
};
static_assert(sizeof(DrawConstants) == 64);

enum : uint32_t {
    kTextured = 1, kUv = 2, kFog = 4, kBlend = 8, kPabe = 16, kFba = 32, kFromTarget = 64, kTarget24 = 128, kDestAlphaBlend = 256,
    kNearest = 512, kSnap = 1024, kAlphaInvert = 2048, kMipmapped = 4096,
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
    gsn::FrameList frame;
    bool fresh = false, any = false;
    uint32_t scaleSetting = 0, scale = 1;

    struct GpuTarget {
        vk::Image image;
        uint32_t width = 0, height = 0;   // GS pixels
        uint32_t psm = 0;
        bool depth = false;
    };
    std::unordered_map<uint32_t, GpuTarget> targets;
    struct GpuTexture {
        vk::Image image;
        uint32_t version = 0, width = 0, height = 0, levels = 1;
    };
    std::unordered_map<uint32_t, GpuTexture> textures;
    // Retired textures, destroyed once the frames that may still sample them are done.
    std::vector<std::pair<uint64_t, vk::Image>> graveyard;
    uint64_t uploads = 0;
    vk::Image blank;                     // 1 x 1 for untextured draws
    vk::Image feedback;                  // a copy of a target a draw samples while drawing into it
    vk::Image scratchDepth;              // stencil for destination-alpha tests without a Z buffer
    std::array<vk::Buffer, kSlots> vertexBuffers{}, staging{}, clutBuffers{}, indexBuffers{};
    std::vector<vk::Image> reinterpreted;   // per reinterpretation of the frame (gs_frontend.h)
    std::vector<bool> built;
    vk::Pipeline reinterpret;
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
        for (int uv = 0; uv < 4; ++uv) {   // the address mode per axis (WMS, WMT)
            VkSamplerCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            info.magFilter = info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.addressModeU = (uv & 2) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeV = (uv & 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.anisotropyEnable = device.anisotropy ? VK_TRUE : VK_FALSE;
            info.maxAnisotropy = std::min(16.0f, device.maxAnisotropy);
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
        for (auto& i : reinterpreted) vk::destroy(device, i);
        vk::destroy(device, reinterpret);
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

    void ensureTarget(const gsn::Target& t) {
        GpuTarget& g = targets[t.id];
        const uint32_t width = std::max<uint32_t>(64, t.bw * 64), height = std::max<uint32_t>(32, t.height);
        if (g.image.valid() && g.width >= width && g.height >= height && g.image.extent.width == g.width * scale) {
            g.psm = t.psm;
            return;
        }
        if (g.image.valid()) {
            vkDeviceWaitIdle(device.device);   // still sampled by a frame in flight
            vk::destroy(device, g.image);
        }
        g.width = std::max(g.width, width);
        g.height = std::max(g.height, height);
        g.depth = t.depth;
        g.psm = t.psm;
        vk::ImageDesc desc;
        desc.format = t.depth ? kDepth : kColour;
        desc.extent = {g.width * scale, g.height * scale};
        desc.usage = t.depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
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

    vk::Buffer& buffer(std::array<vk::Buffer, kSlots>& ring, uint32_t slot, VkDeviceSize bytes, VkBufferUsageFlags usage) {
        vk::Buffer& b = ring[slot % kSlots];
        if (b.size < bytes) {
            if (b.buffer) {
                vkDeviceWaitIdle(device.device);
                vk::destroy(device, b);
            }
            b = vk::createBuffer(device, std::max<VkDeviceSize>(bytes + bytes / 2, 1 << 20), usage, true);
        }
        return b;
    }

    void uploadTextures(VkCommandBuffer command, uint32_t slot) {
        ++uploads;
        if (frame.resetTextures) {
            for (auto& [id, t] : textures) graveyard.emplace_back(uploads, t.image);
            textures.clear();
        }
        for (auto it = graveyard.begin(); it != graveyard.end();) {
            if (it->first + kSlots + 1 <= uploads) {
                vk::destroy(device, it->second);
                it = graveyard.erase(it);
            } else {
                ++it;
            }
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
                if (g.image.valid()) {
                    vkDeviceWaitIdle(device.device);
                    vk::destroy(device, g.image);
                }
                vk::ImageDesc desc;
                desc.format = VK_FORMAT_R8G8B8A8_UNORM;
                desc.extent = {t.width, t.height};
                desc.mips = t.levels;
                desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                desc.name = "gsn texture";
                g.image = vk::createImage(device, desc);
                g.width = t.width;
                g.height = t.height;
                g.levels = t.levels;
            }
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
        for (const auto& [t, at] : copies) vk::toSampled(b, textures[t->id].image, vk::kFragment);
        b.flush(command);
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

    // Reinterpreted textures: built from their target when a draw first needs them.
    const size_t count = frame.reinterpretations.size();
    built.assign(count, false);
    if (reinterpreted.size() < count) reinterpreted.resize(count);
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
    auto build = [&](uint32_t index) -> vk::Image* {
        const gsn::Reinterpretation& r = frame.reinterpretations[index];
        GpuTarget* source = nullptr;
        if (r.target) {
            auto it = targets.find(r.target);
            if (it == targets.end() || !it->second.image.valid() || it->second.depth) return nullptr;
            source = &it->second;
        }
        vk::Image& out = reinterpreted[index];
        if (!built[index]) {
            end();
            if (!out.valid() || out.extent.width != r.width * scale || out.extent.height != r.height * scale) {
                if (out.valid()) {
                    vkDeviceWaitIdle(device.device);
                    vk::destroy(device, out);
                }
                vk::ImageDesc desc;
                desc.format = VK_FORMAT_R8G8B8A8_UNORM;
                desc.extent = {r.width * scale, r.height * scale};   // scale x scale per GS texel
                desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                desc.name = "gsn reinterpreted";
                out = vk::createImage(device, desc);
            }
            // A palette drawn into a target: read there (gs_frontend.cpp, clutWritten).
            GpuTarget* palette = nullptr;
            if (r.clutTarget) {
                auto it = targets.find(r.clutTarget);
                if (it != targets.end() && it->second.image.valid() && !it->second.depth) palette = &it->second;
            }
            if (source) vk::toSampled(b, source->image, vk::kCompute);
            if (palette && palette != source) vk::toSampled(b, palette->image, vk::kCompute);
            vk::toStorage(b, out, true);
            b.flush(command);
            struct {
                uint32_t targetBp, targetBw, targetHeight, scale, tbp, tbw, psm, width, height, cpsm, csa, texa;
                uint32_t clutTargetBp, clutBw, clutHeight, cbp;
                uint32_t indexStart, pad[3];
            } k{r.targetBp, source ? r.targetBw : 0, source ? source->height : 0, scale, static_cast<uint32_t>(r.tex0 & 0x3FFF),
                static_cast<uint32_t>((r.tex0 >> 14) & 0x3F), static_cast<uint32_t>((r.tex0 >> 20) & 0x3F), r.width, r.height,
                static_cast<uint32_t>((r.tex0 >> 51) & 0xF), static_cast<uint32_t>((r.tex0 >> 56) & 0x1F),
                static_cast<uint32_t>(r.texa & 0xFF) | (static_cast<uint32_t>((r.texa >> 32) & 0xFF) << 8) | (static_cast<uint32_t>((r.texa >> 15) & 1) << 16),
                palette ? r.clutTargetBp : 0, palette ? r.clutTargetBw : 0, palette ? palette->height : 0,
                static_cast<uint32_t>((r.tex0 >> 37) & 0x3FFF), source ? 0 : indexStart[index], {}};
            const VkImageView fallback = source ? source->image.view : palette ? palette->image.view : blank.view;
            vk::bind(command, reinterpret);
            vk::Descriptors()
                .sampled(0, fallback, samplers[0])
                .storage(1, out.view)
                .storageBuffer(2, cluts.buffer, VkDeviceSize(index) * 1024, 1024)
                .sampled(3, palette ? palette->image.view : fallback, samplers[0])
                .storageBuffer(4, indexBuffer.buffer, 0, indexWords * 4)
                .push(command, reinterpret);
            vk::pushConstants(command, reinterpret, k);
            vk::dispatch2D(command, r.width * scale, r.height * scale);
            vk::toSampled(b, out, vk::kFragment);
            b.flush(command);
            built[index] = true;
        }
        return &out;
    };

    // GT4_GSN_LIMIT=K (debugging): only the first K draws of each frame.
    static const long limit = std::getenv("GT4_GSN_LIMIT") ? std::strtol(std::getenv("GT4_GSN_LIMIT"), nullptr, 10) : -1;
    long executed = 0;
    for (const auto& cmd : frame.commands) {
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
        k.scale = static_cast<float>(scale);
        // The texture.
        VkImageView view = blank.view;
        VkSampler sampler = samplers[0];
        if ((prim >> 4) & 1) {
            k.flags |= kTextured;
            if ((prim >> 8) & 1) k.flags |= kUv;
            const uint32_t tw = (s.tex0 >> 26) & 0xF, th = (s.tex0 >> 30) & 0xF;
            k.textureSize[0] = static_cast<float>(1u << std::min(tw, 10u));
            k.textureSize[1] = static_cast<float>(1u << std::min(th, 10u));
            const bool linear = (s.tex1 >> 5) & 1;
            const bool repeat = (s.clamp & 3) == 0;
            if (!linear) k.flags |= kNearest;
            k.clampU = static_cast<uint32_t>(s.clamp & 3) | (static_cast<uint32_t>((s.clamp >> 4) & 0x3FF) << 2) |
                       (static_cast<uint32_t>((s.clamp >> 14) & 0x3FF) << 12);
            k.clampV = static_cast<uint32_t>((s.clamp >> 2) & 3) | (static_cast<uint32_t>((s.clamp >> 24) & 0x3FF) << 2) |
                       (static_cast<uint32_t>((s.clamp >> 34) & 0x3FF) << 12);
            sampler = samplers[(linear ? 2 : 0) + (repeat ? 1 : 0)];
            if (d.texture.kind == gsn::TextureSource::Kind::Target) {
                auto source = targets.find(d.texture.id);
                if (source != targets.end() && source->second.image.valid() && !source->second.depth) {
                    vk::Image* image = &source->second.image;
                    if (d.texture.id == d.target) {   // drawing into what it samples: from a copy
                        end();
                        if (!feedback.valid() || feedback.extent.width < image->extent.width || feedback.extent.height < image->extent.height) {
                            vkDeviceWaitIdle(device.device);
                            vk::destroy(device, feedback);
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
                        k.sourceSize[0] = static_cast<float>(feedback.extent.width) / scale;
                        k.sourceSize[1] = static_cast<float>(feedback.extent.height) / scale;
                    } else {
                        k.sourceSize[0] = static_cast<float>(source->second.image.extent.width) / scale;
                        k.sourceSize[1] = static_cast<float>(source->second.image.extent.height) / scale;
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
                    if (t->second.levels > 1) {   // the GS's mipmaps, sampled as a modern GPU does
                        k.flags = (k.flags & ~kNearest) | kMipmapped;
                        sampler = mipSamplers[((s.clamp & 3) == 0 ? 2 : 0) + (((s.clamp >> 2) & 3) == 0 ? 1 : 0)];
                    }
                }
            } else if (d.texture.kind == gsn::TextureSource::Kind::Reinterpreted && d.texture.id < count) {
                // Bytes of a target read as indices: sampled once per GS pixel, as the effect expects.
                if (vk::Image* image = build(d.texture.id)) view = image->view;
                k.flags |= kSnap;
            }
            k.tex = static_cast<uint32_t>((s.tex0 >> 35) & 3) | (static_cast<uint32_t>((s.tex0 >> 34) & 1) << 2) |
                    (static_cast<uint32_t>(s.texa & 0xFF) << 8) | (static_cast<uint32_t>((s.texa >> 32) & 0xFF) << 16) |
                    (static_cast<uint32_t>((s.texa >> 15) & 1) << 24);
        }
        if ((prim >> 5) & 1) k.flags |= kFog;
        k.fogColour = s.fogcol;
        k.alphaTest = static_cast<uint32_t>(s.test & 0x3FFF);
        if (s.fba) k.flags |= kFba;
        // The blend.
        uint32_t blendKey = 0;
        if ((prim >> 6) & 1) {
            k.flags |= kBlend;
            if (s.pabe) k.flags |= kPabe;
            k.blend = static_cast<uint32_t>(s.alpha & 0xFF) | (static_cast<uint32_t>((s.alpha >> 32) & 0xFF) << 8);
            const uint32_t A = s.alpha & 3, B = (s.alpha >> 2) & 3, C = (s.alpha >> 4) & 3, D = (s.alpha >> 6) & 3;
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
        const uint32_t fbmsk = static_cast<uint32_t>(s.frame >> 32);
        uint32_t mask = 0;
        if ((fbmsk & 0xFF) != 0xFF) mask |= VK_COLOR_COMPONENT_R_BIT;
        if (((fbmsk >> 8) & 0xFF) != 0xFF) mask |= VK_COLOR_COMPONENT_G_BIT;
        if (((fbmsk >> 16) & 0xFF) != 0xFF) mask |= VK_COLOR_COMPONENT_B_BIT;
        if (((fbmsk >> 24) & 0xFF) != 0xFF && ((s.frame >> 24) & 0x3F) != gsn::PSMCT24) mask |= VK_COLOR_COMPONENT_A_BIT;
        // Destination alpha test: through the stencil copy.
        const bool date = (s.test >> 14) & 1;
        uint32_t depthId = d.depth;
        if (date && !depthId) {
            depthId = ~0u;
            if (!scratchDepth.valid() || scratchDepth.extent.width < colourTarget.image.extent.width ||
                scratchDepth.extent.height < colourTarget.image.extent.height) {
                vkDeviceWaitIdle(device.device);
                vk::destroy(device, scratchDepth);
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
        const bool zte = (s.test >> 16) & 1;
        const uint32_t ztst = (s.test >> 17) & 3;
        static const VkCompareOp zops[4] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_GREATER};
        const bool zwrite = hasDepth && zte && d.depth && !((s.zbuf >> 32) & 1);
        vkCmdSetDepthTestEnable(command, hasDepth && zte && d.depth ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(command, zops[ztst]);
        vkCmdSetStencilTestEnable(command, date && hasDepth ? VK_TRUE : VK_FALSE);
        vkCmdSetStencilOp(command, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL);
        vkCmdSetStencilCompareMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
        vkCmdSetStencilWriteMask(command, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
        vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, static_cast<uint32_t>((s.test >> 15) & 1));
        // Scissor, in render pixels.
        const uint32_t x0 = static_cast<uint32_t>(s.scissor & 0x7FF), x1 = static_cast<uint32_t>((s.scissor >> 16) & 0x7FF);
        const uint32_t y0 = static_cast<uint32_t>((s.scissor >> 32) & 0x7FF), y1 = static_cast<uint32_t>((s.scissor >> 48) & 0x7FF);
        VkRect2D scissor{};
        scissor.offset = {static_cast<int32_t>(x0 * scale), static_cast<int32_t>(y0 * scale)};
        const uint32_t right = std::min((x1 + 1) * scale, colourTarget.image.extent.width), bottom = std::min((y1 + 1) * scale, colourTarget.image.extent.height);
        scissor.extent = {right > x0 * scale ? right - x0 * scale : 0, bottom > y0 * scale ? bottom - y0 * scale : 0};
        if (!scissor.extent.width || !scissor.extent.height) continue;
        vkCmdSetScissor(command, 0, 1, &scissor);
        // The alpha test: pixels that pass are drawn as usual; those that fail, under AFAIL, update only the
        // frame buffer (FB_ONLY), only Z (ZB_ONLY) or only RGB (RGB_ONLY), or nothing (KEEP): a second pass
        // with the test inverted.
        const bool ate = s.test & 1;
        const uint32_t atst = (s.test >> 1) & 7, afail = (s.test >> 12) & 3;
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
    // The displayed buffer, for recordMain.
    displayTarget = frame.display.target2 ? frame.display.target2 : frame.display.target1;
    auto shown = targets.find(displayTarget);
    if (shown != targets.end()) {
        const uint64_t dispfb = frame.display.target2 ? frame.display.dispfb2 : frame.display.dispfb1;
        const uint64_t disp = frame.display.target2 ? frame.display.display2 : frame.display.display1;
        const uint32_t dbx = static_cast<uint32_t>((dispfb >> 32) & 0x7FF), dby = static_cast<uint32_t>((dispfb >> 43) & 0x7FF);
        const uint32_t magh = static_cast<uint32_t>((disp >> 23) & 0xF) + 1;
        const uint32_t w = (static_cast<uint32_t>((disp >> 32) & 0xFFF) + 1) / magh;
        uint32_t h = static_cast<uint32_t>((disp >> 44) & 0x7FF) + 1;
        // Interlaced with FFMD = 1 (field mode): each field shows every line of a half-height buffer.
        if ((frame.display.smode2 & 3) == 3) h /= 2;
        const float tw = static_cast<float>(shown->second.image.extent.width) / scale, th = static_cast<float>(shown->second.image.extent.height) / scale;
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
        vk::destroy(device, buffer);
    }
    dump.images.clear();
    dump.extents.clear();
    dump.bps.clear();
}

GsnScene::GsnScene(const vk::Device& device, VkCommandPool pool) : impl(std::make_unique<Impl>(device, pool)) {}
GsnScene::~GsnScene() = default;

void GsnScene::setFrame(gsn::FrameList& list) {
    if (impl->fresh) gsn::carryOver(impl->frame, list);   // replaced before it was drawn
    std::swap(impl->frame, list);
    impl->fresh = true;
    impl->any = true;
}

bool GsnScene::hasFrame() const { return impl->any; }
void GsnScene::setScale(uint32_t scale) { impl->scaleSetting = scale; }

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

void GsnScene::recordPrepass(VkCommandBuffer command, const SceneView& view) {
    Impl& s = *impl;
    if (!s.fresh) return;   // the same frame list is not executed twice (its blends would apply again)
    s.fresh = false;
    const uint32_t wanted = s.scaleSetting ? s.scaleSetting
                                           : std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(view.renderExtent.height / 448.0)), 1, 8);
    if (wanted != s.scale) {
        vkDeviceWaitIdle(s.device.device);
        for (auto& [id, t] : s.targets) vk::destroy(s.device, t.image);
        s.targets.clear();
        s.scale = wanted;
    }
    vk::beginLabel(command, "gsn frame");
    {
        std::vector<uint32_t> created;
        for (const gsn::Target& t : s.frame.targets) {
            const bool existed = s.targets.count(t.id) && s.targets[t.id].image.valid();
            const VkImage before = existed ? s.targets[t.id].image.image : VK_NULL_HANDLE;
            s.ensureTarget(t);
            if (!existed || s.targets[t.id].image.image != before) created.push_back(t.id);
        }
        for (uint32_t id : created) s.clearNew(command, s.targets[id]);
    }
    s.uploadTextures(command, view.slot);
    s.writeDump();   // a dump recorded two frames ago is complete now
    s.execute(command, view.slot);
    s.retireTextures();
    static const long dumpAt = std::getenv("GT4_GSN_DUMP") ? std::strtol(std::getenv("GT4_GSN_DUMP"), nullptr, 10) : -1;
    if (static_cast<long>(s.frame.vblank) == dumpAt) {
        s.dump.vblank = s.frame.vblank;
        vk::Barriers b;
        for (auto& [id, t] : s.targets) {
            if (t.depth || !t.image.valid()) continue;
            vk::Buffer buffer = vk::createBuffer(s.device, VkDeviceSize(t.image.extent.width) * t.image.extent.height * 8,
                                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
            b.image(t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
            b.flush(command);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {t.image.extent.width, t.image.extent.height, 1};
            vkCmdCopyImageToBuffer(command, t.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &copy);
            vk::toSampled(b, t.image, vk::kFragment);
            b.flush(command);
            s.dump.images.emplace_back(id, buffer);
            s.dump.extents.push_back(t.image.extent);
            s.dump.bps.push_back(0);
        }
        for (const gsn::Target& t : s.frame.targets)
            for (size_t k = 0; k < s.dump.images.size(); ++k)
                if (s.dump.images[k].first == t.id) s.dump.bps[k] = t.bp;
    }
    vk::endLabel(command);
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
