#include "camcorder_page.h"
#include "camcorder_capture.h"
#include "camcorder_runtime.h"
#include "Extension/UI/Overlay/skate_menu_internal.h"
#include "camcorder/commands.h"
#include "camcorder/recorder.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

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

// The keys SET UP KEYS binds, where they are free: F5 to F10 (F1 and F2 answer server votes, F12
// is Steam's screenshot).
struct Recommended {
    const char *bind, *what;
    std::uint32_t key;
};
constexpr Recommended recommended[]{
    {"camcorder", "on / off", 0x74}, {"camrec", "REC / STBY", 0x75}, {"camquick", "quick menu", 0x76},
    {"camstill", "still", 0x77},     {"camoperate", "full camera control", 0x78}, {"camtripod", "tripod: set up / done", 0x79},
};
int bind_index(std::string_view name) {
    for (std::size_t i = 0; i < action_binds.size(); ++i)
        if (action_binds[i].name == name) return static_cast<int>(i);
    return -1;
}

// Buttons that wrap: mods can add any number of cameras and lenses.
void pick(SkateMenu &menu, const CallbacksV3 &callbacks, const char *id,
          const std::vector<camcorder_runtime::Published::Choice> &items, const std::string &current, const char *verb,
          const char *none) {
    ImGui::PushID(id);
    bool first = true;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const auto button = [&](const std::string &label, const std::string &word, bool on, const std::string &tip) {
        const float width = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        if (!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width < right) ImGui::SameLine();
        first = false;
        ImGui::PushID(word.c_str());
        if (small_button(label.c_str(), on)) run(menu, callbacks, std::string(verb) + " " + word);
        if (!tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
        ImGui::PopID();
    };
    if (none) button(none, "stock", current.empty(), "");
    for (const auto &item : items) button(item.name, item.id, item.id == current, item.source.empty() ? "" : "From " + item.source);
    ImGui::PopID();
}
// A player's name as a button shows it: "##" would hide the rest from ImGui, and a name of nothing
// but spaces would be an empty button.
std::string button_name(std::string name) {
    for (std::size_t at = name.find("##"); at != std::string::npos; at = name.find("##", at + 2)) name.replace(at, 2, "# ");
    if (name.find_first_not_of(' ') == std::string::npos) name = "PLAYER";
    return name;
}
// A card that opens and closes from its title, closed until wanted (it stays as left).
bool folding_card(SkateMenu &menu, const char *id, const char *title, bool &open) {
    begin_card(menu, id);
    ImGui::PushID(id);
    const std::string label = std::string(open ? "-  " : "+  ") + title;
    if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, 0))) open = !open;
    ImGui::PopID();
    return open;
}
} // namespace

