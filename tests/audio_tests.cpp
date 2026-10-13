#include "camcorder/audio.h"
#include "camcorder/camera_math.h"
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

TEST(the_sound_track_matches_the_timeline_when_steady) {
    const auto tone = sine(0.5f, 440, 4800);
    AudioTimeline plain;
    plain.start(0);
    SoundTrack track;
    track.start(0);
    std::vector<std::int16_t> a, b;
    for (int i = 0; i < 10; ++i) {
        place_packet(plain, i * 0.01, tone.data() + i * 480 * audio_channels, 480, a);
        track.add(i * 0.01, tone.data() + i * 480 * audio_channels, 480, b);
    }
    track.flush(b);
    CHECK(a.size() == b.size());
    // Only the clip's very first sound fades in; after that it is the same sound.
    bool same = true;
    for (std::size_t i = SoundTrack::fade * audio_channels; i < a.size(); ++i) same = same && a[i] == b[i];
    CHECK(same);
}

namespace {
// The biggest step from one sample to the next in the left channel: a click is a big step.
double biggest_step(const std::vector<std::int16_t> &s, std::size_t from = 0, std::size_t to = SIZE_MAX) {
    double most = 0;
    to = std::min(to, s.size() / audio_channels);
    for (std::size_t i = from + 1; i < to; ++i)
        most = std::max(most, std::abs(static_cast<double>(s[i * audio_channels]) - s[(i - 1) * audio_channels]) / 32768.0);
    return most;
}
} // namespace

TEST(gaps_and_trims_do_not_click) {
    // 100 ms packets of a tone that is never at zero where they meet.
    const auto tone = sine(0.8f, 310, 48000);
    constexpr int size = 4800;
    const auto packet = [&](int n) { return tone.data() + static_cast<std::size_t>(n) * size * audio_channels; };
    // A packet 100 ms late (a gap filled with silence), then one 70 ms early (a trim).
    const std::pair<double, int> arrivals[]{{0.0, 0}, {0.1, 1}, {0.3, 2}, {0.33, 3}, {0.43, 4}};
    SoundTrack track;
    track.start(0);
    std::vector<std::int16_t> out;
    for (const auto &[at, n] : arrivals) track.add(at, packet(n), size, out);
    track.flush(out);
    // A 310 Hz tone at 0.8 moves at most about 0.033 a sample; a click is a jump of up to 1.6.
    CHECK(biggest_step(out) < 0.05);
    // The same through the plain timeline clicks.
    AudioTimeline plain;
    plain.start(0);
    std::vector<std::int16_t> raw;
    for (const auto &[at, n] : arrivals) place_packet(plain, at, packet(n), size, raw);
    CHECK(biggest_step(raw) > 0.2);
    CHECK(raw.size() == out.size()); // and both keep the same time
}

TEST(the_camera_hears_the_skater_from_where_it_stands) {
    // Facing +Z, a skater 10 m away to the camera's right (screen right is -X there).
    const auto camera = camera_matrix({0, 1, 0}, 0, 0);
    const auto right = ear_placement(camera, {-6, 1, 8});
    CHECK_NEAR(right.distance, 10, 1e-4);
    CHECK(right.side > 0.5f);
    CHECK(ear_placement(camera, {6, 1, 8}).side < -0.5f);
    // Close by, nothing changes; far off, quieter and duller.
    const CameraEar ear;
    CHECK(ear.gain_at(1) == 1 && ear.cutoff_at(1) == 20000 && ear.pan_amount_at(1) == 0);
    CHECK_NEAR(ear.gain_at(12), 0.25, 1e-5);
    CHECK(ear.cutoff_at(30) < 8000);
    // A handling.json's sound: carrying further, and no re-panning.
    const CameraEar open_air({10, 0.3f, 40, 0});
    CHECK(open_air.gain_at(8) == 1 && open_air.pan_amount_at(50) == 0);
    CHECK_NEAR(open_air.gain_at(100), 0.3, 1e-5);
    std::vector<float> close_by(2 * 480), sound(2 * 480);
    for (std::size_t i = 0; i < close_by.size(); ++i) close_by[i] = sound[i] = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
    CameraEar close;
    close.process(close_by.data(), 480, {1, 0});
    for (std::size_t i = 0; i < close_by.size(); ++i) CHECK(close_by[i] == sound[i]);
    // Far to the right: the right channel carries more than the left.
    CameraEar far_off;
    for (int packet = 0; packet < 20; ++packet) {
        std::vector<float> both(2 * 480, 0.4f);
        far_off.process(both.data(), 480, {12, 1});
        if (packet == 19) CHECK(both[2 * 479 + 1] > both[2 * 479] * 2);
        if (packet == 19) CHECK(both[2 * 479 + 1] < 0.4f);
    }
}
