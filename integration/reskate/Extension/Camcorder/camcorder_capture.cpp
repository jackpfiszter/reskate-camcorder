#include "camcorder_capture.h"
#include "camcorder_media.h"
#include "camcorder_runtime.h"
#include "camcorder_tape.h"
#include "camcorder_video.h"
#include "Engine/Core/Log/logging.h"
#include "Extension/UI/Overlay/overlay.h"
#include "camcorder/look.h"
#include "camcorder/recorder.h"
#include <backends/imgui_impl_dx12.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace dingosdk::overlay {
namespace {
using Microsoft::WRL::ComPtr;
using namespace dingosdk::camcorder;
using camcorder_media::narrow;
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
    int repeats{};        // video frames it becomes (0: a still only)
    std::int64_t index{}; // the clip's frame number of the first of them
    bool still{};         // also saved as a still
    float vignette{};
    SourceFormat source{};
    Finish finish;        // a still's DV look and date stamp
    bool look_on_screen{}, stamp_on_screen{}; // what the picture pass already did to it
};
// A frame worked out by camcorder_capture_record, copied by camcorder_capture_draw.
struct Armed {
    ID3D12Device *device{};
    ID3D12Resource *back_buffer{};
    D3D12_RESOURCE_DESC desc{};
    PixelBox box;
    SourceFormat source{};
    float vignette{};
    int due{};
    std::int64_t index{};
    bool still{};
    Finish finish;
    bool look_on_screen{}, stamp_on_screen{};
};
// A still being saved on a thread of its own.
struct StillJob {
    std::thread thread;
    std::atomic<bool> busy{};
    std::mutex mutex;
    std::optional<std::pair<bool, std::string>> result; // saved, and the path or why not
};
struct Capture {
    std::array<Slot, 4> slots;
    Slot *recorded{}; // copied this frame, not yet submitted
    std::optional<Armed> armed; // due this frame, not yet copied
    bool look_on_screen{}, stamp_on_screen{}; // this frame's, from draw_camcorder
    ComPtr<ID3D12Fence> fence;
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    int transfer{};
    bool recording{}, stopping{};
    std::uint32_t failed_clip{}; // a clip that could not start is not retried every frame
    FramePacer pacer;
    camcorder_video::Writer writer;
    std::uint32_t still_served{}, capture_served{};
    StillJob still;
    TapeLog tape_log;
    bool tape_loaded{}; // last game's clip list read back
    camcorder_tape::Capture tape;
    std::mutex last_mutex;
    std::string last;
    std::wstring last_clip; // the newest clip written, for review
    bool format_said{};
};
Capture &capture() {
    static auto *value = new Capture;
    return *value;
}
double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::tm local_now() {
    const auto time = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &time);
    return local;
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
// The camcorder's own picture is on screen.
bool on_screen(const camcorder_runtime::Published &p) { return p.frame.status.powered && p.frame.shot.valid; }
// REC on the camcorder whose picture is on screen: a recording should be running.
bool wanted(const camcorder_runtime::Published &p) {
    return on_screen(p) && p.frame.status.transport == Transport::recording;
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
bool in_flight(const Capture &c) {
    for (const auto &slot : c.slots)
        if (slot.stage != Slot::Stage::free) return true;
    return false;
}
// Saves a still on its own thread: full size, the lens's corners, no readouts.
void save_still(StillJob &job, camcorder_video::RawFrame frame, Finish finish) {
    if (job.thread.joinable()) job.thread.join();
    job.busy = true;
    job.thread = std::thread([&job, frame = std::move(frame), finish] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::pair<bool, std::string> result{false, "Windows did not say where the Pictures folder is."};
        const auto folder = camcorder_media::folder(FOLDERID_Pictures);
        if (!folder.empty()) {
            Image image;
            convert_to_bgra(frame.bytes.data(), frame.pitch, frame.width, frame.height, frame.format, image, frame.white_nits);
            apply_vignette(image, frame.vignette);
            const auto when = local_now();
            finish_frame(image, finish, DvClip{0.5f}, 0, clip_rate(finish).fps(), when); // what the screen left
            const auto path = folder + L"\\" + camcorder_media::widen(photo_file_name(local_now()));
            std::string error;
            result = camcorder_media::save_jpeg(image, path, error) ? std::pair{true, narrow(path)} : std::pair{false, error};
        }
        if (SUCCEEDED(com)) CoUninitialize();
        std::lock_guard lock(job.mutex);
        job.result = std::move(result);
        job.busy = false;
    });
}
// Copies finished readbacks to the writer (and the still saver).
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
        frame.index = slot.index;
        void *data{};
        const D3D12_RANGE read{0, static_cast<SIZE_T>(slot.bytes)};
        if (SUCCEEDED(slot.buffer->Map(0, &read, &data)) && data) {
            const auto *bytes = static_cast<const std::uint8_t *>(data) + slot.footprint.Offset;
            frame.bytes.assign(bytes, bytes + frame.pitch * static_cast<std::size_t>(slot.height));
            const D3D12_RANGE written{0, 0};
            slot.buffer->Unmap(0, &written);
            frame.look_on_screen = slot.look_on_screen;
            frame.stamp_on_screen = slot.stamp_on_screen;
            if (slot.still)
                save_still(c.still, slot.repeats > 0 ? frame : std::move(frame),
                           recorded_finish(slot.finish, slot.look_on_screen, slot.stamp_on_screen));
            if (slot.repeats > 0) c.writer.push(std::move(frame));
        }
        slot.stage = Slot::Stage::free;
        slot.still = false;
    }
}
// Queues a copy of the picture into a free readback slot. Null when every slot is busy.
Slot *copy_picture(Capture &c, ID3D12Device *device, ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer,
                   D3D12_RESOURCE_STATES state, const D3D12_RESOURCE_DESC &desc, const PixelBox &box, SourceFormat source,
                   float vignette) {
    Slot *slot = nullptr;
    for (auto &candidate : c.slots)
        if (candidate.stage == Slot::Stage::free) {
            slot = &candidate;
            break;
        }
    if (!slot) return nullptr;
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
            return nullptr;
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
    barrier.Transition.StateBefore = state;
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
    slot->source = source;
    slot->repeats = 0;
    slot->index = 0;
    slot->still = false;
    slot->vignette = vignette;
    c.recorded = slot;
    return slot;
}
} // namespace

