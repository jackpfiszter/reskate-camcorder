#include "look.h"
#include "rows.h"
#include "tape.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <vector>

namespace dingosdk::camcorder {
namespace {
constexpr double two_pi = 6.283185307179586;
std::uint8_t byte(int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); }
std::size_t at(const Image &image, int x, int y) {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4;
}

// One field: the other field's lines become the average of the lines either side.
void keep_field(Image &image, int parity) {
    const int w = image.width, h = image.height;
    if (h < 2) return;
    const std::size_t stride = static_cast<std::size_t>(w) * 4;
    auto *p = image.pixels.data();
    for (int y = 1 - parity; y < h; y += 2) {
        const int above = y - 1 >= 0 ? y - 1 : y + 1, below = y + 1 < h ? y + 1 : y - 1;
        const auto *a = p + static_cast<std::size_t>(above) * stride, *b = p + static_cast<std::size_t>(below) * stride;
        auto *row = p + static_cast<std::size_t>(y) * stride;
        for (std::size_t i = 0; i < stride; ++i) row[i] = static_cast<std::uint8_t>((a[i] + b[i] + 1) / 2);
    }
}

// The camera's tone and colour, by table per channel: dense blacks, a hard early white clip, and
// the white balance's cast (red and blue gains) for this frame; then its saturation.
void tone_and_balance(Image &image, const DvLook &look, const LookProfile &profile, const DvClip &clip, double seconds) {
    float red = 1, blue = 1;
    if (look.white_balance) {
        const auto gains = white_balance(profile, clip, seconds);
        red = gains[0];
        blue = gains[1];
    }
    const float crush = std::clamp(profile.black_crush, 0.0f, 0.5f);
    const float gain = std::clamp(profile.highlight_gain, 0.5f, 2.0f);
    std::array<std::array<std::uint8_t, 256>, 3> table{}; // B, G, R
    for (int c = 0; c < 3; ++c) {
        const float balance = c == 0 ? blue : c == 2 ? red : 1.0f;
        for (int i = 0; i < 256; ++i) {
            float v = i / 255.0f * balance;
            if (look.highlights) {
                v = std::max(0.0f, (v - crush) / (1 - crush)); // dense blacks
                v *= gain;                                     // white arrives early, and stays
            }
            table[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = byte(static_cast<int>(std::lround(v * 255)));
        }
    }
    // Where blown highlights start going white, and how soon they are: bytes.
    const int knee = static_cast<int>(std::lround(std::clamp(profile.highlight_knee, 0.5f, 1.0f) * 255));
    const int spread = std::max(1, static_cast<int>(std::lround(std::clamp(profile.highlight_spread, 0.005f, 0.5f) * 255)));
    // Saturation in 1/256ths, about luma.
    const int saturation = look.highlights ? static_cast<int>(std::lround(std::clamp(profile.saturation, 0.0f, 2.0f) * 256)) : 256;
    for_rows(image.height, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            auto *p = image.pixels.data() + at(image, 0, y);
            for (int x = 0; x < image.width; ++x, p += 4) {
                for (int c = 0; c < 3; ++c) p[c] = table[static_cast<std::size_t>(c)][p[c]];
                if (saturation != 256) {
                    const int yy = (13933 * p[2] + 46871 * p[1] + 4732 * p[0]) >> 16;
                    for (int c = 0; c < 3; ++c) p[c] = byte(yy + ((p[c] - yy) * saturation >> 8));
                }
                if (!look.highlights || gain <= 1) continue;
                // A clipped channel bleeds into the others: blown highlights go white, not orange.
                const int top = std::max({p[0], p[1], p[2]});
                if (top > knee) {
                    const int pull = std::min((top - knee) * 256 / spread, 256);
                    for (int c = 0; c < 3; ++c) p[c] = byte(p[c] + ((255 - p[c]) * pull >> 8));
                }
            }
        }
    });
}

