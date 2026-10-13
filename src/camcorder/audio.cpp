#include "audio.h"
#include "camera_math.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::camcorder {
void AudioTimeline::start(double seconds) noexcept {
    start_ = seconds;
    written_ = 0;
}

AudioTimeline::Placement AudioTimeline::place(double seconds, std::int64_t frames) noexcept {
    Placement p;
    if (frames <= 0 || !std::isfinite(seconds)) return p;
    // Where the packet belongs on the clip, in frames from its start.
    const auto at = static_cast<std::int64_t>(std::llround((seconds - start_) * audio_rate));
    const auto tolerance = static_cast<std::int64_t>(tolerance_ * audio_rate);
    const auto drift = at - written_;
    if (at + frames <= 0) {
        p.skip = frames; // all before the clip started
    } else if (at < 0 && written_ == 0) {
        p.skip = -at; // the part before the start
    } else if (drift > tolerance) {
        p.silence = drift; // the game was silent (or a packet was lost): keep the clock
    } else if (drift < -tolerance) {
        p.skip = std::min(-drift, frames); // overlaps what is already written
    }
    written_ += p.silence + frames - p.skip;
    return p;
}

void place_packet(AudioTimeline &timeline, double seconds, const std::int16_t *samples, std::int64_t frames,
                  std::vector<std::int16_t> &out) {
    const auto p = timeline.place(seconds, frames);
    out.insert(out.end(), static_cast<std::size_t>(p.silence) * audio_channels, 0);
    if (samples && frames > p.skip)
        out.insert(out.end(), samples + p.skip * audio_channels, samples + frames * audio_channels);
}

void to_pcm16(const float *samples, std::size_t count, std::int16_t *out) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        const float s = std::isfinite(samples[i]) ? std::clamp(samples[i], -1.0f, 1.0f) : 0.0f;
        out[i] = static_cast<std::int16_t>(std::lround(s * 32767.0f));
    }
}

void SoundTrack::start(double seconds) {
    timeline_.start(seconds);
    tail_.clear();
    sounding_ = false;
}

void SoundTrack::add(double seconds, const std::int16_t *samples, std::int64_t frames, std::vector<std::int16_t> &out) {
    const auto p = timeline_.place(seconds, frames);
    const auto kept = samples ? std::max<std::int64_t>(frames - p.skip, 0) : 0;
    if (p.silence == 0 && kept == 0) return;
    // A jump on the clock, in the middle of sound: fade out what was before it.
    const bool jump = sounding_ && (p.silence > 0 || p.skip > 0);
    const auto fade_frames = [](std::int16_t *s, std::int64_t count, bool in) {
        for (std::int64_t i = 0; i < count; ++i) {
            const float g = static_cast<float>(in ? i : count - 1 - i) / static_cast<float>(count);
            for (int c = 0; c < audio_channels; ++c) {
                auto &v = s[i * audio_channels + c];
                v = static_cast<std::int16_t>(std::lround(v * g));
            }
        }
    };
    if (jump && !tail_.empty()) fade_frames(tail_.data(), static_cast<std::int64_t>(tail_.size() / audio_channels), false);
    out.insert(out.end(), tail_.begin(), tail_.end());
    tail_.clear();
    out.insert(out.end(), static_cast<std::size_t>(p.silence) * audio_channels, 0);
    if (kept == 0) {
        sounding_ = false;
        return;
    }
    std::vector<std::int16_t> fresh(samples + p.skip * audio_channels, samples + frames * audio_channels);
    // And fade in what comes after it (or the clip's first sound).
    if (jump || !sounding_ || p.silence > 0) fade_frames(fresh.data(), std::min<std::int64_t>(kept, fade), true);
    sounding_ = true;
    const auto held = std::min<std::int64_t>(kept, fade) * audio_channels;
    out.insert(out.end(), fresh.begin(), fresh.end() - held);
    tail_.assign(fresh.end() - held, fresh.end());
}

