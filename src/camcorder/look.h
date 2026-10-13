#pragma once
// What tape did to the camera's picture, for the recorded clips and stills: the tape look and the
// date stamp. The game's picture is clean and sharp; a 90s part is not. How much of each a camera
// gives is its LookProfile (a VX1000's MiniDV by default; a mod's camera brings its own,
// profiles.h); which parts are applied is the player's DvLook.
//
// - Fields: interlaced video is 60 (or 50) fields a second, each every other line, so motion is
//   smooth and detail is half height. With fields on, an interlaced camera's clip runs at 59.94
//   (or 50) frames a second, each frame one field with the lines between filled in, as a player
//   shows interlaced video.
// - Highlights: a small CCD clips white hard and early, and its blacks are dense. Saturation too.
// - White balance: the auto white balance never quite settles; the colour drifts warm and cool.
// - Sharpening: the camera's edge enhancement, a bright and dark halo along hard edges.
// - Grain: sensor noise, or film grain.
// - Chroma: tape keeps colour at a fraction of the width (DV's 4:1:1), so colour smears
//   sideways; and analogue tape is soft (blur).
// - The date stamp: the camcorder's own character generator, recorded into the picture.
//
// Order, as in the camera: fields (the sensor), grain, tone and white balance and sharpening (the
// camera's processing), the date stamp (its character generator), then softness and chroma (the tape).
#include "audio.h"
#include "recorder.h"
#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::camcorder {
// How a camera records motion.
enum class Motion : std::uint8_t {
    ntsc_60i, // NTSC video: 29.97 frames of two fields (59.94 fields a second)
    pal_50i,  // PAL video: 25 frames of two fields
    p30,      // 29.97 whole frames
    p25,      // 25 whole frames
    p24,      // 23.976 whole frames (film-like video)
    p18,      // 18 frames: Super 8
};
bool interlaced(Motion) noexcept;
// "60i", "50i", "30p", "25p", "24p", "18p": the words profiles use.
std::string_view motion_word(Motion) noexcept;
bool motion_from_word(std::string_view, Motion &) noexcept;

// Frames a second as a fraction (Media Foundation and MP4 keep it that way).
struct FrameRate {
    int numerator{30000}, denominator{1001};
    double fps() const { return static_cast<double>(numerator) / denominator; }
    bool operator==(const FrameRate &) const = default;
};

// How strongly a camera and its tape do each part of the look (profiles.h reads these).
struct LookProfile {
    std::string format{"MiniDV"};   // what the menu calls the look ("MiniDV look")
    float black_crush{0.05f};        // the share of the darkest range pushed to black
    float highlight_gain{1.15f};     // >1: white arrives early (1.15 clips the top 13%)
    float saturation{1};             // 1 as the game draws it; Hi8 and VHS are lower
    float white_balance_drift{0.035f}; // how far red and blue wander, as a gain (0.035 is 3.5%)
    float sharpen{0.6f};             // edge enhancement, 0 none
    float grain{0.04f};              // noise, 0 none, 1 heavy (a CCD in shade: a little)
    float chroma_width{4};           // colour averaged over this many pixels of a 720-wide picture
    float softness{0};               // blur, in pixels of a 720-wide picture (VHS about 1.5)
    // How each of those behaves (the amounts above say how strong):
    float highlight_knee{0.925f};    // where a clipped channel starts to bleed into the others (of white)
    float highlight_spread{0.075f};  // how far above the knee it is fully white
    float grain_size{1};             // the grain's size, in pixels of a 720-wide picture (film: 2 or 3)
    float grain_colour{0};           // 0 the same on every channel (a CCD's), 1 colour noise (cheap tape, film)
    float sharpen_radius{1};         // the halo's reach, in pixels of a 720-wide picture
    std::array<float, 2> drift_seconds{37, 11}; // the white balance's two wanders: a slow one and a quicker one
    bool operator==(const LookProfile &) const = default;
};

// How a camera's date stamp looks.
struct StampProfile {
    enum class Corner : std::uint8_t { bottom_right, bottom_left, top_right, top_left };
    enum class Lines : std::uint8_t { time_over_date, date_only, date_over_time };
    enum class Date : std::uint8_t { month_day_year, day_month_year };
    Corner corner{Corner::bottom_right};
    Lines lines{Lines::time_over_date};
    Date date{Date::month_day_year};
    std::array<std::uint8_t, 3> colour{235, 235, 235}; // R, G, B
    float size{1};                                       // 1 as a VX1000's: a dot is 1/130 of the picture's height
    std::array<float, 2> margin{0.07f, 0.08f};           // from the picture's side and its top or bottom, in fractions of it
    bool outline{true};                                  // a dark edge round each dot
    std::array<std::uint8_t, 3> outline_colour{16, 16, 16};
    bool operator==(const StampProfile &) const = default;
};

// A lamp on the camera (`camcorder light`): what the picture's middle is lit by.
struct LightProfile {
    float strength{2.5f};                                // how much it brightens the middle (light times 1 + this)
    float reach{0.8f};                                   // the beam's width, in half picture widths
    std::array<std::uint8_t, 3> colour{255, 245, 224};   // a halogen lamp on a camera balanced for daylight
    bool operator==(const LightProfile &) const = default;
};

