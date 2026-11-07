#pragma once
// The pipeline cache's file (vk::openPipelineCache / savePipelineCache): the check of Vulkan's cache header and
// a write that replaces the old file in one step. No Vulkan calls, so tests/pipeline_cache_file.cpp runs
// without a GPU.
//
// The file: a 32-byte header of ours (kCacheMagic, format, the build stamp, the data's size), then the data
// vkGetPipelineCacheData gave. Data made by another build of the host is dropped, not merged: drivers key their
// entries by the executable (AMD's did not hit entries an earlier build made, and the file doubled), so keeping
// them would only grow the file.
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

// This build of the program: a hash of the running executable's bytes (0 when it cannot be read).
uint64_t executableStamp();

inline constexpr size_t kCacheFileLimit = 64u << 20;   // larger data is not saved

// `path` is UTF-8. The driver's data from the file when it is ours and `stamp`'s, else nothing; `why` says why not
// (empty: no file).
std::vector<char> readCacheFile(const std::string& path, uint64_t stamp, std::string* why = nullptr);

// Writes our header and the data to `path`.new, then moves it over `path` in one step (MoveFileExW with
// MOVEFILE_REPLACE_EXISTING on Windows, rename on POSIX): a crash at any point leaves the old file or the new one,
// never neither. False (and no .new left behind) when the write or the move fails, or the data is over the limit.
bool writeCacheFile(const std::string& path, uint64_t stamp, const void* data, size_t bytes);

}  // namespace gt4::vk
