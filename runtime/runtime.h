#pragma once
// Process-wide settings and the host services that replace the PS2's (kernel, IOP, disc, pad...).
#include <cstdint>

namespace gt4::rt {

struct RuntimeConfig {
    const char *elf_path = "";
    const char *iso_path = "";
    const char *link_name = "";     // --link: the shared section gt4_host created
    const char *capture_path = "";  // --capture: write the graphics stream to a file
};
extern RuntimeConfig g_config;

// Starts the host services (kernel emulation of threads/semaphores is NOT done here: see kernel.cpp).
void services_init();

// Reads n bytes of the disc image at a byte offset (absolute: the image is linear, layer 1 follows
// layer 0); the bytes past the end of the image read as zeros. Returns false if nothing was read.
bool disc_read(uint64_t offset, void *dst, uint32_t n);

}  // namespace gt4::rt
