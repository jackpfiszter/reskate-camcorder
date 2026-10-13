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

void VxMic::process(float *interleaved, std::size_t frames) noexcept {
    if (!interleaved) return;
    constexpr float rate = static_cast<float>(audio_rate);
    const auto coefficient = [](float seconds) { return 1 - std::exp(-1 / (std::max(seconds, 1e-4f) * rate)); };
    // A small capsule: little below the high cut, rolled off above the low cut.
    const float high_cut = std::clamp(mic_.high_cut, 10.0f, 2000.0f), low_cut = std::clamp(mic_.low_cut, 1000.0f, 23000.0f);
    const float high_a = 1 / (1 + 2 * pi * high_cut / rate);
    const float low_a = 1 - std::exp(-2 * pi * low_cut / rate);
    // The level control: it hears a level smoothed over a few milliseconds, ducks a slam over 10 ms
    // and brings the room back up over 400 ms; the gain it sets moves smoothly too. (Following each
    // sample, as it once did, rode the gain within the waveform: pops came out as crunchy clicks.)
    const float detect = coefficient(0.003f), attack = coefficient(mic_.attack), release = coefficient(mic_.release);
    const float glide = coefficient(0.015f);
    const float target = mic_.target, most_gain = std::max(mic_.most_gain, 0.0f), drive = std::max(mic_.drive, 0.05f);
    for (std::size_t i = 0; i < frames; ++i) {
        float x[audio_channels];
        float loudest = 0;
        for (int c = 0; c < audio_channels; ++c) {
            auto &ch = channels_[c];
            const float in = interleaved[i * audio_channels + static_cast<std::size_t>(c)];
            ch.high_out = high_a * (ch.high_out + in - ch.high_in);
            ch.high_in = in;
            ch.low1 += low_a * (ch.high_out - ch.low1);
            ch.low2 += low_a * (ch.low1 - ch.low2);
            x[c] = ch.low2;
            loudest = std::max(loudest, std::abs(x[c]));
        }
        detect_ += detect * (loudest - detect_);
        level_ += (detect_ > level_ ? attack : release) * (detect_ - level_);
        gain_ += glide * (std::min(most_gain, target / std::max(level_, 1e-4f)) - gain_);
        // Pushed into the converter: loud hits round off softly towards full scale, never cut flat.
        for (int c = 0; c < audio_channels; ++c)
            interleaved[i * audio_channels + static_cast<std::size_t>(c)] = std::tanh(x[c] * gain_ * drive);
    }
}

void VxMic::process(std::int16_t *interleaved, std::size_t frames) noexcept {
    if (!interleaved) return;
    std::vector<float> samples(frames * audio_channels);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = interleaved[i] / 32768.0f;
    process(samples.data(), frames);
    to_pcm16(samples.data(), samples.size(), interleaved);
}

void Limiter::process(float *interleaved, std::size_t frames) noexcept {
    if (!interleaved) return;
    const float attack = 1 - std::exp(-4.0f / lookahead);               // most of the way within the lookahead
    const float release = 1 - std::exp(-1 / (0.08f * static_cast<float>(audio_rate)));
    for (std::size_t i = 0; i < frames; ++i) {
        float *frame = interleaved + i * audio_channels;
        // The oldest frame leaves the ring as this one enters it.
        const float out_left = delay_[at_], out_right = delay_[at_ + 1];
        delay_[at_] = std::isfinite(frame[0]) ? frame[0] : 0.0f;
        delay_[at_ + 1] = std::isfinite(frame[1]) ? frame[1] : 0.0f;
        at_ = (at_ + 2) % delay_.size();
        float peak = 0;
        for (const float s : delay_) peak = std::max(peak, std::abs(s));
        const float wanted = peak > ceiling_ ? ceiling_ / peak : 1.0f;
        gain_ += (wanted < gain_ ? attack : release) * (wanted - gain_);
        // Anything the gain has not quite caught is rounded under the ceiling, never cut flat.
        const auto keep = [&](float s) {
            const float y = s * gain_;
            return std::abs(y) <= ceiling_ ? y : std::copysign(ceiling_ + (1 - ceiling_) * std::tanh((std::abs(y) - ceiling_) / (1 - ceiling_)), y);
        };
        frame[0] = keep(out_left);
        frame[1] = keep(out_right);
    }
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
    const float near = std::clamp(settings_.near, 0.5f, 100.0f);
    if (!std::isfinite(distance) || distance <= near) return 1.0f;
    return std::max(near / distance, std::clamp(settings_.floor, 0.0f, 1.0f));
}
float CameraEar::cutoff_at(float distance) const noexcept {
    const float near = std::clamp(settings_.near, 0.5f, 100.0f);
    if (!std::isfinite(distance) || distance <= near) return 20000.0f;
    return std::clamp(20000.0f / (1.0f + (distance - near) / std::clamp(settings_.air, 1.0f, 1000.0f)), 2500.0f, 20000.0f);
}
float CameraEar::pan_amount_at(float distance) const noexcept {
    if (!std::isfinite(distance)) return 0;
    const float near = std::clamp(settings_.near, 0.5f, 100.0f);
    return std::clamp((distance - near * 2 / 3) / 8.0f, 0.0f, std::clamp(settings_.pan, 0.0f, 1.0f));
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