// Edge enhancement along the line (as a camera's aperture correction works): each pixel pushed away
// from the average of its neighbours `radius` pixels (of a 720-wide picture) either side, by `amount`.
void sharpen(Image &image, float amount, float radius) {
    const int w = image.width;
    const int reach = std::max(1, static_cast<int>(std::lround(std::clamp(radius, 0.25f, 8.0f) * static_cast<float>(w) / 720.0f)));
    const int strength = static_cast<int>(std::lround(std::clamp(amount, 0.0f, 2.0f) * 256));
    for_rows(image.height, [&](int first, int last) {
        std::vector<std::uint8_t> line(static_cast<std::size_t>(w) * 4);
        for (int y = first; y < last; ++y) {
            auto *row = image.pixels.data() + at(image, 0, y);
            std::copy(row, row + line.size(), line.begin());
            for (int x = 0; x < w; ++x) {
                const auto *l = line.data() + static_cast<std::size_t>(std::max(x - reach, 0)) * 4;
                const auto *r = line.data() + static_cast<std::size_t>(std::min(x + reach, w - 1)) * 4;
                const auto *m = line.data() + static_cast<std::size_t>(x) * 4;
                for (int c = 0; c < 3; ++c) {
                    const int detail = 2 * m[c] - l[c] - r[c];
                    row[x * 4 + c] = byte(m[c] + detail * strength / 512);
                }
            }
        }
    });
}

// Noise for one grain cell and frame, about -255 to 255 (two uniform draws added: a softer, more
// grain-like spread than one). `salt` picks another, independent draw (a colour channel's).
int grain_noise(std::uint32_t cx, std::uint32_t cy, std::uint32_t frame, std::uint32_t salt) {
    std::uint32_t h = cx * 0x85EBCA6Bu ^ cy * 0xC2B2AE35u ^ frame ^ salt * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return static_cast<int>(h & 0xff) + static_cast<int>((h >> 8) & 0xff) - 255;
}
// Noise, new every frame, in cells `size` pixels (of a 720-wide picture) across: the same on every
// channel (a CCD's), or with `colour`, partly each channel's own (cheap tape, film).
void add_grain(Image &image, float amount, std::int64_t index, float size, float colour) {
    const int strength = static_cast<int>(std::lround(std::clamp(amount, 0.0f, 1.0f) * 56));
    if (strength <= 0) return;
    const auto frame = static_cast<std::uint32_t>(index * 0x9E3779B9u);
    const float cell = std::max(1.0f, std::clamp(size, 0.25f, 8.0f) * static_cast<float>(image.width) / 720.0f);
    const int own = static_cast<int>(std::lround(std::clamp(colour, 0.0f, 1.0f) * 256));
    for_rows(image.height, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            auto *p = image.pixels.data() + at(image, 0, y);
            const auto cy = static_cast<std::uint32_t>(static_cast<float>(y) / cell);
            for (int x = 0; x < image.width; ++x, p += 4) {
                const auto cx = static_cast<std::uint32_t>(static_cast<float>(x) / cell);
                const int shared = grain_noise(cx, cy, frame, 0);
                for (int c = 0; c < 3; ++c) {
                    const int mixed = own ? (shared * (256 - own) + grain_noise(cx, cy, frame, static_cast<std::uint32_t>(c + 1)) * own) >> 8 : shared;
                    p[c] = byte(p[c] + mixed * strength / 255);
                }
            }
        }
    });
}

// Analogue tape's softness: a sideways blur of `radius` pixels.
void soften(Image &image, int radius) {
    if (radius < 1) return;
    const int w = image.width, span = 2 * radius + 1;
    for_rows(image.height, [&](int first, int last) {
        std::vector<std::uint8_t> line(static_cast<std::size_t>(w) * 4);
        for (int y = first; y < last; ++y) {
            auto *row = image.pixels.data() + at(image, 0, y);
            std::copy(row, row + line.size(), line.begin());
            for (int c = 0; c < 3; ++c) {
                const auto px = [&](int x) { return static_cast<int>(line[static_cast<std::size_t>(std::clamp(x, 0, w - 1)) * 4 + static_cast<std::size_t>(c)]); };
                int sum = 0;
                for (int x = -radius; x <= radius; ++x) sum += px(x);
                for (int x = 0; x < w; ++x) {
                    row[x * 4 + c] = static_cast<std::uint8_t>((sum + span / 2) / span);
                    sum += px(x + radius + 1) - px(x - radius);
                }
            }
        }
    });
}

