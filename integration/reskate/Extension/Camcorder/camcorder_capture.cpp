#include "camcorder_capture.h"
#include "camcorder_runtime.h"
#include "camcorder_video.h"
#include "Engine/Core/Log/logging.h"
#include "Extension/UI/Overlay/overlay.h"
#include "camcorder/recorder.h"
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <mutex>
#include <optional>
#include <utility>

namespace dingosdk::overlay {
namespace {
using Microsoft::WRL::ComPtr;
using namespace dingosdk::camcorder;
constexpr float hdr_white_nits = 200.0f; // the brightness an HDR game's white is taken to be

struct Slot {
    enum class Stage { free, recorded, submitted };
    Stage stage{Stage::free};
    ComPtr<ID3D12Resource> buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes{};
    int width{}, height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    UINT64 fence{};
    int repeats{};
    float vignette{};
    SourceFormat source{};
};
struct Capture {
    std::array<Slot, 4> slots;
    Slot *recorded{}; // copied this frame, not yet submitted
    ComPtr<ID3D12Fence> fence;
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    int transfer{};
    bool recording{}, stopping{};
    std::uint32_t failed_clip{}; // a clip that could not start is not retried every frame
    FramePacer pacer;
    camcorder_video::Writer writer;
    std::mutex last_mutex;
    std::string last;
    bool format_said{};
};
Capture &capture() {
    static auto *value = new Capture;
    return *value;
}
double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::optional<SourceFormat> source_format(DXGI_FORMAT format, int transfer) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return SourceFormat::rgba8;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return SourceFormat::bgra8;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return transfer == 2 ? SourceFormat::rgb10a2_pq : SourceFormat::rgb10a2;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return SourceFormat::rgba16f;
    default: return std::nullopt;
    }
}
// REC on the camcorder whose picture is on screen: a recording should be running.
bool wanted(const camcorder_runtime::Published &p) {
    return p.frame.status.powered && p.frame.shot.valid && p.frame.status.transport == Transport::recording;
}
void say(NoticeLevel level, const std::string &title, const std::string &text) {
    {
        std::lock_guard lock(capture().last_mutex);
        capture().last = title + (text.empty() ? "" : " " + text);
    }
    notify(level, title, text);
    logging::write(level == NoticeLevel::info ? logging::Level::info : logging::Level::warning, logging::Channel::graphics,
                   "Camcorder: " + title + " " + text);
}
std::string narrow(const std::wstring &text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
bool in_flight(const Capture &c) {
    for (const auto &slot : c.slots)
        if (slot.stage != Slot::Stage::free) return true;
    return false;
}
// Copies finished readbacks to the writer.
void collect(Capture &c) {
    if (!c.fence) return;
    const UINT64 done = c.fence->GetCompletedValue();
    for (auto &slot : c.slots) {
        if (slot.stage != Slot::Stage::submitted || slot.fence > done) continue;
        camcorder_video::RawFrame frame;
        frame.pitch = slot.footprint.Footprint.RowPitch;
        frame.width = slot.width;
        frame.height = slot.height;
        frame.format = slot.source;
        frame.white_nits = hdr_white_nits;
        frame.vignette = slot.vignette;
        frame.repeats = slot.repeats;
        void *data{};
        const D3D12_RANGE read{0, static_cast<SIZE_T>(slot.bytes)};
        if (SUCCEEDED(slot.buffer->Map(0, &read, &data)) && data) {
            const auto *bytes = static_cast<const std::uint8_t *>(data) + slot.footprint.Offset;
            frame.bytes.assign(bytes, bytes + frame.pitch * static_cast<std::size_t>(slot.height));
            const D3D12_RANGE written{0, 0};
            slot.buffer->Unmap(0, &written);
            c.writer.push(std::move(frame));
        }
        slot.stage = Slot::Stage::free;
    }
}
} // namespace

void camcorder_capture_output(DXGI_FORMAT format, int transfer) {
    auto &c = capture();
    c.format = format;
    c.transfer = transfer;
}

bool camcorder_capture_poll() {
    auto &c = capture();
    collect(c);
    const auto published = camcorder_runtime::published();
    if (c.recording && !wanted(published)) {
        c.recording = false;
        c.stopping = true;
    }
    // STBY: the clip is closed once the last copies are in.
    if (c.stopping && !in_flight(c) && !c.recorded) {
        c.stopping = false;
        c.writer.finish();
    }
    if (auto result = c.writer.take_result()) {
        if (result->ok) {
            say(NoticeLevel::info, "Clip saved.", narrow(result->path));
            if (const auto dropped = c.writer.dropped())
                logging::printf(logging::Level::warning, logging::Channel::graphics,
                                "Camcorder: %llu frames were dropped (the encoder fell behind).",
                                static_cast<unsigned long long>(dropped));
        } else {
            say(NoticeLevel::warning, "Clip not saved.", result->error);
        }
    }
    return c.recording || c.stopping || c.writer.running() || in_flight(c);
}

