// The light list contract (docs/modern/CONTRACTS.md (c), ticket F0): include/gt4/render/light_list.h and
// platform/render/shaders/light_list.glsl agree. A compute shader (tests/shaders/light_list_test.comp) writes a
// sentinel into every field of the GLSL structs in a std430 buffer; each must land at the C++ offsetof. The same
// shader evaluates the GLSL falloff, cone, slice and cell index on random cases, compared with the C++ ones.
// Needs a Vulkan device with a compute queue; exits 77 (skipped) without one.
#include <volk.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "gt4/render/light_list.h"
#include "spv_light_list_test.h"

using namespace gt4::render;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

constexpr int kSkip = 77;

struct Case { float falloff[4]; float cone[4]; };
struct Push { float sliceScale, sliceBias; uint32_t count; };

// The buffer the shader's Layout block describes: the header at 0, lights at 32, cells after them.
struct LayoutBlock {
    LightListHeader header;
    GpuLight lights[2];
    ClusterCell cells[2];
};
static_assert(offsetof(LayoutBlock, lights) == 32 && offsetof(LayoutBlock, cells) == 160 && sizeof(LayoutBlock) == 176);

uint32_t u32At(const uint8_t* base, size_t offset) { uint32_t v; std::memcpy(&v, base + offset, 4); return v; }
float f32At(const uint8_t* base, size_t offset) { float v; std::memcpy(&v, base + offset, 4); return v; }

void checkCpuFunctions() {
    expect(lightFalloff(10.0f, 10.0f, 0.1f) == 0.0f, "falloff is 0 at the range");
    expect(lightFalloff(20.0f, 10.0f, 0.1f) == 0.0f, "falloff is 0 beyond the range");
    expect(std::fabs(lightFalloff(0.0f, 10.0f, 0.5f) - 4.0f) < 1e-6f, "falloff is 1 / r^2 at the source");
    expect(std::fabs(lightFalloff(1.0f, 1000.0f, 0.0f) - 1.0f) < 1e-4f, "falloff is inverse square near the source");
    expect(std::isfinite(lightFalloff(0.0f, 10.0f, 0.0f)), "falloff at the source of a point light is finite");
    expect(lightFalloff(1.0f, 0.0f, 0.1f) == 0.0f && lightFalloff(0.0f, 0.0f, 0.0f) == 0.0f &&
           lightFalloff(1.0f, -1.0f, 0.1f) == 0.0f && lightFalloff(1.0f, std::nanf(""), 0.1f) == 0.0f,
           "a zero, negative or NaN range gives no light");
    const float s = spotScale(0.9f, 0.7f), o = spotOffset(0.9f, 0.7f);
    expect(lightCone(0.95f, s, o) == 1.0f && lightCone(0.6f, s, o) == 0.0f, "cone: 1 inside, 0 outside");
    expect(std::fabs(lightCone(0.8f, s, o) - 0.25f) < 1e-5f, "cone: (1/2)^2 halfway");
    // F7's profiles: none is 1; the street lamp lights the ground under a lamp pointing down evenly (the factor x the
    // inverse square x the ground's cosine is the same below the lamp and 60 degrees off its axis) and the same from
    // any height (a lamp at 4 m and one at 12 m), 1 / kStreetLampHeight^2; the cosine is held towards the horizon.
    expect(lightProfile(kProfileNone, 0.3f, 5.0f) == 1.0f && lightProfile(77u, 0.3f, 5.0f) == 1.0f, "no profile, or an unknown one: 1");
    expect(lightProfile(kProfileStreetLamp, 1.0f, kStreetLampHeight) == 1.0f, "street lamp: 1 on its axis at its height");
    {
        auto ground = [](float h, float c) {   // the light on level ground: the lamp h above it, the point at cosine c
            const float d = h / c;
            return lightProfile(kProfileStreetLamp, c, d) / (d * d) * c;
        };
        const float want = 1.0f / (kStreetLampHeight * kStreetLampHeight);
        expect(std::fabs(ground(8.0f, 1.0f) - want) < 1e-6f && std::fabs(ground(8.0f, 0.5f) - want) < 1e-6f,
               "street lamp: even ground light");
        expect(std::fabs(ground(4.0f, 0.7f) - want) < 1e-6f && std::fabs(ground(12.0f, 0.4f) - want) < 1e-6f,
               "street lamp: the same from any height");
    }
    expect(lightProfile(kProfileStreetLamp, 0.1f, 8.0f) == lightProfile(kProfileStreetLamp, kStreetLampMinCos, 8.0f) &&
           lightProfile(kProfileStreetLamp, -1.0f, 8.0f) == lightProfile(kProfileStreetLamp, kStreetLampMinCos, 8.0f),
           "street lamp: the cosine held towards the horizon");
    const LightListHeader h = makeHeader(0, 0, 0.1f, 1000.0f);
    expect(clusterSlice(0.1f * 1.0001f, h.sliceScale, h.sliceBias) == 0, "slice 0 at zNear");
    expect(clusterSlice(999.0f, h.sliceScale, h.sliceBias) == kClusterZ - 1, "last slice at zFar");
    expect(clusterSlice(0.01f, h.sliceScale, h.sliceBias) == 0 &&
           clusterSlice(1e6f, h.sliceScale, h.sliceBias) == kClusterZ - 1, "slices clamp outside zNear..zFar");
    expect(clusterIndex(kClusterX - 1, kClusterY - 1, kClusterZ - 1) == kClusterCount - 1, "last cell index");
}

