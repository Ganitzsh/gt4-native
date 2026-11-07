#include "pipeline.h"

#include "../gs/cpu_profile.h"
#include "fsr2.h"
#include "render_graph.h"
#include "gt4_shaders.h"
#include "smaa_textures.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace gt4 {
namespace {

constexpr VkFormat kHdr = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr float kExposureMinLog2 = -12.0f;
constexpr float kExposureRangeLog2 = 28.0f;
constexpr uint32_t kMaxBloomMips = 8;

uint32_t roundSize(float value) { return std::max(1u, static_cast<uint32_t>(std::lround(value))); }

bool sameExtent(VkExtent2D a, VkExtent2D b) { return a.width == b.width && a.height == b.height; }

int tonemapperIndex(Tonemapper t) {
    switch (t) {
    case Tonemapper::Original: return 0;
    case Tonemapper::Reinhard: return 1;
    case Tonemapper::Aces: return 2;
    case Tonemapper::AgX: return 3;
    case Tonemapper::AgXPunchy: return 4;
    case Tonemapper::PbrNeutral: return 5;
    }
    return 0;
}

bool enabled(EffectQuality q) { return q != EffectQuality::Off && q != EffectQuality::Original; }

int level(EffectQuality q) {  // 0 low .. 3 ultra
    switch (q) {
    case EffectQuality::Low: return 0;
    case EffectQuality::Medium: return 1;
    case EffectQuality::High: return 2;
    case EffectQuality::Ultra: return 3;
    default: return 0;
    }
}

// The path through the pipeline that the settings select (what really runs, after device limits).
struct Config {
    VkExtent2D swapchain{};
    VkRect2D content{};
    VkExtent2D render{}, output{}, post{};
    uint32_t msaa = 1;
    bool fsr2 = false, taa = false;
    AntiAliasing spatialAa = AntiAliasing::Off;
    Upscaler upscaler = Upscaler::Bilinear;
    bool temporal() const { return fsr2 || taa; }
    bool operator==(const Config& o) const {
        return sameExtent(swapchain, o.swapchain) && sameExtent(render, o.render) && sameExtent(output, o.output) &&
               msaa == o.msaa && fsr2 == o.fsr2 && taa == o.taa && spatialAa == o.spatialAa &&
               content.offset.x == o.content.offset.x && content.offset.y == o.content.offset.y;
    }
};

}  // namespace

struct FramePipeline::Impl {
    const vk::Device& device;
    VkCommandPool pool;
    Settings settings;
    Config config;
    bool configured = false;
    Fsr2 fsr2;
    bool fsr2Failed = false;

    // Samplers.
    VkSampler linearClamp = VK_NULL_HANDLE, pointClamp = VK_NULL_HANDLE, material = VK_NULL_HANDLE;
    float materialBias = 1e9f;
    TextureFilter materialFilter = TextureFilter::Trilinear;
    int materialAnisotropy = -1;

    // Render-resolution targets.
    vk::Image sceneColor, sceneVelocity, sceneDepth, msColor, msVelocity, msDepth, depthResolved;
    vk::Image exposure;
    vk::Buffer histogram, exposureState;
    // Output-resolution targets.
    vk::Image fsrOutput;
    // The work images, as descriptions only (format, extent, mips; no VkImage): the render graph creates each as a
    // transient in the frames that use it (handle()). The TAA history and SMAA T2x's two frames are graph histories.
    vk::Image hdrA, hdrB, dofHalf, dofBlur, tiles, neighbourMax, bloomDown, bloomUp, ldrA, ldrB;
    vk::Image smaaEdges, smaaWeights;
    vk::Image outA, outB;
    vk::Image areaTexture, searchTexture;

    vk::Pipeline resolve, histogramPass, adapt, taa, bloomDownPass, bloomUpPass, dofPrepare, dofGather, dofComposite,
        tileMax, neighbourMaxPass, motionBlur, tonemap, fxaa, smaaResolve, resample, easu, rcas;
    vk::Pipeline smaaEdge, smaaWeight, smaaBlend, finalPass;
    VkFormat outputFormat = VK_FORMAT_UNDEFINED;
    bool srgbOutput = false;
    uint32_t outputBits = 8;
    float hdrPaperWhite = 0.0f, hdrPeak = 0.0f;   // HDR10 output (setHdr); 0: SDR

    // Frame state.
    uint32_t frameIndex = 0;
    bool resetPending = true;
    bool exposureReset = true;
    bool gameImage = false;   // the scene is the game's displayed frame (SceneSource::drawsOriginalEffects)
    Vec2 jitter;
    SceneFormats sceneFormats;
    bool scenePrepared = false;
    // The display pipeline's render graph (ticket B3, docs/modern/CONTRACTS.md b.9), made on first use.
    std::unique_ptr<rg::Graph> graph;
    uint32_t frameSlot = 0;
    VkDeviceSize reportedTransientBytes = ~VkDeviceSize(0);   // the graph transients' memory last printed
    VkDeviceSize separateBytes = 0;   // the work images and the TAA / SMAA frames as images of their own (before the graph)
    VkDeviceSize historyBytes = 0;    // the graph histories (TAA, SMAA T2x): kept as before, not aliased

