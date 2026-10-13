#pragma once
// What the screen shows over the shot, laid out in pixels for the overlay to draw (ReSkate draws
// its overlay with Dear ImGui). The overlay is not on the picture a recording keeps unless
// the player captures the screen, so a clean feed keeps the frame and the lens and drops the
// readouts.
//
// - The picture, the camera's shape (4:3 for the VX1000): the game renders the full window; the bars either side (or above and
//   below) are painted black.
// - The fisheye housing's shadow: black outside a circle, while the zoom is wide enough to see it.
// - The readouts, after the VX1000's viewfinder: STBY or a red REC, the tape counter, the minutes
//   of tape left, the battery, and the W-T zoom bar while the zoom moves. Their places are in the
//   spirit of the original, not traced from it.
#include "camcorder.h"
#include <string>
#include <vector>

namespace dingosdk::camcorder {
struct Rect {
    float x{}, y{}, width{}, height{};
};
// The largest `aspect` rectangle centred in the screen.
Rect picture_rect(float screen_width, float screen_height, float aspect = 4.0f / 3.0f) noexcept;

enum class Ink : std::uint8_t { white, red, amber };
enum class Align : std::uint8_t { left, right, centre };
struct Label {
    std::string text;
    float x{}, y{};   // top-left (Align::left), top-right (Align::right) or top-centre (Align::centre), in pixels
    float size{};     // text height, in pixels
    Ink ink{Ink::white};
    Align align{Align::left};
};
struct Circle {
    float x{}, y{}, radius{};
};
struct ZoomBar {
    bool visible{};
    Rect track;       // the bar between W and T
    float position{}; // 0 at W, 1 at T
};
struct Battery {
    bool visible{};
    Rect body;
    float level{}; // 0 to 1
};

struct ViewfinderOptions {
    bool clean_feed{};   // the frame and the lens only
    bool pillarbox{true}; // paint outside the picture black
    float blink_time{};  // seconds, for things that blink (the battery when it is low)
    // A line of control hints along the bottom of the picture, outside the clean feed
    // ("Mouse: aim   Q / E: zoom"). Empty: none.
    std::string hint;
};

struct Viewfinder {
    bool visible{};
    Rect picture;
    std::vector<Rect> bars;            // fill black
    std::optional<Circle> vignette;    // black outside it (inside the picture)
    bool rec_dot{};                    // the red dot by REC
    float rec_dot_radius{};
    float rec_dot_x{}, rec_dot_y{};
    std::vector<Label> labels;
    ZoomBar zoom;
    Battery battery;
    // The safe-action frame filmers kept the trick inside, faint; setup only.
    std::optional<Rect> safe_frame;
};

Viewfinder layout_viewfinder(const Frame &, float screen_width, float screen_height, const ViewfinderOptions & = {});
} // namespace dingosdk::camcorder
