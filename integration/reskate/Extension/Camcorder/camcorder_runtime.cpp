// REFERENCE ONLY: not yet compiled or run in the game (integration/reskate/README.md).
#include "camcorder_runtime.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Game/UI/game_view.h"
#include "Extension/Multiplayer/Hud/native_party.h"
#include "Extension/Skater/client_source_spawn.h"
#include "Extension/UI/Overlay/overlay.h"
#include "camcorder/commands.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>

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
    std::vector<Player> players;
    std::uint64_t subject{}; // 0: nobody (a tripod then films the local player)
    VelocityEstimate subject_velocity;
    bool operating{};        // the pad drives the camera (`camcorder operate`)
    bool captured{};         // we asked the overlay to hold the pad
    bool owned{};            // update_party_camera holds the screen for us
    bool clean_feed{};
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
    // TODO(in play): use the physics state for on/off board. client_source::detail::first_person_on_foot
    // reads it; it is internal to Extension/Skater today.
    f.on_board = true;
    return f;
}

std::optional<Subject> subject_from(State &s, float dt) {
    if (s.subject)
        for (const auto &p : s.players)
            if (p.id == s.subject) {
                const auto at = to_vec(p.root.position);
                return Subject{true, at, s.subject_velocity.update(at, dt)};
            }
    s.subject_velocity.reset();
    return std::nullopt;
}

// Only while the camcorder's picture is on screen: without it the pad stays the skater's.
bool wants_pad(State &s) { return s.camcorder.powered() && s.owned && (s.operating || s.camcorder.tripod_setup()); }

// The pad, keyboard and mouse, while the camera has them (Extension/UI/Overlay/overlay_input.cpp):
// the freecam's flight input for the pad and keyboard (sticks and WASD; triggers or Q / E as
// up = right - left; L3 or Shift as boost; look as stick deflection times 25), and the mouse with
// no button held, in pixels of .0025 radians each as free_flight.h turns them. Look y grows
// downward there; the camcorder's grows upward.
OperatorInput read_controls(State &s, float dt) {
    OperatorInput in;
    if (!wants_pad(s)) {
        if (s.captured) {
            DingoSDKOverlaySetFreecamInputCapture(false);
            s.captured = false;
        }
        return in;
    }
    // Every tick: the client tick sets the capture too (client_tick.cpp asks wants_controller()).
    DingoSDKOverlaySetFreecamInputCapture(true);
    s.captured = true;
    overlay::FlightInput pad;
    DingoSDKOverlayReadFlightInput(&pad, true, true);
    float mouse_x{}, mouse_y{};
    DingoSDKOverlayReadMouseLook(&mouse_x, &mouse_y);
    if (!pad.active) return in; // a menu or the console is open
    const float turn = std::max(s.camcorder.handheld().settings().look_speed * std::max(dt, 1e-3f), 1e-4f);
    in.look_x = std::clamp(pad.look_x / 25.0f + mouse_x * .0025f / turn, -1.0f, 1.0f);
    in.look_y = std::clamp(-(pad.look_y / 25.0f + mouse_y * .0025f / turn), -1.0f, 1.0f);
    in.move_x = pad.right;   // the pad's reading includes the keyboard's WASD, Q and E
    in.move_y = pad.forward;
    if (s.camcorder.tripod_setup() && !pad.boost) {
        in.raise = pad.up; // setting up: triggers raise and lower the head
    } else {
        in.zoom = pad.up;  // otherwise (or holding boost) they work the zoom rocker
        in.fast = pad.boost;
    }
    return in;
}

void release(State &s, std::uintptr_t base, std::uintptr_t client, bool ready, bool phase) {
    if (s.captured) {
        DingoSDKOverlaySetFreecamInputCapture(false);
        s.captured = false;
    }
    if (!s.owned) return;
    std::string detail;
    if (update_party_camera(base, client, ready, phase, nullptr, detail)) s.owned = false;
}

void publish(State &s, const Frame &frame) {
    std::lock_guard lock(s.published_mutex);
    s.published.frame = frame;
    s.published.clean_feed = s.clean_feed;
    s.published.operating = s.operating;
    s.published.blink += 1.0f / 60; // only blinks; exact time does not matter
    // What the viewfinder says to do with the controls the camera has right now.
    const auto &status = frame.status;
    if (status.tripod_setup)
        s.published.hint = "Mouse aim   WASD move   Q/E height   Shift+Q/E zoom   REC to roll";
    else if (s.operating && wants_pad(s))
        s.published.hint = "Mouse aim   Q/E zoom   (operate off to skate)";
    else
        s.published.hint.clear();
    s.published.players.clear();
    s.published.subject.clear();
    for (const auto &p : s.players) {
        s.published.players.push_back(p.name);
        if (p.id == s.subject) s.published.subject = p.name;
    }
}
} // namespace

void note_local_skater(const multiplayer::Transform &root) noexcept {
    auto &s = state();
    s.local = root;
    s.local_at = Clock::now();
}

