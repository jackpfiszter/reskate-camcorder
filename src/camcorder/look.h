#pragma once
// What tape did to the VX1000's picture, for the recorded clips and stills: the DV look and the
// date stamp. The game's picture is clean and sharp; a 90s part is not.
//
// - Fields: NTSC video is 60 fields a second, each every other line, so motion is smooth and
//   detail is half height. A clip with fields on runs at 59.94 frames a second, each frame one
//   field with the lines between filled in, as a player shows interlaced video.
// - Highlights: a 1/3" CCD clips white hard and early (sky, lights), and its blacks are dense.
// - White balance: the auto white balance never quite settles; the colour drifts warm and cool.
// - Sharpening: the camera's edge enhancement, a bright and dark halo along hard edges.
// - Chroma: DV keeps colour at a quarter of the width (4:1:1), so colour smears sideways.
// - The date stamp: the camcorder's own character generator, recorded into the picture.
//
// Order, as in the camera: fields (the sensor), highlights, white balance and sharpening (the
// camera's processing), the date stamp (its character generator), chroma (the tape).
#include "recorder.h"
#include <array>
#include <ctime>
#include <string>

namespace dingosdk::camcorder {
struct DvLook {
    bool on{};             // the whole look; the parts below only count while it is on
    bool fields{true};
    bool highlights{true};
    bool white_balance{true};
    bool sharpen{true};
    bool chroma{true};
    bool operator==(const DvLook &) const = default;
};
// Everything done to a recorded picture after the lens.
struct Finish {
    DvLook dv;
    bool date_stamp{};
    int stamp_year{}; // 0: the real year; otherwise the year the stamp says (1996, say)
    bool operator==(const Finish &) const = default;
    // A clip runs at 59.94 frames a second when its frames are fields.
    bool fields() const { return dv.on && dv.fields; }
};
// Frames a second of a clip with this finish: 30000/1001, or 60000/1001 with fields.
double clip_frame_rate(const Finish &) noexcept;

// What changes from frame to frame in one clip: the white balance's wander.
struct DvClip {
    float seed{}; // where in its wander the white balance starts, 0 to 1 (a clip's own)
};

// The DV look on frame `index` of a clip running at `frame_rate`. Nothing when `look.on` is false.
void apply_dv(Image &image, const DvLook &look, const DvClip &clip, std::int64_t index, double frame_rate);
// The date stamp's two lines, "4:03:22 PM" over "OCT 12 1996", as the VX1000 showed them.
// `year` 0 keeps the real year.
std::array<std::string, 2> date_stamp_lines(const std::tm &local_time, int year);
// Draws them into the bottom right of the picture: white, blocky, with a dark edge.
void burn_date(Image &image, const std::tm &local_time, int year);
// The DV look's parts before the stamp, the stamp, then the chroma: one finished frame.
void finish_frame(Image &image, const Finish &finish, const DvClip &clip, std::int64_t index, double frame_rate,
                  const std::tm &local_time);
} // namespace dingosdk::camcorder
