// The settings bridge (platform/ui/host/settings_schema, MENU_DESIGN.md M4): every option on a page or hidden with a
// reason; a set through the bridge equals setOption; refusals (a hidden key, a bad value) answer ok = 0 with the
// reason; the apply classes; a preset's followers come back as values; another writer's change comes back on refresh;
// and the architect's save rule checked on a real settings file (lane C: save() is a no-op in session runs, so the file
// check is here): a live set saves, a confirm set saves only when kept, a reverted confirm leaves the file byte for
// byte.
#include "prompt_style.h"
#include "settings_schema.h"
#include "temp_path.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

uint32_t requestId = 0;
std::vector<uint8_t> set(uint32_t kind, const std::string &key, const std::string &value) {
    return encodeSet(++requestId, kind, key, value);
}

struct Decoded {
    wire::SettingsHeader header{};
    std::string body;
};
std::vector<Decoded> decode(const SettingsBridge::Reply &reply) {
    std::vector<Decoded> out;
    for (const auto &p : reply.payloads) {
        Decoded d;
        if (decodeSettings(p.data(), p.size(), d.header, d.body)) out.push_back(d);
    }
    return out;
}

bool resultOk(const SettingsBridge::Reply &reply, std::string *why = nullptr) {
    for (const auto &d : decode(reply))
        if (d.header.kind == wire::kResult) {
            if (why) *why = d.body;
            return d.header.ok != 0;
        }
    return false;
}

std::vector<ValueUpdate> values(const SettingsBridge::Reply &reply) {
    std::vector<ValueUpdate> out;
    for (const auto &d : decode(reply))
        if (d.header.kind == wire::kValues) decodeValues(d.body, out);
    return out;
}

bool hasValue(const std::vector<ValueUpdate> &list, const std::string &key, const std::string &value) {
    for (const auto &v : list)
        if (v.key == key && v.value == value) return true;
    return false;
}

std::string readFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

