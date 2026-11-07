#pragma once
// Small Vulkan helpers for the host renderer: one device, images and buffers with dedicated memory,
// barrier tracking (synchronization2) and pipelines whose descriptors are pushed (VK_KHR_push_descriptor).
#include <volk.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace gt4::vk {

void check(VkResult result, const char* operation);

struct Device {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    float maxAnisotropy = 1.0f;
    VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;  // color & depth framebuffer support
    bool anisotropy = false;
    bool dualSourceBlend = false;   // VkPhysicalDeviceFeatures::dualSrcBlend, enabled when supported
    bool stencilExport = false;     // VK_EXT_shader_stencil_export, enabled when supported (GT4_STENCIL_EXPORT=0: not)
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;   // every pipeline made through it (openPipelineCache)
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;
    uint32_t maxSamples(uint32_t requested) const;
};

class ImageHeap;

struct ImageDesc {
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkExtent2D extent{1, 1};
    VkImageUsageFlags usage = 0;
    uint32_t mips = 1;
    uint32_t layers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool cube = false;
    const char* name = "";
    ImageHeap* heap = nullptr;   // memory carved from the heap's blocks (else its own allocation; also when it does not fit)
    bool mipViews = true;        // a view per mip as well (sampled, storage or colour images with mips)
};

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;         // its own, or a heap's block (heap set)
    ImageHeap* heap = nullptr;
    VkDeviceSize offset = 0;                        // in the heap's block
    VkImageView view = VK_NULL_HANDLE;              // all mips / layers
    // One view per mip (storage writes, per-mip reads). For cube images these are 2D arrays of the 6 faces.
    std::vector<VkImageView> mipViews;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint32_t mips = 1, layers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    // Tracked state of the whole image (all mips are kept in the same layout).
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    bool valid() const { return image != VK_NULL_HANDLE; }
};

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

// Device memory for many small images (a texture stream): ranges of 64 MiB blocks, first fit, merged when freed. One
// allocation per image cost 1-3 ms on the desktop's driver (a burst of 995 new textures: 1.4 s; freeing them: 3 s).
// The render thread's alone (no lock). Destroy every image in it before the heap. Images only, all of them optimal
// tiling (createImage): no linear resource shares a block, so bufferImageGranularity does not apply; each range is
// aligned to its image's own alignment, and each memory type has its own blocks.
class ImageHeap {
public:
    explicit ImageHeap(VkDeviceSize blockSize = 64ull << 20) : blockSize(blockSize) {}
    // False when the image does not fit a block (it gets its own allocation then).
    bool allocate(const Device& device, const VkMemoryRequirements& requirements, uint32_t memoryType,
                  VkDeviceMemory& memory, VkDeviceSize& offset);
    void release(const Device& device, VkDeviceMemory memory, VkDeviceSize offset);
    void destroy(const Device& device);
    size_t blockCount() const { return blocks.size(); }
    // "2 blocks of 64 MiB: free 12.3 MiB (largest 2.0), free 63.9 MiB (largest 63.9); 3 own": fragmentation in a log line
    std::string summary() const;
    uint64_t ownAllocations = 0;   // images that did not fit a block (their own allocation), so far

private:
    struct Block {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint32_t type = 0;
        std::map<VkDeviceSize, VkDeviceSize> free;   // offset -> size
        std::map<VkDeviceSize, VkDeviceSize> used;
    };
    VkDeviceSize blockSize;
    std::vector<Block> blocks;
};

// The pipelines' cache, kept in a file (the host: pipeline_cache.bin beside settings.ini): a pipeline compiled once
// is not compiled again in a later run (6 at a race's first frame took 840 ms on the desktop; more on a handheld,
// whose driver cache may be off under Proton). Data from another GPU or driver is left out (its header). An empty
// path: in memory only.
void openPipelineCache(Device& device, const std::string& path);
void savePipelineCache(const Device& device, const std::string& path);   // (written whole, then renamed over the old)
void closePipelineCache(Device& device);

Image createImage(const Device& device, const ImageDesc& desc);
void destroy(const Device& device, Image& image);
// hostVisible: mapped, coherent; hostCached: also CPU-cached (for buffers the CPU reads back).
Buffer createBuffer(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, bool hostCached = false);
void destroy(const Device& device, Buffer& buffer);
bool hasStencil(VkFormat format);
bool isDepth(VkFormat format);

// Collects image barriers from the tracked state of each image and emits them in one call.
// Read-after-read in the same layout needs no barrier; the stages are merged so a later write waits for all.
class Barriers {
public:
    void image(Image& image, VkImageLayout layout, VkPipelineStageFlags2 stage, VkAccessFlags2 access,
               bool discard = false);
    void flush(VkCommandBuffer command);
private:
    std::vector<VkImageMemoryBarrier2> pending;
};

