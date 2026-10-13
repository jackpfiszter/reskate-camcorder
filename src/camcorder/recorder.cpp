#include "recorder.h"
#include "rows.h"
#include "tape.h"
#include "viewfinder.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dingosdk::camcorder {
namespace {
int even_down(float v) { return std::max(0, static_cast<int>(std::floor(v * 0.5f)) * 2); }

// A display's white is 1; HDR highlights above it roll off into it instead of clipping hard.
double tone(double x) {
    if (!(x > 0)) return 0;
    constexpr double knee = 0.8;
    if (x <= knee) return x;
    return knee + (1 - knee) * (1 - std::exp(-(x - knee) / (1 - knee)));
}
// Linear light (white 1, HDR up to `encode_range`) to a toned sRGB byte, by table: a pow and an exp
// per channel per pixel are far too slow at 30 frames a second.
constexpr float encode_range = 8.0f;
constexpr int encode_steps = 16384;
const std::array<std::uint8_t, encode_steps + 1> &encode_table() {
    static const auto table = [] {
        std::array<std::uint8_t, encode_steps + 1> t{};
        for (int i = 0; i <= encode_steps; ++i) {
            const double x = std::clamp(tone(static_cast<double>(i) * encode_range / encode_steps), 0.0, 1.0);
            const double s = x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - 0.055;
            t[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::clamp(std::lround(s * 255), 0L, 255L));
        }
        return t;
    }();
    return table;
}
std::uint8_t encode(float linear) {
    if (!(linear > 0)) return 0;
    const float at = std::min(linear * (encode_steps / encode_range), static_cast<float>(encode_steps));
    return encode_table()[static_cast<std::size_t>(at + 0.5f)];
}
// PQ (SMPTE ST 2084) code value to nits, for every 10-bit code.
const std::array<float, 1024> &pq_table() {
    static const auto table = [] {
        std::array<float, 1024> t{};
        constexpr double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
        for (int i = 0; i < 1024; ++i) {
            const double p = std::pow(i / 1023.0, 1 / m2);
            t[static_cast<std::size_t>(i)] = static_cast<float>(
                10000 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1 / m1));
        }
        return t;
    }();
    return table;
}
float half_to_float(std::uint16_t h) {
    const std::uint32_t sign = (h & 0x8000u) << 16, exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ffu;
    std::uint32_t bits{};
    if (exponent == 0) {
        if (mantissa == 0) bits = sign;
        else { // subnormal
            float v = std::ldexp(static_cast<float>(mantissa), -24);
            return sign ? -v : v;
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}
void store(std::uint8_t *to, float r, float g, float b) {
    to[0] = encode(b);
    to[1] = encode(g);
    to[2] = encode(r);
    to[3] = 255;
}
} // namespace

PixelBox capture_box(int width, int height, float aspect) noexcept {
    if (width < 2 || height < 2) return {};
    const auto r = picture_rect(static_cast<float>(width), static_cast<float>(height), aspect);
    PixelBox box;
    box.width = std::min(even_down(r.width), width & ~1);
    box.height = std::min(even_down(r.height), height & ~1);
    box.x = (width - box.width) / 2;
    box.y = (height - box.height) / 2;
    return box;
}

VideoSize video_size(Quality quality, const PixelBox &capture, int own_lines) noexcept {
    if (capture.width < 2 || capture.height < 2) return {2, 2};
    const double aspect = static_cast<double>(capture.width) / capture.height;
    int tallest = quality == Quality::dv ? std::clamp(own_lines, 120, 1080) : 1080;
    if (quality == Quality::hd) tallest = std::min(tallest, static_cast<int>(1920 / aspect));
    const int height = std::max(2, std::min(tallest, capture.height) & ~1);
    const int width = 2 * static_cast<int>(std::lround(height * aspect * 0.5)); // the nearest even
    return {std::clamp(width, 2, capture.width & ~1), height};
}

std::uint32_t video_bitrate(const VideoSize &size, double frame_rate) noexcept {
    // About a quarter of a bit a pixel a frame: plenty for a fisheye line full of concrete texture.
    const double bits = static_cast<double>(size.width) * size.height * (frame_rate > 0 ? frame_rate : ntsc_frame_rate) * 0.25;
    return static_cast<std::uint32_t>(std::clamp(bits, 1.0e6, 40.0e6));
}

void convert_to_bgra(const std::uint8_t *source, std::size_t pitch, int width, int rows, SourceFormat format,
                     Image &out, float white_nits) {
    out.resize(std::max(width, 0), std::max(rows, 0));
    if (!source || width <= 0 || rows <= 0) return;
    const float hdr_scale = 1.0f / std::max(white_nits, 1.0f);
    // scRGB: each half float straight to its byte, through a table made once per white level.
    thread_local std::vector<std::uint8_t> half_bytes;
    thread_local float half_bytes_scale = -1;
    if (format == SourceFormat::rgba16f && half_bytes_scale != hdr_scale) {
        half_bytes.resize(65536);
        for (std::uint32_t h = 0; h < 65536; ++h)
            half_bytes[h] = encode(half_to_float(static_cast<std::uint16_t>(h)) * 80.0f * hdr_scale); // 1.0 is 80 nits
        half_bytes_scale = hdr_scale;
    }
    const auto *half_table = half_bytes.data();
    for_rows(rows, [&](int first, int last) {
    for (int y = first; y < last; ++y) {
        const auto *row = source + static_cast<std::size_t>(y) * pitch;
        auto *to = out.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        switch (format) {
        case SourceFormat::bgra8:
            for (int x = 0; x < width; ++x, to += 4, row += 4) {
                to[0] = row[0]; to[1] = row[1]; to[2] = row[2]; to[3] = 255;
            }
            break;
        case SourceFormat::rgba8:
            for (int x = 0; x < width; ++x, to += 4, row += 4) {
                to[0] = row[2]; to[1] = row[1]; to[2] = row[0]; to[3] = 255;
            }
            break;
        case SourceFormat::rgb10a2:
            for (int x = 0; x < width; ++x, to += 4, row += 4) {
                std::uint32_t v;
                std::memcpy(&v, row, 4);
                to[0] = static_cast<std::uint8_t>(((v >> 20) & 0x3ff) >> 2);
                to[1] = static_cast<std::uint8_t>(((v >> 10) & 0x3ff) >> 2);
                to[2] = static_cast<std::uint8_t>((v & 0x3ff) >> 2);
                to[3] = 255;
            }
            break;
        case SourceFormat::rgb10a2_pq: {
            const auto &pq = pq_table();
            for (int x = 0; x < width; ++x, to += 4, row += 4) {
                std::uint32_t v;
                std::memcpy(&v, row, 4);
                const float r = pq[v & 0x3ff] * hdr_scale, g = pq[(v >> 10) & 0x3ff] * hdr_scale,
                            b = pq[(v >> 20) & 0x3ff] * hdr_scale;
                // BT.2020 primaries to BT.709.
                store(to, 1.6605f * r - 0.5876f * g - 0.0728f * b, -0.1246f * r + 1.1329f * g - 0.0083f * b,
                      -0.0182f * r - 0.1006f * g + 1.1187f * b);
            }
            break;
        }
        case SourceFormat::rgba16f:
            for (int x = 0; x < width; ++x, to += 4, row += 8) {
                std::array<std::uint16_t, 3> h{};
                std::memcpy(h.data(), row, 6);
                to[0] = half_table[h[2]];
                to[1] = half_table[h[1]];
                to[2] = half_table[h[0]];
                to[3] = 255;
            }
            break;
        }
    }
    });
}

namespace {
// Halves an image (2 x 2 averages): bilinear alone aliases when shrinking more than 2x.
Image halve(const Image &in) {
    Image out;
    out.resize(std::max(1, in.width / 2), std::max(1, in.height / 2));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const auto at = [&](int xx, int yy) {
                xx = std::min(xx, in.width - 1);
                yy = std::min(yy, in.height - 1);
                return in.pixels.data() + (static_cast<std::size_t>(yy) * static_cast<std::size_t>(in.width) + static_cast<std::size_t>(xx)) * 4;
            };
            const auto *a = at(2 * x, 2 * y), *b = at(2 * x + 1, 2 * y), *c = at(2 * x, 2 * y + 1), *d = at(2 * x + 1, 2 * y + 1);
            auto *to = out.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)) * 4;
            for (int i = 0; i < 4; ++i) to[i] = static_cast<std::uint8_t>((a[i] + b[i] + c[i] + d[i] + 2) / 4);
        }
    return out;
}
} // namespace

