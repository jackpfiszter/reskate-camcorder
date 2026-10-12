#include "camcorder_review.h"
#include "camcorder_capture.h"
#include "camcorder_media.h"
#include "Engine/Core/Log/logging.h"
#include "Extension/UI/Overlay/overlay.h"
#include <mfapi.h>
#include <propidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dingosdk::overlay {
namespace {
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

// The decoding thread and what it hands over.
struct Player {
    std::thread thread;
    std::atomic<bool> stop{}, ended{};
    std::mutex mutex;
    std::vector<std::uint8_t> picture; // BGRA, rows packed
    UINT width{}, height{};
    double seconds{}, length{};
    bool fresh{}; // a picture not yet uploaded
    std::string error;

    void run(std::wstring path) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        [&] {
            if (!camcorder_media::available()) return finish("Windows' video decoder (Media Foundation) is unavailable.");
            ComPtr<IMFAttributes> attributes;
            ComPtr<IMFSourceReader> reader;
            if (FAILED(MFCreateAttributes(&attributes, 1)) ||
                FAILED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE)) ||
                FAILED(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader)))
                return finish("The clip cannot be opened.");
            const auto video = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
            reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
            reader->SetStreamSelection(video, TRUE);
            // Decoded straight to 32-bit BGR, the clip's own size.
            ComPtr<IMFMediaType> rgb, current;
            UINT32 w{}, h{};
            if (FAILED(MFCreateMediaType(&rgb)) || FAILED(rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
                FAILED(rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) || FAILED(reader->SetCurrentMediaType(video, nullptr, rgb.Get())) ||
                FAILED(reader->GetCurrentMediaType(video, &current)) || FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h)) ||
                !w || !h)
                return finish("The clip's picture cannot be decoded.");
            // A decoder's frame can be taller than the picture (1080 is coded as 1088): only the
            // part meant to be seen. And its rows' step, which runs upwards for a bottom-up picture.
            UINT32 left{}, top{}, shown_w = w, shown_h = h;
            MFVideoArea area{};
            if (SUCCEEDED(current->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8 *>(&area), sizeof(area), nullptr)) &&
                area.Area.cx > 0 && area.Area.cy > 0) {
                left = std::min<UINT32>(static_cast<UINT32>(std::max<short>(area.OffsetX.value, 0)), w - 1);
                top = std::min<UINT32>(static_cast<UINT32>(std::max<short>(area.OffsetY.value, 0)), h - 1);
                shown_w = std::min<UINT32>(static_cast<UINT32>(area.Area.cx), w - left);
                shown_h = std::min<UINT32>(static_cast<UINT32>(area.Area.cy), h - top);
            }
            const auto default_stride = static_cast<LONG>(static_cast<INT32>(MFGetAttributeUINT32(current.Get(), MF_MT_DEFAULT_STRIDE, w * 4)));
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value))) {
                std::lock_guard lock(mutex);
                length = static_cast<double>(value.uhVal.QuadPart) / 1e7;
            }
            PropVariantClear(&value);
            const auto start = std::chrono::steady_clock::now();
            std::vector<std::uint8_t> packed(static_cast<std::size_t>(shown_w) * shown_h * 4);
            while (!stop) {
                DWORD flags{};
                LONGLONG stamp{};
                ComPtr<IMFSample> sample;
                if (FAILED(reader->ReadSample(video, 0, nullptr, &flags, &stamp, &sample))) break;
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
                if (!sample) continue;
                ComPtr<IMFMediaBuffer> buffer;
                if (FAILED(sample->GetBufferByIndex(0, &buffer)) && FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
                // Rows as the decoder lays them out: from the top row, a step (bytes) to the next.
                const auto copy_rows = [&](const BYTE *first, LONG row_step) {
                    for (UINT32 y = 0; y < shown_h; ++y)
                        std::memcpy(packed.data() + static_cast<std::size_t>(y) * shown_w * 4,
                                    first + static_cast<std::ptrdiff_t>(top + y) * row_step + static_cast<std::ptrdiff_t>(left) * 4,
                                    static_cast<std::size_t>(shown_w) * 4);
                };
                ComPtr<IMF2DBuffer> planes;
                BYTE *scan0{};
                LONG pitch{};
                if (SUCCEEDED(buffer.As(&planes)) && SUCCEEDED(planes->Lock2D(&scan0, &pitch))) {
                    copy_rows(scan0, pitch);
                    planes->Unlock2D();
                } else {
                    BYTE *data{};
                    DWORD bytes{};
                    if (FAILED(buffer->Lock(&data, nullptr, &bytes))) continue;
                    const auto step = static_cast<std::size_t>(std::abs(default_stride));
                    if (step >= static_cast<std::size_t>(w) * 4 && bytes >= step * h)
                        copy_rows(default_stride < 0 ? data + step * (h - 1) : data, default_stride);
                    buffer->Unlock();
                }
                // Shown when it is due, as it played when it was shot.
                const auto due = start + std::chrono::microseconds(stamp / 10);
                while (!stop && std::chrono::steady_clock::now() < due) std::this_thread::sleep_for(2ms);
                std::lock_guard lock(mutex);
                picture = packed;
                width = shown_w;
                height = shown_h;
                seconds = static_cast<double>(stamp) / 1e7;
                fresh = true;
            }
            finish("");
        }();
        if (SUCCEEDED(com)) CoUninitialize();
    }
    void finish(std::string why) {
        std::lock_guard lock(mutex);
        error = std::move(why);
        ended = true;
    }
};

