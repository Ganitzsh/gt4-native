// How Settings opens, the race pause and the pages' prompts: ui.* (lane M; docs/modern/MENU_DESIGN.md 3.8)
// (settings_table.h). Every preset sets ui.entry and ui.pause (settings.cpp presetFollowers), but neither decides
// which preset is shown: a player who opens Settings by hotkey only still reads "Preset: High".
#include "settings_table.h"

namespace gt4::settings_table {

void uiOptions(std::vector<Option>& o) {
    o.push_back(enumOption("ui.entry", &Settings::uiEntry, {"hotkey", "integrated"},
        "How Settings opens. hotkey = only by the host's keys (Esc, Guide, Select + Start): GT4's screens are as "
        "on the PS2 (the original preset). integrated = also from GT4's own Options items, in the top menu and in "
        "the pre-race menus (they open Settings, and its GT4 options row opens GT4's Options screen), and from Start "
        "on GT mode's map, where GT4 does nothing with it (low to ultra)."));
    o.push_back(enumOption("ui.pause", &Settings::uiPause, {"gt4", "native"},
        "The race pause. gt4 = GT4's own pause screen (the original preset). native = this game's pause page, in "
        "GT4's order: Continue, Retry where the race allows it, Exit, Screen (GT4's brightness and contrast), then "
        "Settings, with the race held as GT4's pause holds it (low to ultra). Esc, Guide or Select + Start in a "
        "race open this page on Settings under either value."));
    o.push_back(enumOption("ui.prompts", &Settings::uiPrompts, {"auto", "xbox", "playstation", "keyboard"},
        "The button names the settings pages show. auto = the controller in use: the keyboard's keys when it was "
        "used last, else PlayStation's names on a PlayStation pad and Xbox's on others. Under Steam Input every pad "
        "reports itself as an Xbox one, so a PlayStation player there picks playstation."));
}

}  // namespace gt4::settings_table
