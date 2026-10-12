#include "camcorder_tape.h"
#include "camcorder_media.h"
#include "camcorder/audio.h"
#include <mfapi.h>
#include <propidl.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

namespace dingosdk::camcorder_tape {
namespace {
using Microsoft::WRL::ComPtr;
using namespace dingosdk::camcorder;
using camcorder_media::frame_duration;

ComPtr<IMFSourceReader> open_clip(const std::wstring &path) {
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFSourceReader> reader;
    // Lets the reader scale and convert to what we ask for.
    if (FAILED(MFCreateAttributes(&attributes, 1)) ||
        FAILED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE)) ||
        FAILED(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader)))
        return {};
    return reader;
}
bool clip_size(IMFSourceReader *reader, VideoSize &size) {
    ComPtr<IMFMediaType> type;
    UINT32 width{}, height{};
    if (FAILED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &type)) ||
        FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height)) || !width || !height)
        return false;
    size = {static_cast<int>(width) & ~1, static_cast<int>(height) & ~1};
    return true;
}
bool has_sound(IMFSourceReader *reader) {
    ComPtr<IMFMediaType> type;
    return SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, &type));
}
LONGLONG duration(IMFSourceReader *reader) {
    PROPVARIANT value;
    PropVariantInit(&value);
    LONGLONG length = 0;
    if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value)))
        length = static_cast<LONGLONG>(value.uhVal.QuadPart);
    PropVariantClear(&value);
    return length;
}
// Copies a decoded NV12 picture into `out` packed (the decoder's rows and planes can be padded).
bool packed_nv12(IMFSample *sample, VideoSize size, std::vector<std::uint8_t> &out) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->GetBufferByIndex(0, &buffer))) return false;
    const auto w = static_cast<std::size_t>(size.width), h = static_cast<std::size_t>(size.height);
    out.resize(w * h * 3 / 2);
    ComPtr<IMF2DBuffer2> planes;
    if (SUCCEEDED(buffer.As(&planes))) {
        BYTE *scan0{}, *start{};
        LONG pitch{};
        DWORD length{};
        if (FAILED(planes->Lock2DSize(MF2DBuffer_LockFlags_Read, &scan0, &pitch, &start, &length))) return false;
        const auto row = static_cast<std::size_t>(pitch > 0 ? pitch : -pitch);
        // The chroma plane follows the luma plane's padded height.
        const std::size_t plane_rows = row ? std::max(h, static_cast<std::size_t>(length) * 2 / 3 / row) : h;
        const bool fits = pitch > 0 && row >= w && static_cast<std::size_t>(length) >= row * (plane_rows + h / 2);
        if (fits) {
            for (std::size_t y = 0; y < h; ++y) std::memcpy(out.data() + y * w, scan0 + y * row, w);
            for (std::size_t y = 0; y < h / 2; ++y)
                std::memcpy(out.data() + w * h + y * w, scan0 + (plane_rows + y) * row, w);
        }
        planes->Unlock2D();
        return fits;
    }
    BYTE *data{};
    DWORD length{};
    if (FAILED(buffer->Lock(&data, nullptr, &length))) return false;
    const bool fits = length >= out.size();
    if (fits) std::memcpy(out.data(), data, out.size());
    buffer->Unlock();
    return fits;
}
} // namespace

struct Capture::Impl {
    mutable std::mutex mutex;
    std::thread thread;
    bool running{};
    std::optional<Result> result;

