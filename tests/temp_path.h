// A name in the system's temporary folder that no other test process uses: the stem, this process's id and a
// random suffix (two clones running ctest at once shared gt4_ps2gfx_test.cap and the like, and one test read or
// deleted the other's file). The caller removes what it made.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace gt4::test {

inline std::filesystem::path uniqueTempPath(const std::string& stem, const std::string& extension = "") {
#ifdef _WIN32
    const unsigned long pid = static_cast<unsigned long>(_getpid());
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    std::random_device random;
    const uint64_t suffix = (uint64_t(random()) << 32) ^ random();
    char tail[48];
    std::snprintf(tail, sizeof tail, "_%lu_%016llx", pid, static_cast<unsigned long long>(suffix));
    return std::filesystem::temp_directory_path() / (stem + tail + extension);
}

}  // namespace gt4::test
