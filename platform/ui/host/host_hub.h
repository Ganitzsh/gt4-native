#pragma once
// The settings hub the host runs itself when no game is there to run it (MENU_DESIGN 3.2 point 5, M9e): the test
// scene and replays. The same page engine (menu_model) and layout (menu_layout) the game runs, and the same settings
// path: every request goes through the M4 bridge (SettingsBridge::handle, as the game's GT4_REC_SETTINGS_SET does),
// and the bridge's answers set the rows' values. The System tab gains a Quit row here only: with no game, Esc opens
// this hub instead of ending the host (lane C), so quitting is a row (or the window's close). Host only.
#include "menu_layout.h"
#include "menu_model.h"
#include "settings.h"
#include "settings_schema.h"

#include <cstdint>
#include <vector>

namespace gt4::ui {

class HostHub {
public:
    static constexpr const char* kQuitKey = "host.quit";

    struct Result {
        bool changed = false;   // apply the settings (applySettings)
        bool save = false;      // and save them (the bridge's save rule)
        bool quit = false;      // the Quit row
        Sound sound = Sound::None;
    };

    explicit HostHub(SettingsBridge& bridge) : bridge_(bridge) {}

    bool isOpen() const { return menu_.isOpen(); }
    void open(Settings& settings);   // the schema and values from the bridge, then the hub's first tab
    void close();

    Result press(Input input, Settings& settings);
    void hold(Input direction, bool down) { menu_.hold(direction, down); }
    Result tick(float seconds, Settings& settings);
    // Another writer changed the settings (a key, the session): the rows that changed (the bridge's refresh()).
    void refresh(const Settings& settings);

    DrawList layout(float canvasWidth, PromptStyle prompts) const;
    const Menu& menu() const { return menu_; }

private:
    Result run(const Output& out, Settings& settings);
    void take(const SettingsBridge::Reply& reply);
    SettingsBridge& bridge_;
    Menu menu_;
    uint32_t request_ = 0;
};

}  // namespace gt4::ui
