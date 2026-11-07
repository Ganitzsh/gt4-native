// The pipeline cache's file (platform/render/pipeline_cache_file.h): Vulkan's cache header is checked against the
// GPU and driver before the data is used, and a save replaces the old file in one step. No GPU needed.
#include "pipeline_cache_file.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// A cache as a driver writes it: VkPipelineCacheHeaderVersionOne, then the driver's own data.
std::vector<char> cache(uint32_t length, uint32_t version, const gt4::vk::PipelineCacheIdentity& id, size_t payload) {
    std::vector<char> data(32 + payload, 'x');
    const uint32_t words[4] = {length, version, id.vendorID, id.deviceID};
    std::memcpy(data.data(), words, sizeof words);
    std::memcpy(data.data() + 16, id.uuid, sizeof id.uuid);
    return data;
}

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}
}  // namespace

// argv[1]: a folder of its own to write in (the build tree), so clones running ctest at once do not share it.
int main(int argc, char** argv) {
    using namespace gt4::vk;

    PipelineCacheIdentity gpu;
    gpu.vendorID = 0x1002;
    gpu.deviceID = 0x744C;
    for (int i = 0; i < 16; ++i) gpu.uuid[i] = static_cast<uint8_t>(0xA0 + i);

    // The header check
    {
        const std::vector<char> good = cache(32, 1, gpu, 1000);
        check(pipelineCacheMatches(good.data(), good.size(), gpu), "this GPU's cache is used");
        check(pipelineCacheMatches(good.data(), 32, gpu), "a header alone is used");

        PipelineCacheIdentity other = gpu;
        other.uuid[15] ^= 1;
        check(!pipelineCacheMatches(good.data(), good.size(), other), "another pipelineCacheUUID: not used");
        other = gpu;
        other.vendorID = 0x10DE;
        check(!pipelineCacheMatches(good.data(), good.size(), other), "another vendor: not used");
        other = gpu;
        other.deviceID = 0x7448;
        check(!pipelineCacheMatches(good.data(), good.size(), other), "another device: not used");

        const std::vector<char> version2 = cache(32, 2, gpu, 100);
        check(!pipelineCacheMatches(version2.data(), version2.size(), gpu), "header version 2: not used");
        const std::vector<char> shortLength = cache(16, 1, gpu, 100);
        check(!pipelineCacheMatches(shortLength.data(), shortLength.size(), gpu), "header length under 32: not used");
        const std::vector<char> longLength = cache(4096, 1, gpu, 100);
        check(!pipelineCacheMatches(longLength.data(), longLength.size(), gpu), "header longer than the data: not used");
        check(!pipelineCacheMatches(good.data(), 31, gpu), "data shorter than a header: not used");
        check(!pipelineCacheMatches(nullptr, 0, gpu), "no data: not used");
    }

    // The file: written whole, then moved over the old one
    {
        std::error_code ignored;
        const std::filesystem::path base = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
        const std::filesystem::path folder = base / u8"pipeline_cache_test_é";   // (a non-ASCII name: paths are UTF-8)
        std::filesystem::remove_all(folder, ignored);
        std::filesystem::create_directories(folder);
        const std::string path = utf8(folder / "pipeline_cache.bin");
        std::filesystem::path temporary = folder / "pipeline_cache.bin";
        temporary += ".new";

        const uint64_t build = 0x1234567890ABCDEFull, otherBuild = build + 1;
        std::string why;
        check(readCacheFile(path, build, &why).empty() && why.empty(), "a missing file reads as nothing, no reason given");
        check(readCacheFile("", build).empty(), "no path reads as nothing");
        check(!writeCacheFile("", build, "a", 1), "no path is not written");

        const std::vector<char> first = cache(32, 1, gpu, 70000);
        check(writeCacheFile(path, build, first.data(), first.size()), "a new file is written");
        check(readCacheFile(path, build, &why) == first && why.empty(), "the new file reads back whole for the same build");
        check(std::filesystem::file_size(folder / "pipeline_cache.bin") == first.size() + 32, "our 32-byte header, then the data");

        // Another build's data is dropped (drivers key their entries by the executable: kept, they only grow).
        check(readCacheFile(path, otherBuild, &why).empty() && why == "made by another build", "another build's file: not used");
        check(readCacheFile(path, 0, &why).empty(), "an unknown build (stamp 0) uses no file");

        const std::vector<char> second = cache(32, 1, gpu, 10);
        check(writeCacheFile(path, build, second.data(), second.size()), "an existing file is replaced");
        check(readCacheFile(path, build) == second, "the replaced file holds the new data only");
        check(!std::filesystem::exists(temporary), "no .new file is left behind");

        // Files that are not ours, or not whole.
        {
            std::ofstream(folder / "raw.bin", std::ios::binary).write(first.data(), static_cast<std::streamsize>(first.size()));   // Vulkan's data alone
            check(readCacheFile(utf8(folder / "raw.bin"), build, &why).empty() && why == "not a cache file of this format",
                  "a file without our header: not used");
            std::filesystem::copy_file(folder / "pipeline_cache.bin", folder / "short.bin");
            std::filesystem::resize_file(folder / "short.bin", 32 + 5);
            check(readCacheFile(utf8(folder / "short.bin"), build, &why).empty() && why == "cut short or padded", "a file cut short: not used");
        }
        check(!writeCacheFile(path, build, first.data(), kCacheFileLimit + 1), "data over the limit is not saved");
        check(readCacheFile(path, build) == second, "and the file stays as it was");

        // A target that cannot be replaced (a folder): the old content stays and no .new is left.
        const std::string blocked = utf8(folder / "blocked");
        std::filesystem::create_directories(folder / "blocked" / "inside");
        check(!writeCacheFile(blocked, build, second.data(), second.size()), "a folder in the way is not replaced");
        check(std::filesystem::is_directory(folder / "blocked" / "inside"), "the folder in the way is kept");
        check(!std::filesystem::exists(folder / "blocked.new"), "no .new left after a failed move");

        std::filesystem::remove_all(folder, ignored);
    }

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("pipeline_cache_file: all checks passed\n");
    return failures ? 1 : 0;
}
