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
#include <array>
#include <cstdint>
#include <optional>
#include <span>
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
// Whether the camcorder wants the pad kept from the skater (setting up a tripod, the quick menu).
// The client tick ORs it into the freecam's input capture, which it sets every tick.
bool wants_controller() noexcept;

// Session, client thread: the REC light (camcorder/presence.h). True when a throwdown message was
// the camcorder's word from `sender` (then it is not a throwdown); what to send the others now, if
// anything; and what `player` last said: 0 no camcorder, 1 on, 2 recording.
bool receive_presence(std::uint64_t sender, std::span<const std::uint8_t> message) noexcept;
std::optional<std::vector<std::uint8_t>> presence_to_send() noexcept;
std::uint8_t presence_of(std::uint64_t player) noexcept;

// Console, client thread: one `camcorder` command; the line to print.
std::string command(std::string_view verb, const std::vector<std::string> &words);

// Overlay, render thread: what to draw over the game this frame.
struct Published {
    camcorder::Frame frame;
    bool clean_feed{};
    bool aiming{};        // `camcorder aim`: the player aims the camera (otherwise it points itself)
    int frame_on{1};      // `camcorder frame`: 0 board, 1 the whole skater, 2 head
    float blink{};        // seconds, for blinking readouts
    std::string hint;     // the viewfinder's control hints, empty for none
    std::vector<std::string> players; // who could be filmed (the menu's subject list)
    bool closest{};                   // filming whoever is nearest (subject: who that is now)
    bool light{};                     // the light on the camera (`camcorder light`)
    camcorder::ViewfinderLayout viewfinder = camcorder::vx1000_viewfinder(); // the camera's readouts
    std::string subject;  // who is, empty for nobody
    bool dv{};            // `camcorder quality dv`: record at the VX1000's own 640 x 480, not HD
    camcorder::Sound sound{camcorder::Sound::game}; // `camcorder sound off|game|vx`
    std::uint32_t still{};        // counts `camcorder still` requests: each new number takes one
    std::uint32_t tape{1};        // the tape in the camcorder (each `camcorder tape new` is the next)
    std::uint32_t capture_tape{}; // counts `camcorder tape capture` requests
    camcorder::Finish finish;     // `camcorder look|dv|date` and the camera's own look, motion and stamp
    camcorder::EffectStack effects; // the camera's picture (camcorder/effects.h): drawn on screen while the look is on
    int own_lines{480};           // the camera's own recording height (quality dv)
    bool warp{true};              // `camcorder warp`: the true fisheye
    // The cameras and lenses to choose from (built in, and from mods: camcorder/profiles.h).
    struct Choice {
        std::string id, name, source; // source: the mod's name, empty when built in
    };
    std::vector<Choice> cameras, lenses, grips;
    std::size_t mod_notes{};      // problems found in the mods' profiles (`camcorder mods`)
    // The quick menu (`camcorder quick`): its lines, label and value; quick_index -1 when closed.
    std::vector<std::pair<std::string, std::string>> quick;
    int quick_index{-1};
    bool guide{};                 // the first-use guide is over the viewfinder
    float sensitivity{1};
    bool invert{};
    std::uint32_t review{}, review_stop{}; // count `camcorder review` and `review stop` requests
};
Published published() noexcept;
// Any thread (the sound capture's): where the local skater is from the camera now, for the sound.
camcorder::EarPlacement ear() noexcept;
// Overlay, render thread: the back buffer's width over height (the fisheye warp's render needs it).
void note_render_aspect(float aspect) noexcept;
// Overlay, render thread: the fisheye warp cannot be drawn here; keep the flat stand-in.
void note_warp_failed() noexcept;
} // namespace dingosdk::camcorder_runtime
