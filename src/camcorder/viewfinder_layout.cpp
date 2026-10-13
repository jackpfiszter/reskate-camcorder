#include "viewfinder_layout.h"

namespace dingosdk::camcorder {
namespace {
constexpr std::string_view anchor_words[]{"top-left", "top", "top-right", "left", "centre", "right", "bottom-left", "bottom", "bottom-right"};
constexpr std::string_view show_words[]{"text", "dot", "battery", "zoom-bar", "box", "centre", "grid", "corners"};
constexpr std::string_view condition_words[]{"recording", "zooming", "tripod", "setup", "handheld",
                                             "low-battery", "low-tape", "tape-end", "fisheye", "light"};
template <class Enum, std::size_t N> std::optional<Enum> find_word(const std::string_view (&words)[N], std::string_view word) {
    for (std::size_t i = 0; i < N; ++i)
        if (words[i] == word) return static_cast<Enum>(i);
    return std::nullopt;
}
ViewfinderElement text(std::string words, Anchor anchor, float x, float y, float size = 22) {
    ViewfinderElement e;
    e.text = std::move(words);
    e.anchor = anchor;
    e.offset = {x, y};
    e.size = size;
    return e;
}
} // namespace

ViewfinderLayout vx1000_viewfinder() {
    // Readouts inset 30 lines from the picture's corners, 22 lines tall: the VX1000's LCD in spirit.
    ViewfinderLayout layout;
    auto &e = layout.elements;
    e.push_back(text("STBY", Anchor::top_left, 30, 30));
    e.back().when = {{Condition::recording, false}};
    ViewfinderElement light;
    light.show = Show::dot;
    light.offset = {30, 33.96f}; // centred on the REC beside it
    light.size = 14.08f;
    light.ink = Ink::rec;
    light.when = {{Condition::recording, true}};
    e.push_back(light);
    e.push_back(text("REC", Anchor::top_left, 50.08f, 30));
    e.back().ink = Ink::rec;
    e.back().when = {{Condition::recording, true}};
    e.push_back(text("{counter}", Anchor::top_right, 30, 30));
    e.push_back(text("{remaining}", Anchor::top_right, 30, 58.6f));
    e.back().when = {{Condition::low_tape, false}};
    e.push_back(text("{remaining}", Anchor::top_right, 30, 58.6f));
    e.back().ink = Ink::warning;
    e.back().when = {{Condition::low_tape, true}};
    ViewfinderElement battery;
    battery.show = Show::battery;
    battery.offset = {30, 60.8f};
    battery.size = 16.5f;
    e.push_back(battery);
    ViewfinderElement zoom;
    zoom.show = Show::zoom_bar;
    zoom.anchor = Anchor::bottom_left;
    zoom.extent = {0.28f, 0};
    zoom.when = {{Condition::zooming, true}};
    e.push_back(zoom);
    e.push_back(text("TRIPOD {tripod}", Anchor::bottom_right, 30, 34.4f, 17.6f));
    e.back().when = {{Condition::tripod, true}, {Condition::setup, false}};
    e.push_back(text("TRIPOD {tripod}  SETUP", Anchor::bottom_right, 30, 34.4f, 17.6f));
    e.back().when = {{Condition::tripod, true}, {Condition::setup, true}};
    return layout;
}

std::string_view anchor_word(Anchor a) noexcept { return anchor_words[static_cast<std::size_t>(a)]; }
std::optional<Anchor> anchor_from_word(std::string_view w) noexcept { return find_word<Anchor>(anchor_words, w); }
std::string_view show_word(Show s) noexcept { return show_words[static_cast<std::size_t>(s)]; }
std::optional<Show> show_from_word(std::string_view w) noexcept { return find_word<Show>(show_words, w); }
std::string_view condition_word(Condition c) noexcept { return condition_words[static_cast<std::size_t>(c)]; }
std::optional<When> when_from_word(std::string_view w) noexcept {
    const bool is = w.empty() || w.front() != '!';
    if (!is) w.remove_prefix(1);
    const auto what = find_word<Condition>(condition_words, w);
    if (!what) return std::nullopt;
    return When{*what, is};
}
std::string when_word(const When &w) { return (w.is ? "" : "!") + std::string(condition_word(w.what)); }
} // namespace dingosdk::camcorder
