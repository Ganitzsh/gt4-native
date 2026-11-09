// The settings menu itself: ui.* (lane M; docs/modern/MENU_DESIGN.md 3.8) (settings_table.h). Every preset sets these
// (settings.cpp presetFollowers), but none of them decides which preset is shown: a player who opens the menu by
// hotkey only still reads "Preset: High".
#include "settings_table.h"

namespace gt4::settings_table {

void uiOptions(std::vector<Option>& o) {
    o.push_back(enumOption("ui.entry", &Settings::uiEntry, {"hotkey", "integrated"},
        "How the settings menu opens. hotkey = only by the host's keys (Guide, Esc, Select + Start): GT4's screens "
        "are as on the PS2 (the original preset). integrated = also from GT4's own Options items, in the top menu and "
        "in the pre-race menus (they open this menu, and its GT4 options row opens GT4's Options screen), and from "
        "Start on GT mode's map, where GT4 does nothing with it. Every preset sets hotkey until MENU_DESIGN "
        "M9f; this file can opt in (docs/modern/settings-playtest.ini)."));
    o.push_back(enumOption("ui.pause", &Settings::uiPause, {"gt4", "native"},
        "The race pause. gt4 = GT4's own pause screen (the original preset). native = this game's pause page, in "
        "GT4's order: Continue, Retry where the race allows it, Exit, Screen (GT4's brightness and contrast), then "
        "Settings (from M4), with the race held as GT4's pause holds it. Every preset sets gt4 until "
        "MENU_DESIGN M9f; this file can opt in (docs/modern/settings-playtest.ini)."));
    o.push_back(enumOption("ui.prompts", &Settings::uiPrompts, {"auto", "xbox", "playstation", "keyboard"},
        "The button names the settings pages show. auto = the controller in use: the keyboard's keys when it was "
        "used last, else PlayStation's names on a PlayStation pad and Xbox's on others. Under Steam Input every pad "
        "reports itself as an Xbox one, so a PlayStation player there picks playstation."));
}

}  // namespace gt4::settings_table
