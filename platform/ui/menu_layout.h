#pragma once
// Lays the settings menu out as a draw list (docs/modern/MENU_DESIGN.md 4, 5; the mockup
// docs/modern/menu/mock_settings.svg): top bar with the title and the tabs, the list, the detail panel when the
// canvas is wide enough, the help bar with the controller's prompts, and the keep-or-revert dialog. Canvas units
// (draw_list.h); the canvas follows the content's aspect, so 21:9 widens the gaps, 4:3 drops the detail panel.
#include "draw_list.h"
#include "menu_model.h"

namespace gt4::ui {

enum class PromptStyle : uint8_t { Xbox, PlayStation, Keyboard };   // the connected controller's glyphs (5.3)

// Whether the UI's art is packed (MENU_DESIGN M10 sets it with the atlas). Until then the layout draws its own
// stand-ins, never an asset: the tabs as text chips, the controller prompts as key caps with the button's name as text
// (Xbox letters, PlayStation names as words, the keyboard's default keys), so no white square is shown and nothing
// borrows anyone's button artwork (MENU_DESIGN M3, the architect's and the coordinator's rule).
inline constexpr bool kUiAssetsPacked = false;

// The key cap's text for `button` (confirm, back, reset, tabs, tab_left, tab_right) in `style`.
const char* buttonName(PromptStyle style, const char* button);

struct LayoutOptions {
    float canvasWidth = 1920.0f;
    PromptStyle prompts = PromptStyle::Xbox;
};

DrawList layout(const Menu& menu, const LayoutOptions& options);

// The style guide's palette (MENU_DESIGN 5.1), 0xRRGGBBAA.
namespace palette {
constexpr uint32_t kInk = 0x0B0B0CFFu, kBar = 0x000000E0u, kPanel = 0x2A2C30EBu, kText = 0xF4F4F2FFu,
                   kSecondary = 0xB9BCC2FFu, kAccent = 0xF29A1FFFu, kChip = 0xE8C21CFFu, kPaper = 0xE9EAECFFu,
                   kLine = 0x55585DFFu, kWarning = 0xD8452BFFu;
}

}  // namespace gt4::ui
