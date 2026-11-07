// The cluster build (ticket F1): lights_cluster.comp on the GPU gives the same cells and index lists as
// buildClustersCpu, for random light sets (none, one, 64, 256, 1,024; spread out, and crowded so that cells
// overflow), and a few hand-made cases check the CPU reference itself. Also times the dispatch for 256 lights at
// a 1440 x 1080 extent (the ticket's budget: 0.2 ms). Needs a Vulkan 1.3 device with VK_KHR_push_descriptor;
// exits 77 (skipped) without one, after the CPU checks.
#ifndef GT4_LIGHTS_CLUSTER_GPU
#define GT4_LIGHTS_CLUSTER_GPU 1
#endif
#include "lights_cluster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "spv_lights_cluster.h"

using namespace gt4;
using namespace gt4::render;

namespace {

int failures = 0;
void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what.c_str());
        ++failures;
    }
}

constexpr int kSkip = 77;

// A 1440 x 1080 view with a 60 degree vertical field of view, window y down (view y up).
ClusterProjection testProjection() {
    const float w = 1440.0f, h = 1080.0f, fy = 0.5f * h / std::tan(0.5236f), fx = fy;
    return clusterProjection(fx, -fy, 0.5f * w, 0.5f * h, w, h);
}

GpuLight pointLight(float x, float y, float d, float range, uint32_t id) {
    GpuLight l{};
    l.position[0] = x;
    l.position[1] = y;
    l.position[2] = -d;   // GT4's view space looks down -z
    l.range = range;
    l.direction[2] = -1.0f;
    l.colour[0] = l.colour[1] = l.colour[2] = 1.0f;
    l.type = kPoint;
    l.flags = kFromGame;
    l.sourceRadius = 0.1f;
    l.id = id;
    return l;
}

std::vector<GpuLight> randomLights(std::mt19937& rng, uint32_t count, bool crowded) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    std::vector<GpuLight> lights;
    for (uint32_t i = 0; i < count; ++i) {
        const float d = crowded ? 8.0f + 4.0f * u(rng) : std::exp(std::log(0.05f) + std::log(3000.0f / 0.05f) * u(rng));
        const float spread = crowded ? 0.2f : 0.8f;
        GpuLight l = pointLight((2.0f * u(rng) - 1.0f) * spread * d, (2.0f * u(rng) - 1.0f) * spread * 0.75f * d, d,
                                crowded ? 6.0f + 4.0f * u(rng) : 0.5f + 40.0f * u(rng), i);
        if (u(rng) < 0.3f) {
            l.type = kSpot;
            l.spotScale = spotScale(0.95f, 0.8f);
            l.spotOffset = spotOffset(0.95f, 0.8f);
        }
        if (u(rng) < 0.05f) l.position[2] = 2.0f * u(rng);   // behind the camera
        lights.push_back(l);
    }
    return lights;
}