void SoundTrack::flush(std::vector<std::int16_t> &out) {
    out.insert(out.end(), tail_.begin(), tail_.end());
    tail_.clear();
}
EarPlacement ear_placement(const std::array<float, 16> &camera, const std::array<float, 3> &skater) noexcept {
    const auto to = sub(skater, matrix_position(camera));
    const float distance = length(to);
    if (!std::isfinite(distance)) return {};
    const Vec3 right{camera[0], camera[1], camera[2]};
    const float side = distance > 1e-3f ? dot(to, right) / distance : 0.0f;
    return {distance, std::isfinite(side) ? std::clamp(side, -1.0f, 1.0f) : 0.0f};
}

// Within a few metres (handheld) nothing changes; further off the level falls about as sound
// does outdoors (6 dB each time the distance doubles), with a floor so a far-off line is still heard.
float CameraEar::gain_at(float distance) const noexcept {
    const float close = std::clamp(settings_.nearby, 0.5f, 100.0f);
    if (!std::isfinite(distance) || distance <= close) return 1.0f;
    return std::max(close / distance, std::clamp(settings_.floor, 0.0f, 1.0f));
}
float CameraEar::cutoff_at(float distance) const noexcept {
    const float close = std::clamp(settings_.nearby, 0.5f, 100.0f);
    if (!std::isfinite(distance) || distance <= close) return 20000.0f;
    return std::clamp(20000.0f / (1.0f + (distance - close) / std::clamp(settings_.air, 1.0f, 1000.0f)), 2500.0f, 20000.0f);
}
float CameraEar::pan_amount_at(float distance) const noexcept {
    if (!std::isfinite(distance)) return 0;
    const float close = std::clamp(settings_.nearby, 0.5f, 100.0f);
    return std::clamp((distance - close * 2 / 3) / 8.0f, 0.0f, std::clamp(settings_.pan, 0.0f, 1.0f));
}

void CameraEar::process(float *interleaved, std::size_t frames, EarPlacement where) noexcept {
    if (!interleaved || !frames) return;
    const float target_gain = gain_at(where.distance), target_cutoff = cutoff_at(where.distance);
    const float target_amount = pan_amount_at(where.distance);
    const float target_side = std::isfinite(where.side) ? std::clamp(where.side, -1.0f, 1.0f) : 0.0f;
    if (!started_) {
        started_ = true;
        gain_ = target_gain;
        cutoff_ = target_cutoff;
        side_ = target_side;
        amount_ = target_amount;
    }
    // Glides over about 80 ms.
    const float glide = 1.0f - std::exp(-1.0f / (0.08f * audio_rate));
    for (std::size_t f = 0; f < frames; ++f) {
        gain_ += (target_gain - gain_) * glide;
        cutoff_ += (target_cutoff - cutoff_) * glide;
        side_ += (target_side - side_) * glide;
        amount_ += (target_amount - amount_) * glide;
        float *frame = interleaved + f * audio_channels;
        if (gain_ > 0.9999f && cutoff_ > 19900.0f && amount_ < 1e-4f) {
            low_[0] = frame[0];
            low_[1] = frame[1];
            continue; // close by: as the game mixed it
        }
        // The air: a gentle one-pole low-pass.
        const float a = 1.0f - std::exp(-2.0f * pi * cutoff_ / audio_rate);
        for (int c = 0; c < audio_channels; ++c) low_[c] += (frame[c] - low_[c]) * a;
        // The skater's side: their sound, one voice, placed at constant power, blended in.
        const float middle = 0.5f * (low_[0] + low_[1]);
        const float angle = (side_ + 1.0f) * pi * 0.25f;
        const float left = middle * std::cos(angle) * 1.41421356f, right = middle * std::sin(angle) * 1.41421356f;
        frame[0] = gain_ * (low_[0] + (left - low_[0]) * amount_);
        frame[1] = gain_ * (low_[1] + (right - low_[1]) * amount_);
    }
}
} // namespace dingosdk::camcorder
