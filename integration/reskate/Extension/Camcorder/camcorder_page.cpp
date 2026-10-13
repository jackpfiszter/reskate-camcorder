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
    {"camstill", "still", 0x77},     {"camaim", "aim while filming", 0x78}, {"camtripod", "tripod: set up / done", 0x79},
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
    const auto &finish = published.finish;
    ImGui::BeginChild("camcorder-body", ImVec2(0, page_body_height(menu)));

    // What most visits are for, on one card: the camera, rolling, how it films, and who.
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
    if (small_button("OPEN MY CLIPS")) run(menu, callbacks, "open clips");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Videos\\ReSkate Camcorder: the clips, and the stills in Stills.");
    if (const auto last = camcorder_capture_last(); !last.empty()) info(menu, "Last", last);

    field(menu, "Camera", "Its zoom, tape, frame rate, look, date stamp and mic. Mods add cameras (hover for which).");
    pick(menu, callbacks, "camcorder-cameras", published.cameras, s.camera_id, "camera", nullptr);
    field(menu, "Lens", "The Century 0.3x fisheye of every 90s part, the camera's own lens, or a mod's.");
    pick(menu, callbacks, "camcorder-lenses", published.lenses, s.lens_id, "lens", "STOCK LENS");
    bool dv_look = finish.dv.on;
    const std::string look_label = finish.look.format + " look";
    if (toggle_row(menu, look_label.c_str(), "The camera's picture: its lines, hard whites, sharpened edges, colour and grain. Each camera has its own.", dv_look))
        run(menu, callbacks, dv_look ? "look dv" : "look clean");
    bool light = published.light;
    if (toggle_row(menu, "Camera light", "A lamp on the camera for night filming: what it points at is lit, the edges fall off.", light))
        run(menu, callbacks, light ? "light on" : "light off");
    bool aim = published.aiming;
    if (toggle_row(menu, "Aim while filming",
                   "The mouse or right stick aims the camera, the wheel or triggers zoom, while you skate. Off: they are the skater's again (flick-it tricks).", aim))
        run(menu, callbacks, aim ? "aim on" : "aim off");

    field(menu, "Who to film", "Handheld: the camera keeps them framed. A tripod: it pans after them. Closest: whoever is nearest the camera, you too.");
    if (small_button("ME / NOBODY", published.subject.empty() && !published.closest)) run(menu, callbacks, "subject none");
    ImGui::SameLine();
    if (small_button("CLOSEST", published.closest)) run(menu, callbacks, "subject closest");
    if (ImGui::IsItemHovered() && published.closest && !published.subject.empty())
        ImGui::SetTooltip("Filming %s now.", published.subject.c_str());
    for (std::size_t i = 0; i < published.players.size(); ++i) {
        const auto &name = published.players[i];
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i)); // two players can share a name
        if (small_button(name.c_str(), !published.closest && name == published.subject)) run(menu, callbacks, "subject " + name);
        ImGui::PopID();
    }
    end_card();

    // Tripods: what to do, in order, on the card.
    begin_card(menu, "camcorder-tripods", "TRIPODS",
               "1 Set one up where you stand.  2 Carry it (WASD), aim it (mouse), zoom (Q / E), height (R / F).  3 The tripod key again, or REC: it's set.  4 Skate: it films you.");
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
        field(menu, "Head", "Follow: it pans after who it films. Locked off: nothing moves. Manual: you pan it with camera controls.");
        int head = static_cast<int>(s.head);
        if (choice(menu, "camcorder-head", head, {"LOCKED OFF", "MANUAL", "FOLLOW"}))
            run(menu, callbacks, head == 0 ? "head locked" : head == 1 ? "head manual" : "head follow");
    }
    end_card();

    // The rest, out of the way until wanted.
    static bool more = false;
    if (small_button(more ? "FEWER SETTINGS" : "MORE SETTINGS")) more = !more;
    if (!more) {
        ImGui::EndChild();
        return;
    }

    begin_card(menu, "camcorder-handheld", "HANDHELD");
    field(menu, "Height", "Where your hands hold it. LB / RB (R / F with camera controls) move it on the fly.");
    if (small_button("LOW")) run(menu, callbacks, "height low");
    ImGui::SameLine();
    if (small_button("HIP")) run(menu, callbacks, "height hip");
    ImGui::SameLine();
    if (small_button("EYE LEVEL")) run(menu, callbacks, "height eye");
    if (published.grips.size() > 1) {
        field(menu, "Rig", "In your hands, or a mod's rig: a pole cam, a board cam.");
        pick(menu, callbacks, "camcorder-rigs", published.grips, s.grip_id, "rig", nullptr);
    }
    bool lock_on = s.lock_on;
    if (toggle_row(menu, "Keep them framed", "Handheld, the camera stays on who you film by itself; the mouse or stick nudges it.", lock_on))
        run(menu, callbacks, lock_on ? "lockon on" : "lockon off");
    bool operate = published.operating;
    if (toggle_row(menu, "Camera controls", "Stand still and work the camera with everything: mouse aims, Q / E zoom, R / F height. Esc to skate again.", operate))
        run(menu, callbacks, operate ? "operate on" : "operate off");
    if (s.fisheye) {
        bool bend = published.warp;
        if (toggle_row(menu, "True fisheye", "Straight lines bend as through the real lens. The middle is a little softer.", bend))
            run(menu, callbacks, bend ? "warp on" : "warp off");
    }
    end_card();

    begin_card(menu, "camcorder-tape", "TAPE AND SOUND", "Every REC to STBY is a clip: an MP4 in Videos\\ReSkate Camcorder.");
    field(menu, "Quality", "HD for sharing, or the camera's own size.");
    int quality = published.dv ? 1 : 0;
    // The clip's sizes for this camera's shape (recorder.h, video_size).
    const float aspect = published.frame.optics.aspect;
    const auto size_text = [&](Quality q, int lines) {
        const auto box = PixelBox{0, 0, static_cast<int>(std::lround(4320 * aspect)) & ~1, 4320};
        const auto size = video_size(q, box, lines);
        return std::to_string(size.width) + " x " + std::to_string(size.height);
    };
    const std::string hd = "HD " + size_text(Quality::hd, 1080), own = "OWN " + size_text(Quality::dv, published.own_lines);
    if (choice(menu, "camcorder-quality", quality, {hd.c_str(), own.c_str()}))
        run(menu, callbacks, quality ? "quality dv" : "quality hd");
    field(menu, "Sound", "The game's own sound as the camera hears it from where it stands (nothing else on your PC), or through the camera's mic. Turn the game's music down for clean session sound.");
    int sound = static_cast<int>(published.sound);
    if (choice(menu, "camcorder-sound", sound, {"OFF", "GAME", "CAMERA MIC"}))
        run(menu, callbacks, sound == 0 ? "sound off" : sound == 1 ? "sound game" : "sound vx");
    if (small_button("CAPTURE THE TAPE")) run(menu, callbacks, "tape capture");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("This tape's clips, back to back in one MP4, as a MiniDV tape went into the computer.");
    ImGui::SameLine();
    if (small_button("NEW TAPE")) run(menu, callbacks, "tape new");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The next clips start a new tape (CAPTURE THE TAPE then joins only those).");
    end_card();

    begin_card(menu, "camcorder-look", "LOOK DETAILS");
    bool stamp = finish.date_stamp;
    if (toggle_row(menu, "Date stamp", "The camera's date and time in the corner.", stamp))
        run(menu, callbacks, stamp ? "date on" : "date off");
    if (finish.date_stamp) {
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
        part("Highlights", "White clips early and hard; blacks are dense.", finish.dv.highlights, "highlights");
        part("White balance", "The colour drifts warm and cool.", finish.dv.white_balance, "balance");
        part("Sharpening", "Bright and dark halos on hard edges.", finish.dv.sharpen, "sharpen");
        part("Lines and smear", "The camera's own lines (480 on a VX1000), colour bleeding sideways, analogue tape's softness.", finish.dv.chroma, "chroma");
        part("Grain", "Sensor noise or film grain.", finish.dv.grain, "grain");
    }
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
    note("Change any of them, or add pad buttons, in SETTINGS > CONTROLS. Esc or Start backs out of camera controls and the quick menu.");
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

    ImGui::EndChild();
}
} // namespace dingosdk::overlay::menu
