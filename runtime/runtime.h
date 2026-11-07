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
// kOptionAspect: the content's aspect x 1000 as the host shows it now (0: not sent), over the start-up --aspect.
// kOptionSceneRecords: 1 when the host wants scene records (gt4_scene_on, docs/modern/CONTRACTS.md (a)); 0 by default.
// kOptionTriggerTravel: input.trigger_travel, 0 game, 1 full (runtime/pad_pedals.h; lane D, iop.cpp scale_pedals).
enum GameOption : uint32_t { kOptionAiCatchUp = 0, kOptionMirrorDetail = 1, kOptionModelDetail = 2, kOptionRaceFrames = 3, kOptionAspect = 4,
                             kOptionSceneRecords = 5, kOptionTriggerTravel = 6, kGameOptionCount = 8 };
void set_game_option(uint32_t id, uint32_t value);   // GT4_REC_HOST_CMD 4 (runtime/link.cpp)

// Scene records (docs/modern/CONTRACTS.md a.5.1): a scene frame is one kicked list. At the kick (runtime/frame.cpp)
// scene_frame_end closes the open frame: false when none is open, else `record` is its FrameEnd (`bytes` long, its
// counts as runtime/native/pgl.cpp appended them) and `thread` the thread that opened it.
bool scene_frame_end(void *record, uint32_t bytes, uint32_t *thread);
// The frame clock's tick (runtime/gs.cpp on_frame): its link records (the display as it stands at the tick, then the
// VSync), sent by `send`. While a kick walks its list with scene records on, the tick is queued and the kick sends it
// after FrameEnd (a.5.1), so a list and its FrameEnd are never split across the host's FrameLists. Else sent at once.
struct ClockTick {
    uint64_t display[7];
    uint32_t field, count;
    bool captureWindow;
};
void frame_tick(const ClockTick &tick, void (*send)(const ClockTick &));

}  // namespace gt4::rt

// GT4_ORIG (tools/eeref/orig_bridge.cpp, linked with game_link.py --eeorig; otherwise an empty stub).
extern "C" void gt4_orig_install();

namespace gt4::rt {

// Reads n bytes of the disc image at a byte offset (absolute: the image is linear, layer 1 follows
// layer 0); the bytes past the end of the image read as zeros. Returns false if nothing was read.
bool disc_read(uint64_t offset, void *dst, uint32_t n);

}  // namespace gt4::rt
