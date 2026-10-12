#pragma once
// Where the camera is and where it points: held by a filmer (HandheldRig) or standing on a
// tripod (TripodRig). Both produce a camera matrix; the optics (vx1000.h) give it its FOV.
//
// Handheld, the camera rides on the local skater: it is the filmer. Low on a long grip with the
// fisheye is the classic follow shot, chasing a skater a board length behind; eye level is
// the long-lens shot from the side. With "lock on" the filmer keeps the subject (another skater)
// framed by themself, a little late like a person would, and the stick only nudges the
// framing, so the player can skate the line with both hands. Without it the stick aims.
//
// A tripod stays where it was put. Its fluid head can be locked off (a static shot, a skater
// rolling through), panned by hand, or set to follow a subject, panning the way a fluid head
// drags behind.
#include "camera_math.h"
#include <cstdint>
#include <optional>
#include <string_view>

namespace dingosdk::camcorder {
// The person holding the camera: the local skater, as the game places them.
struct Filmer {
    bool valid{};
    Vec3 position{};       // the skater's root (where the game puts it: not always at the feet)
    Vec3 forward{0, 0, 1}; // the way the body faces; only its flat part is used
    Vec3 velocity{};       // metres a second
    bool on_board{true};
    // The height of the surface under the filmer, when the game found it: the handheld camera's
    // heights are from the feet, and the root can be well above them.
    std::optional<float> ground;
};
// Who is being filmed: their root and velocity.
struct Subject {
    bool valid{};
    Vec3 position{};
    Vec3 velocity{};
};
// Operator controls, already freed of deadzones, each in [-1, 1] (look: stick deflection).
struct OperatorInput {
    float look_x{}, look_y{};     // right, up
    float move_x{}, move_y{};     // right, forward (tripod setup only)
    float raise{};                // up: a tripod's head being set up, or the handheld camera
    float zoom{};                 // -1 wide, 1 tele
    bool fast{};                  // tripod setup: move quickly
};
struct Shot {
    bool valid{};
    Matrix camera{};
};

// Smooth, repeatable hand shake: a person breathing and holding a 1.5 kg camera, the board's
// wheels on the ground, footsteps. Small angles; at tele they fill the frame, as they did.
class HandShake {
public:
    struct Offset {
        float yaw{}, pitch{}, roll{}; // radians
        Vec3 position{};              // metres
    };
    explicit HandShake(std::uint32_t seed = 1) noexcept;
    // `steadiness` 0 is a nervous first-timer, 1 a seasoned filmer.
    Offset step(float dt, float speed, bool on_board, float steadiness) noexcept;

private:
    float time_{}, step_phase_{};
    float phases_[12]{};
};

enum class Grip : std::uint8_t {
    long_grip, // low, held out on a long handle: the fisheye follow shot
    hip,       // waist height, the camera hanging from the hand
    eye_level, // through the viewfinder: the long-lens shot
};
std::string_view grip_name(Grip) noexcept;
// The lens's height above the feet for a grip, standing (on a board the filmer crouches a little).
float grip_height(Grip, bool on_board) noexcept;

class HandheldRig {
public:
    struct Settings {
        Grip grip = Grip::long_grip;
        bool lock_on = true;          // keep the subject framed; the stick nudges
        float operator_reaction = 1.6f; // Hz: how fast the filmer swings onto the subject
        float look_speed = 2.2f;      // radians a second at full stick, at wide
        float aim_height = 0.8f;      // metres above the subject's feet to frame
        float lead = 0.12f;           // seconds: aim where the subject is going
        float steadiness = 0.5f;      // HandShake
        float max_nudge = 25 * degrees;
    };
    HandheldRig() = default;
    explicit HandheldRig(Settings settings) : settings_(settings) {}

    // `zoom_ratio` slows the stick at tele, as a careful operator would.
    Shot step(const Filmer &, const Subject *, const OperatorInput &, float dt, float zoom_ratio);
    // Where the camera is held, for a filmer standing at `filmer`.
    Vec3 grip_position(const Filmer &) const;
    // Raising or lowering the camera from its grip's height (LB / RB, `camcorder height`), in
    // metres; the lens stays between `lowest` and `highest` above the feet.
    static constexpr float lowest = 0.12f, highest = 2.3f;
    void set_lift(float metres) { lift_ = std::isfinite(metres) ? std::clamp(metres, -3.0f, 3.0f) : 0.0f; }
    float lift() const { return lift_; }
    // The lens's height above the feet now (the grip's, plus the lift, within lowest to highest).
    float lens_height(bool on_board) const;
    YawPitch aim() const { return {yaw_.value, pitch_.value}; }
    // Points the camera this way in the world now; from then on, without a subject, it keeps that
    // angle to the filmer's heading and turns with them.
    void face(YawPitch aim);
    Settings &settings() { return settings_; }
    const Settings &settings() const { return settings_; }

private:
    Settings settings_;
    Spring yaw_, pitch_;
    // Free aim (no subject): yaw relative to the filmer's heading, so the camera turns with them as
    // a held camera does; pitch absolute.
    float offset_yaw_{}, free_pitch_{};
    float lift_{};
    // How far the root sits above the feet, measured from the ground under the filmer (smoothed;
    // not while airborne). 0 until the game has found the ground.
    float root_above_feet_{};
    bool measured_{};
    bool offset_pending_{}; // face() was given a world yaw: turn it into an offset on the next step
    float nudge_yaw_{}, nudge_pitch_{};
    float roll_{}, last_yaw_{};
    bool started_{};
    HandShake shake_{0x5eed};
};

class TripodRig {
public:
    enum class Head : std::uint8_t {
        locked, // a locked-off shot: nothing moves
        manual, // the operator pans and tilts, against the fluid head's drag
        follow, // pans and tilts after the subject
    };
    struct Settings {
        float height = 1.1f;          // metres from the feet to the lens; 0.15 is a "ground pod"
        Head head = Head::follow;
        float drag = 0.35f;           // seconds the fluid head takes to answer the handle
        float pan_speed = 1.4f;       // radians a second at full handle, at wide
        float follow_reaction = 1.1f; // Hz
        float aim_height = 0.8f;
    };
    static constexpr float lowest = 0.15f, highest = 2.0f;

    TripodRig() = default;
    explicit TripodRig(Settings settings) : settings_(settings) {}

    // Stands the tripod at `feet`, its head at the given aim.
    void place(Vec3 feet, YawPitch aim);
    bool placed() const { return placed_; }
    Vec3 feet() const { return feet_; }
    Vec3 lens() const { return add(feet_, {0, settings_.height, 0}); }
    YawPitch aim() const { return {yaw_.value, pitch_.value}; }

    // Setting up: the input carries the tripod over the ground (relative to where it faces),
    // raises and lowers the head, and aims it with no drag. `ground` is the height of the surface
    // under the new spot, when the game could find it; without it the tripod keeps its height.
    void setup(const OperatorInput &, std::optional<float> ground, float dt, float zoom_ratio);
    // Filming: the shot this tick.
    Shot step(const Subject *, const OperatorInput &, float dt, float zoom_ratio);
    Settings &settings() { return settings_; }
    const Settings &settings() const { return settings_; }

private:
    Settings settings_;
    bool placed_{};
    Vec3 feet_{};
    Spring yaw_, pitch_;
    float yaw_rate_{}, pitch_rate_{};
};
} // namespace dingosdk::camcorder
