#include "camcorder_page.h"
#include "camcorder_capture.h"
#include "camcorder_runtime.h"
#include "Extension/UI/Overlay/skate_menu_internal.h"
#include "camcorder/commands.h"
#include <imgui.h>
#include <string>

namespace dingosdk::overlay::menu {
namespace {
using namespace dingosdk::camcorder;

void run(SkateMenu &menu, const CallbacksV3 &callbacks, const std::string &command) {
    send_console(menu, callbacks, "camcorder " + command);
}
// A row of small buttons; the ones that are on are drawn as primary.
bool small_button(const char *label, bool on = false) {
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const bool clicked = ImGui::Button(label);
    if (on) ImGui::PopStyleColor();
    return clicked;
}
} // namespace

void camcorder_page(SkateMenu &menu, const Model &, const CallbacksV3 &callbacks) {
    const auto published = camcorder_runtime::published();
    const auto &s = published.frame.status;
    ImGui::BeginChild("camcorder-body", ImVec2(0, page_body_height(menu)));

    begin_card(menu, "camcorder-camera", "SONY VX1000", "The camera of 90s skate video. MiniDV, 4:3, a 10x zoom.");
    info(menu, "Now", describe(s));
    if (primary_button(menu, s.powered ? "TURN THE CAMERA OFF" : "TURN THE CAMERA ON"))
        run(menu, callbacks, s.powered ? "power off" : "power on");
    if (s.powered && primary_button(menu, s.transport == Transport::recording ? "STOP  (STBY)" : "REC"))
        run(menu, callbacks, "rec");
    end_card();

    begin_card(menu, "camcorder-shooting", "SHOOTING");
    field(menu, "Filming", "Hold the camera yourself, or film from a tripod you set up.");
    int mode = s.mode == Mode::tripod ? 1 : 0;
    if (choice(menu, "camcorder-mode", mode, {"HANDHELD", "TRIPOD"}))
        run(menu, callbacks, mode ? "mode tripod" : "mode handheld");
    field(menu, "Lens", "The Century 0.3x fisheye of every 90s part, or the VX1000's own lens.");
    int lens = s.adapter == Adapter::century_fisheye ? 0 : 1;
    if (choice(menu, "camcorder-lens", lens, {"FISHEYE", "STOCK LENS"}))
        run(menu, callbacks, lens ? "lens stock" : "lens fisheye");
    field(menu, "Grip", "Low on a long grip (the fisheye follow shot), at the hip, or at your eye.");
    int grip = static_cast<int>(s.grip);
    if (choice(menu, "camcorder-grip", grip, {"LONG GRIP", "HIP", "EYE LEVEL"}))
        run(menu, callbacks, grip == 0 ? "grip long" : grip == 1 ? "grip hip" : "grip eye");
    bool operate = published.operating;
    if (toggle_row(menu, "Camera controls", "Mouse aims, Q / E zoom; your skater stands still while it is on.", operate))
        run(menu, callbacks, operate ? "operate on" : "operate off");
    bool lock_on = s.lock_on;
    if (toggle_row(menu, "Lock on", "Keep the subject framed by yourself while you skate.", lock_on))
        run(menu, callbacks, lock_on ? "lockon on" : "lockon off");
    bool clean = published.clean_feed;
    if (toggle_row(menu, "Clean picture", "Hide REC, the counter and the hints; keep the frame and the lens.", clean))
        run(menu, callbacks, clean ? "clean on" : "clean off");
    end_card();

    begin_card(menu, "camcorder-subject", "WHO TO FILM", "In a session: the camera can lock on to another skater.");
    if (published.players.empty()) {
        note("Nobody else is here. Solo, a tripod follows you.");
    } else {
        if (small_button("NOBODY", published.subject.empty())) run(menu, callbacks, "subject none");
        for (const auto &name : published.players) {
            ImGui::SameLine();
            ImGui::PushID(name.c_str());
            if (small_button(name.c_str(), name == published.subject)) run(menu, callbacks, "subject " + name);
            ImGui::PopID();
        }
    }
    end_card();

    begin_card(menu, "camcorder-tripods", "TRIPODS", "Up to four at a spot. Set one up, aim it, press REC, go skate.");
    if (primary_button(menu, "SET UP A TRIPOD HERE")) run(menu, callbacks, "tripod place");
    bool any = false;
    for (int i = 0; i < static_cast<int>(tripod_slots); ++i) {
        if (!s.tripods[static_cast<std::size_t>(i)]) continue;
        if (any) ImGui::SameLine();
        any = true;
        const auto label = "TRIPOD " + std::to_string(i + 1);
        if (small_button(label.c_str(), s.mode == Mode::tripod && s.active_tripod == i))
            run(menu, callbacks, "tripod " + std::to_string(i + 1));
    }
    if (any) {
        ImGui::SameLine();
        if (small_button("MOVE IT", s.tripod_setup)) run(menu, callbacks, s.tripod_setup ? "tripod setup off" : "tripod setup on");
        ImGui::SameLine();
        if (small_button("TAKE IT DOWN")) run(menu, callbacks, "tripod remove");
        field(menu, "Head", "Locked off: nothing moves. Manual: you pan it. Follow: it pans after the skater.");
        int head = static_cast<int>(s.head);
        if (choice(menu, "camcorder-head", head, {"LOCKED OFF", "MANUAL", "FOLLOW"}))
            run(menu, callbacks, head == 0 ? "head locked" : head == 1 ? "head manual" : "head follow");
    }
    end_card();

    begin_card(menu, "camcorder-tape", "TAPE", "Every REC to STBY is a clip: an MP4 in Videos\\ReSkate Camcorder.");
    field(menu, "Quality", "HD for sharing, or the VX1000's own 640 x 480.");
    int quality = published.dv ? 1 : 0;
    if (choice(menu, "camcorder-quality", quality, {"HD 1440 x 1080", "DV 640 x 480"}))
        run(menu, callbacks, quality ? "quality dv" : "quality hd");
    field(menu, "Sound", "The game's own sound (nothing else on your PC), or as the VX1000's mic heard it.");
    int sound = static_cast<int>(published.sound);
    if (choice(menu, "camcorder-sound", sound, {"OFF", "GAME", "VX MIC"}))
        run(menu, callbacks, sound == 0 ? "sound off" : sound == 1 ? "sound game" : "sound vx");
    if (small_button("LIST CLIPS")) run(menu, callbacks, "tape clips");
    ImGui::SameLine();
    if (small_button("NEW TAPE")) run(menu, callbacks, "tape new");
    if (const auto last = camcorder_capture_last(); !last.empty()) info(menu, "Last clip", last);
    note("Turn the game's music down in its audio settings for clean session sound.");
    end_card();

    begin_card(menu, "camcorder-controls", "CONTROLS");
    note("With camera controls on or a tripod being set up: the mouse aims (no button needed), Q / E zoom, "
         "W A S D move a tripod, Q / E raise and lower it, Shift + Q / E zoom while setting up. "
         "On a pad: right stick, triggers, left stick, L3.");
    note("Put the camcorder on keys or buttons in SETTINGS > CONTROLS: Camcorder on / off, REC, camera controls, "
         "set up a tripod, next tripod. Every action is also a console command: camcorder status.");
    end_card();

    ImGui::EndChild();
}
} // namespace dingosdk::overlay::menu
