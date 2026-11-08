// The render graph (render_graph.h, docs/modern/CONTRACTS.md (b)). compile() is plain logic over the passes'
// declarations (no Vulkan call: tests/render_graph.cpp runs it with a fake backend); realise() and execute() turn
// the plan into images, memory, barriers and timestamps.
#include "render_graph.h"

#include "cpu_profile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string_view>
#include <sstream>

namespace gt4::rg {

// --- Uses ------------------------------------------------------------------------------------------------------

bool writes(Use use) {
    switch (use) {
        case Use::StorageWrite: case Use::StorageReadWrite: case Use::ColorWrite: case Use::ColorReadWrite:
        case Use::DepthWrite: case Use::DepthReadWrite: case Use::TransferDst: case Use::Opaque: return true;
        default: return false;
    }
}
bool reads(Use use) {
    switch (use) {
        case Use::ColorWrite: case Use::DepthWrite: case Use::StorageWrite: case Use::TransferDst: return false;
        default: return true;
    }
}
bool writes(BufferUse use) {
    return use == BufferUse::StorageWrite || use == BufferUse::StorageReadWrite || use == BufferUse::TransferDst;
}
bool reads(BufferUse use) { return use != BufferUse::StorageWrite && use != BufferUse::TransferDst; }

namespace {

constexpr uint32_t kNone = ~0u;
constexpr VkAccessFlags2 kWriteAccess = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

// Uses that may come first on memory with no defined contents (a transient, a history's new copy).
bool discardFirst(Use use) {
    return use == Use::ColorWrite || use == Use::DepthWrite || use == Use::StorageWrite || use == Use::TransferDst;
}

const char* useName(Use use) {
    static const char* names[] = {"Sampled", "StorageRead", "StorageWrite", "StorageReadWrite", "ColorWrite", "ColorReadWrite",
                                  "DepthWrite", "DepthRead", "DepthReadWrite", "TransferSrc", "TransferDst", "Present", "Opaque"};
    return names[static_cast<int>(use)];
}

VkImageLayout layoutFor(Use use) {
    switch (use) {
        case Use::Sampled: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case Use::StorageRead: case Use::StorageWrite: case Use::StorageReadWrite: return VK_IMAGE_LAYOUT_GENERAL;
        case Use::ColorWrite: case Use::ColorReadWrite: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case Use::DepthWrite: case Use::DepthReadWrite: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case Use::DepthRead: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case Use::TransferSrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case Use::TransferDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case Use::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case Use::Opaque: break;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkAccessFlags2 accessFor(Use use) {
    switch (use) {
        case Use::Sampled: return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        case Use::StorageRead: return VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        case Use::StorageWrite: return VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        case Use::StorageReadWrite: return VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        case Use::ColorWrite: return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case Use::ColorReadWrite: return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case Use::DepthWrite: return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case Use::DepthRead: return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case Use::DepthReadWrite: return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case Use::TransferSrc: return VK_ACCESS_2_TRANSFER_READ_BIT;
        case Use::TransferDst: return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case Use::Present: return VK_ACCESS_2_NONE;
        case Use::Opaque: return VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
    return VK_ACCESS_2_NONE;
}

VkImageUsageFlags usageFor(Use use) {
    switch (use) {
        case Use::Sampled: return VK_IMAGE_USAGE_SAMPLED_BIT;
        case Use::StorageRead: case Use::StorageWrite: case Use::StorageReadWrite: return VK_IMAGE_USAGE_STORAGE_BIT;
        case Use::ColorWrite: case Use::ColorReadWrite: return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        case Use::DepthWrite: case Use::DepthRead: case Use::DepthReadWrite: return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        case Use::TransferSrc: return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        case Use::TransferDst: return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        default: return 0;
    }
}

VkAccessFlags2 accessFor(BufferUse use) {
    switch (use) {
        case BufferUse::Uniform: return VK_ACCESS_2_UNIFORM_READ_BIT;
        case BufferUse::StorageRead: return VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        case BufferUse::StorageWrite: return VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        case BufferUse::StorageReadWrite: return VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        case BufferUse::Vertex: return VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        case BufferUse::Index: return VK_ACCESS_2_INDEX_READ_BIT;
        case BufferUse::Indirect: return VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        case BufferUse::TransferSrc: return VK_ACCESS_2_TRANSFER_READ_BIT;
        case BufferUse::TransferDst: return VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    return VK_ACCESS_2_NONE;
}

VkBufferUsageFlags usageFor(BufferUse use) {
    switch (use) {
        case BufferUse::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        case BufferUse::StorageRead: case BufferUse::StorageWrite: case BufferUse::StorageReadWrite: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        case BufferUse::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case BufferUse::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case BufferUse::Indirect: return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        case BufferUse::TransferSrc: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        case BufferUse::TransferDst: return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return 0;
}

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    alignment = std::max<VkDeviceSize>(alignment, 1);
    return (value + alignment - 1) / alignment * alignment;
}

bool overlaps(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a0 <= b1 && b0 <= a1; }

// The Vulkan backend: requirements from the driver without making the image (Vulkan 1.3).
class VulkanBackend final : public Backend {
public:
    explicit VulkanBackend(const vk::Device& device) : device(device) {}
    MemoryNeeds requirements(const ImageInfo& image) override {
        VkImageCreateInfo info = createInfo(image);
        VkDeviceImageMemoryRequirements query{};
        query.sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS;
        query.pCreateInfo = &info;
        VkMemoryRequirements2 out{};
        out.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        vkGetDeviceImageMemoryRequirements(device.device, &query, &out);
        return {out.memoryRequirements.size, out.memoryRequirements.alignment, out.memoryRequirements.memoryTypeBits};
    }
    MemoryNeeds requirements(VkDeviceSize bytes, VkBufferUsageFlags usage) override {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = std::max<VkDeviceSize>(bytes, 4);
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkDeviceBufferMemoryRequirements query{};
        query.sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS;
        query.pCreateInfo = &info;
        VkMemoryRequirements2 out{};
        out.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        vkGetDeviceBufferMemoryRequirements(device.device, &query, &out);
        return {out.memoryRequirements.size, out.memoryRequirements.alignment, out.memoryRequirements.memoryTypeBits};
    }
    uint32_t memoryType(uint32_t typeBits) override { return device.memoryType(typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); }
    // Recorded in order on the graphics queue until async compute is turned on (ticket B21); the plan's edges and
    // lifetimes are made for the queues that run them, so this stays false until then.
    bool asyncCompute() const override { return false; }

    static VkImageCreateInfo createInfo(const ImageInfo& image) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.flags = image.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = image.format;
        info.extent = {std::max(1u, image.extent.width), std::max(1u, image.extent.height), 1};
        info.mipLevels = image.mips;
        info.arrayLayers = image.cube ? 6 : image.layers;
        info.samples = image.samples;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = image.usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return info;
    }

private:
    const vk::Device& device;
};

}  // namespace

// --- Internal records ------------------------------------------------------------------------------------------

struct Resource {
    enum class Kind : uint8_t { Transient, Imported, History };
    Kind kind = Kind::Transient;
    bool buffer = false;
    std::string name;
    TextureDesc texture{};
    BufferDesc bufferDesc{};
    vk::Image* image = nullptr;            // imported texture
    vk::Buffer* importedBuffer = nullptr;
    uint32_t mips = 1;
    bool depth = false;
    uint32_t version = 0;                  // the current version; a write makes version + 1
    std::vector<uint32_t> producers{kNone};   // producers[v]: the pass that made version v (0: none, before the frame)
    VkImageUsageFlags usage = 0;
    VkBufferUsageFlags bufferUsage = 0;
    // An import's state when it was imported (all mips).
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    bool historyPreviousValid = false;     // the history's previous copy was written in an earlier frame
};

struct Graph::Use_ {
    uint32_t resource = 0;
    bool buffer = false;
    Use use = Use::Sampled;
    BufferUse bufferUse = BufferUse::Uniform;
    VkPipelineStageFlags2 stages = 0;
    Range range{};
    uint32_t versionIn = 0, versionOut = 0;   // versionOut: 0 when it does not write
};

struct Graph::PassNode {
    const char* name = "";
    std::function<void(PassContext&)> execute;
    Queue queue = Queue::Graphics;
    bool keep = false;
    std::vector<Use_> uses;
    bool live = false;
    uint32_t planIndex = kNone;
    std::vector<Barrier> merged;   // read after read: no barrier; these stages and accesses join the tracked state
};

// Vulkan objects kept across frames.
struct Graph::Physical {
    struct Arena {
        uint32_t memoryType = 0;
        bool buffer = false;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
    };
    struct CachedImage {
        ImageInfo info;
        uint32_t arena = 0;
        VkDeviceSize offset = 0;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        vk::Image image;
        bool used = false;
    };
    struct CachedBuffer {
        VkDeviceSize bytes = 0;
        VkBufferUsageFlags usage = 0;
        uint32_t arena = 0;
        VkDeviceSize offset = 0;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        bool used = false;
    };
    struct Slot {
        std::vector<Arena> arenas;
        std::vector<std::unique_ptr<CachedImage>> images;   // (stable: the frame holds pointers to them)
        std::vector<CachedBuffer> buffers;
        vk::Buffer ring;                   // host-visible buffers
        VkDeviceSize ringUsed = 0;
        VkQueryPool queries = VK_NULL_HANDLE;
        uint32_t queryCapacity = 0;
        std::vector<std::string> names;    // the passes timed in the last execution
        std::vector<double> cpu;
        bool timed = false;
        std::vector<PassTiming> timings;
        std::vector<uint64_t> stamps;      // (timings' query results, kept to not allocate each frame)
        // The timestamps, copied by the GPU at the end of the frame into host-cached memory: timings() reads them
        // there (vkGetQueryPoolResults cost about 24 us a frame on the render thread, B4's CPU gate).
        vk::Buffer results;
        // What the slot's last full realise bound to the transients (index = id - 1), and the plan it did it for
        // (Cache::generation): a frame using that plan again binds the same objects without searching for them.
        uint64_t generation = 0;
        std::vector<vk::Image*> boundImages;
        std::vector<VkBuffer> boundBuffers;
    };
    struct History {
        ImageInfo info;
        vk::Image image[2];
        uint32_t previous = 0;             // image[previous]: the last frame's; the other one is written this frame
        bool previousValid = false;
        bool written = false;              // this frame
    };
    Slot slots[2];
    std::map<std::string, History> histories;
    // This frame: each resource's objects (index = id - 1).
    std::vector<vk::Image*> images[2];     // [copy]: 0 = the only one, or a history's previous; 1 = a history's new copy
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceSize> bufferOffsets;
    std::vector<void*> mapped;
    float timestampPeriod = 1.0f;
    bool timestamps = false;
    // execute()'s scratch, kept so that a frame's barriers allocate nothing once it fits.
    struct ImageState { const vk::Image* image; VkImageLayout layout; VkPipelineStageFlags2 stage; VkAccessFlags2 access; };
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    std::vector<ImageState> before;
};

// The last compiled plan, kept while the next frame is built: when that frame's signature (what compile() reads) is
// the same, its plan is used again instead of compiled (Plan::reused). Frames that repeat their passes are the rule
// (the display pipeline's passes change only with the settings), and compiling costs the render thread every frame.
struct Graph::Cache {
    struct NodePlan {
        bool live = false;
        uint32_t planIndex = kNone;
        std::vector<Barrier> merged;
    };
    std::vector<uint64_t> key, scratch;   // the signature of the plan held, and this frame's
    std::vector<NodePlan> nodes;          // what compile() leaves in the passes
    Plan plan;                            // the plan, while the next frame is built
    bool held = false;                    // `compiled` is the plan of `key`
    bool stored = false;                  // `plan` is
    bool compiledThisFrame = false;       // compile() since begin() (once a frame)
    uint64_t generation = 0;              // counts the plans compiled afresh (Physical::Slot::generation)
    uint64_t reused = 0, fresh = 0;       // frames that used the last plan again / compiled one (planCounts)
};

// --- Building ----------------------------------------------------------------------------------------------------

Graph::Graph(const vk::Device& vulkan) : device(&vulkan), ownBackend(std::make_unique<VulkanBackend>(vulkan)) {
    backend = ownBackend.get();
    cache = std::make_unique<Cache>();
    physical = std::make_unique<Physical>();
    physical->timestampPeriod = device->properties.limits.timestampPeriod;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device->physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device->physical, &count, families.data());
    physical->timestamps = device->family < count && families[device->family].timestampValidBits > 0;
#ifdef NDEBUG
    abortOnError = false;
#else
    abortOnError = true;
#endif
}

Graph::Graph(Backend& fake) : backend(&fake), abortOnError(false), cache(std::make_unique<Cache>()) {}

Graph::~Graph() { destroy(); }

void Graph::begin(const FrameInfo& info) {
    frame = info;
    resources.clear();
    passes.clear();
    cache->compiledThisFrame = false;
    if (cache->held) {   // (kept for this frame's compile(): moved, not copied)
        cache->plan = std::move(compiled);
        cache->held = false;
        cache->stored = true;
    }
    compiled = Plan{};
    if (physical) {
        for (auto& [name, h] : physical->histories) h.written = false;
    }
}

const Resource& Graph::resourceOf(uint32_t id) const { return resources[id - 1]; }

uint32_t Graph::addResource(Resource resource) {
    resources.push_back(std::move(resource));
    return static_cast<uint32_t>(resources.size());
}

void Graph::error(std::string text) { compiled.errors.push_back(std::move(text)); }

Texture Graph::import(vk::Image& image, const char* name) {
    for (const Resource& other : resources)   // two imports would track one image's state twice
        if (other.image == &image) error(std::string("'") + name + "' imports the image '" + other.name + "' already imports");
    Resource r;
    r.kind = Resource::Kind::Imported;
    r.name = name;
    r.image = &image;
    r.mips = std::max(1u, image.mips);
    r.depth = vk::isDepth(image.format);
    r.layout = image.layout;
    r.stage = image.stage;
    r.access = image.access;
    r.texture.format = image.format;
    r.texture.size = Size::Absolute;
    r.texture.extent = image.extent;
    r.texture.mips = r.mips;
    r.texture.layers = image.layers;
    r.texture.name = name;
    return {addResource(std::move(r)), 0};
}

Buffer Graph::import(vk::Buffer& buffer, const char* name) {
    Resource r;
    r.kind = Resource::Kind::Imported;
    r.buffer = true;
    r.name = name;
    r.importedBuffer = &buffer;
    r.bufferDesc.bytes = buffer.size;
    r.bufferDesc.name = name;
    return {addResource(std::move(r)), 0};
}

Texture Graph::history(const char* name, const TextureDesc& desc) {
    Resource r;
    r.kind = Resource::Kind::History;
    r.name = name;
    r.texture = desc;
    r.mips = std::max(1u, desc.mips);
    r.depth = vk::isDepth(desc.format);
    // Valid when last frame wrote a copy of this description (prepareHistories makes the copies again when it changed;
    // a new usage bit can still make them again, and then they are not valid: historyValid() is read after compile()
    // for that case).
    if (physical) {
        auto it = physical->histories.find(name);
        ImageInfo info = imageInfo(r);
        if (it != physical->histories.end()) {
            info.usage = it->second.info.usage;
            r.historyPreviousValid = it->second.previousValid && info == it->second.info;
        }
    }
    return {addResource(std::move(r)), 0};
}

std::pair<uint64_t, uint64_t> Graph::planCounts() const { return {cache->reused, cache->fresh}; }

bool Graph::historyValid(Texture t) const {
    return t && t.id <= resources.size() && resources[t.id - 1].kind == Resource::Kind::History && resources[t.id - 1].historyPreviousValid;
}

void Graph::prepareHistories() {
    if (!physical) return;
    for (Resource& r : resources) {
        if (r.kind != Resource::Kind::History) continue;
        Physical::History& h = physical->histories[r.name];
        ImageInfo info = imageInfo(r);
        info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | h.info.usage;   // (usage only grows: no new copies for a use left out)
        if (info == h.info && h.image[0].valid()) continue;
        vkDeviceWaitIdle(device->device);   // (the other slot may still use the old copies)
        for (auto& image : h.image) vk::destroy(*device, image);
        h.info = info;
        for (auto& image : h.image) {
            vk::ImageDesc desc;
            desc.format = info.format;
            desc.extent = info.extent;
            desc.usage = info.usage;
            desc.mips = info.mips;
            desc.layers = info.layers;
            desc.samples = info.samples;
            desc.cube = info.cube;
            desc.name = r.texture.name;
            image = vk::createImage(*device, desc);
        }
        h.previous = 0;
        h.previousValid = false;
        r.historyPreviousValid = false;
    }
}

PassBuilder Graph::addPass(const char* name, std::function<void(PassContext&)> execute) {
    auto node = std::make_unique<PassNode>();
    node->name = name;
    node->execute = std::move(execute);
    passes.push_back(std::move(node));
    return PassBuilder(*this, static_cast<uint32_t>(passes.size() - 1));
}

Texture PassBuilder::create(const TextureDesc& desc) {
    Resource r;
    r.name = desc.name;
    r.texture = desc;
    r.mips = std::max(1u, desc.mips);
    r.depth = vk::isDepth(desc.format);
    return {graph.addResource(std::move(r)), 0};
}

Buffer PassBuilder::create(const BufferDesc& desc) {
    Resource r;
    r.buffer = true;
    r.name = desc.name;
    r.bufferDesc = desc;
    return {graph.addResource(std::move(r)), 0};
}

Texture PassBuilder::use(Texture t, Use how, VkPipelineStageFlags2 stages, Range range) {
    return graph.useTexture(pass, t, how, stages, range);
}
Buffer PassBuilder::use(Buffer b, BufferUse how, VkPipelineStageFlags2 stages) { return graph.useBuffer(pass, b, how, stages); }
void PassBuilder::queue(Queue q) { graph.passes[pass]->queue = q; }
void PassBuilder::keep() { graph.passes[pass]->keep = true; }

Texture Graph::useTexture(uint32_t p, Texture t, Use use, VkPipelineStageFlags2 stages, Range range) {
    PassNode& node = *passes[p];
    // (the messages are only made for an error: a frame makes hundreds of uses)
    auto where = [&] { return std::string("pass '") + node.name + "'"; };
    if (!t || t.id > resources.size() || resources[t.id - 1].buffer) {
        error(where() + ": " + useName(use) + " of a texture that does not exist");
        return t;
    }
    Resource& r = resources[t.id - 1];
    auto what = [&] { return where() + ": " + useName(use) + " of '" + r.name + "'"; };
    // A history's version 0 is the previous frame's image: reading it stays valid after this frame's copy is written
    // (it is another image, and nothing writes it this frame).
    const bool previousCopy = r.kind == Resource::Kind::History && t.version == 0 && !writes(use);
    if (t.version != r.version && !previousCopy) {
        if (writes(use) && t.version + 1 < r.producers.size() && r.producers[t.version + 1] != kNone)
            error(what() + " writes version " + std::to_string(t.version) + ", which pass '" + passes[r.producers[t.version + 1]]->name +
                  "' already wrote (current version " + std::to_string(r.version) + ")");
        else
            error(what() + " uses stale version " + std::to_string(t.version) + " (current " + std::to_string(r.version) + ")");
        return t;
    }
    if ((use == Use::Opaque || use == Use::Present) && r.kind != Resource::Kind::Imported) {
        error(what() + ": " + useName(use) + " is for imported images");
        return t;
    }
    if (r.kind == Resource::Kind::Transient && r.version == 0 && !discardFirst(use)) {
        error(what() + ": the first use of a transient must write it without reading (its memory is aliased)");
        return t;
    }
    if (r.kind == Resource::Kind::History && r.version == 0 && writes(use) && !discardFirst(use)) {
        error(what() + ": the first write of a history must not read (it goes to the other copy)");
        return t;
    }
    if (range.mips == 0) {
        error(what() + ": a range of no mips");
        return t;
    }
    if (range.mip >= r.mips) {
        error(what() + ": mip " + std::to_string(range.mip) + " of " + std::to_string(r.mips));
        return t;
    }
    Use_ u;
    u.resource = t.id;
    u.use = use;
    u.stages = stages;
    u.range = range;
    u.versionIn = t.version;
    r.usage |= usageFor(use);
    if (use == Use::Present) node.keep = true;   // presentation is outside the graph
    if (writes(use)) {
        r.version++;
        r.producers.push_back(p);
        u.versionOut = r.version;
    }
    node.uses.push_back(u);
    return {t.id, r.version};
}

Buffer Graph::useBuffer(uint32_t p, Buffer b, BufferUse use, VkPipelineStageFlags2 stages) {
    PassNode& node = *passes[p];
    auto where = [&] { return std::string("pass '") + node.name + "'"; };
    if (!b || b.id > resources.size() || !resources[b.id - 1].buffer) {
        error(where() + ": a use of a buffer that does not exist");
        return b;
    }
    Resource& r = resources[b.id - 1];
    auto what = [&] { return where() + ": buffer '" + r.name + "'"; };
    if (b.version != r.version) {
        error(what() + " uses stale version " + std::to_string(b.version) + " (current " + std::to_string(r.version) + ")");
        return b;
    }
    const bool host = r.kind == Resource::Kind::Transient && r.bufferDesc.hostVisible;
    if (r.kind == Resource::Kind::Transient && !host && r.version == 0 && reads(use)) {
        error(what() + ": read before any write");
        return b;
    }
    Use_ u;
    u.resource = b.id;
    u.buffer = true;
    u.bufferUse = use;
    u.stages = stages;
    u.versionIn = b.version;
    r.bufferUsage |= usageFor(use);
    if (writes(use)) {
        r.version++;
        r.producers.push_back(p);
        u.versionOut = r.version;
    }
    node.uses.push_back(u);
    return {b.id, r.version};
}

ImageInfo Graph::imageInfo(const Resource& r) const {
    ImageInfo info;
    info.format = r.texture.format;
    switch (r.texture.size) {
        case Size::Absolute: info.extent = r.texture.extent; break;
        case Size::Render:
        case Size::Output: {
            const VkExtent2D base = r.texture.size == Size::Render ? frame.render : frame.output;
            info.extent.width = std::max(1u, static_cast<uint32_t>(base.width * r.texture.scaleX + 0.5f));
            info.extent.height = std::max(1u, static_cast<uint32_t>(base.height * r.texture.scaleY + 0.5f));
            break;
        }
    }
    info.mips = r.mips;
    info.layers = r.texture.layers;
    info.samples = r.texture.samples;
    info.cube = r.texture.cube;
    info.usage = r.usage;
    return info;
}

// --- Compile -------------------------------------------------------------------------------------------------------

// Everything compile() reads, by value (compared exactly, not as a hash). Two inputs are left out on purpose:
// - names (passes', histories') go in as a hash and a length: a collision could only leave another frame's names in
//   PlanPass::name and Plan::culled, never a barrier or a placement (those come from the uses and descriptions);
// - the backend's answers (requirements(), memoryType()): the device is fixed for a graph's life. A test backend that
//   changed them between frames would get the last frame's placements.
// An import whose vk::Image state differs frame to frame (an Opaque pass leaving it as it likes) makes every frame
// compile afresh: missed reuse, never a wrong plan (planCounts shows it).
void Graph::signature(std::vector<uint64_t>& k) const {
    k.clear();
    auto put = [&](uint64_t v) { k.push_back(v); };
    auto pair = [&](uint64_t a, uint64_t b) { put(a << 32 | (b & 0xFFFFFFFFull)); };
    auto text = [&](std::string_view s) {
        put(std::hash<std::string_view>{}(s));
        put(s.size());
    };
    auto state = [&](const vk::Image& i) {
        put(i.layout);
        put(i.stage);
        put(i.access);
        put(i.format);
        pair(i.extent.width, i.extent.height);
        pair(i.mips, i.layers);
    };
    pair(frame.render.width, frame.render.height);
    pair(frame.output.width, frame.output.height);
    put(backend->asyncCompute());
    put(resources.size());
    for (const Resource& r : resources) {
        put(uint64_t(r.kind) | uint64_t(r.buffer) << 8 | uint64_t(r.depth) << 9 | uint64_t(r.historyPreviousValid) << 10);
        pair(r.mips, r.version);
        pair(r.usage, r.bufferUsage);
        const TextureDesc& t = r.texture;
        pair(t.format, uint32_t(t.size));
        pair(t.extent.width, t.extent.height);
        uint32_t sx = 0, sy = 0;
        std::memcpy(&sx, &t.scaleX, 4);
        std::memcpy(&sy, &t.scaleY, 4);
        pair(sx, sy);
        pair(t.mips, t.layers);
        pair(t.samples, t.cube);
        pair(r.bufferDesc.bytes >> 32, r.bufferDesc.bytes);
        put(r.bufferDesc.hostVisible);
        put(reinterpret_cast<uintptr_t>(r.image));
        put(reinterpret_cast<uintptr_t>(r.importedBuffer));
        if (r.importedBuffer) put(r.importedBuffer->size);
        if (r.image) state(*r.image);
        if (r.kind == Resource::Kind::History) {
            text(r.name);
            const Physical::History* h = nullptr;
            if (physical)
                if (auto it = physical->histories.find(r.name); it != physical->histories.end()) h = &it->second;
            put(h != nullptr);
            if (h) {
                state(h->image[h->previous]);
                state(h->image[1 - h->previous]);
                put(h->info.usage);
            }
        }
    }
    put(passes.size());
    for (const auto& node : passes) {
        text(node->name);
        pair(uint32_t(node->queue), node->keep);
        put(node->uses.size());
        for (const Use_& u : node->uses) {
            pair(u.resource, u.buffer);
            pair(uint32_t(u.use), uint32_t(u.bufferUse));
            put(u.stages);
            pair(u.range.mip, u.range.mips);
            pair(u.range.layer, u.range.layers);
            pair(u.versionIn, u.versionOut);
        }
    }
}

void Graph::compile() {
    // (once a frame: a second compile() would plan on top of this frame's plan)
    if (cache->compiledThisFrame) {
        error("compile() called twice without begin()");
        if (abortOnError) std::abort();
        return;
    }
    cache->compiledThisFrame = true;
    // The last compiled frame's plan, when nothing it depends on changed (and building found no error).
    signature(cache->scratch);
    if (cache->stored && compiled.ok() && cache->scratch == cache->key && cache->nodes.size() == passes.size()) {
        compiled = std::move(cache->plan);
        cache->stored = false;
        cache->held = true;
        compiled.reused = true;
        ++cache->reused;
        for (size_t i = 0; i < passes.size(); ++i) {
            PassNode& node = *passes[i];
            const Cache::NodePlan& n = cache->nodes[i];
            node.live = n.live;
            node.planIndex = n.planIndex;
            node.merged = n.merged;
        }
        return;
    }
    cache->stored = false;
    compileFresh();
    ++cache->generation;
    ++cache->fresh;
    if (compiled.ok()) {
        cache->key.swap(cache->scratch);
        cache->nodes.resize(passes.size());
        for (size_t i = 0; i < passes.size(); ++i) {
            const PassNode& node = *passes[i];
            Cache::NodePlan& n = cache->nodes[i];
            n.live = node.live;
            n.planIndex = node.planIndex;
            n.merged = node.merged;
        }
        cache->held = true;
    }
}

void Graph::compileFresh() {
    // Culling: from the passes kept, the passes writing imports or histories, back through what they read.
    for (auto& node : passes) {
        node->live = node->keep;
        for (const Use_& u : node->uses)
            if (u.versionOut && resources[u.resource - 1].kind != Resource::Kind::Transient) node->live = true;
    }
    for (size_t i = passes.size(); i-- > 0;) {
        PassNode& node = *passes[i];
        if (!node.live) continue;
        for (const Use_& u : node.uses) {
            // A write of some mips keeps the others: it needs the version before it as much as a read does.
            const Resource& r = resources[u.resource - 1];
            const bool partial = !u.buffer && (u.range.mip > 0 || (u.range.mips != VK_REMAINING_MIP_LEVELS && u.range.mip + u.range.mips < r.mips));
            const bool needs = u.buffer ? reads(u.bufferUse) : reads(u.use) || partial;
            if (!needs || u.versionIn == 0) continue;
            const uint32_t producer = resources[u.resource - 1].producers[u.versionIn];
            if (producer != kNone) passes[producer]->live = true;
        }
    }
    for (size_t i = 0; i < passes.size(); ++i) {
        PassNode& node = *passes[i];
        if (!node.live) {
            compiled.culled.push_back(node.name);
            continue;
        }
        node.planIndex = static_cast<uint32_t>(compiled.passes.size());
        PlanPass step;
        step.pass = static_cast<uint32_t>(i);
        step.name = node.name;
        step.queue = node.queue;
        compiled.passes.push_back(std::move(step));
    }
    if (compiled.ok()) {
        prepareHistories();  // (before the barriers: a history made again has no previous contents)
        planAsync();
        placeTransients();   // (before the barriers: a transient's first use waits for its memory's earlier occupants)
        planBarriers();
    }
    if (!compiled.ok()) {
        for (const std::string& e : compiled.errors) std::fprintf(stderr, "render graph: %s\n", e.c_str());
        if (abortOnError) std::abort();
    }
    static const bool dumpGraph = [] { const char* e = std::getenv("GT4_RG_DUMP"); return e && *e == '1'; }();
    if (dumpGraph && device) {
        static uint32_t dumped = 0;
        if (dumped++ == 0) std::printf("%s\n", dump().c_str());
    }
}

void Graph::planAsync() {
    if (!backend->asyncCompute()) return;
    const uint32_t count = static_cast<uint32_t>(compiled.passes.size());
    for (PlanPass& p : compiled.passes) p.async = p.queue == Queue::Compute;
    auto touches = [&](uint32_t planIndex, const std::vector<uint32_t>& ids) {
        for (const Use_& u : passes[compiled.passes[planIndex].pass]->uses)
            if (std::find(ids.begin(), ids.end(), u.resource) != ids.end()) return true;
        return false;
    };
    for (uint32_t start = 0; start < count;) {
        if (!compiled.passes[start].async) { ++start; continue; }
        uint32_t end = start;
        while (end + 1 < count && compiled.passes[end + 1].async) ++end;
        std::vector<uint32_t> ids;   // everything the run uses
        for (uint32_t i = start; i <= end; ++i)
            for (const Use_& u : passes[compiled.passes[i].pass]->uses) ids.push_back(u.resource);
        // Graphics work before the run on the same resources (its inputs, or what the run overwrites).
        for (uint32_t g = start; g-- > 0;)
            if (!compiled.passes[g].async && touches(g, ids)) {
                compiled.edges.push_back({g, start, false});
                break;
            }
        // The first graphics pass after the run that uses them waits for it; none: the end of the frame.
        uint32_t wait = count;
        for (uint32_t g = end + 1; g < count; ++g)
            if (!compiled.passes[g].async && touches(g, ids)) { wait = g; break; }
        compiled.edges.push_back({end, wait, true});
        start = end + 1;
    }
}

void Graph::planBarriers() {
    // What a later use must wait for, per mip (and per buffer). A read whose stages and accesses the barriers since the
    // last write already cover needs none; another read waits for the last write; a write or a layout change waits for
    // every earlier use.
    struct Sync {
        VkPipelineStageFlags2 writeStage = VK_PIPELINE_STAGE_2_NONE;   // the last write or layout transition
        VkAccessFlags2 writeAccess = VK_ACCESS_2_NONE;                 // what it wrote (to make available)
        VkPipelineStageFlags2 readStages = VK_PIPELINE_STAGE_2_NONE;   // reads since then
        VkAccessFlags2 readAccess = VK_ACCESS_2_NONE;
        VkPipelineStageFlags2 visible = VK_PIPELINE_STAGE_2_NONE;      // stages ordered after the last write ...
        VkAccessFlags2 visibleAccess = VK_ACCESS_2_NONE;               // ... with these accesses made visible
    };
    struct MipState : Sync {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool fromImage = false;   // after an Opaque use: the pass left its own state in vk::Image
        bool fresh = true;        // no defined contents yet this frame (transient, a history's new copy)
        uint32_t lastPass = kNone;
        VkImageLayout lastPassLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        size_t lastBarrier = SIZE_MAX;   // its barrier in lastPass's list, when that use made one
        Sync passStart;                  // the state before that barrier (what a write joining it must wait for)
    };
    auto before = [](const Sync& s) {   // everything a write or a layout change waits for
        const VkPipelineStageFlags2 all = s.writeStage | s.readStages;
        return all ? all : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    };
    auto covered = [](const Sync& s, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
        return !s.writeStage || (!(stages & ~s.visible) && !(access & ~s.visibleAccess));
    };
    auto afterWrite = [](Sync& s, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
        s.writeStage = stages;
        s.writeAccess = access & kWriteAccess;
        s.readStages = s.visible = VK_PIPELINE_STAGE_2_NONE;
        s.readAccess = s.visibleAccess = VK_ACCESS_2_NONE;
    };
    auto afterTransition = [](Sync& s, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {   // a layout change for reads
        s.writeStage = s.readStages = s.visible = stages;
        s.writeAccess = VK_ACCESS_2_NONE;
        s.readAccess = s.visibleAccess = access;
    };
    auto afterRead = [](Sync& s, VkPipelineStageFlags2 stages, VkAccessFlags2 access, bool barrier) {
        s.readStages |= stages;
        s.readAccess |= access;
        if (barrier) s.visible |= stages, s.visibleAccess |= access;
    };
    // Per resource and copy (a history has two images), per mip.
    std::map<std::pair<uint32_t, uint32_t>, std::vector<MipState>> textures;
    std::map<uint32_t, Sync> buffers;
    // A transient whose memory held others earlier in the frame (aliasing): its first use waits for their last uses
    // (write after read or write on the same memory). Their states are final by then: their lifetimes ended before.
    auto occupants = [&](uint32_t id, Sync& s) {
        const Placement* self = nullptr;
        for (const Placement& p : compiled.placements)
            if (p.resource == id) self = &p;
        if (!self) return;
        for (const Placement& p : compiled.placements) {
            if (p.resource == id || p.memoryType != self->memoryType || p.buffer != self->buffer || p.last >= self->first) continue;
            if (p.offset >= self->offset + self->size || self->offset >= p.offset + p.size) continue;
            if (p.buffer) {
                auto b = buffers.find(p.resource);
                if (b != buffers.end()) s.writeStage |= b->second.writeStage | b->second.readStages, s.writeAccess |= b->second.writeAccess;
            } else {
                auto t = textures.find({p.resource, 0});
                if (t != textures.end())
                    for (const MipState& m : t->second) s.writeStage |= m.writeStage | m.readStages, s.writeAccess |= m.writeAccess;
            }
        }
    };
    auto fromImage = [](MipState& m, const vk::Image& image) {   // a state outside the graph: wait for its last use
        m.layout = image.layout;
        m.writeStage = m.readStages = image.stage;
        m.writeAccess = image.access & kWriteAccess;
        m.readAccess = image.access;
        m.visible = VK_PIPELINE_STAGE_2_NONE;
        m.visibleAccess = VK_ACCESS_2_NONE;
    };
    auto stateOf = [&](uint32_t id, uint32_t copy) -> std::vector<MipState>& {
        auto key = std::make_pair(id, copy);
        auto it = textures.find(key);
        if (it != textures.end()) return it->second;
        const Resource& r = resources[id - 1];
        std::vector<MipState> mips(r.mips);
        const Physical::History* h = nullptr;
        if (r.kind == Resource::Kind::History && physical) {
            auto found = physical->histories.find(r.name);
            if (found != physical->histories.end()) h = &found->second;
        }
        for (MipState& m : mips) {
            if (r.kind == Resource::Kind::Transient) {
                occupants(id, m);   // (contents discarded either way; only the wait matters)
            } else if (r.kind == Resource::Kind::Imported) {
                fromImage(m, *r.image);
                m.fresh = false;
            } else if (copy == 0) {   // a history's previous copy: as the last frame left it
                if (h) fromImage(m, h->image[h->previous]);
                m.fresh = !r.historyPreviousValid;
            } else if (h) {           // its new copy: last read as the previous one, its writes wait for that
                fromImage(m, h->image[1 - h->previous]);
                m.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            }
        }
        return textures.emplace(key, std::move(mips)).first->second;
    };
    auto fail = [&](const PassNode& node, const Resource& r, const std::string& what) {
        error(std::string("pass '") + node.name + "': '" + r.name + "' " + what);
    };

    for (uint32_t planIndex = 0; planIndex < compiled.passes.size(); ++planIndex) {
        PlanPass& step = compiled.passes[planIndex];
        PassNode& node = *passes[step.pass];
        for (const Use_& u : node.uses) {
            const Resource& r = resources[u.resource - 1];
            if (u.buffer) {
                if (!buffers.count(u.resource)) {
                    Sync first;
                    if (r.kind == Resource::Kind::Transient && !r.bufferDesc.hostVisible) occupants(u.resource, first);
                    // An imported buffer has no tracked state (vk::Buffer keeps none): its first use waits for all
                    // earlier work. Host-visible ones: the host's writes are visible at submission.
                    if (r.kind == Resource::Kind::Imported)
                        first.writeStage = first.readStages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, first.writeAccess = VK_ACCESS_2_MEMORY_WRITE_BIT;
                    buffers[u.resource] = first;
                }
                Sync& s = buffers[u.resource];
                const VkAccessFlags2 access = accessFor(u.bufferUse);
                const bool write = writes(u.bufferUse);
                Barrier b;
                b.resource = u.resource;
                b.buffer = true;
                b.dstStage = u.stages;
                b.dstAccess = access;
                if (write) {
                    if (s.writeStage | s.readStages) {
                        b.srcStage = before(s);
                        b.srcAccess = s.writeAccess;
                        step.barriers.push_back(b);
                    }
                    afterWrite(s, u.stages, access);
                } else if (!covered(s, u.stages, access)) {
                    b.srcStage = s.writeStage;
                    b.srcAccess = s.writeAccess;
                    b.replaces = false;
                    step.barriers.push_back(b);
                    afterRead(s, u.stages, access, true);
                } else {
                    node.merged.push_back(b);
                    afterRead(s, u.stages, access, false);
                }
                continue;
            }
            // A history's version 0 is the previous frame's image; its writes and what they made are the other one.
            const uint32_t copy = r.kind == Resource::Kind::History && (u.versionOut || u.versionIn > 0) ? 1 : 0;
            std::vector<MipState>& mips = stateOf(u.resource, copy);
            const uint32_t first = u.range.mip;
            const uint32_t last = u.range.mips == VK_REMAINING_MIP_LEVELS || u.range.mips > r.mips - first ? r.mips : first + u.range.mips;
            const bool opaque = u.use == Use::Opaque;
            if (opaque && (first != 0 || last != r.mips)) {
                fail(node, r, "an Opaque use must cover every mip (the pass synchronises the whole image)");
                return;
            }
            const VkAccessFlags2 access = accessFor(u.use);
            const bool write = writes(u.use);
            const VkPipelineStageFlags2 stages = opaque ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : u.stages;
            // An Opaque pass reads vk::Image's one layout: mips in different layouts are brought to the newest one's.
            VkImageLayout opaqueLayout = VK_IMAGE_LAYOUT_UNDEFINED;   // (UNDEFINED: keep the image's, known when it runs)
            if (opaque && !mips[0].fromImage) {
                uint32_t newest = 0;
                for (uint32_t m = 0; m < mips.size(); ++m)
                    if (mips[m].lastPass != kNone && (mips[newest].lastPass == kNone || mips[m].lastPass >= mips[newest].lastPass)) newest = m;
                opaqueLayout = mips[newest].layout;
            }
            for (uint32_t mip = first; mip < last; ++mip) {
                MipState& s = mips[mip];
                const VkImageLayout layout = opaque ? opaqueLayout : layoutFor(u.use);
                if (opaque && s.fresh) {
                    fail(node, r, "has no contents for an Opaque use");
                    return;
                }
                // Opaque on an image nothing has written yet (an import still UNDEFINED, its first frame): nothing to
                // wait for and no layout to keep (a barrier to UNDEFINED is not allowed); the pass takes it as it is.
                if (opaque && !s.fromImage && s.layout == VK_IMAGE_LAYOUT_UNDEFINED && s.lastPass != step.pass) {
                    afterWrite(s, stages, access);
                    s.fromImage = true;
                    s.lastPass = step.pass;
                    s.lastPassLayout = layout;
                    s.lastBarrier = SIZE_MAX;
                    continue;
                }
                const bool discard = !opaque && (s.fresh || u.use == Use::ColorWrite || u.use == Use::DepthWrite);
                const bool unwritten = r.kind == Resource::Kind::Transient || (r.kind == Resource::Kind::History && copy == 1);
                if (reads(u.use) && !opaque && s.fresh && unwritten) {
                    fail(node, r, "mip " + std::to_string(mip) + " is read before it is written");
                    return;
                }
                if (reads(u.use) && !opaque && !s.fromImage && r.kind == Resource::Kind::Imported && s.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
                    fail(node, r, "is read with no contents (imported UNDEFINED)");
                    return;
                }
                // A second use of the mip in the same pass: one barrier for both (two in one flush would not chain).
                if (s.lastPass == step.pass) {
                    if (s.lastPassLayout != layout) {
                        fail(node, r, "mip " + std::to_string(mip) + " used in two layouts");
                        return;
                    }
                    if (s.lastBarrier != SIZE_MAX) {
                        Barrier& earlier = step.barriers[s.lastBarrier];
                        earlier.dstStage |= stages;
                        earlier.dstAccess |= access;
                        earlier.replaces = earlier.replaces || write;
                        if (write) {   // a write joining a read's barrier waits for every use before this pass
                                       // (that barrier may wait for the last write only)
                            earlier.srcStage |= before(s.passStart);
                            earlier.srcAccess |= s.passStart.writeAccess;
                        }
                        if (write) afterWrite(s, earlier.dstStage, earlier.dstAccess);
                        else afterRead(s, stages, access, true);
                        continue;
                    }
                }
                Barrier b;
                b.resource = u.resource;
                b.copy = copy;
                b.mip = mip;
                b.mips = 1;
                b.newLayout = layout;
                b.dstStage = stages;
                b.dstAccess = access;
                s.passStart = s;   // (the Sync part: the state this use's barrier starts from)
                const bool change = s.fromImage || discard || s.layout != layout;
                if (s.fromImage) {   // after Opaque: the old state is vk::Image's when it runs
                    b.fromImage = true;
                    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                } else if (change || write) {
                    b.oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : s.layout;
                    b.srcStage = before(s);
                    b.srcAccess = s.writeAccess;
                } else if (!covered(s, stages, access)) {   // a read the barriers since the write do not cover yet
                    b.oldLayout = s.layout;
                    b.srcStage = s.writeStage;
                    b.srcAccess = s.writeAccess;
                    b.replaces = false;   // (it waits for the write only: the reads since stay in the tracked state)
                } else {                  // a read the last barrier already covers
                    node.merged.push_back(b);
                    afterRead(s, stages, access, false);
                    s.lastPass = step.pass;
                    s.lastPassLayout = layout;
                    s.lastBarrier = SIZE_MAX;
                    continue;
                }
                // Merged with the barrier of the mip before it when it is the same one.
                Barrier* previous = step.barriers.empty() ? nullptr : &step.barriers.back();
                if (previous && !previous->buffer && previous->resource == b.resource && previous->copy == b.copy &&
                    previous->mip + previous->mips == mip && previous->oldLayout == b.oldLayout && previous->newLayout == b.newLayout &&
                    previous->srcStage == b.srcStage && previous->srcAccess == b.srcAccess && previous->dstStage == b.dstStage &&
                    previous->dstAccess == b.dstAccess && previous->fromImage == b.fromImage && previous->replaces == b.replaces)
                    previous->mips++;
                else
                    step.barriers.push_back(b);
                if (write) afterWrite(s, stages, access);
                else if (change) afterTransition(s, stages, access);
                else afterRead(s, stages, access, true);
                s.layout = layout;
                s.fresh = false;
                s.fromImage = opaque;   // (an Opaque pass may change layouts itself: what it leaves is known when it runs)
                s.lastPass = step.pass;
                s.lastPassLayout = layout;
                s.lastBarrier = step.barriers.size() - 1;
            }
        }
    }

    // Imports leave the graph whole: mips in different layouts are brought to the layout of the last one used (after
    // an Opaque use, every mip from vk::Image's state when it runs). Then what each import and history copy ends in,
    // for vk::Image: the union over its mips of what a later use must wait for.
    for (auto& [key, mips] : textures) {
        const Resource& r = resources[key.first - 1];
        if (r.kind == Resource::Kind::Imported && mips.size() > 1) {
            uint32_t newest = 0;
            for (uint32_t m = 0; m < mips.size(); ++m)
                if (mips[m].lastPass != kNone && (mips[newest].lastPass == kNone || mips[m].lastPass >= mips[newest].lastPass)) newest = m;
            const bool anyFromImage = std::any_of(mips.begin(), mips.end(), [](const MipState& m) { return m.fromImage; });
            const VkImageLayout target = mips[newest].fromImage ? VK_IMAGE_LAYOUT_UNDEFINED : mips[newest].layout;
            bool mixed = anyFromImage && target != VK_IMAGE_LAYOUT_UNDEFINED;
            for (const MipState& m : mips) mixed = mixed || m.layout != mips[newest].layout;
            if (mixed && target != VK_IMAGE_LAYOUT_UNDEFINED) {
                for (uint32_t m = 0; m < mips.size(); ++m) {
                    if (!mips[m].fromImage && mips[m].layout == target) continue;
                    Barrier b;
                    b.resource = key.first;
                    b.mip = m;
                    b.fromImage = mips[m].fromImage;
                    b.oldLayout = mips[m].fromImage ? VK_IMAGE_LAYOUT_UNDEFINED : mips[m].layout;
                    b.newLayout = target;
                    b.srcStage = before(mips[m]);
                    b.srcAccess = mips[m].writeAccess;
                    b.dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    b.dstAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                    compiled.finalBarriers.push_back(b);
                    mips[m].layout = target;
                    mips[m].fromImage = false;
                    afterWrite(mips[m], b.dstStage, b.dstAccess);
                }
            }
        }
        if (r.kind == Resource::Kind::Transient) continue;
        Plan::Export e;
        e.resource = key.first;
        e.copy = key.second;
        e.layout = mips[0].layout;
        for (const MipState& m : mips) {
            e.stage |= m.writeStage | m.readStages;
            e.access |= m.writeAccess | m.readAccess;
            e.fromImage = e.fromImage || m.fromImage;
        }
        compiled.exports.push_back(e);
    }
}

void Graph::placeTransients() {
    struct Item {
        uint32_t resource;
        bool buffer;
        MemoryNeeds needs;
        uint32_t type;
        uint32_t first = kNone, last = 0;
    };
    std::vector<Item> items;
    std::map<uint32_t, size_t> index;
    for (uint32_t planIndex = 0; planIndex < compiled.passes.size(); ++planIndex) {
        for (const Use_& u : passes[compiled.passes[planIndex].pass]->uses) {
            const Resource& r = resources[u.resource - 1];
            if (r.kind != Resource::Kind::Transient || (r.buffer && r.bufferDesc.hostVisible)) continue;
            auto it = index.find(u.resource);
            if (it == index.end()) {
                Item item{u.resource, r.buffer, {}, 0};
                item.needs = r.buffer ? backend->requirements(r.bufferDesc.bytes, r.bufferUsage) : backend->requirements(imageInfo(r));
                item.type = backend->memoryType(item.needs.typeBits);
                it = index.emplace(u.resource, items.size()).first;
                items.push_back(item);
            }
            Item& item = items[it->second];
            item.first = std::min(item.first, planIndex);
            item.last = std::max(item.last, planIndex);
        }
    }
    // An async compute pass may overlap the graphics passes between the edge it waits on and the edge that waits
    // for it: its resources live through that whole span.
    for (uint32_t planIndex = 0; planIndex < compiled.passes.size(); ++planIndex) {
        if (!compiled.passes[planIndex].async) continue;
        // Its run's edges: the graphics-to-compute one before the run (the latest), the compute-to-graphics one after
        // it (the earliest; the end of the frame when no pass waits).
        const uint32_t lastPass = static_cast<uint32_t>(compiled.passes.size()) - 1;
        uint32_t from = 0, to = lastPass, waitAfter = kNone;
        for (const AsyncEdge& e : compiled.edges) {
            if (!e.computeToGraphics && e.before <= planIndex) from = std::max(from, e.after + 1);
            if (e.computeToGraphics && e.after >= planIndex && (waitAfter == kNone || e.after < waitAfter)) {
                waitAfter = e.after;
                to = std::min(lastPass, e.before);
            }
        }
        for (const Use_& u : passes[compiled.passes[planIndex].pass]->uses) {
            auto it = index.find(u.resource);
            if (it == index.end()) continue;
            items[it->second].first = std::min(items[it->second].first, from);
            items[it->second].last = std::max(items[it->second].last, to);
        }
    }
    // First fit, largest first; arenas by memory type, images and buffers apart (no granularity question).
    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return items[a].needs.size > items[b].needs.size; });
    std::map<std::pair<uint32_t, bool>, VkDeviceSize> arenaSize;
    for (size_t i : order) {
        const Item& item = items[i];
        std::vector<std::pair<VkDeviceSize, VkDeviceSize>> taken;   // offset, end
        for (const Placement& p : compiled.placements)
            if (p.memoryType == item.type && p.buffer == item.buffer && overlaps(p.first, p.last, item.first, item.last))
                taken.push_back({p.offset, p.offset + p.size});
        std::sort(taken.begin(), taken.end());
        VkDeviceSize offset = 0;
        for (const auto& [start, end] : taken) {
            if (alignUp(offset, item.needs.alignment) + item.needs.size <= start) break;
            offset = std::max(offset, end);
        }
        offset = alignUp(offset, item.needs.alignment);
        Placement p;
        p.resource = item.resource;
        p.buffer = item.buffer;
        p.memoryType = item.type;
        p.offset = offset;
        p.size = item.needs.size;
        p.first = item.first;
        p.last = item.last;
        compiled.placements.push_back(p);
        VkDeviceSize& size = arenaSize[{item.type, item.buffer}];
        size = std::max(size, offset + item.needs.size);
        compiled.transientBytesWithoutAliasing += alignUp(item.needs.size, item.needs.alignment);
    }
    std::sort(compiled.placements.begin(), compiled.placements.end(), [](const Placement& a, const Placement& b) { return a.resource < b.resource; });
    for (const auto& [key, size] : arenaSize) compiled.transientBytes += size;
}

std::string Graph::dump() const {
    std::ostringstream out;
    out << "digraph render_graph {\n  rankdir=LR;\n  node [shape=box];\n";
    for (size_t i = 0; i < passes.size(); ++i) {
        const PassNode& node = *passes[i];
        out << "  p" << i << " [label=\"" << node.name << (node.queue == Queue::Compute ? " (compute)" : "") << "\""
            << (node.live ? "" : ", style=dashed") << "];\n";
    }
    for (size_t id = 1; id <= resources.size(); ++id) {
        const Resource& r = resources[id - 1];
        out << "  r" << id << " [shape=ellipse, label=\"" << r.name << "\"" << (r.kind == Resource::Kind::Transient ? "" : ", style=bold") << "];\n";
    }
    for (size_t i = 0; i < passes.size(); ++i)
        for (const Use_& u : passes[i]->uses) {
            const bool read = u.buffer ? reads(u.bufferUse) : reads(u.use);
            if (read) out << "  r" << u.resource << " -> p" << i << " [label=\"v" << u.versionIn << "\"];\n";
            if (u.versionOut) out << "  p" << i << " -> r" << u.resource << " [label=\"v" << u.versionOut << "\"];\n";
        }
    out << "}\n";
    return out.str();
}

// --- Vulkan ------------------------------------------------------------------------------------------------------

void Graph::realise() {
    Physical& ph = *physical;
    Physical::Slot& slot = ph.slots[frame.slot % 2];
    // The plan this slot last realised, used again: the same objects, bound without searching (only the imports, the
    // history copies of this frame and the host ring are looked at again).
    if (compiled.reused && slot.generation == cache->generation && slot.boundImages.size() == resources.size()) {
        ph.images[0] = slot.boundImages;
        ph.images[1].assign(resources.size(), nullptr);
        ph.buffers = slot.boundBuffers;
        ph.bufferOffsets.assign(resources.size(), 0);
        ph.mapped.assign(resources.size(), nullptr);
        slot.ringUsed = 0;
        for (uint32_t id = 1; id <= resources.size(); ++id) {
            const Resource& r = resources[id - 1];
            if (r.kind == Resource::Kind::Imported) {
                if (r.buffer) ph.buffers[id - 1] = r.importedBuffer->buffer;
                else ph.images[0][id - 1] = r.image;
            } else if (r.kind == Resource::Kind::History) {
                Physical::History& h = ph.histories[r.name];
                ph.images[0][id - 1] = &h.image[h.previous];
                ph.images[1][id - 1] = &h.image[1 - h.previous];
            } else if (r.buffer && r.bufferDesc.hostVisible) {
                slot.ringUsed = alignUp(slot.ringUsed, 256);
                ph.buffers[id - 1] = slot.ring.buffer;
                ph.bufferOffsets[id - 1] = slot.ringUsed;
                ph.mapped[id - 1] = static_cast<char*>(slot.ring.mapped) + slot.ringUsed;
                slot.ringUsed += r.bufferDesc.bytes;
            } else if (vk::Image* image = ph.images[0][id - 1]) {
                image->layout = VK_IMAGE_LAYOUT_UNDEFINED;   // aliased: its contents are not kept
                image->stage = VK_PIPELINE_STAGE_2_NONE;
                image->access = VK_ACCESS_2_NONE;
            }
        }
        return;
    }
    // Arenas: grown (made again, with their images) when the plan needs more.
    std::map<std::pair<uint32_t, bool>, VkDeviceSize> needed;
    for (const Placement& p : compiled.placements) {
        VkDeviceSize& size = needed[{p.memoryType, p.buffer}];
        size = std::max(size, p.offset + p.size);
    }
    auto destroyImage = [&](Physical::CachedImage& c) {
        c.image.memory = VK_NULL_HANDLE;   // the arena's
        vk::destroy(*device, c.image);
    };
    auto destroyBuffer = [&](Physical::CachedBuffer& c) {
        if (c.buffer) vkDestroyBuffer(device->device, c.buffer, nullptr);
        c.buffer = VK_NULL_HANDLE;
    };
    for (const auto& [key, size] : needed) {
        auto it = std::find_if(slot.arenas.begin(), slot.arenas.end(),
                               [&](const Physical::Arena& a) { return a.memoryType == key.first && a.buffer == key.second; });
        if (it != slot.arenas.end() && it->size >= size) continue;
        if (it != slot.arenas.end()) {   // too small: its objects go with it
            const uint32_t arena = static_cast<uint32_t>(it - slot.arenas.begin());
            for (auto& c : slot.images) if (c->arena == arena && c->image.valid()) destroyImage(*c);
            for (auto& c : slot.buffers) if (c.arena == arena && c.buffer) destroyBuffer(c);
            std::erase_if(slot.images, [](const std::unique_ptr<Physical::CachedImage>& c) { return !c->image.valid(); });
            std::erase_if(slot.buffers, [](const Physical::CachedBuffer& c) { return !c.buffer; });
            vkFreeMemory(device->device, it->memory, nullptr);
            it->memory = VK_NULL_HANDLE;
        } else {
            slot.arenas.push_back({key.first, key.second, VK_NULL_HANDLE, 0});
            it = slot.arenas.end() - 1;
        }
        const VkDeviceSize grown = std::max(size, it->size + it->size / 2);   // room to grow without a new one each frame
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = grown;
        allocation.memoryTypeIndex = key.first;
        vk::check(vkAllocateMemory(device->device, &allocation, nullptr, &it->memory), "vkAllocateMemory(render graph)");
        it->size = grown;
    }
    auto arenaOf = [&](uint32_t type, bool buffer) {
        for (uint32_t i = 0; i < slot.arenas.size(); ++i)
            if (slot.arenas[i].memoryType == type && slot.arenas[i].buffer == buffer) return i;
        return kNone;
    };

    for (auto& c : slot.images) c->used = false;
    for (auto& c : slot.buffers) c.used = false;
    for (int copy = 0; copy < 2; ++copy) ph.images[copy].assign(resources.size(), nullptr);
    ph.buffers.assign(resources.size(), VK_NULL_HANDLE);
    ph.bufferOffsets.assign(resources.size(), 0);
    ph.mapped.assign(resources.size(), nullptr);
    std::map<uint32_t, const Placement*> placement;
    for (const Placement& p : compiled.placements) placement[p.resource] = &p;

    // Host-visible buffers: one ring per slot, made again larger when it does not fit.
    VkDeviceSize ringNeeded = 0;
    for (const Resource& r : resources)
        if (r.buffer && r.kind == Resource::Kind::Transient && r.bufferDesc.hostVisible) ringNeeded = alignUp(ringNeeded, 256) + r.bufferDesc.bytes;
    if (ringNeeded > slot.ring.size) {
        vk::destroy(*device, slot.ring);
        slot.ring = vk::createBuffer(*device, std::max(ringNeeded, slot.ring.size * 2),
                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     true);
    }
    slot.ringUsed = 0;

    for (uint32_t id = 1; id <= resources.size(); ++id) {
        const Resource& r = resources[id - 1];
        if (r.kind == Resource::Kind::Imported) {
            if (r.buffer) ph.buffers[id - 1] = r.importedBuffer->buffer;
            else ph.images[0][id - 1] = r.image;
            continue;
        }
        if (r.kind == Resource::Kind::History) {
            Physical::History& h = ph.histories[r.name];   // (its copies made in prepareHistories, before the plan)
            ph.images[0][id - 1] = &h.image[h.previous];
            ph.images[1][id - 1] = &h.image[1 - h.previous];
            continue;
        }
        if (r.buffer && r.bufferDesc.hostVisible) {
            slot.ringUsed = alignUp(slot.ringUsed, 256);
            ph.buffers[id - 1] = slot.ring.buffer;
            ph.bufferOffsets[id - 1] = slot.ringUsed;
            ph.mapped[id - 1] = static_cast<char*>(slot.ring.mapped) + slot.ringUsed;
            slot.ringUsed += r.bufferDesc.bytes;
            continue;
        }
        auto p = placement.find(id);
        if (p == placement.end()) continue;   // used only by culled passes
        const uint32_t arena = arenaOf(p->second->memoryType, r.buffer);
        const VkDeviceMemory memory = slot.arenas[arena].memory;
        if (r.buffer) {
            auto c = std::find_if(slot.buffers.begin(), slot.buffers.end(), [&](const Physical::CachedBuffer& c) {
                return !c.used && c.bytes == r.bufferDesc.bytes && c.usage == r.bufferUsage && c.arena == arena &&
                       c.offset == p->second->offset && c.memory == memory;
            });
            if (c == slot.buffers.end()) {
                Physical::CachedBuffer made;
                made.bytes = r.bufferDesc.bytes;
                made.usage = r.bufferUsage;
                made.arena = arena;
                made.offset = p->second->offset;
                made.memory = memory;
                VkBufferCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                info.size = std::max<VkDeviceSize>(r.bufferDesc.bytes, 4);
                info.usage = r.bufferUsage;
                info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                vk::check(vkCreateBuffer(device->device, &info, nullptr, &made.buffer), "vkCreateBuffer(render graph)");
                vk::check(vkBindBufferMemory(device->device, made.buffer, memory, made.offset), "vkBindBufferMemory(render graph)");
                vk::label(*device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(made.buffer), r.bufferDesc.name);
                slot.buffers.push_back(made);
                c = slot.buffers.end() - 1;
            }
            c->used = true;
            ph.buffers[id - 1] = c->buffer;
            continue;
        }
        const ImageInfo info = imageInfo(r);
        Physical::CachedImage* c = nullptr;
        for (const auto& cached : slot.images)
            if (!cached->used && cached->info == info && cached->arena == arena && cached->offset == p->second->offset && cached->memory == memory) {
                c = cached.get();
                break;
            }
        if (!c) {
            auto owned = std::make_unique<Physical::CachedImage>();
            Physical::CachedImage& made = *owned;
            made.info = info;
            made.arena = arena;
            made.offset = p->second->offset;
            made.memory = memory;
            vk::Image& image = made.image;
            image.format = info.format;
            image.extent = info.extent;
            image.mips = info.mips;
            image.layers = info.cube ? 6 : info.layers;
            image.samples = info.samples;
            image.aspect = vk::isDepth(info.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            if (vk::hasStencil(info.format)) image.aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
            const VkImageCreateInfo create = VulkanBackend::createInfo(info);
            vk::check(vkCreateImage(device->device, &create, nullptr, &image.image), "vkCreateImage(render graph)");
            vk::check(vkBindImageMemory(device->device, image.image, memory, made.offset), "vkBindImageMemory(render graph)");
            image.memory = memory;
            image.offset = made.offset;
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = image.image;
            view.viewType = info.cube ? VK_IMAGE_VIEW_TYPE_CUBE : image.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            view.format = info.format;
            view.subresourceRange = {image.aspect, 0, info.mips, 0, image.layers};
            // Views only for images a view can use (a transfer-only image has none: Vulkan forbids them).
            constexpr VkImageUsageFlags kViewUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            if (info.usage & kViewUsage)
                vk::check(vkCreateImageView(device->device, &view, nullptr, &image.view), "vkCreateImageView(render graph)");
            if (info.mips > 1 && (info.usage & (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))) {
                image.mipViews.resize(info.mips);
                for (uint32_t mip = 0; mip < info.mips; ++mip) {
                    view.viewType = image.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
                    view.subresourceRange = {image.aspect, mip, 1, 0, image.layers};
                    vk::check(vkCreateImageView(device->device, &view, nullptr, &image.mipViews[mip]), "vkCreateImageView(render graph mip)");
                }
            }
            vk::label(*device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image.image), r.texture.name);
            c = owned.get();
            slot.images.push_back(std::move(owned));
        }
        c->used = true;
        c->image.layout = VK_IMAGE_LAYOUT_UNDEFINED;   // aliased: its contents are not kept
        c->image.stage = VK_PIPELINE_STAGE_2_NONE;
        c->image.access = VK_ACCESS_2_NONE;
        ph.images[0][id - 1] = &c->image;
    }
    // Objects no plan used this frame are kept for a later one (the cache), unless the slot holds too many.
    if (slot.images.size() > 256) {
        for (auto& c : slot.images) if (!c->used) destroyImage(*c);
        std::erase_if(slot.images, [](const std::unique_ptr<Physical::CachedImage>& c) { return !c->image.valid(); });
    }
    // For the frames that use this plan again in this slot (the imports' and histories' entries are set again then).
    slot.generation = cache->generation;
    slot.boundImages = ph.images[0];
    slot.boundBuffers = ph.buffers;
}

// The image a pass means by a handle. For a history, version 0 is the previous frame's copy when the pass reads
// it, and the new copy when the pass writes from it (its input handle); later versions are the new copy.
vk::Image& Graph::imageOf(uint32_t resource, uint32_t version, uint32_t pass) {
    const Resource& r = resources[resource - 1];
    uint32_t copy = 0;
    if (r.kind == Resource::Kind::History) {
        copy = version > 0 ? 1 : 0;
        if (version == 0 && pass < passes.size()) {
            bool readsPrevious = false, writesFromIt = false;
            for (const Use_& u : passes[pass]->uses)
                if (!u.buffer && u.resource == resource && u.versionIn == 0) (u.versionOut ? writesFromIt : readsPrevious) = true;
            copy = !readsPrevious && writesFromIt ? 1 : 0;
        }
    }
    return *physical->images[copy][resource - 1];
}

void Graph::execute(VkCommandBuffer command, VkQueue computeQueue) {
    (void)computeQueue;   // async compute runs in order on the graphics queue until ticket B21
    if (!device || !compiled.ok()) return;
    realise();
    Physical& ph = *physical;
    Physical::Slot& slot = ph.slots[frame.slot % 2];
    const uint32_t queries = static_cast<uint32_t>(compiled.passes.size()) * 2;
    if (ph.timestamps && queries > slot.queryCapacity) {
        if (slot.queries) vkDestroyQueryPool(device->device, slot.queries, nullptr);
        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = std::max(queries, 64u);
        vk::check(vkCreateQueryPool(device->device, &info, nullptr, &slot.queries), "vkCreateQueryPool(render graph)");
        slot.queryCapacity = info.queryCount;
    }
    if (ph.timestamps && queries) vkCmdResetQueryPool(command, slot.queries, 0, queries);
    slot.names.resize(compiled.passes.size());   // (assigned in place below: no allocation once the names fit)
    slot.cpu.resize(compiled.passes.size());

    auto apply = [&](const std::vector<Barrier>& list) {
        if (list.empty()) return;
        std::vector<VkImageMemoryBarrier2>& images = ph.imageBarriers;
        std::vector<VkBufferMemoryBarrier2>& buffers = ph.bufferBarriers;
        images.clear();
        buffers.clear();
        // Each image's state as this list starts (what an Opaque pass left): every mip's barrier reads it from here.
        std::vector<Physical::ImageState>& before = ph.before;
        before.clear();
        for (const Barrier& b : list)
            if (!b.buffer) {
                const vk::Image* image = ph.images[b.copy][b.resource - 1];
                if (std::none_of(before.begin(), before.end(), [&](const Physical::ImageState& s) { return s.image == image; }))
                    before.push_back({image, image->layout, image->stage, image->access});
            }
        for (const Barrier& b : list) {
            if (b.buffer) {
                VkBufferMemoryBarrier2 barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                barrier.srcStageMask = b.srcStage;
                barrier.srcAccessMask = b.srcAccess;
                barrier.dstStageMask = b.dstStage;
                barrier.dstAccessMask = b.dstAccess;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = ph.buffers[b.resource - 1];
                barrier.offset = ph.bufferOffsets[b.resource - 1];
                barrier.size = VK_WHOLE_SIZE;
                buffers.push_back(barrier);
                continue;
            }
            vk::Image* image = ph.images[b.copy][b.resource - 1];
            const Physical::ImageState& was =
                *std::find_if(before.begin(), before.end(), [&](const Physical::ImageState& s) { return s.image == image; });
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = b.fromImage ? (was.stage ? was.stage : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT) : b.srcStage;
            barrier.srcAccessMask = b.fromImage ? (was.access & kWriteAccess) : b.srcAccess;
            barrier.oldLayout = b.fromImage ? was.layout : b.oldLayout;
            // UNDEFINED as the new layout: keep the image's (an Opaque use after another one).
            barrier.newLayout = b.newLayout == VK_IMAGE_LAYOUT_UNDEFINED ? barrier.oldLayout : b.newLayout;
            if (barrier.newLayout == VK_IMAGE_LAYOUT_UNDEFINED) continue;   // (kept, and never written: nothing to order)
            barrier.dstStageMask = b.dstStage;
            barrier.dstAccessMask = b.dstAccess;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->image;
            barrier.subresourceRange = {image->aspect, b.mip, b.mips, 0, VK_REMAINING_ARRAY_LAYERS};
            images.push_back(barrier);
            // The tracked state (whole image): a barrier over every mip that waited for every earlier use sets it; one
            // over some mips, or a read that waited for the last write only, adds to it (the reads since stay in it).
            const bool whole = b.mip == 0 && b.mips >= image->mips && b.replaces;
            image->layout = barrier.newLayout;
            image->stage = whole ? b.dstStage : image->stage | b.dstStage;
            image->access = whole ? b.dstAccess : image->access | b.dstAccess;
        }
        if (images.empty() && buffers.empty()) return;
        VkDependencyInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        info.imageMemoryBarrierCount = static_cast<uint32_t>(images.size());
        info.pImageMemoryBarriers = images.data();
        info.bufferMemoryBarrierCount = static_cast<uint32_t>(buffers.size());
        info.pBufferMemoryBarriers = buffers.data();
        vkCmdPipelineBarrier2(command, &info);
    };

    for (uint32_t i = 0; i < compiled.passes.size(); ++i) {
        const PlanPass& step = compiled.passes[i];
        PassNode& node = *passes[step.pass];
        apply(step.barriers);
        for (const Barrier& m : node.merged) {   // reads joining a read: the tracked state gets their stages
            if (m.buffer) continue;
            vk::Image& image = *ph.images[m.copy][m.resource - 1];
            image.stage |= m.dstStage;
            image.access |= m.dstAccess;
        }
        if (ph.timestamps) vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, slot.queries, 2 * i);
        const auto started = std::chrono::steady_clock::now();
        {
            static std::map<const char*, cpuprofile::Slot*> scopes;   // (the render thread's)
            cpuprofile::Slot*& s = scopes[node.name];
            const bool scoped = passScopes && cpuprofile::enabled();
            if (!s && scoped) s = &cpuprofile::slot(node.name);
            cpuprofile::Scope scope(scoped ? s : nullptr);
            PassContext context(*this, frame, step.pass);
            context.command = command;
            context.pipelines = pipelines;
            if (node.execute) node.execute(context);
        }
        slot.cpu[i] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
        if (slot.names[i] != node.name) slot.names[i] = node.name;
        if (ph.timestamps) vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, slot.queries, 2 * i + 1);
        // A history's new copy, once written, is what this frame leaves for the next.
        for (const Use_& u : node.uses)
            if (!u.buffer && u.versionOut && resources[u.resource - 1].kind == Resource::Kind::History)
                ph.histories[resources[u.resource - 1].name].written = true;
    }
    apply(compiled.finalBarriers);
    // What each import and history copy ends the frame in (Plan::exports), for the next frame and code outside the
    // graph; an Opaque pass's own state stays.
    for (const Plan::Export& e : compiled.exports) {
        if (e.fromImage) continue;
        vk::Image& image = *ph.images[e.copy][e.resource - 1];
        image.layout = e.layout;
        image.stage = e.stage;
        image.access = e.access;
    }
    slot.timed = ph.timestamps && queries > 0;
    if (slot.timed) {
        const VkDeviceSize bytes = VkDeviceSize(queries) * sizeof(uint64_t);
        if (slot.results.size < bytes) {   // (the slot's last frame is done: its buffer is free)
            vk::destroy(*device, slot.results);
            slot.results = vk::createBuffer(*device, std::max<VkDeviceSize>(bytes, 64 * sizeof(uint64_t)), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        }
        vkCmdCopyQueryPoolResults(command, slot.queries, 0, queries, slot.results.buffer, 0, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        VkMemoryBarrier2 toHost{};
        toHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &toHost;
        vkCmdPipelineBarrier2(command, &dependency);
    }
    for (auto& [name, h] : ph.histories)
        if (h.written) {
            h.previous = 1 - h.previous;
            h.previousValid = true;
        }
}

const std::vector<PassTiming>& Graph::timings(uint32_t slotIndex) {
    static const std::vector<PassTiming> none;
    if (!physical) return none;   // (a compile-only graph)
    Physical::Slot& slot = physical->slots[slotIndex % 2];
    std::vector<uint64_t>& stamps = slot.stamps;
    stamps.assign(slot.names.size() * 2, 0);
    // (the copy the frame's command buffer made: its fence has signalled, so it is complete)
    const bool gpu = slot.timed && !stamps.empty() && slot.results.mapped && slot.results.size >= stamps.size() * sizeof(uint64_t);
    if (gpu) std::memcpy(stamps.data(), slot.results.mapped, stamps.size() * sizeof(uint64_t));
    slot.timings.resize(slot.names.size());   // (the names assigned in place: no allocation once they fit)
    for (size_t i = 0; i < slot.names.size(); ++i) {
        PassTiming& t = slot.timings[i];
        if (t.name != slot.names[i]) t.name = slot.names[i];
        t.cpuMicroseconds = slot.cpu[i];
        t.gpuMicroseconds = gpu ? static_cast<double>(stamps[2 * i + 1] - stamps[2 * i]) * physical->timestampPeriod / 1000.0 : -1.0;
    }
    return slot.timings;
}

void Graph::destroy() {
    if (!device || !physical) return;
    Physical& ph = *physical;
    for (Physical::Slot& slot : ph.slots) {
        for (auto& c : slot.images) {
            c->image.memory = VK_NULL_HANDLE;
            vk::destroy(*device, c->image);
        }
        for (auto& c : slot.buffers) if (c.buffer) vkDestroyBuffer(device->device, c.buffer, nullptr);
        for (auto& a : slot.arenas) if (a.memory) vkFreeMemory(device->device, a.memory, nullptr);
        vk::destroy(*device, slot.ring);
        vk::destroy(*device, slot.results);
        if (slot.queries) vkDestroyQueryPool(device->device, slot.queries, nullptr);
        slot = Physical::Slot{};
    }
    for (auto& [name, h] : ph.histories)
        for (auto& image : h.image) vk::destroy(*device, image);
    ph.histories.clear();
}

// --- Pass context --------------------------------------------------------------------------------------------------

vk::Image& PassContext::image(Texture t) { return graph.imageOf(t.id, t.version, pass); }
VkBuffer PassContext::buffer(Buffer b) { return graph.physical->buffers[b.id - 1]; }
VkDeviceSize PassContext::offset(Buffer b) { return graph.physical->bufferOffsets[b.id - 1]; }
void* PassContext::mapped(Buffer b) { return graph.physical->mapped[b.id - 1]; }

void PassContext::beginRendering(std::initializer_list<Texture> colours, Texture depth, const VkClearValue* clears) {
    const Graph::PassNode& node = *graph.passes[pass];
    auto declared = [&](Texture t) {
        for (const Graph::Use_& u : node.uses)
            if (!u.buffer && u.resource == t.id && (u.versionOut == t.version || u.versionIn == t.version)) return u.use;
        return Use::ColorReadWrite;
    };
    std::vector<VkRenderingAttachmentInfo> attachments;
    VkExtent2D extent{};
    uint32_t clearIndex = 0;
    for (Texture t : colours) {
        vk::Image& image = this->image(t);
        if (!extent.width) extent = image.extent;
        VkRenderingAttachmentInfo a{};
        a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        a.imageView = image.view;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const Use use = declared(t);
        a.loadOp = use == Use::ColorWrite ? (clears ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE) : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (clears) a.clearValue = clears[clearIndex];
        ++clearIndex;
        attachments.push_back(a);
    }
    VkRenderingAttachmentInfo depthAttachment{};
    if (depth) {
        vk::Image& image = this->image(depth);
        if (!extent.width) extent = image.extent;
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = image.view;
        const Use use = declared(depth);
        depthAttachment.imageLayout = use == Use::DepthRead ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = use == Use::DepthWrite ? (clears ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE) : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = use == Use::DepthRead ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
        if (clears) depthAttachment.clearValue = clears[clearIndex];
    }
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, extent};
    info.layerCount = 1;
    info.colorAttachmentCount = static_cast<uint32_t>(attachments.size());
    info.pColorAttachments = attachments.data();
    info.pDepthAttachment = depth ? &depthAttachment : nullptr;
    vkCmdBeginRendering(command, &info);
}

void PassContext::endRendering() { vkCmdEndRendering(command); }

}  // namespace gt4::rg
