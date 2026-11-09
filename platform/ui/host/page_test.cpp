#include "page_test.h"

#include "menu_layout.h"
#include "settings_schema.h"
#include "ui_records.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

namespace gt4::ui {

std::string pageTestSpec() {
    const char *value = std::getenv("GT4_UI_PAGE_TEST");
    return value ? value : "";
}

bool pageTestList(const std::string &spec, const Settings &settings, float canvasWidth, DrawList &out,
                  std::string &error) {
    std::vector<uint8_t> record;
    if (spec == "hub" || spec == "hub:ps" || spec == "hub:kb") {
        Menu menu;
        menu.setSchema(buildSchema(settings));
        menu.open();
        menu.tick(5.0f);   // past the fade-in and the page's slide
        LayoutOptions options;
        options.canvasWidth = canvasWidth;
        options.prompts = spec == "hub:ps" ? PromptStyle::PlayStation
                        : spec == "hub:kb" ? PromptStyle::Keyboard
                                           : PromptStyle::Xbox;
        if (!encodeDrawList(layout(menu, options), 0, 1, 0, record)) {
            error = "the hub's list is over the record's size cap";
            return false;
        }
    } else {
        std::ifstream file(spec, std::ios::binary);
        if (!file) {
            error = "cannot read " + spec;
            return false;
        }
        record.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    if (!decodeDrawList(record.data(), record.size(), out)) {
        error = "not a GT4_REC_UI payload: " + spec;
        return false;
    }
    return true;
}

}  // namespace gt4::ui