struct Gpu {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    ~Gpu() {
        if (device) vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};

struct HostBuffer {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* data = nullptr;
    VkDeviceSize size = 0;
    ~HostBuffer() {
        if (memory) vkFreeMemory(device, memory, nullptr);
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
    }
};

bool makeBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size, HostBuffer& out) {
    out.device = device;
    out.size = size;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (vkCreateBuffer(device, &info, nullptr, &out.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, out.buffer, &req);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) { type = i; break; }
    if (type == UINT32_MAX) return false;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    if (vkAllocateMemory(device, &alloc, nullptr, &out.memory) != VK_SUCCESS) return false;
    if (vkBindBufferMemory(device, out.buffer, out.memory, 0) != VK_SUCCESS) return false;
    if (vkMapMemory(device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.data) != VK_SUCCESS) return false;
    std::memset(out.data, 0, size_t(size));
    return true;
}

// Runs the shader; false (skip) when there is no usable device.
bool runGpu(const std::vector<Case>& cases, const Push& push, std::vector<uint8_t>& layout, std::vector<float>& results) {
    if (volkInitialize() != VK_SUCCESS) { std::printf("light_list: no Vulkan loader\n"); return false; }
    Gpu gpu;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "gt4_light_list_test";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &gpu.instance) != VK_SUCCESS) { std::printf("light_list: no Vulkan instance\n"); return false; }
    volkLoadInstance(gpu.instance);
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(gpu.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(gpu.instance, &count, devices.data());
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t family = 0;
    int best = -1;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_2) continue;
        uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, families.data());
        for (uint32_t f = 0; f < n; ++f) {
            if (!(families[f].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
            const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                            : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > best) { best = score; physical = d; family = f; }
            break;
        }
    }
    if (!physical) { std::printf("light_list: no Vulkan 1.2 device with a compute queue\n"); return false; }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    std::printf("light_list: on %s\n", props.deviceName);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(physical, &dci, nullptr, &gpu.device) != VK_SUCCESS) { std::printf("light_list: no device\n"); return false; }
    volkLoadDevice(gpu.device);
    VkDevice device = gpu.device;
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);

    HostBuffer layoutBuffer, caseBuffer, resultBuffer;
    const VkDeviceSize caseBytes = sizeof(Case) * cases.size(), resultBytes = 16 * cases.size();
    if (!makeBuffer(physical, device, sizeof(LayoutBlock), layoutBuffer) || !makeBuffer(physical, device, caseBytes, caseBuffer) ||
        !makeBuffer(physical, device, resultBytes, resultBuffer)) {
        std::printf("FAILED: host-visible buffers\n");
        ++failures;
        return true;
    }
    std::memcpy(caseBuffer.data, cases.data(), size_t(caseBytes));

    VkDescriptorSetLayoutBinding bindings[3]{};
    for (uint32_t b = 0; b < 3; ++b) {
        bindings[b].binding = b;
        bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 3;
    dslci.pBindings = bindings;
    VkDescriptorSetLayout setLayout;
    vkCreateDescriptorSetLayout(device, &dslci, nullptr, &setLayout);
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &range;
    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout);
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = sizeof(spv_light_list_test);
    smci.pCode = spv_light_list_test;
    VkShaderModule module;
    vkCreateShaderModule(device, &smci, nullptr, &module);
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = module;
    cpci.stage.pName = "main";
    cpci.layout = pipelineLayout;
    VkPipeline pipeline;
    const bool built = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline) == VK_SUCCESS;
    expect(built, "compute pipeline");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &poolSize;
    VkDescriptorPool pool;
    vkCreateDescriptorPool(device, &dpci, nullptr, &pool);
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &setLayout;
    VkDescriptorSet set;
    vkAllocateDescriptorSets(device, &dsai, &set);
    VkDescriptorBufferInfo infos[3] = {{layoutBuffer.buffer, 0, VK_WHOLE_SIZE}, {caseBuffer.buffer, 0, VK_WHOLE_SIZE},
                                       {resultBuffer.buffer, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t b = 0; b < 3; ++b) {
        writes[b] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[b].dstSet = set;
        writes[b].dstBinding = b;
        writes[b].descriptorCount = 1;
        writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[b].pBufferInfo = &infos[b];
    }
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);

    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.queueFamilyIndex = family;
    VkCommandPool commandPool;
    vkCreateCommandPool(device, &cpi, nullptr, &commandPool);
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer command;
    vkAllocateCommandBuffers(device, &cbai, &command);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command, &begin);
    if (built) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
        vkCmdDispatch(command, (push.count + 63) / 64, 1, 1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier,
                             0, nullptr, 0, nullptr);
    }
    vkEndCommandBuffer(command);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    expect(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS, "submit");
    vkQueueWaitIdle(queue);

    layout.assign(static_cast<uint8_t*>(layoutBuffer.data), static_cast<uint8_t*>(layoutBuffer.data) + sizeof(LayoutBlock));
    results.resize(cases.size() * 4);
    std::memcpy(results.data(), resultBuffer.data, size_t(resultBytes));

    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    if (built) vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    return true;
}

