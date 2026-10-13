#pragma once
// What the screen shows over the shot, laid out in pixels for the overlay to draw (ReSkate draws
// its overlay with Dear ImGui). The overlay is not on the picture a recording keeps unless
// the player captures the screen, so a clean feed keeps the frame and the lens and drops the
// readouts.
//
// - The picture, the camera's shape (4:3 for the VX1000): the game renders the full window; the
//   bars either side (or above and below) are painted black.
// - The fisheye housing's shadow: black outside a circle, while the zoom is wide enough to see it.
// - The readouts: the camera's viewfinder layout (viewfinder_layout.h; the VX1000's by default:
//   STBY or a red REC, the tape counter, the minutes of tape left, the battery, and the W-T zoom
//   bar while the zoom moves, in the spirit of the original, not traced from it), as text and
//   plain shapes for the overlay to draw.
#include "camcorder.h"
#include "viewfinder_layout.h"
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace dingosdk::camcorder {
struct Rect {
    float x{}, y{}, width{}, height{};
};
// The largest `aspect` rectangle centred in the screen.
Rect picture_rect(float screen_width, float screen_height, float aspect = 4.0f / 3.0f) noexcept;

struct Colour {
    std::uint8_t r{}, g{}, b{}, a{255};
    bool operator==(const Colour &) const = default;
};
enum class Align : std::uint8_t { left, right, centre };
struct Label {
    std::string text;
    float x{}, y{};   // top-left (Align::left), top-right (Align::right) or top-centre (Align::centre), in pixels
    float size{};     // text height, in pixels
    Colour colour;
    Align align{Align::left};
    bool shadow{true};
};
// A shape: a dot (centre x0, y0, radius x1), a filled box or an outline (corners x0, y0 to x1, y1),
// or a line (x0, y0 to x1, y1).
struct Mark {
    enum class Kind : std::uint8_t { dot, box, outline, line };
    Kind kind{Kind::box};
    float x0{}, y0{}, x1{}, y1{};
    float thickness{1};
    Colour colour;
    bool shadow{};
};
struct Circle {
    float x{}, y{}, radius{};
};

struct ViewfinderOptions {
    bool clean_feed{};   // the frame and the lens only
    bool pillarbox{true}; // paint outside the picture black
    float blink_time{};  // seconds, for things that blink (the battery when it is low)
    // A line of control hints along the bottom of the picture, outside the clean feed
    // ("Mouse: aim   Q / E: zoom"). Empty: none.
    std::string hint;
    const ViewfinderLayout *layout{}; // the camera's; none: the VX1000's
    // For the text fields beyond the frame's: {format}, {date} and {time}.
    std::string format;
    std::optional<std::tm> clock;
    int year{}; // 0: the clock's own
    StampProfile::Date date_order{StampProfile::Date::month_day_year};
};

struct Viewfinder {
    bool visible{};
    Rect picture;
    std::vector<Rect> bars;            // fill black
    std::optional<Circle> vignette;    // black outside it (inside the picture)
    std::vector<Mark> marks;           // under the labels
    std::vector<Label> labels;
    // The safe-action frame filmers kept the trick inside, faint; setup only.
    std::optional<Rect> safe_frame;
};

Viewfinder layout_viewfinder(const Frame &, float screen_width, float screen_height, const ViewfinderOptions & = {});
// A layout text's fields filled in ("{counter}" to "0:01:23").
std::string viewfinder_text(std::string_view text, const Frame &, const ViewfinderOptions & = {});
} // namespace dingosdk::camcorder
