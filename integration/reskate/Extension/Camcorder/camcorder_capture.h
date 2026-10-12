#pragma once
// The camcorder's recording, the overlay's side: while REC runs, copies the 4:3 picture off the game's
// back buffer (after the fisheye's warp is drawn, camcorder_warp.h, and before the viewfinder's
// readouts, so the file has the lens but a clean picture) into a small ring of readback buffers, and hands the pixels to the video writer
// (camcorder_video.h) once the GPU is done with them. Never waits on the GPU: with every buffer
// still busy, that frame is skipped and the pacer repeats the next.
//
// Render thread only (Extension/UI/Overlay/overlay_render.cpp, under the overlay's render lock).
#include <d3d12.h>
#include <dxgi1_6.h>
#include <imgui.h>
#include <string>

namespace dingosdk::overlay {
// Graphics setup: the swapchain's format, and `transfer` as the overlay works it out (0 SDR,
// 1 scRGB, 2 HDR10).
void camcorder_capture_output(DXGI_FORMAT format, int transfer);
// Every presentation, drawn or not: collects finished copies, starts and finishes clips as REC
// does. True while there is work in flight (the overlay should keep drawing).
bool camcorder_capture_poll();
// A drawn frame, with the command list open and before the overlay draws: works out whether a video
// frame or a still is due, for the copy camcorder_capture_draw makes.
void camcorder_capture_record(ID3D12Device *device, ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer);
// Frame building (draw_camcorder): whether this frame's picture already has the camera's look and
// the date stamp on screen (the recording then leaves them out).
void camcorder_capture_screen(bool look, bool stamp);
// An ImGui draw callback, added by draw_camcorder where the picture is finished: makes the copy
// (the back buffer is a render target then).
void camcorder_capture_draw(const ImDrawList *, const ImDrawCmd *);
// After that frame's commands were submitted and `signal` set on `fence`.
void camcorder_capture_submitted(ID3D12Fence *fence, UINT64 signal);
// Graphics teardown: drops the GPU buffers (a clip being recorded is closed).
void camcorder_capture_release();
// The last clip written ("Saved ..." or why it failed), for the menu.
std::string camcorder_capture_last();
// The newest clip written this game, for review; empty before the first.
std::wstring camcorder_capture_last_clip();
} // namespace dingosdk::overlay
