#pragma once
// A camera's viewfinder as data: what it shows over the picture and where (profiles.h reads it from
// a camera's "viewfinder", viewfinder.h lays it out). Each element is a piece of text (with fields
// such as {counter} and {remaining} filled in each frame), a dot, the battery, the zoom bar, a box,
// or a framing guide, put at one of the picture's corners, edges or middle, and shown always or
// only while something is so (recording, zooming, the tape low...).
//
// Sizes and offsets are in 480ths of the picture's height (an NTSC line), so a layout sits the same
// at any resolution and on any picture shape.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::camcorder {
enum class Anchor : std::uint8_t { top_left, top, top_right, left, centre, right, bottom_left, bottom, bottom_right };
enum class Show : std::uint8_t {
    text,      // `text`, its fields filled in
    dot,       // a filled circle `size` across (the REC light)
    battery,   // a battery `size` tall with its charge in four bars; it blinks when nearly flat
    zoom_bar,  // W, a bar `extent[0]` of the picture's width long, T; filled to the zoom
    box,       // a rectangle `extent` (480ths) in size, filled or an outline
    centre,    // a cross `size` across in the picture's middle
    grid,      // the picture in `size` by `size` (3: thirds), lines across it
    corners,   // brackets `size` long at the picture's corners, `offset` in from them
};
// What must be so for an element to show.
enum class Condition : std::uint8_t {
    recording, zooming, tripod, setup, handheld, low_battery, low_tape, tape_end, fisheye, light,
};
struct When {
    Condition what{};
    bool is{true}; // "!recording": while not recording
    bool operator==(const When &) const = default;
};
// An element's colour: the viewfinder's text, REC or warning colour, or its own.
enum class Ink : std::uint8_t { text, rec, warning, own };

struct ViewfinderElement {
    Show show{Show::text};
    Anchor anchor{Anchor::top_left};
    std::array<float, 2> offset{30, 30}; // in from the anchor: right and down from a left or top one, left and up from a right or bottom one
    float size{22};                      // text height; a dot's or a cross's size; the battery's height; a grid's divisions
    std::array<float, 2> extent{};       // a box's width and height; a zoom bar's length (of the picture's width)
    std::string text;                    // a text's: {counter} {remaining} {minutes} {status} {zoom} {focal} {date} {time} {tripod} {clip} {battery} {camera} {lens} {format}
    Ink ink{Ink::text};
    std::array<std::uint8_t, 3> colour{255, 255, 255}; // Ink::own's
    float opacity{1};
    bool filled{};  // a box's
    float thickness{1.5f}; // lines' and outlines', in screen pixels at 1080 lines
    float blink{};  // seconds a blink (on for half of it); 0 steady
    bool shadow{true}; // text and lines get a dark shadow, to read over any picture
    std::vector<When> when;
    bool operator==(const ViewfinderElement &) const = default;
};

struct ViewfinderLayout {
    std::array<std::uint8_t, 3> text{240, 240, 240}, rec{235, 40, 40}, warning{255, 190, 40};
    std::vector<ViewfinderElement> elements;
    bool operator==(const ViewfinderLayout &) const = default;
};
// The VX1000's: STBY or a red REC top left with the battery under it, the counter and the tape
// left top right, the zoom bar bottom left while it moves, the tripod bottom right.
ViewfinderLayout vx1000_viewfinder();

std::string_view anchor_word(Anchor) noexcept;
std::optional<Anchor> anchor_from_word(std::string_view) noexcept;
std::string_view show_word(Show) noexcept;
std::optional<Show> show_from_word(std::string_view) noexcept;
std::string_view condition_word(Condition) noexcept;
// "recording", or "!recording" for while not.
std::optional<When> when_from_word(std::string_view) noexcept;
std::string when_word(const When &);
} // namespace dingosdk::camcorder