// 4:1:1 and the like: luma stays, colour is averaged over `width` pixels of a 720-wide picture
// and smeared sideways.
void smear_chroma(Image &image, float width) {
    const int w = image.width;
    if (!(width > 1)) return;
    // `width` of a 720-wide picture, and never fewer pixels than that (odd, so it centres).
    const int span = std::max(static_cast<int>(std::lround(width)), static_cast<int>(std::lround(width * w / 720.0f))) | 1;
    const int half = span / 2;
    for_rows(image.height, [&](int first, int last) {
        std::vector<int> luma(static_cast<std::size_t>(w)), cb(luma.size()), cr(luma.size());
        for (int y = first; y < last; ++y) {
            auto *row = image.pixels.data() + at(image, 0, y);
            for (int x = 0; x < w; ++x) {
                const auto *p = row + x * 4;
                const int yy = (13933 * p[2] + 46871 * p[1] + 4732 * p[0]) >> 16; // BT.709
                luma[static_cast<std::size_t>(x)] = yy;
                cb[static_cast<std::size_t>(x)] = p[0] - yy;
                cr[static_cast<std::size_t>(x)] = p[2] - yy;
            }
            // A running box over the colour difference.
            long sum_b = 0, sum_r = 0;
            const auto clamp_x = [w](int x) { return static_cast<std::size_t>(std::clamp(x, 0, w - 1)); };
            for (int x = -half; x <= half; ++x) {
                sum_b += cb[clamp_x(x)];
                sum_r += cr[clamp_x(x)];
            }
            for (int x = 0; x < w; ++x) {
                const int b_minus_y = static_cast<int>(sum_b / span), r_minus_y = static_cast<int>(sum_r / span);
                const int yy = luma[static_cast<std::size_t>(x)];
                const int b = yy + b_minus_y, r = yy + r_minus_y;
                // Green from luma and the other two: Y = .2126 R + .7152 G + .0722 B.
                const int g = (yy * 65536 - 13933 * r - 4732 * b) / 46871;
                auto *p = row + x * 4;
                p[0] = byte(b);
                p[1] = byte(g);
                p[2] = byte(r);
                sum_b += cb[clamp_x(x + half + 1)] - cb[clamp_x(x - half)];
                sum_r += cr[clamp_x(x + half + 1)] - cr[clamp_x(x - half)];
            }
        }
    });
}

void dv_camera(Image &image, const DvLook &look, const LookProfile &profile, bool fields, const DvClip &clip,
               std::int64_t index, double frame_rate) {
    if (!look.on || image.width <= 0 || image.height <= 0) return;
    if (fields) keep_field(image, static_cast<int>(index & 1));
    if (look.grain) add_grain(image, profile.grain, index, profile.grain_size, profile.grain_colour);
    const double seconds = frame_rate > 0 ? static_cast<double>(index) / frame_rate : 0;
    if (look.highlights || look.white_balance) tone_and_balance(image, look, profile, clip, seconds);
    if (look.sharpen && profile.sharpen > 0) sharpen(image, profile.sharpen, profile.sharpen_radius);
}
void dv_tape(Image &image, const DvLook &look, const LookProfile &profile) {
    if (!look.on || !look.chroma || image.width <= 0 || image.height <= 0) return;
    soften(image, static_cast<int>(std::lround(std::clamp(profile.softness, 0.0f, 8.0f) * image.width / 720.0f)));
    smear_chroma(image, std::clamp(profile.chroma_width, 0.0f, 16.0f));
}

