#include "menu_host.h"

#include "gt4/link/protocol.h"
#include "guest.h"
#include "kernel.h"
#include "link.h"
#include "log.h"
#include "runtime.h"

#include "menu_layout.h"
#include "menu_model.h"
#include "pause_page.h"
#include "ui_records.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace gt4::rt;
namespace ui = gt4::ui;
using namespace gt4::rt::menu;

extern "C" int gt4_game_option(int id);   // runtime/sdk.cpp: the host's options (runtime.h kOption*)

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

// ---- Start on GT mode's map (M8; MENU_DESIGN 3.2 point 2; the layout: docs/modern/spikes/M0-menu-flow.md 9)

// The VBlank + 1 at which the menu tick last saw GT mode's map up under ui.entry = integrated (0: not up). The pad
// thread takes Start from GT4 only while it is that fresh, so a map seen before a load or a race never holds Start.
std::atomic<uint32_t> g_map_seen{0};
constexpr uint32_t kMapFreshFrames = 2;
std::atomic<bool> g_map_start{false};    // a Start press taken from GT4 on the map, for the menu tick
std::atomic<bool> g_start_held[2];   // Start down at each port's last read (any pad reader's thread)

bool guest_word(uint32_t a, uint32_t &out) {
    if ((a & 3) || !in_guest_ram(a, 4)) return false;
    out = guest_ref<uint32_t>(a);
    return true;
}

// Whether the menu project `uc` (an update context) shows GT mode's map, at rest, with nothing over it. Read-only:
// its render context (uc + 0xBC, whose + 0x10 points back) keeps its pages on a stack (+ 0x28 begin, + 0x2C end);
// the top is a root window whose + 0xE0 names it, + 0x98 bit 0x00100000 marks the active page, and + 0x00 is 2 at
// rest (3 under a dialog, 4-7 opening or closing). getCurrentPage's slot (+ 0x38) is the last page opened, not the
// top (a closed dialog stays in it), so the stack is read.
bool on_gtmode_map(uint32_t uc) {
    uint32_t rc = 0, back = 0, begin = 0, end = 0, top = 0, state = 0, flags = 0, name = 0;
    if (!guest_word(uc + 0xBC, rc) || !guest_word(rc + 0x10, back) || back != uc) return false;
    if (!guest_word(rc + 0x28, begin) || !guest_word(rc + 0x2C, end) || end <= begin || end - begin > 0x100 ||
        !guest_word(end - 4, top))
        return false;
    if (!guest_word(top, state) || state != 2 || !guest_word(top + 0x98, flags) || !(flags & 0x00100000u) ||
        !guest_word(top + 0xE0, name) || !in_guest_ram(name, 4))
        return false;
    const char *s = static_cast<const char *>(guest_ptr(name));
    return s[0] == 'M' && s[1] == 'A' && s[2] == 'P' && s[3] == 0;
}

// GT4_REC_HOST_CMD's settings commands (protocol.h): open them at the next safe point, close them.
constexpr uint32_t kCommandOpen = 6, kCommandClose = 7;

// The row the hub adds when GT4's Options opened it (MENU_DESIGN 3.2): GT4's own screen, already running under the
// hub. Handled here, never sent to the host.
const char *const kGt4OptionsKey = "menu.gt4_options";