    void run(std::vector<std::wstring> clips, std::wstring path) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Result done;
        done.path = path;
        const auto skip = [&](const std::wstring &clip, const char *why) {
            done.warning += (done.warning.empty() ? "Left out: " : "; ") + camcorder_media::narrow(clip) + " (" + why + ")";
        };
        // The first readable clip decides the size; any clip with sound gives the tape a sound track.
        VideoSize size{};
        bool sound = false;
        for (const auto &clip : clips)
            if (auto reader = open_clip(clip)) {
                if (!size.width) clip_size(reader.Get(), size);
                sound = sound || has_sound(reader.Get());
            }
        ComPtr<IMFSinkWriter> writer;
        DWORD video_stream{}, audio_stream{};
        HRESULT hr = !camcorder_media::available() ? E_FAIL : size.width ? S_OK : E_INVALIDARG;
        if (SUCCEEDED(hr)) hr = camcorder_media::open_mp4(path, size, sound, writer, video_stream, audio_stream);
        if (FAILED(hr)) {
            done.error = size.width ? camcorder_media::failed("Opening the tape file", hr) : "None of the clips could be read.";
        } else {
            LONGLONG offset = 0;
            std::vector<std::uint8_t> nv12;
            for (const auto &clip : clips) {
                auto reader = open_clip(clip);
                if (!reader) {
                    skip(clip, "cannot be read");
                    continue;
                }
                const auto video = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
                const auto audio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
                // Ask for NV12 at the tape's size, and 48 kHz 16-bit stereo.
                ComPtr<IMFMediaType> picture, pcm;
                HRESULT r = MFCreateMediaType(&picture);
                if (SUCCEEDED(r)) r = picture->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                if (SUCCEEDED(r)) r = picture->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
                if (SUCCEEDED(r)) r = MFSetAttributeSize(picture.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(size.width), static_cast<UINT32>(size.height));
                if (SUCCEEDED(r)) r = reader->SetCurrentMediaType(video, nullptr, picture.Get());
                if (FAILED(r)) {
                    skip(clip, "its picture cannot be converted");
                    continue;
                }
                bool clip_sound = false;
                if (sound && has_sound(reader.Get())) {
                    r = MFCreateMediaType(&pcm);
                    if (SUCCEEDED(r)) r = pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                    if (SUCCEEDED(r)) r = pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                    if (SUCCEEDED(r)) r = pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                    if (SUCCEEDED(r)) r = pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_rate);
                    if (SUCCEEDED(r)) r = pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels);
                    if (SUCCEEDED(r)) r = reader->SetCurrentMediaType(audio, nullptr, pcm.Get());
                    clip_sound = SUCCEEDED(r);
                }
                const LONGLONG length = duration(reader.Get());
                LONGLONG end = 0; // where this clip's picture ends, on the tape
                bool broken = false;
                // The picture, then the sound: the writer interleaves them by time.
                for (bool reading = true; reading && !broken;) {
                    DWORD flags{};
                    LONGLONG time{};
                    ComPtr<IMFSample> sample;
                    if (FAILED(reader->ReadSample(video, 0, nullptr, &flags, &time, &sample))) { broken = true; break; }
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) reading = false;
                    if (!sample) continue;
                    LONGLONG span{};
                    if (FAILED(sample->GetSampleDuration(&span)) || span <= 0) span = frame_duration;
                    if (!packed_nv12(sample.Get(), size, nv12)) { broken = true; break; }
                    if (FAILED(camcorder_media::write_sample(writer.Get(), video_stream, nv12.data(), nv12.size(),
                                                              offset + time, span))) { broken = true; break; }
                    end = std::max(end, time + span);
                }
                if (broken) {
                    skip(clip, "it stopped decoding part way");
                    hr = E_FAIL;
                    break; // the tape's timeline is no longer whole
                }
                const LONGLONG clip_length = std::max(end, length);
                if (sound) {
                    LONGLONG written = 0;
                    if (clip_sound) {
                        for (bool reading = true; reading;) {
                            DWORD flags{};
                            LONGLONG time{};
                            ComPtr<IMFSample> sample;
                            if (FAILED(reader->ReadSample(audio, 0, nullptr, &flags, &time, &sample))) break;
                            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) reading = false;
                            if (!sample) continue;
                            ComPtr<IMFMediaBuffer> buffer;
                            BYTE *data{};
                            DWORD bytes{};
                            if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || FAILED(buffer->Lock(&data, nullptr, &bytes))) break;
                            const LONGLONG frames = bytes / (audio_channels * 2);
                            const LONGLONG span = frames * 10'000'000LL / audio_rate;
                            // Sound past the picture's end would push the next clip's sound late.
                            if (time < clip_length)
                                camcorder_media::write_sample(writer.Get(), audio_stream, data, bytes, offset + time, span);
                            buffer->Unlock();
                            written = std::max(written, time + span);
                        }
                    }
                    // Silence for the rest of the clip (all of it when the clip had no sound).
                    std::vector<std::int16_t> quiet(static_cast<std::size_t>(audio_rate) * audio_channels / 10, 0); // 100 ms
                    const LONGLONG step = 1'000'000; // 100 ms
                    for (LONGLONG at = written; at < clip_length; at += step)
                        camcorder_media::write_sample(writer.Get(), audio_stream, quiet.data(), quiet.size() * sizeof(std::int16_t),
                                                      offset + at, std::min(step, clip_length - at));
                }
                offset += clip_length;
                ++done.clips;
            }
            if (SUCCEEDED(hr) && done.clips) {
                hr = writer->Finalize();
                done.ok = SUCCEEDED(hr);
                if (FAILED(hr)) done.error = camcorder_media::failed("Closing the tape file", hr);
            } else if (done.error.empty()) {
                done.error = done.clips ? "A clip stopped decoding part way." : "None of the clips could be read.";
            }
            writer.Reset();
            if (!done.ok) DeleteFileW(path.c_str());
        }
        if (SUCCEEDED(com)) CoUninitialize();
        std::lock_guard lock(mutex);
        result = std::move(done);
        running = false;
    }
};

Capture::Capture() : impl_(std::make_unique<Impl>()) {}

Capture::~Capture() {
    if (impl_->thread.joinable()) impl_->thread.join();
}

bool Capture::start(std::vector<std::wstring> clips, std::wstring path) {
    auto &s = *impl_;
    {
        std::lock_guard lock(s.mutex);
        if (s.running) return false;
        s.running = true;
        s.result.reset();
    }
    if (s.thread.joinable()) s.thread.join();
    s.thread = std::thread([&s, clips = std::move(clips), path = std::move(path)]() mutable {
        s.run(std::move(clips), std::move(path));
    });
    return true;
}

bool Capture::running() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->running;
}

std::optional<Result> Capture::take_result() {
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->result, std::nullopt);
}
} // namespace dingosdk::camcorder_tape
