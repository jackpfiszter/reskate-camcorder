#include "look.h"
#include "rows.h"
#include "tape.h"
#include <algorithm>
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
// the white balance's cast (red and blue gains) for this frame.
void tone_and_balance(Image &image, const DvLook &look, const DvClip &clip, double seconds) {
    float red = 1, blue = 1;
    if (look.white_balance) {
        // Two slow wanders, never in step: a cast that drifts over tens of seconds, never still.
        const double phase = clip.seed * two_pi;
        const double cast = 0.035 * std::sin(seconds * two_pi / 37.0 + phase) + 0.02 * std::sin(seconds * two_pi / 11.0 + 2.1 * phase);
        red = static_cast<float>(1 + cast);
        blue = static_cast<float>(1 - cast);
    }
    std::array<std::array<std::uint8_t, 256>, 3> table{}; // B, G, R
    for (int c = 0; c < 3; ++c) {
        const float gain = c == 0 ? blue : c == 2 ? red : 1.0f;
        for (int i = 0; i < 256; ++i) {
            float v = i / 255.0f * gain;
            if (look.highlights) {
                v = std::max(0.0f, (v - 0.035f) / 0.965f); // dense blacks
                v *= 1.1f;                                 // white arrives early, and stays
            }
            table[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = byte(static_cast<int>(std::lround(v * 255)));
        }
    }
    for_rows(image.height, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            auto *p = image.pixels.data() + at(image, 0, y);
            for (int x = 0; x < image.width; ++x, p += 4) {
                for (int c = 0; c < 3; ++c) p[c] = table[static_cast<std::size_t>(c)][p[c]];
                if (!look.highlights) continue;
                // A clipped channel bleeds into the others: blown highlights go white, not orange.
                const int top = std::max({p[0], p[1], p[2]});
                if (top > 236) {
                    const int pull = (top - 236) * 256 / 19;
                    for (int c = 0; c < 3; ++c) p[c] = byte(p[c] + ((255 - p[c]) * pull >> 8));
                }
            }
        }
    });
}

// Edge enhancement along the line (as a camera's aperture correction works): each pixel pushed away
// from the average of its neighbours `reach` pixels either side.
void sharpen(Image &image) {
    const int w = image.width;
    const int reach = std::max(1, w / 640);
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
                    row[x * 4 + c] = byte(m[c] + detail * 7 / 16);
                }
            }
        }
    });
}

// 4:1:1: luma stays, colour is averaged over four DV pixels' width and smeared sideways.
void smear_chroma(Image &image) {
    const int w = image.width;
    const int span = std::max(4, (w * 4 + 360) / 720) | 1; // 4 DV pixels of 720, odd so it centres
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

void dv_camera(Image &image, const DvLook &look, const DvClip &clip, std::int64_t index, double frame_rate) {
    if (!look.on || image.width <= 0 || image.height <= 0) return;
    if (look.fields) keep_field(image, static_cast<int>(index & 1));
    const double seconds = frame_rate > 0 ? static_cast<double>(index) / frame_rate : 0;
    if (look.highlights || look.white_balance) tone_and_balance(image, look, clip, seconds);
    if (look.sharpen) sharpen(image);
}
void dv_tape(Image &image, const DvLook &look) {
    if (look.on && look.chroma && image.width > 0 && image.height > 0) smear_chroma(image);
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
void fill(Image &image, int x0, int y0, int x1, int y1, std::uint8_t value) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, image.width);
    y1 = std::min(y1, image.height);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            auto *p = image.pixels.data() + at(image, x, y);
            p[0] = p[1] = p[2] = value;
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

double clip_frame_rate(const Finish &finish) noexcept { return finish.fields() ? 2 * ntsc_frame_rate : ntsc_frame_rate; }

void apply_dv(Image &image, const DvLook &look, const DvClip &clip, std::int64_t index, double frame_rate) {
    dv_camera(image, look, clip, index, frame_rate);
    dv_tape(image, look);
}

std::array<std::string, 2> date_stamp_lines(const std::tm &t, int year) {
    static constexpr const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const int hour = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    char time[32], date[32];
    std::snprintf(time, sizeof(time), "%d:%02d:%02d %s", hour, t.tm_min, t.tm_sec, t.tm_hour < 12 ? "AM" : "PM");
    std::snprintf(date, sizeof(date), "%s %d %d", months[std::clamp(t.tm_mon, 0, 11)], t.tm_mday,
                  year > 0 ? year : t.tm_year + 1900);
    return {time, date};
}

void burn_date(Image &image, const std::tm &local_time, int year) {
    if (image.width < 64 || image.height < 48) return;
    const auto lines = date_stamp_lines(local_time, year);
    const int dot = std::max(1, static_cast<int>(std::lround(image.height / 130.0)));
    const int edge = std::max(1, dot / 3);
    // Inside the picture's safe area, as the camera kept it.
    const int right = image.width - static_cast<int>(image.width * 0.07f);
    const int bottom = image.height - static_cast<int>(image.height * 0.08f);
    const int line_height = 9 * dot;
    const int top = bottom - 2 * line_height + 2 * dot;
    // The dark edge first, then the white dots over it.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < 2; ++i)
            each_dot(lines[static_cast<std::size_t>(i)], right, top + i * line_height, dot, [&](int x, int y) {
                if (pass == 0) fill(image, x - edge, y - edge, x + dot + edge, y + dot + edge, 16);
                else fill(image, x, y, x + dot, y + dot, 235);
            });
}

void finish_frame(Image &image, const Finish &finish, const DvClip &clip, std::int64_t index, double frame_rate,
                  const std::tm &local_time) {
    dv_camera(image, finish.dv, clip, index, frame_rate);
    if (finish.date_stamp) burn_date(image, local_time, finish.stamp_year);
    dv_tape(image, finish.dv);
}
} // namespace dingosdk::camcorder
