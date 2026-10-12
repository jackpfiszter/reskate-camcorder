#include "camcorder_media.h"
#include "camcorder/audio.h"
#include <ShlObj.h>
#include <mfapi.h>
#include <wincodec.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace dingosdk::camcorder_media {
using Microsoft::WRL::ComPtr;
using namespace dingosdk::camcorder;

bool available() {
    static const bool started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    return started;
}

std::string failed(const char *what, HRESULT result) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s failed (0x%08lx).", what, static_cast<unsigned long>(result));
    return text;
}

HRESULT open_mp4(const std::wstring &path, VideoSize size, bool sound, ComPtr<IMFSinkWriter> &writer, DWORD &video,
                 DWORD &audio) {
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaType> out, in;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes.Get(), &writer);
    if (FAILED(hr)) return hr;
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
    if (SUCCEEDED(hr)) hr = writer->AddStream(out.Get(), &video);
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&in);
    if (SUCCEEDED(hr)) hr = describe(in.Get(), MFVideoFormat_NV12);
    if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(size.width));
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(video, in.Get(), nullptr);
    if (SUCCEEDED(hr) && sound) {
        ComPtr<IMFMediaType> audio_out, audio_in;
        hr = MFCreateMediaType(&audio_out);
        if (SUCCEEDED(hr)) hr = audio_out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = audio_out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_rate);
        if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels);
        if (SUCCEEDED(hr)) hr = audio_out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        if (SUCCEEDED(hr)) hr = writer->AddStream(audio_out.Get(), &audio);
        if (SUCCEEDED(hr)) hr = MFCreateMediaType(&audio_in);
        if (SUCCEEDED(hr)) hr = audio_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = audio_in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_rate);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, audio_channels * 2);
        if (SUCCEEDED(hr)) hr = audio_in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audio_rate * audio_channels * 2);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audio, audio_in.Get(), nullptr);
    }
    if (SUCCEEDED(hr)) hr = writer->BeginWriting();
    if (FAILED(hr)) {
        writer.Reset();
        DeleteFileW(path.c_str());
    }
    return hr;
}

HRESULT write_sample(IMFSinkWriter *writer, DWORD stream, const void *bytes, std::size_t size, LONGLONG time,
                     LONGLONG duration) {
    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFSample> sample;
    BYTE *data{};
    HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(size), &buffer);
    if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        std::memcpy(data, bytes, size);
        buffer->Unlock();
        hr = buffer->SetCurrentLength(static_cast<DWORD>(size));
    }
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(time);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration);
    if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample.Get());
    return hr;
}

bool save_jpeg(const Image &image, const std::wstring &path, std::string &error) {
    if (image.width <= 0 || image.height <= 0) {
        error = "The still was empty.";
        return false;
    }
    // JPEG takes 24-bit BGR.
    const UINT stride = static_cast<UINT>(image.width) * 3;
    std::vector<BYTE> bgr(static_cast<std::size_t>(stride) * static_cast<std::size_t>(image.height));
    for (std::size_t i = 0, j = 0; i < image.pixels.size(); i += 4, j += 3) {
        bgr[j] = image.pixels[i];
        bgr[j + 1] = image.pixels[i + 1];
        bgr[j + 2] = image.pixels[i + 2];
    }
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(hr) && options) {
        PROPBAG2 quality{};
        wchar_t name[] = L"ImageQuality";
        quality.pstrName = name;
        VARIANT value{};
        value.vt = VT_R4;
        value.fltVal = 0.95f;
        hr = options->Write(1, &quality, &value);
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(options.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat24bppBGR) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(static_cast<UINT>(image.height), stride, static_cast<UINT>(bgr.size()), bgr.data());
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) {
        error = failed("Saving the still", hr);
        stream.Reset();
        DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

std::wstring folder(const KNOWNFOLDERID &known) {
    PWSTR base{};
    if (FAILED(SHGetKnownFolderPath(known, KF_FLAG_CREATE, nullptr, &base)) || !base) {
        if (base) CoTaskMemFree(base);
        return {};
    }
    std::wstring path = std::wstring(base) + L"\\ReSkate Camcorder";
    CoTaskMemFree(base);
    CreateDirectoryW(path.c_str(), nullptr); // already there is fine
    return path;
}

std::wstring widen(const std::string &ascii) { return std::wstring(ascii.begin(), ascii.end()); }

std::string narrow(const std::wstring &text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
} // namespace dingosdk::camcorder_media