void camcorder_page(SkateMenu &menu, const Model &model, const CallbacksV3 &callbacks) {
    const auto published = camcorder_runtime::published();
    const auto &s = published.frame.status;
    const auto &finish = published.finish;
    ImGui::BeginChild("camcorder-body", ImVec2(0, page_body_height(menu)));

    // What most visits are for, on one card: rolling, the tape, the camera, how and who it films.
    begin_card(menu, "camcorder-camera", "CAMCORDER", "Film yourself and your friends, 90s skate video style.");
    info(menu, "Now", describe(s));
    if (primary_button(menu, s.powered ? "TURN THE CAMERA OFF" : "TURN THE CAMERA ON"))
        run(menu, callbacks, s.powered ? "power off" : "power on");
    if (s.powered && primary_button(menu, s.transport == Transport::recording ? "STOP  (STBY)" : "REC"))
        run(menu, callbacks, "rec");
    if (s.powered) {
        if (small_button("TAKE A STILL")) run(menu, callbacks, "still");
        ImGui::SameLine();
    }
    if (small_button("CAPTURE THE TAPE")) run(menu, callbacks, "tape capture");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("This tape's clips, back to back in one MP4, as a tape went into the computer.");
    ImGui::SameLine();
    if (small_button("NEW TAPE")) run(menu, callbacks, "tape new");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The next clips start a new tape (CAPTURE THE TAPE then joins only those).");
    ImGui::SameLine();
    if (small_button("OPEN MY CLIPS")) run(menu, callbacks, "open clips");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Videos\\ReSkate Camcorder: the clips, and the stills in Stills.");
    if (const auto last = camcorder_capture_last(); !last.empty()) info(menu, "Last", last);

    field(menu, "Camera", "Each has its own zoom, picture, frame rate and clip size. Mods add more (hover for which).");
    pick(menu, callbacks, "camcorder-cameras", published.cameras, s.camera_id, "camera", nullptr);
    field(menu, "Lens", "The fisheye of every 90s part, the camera's own lens, or a mod's.");
    pick(menu, callbacks, "camcorder-lenses", published.lenses, s.lens_id, "lens", "STOCK LENS");
    field(menu, "Hold it", "In your hand (R / F or the bumpers raise and lower it), or on the board's nose, just off the ground.");
    int held = s.grip_id == board_cam().id ? 1 : 0;
    if (choice(menu, "camcorder-rig", held, {"HANDHELD", "BOARD CAM"})) run(menu, callbacks, held ? "rig board" : "rig handheld");
    if (s.mode == Mode::handheld) {
        field(menu, "Camera control", "Film while you skate (the camera has the mouse, right stick, triggers, bumpers and Q / E / R / F), or stop and work the camera with everything, roll too (A / D or the left stick). Esc or Start goes back to skating.");
        int full = published.operating ? 1 : 0;
        if (choice(menu, "camcorder-operate", full, {"FILM WHILE SKATING", "FULL CAMERA CONTROL"}))
            run(menu, callbacks, full ? "operate on" : "operate off");
    }

    field(menu, "Who to film", "The camera follows them; the mouse or right stick nudges the framing. Nobody: you aim it. Closest: whoever is nearest the camera, you too.");
    if (small_button("ME / NOBODY", published.subject_id == 0 && !published.closest)) run(menu, callbacks, "subject none");
    ImGui::SameLine();
    if (small_button("CLOSEST", published.closest)) run(menu, callbacks, "subject closest");
    if (ImGui::IsItemHovered() && published.closest && !published.subject.empty())
        ImGui::SetTooltip("Filming %s now.", published.subject.c_str());
    // Picked by id: any name works, whatever letters it has (and two players can share one).
    for (const auto &player : published.players) {
        ImGui::SameLine();
        const auto id = std::to_string(player.id);
        ImGui::PushID(id.c_str());
        if (small_button(button_name(player.name).c_str(), !published.closest && player.id == published.subject_id))
            run(menu, callbacks, "subject " + id);
        ImGui::PopID();
    }
    field(menu, "Frame on", "Where the camera points on them: the board, the whole skater, or their head.");
    int frame_on = published.frame_on;
    if (choice(menu, "camcorder-frame", frame_on, {"BOARD", "WHOLE SKATER", "HEAD"}))
        run(menu, callbacks, frame_on == 0 ? "frame board" : frame_on == 1 ? "frame body" : "frame head");

    bool dv_look = finish.dv.on;
    const std::string look_label = finish.look.format + " look";
    if (toggle_row(menu, look_label.c_str(), "The camera's own picture: its lines, colour, grain and quirks. Off: the game's clean picture.", dv_look))
        run(menu, callbacks, dv_look ? "look dv" : "look clean");
    if (finish.stamp.fitted) {
        bool stamp = finish.date_stamp;
        if (toggle_row(menu, "Date stamp", "The camera's date and time in the corner (set its clock under LOOK DETAILS).", stamp))
            run(menu, callbacks, stamp ? "date on" : "date off");
    }
    field(menu, "Sound", "The game's own sound as the camera hears it from where it is (nothing else on your PC). Turn the game's music down for clean session sound.");
    int sound = published.sound == Sound::off ? 0 : 1;
    if (choice(menu, "camcorder-sound", sound, {"OFF", "GAME"})) run(menu, callbacks, sound ? "sound game" : "sound off");
    end_card();

    // Tripods: what to do, in order, on the card.
    static bool tripods_open = true;
    if (folding_card(menu, "camcorder-tripods", "TRIPODS", tripods_open)) {
        note("1 Set one up where you stand.  2 Carry it (WASD), aim it (mouse), zoom (Q / E), height (R / F).  3 The tripod key again, or REC: it's set.  4 Skate: it films you.");
        if (primary_button(menu, s.tripod_setup ? "DONE SETTING UP" : "SET UP A TRIPOD HERE"))
            run(menu, callbacks, s.tripod_setup ? "tripod done" : "tripod place");
        bool any = false;
        for (int i = 0; i < static_cast<int>(tripod_slots); ++i) {
            if (!s.tripods[static_cast<std::size_t>(i)]) continue;
            if (any) ImGui::SameLine();
            any = true;
            const auto label = "TRIPOD " + std::to_string(i + 1);
            if (small_button(label.c_str(), s.mode == Mode::tripod && s.active_tripod == i))
                run(menu, callbacks, "tripod " + std::to_string(i + 1));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Look through this tripod (up to four stand at once: cut between them).");
        }
        if (any) {
            ImGui::SameLine();
            if (small_button("HANDHELD", s.mode == Mode::handheld)) run(menu, callbacks, "mode handheld");
            // The tripod these act on: the one in use, or else the first standing.
            int which = s.active_tripod;
            for (int i = 0; which < 0 && i < static_cast<int>(tripod_slots); ++i)
                if (s.tripods[static_cast<std::size_t>(i)]) which = i;
            const auto number = std::to_string(which + 1);
            if (small_button("MOVE IT", s.tripod_setup)) {
                if (s.tripod_setup) run(menu, callbacks, "tripod done");
                else {
                    run(menu, callbacks, "tripod " + number);
                    run(menu, callbacks, "tripod setup on");
                }
            }
            ImGui::SameLine();
            if (small_button("TAKE IT DOWN")) run(menu, callbacks, "tripod remove " + number);
            field(menu, "Head", "Follow: it pans after who it films. Locked off: it holds the shot you set.");
            int head = s.head == TripodRig::Head::follow ? 1 : 0;
            if (choice(menu, "camcorder-head", head, {"LOCKED OFF", "FOLLOW"})) run(menu, callbacks, head ? "head follow" : "head locked");
        }
    }
    end_card();

    static bool look_open = false;
    if (folding_card(menu, "camcorder-look", "LOOK DETAILS", look_open)) {
        if (finish.stamp.fitted) {
            // The camera's clock, set as on a real camcorder: it runs on from what you set.
            field(menu, "Date and time", "What the stamp says, running on from when you set it: 1996-10-12 and 16:03 (the time can be left empty).");
            static char date[16] = "1996-10-12", hhmm[8] = "";
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000-00-00  ").x + ImGui::GetStyle().FramePadding.x * 2);
            ImGui::InputText("##camcorder-date", date, sizeof(date));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("00:00  ").x + ImGui::GetStyle().FramePadding.x * 2);
            ImGui::InputTextWithHint("##camcorder-time", "HH:MM", hhmm, sizeof(hhmm));
            ImGui::SameLine();
            if (small_button("SET")) run(menu, callbacks, std::string("date set ") + date + (hhmm[0] ? std::string(" ") + hhmm : std::string()));
            ImGui::SameLine();
            if (small_button("REAL TIME", finish.stamp_offset == 0 && finish.stamp_year == 0)) {
                run(menu, callbacks, "date set now");
                run(menu, callbacks, "date year now");
            }
        }
        bool clean = published.clean_feed;
        if (toggle_row(menu, "Clean viewfinder", "Hide REC, the counter and the hints; keep the frame and the lens.", clean))
            run(menu, callbacks, clean ? "clean on" : "clean off");
        if (finish.dv.on) {
            const auto part = [&](const char *label, const char *detail, bool on, const char *word) {
                bool value = on;
                if (toggle_row(menu, label, detail, value)) run(menu, callbacks, std::string("dv ") + word + (value ? " on" : " off"));
            };
            if (interlaced(finish.motion)) part("Fields", "60 (or 50) a second: smooth video motion in clips, half the lines each.", finish.dv.fields, "fields");
            part("Tone and colour", "The camera's tone, colour and glow.", finish.dv.highlights, "highlights");
            part("White balance", "The colour drifts warm and cool.", finish.dv.white_balance, "balance");
            part("Sharpening", "Bright and dark halos on hard edges.", finish.dv.sharpen, "sharpen");
            part("Tape", "The camera's lines, colour smear and the tape's own faults.", finish.dv.chroma, "chroma");
            part("Grain", "Sensor noise, film grain and dust.", finish.dv.grain, "grain");
        }
    }
    end_card();

    static bool controls_open = false;
    if (folding_card(menu, "camcorder-controls", "CONTROLS", controls_open)) {
        note("Filming: the mouse or right stick aims, Q / E (or the wheel, or the triggers) zoom, R / F (or the bumpers) raise and lower it. You keep skating. The same setting up a tripod, with WASD to carry it. Esc or Start backs out of the quick menu and a tripod's setup.");
        const bool can_bind = model.bindings.available && callbacks.queue_console_command;
        if (primary_button(menu, "SET UP KEYS (F5 TO F10)", can_bind)) {
            int done = 0;
            for (const auto &r : recommended) {
                const int index = bind_index(r.bind);
                if (index < 0 || model.bindings.action_combos[static_cast<std::size_t>(index)]) continue; // already bound
                const std::uint32_t combo = keyboard_binding_tag | r.key;
                const auto &b = model.bindings;
                bool taken = false;
                for (const auto existing : b.action_combos) taken = taken || existing == combo;
                for (const auto existing : {b.freecam_controller_combo, b.freecam_combo, b.tp_to_freecam_combo, b.noclip_combo,
                                            b.forward_velocity_combo, b.up_velocity_combo, b.offboard_up_velocity_combo,
                                            b.vote_yes_combo, b.vote_no_combo})
                    taken = taken || existing == combo;
                if (taken) continue;
                send_console(menu, callbacks, std::string("bind ") + r.bind + " " + std::to_string(combo));
                ++done;
            }
            if (!done) feedback(menu, "Nothing to set up: those are bound already, or the keys are taken.");
        }
        for (const auto &r : recommended) {
            const int index = bind_index(r.bind);
            if (index < 0) continue;
            const auto combo = model.bindings.action_combos[static_cast<std::size_t>(index)];
            info(menu, r.what, combo ? controller_combo_label(combo) : std::string("not bound"));
        }
        note("Change any of them, or add pad buttons, in SETTINGS > CONTROLS.");
        static constexpr float speeds[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f};
        int speed = 2;
        for (int i = 0; i < 6; ++i)
            if (std::abs(speeds[i] - published.sensitivity) < 0.01f) speed = i;
        field(menu, "Aim speed", "How fast the mouse and the stick aim.");
        if (choice(menu, "camcorder-speed", speed, {"0.5x", "0.75x", "1x", "1.5x", "2x", "3x"})) {
            char text[32];
            std::snprintf(text, sizeof(text), "sensitivity %g", static_cast<double>(speeds[speed]));
            run(menu, callbacks, text);
        }
        bool invert = published.invert;
        if (toggle_row(menu, "Invert up and down", "Push up (or move the mouse up) to look down.", invert))
            run(menu, callbacks, invert ? "invert on" : "invert off");
        if (small_button("SHOW THE GUIDE")) run(menu, callbacks, "guide");
    }
    end_card();

    ImGui::EndChild();
}
} // namespace dingosdk::overlay::menu
