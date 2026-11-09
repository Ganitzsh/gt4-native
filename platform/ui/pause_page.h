#pragma once
// The race pause page's rules both sides share (MENU_DESIGN.md 3.2 point 4; M7, M4): which pages are the pause's own,
// and when GT4's pause time limit counts. Header only, no host includes: runtime/menu/menu_host.cpp uses it in the game,
// tests/ui_menu.cpp checks it.
#include "menu_model.h"

#include <string>

namespace gt4::ui {

// The pause's own pages: its tab "pause" and its sub-pages "pause.*" (Screen, the exit question); the host's tabs are
// the settings.
inline bool isPausePage(const Page *page) {
    return page && (page->id == "pause" || page->id.compare(0, 6, "pause.") == 0);
}

// Whether GT4's pause time limit counts this frame (PauseBase::checkTimeLimit): only on the pause's own pages and
// without input, as GT4's strobe counts it; never while a Settings tab is shown, so changing settings cannot run the
// race's clock out (a deliberate difference, native only).
inline bool pauseTimeLimitCounts(const Menu &menu, bool input) { return !input && isPausePage(menu.page()); }

}  // namespace gt4::ui
