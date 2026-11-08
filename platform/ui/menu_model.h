#pragma once
// The settings menu's page engine (docs/modern/MENU_DESIGN.md 3.1, 4): pages, tabs, rows, focus, held-direction
// repeat, apply classes, the confirm-and-revert dialog and the transitions' clock. It owns no setting: the schema
// and the values come from the host (M4's bridge), and every change goes back as a Request. No I/O, no clock of its
// own (tick() gets the frame's time), no SDL: built into the game's runtime and into the host.
#include <cstdint>
#include <string>
#include <vector>

namespace gt4::ui {

enum class RowKind : uint8_t {
    Header,   // a section title, not selectable
    Choice,   // one of `choices`; Left / Right step through them
    Step,     // a number from minimum to maximum by step; Left / Right change it (held: faster)
    Action,   // Confirm sends Request::Action with `key`
    Link,     // Confirm opens the page `key`
};

// MENU_DESIGN 3.7.
enum class ApplyClass : uint8_t { Live, Confirm, NextRace, Restart };

struct ChoiceValue {
    std::string value, label;
};

struct Row {
    RowKind kind = RowKind::Choice;
    std::string key, label, help;
    std::vector<ChoiceValue> choices;
    float minimum = 0.0f, maximum = 0.0f, step = 0.0f;
    bool percent = false;          // a 0..1 value shown as a percentage
    std::string unit;              // shown after a Step value ("ms", "%")
    ApplyClass apply = ApplyClass::Live;
    bool previewFirst = false;     // Left / Right show a pending value; Confirm applies it, Back drops it (presets)
    bool enabled = true;           // false: shown, never changed (the Disable sound)
    std::string value;             // the host's current value
    std::string presetValue;       // the value Reset returns to (the preset's); empty: no reset
    std::string unused;            // why the value has no effect now (greyed, still changeable); empty when used
    bool pending = false;          // the host says it takes effect later (next race, restart)
};

struct Page {
    std::string id, title, icon;   // icon: the tab's asset name ("tab.graphics"); empty for a sub-page
    std::vector<Row> rows;
    bool livePreview = false;      // the game's picture shows the change: dimmed less (MENU_DESIGN 4.3)
};

struct Schema {
    std::vector<Page> pages;
    std::vector<std::string> tabs;   // page ids, in tab order
};

enum class Input : uint8_t { Up, Down, Left, Right, Confirm, Back, TabLeft, TabRight, Reset, Close };
enum class Sound : uint8_t { None, Cursor, Ok, Cancel, Disable };

struct Request {
    enum class Kind : uint8_t { Set, Revert, Action } kind = Kind::Set;
    std::string key, value;   // Revert: key back to value (the confirm dialog ran out or was refused)
};

struct Output {
    std::vector<Request> requests;
    Sound sound = Sound::None;
    bool closed = false;
};

// The repeat of a held direction (MENU_DESIGN 4.2), seconds: first after `delay`, then every `interval`; a Step
// row's Left / Right go to `fastInterval` once held for `accelerateAfter`. `delay` and `interval` are GT4's own, 20
// and 6 VBlanks (measured in its menus, docs/modern/spikes/M0-menu-flow.md 7). (Outside Menu: GCC refuses a nested
// struct's default member initialisers in a default argument of the enclosing class.)
struct MenuTiming {
    float delay = 20.0f / 60.0f, interval = 6.0f / 60.0f, fastInterval = 0.04f, accelerateAfter = 1.0f;
    float confirmSeconds = 15.0f;   // the keep-or-revert dialog
    float transition = 0.2f;        // page and tab fades (0 with reduce motion)
};

class Menu {
public:
    using Timing = MenuTiming;
    static constexpr int kVisibleRows = 11;   // rows of the list on screen (menu_layout.cpp)

    explicit Menu(Timing timing = Timing());

    void setSchema(Schema schema);   // keeps the tab and row where they still exist
    // A value the host sent (after a request, or changed by anything else). `unused` and `pending` as in Row.
    void setValue(const std::string& key, const std::string& value, const std::string& unused = std::string(),
                  bool pending = false);
    void setReduceMotion(bool on) { reduceMotion_ = on; }

    void open();
    void close();
    bool isOpen() const { return open_; }

    // An input's press (its edge). Directions also repeat while held: hold() says when one is down or released.
    Output press(Input input);
    void hold(Input direction, bool down);
    // The frame's time: held-direction repeats, the confirm dialog's countdown, the transitions.
    Output tick(float seconds);

    // State the layout reads.
    const Schema& schema() const { return schema_; }
    const Page* page() const;            // the page shown (a tab, or a sub-page opened from it)
    int tab() const { return tab_; }
    int row() const;                     // the selected row of page(), -1 when the page has none
    int scroll() const;                  // the first row of page() on screen
    bool inSubPage() const { return !stack_.empty(); }
    std::string shownValue(const Row& row) const;   // the value to show: a pending preview, or the host's
    bool previewing() const { return preview_.active; }
    bool confirming() const { return confirm_.active; }
    float confirmLeft() const { return confirm_.left; }
    float openTime() const { return openTime_; }     // seconds since open(), for the fade-in
    float pageTime() const { return pageTime_; }     // seconds since the page or tab changed
    float transition() const { return reduceMotion_ ? 0.0f : timing_.transition; }

private:
    struct Place { std::string page; int row = -1; int scroll = 0; };
    Timing timing_;
    Schema schema_;
    bool open_ = false, reduceMotion_ = false;
    int tab_ = 0;
    std::vector<Place> tabs_;      // per tab: its row and scroll, kept while the menu lives
    std::vector<Place> stack_;     // sub-pages opened from the tab, innermost last
    struct Held { Input input = Input::Up; bool down = false; float time = 0.0f, next = 0.0f; } held_;
    struct Preview { bool active = false; std::string key, value; } preview_;
    struct Confirm { bool active = false; std::string key, previous; float left = 0.0f; } confirm_;
    float openTime_ = 0.0f, pageTime_ = 0.0f;

    Page* findPage(const std::string& id);
    const Page* findPage(const std::string& id) const;
    Place& place();
    const Place* placeOrNull() const;
    Row* selectedRow();
    void firstSelectable(Place& place, const Page& page);
    bool move(int direction, bool wrap);
    Output change(int direction, bool repeat);
};

// The text a Step value is shown as ("20 %", "150 ms", "1.4").
std::string formatStep(const Row& row, const std::string& value);

}  // namespace gt4::ui
