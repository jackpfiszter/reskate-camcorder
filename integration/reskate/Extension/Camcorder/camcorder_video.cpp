#include "camcorder_video.h"
#include "camcorder_audio.h"
#include "camcorder_media.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace dingosdk::camcorder_video {
namespace {
using Microsoft::WRL::ComPtr;
using namespace dingosdk::camcorder;
using camcorder_media::frame_time;
constexpr std::size_t queue_limit = 8; // about a quarter of a second of pictures
std::string hresult(const char *what, HRESULT result) { return camcorder_media::failed(what, result); }
} // namespace

struct Writer::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<RawFrame> queue;
    bool finishing{}, running{};
    std::optional<Result> result;
    std::atomic<std::uint64_t> dropped{};
    std::thread thread;
    // Encoder thread only.
    ComPtr<IMFSinkWriter> writer;
    DWORD stream{}, audio_stream{};
    bool sound{};
    camcorder_audio::Capture audio;
    std::int64_t audio_frames{}, video_frames{};
    VideoSize size;
    std::wstring path;
    Finish finish;
    DvClip dv_clip;
    std::time_t started{};

    // The wall-clock time of frame `index`, for the date stamp: it ticks over with the clip.
    std::tm when(std::int64_t index) const {
        const auto time = started + static_cast<std::time_t>(static_cast<double>(index) / clip_rate(finish).fps());
        std::tm local{};
        localtime_s(&local, &time);
        return local;
    }

    // Writes interleaved 16-bit stereo at the end of the sound written so far.
    HRESULT write_audio(const std::vector<std::int16_t> &pcm) {
        if (pcm.empty()) return S_OK;
        const auto frames = static_cast<std::int64_t>(pcm.size() / audio_channels);
        const HRESULT hr = camcorder_media::write_sample(writer.Get(), audio_stream, pcm.data(),
                                                         pcm.size() * sizeof(std::int16_t),
                                                         audio_frames * 10'000'000LL / audio_rate,
                                                         frames * 10'000'000LL / audio_rate);
        if (SUCCEEDED(hr)) audio_frames += frames;
        return hr;
    }

    void run() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Result done;
        done.path = path;
        Image picture, scaled;
        scaled.resize(size.width, size.height);
        std::vector<std::uint8_t> nv12;
        bool failed = false;
        const auto fail = [&](const char *what, HRESULT hr) {
            failed = true;
            done.error = hresult(what, hr);
        };
        for (;;) {
            std::optional<RawFrame> frame;
            bool last = false;
            {
                std::unique_lock lock(mutex);
                // Wakes for a picture, or every 30 ms to write the sound that came in meanwhile.
                wake.wait_for(lock, std::chrono::milliseconds(30), [&] { return !queue.empty() || finishing; });
                if (!queue.empty()) {
                    frame = std::move(queue.front());
                    queue.pop_front();
                } else if (finishing) {
                    last = true;
                }
            }
            if (sound && !failed)
                if (const HRESULT hr = write_audio(audio.take()); FAILED(hr)) fail("Encoding the sound", hr);
            if (last) break;
            if (!frame || failed) continue;
            convert_to_bgra(frame->bytes.data(), frame->pitch, frame->width, frame->height, frame->format, picture,
                            frame->white_nits);
            scale_bgra(picture, scaled);
            apply_vignette(scaled, frame->vignette);
            const auto rate = clip_rate(finish);
            // Only what the screen did not already do (recorded_finish): the fields, at least.
            const auto left = recorded_finish(finish, frame->look_on_screen, frame->stamp_on_screen);
            const bool styled = left.fields() || (left.dv.on && (left.dv.highlights || left.dv.white_balance || left.dv.sharpen ||
                                                                left.dv.chroma || left.dv.grain)) ||
                                left.date_stamp;
            if (!styled) bgra_to_nv12(scaled, nv12);
            Image finished;
            for (int i = 0; i < frame->repeats && !failed; ++i) {
                // On the clip's clock: after a stall the picture skips ahead with the sound.
                const auto index = frame->index + i;
                if (styled) {
                    // Each repeat is its own frame of tape: the other field, the next second of the stamp.
                    finished = scaled;
                    finish_frame(finished, left, dv_clip, index, rate.fps(), when(index));
                    bgra_to_nv12(finished, nv12);
                }
                const auto at = frame_time(index, rate);
                const HRESULT hr = camcorder_media::write_sample(writer.Get(), stream, nv12.data(), nv12.size(), at,
                                                                 frame_time(index + 1, rate) - at);
                if (FAILED(hr)) {
                    fail("Encoding a frame", hr);
                } else {
                    ++done.frames;
                    video_frames = std::max(video_frames, index + 1);
                }
            }
        }
        if (sound) {
            audio.stop();
            if (!failed) {
                auto rest = audio.take();
                // The sound runs to the end of the picture (all silence when it could not be captured).
                const auto end = frame_time(video_frames, clip_rate(finish)) * audio_rate / 10'000'000LL;
                const auto have = audio_frames + static_cast<std::int64_t>(rest.size() / audio_channels);
                if (have < end) rest.resize(rest.size() + static_cast<std::size_t>(end - have) * audio_channels, 0);
                if (const HRESULT hr = write_audio(rest); FAILED(hr)) fail("Encoding the sound", hr);
            }
            if (const auto why = audio.error(); !why.empty()) done.warning = "No sound: " + why;
        }
        if (!failed && done.frames > 0) {
            const HRESULT hr = writer->Finalize();
            if (FAILED(hr)) done.error = hresult("Closing the video file", hr);
            done.ok = SUCCEEDED(hr);
        } else if (!failed) {
            done.error = "The clip had no frames.";
        }
        writer.Reset();
        if (!done.ok) DeleteFileW(path.c_str()); // nothing worth keeping
        if (SUCCEEDED(com)) CoUninitialize();
        std::lock_guard lock(mutex);
        result = std::move(done);
        running = false;
    }
};