void checkLayout(const std::vector<uint8_t>& bytes) {
    const uint8_t* b = bytes.data();
    char what[160];
    const size_t h = offsetof(LayoutBlock, header);
    const struct { size_t offset; uint32_t value; bool isFloat; const char* name; } header[] = {
        {offsetof(LightListHeader, count), 1001, false, "count"},
        {offsetof(LightListHeader, directionalCount), 1002, false, "directionalCount"},
        {offsetof(LightListHeader, zNear), 1003, true, "zNear"},
        {offsetof(LightListHeader, zFar), 1004, true, "zFar"},
        {offsetof(LightListHeader, sliceScale), 1005, true, "sliceScale"},
        {offsetof(LightListHeader, sliceBias), 1006, true, "sliceBias"},
        {offsetof(LightListHeader, clustersX), 1007, false, "clustersX"},
        {offsetof(LightListHeader, clustersY), 1008, false, "clustersY"},
    };
    for (const auto& f : header) {
        const bool ok = f.isFloat ? f32At(b, h + f.offset) == float(f.value) : u32At(b, h + f.offset) == f.value;
        std::snprintf(what, sizeof what, "LightListHeader::%s at offset %zu", f.name, f.offset);
        expect(ok, what);
    }
    for (uint32_t l = 0; l < 2; ++l) {
        const size_t base = offsetof(LayoutBlock, lights) + sizeof(GpuLight) * l;
        const uint32_t k = 2000 + 100 * l;
        const struct { size_t offset; uint32_t value; bool isFloat; const char* name; } light[] = {
            {offsetof(GpuLight, position) + 0, k + 0, true, "position.x"},
            {offsetof(GpuLight, position) + 4, k + 1, true, "position.y"},
            {offsetof(GpuLight, position) + 8, k + 2, true, "position.z"},
            {offsetof(GpuLight, range), k + 3, true, "range"},
            {offsetof(GpuLight, direction) + 0, k + 4, true, "direction.x"},
            {offsetof(GpuLight, direction) + 4, k + 5, true, "direction.y"},
            {offsetof(GpuLight, direction) + 8, k + 6, true, "direction.z"},
            {offsetof(GpuLight, spotScale), k + 7, true, "spotScale"},
            {offsetof(GpuLight, colour) + 0, k + 8, true, "colour.r"},
            {offsetof(GpuLight, colour) + 4, k + 9, true, "colour.g"},
            {offsetof(GpuLight, colour) + 8, k + 10, true, "colour.b"},
            {offsetof(GpuLight, spotOffset), k + 11, true, "spotOffset"},
            {offsetof(GpuLight, type), k + 12, false, "type"},
            {offsetof(GpuLight, flags), k + 13, false, "flags"},
            {offsetof(GpuLight, sourceRadius), k + 14, true, "sourceRadius"},
            {offsetof(GpuLight, id), k + 15, false, "id"},
        };
        for (const auto& f : light) {
            const bool ok = f.isFloat ? f32At(b, base + f.offset) == float(f.value) : u32At(b, base + f.offset) == f.value;
            std::snprintf(what, sizeof what, "lights[%u].%s at offset %zu (stride %zu)", l, f.name, f.offset, sizeof(GpuLight));
            expect(ok, what);
        }
        const size_t cell = offsetof(LayoutBlock, cells) + sizeof(ClusterCell) * l;
        std::snprintf(what, sizeof what, "cells[%u] offset and count", l);
        expect(u32At(b, cell + offsetof(ClusterCell, offset)) == 3000 + 10 * l &&
               u32At(b, cell + offsetof(ClusterCell, count)) == 3001 + 10 * l, what);
    }
}

}  // namespace

