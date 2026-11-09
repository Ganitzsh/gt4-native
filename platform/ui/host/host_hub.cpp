#include "host_hub.h"

#include "ui_records.h"

#include <cstdio>
#include <string>

namespace gt4::ui {

void HostHub::take(const SettingsBridge::Reply& reply) {
    for (const auto& payload : reply.payloads) {
        wire::SettingsHeader header{};
        std::string body;
        if (!decodeSettings(payload.data(), payload.size(), header, body)) continue;
        if (header.kind == wire::kSchema) {
            Schema schema;
            if (!decodeSchema(body, schema)) continue;
            // The Quit row, on the System tab (or a tab of its own if the table has none): the host-run hub only.
            Row quit;
            quit.kind = RowKind::Action;
            quit.key = kQuitKey;
            quit.label = "Quit";
            quit.help = "Closes the program.";
            bool placed = false;
            for (auto& page : schema.pages)
                if (page.id == "system") {
                    page.rows.push_back(quit);
                    placed = true;
                }
            if (!placed) {
                Page page;
                page.id = "system";
                page.title = "System";
                page.rows.push_back(quit);
                schema.pages.push_back(page);
                schema.tabs.push_back(page.id);
            }
            menu_.setSchema(std::move(schema));
        } else if (header.kind == wire::kValues) {
            std::vector<ValueUpdate> values;
            if (!decodeValues(body, values)) continue;
            for (const auto& v : values) menu_.setValue(v.key, v.value, v.unused, v.pending);
        }
    }
    if (!reply.summary.empty()) std::printf("settings page: %s\n", reply.summary.c_str());
}

void HostHub::open(Settings& settings) {
    const auto bytes = encodeSet(++request_, wire::kOpenSchema, "", "");
    take(bridge_.handle(settings, bytes.data(), bytes.size()));
    menu_.open();
}

void HostHub::close() { menu_.close(); }

HostHub::Result HostHub::run(const Output& out, Settings& settings) {
    Result result;
    result.sound = out.sound;
    for (const auto& r : out.requests) {
        if (r.kind == Request::Kind::Action && r.key == kQuitKey) {
            result.quit = true;
            continue;
        }
        const uint32_t kind = r.kind == Request::Kind::Set      ? wire::kSet
                              : r.kind == Request::Kind::Revert ? wire::kRevert
                                                                 : wire::kAction;
        const auto bytes = encodeSet(++request_, kind, r.key, r.value);
        const SettingsBridge::Reply reply = bridge_.handle(settings, bytes.data(), bytes.size());
        result.changed = result.changed || reply.changed;
        result.save = result.save || reply.save;
        take(reply);
    }
    if (out.closed) menu_.close();
    return result;
}

HostHub::Result HostHub::press(Input input, Settings& settings) { return run(menu_.press(input), settings); }

HostHub::Result HostHub::tick(float seconds, Settings& settings) { return run(menu_.tick(seconds), settings); }

void HostHub::refresh(const Settings& settings) {
    const std::vector<uint8_t> payload = bridge_.refresh(settings);
    if (payload.empty()) return;
    SettingsBridge::Reply reply;
    reply.payloads.push_back(payload);
    take(reply);
}

DrawList HostHub::layout(float canvasWidth, PromptStyle prompts) const {
    LayoutOptions options;
    options.canvasWidth = canvasWidth;
    options.prompts = prompts;
    return ui::layout(menu_, options);
}

}  // namespace gt4::ui
