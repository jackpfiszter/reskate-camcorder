#pragma once
// ReSkate's side of the camcorder (src/camcorder is the model): reads the world each client
// tick, runs the model, and puts its shot on screen through the same native camera lease the
// party's Spectate uses (update_party_camera, Extension/Skater/client_source_spawn.h).
//
// Built into ReSkate.dll by integration/reskate/install.ps1 (see its README for where it plugs in).
//
// Threads, as in the trainer: the client (game) thread owns the camcorder. The console runs
// `camcorder ...` commands on that thread; the overlay only reads the published frame.
#include "Extension/Multiplayer/Net/protocol.h"
#include "camcorder/audio.h"
#include "camcorder/camcorder.h"
#include "camcorder/look.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::camcorder_runtime {
// Whether the camcorder is on. The session only reads the local skater for it while it is.
bool powered() noexcept;
// Session, client thread, every tick: the local skater as the game placed them.
void note_local_skater(const multiplayer::Transform &root) noexcept;
// Session, client thread, every tick: the other skaters a camera could film.
struct Player {
    std::uint64_t id{};
    std::string name;
    multiplayer::Transform root;
};
void note_players(std::vector<Player> players) noexcept;

// Client thread, every tick, after the session placed the remote skaters (beside
// multiplayer::tick_native_party_actions). `ready`: a local skater is in control; `camera_phase`:
// the native camera may be changed now (camera_observer.h).
void tick(std::uintptr_t base, std::uintptr_t client, bool ready, bool camera_phase) noexcept;
// Whether the camcorder holds the screen (it yields to Spectate).
bool owns_camera() noexcept;
// Whether the camcorder wants the pad kept from the skater (operating, or setting up a tripod).
// The client tick ORs it into the freecam's input capture, which it sets every tick.
bool wants_controller() noexcept;

// Console, client thread: one `camcorder` command; the line to print.
std::string command(std::string_view verb, const std::vector<std::string> &words);

// Overlay, render thread: what to draw over the game this frame.
struct Published {
    camcorder::Frame frame;
    bool clean_feed{};
    bool operating{};     // `camcorder operate`: the pad works the camera
    bool aiming{};        // `camcorder aim`: the camera aims while the player skates
    float blink{};        // seconds, for blinking readouts
    std::string hint;     // the viewfinder's control hints, empty for none
    std::vector<std::string> players; // who could be filmed (the menu's subject list)
    std::string subject;  // who is, empty for nobody
    bool dv{};            // `camcorder quality dv`: record at the VX1000's own 640 x 480, not HD
    camcorder::Sound sound{camcorder::Sound::game}; // `camcorder sound off|game|vx`
    std::uint32_t still{};        // counts `camcorder still` requests: each new number takes one
    std::uint32_t tape{1};        // the tape in the camcorder (each `camcorder tape new` is the next)
    std::uint32_t capture_tape{}; // counts `camcorder tape capture` requests
    camcorder::Finish finish;     // `camcorder look|dv|date` and the camera's own look, motion and stamp
    int own_lines{480};           // the camera's own recording height (quality dv)
    bool warp{true};              // `camcorder warp`: the true fisheye
    // The cameras and lenses to choose from (built in, and from mods: camcorder/profiles.h).
    struct Choice {
        std::string id, name, source; // source: the mod's name, empty when built in
    };
    std::vector<Choice> cameras, lenses;
    std::size_t mod_notes{};      // problems found in the mods' profiles (`camcorder mods`)
};
Published published() noexcept;
// Overlay, render thread: the back buffer's width over height (the fisheye warp's render needs it).
void note_render_aspect(float aspect) noexcept;
// Overlay, render thread: the fisheye warp cannot be drawn here; keep the flat stand-in.
void note_warp_failed() noexcept;
} // namespace dingosdk::camcorder_runtime