int main() {
    Settings start;
    applyPreset(start, Preset::High);

    // Every option is on a page or hidden with a reason; nothing listed is missing from the options.
    {
        std::set<std::string> keys;
        for (const auto &info : describe(start)) keys.insert(info.key);
        std::set<std::string> hidden;
        for (const auto &h : hiddenOptions()) {
            hidden.insert(h.key);
            check(keys.count(h.key) > 0, std::string("hidden option exists: ") + h.key);
            check(h.reason && *h.reason, std::string("hidden option has a reason: ") + h.key);
        }
        for (const auto &key : keys) {
            ApplyClass apply;
            const bool onPage = applyClass(key, apply);
            check(onPage != (hidden.count(key) > 0), "on a page or hidden, not both: " + key);
        }
        const Schema schema = buildSchema(start);
        size_t rows = 0;
        for (const auto &page : schema.pages)
            for (const auto &row : page.rows)
                if (row.kind != RowKind::Header) {
                    ++rows;
                    check(keys.count(row.key) > 0, "a row's key is an option: " + row.key);
                    check(!row.help.empty(), "a row has help: " + row.key);
                }
        check(rows + hidden.size() == keys.size(), "each option once");
        check(schema.tabs.size() == schema.pages.size() && !schema.tabs.empty(), "every page is a tab");
    }

    // Row shapes and apply classes (MENU_DESIGN 3.7).
    {
        const Schema schema = buildSchema(start);
        const Row *preset = nullptr, *scale = nullptr;
        for (const auto &page : schema.pages)
            for (const auto &row : page.rows) {
                if (row.key == "graphics.preset") preset = &row;
                if (row.key == "render.scale") scale = &row;
            }
        check(preset && preset->kind == RowKind::Choice && preset->previewFirst, "the preset shows its card first");
        check(scale && scale->kind == RowKind::Step && scale->minimum == 50.0f && scale->maximum == 200.0f &&
                  scale->step == 10.0f,
              "render scale is a 50..200 step of 10");
        ApplyClass a;
        check(applyClass("display.mode", a) && a == ApplyClass::Confirm, "window mode is confirm");
        check(applyClass("display.hdr", a) && a == ApplyClass::Confirm, "HDR is confirm");
        check(applyClass("display.race_frames", a) && a == ApplyClass::NextRace, "race frames is next race");
        check(applyClass("effects.mirror", a) && a == ApplyClass::NextRace, "mirror detail is next race");
        check(applyClass("aspect.ratio", a) && a == ApplyClass::Restart, "aspect ratio is restart");
        check(applyClass("render.aa", a) && a == ApplyClass::Live, "anti-aliasing is live");
        check(!applyClass("display.window_width", a) && !applyClass("no.such", a), "hidden and unknown keys: no class");
    }

    // ui.prompts (M9): auto follows the input used last and the pad's type; a fixed choice wins over both (a
    // PlayStation player under Steam Input, whose pad reports itself as an Xbox one). Values as kOptionPromptStyle.
    {
        check(promptStyle(UiPrompts::Auto, false, false) == PromptStyle::Xbox &&
                  promptStyle(UiPrompts::Auto, true, false) == PromptStyle::PlayStation &&
                  promptStyle(UiPrompts::Auto, true, true) == PromptStyle::Keyboard,
              "auto: Xbox, PlayStation on a PlayStation pad, the keyboard when used last");
        check(promptStyle(UiPrompts::PlayStation, false, true) == PromptStyle::PlayStation &&
                  promptStyle(UiPrompts::Xbox, true, true) == PromptStyle::Xbox &&
                  promptStyle(UiPrompts::Keyboard, true, false) == PromptStyle::Keyboard,
              "a fixed choice wins");
        check(static_cast<int>(PromptStyle::Xbox) == 0 && static_cast<int>(PromptStyle::PlayStation) == 1 &&
                  static_cast<int>(PromptStyle::Keyboard) == 2,
              "PromptStyle's values are kOptionPromptStyle's (runtime.h)");
        Settings s = start;
        check(getOption(s, "ui.prompts") == "auto" && setOption(s, "ui.prompts", "playstation").empty() &&
                  s.uiPrompts == UiPrompts::PlayStation,
              "ui.prompts defaults to auto and takes playstation");
        for (Preset preset : {Preset::Original, Preset::Low, Preset::Ultra}) {
            applyPreset(s, preset);
            check(s.uiPrompts == UiPrompts::PlayStation, "no preset sets ui.prompts (a player's choice)");
        }
    }

    // No confirm-class key is a preset member (lane C's note): savable() puts an unkept confirm value back with
    // setOption, which would turn a preset member's file into preset = custom. Each confirm key, set to another
    // allowed value, survives every preset.
    {
        for (const auto &info : describe(start)) {
            ApplyClass apply;
            if (!applyClass(info.key, apply) || apply != ApplyClass::Confirm) continue;
            std::string other;
            for (const std::string candidate : {"borderless", "windowed", "on", "off", "true", "false", "1", "8", "10"})
                if (candidate != info.value) {
                    Settings probe = start;
                    if (setOption(probe, info.key, candidate).empty()) {
                        other = candidate;
                        break;
                    }
                }
            check(!other.empty(), "the preset test found another value for " + info.key);
            for (Preset preset : {Preset::Original, Preset::Low, Preset::Medium, Preset::High, Preset::Ultra}) {
                Settings s = start;
                setOption(s, info.key, other);
                applyPreset(s, preset);
                check(getOption(s, info.key) == other,
                      "a confirm-class key is no preset's member: " + info.key + " under " + name(preset));
            }
        }
    }

    // A set through the bridge equals setOption; refusals answer ok = 0 with the reason.
    {
        Settings settings = start, direct = start;
        SettingsBridge bridge(start);
        const auto request = set(wire::kOpenSchema, "", "");
        const auto open = bridge.handle(settings, request.data(), request.size());
        check(decode(open).size() == 1 && decode(open)[0].header.kind == wire::kSchema && bridge.schemaSent(),
              "the schema request answers the schema");
        auto payload = set(wire::kSet, "render.upscaler", "fsr2");
        auto reply = bridge.handle(settings, payload.data(), payload.size());
        setOption(direct, "render.upscaler", "fsr2");
        check(resultOk(reply) && reply.changed && reply.save, "a live set applies and saves");
        check(hasValue(values(reply), "render.upscaler", "fsr2"), "the new value comes back");
        check(hasValue(values(reply), "graphics.preset", "custom"), "and the preset it moved to");
        bool same = true;
        for (const auto &info : describe(direct)) same = same && getOption(settings, info.key) == info.value;
        check(same, "the bridge's set equals setOption's");

        payload = set(wire::kSet, "render.scale", "999");
        std::string why;
        reply = bridge.handle(settings, payload.data(), payload.size());
        check(!resultOk(reply, &why) && !why.empty() && !reply.save && !reply.changed, "a bad value is refused");
        payload = set(wire::kSet, "display.window_width", "800");
        reply = bridge.handle(settings, payload.data(), payload.size());
        check(!resultOk(reply, &why) && why.find("not on a settings page") != std::string::npos,
              "a hidden key is refused");
        payload = set(wire::kAction, "no.such.action", "");
        reply = bridge.handle(settings, payload.data(), payload.size());
        check(!resultOk(reply), "an unknown action is refused");

        // A preset's followers come back with it.
        payload = set(wire::kSet, "graphics.preset", "low");
        reply = bridge.handle(settings, payload.data(), payload.size());
        check(resultOk(reply) && values(reply).size() > 5, "a preset sends its followers' values");

        // Another writer (a key, a session's set): refresh sends what changed, then nothing.
        setOption(settings, "post.film_grain", "0.3");
        const auto refreshed = bridge.refresh(settings);
        wire::SettingsHeader h{};
        std::string body;
        std::vector<ValueUpdate> list;
        check(decodeSettings(refreshed.data(), refreshed.size(), h, body) && h.kind == wire::kValues &&
                  decodeValues(body, list) && hasValue(list, "post.film_grain", "0.3"),
              "refresh sends another writer's change");
        check(bridge.refresh(settings).empty(), "and nothing when nothing changed");
    }

    // Next-race and restart rows are pending against the values the game started with.
    {
        Settings settings = start;
        SettingsBridge bridge(start);
        const auto open = set(wire::kOpenSchema, "", "");
        bridge.handle(settings, open.data(), open.size());
        const auto payload = set(wire::kSet, "display.race_frames", "fields");
        const auto reply = bridge.handle(settings, payload.data(), payload.size());
        bool pending = false;
        for (const auto &v : values(reply))
            if (v.key == "display.race_frames") pending = v.pending;
        check(resultOk(reply) && reply.save && pending, "a next-race value saves and is pending");
    }

    // The save rule on a real settings file: live saves; confirm saves only when kept; a revert leaves the file as it
    // was; a live save while a confirm waits writes the confirm key's old value.
    {
        const auto path = gt4::test::uniqueTempPath("gt4_bridge_settings", ".ini");
        Settings settings = start;
        SettingsBridge bridge(start);
        const auto save = [&](const SettingsBridge::Reply &reply) {
            if (reply.save) saveSettings(bridge.savable(settings), path.string());
        };
        saveSettings(bridge.savable(settings), path.string());
        const auto open = set(wire::kOpenSchema, "", "");
        bridge.handle(settings, open.data(), open.size());
        const std::string first = readFile(path);

        auto payload = set(wire::kSet, "post.vignette", "0.2");
        auto reply = bridge.handle(settings, payload.data(), payload.size());
        save(reply);
        const std::string afterLive = readFile(path);
        check(reply.save && afterLive != first && afterLive.find("vignette = 0.2") != std::string::npos,
              "a live set is saved");

        const std::string modeBefore = getOption(settings, "display.mode");
        payload = set(wire::kSet, "display.mode", "borderless");
        reply = bridge.handle(settings, payload.data(), payload.size());
        save(reply);
        check(resultOk(reply) && !reply.save && readFile(path) == afterLive, "a confirm set is not saved yet");
        payload = set(wire::kSet, "post.film_grain", "0.1");
        reply = bridge.handle(settings, payload.data(), payload.size());
        save(reply);
        const std::string duringConfirm = readFile(path);
        check(reply.save && duringConfirm.find("grain = 0.1") != std::string::npos &&
                  duringConfirm.find("mode = borderless") == std::string::npos,
              "a live save while a confirm waits keeps the confirm key's old value in the file");
        payload = set(wire::kRevert, "display.mode", modeBefore);
        reply = bridge.handle(settings, payload.data(), payload.size());
        save(reply);
        check(resultOk(reply) && !reply.save && readFile(path) == duringConfirm &&
                  getOption(settings, "display.mode") == modeBefore,
              "a reverted confirm leaves the file byte for byte");

        payload = set(wire::kSet, "display.mode", "borderless");
        reply = bridge.handle(settings, payload.data(), payload.size());
        save(reply);
        check(!reply.save && readFile(path) == duringConfirm, "confirm set again: not saved");
        payload = set(wire::kAction, kConfirmKeep, "display.mode");
        reply = bridge.handle(settings, payload.data(), payload.size());
        save(reply);
        check(resultOk(reply) && reply.save && readFile(path).find("mode = borderless") != std::string::npos,
              "a kept confirm is saved");
        payload = set(wire::kAction, kConfirmKeep, "display.mode");
        reply = bridge.handle(settings, payload.data(), payload.size());
        check(!resultOk(reply) && !reply.save, "keeping twice is refused");

        payload = set(wire::kSet, "display.race_frames", "fields");
        reply = bridge.handle(settings, payload.data(), payload.size());
        save(reply);
        check(reply.save && readFile(path).find("race_frames = fields") != std::string::npos,
              "a next-race set is saved");
        std::filesystem::remove(path);
    }

    if (failures) {
        std::printf("ui_settings_schema: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_settings_schema: all checks passed\n");
    return 0;
}
