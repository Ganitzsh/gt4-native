#pragma once
// The render graph (docs/modern/CONTRACTS.md (b)): one graph per presented frame, built on the render thread,
// compiled (culling, version checks, barriers, transient aliasing, async-compute edges), then recorded. It owns
// transient resources and their memory, barriers (synchronization2), GPU timestamps per pass, and the queue a pass
// runs on. It does not own the swapchain, the GS targets or the scene cache's meshes: those are imported.
//
// Additions to the contract's b.2 interface, for the frame loop and the CPU tests:
// - Graph::begin(frame) starts a frame (the frame info sizes Render / Output textures);
// - Graph(Backend&) runs compile() without a GPU (tests/render_graph.cpp); Graph(const vk::Device&) is the
//   Vulkan one, the only one that can execute();
// - Graph::plan() is the compiled result (live passes, barriers, placements, async edges, errors);
// - PassContext::pipelines is a pointer until the PipelineCache class lands (ticket B2b).
#include "vk.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gt4 { struct Settings; }
namespace gt4::scene { struct SceneFrame; }

namespace gt4::rg {

enum class Queue : uint8_t { Graphics, Compute };   // Compute: async when the device has a compute-only family

enum class Size : uint8_t { Absolute, Render, Output };   // Render / Output: times scale, from FrameInfo
struct TextureDesc {
    VkFormat format = VK_FORMAT_UNDEFINED;
    Size size = Size::Render;
    float scaleX = 1.0f, scaleY = 1.0f;     // Render / Output
    VkExtent2D extent{};                    // Absolute
    uint32_t mips = 1, layers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool cube = false;
    const char* name = "";
};
struct BufferDesc {
    VkDeviceSize bytes = 0;
    bool hostVisible = false;               // uploaded each frame (per-slot ring)
    const char* name = "";
};

struct Texture { uint32_t id = 0; uint32_t version = 0; explicit operator bool() const { return id != 0; } };
struct Buffer  { uint32_t id = 0; uint32_t version = 0; explicit operator bool() const { return id != 0; } };

enum class Use : uint8_t {
    Sampled,                                   // SHADER_READ_ONLY_OPTIMAL
    StorageRead, StorageWrite, StorageReadWrite,   // GENERAL
    ColorWrite,                                // attachment, previous contents not needed
    ColorReadWrite,                            // attachment, loaded (blending, partial writes)
    DepthWrite, DepthRead, DepthReadWrite,     // attachment
    TransferSrc, TransferDst,
    Present,
    Opaque,                                    // the pass synchronises the image itself (GS segments); see b.6
};
enum class BufferUse : uint8_t { Uniform, StorageRead, StorageWrite, StorageReadWrite, Vertex, Index, Indirect, TransferSrc, TransferDst };

struct Range { uint32_t mip = 0, mips = VK_REMAINING_MIP_LEVELS, layer = 0, layers = VK_REMAINING_ARRAY_LAYERS; };

// Whether a use writes, and whether it needs the resource's earlier contents.
bool writes(Use use);
bool reads(Use use);
bool writes(BufferUse use);
bool reads(BufferUse use);

struct FrameInfo {
    uint32_t slot = 0, frameIndex = 0;
    VkExtent2D render{}, output{};
    const Settings* settings = nullptr;
    const scene::SceneFrame* scene = nullptr;   // null on frames without scene records
};

class PipelineCache;   // ticket B2b
class Graph;
struct Resource;

class PassBuilder {
public:
    Texture create(const TextureDesc& desc);                         // transient
    Buffer create(const BufferDesc& desc);
    // Declares a use; a writing use returns the next version, which later passes must use.
    Texture use(Texture t, Use use, VkPipelineStageFlags2 stages, Range range = {});
    Buffer use(Buffer b, BufferUse use, VkPipelineStageFlags2 stages);
    void queue(Queue q);                     // default Graphics
    void keep();                             // side effects outside the graph: never culled

private:
    friend class Graph;
    PassBuilder(Graph& owner, uint32_t index) : graph(owner), pass(index) {}
    Graph& graph;
    uint32_t pass;
};

class PassContext {
public:
    VkCommandBuffer command = VK_NULL_HANDLE;
    const FrameInfo& frame;
    PipelineCache* pipelines = nullptr;
    vk::Image& image(Texture t);            // in the layout the pass declared
    VkBuffer buffer(Buffer b);
    VkDeviceSize offset(Buffer b);           // host-visible rings
    void* mapped(Buffer b);
    // Dynamic rendering over declared attachments; load = clear for ColorWrite / DepthWrite when clear is given.
    void beginRendering(std::initializer_list<Texture> colours, Texture depth, const VkClearValue* clears = nullptr);
    void endRendering();

private:
    friend class Graph;
    PassContext(Graph& owner, const FrameInfo& info, uint32_t index) : frame(info), graph(owner), pass(index) {}
    Graph& graph;
    uint32_t pass;
};

// What compile() needs from the device: memory requirements of the transients, and whether a separate compute
// queue exists. The Vulkan backend asks the driver; tests give fixed numbers.
struct MemoryNeeds { VkDeviceSize size = 0, alignment = 1; uint32_t typeBits = ~0u; };
struct ImageInfo {   // a transient or history texture as it is created
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint32_t mips = 1, layers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool cube = false;
    VkImageUsageFlags usage = 0;
    bool operator==(const ImageInfo& o) const {
        return format == o.format && extent.width == o.extent.width && extent.height == o.extent.height && mips == o.mips &&
               layers == o.layers && samples == o.samples && cube == o.cube && usage == o.usage;
    }
};
class Backend {
public:
    virtual ~Backend() = default;
    virtual MemoryNeeds requirements(const ImageInfo& image) = 0;
    virtual MemoryNeeds requirements(VkDeviceSize bytes, VkBufferUsageFlags usage) = 0;
    virtual uint32_t memoryType(uint32_t typeBits) = 0;   // the device-local type a placement uses
    virtual bool asyncCompute() const = 0;                 // a compute-only queue family, and GT4_RG_ASYNC != 0
};

// --- The compiled plan -----------------------------------------------------------------------------------------
struct Barrier {
    uint32_t resource = 0;            // Texture::id / Buffer::id
    bool buffer = false;
    uint32_t copy = 0;                // a history's image: 0 the previous frame's, 1 this frame's (else 0)
    uint32_t mip = 0, mips = 1;       // textures
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 srcStage = 0, dstStage = 0;
    VkAccessFlags2 srcAccess = 0, dstAccess = 0;
    bool fromImage = false;           // old state read from vk::Image when executed (after an Opaque use)
    bool replaces = true;             // its dst becomes vk::Image's state; false for a read that waited for the last
                                      // write only (the reads since stay: the state gets its stages added)
};
struct Placement {                    // a transient's memory: per frame slot, one arena per memory type
    uint32_t resource = 0;
    bool buffer = false;
    uint32_t memoryType = 0;
    VkDeviceSize offset = 0, size = 0;
    uint32_t first = 0, last = 0;     // lifetime, in live-pass order (indices into Plan::passes)
};
struct AsyncEdge {                    // a timeline semaphore: `signal` after pass `after`, waited before pass `before`
    uint32_t after = 0, before = 0;   // indices into Plan::passes
    bool computeToGraphics = true;
};
struct PlanPass {
    uint32_t pass = 0;                // index in declaration order
    std::string name;
    Queue queue = Queue::Graphics;
    bool async = false;               // runs on the compute queue
    std::vector<Barrier> barriers;    // flushed once at the start of the pass
};
struct Plan {
    std::vector<PlanPass> passes;     // live passes in order
    std::vector<std::string> culled;
    std::vector<Placement> placements;
    std::vector<AsyncEdge> edges;
    std::vector<Barrier> finalBarriers;   // imports whose mips end in different layouts: made whole again
    // What each import and history copy ends the frame in, written to its vk::Image after execute: the layout, and
    // the stages and accesses a later use must wait for (the union over its mips: the last write and every read since).
    // fromImage: an Opaque pass decides (vk::Image keeps what it left).
    struct Export {
        uint32_t resource = 0, copy = 0;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 stage = 0;
        VkAccessFlags2 access = 0;
        bool fromImage = false;
    };
    std::vector<Export> exports;
    VkDeviceSize transientBytes = 0, transientBytesWithoutAliasing = 0;
    // The last compiled frame's plan, used again: its passes, their uses and everything the plan reads (the resources'
    // descriptions, the imports' and history copies' states, the frame's sizes) were the same, so compile() was skipped.
    bool reused = false;
    std::vector<std::string> errors;
    bool ok() const { return errors.empty(); }
};

// Per-pass timings of a slot's last execution (GT4_CPU_SCOPE for the CPU time).
struct PassTiming { std::string name; double gpuMicroseconds = -1.0, cpuMicroseconds = 0.0; };

class Graph {
public:
    explicit Graph(const vk::Device& device);
    explicit Graph(Backend& backend);       // compile only (tests)
    ~Graph();
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    void begin(const FrameInfo& frame);     // a new frame: drops the last frame's passes and transients
    Texture import(vk::Image& image, const char* name);   // state taken from image.layout / stage / access, written back after execute
    Buffer import(vk::Buffer& buffer, const char* name);
    Texture history(const char* name, const TextureDesc& desc);   // persists across frames (two copies, swapped per frame); never aliased
    // Whether a history's version 0 holds what the last frame wrote (false on its first frame, after a resize or a
    // format change, and on a compile-only graph). Known when history() returns; final after compile().
    bool historyValid(Texture t) const;
    template <class Setup, class Execute> void pass(const char* name, Setup&& setup, Execute&& execute) {
        PassBuilder builder = addPass(name, std::function<void(PassContext&)>(std::forward<Execute>(execute)));
        setup(builder);
    }
    void compile();
    void execute(VkCommandBuffer graphics, VkQueue computeQueue /* null: none */);
    std::string dump() const;                              // GT4_RG_DUMP=1: the compiled graph as Graphviz

