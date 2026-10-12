#include "viewfinder.h"
#include <cmath>

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

Viewfinder layout_viewfinder(const Frame &frame, float width, float height, const ViewfinderOptions &options) {
    Viewfinder v;
    const auto &s = frame.status;
    if (!s.powered || !frame.shot.valid || !(width > 0) || !(height > 0)) return v;
    v.visible = true;
    v.picture = options.pillarbox ? picture_rect(width, height) : Rect{0, 0, width, height};
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
    if (frame.optics.fisheye && frame.optics.vignette_radius > 0)
        v.vignette = Circle{p.x + p.width * 0.5f, p.y + p.height * 0.5f, frame.optics.vignette_radius * p.width * 0.5f};
    if (options.clean_feed) return v;

    // Readouts inset from the picture's corners, sized to it, so they sit the same at any resolution.
    const float unit = p.height / 480.0f; // one NTSC line
    const float text = 22 * unit, margin = 30 * unit;
    const float left = p.x + margin, right = p.x + p.width - margin, top = p.y + margin;
    const float bottom = p.y + p.height - margin - text;

    const bool rec = s.transport == Transport::recording;
    if (rec) {
        v.rec_dot = true;
        v.rec_dot_radius = text * 0.32f;
        v.rec_dot_x = left + v.rec_dot_radius;
        v.rec_dot_y = top + text * 0.5f;
        v.labels.push_back({"REC", left + v.rec_dot_radius * 2 + 6 * unit, top, text, Ink::red});
    } else {
        v.labels.push_back({"STBY", left, top, text, Ink::white});
    }
    v.labels.push_back({format_counter(s.counter_seconds), right, top, text, Ink::white, Align::right});
    const int minutes = static_cast<int>(std::ceil(s.tape_remaining / 60.0));
    const bool tape_low = s.tape_remaining < 5 * 60;
    v.labels.push_back({s.tape_remaining <= 0 ? "TAPE END" : std::to_string(minutes) + "min", right,
                        top + text * 1.3f, text, tape_low ? Ink::amber : Ink::white, Align::right});

    // The battery, top middle-left, blinking when nearly flat.
    const bool low = s.battery < 0.1;
    v.battery.visible = !low || std::fmod(options.blink_time, 1.0f) < 0.5f;
    v.battery.body = {left, top + text * 1.4f, text * 1.6f, text * 0.75f};
    v.battery.level = static_cast<float>(std::clamp(s.battery, 0.0, 1.0));

    if (s.zoom_bar) {
        v.zoom.visible = true;
        v.zoom.track = {left + text, bottom + text * 0.35f, p.width * 0.28f, text * 0.3f};
        v.zoom.position = s.zoom;
        v.labels.push_back({"W", left, bottom, text});
        v.labels.push_back({"T", v.zoom.track.x + v.zoom.track.width + text * 0.4f, bottom, text});
    }

    if (s.mode == Mode::tripod && s.active_tripod >= 0) {
        std::string what = "TRIPOD " + std::to_string(s.active_tripod + 1);
        if (s.tripod_setup) what += "  SETUP";
        v.labels.push_back({what, right, bottom, text * 0.8f, Ink::white, Align::right});
    }
    if (!options.hint.empty())
        v.labels.push_back({options.hint, p.x + p.width * 0.5f, p.y + p.height - margin * 0.5f - text * 0.7f,
                            text * 0.62f, Ink::white, Align::centre});
    if (s.tripod_setup) {
        // Action safe: 90% of the picture.
        v.safe_frame = Rect{p.x + p.width * 0.05f, p.y + p.height * 0.05f, p.width * 0.9f, p.height * 0.9f};
    }
    return v;
}
} // namespace dingosdk::camcorder