// The player's switches for the look.
struct DvLook {
    bool on{};             // the whole look; the parts below only count while it is on
    bool fields{true};
    bool highlights{true}; // tone: highlights, blacks, saturation
    bool white_balance{true};
    bool sharpen{true};
    bool chroma{true};     // the tape: colour smear and softness
    bool grain{true};
    bool operator==(const DvLook &) const = default;
};

// Everything done to a recorded picture after the lens.
struct Finish {
    DvLook dv;
    bool date_stamp{};
    int stamp_year{}; // 0: the real year; otherwise the year the stamp says (1996, say)
    std::int64_t stamp_offset{}; // seconds the camera's clock runs ahead of the real one (set with `date set`)
    // The camera's: how it records motion, its look, its stamp and its mic (profiles.h).
    Motion motion{Motion::ntsc_60i};
    LookProfile look;
    StampProfile stamp;
    MicProfile mic;
    LightProfile light;
    EarSettings ear; // how far sound carries to the camera (the handling's)
    bool operator==(const Finish &) const = default;
    // A clip's frames are fields: the look's fields on an interlaced camera.
    bool fields() const { return dv.on && dv.fields && interlaced(motion); }
};
// Frames a second of a clip with this finish (59.94 for a VX1000's fields, 29.97 without).
FrameRate clip_rate(const Finish &) noexcept;
double clip_frame_rate(const Finish &) noexcept;

// What changes from frame to frame in one clip: the white balance's wander.
struct DvClip {
    float seed{}; // where in its wander the white balance starts, 0 to 1 (a clip's own)
};

// The look on frame `index` of a clip running at `frame_rate`. Nothing when `look.on` is false.
void apply_dv(Image &image, const DvLook &look, const LookProfile &profile, const DvClip &clip, std::int64_t index,
              double frame_rate);
// The date stamp's lines, top first: "4:03:22 PM" over "OCT 12 1996" for a VX1000. `year` 0 keeps
// the real year.
std::vector<std::string> date_stamp_lines(const std::tm &local_time, int year, const StampProfile & = {});
// Draws them into a bottom corner of the picture: blocky, with a dark edge.
void burn_date(Image &image, const std::tm &local_time, int year, const StampProfile & = {});
// The date stamp as rectangles in a `width` x `height` picture: the dark edges, then the dots over
// them. burn_date draws these; the overlay draws the same on screen.
struct StampMarks {
    std::vector<PixelBox> edges, dots;
    std::array<std::uint8_t, 3> colour{}, outline_colour{};
};
StampMarks stamp_marks(int width, int height, const std::tm &local_time, int year, const StampProfile & = {});

// The camera's clock at real time `now`: the real time, or what the player set it to, running on.
// (An offset beyond 200 years either way, a hand-edited settings file's, counts as none.)
inline std::time_t stamp_clock(std::time_t now, const Finish &finish) {
    constexpr std::int64_t most = 200LL * 366 * 24 * 3600;
    return finish.stamp_offset > most || finish.stamp_offset < -most ? now : now + static_cast<std::time_t>(finish.stamp_offset);
}
// Setting the camera's clock to `date` (YYYY-MM-DD) and `time` (HH:MM; empty keeps the time of day):
// the offset from the real clock at `now`, in local time. Nothing when the date or time is not one.
std::optional<std::int64_t> clock_offset(std::string_view date, std::string_view time, std::time_t now);

// The auto white balance's red and blue gains at `seconds` into a clip.
std::array<float, 2> white_balance(const LookProfile &, const DvClip &, double seconds);

// The look as the overlay's shader does it on screen (and so in clips and stills): the numbers
// for one frame, with the player's switched-off parts at their neutral values. Fields are left to
// the recording (recorded_finish).
struct ScreenLook {
    bool on{};
    float black_crush{}, highlight_gain{1}, saturation{1};
    bool highlight_bleed{};
    float red_gain{1}, blue_gain{1};
    float sharpen{}, grain{};
    float chroma_width{}, softness{}; // in pixels of a 720-wide picture
    float highlight_knee{0.925f}, highlight_spread{0.075f};
    float grain_size{1}, grain_colour{}, sharpen_radius{1}; // sizes in pixels of a 720-wide picture
};
ScreenLook screen_look(const Finish &, const DvClip &, double seconds);
// How often the screen shows a new picture for a film-like camera (24p, Super 8's 18), so it moves
// as it records; 0 for video, which shows every frame the game draws.
double screen_frame_rate(const Finish &) noexcept;
// What is left to do to a recorded frame when the screen already had the look and the stamp.
Finish recorded_finish(Finish finish, bool look_on_screen, bool stamp_on_screen);

// The look's camera parts, the stamp, then the tape: one finished frame.
void finish_frame(Image &image, const Finish &finish, const DvClip &clip, std::int64_t index, double frame_rate,
                  const std::tm &local_time);
} // namespace dingosdk::camcorder
