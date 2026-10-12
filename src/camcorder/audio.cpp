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

void VxMic::process(std::int16_t *interleaved, std::size_t frames) noexcept {
    if (!interleaved) return;
    constexpr float pi = 3.14159265f, rate = static_cast<float>(audio_rate);
    // A small capsule: little below 120 Hz, rolled off above 9 kHz.
    constexpr float high_cut = 120.0f, low_cut = 9000.0f;
    const float high_a = 1 / (1 + 2 * pi * high_cut / rate);
    const float low_a = 1 - std::exp(-2 * pi * low_cut / rate);
    // The level control: quick to duck a slam (5 ms), slow to bring the room back up (400 ms).
    const float attack = 1 - std::exp(-1 / (0.005f * rate)), release = 1 - std::exp(-1 / (0.4f * rate));
    constexpr float target = 0.25f, most_gain = 4.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        float x[audio_channels];
        float loudest = 0;
        for (int c = 0; c < audio_channels; ++c) {
            auto &ch = channels_[c];
            const float in = interleaved[i * audio_channels + static_cast<std::size_t>(c)] / 32768.0f;
            ch.high_out = high_a * (ch.high_out + in - ch.high_in);
            ch.high_in = in;
            ch.low1 += low_a * (ch.high_out - ch.low1);
            ch.low2 += low_a * (ch.low1 - ch.low2);
            x[c] = ch.low2;
            loudest = std::max(loudest, std::abs(x[c]));
        }
        level_ += (loudest > level_ ? attack : release) * (loudest - level_);
        const float gain = std::min(most_gain, target / std::max(level_, 1e-4f));
        for (int c = 0; c < audio_channels; ++c) {
            // Pushed into the converter: loud hits round off, then hard-clip like the real thing.
            const float y = std::tanh(x[c] * gain * 1.6f) / std::tanh(1.6f);
            interleaved[i * audio_channels + static_cast<std::size_t>(c)] =
                static_cast<std::int16_t>(std::clamp(y, -1.0f, 1.0f) * 32767.0f);
        }
    }
}
} // namespace dingosdk::camcorder
