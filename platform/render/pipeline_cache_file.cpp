#include "pipeline_cache_file.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gt4::vk {
namespace {

std::filesystem::path fromUtf8(const std::string& text) { return std::filesystem::path(std::u8string(text.begin(), text.end())); }

}  // namespace

bool pipelineCacheMatches(const void* data, size_t bytes, const PipelineCacheIdentity& identity) {
    constexpr size_t kHeader = 16 + sizeof identity.uuid;   // VkPipelineCacheHeaderVersionOne
    if (!data || bytes < kHeader) return false;
    uint32_t words[4];
    std::memcpy(words, data, sizeof words);
    return words[0] >= kHeader && words[0] <= bytes && words[1] == 1 /* VK_PIPELINE_CACHE_HEADER_VERSION_ONE */ &&
           words[2] == identity.vendorID && words[3] == identity.deviceID &&
           !std::memcmp(static_cast<const char*>(data) + 16, identity.uuid, sizeof identity.uuid);
}

std::vector<char> readCacheFile(const std::string& path) {
    std::vector<char> data;
    if (path.empty()) return data;
    std::ifstream file(fromUtf8(path), std::ios::binary);
    if (!file) return data;
    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (file.bad()) data.clear();
    return data;
}

bool writeCacheFile(const std::string& path, const void* data, size_t bytes) {
    if (path.empty()) return false;
    const std::filesystem::path target = fromUtf8(path);
    std::filesystem::path temporary = target;
    temporary += ".new";
    std::error_code ignored;
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (file) file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
        if (file) file.close();
        if (!file) {
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
#ifdef _WIN32
    const bool moved = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    const bool moved = std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
    if (!moved) std::filesystem::remove(temporary, ignored);
    return moved;
}

}  // namespace gt4::vk
