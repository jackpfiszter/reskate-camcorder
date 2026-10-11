// REFERENCE ONLY: not yet compiled or run in the game (integration/reskate/README.md).
#include "camcorder_runtime.h"
#include "Engine/Core/Log/logging.h"
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
    std::optional<multiplayer::Transform> local, previous_local;
    std::vector<Player> players, previous_players;
    float since_local{}; // seconds between the last two local notes
    std::uint64_t subject{}; // 0: nobody
    bool operating{};        // the pad drives the camera (`camcorder operate`)
    bool captured{};         // we asked the overlay to hold the pad
    bool owned{};            // update_party_camera holds the screen for us
    bool clean_feed{};
    std::string last_detail;
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

Filmer filmer_from(const State &s) {
    Filmer f;
    if (!s.local) return f;
    f.valid = true;
    f.position = to_vec(s.local->position);
    // The skater's body faces along its third row (follow_camera.cpp reads it the same way).
    const auto body = multiplayer::to_matrix(*s.local);
    f.forward = {body[8], 0, body[10]};
    if (s.previous_local && s.since_local > 0)
        f.velocity = mul(sub(f.position, to_vec(s.previous_local->position)), 1.0f / s.since_local);
    // TODO(in play): use the physics state for on/off board. client_source::detail::first_person_on_foot
    // reads it; it is internal to Extension/Skater today.
    f.on_board = true;
    return f;
}

std::optional<Subject> subject_from(const State &s) {
    if (!s.subject) return std::nullopt;
    for (const auto &p : s.players) {
        if (p.id != s.subject) continue;
        Subject subject{true, to_vec(p.root.position), {}};
        for (const auto &q : s.previous_players)
            if (q.id == p.id && s.since_local > 0)
                subject.velocity = mul(sub(subject.position, to_vec(q.root.position)), 1.0f / s.since_local);
        return subject;
    }
    return std::nullopt;
}

// The pad, while the camera has it. The overlay's flight input is the freecam's: sticks,
// triggers (up = right trigger - left trigger) and a boost button; look_* are freecam deltas
// of .0025 radians each (free_flight.h), turned here into the stick deflection that turns the
// camcorder by the same angle this tick.
OperatorInput read_controls(State &s, float dt) {
    OperatorInput in;
    const bool want = s.operating || s.camcorder.status().tripod_setup;
    if (want != s.captured) {
        DingoSDKOverlaySetFreecamInputCapture(want);
        s.captured = want;
    }
    if (!want) return in;
    overlay::FlightInput flight;
    DingoSDKOverlayReadFlightInput(&flight, true, false);
    if (!flight.active) return in;
    const float turn = std::max(s.camcorder.handheld().settings().look_speed * std::max(dt, 1e-3f), 1e-4f);
    in.look_x = std::clamp(flight.look_x * .0025f / turn, -1.0f, 1.0f);
    in.look_y = std::clamp(flight.look_y * .0025f / turn, -1.0f, 1.0f);
    in.move_x = flight.right;
    in.move_y = flight.forward;
    if (s.camcorder.status().tripod_setup && !flight.boost) {
        in.raise = flight.up; // setting up: triggers raise and lower the head
    } else {
        in.zoom = flight.up;  // otherwise (or holding boost) they work the zoom rocker
        in.fast = flight.boost;
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
    s.published.blink += 1.0f / 60; // only blinks; exact time does not matter
}
} // namespace

void note_local_skater(const multiplayer::Transform &root) noexcept {
    auto &s = state();
    s.previous_local = s.local;
    s.local = root;
}

void note_players(std::vector<Player> players) noexcept {
    auto &s = state();
    s.previous_players = std::move(s.players);
    s.players = std::move(players);
}

bool owns_camera() noexcept { return state().owned; }

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
        s.since_local = dt;
        if (!s.camcorder.powered() || !ready) {
            if (!ready && s.camcorder.deck().recording()) s.camcorder.deck().stop();
            release(s, base, client, ready, camera_phase);
            publish(s, s.camcorder.tick({}));
            return;
        }
        TickInput in;
        in.dt = dt;
        in.filmer = filmer_from(s);
        in.subject = subject_from(s);
        in.controls = read_controls(s, dt);
        // TODO: the ground under the tripod. Factor the trainer's downward ray (trainer.cpp
        // land_at, trainer_landing.h) into a shared helper and query it at the tripod's x, z.
        in.ground_under_tripod = std::nullopt;
        in.render_aspect = s.render_aspect.load(std::memory_order_relaxed);
        const auto frame = s.camcorder.tick(in);
        std::string detail;
        if (frame.shot.valid) {
            const bool applied = update_party_camera(base, client, ready, camera_phase, &frame.shot.camera, detail,
                                                     frame.optics.render_vertical_fov);
            if (applied) s.owned = true;
        } else {
            release(s, base, client, ready, camera_phase);
        }
        if (!detail.empty() && detail != s.last_detail) {
            s.last_detail = detail;
            logging::write(logging::Level::info, logging::Channel::ui, detail);
        }
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
        for (const auto &p : s.players)
            if (p.name == words[0] || std::to_string(p.id) == words[0]) {
                s.subject = p.id;
                return "Filming " + p.name + ".";
            }
        return "No player called " + words[0] + ".";
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
    context.filmer = filmer_from(s);
    return run_command(s.camcorder, verb, words, context);
}

Published published() noexcept {
    auto &s = state();
    std::lock_guard lock(s.published_mutex);
    return s.published;
}
} // namespace dingosdk::camcorder_runtime
