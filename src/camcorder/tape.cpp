#include "tape.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dingosdk::camcorder {
Timecode drop_frame_timecode(std::int64_t frame) noexcept {
    // 17982 frames each ten minutes, 1798 in each minute that drops two numbers.
    constexpr std::int64_t ten_minutes = 17982, minute = 1798;
    frame = std::max<std::int64_t>(frame, 0);
    const auto tens = frame / ten_minutes, rest = frame % ten_minutes;
    // The first minute of each ten drops nothing; the rest drop two numbers each.
    const std::int64_t numbered = frame + 18 * tens + (rest > 1 ? 2 * ((rest - 2) / minute) : 0);
    Timecode t;
    t.frames = static_cast<int>(numbered % 30);
    t.seconds = static_cast<int>(numbered / 30 % 60);
    t.minutes = static_cast<int>(numbered / (30 * 60) % 60);
    t.hours = static_cast<int>(numbered / (30 * 60 * 60) % 24);
    return t;
}

std::string format_timecode(const Timecode &t) {
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d:%02d;%02d", t.hours, t.minutes, t.seconds, t.frames);
    return text;
}

std::string format_counter(double seconds) {
    const auto whole = static_cast<long long>(std::isfinite(seconds) ? std::max(0.0, std::floor(seconds)) : 0);
    char text[48];
    std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", whole / 3600, whole / 60 % 60, whole % 60);
    return text;
}

std::int64_t Deck::tape_frames() const {
    return static_cast<std::int64_t>(std::floor(settings_.tape_seconds * ntsc_frame_rate));
}

double Deck::tape_remaining() const {
    if (!settings_.finite_tape) return settings_.tape_seconds;
    return std::max(0.0, static_cast<double>(tape_frames() - frame_) / ntsc_frame_rate);
}

double Deck::battery_fraction() const {
    if (!settings_.finite_battery || settings_.battery_seconds <= 0) return 1;
    return std::clamp(1 - battery_used_ / settings_.battery_seconds, 0.0, 1.0);
}

bool Deck::record(std::string label, std::string *reason) {
    const auto refuse = [&](const char *why) {
        if (reason) *reason = why;
        return false;
    };
    if (recording()) return true;
    if (settings_.finite_tape && frame_ >= tape_frames()) return refuse("Tape end. Put in a new tape.");
    if (battery_fraction() <= 0) return refuse("Battery is flat.");
    stopped_.clear();
    Clip clip;
    clip.number = static_cast<std::uint32_t>(clips_.size() + 1);
    clip.start_frame = clip.end_frame = frame_;
    clip.label = std::move(label);
    clips_.push_back(std::move(clip));
    fraction_ = 0;
    transport_ = Transport::recording;
    return true;
}

void Deck::stop() {
    if (!recording()) return;
    transport_ = Transport::standby;
    clips_.back().end_frame = frame_;
}

void Deck::tick(double dt, bool powered) {
    if (!std::isfinite(dt) || dt <= 0) return;
    if (powered && settings_.finite_battery) battery_used_ += dt;
    if (!recording()) return;
    if (!powered || battery_fraction() <= 0) {
        stop();
        stopped_ = powered ? "Battery is flat." : "Powered off.";
        return;
    }
    fraction_ += dt * ntsc_frame_rate;
    const auto whole = static_cast<std::int64_t>(std::floor(fraction_));
    fraction_ -= static_cast<double>(whole);
    frame_ += whole;
    if (settings_.finite_tape && frame_ >= tape_frames()) {
        frame_ = tape_frames();
        stop();
        stopped_ = "Tape end.";
        return;
    }
    clips_.back().end_frame = frame_;
}

void Deck::new_tape() {
    stop();
    frame_ = 0;
    fraction_ = 0;
    clips_.clear();
    stopped_.clear();
}
} // namespace dingosdk::camcorder