void camcorder_capture_output(DXGI_FORMAT format, int transfer) {
    auto &c = capture();
    c.format = format;
    c.transfer = transfer;
}

namespace {
// The tape's clip list, kept between games beside the settings: the clips stay on the disk, and
// so does the tape (until `camcorder tape new`). Clips deleted since are left out.
std::wstring tape_list_path() {
    const auto folder = camcorder_media::folder(FOLDERID_LocalAppData);
    return folder.empty() ? folder : folder + L"\\tape.txt";
}
void save_tape_list(const TapeLog &log) {
    const auto path = tape_list_path();
    if (path.empty()) return;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    for (const auto &clip : log.clips()) file << camcorder_media::narrow(clip) << '\n';
}
std::vector<std::wstring> load_tape_list() {
    std::vector<std::wstring> clips;
    const auto path = tape_list_path();
    if (path.empty()) return clips;
    std::ifstream file(path, std::ios::binary);
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const int size = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0);
        if (size <= 0) continue;
        std::wstring clip(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), clip.data(), size);
        if (GetFileAttributesW(clip.c_str()) != INVALID_FILE_ATTRIBUTES) clips.push_back(std::move(clip));
    }
    return clips;
}
} // namespace

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
    if (!c.tape_loaded) {
        c.tape_loaded = true;
        c.tape_log.restore(published.tape, load_tape_list());
    } else if (published.tape != c.tape_log.number()) {
        c.tape_log.tape(published.tape); // a new tape: an empty list
        save_tape_list(c.tape_log);
    }
    if (auto result = c.writer.take_result()) {
        if (result->ok) {
            c.tape_log.saved(result->path);
            save_tape_list(c.tape_log);
            {
                std::lock_guard lock(c.last_mutex);
                c.last_clip = result->path;
            }
            say(result->warning.empty() ? NoticeLevel::info : NoticeLevel::warning, "Clip saved.",
                narrow(result->path) + (result->warning.empty() ? "" : " " + result->warning));
            if (const auto dropped = c.writer.dropped())
                logging::printf(logging::Level::warning, logging::Channel::graphics,
                                "Camcorder: %llu frames were dropped (the encoder fell behind).",
                                static_cast<unsigned long long>(dropped));
        } else {
            say(NoticeLevel::warning, "Clip not saved.", result->error);
        }
    }
    // Stills.
    {
        std::optional<std::pair<bool, std::string>> still;
        {
            std::lock_guard lock(c.still.mutex);
            still = std::exchange(c.still.result, std::nullopt);
        }
        if (still) say(still->first ? NoticeLevel::info : NoticeLevel::warning, still->first ? "Still saved." : "Still not saved.", still->second);
    }
    if (published.still != c.still_served && !on_screen(published)) {
        c.still_served = published.still; // asked with no picture to take
        say(NoticeLevel::warning, "No still taken.", "Turn the camcorder on first.");
    }
    // Capturing the tape.
    // (Asked for straight after STBY, it waits for that clip to be saved, so it is on the tape.)
    if (published.capture_tape != c.capture_served && !c.stopping && !c.writer.running() && !in_flight(c)) {
        c.capture_served = published.capture_tape;
        if (c.recording) {
            say(NoticeLevel::warning, "Not while recording.", "Press STBY, then capture the tape.");
        } else if (c.tape_log.clips().empty()) {
            say(NoticeLevel::warning, "Nothing to capture.", "Record some clips on this tape first.");
        } else if (c.tape.running()) {
            say(NoticeLevel::warning, "Already capturing a tape.", "");
        } else {
            const auto folder = camcorder_media::folder(FOLDERID_Videos);
            const auto path = folder + L"\\" +
                              camcorder_media::widen(tape_file_name(c.tape_log.number(), c.tape_log.clips().size(), local_now()));
            if (folder.empty() || !c.tape.start(c.tape_log.clips(), path))
                say(NoticeLevel::warning, "The tape cannot be captured.", "");
            else
                say(NoticeLevel::info, "Capturing the tape...", std::to_string(c.tape_log.clips().size()) + " clips.");
        }
    }
    if (auto result = c.tape.take_result()) {
        if (result->ok)
            say(result->warning.empty() ? NoticeLevel::info : NoticeLevel::warning, "Tape captured.",
                narrow(result->path) + (result->warning.empty() ? "" : " " + result->warning));
        else
            say(NoticeLevel::warning, "Tape not captured.", result->error + (result->warning.empty() ? "" : " " + result->warning));
    }
    return c.recording || c.stopping || c.writer.running() || in_flight(c) || c.still.busy || c.tape.running() ||
           published.still != c.still_served;
}