void checkCpuReference() {
    const LightListHeader header = makeHeader(1, 0, 0.1f, 2000.0f);
    const ClusterGrid grid = clusterGrid(header, testProjection());
    // the slice boundaries agree with clusterSlice
    for (uint32_t s = 1; s < kClusterZ; ++s) {
        const float z = grid.v[kGridDepth + s];
        expect(clusterSlice(z * 1.001f, header.sliceScale, header.sliceBias) == s &&
               clusterSlice(z * 0.999f, header.sliceScale, header.sliceBias) == s - 1,
               "grid slice boundary " + std::to_string(s) + " agrees with clusterSlice");
    }
    // a small light straight ahead at 10 m lands in the centre tiles of its slice, and nowhere else
    const GpuLight ahead = pointLight(0.0f, 0.0f, 10.0f, 0.05f, 1);
    ClusterResult r = buildClustersCpu(header, &ahead, grid);
    const uint32_t slice = clusterSlice(10.0f, header.sliceScale, header.sliceBias);
    uint32_t cellsHit = 0;
    bool right = true;
    for (uint32_t c = 0; c < kClusterCount; ++c) {
        if (!r.cells[c].count) continue;
        ++cellsHit;
        const uint32_t tx = c % kClusterX, ty = (c / kClusterX) % kClusterY, s = c / (kClusterX * kClusterY);
        right = right && s == slice && (tx == 7 || tx == 8) && ty == 4;
    }
    expect(cellsHit >= 1 && cellsHit <= 2 && right, "a small light ahead is in the centre cells of its slice");
    // Off-centre lights, placed by the window pixel they project to (x right, y down, 1440 x 1080, tiles of
    // 90 x 120 pixels). A mirrored x or y mapping in clusterProjection / clusterGrid would put them in the opposite
    // corner, and the CPU and GPU builds would agree on the mistake. Each must reach its own tile, and every cell
    // it reaches must be on the same side of the centre in x and in y. (It may also reach one or two neighbouring
    // tiles: the build tests a light against each froxel's view-space box, which is wider than the froxel, more
    // so towards the edges of the view; a slice here spans 1.5 times in depth.)
    const float f = 0.5f * 1080.0f / std::tan(0.5236f);
    const struct { float px, py; uint32_t tx, ty; const char* name; } corners[] = {
        {72.0f, 60.0f, 0, 0, "top left"},          {1400.0f, 60.0f, 15, 0, "top right"},
        {72.0f, 1020.0f, 0, 8, "bottom left"},      {1400.0f, 1020.0f, 15, 8, "bottom right"},
        {300.0f, 400.0f, 3, 3, "left of centre and above it"},
    };
    for (const auto& k : corners) {
        const float d = 10.0f;
        // window x = f * x / d + 720; window y = 540 - f * y / d (view y up)
        const GpuLight l = pointLight((k.px - 720.0f) / f * d, (540.0f - k.py) / f * d, d, 0.01f, 4);
        r = buildClustersCpu(header, &l, grid);
        bool own = false, sameSide = true;
        uint32_t minX = kClusterX, maxX = 0;
        std::string hit;
        for (uint32_t c = 0; c < kClusterCount; ++c)
            if (r.cells[c].count) {
                const uint32_t tx = c % kClusterX, ty = (c / kClusterX) % kClusterY;
                own = own || (tx == k.tx && ty == k.ty);
                sameSide = sameSide && (tx < kClusterX / 2) == (k.tx < kClusterX / 2) && (2 * ty < kClusterY) == (2 * k.ty < kClusterY);
                minX = std::min(minX, tx);
                maxX = std::max(maxX, tx);
                hit += " (" + std::to_string(tx) + "," + std::to_string(ty) + ")";
            }
        expect(own && sameSide && maxX - minX <= 2,
               std::string("a light at the ") + k.name + " of the window reaches tile (" + std::to_string(k.tx) + ", " +
                   std::to_string(k.ty) + ") and only tiles on its side; reached:" + hit);
    }
    // a light behind the camera, out of reach of the view, is in no cell
    const GpuLight behind = pointLight(0.0f, 0.0f, -5.0f, 1.0f, 2);
    r = buildClustersCpu(header, &behind, grid);
    expect(r.stats.pairs == 0, "a light behind the camera reaches no cell");
    // a light at the camera with a 1 m range reaches only the first slices
    const GpuLight here = pointLight(0.0f, 0.0f, 0.0f, 1.0f, 3);
    r = buildClustersCpu(header, &here, grid);
    bool near = r.stats.pairs > 0;
    for (uint32_t c = 0; c < kClusterCount; ++c)
        if (r.cells[c].count) near = near && grid.v[kGridDepth + c / (kClusterX * kClusterY)] <= 1.0f;
    expect(near, "a light at the camera reaches only cells that start within its range");
    // directional lights are skipped, the cap holds and overflow counts the rest
    std::vector<GpuLight> many(200, pointLight(0.0f, 0.0f, 10.0f, 5.0f, 0));
    many[0].type = kDirectional;
    LightListHeader h2 = makeHeader(200, 1, 0.1f, 2000.0f);
    r = buildClustersCpu(h2, many.data(), grid);
    expect(r.stats.maxCount == 199 && r.stats.overflow > 0, "199 point lights counted in a cell, the directional one not");
    bool capped = true, ordered = true;
    for (uint32_t c = 0; c < kClusterCount; ++c) {
        capped = capped && r.cells[c].count <= kMaxLightsPerCluster && r.cells[c].offset == c * kMaxLightsPerCluster;
        for (uint32_t k = 1; k < r.cells[c].count; ++k)
            ordered = ordered && r.indices[r.cells[c].offset + k - 1] < r.indices[r.cells[c].offset + k];
        if (r.cells[c].count) ordered = ordered && r.indices[r.cells[c].offset] >= 1;
    }
    expect(capped && ordered, "cells capped at kMaxLightsPerCluster, indices ascending, the directional light skipped");
}