    const Plan& plan() const { return compiled; }
    // Frames that used the last compiled plan again, and frames that compiled one (Plan::reused), since the graph was made.
    std::pair<uint64_t, uint64_t> planCounts() const;
    void setAbortOnError(bool abort) { abortOnError = abort; }   // default: abort in debug builds
    // GT4_CPU_PROFILE's scope per pass, named after it (default on). Off for a graph whose work its caller already times
    // as one stage (GsnScene::read's: "main: a read the game waits for"), so a read's passes are not counted again.
    void setPassScopes(bool on) { passScopes = on; }
    void setPipelineCache(PipelineCache* cache) { pipelines = cache; }
    // After the slot's fence: its last execution's timestamps (GPU -1 when the slot has none yet).
    const std::vector<PassTiming>& timings(uint32_t slot);
    void destroy();                                        // the device's objects (before the device)

private:
    friend class PassBuilder;
    friend class PassContext;
    struct Use_;
    struct PassNode;
    struct Physical;
    struct Cache;
    void signature(std::vector<uint64_t>& key) const;      // what compile()'s result depends on
    void compileFresh();                                   // compile() without the cache
    PassBuilder addPass(const char* name, std::function<void(PassContext&)> execute);
    uint32_t addResource(Resource resource);
    Texture useTexture(uint32_t pass, Texture t, Use use, VkPipelineStageFlags2 stages, Range range);
    Buffer useBuffer(uint32_t pass, Buffer b, BufferUse use, VkPipelineStageFlags2 stages);
    void error(std::string text);
    ImageInfo imageInfo(const Resource& r) const;
    void planBarriers();
    void placeTransients();
    void planAsync();
    void prepareHistories();                               // Vulkan: history copies made (again) before the plan
    void realise();                                        // Vulkan: images, buffers, memory for the plan
    vk::Image& imageOf(uint32_t resource, uint32_t version, uint32_t pass);
    const Resource& resourceOf(uint32_t id) const;

