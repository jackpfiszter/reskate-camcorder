#pragma once
// The Sony DCR-VX1000 (1995), the camcorder of 90s skate video, as numbers the game camera can use.
//
// From Sony's spec sheet: three 1/3" CCDs (410,000 pixels each), a 10:1 power zoom of
// f = 5.9-59 mm at F1.6 whose servo runs end to end in 4.1 s (fastest) to 20 s (slowest), a colour
// LCD viewfinder, NTSC MiniDV (720x480, 4:3, 29.97 frames a second, interlaced).
//
// The lens alone is not wide: about 44 degrees across at its widest, so skaters screwed the
// Century Optics 0.3x fisheye ("MK1", "the death lens") onto its bayonet. Century gives the
// adapter about 125 degrees across. At full wide its housing shows in the frame as round black
// corners; a touch of zoom clears them. Both are modelled here.
//
// A 1/3" sensor's active area is 4.8 x 3.6 mm (4:3). Angles below are of the 4:3 picture the
// camcorder records.
#include "camera_math.h"
#include <cstdint>
#include <string_view>

namespace dingosdk::camcorder {
struct LensSpec {
    float sensor_width_mm = 4.8f, sensor_height_mm = 3.6f;
    float wide_mm = 5.9f, tele_mm = 59.0f;
    float max_aperture = 1.6f;
    float fastest_zoom_s = 4.1f, slowest_zoom_s = 20.0f; // servo, wide to tele
    float aspect() const { return sensor_width_mm / sensor_height_mm; }
};
inline constexpr LensSpec vx1000_lens{};

enum class Adapter : std::uint8_t {
    none,            // the stock 5.9-59 mm lens
    century_fisheye, // Century Optics 0.3x Ultra Fisheye (MK1)
};
struct AdapterSpec {
    std::string_view name;
    bool fisheye{};
    // Degrees across the 4:3 picture at full wide (fisheye only; a plain adapter scales focal length).
    float horizontal_fov_deg{};
    // The housing's round shadow, as a radius in half picture widths (the picture's corners are at
    // 1.25), at full wide, and how far it opens with each 1x of zoom. 0 means no shadow.
    float vignette_radius{}, vignette_growth{};
};
AdapterSpec adapter_spec(Adapter) noexcept;
std::string_view adapter_name(Adapter) noexcept;

// How the game camera shows a fisheye. The game renders a flat (rectilinear) picture.
enum class FisheyeRender : std::uint8_t {
    // No post-processing: the flat camera gets about the fisheye's coverage (the overlay still
    // draws the housing's shadow). Works with nothing but a camera FOV.
    approximate,
    // A full-screen warp bends the flat picture into the fisheye's (fisheye_source below). The
    // game camera is opened wide enough that the warp always has picture to sample.
    warp,
};

// What the lens does right now.
struct Optics {
    float zoom_ratio{1};      // 1 at wide, 10 at tele
    float focal_mm{5.9f};     // of the camcorder's own lens
    float horizontal_fov{};   // degrees across the 4:3 picture the viewer sees
    float vertical_fov{};     // degrees up and down it
    bool fisheye{};
    float vignette_radius{};  // half picture widths; 0 when the housing's shadow is outside the picture
    float render_vertical_fov{}; // degrees: what to give the game's (flat, vertical-FOV) camera
};

// Zoom position 0 (wide) to 1 (tele) as a focal length. The servo moves the zoom ring at a steady
// rate and the picture magnifies geometrically, so equal steps are equal ratios.
float focal_length(float zoom, const LensSpec & = vx1000_lens) noexcept;
// Degrees across `extent_mm` of sensor through a flat lens of `focal_mm`.
float rectilinear_fov(float extent_mm, float focal_mm) noexcept;
// `render_aspect` is the game's back buffer's width over height (16:9 is 1.778): with the warp, the
// flat camera must cover the 4:3 picture cut from its middle.
Optics optics(Adapter, float zoom, FisheyeRender = FisheyeRender::approximate,
              float render_aspect = 16.0f / 9.0f, const LensSpec & = vx1000_lens) noexcept;

// For the warp: a point of the recorded picture, x and y in half picture widths from its centre
// (x right in [-1, 1], y up in [-0.75, 0.75]), to where it is in the flat render, as tangents
// (screen x = tan / tan(half horizontal render FOV)). The fisheye is equidistant: the angle
// off-axis grows in step with the distance from the centre.
struct Tangent {
    float x{}, y{};
};
Tangent fisheye_source(const Optics &, float x, float y) noexcept;

// The zoom rocker and the servo behind it. A light press creeps (20 s end to end), a full press
// races (4.1 s), and the motor takes a moment to get going and to stop.
struct ZoomServo {
    float position{};  // 0 wide, 1 tele
    float speed{};     // positions a second
    float shown_for{}; // seconds the viewfinder's zoom bar has left to show
    // `rocker` from -1 (wide) to 1 (tele).
    void step(float rocker, float dt, const LensSpec & = vx1000_lens) noexcept;
};
} // namespace dingosdk::camcorder