// Shorthands for the common uses.
constexpr VkPipelineStageFlags2 kCompute = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags2 kFragment = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkPipelineStageFlags2 kColorOut = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr VkPipelineStageFlags2 kDepthTests = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
constexpr VkPipelineStageFlags2 kTransfer = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
void toSampled(Barriers& b, Image& image, VkPipelineStageFlags2 stage = kCompute | kFragment);
void toStorage(Barriers& b, Image& image, bool discard = false);  // compute read/write in GENERAL
void toColor(Barriers& b, Image& image, bool discard = false);
void toDepth(Barriers& b, Image& image, bool discard = false);

struct ShaderCode {
    const uint32_t* words = nullptr;
    size_t bytes = 0;
};

struct Pipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout set = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    uint32_t pushSize = 0;
    VkShaderStageFlags stages = 0;
    bool valid() const { return pipeline != VK_NULL_HANDLE; }
};
void destroy(const Device& device, Pipeline& pipeline);

// Descriptor binding i has type types[i] (a push descriptor set at set 0).
Pipeline createCompute(const Device& device, ShaderCode code, std::initializer_list<VkDescriptorType> types,
                       uint32_t pushSize, const char* name);

enum class Blend : uint8_t { None, Alpha, PremultipliedAlpha, Additive };
struct VertexLayout {
    uint32_t stride = 0;
    std::vector<VkVertexInputAttributeDescription> attributes;
};
struct GraphicsDesc {
    ShaderCode vertex, fragment;               // fragment may be empty (depth-only)
    std::vector<VkFormat> colors;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    Blend blend = Blend::None;
    bool depthTest = false, depthWrite = false;
    VkCompareOp depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;  // reverse Z
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    bool depthBias = false, depthClamp = false;
    VertexLayout vertices;
    std::vector<VkDescriptorType> types;
    uint32_t pushSize = 0;
    const char* name = "";
    // Optional: the primitive topology, a stencil attachment format, blend states replacing `blend` (one per
    // colour attachment), and dynamic states beyond viewport and scissor.
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkFormat stencil = VK_FORMAT_UNDEFINED;
    std::vector<VkPipelineColorBlendAttachmentState> blendStates;
    std::vector<VkDynamicState> dynamics;
};
Pipeline createGraphics(const Device& device, const GraphicsDesc& desc);

// Push-descriptor writer for one dispatch or draw.
class Descriptors {
public:
    Descriptors& sampled(uint32_t binding, VkImageView view, VkSampler sampler,
                         VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Descriptors& texture(uint32_t binding, VkImageView view,
                         VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);  // SAMPLED_IMAGE
    Descriptors& storage(uint32_t binding, VkImageView view);
    Descriptors& sampler(uint32_t binding, VkSampler sampler);
    Descriptors& uniform(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
    Descriptors& storageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
    void push(VkCommandBuffer command, const Pipeline& pipeline);
private:
    static constexpr size_t kMax = 24;
    std::array<VkWriteDescriptorSet, kMax> writes{};
    std::array<VkDescriptorImageInfo, kMax> images{};
    std::array<VkDescriptorBufferInfo, kMax> buffers{};
    uint32_t count = 0;
    VkWriteDescriptorSet& next(uint32_t binding, VkDescriptorType type);
};

void bind(VkCommandBuffer command, const Pipeline& pipeline);
void pushConstants(VkCommandBuffer command, const Pipeline& pipeline, const void* data, uint32_t size);
template <typename T>
void pushConstants(VkCommandBuffer command, const Pipeline& pipeline, const T& data) {
    static_assert(sizeof(T) <= 128, "push constants are limited to 128 bytes");
    pushConstants(command, pipeline, &data, static_cast<uint32_t>(sizeof(T)));
}
// Dispatch enough 8x8 (or given) groups to cover width x height.
void dispatch2D(VkCommandBuffer command, uint32_t width, uint32_t height, uint32_t groupX = 8, uint32_t groupY = 8);

VkSampler createSampler(const Device& device, VkFilter filter, VkSamplerMipmapMode mip, VkSamplerAddressMode address,
                        float anisotropy = 0.0f, float lodBias = 0.0f, bool compare = false);

// One-shot command buffer for uploads at load time.
class Uploader {
public:
    Uploader(const Device& device, VkCommandPool pool);
    ~Uploader();
    Uploader(const Uploader&) = delete;
    Uploader& operator=(const Uploader&) = delete;
    VkCommandBuffer command() const { return cmd; }
    // Copies data into the given mip/layer of an image (TRANSFER_DST layout is handled by the caller).
    void copyToImage(Image& image, const void* data, size_t bytes, uint32_t mip, uint32_t layer, VkExtent2D extent);
    void copyToBuffer(Buffer& buffer, const void* data, size_t bytes);
    void submit();  // waits for completion
private:
    const Device& device;
    VkCommandPool pool;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::vector<Buffer> staging;
};

void label(const Device& device, VkObjectType type, uint64_t handle, const char* name);
void beginLabel(VkCommandBuffer command, const char* name);
void endLabel(VkCommandBuffer command);

}  // namespace gt4::vk
