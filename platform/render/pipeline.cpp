#include "pipeline.h"

#include "../gs/cpu_profile.h"
#include "display_feature.h"
#include "fsr2.h"
#include "render_graph.h"
#include "gt4_shaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace gt4 {
namespace {

constexpr VkFormat kHdr = VK_FORMAT_R16G16B16A16_SFLOAT;

uint32_t roundSize(float value) { return std::max(1u, static_cast<uint32_t>(std::lround(value))); }

bool sameExtent(VkExtent2D a, VkExtent2D b) { return a.width == b.width && a.height == b.height; }

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
    Settings settings;
    Config config;
    bool configured = false;
    bool fsr2Failed = false;
    // The display stages after the scene pass (ticket B3, docs/modern/CONTRACTS.md b.9): their pipelines and samplers,
    // FSR 2, exposure, the work images and the output pass.
    DisplayFeature display;

    // Samplers.
    VkSampler material = VK_NULL_HANDLE;
    float materialBias = 1e9f;
    TextureFilter materialFilter = TextureFilter::Trilinear;
    int materialAnisotropy = -1;

    // Render-resolution targets.
    vk::Image sceneColor, sceneVelocity, sceneDepth, msColor, msVelocity, msDepth, depthResolved;
    vk::Pipeline resolve;

    // Frame state.
    uint32_t frameIndex = 0;
    bool resetPending = true;
    Vec2 jitter;
    SceneFormats sceneFormats;
    bool scenePrepared = false;
    // The display pipeline's render graph (ticket B3, docs/modern/CONTRACTS.md b.9), made on first use.
    std::unique_ptr<rg::Graph> graph;
    uint32_t frameSlot = 0;

    Impl(const vk::Device& d, VkCommandPool p) : device(d), display(d, p) {
        using T = VkDescriptorType;
        const T sampled = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        resolve = vk::createCompute(device, shaders::msaa_resolve_comp(), {sampled, sampled, sampled, storage, storage, storage}, 16, "msaa resolve");
    }

    ~Impl() {
        vkDeviceWaitIdle(device.device);
        graph.reset();   // (its images and memory, before the device; it imports the display stages' images)
        releaseTargets();
        vk::destroy(device, resolve);
        if (material) vkDestroySampler(device.device, material, nullptr);
    }   // then `display` (FSR 2, its images, pipelines and samplers)

