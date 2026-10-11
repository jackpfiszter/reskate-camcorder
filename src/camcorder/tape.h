#pragma once
// The tape side of the camcorder: record and standby, the counter, the MiniDV cassette and the
// battery, and the list of clips a session has shot.
//
// The VX1000 records NTSC DV at 30000/1001 frames a second, timecoded drop frame
// (HH:MM:SS;FF), onto MiniDV: 60 minutes a cassette in SP. Its viewfinder counts H:MM:SS.
// Running out of tape or battery stops a recording, as it did on the real thing; both can be
// switched off (Deck::Settings).
#include <cstdint>
#include <string>
#include <vector>

namespace dingosdk::camcorder {
inline constexpr double ntsc_frame_rate = 30000.0 / 1001.0;

struct Timecode {
    int hours{}, minutes{}, seconds{}, frames{};
    bool operator==(const Timecode &) const = default;
};
// SMPTE 29.97 drop frame: frame numbers ;00 and ;01 are skipped at the start of every minute
// except each tenth, so the timecode keeps time with the clock.
Timecode drop_frame_timecode(std::int64_t frame) noexcept;
std::string format_timecode(const Timecode &);  // "00:01:00;02"
std::string format_counter(double seconds);     // the viewfinder's "0:01:00"

enum class Transport : std::uint8_t { standby, recording };

struct Clip {
    std::uint32_t number{};       // from 1, per cassette
    std::int64_t start_frame{};   // on the tape
    std::int64_t end_frame{};     // exclusive; equal to start while recording
    std::string label;            // what the shot was (the rig, the subject)
    double seconds() const { return static_cast<double>(end_frame - start_frame) / ntsc_frame_rate; }
};

class Deck {
public:
    struct Settings {
        double tape_seconds = 60 * 60;      // a 60-minute MiniDV cassette, SP
        double battery_seconds = 100 * 60;  // a large NP-F battery, roughly
        bool finite_tape = true;
        bool finite_battery = false;        // off by default: a flat battery is rarely fun
    };
    Deck() = default;
    explicit Deck(Settings settings) : settings_(settings) {}

    // Starts recording at the end of what is on the tape. False (and why) when it cannot.
    bool record(std::string label, std::string *reason = nullptr);
    void stop();
    // Advances the tape while recording and drains the battery while powered.
    void tick(double dt, bool powered);
    // A fresh cassette: back to the start, no clips.
    void new_tape();
    void charge() { battery_used_ = 0; }

    Transport transport() const { return transport_; }
    bool recording() const { return transport_ == Transport::recording; }
    std::int64_t frame() const { return frame_; }                      // the tape's write head
    double tape_seconds() const { return static_cast<double>(frame_) / ntsc_frame_rate; }
    double tape_remaining() const;                                      // seconds
    double battery_fraction() const;                                    // 1 full, 0 flat
    const std::vector<Clip> &clips() const { return clips_; }
    // The clip being recorded, or the last one.
    const Clip *current_clip() const { return clips_.empty() ? nullptr : &clips_.back(); }
    // Set when the deck stopped by itself; cleared by the next record().
    const std::string &stopped_because() const { return stopped_; }
    Settings &settings() { return settings_; }
    const Settings &settings() const { return settings_; }

private:
    Settings settings_;
    Transport transport_{Transport::standby};
    std::int64_t frame_{};
    double fraction_{}; // the part of a frame recorded so far
    double battery_used_{};
    std::vector<Clip> clips_;
    std::string stopped_;
    std::int64_t tape_frames() const;
};
} // namespace dingosdk::camcorder