struct Review {
    ID3D12Device *device{};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    std::unique_ptr<Player> player;
    std::uint32_t served{}, stop_served{};
    bool started{}; // the counters were read once (requests made before the game started are old)
    // The picture's texture, and an upload buffer for each frame that may be in flight.
    ComPtr<ID3D12Resource> texture;
    UINT width{}, height{};
    bool shown{};         // a picture has been uploaded
    struct Upload {
        ComPtr<ID3D12Resource> buffer;
        UINT64 fence{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    };
    std::array<Upload, 3> uploads;
    std::size_t next{};
    bool uploaded{}, drawn{};
    ComPtr<ID3D12Fence> fence;
    UINT64 last_use{};
    double seconds{}, length{};
};
Review &review() {
    static auto *value = new Review;
    return *value;
}
bool idle(const Review &r) { return !r.fence || r.fence->GetCompletedValue() >= r.last_use; }
void stop_player(Review &r) {
    if (!r.player) return;
    r.player->stop = true;
    if (r.player->thread.joinable()) r.player->thread.join();
    r.player.reset();
    r.shown = false;
}
void transition(ID3D12GraphicsCommandList *commands, ID3D12Resource *resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    commands->ResourceBarrier(1, &barrier);
}
// A texture of the clip's size (and its view: alpha read as 1), and upload buffers for it.
bool make_texture(Review &r, UINT width, UINT height) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc = {1, 0};
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> made;
    if (FAILED(r.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                 nullptr, IID_PPV_ARGS(&made))))
        return false;
    UINT64 total{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    r.device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, nullptr, &total);
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc = {1, 0};
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    for (auto &u : r.uploads) {
        u = {};
        if (FAILED(r.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                     nullptr, IID_PPV_ARGS(&u.buffer))))
            return false;
        u.layout = layout;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
    view.Texture2D.MipLevels = 1;
    r.device->CreateShaderResourceView(made.Get(), &view, r.cpu);
    r.texture = std::move(made);
    r.width = width;
    r.height = height;
    return true;
}
} // namespace

void camcorder_review_heap(ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    auto &r = review();
    r.device = device;
    r.cpu = cpu;
    r.gpu = gpu;
}