Writer::Writer() : impl_(std::make_unique<Impl>()) {}

Writer::~Writer() {
    finish();
    if (impl_->thread.joinable()) impl_->thread.join();
}

bool Writer::start(const std::wstring &path, VideoSize size, Sound sound, double origin_seconds, const Finish &finish,
                   std::time_t started, std::string &error) {
    auto &s = *impl_;
    if (running()) {
        error = "A clip is still being written.";
        return false;
    }
    if (s.thread.joinable()) s.thread.join();
    if (!camcorder_media::available()) {
        error = "Windows' video encoder (Media Foundation) is unavailable. On Windows N, install the Media Feature Pack.";
        return false;
    }
    const bool with_sound = sound != Sound::off;
    ComPtr<IMFSinkWriter> writer;
    if (const HRESULT hr = camcorder_media::open_mp4(path, size, with_sound, clip_rate(finish), writer, s.stream, s.audio_stream); FAILED(hr)) {
        error = hresult("Setting up the H.264 encoder", hr);
        return false;
    }
    {
        std::lock_guard lock(s.mutex);
        s.writer = std::move(writer);
        s.size = size;
        s.path = path;
        s.queue.clear();
        s.finishing = false;
        s.running = true;
        s.result.reset();
        s.dropped = 0;
        s.sound = with_sound;
        s.audio_frames = s.video_frames = 0;
        s.finish = finish;
        s.started = started;
        // Each clip's white balance starts somewhere of its own in its wander.
        s.dv_clip.seed = static_cast<float>(origin_seconds - std::floor(origin_seconds));
    }
    if (with_sound) s.audio.start(origin_seconds, sound, finish.mic);
    s.thread = std::thread([&s] { s.run(); });
    return true;
}

void Writer::push(RawFrame frame) {
    auto &s = *impl_;
    {
        std::lock_guard lock(s.mutex);
        if (!s.running || s.finishing) return;
        if (s.queue.size() >= queue_limit) {
            ++s.dropped;
            return;
        }
        s.queue.push_back(std::move(frame));
    }
    s.wake.notify_one();
}

void Writer::finish() {
    auto &s = *impl_;
    {
        std::lock_guard lock(s.mutex);
        if (!s.running) return;
        s.finishing = true;
    }
    s.wake.notify_one();
}

bool Writer::running() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->running;
}

std::optional<Result> Writer::take_result() {
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->result, std::nullopt);
}

std::uint64_t Writer::dropped() const { return impl_->dropped.load(); }

std::wstring clip_folder() { return camcorder_media::folder(FOLDERID_Videos); }
} // namespace dingosdk::camcorder_video
