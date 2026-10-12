// Draws the viewfinder (src/camcorder/viewfinder.h) on ImGui's background draw list, under
// ReSkate's own menus and chat, taking no input: the 4:3 bars, the fisheye housing's shadow
// and the VX1000-style readouts. Call draw_camcorder() from overlay_render.cpp beside
// draw_hall_of_meat(), and count camcorder_pending() among the reasons to render with the
// menu closed.
#include "camcorder_overlay.h"
#include "camcorder_runtime.h"
#include "camcorder/viewfinder.h"
#include <imgui.h>
#include <cfloat>
#include <cmath>

namespace dingosdk::overlay {
namespace {
using namespace dingosdk::camcorder;
ImU32 ink(Ink value) {
    switch (value) {
    case Ink::red: return IM_COL32(235, 40, 40, 255);
    case Ink::amber: return IM_COL32(255, 190, 40, 255);
    case Ink::white: default: return IM_COL32(240, 240, 240, 255);
    }
}
// Black outside a circle, inside the picture: a ring of quads from the circle out to a radius
// that covers the picture's corners.
void shadow(ImDrawList *list, const Circle &c, const Rect &picture) {
    const float outer = std::hypot(picture.width, picture.height); // past every corner
    constexpr int segments = 96;
    const ImU32 black = IM_COL32(0, 0, 0, 255);
    list->PushClipRect({picture.x, picture.y}, {picture.x + picture.width, picture.y + picture.height}, true);
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2 * 3.14159265f * i / segments, a1 = 2 * 3.14159265f * (i + 1) / segments;
        const ImVec2 p0{c.x + std::cos(a0) * c.radius, c.y + std::sin(a0) * c.radius};
        const ImVec2 p1{c.x + std::cos(a1) * c.radius, c.y + std::sin(a1) * c.radius};
        const ImVec2 q1{c.x + std::cos(a1) * outer, c.y + std::sin(a1) * outer};
        const ImVec2 q0{c.x + std::cos(a0) * outer, c.y + std::sin(a0) * outer};
        list->AddQuadFilled(p0, p1, q1, q0, black);
    }
    // A soft edge: the real one is a blurry lens barrel, not a cut-out.
    for (int ring = 0; ring < 6; ++ring)
        list->AddCircle({c.x, c.y}, c.radius - ring * 2.0f, IM_COL32(0, 0, 0, 160 - ring * 25), segments, 2.5f);
    list->PopClipRect();
}
} // namespace

bool camcorder_pending() {
    const auto p = camcorder_runtime::published();
    return p.frame.status.powered && p.frame.shot.valid;
}

void draw_camcorder() {
    const auto p = camcorder_runtime::published();
    const auto &io = ImGui::GetIO();
    if (io.DisplaySize.y > 0) camcorder_runtime::note_render_aspect(io.DisplaySize.x / io.DisplaySize.y);
    ViewfinderOptions options;
    options.clean_feed = p.clean_feed;
    options.blink_time = p.blink;
    options.hint = p.hint;
    // While recording the overlay must draw something every frame, or it skips the frame and the
    // picture is not copied for the clip (camcorder_capture.h): one pixel, all but transparent.
    if (p.frame.status.transport == Transport::recording)
        ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, {1, 1}, IM_COL32(0, 0, 0, 1));
    const auto v = layout_viewfinder(p.frame, io.DisplaySize.x, io.DisplaySize.y, options);
    if (!v.visible) return;
    auto *list = ImGui::GetBackgroundDrawList();
    if (v.vignette) shadow(list, *v.vignette, v.picture);
    for (const auto &bar : v.bars)
        list->AddRectFilled({bar.x, bar.y}, {bar.x + bar.width, bar.y + bar.height}, IM_COL32(0, 0, 0, 255));
    if (v.safe_frame) {
        const auto &f = *v.safe_frame;
        list->AddRect({f.x, f.y}, {f.x + f.width, f.y + f.height}, IM_COL32(255, 255, 255, 70), 0, 0, 1.5f);
    }
    if (v.rec_dot) list->AddCircleFilled({v.rec_dot_x, v.rec_dot_y}, v.rec_dot_radius, ink(Ink::red));
    auto *font = ImGui::GetFont();
    for (const auto &label : v.labels) {
        const auto size = font->CalcTextSizeA(label.size, FLT_MAX, 0, label.text.c_str());
        const float x = label.align == Align::right ? label.x - size.x
                      : label.align == Align::centre ? label.x - size.x * 0.5f : label.x;
        // A one-pixel shadow, as an EVF's white text sits over any picture.
        list->AddText(font, label.size, {x + 1, label.y + 1}, IM_COL32(0, 0, 0, 180), label.text.c_str());
        list->AddText(font, label.size, {x, label.y}, ink(label.ink), label.text.c_str());
    }
    if (v.battery.visible) {
        const auto &b = v.battery.body;
        list->AddRect({b.x, b.y}, {b.x + b.width, b.y + b.height}, ink(Ink::white), 0, 0, 1.5f);
        list->AddRectFilled({b.x + b.width, b.y + b.height * 0.3f}, {b.x + b.width + 3, b.y + b.height * 0.7f}, ink(Ink::white));
        const float bars = std::ceil(v.battery.level * 4);
        for (int i = 0; i < static_cast<int>(bars); ++i) {
            const float w = (b.width - 4) / 4;
            list->AddRectFilled({b.x + 2 + i * w + 1, b.y + 2}, {b.x + 2 + (i + 1) * w - 1, b.y + b.height - 2}, ink(Ink::white));
        }
    }
    if (v.zoom.visible) {
        const auto &t = v.zoom.track;
        list->AddRect({t.x, t.y}, {t.x + t.width, t.y + t.height}, ink(Ink::white), 0, 0, 1.5f);
        list->AddRectFilled({t.x, t.y}, {t.x + t.width * v.zoom.position, t.y + t.height}, ink(Ink::white));
    }
}
} // namespace dingosdk::overlay
