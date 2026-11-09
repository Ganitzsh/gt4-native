// The host-run hub (platform/ui/host/host_hub, MENU_DESIGN M9e): with no game, the host runs the page engine itself
// through the M4 bridge. Opening it gives the schema's tabs with a Quit row on System; a change goes through the
// bridge into the settings (and says whether to save); the Quit row asks the host to end; Back at a tab's root closes
// it; a refresh after another writer shows the new value.
#include "host_hub.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;
void check(bool ok, const std::string &what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

using namespace gt4;
using namespace gt4::ui;

const Row *row(const Menu &menu, const std::string &key) {
    for (const auto &page : menu.schema().pages)
        for (const auto &r : page.rows)
            if (r.key == key) return &r;
    return nullptr;
}

}  // namespace

int main() {
    Settings settings;
    applyPreset(settings, Preset::High);
    SettingsBridge bridge(settings);
    HostHub hub(bridge);
    hub.open(settings);
    check(hub.isOpen() && hub.menu().schema().tabs.size() >= 8, "open: the schema's tabs");
    const Row *quit = row(hub.menu(), HostHub::kQuitKey);
    check(quit && quit->kind == RowKind::Action, "a Quit row");
    bool onSystem = false;
    for (const auto &page : hub.menu().schema().pages)
        if (page.id == "system")
            for (const auto &r : page.rows) onSystem = onSystem || r.key == HostHub::kQuitKey;
    check(onSystem, "on the System tab");

    // The first tab's first row (Game: the AI's catch-up), one step right: through the bridge into the settings.
    const std::string before = getOption(settings, "gameplay.ai_catchup");
    HostHub::Result r = hub.press(Input::Right, settings);
    check(r.changed && r.save && getOption(settings, "gameplay.ai_catchup") != before,
          "a change goes through the bridge, applied and saved");
    const Row *catchup = row(hub.menu(), "gameplay.ai_catchup");
    check(catchup && catchup->value == getOption(settings, "gameplay.ai_catchup"), "the row shows the new value");

    // Another writer (a key): refresh shows it.
    setOption(settings, "debug.overlay", getOption(settings, "debug.overlay") == "off" ? "fps" : "off");
    hub.refresh(settings);
    const Row *overlay = row(hub.menu(), "debug.overlay");
    check(overlay && overlay->value == getOption(settings, "debug.overlay"), "refresh shows another writer's change");

    // System is the last tab: one tab left from the first wraps there; Down to Quit, Confirm.
    hub.press(Input::TabLeft, settings);
    hub.tick(1.0f, settings);
    check(hub.menu().page() && hub.menu().page()->id == "system", "the last tab is System");
    hub.press(Input::Down, settings);
    r = hub.press(Input::Confirm, settings);
    check(r.quit && !r.changed, "the Quit row asks the host to end");

    // Back at a tab's root closes the hub.
    hub.press(Input::Back, settings);
    check(!hub.isOpen(), "Back at the root closes it");

    if (failures) {
        std::printf("ui_host_hub: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_host_hub: all checks passed\n");
    return 0;
}
