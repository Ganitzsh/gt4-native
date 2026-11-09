#pragma once
// The settings bridge (docs/modern/MENU_DESIGN.md 3.7, 4.1; ticket M4), host side: the page layout table that puts
// every host option on a page or hides it with a reason, the schema and values the game's pages get
// (GT4_REC_SETTINGS), and the pages' requests (GT4_REC_SETTINGS_SET) applied through setOption, the only writer.
// Host only (it reads platform/sdl/settings.h); the game side is runtime/menu/menu_host.cpp.
#include "menu_model.h"
#include "settings.h"
#include "ui_records.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gt4::ui {

// An option kept off the pages, and why (MENU_DESIGN 4.1: every option is on a page or listed here).
struct HiddenOption {
    const char *key, *reason;
};
const std::vector<HiddenOption> &hiddenOptions();

// The pages (4.1's tabs that have options today) built from the layout table and describe(): choices from an
// option's "a | b" values, steps from its "min..max", its help and unused reason, Square's reset to the preset's
// value, each row's apply class.
Schema buildSchema(const Settings &settings);
// Every row's value, unused reason and pending flag; `pending` marks next-race and restart rows whose value differs
// from `baseline` (the values the game started with, after --set).
std::vector<ValueUpdate> buildValues(const Settings &settings, const Settings &baseline);
// The apply class of a key on a page, false when the key is not on one.
bool applyClass(const std::string &key, ApplyClass &out);

class SettingsBridge {
public:
    // `baseline`: the settings as the game started (after --set), for the pending flags.
    explicit SettingsBridge(const Settings &baseline);

    struct Reply {
        std::vector<std::vector<uint8_t>> payloads;   // GT4_REC_SETTINGS payloads to send, in order
        bool changed = false;   // settings changed: apply them (renderer, display)
        bool save = false;      // this request means a save: a live, next-race or restart value set, a confirm value
                                // kept; never a revert, a refusal or an unconfirmed confirm value (the architect's)
        std::string summary;    // one line for the host's log: the request and what came of it
    };
    // One GT4_REC_SETTINGS_SET payload: kOpenSchema answers the schema; kSet and kRevert go through setOption (a key
    // not on a page is refused); the confirm.keep action marks a confirm value kept. Answers kResult, then kValues
    // with every row that changed.
    Reply handle(Settings &settings, const uint8_t *payload, size_t size);
    // After anything else changed settings (a key, a session's set, the Esc menu): the rows that differ from what
    // the page last got, as one kValues payload; empty when nothing changed or no page has asked for the schema.
    std::vector<uint8_t> refresh(const Settings &settings);
    // What a save writes: `settings` with every confirm value not yet kept at its value before the change.
    Settings savable(const Settings &settings) const;
    bool schemaSent() const { return !sent_.empty(); }

private:
    std::vector<uint8_t> changedValues(const Settings &settings);
    Settings baseline_;
    std::map<std::string, ValueUpdate> sent_;          // the values the page last got, by key
    std::map<std::string, std::string> unconfirmed_;   // confirm-class keys set and not yet kept: their value before
};

}  // namespace gt4::ui