struct Page {
    ui::Menu menu;
    ui::Schema host;             // the host's schema, as it sent it
    uint32_t project = 0;        // the GT4 project the page opened over
    uint32_t entry = 0;          // that project when it is GT4's Options (the page opened in its place), else 0
    uint32_t pause = 0;          // the RacePause the page answers for (M7), else 0
    int items = 0;               // that pause's PauseItem bits
    int *screen = nullptr;       // its brightness and contrast (menu_host.h gt4_pause_tick)
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

// The host's command 6 in a race (MENU_DESIGN 3.2 point 5, M9d2). The game_src copy of RaceBasic::updatePlayCheckPause
// (func_0038B0E0.cpp) reports the race's state each frame through gt4_race_pause_command; a command 6 taken while a
// race runs waits there until GT4's own Start branch would pause (state 0), then that frame acts as Start and GT4 opens
// its pause. That pause shows the native page on Settings, under ui.pause = gt4 too (a deliberate exception for
// original). Waiting, the game tells the host why once (page.waiting), and the host says so.
std::atomic<bool> g_race_open_wanted{false};   // a command 6 waits for the race to be pausable
std::atomic<uint32_t> g_race_seen{0};          // the VBlank the race's Start check last ran (0: never)
std::atomic<int> g_race_wait_state{-1};        // the state the waiting command last saw (its reason sent once)
std::atomic<bool> g_pause_by_command{false};   // the open pause came from command 6: native, on Settings
std::atomic<bool> g_pause_settings{false};     // that pause's page shows the host's first tab once its tabs are in
std::atomic<bool> g_cancel_waiting{false};     // the menu tick's waiting open is done with (the race paused for it)
std::atomic<uint32_t> g_boot_project{0};       // GT4's opening ("boot"): command 6 waits through it (M9d2)

// page.waiting (M9d2): the reason a command 6 waits, to the host (no Page: request 0, any thread).
void send_waiting(const char *reason) {
    const std::vector<uint8_t> bytes = ui::encodeSet(0, ui::wire::kAction, "page.waiting", reason);
    link_send(GT4_REC_SETTINGS_SET, bytes.data(), static_cast<uint32_t>(bytes.size()));
}
std::atomic<uint32_t> g_entry_project{0};   // GT4's Options project just started under ui.entry = integrated

// The page is ticked by GT4's menu update and by the race pause, on whichever guest threads run them. One guest thread
// runs at a time, but an interrupt can switch threads mid-tick, so each tick takes the page with try_take(): a tick
// that finds it taken skips its frame (a blocking lock could wait on a thread that never runs again).
std::atomic<bool> g_page_taken{false};
struct PageTake {
    const bool ok = !g_page_taken.exchange(true);
    ~PageTake() {
        if (ok) g_page_taken.store(false);
    }
};

// The pause page's state other threads read: the pause it is open for (0: none), the VBlank of its last tick, and a
// pause GT4 ended while the page was taken (closed by the next tick that takes it).
std::atomic<uint32_t> g_pause_open{0};
std::atomic<uint32_t> g_pause_tick_frame{0};
std::atomic<uint32_t> g_pause_ended{0};
// A pause page not ticked for this many VBlanks has lost its pause without an end: the pad goes back to GT4. A second,
// so a hitch (a disc read, a host stall that does not stop the bus clock) never closes an open page (lane A).
constexpr uint32_t kPauseStaleFrames = 60;

// The pause page's rows (MENU_DESIGN 3.2, point 4), handled here, never sent to the host.
const char *const kPauseTab = "pause", *const kPauseScreenPage = "pause.screen", *const kPauseExitPage = "pause.exit";
const char *const kKeyContinue = "pause.continue", *const kKeyRetry = "pause.retry",
                  *const kKeySettings = "pause.settings", *const kKeyExit = "pause.exit_race",
                  *const kKeyBrightness = "pause.brightness", *const kKeyContrast = "pause.contrast";
// The strobe's brightness and contrast steps: GT4's Screen Settings stops at -5 and 5 (measured, M7).
constexpr int kScreenMin = -5, kScreenMax = 5;

// What the page shows: the host's schema, and with GT4's Options under the hub, the Game tab first with the GT4
// options row on top (the tab is made when the host's schema has none yet).
ui::Row action_row(const char *key, const char *label, const char *help) {
    ui::Row row;
    row.kind = ui::RowKind::Action;
    row.key = key;
    row.label = label;
    row.help = help;
    return row;
}

// Over a race pause: the pause tab first, with only the items this pause enables, in GT4's order (Continue, Retry when
// RETRYENABLE, Exit, confirmed when CONFIRMEXIT with Continue preselected, Screen Settings when SCREENSETTINGENABLE;
// M0, M7), then Settings, which shows the host's first tab. The host's tabs follow.
void add_pause_tab(const Page &p, ui::Schema &schema) {
    ui::Page tab;
    tab.id = kPauseTab;
    tab.title = "Paused";
    tab.icon = "tab.pause";
    tab.rows.push_back(action_row(kKeyContinue, "Continue", "Back to the race."));
    if (p.items & kPauseRetry) tab.rows.push_back(action_row(kKeyRetry, "Retry", "Start this race again."));
    if (p.items & kPauseConfirmExit) {
        ui::Row exit;
        exit.kind = ui::RowKind::Link;
        exit.key = kPauseExitPage;
        exit.label = "Exit";
        exit.help = "Leave the race.";
        tab.rows.push_back(std::move(exit));
    } else {
        tab.rows.push_back(action_row(kKeyExit, "Exit", "Leave the race."));
    }
    if (p.items & kPauseScreen) {
        ui::Row screen;
        screen.kind = ui::RowKind::Link;
        screen.key = kPauseScreenPage;
        screen.label = "Screen";
        screen.help = "GT4's brightness and contrast, as its own pause sets them.";
        tab.rows.push_back(std::move(screen));
    }
    ui::Row settings = action_row(kKeySettings, "Settings", "The game's settings, with the race paused behind.");
    settings.enabled = !p.host.tabs.empty();
    tab.rows.push_back(std::move(settings));
    schema.pages.push_back(std::move(tab));

    ui::Page screen;
    screen.id = kPauseScreenPage;
    screen.title = "Screen";
    screen.livePreview = true;
    const char *const keys[2] = {kKeyBrightness, kKeyContrast};
    const char *const labels[2] = {"Brightness", "Contrast"};
    for (int i = 0; i < 2; ++i) {
        ui::Row row;
        row.kind = ui::RowKind::Step;
        row.key = keys[i];
        row.label = labels[i];
        row.minimum = static_cast<float>(kScreenMin);
        row.maximum = static_cast<float>(kScreenMax);
        row.step = 1.0f;
        row.value = std::to_string(p.screen ? p.screen[i] : 0);
        screen.rows.push_back(std::move(row));
    }
    schema.pages.push_back(std::move(screen));

    ui::Page exit;
    exit.id = kPauseExitPage;
    exit.title = "Exit";
    ui::Row question;
    question.kind = ui::RowKind::Header;
    question.label = "Are you sure?";
    exit.rows.push_back(std::move(question));
    exit.rows.push_back(action_row(kKeyContinue, "Continue", "Back to the race."));
    exit.rows.push_back(action_row(kKeyExit, "Exit", "Leave the race."));
    schema.pages.push_back(std::move(exit));

    schema.tabs.insert(schema.tabs.begin(), kPauseTab);
}

ui::Schema shown_schema(const Page &p) {
    ui::Schema schema = p.host;
    if (p.pause) add_pause_tab(p, schema);
    if (!p.entry) return schema;
    ui::Row row;
    row.kind = ui::RowKind::Action;
    row.key = kGt4OptionsKey;
    row.label = "GT4 options";
    row.help = "GT4's own Options screen: sound, the controller's button functions, the race display and more.";
    ui::Page *game = nullptr;
    for (auto &page : schema.pages)
        if (page.id == "game") game = &page;
    if (!game) {
        ui::Page page;
        page.id = "game";
        page.title = "Game";
        page.icon = "tab.game";
        schema.pages.insert(schema.pages.begin(), std::move(page));
        game = &schema.pages.front();
    }
    game->rows.insert(game->rows.begin(), std::move(row));
    for (auto it = schema.tabs.begin(); it != schema.tabs.end(); ++it)
        if (*it == "game") {
            schema.tabs.erase(it);
            break;
        }
    schema.tabs.insert(schema.tabs.begin(), "game");
    return schema;
}

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
        // The host's layout inputs (M9): the content's canvas width, and the controller in use for the prompts.
        ui::LayoutOptions options;
        const int width = gt4_game_option(static_cast<int>(kOptionCanvasWidth));
        if (width >= 1000 && width <= 6000) options.canvasWidth = static_cast<float>(width);
        const int style = gt4_game_option(static_cast<int>(kOptionPromptStyle));
        options.prompts = style == 1 ? ui::PromptStyle::PlayStation
                        : style == 2 ? ui::PromptStyle::Keyboard
                                     : ui::PromptStyle::Xbox;
        list = ui::layout(p.menu, options);
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
            p.host = std::move(schema);
            p.menu.setSchema(shown_schema(p));
            p.schema = true;
        } else if (header.kind == ui::wire::kValues) {
            std::vector<ui::ValueUpdate> values;
            if (!ui::decodeValues(body, values)) {
                log_warn("menu: the host's values could not be read (%u bytes)", static_cast<unsigned>(body.size()));
                continue;
            }
            for (const auto &v : values) p.menu.setValue(v.key, v.value, v.unused, v.pending);
            if (trace()) {
                // GT4_TRACE_MENU: what the host sent back (M4's session checks the page's values through it).
                std::string list;
                for (size_t i = 0; i < values.size() && i < 8; ++i)
                    list += (i ? ", " : "") + values[i].key + " = " + values[i].value +
                            (values[i].pending ? " (pending)" : "");
                log_info("menu: %u value(s) from the host: %s%s", static_cast<unsigned>(values.size()), list.c_str(),
                         values.size() > 8 ? ", ..." : "");
            }
        } else if (!header.ok) {
            log_info("menu: request %u refused: %s", header.request, body.c_str());
            sound = kSoundDisable;
        }
    }
    return sound;
}

