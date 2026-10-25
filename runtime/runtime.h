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

}  // namespace gt4::rt
