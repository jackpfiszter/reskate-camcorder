#pragma once
// Reviewing the tape: the last clip plays back in the viewfinder, as on the camera, so a filmer
// knows whether they got the trick before skating off. A thread decodes the clip (Media
// Foundation) and hands over each picture when it is due; the render thread uploads it to a
// texture and the viewfinder draws it over the picture. Picture only: the sound is not played back.
//
// Render thread only (Extension/UI/Overlay/overlay_render.cpp, under the overlay's render lock).
#include "camcorder_runtime.h"
#include <d3d12.h>
#include <imgui.h>
#include <optional>

namespace dingosdk::overlay {
// Graphics setup: the device and the SRV heap slot the picture is drawn from.
void camcorder_review_heap(ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);
// Frame building (draw_camcorder): starts and stops as `camcorder review` asks (and stops for REC
// or the camera going off). While a clip plays: its picture, and where it is in it.
struct ReviewPicture {
    ImTextureID texture{};
    double seconds{}, length{};
};
std::optional<ReviewPicture> camcorder_review(const camcorder_runtime::Published &);
// With the frame's command list open, before the overlay draws: uploads a newly due picture.
void camcorder_review_upload(ID3D12GraphicsCommandList *commands);
// After the frame's commands were submitted and `signal` set on `fence`.
void camcorder_review_submitted(ID3D12Fence *fence, UINT64 signal);
// Graphics teardown (a clip playing stops).
void camcorder_review_release();
} // namespace dingosdk::overlay