int main() {
    checkCpuFunctions();

    const LightListHeader header = makeHeader(0, 0, 0.1f, 2000.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<Case> cases(1024);
    for (Case& c : cases) {
        const float range = 1.0f + 99.0f * unit(rng);
        c.falloff[0] = range * 1.2f * unit(rng);        // inside and beyond the range
        c.falloff[1] = range;
        c.falloff[2] = 0.05f + 0.5f * unit(rng);
        c.falloff[3] = std::exp(std::log(0.05f) + std::log(4000.0f / 0.05f) * unit(rng));   // 0.05..4000 m
        const float cosOuter = 0.5f + 0.4f * unit(rng), cosInner = cosOuter + (1.0f - cosOuter) * unit(rng);
        c.cone[0] = 2.0f * unit(rng) - 1.0f;
        c.cone[1] = spotScale(cosInner, cosOuter);
        c.cone[2] = spotOffset(cosInner, cosOuter);
        c.cone[3] = 0.0f;
    }
    const Push push{header.sliceScale, header.sliceBias, uint32_t(cases.size())};
    std::vector<uint8_t> layout;
    std::vector<float> results;
    if (!runGpu(cases, push, layout, results)) {
        std::printf("light_list: skipped (no GPU); CPU checks %s\n", failures ? "failed" : "passed");
        return failures ? 1 : kSkip;
    }
    checkLayout(layout);

    int falloffBad = 0, coneBad = 0, sliceBad = 0, indexBad = 0;
    for (size_t i = 0; i < cases.size(); ++i) {
        const Case& c = cases[i];
        const float* r = &results[4 * i];
        const float f = lightFalloff(c.falloff[0], c.falloff[1], c.falloff[2]);
        if (std::fabs(r[0] - f) > 1e-5f * std::max(1.0f, std::fabs(f))) ++falloffBad;
        const float k = lightCone(c.cone[0], c.cone[1], c.cone[2]);
        if (std::fabs(r[1] - k) > 1e-5f) ++coneBad;
        const uint32_t s = clusterSlice(c.falloff[3], header.sliceScale, header.sliceBias);
        // the GPU's log may differ in the last bits: a slice off by one only right at a boundary is accepted
        const float exact = std::log(c.falloff[3]) * header.sliceScale + header.sliceBias;
        const bool atBoundary = std::fabs(exact - std::round(exact)) < 1e-3f;
        if (uint32_t(r[2]) != s && !atBoundary) ++sliceBad;
        const uint32_t idx = clusterIndex(uint32_t(i) % kClusterX, (uint32_t(i) / kClusterX) % kClusterY, uint32_t(r[2]));
        if (uint32_t(r[3]) != idx) ++indexBad;
    }
    char what[96];
    std::snprintf(what, sizeof what, "GLSL falloff equals C++ (%d of %zu differ)", falloffBad, cases.size());
    expect(falloffBad == 0, what);
    std::snprintf(what, sizeof what, "GLSL cone equals C++ (%d differ)", coneBad);
    expect(coneBad == 0, what);
    std::snprintf(what, sizeof what, "GLSL slice equals C++ (%d differ)", sliceBad);
    expect(sliceBad == 0, what);
    std::snprintf(what, sizeof what, "GLSL cell index equals C++ (%d differ)", indexBad);
    expect(indexBad == 0, what);

    if (failures) {
        std::printf("light_list: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("light_list: all checks passed\n");
    return 0;
}