void camcorder_capture_record(ID3D12Device *device, ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer) {
    auto &c = capture();
    if (c.recorded) { // last frame's copy never reached the GPU (the frame was abandoned)
        c.recorded->stage = Slot::Stage::free;
        c.recorded = nullptr;
    }
    if (!device || !commands || !back_buffer) return;
    const auto published = camcorder_runtime::published();
    if (!wanted(published) || c.stopping) return;
    const auto &status = published.frame.status;
    const auto desc = back_buffer->GetDesc();
    const auto source = source_format(desc.Format, c.transfer);
    if (!source) {
        if (!std::exchange(c.format_said, true))
            say(NoticeLevel::warning, "The camcorder cannot record this display format.", "");
        return;
    }
    const auto box = capture_box(static_cast<int>(desc.Width), static_cast<int>(desc.Height));
    if (box.width < 16 || box.height < 16) return;
    if (!c.recording) {
        // The last clip is still being closed: this one starts as soon as it is.
        if (status.clip == c.failed_clip || c.writer.running()) return;
        const auto folder = camcorder_video::clip_folder();
        std::string error = "Windows did not say where the Videos folder is.";
        bool started = false;
        if (!folder.empty()) {
            const auto time = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &time);
            const auto name = clip_file_name(status.clip, local);
            const auto path = folder + L"\\" + std::wstring(name.begin(), name.end());
            started = c.writer.start(path, video_size(published.dv ? Quality::dv : Quality::hd, box), error);
        }
        if (!started) {
            c.failed_clip = status.clip;
            say(NoticeLevel::warning, "The camcorder cannot record.", error);
            return;
        }
        c.failed_clip = 0;
        c.recording = true;
        c.pacer.start(now_seconds());
    }
    const int due = c.pacer.due(now_seconds());
    if (due <= 0) return;
    Slot *slot = nullptr;
    for (auto &candidate : c.slots)
        if (candidate.stage == Slot::Stage::free) {
            slot = &candidate;
            break;
        }
    if (!slot) return; // the GPU is behind: the next due frame repeats to cover this one
    if (!slot->buffer || slot->width != box.width || slot->height != box.height || slot->format != desc.Format) {
        auto crop = desc;
        crop.Width = static_cast<UINT64>(box.width);
        crop.Height = static_cast<UINT>(box.height);
        crop.DepthOrArraySize = 1;
        crop.MipLevels = 1;
        crop.SampleDesc = {1, 0};
        crop.Flags = D3D12_RESOURCE_FLAG_NONE;
        crop.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        crop.Alignment = 0;
        UINT64 total{};
        device->GetCopyableFootprints(&crop, 0, 1, 0, &slot->footprint, nullptr, nullptr, &total);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = total;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc = {1, 0};
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        slot->buffer.Reset();
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(&slot->buffer)))) {
            slot->buffer.Reset();
            return;
        }
        slot->bytes = total;
        slot->width = box.width;
        slot->height = box.height;
        slot->format = desc.Format;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = back_buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION to{};
    to.pResource = slot->buffer.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = slot->footprint;
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource = back_buffer;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.SubresourceIndex = 0;
    const D3D12_BOX region{static_cast<UINT>(box.x), static_cast<UINT>(box.y), 0,
                           static_cast<UINT>(box.x + box.width), static_cast<UINT>(box.y + box.height), 1};
    commands->CopyTextureRegion(&to, 0, 0, 0, &from, &region);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands->ResourceBarrier(1, &barrier);
    slot->stage = Slot::Stage::recorded;
    slot->source = *source;
    slot->repeats = due;
    slot->vignette = published.frame.optics.fisheye ? published.frame.optics.vignette_radius : 0.0f;
    c.recorded = slot;
}

void camcorder_capture_submitted(ID3D12Fence *fence, UINT64 signal) {
    auto &c = capture();
    if (!c.recorded) return;
    c.recorded->fence = signal;
    c.recorded->stage = Slot::Stage::submitted;
    c.recorded = nullptr;
    c.fence = fence;
}

void camcorder_capture_release() {
    auto &c = capture();
    // Copies still on the GPU are waited for (briefly) before their buffers go.
    if (c.fence) {
        UINT64 newest{};
        for (const auto &slot : c.slots)
            if (slot.stage == Slot::Stage::submitted) newest = std::max(newest, slot.fence);
        for (int waited = 0; waited < 500 && c.fence->GetCompletedValue() < newest; ++waited) Sleep(1);
    }
    collect(c);
    c.recorded = nullptr;
    for (auto &slot : c.slots) slot = {};
    c.fence.Reset();
    if (c.recording || c.stopping) {
        c.recording = c.stopping = false;
        c.writer.finish();
    }
}

std::string camcorder_capture_last() {
    std::lock_guard lock(capture().last_mutex);
    return capture().last;
}
} // namespace dingosdk::overlay
