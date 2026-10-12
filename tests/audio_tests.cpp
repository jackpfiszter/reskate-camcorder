#include "camcorder/audio.h"
#include "check.h"
#include <cmath>

using namespace dingosdk::camcorder;

namespace {
std::vector<std::int16_t> sine(float amplitude, float hz, int frames) {
    std::vector<std::int16_t> out(static_cast<std::size_t>(frames) * audio_channels);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < audio_channels; ++c)
            out[static_cast<std::size_t>(i) * audio_channels + c] =
                static_cast<std::int16_t>(amplitude * 32767 * std::sin(2 * 3.14159265f * hz * i / audio_rate));
    return out;
}
// How much of `hz` is in the left channel of the second half (Goertzel).
double strength(const std::vector<std::int16_t> &s, float hz) {
    const std::size_t frames = s.size() / audio_channels, from = frames / 2;
    const double k = 2 * std::cos(2 * 3.14159265358979 * hz / audio_rate);
    double a = 0, b = 0;
    for (std::size_t i = from; i < frames; ++i) {
        const double x = s[i * audio_channels] / 32768.0 + k * a - b;
        b = a;
        a = x;
    }
    return std::sqrt(a * a + b * b - k * a * b) / static_cast<double>(frames - from);
}
double rms(const std::vector<std::int16_t> &s, std::size_t from = 0) {
    double sum = 0;
    for (std::size_t i = from; i < s.size(); ++i) sum += static_cast<double>(s[i]) * s[i];
    return std::sqrt(sum / static_cast<double>(s.size() - from)) / 32768.0;
}
} // namespace

TEST(timeline_leaves_steady_audio_alone) {
    AudioTimeline t;
    t.start(10.0);
    for (int i = 0; i < 100; ++i) {
        // 10 ms packets, each a few milliseconds early or late.
        const double jitter = i ? (i % 3 - 1) * 0.004 : 0.0;
        const auto p = t.place(10.0 + i * 0.01 + jitter, 480);
        CHECK(p.silence == 0 && p.skip == 0);
    }
    CHECK(t.written() == 48000);
}

TEST(timeline_fills_silence_and_trims_overlap) {
    AudioTimeline t;
    t.start(0);
    t.place(0, 480);
    // Half a second with no packets (the game was quiet): silence keeps the clock.
    auto p = t.place(0.51, 480);
    CHECK_NEAR(static_cast<double>(p.silence), 0.5 * audio_rate, 2);
    CHECK_NEAR(static_cast<double>(t.written()), 0.52 * audio_rate, 2);
    // A packet stamped 100 ms before where the clip is: the overlap is left out.
    p = t.place(0.52 - 0.1, 9600);
    CHECK_NEAR(static_cast<double>(p.skip), 4800, 2);
    CHECK_NEAR(static_cast<double>(t.written()), 0.62 * audio_rate, 2);
}

TEST(timeline_starts_with_the_clip) {
    AudioTimeline t;
    t.start(5.0);
    CHECK(t.place(4.0, 480).skip == 480);           // before the clip
    const auto p = t.place(5.0 - 0.005, 480);       // straddles the start
    CHECK(p.skip == 240);
    CHECK(t.written() == 240);
    std::vector<std::int16_t> out;
    AudioTimeline u;
    u.start(0);
    const std::int16_t one[4]{1, 2, 3, 4};
    place_packet(u, 0.1, one, 2, out); // 100 ms late: silence first
    CHECK(out.size() == (4800 + 2) * 2);
    CHECK(out[out.size() - 1] == 4 && out[0] == 0);
}

TEST(vx_mic_levels_and_colours_the_sound) {
    VxMic mic;
    // Silence stays silence.
    std::vector<std::int16_t> quiet(9600, 0);
    mic.process(quiet.data(), 4800);
    CHECK(rms(quiet) == 0);
    // A quiet room comes up, a loud slam comes down: the level control.
    VxMic a, b;
    auto soft = sine(0.02f, 1000, 48000), loud = sine(0.9f, 1000, 48000);
    const double soft_in = rms(soft), loud_in = rms(loud);
    a.process(soft.data(), 48000);
    b.process(loud.data(), 48000);
    CHECK(rms(soft, soft.size() / 2) > soft_in * 2);
    CHECK(rms(loud, loud.size() / 2) < loud_in);
    // Rumble below the capsule's range comes out much weaker than the voice of the board.
    VxMic c;
    auto mix = sine(0.2f, 30, 48000);
    const auto tone = sine(0.2f, 1000, 48000);
    for (std::size_t i = 0; i < mix.size(); ++i) mix[i] = static_cast<std::int16_t>(mix[i] + tone[i]);
    const double before = strength(mix, 30) / strength(mix, 1000);
    c.process(mix.data(), 48000);
    CHECK(strength(mix, 30) / strength(mix, 1000) < before * 0.4);
    // A constant offset (DC) is removed.
    VxMic e;
    std::vector<std::int16_t> dc(96000, 8000);
    e.process(dc.data(), 48000);
    CHECK(std::abs(dc.back()) < 200);
}
