#pragma once
// Writes the camcorder's clips to MP4 with Windows' own H.264 encoder (Media Foundation), on a thread
// of its own: the render thread only hands it the picture's pixels as they came off the GPU, and
// the converting (camcorder/recorder.h) and encoding happen here.
#include "camcorder/audio.h"
#include "camcorder/look.h"
#include "camcorder/recorder.h"
#include <ctime>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dingosdk::camcorder_video {
// One picture as copied off the GPU: rows `pitch` bytes apart, in the back buffer's format.
struct RawFrame {
    std::vector<std::uint8_t> bytes;
    std::size_t pitch{};
    int width{}, height{};
    camcorder::SourceFormat format{};
    float white_nits{200};
    float vignette{};  // the fisheye housing's shadow, camcorder::Optics::vignette_radius
    int repeats{1};    // how many video frames it becomes (camcorder::FramePacer)
    std::int64_t index{}; // the first of them, from 0 at the clip's start (FramePacer::first)
};

struct Result {
    bool ok{};
    std::wstring path;   // the file
    std::int64_t frames{};
    std::string error;   // why not, when not ok
    std::string warning; // saved, but something is missing (the sound, and why)
};

class Writer {
public:
    Writer();
    ~Writer();
    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;
    // Opens `path` for a clip of `size` and starts the encoding thread, and the game's sound unless
    // `sound` is off. `origin_seconds` is the clip's start on the steady clock (the pacer's) and
    // `started` on the wall clock (for the date stamp); `finish` is the camera's look, motion and
    // stamp (the clip runs at clip_rate(finish)). False and why when it cannot.
    bool start(const std::wstring &path, camcorder::VideoSize size, camcorder::Sound sound, double origin_seconds,
               const camcorder::Finish &finish, std::time_t started, std::string &error);
    // Queues a picture; drops it (and counts it) when the encoder is too far behind.
    void push(RawFrame frame);
    // No more pictures: the thread encodes what is queued, closes the file and leaves a Result.
    void finish();
    bool running() const;
    std::optional<Result> take_result();
    std::uint64_t dropped() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The folder clips go to: Videos\ReSkate Camcorder (created when missing). Empty when Windows will
// not say where Videos is.
std::wstring clip_folder();
} // namespace dingosdk::camcorder_video
