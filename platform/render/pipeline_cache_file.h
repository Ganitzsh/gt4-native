#pragma once
// The pipeline cache's file (vk::openPipelineCache / savePipelineCache): the check of Vulkan's cache header and
// a write that replaces the old file in one step. No Vulkan calls, so tests/pipeline_cache_file.cpp runs
// without a GPU.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gt4::vk {

// What a cache's header must name to be used: this GPU and driver (VkPhysicalDeviceProperties).
struct PipelineCacheIdentity {
    uint32_t vendorID = 0, deviceID = 0;
    uint8_t uuid[16] = {};   // pipelineCacheUUID (VK_UUID_SIZE)
};

// Vulkan's header (VK_PIPELINE_CACHE_HEADER_VERSION_ONE): header length (at least 32, within the data), version 1,
// vendor, device, then the cache UUID. True when all of it is this identity's: another GPU's or driver's data
// is not handed to the driver.
bool pipelineCacheMatches(const void* data, size_t bytes, const PipelineCacheIdentity& identity);

// `path` is UTF-8. The whole file, or nothing when it is missing or unreadable.
std::vector<char> readCacheFile(const std::string& path);

// Writes `path`.new whole, then moves it over `path` in one step (MoveFileExW with MOVEFILE_REPLACE_EXISTING on
// Windows, rename on POSIX): a crash at any point leaves the old file or the new one, never neither. False (and
// no .new left behind) when the write or the move fails.
bool writeCacheFile(const std::string& path, const void* data, size_t bytes);

}  // namespace gt4::vk
