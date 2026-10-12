#include "audio.h"
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
    constexpr float pi = 3.14159265f, rate = static_cast<float>(audio_rate);
    const auto coefficient = [](float seconds) { return 1 - std::exp(-1 / (seconds * rate)); };
    // A small capsule: little below 120 Hz, rolled off above 9 kHz.
    constexpr float high_cut = 120.0f, low_cut = 9000.0f;
    const float high_a = 1 / (1 + 2 * pi * high_cut / rate);
    const float low_a = 1 - std::exp(-2 * pi * low_cut / rate);
    // The level control: it hears a level smoothed over a few milliseconds, ducks a slam over 10 ms
    // and brings the room back up over 400 ms; the gain it sets moves smoothly too. (Following each
    // sample, as it once did, rode the gain within the waveform: pops came out as crunchy clicks.)
    const float detect = coefficient(0.003f), attack = coefficient(0.01f), release = coefficient(0.4f);
    const float glide = coefficient(0.015f);
    constexpr float target = 0.25f, most_gain = 3.0f, drive = 1.2f;
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
} // namespace dingosdk::camcorder
