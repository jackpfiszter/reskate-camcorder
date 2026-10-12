#pragma once
// The fisheye's bend on screen. The game renders a flat picture, opened wide enough for the warp
// (camcorder/vx1000.h, FisheyeRender::warp); the overlay copies that frame into a texture and draws
// it back over the 4:3 picture through a fine mesh whose texture coordinates follow the fisheye
// (warp_uv), so straight lines bow as they did through a Century fisheye. Clips and stills are
// copied after the mesh is drawn (camcorder_capture.h), so they have the bend too.
//
// Render thread only (Extension/UI/Overlay/overlay_render.cpp, under the overlay's render lock).
#include <d3d12.h>
#include <imgui.h>
#include <optional>

namespace dingosdk::overlay {
// Graphics setup: the device and the SRV heap slot the copy is drawn from.
void camcorder_warp_heap(ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);
// Frame building (draw_camcorder): asks for this frame's picture to be copied. The texture to draw
// it from, when there is one of `width` x `height` (none on the first frame at a new size: then
// draw nothing warped this frame).
std::optional<ImTextureID> camcorder_warp_request(int width, int height);
// With the frame's command list open, before the overlay draws (the back buffer in PRESENT):
// copies the game's picture when it was asked for this frame.
void camcorder_warp_copy(ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer);
// After the frame's commands were submitted and `signal` set on `fence`.
void camcorder_warp_submitted(ID3D12Fence *fence, UINT64 signal);
// Graphics teardown.
void camcorder_warp_release();
} // namespace dingosdk::overlay
