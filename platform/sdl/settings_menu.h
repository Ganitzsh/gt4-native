#pragma once
// The in-game settings menu (Esc, or the pad's Guide button / Select + Start): the host's display and
// image-quality settings, changed while the game runs. The game is paused while it is open and gets no
// pad input. This is the model (rows, values, navigation); the renderer draws it (Renderer::setMenu).
#include "settings.h"

#include <string>
#include <vector>

namespace gt4 {

struct MenuRow {
    std::string label, value;
    bool header = false;      // a section title (not selectable)
    bool selected = false;
    bool adjustable = false;  // left / right change the value
    bool restart = false;     // applies when the game starts again
};

struct MenuView {
    std::string title;
    std::vector<MenuRow> rows;
    std::string help;         // the selected row's description
    std::string footer;       // the controls
};

class SettingsMenu {
public:
    enum class Input { Up, Down, Left, Right, Accept, Back, PageUp, PageDown };
    struct Result {
        bool changed = false;   // a setting changed: apply and save
        bool closed = false;
        bool quit = false;      // "Quit game" chosen
    };

    // game: the host shows the game's frames (only the options that apply to them are listed).
    explicit SettingsMenu(bool game);
    ~SettingsMenu();

    bool isOpen() const { return open_; }
    void open();
    void close() { open_ = false; }
    Result input(Input input, Settings& settings);
    MenuView view(const Settings& settings) const;
    // Whether a restart-only option differs from the value the game started with.
    bool restartPending(const Settings& settings) const;
    void setStartValues(const Settings& settings);

    struct Item;

private:
    std::vector<Item> items_;
    size_t selected_ = 0;
    bool open_ = false;
    std::vector<std::string> startValues_;   // restart-only options, as the game started
    void move(int direction);
};

}  // namespace gt4
