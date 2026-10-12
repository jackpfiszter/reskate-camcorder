#include "camcorder_warp.h"
#include "camcorder_runtime.h"
#include "Engine/Core/Log/logging.h"
#include <wrl/client.h>
#include <utility>

namespace dingosdk::overlay {
namespace {
using Microsoft::WRL::ComPtr;

struct Warp {
    ID3D12Device *device{};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    ComPtr<ID3D12Resource> texture;
    UINT64 width{};
    UINT height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    bool requested{}, drawn{}, copied{};
    ComPtr<ID3D12Fence> fence;
    UINT64 last_use{}; // the newest frame that drew from (or copied into) the texture
    bool working{}, failure_said{};
};
Warp &warp() {
    static auto *value = new Warp;
    return *value;
}
void transition(ID3D12GraphicsCommandList *commands, ID3D12Resource *resource, D3D12_RESOURCE_STATES from,
                D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    commands->ResourceBarrier(1, &barrier);
}
bool idle(const Warp &w) { return !w.fence || w.fence->GetCompletedValue() >= w.last_use; }
// A texture like the back buffer, in COPY_DEST, with its view in the heap slot. The view reads
// alpha as 1: what the game leaves in its back buffer's alpha means nothing.
bool make_texture(Warp &w, const D3D12_RESOURCE_DESC &back) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = back.Width;
    desc.Height = back.Height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = back.Format;
    desc.SampleDesc = {1, 0};
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> made;
    if (FAILED(w.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&made))))
        return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
    view.Texture2D.MipLevels = 1;
    w.device->CreateShaderResourceView(made.Get(), &view, w.cpu);
    w.texture = std::move(made);
    w.width = desc.Width;
    w.height = desc.Height;
    w.format = desc.Format;
    return true;
}
} // namespace

void camcorder_warp_heap(ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    auto &w = warp();
    w.device = device;
    w.cpu = cpu;
    w.gpu = gpu;
}

std::optional<ImTextureID> camcorder_warp_request(int width, int height) {
    auto &w = warp();
    if (!w.device || !w.gpu.ptr) return std::nullopt;
    w.requested = true;
    if (!w.texture || w.width != static_cast<UINT64>(width) || w.height != static_cast<UINT>(height)) return std::nullopt;
    w.drawn = true;
    return static_cast<ImTextureID>(w.gpu.ptr);
}

void camcorder_warp_copy(ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer) {
    auto &w = warp();
    if (!std::exchange(w.requested, false) || !commands || !back_buffer || !w.device) return;
    const auto back = back_buffer->GetDesc();
    if (back.SampleDesc.Count != 1) {
        camcorder_runtime::note_warp_failed();
        return;
    }
    bool fresh = false;
    if (!w.texture || w.width != back.Width || w.height != back.Height || w.format != back.Format) {
        // The old texture (and its view) may still be in use by frames on the GPU.
        if (!idle(w)) return;
        w.texture.Reset();
        if (!make_texture(w, back)) {
            if (!std::exchange(w.failure_said, true))
                logging::write(logging::Level::warning, logging::Channel::graphics,
                               "Camcorder: no texture for the fisheye warp; the picture stays flat.");
            camcorder_runtime::note_warp_failed();
            return;
        }
        fresh = true;
    }
    if (!fresh) transition(commands, w.texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    transition(commands, back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands->CopyResource(w.texture.Get(), back_buffer);
    transition(commands, back_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    transition(commands, w.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    w.copied = true;
}

void camcorder_warp_submitted(ID3D12Fence *fence, UINT64 signal) {
    auto &w = warp();
    if (w.copied || w.drawn) {
        w.fence = fence;
        w.last_use = signal;
        if (w.drawn && !std::exchange(w.working, true))
            logging::write(logging::Level::info, logging::Channel::graphics, "Camcorder: fisheye warp drawing.");
    }
    w.copied = w.drawn = false;
}

void camcorder_warp_release() {
    auto &w = warp();
    for (int waited = 0; waited < 500 && !idle(w); ++waited) Sleep(1);
    w.texture.Reset();
    w.fence.Reset();
    w.width = 0;
    w.height = 0;
    w.format = DXGI_FORMAT_UNKNOWN;
    w.requested = w.drawn = w.copied = false;
    w.device = nullptr;
    w.cpu = {};
    w.gpu = {};
}
} // namespace dingosdk::overlay
