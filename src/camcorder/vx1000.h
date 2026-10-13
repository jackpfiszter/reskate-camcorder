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
// A 1/3" sensor's active area is 4.8 x 3.6 mm (4:3). Angles below are of the picture the camcorder
// records, whose shape is its sensor's (a camera profile can give any: 16:9, Super 8's 1.44, square).
#include "camera_math.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace dingosdk::camcorder {
struct LensSpec {
    float sensor_width_mm = 4.8f, sensor_height_mm = 3.6f;
    float wide_mm = 5.9f, tele_mm = 59.0f;
    float max_aperture = 1.6f;
    float fastest_zoom_s = 4.1f, slowest_zoom_s = 20.0f; // servo, wide to tele
    // The picture's width over its height: the sensor's.
    float aspect() const { return sensor_width_mm > 0 && sensor_height_mm > 0 ? sensor_width_mm / sensor_height_mm : 4.0f / 3.0f; }
    bool operator==(const LensSpec &) const = default;
};
inline constexpr LensSpec vx1000_lens{};

// How a fisheye bends: the angle off the lens's axis against the distance from the picture's
// centre. Equidistant in step (the Century's); equisolid squeezes the edges a little more (most
// modern fisheyes); stereographic keeps shapes rounder toward the edges; orthographic squeezes the
// edges hardest (a "bubble").
enum class Projection : std::uint8_t { equidistant, equisolid, stereographic, orthographic };

// A lens screwed onto the front of the camera's own: a fisheye, or a flat wide or tele converter.
// The built-in one is the Century below; mods add more (profiles.h). No adapter is the stock lens.
struct Adapter {
    std::string id;   // short, for commands and the settings file: "century"
    std::string name; // "Century Optics 0.3x fisheye"
    bool fisheye{};
    // A fisheye's degrees across the picture with the camera's lens at its widest.
    float fisheye_fov{};
    Projection projection{Projection::equidistant};
    // A flat converter's effect on focal length: 0.5 for a half-wide, 1.5 for a tele converter.
    float magnification{1};
    // The housing's round shadow, as a radius in half picture widths (a 4:3 picture's corners are at
    // 1.25, a 16:9's at 1.15), at full wide, and how far it opens with each 1x of zoom. 0 means no shadow.
    float vignette_radius{}, vignette_growth{};
    std::string source; // the mod folder it came from (profiles.h); empty when built in
    bool operator==(const Adapter &) const = default;
};
// Century Optics 0.3x Ultra Fisheye (MK1), "the death lens": 125 degrees is Century's figure; the
// shadow's size is set by eye from MK1 footage (round black corners at full wide, gone by about 1.5x).
const Adapter &century_fisheye();

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
    float aspect{4.0f / 3.0f}; // the picture's width over its height (the sensor's)
    float horizontal_fov{};   // degrees across the picture the viewer sees
    float vertical_fov{};     // degrees up and down it
    bool fisheye{};
    Projection projection{Projection::equidistant}; // a fisheye's
    float vignette_radius{};  // half picture widths; 0 when the housing's shadow is outside the picture
    float render_vertical_fov{}; // degrees: what to give the game's (flat, vertical-FOV) camera
    bool warped{};            // the flat render is opened for the warp (FisheyeRender::warp)
};

// Zoom position 0 (wide) to 1 (tele) as a focal length. The servo moves the zoom ring at a steady
// rate and the picture magnifies geometrically, so equal steps are equal ratios.
float focal_length(float zoom, const LensSpec & = vx1000_lens) noexcept;
// Degrees across `extent_mm` of sensor through a flat lens of `focal_mm`.
float rectilinear_fov(float extent_mm, float focal_mm) noexcept;
// `render_aspect` is the game's back buffer's width over height (16:9 is 1.778): with the warp, the
// flat camera must cover the picture (the lens's sensor's shape) cut from its middle.
// `adapter` null: the camera's own lens alone.
Optics optics(const Adapter *adapter, float zoom, FisheyeRender = FisheyeRender::approximate,
              float render_aspect = 16.0f / 9.0f, const LensSpec & = vx1000_lens) noexcept;

// For the warp: a point of the recorded picture, x and y in half picture widths from its centre
// (x right in [-1, 1], y up in [-1 / aspect, 1 / aspect]), to where it is in the flat render, as tangents
// (screen x = tan / tan(half horizontal render FOV)). How the angle off-axis grows with the
// distance from the centre is the fisheye's projection.
struct Tangent {
    float x{}, y{};
};
Tangent fisheye_source(const Optics &, float x, float y) noexcept;
// The angle off-axis (radians) at `rho` half picture widths from the centre, for a fisheye `half`
// radians across half the picture's width.
float fisheye_angle(Projection, float rho, float half) noexcept;
// The same point as a texture coordinate in the whole flat render (u right, v down, 0 to 1 across the
// back buffer), which is `render_aspect` wide for 1 tall and `optics.render_vertical_fov` tall.
struct TextureUv {
    float u{}, v{};
};
TextureUv warp_uv(const Optics &, float x, float y, float render_aspect) noexcept;

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