// 5 x 7 dots a character, each row 5 bits from the left.
struct Glyph {
    char c;
    std::array<std::uint8_t, 7> rows;
};
constexpr Glyph glyphs[] = {
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}}, {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}}, {'3', {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}}, {'5', {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}},
    {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}}, {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}}, {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}}, {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}}, {'D', {0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}}, {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}}, {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}}, {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}}, {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}}, {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
    {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}}, {':', {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}},
};
const Glyph *glyph(char c) {
    for (const auto &g : glyphs)
        if (g.c == c) return &g;
    return nullptr; // a space, or anything the camera could not show
}
void fill(Image &image, int x0, int y0, int x1, int y1, const std::array<std::uint8_t, 3> &rgb) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, image.width);
    y1 = std::min(y1, image.height);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            auto *p = image.pixels.data() + at(image, x, y);
            p[0] = rgb[2];
            p[1] = rgb[1];
            p[2] = rgb[0];
        }
}
// Each lit dot of `text`, right-aligned to `right`, its top at `top`, `dot` pixels a dot.
template <class Dot> void each_dot(const std::string &text, int right, int top, int dot, Dot &&draw) {
    const int advance = 6 * dot;
    int x = right - static_cast<int>(text.size()) * advance + dot; // no gap after the last
    for (char c : text) {
        if (const auto *g = glyph(c))
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g->rows[static_cast<std::size_t>(row)] & (0x10 >> col)) draw(x + col * dot, top + row * dot);
        x += advance;
    }
}
} // namespace

bool interlaced(Motion motion) noexcept { return motion == Motion::ntsc_60i || motion == Motion::pal_50i; }

std::string_view motion_word(Motion motion) noexcept {
    switch (motion) {
    case Motion::ntsc_60i: return "60i";
    case Motion::pal_50i: return "50i";
    case Motion::p30: return "30p";
    case Motion::p25: return "25p";
    case Motion::p24: return "24p";
    case Motion::p18: return "18p";
    }
    return "60i";
}

bool motion_from_word(std::string_view word, Motion &motion) noexcept {
    for (const auto m : {Motion::ntsc_60i, Motion::pal_50i, Motion::p30, Motion::p25, Motion::p24, Motion::p18})
        if (word == motion_word(m)) {
            motion = m;
            return true;
        }
    return false;
}

FrameRate clip_rate(const Finish &finish) noexcept {
    const int twice = finish.fields() ? 2 : 1;
    switch (finish.motion) {
    case Motion::ntsc_60i:
    case Motion::p30: return {30000 * twice, 1001};
    case Motion::pal_50i:
    case Motion::p25: return {25 * twice, 1};
    case Motion::p24: return {24000, 1001};
    case Motion::p18: return {18, 1};
    }
    return {30000, 1001};
}
double clip_frame_rate(const Finish &finish) noexcept { return clip_rate(finish).fps(); }

void apply_dv(Image &image, const DvLook &look, const LookProfile &profile, const DvClip &clip, std::int64_t index,
              double frame_rate) {
    dv_camera(image, look, profile, look.fields, clip, index, frame_rate);
    dv_tape(image, look, profile);
}

std::vector<std::string> date_stamp_lines(const std::tm &t, int year, const StampProfile &stamp) {
    static constexpr const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const int hour = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    const char *month = months[std::clamp(t.tm_mon, 0, 11)];
    const int shown_year = year > 0 ? year : t.tm_year + 1900;
    char time[32], date[32];
    std::snprintf(time, sizeof(time), "%d:%02d:%02d %s", hour, t.tm_min, t.tm_sec, t.tm_hour < 12 ? "AM" : "PM");
    if (stamp.date == StampProfile::Date::day_month_year)
        std::snprintf(date, sizeof(date), "%d %s %d", t.tm_mday, month, shown_year);
    else
        std::snprintf(date, sizeof(date), "%s %d %d", month, t.tm_mday, shown_year);
    switch (stamp.lines) {
    case StampProfile::Lines::date_only: return {date};
    case StampProfile::Lines::date_over_time: return {date, time};
    case StampProfile::Lines::time_over_date: break;
    }
    return {time, date};
}

