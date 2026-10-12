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

    begin_card(menu, "camcorder-camera", "CAMCORDER", "The VX1000 was the camera of 90s skate video; mods add others.");
    info(menu, "Now", describe(s));
    if (primary_button(menu, s.powered ? "TURN THE CAMERA OFF" : "TURN THE CAMERA ON"))
        run(menu, callbacks, s.powered ? "power off" : "power on");
    if (s.powered && primary_button(menu, s.transport == Transport::recording ? "STOP  (STBY)" : "REC"))
        run(menu, callbacks, "rec");
    if (s.powered && small_button("TAKE A STILL")) run(menu, callbacks, "still");
    end_card();

    begin_card(menu, "camcorder-shooting", "SHOOTING");
    field(menu, "Filming", "Hold the camera yourself, or film from a tripod you set up.");
    int mode = s.mode == Mode::tripod ? 1 : 0;
    if (choice(menu, "camcorder-mode", mode, {"HANDHELD", "TRIPOD"}))
        run(menu, callbacks, mode ? "mode tripod" : "mode handheld");
    // Buttons that wrap: mods can add any number of cameras and lenses.
    const auto pick = [&](const char *id, const std::vector<camcorder_runtime::Published::Choice> &items,
                          const std::string &current, const char *verb, const char *none) {
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
    };
    field(menu, "Camera", "Its zoom, tape, frame rate, look and date stamp. Mods add cameras (hover for which).");
    pick("camcorder-cameras", published.cameras, s.camera_id, "camera", nullptr);
    field(menu, "Lens", "The Century 0.3x fisheye of every 90s part, the camera's own lens, or a mod's.");
    pick("camcorder-lenses", published.lenses, s.lens_id, "lens", "STOCK LENS");
    if (s.fisheye) {
        bool bend = published.warp;
        if (toggle_row(menu, "True fisheye", "Straight lines bend as through the real lens. The middle is a little softer.", bend))
            run(menu, callbacks, bend ? "warp on" : "warp off");
    }
    field(menu, "Grip", "Low on a long grip (the fisheye follow shot), at the hip, or at your eye.");
    int grip = static_cast<int>(s.grip);
    if (choice(menu, "camcorder-grip", grip, {"LONG GRIP", "HIP", "EYE LEVEL"}))
        run(menu, callbacks, grip == 0 ? "grip long" : grip == 1 ? "grip hip" : "grip eye");
    bool operate = published.operating;
    if (toggle_row(menu, "Camera controls", "Mouse aims, Q / E zoom; your skater stands still while it is on.", operate))
        run(menu, callbacks, operate ? "operate on" : "operate off");
    bool aim = published.aiming;
    if (toggle_row(menu, "Steer while filming",
                   "You skate (keys, left stick); the mouse or right stick aims, the wheel or triggers zoom.", aim))
        run(menu, callbacks, aim ? "aim on" : "aim off");
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

    const auto &finish = published.finish;
    const std::string format = finish.look.format;
    begin_card(menu, "camcorder-look", "TAPE LOOK", ("What " + format + " did to the picture. On clips and stills, not the screen.").c_str());
    bool dv_look = finish.dv.on;
    const std::string look_label = format + " look";
    if (toggle_row(menu, look_label.c_str(), "The camera's motion, highlights, colour drift, sharpening, grain and tape smear. Sets its own size.", dv_look))
        run(menu, callbacks, dv_look ? "look dv" : "look clean");
    if (finish.dv.on) {
        const auto part = [&](const char *label, const char *detail, bool on, const char *word) {
            bool value = on;
            if (toggle_row(menu, label, detail, value)) run(menu, callbacks, std::string("dv ") + word + (value ? " on" : " off"));
        };
        if (interlaced(finish.motion)) part("Fields", "60 (or 50) a second: smooth video motion, half the lines each.", finish.dv.fields, "fields");
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
    end_card();

    begin_card(menu, "camcorder-tape", "TAPE", "Every REC to STBY is a clip: an MP4 in Videos\\ReSkate Camcorder.");
    field(menu, "Quality", "HD for sharing, or the camera's own size.");
    int quality = published.dv ? 1 : 0;
    const std::string own = "OWN " + std::to_string(published.own_lines * 4 / 3) + " x " + std::to_string(published.own_lines);
    if (choice(menu, "camcorder-quality", quality, {"HD 1440 x 1080", own.c_str()}))
        run(menu, callbacks, quality ? "quality dv" : "quality hd");
    field(menu, "Sound", "The game's own sound (nothing else on your PC), or as the VX1000's mic heard it.");
    int sound = static_cast<int>(published.sound);
    if (choice(menu, "camcorder-sound", sound, {"OFF", "GAME", "VX MIC"}))
        run(menu, callbacks, sound == 0 ? "sound off" : sound == 1 ? "sound game" : "sound vx");
    if (small_button("LIST CLIPS")) run(menu, callbacks, "tape clips");
    ImGui::SameLine();
    if (small_button("CAPTURE THE TAPE")) run(menu, callbacks, "tape capture");
    ImGui::SameLine();
    if (small_button("NEW TAPE")) run(menu, callbacks, "tape new");
    note("Capture the tape joins this tape's clips into one file, the way a MiniDV tape went into the computer.");
    if (const auto last = camcorder_capture_last(); !last.empty()) info(menu, "Last clip", last);
    note("Turn the game's music down in its audio settings for clean session sound.");
    end_card();

    begin_card(menu, "camcorder-controls", "CONTROLS");
    note("With camera controls on or a tripod being set up: the mouse aims (no button needed), Q / E zoom, "
         "W A S D move a tripod, Q / E raise and lower it, Shift + Q / E zoom while setting up. "
         "On a pad: right stick, triggers, left stick, L3.");
    note("Steer while filming: you keep skating on the keyboard or left stick and buttons; the mouse (or right "
         "stick) aims and the wheel (or triggers) zooms. Mouse or right-stick tricks wait until you turn it off.");
    note("Settings are kept between games.");
    note("Cameras and lenses from mods: Mods/<mod>/camcorders/*.camera.json and *.lens.json. camcorder mods lists them, "
         "camcorder export writes the ones in use to Mods/My Camcorder to start from.");
    if (published.mod_notes) {
        const std::string warning = std::to_string(published.mod_notes) + " problem(s) in the mods' camera profiles: camcorder mods.";
        note(warning.c_str());
    }
    note("Put the camcorder on keys or buttons in SETTINGS > CONTROLS: Camcorder on / off, REC, camera controls, "
         "steer while filming, set up a tripod, next tripod, take a still. Every action is also a console command: camcorder status.");
    end_card();

    ImGui::EndChild();
}
} // namespace dingosdk::overlay::menu