    void releaseTargets() {
        for (vk::Image* image : {&sceneColor, &sceneVelocity, &sceneDepth, &msColor, &msVelocity, &msDepth, &depthResolved})
            vk::destroy(device, *image);
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

    // What the display stages need of the configuration.
    DisplayConfig displayConfig() const {
        DisplayConfig d;
        d.content = config.content;
        d.render = config.render;
        d.output = config.output;
        d.post = config.post;
        d.msaa = config.msaa;
        d.fsr2 = config.fsr2;
        d.taa = config.taa;
        d.spatialAa = config.spatialAa;
        d.upscaler = config.upscaler;
        return d;
    }

    void createTargets(SceneSource& scene) {
        vkDeviceWaitIdle(device.device);
        releaseTargets();
        display.releaseTargets();
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
        // The display stages' targets: FSR 2 and its output, the work images' descriptions.
        if (!display.configure(displayConfig())) {
            fsr2Failed = true;
            config = resolveConfig(settings, c.swapchain, scene.hasMotionVectors(), scene.antiAliasesItself());
            createTargets(scene);
            return;
        }
        // Scene pipelines depend on the sample count.
        SceneFormats formats;
        formats.samples = static_cast<VkSampleCountFlagBits>(c.msaa);
        if (!scenePrepared || formats.samples != sceneFormats.samples) {
            scene.prepare(formats);
            sceneFormats = formats;
            scenePrepared = true;
        }
        display.reportMemory();
        resetPending = true;
    }

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
            const VkSampler pointClamp = display.pointSampler();
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

    // The scene pass, then the display stages (DisplayFeature) through the render graph. The scene images are imports:
    // the scene synchronises them itself (Opaque). The camera and reset are written when the scene pass runs and read
    // by the display passes when they run. False, with nothing recorded, when the graph does not compile.
    bool recordGraph(VkCommandBuffer command, SceneSource& scene, const SceneView& view, vk::Image& target, Overlay& overlay, float dt) {
        using rg::Use;
        constexpr VkPipelineStageFlags2 kAll = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        if (!graph) graph = std::make_unique<rg::Graph>(device);
        rg::FrameInfo info;
        info.slot = frameSlot;
        info.frameIndex = frameIndex;
        info.render = config.render;
        info.output = config.output;
        info.settings = &settings;
        graph->begin(info);

        const bool ms = config.msaa > 1;
        std::vector<vk::Image*> sceneImages = ms ? std::vector<vk::Image*>{&msColor, &msVelocity, &msDepth, &sceneColor, &sceneVelocity, &depthResolved}
                                                 : std::vector<vk::Image*>{&sceneColor, &sceneVelocity, &sceneDepth};
        std::vector<rg::Texture> sceneHandles(sceneImages.size());   // after the scene pass
        DisplaySceneState sceneState;
        graph->pass("scene", [&](rg::PassBuilder& b) {
            for (size_t i = 0; i < sceneImages.size(); ++i)
                sceneHandles[i] = b.use(graph->import(*sceneImages[i], "pipeline image"), Use::Opaque, kAll);
        }, [&](rg::PassContext& c) {
            renderScene(c.command, scene, view);
            sceneState.camera = scene.camera();   // the camera this frame was rendered with
            sceneState.reset = resetPending || sceneState.camera.cut;
            resetPending = false;
        });

        rg::Blackboard board;
        DisplayInputs& in = board.add<DisplayInputs>();
        auto sceneHandle = [&](const vk::Image& image) {
            return sceneHandles[static_cast<size_t>(std::find(sceneImages.begin(), sceneImages.end(), &image) - sceneImages.begin())];
        };
        in.color = &sceneColor;
        in.velocity = &sceneVelocity;
        in.depth = &depthImage();
        in.colorHandle = sceneHandle(sceneColor);
        in.velocityHandle = sceneHandle(sceneVelocity);
        in.depthHandle = sceneHandle(depthImage());
        in.scene = &scene;
        in.sceneState = &sceneState;
        in.target = &target;
        in.overlay = &overlay;
        in.dt = dt;
        in.jitter = jitter;
        in.config = displayConfig();
        in.frameIndex = frameIndex;
        display.build(*graph, board, info);

        graph->compile();
        if (!graph->plan().ok()) {
            static bool told = false;
            if (!told)
                for (const std::string& error : graph->plan().errors)
                    std::fprintf(stderr, "render graph: the display pipeline did not compile: %s\n", error.c_str());
            told = true;
            return false;
        }
        display.reportTransients(graph->plan());
        graph->execute(command, VK_NULL_HANDLE);
        return true;
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
void FramePipeline::prepareOutput(VkFormat format, bool srgb, uint32_t bits) { impl->display.prepareOutput(format, srgb, bits); }
void FramePipeline::setHdr(float paperWhite, float peak) { impl->display.setHdr(paperWhite, peak); }
SceneView FramePipeline::beginFrame(uint32_t slot, uint32_t frameIndex) { return impl->beginFrame(slot, frameIndex); }
void FramePipeline::record(VkCommandBuffer command, SceneSource& scene, const SceneView& view, vk::Image& target,
                           Overlay& overlay, float dt) { impl->record(command, scene, view, target, overlay, dt); }
void FramePipeline::resetHistory() { impl->resetPending = true; impl->display.resetExposure(); }
VkRect2D FramePipeline::contentRect() const { return impl->config.content; }
VkExtent2D FramePipeline::renderExtent() const { return impl->config.render; }
VkExtent2D FramePipeline::outputExtent() const { return impl->config.output; }
std::string FramePipeline::describe() const { return impl->describe(); }

}  // namespace gt4
