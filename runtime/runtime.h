#pragma once
// Process-wide settings and the host services that replace the PS2's (kernel, IOP, disc, pad...).
#include <cstdint>

namespace gt4::rt {

struct RuntimeConfig {
    const char *elf_path = "";
    const char *iso_path = "";
    const char *link_name = "";     // --link: the shared section gt4_host created
    const char *capture_path = "";  // --capture: write the graphics stream to a file
    const char *memcard_path = "";  // --memcard: the folder holding memory card 1 (runtime/memcard.cpp)
    const char *aspect = "";        // --aspect: the display shape (16:9, 21:9, 1.6...): the game's wide mode
    const char *menus = "";         // --menus centered|stretched: the menus' 2D in the wide mode (centred)
};
extern RuntimeConfig g_config;

// Starts the host services (kernel emulation of threads/semaphores is NOT done here: see kernel.cpp).
void services_init();
// Test tools, started by main when their variable is set: GT4_RECORD (record.cpp), GT4_PROFILE (profile.cpp).
void record_install();
void profile_start();

// The host's gameplay options (runtime/sdk.cpp), read by game_src copies through gt4_game_option(id).
enum GameOption : uint32_t { kOptionAiCatchUp = 0, kOptionMirrorDetail = 1, kGameOptionCount = 8 };
void set_game_option(uint32_t id, uint32_t value);   // GT4_REC_HOST_CMD 4 (runtime/link.cpp)

}  // namespace gt4::rt

// GT4_ORIG (tools/eeref/orig_bridge.cpp, linked with game_link.py --eeorig; otherwise an empty stub).
extern "C" void gt4_orig_install();

namespace gt4::rt {

// Reads n bytes of the disc image at a byte offset (absolute: the image is linear, layer 1 follows
// layer 0); the bytes past the end of the image read as zeros. Returns false if nothing was read.
bool disc_read(uint64_t offset, void *dst, uint32_t n);

}  // namespace gt4::rt
