#include "menu_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace gt4::ui {

namespace {

bool selectable(const Row& row) { return row.kind != RowKind::Header; }

float parse(const std::string& text) { return std::strtof(text.c_str(), nullptr); }

std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%g", static_cast<double>(value));
    return text;
}

void merge(Output& into, Output from) {
    for (auto& request : from.requests) into.requests.push_back(std::move(request));
    if (from.sound != Sound::None) into.sound = from.sound;
    into.closed = into.closed || from.closed;
}

}  // namespace

std::string formatStep(const Row& row, const std::string& value) {
    const float v = parse(value);
    char text[48];
    if (row.percent) std::snprintf(text, sizeof text, "%d %%", static_cast<int>(std::lround(v * 100.0f)));
    else if (row.step >= 1.0f) std::snprintf(text, sizeof text, "%ld", std::lround(v));
    else std::snprintf(text, sizeof text, "%g", static_cast<double>(v));
    std::string shown = text;
    if (!row.unit.empty()) shown += " " + row.unit;
    return shown;
}

Menu::Menu(Timing timing) : timing_(timing) {}

Page* Menu::findPage(const std::string& id) {
    for (auto& page : schema_.pages)
        if (page.id == id) return &page;
    return nullptr;
}

const Page* Menu::findPage(const std::string& id) const {
    for (const auto& page : schema_.pages)
        if (page.id == id) return &page;
    return nullptr;
}

void Menu::firstSelectable(Place& place, const Page& page) {
    place.row = -1;
    place.scroll = 0;
    for (size_t i = 0; i < page.rows.size(); ++i)
        if (selectable(page.rows[i])) { place.row = static_cast<int>(i); break; }
}

void Menu::setSchema(Schema schema) {
    schema_ = std::move(schema);
    std::vector<Place> previous = std::move(tabs_);
    tabs_.clear();
    for (const auto& id : schema_.tabs) {
        Place place;
        place.page = id;
        const Page* page = findPage(id);
        auto kept = std::find_if(previous.begin(), previous.end(), [&](const Place& p) { return p.page == id; });
        if (page && kept != previous.end() && kept->row >= 0 && kept->row < static_cast<int>(page->rows.size()) &&
            selectable(page->rows[static_cast<size_t>(kept->row)]))
            place = *kept;
        else if (page)
            firstSelectable(place, *page);
        tabs_.push_back(place);
    }
    // A sub-page whose page has gone closes, with the ones opened from it.
    for (size_t i = 0; i < stack_.size(); ++i)
        if (!findPage(stack_[i].page)) { stack_.resize(i); break; }
    if (tab_ >= static_cast<int>(tabs_.size())) tab_ = 0;
    if (preview_.active) {
        bool found = false;
        for (const auto& page : schema_.pages)
            for (const auto& row : page.rows) found = found || row.key == preview_.key;
        if (!found) preview_ = Preview();
    }
}

void Menu::setValue(const std::string& key, const std::string& value, const std::string& unused, bool pending) {
    for (auto& page : schema_.pages)
        for (auto& row : page.rows)
            if (row.key == key && row.kind != RowKind::Header && row.kind != RowKind::Link &&
                row.kind != RowKind::Action) {
                row.value = value;
                row.unused = unused;
                row.pending = pending;
            }
}

void Menu::open() {
    open_ = true;
    openTime_ = 0.0f;
    pageTime_ = 0.0f;
    stack_.clear();   // reopening lands on the last tab and its row (MENU_DESIGN 4.2)
    held_ = Held();
}

void Menu::close() {
    open_ = false;
    held_ = Held();
    preview_ = Preview();
}

bool Menu::selectTab(const std::string& id) {
    for (size_t i = 0; i < tabs_.size(); ++i)
        if (tabs_[i].page == id) {
            stack_.clear();
            preview_ = Preview();
            tab_ = static_cast<int>(i);
            pageTime_ = 0.0f;
            return true;
        }
    return false;
}

Menu::Place& Menu::place() { return stack_.empty() ? tabs_[static_cast<size_t>(tab_)] : stack_.back(); }

const Menu::Place* Menu::placeOrNull() const {
    if (!stack_.empty()) return &stack_.back();
    if (tab_ < static_cast<int>(tabs_.size())) return &tabs_[static_cast<size_t>(tab_)];
    return nullptr;
}

const Page* Menu::page() const {
    const Place* place = placeOrNull();
    return place ? findPage(place->page) : nullptr;
}

int Menu::row() const {
    const Place* place = placeOrNull();
    return place ? place->row : -1;
}

