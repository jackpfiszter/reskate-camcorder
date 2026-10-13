#include "camcorder_runtime.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Vfs/mod_catalog.h"
#include "Engine/Vfs/mod_list.h"
#include "Engine/Game/UI/game_view.h"
#include "Extension/Profile/local_profile_runtime.h"
#include "Extension/Skater/no_bail.h"
#include "Extension/Multiplayer/Hud/game_ui_state.h"
#include "Extension/Multiplayer/Hud/native_party.h"
#include "Extension/Skater/client_source_spawn.h"
#include "Extension/UI/Overlay/overlay.h"
#include "camcorder/commands.h"
#include "camcorder/preferences.h"
#include "camcorder/profiles.h"
#include "camcorder/recorder.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <ShlObj.h>
#include <shellapi.h>

namespace dingosdk::camcorder_runtime {
namespace {
using Clock = std::chrono::steady_clock;
using namespace dingosdk::camcorder;

struct State {
    // Client thread only.
    Camcorder camcorder;
    Clock::time_point last_tick{};
    std::optional<multiplayer::Transform> local;
    Clock::time_point local_at{};
    VelocityEstimate local_velocity;
    std::optional<float> local_feet;     // the local skater's feet, from the board under them
    float local_lift{};                  // the root's height above them when the board last said
    Clock::time_point local_lift_at{}, full_pose_at{};
    RootHeight local_root, subject_root; // how far each root sits above its feet (the framing's)
    int frame_on{1};                     // what the camera frames on someone: 0 board, 1 the whole skater, 2 head
    std::vector<Player> players;
    std::uint64_t subject{}; // 0: nobody (a tripod then films the local player)
    bool closest{};          // film whoever is nearest the camera (`camcorder subject closest`)
    std::atomic<float> ear_distance{}, ear_side{}; // the local skater from the camera, for the sound
    std::uint64_t nearest{}; // that one now: 0 is the local player
    VelocityEstimate subject_velocity;
    bool operating{};        // full camera control: the skater waits, every control works the camera (`camcorder operate`)
    bool captured{};         // we asked the overlay to hold the pad
    bool aim_captured{};     // we asked the overlay for the right stick, the triggers and the mouse
    float wheel_zoom{};      // seconds of zoom rocker left from the mouse wheel, signed
    bool owned{};            // update_party_camera holds the screen for us
    bool clean_feed{};
    Sound sound{Sound::game};
    std::uint32_t still{}, tape{1}, capture_tape{};
    Finish finish{.dv = {.on = true}}; // the look (on unless turned off: it is the camera's character) and the stamp
    std::atomic<bool> warp_failed{}; // the overlay could not draw the true fisheye on this display: the flat stand-in
    bool preferences_loaded{}, loading{};
    // The player's own feel (not a mod's): the mouse's speed and which way is up.
    float sensitivity{1};
    bool invert{};
    // The first-use guide: shown once, the first time the camera comes on (`camcorder guide` again).
    bool guide_seen{};
    double guide_until{};
    // The quick menu (`camcorder quick`): a short list over the viewfinder, worked with the keys,
    // the stick or the wheel while it is open (the skater waits).
    bool quick{};
    int quick_index{};
    double quick_used{};       // when it was last touched; it closes itself after a while
    float quick_held{};        // seconds a direction has been held (it repeats)
    int quick_last{};          // that direction: -1 up, 1 down, 2 do, -2 close, 0 none
    std::uint32_t review{}, review_stop{}; // requests for the overlay's clip review
    bool back_held{};          // Esc or Start, while the camera has the controls
    bool pad_was_held{};       // the camera had the controls last tick
    bool back_armed{};         // Esc / Start seen up since then: the next press backs out
    bool left_play{};          // `ready` dropped (loading): the tripods go with the old world
    bool in_play{};            // this tick: in play, no game menu up (the pad is never held otherwise)
    double place_until{};      // a tripod asked for with the camera off: placed once the skater is known (until then)
    Catalogue catalogue;                 // the built-in cameras and lenses, and the mods'
    std::vector<std::string> mod_notes;  // what reading the mods' profiles found wrong
    bool was_powered{};
    std::string last_detail;
    std::string refused; // why the game has not handed over its camera, while it has not
    std::atomic<float> render_aspect{16.0f / 9.0f}; // the overlay's display, width over height
    // Read by the overlay.
    std::mutex published_mutex;
    Published published;
};
State &state() {
    static auto *value = new State;
    return *value;
}

Vec3 to_vec(const std::array<float, 3> &v) { return {v[0], v[1], v[2]}; }

// The local skater, if the session placed them in the last half second.
Filmer filmer_from(State &s, Clock::time_point now, float dt) {
    Filmer f;
    if (!s.local || now - s.local_at > std::chrono::milliseconds(500)) {
        s.local_velocity.reset();
        return f;
    }
    f.valid = true;
    f.position = to_vec(s.local->position);
    // The skater's body faces along its third row (follow_camera.cpp reads it the same way).
    const auto body = multiplayer::to_matrix(*s.local);
    f.forward = {body[8], 0, body[10]};
    f.velocity = s.local_velocity.update(f.position, dt);
    f.on_board = true; // the tick asks the physics state (on_board below)
    return f;
}

// Whether the local skater rides (wheels buzz) or walks (footsteps): the physics state No Bail
// watches, 500 to 599 being on foot (getting on included), as Board Break reads it.
bool on_board(std::uintptr_t client) {
    NoBailSkater skater;
    if (!no_bail_skater(skater) || !skater.entity) return true;
    watch_physics_state(client, skater.entity);
    const auto watch = watched_physics_state();
    return !(watch.valid && watch.state >= 500 && watch.state < 600);
}

// The surface under the tripod being set up: from a little above its feet (so it climbs onto a
// ledge it is carried onto, not onto the roof above) down a long way (so it drops off one).
std::optional<float> ground_under_tripod(const Camcorder &camcorder) {
    const auto status = camcorder.status();
    if (!status.tripod_setup) return std::nullopt;
    const auto *tripod = camcorder.tripod(status.active_tripod);
    if (!tripod) return std::nullopt;
    const auto feet = tripod->feet();
    return local_ground_height(feet[0], feet[2], feet[1] + 2.5f, feet[1] - 40.0f);
}

// How far a subject's root sits above their feet (the root is the game's, around the hips): the
// framing is measured from the feet, so "the whole skater" is not their face. From the board under
// them when they ride it, or else the ground found under them.
std::optional<float> subject_root(State &s, const Vec3 &at, std::optional<float> feet, float dt) {
    if (!feet) feet = local_ground_height(at[0], at[2], at[1] + 0.5f, at[1] - 3.0f);
    return s.subject_root.update(feet ? std::optional<float>(at[1] - *feet) : std::nullopt, dt);
}

std::optional<Subject> subject_from(State &s, const Filmer &filmer, float dt) {
    if (s.closest) {
        // Whoever is nearest: to a tripod, the local player too; handheld, the others.
        const auto status = s.camcorder.status();
        const auto *tripod = status.mode == Mode::tripod ? s.camcorder.tripod(status.active_tripod) : nullptr;
        std::optional<Vec3> camera;
        if (tripod) camera = tripod->lens();
        else if (filmer.valid) camera = filmer.position;
        std::vector<Vec3> skaters;
        std::vector<std::uint64_t> ids;
        std::vector<std::optional<float>> feet;
        if (tripod && filmer.valid) {
            skaters.push_back(filmer.position);
            ids.push_back(0);
            feet.push_back(std::nullopt);
        }
        int current = -1;
        for (const auto &p : s.players) {
            if (p.id == s.nearest) current = static_cast<int>(skaters.size());
            skaters.push_back(to_vec(p.root.position));
            ids.push_back(p.id);
            feet.push_back(p.feet);
        }
        if (tripod && filmer.valid && s.nearest == 0) current = 0;
        const int pick = camera ? closest_skater(*camera, skaters, current) : -1;
        const auto id = pick >= 0 ? ids[static_cast<std::size_t>(pick)] : 0;
        if (id != s.nearest) {
            s.subject_velocity.reset();
            s.subject_root.reset();
        }
        s.nearest = id;
        s.subject = 0;
        if (id == 0) {
            s.subject_velocity.reset();
            return std::nullopt; // a tripod with nobody picked films the local player
        }
        const auto at = skaters[static_cast<std::size_t>(pick)];
        return Subject{true, at, s.subject_velocity.update(at, dt), subject_root(s, at, feet[static_cast<std::size_t>(pick)], dt)};
    }
    if (s.subject)
        for (const auto &p : s.players)
            if (p.id == s.subject) {
                const auto at = to_vec(p.root.position);
                return Subject{true, at, s.subject_velocity.update(at, dt), subject_root(s, at, p.feet, dt)};
            }
    s.subject_velocity.reset();
    return std::nullopt;
}

// The settings file: %LOCALAPPDATA%\ReSkate Camcorder\settings.txt.
std::filesystem::path preferences_path() {
    wchar_t *base{};
    std::size_t length{};
    if (_wdupenv_s(&base, &length, L"LOCALAPPDATA") != 0 || !base) return {};
    std::filesystem::path path = *base ? std::filesystem::path(base) / L"ReSkate Camcorder" / L"settings.txt" : std::filesystem::path{};
    std::free(base);
    return path;
}
const char *on_off(bool on) { return on ? "on" : "off"; }
constexpr const char *frame_words[]{"board", "body", "head"};
// The mods' handling, with what the camera frames on someone the player's choice: their board and
// feet, the whole skater (the handling's own height), or their head and shoulders.
void apply_handling(State &s) {
    auto handling = s.catalogue.handling;
    if (s.frame_on == 0) handling.aim_height = 0.35f;
    else if (s.frame_on == 2) handling.aim_height = 1.5f;
    s.camcorder.set_handling(handling);
}
Preferences current_preferences(const State &s) {
    const auto status = s.camcorder.status();
    const auto &dv = s.finish.dv;
    return {
        {"version", "2"}, // 2: the look on by default (files without it said `look clean` for everyone)
        {"camera", status.camera_id},
        {"lens", status.lens_id.empty() ? std::string("stock") : status.lens_id},
        {"grip", status.grip_id},
        {"head", status.head == TripodRig::Head::locked ? "locked" : status.head == TripodRig::Head::manual ? "manual" : "follow"},
        {"frame", frame_words[std::clamp(s.frame_on, 0, 2)]},
        {"clean", on_off(s.clean_feed)},
        {"sound", s.sound == Sound::off ? "off" : "game"},
        {"look", dv.on ? "dv" : "clean"},
        {"dv-fields", on_off(dv.fields)},
        {"dv-highlights", on_off(dv.highlights)},
        {"dv-balance", on_off(dv.white_balance)},
        {"dv-sharpen", on_off(dv.sharpen)},
        {"dv-chroma", on_off(dv.chroma)},
        {"dv-grain", on_off(dv.grain)},
        {"date", on_off(s.finish.date_stamp)},
        {"date-year", s.finish.stamp_year > 0 ? std::to_string(s.finish.stamp_year) : "now"},
        {"date-offset", std::to_string(s.finish.stamp_offset)},
        {"sensitivity", [&] { char t[16]; std::snprintf(t, sizeof(t), "%g", static_cast<double>(s.sensitivity)); return std::string(t); }()},
        {"invert", on_off(s.invert)},
        {"guide", s.guide_seen ? "seen" : "off"},
    };
}
void save_preferences(const State &s) {
    const auto path = preferences_path();
    if (path.empty()) return;
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << write_preferences(current_preferences(s));
    if (!file) logging::write(logging::Level::warning, logging::Channel::ui, "Camcorder: could not save its settings.");
}
bool is_setting(std::string_view verb) {
    for (const auto *name : {"sensitivity", "invert", "guide", "camera", "lens", "grip", "head", "frame", "clean", "sound", "look", "dv", "date"})
        if (verb == name) return true;
    return false;
}

// The Mods folder, where profiles come from and `camcorder export` writes.
std::filesystem::path mods_root() { return mods::engine_data_root() / mods::mods_folder; }

// Every enabled mod's camcorders/*.camera.json and *.lens.json (profiles.h), lowest priority first
// so a higher mod's profile replaces a lower one's with the same id.
void scan_profiles(State &s) {
    Catalogue catalogue;
    std::vector<std::string> notes;
    std::size_t found = 0;
    const auto list = mods::scan_mods(mods::engine_data_root());
    for (auto entry = list.entries.rbegin(); entry != list.entries.rend(); ++entry) {
        if (!entry->enabled) continue;
        std::error_code error;
        const auto folder = entry->mod.directory / L"camcorders";
        if (!std::filesystem::is_directory(folder, error)) continue;
        const auto source = entry->mod.title.empty() ? entry->mod.name : entry->mod.title;
        for (std::filesystem::directory_iterator file(folder, error), end; !error && file != end; file.increment(error)) {
            const auto name = mods::ascii_path(file->path().filename());
            const auto kind = profile_file(name);
            std::error_code ignored;
            if (!kind || !file->is_regular_file(ignored)) continue;
            const auto where = entry->mod.name + "/camcorders/" + name + ": ";
            if (file->file_size(ignored) > 64 * 1024) {
                notes.push_back(where + "larger than 64 KB; left out.");
                continue;
            }
            std::ifstream in(file->path(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            const auto take = [&](auto reading) {
                for (const auto &note : reading.notes) notes.push_back(where + note);
                if (!reading.profile) {
                    notes.push_back(where + reading.error + " Left out.");
                    return;
                }
                reading.profile->source = source;
                catalogue.add(std::move(*reading.profile));
                ++found;
            };
            // What a camera names beside itself (a lut's .cube), from the same folder.
            const ProfileFiles beside = [&](std::string_view file_name) -> std::optional<std::string> {
                // (The name is checked to be plain ASCII: profiles.cpp, plain_file_name.)
                const auto path = folder / std::filesystem::path(std::string(file_name));
                std::error_code missing;
                if (!std::filesystem::is_regular_file(path, missing) || std::filesystem::file_size(path, missing) > 16u * 1024 * 1024)
                    return std::nullopt;
                std::ifstream in(path, std::ios::binary);
                std::stringstream text;
                text << in.rdbuf();
                return in ? std::optional<std::string>(text.str()) : std::nullopt;
            };
            switch (kind->first) {
            case ProfileKind::camera: take(read_camera(text.str(), kind->second, beside)); break;
            case ProfileKind::lens: take(read_adapter(text.str(), kind->second)); break;
            case ProfileKind::handling: {
                // Layered: this mod's numbers over the lower mods'.
                auto reading = read_handling(text.str(), catalogue.handling);
                for (const auto &note : reading.notes) notes.push_back(where + note);
                if (!reading.profile) {
                    notes.push_back(where + reading.error + " Left out.");
                    break;
                }
                catalogue.handling = *reading.profile;
                catalogue.handling_sources.push_back(source);
                ++found;
                break;
            }
            }
        }
    }
    s.catalogue = std::move(catalogue);
    s.mod_notes = std::move(notes);
    // What is in use takes its profile's numbers now (a mod's VX1000 replaces the built-in one).
    const auto &settings = s.camcorder.settings();
    if (const auto *camera = s.catalogue.camera(settings.camera.id); camera && !(*camera == settings.camera))
        s.camcorder.set_camera(*camera);
    if (settings.adapter)
        if (const auto *lens = s.catalogue.adapter(settings.adapter->id)) s.camcorder.set_adapter(*lens);
    apply_handling(s);
    logging::printf(logging::Level::info, logging::Channel::ui, "Camcorder: %zu profiles from mods%s.",
                    found, s.mod_notes.empty() ? "" : " (camcorder mods lists what was wrong with some)");
    for (const auto &note : s.mod_notes) logging::write(logging::Level::warning, logging::Channel::ui, "Camcorder mods: " + note);
}

// `camcorder export`: the camera and lens in use as a mod to start from, in Mods/My Camcorder.
std::string export_profiles(const State &s) {
    const auto folder = mods_root() / L"My Camcorder";
    const auto profiles = folder / L"camcorders";
    std::error_code error;
    std::filesystem::create_directories(profiles, error);
    // (path::string() throws on characters outside the code page; ascii_path does not.)
    const auto shown = mods::ascii_path(folder).empty() ? std::string("Mods/My Camcorder") : mods::ascii_path(folder);
    if (error) return "Could not make " + shown + ".";
    if (!std::filesystem::exists(folder / mods::manifest_file, error))
        std::ofstream(folder / mods::manifest_file, std::ios::binary)
            << "{\n  \"name\": \"My Camcorder\",\n  \"author\": \"\",\n  \"version_number\": \"1.0.0\",\n"
               "  \"description\": \"Cameras and lenses for the ReSkate camcorder.\"\n}\n";
    std::string written;
    const auto write = [&](const std::string &file, const std::string &text) {
        const auto path = profiles / file;
        if (std::filesystem::exists(path, error)) {
            written += " " + file + " is already there (left as it is).";
            return;
        }
        std::ofstream(path, std::ios::binary) << text;
        written += " Wrote " + file + ".";
    };
    const auto &settings = s.camcorder.settings();
    write(settings.camera.id + ".camera.json", write_camera(settings.camera));
    if (settings.adapter) write(settings.adapter->id + ".lens.json", write_adapter(*settings.adapter));
    write("handling.json", write_handling(s.camcorder.handling()));
    return "In " + shown + ":" + written +
           " Change the id (the file name) and the numbers, then camcorder mods reload. docs/MODDING.md has every field.";
}

// Only while the camcorder's picture is on screen: without it the pad stays the skater's.
bool wants_pad(State &s) {
    return s.camcorder.powered() && s.owned && s.in_play &&
           (s.camcorder.tripod_setup() || s.quick || s.back_held || (s.operating && s.camcorder.status().mode == Mode::handheld));
}
double wall_seconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

// The quick menu's lines: what each shows, and the command it runs.
struct QuickItem {
    std::string label, value, verb;
    std::vector<std::string> words;
    bool closes{}; // done: the menu closes
};
std::vector<QuickItem> quick_items(const State &s) {
    const auto status = s.camcorder.status();
    const auto on = [](bool b) { return std::string(b ? "on" : "off"); };
    std::vector<QuickItem> items{
        {"REC", status.transport == Transport::recording ? "recording" : "standby", "rec", {}, true},
        {"Take a still", "", "still", {}, true},
        {"Camera", status.camera_name, "camera", {"next"}, false},
        {"Lens", status.lens_name, "lens", {"next"}, false},
        {"Look", on(s.finish.dv.on), "look", {"toggle"}, false},
        {"Frame on", s.frame_on == 0 ? "board" : s.frame_on == 1 ? "whole skater" : "head", "frame", {"next"}, false},
    };
    if (status.mode == Mode::handheld) {
        items.push_back({"Hold it", status.grip_id == board_cam().id ? "board cam" : "handheld", "rig", {"toggle"}, false});
        items.push_back({"Camera control", s.operating ? "full (skater waits)" : "while skating", "operate", {"toggle"}, true});
    }
    items.push_back(status.tripod_setup ? QuickItem{"Tripod: done", "", "tripod", {"done"}, true}
                                        : QuickItem{"Set up a tripod here", "", "tripod", {"place"}, true});
    const auto standing = std::count(status.tripods.begin(), status.tripods.end(), true);
    if (standing > 0)
        items.push_back({"Handheld / tripod", status.mode == Mode::tripod ? "tripod" : "handheld", "mode", {"toggle"}, false});
    if (standing > 1 && status.mode == Mode::tripod) items.push_back({"Next tripod", "", "tripod", {"next"}, false});
    items.push_back({"Close", "", "", {}, true});
    return items;
}

// The quick menu's controls while it is open: up / down (W / S, the stick, the wheel) choose, right
// (D) does it, left (A) closes. A held direction repeats.
void quick_controls(State &s, float dt) {
    overlay::FlightInput pad;
    DingoSDKOverlayReadFlightInput(&pad, true, true);
    float notches{};
    DingoSDKOverlayReadMouseWheel(&notches);
    if (!pad.active) return;
    const int count = static_cast<int>(quick_items(s).size());
    int direction = pad.forward > 0.6f ? -1 : pad.forward < -0.6f ? 1 : pad.right > 0.6f ? 2 : pad.right < -0.6f ? -2 : 0;
    const bool fresh = direction != 0 && direction != s.quick_last;
    s.quick_held = direction == s.quick_last && direction != 0 ? s.quick_held + dt : 0;
    s.quick_last = direction;
    const bool repeat = (direction == -1 || direction == 1) && s.quick_held > 0.35f;
    if (repeat) s.quick_held -= 0.12f;
    if (notches != 0 && std::isfinite(notches)) {
        direction = notches > 0 ? -1 : 1;
        s.quick_used = wall_seconds();
        s.quick_index = (s.quick_index + direction + count) % count;
        return;
    }
    if (!fresh && !repeat) return;
    s.quick_used = wall_seconds();
    if (direction == -1 || direction == 1) {
        s.quick_index = (s.quick_index + direction + count) % count;
    } else if (direction == -2) {
        s.quick = false;
    } else if (direction == 2) {
        const auto item = quick_items(s)[static_cast<std::size_t>(std::clamp(s.quick_index, 0, count - 1))];
        if (item.closes) s.quick = false;
        if (!item.verb.empty()) command(item.verb, item.words);
        // The list can change length (a tripod standing, handheld or not): stay on the same line.
        const auto now = quick_items(s);
        for (std::size_t i = 0; i < now.size(); ++i)
            if (now[i].label == item.label || (now[i].verb == item.verb && now[i].words == item.words)) s.quick_index = static_cast<int>(i);
        s.quick_index = std::clamp(s.quick_index, 0, static_cast<int>(now.size()) - 1);
    }
}
// Filming handheld (or from the board) while skating: the camera takes what a filmer doesn't skate
// with (the right stick and the mouse to aim, the triggers, Q / E and the wheel to zoom, the bumpers
// and R / F for height), the skater keeps the rest. Watching through a tripod, all of it is the skater's.
bool wants_aim(State &s) {
    return s.camcorder.powered() && s.owned && s.in_play && s.camcorder.status().mode == Mode::handheld && !wants_pad(s);
}

void set_capture(State &s, bool pad, bool aim) {
    if (pad || s.captured) DingoSDKOverlaySetFreecamInputCapture(pad); // every tick while held (client_tick.cpp too)
    if (aim != s.aim_captured) DingoSDKOverlaySetCameraAimCapture(aim);
    s.captured = pad;
    s.aim_captured = aim;
}

// The mouse wheel works the zoom rocker: each notch holds it for a moment, as a thumb does.
float wheel_rocker(State &s, float dt) {
    float notches{};
    DingoSDKOverlayReadMouseWheel(&notches); // drained every tick, so old turns never zoom later
    if (std::isfinite(notches) && notches != 0) {
        if ((notches > 0) != (s.wheel_zoom > 0)) s.wheel_zoom = 0; // turning back stops at once
        const float notch = s.camcorder.handling().wheel_notch;
        s.wheel_zoom = std::clamp(s.wheel_zoom + notches * notch, -4 * notch, 4 * notch);
    }
    if (s.wheel_zoom == 0) return 0;
    const float sign = s.wheel_zoom > 0 ? 1.0f : -1.0f;
    s.wheel_zoom = sign * std::max(std::abs(s.wheel_zoom) - dt, 0.0f);
    return sign;
}

// The pad, keyboard and mouse, while the camera has them (Extension/UI/Overlay/overlay_input.cpp):
// the freecam's flight input for the pad and keyboard (sticks and WASD; triggers or Q / E as
// up = right - left; L3 or Shift as boost; look as stick deflection times 25), and the mouse with
// no button held, in pixels of .0025 radians each as free_flight.h turns them. Look y grows
// downward there; the camcorder's grows upward.
//
// Aiming while skating (`camcorder aim`), the game keeps the left stick, the buttons and the
// keyboard: only the look (right stick, mouse) and the zoom (triggers, mouse wheel) are read.
OperatorInput read_controls(State &s, float dt) {
    OperatorInput in;
    if (s.camcorder.status().mode != Mode::handheld) s.operating = false; // a tripod's view: the skater's again
    const bool pad_held = wants_pad(s), aim = wants_aim(s);
    set_capture(s, pad_held, aim);
    if (pad_held) {
        // Esc or Start while the camera has the controls (which keeps them from the game): a step
        // back out, the quick menu, then a tripod's setup, then full camera control. The next press
        // is the game's again: its pause menu.
        dingosdk::ControllerInput pressed;
        DingoSDKOverlayReadControllerInput(&pressed);
        constexpr std::uint32_t start_button = 0x0010; // XINPUT_GAMEPAD_START (Options on a PlayStation pad)
        const bool back = (DingoSDKOverlayReadPromptKeys() & 8u) != 0 || (pressed.buttons & start_button) != 0;
        // A press counts only once the key was seen up while the camera had the controls: one
        // already down when it took them, or begun while the ReSkate menu or console had the keys
        // (Esc closing the menu straight after SET UP A TRIPOD), is not a press to back out with.
        overlay::FlightInput probe;
        DingoSDKOverlayReadFlightInput(&probe, true, true);
        const bool readable = probe.active; // false while the menu, the console or chat has the keys
        if (!s.pad_was_held || !readable) s.back_armed = false;
        else if (!back) s.back_armed = true;
        s.pad_was_held = true;
        if (back && s.back_armed) {
            s.back_armed = false;
            if (s.quick) s.quick = false;
            else if (s.camcorder.tripod_setup()) s.camcorder.set_tripod_setup(false);
            else s.operating = false;
        }
        s.back_held = back;
        if (back) {
            s.wheel_zoom = 0;
            return in;
        }
    } else {
        s.back_held = false;
        s.pad_was_held = false;
        s.back_armed = false;
    }
    if (s.quick) {
        // The quick menu has the controls; nothing moves the camera meanwhile.
        if (wall_seconds() - s.quick_used > 8) s.quick = false; // left open: it closes itself
        else if (pad_held) quick_controls(s, dt);
        s.wheel_zoom = 0;
        return in;
    }
    const float wheel = wheel_rocker(s, dt);
    if (!pad_held && !aim) {
        s.wheel_zoom = 0;
        return in;
    }
    overlay::FlightInput pad;
    DingoSDKOverlayReadFlightInput(&pad, true, true);
    float mouse_x{}, mouse_y{};
    DingoSDKOverlayReadMouseLook(&mouse_x, &mouse_y);
    if (!pad.active) {
        s.wheel_zoom = 0; // the wheel scrolled the menu or the console
        return in;
    }
    const float turn = std::max(s.camcorder.handheld().settings().look_speed * std::max(dt, 1e-3f), 1e-4f);
    // Radians a pixel (the mod's), times the player's own sensitivity; up is up unless inverted.
    const float mouse = s.camcorder.handling().mouse * s.sensitivity;
    const float up = s.invert ? 1.0f : -1.0f;
    in.look_x = std::clamp(pad.look_x / 25.0f * s.sensitivity + mouse_x * mouse / turn, -1.0f, 1.0f);
    in.look_y = std::clamp(up * (pad.look_y / 25.0f * s.sensitivity + mouse_y * mouse / turn), -1.0f, 1.0f);
    // The same everywhere: Q / E, the wheel or the triggers zoom; R / F or LB / RB the height (the
    // camera's in the hand, a tripod's head). (Filming handheld those keys are kept from the game.)
    in.zoom = std::clamp(pad.up + wheel, -1.0f, 1.0f);
    in.raise = std::clamp(pad.pad_raise + pad.key_raise, -1.0f, 1.0f);
    if (aim) return in;
    // Full camera control, handheld: A / D or the left stick roll the frame (a dutch angle, held
    // while they are).
    if (!s.camcorder.tripod_setup()) {
        in.tilt = pad.right;
        return in;
    }
    // Setting up a tripod also carries it: WASD or the left stick, Shift or L3 faster.
    in.move_x = pad.right;
    in.move_y = pad.forward;
    in.fast = pad.boost;
    return in;
}

void release(State &s, std::uintptr_t base, std::uintptr_t client, bool ready, bool phase) {
    set_capture(s, false, false);
    s.wheel_zoom = 0;
    if (!s.owned) return;
    std::string detail;
    if (update_party_camera(base, client, ready, phase, nullptr, detail)) s.owned = false;
}

void publish(State &s, const Frame &frame) {
    std::lock_guard lock(s.published_mutex);
    s.published.frame = frame;
    s.published.clean_feed = s.clean_feed;
    s.published.frame_on = s.frame_on;
    s.published.sound = s.sound;
    s.published.still = s.still;
    s.published.tape = s.tape;
    s.published.capture_tape = s.capture_tape;
    {
        // The player's switches, and the camera's own motion, look and stamp.
        const auto &camera = s.camcorder.settings().camera;
        auto finish = s.finish;
        finish.motion = camera.motion;
        finish.look = camera.look;
        finish.stamp = camera.stamp;
        finish.ear = s.camcorder.handling().sound;
        s.published.finish = finish;
        s.published.effects = camera_effects(camera);
        s.published.viewfinder = camera.viewfinder;
        s.published.own_lines = camera.lines;
    }
    s.published.cameras.clear();
    for (const auto &c : s.catalogue.cameras()) s.published.cameras.push_back({c.id, c.name, c.source});
    s.published.lenses.clear();
    for (const auto &a : s.catalogue.adapters()) s.published.lenses.push_back({a.id, a.name, a.source});
    s.published.mod_notes = s.mod_notes.size();
    s.published.blink += 1.0f / 60; // only blinks; exact time does not matter
    // What the viewfinder says to do with the controls the camera has right now.
    const auto &status = frame.status;
    if (s.quick)
        s.published.hint = "W/S or wheel choose   D do it   A or Esc close   (stick: up/down, right, left)";
    else if (status.tripod_setup)
        s.published.hint = "WASD / L-stick carry it   Mouse / R-stick aim   Q/E or LT/RT zoom   R/F or LB/RB height   Tripod key or Esc: done   REC: roll";
    else if (s.operating && wants_pad(s))
        s.published.hint = status.grip_id == board_cam().id
                               ? "Mouse / R-stick aim   Q/E or LT/RT zoom   A/D or L-stick roll   Esc or Start: back to skating"
                               : "Mouse / R-stick aim   Q/E or LT/RT zoom   R/F or LB/RB height   A/D or L-stick roll   Esc or Start: back to skating";
    else if (wants_aim(s) && status.grip_id == board_cam().id)
        s.published.hint = "Mouse / R-stick aim   Q/E or LT/RT zoom";
    else if (wants_aim(s))
        s.published.hint = "Mouse / R-stick aim   Q/E or LT/RT zoom   R/F or LB/RB height";
    else
        s.published.hint.clear();
    s.published.quick.clear();
    s.published.quick_index = -1;
    if (s.quick) {
        for (const auto &item : quick_items(s)) s.published.quick.push_back({item.label, item.value});
        s.quick_index = std::clamp(s.quick_index, 0, static_cast<int>(s.published.quick.size()) - 1);
        s.published.quick_index = s.quick_index;
    }
    s.published.guide = wall_seconds() < s.guide_until;
    s.published.sensitivity = s.sensitivity;
    s.published.invert = s.invert;
    s.published.review = s.review;
    s.published.review_stop = s.review_stop;
    s.published.operating = s.operating;
    s.published.players.clear();
    s.published.subject.clear();
    s.published.subject_id = s.closest ? 0 : s.subject;
    s.published.closest = s.closest;
    for (const auto &p : s.players) {
        s.published.players.push_back({p.id, p.name});
        if (p.id == (s.closest ? s.nearest : s.subject)) s.published.subject = p.name;
    }
    if (s.closest && s.nearest == 0) s.published.subject = "you";
}
} // namespace

namespace {
// The feet's height from the board, when the board is under the skater: just below their root and
// close beside it (off it, the board lies about or is carried, and says nothing of the feet).
std::optional<float> feet_on_board(const multiplayer::Pose &pose) {
    if (pose.board.empty()) return std::nullopt;
    const auto &b = pose.board[0].position, &r = pose.root.position;
    const float dx = b[0] - r[0], dz = b[2] - r[2], below = r[1] - b[1];
    if (!std::isfinite(dx + dz + below) || dx * dx + dz * dz > 0.8f * 0.8f || below < 0.3f || below > 1.8f) return std::nullopt;
    return b[1] + 0.05f; // the deck, a touch above the board's own point
}
} // namespace

void note_local_skater(const multiplayer::Pose &pose) noexcept {
    auto &s = state();
    const auto now = Clock::now();
    s.local = pose.root;
    // The board is in the pose a few times a second (wants_full_pose); in between, the feet are
    // where they were under the root.
    if (const auto feet = feet_on_board(pose)) {
        s.local_lift = pose.root.position[1] - *feet;
        s.local_lift_at = now;
        s.local_feet = feet;
    } else if (now - s.local_lift_at < std::chrono::seconds(1)) {
        s.local_feet = pose.root.position[1] - s.local_lift;
    } else {
        s.local_feet.reset();
    }
    s.local_at = now;
}

bool wants_full_pose() noexcept {
    auto &s = state();
    const auto now = Clock::now();
    if (!s.camcorder.powered() || now - s.full_pose_at < std::chrono::milliseconds(250)) return false;
    s.full_pose_at = now;
    return true;
}

Player filmable(std::uint64_t id, std::string name, const multiplayer::Pose &pose) {
    return {id, std::move(name), pose.root, feet_on_board(pose)};
}

void note_players(std::vector<Player> players) noexcept { state().players = std::move(players); }

bool owns_camera() noexcept { return state().owned; }

bool wants_controller() noexcept { return wants_pad(state()); }

bool powered() noexcept { return state().camcorder.powered(); }

void note_warp_failed() noexcept { state().warp_failed.store(true); }

void note_render_aspect(float aspect) noexcept {
    if (std::isfinite(aspect) && aspect > 0.2f && aspect < 10) state().render_aspect.store(aspect, std::memory_order_relaxed);
}

void tick(std::uintptr_t base, std::uintptr_t client, bool ready, bool camera_phase) noexcept {
    auto &s = state();
    try {
        if (!s.preferences_loaded) {
            // The mods' cameras and lenses, then last time's settings, once: each line is a camcorder command.
            s.preferences_loaded = true;
            scan_profiles(s);
            if (const auto path = preferences_path(); !path.empty()) {
                std::ifstream file(path, std::ios::binary);
                std::stringstream text;
                text << file.rdbuf();
                s.loading = true;
                const auto preferences = read_preferences(text.str());
                bool current = false;
                for (const auto &preference : preferences) current = current || preference_command(preference).first == "version";
                for (const auto &preference : preferences) {
                    auto [verb, words] = preference_command(preference);
                    // An older file has the look off only because it was off by default then.
                    if (!current && verb == "look") continue;
                    if (is_setting(verb)) command(verb, words);
                }
                s.loading = false;
            }
        }
        // The true fisheye, unless the overlay found it cannot draw it here.
        s.camcorder.settings().fisheye_render =
            !s.warp_failed.load(std::memory_order_relaxed) ? FisheyeRender::warp : FisheyeRender::approximate;
        const auto now = Clock::now();
        const float dt = s.last_tick == Clock::time_point{} ? 0.0f
                         : std::clamp(std::chrono::duration<float>(now - s.last_tick).count(), 0.0f, 0.1f);
        s.last_tick = now;
        // The party's Spectate holds the same native camera lease: never both at once.
        if (multiplayer::spectating_party_member()) {
            if (s.camcorder.powered()) {
                s.camcorder.power(false);
                logging::write(logging::Level::info, logging::Channel::ui, "Camcorder off: stop spectating to film.");
            }
            // Spectate drives the one camera lease now: let go of it without handing the game its
            // camera back (that would undo Spectate's for a frame).
            if (s.owned) {
                s.owned = false;
                set_capture(s, false, false);
            }
        }
        if (s.camcorder.powered() != s.was_powered) {
            s.was_powered = s.camcorder.powered();
            if (s.was_powered) {
                // The first time ever: a short guide over the viewfinder.
                if (!s.guide_seen) {
                    s.guide_seen = true;
                    s.guide_until = wall_seconds() + 15;
                    save_preferences(s);
                }
                // Start looking where the game's camera was looking, level, so the first frame faces
                // the way the player was already looking.
                if (const auto view = latest_game_view(); view && valid_camera_matrix(view->world))
                    s.camcorder.aim_handheld({matrix_yaw_pitch(view->world).yaw, 3 * degrees});
            } else {
                s.quick = false;
                s.operating = false; // the skater's controls again next time
                s.guide_until = 0;
                ++s.review_stop; // a review ends with the camera
            }
        }
        // Out of play and back (a new map, a reload): the tripods stood in the world that was. (Not
        // for the game's menus below: a pause leaves them standing.)
        if (!ready) {
            s.left_play = true;
        } else if (std::exchange(s.left_play, false)) {
            for (int slot = 0; slot < static_cast<int>(tripod_slots); ++slot) s.camcorder.remove_tripod(slot);
            s.place_until = 0;
        }
        // The game's pause menu (or any of its menus) gets the screen, the camera and the controls:
        // the camcorder steps aside, as for loading, and comes back when it closes.
        if (s.camcorder.powered() && multiplayer::sample_game_ui_state(base).in_menu) {
            ready = false;
            s.quick = false;
        }
        s.in_play = ready;
        if (!s.camcorder.powered() || !ready) {
            if (!ready && s.camcorder.deck().recording()) s.camcorder.deck().stop();
            release(s, base, client, ready, camera_phase);
            auto idle = s.camcorder.tick({});
            idle.shot.valid = false; // no viewfinder over menus or loading (a tripod still has its view)
            publish(s, idle);
            return;
        }
        TickInput in;
        in.dt = dt;
        in.filmer = filmer_from(s, now, dt);
        if (in.filmer.valid) {
            in.filmer.on_board = on_board(client);
            // The ground under the filmer, so the handheld camera's heights are from the feet.
            const auto &p = in.filmer.position;
            // The board under them says where the feet are; on foot (or without a board), the ground.
            in.filmer.ground = in.filmer.on_board && s.local_feet ? s.local_feet : local_ground_height(p[0], p[2], p[1] + 0.5f, p[1] - 3.0f);
            in.filmer.root_height = s.local_root.update(in.filmer.ground ? std::optional<float>(p[1] - *in.filmer.ground) : std::nullopt, dt);
        }
        if (s.place_until > 0) {
            if (in.filmer.valid) {
                s.place_until = 0;
                s.camcorder.place_tripod(in.filmer);
            } else if (wall_seconds() > s.place_until) {
                s.place_until = 0;
                overlay::notify(overlay::NoticeLevel::warning, "No tripod set up.", "Nowhere to stand it: load into a map and stand on the ground.");
            }
        }
        in.subject = subject_from(s, in.filmer, dt);
        in.controls = read_controls(s, dt);
        in.ground_under_tripod = ground_under_tripod(s.camcorder);
        in.render_aspect = s.render_aspect.load(std::memory_order_relaxed);
        auto frame = s.camcorder.tick(in);
        // Never under the ground: a handheld lens dipping below the surface under it (a landing, a
        // slope, the board cam over a kerb) is held just above it. (Only the ground: walls and the
        // skater's own body are the rig's to keep clear of.)
        if (frame.shot.valid && s.camcorder.status().mode == Mode::handheld) {
            auto &c = frame.shot.camera;
            if (const auto ground = local_ground_height(c[12], c[14], c[13] + 0.3f, c[13] - 2.0f); ground && c[13] < *ground + 0.06f)
                c[13] = *ground + 0.06f;
        }
        std::string detail;
        if (frame.shot.valid) {
            if (update_party_camera(base, client, ready, camera_phase, &frame.shot.camera, detail,
                                    frame.optics.render_vertical_fov)) {
                s.owned = true;
                s.refused.clear();
            } else if (!s.owned) {
                s.refused = detail; // the game kept its camera (Freecam, noclip, a cutscene...)
            } else if (detail.find("replaced by the game") != std::string::npos) {
                s.owned = false; // the game switched cameras (a menu, a cutscene): it is theirs now
            }
        } else {
            release(s, base, client, ready, camera_phase);
        }
        if (!detail.empty() && detail != s.last_detail) {
            s.last_detail = detail;
            logging::write(logging::Level::info, logging::Channel::ui, "Camcorder: " + detail);
        }
        // The viewfinder only over the camcorder's own picture.
        frame.shot.valid = frame.shot.valid && s.owned;
        // The sound, as heard where the camera is (the game mixes it for a listener by the skater).
        const auto placement = frame.shot.valid && in.filmer.valid ? ear_placement(frame.shot.camera, in.filmer.position) : EarPlacement{};
        s.ear_distance.store(placement.distance, std::memory_order_relaxed);
        s.ear_side.store(placement.side, std::memory_order_relaxed);
        publish(s, frame);
    } catch (...) {
        release(s, base, client, ready, camera_phase);
    }
}

std::string command(std::string_view verb, const std::vector<std::string> &words) {
    auto &s = state();
    const auto save = [&s] {
        if (!s.loading) save_preferences(s);
    };
    if (verb == "subject") {
        s.subject_root.reset();
        s.subject_velocity.reset(); // a new subject: its speed starts afresh, not as one leap across
        if (words.empty() || words[0] == "none") {
            s.subject = 0;
            s.closest = false;
            return "Filming nobody in particular.";
        }
        if (words[0] == "closest" || words[0] == "nearest") {
            s.subject = 0;
            s.closest = true;
            s.nearest = ~std::uint64_t{};
            return "Filming whoever is closest to the camera (a tripod: you too).";
        }
        s.closest = false;
        if (words[0] == "next") {
            // The next player after the current one, round the list.
            if (s.players.empty()) return "Nobody else here to film.";
            std::size_t at = 0;
            for (std::size_t i = 0; i < s.players.size(); ++i)
                if (s.players[i].id == s.subject) at = i + 1;
            s.subject = s.players[at % s.players.size()].id;
            return "Filming " + s.players[at % s.players.size()].name + ".";
        }
        // A player's id (the menu picks by it: any name works, whatever letters it has), or their
        // name: the words are the name (it can have spaces), exact, then any case, then its start.
        std::string name;
        for (const auto &word : words) name += (name.empty() ? "" : " ") + word;
        const auto lower = [](std::string text) {
            for (auto &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        };
        const auto pick = [&](auto &&matches) -> const Player * {
            for (const auto &p : s.players)
                if (matches(p)) return &p;
            return nullptr;
        };
        const auto wanted = lower(name);
        const Player *found = pick([&](const Player &p) { return std::to_string(p.id) == name; });
        if (!found) found = pick([&](const Player &p) { return p.name == name; });
        if (!found) found = pick([&](const Player &p) { return lower(p.name) == wanted; });
        if (!found) found = pick([&](const Player &p) { return lower(p.name).starts_with(wanted); });
        if (!found) return "No player called " + name + ".";
        s.subject = found->id;
        return "Filming " + found->name + ".";
    }
    // Full camera control or filming while skating: the skater waits and every control works the
    // camera (roll too), or the skater keeps the left stick and buttons and films as they go.
    if (verb == "operate") {
        const bool want = words.empty() || words[0] == "toggle" ? !s.operating : words[0] == "on" || words[0] == "1";
        if (want && s.camcorder.status().mode != Mode::handheld)
            return "From a tripod, set it up (camcorder tripod setup) for full control of it.";
        s.operating = want;
        return s.operating ? "Full camera control: the skater waits. Mouse or right stick aim, Q / E or the triggers zoom, R / F or the bumpers raise it, A / D or the left stick roll. Esc or Start: back to skating."
                           : "Filming while skating: the mouse or right stick aim, Q / E or the triggers zoom, R / F or the bumpers raise it.";
    }
    if (verb == "frame") {
        int pick = s.frame_on;
        if (words.empty() || words[0] == "next") pick = (s.frame_on + 1) % 3;
        else if (words[0] == "board" || words[0] == "feet") pick = 0;
        else if (words[0] == "body" || words[0] == "skater") pick = 1;
        else if (words[0] == "head" || words[0] == "face") pick = 2;
        else return "Usage: camcorder frame board|body|head|next";
        s.frame_on = pick;
        apply_handling(s);
        save();
        return pick == 0 ? "Framing their board and feet." : pick == 1 ? "Framing the whole skater." : "Framing their head and shoulders.";
    }
    const auto switched = [&](bool current) {
        return words.empty() || words[0] == "toggle" ? !current : words[0] == "on" || words[0] == "1";
    };
    if (verb == "look") {
        const auto word = words.empty() ? std::string("toggle") : words[0];
        if (word == "dv") s.finish.dv.on = true;
        else if (word == "clean" || word == "off") s.finish.dv.on = false;
        else if (word == "toggle") s.finish.dv.on = !s.finish.dv.on;
        else return "Usage: camcorder look dv|clean";
        save();
        return s.finish.dv.on ? s.camcorder.settings().camera.look.format + " look on." : "Clean picture.";
    }
    if (verb == "dv") {
        auto &dv = s.finish.dv;
        const auto part = words.empty() ? std::string() : words[0];
        bool *flag = part == "fields" ? &dv.fields : part == "highlights" ? &dv.highlights
                   : part == "balance" ? &dv.white_balance : part == "sharpen" ? &dv.sharpen
                   : part == "chroma" ? &dv.chroma : part == "grain" ? &dv.grain : nullptr;
        if (!flag) return "Usage: camcorder dv fields|highlights|balance|sharpen|chroma|grain [on|off|toggle]";
        const std::vector<std::string> rest(words.begin() + 1, words.end());
        *flag = rest.empty() || rest[0] == "toggle" ? !*flag : rest[0] == "on" || rest[0] == "1";
        save();
        return "DV " + part + (*flag ? " on." : " off.") + (dv.on ? "" : " (The DV look itself is off: camcorder look dv.)");
    }
    if (verb == "date") {
        // The camera's clock: set like a real camcorder's, it runs on from there (and is kept).
        if (!words.empty() && words[0] == "set") {
            if (words.size() < 2 || words[1] == "now") {
                s.finish.stamp_offset = 0;
                save();
                return "The camera's clock is the real time.";
            }
            const auto offset = clock_offset(words[1], words.size() > 2 ? words[2] : std::string(), std::time(nullptr));
            if (!offset) return "Usage: camcorder date set <YYYY-MM-DD> [HH:MM] | now";
            s.finish.stamp_offset = *offset;
            s.finish.stamp_year = 0; // the set date says its own year
            s.finish.date_stamp = true;
            save();
            return "The camera's clock is set to " + words[1] + (words.size() > 2 ? " " + words[2] : std::string()) + ", and runs on from there.";
        }
        if (!words.empty() && words[0] == "offset") { // the settings file's form of `date set`
            if (words.size() > 1) s.finish.stamp_offset = std::atoll(words[1].c_str());
            return "";
        }
        if (!words.empty() && words[0] == "year") {
            if (words.size() < 2 || words[1] == "now") s.finish.stamp_year = 0;
            else {
                const int year = std::atoi(words[1].c_str());
                if (year < 1970 || year > 2099) return "Usage: camcorder date year <1970-2099>|now";
                s.finish.stamp_year = year;
            }
            save();
            return s.finish.stamp_year ? "The date stamp says " + std::to_string(s.finish.stamp_year) + "."
                                       : "The date stamp says this year.";
        }
        s.finish.date_stamp = switched(s.finish.date_stamp);
        save();
        return s.finish.date_stamp ? "Date stamp on clips and stills." : "No date stamp.";
    }
    if (verb == "still" || verb == "photo") {
        if (!s.camcorder.powered()) return "Turn the camcorder on first.";
        ++s.still;
        return "Still taken: it goes to Videos\\ReSkate Camcorder\\Stills.";
    }
    if (verb == "tape" && !words.empty() && words[0] == "capture") {
        ++s.capture_tape;
        return "Capturing this tape's clips into one file...";
    }
    if (verb == "tape" && !words.empty() && words[0] == "new") ++s.tape; // and the model starts the new tape
    if (verb == "sound") {
        const auto word = words.empty() ? std::string() : words[0];
        if (word == "off") s.sound = Sound::off;
        else if (word == "game" || word == "on") s.sound = Sound::game;
        else if (word == "vx" || word == "mic" || word == "camera") s.sound = Sound::game; // older settings: the mic is gone
        else if (!word.empty()) return "Usage: camcorder sound off|game";
        save();
        return s.sound == Sound::off ? "Clips without sound." : "Clips with the game's sound, heard from where the camera is.";
    }
    if (verb == "sensitivity") {
        const float value = words.empty() ? 1.0f : static_cast<float>(std::atof(words[0].c_str()));
        if (!(value >= 0.25f && value <= 4.0f)) return "Usage: camcorder sensitivity <0.25 to 4> (1 as built)";
        s.sensitivity = value;
        save();
        char text[64];
        std::snprintf(text, sizeof(text), "Aim sensitivity %.2gx.", static_cast<double>(value));
        return text;
    }
    if (verb == "invert") {
        s.invert = switched(s.invert);
        save();
        return s.invert ? "Up and down inverted: push up to look down." : "Push up to look up.";
    }
    if (verb == "guide") {
        if (!words.empty() && words[0] == "seen") {
            s.guide_seen = true;
            return "";
        }
        if (!words.empty() && words[0] == "off") {
            s.guide_until = 0;
            return "Guide hidden.";
        }
        if (!s.camcorder.powered()) return "Turn the camcorder on to see the guide over its picture.";
        s.guide_until = wall_seconds() + 20;
        return "The guide is over the viewfinder for a few seconds.";
    }
    if (verb == "quick") {
        const auto word = words.empty() ? std::string("toggle") : words[0];
        const bool open = word == "open" || (word == "toggle" && !s.quick);
        if (open && !s.camcorder.powered()) return "Turn the camcorder on first.";
        s.quick = open;
        if (open) {
            s.quick_used = wall_seconds();
            s.quick_index = 0; // from the top each time
        }
        s.quick_last = 0;
        return open ? "Quick menu: W/S or the wheel choose, D does it, A closes." : "Quick menu closed.";
    }
    if (verb == "review") {
        if (!words.empty() && words[0] == "stop") {
            ++s.review_stop;
            return "Review stopped.";
        }
        if (!s.camcorder.powered()) return "Turn the camcorder on first: the clip plays in its viewfinder.";
        ++s.review;
        return "Playing the last clip in the viewfinder (camcorder review stop, or REC, to stop).";
    }
    if (verb == "open") {
        // Explorer on the folder, off the game thread (Explorer can take its time).
        const bool stills = !words.empty() && (words[0] == "stills" || words[0] == "pictures");
        std::thread([stills] {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            PWSTR base{};
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr, &base)) && base) {
                std::wstring path = std::wstring(base) + L"\\ReSkate Camcorder";
                CreateDirectoryW(path.c_str(), nullptr);
                if (stills) {
                    path += L"\\Stills";
                    CreateDirectoryW(path.c_str(), nullptr);
                }
                ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (base) CoTaskMemFree(base);
            if (SUCCEEDED(com)) CoUninitialize();
        }).detach();
        return stills ? "Opening Videos\\ReSkate Camcorder\\Stills." : "Opening Videos\\ReSkate Camcorder.";
    }
    if (verb == "clean") {
        s.clean_feed = words.empty() || words[0] == "toggle" ? !s.clean_feed : words[0] == "on" || words[0] == "1";
        save();
        return s.clean_feed ? "Clean feed: the frame and lens only." : "Viewfinder readouts on.";
    }
    if (verb == "mods") {
        if (!words.empty() && words[0] == "reload") scan_profiles(s);
        std::string text = "Cameras:";
        for (const auto &c : s.catalogue.cameras()) text += " " + c.id + (c.source.empty() ? "" : " (" + c.source + ")");
        text += ". Lenses: stock";
        for (const auto &a : s.catalogue.adapters()) text += ", " + a.id + (a.source.empty() ? "" : " (" + a.source + ")");
        text += ". Handling: ";
        if (s.catalogue.handling_sources.empty()) text += "as built";
        for (std::size_t i = 0; i < s.catalogue.handling_sources.size(); ++i)
            text += (i ? ", then " : "from ") + s.catalogue.handling_sources[i];
        text += ".";
        if (s.mod_notes.empty()) return text + " Nothing wrong in the mods' profiles.";
        for (const auto &note : s.mod_notes) text += "\n" + note;
        return text;
    }
    if (verb == "effects") {
        if (!words.empty() && words[0] == "list") {
            std::string text = "Effects a camera can use (docs/MODDING.md has their fields):";
            for (const auto &kind : effect_library()) text += "\n" + std::string(kind.name) + ": " + std::string(kind.what) + ".";
            return text;
        }
        const auto &camera = s.camcorder.settings().camera;
        std::string text = camera.name + (camera.effects ? "'s effects:" : "'s effects (from its look):");
        const auto stack = camera_effects(camera);
        for (const auto &block : stack) text += " " + std::string(effect_kind(block.type).name) + (part_on(block.part, s.finish.dv) ? "" : " (off)") + ",";
        if (stack.empty()) text += " none,";
        text.back() = '.';
        return text + (s.finish.dv.on ? "" : " The look is off (camcorder look dv).");
    }
    if (verb == "export") return export_profiles(s);
    CommandContext context;
    context.filmer = filmer_from(s, Clock::now(), 0);
    context.catalogue = &s.catalogue;
    // With the camera off the game isn't asked where the skater is: turn it on, and the tripod goes
    // down on the first tick that knows (a moment later).
    if (verb == "tripod" && !words.empty() && words[0] == "place" && !s.camcorder.tripod_setup() && !context.filmer.valid) {
        s.camcorder.power(true);
        s.place_until = wall_seconds() + 3;
        return "Setting up a tripod where you stand...";
    }
    auto answer = run_command(s.camcorder, verb, words, context);
    if (verb == "camera" || verb == "lens" || verb == "grip" || verb == "rig" || verb == "head") save();
    if ((verb.empty() || verb == "status") && s.camcorder.powered() && !s.owned) {
        if (!s.refused.empty()) answer += " The game kept its camera: " + s.refused;
        else if (!context.filmer.valid && s.camcorder.status().mode == Mode::handheld)
            answer += " No position for your skater yet (load into a map and skate).";
        else answer += " Waiting for the game's camera.";
    }
    return answer;
}

EarPlacement ear() noexcept {
    const auto &s = state();
    return {s.ear_distance.load(std::memory_order_relaxed), s.ear_side.load(std::memory_order_relaxed)};
}

Published published() noexcept {
    auto &s = state();
    std::lock_guard lock(s.published_mutex);
    return s.published;
}
} // namespace dingosdk::camcorder_runtime