// With VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation (the VAL acceptance), every warning and error the layer
// reports is counted and fails the test.
int validationMessages = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL onValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
                                            void*) {
    if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        ++validationMessages;
        std::printf("validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}

// The loader's extensions, or the validation layer's (it brings VK_EXT_debug_utils when enabled by environment).
bool instanceHas(const char* name) {
    for (const char* layer : {static_cast<const char*>(nullptr), "VK_LAYER_KHRONOS_validation"}) {
        uint32_t n = 0;
        if (vkEnumerateInstanceExtensionProperties(layer, &n, nullptr) != VK_SUCCESS) continue;
        std::vector<VkExtensionProperties> list(n);
        vkEnumerateInstanceExtensionProperties(layer, &n, list.data());
        for (const auto& e : list)
            if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

struct Gpu {
    vk::Device device;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    float timestampPeriod = 0.0f;
    bool timestamps = false;
    // The device goes first, so the layer's reports about objects left alive are counted before the check.
    void closeDevice() {
        if (queries) vkDestroyQueryPool(device.device, queries, nullptr);
        if (pool) vkDestroyCommandPool(device.device, pool, nullptr);
        if (device.device) vkDestroyDevice(device.device, nullptr);
        queries = VK_NULL_HANDLE;
        pool = VK_NULL_HANDLE;
        device.device = VK_NULL_HANDLE;
    }
    ~Gpu() {
        closeDevice();
        if (messenger) vkDestroyDebugUtilsMessengerEXT(device.instance, messenger, nullptr);
        if (device.instance) vkDestroyInstance(device.instance, nullptr);
    }
};

bool hasExtension(VkPhysicalDevice d, const char* name) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(d, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> list(n);
    vkEnumerateDeviceExtensionProperties(d, nullptr, &n, list.data());
    for (const auto& e : list)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

bool openGpu(Gpu& g) {
    if (volkInitialize() != VK_SUCCESS) { std::printf("lights_cluster: no Vulkan loader\n"); return false; }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "gt4_lights_cluster_test";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    const bool debug = instanceHas(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    const char* instanceExtensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    if (debug) {
        ici.enabledExtensionCount = 1;
        ici.ppEnabledExtensionNames = instanceExtensions;
    }
    if (vkCreateInstance(&ici, nullptr, &g.device.instance) != VK_SUCCESS) { std::printf("lights_cluster: no instance\n"); return false; }
    volkLoadInstance(g.device.instance);
    if (debug && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT mci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mci.pfnUserCallback = onValidation;
        vkCreateDebugUtilsMessengerEXT(g.device.instance, &mci, nullptr, &g.messenger);
    }
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(g.device.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(g.device.instance, &count, devices.data());
    int best = -1;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3 || !hasExtension(d, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) continue;
        uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, families.data());
        for (uint32_t f = 0; f < n; ++f) {
            if (!(families[f].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
            const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                            : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > best) {
                best = score;
                g.device.physical = d;
                g.device.family = f;
                g.timestamps = families[f].timestampValidBits > 0;
            }
            break;
        }
    }
    if (!g.device.physical) { std::printf("lights_cluster: no Vulkan 1.3 device with push descriptors\n"); return false; }
    vkGetPhysicalDeviceProperties(g.device.physical, &g.device.properties);
    vkGetPhysicalDeviceMemoryProperties(g.device.physical, &g.device.memory);
    g.timestampPeriod = g.device.properties.limits.timestampPeriod;
    std::printf("lights_cluster: on %s\n", g.device.properties.deviceName);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = g.device.family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.synchronization2 = VK_TRUE;
    const char* extensions[] = {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &v13;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = extensions;
    if (vkCreateDevice(g.device.physical, &dci, nullptr, &g.device.device) != VK_SUCCESS) { std::printf("lights_cluster: no device\n"); return false; }
    volkLoadDevice(g.device.device);
    vkGetDeviceQueue(g.device.device, g.device.family, 0, &g.device.queue);
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = g.device.family;
    vk::check(vkCreateCommandPool(g.device.device, &cpi, nullptr, &g.pool), "command pool");
    if (g.timestamps) {
        VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpi.queryCount = 2;
        vk::check(vkCreateQueryPool(g.device.device, &qpi, nullptr, &g.queries), "query pool");
    }
    return true;
}

struct Run {
    ClusterResult result;
    double gpuMs = -1.0;
};

// Runs the GPU build `repeats` times in one command buffer (timed: the last one) and reads the last result back.
Run runGpu(Gpu& g, const LightClusters& clusters, const LightListHeader& header, const std::vector<GpuLight>& lights,
           const ClusterGrid& grid, uint32_t repeats = 1) {
    const vk::Device& d = g.device;
    const VkBufferUsageFlags use = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkDeviceSize lightsBytes = sizeof(GpuLight) * std::max<size_t>(lights.size(), 1);
    vk::Buffer bHeader = vk::createBuffer(d, sizeof header, use, true);
    vk::Buffer bLights = vk::createBuffer(d, lightsBytes, use, true);
    vk::Buffer bGrid = vk::createBuffer(d, sizeof grid, use, true);
    vk::Buffer bCells = vk::createBuffer(d, sizeof(ClusterCell) * kClusterCount, use, true, true);
    vk::Buffer bIndices = vk::createBuffer(d, sizeof(uint32_t) * kClusterCount * kMaxLightsPerCluster, use, true, true);
    vk::Buffer bStats = vk::createBuffer(d, sizeof(ClusterStats), use | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
    std::memcpy(bHeader.mapped, &header, sizeof header);
    if (!lights.empty()) std::memcpy(bLights.mapped, lights.data(), sizeof(GpuLight) * lights.size());
    std::memcpy(bGrid.mapped, &grid, sizeof grid);
    std::memset(bCells.mapped, 0xFF, sizeof(ClusterCell) * kClusterCount);
    // 0xFF: 0 is a valid light index, so unwritten room must not look like one
    std::memset(bIndices.mapped, 0xFF, sizeof(uint32_t) * kClusterCount * kMaxLightsPerCluster);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = g.pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vk::check(vkAllocateCommandBuffers(d.device, &cbai, &cmd), "command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    if (g.timestamps) vkCmdResetQueryPool(cmd, g.queries, 0, 2);
    VkMemoryBarrier2 between{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    between.srcStageMask = between.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    between.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    between.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &between;
    for (uint32_t i = 0; i < repeats; ++i) {
        if (i + 1 == repeats) {
            // the stats of the last run only
            vkCmdFillBuffer(cmd, bStats.buffer, 0, sizeof(ClusterStats), 0);
            VkMemoryBarrier2 fill{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            fill.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            fill.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            fill.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            fill.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            VkDependencyInfo fd{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            fd.memoryBarrierCount = 1;
            fd.pMemoryBarriers = &fill;
            vkCmdPipelineBarrier2(cmd, &fd);
            if (g.timestamps) vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, g.queries, 0);
        }
        clusters.record(cmd, bHeader.buffer, bLights.buffer, lightsBytes, bGrid.buffer, bCells.buffer, bIndices.buffer,
                        bStats.buffer);
        if (i + 1 == repeats && g.timestamps) vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, g.queries, 1);
        else vkCmdPipelineBarrier2(cmd, &dep);
    }
    VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    host.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    host.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo hd{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    hd.memoryBarrierCount = 1;
    hd.pMemoryBarriers = &host;
    vkCmdPipelineBarrier2(cmd, &hd);
    vkEndCommandBuffer(cmd);
    VkCommandBufferSubmitInfo cbsi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbsi.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cbsi;
    vk::check(vkQueueSubmit2(d.queue, 1, &submit, VK_NULL_HANDLE), "submit");
    vk::check(vkQueueWaitIdle(d.queue), "wait");

    Run run;
    run.result.cells.resize(kClusterCount);
    std::memcpy(run.result.cells.data(), bCells.mapped, sizeof(ClusterCell) * kClusterCount);
    run.result.indices.resize(size_t(kClusterCount) * kMaxLightsPerCluster);
    std::memcpy(run.result.indices.data(), bIndices.mapped, sizeof(uint32_t) * run.result.indices.size());
    std::memcpy(&run.result.stats, bStats.mapped, sizeof(ClusterStats));
    if (g.timestamps) {
        uint64_t t[2] = {0, 0};
        if (vkGetQueryPoolResults(d.device, g.queries, 0, 2, sizeof t, t, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
            run.gpuMs = double(t[1] - t[0]) * g.timestampPeriod * 1e-6;
    }
    vkFreeCommandBuffers(d.device, g.pool, 1, &cmd);
    for (vk::Buffer* b : {&bHeader, &bLights, &bGrid, &bCells, &bIndices, &bStats}) vk::destroy(d, *b);
    return run;
}

void compare(const ClusterResult& cpu, const ClusterResult& gpu, const std::string& name) {
    uint32_t cellsDiffer = 0, listsDiffer = 0;
    for (uint32_t c = 0; c < kClusterCount; ++c) {
        if (cpu.cells[c].offset != gpu.cells[c].offset || cpu.cells[c].count != gpu.cells[c].count) {
            ++cellsDiffer;
            continue;
        }
        const uint32_t* a = &cpu.indices[cpu.cells[c].offset];
        const uint32_t* b = &gpu.indices[gpu.cells[c].offset];
        if (std::memcmp(a, b, sizeof(uint32_t) * cpu.cells[c].count) != 0) ++listsDiffer;
    }
    expect(cellsDiffer == 0 && listsDiffer == 0,
           name + ": GPU cells equal the CPU reference (" + std::to_string(cellsDiffer) + " cells, " +
               std::to_string(listsDiffer) + " index lists differ)");
    expect(cpu.stats.pairs == gpu.stats.pairs && cpu.stats.overflow == gpu.stats.overflow &&
               cpu.stats.maxCount == gpu.stats.maxCount,
           name + ": GPU stats equal the CPU's (pairs " + std::to_string(gpu.stats.pairs) + " / " +
               std::to_string(cpu.stats.pairs) + ", overflow " + std::to_string(gpu.stats.overflow) + " / " +
               std::to_string(cpu.stats.overflow) + ", max " + std::to_string(gpu.stats.maxCount) + " / " +
               std::to_string(cpu.stats.maxCount) + ")");
}

}  // namespace

int main() {
    checkCpuReference();
    Gpu g;
    try {
        if (!openGpu(g)) {
            std::printf("lights_cluster: skipped (no GPU); CPU checks %s\n", failures ? "failed" : "passed");
            return failures ? 1 : kSkip;
        }
        LightClusters clusters;
        clusters.create(g.device, {spv_lights_cluster, sizeof(spv_lights_cluster)});
        const ClusterProjection projection = testProjection();
        std::mt19937 rng(11);
        struct Set { const char* name; uint32_t count, directional; bool crowded; };
        const Set sets[] = {{"no lights", 0, 0, false},  {"one light", 1, 0, false},   {"64 lights", 64, 1, false},
                            {"256 lights", 256, 1, false}, {"1,024 lights", 1024, 2, false},
                            {"256 crowded lights", 256, 0, true}};
        for (const Set& s : sets) {
            std::vector<GpuLight> lights = randomLights(rng, s.count, s.crowded);
            for (uint32_t i = 0; i < s.directional && i < lights.size(); ++i) lights[i].type = kDirectional;
            // a light with a NaN range and one with a negative range: skipped by both builds
            if (lights.size() >= 8) {
                lights[5].range = std::nanf("");
                lights[6].range = -1.0f;
            }
            const LightListHeader header = makeHeader(s.count, s.directional, 0.1f, 2000.0f);
            const ClusterGrid grid = clusterGrid(header, projection);
            const ClusterResult cpu = buildClustersCpu(header, lights.data(), grid);
            const Run gpu = runGpu(g, clusters, header, lights, grid);
            compare(cpu, gpu.result, s.name);
            std::printf("lights_cluster: %s: %u light-cell pairs, max %u in a cell, overflow %u\n", s.name,
                        cpu.stats.pairs, cpu.stats.maxCount, cpu.stats.overflow);
            if (s.crowded) expect(cpu.stats.overflow > 0, "the crowded set overflows some cells");
        }
        // timing: 256 lights, the build run 20 times back to back, the last one timed
        std::vector<GpuLight> lights = randomLights(rng, 256, false);
        const LightListHeader header = makeHeader(256, 0, 0.1f, 2000.0f);
        const ClusterGrid grid = clusterGrid(header, projection);
        const Run timed = runGpu(g, clusters, header, lights, grid, 20);
        compare(buildClustersCpu(header, lights.data(), grid), timed.result, "timed 256 lights");
        if (timed.gpuMs >= 0.0)
            std::printf("lights_cluster: 256 lights at 1440 x 1080: %.4f ms GPU (budget 0.2 ms)%s\n", timed.gpuMs,
                        timed.gpuMs > 0.2 ? " OVER BUDGET" : "");
        clusters.destroy(g.device);
        g.closeDevice();
        if (g.messenger) std::printf("lights_cluster: %d validation messages\n", validationMessages);
        expect(validationMessages == 0, "no validation messages");
    } catch (const std::exception& e) {
        std::printf("FAILED: %s\n", e.what());
        ++failures;
    }
    if (failures) {
        std::printf("lights_cluster: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("lights_cluster: all checks passed\n");
    return 0;
}