void scale_bgra(const Image &in, Image &out) {
    if (out.width <= 0 || out.height <= 0 || in.width <= 0 || in.height <= 0) return;
    if (in.width == out.width && in.height == out.height) {
        out.pixels = in.pixels;
        return;
    }
    const Image *source = &in;
    Image reduced;
    while (source->width >= out.width * 2 && source->height >= out.height * 2) {
        reduced = halve(*source);
        source = &reduced;
    }
    // Integer bilinear: each column's two source columns and weight (0 to 256) worked out once.
    const float sx = static_cast<float>(source->width) / out.width, sy = static_cast<float>(source->height) / out.height;
    std::vector<int> x0(static_cast<std::size_t>(out.width)), x1(x0.size()), wx(x0.size());
    for (int x = 0; x < out.width; ++x) {
        const float fx = std::clamp((x + 0.5f) * sx - 0.5f, 0.0f, static_cast<float>(source->width - 1));
        const int left = static_cast<int>(fx);
        x0[static_cast<std::size_t>(x)] = left * 4;
        x1[static_cast<std::size_t>(x)] = std::min(left + 1, source->width - 1) * 4;
        wx[static_cast<std::size_t>(x)] = static_cast<int>((fx - left) * 256 + 0.5f);
    }
    const std::size_t stride = static_cast<std::size_t>(source->width) * 4;
    for_rows(out.height, [&](int first, int last) {
    for (int y = first; y < last; ++y) {
        const float fy = std::clamp((y + 0.5f) * sy - 0.5f, 0.0f, static_cast<float>(source->height - 1));
        const int top = static_cast<int>(fy);
        const int wy = static_cast<int>((fy - top) * 256 + 0.5f);
        const auto *r0 = source->pixels.data() + static_cast<std::size_t>(top) * stride;
        const auto *r1 = source->pixels.data() + static_cast<std::size_t>(std::min(top + 1, source->height - 1)) * stride;
        auto *to = out.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) * 4;
        for (int x = 0; x < out.width; ++x, to += 4) {
            const auto i = static_cast<std::size_t>(x);
            const auto *a = r0 + x0[i], *b = r0 + x1[i], *c = r1 + x0[i], *d = r1 + x1[i];
            const int w = wx[i];
            for (int k = 0; k < 4; ++k) {
                const int upper = a[k] * (256 - w) + b[k] * w, lower = c[k] * (256 - w) + d[k] * w;
                to[k] = static_cast<std::uint8_t>((upper * (256 - wy) + lower * wy + (1 << 15)) >> 16);
            }
        }
    }
    });
}

