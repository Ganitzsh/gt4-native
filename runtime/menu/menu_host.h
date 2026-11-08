#pragma once
// The settings pages inside GT4's menu flow (docs/modern/MENU_DESIGN.md 3.2-3.5, ticket M5): the page engine
// (platform/ui) run on the guest thread of GT4's menu update, the pad taken from GT4 while a page is open, the
// host's settings records read, the page's requests and draw list sent.
//
// Threads: gt4_menu_tick runs only on the guest thread that runs GT4's menu project update (game_src's copy of
// 00109B38, inside GT4's flow); pad_focus runs on whichever guest thread reads the pad. Nothing here runs in link_poll.
//
// For unattended runs: GT4_MENU_OPEN_AT=VBLANK opens the settings at the first menu update from that VBlank on (as
// the host's command 6 would), and GT4_PAD_SCRIPT's presses reach the page like the host's pad. GT4_TRACE_MENU=1 logs
// the page's inputs and sounds.
#include <cstdint>

namespace gt4::rt::menu {

// GT4's menu sounds, as gt4_menu_tick returns them for its caller to play by name through func_0023F8C8 ("cursor",
// "ok", "cancel", "disable": the names GT4's own widgets and scripts play, M0).
enum MenuSound : int { kSoundNone = 0, kSoundCursor = 1, kSoundOk = 2, kSoundCancel = 3, kSoundDisable = 4 };
// Or'ed into gt4_menu_tick's result when a hub that stood in for GT4's Options closed (any way but its GT4 options
// row): the hook ends GT4's Options as GT4's own exit from it does (next project = last, the result left empty; M0).
constexpr int kLeaveProject = 0x100;

// On every guest thread that reads the pad (gt4_replace_scePad2Read: the pad thread, GT4's MenuControls, the race's
// input; after the host's mapping and the scripted presses): while a page is open, keeps the port's bytes for the page
// and gives GT4 a neutral pad; after the page closes, a port that the page read stays neutral to GT4 until all its
// buttons are up and its sticks are centred, so the closing press never acts in GT4. With no page open, on GT mode's
// map under ui.entry = integrated (as the menu tick last saw it, at most two VBlanks ago), Start's press is taken from
// GT4 and opens the hub at the next menu tick (M8). Returns whether `data` was replaced.
bool pad_focus(int port, uint8_t data[18]);

// The items a race pause enables, as game_src/func_003C3820.cpp hands them to gt4_pause_tick (RacePause + 0x28,
// + 0x2C, + 0x30: what the strobe's RETRYENABLE, CONFIRMEXIT and SCREENSETTINGENABLE read).
enum PauseItem : int { kPauseRetry = 1, kPauseConfirmExit = 2, kPauseScreen = 4 };

// An entry asks for the settings (M7, M8); they open at the next menu update that runs a menu project.
void request_open();
bool is_open();   // any thread

}  // namespace gt4::rt::menu

// One page frame, from the hook in GT4's menu update. `project` is the project that update ran (0: none), so a page
// left open when GT4 changes project is closed. Returns a MenuSound for the hook to play, with kLeaveProject.
extern "C" int gt4_menu_tick(uint32_t project);

// From the hook in startProject (game_src/func_00109D00.cpp, M6), on GT4's sequence thread: GT4's "option" project
// has started (from the top menu or a quick menu's Options). With ui.entry = integrated the settings hub opens over
// it at its first update, with a GT4 options row first that hands the pad to GT4's screen; closing the hub
// otherwise ends GT4's Options (kLeaveProject). With ui.entry = hotkey, nothing changes.
extern "C" void gt4_menu_entry(uint32_t project);

// One frame of a race pause, from the copy of RacePause::update (game_src/func_003C3820.cpp, M7), on the thread that
// runs the race's updatePause. `pause` is the RacePause; `items` its PauseItem bits; `screen` the brightness and
// contrast the strobe's SETVAL writes (two ints, D_00622F4C + 0x39E34 or + 0x39E44). Returns -1 with ui.pause = gt4
// (GT4's pause runs as it was). With ui.pause = native the pause page answers in the low byte: 0 open (the time limit
// counts), 1 open with input this frame or a Settings tab shown (the count is cleared), or GT4's own answers 3
// continue, 4 exit, 5 retry, the page then closed; bits 8-11 carry a MenuSound for the copy to play.
extern "C" int gt4_pause_tick(uint32_t pause, int items, int *screen);

// GT4 ended the pause `pause` itself (the race's continue or exit events, the time limit): the page closes with it.
extern "C" void gt4_pause_end(uint32_t pause);