int Menu::scroll() const {
    const Place* place = placeOrNull();
    return place ? place->scroll : 0;
}

Row* Menu::selectedRow() {
    if (tabs_.empty() && stack_.empty()) return nullptr;
    Place& here = place();
    Page* shown = findPage(here.page);
    if (!shown || here.row < 0 || here.row >= static_cast<int>(shown->rows.size())) return nullptr;
    return &shown->rows[static_cast<size_t>(here.row)];
}

std::string Menu::shownValue(const Row& row) const {
    return preview_.active && preview_.key == row.key ? preview_.value : row.value;
}

// To the next selectable row in `direction`; at either end of the list it wraps (one press) or stops (a repeat).
bool Menu::move(int direction, bool wrap) {
    if (tabs_.empty() && stack_.empty()) return false;
    Place& here = place();
    const Page* shown = findPage(here.page);
    if (!shown || here.row < 0) return false;
    const int count = static_cast<int>(shown->rows.size());
    int index = here.row;
    for (int tries = 0; tries < count; ++tries) {
        index += direction;
        if (index < 0 || index >= count) {
            if (!wrap) return false;
            index = index < 0 ? count - 1 : 0;
        }
        if (selectable(shown->rows[static_cast<size_t>(index)])) break;
    }
    if (index == here.row) return false;
    here.row = index;
    // Keep the row on screen, with the section header above it when there is room.
    const bool headerAbove = index > 0 && shown->rows[static_cast<size_t>(index - 1)].kind == RowKind::Header;
    const int top = headerAbove ? index - 1 : index;
    if (top < here.scroll) here.scroll = top;
    if (index >= here.scroll + kVisibleRows) here.scroll = index - kVisibleRows + 1;
    here.scroll = std::clamp(here.scroll, 0, std::max(0, count - kVisibleRows));
    return true;
}

Output Menu::change(int direction, bool repeat) {
    Output out;
    Row* row = selectedRow();
    if (!row || (row->kind != RowKind::Choice && row->kind != RowKind::Step)) return out;
    if (!row->enabled) {
        if (!repeat) out.sound = Sound::Disable;
        return out;
    }
    const std::string current = shownValue(*row);
    std::string next = current;
    if (row->kind == RowKind::Choice) {
        if (row->choices.empty()) return out;
        auto found = std::find_if(row->choices.begin(), row->choices.end(),
                                  [&](const ChoiceValue& c) { return c.value == current; });
        const int count = static_cast<int>(row->choices.size());
        int index = found == row->choices.end() ? (direction > 0 ? -1 : count)
                                                : static_cast<int>(found - row->choices.begin());
        index = std::clamp(index + direction, 0, count - 1);
        next = row->choices[static_cast<size_t>(index)].value;
    } else {
        const float stepSize = row->step > 0.0f ? row->step : 1.0f;
        const float value = parse(current);
        const float stepped = std::round((value + stepSize * static_cast<float>(direction)) / stepSize) * stepSize;
        next = number(std::clamp(stepped, row->minimum, row->maximum));
    }
    if (next == current) {
        if (!repeat) out.sound = Sound::Disable;   // at the end of its range
        return out;
    }
    out.sound = Sound::Cursor;
    if (row->previewFirst) {
        preview_ = Preview{true, row->key, next};
        if (next == row->value) preview_ = Preview();   // back where it started: nothing pending
        return out;
    }
    if (row->apply == ApplyClass::Confirm && !confirm_.active)
        confirm_ = Confirm{true, row->key, row->value, timing_.confirmSeconds};
    row->value = next;   // shown at once; the host's answer (setValue) has the last word
    out.requests.push_back(Request{Request::Kind::Set, row->key, next});
    return out;
}