std::optional<ReviewPicture> camcorder_review(const camcorder_runtime::Published &p) {
    auto &r = review();
    if (!r.started) { // requests from before (or a previous graphics device) are not replayed
        r.started = true;
        r.served = p.review;
        r.stop_served = p.review_stop;
    }
    const bool recording = p.frame.status.transport == camcorder::Transport::recording;
    if (p.review_stop != r.stop_served || !p.frame.status.powered || recording) {
        r.stop_served = p.review_stop;
        stop_player(r);
    }
    if (p.review != r.served) {
        r.served = p.review;
        stop_player(r);
        const auto path = camcorder_capture_last_clip();
        if (path.empty()) {
            notify(NoticeLevel::warning, "Nothing to review.", "Record a clip first.");
        } else if (p.frame.status.powered && !recording && r.device) {
            r.player = std::make_unique<Player>();
            r.player->thread = std::thread([player = r.player.get(), path] { player->run(path); });
        }
    }
    if (!r.player) return std::nullopt;
    if (r.player->ended) {
        std::string error;
        {
            std::lock_guard lock(r.player->mutex);
            error = r.player->error;
        }
        if (!error.empty()) notify(NoticeLevel::warning, "The clip cannot be played.", error);
        stop_player(r);
        return std::nullopt;
    }
    {
        std::lock_guard lock(r.player->mutex);
        r.seconds = r.player->seconds;
        r.length = r.player->length;
    }
    if (!r.shown || !r.texture) return ReviewPicture{{}, r.seconds, r.length}; // the first picture is on its way
    r.drawn = true;
    return ReviewPicture{static_cast<ImTextureID>(r.gpu.ptr), r.seconds, r.length};
}

void camcorder_review_upload(ID3D12GraphicsCommandList *commands) {
    auto &r = review();
    if (!r.player || !commands || !r.device) return;
    std::lock_guard lock(r.player->mutex);
    if (!r.player->fresh || r.player->picture.empty()) return;
    if (!r.texture || r.width != r.player->width || r.height != r.player->height) {
        if (!idle(r)) return; // the old texture may still be drawn by frames in flight
        r.texture.Reset();
        if (!make_texture(r, r.player->width, r.player->height)) {
            logging::write(logging::Level::warning, logging::Channel::graphics, "Camcorder: no texture for the clip review.");
            return;
        }
    }
    auto &u = r.uploads[r.next];
    if (r.fence && r.fence->GetCompletedValue() < u.fence) return; // the GPU still reads this one: the next picture then
    void *mapped{};
    const D3D12_RANGE none{};
    if (FAILED(u.buffer->Map(0, &none, &mapped)) || !mapped) return;
    const std::size_t row = static_cast<std::size_t>(r.width) * 4;
    for (UINT y = 0; y < r.height; ++y)
        std::memcpy(static_cast<std::uint8_t *>(mapped) + u.layout.Offset + static_cast<std::size_t>(y) * u.layout.Footprint.RowPitch,
                    r.player->picture.data() + y * row, row);
    u.buffer->Unmap(0, nullptr);
    r.player->fresh = false;
    transition(commands, r.texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{}, from{};
    to.pResource = r.texture.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = u.buffer.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = u.layout;
    commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    transition(commands, r.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    r.uploaded = true;
    r.shown = true;
}

void camcorder_review_submitted(ID3D12Fence *fence, UINT64 signal) {
    auto &r = review();
    if (r.uploaded) {
        r.uploads[r.next].fence = signal;
        r.next = (r.next + 1) % r.uploads.size();
    }
    if (r.uploaded || r.drawn) {
        r.fence = fence;
        r.last_use = signal;
    }
    r.uploaded = r.drawn = false;
}

void camcorder_review_release() {
    auto &r = review();
    stop_player(r);
    for (int waited = 0; waited < 500 && !idle(r); ++waited) Sleep(1);
    r.texture.Reset();
    for (auto &u : r.uploads) u = {};
    r.fence.Reset();
    r.width = r.height = 0;
    r.device = nullptr;
    r.cpu = {};
    r.gpu = {};
    r.started = false;
}
} // namespace dingosdk::overlay
