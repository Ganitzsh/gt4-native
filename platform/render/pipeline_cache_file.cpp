#include "pipeline_cache_file.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

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

constexpr char kMagic[8] = {'G', 'T', '4', 'P', 'C', 'A', 'C', 'H'};
constexpr uint32_t kFormat = 1;
struct FileHeader {
    char magic[8];
    uint32_t format;
    uint32_t reserved;
    uint64_t stamp;
    uint64_t bytes;   // the driver's data after this header
};
static_assert(sizeof(FileHeader) == 32);

std::vector<char> readAll(const std::filesystem::path& path) {
    std::vector<char> data;
    std::ifstream file(path, std::ios::binary);
    if (!file) return data;
    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (file.bad()) data.clear();
    return data;
}

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

uint64_t executableStamp() {
    std::filesystem::path self;
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n == 0 || n >= buffer.size()) return 0;
    buffer.resize(n);
    self = buffer;
#else
    std::error_code error;
    self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return 0;
#endif
    const std::vector<char> bytes = readAll(self);
    return bytes.empty() ? 0 : XXH3_64bits(bytes.data(), bytes.size());
}

std::vector<char> readCacheFile(const std::string& path, uint64_t stamp, std::string* why) {
    std::vector<char> data;
    if (why) why->clear();
    if (path.empty()) return data;
    data = readAll(fromUtf8(path));
    if (data.empty()) return data;
    FileHeader header{};
    if (data.size() < sizeof header || (std::memcpy(&header, data.data(), sizeof header), std::memcmp(header.magic, kMagic, sizeof kMagic)) ||
        header.format != kFormat) {
        if (why) *why = "not a cache file of this format";
        return {};
    }
    if (header.bytes != data.size() - sizeof header) {
        if (why) *why = "cut short or padded";
        return {};
    }
    if (header.stamp != stamp || stamp == 0) {
        if (why) *why = "made by another build";
        return {};
    }
    data.erase(data.begin(), data.begin() + sizeof header);
    return data;
}

bool writeCacheFile(const std::string& path, uint64_t stamp, const void* data, size_t bytes) {
    if (path.empty() || bytes > kCacheFileLimit) return false;
    const std::filesystem::path target = fromUtf8(path);
    std::filesystem::path temporary = target;
    temporary += ".new";
    std::error_code ignored;
    {
        FileHeader header{};
        std::memcpy(header.magic, kMagic, sizeof kMagic);
        header.format = kFormat;
        header.stamp = stamp;
        header.bytes = bytes;
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (file) file.write(reinterpret_cast<const char*>(&header), sizeof header);
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
