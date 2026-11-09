#pragma once
// Process-wide settings and the host services that replace the PS2's (kernel, IOP, disc, pad...).
#include <cstdint>

#include "gt4/link/protocol.h"

namespace gt4::gfx::scene {
struct FrameBegin;
struct FrameEnd;
}  // namespace gt4::gfx::scene

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
// kOptionSteerBand: input.steer.game_band in permille, 0 = off (original): GT4's steering band compensated in the left
// stick's byte while a race is loaded (runtime/pad_steer.h; lane D, iop.cpp steer_band_pad).
// kOptionHostMixer: audio.mixer, 0 game (the game mixes, as always), 1 host: the game keeps answering GT4's SPU status
// from its own voices and sends the register images and SPU RAM for the host to mix (runtime/audio.cpp, PLAN.md D5).
// kOptionUiEntry: ui.entry, 0 hotkey (original, and before the host sends it), 1 integrated: GT4's Options items open
// the settings hub (runtime/menu/menu_host.cpp; lane M, docs/modern/MENU_DESIGN.md 3.2).
// kOptionUiPause: ui.pause, 0 gt4 (original, and before the host sends it), 1 native: the race pause shows the native
// pause page instead of GT4's pause.strb (game_src/func_003C3820.cpp, runtime/menu/menu_host.cpp; lane M, M7).
// kOptionCanvasWidth: the pages' canvas width in units (1080 high: 1920 at 16:9, 1440 at 4:3), from the content's
// aspect; 0 before the host sends it (1920 then). kOptionPromptStyle: the prompts' controller, 0 Xbox, 1 PlayStation,
// 2 keyboard (platform/ui/menu_layout.h PromptStyle; the host's ui.prompts). Both lay the settings pages out
// (runtime/menu/menu_host.cpp; lane M, MENU_DESIGN M9).
enum GameOption : uint32_t { kOptionAiCatchUp = 0, kOptionMirrorDetail = 1, kOptionModelDetail = 2, kOptionRaceFrames = 3, kOptionAspect = 4,
                             kOptionSceneRecords = 5, kOptionTriggerTravel = 6, kOptionSteerBand = 7,
                             kOptionHostMixer = 8, kOptionUiEntry = 9, kOptionUiPause = 10,
                             kOptionCanvasWidth = 11, kOptionPromptStyle = 12,
                             kGameOptionCount = 13 };
static_assert(kGameOptionCount == GT4_GAME_OPTION_COUNT, "the options the host sends: include/gt4/link/protocol.h");
void set_game_option(uint32_t id, uint32_t value);   // GT4_REC_HOST_CMD 4 (runtime/link.cpp)

// Scene records (docs/modern/CONTRACTS.md a.5.1): a scene frame is one kicked list. At the kick (runtime/frame.cpp)
// scene_frame_end closes the open frame: false when none is open, else `record` is its FrameEnd (`bytes` long, its
// counts as runtime/native/pgl.cpp appended them) and `thread` the thread that opened it.
bool scene_frame_end(void *record, uint32_t bytes, uint32_t *thread);
// kOptionSceneRecords 2 (vehicles only, CONTRACTS.md a.1): a vehicles-only frame's FrameBegin and FrameEnd, numbered with
// the full frames; the kick sends them in one block around the Vehicles (runtime/native/pgl.cpp).
void scene_vehicles_frame(gfx::scene::FrameBegin &begin, gfx::scene::FrameEnd &end);
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
