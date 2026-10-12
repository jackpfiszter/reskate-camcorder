#pragma once
// The clip's sound, the parts that need no Windows: keeping the game's audio on the clip's clock,
// and the VX mic.
//
// The ReSkate side records the game's own audio (Windows' per-process loopback, so nothing else on
// the PC ends up in the clip) as 48 kHz stereo, in floating point so loud moments (several skaters'
// pops and slams at once) are not clipped, and hands each packet here with the time it was played.
// A limiter brings those moments under full scale smoothly. Windows sends nothing while the game is
// silent and packets come a little early or late, so the timeline fills gaps with silence and trims
// overlaps: the sound never drifts off the picture. Each such jump is faded over two milliseconds
// either side, or it would click (SoundTrack).
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dingosdk::camcorder {
inline constexpr int audio_rate = 48000, audio_channels = 2;

enum class Sound : std::uint8_t {
    off,    // picture only
    game,   // the game's sound as it plays
    vx_mic, // through the VX mic (below)
};

class AudioTimeline {
public:
    // Packets this far off the clock are put back on it; anything closer is left (jitter).
    explicit AudioTimeline(double tolerance_seconds = 0.04) : tolerance_(tolerance_seconds) {}
    // The clip starts at `seconds` (the same clock as the picture's FramePacer).
    void start(double seconds) noexcept;
    struct Placement {
        std::int64_t silence{}; // frames of silence to write first
        std::int64_t skip{};    // frames at the start of the packet to leave out
    };
    // A packet of `frames` frames that began playing at `seconds`. Before the clip starts: all skipped.
    Placement place(double seconds, std::int64_t frames) noexcept;
    std::int64_t written() const noexcept { return written_; }

private:
    double start_{}, tolerance_;
    std::int64_t written_{};
};

// Appends one packet to `out` (interleaved stereo), placed by the timeline.
void place_packet(AudioTimeline &, double seconds, const std::int16_t *samples, std::int64_t frames,
                  std::vector<std::int16_t> &out);

// The timeline without clicks: where it fills a gap or trims an overlap, the sound before fades
// out and the sound after fades in, over `fade` frames each (2 ms). The last `fade` frames are held
// back until the next packet says whether they need fading; flush() lets them go at the end.
class SoundTrack {
public:
    static constexpr int fade = 96;
    void start(double seconds);
    void add(double seconds, const std::int16_t *samples, std::int64_t frames, std::vector<std::int16_t> &out);
    void flush(std::vector<std::int16_t> &out);
    std::int64_t written() const noexcept { return timeline_.written(); }

private:
    AudioTimeline timeline_;
    std::vector<std::int16_t> tail_;
    bool sounding_{};
};

// A peak limiter: nothing above `ceiling` (of full scale) comes out, and nothing is clipped. It sees
// `lookahead` frames ahead (1.5 ms; its output is that much late), so the gain comes down smoothly
// before a hit arrives, and goes back up over about 80 ms.
class Limiter {
public:
    static constexpr int lookahead = 72;
    explicit Limiter(float ceiling = 0.89f) : ceiling_(ceiling) {}
    // Interleaved stereo floats, in place.
    void process(float *interleaved, std::size_t frames) noexcept;

private:
    float ceiling_;
    float gain_{1};
    std::array<float, static_cast<std::size_t>(lookahead) * 2> delay_{}; // interleaved, a ring
    std::size_t at_{};
};
// Floats (full scale 1) to 16-bit samples, rounded and kept in range.
void to_pcm16(const float *samples, std::size_t count, std::int16_t *out) noexcept;

// The VX1000's own microphone, roughly: the stereo mic on top of the camera, its automatic level
// control pumping up the room and squashing the board's slams, the low end thin and the top rolled
// off, and loud hits breaking up. Filters and the level control keep their state across packets.
class VxMic {
public:
    // Interleaved stereo floats (full scale 1), in place.
    void process(float *interleaved, std::size_t frames) noexcept;
    void process(std::int16_t *interleaved, std::size_t frames) noexcept;

private:
    struct Channel {
        float high_in{}, high_out{}; // high-pass
        float low1{}, low2{};        // two-pole low-pass
    };
    Channel channels_[audio_channels];
    float level_{};    // the level control's envelope (of a smoothed level, not each sample)
    float detect_{};   // that smoothed level
    float gain_{1};    // the gain it sets, itself smoothed: a gain that moves within a wave distorts it
};
} // namespace dingosdk::camcorder