StampMarks stamp_marks(int width, int height, const std::tm &local_time, int year, const StampProfile &stamp) {
    StampMarks marks;
    marks.colour = stamp.colour;
    marks.outline_colour = stamp.outline_colour;
    if (width < 64 || height < 48) return marks;
    const auto lines = date_stamp_lines(local_time, year, stamp);
    const int dot = std::max(1, static_cast<int>(std::lround(height / 130.0 * std::clamp(stamp.size, 0.25f, 4.0f))));
    const int edge = std::max(1, dot / 3);
    // Inside the picture's safe area, as the camera kept it.
    const int margin = static_cast<int>(static_cast<float>(width) * std::clamp(stamp.margin[0], 0.0f, 0.4f));
    const int from_edge = static_cast<int>(static_cast<float>(height) * std::clamp(stamp.margin[1], 0.0f, 0.4f));
    const bool left = stamp.corner == StampProfile::Corner::bottom_left || stamp.corner == StampProfile::Corner::top_left;
    const bool at_top = stamp.corner == StampProfile::Corner::top_left || stamp.corner == StampProfile::Corner::top_right;
    const int line_height = 9 * dot;
    const int count = static_cast<int>(lines.size());
    const int top = at_top ? from_edge : height - from_edge - count * line_height + 2 * dot;
    for (int i = 0; i < count; ++i) {
        const auto &text = lines[static_cast<std::size_t>(i)];
        const int text_width = static_cast<int>(text.size()) * 6 * dot - dot;
        const int right = left ? margin + text_width : width - margin;
        each_dot(text, right, top + i * line_height, dot, [&](int x, int y) {
            if (stamp.outline) marks.edges.push_back({x - edge, y - edge, dot + 2 * edge, dot + 2 * edge});
            marks.dots.push_back({x, y, dot, dot});
        });
    }
    return marks;
}

void burn_date(Image &image, const std::tm &local_time, int year, const StampProfile &stamp) {
    const auto marks = stamp_marks(image.width, image.height, local_time, year, stamp);
    // The dark edge first, then the dots over it.
    for (const auto &b : marks.edges) fill(image, b.x, b.y, b.x + b.width, b.y + b.height, marks.outline_colour);
    for (const auto &b : marks.dots) fill(image, b.x, b.y, b.x + b.width, b.y + b.height, marks.colour);
}

std::array<float, 2> white_balance(const LookProfile &profile, const DvClip &clip, double seconds) {
    if (!(profile.white_balance_drift > 0)) return {1, 1};
    // Two slow wanders, never in step: a cast that drifts over tens of seconds, never still.
    const double phase = clip.seed * two_pi, amount = profile.white_balance_drift;
    const double slow = std::clamp(static_cast<double>(profile.drift_seconds[0]), 1.0, 3600.0);
    const double quick = std::clamp(static_cast<double>(profile.drift_seconds[1]), 1.0, 3600.0);
    const double cast = amount * std::sin(seconds * two_pi / slow + phase) +
                        0.57 * amount * std::sin(seconds * two_pi / quick + 2.1 * phase);
    return {static_cast<float>(1 + cast), static_cast<float>(1 - cast)};
}

