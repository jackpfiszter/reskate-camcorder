#include "camcorder_audio.h"
#include <Windows.h>
#include <Audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

namespace dingosdk::camcorder_audio {
namespace {
using Microsoft::WRL::ComPtr;
using namespace dingosdk::camcorder;

// ActivateAudioInterfaceAsync's answer, waited for on the capture thread.
class Activation : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                                       Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    HANDLE done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HRESULT result{E_FAIL};
    ComPtr<IAudioClient> client;
    ~Activation() override {
        if (done) CloseHandle(done);
    }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *operation) override {
        HRESULT activated = E_FAIL;
        ComPtr<IUnknown> unknown;
        const HRESULT hr = operation->GetActivateResult(&activated, &unknown);
        result = FAILED(hr) ? hr : activated;
        if (SUCCEEDED(result)) result = unknown.As(&client);
        SetEvent(done);
        return S_OK;
    }
};
std::string failed(const char *what, HRESULT result) {
    char text[200];
    std::snprintf(text, sizeof(text), "%s failed (0x%08lx). Game sound needs Windows 10 version 2004 or later.", what,
                  static_cast<unsigned long>(result));
    return text;
}
double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

struct Capture::Impl {
    mutable std::mutex mutex;
    std::vector<std::int16_t> pending;
    std::string error;
    std::thread thread;
    HANDLE stop_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};

    void run(double origin, Sound sound) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const auto fail = [&](std::string why) {
            std::lock_guard lock(mutex);
            error = std::move(why);
        };
        HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        [&] {
            // Only this process (the game) and what it starts: no Discord, no music player, no notifications.
            AUDIOCLIENT_ACTIVATION_PARAMS params{};
            params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
            params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
            params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
            PROPVARIANT activation{};
            activation.vt = VT_BLOB;
            activation.blob.cbSize = sizeof(params);
            activation.blob.pBlobData = reinterpret_cast<BYTE *>(&params);
            auto handler = Microsoft::WRL::Make<Activation>();
            ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
            HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                                     &activation, handler.Get(), &operation);
            if (FAILED(hr)) return fail(failed("Starting the game's sound capture", hr));
            if (WaitForSingleObject(handler->done, 3000) != WAIT_OBJECT_0)
                return fail("Windows did not start the game's sound capture in time.");
            if (FAILED(handler->result)) return fail(failed("Starting the game's sound capture", handler->result));
            auto client = handler->client;
            // The loopback device has no format of its own: ask for the clip's.
            WAVEFORMATEX format{};
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.nChannels = audio_channels;
            format.nSamplesPerSec = audio_rate;
            format.wBitsPerSample = 16;
            format.nBlockAlign = static_cast<WORD>(audio_channels * 2);
            format.nAvgBytesPerSec = audio_rate * format.nBlockAlign;
            constexpr REFERENCE_TIME second = 10'000'000;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    second, 0, &format, nullptr);
            ComPtr<IAudioCaptureClient> capture;
            if (SUCCEEDED(hr)) hr = client->SetEventHandle(ready);
            if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
            if (SUCCEEDED(hr)) hr = client->Start();
            if (FAILED(hr)) return fail(failed("Setting up the game's sound capture", hr));
            AudioTimeline timeline;
            timeline.start(origin);
            VxMic mic;
            std::vector<std::int16_t> packet;
            const HANDLE waits[]{stop_event, ready};
            for (;;) {
                const auto woke = WaitForMultipleObjects(2, waits, FALSE, 200);
                UINT32 size{};
                while (SUCCEEDED(capture->GetNextPacketSize(&size)) && size) {
                    BYTE *data{};
                    UINT32 frames{};
                    DWORD flags{};
                    UINT64 device_position{}, qpc_position{};
                    if (FAILED(capture->GetBuffer(&data, &frames, &flags, &device_position, &qpc_position))) break;
                    packet.assign(static_cast<std::size_t>(frames) * audio_channels, 0);
                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data)
                        std::memcpy(packet.data(), data, packet.size() * sizeof(std::int16_t));
                    capture->ReleaseBuffer(frames);
                    if (sound == Sound::vx_mic) mic.process(packet.data(), frames);
                    // When it was played: the packet's own clock time (100 ns units of the
                    // performance counter, the steady clock's), or now when it has none.
                    const bool timed = qpc_position && !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
                    const double at = timed ? static_cast<double>(qpc_position) / 1e7
                                            : now_seconds() - static_cast<double>(frames) / audio_rate;
                    std::lock_guard lock(mutex);
                    place_packet(timeline, at, packet.data(), frames, pending);
                }
                if (woke == WAIT_OBJECT_0) break;
            }
            client->Stop();
        }();
        CloseHandle(ready);
        if (SUCCEEDED(com)) CoUninitialize();
    }
};

Capture::Capture() : impl_(std::make_unique<Impl>()) {}

Capture::~Capture() {
    stop();
    if (impl_->stop_event) CloseHandle(impl_->stop_event);
}

void Capture::start(double origin_seconds, Sound sound) {
    auto &s = *impl_;
    stop();
    {
        std::lock_guard lock(s.mutex);
        s.pending.clear();
        s.error.clear();
    }
    ResetEvent(s.stop_event);
    s.thread = std::thread([&s, origin_seconds, sound] { s.run(origin_seconds, sound); });
}

std::vector<std::int16_t> Capture::take() {
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->pending, {});
}

void Capture::stop() {
    auto &s = *impl_;
    if (!s.thread.joinable()) return;
    SetEvent(s.stop_event);
    s.thread.join();
}

std::string Capture::error() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->error;
}
} // namespace dingosdk::camcorder_audio