Output Menu::press(Input input) {
    Output out;
    if (!open_) return out;
    // The keep-or-revert dialog takes every input while it is up.
    if (confirm_.active) {
        if (input == Input::Confirm) {
            out.requests.push_back(Request{Request::Kind::Action, kConfirmKeep, confirm_.key});
            confirm_ = Confirm();
            out.sound = Sound::Ok;
        } else if (input == Input::Back || input == Input::Close) {
            out.requests.push_back(Request{Request::Kind::Revert, confirm_.key, confirm_.previous});
            setValue(confirm_.key, confirm_.previous);
            confirm_ = Confirm();
            out.sound = Sound::Cancel;
        }
        return out;
    }
    if (preview_.active) {
        switch (input) {
        case Input::Left: case Input::Right: break;   // handled below: the preview moves
        case Input::Confirm: {
            Row* row = selectedRow();
            if (row && row->key == preview_.key) {
                row->value = preview_.value;
                out.requests.push_back(Request{Request::Kind::Set, preview_.key, preview_.value});
            }
            preview_ = Preview();
            out.sound = Sound::Ok;
            return out;
        }
        default:
            preview_ = Preview();
            out.sound = Sound::Cancel;
            if (input != Input::Close) return out;   // the first press only drops the preview
        }
    }
    switch (input) {
    case Input::Up:
    case Input::Down:
        if (move(input == Input::Up ? -1 : 1, true)) out.sound = Sound::Cursor;
        break;
    case Input::Left:
    case Input::Right:
        merge(out, change(input == Input::Left ? -1 : 1, false));
        break;
    case Input::TabLeft:
    case Input::TabRight: {
        const int count = static_cast<int>(tabs_.size());
        if (count < 2) break;
        stack_.clear();
        tab_ = (tab_ + (input == Input::TabLeft ? count - 1 : 1)) % count;
        pageTime_ = 0.0f;
        out.sound = Sound::Cursor;
        break;
    }
    case Input::Confirm: {
        Row* row = selectedRow();
        if (!row) break;
        if (!row->enabled) { out.sound = Sound::Disable; break; }
        if (row->kind == RowKind::Link) {
            if (const Page* target = findPage(row->key)) {
                Place opened;
                opened.page = target->id;
                firstSelectable(opened, *target);
                stack_.push_back(opened);
                pageTime_ = 0.0f;
                out.sound = Sound::Ok;
            } else {
                out.sound = Sound::Disable;
            }
        } else if (row->kind == RowKind::Action) {
            out.requests.push_back(Request{Request::Kind::Action, row->key, std::string()});
            out.sound = Sound::Ok;
        } else if (!row->previewFirst) {
            // A value row that applies at once: Left and Right change it, and it is saved as it changes. Confirm changes
            // nothing (it stepped the value once more, and at the end of the range refused with the 'can't' sound: the
            // owner, 2026-11-11, read that as 'my change can't be saved'): the sound that says it is done.
            out.sound = Sound::Ok;
        }
        break;
    }
    case Input::Back:
        if (!stack_.empty()) {
            stack_.pop_back();
            pageTime_ = 0.0f;
        } else {
            close();
            out.closed = true;
        }
        out.sound = Sound::Cancel;
        break;
    case Input::Reset: {
        Row* row = selectedRow();
        if (!row || row->presetValue.empty() || !row->enabled || row->presetValue == row->value ||
            (row->kind != RowKind::Choice && row->kind != RowKind::Step)) {
            out.sound = Sound::Disable;
            break;
        }
        if (row->apply == ApplyClass::Confirm) confirm_ = Confirm{true, row->key, row->value, timing_.confirmSeconds};
        row->value = row->presetValue;
        out.requests.push_back(Request{Request::Kind::Set, row->key, row->presetValue});
        out.sound = Sound::Ok;
        break;
    }
    case Input::Close:
        close();
        out.closed = true;
        out.sound = Sound::Cancel;
        break;
    }
    return out;
}

void Menu::hold(Input direction, bool down) {
    if (direction != Input::Up && direction != Input::Down && direction != Input::Left && direction != Input::Right)
        return;
    if (down) held_ = Held{direction, true, 0.0f, timing_.delay};
    else if (held_.input == direction) held_ = Held();
}

Output Menu::tick(float seconds) {
    Output out;
    if (!open_) return out;
    openTime_ += seconds;
    pageTime_ += seconds;
    if (confirm_.active) {
        confirm_.left -= seconds;
        if (confirm_.left <= 0.0f) {
            out.requests.push_back(Request{Request::Kind::Revert, confirm_.key, confirm_.previous});
            setValue(confirm_.key, confirm_.previous);
            confirm_ = Confirm();
            out.sound = Sound::Cancel;
        }
        return out;   // nothing repeats under the dialog
    }
    if (!held_.down) return out;
    held_.time += seconds;
    for (int fired = 0; held_.down && held_.time >= held_.next && fired < 8; ++fired) {
        const bool horizontal = held_.input == Input::Left || held_.input == Input::Right;
        const int direction = held_.input == Input::Up || held_.input == Input::Left ? -1 : 1;
        if (horizontal) {
            merge(out, change(direction, true));
        } else if (move(direction, false)) {
            out.sound = Sound::Cursor;
        }
        const Row* row = selectedRow();
        const bool fast = horizontal && row && row->kind == RowKind::Step && held_.time >= timing_.accelerateAfter;
        held_.next += fast ? timing_.fastInterval : timing_.interval;
    }
    return out;
}

}  // namespace gt4::ui
