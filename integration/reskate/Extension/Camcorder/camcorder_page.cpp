#include "camcorder_page.h"
#include "camcorder_capture.h"
#include "camcorder_runtime.h"
#include "Extension/UI/Overlay/skate_menu_internal.h"
#include "camcorder/commands.h"
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
    {"camstill", "still", 0x77},     {"camaim", "steer while filming", 0x78}, {"camtripod", "set up a tripod", 0x79},
};
int bind_index(std::string_view name) {
    for (std::size_t i = 0; i < action_binds.size(); ++i)
        if (action_binds[i].name == name) return static_cast<int>(i);
    return -1;
}

// Buttons that wrap: mods can add any number of cameras, lenses and grips.
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
} // namespace

void camcorder_page(SkateMenu &menu, const Model &model, const CallbacksV3 &callbacks) {
    const auto published = camcorder_runtime::published();
    const auto &s = published.frame.status;
    ImGui::BeginChild("camcorder-body", ImVec2(0, page_body_height(menu)));

    // The camera itself: what most visits are for.
    begin_card(menu, "camcorder-camera", "CAMCORDER", "Film yourself and your friends, 90s skate video style.");
    info(menu, "Now", describe(s));
    if (primary_button(menu, s.powered ? "TURN THE CAMERA OFF" : "TURN THE CAMERA ON"))
        run(menu, callbacks, s.powered ? "power off" : "power on");
    if (s.powered && primary_button(menu, s.transport == Transport::recording ? "STOP  (STBY)" : "REC"))
        run(menu, callbacks, "rec");
    if (s.powered) {
        if (small_button("TAKE A STILL")) run(menu, callbacks, "still");
        ImGui::SameLine();
        if (small_button("REVIEW THE LAST CLIP")) run(menu, callbacks, "review");
        ImGui::SameLine();
    }
    if (small_button("OPEN CLIPS")) run(menu, callbacks, "open clips");
    ImGui::SameLine();
    if (small_button("OPEN STILLS")) run(menu, callbacks, "open stills");
    if (const auto last = camcorder_capture_last(); !last.empty()) info(menu, "Last", last);
    end_card();

    begin_card(menu, "camcorder-shooting", "SHOOTING");
    field(menu, "Filming", "Hold the camera yourself, or film from a tripod you set up.");
    int mode = s.mode == Mode::tripod ? 1 : 0;
    if (choice(menu, "camcorder-mode", mode, {"HANDHELD", "TRIPOD"}))
        run(menu, callbacks, mode ? "mode tripod" : "mode handheld");
    field(menu, "Camera", "Its zoom, tape, frame rate, look, date stamp and mic. Mods add cameras (hover for which).");
    pick(menu, callbacks, "camcorder-cameras", published.cameras, s.camera_id, "camera", nullptr);
    field(menu, "Lens", "The Century 0.3x fisheye of every 90s part, the camera's own lens, or a mod's.");
    pick(menu, callbacks, "camcorder-lenses", published.lenses, s.lens_id, "lens", "STOCK LENS");
    if (s.fisheye) {
        bool bend = published.warp;
        if (toggle_row(menu, "True fisheye", "Straight lines bend as through the real lens. The middle is a little softer.", bend))
            run(menu, callbacks, bend ? "warp on" : "warp off");
    }
    field(menu, "Grip", "Low on a long grip (the fisheye follow shot), at the hip, at your eye, or a mod's. LB / RB or R / F move it up and down.");
    pick(menu, callbacks, "camcorder-grips", published.grips, s.grip_id, "grip", nullptr);
    bool aim = published.aiming;
    if (toggle_row(menu, "Steer while filming",
                   "You keep skating (keys, left stick); the mouse or right stick aims, the wheel or triggers zoom, LB / RB the height.", aim))
        run(menu, callbacks, aim ? "aim on" : "aim off");
    bool operate = published.operating;
    if (toggle_row(menu, "Camera controls", "You stand still and work the camera: mouse aims, Q / E zoom, R / F height.", operate))
        run(menu, callbacks, operate ? "operate on" : "operate off");
    bool lock_on = s.lock_on;
    if (toggle_row(menu, "Lock on", "Keep the person you film framed by yourself while you skate.", lock_on))
        run(menu, callbacks, lock_on ? "lockon on" : "lockon off");
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
    field(menu, "Quality", "HD for sharing, or the camera's own size.");
    int quality = published.dv ? 1 : 0;
    const std::string own = "OWN " + std::to_string(published.own_lines * 4 / 3) + " x " + std::to_string(published.own_lines);
    if (choice(menu, "camcorder-quality", quality, {"HD 1440 x 1080", own.c_str()}))
        run(menu, callbacks, quality ? "quality dv" : "quality hd");
    field(menu, "Sound", "The game's own sound (nothing else on your PC), or as the camera's own mic heard it. Turn the game's music down for clean session sound.");
    int sound = static_cast<int>(published.sound);
    if (choice(menu, "camcorder-sound", sound, {"OFF", "GAME", "CAMERA MIC"}))
        run(menu, callbacks, sound == 0 ? "sound off" : sound == 1 ? "sound game" : "sound vx");
    if (small_button("LIST CLIPS")) run(menu, callbacks, "tape clips");
    ImGui::SameLine();
    if (small_button("CAPTURE THE TAPE")) run(menu, callbacks, "tape capture");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("This tape's clips, back to back in one file, as a MiniDV tape went into the computer.");
    ImGui::SameLine();
    if (small_button("NEW TAPE")) run(menu, callbacks, "tape new");
    end_card();

    const auto &finish = published.finish;
    const std::string format = finish.look.format;
    begin_card(menu, "camcorder-look", "LOOK", ("What " + format + " did to the picture: on screen, in clips and in stills.").c_str());
    bool dv_look = finish.dv.on;
    const std::string look_label = format + " look";
    if (toggle_row(menu, look_label.c_str(), "The camera's highlights, colour drift, sharpening, grain, tape smear and motion. Each camera has its own.", dv_look))
        run(menu, callbacks, dv_look ? "look dv" : "look clean");
    static bool fine_tune = false;
    if (finish.dv.on) toggle_row(menu, "Fine-tune the look", "Turn single parts of the look off.", fine_tune);
    if (finish.dv.on && fine_tune) {
        const auto part = [&](const char *label, const char *detail, bool on, const char *word) {
            bool value = on;
            if (toggle_row(menu, label, detail, value)) run(menu, callbacks, std::string("dv ") + word + (value ? " on" : " off"));
        };
        if (interlaced(finish.motion)) part("Fields", "60 (or 50) a second: smooth video motion in clips, half the lines each.", finish.dv.fields, "fields");
        part("Highlights", "White clips early and hard; blacks are dense.", finish.dv.highlights, "highlights");
        part("White balance", "The colour drifts warm and cool.", finish.dv.white_balance, "balance");
        part("Sharpening", "Bright and dark halos on hard edges.", finish.dv.sharpen, "sharpen");
        part("Tape smear", "Colour bleeds sideways (DV's 4:1:1), and analogue tape softens.", finish.dv.chroma, "chroma");
        if (finish.look.grain > 0) part("Grain", "Sensor noise or film grain.", finish.dv.grain, "grain");
    }
    bool stamp = finish.date_stamp;
    if (toggle_row(menu, "Date stamp", "The camera's date and time in the corner.", stamp))
        run(menu, callbacks, stamp ? "date on" : "date off");
    if (finish.date_stamp) {
        static constexpr int years[] = {0, 1995, 1996, 1997, 1998, 1999};
        int year = 0;
        for (int i = 0; i < 6; ++i)
            if (years[i] == finish.stamp_year) year = i;
        field(menu, "Year", "The year the stamp says (any year: camcorder date year 1993).");
        if (choice(menu, "camcorder-year", year, {"THIS YEAR", "1995", "1996", "1997", "1998", "1999"}))
            run(menu, callbacks, year ? "date year " + std::to_string(years[year]) : std::string("date year now"));
    }
    bool clean = published.clean_feed;
    if (toggle_row(menu, "Clean viewfinder", "Hide REC, the counter and the hints; keep the frame and the lens.", clean))
        run(menu, callbacks, clean ? "clean on" : "clean off");
    end_card();

    begin_card(menu, "camcorder-controls", "CONTROLS", "Settings are kept between games.");
    // One click: the recommended keys, where they are free.
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
    end_card();

    begin_card(menu, "camcorder-mods", "MODS", "Cameras, lenses, grips and the feel come from data mods too.");
    info(menu, "Cameras", std::to_string(published.cameras.size()));
    info(menu, "Lenses", std::to_string(published.lenses.size() + 1));
    info(menu, "Grips", std::to_string(published.grips.size()));
    if (published.mod_notes) warn((std::to_string(published.mod_notes) + " problem(s) in the mods' profiles: LIST shows them.").c_str());
    if (small_button("LIST")) run(menu, callbacks, "mods");
    ImGui::SameLine();
    if (small_button("RELOAD")) run(menu, callbacks, "mods reload");
    ImGui::SameLine();
    if (small_button("START A MOD FROM THESE")) run(menu, callbacks, "export");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes the camera, lens, grip and feel in use to Mods/My Camcorder, every field filled in.");
    note("A mod's files go in Mods/<mod>/camcorders: *.camera.json, *.lens.json, *.grip.json, handling.json (see MODDING.md).");
    end_card();

    ImGui::EndChild();
}
} // namespace dingosdk::overlay::menu
