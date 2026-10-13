#include "viewfinder.h"
#include "tape.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dingosdk::camcorder {
Rect picture_rect(float width, float height, float aspect) noexcept {
    if (!(width > 0) || !(height > 0) || !(aspect > 0)) return {};
    if (width / height > aspect) {
        const float w = height * aspect;
        return {(width - w) * 0.5f, 0, w, height};
    }
    const float h = width / aspect;
    return {0, (height - h) * 0.5f, width, h};
}

namespace {
bool holds(const When &when, const Frame &frame, const ViewfinderOptions &options) {
    const auto &s = frame.status;
    bool so = false;
    switch (when.what) {
    case Condition::recording: so = s.transport == Transport::recording; break;
    case Condition::zooming: so = s.zoom_bar; break;
    case Condition::tripod: so = s.mode == Mode::tripod && s.active_tripod >= 0; break;
    case Condition::setup: so = s.tripod_setup; break;
    case Condition::handheld: so = s.mode == Mode::handheld; break;
    case Condition::low_battery: so = s.battery < 0.1; break;
    case Condition::low_tape: so = s.tape_remaining < 5 * 60; break;
    case Condition::tape_end: so = s.tape_remaining <= 0; break;
    case Condition::fisheye: so = s.fisheye; break;
    case Condition::light: so = options.light; break;
    }
    return so == when.is;
}
std::string one_decimal(double v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f", v);
    return text;
}
} // namespace

std::string viewfinder_text(std::string_view text, const Frame &frame, const ViewfinderOptions &options) {
    const auto &s = frame.status;
    const int minutes = static_cast<int>(std::ceil(std::max(s.tape_remaining, 0.0) / 60.0));
    std::string out;
    while (!text.empty()) {
        const auto open = text.find('{');
        const auto close = open == std::string_view::npos ? open : text.find('}', open);
        if (close == std::string_view::npos) {
            out += text;
            break;
        }
        out += text.substr(0, open);
        const auto field = text.substr(open + 1, close - open - 1);
        text.remove_prefix(close + 1);
        std::vector<std::string> stamp;
        if ((field == "date" || field == "time") && options.clock) {
            StampProfile profile;
            profile.lines = StampProfile::Lines::time_over_date;
            profile.date = options.date_order;
            stamp = date_stamp_lines(*options.clock, options.year, profile);
        }
        if (field == "counter") out += format_counter(s.counter_seconds);
        else if (field == "remaining") out += s.tape_remaining <= 0 ? "TAPE END" : std::to_string(minutes) + "min";
        else if (field == "minutes") out += std::to_string(minutes);
        else if (field == "status") out += s.transport == Transport::recording ? "REC" : "STBY";
        else if (field == "zoom") out += one_decimal(s.zoom_ratio);
        else if (field == "focal") out += one_decimal(frame.optics.focal_mm);
        else if (field == "time") out += stamp.size() > 0 ? stamp[0] : "";
        else if (field == "date") out += stamp.size() > 1 ? stamp[1] : "";
        else if (field == "tripod") out += s.active_tripod >= 0 ? std::to_string(s.active_tripod + 1) : "";
        else if (field == "clip") out += std::to_string(s.clip);
        else if (field == "battery") out += std::to_string(static_cast<int>(std::lround(std::clamp(s.battery, 0.0, 1.0) * 100)));
        else if (field == "camera") out += s.camera_name;
        else if (field == "lens") out += s.lens_name;
        else if (field == "format") out += options.format;
        else out += "{" + std::string(field) + "}"; // not a field: as written
    }
    return out;
}

