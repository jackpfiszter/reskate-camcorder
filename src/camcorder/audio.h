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

// A camera's own microphone (a camera profile's "mic": profiles.h). The defaults are the VX1000's:
// the stereo mic on top of the camera, little below 120 Hz, rolled off above 9 kHz, its automatic
// level control pumping up the room and squashing the board's slams.
struct MicProfile {
    float high_cut{120};    // Hz: little below this (a small capsule)
    float low_cut{9000};    // Hz: rolled off above this
    float target{0.25f};    // the level the level control aims for (of full scale)
    float most_gain{3};     // the most it turns a quiet room up
    float attack{0.01f};    // seconds to duck a slam
    float release{0.4f};    // seconds to bring the room back up
    float drive{1.2f};      // how hard it pushes into the converter (loud hits round off)
    bool operator==(const MicProfile &) const = default;
};

// The camera's mic: its filters and level control. Filters and the level control keep their state
// across packets.
class VxMic {
public:
    explicit VxMic(MicProfile mic = {}) : mic_(mic) {}
    // Interleaved stereo floats (full scale 1), in place.
    void process(float *interleaved, std::size_t frames) noexcept;
    void process(std::int16_t *interleaved, std::size_t frames) noexcept;

private:
    struct Channel {
        float high_in{}, high_out{}; // high-pass
        float low1{}, low2{};        // two-pole low-pass
    };
    MicProfile mic_;
    Channel channels_[audio_channels];
    float level_{};    // the level control's envelope (of a smoothed level, not each sample)
    float detect_{};   // that smoothed level
    float gain_{1};    // the gain it sets, itself smoothed: a gain that moves within a wave distorts it
};
// Where the skater is from the camera, for the sound: the game mixes its sound for its own
// listener, near the skater, so a camera across the spot hears it as if it stood there.
struct EarPlacement {
    float distance{}; // metres from the camera to the skater
    float side{};     // -1 hard left of the picture, 1 hard right
    bool operator==(const EarPlacement &) const = default;
};
// `camera` a camera matrix (camera_math.h: right, up, back, position), `skater` a position.
EarPlacement ear_placement(const std::array<float, 16> &camera, const std::array<float, 3> &skater) noexcept;

// The game's sound as the camera would hear it: further off it is quieter and duller (the air takes
// the highs), and it comes from the skater's side of the picture. Close to the skater (handheld) it
// leaves the sound alone. Changes glide, so a cut between tripods or a fast pan doesn't click.
// How far sound carries to the camera (a handling.json's "sound"; the defaults are as built).
struct EarSettings {
    float near{3};      // metres: closer than this, the game's sound as it is
    float floor{0.12f}; // the quietest a far-off skater gets (of their level close by)
    float air{10};      // metres beyond `near` over which the top end halves (the air takes the highs)
    float pan{0.7f};    // how far a far-off skater's sound moves to their side of the picture, 0 to 1
    bool operator==(const EarSettings &) const = default;
};

class CameraEar {
public:
    explicit CameraEar(EarSettings settings = {}) : settings_(settings) {}
    // Interleaved stereo floats, in place, heard from `where`.
    void process(float *interleaved, std::size_t frames, EarPlacement where) noexcept;
    // What it does at a distance, at rest (for tests and the docs): the level and the top it keeps.
    float gain_at(float distance) const noexcept;
    float cutoff_at(float distance) const noexcept;     // Hz
    float pan_amount_at(float distance) const noexcept; // 0: the game's stereo, 1: all re-panned

private:
    EarSettings settings_;
    bool started_{};
    float gain_{1}, cutoff_{20000}, side_{}, amount_{};
    float low_[audio_channels]{};
};
} // namespace dingosdk::camcorder