void apply_vignette(Image &image, float radius) {
    if (!(radius > 0) || image.width <= 0 || image.height <= 0) return;
    const float half = image.width * 0.5f, cx = image.width * 0.5f, cy = image.height * 0.5f;
    constexpr float edge = 0.03f; // half picture widths of soft edge: a blurred lens barrel, not a cut-out
    const float inner = std::max(radius - edge, 0.0f);
    for_rows(image.height, [&](int first, int last) {
    for (int y = first; y < last; ++y) {
        const float dy = (y + 0.5f - cy) / half;
        auto *row = image.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4;
        for (int x = 0; x < image.width; ++x) {
            const float dx = (x + 0.5f - cx) / half;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d <= inner) continue;
            const float keep = 1 - std::clamp((d - inner) / edge, 0.0f, 1.0f);
            auto *p = row + static_cast<std::size_t>(x) * 4;
            for (int i = 0; i < 3; ++i) p[i] = static_cast<std::uint8_t>(p[i] * keep + 0.5f);
        }
    }
    });
}

void bgra_to_nv12(const Image &image, std::vector<std::uint8_t> &out) {
    const int w = image.width & ~1, h = image.height & ~1;
    out.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3 / 2, 0);
    if (w <= 0 || h <= 0) return;
    // BT.709, limited range, in 16.16 fixed point.
    constexpr int kr = 13933, kg = 46871, kb = 4732; // 0.2126, 0.7152, 0.0722 x 65536
    const auto luma = [&](const std::uint8_t *p) { return (kr * p[2] + kg * p[1] + kb * p[0]) >> 16; }; // 0..255
    auto *y_plane = out.data();
    auto *uv_plane = out.data() + static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for_rows(h / 2, [&](int first, int last) {
    for (int y = first * 2; y < last * 2; ++y) {
        const auto *row = image.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4;
        for (int x = 0; x < w; ++x)
            y_plane[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(16 + (luma(row + x * 4) * 219 + 127) / 255);
    }
    for (int y = first * 2; y < last * 2; y += 2) {
        const auto *r0 = image.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4;
        const auto *r1 = r0 + static_cast<std::size_t>(image.width) * 4;
        auto *uv = uv_plane + static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(w);
        for (int x = 0; x < w; x += 2) {
            int b = 0, g = 0, r = 0;
            for (const auto *p : {r0 + x * 4, r0 + x * 4 + 4, r1 + x * 4, r1 + x * 4 + 4}) {
                b += p[0];
                g += p[1];
                r += p[2];
            }
            b = (b + 2) / 4;
            g = (g + 2) / 4;
            r = (r + 2) / 4;
            const std::uint8_t px[3]{static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(r)};
            const int yy = luma(px);
            // Cb = (B - Y) / 1.8556, Cr = (R - Y) / 1.5748, scaled to limited range (224 / 255).
            const int cb = 128 + static_cast<int>(std::lround((b - yy) * (224.0 / 255.0 / 1.8556)));
            const int cr = 128 + static_cast<int>(std::lround((r - yy) * (224.0 / 255.0 / 1.5748)));
            uv[x] = static_cast<std::uint8_t>(std::clamp(cb, 16, 240));
            uv[x + 1] = static_cast<std::uint8_t>(std::clamp(cr, 16, 240));
        }
    }
    });
}