ScreenLook screen_look(const Finish &finish, const DvClip &clip, double seconds) {
    ScreenLook s;
    const auto &look = finish.dv;
    const auto &p = finish.look;
    if (!look.on) return s;
    s.on = true;
    if (look.highlights) {
        s.black_crush = std::clamp(p.black_crush, 0.0f, 0.5f);
        s.highlight_gain = std::clamp(p.highlight_gain, 0.5f, 2.0f);
        s.saturation = std::clamp(p.saturation, 0.0f, 2.0f);
        s.highlight_bleed = s.highlight_gain > 1;
    }
    if (look.white_balance) {
        const auto gains = white_balance(p, clip, seconds);
        s.red_gain = gains[0];
        s.blue_gain = gains[1];
    }
    s.highlight_knee = std::clamp(p.highlight_knee, 0.5f, 1.0f);
    s.highlight_spread = std::clamp(p.highlight_spread, 0.005f, 0.5f);
    s.grain_size = std::clamp(p.grain_size, 0.25f, 8.0f);
    s.grain_colour = std::clamp(p.grain_colour, 0.0f, 1.0f);
    s.sharpen_radius = std::clamp(p.sharpen_radius, 0.25f, 8.0f);
    if (look.sharpen) s.sharpen = std::clamp(p.sharpen, 0.0f, 2.0f);
    if (look.grain) s.grain = std::clamp(p.grain, 0.0f, 1.0f);
    if (look.chroma) {
        s.chroma_width = p.chroma_width > 1 ? std::clamp(p.chroma_width, 0.0f, 16.0f) : 0.0f;
        s.softness = std::clamp(p.softness, 0.0f, 8.0f);
    }
    return s;
}

double screen_frame_rate(const Finish &finish) noexcept {
    // Film-like cameras (24p, Super 8's 18) show their own cadence; video's 25 to 60 is left to the game.
    return finish.motion == Motion::p24 || finish.motion == Motion::p18 ? clip_rate(finish).fps() : 0;
}

Finish recorded_finish(Finish finish, bool look_on_screen, bool stamp_on_screen) {
    if (look_on_screen) {
        // The fields stay: they are about which moments a frame shows, so they follow the clip's own frames.
        finish.dv.highlights = finish.dv.white_balance = finish.dv.sharpen = finish.dv.chroma = finish.dv.grain = false;
    }
    if (stamp_on_screen) finish.date_stamp = false;
    return finish;
}

void finish_frame(Image &image, const Finish &finish, const DvClip &clip, std::int64_t index, double frame_rate,
                  const std::tm &local_time) {
    dv_camera(image, finish.dv, finish.look, finish.fields(), clip, index, frame_rate);
    if (finish.date_stamp) burn_date(image, local_time, finish.stamp_year, finish.stamp);
    dv_tape(image, finish.dv, finish.look);
}
std::optional<std::int64_t> clock_offset(std::string_view date, std::string_view time, std::time_t now) {
    // Numbers split by `separator`, exactly `count` of them and nothing else.
    const auto numbers = [](std::string_view text, char separator, int *out, int count) {
        for (int i = 0; i < count; ++i) {
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out[i]);
            if (error != std::errc{} || end == text.data()) return false;
            text.remove_prefix(static_cast<std::size_t>(end - text.data()));
            if (i + 1 < count) {
                if (text.empty() || text.front() != separator) return false;
                text.remove_prefix(1);
            }
        }
        return text.empty();
    };
    int ymd[3]{}, hm[2]{};
    if (!numbers(date, '-', ymd, 3)) return std::nullopt;
    const int year = ymd[0], month = ymd[1], day = ymd[2];
    if (year < 1971 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) return std::nullopt;
    if (!time.empty() && (!numbers(time, ':', hm, 2) || hm[0] < 0 || hm[0] > 23 || hm[1] < 0 || hm[1] > 59)) return std::nullopt;
    const int hour = hm[0], minute = hm[1];
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    if (!time.empty()) {
        local.tm_hour = hour;
        local.tm_min = minute;
        local.tm_sec = 0;
    }
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_isdst = -1;
    const std::time_t set = std::mktime(&local);
    if (set == static_cast<std::time_t>(-1) || local.tm_mday != day) return std::nullopt; // 31 February
    return static_cast<std::int64_t>(set) - static_cast<std::int64_t>(now);
}
} // namespace dingosdk::camcorder