void open_page(Page &p, uint32_t project, uint32_t entry, const char *why) {
    p.project = project;
    p.entry = entry;
    p.menu.setSchema(shown_schema(p));
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

// Returns kLeaveProject when the page stood in for GT4's Options: the hook then ends that project as GT4's own exit
// from it does.
int close_page(Page &p, const char *why) {
    const bool leave = p.entry != 0;
    p.entry = 0;
    if (p.menu.isOpen()) p.menu.close();
    g_open.store(false);
    {
        KernelScope ks;
        std::lock_guard<std::mutex> lock(g_pad_mutex);
        g_draining[0] = g_page_pad_seen[0];
        g_draining[1] = g_page_pad_seen[1];
        g_focus.store(g_draining[0] || g_draining[1] ? kDraining : kGame);
    }
    send_list(p, true);
    log_info("menu: settings closed (%s) at VBlank %u%s", why, kernel_frame_count(),
             leave ? ", GT4's Options ends with them" : "");
    return leave ? kLeaveProject : 0;
}

void merge(ui::Output &out, ui::Output o) {
    for (auto &r : o.requests) out.requests.push_back(std::move(r));
    if (o.sound != ui::Sound::None) out.sound = o.sound;
    out.closed = out.closed || o.closed;
}

// One frame of the page's input: the pad's new presses (`edges`, and `down`, pressed_bits of both ports) pressed on
// the menu, then the frame's time. `intercept(input, out)` sees each press first; true when it took it.
template <class Intercept>
ui::Output drive(Page &p, uint32_t &edges, uint32_t &down, Intercept intercept) {
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
    down = 0;
    for (int port = 0; port < 2; ++port)
        if (seen[port]) down |= pressed_bits(pads[port]);
    edges = down & ~p.held;
    const uint32_t ups = p.held & ~down;
    p.held = down;

    ui::Output out;
    auto press = [&](ui::Input input) {
        if (!intercept(input, out)) merge(out, p.menu.press(input));
    };
    static const struct { int bit; ui::Input input; } kDirections[] = {
        {kUp, ui::Input::Up}, {kDown, ui::Input::Down}, {kLeft, ui::Input::Left}, {kRight, ui::Input::Right}};
    for (const auto &d : kDirections) {
        if (edges & (1u << d.bit)) {
            p.menu.hold(d.input, true);
            press(d.input);
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
        if (edges & (1u << b.bit)) press(b.input);
    merge(out, p.menu.tick(seconds));
    return out;
}

// Opens the pause page over `pause` (the race pause GT4 has started; its pause.strb is never started).
void open_pause(Page &p, uint32_t pause, int items, int *screen) {
    p.pause = pause;
    p.items = items;
    p.screen = screen;
    open_page(p, 0, 0, "race pause");
    p.menu.selectTab(kPauseTab);   // a pause always opens on its own tab, Continue selected
    p.menu.setSchema(shown_schema(p));
    g_pause_open.store(pause);
    g_pause_tick_frame.store(kernel_frame_count());
    log_info("menu: pause page over RacePause %08x, items%s%s%s", pause, items & kPauseRetry ? " retry" : "",
             items & kPauseConfirmExit ? " confirm-exit" : "", items & kPauseScreen ? " screen" : "");
}


void close_pause(Page &p, const char *why) {
    g_pause_by_command.store(false);
    g_pause_settings.store(false);
    close_page(p, why);
    p.pause = 0;
    p.items = 0;
    p.screen = nullptr;
    p.menu.setSchema(shown_schema(p));
    g_pause_open.store(0u);
}

// A pause that ended while another tick had the page, or that stopped being ticked (pad_focus): its page closes.
void close_ended(Page &p) {
    const uint32_t ended = g_pause_ended.exchange(0u);
    if (ended && ended == p.pause) close_pause(p, "the pause ended");
}

}  // namespace

namespace gt4::rt::menu {

bool pad_focus(int port, uint8_t data[18]) {
    const int focus = g_focus.load();
    // Start on GT mode's map opens the hub (M8): its press is taken from GT4 there (GT4 does nothing with Start on
    // the map: M0, M8) and the menu tick opens the hub.
    const int pt = port & 1;
    const bool start = !(data[0] & (1u << kStart));
    const bool start_edge = !g_start_held[pt].exchange(start) && start;
    if (focus == kGame) {
        const uint32_t seen = g_map_seen.load();
        if (!start || !seen || kernel_frame_count() + 1 - seen > kMapFreshFrames) return false;
        if (start_edge) g_map_start.store(true);
        data[0] |= static_cast<uint8_t>(1u << kStart);
        return true;
    }
    // A pause page no longer ticked (its pause gone without gt4_pause_end) never holds the race's pad: GT4 has it
    // back, and the next tick that takes the page closes it.
    const uint32_t paused = g_pause_open.load();
    if (paused && kernel_frame_count() - g_pause_tick_frame.load() > kPauseStaleFrames) {
        g_pause_ended.store(paused);
        g_focus.store(kGame);
        return false;
    }
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

extern "C" void gt4_menu_entry(uint32_t project) {
    if (gt4_game_option(static_cast<int>(kOptionUiEntry)) != 1) {
        log_info("menu: GT4's Options started (project %08x), ui.entry = hotkey: GT4's screen as it is", project);
        return;
    }
    log_info("menu: GT4's Options started (project %08x), ui.entry = integrated: the settings hub opens over it",
             project);
    g_entry_project.store(project);
}

extern "C" int gt4_menu_tick(uint32_t project) {
    const uint32_t paused = g_pause_open.load();
    if (paused && g_pause_ended.load() != paused) return kSoundNone;   // a race pause has the page (gt4_pause_tick)
    const PageTake take;
    if (!take.ok) return kSoundNone;   // the pause tick has it this frame
    Page &p = page();
    close_ended(p);
    int sound = take_settings(p);

    const uint32_t command = link_take_menu_command();
    bool scripted = false;
    if (open_at() && kernel_frame_count() >= open_at()) {
        static bool done = false;
        scripted = !done;
        done = true;
    }
    const bool asked = g_open_requested.exchange(false);
    // GT mode's map under ui.entry = integrated: Start opens the hub (M8). Only while no page is open.
    const bool map = !p.menu.isOpen() && project && gt4_game_option(static_cast<int>(kOptionUiEntry)) == 1 &&
                     on_gtmode_map(project);
    g_map_seen.store(map ? kernel_frame_count() + 1 : 0u);
    const bool map_start = g_map_start.exchange(false) && map;
    if (!p.menu.isOpen()) {
        // GT4's Options, started under ui.entry = integrated, opens the hub at its first update. Another project
        // running instead means it has already ended: nothing to open.
        uint32_t entry = g_entry_project.load();
        if (entry && project && project != entry) {
            log_warn("menu: GT4's Options (%08x) ended before the hub opened", entry);
            g_entry_project.compare_exchange_strong(entry, 0u);
            entry = 0;
        }
        if (entry && project == entry) {
            g_entry_project.store(0u);
            open_page(p, project, entry, "GT4's Options");
            return sound;
        }
        if (map_start) {
            g_map_seen.store(0u);
            open_page(p, project, 0, "Start on the map");
            return sound;
        }
        // An open asked for while no menu project runs (a race, a load: GT4's menu object idles) waits for one; the
        // pages never open over a running race (M7 opens them over GT4's own pause).
        static const char *waiting = nullptr;
        if (g_cancel_waiting.exchange(false)) waiting = nullptr;   // the race paused for it (M9d2)
        if (command == kCommandClose && (waiting || g_race_open_wanted.load())) {
            waiting = nullptr;   // the host asked to close: a waiting open is dropped
            g_race_open_wanted.store(false);
            return sound;
        }
        const char *why = waiting;
        if (command == kCommandOpen) why = "host command";
        else if (asked) why = "entry";
        else if (scripted) why = "GT4_MENU_OPEN_AT";
        if (!why) return sound;
        if (!project) {
            if (command == kCommandOpen) {
                // A race is running when its Start check ran within the last few VBlanks: it decides (and says why
                // it waits); else a load, a movie or the opening.
                const uint32_t seen = g_race_seen.load();
                if (seen && kernel_frame_count() - seen < 4) {
                    g_race_wait_state.store(-1);
                    g_race_open_wanted.store(true);
                } else {
                    send_waiting("load");
                }
            }
            if (!waiting) log_info("menu: settings asked for (%s) with no menu project: they open with the next", why);
            waiting = why;
            return sound;
        }
        if (project == g_boot_project.load() && (command == kCommandOpen || waiting)) {
            // The opening (GT4's boot): the hub waits for the next menu project (the architect, M9d2), the opening
            // runs on unchanged.
            if (command == kCommandOpen) {
                log_info("menu: settings asked for in the opening: they open with the next menu screen");
                send_waiting("menu");
            }
            waiting = why;
            return sound;
        }
        waiting = nullptr;
        g_race_open_wanted.store(false);
        open_page(p, project, 0, why);
    } else if (command == kCommandClose) {
        return close_page(p, "host command") | sound;
    } else if (project != p.project) {
        // GT4 changed project under the page (a timer, the end of a race): the page closes with it.
        log_warn("menu: GT4 changed project (%08x -> %08x) with the settings open", p.project, project);
        p.entry = 0;   // nothing to leave
        close_page(p, "project change");
        return sound;
    }

    uint32_t edges = 0, down = 0;
    ui::Output out = drive(p, edges, down, [](ui::Input, ui::Output &) { return false; });
    const uint32_t now = kernel_frame_count();

    bool to_gt4 = false;   // the GT4 options row: GT4's screen, under the hub, gets the pad
    for (const auto &r : out.requests) {
        if (p.entry && r.kind == ui::Request::Kind::Action && r.key == kGt4OptionsKey) {
            to_gt4 = true;
            continue;
        }
        const uint32_t kind = r.kind == ui::Request::Kind::Set      ? ui::wire::kSet
                              : r.kind == ui::Request::Kind::Revert ? ui::wire::kRevert
                                                                    : ui::wire::kAction;
        send_set(p, kind, r.key, r.value);
    }
    if (out.sound != ui::Sound::None) sound = sound_of(out.sound);
    if (trace() && (edges || sound != kSoundNone))
        log_info("menu: VBlank %u, inputs down %04x (new %04x), sound %s, %u request(s)", now, down, edges,
                 sound_name(sound), static_cast<unsigned>(out.requests.size()));
    if (to_gt4) {
        p.entry = 0;   // GT4's Options stays
        close_page(p, "to GT4's Options");
    } else if (out.closed || !p.menu.isOpen()) {
        return close_page(p, "closed by the player") | sound;
    } else {
        send_list(p, false);
    }
    return sound;
}

extern "C" void gt4_menu_started(uint32_t project, const char *name) {
    if (name && std::strcmp(name, "boot") == 0) g_boot_project.store(project);
    else if (project == g_boot_project.load()) g_boot_project.store(0u);   // (a project object reused by another)
}

extern "C" int gt4_race_pause_command(uint32_t race, int state) {
    g_race_seen.store(kernel_frame_count() ? kernel_frame_count() : 1u);
    // GT4's menu update doesn't run in a race (the M9d2 session: no menu tick between the route's start and its
    // pause), so the race's check takes the host's commands while the race runs unpaused; paused, the pause tick
    // takes them as before.
    const uint32_t command = link_take_menu_command();
    if (command == kCommandOpen) {
        g_race_wait_state.store(-1);
        g_race_open_wanted.store(true);
        log_info("menu: command 6 in a race (%08x, state %d)", race, state);
    } else if (command == kCommandClose && g_race_open_wanted.load()) {
        g_race_open_wanted.store(false);
        log_info("menu: command 7: the race's waiting open dropped");
    }
    if (!g_race_open_wanted.load()) return 0;
    if (state == 0) {
        g_race_open_wanted.store(false);
        g_race_wait_state.store(-1);
        g_pause_by_command.store(true);
        g_cancel_waiting.store(true);
        log_info("menu: command 6 pauses the race (%08x) through GT4's Start branch", race);
        return 1;
    }
    if (g_race_wait_state.exchange(state) != state) {
        static const char *const why[] = {"", "before the start", "(unused since M9d3)",
                                          "a replay or the demo", "the race's end delay"};
        log_info("menu: command 6 waits in the race (%s)", state > 0 && state < 5 ? why[state] : "?");
        send_waiting(state == 1 ? "race-start" : state == 4 ? "race-end" : "menu");
    }
    return 0;
}

extern "C" void gt4_pause_end(uint32_t pause) {
    if (!pause || g_pause_open.load() != pause) return;
    const PageTake take;
    if (!take.ok) {
        g_pause_ended.store(pause);   // closed by the tick that has the page
        return;
    }
    Page &p = page();
    if (p.pause == pause) close_pause(p, "GT4 ended the pause");
}

extern "C" int gt4_pause_tick(uint32_t pause, int items, int *screen) {
    const bool native = gt4_game_option(static_cast<int>(kOptionUiPause)) == 1 || g_pause_by_command.load();
    if (trace() && screen) {
        // GT4_TRACE_MENU: the brightness and contrast as either pause leaves them (GT4's SETVAL or the page).
        static int shown[2] = {0x7FFFFFFF, 0x7FFFFFFF};
        if (screen[0] != shown[0] || screen[1] != shown[1]) {
            log_info("menu: pause %08x (%s), items %d, brightness %d, contrast %d", pause, native ? "native" : "gt4",
                     items, screen[0], screen[1]);
            shown[0] = screen[0];
            shown[1] = screen[1];
        }
    }
    if (!native && !g_pause_open.load()) return -1;
    const PageTake take;
    if (!take.ok) return native ? 1 : -1;   // the menu update has the page this frame: the pause waits a frame
    Page &p = page();
    close_ended(p);
    if (!native) {
        if (p.pause) close_pause(p, "ui.pause = gt4");   // set while the page was open: GT4's pause takes over
        return -1;
    }
    g_pause_tick_frame.store(kernel_frame_count());
    int sound = take_settings(p);
    if (p.pause && p.pause != pause) close_pause(p, "another pause");
    if (!p.pause) {
        if (p.menu.isOpen()) close_page(p, "race pause");
        open_pause(p, pause, items, screen);
        if (g_pause_by_command.load()) g_pause_settings.store(true);
        return 1 | sound << 8;
    }
    if (g_pause_settings.load() && !p.host.tabs.empty()) {
        g_pause_settings.store(false);
        p.menu.selectTab(p.host.tabs.front());   // command 6's pause opens on Settings
    }
    if (p.screen) {
        p.menu.setValue(kKeyBrightness, std::to_string(p.screen[0]));
        p.menu.setValue(kKeyContrast, std::to_string(p.screen[1]));
    }
    // The host's open and close commands: Settings shows the host's first tab, close goes back to the pause tab.
    const uint32_t command = link_take_menu_command();
    if (command == kCommandOpen && !p.host.tabs.empty()) p.menu.selectTab(p.host.tabs.front());
    else if (command == kCommandClose) p.menu.selectTab(kPauseTab);

    // Triangle and Start on the pause's own pages continue the race, as Triangle does anywhere in GT4's pause (M0);
    // on a Settings tab, Start and Triangle at the tab go back to the pause tab.
    int answer = 0;
    uint32_t edges = 0, down = 0;
    ui::Output out = drive(p, edges, down, [&](ui::Input input, ui::Output &o) {
        if (answer) return true;
        if (input != ui::Input::Back && input != ui::Input::Close) return false;
        if (p.menu.confirming() || p.menu.previewing()) return false;
        if (ui::isPausePage(p.menu.page())) {
            answer = 3;
        } else if (input == ui::Input::Close || !p.menu.inSubPage()) {
            p.menu.selectTab(kPauseTab);
        } else {
            return false;
        }
        o.sound = ui::Sound::Cancel;
        return true;
    });
    for (const auto &r : out.requests) {
        if (r.kind == ui::Request::Kind::Action && r.key.compare(0, 6, "pause.") == 0) {
            if (r.key == kKeyContinue) answer = answer ? answer : 3;
            else if (r.key == kKeyExit) answer = answer ? answer : 4;
            else if (r.key == kKeyRetry && (p.items & kPauseRetry)) answer = answer ? answer : 5;
            else if (r.key == kKeySettings && !p.host.tabs.empty()) p.menu.selectTab(p.host.tabs.front());
            continue;
        }
        if (r.kind != ui::Request::Kind::Action && (r.key == kKeyBrightness || r.key == kKeyContrast)) {
            const int value = std::clamp(std::atoi(r.value.c_str()), kScreenMin, kScreenMax);
            if (p.screen) p.screen[r.key == kKeyBrightness ? 0 : 1] = value;   // as the strobe's SETVAL writes it
            continue;
        }
        const uint32_t kind = r.kind == ui::Request::Kind::Set      ? ui::wire::kSet
                              : r.kind == ui::Request::Kind::Revert ? ui::wire::kRevert
                                                                    : ui::wire::kAction;
        send_set(p, kind, r.key, r.value);
    }
    if (out.sound != ui::Sound::None) sound = sound_of(out.sound);
    if (trace() && (edges || sound != kSoundNone))
        log_info("menu: pause, VBlank %u, inputs down %04x (new %04x), sound %s, answer %d", kernel_frame_count(), down,
                 edges, sound_name(sound), answer);
    if (!answer && !p.menu.isOpen()) answer = 3;   // closed by the engine: the race goes on
    if (answer) {
        close_pause(p, answer == 3 ? "continue" : answer == 4 ? "exit" : "retry");
        return answer | sound << 8;
    }
    send_list(p, false);
    // 0 lets GT4's time limit count (the pause's own pages, no input), 1 clears it (pause_page.h).
    return (ui::pauseTimeLimitCounts(p.menu, edges != 0) ? 0 : 1) | sound << 8;
}