void note_players(std::vector<Player> players) noexcept { state().players = std::move(players); }

bool owns_camera() noexcept { return state().owned; }

bool wants_controller() noexcept { return wants_pad(state()); }

bool powered() noexcept { return state().camcorder.powered(); }

void note_render_aspect(float aspect) noexcept {
    if (std::isfinite(aspect) && aspect > 0.2f && aspect < 10) state().render_aspect.store(aspect, std::memory_order_relaxed);
}

void tick(std::uintptr_t base, std::uintptr_t client, bool ready, bool camera_phase) noexcept {
    auto &s = state();
    try {
        const auto now = Clock::now();
        const float dt = s.last_tick == Clock::time_point{} ? 0.0f
                         : std::clamp(std::chrono::duration<float>(now - s.last_tick).count(), 0.0f, 0.1f);
        s.last_tick = now;
        // The party's Spectate holds the same native camera lease: never both at once.
        if (s.camcorder.powered() && multiplayer::spectating_party_member()) {
            s.camcorder.power(false);
            logging::write(logging::Level::info, logging::Channel::ui, "Camcorder off: stop spectating to film.");
        }
        if (s.camcorder.powered() != s.was_powered) {
            s.was_powered = s.camcorder.powered();
            if (s.was_powered) {
                // Start looking where the game's camera was looking, level, so the first frame faces
                // the way the player was already looking.
                if (const auto view = latest_game_view(); view && valid_camera_matrix(view->world))
                    s.camcorder.aim_handheld({matrix_yaw_pitch(view->world).yaw, 3 * degrees});
            } else {
                s.operating = false; // a fresh start next time: the pad is the skater's
            }
        }
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
        in.subject = subject_from(s, dt);
        in.controls = read_controls(s, dt);
        // TODO: the ground under the tripod. Factor the trainer's downward ray (trainer.cpp
        // land_at, trainer_landing.h) into a shared helper and query it at the tripod's x, z.
        in.ground_under_tripod = std::nullopt;
        in.render_aspect = s.render_aspect.load(std::memory_order_relaxed);
        auto frame = s.camcorder.tick(in);
        std::string detail;
        if (frame.shot.valid) {
            if (update_party_camera(base, client, ready, camera_phase, &frame.shot.camera, detail,
                                    frame.optics.render_vertical_fov)) {
                s.owned = true;
                s.refused.clear();
            } else if (!s.owned) {
                s.refused = detail; // the game kept its camera (Freecam, noclip, a cutscene...)
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
        publish(s, frame);
    } catch (...) {
        release(s, base, client, ready, camera_phase);
    }
}

std::string command(std::string_view verb, const std::vector<std::string> &words) {
    auto &s = state();
    if (verb == "subject") {
        if (words.empty() || words[0] == "none") {
            s.subject = 0;
            return "Filming nobody in particular.";
        }
        if (words[0] == "next") {
            // The next player after the current one, round the list.
            if (s.players.empty()) return "Nobody else here to film.";
            std::size_t at = 0;
            for (std::size_t i = 0; i < s.players.size(); ++i)
                if (s.players[i].id == s.subject) at = i + 1;
            s.subject = s.players[at % s.players.size()].id;
            return "Filming " + s.players[at % s.players.size()].name + ".";
        }
        // A name can have spaces: the words are the name.
        std::string name;
        for (const auto &word : words) name += (name.empty() ? "" : " ") + word;
        for (const auto &p : s.players)
            if (p.name == name || std::to_string(p.id) == name) {
                s.subject = p.id;
                return "Filming " + p.name + ".";
            }
        return "No player called " + name + ".";
    }
    if (verb == "operate") {
        s.operating = words.empty() || words[0] == "toggle" ? !s.operating : words[0] == "on" || words[0] == "1";
        return s.operating ? "The pad works the camera." : "The pad is the skater's again.";
    }
    if (verb == "clean") {
        s.clean_feed = words.empty() || words[0] == "toggle" ? !s.clean_feed : words[0] == "on" || words[0] == "1";
        return s.clean_feed ? "Clean feed: the frame and lens only." : "Viewfinder readouts on.";
    }
    CommandContext context;
    context.filmer = filmer_from(s, Clock::now(), 0);
    auto answer = run_command(s.camcorder, verb, words, context);
    if ((verb.empty() || verb == "status") && s.camcorder.powered() && s.operating)
        answer += " Operate is on: the pad works the camera.";
    if ((verb.empty() || verb == "status") && s.camcorder.powered() && !s.owned) {
        if (!s.refused.empty()) answer += " The game kept its camera: " + s.refused;
        else if (!context.filmer.valid && s.camcorder.status().mode == Mode::handheld)
            answer += " No position for your skater yet (load into a map and skate).";
        else answer += " Waiting for the game's camera.";
    }
    return answer;
}

Published published() noexcept {
    auto &s = state();
    std::lock_guard lock(s.published_mutex);
    return s.published;
}
} // namespace dingosdk::camcorder_runtime