void FramePacer::start(double seconds, double frame_rate) noexcept {
    start_ = seconds;
    rate_ = frame_rate > 0 && std::isfinite(frame_rate) ? frame_rate : ntsc_frame_rate;
    emitted_ = first_ = 0;
    started_ = true;
}

int FramePacer::due(double seconds) noexcept {
    if (!std::isfinite(seconds)) return 0;
    if (!started_) start(seconds);
    // Frames that should exist by now, the first at the start.
    const auto wanted = static_cast<std::int64_t>(std::floor((seconds - start_) * rate_)) + 1;
    const auto missing = wanted - emitted_;
    if (missing <= 0) return 0;
    const int count = static_cast<int>(std::min<std::int64_t>(missing, max_repeat_));
    // A long stall (a load, a pause) is not filled with a frozen frame: what is beyond the repeats is let go.
    emitted_ = missing > max_repeat_ ? wanted : emitted_ + count;
    first_ = emitted_ - count;
    return count;
}

void TapeLog::tape(std::uint32_t number) {
    if (number == number_) return;
    number_ = number;
    clips_.clear();
}

std::string photo_file_name(const std::tm &t) {
    char name[96];
    std::snprintf(name, sizeof(name), "ReSkate %04d-%02d-%02d %02d-%02d-%02d still.jpg", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return name;
}

std::string tape_file_name(std::uint32_t tape, std::size_t clips, const std::tm &t) {
    char name[128];
    std::snprintf(name, sizeof(name), "ReSkate %04d-%02d-%02d %02d-%02d-%02d tape %u (%zu clip%s).mp4", t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, tape, clips, clips == 1 ? "" : "s");
    return name;
}

std::string clip_file_name(std::uint32_t clip, const std::tm &t) {
    char name[96];
    std::snprintf(name, sizeof(name), "ReSkate %04d-%02d-%02d %02d-%02d-%02d clip %02u.mp4", t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, clip);
    return name;
}
} // namespace dingosdk::camcorder
