#pragma once
// GT4_UI_PAGE_TEST (diagnostic, MENU_DESIGN M3): a settings page drawn without the game, for the hub's goldens, VAL
// and PERF. "hub" lays the hub out from this host's own schema and values (buildSchema, M2's page engine, past its
// fade-in), encodes it as a GT4_REC_UI record and decodes it back, so the list drawn is the one the record path
// carries, with Xbox prompts ("hub:ps" PlayStation's, "hub:kb" the keyboard's); any other value is a file holding a
// GT4_REC_UI payload (a record saved from a live run). Host only.
#include "draw_list.h"
#include "settings.h"

#include <string>

namespace gt4::ui {

// The variable's value, or empty when unset.
std::string pageTestSpec();
// The test's list for a canvas `canvasWidth` units wide (the hub follows it; a file's list keeps its own). False
// with `error` set when the file cannot be read or decoded.
bool pageTestList(const std::string &spec, const Settings &settings, float canvasWidth, DrawList &out,
                  std::string &error);

}  // namespace gt4::ui
