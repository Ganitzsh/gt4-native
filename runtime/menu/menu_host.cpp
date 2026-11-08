#include "menu_host.h"

#include "gt4/link/protocol.h"
#include "kernel.h"
#include "link.h"
#include "log.h"

#include "menu_layout.h"
#include "menu_model.h"
#include "ui_records.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace gt4::rt;
namespace ui = gt4::ui;
using namespace gt4::rt::menu;

namespace {

// ---- the pad (the pad thread and the menu update's thread)

// Who the pad's presses go to. kPage: an open page has them and GT4 sees a neutral pad. kDraining: the page has
// closed, and each port the page read stays neutral to GT4 until it is released. kGame: GT4 has the pad.
enum Focus : int { kGame = 0, kPage = 1, kDraining = 2 };
std::atomic<int> g_focus{kGame};
std::atomic<bool> g_open{false};
std::mutex g_pad_mutex;           // taken under KernelScope on both threads (no interrupt while it is held)
uint8_t g_page_pad[2][18];        // the last bytes each port read while a page had focus
bool g_page_pad_seen[2] = {false, false};
bool g_draining[2] = {false, false};

void neutral(uint8_t data[18]) {
    std::memset(data, 0, 18);
    data[0] = data[1] = 0xFF;                      // buttons, active low
    data[2] = data[3] = data[4] = data[5] = 0x80;  // sticks centred
}

bool released(const uint8_t data[18]) {
    auto centred = [](uint8_t v) { return v >= 0x40 && v <= 0xC0; };
    return data[0] == 0xFF && data[1] == 0xFF && centred(data[2]) && centred(data[3]) && centred(data[4]) &&
           centred(data[5]);
}

// ---- the page (the menu update's thread only)

// ps2_pad.h's bit order: byte 0 select l3 r3 start up right down left, byte 1 l2 r2 l1 r1 triangle circle cross
// square; a pressed button's bit is 0.
enum Button : int { kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7, kL1 = 10, kR1 = 11, kTriangle = 12,
                    kCircle = 13, kCross = 14, kSquare = 15 };

uint32_t pressed_bits(const uint8_t data[18]) {
    uint32_t bits = static_cast<uint32_t>(static_cast<uint8_t>(~data[0])) |
                    static_cast<uint32_t>(static_cast<uint8_t>(~data[1])) << 8;
    // The left stick moves like the D-pad, past half its travel.
    if (data[4] < 0x40) bits |= 1u << kLeft;
    if (data[4] > 0xC0) bits |= 1u << kRight;
    if (data[5] < 0x40) bits |= 1u << kUp;
    if (data[5] > 0xC0) bits |= 1u << kDown;
    return bits;
}

// GT4_REC_HOST_CMD's settings commands (protocol.h): open them at the next safe point, close them.
constexpr uint32_t kCommandOpen = 6, kCommandClose = 7;

struct Page {
    ui::Menu menu;
    uint32_t project = 0;        // the GT4 project the page opened over
    uint32_t last_frame = 0;     // kernel_frame_count() at the previous tick
    uint32_t held = 0;           // inputs down at the previous tick (pressed_bits)
    uint32_t request = 0;        // the last request id sent
    uint32_t list = 0;           // draw lists sent
    uint32_t dropped = 0;        // draw lists over the cap, not sent
    bool schema = false;         // the host's schema has arrived
};

Page &page() {
    static Page *p = new Page();   // never destroyed: other threads may still read the pad state at exit
    return *p;
}

std::atomic<bool> g_open_requested{false};

bool trace() {
    static const bool on = [] {
        const char *e = std::getenv("GT4_TRACE_MENU");
        return e && *e && std::strcmp(e, "0") != 0;
    }();
    return on;
}

// GT4_MENU_OPEN_AT=VBLANK: the settings open once, at the first menu update from that VBlank on. 0: never.
uint32_t open_at() {
    static const uint32_t at = [] {
        const char *e = std::getenv("GT4_MENU_OPEN_AT");
        return e ? static_cast<uint32_t>(std::strtoul(e, nullptr, 10)) : 0u;
    }();
    return at;
}

const char *sound_name(int sound) {
    static const char *const names[] = {"none", "cursor", "ok", "cancel", "disable"};
    return sound >= 0 && sound <= kSoundDisable ? names[sound] : "?";
}

int sound_of(ui::Sound s) {
    switch (s) {
    case ui::Sound::Cursor: return kSoundCursor;
    case ui::Sound::Ok: return kSoundOk;
    case ui::Sound::Cancel: return kSoundCancel;
    case ui::Sound::Disable: return kSoundDisable;
    default: return kSoundNone;
    }
}

void send_set(Page &p, uint32_t kind, const std::string &key, const std::string &value) {
    const std::vector<uint8_t> bytes = ui::encodeSet(++p.request, kind, key, value);
    link_send(GT4_REC_SETTINGS_SET, bytes.data(), static_cast<uint32_t>(bytes.size()));
}

void send_list(Page &p, bool empty) {
    ui::DrawList list;
    if (!empty) {
        list = ui::layout(p.menu, ui::LayoutOptions());
        list.flags = ui::kDimGame | ui::kBlockHud;
    }
    std::vector<uint8_t> bytes;
    if (!ui::encodeDrawList(list, kernel_frame_count(), p.list + 1, p.dropped, bytes)) {
        if (p.dropped++ == 0) log_warn("menu: a draw list over %u bytes was not sent", ui::wire::kUiMaxBytes);
        return;
    }
    ++p.list;
    link_send(GT4_REC_UI, bytes.data(), static_cast<uint32_t>(bytes.size()));
}

// The host's settings records: the schema, values, the results of requests. Read every tick, open or not, so the
// buffer in runtime/link.cpp never fills and the page has the current values when it opens.
int take_settings(Page &p) {
    int sound = kSoundNone;
    std::vector<uint8_t> record;
    while (link_take_settings(record)) {
        ui::wire::SettingsHeader header{};
        std::string body;
        if (!ui::decodeSettings(record.data(), record.size(), header, body)) {
            log_warn("menu: a malformed settings record (%u bytes) ignored", static_cast<unsigned>(record.size()));
            continue;
        }
        if (header.kind == ui::wire::kSchema) {
            ui::Schema schema;
            if (!ui::decodeSchema(body, schema)) {
                log_warn("menu: the host's schema could not be read (%u bytes)", static_cast<unsigned>(body.size()));
                continue;
            }
            p.menu.setSchema(std::move(schema));
            p.schema = true;
        } else if (header.kind == ui::wire::kValues) {
            std::vector<ui::ValueUpdate> values;
            if (!ui::decodeValues(body, values)) {
                log_warn("menu: the host's values could not be read (%u bytes)", static_cast<unsigned>(body.size()));
                continue;
            }
            for (const auto &v : values) p.menu.setValue(v.key, v.value, v.unused, v.pending);
        } else if (!header.ok) {
            log_info("menu: request %u refused: %s", header.request, body.c_str());
            sound = kSoundDisable;
        }
    }
    return sound;
}

void open_page(Page &p, uint32_t project, const char *why) {
    p.project = project;
    p.held = ~0u;   // what is down when the page opens (the press that opened it) acts only after its release
    p.last_frame = kernel_frame_count();
    {
        KernelScope ks;
        std::lock_guard<std::mutex> lock(g_pad_mutex);
        g_page_pad_seen[0] = g_page_pad_seen[1] = false;
        g_draining[0] = g_draining[1] = false;
    }
    g_focus.store(kPage);
    p.menu.open();
    g_open.store(true);
    if (!p.schema) send_set(p, ui::wire::kOpenSchema, "", "");
    log_info("menu: settings opened (%s) over project %08x at VBlank %u", why, project, kernel_frame_count());
}

void close_page(Page &p, const char *why) {
    p.menu.close();
    g_open.store(false);
    {
        KernelScope ks;
        std::lock_guard<std::mutex> lock(g_pad_mutex);
        g_draining[0] = g_page_pad_seen[0];
        g_draining[1] = g_page_pad_seen[1];
        g_focus.store(g_draining[0] || g_draining[1] ? kDraining : kGame);
    }
    send_list(p, true);
    log_info("menu: settings closed (%s) at VBlank %u", why, kernel_frame_count());
}

}  // namespace

