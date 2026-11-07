#include "lights_cluster.h"

#include <algorithm>
#include <cmath>

namespace gt4::render {

ClusterProjection clusterProjection(float fx, float fy, float cx, float cy, float width, float height) {
    return {fx * float(kClusterX) / width, cx * float(kClusterX) / width,
            fy * float(kClusterY) / height, cy * float(kClusterY) / height};
}

ClusterGrid clusterGrid(const LightListHeader& header, const ClusterProjection& p) {
    ClusterGrid g{};
    for (uint32_t i = 0; i <= kClusterX; ++i) g.v[kGridSlopeX + i] = (float(i) - p.offsetX) / p.scaleX;
    for (uint32_t i = 0; i <= kClusterY; ++i) g.v[kGridSlopeY + i] = (float(i) - p.offsetY) / p.scaleY;
    // slice s covers log(z) * sliceScale + sliceBias in [s, s + 1): z = exp((s - sliceBias) / sliceScale)
    g.v[kGridDepth] = 0.0f;
    for (uint32_t s = 1; s < kClusterZ; ++s)
        g.v[kGridDepth + s] = float(std::exp((double(s) - header.sliceBias) / header.sliceScale));
    g.v[kGridDepth + kClusterZ] = kClusterFar;
    return g;
}

namespace {

// The cell's view-space box in (x, y, d) and the squared distance from a point to it. Written as the shader
// writes it: the same operations in the same order.
struct Box { float lo[3], hi[3]; };

Box cellBox(const ClusterGrid& g, uint32_t tx, uint32_t ty, uint32_t s) {
    const float sx0 = std::min(g.v[kGridSlopeX + tx], g.v[kGridSlopeX + tx + 1]);
    const float sx1 = std::max(g.v[kGridSlopeX + tx], g.v[kGridSlopeX + tx + 1]);
    const float sy0 = std::min(g.v[kGridSlopeY + ty], g.v[kGridSlopeY + ty + 1]);
    const float sy1 = std::max(g.v[kGridSlopeY + ty], g.v[kGridSlopeY + ty + 1]);
    const float d0 = g.v[kGridDepth + s], d1 = g.v[kGridDepth + s + 1];
    Box b;
    b.lo[0] = std::min(sx0 * d0, sx0 * d1);
    b.hi[0] = std::max(sx1 * d0, sx1 * d1);
    b.lo[1] = std::min(sy0 * d0, sy0 * d1);
    b.hi[1] = std::max(sy1 * d0, sy1 * d1);
    b.lo[2] = d0;
    b.hi[2] = d1;
    return b;
}

// !(d2 > r2) rather than d2 <= r2 would differ for NaN; both builds write `d2 > r2` as the skip.
bool reaches(const Box& b, const GpuLight& l) {
    const float c[3] = {l.position[0], l.position[1], -l.position[2]};
    float d2 = 0.0f;
    for (int k = 0; k < 3; ++k) {
        const float e = std::max(std::max(b.lo[k] - c[k], 0.0f), c[k] - b.hi[k]);
        d2 = d2 + e * e;
    }
    return !(d2 > l.range * l.range);
}

}  // namespace

ClusterResult buildClustersCpu(const LightListHeader& header, const GpuLight* lights, const ClusterGrid& grid) {
    ClusterResult r;
    r.cells.resize(kClusterCount);
    r.indices.assign(size_t(kClusterCount) * kMaxLightsPerCluster, 0u);
    const uint32_t count = std::min(header.count, kMaxLights);
    for (uint32_t cell = 0; cell < kClusterCount; ++cell) {
        const uint32_t tx = cell % kClusterX, ty = (cell / kClusterX) % kClusterY, s = cell / (kClusterX * kClusterY);
        const Box b = cellBox(grid, tx, ty, s);
        const uint32_t base = cell * kMaxLightsPerCluster;
        uint32_t n = 0;
        for (uint32_t i = header.directionalCount; i < count; ++i) {
            // !(range >= 0): a negative or NaN range is no light (the shader's test is the same)
            if (lights[i].type == kDirectional || !(lights[i].range >= 0.0f) || !reaches(b, lights[i])) continue;
            if (n < kMaxLightsPerCluster) r.indices[base + n] = i;
            ++n;
        }
        const uint32_t kept = std::min(n, kMaxLightsPerCluster);
        r.cells[cell] = {base, kept};
        r.stats.pairs += kept;
        r.stats.overflow += n - kept;
        r.stats.maxCount = std::max(r.stats.maxCount, n);
    }
    return r;
}

}  // namespace gt4::render

#ifdef GT4_LIGHTS_CLUSTER_GPU
namespace gt4::render {

void LightClusters::create(const vk::Device& device, vk::ShaderCode code) {
    const VkDescriptorType s = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pipeline = vk::createCompute(device, code, {s, s, s, s, s, s}, 0, "lights_cluster");
}

void LightClusters::destroy(const vk::Device& device) { vk::destroy(device, pipeline); }

void LightClusters::record(VkCommandBuffer command, VkBuffer header, VkBuffer lights, VkDeviceSize lightsBytes,
                           VkBuffer grid, VkBuffer cells, VkBuffer indices, VkBuffer stats) const {
    vk::bind(command, pipeline);
    vk::Descriptors()
        .storageBuffer(0, header, 0, sizeof(LightListHeader))
        .storageBuffer(1, lights, 0, lightsBytes)
        .storageBuffer(2, grid, 0, sizeof(ClusterGrid))
        .storageBuffer(3, cells, 0, sizeof(ClusterCell) * kClusterCount)
        .storageBuffer(4, indices, 0, sizeof(uint32_t) * kClusterCount * kMaxLightsPerCluster)
        .storageBuffer(5, stats, 0, sizeof(ClusterStats))
        .push(command, pipeline);
    vkCmdDispatch(command, (kClusterCount + 63) / 64, 1, 1);
}

}  // namespace gt4::render
#endif
