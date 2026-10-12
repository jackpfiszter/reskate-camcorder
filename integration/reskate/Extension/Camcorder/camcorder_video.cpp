#include "camcorder_video.h"
#include "camcorder_audio.h"
#include <Windows.h>
#include <ShlObj.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace dingosdk::camcorder_video {
namespace {
using Microsoft::WRL::ComPtr;
using namespace dingosdk::camcorder;
constexpr std::size_t queue_limit = 8; // about a quarter of a second of pictures
// 29.97 frames a second, in Media Foundation's 100-nanosecond units.
constexpr LONGLONG frame_duration = 10'000'000LL * 1001 / 30000;

bool media_foundation() {
    static const bool started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    return started;
}
std::string hresult(const char *what, HRESULT result) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s failed (0x%08lx).", what, static_cast<unsigned long>(result));
    return text;
}
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

    // Writes interleaved 16-bit stereo at the end of the sound written so far.
    HRESULT write_audio(const std::vector<std::int16_t> &pcm) {
        if (pcm.empty()) return S_OK;
        const auto frames = static_cast<std::int64_t>(pcm.size() / audio_channels);
        const auto bytes = static_cast<DWORD>(pcm.size() * sizeof(std::int16_t));
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        BYTE *data{};
        HRESULT hr = MFCreateMemoryBuffer(bytes, &buffer);
        if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
        if (SUCCEEDED(hr)) {
            std::memcpy(data, pcm.data(), bytes);
            buffer->Unlock();
            hr = buffer->SetCurrentLength(bytes);
        }
        if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(audio_frames * 10'000'000LL / audio_rate);
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(frames * 10'000'000LL / audio_rate);
        if (SUCCEEDED(hr)) hr = writer->WriteSample(audio_stream, sample.Get());
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
            bgra_to_nv12(scaled, nv12);
            for (int i = 0; i < frame->repeats && !failed; ++i) {
                ComPtr<IMFMediaBuffer> buffer;
                ComPtr<IMFSample> sample;
                BYTE *data{};
                HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(nv12.size()), &buffer);
                if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
                if (SUCCEEDED(hr)) {
                    std::memcpy(data, nv12.data(), nv12.size());
                    buffer->Unlock();
                    hr = buffer->SetCurrentLength(static_cast<DWORD>(nv12.size()));
                }
                // On the clip's clock: after a stall the picture skips ahead with the sound.
                const auto index = frame->index + i;
                if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
                if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
                if (SUCCEEDED(hr)) hr = sample->SetSampleTime(index * frame_duration);
                if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(frame_duration);
                if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample.Get());
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
                const auto end = video_frames * frame_duration * audio_rate / 10'000'000LL;
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

bool Writer::start(const std::wstring &path, VideoSize size, Sound sound, double origin_seconds, std::string &error) {
    auto &s = *impl_;
    if (running()) {
        error = "A clip is still being written.";
        return false;
    }
    if (s.thread.joinable()) s.thread.join();
    if (!media_foundation()) {
        error = "Windows' video encoder (Media Foundation) is unavailable. On Windows N, install the Media Feature Pack.";
        return false;
    }
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFSinkWriter> writer;
    ComPtr<IMFMediaType> out, in;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes.Get(), &writer);
    if (FAILED(hr)) {
        error = hresult("Creating the video file", hr);
        return false;
    }
    const auto describe = [&](IMFMediaType *type, const GUID &subtype) {
        HRESULT r = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(r)) r = type->SetGUID(MF_MT_SUBTYPE, subtype);
        if (SUCCEEDED(r)) r = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(r)) r = MFSetAttributeSize(type, MF_MT_FRAME_SIZE, static_cast<UINT32>(size.width), static_cast<UINT32>(size.height));
        if (SUCCEEDED(r)) r = MFSetAttributeRatio(type, MF_MT_FRAME_RATE, 30000, 1001);
        if (SUCCEEDED(r)) r = MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        return r;
    };
    hr = MFCreateMediaType(&out);
    if (SUCCEEDED(hr)) hr = describe(out.Get(), MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = out->SetUINT32(MF_MT_AVG_BITRATE, video_bitrate(size));
    if (SUCCEEDED(hr)) hr = writer->AddStream(out.Get(), &s.stream);
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&in);
    if (SUCCEEDED(hr)) hr = describe(in.Get(), MFVideoFormat_NV12);
    if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(size.width));
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(s.stream, in.Get(), nullptr);
    // The sound: AAC at 192 kbit/s from 48 kHz 16-bit stereo.
    const bool with_sound = sound != Sound::off;
    if (SUCCEEDED(hr) && with_sound) {
        ComPtr<IMFMediaType> audio_out, audio_in;
        hr = MFCreateMediaType(&audio_out);
        if (SUCCEEDED(hr)) hr = audio_out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = audio_out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_rate);
        if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels);
        if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        if (SUCCEEDED(hr)) hr = writer->AddStream(audio_out.Get(), &s.audio_stream);
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&audio_in);
        if (SUCCEEDED(hr)) hr = audio_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = audio_in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_rate);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, audio_channels * 2);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audio_rate * audio_channels * 2);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(s.audio_stream, audio_in.Get(), nullptr);
    }
    if (SUCCEEDED(hr)) hr = writer->BeginWriting();
    if (FAILED(hr)) {
        error = hresult("Setting up the H.264 encoder", hr);
        writer.Reset();
        DeleteFileW(path.c_str());
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
    }
    if (with_sound) s.audio.start(origin_seconds, sound);
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

std::wstring clip_folder() {
    PWSTR videos{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr, &videos)) || !videos) {
        if (videos) CoTaskMemFree(videos);
        return {};
    }
    std::wstring folder = std::wstring(videos) + L"\\ReSkate Camcorder";
    CoTaskMemFree(videos);
    CreateDirectoryW(folder.c_str(), nullptr); // already there is fine
    return folder;
}
} // namespace dingosdk::camcorder_video
