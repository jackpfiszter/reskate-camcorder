#pragma once
// The camcorder as the player uses it: a VX1000 (vx1000.h) with a tape deck (tape.h), held by
// the player or stood on one of a few tripods (rigs.h). Each game tick it is given where the
// filmer and the subject are and the operator's controls, and it answers with the shot (a camera
// matrix and FOV for the game) and what the viewfinder shows.
//
// Nothing here touches the game: the ReSkate side (integration/reskate) reads the world, hands
// this the numbers and applies the shot through ReSkate's camera lease.
#include "profiles.h"
#include "rigs.h"
#include "tape.h"
#include "vx1000.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace dingosdk::camcorder {
enum class Mode : std::uint8_t {
    handheld, // the player is the filmer
    tripod,   // the active tripod's view
};
inline constexpr std::size_t tripod_slots = 4;

struct Settings {
    CameraProfile camera = vx1000();                    // the body: its lens, tape and look
    std::optional<Adapter> adapter = century_fisheye(); // on the front; none is the stock lens
    FisheyeRender fisheye_render = FisheyeRender::approximate;
    HandheldRig::Settings handheld; // the handheld rig's at construction (then set_grip, set_lock_on)
    TripodRig::Settings tripod;     // for tripods set up from now on
    Deck::Settings deck;            // the deck's at construction
};

// One tick's world and controls.
struct TickInput {
    float dt{};
    Filmer filmer;
    std::optional<Subject> subject;  // nobody to film: none
    OperatorInput controls;
    // The surface height under the active tripod's feet, when the game could find it (setup).
    std::optional<float> ground_under_tripod;
    float render_aspect = 16.0f / 9.0f; // the game's back buffer, width over height
};

// What the camcorder is doing, for the viewfinder, the menu and the console.
struct Status {
    bool powered{};
    Mode mode{Mode::handheld};
    bool tripod_setup{};
    int active_tripod{-1};                 // -1: none
    std::array<bool, tripod_slots> tripods{}; // which slots hold a tripod
    Transport transport{Transport::standby};
    double counter_seconds{};              // the viewfinder counter: the tape position
    double tape_remaining{};
    double battery{1};
    std::uint32_t clip{};                  // the clip being recorded or last recorded; 0 none
    float zoom{};                          // 0 wide, 1 tele
    float zoom_ratio{1};                   // 1x at wide
    bool zoom_bar{};                       // the zoom bar is up
    std::string camera_id, camera_name;    // the body
    std::string lens_id, lens_name;        // the adapter; an empty id is the stock lens
    bool fisheye{};
    Grip grip{};
    float handheld_height{};               // the handheld lens above the feet, metres
    bool lock_on{};
    TripodRig::Head head{};
    std::string message;                   // the last thing worth telling the player
};

// The tick's answer.
struct Frame {
    Shot shot;      // valid: put this camera on screen; otherwise give the game its camera back
    Optics optics;  // optics.render_vertical_fov is the game camera's FOV
    Status status;
};

class Camcorder {
public:
    Camcorder() = default;
    explicit Camcorder(Settings settings);

    // Power on: the shot takes over the screen. Power off stops any recording.
    void power(bool on);
    bool powered() const { return powered_; }
    bool tripod_setup() const { return tripod_setup_; }
    // REC / STBY, as the button under the thumb.
    void toggle_record();
    void set_mode(Mode);
    // A new body: its lens and its cassette's length from now on (the zoom keeps its place).
    void set_camera(const CameraProfile &camera);
    // A lens on the front, or none for the camera's own.
    void set_adapter(std::optional<Adapter> adapter) { settings_.adapter = std::move(adapter); }
    // A grip puts the camera back at that grip's height.
    void set_grip(Grip grip) {
        handheld_.settings().grip = grip;
        handheld_.set_lift(0);
    }
    // Raises or lowers the handheld camera from its grip's height.
    void set_handheld_lift(float metres) { handheld_.set_lift(metres); }
    // Points the handheld camera this way in the world (it then turns with the filmer).
    void aim_handheld(YawPitch aim) { handheld_.face(aim); }
    void set_lock_on(bool on) { handheld_.settings().lock_on = on; }
    void set_head(TripodRig::Head);

    // Stands a tripod a step ahead of the filmer, aimed as the handheld camera is, in the first free
    // slot (or the active one when all are used), makes it active and starts setting it up.
    int place_tripod(const Filmer &);
    void remove_tripod(int slot);
    // Cuts to the next tripod that stands (in tripod mode).
    void next_tripod();
    void select_tripod(int slot);
    // Setup routes the controls to carrying and aiming the active tripod.
    void set_tripod_setup(bool on);
    // Clamped to TripodRig::lowest..highest.
    void set_tripod_height(int slot, float metres);
    void new_tape() { deck_.new_tape(); }

    Frame tick(const TickInput &);

    const Settings &settings() const { return settings_; }
    Settings &settings() { return settings_; }
    const Deck &deck() const { return deck_; }
    Deck &deck() { return deck_; }
    const HandheldRig &handheld() const { return handheld_; }
    const TripodRig *tripod(int slot) const;
    Status status() const;

private:
    Settings settings_;
    bool powered_{};
    Mode mode_{Mode::handheld};
    bool tripod_setup_{};
    int active_tripod_{-1};
    std::array<std::optional<TripodRig>, tripod_slots> tripods_{};
    HandheldRig handheld_;
    ZoomServo zoom_;
    Deck deck_;
    std::string message_;
    std::string shot_label() const;
    TripodRig *active() { return active_tripod_ >= 0 && tripods_[active_tripod_] ? &*tripods_[active_tripod_] : nullptr; }
};
} // namespace dingosdk::camcorder
