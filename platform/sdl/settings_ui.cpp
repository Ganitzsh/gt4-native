// How Settings opens, the race pause and the pages' prompts: ui.* (lane M; docs/modern/MENU_DESIGN.md 3.8)
// (settings_table.h). Every preset sets ui.entry and ui.pause (settings.cpp presetFollowers), but neither decides
// which preset is shown: a player who opens Settings by hotkey only still reads "Preset: High".
#include "settings_table.h"

namespace gt4::settings_table {

void uiOptions(std::vector<Option>& o) {
    o.push_back(enumOption("ui.entry", &Settings::uiEntry, {"hotkey", "integrated"},
        "How Settings opens. hotkey = only by Esc, Guide or Select + Start: GT4's screens as on the PS2 (the "
        "original preset). integrated = also from GT4's Options items, whose GT4 options row still opens GT4's own "
        "screen, and from Start on GT mode's map (low to ultra)."));
    o.push_back(enumOption("ui.pause", &Settings::uiPause, {"gt4", "native"},
        "The race pause. gt4 = GT4's own pause screen (the original preset). native = this game's pause page: "
        "Continue, Retry where allowed, Exit, Screen, Settings (low to ultra). Esc, Guide or Select + Start in a "
        "race open it on Settings under either value."));
    o.push_back(enumOption("ui.prompts", &Settings::uiPrompts, {"auto", "xbox", "playstation", "keyboard"},
        "The button names the settings pages show. auto = the controller in use: the keyboard's keys when it was "
        "used last, else PlayStation's names on a PlayStation pad and Xbox's on others. Under Steam Input every pad "
        "reports itself as an Xbox one, so a PlayStation player there picks playstation."));
}

}  // namespace gt4::settings_table
