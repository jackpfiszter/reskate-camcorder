#pragma once
// The camcorder's picture on screen: the fisheye's bend and the camera's look. The game renders a
// flat picture (opened wide enough for the warp: camcorder/vx1000.h, FisheyeRender::warp); the
// overlay copies that frame into a texture and draws it back over the 4:3 picture through a pixel
// shader (the picture pass) that bends it as the fisheye did and gives it the camera's tone,
// colour, sharpening, grain and tape smear (camcorder/look.h, ScreenLook). Clips and stills are
// copied after the pass is drawn (camcorder_capture.h), so they have what the screen has.
//
// Where the shader cannot be built, the bend alone is drawn through a fine ImGui mesh, and the
// look is left to the recording (the old way).
//
// Render thread only (Extension/UI/Overlay/overlay_render.cpp, under the overlay's render lock).
#include <d3d12.h>
#include <dxgi.h>
#include <imgui.h>
#include <optional>

namespace dingosdk::overlay {
// Graphics setup: the device, the SRV heap slot the copy is drawn from, and the back buffer's
// format and transfer (0 SDR, 1 scRGB, 2 HDR10).
void camcorder_warp_heap(ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu,
                         DXGI_FORMAT format, int transfer);
// Frame building (draw_camcorder): asks for this frame's picture to be copied (or, with `copy`
// false, for the last one to be shown again: a film camera's slower cadence). The texture to draw
// it from, when there is one of `width` x `height` (none on the first frame at a new size: then
// draw nothing over the game this frame).
std::optional<ImTextureID> camcorder_warp_request(int width, int height, bool copy = true);

// The picture pass's numbers (the shader's constant buffer, camcorder_warp.cpp).
struct PicturePass {
    float rect[4]{};   // the picture on screen: left, top, width, height
    float screen[4]{}; // the copy: width, height, 1 / width, 1 / height
    float lens[4]{};   // warped (0 / 1), half the picture's width (radians), tan(half the render's height), render aspect
    float tone[4]{};   // look on (0 / 1), black crush, highlight gain, saturation
    float colour[4]{}; // red gain, blue gain, sharpen, grain
    float tape[4]{};   // chroma width, softness (in 720ths), screen pixels per 720th, frame
    float output[4]{}; // transfer, white (nits), highlight bleed (0 / 1), 0
    float camera[4]{}; // the camera's lines (0: the screen's own), light strength (0 off), light reach, 0
};
// Whether the picture pass can be drawn (built on first ask; false for good if it cannot be).
bool camcorder_picture_pass_ready();
// The back buffer's transfer (0 SDR, 1 scRGB, 2 HDR10), for the pass's numbers.
int camcorder_warp_transfer();
// Draws the pass on `list` with these numbers (once a frame), from the texture camcorder_warp_request gave.
void camcorder_picture_pass(ImDrawList *list, const PicturePass &pass);
// With the frame's command list open, before the overlay draws (the back buffer in PRESENT):
// copies the game's picture when it was asked for this frame.
void camcorder_warp_copy(ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer);
// After the frame's commands were submitted and `signal` set on `fence`.
void camcorder_warp_submitted(ID3D12Fence *fence, UINT64 signal);
// Graphics teardown.
void camcorder_warp_release();
} // namespace dingosdk::overlay
