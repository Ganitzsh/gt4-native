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

// On the pad thread (gt4_replace_scePad2Read, after the host's mapping and the scripted presses): while a page is
// open, keeps the port's bytes for the page and gives GT4 a neutral pad; after the page closes, a port that the page
// read stays neutral to GT4 until all its buttons are up and its sticks are centred, so the closing press never
// acts in GT4. Returns whether `data` was replaced.
bool pad_focus(int port, uint8_t data[18]);

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