    const vk::Device* device = nullptr;
    std::unique_ptr<Backend> ownBackend;
    Backend* backend = nullptr;
    FrameInfo frame{};
    std::vector<Resource> resources;        // index = id - 1
    std::vector<std::unique_ptr<PassNode>> passes;
    Plan compiled;
    bool abortOnError;
    bool passScopes = true;
    PipelineCache* pipelines = nullptr;
    std::unique_ptr<Physical> physical;     // Vulkan objects kept across frames
    std::unique_ptr<Cache> cache;           // the last compiled plan and its signature
};

// Feature modules connect through typed slots, not through each other's headers.
class Blackboard {
public:
    template <class T> T& add() {            // one T per frame
        auto& slot = slots[key<T>()];
        slot = std::shared_ptr<void>(new T(), [](void* p) { delete static_cast<T*>(p); });
        return *static_cast<T*>(slot.get());
    }
    template <class T> const T* get() const { // null when the producer did not run
        auto it = slots.find(key<T>());
        return it == slots.end() ? nullptr : static_cast<const T*>(it->second.get());
    }
    void clear() { slots.clear(); }

private:
    template <class T> static const void* key() { static const char tag = 0; return &tag; }
    std::unordered_map<const void*, std::shared_ptr<void>> slots;
};

// The formats a feature builds its pipelines for (the scene targets').
struct SceneFormats {
    VkFormat colour = VK_FORMAT_UNDEFINED, depth = VK_FORMAT_UNDEFINED, velocity = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
};
struct FeatureConfig { const Settings* settings; SceneFormats formats; };

class Feature {
public:
    virtual ~Feature() = default;
    virtual const char* name() const = 0;
    virtual void prepare(PipelineCache& pipelines, const FeatureConfig& config) { (void)pipelines; (void)config; }   // on settings or format change
    virtual void build(Graph& graph, Blackboard& board, const FrameInfo& frame) = 0; // adds passes
};

}  // namespace gt4::rg
