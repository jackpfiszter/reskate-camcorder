#pragma once
// The camcorder's picture on screen: the lens's bend and the camera's effects
// (camcorder/effects.h). The game renders a flat picture (opened wide enough for the bend:
// camcorder/vx1000.h, FisheyeRender::warp); before the overlay draws, that frame is copied, and the
// picture is drawn back over it pass by pass through the picture shader (camcorder_shader.h): the
// lens, each effect, then out to the screen. The overlay's readouts go over the result,
// and clips and stills are copied after it (camcorder_capture.h), so they have what the screen has.
//
// Where the shader cannot be built, the bend alone is drawn through a fine ImGui mesh, and the
// look is left to the recording (the old way).
//
// Render thread only (Extension/UI/Overlay/overlay_render.cpp, under the overlay's render lock).
#include "camcorder/effects.h"
#include <d3d12.h>
#include <dxgi.h>
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

namespace dingosdk::overlay {
// Graphics setup: the device, the SRV heap slot the copy is drawn from, and the back buffer's
// format and transfer (0 SDR, 1 scRGB, 2 HDR10).
void camcorder_warp_heap(ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu,
                         DXGI_FORMAT format, int transfer);
// Frame building (draw_camcorder): asks for this frame's picture to be copied (or, with `copy`
// false, for the last one to be shown again: a film camera's slower cadence). The texture to draw
// it from, at the back buffer's own size, once there is one (none on the very first frame: then
// draw nothing over the game this frame).
std::optional<ImTextureID> camcorder_warp_request(bool copy = true);
// Why the last request had no picture (empty when it had one), for the player.
std::string camcorder_warp_why_not();

// This frame's picture: where it is, the lens and the effects' passes.
struct PictureChain {
    float rect[4]{};   // the picture on screen: left, top, width, height (the overlay's units)
    float screen[2]{}; // the overlay's width and height
    float lens[4]{};   // bent (0 / 1), half the picture's width (radians), tan(half the render's height), render aspect
    float projection{}; // the fisheye's (0 equidistant, 1 equisolid, 2 stereographic, 3 orthographic)
    bool look{};       // the look is on: the picture goes into the look's values and back out
    float frame{}, seconds{};
    std::vector<camcorder::EffectPass> passes; // camcorder::plan_effects
};
// Whether the picture shader is ready (it is built on the first ask, away from the render thread:
// false until then), and whether it failed for good.
bool camcorder_picture_pass_ready();
bool camcorder_picture_pass_failed();
// The back buffer's transfer (0 SDR, 1 scRGB, 2 HDR10).
int camcorder_warp_transfer();
// Frame building: draw `chain` over the copy camcorder_warp_request gave, before the overlay draws.
void camcorder_picture_chain(PictureChain chain);
// With the frame's command list open, before the overlay draws (the back buffer in PRESENT):
// copies the game's picture when it was asked for this frame, and draws the picture chain over it.
void camcorder_warp_copy(ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer);
// After the frame's commands were submitted and `signal` set on `fence`.
void camcorder_warp_submitted(ID3D12Fence *fence, UINT64 signal);
// Graphics teardown.
void camcorder_warp_release();
} // namespace dingosdk::overlay