    Impl(const vk::Device& d, VkCommandPool p) : device(d), pool(p), fsr2(d) {
        linearClamp = vk::createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        pointClamp = vk::createSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        createPipelines();
        createLookupTextures();
        histogram = vk::createBuffer(device, 256 * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        exposureState = vk::createBuffer(device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        vk::ImageDesc desc;
        desc.format = VK_FORMAT_R32_SFLOAT;
        desc.extent = {1, 1};
        desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.name = "exposure";
        exposure = vk::createImage(device, desc);
    }

    ~Impl() {
        vkDeviceWaitIdle(device.device);
        graph.reset();   // (its images and memory, before the device)
        fsr2.destroy();
        releaseTargets();
        for (vk::Pipeline* p : {&resolve, &histogramPass, &adapt, &taa, &bloomDownPass, &bloomUpPass, &dofPrepare, &dofGather,
                                &dofComposite, &tileMax, &neighbourMaxPass, &motionBlur, &tonemap, &fxaa, &smaaResolve,
                                &resample, &easu, &rcas, &smaaEdge, &smaaWeight, &smaaBlend, &finalPass})
            vk::destroy(device, *p);
        vk::destroy(device, areaTexture);
        vk::destroy(device, searchTexture);
        vk::destroy(device, exposure);
        vk::destroy(device, histogram);
        vk::destroy(device, exposureState);
        for (VkSampler s : {linearClamp, pointClamp, material}) if (s) vkDestroySampler(device.device, s, nullptr);
    }

    void createPipelines() {
        using T = VkDescriptorType;
        const T sampled = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        const T buffer = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        resolve = vk::createCompute(device, shaders::msaa_resolve_comp(), {sampled, sampled, sampled, storage, storage, storage}, 16, "msaa resolve");
        histogramPass = vk::createCompute(device, shaders::exposure_histogram_comp(), {sampled, buffer}, 16, "exposure histogram");
        adapt = vk::createCompute(device, shaders::exposure_adapt_comp(), {buffer, buffer, storage}, 24, "exposure adapt");
        taa = vk::createCompute(device, shaders::taa_comp(), {sampled, sampled, sampled, sampled, sampled, storage}, 32, "taa");
        bloomDownPass = vk::createCompute(device, shaders::bloom_down_comp(), {sampled, storage, sampled}, 24, "bloom down");
        bloomUpPass = vk::createCompute(device, shaders::bloom_up_comp(), {sampled, sampled, storage}, 20, "bloom up");
        dofPrepare = vk::createCompute(device, shaders::dof_prepare_comp(), {sampled, sampled, storage}, 36, "dof prepare");
        dofGather = vk::createCompute(device, shaders::dof_gather_comp(), {sampled, storage}, 16, "dof gather");
        dofComposite = vk::createCompute(device, shaders::dof_composite_comp(), {sampled, sampled, sampled, storage}, 36, "dof composite");
        tileMax = vk::createCompute(device, shaders::mb_tilemax_comp(), {sampled, storage}, 24, "motion blur tiles");
        neighbourMaxPass = vk::createCompute(device, shaders::mb_neighbormax_comp(), {sampled, storage}, 8, "motion blur neighbours");
        motionBlur = vk::createCompute(device, shaders::mb_gather_comp(), {sampled, sampled, sampled, sampled, storage}, 32, "motion blur");
        tonemap = vk::createCompute(device, shaders::tonemap_comp(), {sampled, sampled, sampled, storage}, 24, "tonemap");
        fxaa = vk::createCompute(device, shaders::fxaa_comp(), {sampled, storage}, 20, "fxaa");
        smaaResolve = vk::createCompute(device, shaders::smaa_resolve_comp(), {sampled, sampled, sampled, storage}, 12, "smaa resolve");
        resample = vk::createCompute(device, shaders::resample_comp(), {sampled, storage}, 20, "resample");
        easu = vk::createCompute(device, shaders::easu_comp(), {sampled, storage}, 72, "fsr1 easu");
        rcas = vk::createCompute(device, shaders::rcas_comp(), {sampled, storage}, 24, "fsr1 rcas");
        vk::GraphicsDesc smaa;
        smaa.colors = {VK_FORMAT_R8G8_UNORM};
        smaa.types = {sampled};
        smaa.pushSize = 32;
        smaa.vertex = shaders::smaa_edge_vert();
        smaa.fragment = shaders::smaa_edge_frag();
        smaa.name = "smaa edges";
        smaaEdge = vk::createGraphics(device, smaa);
        smaa.colors = {VK_FORMAT_R8G8B8A8_UNORM};
        smaa.types = {sampled, sampled, sampled};
        smaa.vertex = shaders::smaa_weight_vert();
        smaa.fragment = shaders::smaa_weight_frag();
        smaa.name = "smaa weights";
        smaaWeight = vk::createGraphics(device, smaa);
        smaa.colors = {kHdr};
        smaa.vertex = shaders::smaa_blend_vert();
        smaa.fragment = shaders::smaa_blend_frag();
        smaa.name = "smaa blend";
        smaaBlend = vk::createGraphics(device, smaa);
    }

    void createLookupTextures() {
        size_t areaBytes = 0, searchBytes = 0;
        unsigned areaWidth = 0, areaHeight = 0, searchWidth = 0, searchHeight = 0;
        const unsigned char* area = smaa::areaTexture(areaBytes, areaWidth, areaHeight);
        const unsigned char* search = smaa::searchTexture(searchBytes, searchWidth, searchHeight);
        vk::ImageDesc desc;
        desc.format = VK_FORMAT_R8G8_UNORM;
        desc.extent = {areaWidth, areaHeight};
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.name = "smaa area";
        areaTexture = vk::createImage(device, desc);
        desc.format = VK_FORMAT_R8_UNORM;
        desc.extent = {searchWidth, searchHeight};
        desc.name = "smaa search";
        searchTexture = vk::createImage(device, desc);
        vk::Uploader upload(device, pool);
        vk::Barriers barriers;
        barriers.image(areaTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
        barriers.image(searchTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
        barriers.flush(upload.command());
        upload.copyToImage(areaTexture, area, areaBytes, 0, 0, {areaWidth, areaHeight});
        upload.copyToImage(searchTexture, search, searchBytes, 0, 0, {searchWidth, searchHeight});
        vk::toSampled(barriers, areaTexture, vk::kFragment);
        vk::toSampled(barriers, searchTexture, vk::kFragment);
        barriers.flush(upload.command());
        upload.submit();
    }

    void releaseTargets() {
        for (vk::Image* image : {&sceneColor, &sceneVelocity, &sceneDepth, &msColor, &msVelocity, &msDepth, &depthResolved, &fsrOutput})
            vk::destroy(device, *image);
        for (vk::Image* work : {&hdrA, &hdrB, &dofHalf, &dofBlur, &tiles, &neighbourMax, &bloomDown, &bloomUp, &ldrA, &ldrB,
                                &smaaEdges, &smaaWeights, &outA, &outB})
            *work = vk::Image{};
    }

    vk::Image image(VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, const char* name, uint32_t mips = 1,
                    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
        vk::ImageDesc desc;
        desc.format = format;
        desc.extent = extent;
        desc.usage = usage;
        desc.mips = mips;
        desc.samples = samples;
        desc.name = name;
        return vk::createImage(device, desc);
    }

    // What an image would take with memory of its own (the create info vk::createImage would use), for the memory report.
    VkDeviceSize imageBytes(VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, uint32_t mips = 1) const {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {std::max(1u, extent.width), std::max(1u, extent.height), 1};
        info.mipLevels = mips;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkDeviceImageMemoryRequirements query{};
        query.sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS;
        query.pCreateInfo = &info;
        VkMemoryRequirements2 requirements{};
        requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        vkGetDeviceImageMemoryRequirements(device.device, &query, &requirements);
        return requirements.memoryRequirements.size;
    }

    // A work image's description for the graph (handle()). `separateBytes` adds what it took as an image of its own
    // before the graph (every work image allocated for the whole configuration), for the memory report.
    vk::Image workImage(VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, uint32_t mips = 1) {
        vk::Image d;
        d.format = format;
        d.extent = extent;
        d.mips = mips;
        separateBytes += imageBytes(format, extent, usage, mips);
        return d;
    }

    // `selfResolved`: the scene does its own temporal anti-aliasing (SceneSource::antiAliasesItself).
    Config resolveConfig(const Settings& s, VkExtent2D swapchain, bool motionVectors, bool selfResolved = false) {
        Config c;
        c.swapchain = swapchain;
        const float target = aspectValue(s.aspect);
        uint32_t width = swapchain.width, height = swapchain.height;
        if (target > 0.0f) {
            const float current = static_cast<float>(swapchain.width) / static_cast<float>(std::max(1u, swapchain.height));
            if (current > target) width = roundSize(static_cast<float>(swapchain.height) * target);   // pillarbox
            else height = roundSize(static_cast<float>(swapchain.width) / target);                    // letterbox
        }
        width = std::min(width, swapchain.width);
        height = std::min(height, swapchain.height);
        c.content.offset = {static_cast<int32_t>((swapchain.width - width) / 2), static_cast<int32_t>((swapchain.height - height) / 2)};
        c.content.extent = {width, height};
        c.output = {width, height};
        float ratio = upscaleRatio(s);
        c.upscaler = !motionVectors && s.upscaler == Upscaler::Fsr2 ? Upscaler::Fsr1 : s.upscaler;
        c.fsr2 = c.upscaler == Upscaler::Fsr2 && !fsr2Failed;
        if (c.fsr2 && ratio < 1.0f) ratio = 1.0f;   // FSR 2 does not downsample; supersampling uses the other paths
        AntiAliasing aa = !motionVectors && (s.aa == AntiAliasing::Taa || s.aa == AntiAliasing::SmaaT2x) ? AntiAliasing::Smaa : s.aa;
        if (selfResolved && (s.aa == AntiAliasing::Taa || s.upscaler == Upscaler::Fsr2)) aa = AntiAliasing::Off;
        c.taa = !c.fsr2 && aa == AntiAliasing::Taa;
        c.spatialAa = c.temporal() ? AntiAliasing::Off : aa;
        c.render = {roundSize(static_cast<float>(width) / ratio), roundSize(static_cast<float>(height) / ratio)};
        c.msaa = device.maxSamples(static_cast<uint32_t>(std::max(1, s.msaa)));
        c.post = c.temporal() ? c.output : c.render;
        return c;
    }

    void createTargets(SceneSource& scene) {
        vkDeviceWaitIdle(device.device);
        releaseTargets();
        const auto& c = config;
        std::printf("Pipeline: targets for render %ux%u, output %ux%u%s%s, MSAA %u\n", c.render.width, c.render.height,
                    c.output.width, c.output.height, c.fsr2 ? ", FSR 2" : "", c.taa ? ", TAA" : "", c.msaa);
        const VkImageUsageFlags rw = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        const VkImageUsageFlags attachment = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        sceneColor = image(kHdr, c.render, attachment | VK_IMAGE_USAGE_STORAGE_BIT, "scene colour");
        sceneVelocity = image(VK_FORMAT_R16G16_SFLOAT, c.render, attachment | VK_IMAGE_USAGE_STORAGE_BIT, "scene motion vectors");
        if (c.msaa > 1) {
            const auto samples = static_cast<VkSampleCountFlagBits>(c.msaa);
            msColor = image(kHdr, c.render, attachment, "scene colour msaa", 1, samples);
            msVelocity = image(VK_FORMAT_R16G16_SFLOAT, c.render, attachment, "scene motion vectors msaa", 1, samples);
            msDepth = image(VK_FORMAT_D32_SFLOAT, c.render, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            "scene depth msaa", 1, samples);
            depthResolved = image(VK_FORMAT_R32_SFLOAT, c.render, rw, "scene depth resolved");
        } else {
            sceneDepth = image(VK_FORMAT_D32_SFLOAT, c.render, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               "scene depth");
        }
        // Before the graph: the TAA history's two images and (for SMAA and SMAA T2x) two SMAA frames, beside the work
        // images. Now: the TAA history and SMAA T2x's frames as graph histories (not aliased), nothing for plain SMAA.
        separateBytes = 0;
        historyBytes = 0;
        if (c.taa) {
            separateBytes += 2 * imageBytes(kHdr, c.output, rw);
            historyBytes += 2 * imageBytes(kHdr, c.output, rw);
        }
        if (c.fsr2) {
            fsrOutput = image(kHdr, c.output, rw, "fsr2 output");
            if (!fsr2.valid() || !sameExtent(fsr2.displaySize(), c.output)) {
                if (!fsr2.create(c.output)) {
                    std::fprintf(stderr, "FSR 2 is unavailable on this device; using TAA instead\n");
                    fsr2Failed = true;
                    config = resolveConfig(settings, c.swapchain, scene.hasMotionVectors(), scene.antiAliasesItself());
                    createTargets(scene);
                    return;
                }
            }
        } else {
            fsr2.destroy();
        }
        const VkExtent2D p = c.post;
        const VkExtent2D half{std::max(1u, p.width / 2), std::max(1u, p.height / 2)};
        hdrA = workImage(kHdr, p, rw);
        hdrB = workImage(kHdr, p, rw);
        dofHalf = workImage(kHdr, half, rw);
        dofBlur = workImage(kHdr, half, rw);
        const uint32_t tile = tileSize();
        const VkExtent2D tileCount{(p.width + tile - 1) / tile, (p.height + tile - 1) / tile};
        tiles = workImage(VK_FORMAT_R16G16_SFLOAT, tileCount, rw);
        neighbourMax = workImage(VK_FORMAT_R16G16_SFLOAT, tileCount, rw);
        uint32_t mips = 1;
        for (uint32_t size = std::min(half.width, half.height); size > 8 && mips < kMaxBloomMips; size /= 2) ++mips;
        bloomDown = workImage(kHdr, half, rw, mips);
        bloomUp = workImage(kHdr, half, rw, mips);
        ldrA = workImage(kHdr, p, rw | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        ldrB = workImage(kHdr, p, rw | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        if (c.spatialAa == AntiAliasing::Smaa || c.spatialAa == AntiAliasing::SmaaT2x) {
            smaaEdges = workImage(VK_FORMAT_R8G8_UNORM, p, attachment);
            smaaWeights = workImage(VK_FORMAT_R8G8B8A8_UNORM, p, attachment);
            separateBytes += 2 * imageBytes(kHdr, p, attachment);
            if (c.spatialAa == AntiAliasing::SmaaT2x) historyBytes += 2 * imageBytes(kHdr, p, attachment);
        }
        outA = workImage(kHdr, c.output, rw);
        outB = workImage(kHdr, c.output, rw);
        // Scene pipelines depend on the sample count.
        SceneFormats formats;
        formats.samples = static_cast<VkSampleCountFlagBits>(c.msaa);
        if (!scenePrepared || formats.samples != sceneFormats.samples) {
            scene.prepare(formats);
            sceneFormats = formats;
            scenePrepared = true;
        }
        std::printf("Pipeline: work images and histories %.1f MiB before the render graph; now its transients (below) and %.1f MiB "
                    "of histories\n", separateBytes / 1048576.0, historyBytes / 1048576.0);
        resetPending = true;
    }

    uint32_t tileSize() const { return config.post.height > 1200 ? 32u : 20u; }

    void updateMaterialSampler() {
        // Automatic LOD bias: textures keep output-resolution detail when rendering below it.
        float bias = 0.0f;
        if (settings.autoLodBias) {
            const float ratio = static_cast<float>(config.render.width) / static_cast<float>(std::max(1u, config.output.width));
            bias = std::min(0.0f, std::log2(ratio));
            if (config.fsr2) bias -= 1.0f;          // AMD's FSR 2 recommendation
            else if (config.taa) bias -= 0.5f;      // temporal accumulation resolves the extra detail
        }
        bias += settings.lodBias;
        if (material && bias == materialBias && settings.textureFilter == materialFilter && settings.anisotropy == materialAnisotropy) return;
        if (material) { vkDeviceWaitIdle(device.device); vkDestroySampler(device.device, material, nullptr); }
        const VkFilter filter = settings.textureFilter == TextureFilter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        const VkSamplerMipmapMode mip = settings.textureFilter == TextureFilter::Trilinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        const float anisotropy = settings.textureFilter == TextureFilter::Point ? 1.0f : static_cast<float>(settings.anisotropy);
        material = vk::createSampler(device, filter, mip, VK_SAMPLER_ADDRESS_MODE_REPEAT, anisotropy, bias);
        std::printf("Pipeline: material sampler %s, anisotropy %gx, LOD bias %+.2f\n", name(settings.textureFilter),
                    static_cast<double>(anisotropy), static_cast<double>(bias));
        materialBias = bias;
        materialFilter = settings.textureFilter;
        materialAnisotropy = settings.anisotropy;
    }

    void configure(const Settings& s, VkExtent2D swapchain, SceneSource& scene) {
        settings = s;
        const Config next = resolveConfig(s, swapchain, scene.hasMotionVectors(), scene.antiAliasesItself());
        const bool rebuild = !configured || !(next == config);
        config = next;   // fields that need no new targets (e.g. the spatial upscaler) apply directly
        if (rebuild) {
            createTargets(scene);
            configured = true;
        }
        updateMaterialSampler();
    }

    void prepareOutput(VkFormat format, bool srgb, uint32_t bits) {
        if (finalPass.valid() && format == outputFormat && srgb == srgbOutput) { outputBits = bits; return; }
        vkDeviceWaitIdle(device.device);
        vk::destroy(device, finalPass);
        vk::GraphicsDesc desc;
        desc.vertex = shaders::fullscreen_vert();
        desc.fragment = shaders::final_frag();
        desc.colors = {format};
        desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
        desc.pushSize = 40;
        desc.name = "output";
        finalPass = vk::createGraphics(device, desc);
        outputFormat = format;
        srgbOutput = srgb;
        outputBits = bits;
    }

    SceneView beginFrame(uint32_t slot, uint32_t index) {
        frameIndex = index;
        frameSlot = slot;
        SceneView view;
        view.renderExtent = config.render;
        view.outputExtent = config.output;
        view.aspect = static_cast<float>(config.output.width) / static_cast<float>(std::max(1u, config.output.height));
        view.frameIndex = index;
        view.slot = slot;
        view.materialSampler = material;
        view.lodBias = materialBias;
        view.settings = &settings;
        jitter = Vec2{};
        if (config.fsr2) {
            const int phases = Fsr2::jitterPhaseCount(config.render.width, config.output.width);
            jitter = Fsr2::jitterOffset(static_cast<int>(index % static_cast<uint32_t>(phases)), phases);
        } else if (config.taa) {
            // Halton (2, 3), with more phases when upsampling so every output pixel gets covered.
            const float ratio = static_cast<float>(config.output.width) / static_cast<float>(config.render.width);
            const uint32_t phases = std::clamp(static_cast<uint32_t>(std::ceil(8.0f * ratio * ratio)), 8u, 64u);
            const uint32_t i = index % phases + 1;
            jitter = Vec2{halton(i, 2) - 0.5f, halton(i, 3) - 0.5f};
        } else if (config.spatialAa == AntiAliasing::SmaaT2x) {
            // SMAA T2x sample pattern (Jimenez et al. 2012), y pointing down.
            jitter = (index & 1) ? Vec2{-0.25f, -0.25f} : Vec2{0.25f, 0.25f};
        }
        view.jitterPixels = jitter;
        view.jitterClip = Vec2{2.0f * jitter.x / static_cast<float>(config.render.width),
                               2.0f * jitter.y / static_cast<float>(config.render.height)};
        return view;
    }

    // --- recording helpers ---
    static void viewport(VkCommandBuffer command, VkRect2D rect) {
        VkViewport v{static_cast<float>(rect.offset.x), static_cast<float>(rect.offset.y), static_cast<float>(rect.extent.width),
                     static_cast<float>(rect.extent.height), 0.0f, 1.0f};
        vkCmdSetViewport(command, 0, 1, &v);
        vkCmdSetScissor(command, 0, 1, &rect);
    }

    static VkRenderingAttachmentInfo colorAttachment(VkImageView view, bool clear) {
        VkRenderingAttachmentInfo a{};
        a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        a.imageView = view;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        return a;
    }

    static void beginRendering(VkCommandBuffer command, VkExtent2D extent, const VkRenderingAttachmentInfo* colors,
                               uint32_t count, const VkRenderingAttachmentInfo* depth = nullptr) {
        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea.extent = extent;
        info.layerCount = 1;
        info.colorAttachmentCount = count;
        info.pColorAttachments = colors;
        info.pDepthAttachment = depth;
        vkCmdBeginRendering(command, &info);
    }

    vk::Image& depthImage() { return config.msaa > 1 ? depthResolved : sceneDepth; }

    void renderScene(VkCommandBuffer command, SceneSource& scene, const SceneView& view) {
        {
            GT4_STAGE_SCOPE("render: the scene (game lists)");
            scene.recordPrepass(command, view);
        }
        vk::beginLabel(command, "scene");
        const bool ms = config.msaa > 1;
        vk::Image& color = ms ? msColor : sceneColor;
        vk::Image& velocity = ms ? msVelocity : sceneVelocity;
        vk::Image& depth = ms ? msDepth : sceneDepth;
        vk::Barriers b;
        vk::toColor(b, color, true);
        vk::toColor(b, velocity, true);
        vk::toDepth(b, depth, true);
        b.flush(command);
        VkRenderingAttachmentInfo colors[2] = {colorAttachment(color.view, true), colorAttachment(velocity.view, true)};
        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = depth.view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {0.0f, 0};   // reverse Z: far
        beginRendering(command, config.render, colors, 2, &depthAttachment);
        {
            GT4_STAGE_SCOPE("render: the scene's main pass");
            scene.recordMain(command, view);
        }
        vkCmdEndRendering(command);
        vk::endLabel(command);
        if (ms) {
            vk::beginLabel(command, "msaa resolve");
            vk::toSampled(b, msColor, vk::kCompute);
            vk::toSampled(b, msVelocity, vk::kCompute);
            vk::toSampled(b, msDepth, vk::kCompute);
            vk::toStorage(b, sceneColor, true);
            vk::toStorage(b, sceneVelocity, true);
            vk::toStorage(b, depthResolved, true);
            b.flush(command);
            vk::bind(command, resolve);
            vk::Descriptors().sampled(0, msColor.view, pointClamp).sampled(1, msVelocity.view, pointClamp)
                .sampled(2, msDepth.view, pointClamp).storage(3, sceneColor.view).storage(4, sceneVelocity.view)
                .storage(5, depthResolved.view).push(command, resolve);
            struct { int32_t w, h, samples, pad; } push{static_cast<int32_t>(config.render.width),
                static_cast<int32_t>(config.render.height), static_cast<int32_t>(config.msaa), 0};
            vk::pushConstants(command, resolve, push);
            vk::dispatch2D(command, config.render.width, config.render.height);
            vk::endLabel(command);
        }
        vk::toSampled(b, sceneColor, vk::kCompute);
        vk::toSampled(b, sceneVelocity, vk::kCompute | vk::kFragment);
        vk::toSampled(b, depthImage(), vk::kCompute | vk::kFragment);
        b.flush(command);
    }


    // The display pipeline, through the render graph (ticket B3, docs/modern/CONTRACTS.md b.9).
    void record(VkCommandBuffer command, SceneSource& scene, const SceneView& view, vk::Image& target, Overlay& overlay, float dt) {
        if (recordGraph(command, scene, view, target, overlay, dt)) return;
        // The graph did not compile: a bug in the passes below (its errors were printed). The frame is cleared to black.
        vk::Barriers b;
        vk::toColor(b, target, true);
        b.flush(command);
        VkRenderingAttachmentInfo attachment = colorAttachment(target.view, true);
        attachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        beginRendering(command, target.extent, &attachment, 1);
        vkCmdEndRendering(command);
    }

    // recordGraph: the stages, shaders, push constants and dispatches of the display pipeline as it was before the
    // graph (bit-identical frames, checked against it in B3), with the barriers planned by the graph. The work images
    // (HDR ping-pong, depth of field, motion blur tiles, bloom, LDR, SMAA, output) are transients, aliased by the
    // graph; createTargets keeps only their descriptions. The TAA history and SMAA T2x's frames are graph histories.
    // The scene images, exposure, FSR 2's output and the target stay imported: the scene and FSR 2 synchronise their
    // own images (Opaque), and code outside the graph reads them. False, with nothing recorded, when the graph does
    // not compile.
    struct GraphFrame {
        std::map<const vk::Image*, rg::Texture> textures;   // each image's handle at its current version
        SceneCamera camera{};
        bool reset = false;
    };
    bool isWork(const vk::Image* image) const {
        for (const vk::Image* w : {&hdrA, &hdrB, &dofHalf, &dofBlur, &tiles, &neighbourMax, &bloomDown, &bloomUp, &ldrA, &ldrB, &smaaEdges,
                                   &smaaWeights, &outA, &outB})
            if (image == w) return true;
        return false;
    }
    // The image's handle: a transient like the old image (created by the first pass that writes it) or an import.
    rg::Texture& handle(GraphFrame& f, rg::PassBuilder& b, vk::Image& image) {
        auto it = f.textures.find(&image);
        if (it != f.textures.end()) return it->second;
        rg::Texture t;
        if (isWork(&image)) {
            rg::TextureDesc d;
            d.format = image.format;
            d.size = rg::Size::Absolute;
            d.extent = image.extent;
            d.mips = image.mips;
            d.layers = image.layers;
            d.name = "pipeline work image";
            t = b.create(d);
        } else {
            t = graph->import(image, "pipeline image");
        }
        return f.textures.emplace(&image, t).first->second;
    }
    // Inside a pass: the image the graph gives for it (a transient's own image, or the import).
    static vk::Image& actual(GraphFrame& f, rg::PassContext& c, vk::Image& image) { return c.image(f.textures.at(&image)); }

    bool recordGraph(VkCommandBuffer command, SceneSource& scene, const SceneView& view, vk::Image& target, Overlay& overlay, float dt) {
        using rg::Use;
        using rg::BufferUse;
        constexpr VkPipelineStageFlags2 kC = vk::kCompute, kF = vk::kFragment, kOut = vk::kColorOut;
        constexpr VkPipelineStageFlags2 kAll = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        if (!graph) graph = std::make_unique<rg::Graph>(device);
        rg::FrameInfo info;
        info.slot = frameSlot;
        info.frameIndex = frameIndex;
        info.render = config.render;
        info.output = config.output;
        info.settings = &settings;
        graph->begin(info);
        GraphFrame f;
        auto H = [&](rg::PassBuilder& b, vk::Image& image) -> rg::Texture& { return handle(f, b, image); };
        rg::Buffer histogramBuffer = graph->import(histogram, "exposure histogram");
        rg::Buffer stateBuffer = graph->import(exposureState, "exposure state");

        // The game's frames are already graded for display: no tone mapping, automatic exposure or extra bloom.
        gameImage = scene.drawsOriginalEffects();
        const bool ms = config.msaa > 1;
        std::vector<vk::Image*> sceneImages = ms ? std::vector<vk::Image*>{&msColor, &msVelocity, &msDepth, &sceneColor, &sceneVelocity, &depthResolved}
                                                 : std::vector<vk::Image*>{&sceneColor, &sceneVelocity, &sceneDepth};
        graph->pass("scene", [&](rg::PassBuilder& b) {
            for (vk::Image* image : sceneImages) H(b, *image) = b.use(H(b, *image), Use::Opaque, kAll);
        }, [&](rg::PassContext& c) {
            renderScene(c.command, scene, view);
            f.camera = scene.camera();   // the camera this frame was rendered with
            f.reset = resetPending || f.camera.cut;
            resetPending = false;
        });

        // Exposure.
        const bool resetExposure = exposureReset;
        graph->pass("exposure: clear", [&](rg::PassBuilder& b) {
            histogramBuffer = b.use(histogramBuffer, BufferUse::TransferDst, vk::kTransfer);
            if (resetExposure) stateBuffer = b.use(stateBuffer, BufferUse::TransferDst, vk::kTransfer);
        }, [&](rg::PassContext& c) {
            vk::beginLabel(c.command, "exposure");
            if (resetExposure) vkCmdFillBuffer(c.command, exposureState.buffer, 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(c.command, histogram.buffer, 0, VK_WHOLE_SIZE, 0);
        });
        graph->pass("exposure: histogram", [&](rg::PassBuilder& b) {
            b.use(H(b, sceneColor), Use::Sampled, kC);
            histogramBuffer = b.use(histogramBuffer, BufferUse::StorageReadWrite, kC);
        }, [&](rg::PassContext& c) {
            vk::bind(c.command, histogramPass);
            vk::Descriptors().sampled(0, sceneColor.view, pointClamp).storageBuffer(1, histogram.buffer, 0, VK_WHOLE_SIZE)
                .push(c.command, histogramPass);
            struct { int32_t w, h; float minLog2, range; } push{static_cast<int32_t>(config.render.width),
                static_cast<int32_t>(config.render.height), kExposureMinLog2, kExposureRangeLog2};
            vk::pushConstants(c.command, histogramPass, push);
            vk::dispatch2D(c.command, (config.render.width + 1) / 2, (config.render.height + 1) / 2, 16, 16);
        });
        graph->pass("exposure: adapt", [&](rg::PassBuilder& b) {
            histogramBuffer = b.use(histogramBuffer, BufferUse::StorageRead, kC);
            stateBuffer = b.use(stateBuffer, BufferUse::StorageReadWrite, kC);
            H(b, exposure) = b.use(H(b, exposure), Use::StorageWrite, kC);
        }, [&](rg::PassContext& c) {
            vk::bind(c.command, adapt);
            vk::Descriptors().storageBuffer(0, histogram.buffer, 0, VK_WHOLE_SIZE).storageBuffer(1, exposureState.buffer, 0, VK_WHOLE_SIZE)
                .storage(2, exposure.view).push(c.command, adapt);
            struct { float minLog2, range, dt, compensation, automatic, reset; } adaptPush{kExposureMinLog2, kExposureRangeLog2,
                std::clamp(dt, 0.0f, 0.25f), settings.exposure, settings.autoExposure && !gameImage ? 1.0f : 0.0f, resetExposure ? 1.0f : 0.0f};
            vk::pushConstants(c.command, adapt, adaptPush);
            vkCmdDispatch(c.command, 1, 1, 1);
            exposureReset = false;
            vk::endLabel(c.command);
        });

        // 1. Temporal reconstruction (to output resolution) or the render-resolution image. `hdr` is the image the
        // next stages read: an import (the scene colour, FSR 2's output), a work transient or the TAA history.
        rg::Texture taaPrevious, taaCurrent;
        vk::Image* hdrKey = &sceneColor;   // an image of f.textures, or null for the TAA history
        bool hdrIsHistory = false;
        if (config.fsr2) {
            graph->pass("fsr2", [&](rg::PassBuilder& b) {
                for (vk::Image* image : {&sceneColor, &depthImage(), &sceneVelocity, &exposure, &fsrOutput})
                    H(b, *image) = b.use(H(b, *image), Use::Opaque, kAll);
            }, [&](rg::PassContext& c) {
                vk::beginLabel(c.command, "fsr2");
                Fsr2Inputs in;
                in.color = &sceneColor;
                in.depth = &depthImage();
                in.velocity = &sceneVelocity;
                in.exposure = &exposure;
                in.output = &fsrOutput;
                in.renderSize = config.render;
                in.jitter = jitter;
                in.sharpness = settings.sharpness;
                in.deltaMilliseconds = dt * 1000.0f;
                in.reset = f.reset;
                in.near = f.camera.near;
                in.verticalFov = f.camera.verticalFov;
                in.reactive = scene.reactiveMask();
                in.transparencyAndComposition = scene.transparencyMask();
                fsr2.dispatch(c.command, in);
                vk::endLabel(c.command);
            });
            hdrKey = &fsrOutput;
        } else if (config.taa) {
            rg::TextureDesc d;
            d.format = kHdr;
            d.size = rg::Size::Absolute;
            d.extent = config.output;
            d.name = "taa history";
            const rg::Texture h = graph->history("taa history", d);
            const bool valid = graph->historyValid(h);
            graph->pass("taa", [&](rg::PassBuilder& b) {
                for (vk::Image* image : {&sceneColor, &depthImage(), &sceneVelocity, &exposure}) b.use(H(b, *image), Use::Sampled, kC);
                taaPrevious = b.use(h, Use::Sampled, kC);
                taaCurrent = b.use(h, Use::StorageWrite, kC);
            }, [&, valid](rg::PassContext& c) {
                vk::beginLabel(c.command, "taa");
                vk::bind(c.command, taa);
                vk::Descriptors().sampled(0, sceneColor.view, pointClamp).sampled(1, depthImage().view, pointClamp)
                    .sampled(2, sceneVelocity.view, pointClamp).sampled(3, c.image(taaPrevious).view, linearClamp)
                    .sampled(4, exposure.view, pointClamp).storage(5, c.image(taaCurrent).view).push(c.command, taa);
                // (no previous frame in the history: as a reset)
                struct { float rw, rh, ow, oh, jx, jy, reset, sharpen; } push{static_cast<float>(config.render.width),
                    static_cast<float>(config.render.height), static_cast<float>(config.output.width), static_cast<float>(config.output.height),
                    jitter.x, jitter.y, f.reset || !valid ? 1.0f : 0.0f, 0.0f};
                vk::pushConstants(c.command, taa, push);
                vk::dispatch2D(c.command, config.output.width, config.output.height);
                vk::endLabel(c.command);
            });
            hdrKey = nullptr;
            hdrIsHistory = true;
        }
        // The current HDR image as a handle, and as the physical image inside a pass.
        auto hdrHandle = [&](rg::PassBuilder& b) -> rg::Texture { return hdrIsHistory ? taaCurrent : H(b, *hdrKey); };

        // 2. HDR effects at the post resolution.
        auto pingPong = [&]() -> vk::Image* { return hdrKey == &hdrA ? &hdrB : &hdrA; };
        if (enabled(settings.dof)) {
            vk::Image* out = pingPong();
            depthOfFieldGraph(f, hdrHandle, hdrIsHistory, hdrKey, out);
            hdrKey = out;
            hdrIsHistory = false;
        }
        if (enabled(settings.motionBlur)) {
            vk::Image* out = pingPong();
            motionBlurGraph(f, hdrHandle, hdrIsHistory, hdrKey, out);
            hdrKey = out;
            hdrIsHistory = false;
        }
        const uint32_t bloomLevels = bloomGraph(f, hdrHandle, hdrIsHistory, hdrKey, gameImage);

        // 3. Display transform. Without bloom levels the shader never samples its bloom input (bloomScale 0): the HDR
        // image it already reads is bound there, so no image without contents is read.
        vk::Image* bloomImage = bloomLevels > 1 ? &bloomUp : &bloomDown;
        graph->pass("tonemap", [&](rg::PassBuilder& b) {
            b.use(hdrHandle(b), Use::Sampled, kC);
            if (bloomLevels) b.use(H(b, *bloomImage), Use::Sampled, kC, {0, 1});
            b.use(H(b, exposure), Use::Sampled, kC);
            H(b, ldrA) = b.use(H(b, ldrA), Use::StorageWrite, kC);
        }, [&, isHistory = hdrIsHistory, key = hdrKey, bloomImage, bloomLevels](rg::PassContext& c) {
            vk::Image& hdr = isHistory ? c.image(taaCurrent) : actual(f, c, *key);
            const VkImageView bloomView = bloomLevels ? actual(f, c, *bloomImage).mipViews[0] : hdr.view;
            vk::beginLabel(c.command, "tonemap");
            vk::bind(c.command, tonemap);
            vk::Descriptors().sampled(0, hdr.view, pointClamp).sampled(1, bloomView, linearClamp)
                .sampled(2, exposure.view, pointClamp).storage(3, actual(f, c, ldrA).view).push(c.command, tonemap);
            struct { int32_t w, h, tonemapper; float intensity, scale, additive; } tonePush{static_cast<int32_t>(config.post.width),
                static_cast<int32_t>(config.post.height), gameImage ? 0 : tonemapperIndex(settings.tonemapper), settings.bloomIntensity,
                bloomLevels ? 1.0f / static_cast<float>(bloomLevels) : 0.0f, settings.bloom == EffectQuality::Original ? 1.0f : 0.0f};
            vk::pushConstants(c.command, tonemap, tonePush);
            vk::dispatch2D(c.command, config.post.width, config.post.height);
            vk::endLabel(c.command);
        });
        vk::Image* ldr = &ldrA;

        // 4. Spatial anti-aliasing (render resolution, perceptual space).
        if (config.spatialAa == AntiAliasing::Fxaa) {
            graph->pass("fxaa", [&](rg::PassBuilder& b) {
                b.use(H(b, ldrA), Use::Sampled, kC);
                H(b, ldrB) = b.use(H(b, ldrB), Use::StorageWrite, kC);
            }, [&](rg::PassContext& c) {
                vk::beginLabel(c.command, "fxaa");
                vk::bind(c.command, fxaa);
                vk::Descriptors().sampled(0, actual(f, c, ldrA).view, linearClamp).storage(1, actual(f, c, ldrB).view).push(c.command, fxaa);
                struct { int32_t w, h; float subpixel, threshold, minimum; } push{static_cast<int32_t>(config.post.width),
                    static_cast<int32_t>(config.post.height), 0.75f, 0.166f, 0.0833f};
                vk::pushConstants(c.command, fxaa, push);
                vk::dispatch2D(c.command, config.post.width, config.post.height);
                vk::endLabel(c.command);
            });
            ldr = &ldrB;
        } else if (config.spatialAa == AntiAliasing::Smaa || config.spatialAa == AntiAliasing::SmaaT2x) {
            ldr = smaaGraph(f, ldr);
        }

        // 5. Spatial upscaling (or supersampling downscale) to the output resolution.
        vk::Image* out = ldr;
        if (!sameExtent(config.post, config.output)) {
            graph->pass("upscale", [&](rg::PassBuilder& b) {
                b.use(H(b, *ldr), Use::Sampled, kC);
                H(b, outA) = b.use(H(b, outA), Use::StorageWrite, kC);
            }, [&, input = ldr](rg::PassContext& c) {
                vk::beginLabel(c.command, "upscale");
                const VkImageView source = actual(f, c, *input).view, destination = actual(f, c, outA).view;
                const bool upscaling = config.post.width < config.output.width;
                if (config.upscaler == Upscaler::Fsr1 && upscaling) {
                    vk::bind(c.command, easu);
                    vk::Descriptors().sampled(0, source, linearClamp).storage(1, destination).push(c.command, easu);
                    const float iw = static_cast<float>(config.post.width), ih = static_cast<float>(config.post.height);
                    const float ow = static_cast<float>(config.output.width), oh = static_cast<float>(config.output.height);
                    const float con[16] = {iw / ow, ih / oh, 0.5f * iw / ow - 0.5f, 0.5f * ih / oh - 0.5f,
                                           1.0f / iw, 1.0f / ih, 1.0f / iw, -1.0f / ih,
                                           -1.0f / iw, 2.0f / ih, 1.0f / iw, 2.0f / ih,
                                           0.0f / iw, 4.0f / ih, 0.0f, 0.0f};
                    struct { uint32_t con[16]; int32_t w, h; } push{};
                    std::memcpy(push.con, con, sizeof(con));
                    push.con[14] = push.con[15] = 0;
                    push.w = static_cast<int32_t>(config.output.width);
                    push.h = static_cast<int32_t>(config.output.height);
                    vk::pushConstants(c.command, easu, push);
                } else {
                    vk::bind(c.command, resample);
                    vk::Descriptors().sampled(0, source, linearClamp).storage(1, destination).push(c.command, resample);
                    int mode = 2;
                    if (upscaling) mode = config.upscaler == Upscaler::Bilinear ? 0 : config.upscaler == Upscaler::Bicubic ? 1 : 2;
                    struct { int32_t sw, sh, tw, th, mode; } push{static_cast<int32_t>(config.post.width), static_cast<int32_t>(config.post.height),
                        static_cast<int32_t>(config.output.width), static_cast<int32_t>(config.output.height), mode};
                    vk::pushConstants(c.command, resample, push);
                }
                vk::dispatch2D(c.command, config.output.width, config.output.height);
                vk::endLabel(c.command);
            });
            out = &outA;
        }

        // 6. Sharpening (FSR 2 sharpens inside its own pass).
        if (settings.sharpness > 0.0f && !config.fsr2) {
            vk::Image* sharpened = out == &outA ? &outB : &outA;
            graph->pass("rcas", [&](rg::PassBuilder& b) {
                b.use(H(b, *out), Use::Sampled, kC);
                H(b, *sharpened) = b.use(H(b, *sharpened), Use::StorageWrite, kC);
            }, [&, input = out, sharpened](rg::PassContext& c) {
                vk::beginLabel(c.command, "rcas");
                vk::bind(c.command, rcas);
                vk::Descriptors().sampled(0, actual(f, c, *input).view, pointClamp).storage(1, actual(f, c, *sharpened).view).push(c.command, rcas);
                const float stops = 2.0f - 2.0f * std::clamp(settings.sharpness, 0.0f, 1.0f);
                const float linear = std::exp2(-stops);
                struct { uint32_t con[4]; int32_t w, h; } push{};
                std::memcpy(&push.con[0], &linear, sizeof(float));
                push.w = static_cast<int32_t>(config.output.width);
                push.h = static_cast<int32_t>(config.output.height);
                vk::pushConstants(c.command, rcas, push);
                vk::dispatch2D(c.command, config.output.width, config.output.height);
                vk::endLabel(c.command);
            });
            out = sharpened;
        }

        // 7. Output pass and the 2D layer.
        graph->pass("output", [&](rg::PassBuilder& b) {
            b.use(H(b, *out), Use::Sampled, kF);
            H(b, target) = b.use(H(b, target), Use::ColorWrite, kOut);
        }, [&, input = out](rg::PassContext& c) {
            vk::beginLabel(c.command, "output");
            VkRenderingAttachmentInfo attachment = colorAttachment(target.view, true);
            attachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            beginRendering(c.command, target.extent, &attachment, 1);
            viewport(c.command, config.content);
            vk::bind(c.command, finalPass);
            vk::Descriptors().sampled(0, actual(f, c, *input).view, linearClamp).push(c.command, finalPass);
            struct { float w, h, aberration, vignette, grain, dither, frame, srgb, paperWhite, peak; } push{
                static_cast<float>(config.output.width), static_cast<float>(config.output.height), settings.chromaticAberration,
                settings.vignette, settings.filmGrain, settings.dither ? 1.0f / static_cast<float>((1u << outputBits) - 1u) : 0.0f,
                static_cast<float>(frameIndex % 65536u), srgbOutput ? 1.0f : 0.0f, hdrPaperWhite, hdrPeak};
            vk::pushConstants(c.command, finalPass, push);
            vkCmdDraw(c.command, 3, 1, 0, 0);
            overlay.record(c.command);
            vkCmdEndRendering(c.command);
            vk::endLabel(c.command);
        });

        graph->compile();
        if (!graph->plan().ok()) {
            static bool told = false;
            if (!told)
                for (const std::string& error : graph->plan().errors)
                    std::fprintf(stderr, "render graph: the display pipeline did not compile: %s\n", error.c_str());
            told = true;
            return false;
        }
        // The work images' memory, when the configuration changes: aliased against one allocation each.
        const rg::Plan& plan = graph->plan();
        if (plan.transientBytes != reportedTransientBytes) {
            reportedTransientBytes = plan.transientBytes;
            std::printf("Pipeline: render graph transients %.1f MiB (%.1f MiB without aliasing)\n", plan.transientBytes / 1048576.0,
                        plan.transientBytesWithoutAliasing / 1048576.0);
        }
        graph->execute(command, VK_NULL_HANDLE);
        return true;
    }

    // recordGraph's stages: the same work as depthOfField, motionBlurPass, bloomPass and smaa, as graph passes. `hdrOf`
    // gives the current HDR image's handle in a pass's setup; the camera and reset are read from `f` when the passes run.
    template <class Hdr> void depthOfFieldGraph(GraphFrame& f, Hdr& hdrOf, bool hdrIsHistory, vk::Image* hdrKey, vk::Image* out) {
        using rg::Use;
        constexpr VkPipelineStageFlags2 kC = vk::kCompute;
        auto constants = [this](const SceneCamera& cam) {
            const int q = level(settings.dof);
            const float maxCoc[4] = {8.0f, 12.0f, 16.0f, 24.0f};
            const float sensor = 0.024f;
            const float focal = 0.5f * sensor / std::tan(0.5f * cam.verticalFov);
            struct Push { int32_t w, h; float near, focus, focal, aperture, sensor, maxCoc, autofocus; } push{};
            const VkExtent2D half = dofHalf.extent;
            push.w = static_cast<int32_t>(half.width);
            push.h = static_cast<int32_t>(half.height);
            push.near = cam.near;
            push.focus = std::max(settings.focusDistance, 0.5f);
            push.focal = focal;
            push.aperture = focal / std::max(settings.aperture, 0.5f);
            push.sensor = sensor;
            push.maxCoc = maxCoc[q] * static_cast<float>(half.height) / 540.0f;
            push.autofocus = settings.focusDistance <= 0.0f ? 1.0f : 0.0f;
            return push;
        };
        auto hdrImage = [this, &f, hdrIsHistory, hdrKey](rg::PassContext& c, rg::Texture t) -> vk::Image& {
            return hdrIsHistory ? c.image(t) : actual(f, c, *hdrKey);
        };
        auto input = std::make_shared<rg::Texture>();
        graph->pass("dof: prepare", [&](rg::PassBuilder& b) {
            *input = hdrOf(b);
            b.use(*input, Use::Sampled, kC);
            b.use(handle(f, b, depthImage()), Use::Sampled, kC);
            handle(f, b, dofHalf) = b.use(handle(f, b, dofHalf), Use::StorageWrite, kC);
        }, [this, &f, input, hdrImage, constants](rg::PassContext& c) {
            vk::beginLabel(c.command, "depth of field");
            const auto push = constants(f.camera);
            vk::Image& half = actual(f, c, dofHalf);
            vk::bind(c.command, dofPrepare);
            vk::Descriptors().sampled(0, hdrImage(c, *input).view, linearClamp).sampled(1, depthImage().view, pointClamp)
                .storage(2, half.view).push(c.command, dofPrepare);
            vk::pushConstants(c.command, dofPrepare, push);
            vk::dispatch2D(c.command, half.extent.width, half.extent.height);
        });
        graph->pass("dof: gather", [&](rg::PassBuilder& b) {
            b.use(handle(f, b, dofHalf), Use::Sampled, kC);
            handle(f, b, dofBlur) = b.use(handle(f, b, dofBlur), Use::StorageWrite, kC);
        }, [this, &f, constants](rg::PassContext& c) {
            const auto push = constants(f.camera);
            const float sampleCounts[4] = {16.0f, 32.0f, 64.0f, 96.0f};   // bokeh gather samples per pixel
            vk::Image& half = actual(f, c, dofHalf);
            vk::bind(c.command, dofGather);
            vk::Descriptors().sampled(0, half.view, linearClamp).storage(1, actual(f, c, dofBlur).view).push(c.command, dofGather);
            struct { int32_t w, h; float samples, maxCoc; } gather{push.w, push.h, sampleCounts[level(settings.dof)], push.maxCoc};
            vk::pushConstants(c.command, dofGather, gather);
            vk::dispatch2D(c.command, half.extent.width, half.extent.height);
        });
        graph->pass("dof: composite", [&](rg::PassBuilder& b) {
            b.use(*input, Use::Sampled, kC);
            b.use(handle(f, b, dofBlur), Use::Sampled, kC);
            b.use(handle(f, b, depthImage()), Use::Sampled, kC);
            handle(f, b, *out) = b.use(handle(f, b, *out), Use::StorageWrite, kC);
        }, [this, &f, input, hdrImage, out, constants](rg::PassContext& c) {
            auto push = constants(f.camera);
            vk::bind(c.command, dofComposite);
            vk::Descriptors().sampled(0, hdrImage(c, *input).view, pointClamp).sampled(1, actual(f, c, dofBlur).view, linearClamp)
                .sampled(2, depthImage().view, pointClamp).storage(3, actual(f, c, *out).view).push(c.command, dofComposite);
            push.w = static_cast<int32_t>(config.post.width);
            push.h = static_cast<int32_t>(config.post.height);
            vk::pushConstants(c.command, dofComposite, push);
            vk::dispatch2D(c.command, config.post.width, config.post.height);
            vk::endLabel(c.command);
        });
    }

    template <class Hdr> void motionBlurGraph(GraphFrame& f, Hdr& hdrOf, bool hdrIsHistory, vk::Image* hdrKey, vk::Image* out) {
        using rg::Use;
        constexpr VkPipelineStageFlags2 kC = vk::kCompute;
        auto input = std::make_shared<rg::Texture>();
        auto hdrImage = [this, &f, hdrIsHistory, hdrKey](rg::PassContext& c, rg::Texture t) -> vk::Image& {
            return hdrIsHistory ? c.image(t) : actual(f, c, *hdrKey);
        };
        graph->pass("motion blur: tiles", [&](rg::PassBuilder& b) {
            b.use(handle(f, b, sceneVelocity), Use::Sampled, kC);
            handle(f, b, tiles) = b.use(handle(f, b, tiles), Use::StorageWrite, kC);
        }, [this, &f](rg::PassContext& c) {
            vk::beginLabel(c.command, "motion blur");
            vk::Image& t = actual(f, c, tiles);
            const VkExtent2D count = t.extent;
            vk::bind(c.command, tileMax);
            vk::Descriptors().sampled(0, sceneVelocity.view, pointClamp).storage(1, t.view).push(c.command, tileMax);
            struct { int32_t cw, ch, pw, ph, tile; float shutter; } tilePush{static_cast<int32_t>(count.width), static_cast<int32_t>(count.height),
                static_cast<int32_t>(config.post.width), static_cast<int32_t>(config.post.height), static_cast<int32_t>(tileSize()), settings.shutter};
            vk::pushConstants(c.command, tileMax, tilePush);
            vk::dispatch2D(c.command, count.width, count.height);
        });
        graph->pass("motion blur: neighbours", [&](rg::PassBuilder& b) {
            b.use(handle(f, b, tiles), Use::Sampled, kC);
            handle(f, b, neighbourMax) = b.use(handle(f, b, neighbourMax), Use::StorageWrite, kC);
        }, [this, &f](rg::PassContext& c) {
            vk::Image& t = actual(f, c, tiles);
            const VkExtent2D count = t.extent;
            vk::bind(c.command, neighbourMaxPass);
            vk::Descriptors().sampled(0, t.view, pointClamp).storage(1, actual(f, c, neighbourMax).view).push(c.command, neighbourMaxPass);
            struct { int32_t cw, ch; } neighbourPush{static_cast<int32_t>(count.width), static_cast<int32_t>(count.height)};
            vk::pushConstants(c.command, neighbourMaxPass, neighbourPush);
            vk::dispatch2D(c.command, count.width, count.height);
        });
        graph->pass("motion blur", [&](rg::PassBuilder& b) {
            *input = hdrOf(b);
            b.use(*input, Use::Sampled, kC);
            for (vk::Image* image : {&sceneVelocity, &depthImage(), &neighbourMax}) b.use(handle(f, b, *image), Use::Sampled, kC);
            handle(f, b, *out) = b.use(handle(f, b, *out), Use::StorageWrite, kC);
        }, [this, &f, input, hdrImage, out](rg::PassContext& c) {
            const int samples[4] = {8, 12, 16, 24};
            vk::bind(c.command, motionBlur);
            vk::Descriptors().sampled(0, hdrImage(c, *input).view, linearClamp).sampled(1, sceneVelocity.view, pointClamp)
                .sampled(2, depthImage().view, pointClamp).sampled(3, actual(f, c, neighbourMax).view, pointClamp)
                .storage(4, actual(f, c, *out).view).push(c.command, motionBlur);
            struct { int32_t w, h, tile, samples; float shutter, near, frame, pad; } push{static_cast<int32_t>(config.post.width),
                static_cast<int32_t>(config.post.height), static_cast<int32_t>(tileSize()), samples[level(settings.motionBlur)], settings.shutter,
                f.camera.near, static_cast<float>(frameIndex % 1024u), 0.0f};
            vk::pushConstants(c.command, motionBlur, push);
            vk::dispatch2D(c.command, config.post.width, config.post.height);
            vk::endLabel(c.command);
        });
    }

    // Returns the number of accumulated levels (0 = no bloom), as bloomPass.
    template <class Hdr> uint32_t bloomGraph(GraphFrame& f, Hdr& hdrOf, bool hdrIsHistory, vk::Image* hdrKey, bool gameDrawsOriginal) {
        using rg::Use;
        constexpr VkPipelineStageFlags2 kC = vk::kCompute;
        if (settings.bloom == EffectQuality::Off || gameDrawsOriginal) return 0;
        const bool original = settings.bloom == EffectQuality::Original;
        const uint32_t wanted[4] = {4, 5, 6, 8};
        const uint32_t levels = std::min(bloomDown.mips, original ? 3u : wanted[level(settings.bloom)]);
        auto input = std::make_shared<rg::Texture>();
        for (uint32_t mip = 0; mip < levels; ++mip) {
            graph->pass("bloom: down", [&](rg::PassBuilder& b) {
                if (mip == 0) {
                    *input = hdrOf(b);
                    b.use(*input, Use::Sampled, kC);
                } else {
                    b.use(handle(f, b, bloomDown), Use::Sampled, kC, {mip - 1, 1});
                }
                b.use(handle(f, b, exposure), Use::Sampled, kC);
                handle(f, b, bloomDown) = b.use(handle(f, b, bloomDown), Use::StorageWrite, kC, {mip, 1});
            }, [this, &f, input, hdrIsHistory, hdrKey, mip, original](rg::PassContext& c) {
                if (mip == 0) vk::beginLabel(c.command, "bloom");
                vk::Image& down = actual(f, c, bloomDown);
                const VkExtent2D size{std::max(1u, down.extent.width >> mip), std::max(1u, down.extent.height >> mip)};
                const VkExtent2D source = mip == 0 ? config.post : VkExtent2D{std::max(1u, down.extent.width >> (mip - 1)),
                                                                             std::max(1u, down.extent.height >> (mip - 1))};
                const VkImageView in = mip == 0 ? (hdrIsHistory ? c.image(*input) : actual(f, c, *hdrKey)).view : down.mipViews[mip - 1];
                vk::bind(c.command, bloomDownPass);
                vk::Descriptors().sampled(0, in, linearClamp, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL).storage(1, down.mipViews[mip])
                    .sampled(2, exposure.view, pointClamp).push(c.command, bloomDownPass);
                struct { float tx, ty; int32_t w, h; float first, threshold; } push{1.0f / static_cast<float>(source.width),
                    1.0f / static_cast<float>(source.height), static_cast<int32_t>(size.width), static_cast<int32_t>(size.height),
                    mip == 0 ? 1.0f : 0.0f, original ? 1.0f : 0.0f};
                vk::pushConstants(c.command, bloomDownPass, push);
                vk::dispatch2D(c.command, size.width, size.height);
            });
        }
        for (int mip = static_cast<int>(levels) - 2; mip >= 0; --mip) {
            const auto m = static_cast<uint32_t>(mip);
            const bool fromDown = m + 2 == levels;   // the smallest level comes from the down chain
            graph->pass("bloom: up", [&](rg::PassBuilder& b) {
                b.use(handle(f, b, fromDown ? bloomDown : bloomUp), Use::Sampled, kC, {m + 1, 1});
                b.use(handle(f, b, bloomDown), Use::Sampled, kC, {m, 1});
                handle(f, b, bloomUp) = b.use(handle(f, b, bloomUp), Use::StorageWrite, kC, {m, 1});
            }, [this, &f, m, fromDown](rg::PassContext& c) {
                vk::Image& down = actual(f, c, bloomDown);
                vk::Image& up = actual(f, c, bloomUp);
                const VkExtent2D size{std::max(1u, up.extent.width >> m), std::max(1u, up.extent.height >> m)};
                const VkExtent2D smallerSize{std::max(1u, up.extent.width >> (m + 1)), std::max(1u, up.extent.height >> (m + 1))};
                const VkImageView smaller = fromDown ? down.mipViews[m + 1] : up.mipViews[m + 1];
                vk::bind(c.command, bloomUpPass);
                vk::Descriptors().sampled(0, smaller, linearClamp, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .sampled(1, down.mipViews[m], linearClamp, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .storage(2, up.mipViews[m]).push(c.command, bloomUpPass);
                struct { float tx, ty; int32_t w, h; float radius; } push{1.0f / static_cast<float>(smallerSize.width),
                    1.0f / static_cast<float>(smallerSize.height), static_cast<int32_t>(size.width), static_cast<int32_t>(size.height), 1.0f};
                vk::pushConstants(c.command, bloomUpPass, push);
                vk::dispatch2D(c.command, size.width, size.height);
                if (m == 0) vk::endLabel(c.command);
            });
        }
        if (levels == 1) graph->pass("bloom: end", [](rg::PassBuilder& b) { b.keep(); }, [](rg::PassContext& c) { vk::endLabel(c.command); });
        return levels;
    }

    vk::Image* smaaGraph(GraphFrame& f, vk::Image* ldr) {
        using rg::Use;
        constexpr VkPipelineStageFlags2 kC = vk::kCompute, kF = vk::kFragment, kOut = vk::kColorOut;
        const VkExtent2D p = config.post;
        struct Push { float metrics[4]; float subsample[4]; } push{};
        const float metrics[4] = {1.0f / static_cast<float>(p.width), 1.0f / static_cast<float>(p.height),
                                  static_cast<float>(p.width), static_cast<float>(p.height)};
        std::memcpy(push.metrics, metrics, sizeof(metrics));
        const bool t2x = config.spatialAa == AntiAliasing::SmaaT2x;
        if (t2x) {
            const float index = (frameIndex & 1u) ? 2.0f : 1.0f;
            push.subsample[0] = push.subsample[1] = push.subsample[2] = index;
        }
        const VkRect2D full{{0, 0}, p};
        graph->pass("smaa: edges", [&](rg::PassBuilder& b) {
            b.use(handle(f, b, *ldr), Use::Sampled, kF);
            handle(f, b, smaaEdges) = b.use(handle(f, b, smaaEdges), Use::ColorWrite, kOut);
        }, [this, &f, ldr, push, full, p](rg::PassContext& c) {
            vk::beginLabel(c.command, "smaa");
            VkRenderingAttachmentInfo edges = colorAttachment(actual(f, c, smaaEdges).view, true);
            beginRendering(c.command, p, &edges, 1);
            viewport(c.command, full);
            vk::bind(c.command, smaaEdge);
            vk::Descriptors().sampled(0, actual(f, c, *ldr).view, linearClamp).push(c.command, smaaEdge);
            vk::pushConstants(c.command, smaaEdge, push);
            vkCmdDraw(c.command, 3, 1, 0, 0);
            vkCmdEndRendering(c.command);
        });
        graph->pass("smaa: weights", [&](rg::PassBuilder& b) {
            for (vk::Image* image : {&smaaEdges, &areaTexture, &searchTexture}) b.use(handle(f, b, *image), Use::Sampled, kF);
            handle(f, b, smaaWeights) = b.use(handle(f, b, smaaWeights), Use::ColorWrite, kOut);
        }, [this, &f, push, full, p](rg::PassContext& c) {
            VkRenderingAttachmentInfo weights = colorAttachment(actual(f, c, smaaWeights).view, true);
            beginRendering(c.command, p, &weights, 1);
            viewport(c.command, full);
            vk::bind(c.command, smaaWeight);
            vk::Descriptors().sampled(0, actual(f, c, smaaEdges).view, linearClamp).sampled(1, areaTexture.view, linearClamp)
                .sampled(2, searchTexture.view, pointClamp).push(c.command, smaaWeight);
            vk::pushConstants(c.command, smaaWeight, push);
            vkCmdDraw(c.command, 3, 1, 0, 0);
            vkCmdEndRendering(c.command);
        });
        if (!t2x) {
            graph->pass("smaa: blend", [&](rg::PassBuilder& b) {
                for (vk::Image* image : {ldr, &smaaWeights, &sceneVelocity}) b.use(handle(f, b, *image), Use::Sampled, kF);
                handle(f, b, ldrB) = b.use(handle(f, b, ldrB), Use::ColorWrite, kOut);
            }, [this, &f, ldr, push, full, p](rg::PassContext& c) {
                VkRenderingAttachmentInfo blend = colorAttachment(actual(f, c, ldrB).view, false);
                beginRendering(c.command, p, &blend, 1);
                viewport(c.command, full);
                vk::bind(c.command, smaaBlend);
                vk::Descriptors().sampled(0, actual(f, c, *ldr).view, linearClamp).sampled(1, actual(f, c, smaaWeights).view, linearClamp)
                    .sampled(2, sceneVelocity.view, pointClamp).push(c.command, smaaBlend);
                vk::pushConstants(c.command, smaaBlend, push);
                vkCmdDraw(c.command, 3, 1, 0, 0);
                vkCmdEndRendering(c.command);
                vk::endLabel(c.command);
            });
            return &ldrB;
        }
        // T2x: this frame's blend goes into a history; its previous copy is the last frame's.
        rg::TextureDesc d;
        d.format = kHdr;
        d.size = rg::Size::Absolute;
        d.extent = p;
        d.name = "smaa t2x frames";
        const rg::Texture frames = graph->history("smaa t2x frames", d);
        const bool none = !graph->historyValid(frames);
        auto blended = std::make_shared<rg::Texture>();
        auto previous = std::make_shared<rg::Texture>(frames);
        graph->pass("smaa: blend", [&](rg::PassBuilder& b) {
            for (vk::Image* image : {ldr, &smaaWeights, &sceneVelocity}) b.use(handle(f, b, *image), Use::Sampled, kF);
            *blended = b.use(frames, Use::ColorWrite, kOut);
        }, [this, &f, ldr, blended, push, full, p](rg::PassContext& c) {
            VkRenderingAttachmentInfo blend = colorAttachment(c.image(*blended).view, false);
            beginRendering(c.command, p, &blend, 1);
            viewport(c.command, full);
            vk::bind(c.command, smaaBlend);
            vk::Descriptors().sampled(0, actual(f, c, *ldr).view, linearClamp).sampled(1, actual(f, c, smaaWeights).view, linearClamp)
                .sampled(2, sceneVelocity.view, pointClamp).push(c.command, smaaBlend);
            vk::pushConstants(c.command, smaaBlend, push);
            vkCmdDraw(c.command, 3, 1, 0, 0);
            vkCmdEndRendering(c.command);
        });
        graph->pass("smaa: resolve", [&](rg::PassBuilder& b) {
            b.use(*blended, Use::Sampled, kC);
            b.use(*previous, Use::Sampled, kC);
            b.use(handle(f, b, sceneVelocity), Use::Sampled, kC);
            handle(f, b, ldrB) = b.use(handle(f, b, ldrB), Use::StorageWrite, kC);
        }, [this, &f, blended, previous, p, none](rg::PassContext& c) {
            const bool first = f.reset || none;
            vk::bind(c.command, smaaResolve);
            vk::Descriptors().sampled(0, c.image(*blended).view, pointClamp).sampled(1, c.image(*previous).view, linearClamp)
                .sampled(2, sceneVelocity.view, pointClamp).storage(3, actual(f, c, ldrB).view).push(c.command, smaaResolve);
            struct { int32_t w, h; float reset; } resolvePush{static_cast<int32_t>(p.width), static_cast<int32_t>(p.height), first ? 1.0f : 0.0f};
            vk::pushConstants(c.command, smaaResolve, resolvePush);
            vk::dispatch2D(c.command, p.width, p.height);
            vk::endLabel(c.command);
        });
        return &ldrB;
    }

    std::string describe() const {
        std::ostringstream out;
        const bool upscaling = config.render.width < config.output.width;
        const bool downsampling = config.render.width > config.output.width;
        std::string path;
        if (config.fsr2) path = upscaling ? "FSR 2" : "FSR 2 native AA";
        else if (config.taa) path = upscaling ? "TAAU" : downsampling ? "TAA, supersampled" : "TAA";
        else if (downsampling) path = "supersampled, Lanczos downsample";
        else if (upscaling) path = config.upscaler == Upscaler::Fsr1 ? std::string("FSR 1 EASU") : std::string(name(config.upscaler));
        else path = "native";
        out << "render " << config.render.width << "x" << config.render.height << " -> output " << config.output.width << "x"
            << config.output.height << " (" << path << ")";
        if (config.msaa > 1) out << " MSAA " << config.msaa << "x";
        if (!config.temporal() && config.spatialAa != AntiAliasing::Off) out << " AA " << name(config.spatialAa);
        if (fsr2Failed) out << " [FSR 2 unavailable]";
        return out.str();
    }
};

FramePipeline::FramePipeline(const vk::Device& device, VkCommandPool pool) : impl(std::make_unique<Impl>(device, pool)) {}
FramePipeline::~FramePipeline() = default;
void FramePipeline::configure(const Settings& settings, VkExtent2D swapchain, SceneSource& scene) { impl->configure(settings, swapchain, scene); }
void FramePipeline::prepareOutput(VkFormat format, bool srgb, uint32_t bits) { impl->prepareOutput(format, srgb, bits); }
void FramePipeline::setHdr(float paperWhite, float peak) {
    impl->hdrPaperWhite = paperWhite;
    impl->hdrPeak = peak;
}
SceneView FramePipeline::beginFrame(uint32_t slot, uint32_t frameIndex) { return impl->beginFrame(slot, frameIndex); }
void FramePipeline::record(VkCommandBuffer command, SceneSource& scene, const SceneView& view, vk::Image& target,
                           Overlay& overlay, float dt) { impl->record(command, scene, view, target, overlay, dt); }
void FramePipeline::resetHistory() { impl->resetPending = true; impl->exposureReset = true; }
VkRect2D FramePipeline::contentRect() const { return impl->config.content; }
VkExtent2D FramePipeline::renderExtent() const { return impl->config.render; }
VkExtent2D FramePipeline::outputExtent() const { return impl->config.output; }
std::string FramePipeline::describe() const { return impl->describe(); }

}  // namespace gt4