void camcorder_capture_record(ID3D12Device *device, ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer) {
    auto &c = capture();
    if (c.recorded) { // last frame's copy never reached the GPU (the frame was abandoned)
        c.recorded->stage = Slot::Stage::free;
        c.recorded = nullptr;
    }
    c.armed.reset(); // nor did last frame's draw reach the camcorder's (it was not on screen)
    if (!device || !commands || !back_buffer) return;
    const auto published = camcorder_runtime::published();
    const bool still = on_screen(published) && published.still != c.still_served && !c.still.busy;
    const bool video = wanted(published) && !c.stopping;
    if (!still && !video) return;
    const auto &status = published.frame.status;
    const auto desc = back_buffer->GetDesc();
    const auto source = source_format(desc.Format, c.transfer);
    if (!source) {
        if (!std::exchange(c.format_said, true))
            say(NoticeLevel::warning, "The camcorder cannot record this display format.", "");
        c.still_served = published.still;
        return;
    }
    const auto box = capture_box(static_cast<int>(desc.Width), static_cast<int>(desc.Height));
    if (box.width < 16 || box.height < 16) return;
    int due = 0;
    if (video) {
        if (!c.recording) {
            // The last clip is still being closed: this one starts as soon as it is.
            if (status.clip != c.failed_clip && !c.writer.running()) {
                const auto folder = camcorder_video::clip_folder();
                std::string error = "Windows did not say where the Videos folder is.";
                bool started = false;
                // The picture's pacer and the sound share this start, so they stay together.
                const double origin = now_seconds();
                if (!folder.empty()) {
                    const auto path = folder + L"\\" + camcorder_media::widen(clip_file_name(status.clip, local_now()));
                    started = c.writer.start(path, video_size(published.dv ? Quality::dv : Quality::hd, box, published.own_lines),
                                             published.sound, origin, published.finish, std::time(nullptr), error);
                }
                if (started) {
                    c.failed_clip = 0;
                    c.recording = true;
                    c.pacer.start(origin, clip_rate(published.finish).fps());
                } else {
                    c.failed_clip = status.clip;
                    say(NoticeLevel::warning, "The camcorder cannot record.", error);
                }
            }
        }
        if (c.recording) due = c.pacer.due(now_seconds());
    }
    if (due <= 0 && !still) return;
    Armed armed;
    armed.device = device;
    armed.back_buffer = back_buffer;
    armed.desc = desc;
    armed.box = box;
    armed.source = *source;
    armed.vignette = published.frame.optics.vignette_radius;
    armed.due = std::max(due, 0);
    armed.index = c.pacer.first();
    armed.still = still;
    armed.finish = published.finish;
    armed.look_on_screen = c.look_on_screen;
    armed.stamp_on_screen = c.stamp_on_screen;
    c.armed = armed;
    if (still) c.still_served = published.still;
}

void camcorder_capture_draw(const ImDrawList *, const ImDrawCmd *) {
    auto &c = capture();
    const auto armed = std::exchange(c.armed, std::nullopt);
    const auto *render = static_cast<ImGui_ImplDX12_RenderState *>(ImGui::GetPlatformIO().Renderer_RenderState);
    if (!armed || !render || !render->CommandList) return;
    // With every slot busy the GPU is behind: the next due frame repeats to cover this one.
    Slot *slot = copy_picture(c, armed->device, render->CommandList, armed->back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
                              armed->desc, armed->box, armed->source, armed->vignette);
    if (!slot) return;
    slot->repeats = armed->due;
    slot->index = armed->index;
    slot->still = armed->still;
    slot->finish = armed->finish;
    slot->look_on_screen = armed->look_on_screen;
    slot->stamp_on_screen = armed->stamp_on_screen;
}

void camcorder_capture_screen(bool look, bool stamp) {
    auto &c = capture();
    c.look_on_screen = look;
    c.stamp_on_screen = stamp;
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

std::wstring camcorder_capture_last_clip() {
    auto &c = capture();
    {
        std::lock_guard lock(c.last_mutex);
        if (!c.last_clip.empty()) return c.last_clip;
    }
    // None yet this game: the tape's last, from last game (render thread, as the tape list is).
    return c.tape_log.clips().empty() ? std::wstring() : c.tape_log.clips().back();
}
} // namespace dingosdk::overlay
