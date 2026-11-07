#pragma once
// The cluster build (ticket F1, docs/modern/CONTRACTS.md (c)): which lights of a view's light list reach each
// froxel. kClusterX x kClusterY tiles over the view's render extent, kClusterZ slices in view distance
// (logarithmic, LightListHeader's sliceScale / sliceBias); per cell the indices of the lights whose sphere of
// `range` meets the cell's view-space box, in ascending light index. Directional lights (the first
// directionalCount) are not clustered.
//
// The cells are stored with fixed room: cell i's indices start at i * kMaxLightsPerCluster, so the GPU build
// needs no prefix sum and gives the same list as the CPU one, bit for bit (the test compares them). A cell that
// is reached by more than kMaxLightsPerCluster lights keeps the first kMaxLightsPerCluster and counts the rest
// in ClusterStats::overflow.
//
// The GPU build reads every bound from ClusterGrid, computed here on the CPU, so it does no division, log or
// exp of its own: its arithmetic is adds, multiplies, min and max ('precise', no contraction), which Vulkan rounds
// as IEEE does. The CPU side must not contract either: true for MSVC's default /fp:precise, not for GCC and Clang,
// whose default (-ffp-contract=fast or on) may fuse a * b + c on aarch64 and x86 with FMA; the bit-for-bit
// comparison relies on C5's -ffp-contract=off there.
#include <cstdint>
#include <vector>

#include "gt4/render/light_list.h"

namespace gt4::render {

// How a view-space direction maps to tile coordinates: tile = scale * (v / d) + offset, v the view-space x or
// y, d = -z the view distance. For a view whose window projection is x_window = fx * x / d + cx over a render
// extent W (pixels), scaleX = fx * kClusterX / W and offsetX = cx * kClusterX / W; y likewise (a negative scale
// when window y runs down while view y runs up).
struct ClusterProjection {
    float scaleX = 1.0f, offsetX = 0.0f, scaleY = 1.0f, offsetY = 0.0f;
};
ClusterProjection clusterProjection(float fx, float fy, float cx, float cy, float width, float height);

// The bounds the build tests against, laid out as one std430 float array (the shader's ClusterGrid buffer):
// [0, kClusterX] tile edges in x as slopes v / d, then [0, kClusterY] in y, then the kClusterZ + 1 slice
// distances: 0, then the slice boundaries from the header, then kFar (the last slice reaches to infinity).
inline constexpr uint32_t kGridSlopeX = 0, kGridSlopeY = kClusterX + 1, kGridDepth = kGridSlopeY + kClusterY + 1;
inline constexpr uint32_t kGridFloats = kGridDepth + kClusterZ + 1;
inline constexpr float kClusterFar = 1e30f;
struct ClusterGrid { float v[kGridFloats]; };
ClusterGrid clusterGrid(const LightListHeader& header, const ClusterProjection& projection);

struct ClusterStats {
    uint32_t overflow = 0;    // light-cell pairs dropped because a cell was full
    uint32_t maxCount = 0;    // the most lights any cell was reached by (before the cap)
    uint32_t pairs = 0;       // light-cell pairs kept
    uint32_t pad = 0;
};

struct ClusterResult {
    std::vector<ClusterCell> cells;   // kClusterCount
    std::vector<uint32_t> indices;    // kClusterCount * kMaxLightsPerCluster; unused room stays 0
    ClusterStats stats;
};

// The CPU reference (and the fallback without a GPU): the same tests, in the same order, as lights_cluster.comp.
ClusterResult buildClustersCpu(const LightListHeader& header, const GpuLight* lights, const ClusterGrid& grid);

}  // namespace gt4::render

// The GPU build: one dispatch, after the light list is written and before its readers.
#ifdef GT4_LIGHTS_CLUSTER_GPU
#include "vk.h"

namespace gt4::render {

class LightClusters {
public:
    // The SPIR-V of lights_cluster.comp (gt4::shaders::lights_cluster_comp() in the host).
    void create(const vk::Device& device, vk::ShaderCode code);
    void destroy(const vk::Device& device);
    // Buffers (storage): header (LightListHeader), lights (GpuLight[]), grid (ClusterGrid), cells
    // (ClusterCell[kClusterCount]), indices (uint32[kClusterCount * kMaxLightsPerCluster]), stats (ClusterStats,
    // zeroed by the caller before the dispatch when it is read). The caller orders the writes before and the reads
    // after (compute shader stage, storage access).
    void record(VkCommandBuffer command, VkBuffer header, VkBuffer lights, VkDeviceSize lightsBytes, VkBuffer grid,
                VkBuffer cells, VkBuffer indices, VkBuffer stats) const;

private:
    vk::Pipeline pipeline;
};

}  // namespace gt4::render
#endif
