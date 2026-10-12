#pragma once
// The clip's sound: the game's own audio and nothing else on the PC (Windows' per-process loopback,
// Windows 10 2004 or later), 48 kHz 16-bit stereo, put on the clip's clock (camcorder/audio.h) as
// it arrives. Runs on a thread of its own, so starting it never holds up the game.
#include "camcorder/audio.h"
#include <memory>
#include <string>
#include <vector>

namespace dingosdk::camcorder_audio {
class Capture {
public:
    Capture();
    ~Capture();
    Capture(const Capture &) = delete;
    Capture &operator=(const Capture &) = delete;
    // Starts capturing for a clip that starts at `origin_seconds` (the steady clock, as the picture's
    // pacer). Returns at once; whether it worked comes later through error().
    // With Sound::vx_mic the sound goes through `mic`, the camera's own (its profile's).
    void start(double origin_seconds, camcorder::Sound sound, camcorder::MicProfile mic = {});
    // What has been captured since the last call, interleaved stereo, on the clip's clock.
    std::vector<std::int16_t> take();
    // Stops capturing; what was captured can still be taken.
    void stop();
    // Why there is no sound, once that is known; empty while it works (or has not failed yet).
    std::string error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dingosdk::camcorder_audio