Viewfinder layout_viewfinder(const Frame &frame, float width, float height, const ViewfinderOptions &options) {
    Viewfinder v;
    const auto &s = frame.status;
    if (!s.powered || !frame.shot.valid || !(width > 0) || !(height > 0)) return v;
    v.visible = true;
    v.picture = options.pillarbox ? picture_rect(width, height, frame.optics.aspect) : Rect{0, 0, width, height};
    const auto &p = v.picture;
    if (options.pillarbox) {
        if (p.x > 0.5f) {
            v.bars.push_back({0, 0, p.x, height});
            v.bars.push_back({p.x + p.width, 0, width - p.x - p.width, height});
        }
        if (p.y > 0.5f) {
            v.bars.push_back({0, 0, width, p.y});
            v.bars.push_back({0, p.y + p.height, width, height - p.y - p.height});
        }
    }
    if (frame.optics.vignette_radius > 0)
        v.vignette = Circle{p.x + p.width * 0.5f, p.y + p.height * 0.5f, frame.optics.vignette_radius * p.width * 0.5f};
    if (options.clean_feed) return v;

    // Sized to the picture, so a layout sits the same at any resolution: 480ths of its height.
    const float unit = p.height / 480.0f;
    const float line_scale = std::max(p.height / 1080.0f, 0.75f);
    static const ViewfinderLayout vx1000 = vx1000_viewfinder();
    const auto &layout = options.layout ? *options.layout : vx1000;
    for (const auto &e : layout.elements) {
        if (!std::all_of(e.when.begin(), e.when.end(), [&](const When &w) { return holds(w, frame, options); })) continue;
        if (e.blink > 0 && std::fmod(options.blink_time, e.blink) >= e.blink * 0.5f) continue;
        const auto rgb = e.ink == Ink::rec ? layout.rec : e.ink == Ink::warning ? layout.warning : e.ink == Ink::own ? e.colour : layout.text;
        const Colour colour{rgb[0], rgb[1], rgb[2], static_cast<std::uint8_t>(std::lround(std::clamp(e.opacity, 0.0f, 1.0f) * 255))};
        const float size = e.size * unit, thickness = std::max(e.thickness * line_scale, 0.5f);
        const float dx = e.offset[0] * unit, dy = e.offset[1] * unit;
        const auto column = static_cast<int>(e.anchor) % 3, row = static_cast<int>(e.anchor) / 3;
        // Where a box `w` x `h` goes at this element's anchor and offset.
        const auto place = [&](float w, float h) {
            const float x = column == 0 ? p.x + dx : column == 1 ? p.x + (p.width - w) * 0.5f + dx : p.x + p.width - dx - w;
            const float y = row == 0 ? p.y + dy : row == 1 ? p.y + (p.height - h) * 0.5f + dy : p.y + p.height - dy - h;
            return std::pair{x, y};
        };
        const auto line = [&](float x0, float y0, float x1, float y1) {
            v.marks.push_back({Mark::Kind::line, x0, y0, x1, y1, thickness, colour, e.shadow});
        };
        switch (e.show) {
        case Show::text: {
            const auto words = viewfinder_text(e.text, frame, options);
            if (words.empty()) break;
            const auto [x, y] = place(0, size);
            v.labels.push_back({words, x, y, size, colour, column == 0 ? Align::left : column == 1 ? Align::centre : Align::right, e.shadow});
            break;
        }
        case Show::dot: {
            const auto [x, y] = place(size, size);
            v.marks.push_back({Mark::Kind::dot, x + size * 0.5f, y + size * 0.5f, size * 0.5f, 0, thickness, colour, false});
            break;
        }
        case Show::battery: {
            // A body twice and a bit as wide as tall, a nub, and four bars of charge; nearly flat, it blinks.
            if (s.battery < 0.1 && std::fmod(options.blink_time, 1.0f) >= 0.5f) break;
            const float w = size * 2.133f, nub = std::max(3.0f * line_scale, 1.0f);
            const auto [x, y] = place(w + nub, size);
            v.marks.push_back({Mark::Kind::outline, x, y, x + w, y + size, thickness, colour, e.shadow});
            v.marks.push_back({Mark::Kind::box, x + w, y + size * 0.3f, x + w + nub, y + size * 0.7f, thickness, colour, false});
            const int bars = static_cast<int>(std::ceil(std::clamp(s.battery, 0.0, 1.0) * 4));
            const float pad = 2 * line_scale, bar = (w - 2 * pad) / 4;
            for (int i = 0; i < bars; ++i) {
                const float from = x + pad + static_cast<float>(i) * bar;
                v.marks.push_back({Mark::Kind::box, from + line_scale, y + pad, from + bar - line_scale, y + size - pad, thickness, colour, false});
            }
            break;
        }
        case Show::zoom_bar: {
            // W, the bar, T: the bar `extent[0]` of the picture's width, filled to the zoom.
            const float track = std::clamp(e.extent[0], 0.02f, 1.0f) * p.width;
            const auto [x, y] = place(size * 2 + track, size);
            v.labels.push_back({"W", x, y, size, colour, Align::left, e.shadow});
            const float tx = x + size, ty = y + size * 0.35f, th = size * 0.3f;
            v.marks.push_back({Mark::Kind::outline, tx, ty, tx + track, ty + th, thickness, colour, e.shadow});
            v.marks.push_back({Mark::Kind::box, tx, ty, tx + track * std::clamp(s.zoom, 0.0f, 1.0f), ty + th, thickness, colour, false});
            v.labels.push_back({"T", tx + track + size * 0.4f, y, size, colour, Align::left, e.shadow});
            break;
        }
        case Show::box: {
            const float w = e.extent[0] * unit, h = e.extent[1] * unit;
            const auto [x, y] = place(w, h);
            v.marks.push_back({e.filled ? Mark::Kind::box : Mark::Kind::outline, x, y, x + w, y + h, thickness, colour, e.shadow && !e.filled});
            break;
        }
        case Show::centre: {
            const float cx = p.x + p.width * 0.5f + dx, cy = p.y + p.height * 0.5f + dy, arm = size * 0.5f;
            line(cx - arm, cy, cx + arm, cy);
            line(cx, cy - arm, cx, cy + arm);
            break;
        }
        case Show::grid: {
            const int n = std::clamp(static_cast<int>(std::lround(e.size)), 2, 12);
            for (int i = 1; i < n; ++i) {
                const float x = p.x + p.width * static_cast<float>(i) / static_cast<float>(n);
                const float y = p.y + p.height * static_cast<float>(i) / static_cast<float>(n);
                line(x, p.y, x, p.y + p.height);
                line(p.x, y, p.x + p.width, y);
            }
            break;
        }
        case Show::corners: {
            const float l = p.x + dx, r = p.x + p.width - dx, t = p.y + dy, b = p.y + p.height - dy;
            for (const auto &[x, y, sx, sy] : {std::array{l, t, 1.0f, 1.0f}, std::array{r, t, -1.0f, 1.0f}, std::array{l, b, 1.0f, -1.0f},
                                               std::array{r, b, -1.0f, -1.0f}}) {
                line(x, y, x + sx * size, y);
                line(x, y, x, y + sy * size);
            }
            break;
        }
        }
    }

    if (!options.hint.empty()) {
        const float text = 22 * unit, margin = 30 * unit;
        const Colour colour{layout.text[0], layout.text[1], layout.text[2], 255};
        v.labels.push_back({options.hint, p.x + p.width * 0.5f, p.y + p.height - margin * 0.5f - text * 0.7f, text * 0.62f, colour, Align::centre});
    }
    if (s.tripod_setup) {
        // Action safe: 90% of the picture.
        v.safe_frame = Rect{p.x + p.width * 0.05f, p.y + p.height * 0.05f, p.width * 0.9f, p.height * 0.9f};
    }
    return v;
}
} // namespace dingosdk::camcorder