namespace gt4::rt::menu {

bool pad_focus(int port, uint8_t data[18]) {
    const int focus = g_focus.load();
    if (focus == kGame) return false;
    port &= 1;
    {
        KernelScope ks;
        std::lock_guard<std::mutex> lock(g_pad_mutex);
        if (g_focus.load() == kPage) {
            std::memcpy(g_page_pad[port], data, 18);
            g_page_pad_seen[port] = true;
        } else {
            // Draining: a port the page did not read, or that is released now, is GT4's again.
            if (!g_draining[port] || released(data)) {
                g_draining[port] = false;
                if (!g_draining[0] && !g_draining[1]) g_focus.store(kGame);
                return false;
            }
        }
    }
    neutral(data);
    return true;
}

void request_open() { g_open_requested.store(true); }

bool is_open() { return g_open.load(); }

}  // namespace gt4::rt::menu

extern "C" int gt4_menu_tick(uint32_t project) {
    Page &p = page();
    int sound = take_settings(p);

    const uint32_t command = link_take_menu_command();
    bool scripted = false;
    if (open_at() && kernel_frame_count() >= open_at()) {
        static bool done = false;
        scripted = !done;
        done = true;
    }
    const bool asked = g_open_requested.exchange(false);
    if (!p.menu.isOpen()) {
        // An open asked for while no menu project runs (a race, a load: GT4's menu object idles) waits for one; the
        // pages never open over a running race (M7 opens them over GT4's own pause).
        static const char *waiting = nullptr;
        const char *why = waiting;
        if (command == kCommandOpen) why = "host command";
        else if (asked) why = "entry";
        else if (scripted) why = "GT4_MENU_OPEN_AT";
        if (!why) return sound;
        if (!project) {
            if (!waiting) log_info("menu: settings asked for (%s) with no menu project: they open with the next", why);
            waiting = why;
            return sound;
        }
        waiting = nullptr;
        open_page(p, project, why);
    } else if (command == kCommandClose) {
        close_page(p, "host command");
        return sound;
    } else if (project != p.project) {
        // GT4 changed project under the page (a timer, the end of a race): the page closes with it.
        log_warn("menu: GT4 changed project (%08x -> %08x) with the settings open", p.project, project);
        close_page(p, "project change");
        return sound;
    }

    // The frame's time, in VBlanks of the NTSC clock GT4 runs on.
    const uint32_t now = kernel_frame_count();
    const float seconds = static_cast<float>(now - p.last_frame) / 59.94f;
    p.last_frame = now;

    uint8_t pads[2][18];
    bool seen[2];
    {
        KernelScope ks;
        std::lock_guard<std::mutex> lock(g_pad_mutex);
        std::memcpy(pads, g_page_pad, sizeof pads);
        seen[0] = g_page_pad_seen[0];
        seen[1] = g_page_pad_seen[1];
    }
    uint32_t down = 0;
    for (int port = 0; port < 2; ++port)
        if (seen[port]) down |= pressed_bits(pads[port]);
    const uint32_t edges = down & ~p.held;
    const uint32_t ups = p.held & ~down;
    p.held = down;

    ui::Output out;
    auto merge = [&](ui::Output o) {
        for (auto &r : o.requests) out.requests.push_back(std::move(r));
        if (o.sound != ui::Sound::None) out.sound = o.sound;
        out.closed = out.closed || o.closed;
    };
    static const struct { int bit; ui::Input input; } kDirections[] = {
        {kUp, ui::Input::Up}, {kDown, ui::Input::Down}, {kLeft, ui::Input::Left}, {kRight, ui::Input::Right}};
    for (const auto &d : kDirections) {
        if (edges & (1u << d.bit)) {
            p.menu.hold(d.input, true);
            merge(p.menu.press(d.input));
        } else if (ups & (1u << d.bit)) {
            p.menu.hold(d.input, false);
        }
    }
    // GT4's menus confirm with Cross and Circle and go back with Triangle (M0); the shoulders change tabs, Square
    // resets a row, Start closes the settings from any depth (MENU_DESIGN 4.2).
    static const struct { int bit; ui::Input input; } kButtons[] = {
        {kCross, ui::Input::Confirm}, {kCircle, ui::Input::Confirm}, {kTriangle, ui::Input::Back},
        {kL1, ui::Input::TabLeft}, {kR1, ui::Input::TabRight}, {kSquare, ui::Input::Reset}, {kStart, ui::Input::Close}};
    for (const auto &b : kButtons)
        if (edges & (1u << b.bit)) merge(p.menu.press(b.input));
    merge(p.menu.tick(seconds));

    for (const auto &r : out.requests) {
        const uint32_t kind = r.kind == ui::Request::Kind::Set      ? ui::wire::kSet
                              : r.kind == ui::Request::Kind::Revert ? ui::wire::kRevert
                                                                    : ui::wire::kAction;
        send_set(p, kind, r.key, r.value);
    }
    if (out.sound != ui::Sound::None) sound = sound_of(out.sound);
    if (trace() && (edges || sound != kSoundNone))
        log_info("menu: VBlank %u, inputs down %04x (new %04x), sound %s, %u request(s)", now, down, edges,
                 sound_name(sound), static_cast<unsigned>(out.requests.size()));
    if (out.closed || !p.menu.isOpen()) close_page(p, "closed by the player");
    else send_list(p, false);
    return sound;
}
