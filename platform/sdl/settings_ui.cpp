// The settings menu itself: ui.* (lane M; docs/modern/MENU_DESIGN.md 3.8) (settings_table.h). Every preset sets these
// (settings.cpp presetFollowers), but none of them decides which preset is shown: a player who opens the menu by
// hotkey only still reads "Preset: High".
#include "settings_table.h"

namespace gt4::settings_table {

void uiOptions(std::vector<Option>& o) {
    o.push_back(enumOption("ui.entry", &Settings::uiEntry, {"hotkey", "integrated"},
        "How the settings menu opens. hotkey = only by the host's keys (Guide, Esc, Select + Start): GT4's screens "
        "are as on the PS2 (the original preset). integrated = also from GT4's own Options items, in the top menu and "
        "in the pre-race menus: they open this menu, and its GT4 options row opens GT4's Options screen."));
}

}  // namespace gt4::settings_table
