#include "camcorder_tape.h"
#include "camcorder_media.h"
#include "camcorder/audio.h"
#include <mfapi.h>
#include <propidl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <mutex>
#include <thread>
#include <utility>

namespace dingosdk::camcorder_tape {
namespace {
using Microsoft::WRL::ComPtr;
using namespace dingosdk::camcorder;

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
// A clip's frame rate (its camera's, look.h), 29.97 when it does not say.
FrameRate reader_rate(IMFSourceReader *reader) {
    ComPtr<IMFMediaType> type;
    UINT32 numerator{}, denominator{};
    if (SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &type)) &&
        SUCCEEDED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) && numerator && denominator) {
        // An MP4 can say 30000/1001 as 10000000/333667: brought to lowest terms, or the nearest
        // standard rate when that is still too big.
        const UINT32 common = std::gcd(numerator, denominator);
        numerator /= common;
        denominator /= common;
        if (numerator <= 1'000'000 && denominator <= 1'000'000) return {static_cast<int>(numerator), static_cast<int>(denominator)};
        const double fps = static_cast<double>(numerator) / denominator;
        for (const FrameRate standard : {FrameRate{60000, 1001}, FrameRate{50, 1}, FrameRate{30000, 1001}, FrameRate{25, 1},
                                          FrameRate{24000, 1001}, FrameRate{18, 1}})
            if (std::abs(fps - static_cast<double>(standard.numerator) / standard.denominator) < 0.05) return standard;
    }
    return {};
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
        FrameRate rate;
        const bool media = camcorder_media::available(); // starts Media Foundation, before any clip is opened
        for (const auto &clip : clips)
            if (auto reader = media ? open_clip(clip) : ComPtr<IMFSourceReader>{}) {
                if (!size.width && clip_size(reader.Get(), size)) rate = reader_rate(reader.Get());
                sound = sound || has_sound(reader.Get());
            }
        ComPtr<IMFSinkWriter> writer;
        DWORD video_stream{}, audio_stream{};
        HRESULT hr = !media ? E_FAIL : size.width ? S_OK : E_INVALIDARG;
        if (SUCCEEDED(hr)) hr = camcorder_media::open_mp4(path, size, sound, rate, writer, video_stream, audio_stream);
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
                // A clip from a camera of another shape would be stretched to the tape's: left out.
                VideoSize own{};
                if (clip_size(reader.Get(), own) &&
                    std::abs(static_cast<double>(own.width) * size.height - static_cast<double>(size.width) * own.height) >
                        0.02 * static_cast<double>(size.width) * own.height) {
                    skip(clip, "a camera of another shape filmed it");
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
                // Both streams together, in the file's order: the writer interleaves them by time
                // and holds the picture back when the sound falls behind (reading all of one first
                // stalled it). Clips with no sound give silence as the picture goes.
                reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
                reader->SetStreamSelection(video, TRUE);
                if (clip_sound) reader->SetStreamSelection(audio, TRUE);
                // The sound written for this clip, in sample frames: times are made from the count,
                // as the encoder counts them, so the next clip's sound is not pushed late.
                LONGLONG frames_written = 0;
                const auto frames_at = [](LONGLONG time) { return std::max<LONGLONG>(time, 0) * audio_rate / 10'000'000LL; };
                const auto time_of = [](LONGLONG frames) { return frames * 10'000'000LL / audio_rate; };
                std::vector<std::int16_t> quiet(static_cast<std::size_t>(audio_rate) * audio_channels / 10, 0); // 100 ms
                const auto silence_to = [&](LONGLONG time) {
                    const LONGLONG until = frames_at(time);
                    while (frames_written < until) {
                        const LONGLONG n = std::min<LONGLONG>(until - frames_written, static_cast<LONGLONG>(quiet.size()) / audio_channels);
                        if (FAILED(camcorder_media::write_sample(writer.Get(), audio_stream, quiet.data(),
                                                                  static_cast<std::size_t>(n) * audio_channels * sizeof(std::int16_t),
                                                                  offset + time_of(frames_written), time_of(frames_written + n) - time_of(frames_written))))
                            return false;
                        frames_written += n;
                    }
                    return true;
                };
                // Sound is kept to the clip's length: past it, it would run into the next clip.
                const LONGLONG sound_limit = length > 0 ? frames_at(length) : std::numeric_limits<LONGLONG>::max();
                LONGLONG end = 0; // where this clip's picture ends, on the tape
                bool broken = false, video_done = false, audio_done = !clip_sound;
                while (!broken && (!video_done || !audio_done)) {
                    DWORD index{}, flags{};
                    LONGLONG time{};
                    ComPtr<IMFSample> sample;
                    if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &index, &flags, &time, &sample))) {
                        broken = true;
                        break;
                    }
                    ComPtr<IMFMediaType> type;
                    GUID major{};
                    const bool is_video = SUCCEEDED(reader->GetCurrentMediaType(index, &type)) &&
                                          SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) && major == MFMediaType_Video;
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) (is_video ? video_done : audio_done) = true;
                    if (flags & MF_SOURCE_READERF_ERROR) broken = true;
                    if (!sample || broken) continue;
                    if (is_video) {
                        LONGLONG span{};
                        if (FAILED(sample->GetSampleDuration(&span)) || span <= 0) span = camcorder_media::frame_time(1, rate);
                        if (!packed_nv12(sample.Get(), size, nv12) ||
                            FAILED(camcorder_media::write_sample(writer.Get(), video_stream, nv12.data(), nv12.size(), offset + time, span))) {
                            broken = true;
                            break;
                        }
                        end = std::max(end, time + span);
                        // Silence alongside the picture once the clip's own sound is over (or it had none),
                        // so the writer always has sound up to the picture.
                        if (sound && (!clip_sound || audio_done) && !silence_to(std::min(time + span, length > 0 ? std::max(length, time + span) : time + span))) broken = true;
                        continue;
                    }
                    // Sound: packets follow on from each other. Their times are rounded (a packet's start
                    // lands a sample either side of where the last ended), so within a few ms a packet
                    // is taken as following straight on: filling or cutting that one sample each
                    // packet ticked, 47 times a second. Only a real gap is filled with silence.
                    constexpr LONGLONG tolerance = audio_rate / 200; // 5 ms
                    const LONGLONG start = frames_at(time);
                    if (start - frames_written > tolerance && !silence_to(time)) {
                        broken = true;
                        break;
                    }
                    ComPtr<IMFMediaBuffer> buffer;
                    BYTE *data{};
                    DWORD bytes{};
                    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || FAILED(buffer->Lock(&data, nullptr, &bytes))) continue;
                    const LONGLONG frames = bytes / (audio_channels * 2);
                    // A real overlap (more than the rounding) is skipped, so nothing plays twice.
                    const LONGLONG skip_frames = frames_written - start > tolerance ? std::clamp<LONGLONG>(frames_written - start, 0, frames) : 0;
                    const LONGLONG n = std::clamp<LONGLONG>(std::min(frames - skip_frames, sound_limit - frames_written), 0, frames);
                    if (n > 0) {
                        if (FAILED(camcorder_media::write_sample(writer.Get(), audio_stream, data + skip_frames * audio_channels * 2,
                                                                  static_cast<std::size_t>(n) * audio_channels * 2, offset + time_of(frames_written),
                                                                  time_of(frames_written + n) - time_of(frames_written))))
                            broken = true;
                        else
                            frames_written += n;
                    }
                    buffer->Unlock();
                    if (frames_written >= sound_limit) {
                        audio_done = true;
                        reader->SetStreamSelection(audio, FALSE); // the rest would be cut anyway
                    }
                }
                if (broken) {
                    skip(clip, "it stopped decoding part way");
                    hr = E_FAIL;
                    break; // the tape's timeline is no longer whole
                }
                const LONGLONG clip_length = std::max(end, length);
                // Silence for the rest of the clip (all of it when the clip had no sound).
                if (sound && !silence_to(clip_length)) {
                    skip(clip, "its sound could not be written");
                    hr = E_FAIL;
                    break;
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
