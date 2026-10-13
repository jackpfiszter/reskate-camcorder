// Draws the viewfinder (src/camcorder/viewfinder.h) on ImGui's background draw list, under
// ReSkate's own menus and chat, taking no input: the bars beside the picture, the fisheye housing's shadow
// and the VX1000-style readouts. Call draw_camcorder() from overlay_render.cpp beside
// draw_hall_of_meat(), and count camcorder_pending() among the reasons to render with the
// menu closed.
#include "camcorder_overlay.h"
#include "camcorder_capture.h"
#include "camcorder_review.h"
#include "camcorder_runtime.h"
#include "camcorder_warp.h"
#include "Extension/UI/Overlay/overlay.h"
#include "camcorder/look.h"
#include "camcorder/viewfinder.h"
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <algorithm>
#include <array>
#include <iterator>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

namespace dingosdk::overlay {
namespace {
using namespace dingosdk::camcorder;
ImU32 imgui_colour(const Colour &c) { return IM_COL32(c.r, c.g, c.b, c.a); }
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
// The warp's mesh is drawn as the game drew it: the overlay's shader would otherwise treat the
// game's pixels as sRGB colours and convert them again for an HDR display.
void straight_output(const ImDrawList *, const ImDrawCmd *) {
    const auto *render = static_cast<ImGui_ImplDX12_RenderState *>(ImGui::GetPlatformIO().Renderer_RenderState);
    if (!render || !render->CommandList) return;
    const float none[4]{0, 80, 0, 0};
    render->CommandList->SetGraphicsRoot32BitConstants(0, 4, none, 16);
}

// The flat render, bent into the fisheye's picture: a grid over the picture, each point
// sampling the copy of the game's frame where the lens would have seen it (vx1000.h, warp_uv).
void warp(ImDrawList *list, ImTextureID texture, const Optics &optics, const Rect &picture, float aspect) {
    constexpr int columns = 64, rows = 48;
    const float half_height = 1 / optics.aspect; // the picture's top, in half picture widths
    list->AddCallback(straight_output, nullptr);
    list->PushTextureID(texture);
    list->PrimReserve(columns * rows * 6, (columns + 1) * (rows + 1));
    const auto first = static_cast<unsigned>(list->_VtxCurrentIdx);
    for (int j = 0; j <= rows; ++j)
        for (int i = 0; i <= columns; ++i) {
            const float s = static_cast<float>(i) / columns, r = static_cast<float>(j) / rows;
            const auto uv = warp_uv(optics, -1 + 2 * s, half_height * (1 - 2 * r), aspect);
            list->PrimWriteVtx({picture.x + picture.width * s, picture.y + picture.height * r},
                               {std::clamp(uv.u, 0.0f, 1.0f), std::clamp(uv.v, 0.0f, 1.0f)}, IM_COL32_WHITE);
        }
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < columns; ++i) {
            const auto a = static_cast<ImDrawIdx>(first + static_cast<unsigned>(j * (columns + 1) + i));
            const auto b = static_cast<ImDrawIdx>(a + 1), c = static_cast<ImDrawIdx>(a + columns + 1), d = static_cast<ImDrawIdx>(c + 1);
            list->PrimWriteIdx(a);
            list->PrimWriteIdx(b);
            list->PrimWriteIdx(d);
            list->PrimWriteIdx(a);
            list->PrimWriteIdx(d);
            list->PrimWriteIdx(c);
        }
    list->PopTextureID();
    list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
double seconds_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// This frame's picture (camcorder_warp.h): the lens, the light and the camera's effects.
PictureChain chain_numbers(const camcorder_runtime::Published &p, const Rect &picture, float width, float height,
                           std::uint32_t frame, double frame_rate) {
    static const double start = seconds_now();
    const auto &o = p.frame.optics;
    constexpr float radians = 3.14159265f / 180;
    PictureChain chain;
    chain.rect[0] = picture.x;
    chain.rect[1] = picture.y;
    chain.rect[2] = picture.width;
    chain.rect[3] = picture.height;
    chain.screen[0] = width;
    chain.screen[1] = height;
    chain.lens[0] = o.warped ? 1.0f : 0.0f;
    chain.lens[1] = o.horizontal_fov * 0.5f * radians;
    chain.lens[2] = std::tan(std::clamp(o.render_vertical_fov, 1.0f, 179.0f) * 0.5f * radians);
    chain.lens[3] = width / height;
    chain.projection = static_cast<float>(static_cast<int>(o.projection));
    chain.look = p.finish.dv.on;
    if (p.light) {
        const auto &lamp = p.finish.light;
        chain.light[0] = std::clamp(lamp.strength, 0.0f, 10.0f);
        chain.light[1] = std::clamp(lamp.reach, 0.1f, 3.0f);
        chain.light[2] = std::clamp(lamp.exposure, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) chain.light[4 + i] = static_cast<float>(lamp.colour[static_cast<std::size_t>(i)]) / 255.0f;
    }
    const double seconds = seconds_now() - start;
    chain.frame = static_cast<float>(frame % 65536u);
    chain.seconds = static_cast<float>(std::fmod(seconds, 65536.0));
    chain.passes = plan_effects(p.effects, p.finish.dv, EffectTime{seconds, frame, frame_rate, 0.37f});
    return chain;
}

// The camera's date stamp, over the picture: what it records is what it shows.
void draw_stamp(ImDrawList *list, const camcorder_runtime::Published &p, const Rect &picture) {
    const auto time = stamp_clock(std::time(nullptr), p.finish);
    std::tm local{};
    localtime_s(&local, &time);
    const auto marks = stamp_marks(static_cast<int>(picture.width), static_cast<int>(picture.height), local,
                                   p.finish.stamp_year, p.finish.stamp);
    // On whole screen pixels, so its dots are the same size in every frame.
    const float left = std::round(picture.x), top = std::round(picture.y);
    const auto box = [&](const PixelBox &b, ImU32 colour) {
        list->AddRectFilled({left + static_cast<float>(b.x), top + static_cast<float>(b.y)},
                            {left + static_cast<float>(b.x + b.width), top + static_cast<float>(b.y + b.height)}, colour);
    };
    for (const auto &b : marks.edges) box(b, IM_COL32(marks.outline_colour[0], marks.outline_colour[1], marks.outline_colour[2], 255));
    for (const auto &b : marks.dots) box(b, IM_COL32(marks.colour[0], marks.colour[1], marks.colour[2], 255));
}
// A dark panel with a thin edge, for the quick menu and the guide.
void panel(ImDrawList *list, ImVec2 from, ImVec2 to) {
    list->AddRectFilled(from, to, IM_COL32(8, 8, 8, 200), 6);
    list->AddRect(from, to, IM_COL32(255, 255, 255, 60), 6, 0, 1.0f);
}

// The quick menu: a column on the right of the picture, the chosen line lit in the EVF's amber.
void draw_quick(ImDrawList *list, ImFont *font, const camcorder_runtime::Published &p, const Rect &picture) {
    const float size = std::clamp(picture.height / 28, 13.0f, 30.0f), line = size * 1.45f;
    const float width = std::max(picture.width * 0.36f, size * 15), pad = size * 0.7f;
    const float height = line * static_cast<float>(p.quick.size()) + pad * 2 + line;
    const ImVec2 from{picture.x + picture.width - width - picture.width * 0.04f, picture.y + (picture.height - height) * 0.5f};
    panel(list, from, {from.x + width, from.y + height});
    list->AddText(font, size * 0.8f, {from.x + pad, from.y + pad * 0.6f}, IM_COL32(255, 190, 40, 255), "CAMCORDER");
    for (std::size_t i = 0; i < p.quick.size(); ++i) {
        const auto &[label, value] = p.quick[i];
        const float y = from.y + pad + line * static_cast<float>(i + 1);
        const bool chosen = static_cast<int>(i) == p.quick_index;
        if (chosen) list->AddRectFilled({from.x + 3, y - line * 0.12f}, {from.x + width - 3, y + line * 0.88f}, IM_COL32(255, 190, 40, 60), 4);
        const auto colour = chosen ? IM_COL32(255, 210, 90, 255) : IM_COL32(235, 235, 235, 255);
        list->AddText(font, size, {from.x + pad, y}, colour, label.c_str());
        if (!value.empty()) {
            const auto shown = value + (chosen ? "  >" : "");
            const auto w = font->CalcTextSizeA(size * 0.85f, FLT_MAX, 0, shown.c_str()).x;
            list->AddText(font, size * 0.85f, {from.x + width - pad - w, y + size * 0.08f}, IM_COL32(180, 180, 180, 255), shown.c_str());
        }
    }
}

// The first-use guide: the few controls that matter, over the middle of the picture.
void draw_guide(ImDrawList *list, ImFont *font, const Rect &picture) {
    static constexpr const char *lines[] = {
        "THE CAMCORDER, QUICKLY",
        "REC / STBY: F6 after SET UP KEYS (the CAMCORDER page), or REC there",
        "It points itself at who you film. F9: you aim it (mouse or right stick) as you skate",
        "Zoom: wheel or triggers      Height: LB / RB",
        "Quick menu: F7: camera, lens, look, light, height, tripod",
        "Tripod: F10 sets one up; move and aim it; F10 again (or REC), then skate",
        "Esc or Start: back out of the quick menu or a tripod's setup",
        "Everything else: the CAMCORDER page in the ReSkate menu",
    };
    const float size = std::clamp(picture.height / 34, 12.0f, 26.0f), line = size * 1.4f, pad = size * 0.9f;
    float width = 0;
    for (const auto *text : lines) width = std::max(width, font->CalcTextSizeA(size, FLT_MAX, 0, text).x);
    width = std::min(width + pad * 2, picture.width * 0.92f);
    const float height = line * static_cast<float>(std::size(lines)) + pad * 2;
    const ImVec2 from{picture.x + (picture.width - width) * 0.5f, picture.y + picture.height * 0.16f};
    panel(list, from, {from.x + width, from.y + height});
    for (std::size_t i = 0; i < std::size(lines); ++i)
        list->AddText(font, size, {from.x + pad, from.y + pad + line * static_cast<float>(i)},
                      i == 0 ? IM_COL32(255, 190, 40, 255) : IM_COL32(235, 235, 235, 255), lines[i]);
}

// Playing back the last clip: the clip over the whole picture, and the tape's place in it, as a
// camera shows in PLAY. The readouts of shooting are put away while it plays.
void draw_review(ImDrawList *list, ImFont *font, const ReviewPicture &review, const Rect &picture) {
    const ImVec2 from{picture.x, picture.y}, to{picture.x + picture.width, picture.y + picture.height};
    if (review.texture) list->AddImage(review.texture, from, to);
    else list->AddRectFilled(from, to, IM_COL32(0, 0, 0, 255));
    const float size = std::clamp(picture.height / 22, 13.0f, 36.0f), margin = picture.height * 0.05f;
    const auto as_clock = [](double seconds) {
        const int whole = static_cast<int>(std::max(seconds, 0.0));
        char text[16];
        std::snprintf(text, sizeof text, "%d:%02d", whole / 60, whole % 60);
        return std::string(text);
    };
    const auto place = as_clock(review.seconds) + (review.length > 0 ? " / " + as_clock(review.length) : std::string());
    const auto text = [&](float x, float y, const char *what) {
        list->AddText(font, size, {x + 1, y + 1}, IM_COL32(0, 0, 0, 180), what);
        list->AddText(font, size, {x, y}, IM_COL32(235, 235, 235, 255), what);
    };
    text(picture.x + margin, picture.y + margin, "PLAY >");
    const auto w = font->CalcTextSizeA(size, FLT_MAX, 0, place.c_str()).x;
    text(picture.x + picture.width - margin - w, picture.y + margin, place.c_str());
    if (review.length > 0) {
        const float y = picture.y + picture.height - margin, left = picture.x + margin, right = picture.x + picture.width - margin;
        const float done = static_cast<float>(std::clamp(review.seconds / review.length, 0.0, 1.0));
        list->AddRectFilled({left, y - 3}, {right, y}, IM_COL32(255, 255, 255, 60));
        list->AddRectFilled({left, y - 3}, {left + (right - left) * done, y}, IM_COL32(255, 255, 255, 220));
    }
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
    // The camera's own viewfinder, and what its text fields need.
    options.layout = &p.viewfinder;
    options.format = p.finish.look.format;
    {
        const auto time = stamp_clock(std::time(nullptr), p.finish);
        std::tm local{};
        localtime_s(&local, &time);
        options.clock = local;
    }
    options.year = p.finish.stamp_year;
    options.date_order = p.finish.stamp.date;
    options.light = p.light;
    // While the camcorder's picture is up the overlay must draw something every frame, or it skips
    // the frame and the picture is not copied for a clip or a still (camcorder_capture.h): one
    // pixel, all but transparent.
    if (p.frame.status.powered && p.frame.shot.valid)
        ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, {1, 1}, IM_COL32(0, 0, 0, 1));
    const auto v = layout_viewfinder(p.frame, io.DisplaySize.x, io.DisplaySize.y, options);
    if (!v.visible) return;
    auto *list = ImGui::GetBackgroundDrawList();
    auto *font = ImGui::GetFont();
    // Reviewing the last clip: no live picture, nothing recorded (REC stops the review).
    if (const auto review = camcorder_review(p)) {
        draw_review(list, font, *review, v.picture);
        if (!p.quick.empty()) draw_quick(list, font, p, v.picture);
        if (p.guide) draw_guide(list, font, v.picture);
        return;
    }
    const int height = static_cast<int>(io.DisplaySize.y + 0.5f);
    const bool warped = p.frame.optics.warped;
    // A film camera's picture moves at its own rate (Super 8's 18): the copy is held in between.
    static double next_picture = 0;
    static std::uint32_t pictures = 0;
    const double film_rate = screen_frame_rate(p.finish), now = seconds_now();
    bool copy = true;
    if (film_rate > 0) {
        copy = now >= next_picture;
        if (copy) next_picture = (now - next_picture > 0.25 ? now : next_picture) + 1 / film_rate;
    }
    bool look_on_screen = false;
    const bool chain_wanted = warped || p.finish.dv.on || film_rate > 0 || p.light;
    if (chain_wanted && height > 0 && camcorder_picture_pass_ready()) {
        if (camcorder_warp_request(copy)) {
            if (copy) ++pictures; // the grain changes with the picture
            const double rate = film_rate > 0 ? film_rate : std::max(io.Framerate, 1.0f);
            camcorder_picture_chain(chain_numbers(p, v.picture, io.DisplaySize.x, io.DisplaySize.y, pictures, rate));
            look_on_screen = p.finish.dv.on;
        }
    } else if (warped && height > 0) {
        // No picture pass on this system: the bend alone, as a mesh; the look is left to the recording.
        if (const auto texture = camcorder_warp_request())
            warp(list, *texture, p.frame.optics, v.picture, io.DisplaySize.x / io.DisplaySize.y);
    }
    // The look or the light asked for and not on screen for a while: say why, once a game.
    {
        static double missing_since = 0;
        static bool said = false;
        const bool wanted = p.finish.dv.on || p.light;
        // (While the shader is still being built, a moment after the camera first comes on, it is not missing.)
        const bool building = !camcorder_picture_pass_ready() && !camcorder_picture_pass_failed();
        if (!wanted || look_on_screen || building || (p.light && camcorder_picture_pass_ready() && camcorder_warp_why_not().empty())) {
            missing_since = 0;
        } else if (missing_since == 0) {
            missing_since = now;
        } else if (now - missing_since > 3 && !said) {
            said = true;
            const auto why = camcorder_picture_pass_failed() ? std::string("its shader could not be set up (logs\\ReSkate.log has why)")
                                                             : camcorder_warp_why_not();
            notify(NoticeLevel::warning, "The camcorder's look isn't showing on screen.",
                   "Clips and stills still get it. Why: " + (why.empty() ? std::string("unknown") : why) + ".");
        }
    }
    // With fields, a stamp in the copy would be split between them and twitter up and down in
    // clips: the recording then burns its own after the fields, and the screen's comes after the copy.
    const bool stamp_in_copy = p.finish.date_stamp && !p.finish.fields();
    if (stamp_in_copy) draw_stamp(list, p, v.picture);
    // A clip's or a still's copy of the picture is made here: after the picture pass and the stamp,
    // before the readouts. It is told what the screen already did, so it is not done twice.
    camcorder_capture_screen(look_on_screen, stamp_in_copy);
    list->AddCallback(camcorder_capture_draw, nullptr);
    if (p.finish.date_stamp && !stamp_in_copy) draw_stamp(list, p, v.picture);
    if (v.vignette) shadow(list, *v.vignette, v.picture);
    for (const auto &bar : v.bars)
        list->AddRectFilled({bar.x, bar.y}, {bar.x + bar.width, bar.y + bar.height}, IM_COL32(0, 0, 0, 255));
    if (v.safe_frame) {
        const auto &f = *v.safe_frame;
        list->AddRect({f.x, f.y}, {f.x + f.width, f.y + f.height}, IM_COL32(255, 255, 255, 70), 0, 0, 1.5f);
    }
    // The readouts: the camera's shapes, then its text, each with a soft shadow to read over any picture.
    for (const auto &m : v.marks) {
        const auto colour = imgui_colour(m.colour);
        const auto shade = IM_COL32(0, 0, 0, m.colour.a * 3 / 4);
        switch (m.kind) {
        case Mark::Kind::dot: list->AddCircleFilled({m.x0, m.y0}, m.x1, colour); break;
        case Mark::Kind::box: list->AddRectFilled({m.x0, m.y0}, {m.x1, m.y1}, colour); break;
        case Mark::Kind::outline:
            if (m.shadow) list->AddRect({m.x0 + 1, m.y0 + 1}, {m.x1 + 1, m.y1 + 1}, shade, 0, 0, m.thickness);
            list->AddRect({m.x0, m.y0}, {m.x1, m.y1}, colour, 0, 0, m.thickness);
            break;
        case Mark::Kind::line:
            if (m.shadow) list->AddLine({m.x0 + 1, m.y0 + 1}, {m.x1 + 1, m.y1 + 1}, shade, m.thickness);
            list->AddLine({m.x0, m.y0}, {m.x1, m.y1}, colour, m.thickness);
            break;
        }
    }
    for (const auto &label : v.labels) {
        const auto size = font->CalcTextSizeA(label.size, FLT_MAX, 0, label.text.c_str());
        const float x = label.align == Align::right ? label.x - size.x
                      : label.align == Align::centre ? label.x - size.x * 0.5f : label.x;
        // A one-pixel shadow, as an EVF's white text sits over any picture.
        if (label.shadow) list->AddText(font, label.size, {x + 1, label.y + 1}, IM_COL32(0, 0, 0, label.colour.a * 180 / 255), label.text.c_str());
        list->AddText(font, label.size, {x, label.y}, imgui_colour(label.colour), label.text.c_str());
    }
    // Over the readouts, and after the recording's copy: the quick menu and the first-use guide.
    if (!p.quick.empty()) draw_quick(list, font, p, v.picture);
    if (p.guide) draw_guide(list, font, v.picture);
}
} // namespace dingosdk::overlay
