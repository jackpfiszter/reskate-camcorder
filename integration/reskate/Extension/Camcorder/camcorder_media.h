#pragma once
// Windows media plumbing shared by the camcorder's clips, stills and tape captures: an MP4 with
// Windows' own encoders (Media Foundation: H.264 picture, AAC sound) and JPEG stills (WIC).
#include "camcorder/look.h"
#include "camcorder/recorder.h"
#include <Windows.h>
#include <ShlObj.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

namespace dingosdk::camcorder_media {
// When frame `index` of a clip at `rate` starts, in Media Foundation's 100-nanosecond units. Exact
// (no rounding builds up over a long clip).
inline LONGLONG frame_time(std::int64_t index, camcorder::FrameRate rate) {
    return index * 10'000'000LL * rate.denominator / rate.numerator;
}

// Starts Media Foundation once for the process. False where it is missing (Windows N without the
// Media Feature Pack).
bool available();
std::string failed(const char *what, HRESULT result);

// Opens `path` for writing: H.264 at `size` and `rate` from NV12 (stream `video`), and AAC
// 192 kbit/s from 48 kHz 16-bit stereo (stream `audio`) when `sound`. Ready for samples on success.
HRESULT open_mp4(const std::wstring &path, camcorder::VideoSize size, bool sound, camcorder::FrameRate rate,
                 Microsoft::WRL::ComPtr<IMFSinkWriter> &writer, DWORD &video, DWORD &audio);
// One sample of `bytes` at `time` for `duration` (100 ns units).
HRESULT write_sample(IMFSinkWriter *writer, DWORD stream, const void *bytes, std::size_t size, LONGLONG time,
                     LONGLONG duration);

// A still, as JPEG (quality 95).
bool save_jpeg(const camcorder::Image &image, const std::wstring &path, std::string &error);

// "<known folder>\ReSkate Camcorder", created when missing; empty when Windows will not say where.
std::wstring folder(const KNOWNFOLDERID &known);
std::wstring widen(const std::string &ascii);
std::string narrow(const std::wstring &text);
} // namespace dingosdk::camcorder_media
