#pragma once
// Which controller's names the pages' prompts show (MENU_DESIGN M9; the coordinator: the controller in use).
// ui.prompts = auto by default (lane C: under Steam Input every pad reports itself as an Xbox one, so a PlayStation
// player there picks playstation): auto is the keyboard's keys when the keyboard was used last, else PlayStation's
// names on a PlayStation pad (SDL's gamepad type), else Xbox's. Sent to the game as kOptionPromptStyle (0 Xbox,
// 1 PlayStation, 2 keyboard: PromptStyle's values), which lays the pages out. Host only.
#include "menu_layout.h"
#include "settings.h"

namespace gt4::ui {

inline PromptStyle promptStyle(UiPrompts setting, bool playstationPad, bool keyboardLast) {
    switch (setting) {
    case UiPrompts::Xbox: return PromptStyle::Xbox;
    case UiPrompts::PlayStation: return PromptStyle::PlayStation;
    case UiPrompts::Keyboard: return PromptStyle::Keyboard;
    case UiPrompts::Auto: break;
    }
    return keyboardLast ? PromptStyle::Keyboard : playstationPad ? PromptStyle::PlayStation : PromptStyle::Xbox;
}

}  // namespace gt4::ui
