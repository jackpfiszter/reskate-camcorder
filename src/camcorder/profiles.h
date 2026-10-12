#pragma once
// Cameras and lenses as data, so mods can add them. A mod is a folder in ReSkate's Mods, as any
// other mod (installed and shared from the mod browser); the camcorder reads
//
//   Mods/<mod>/camcorders/<id>.camera.json   a camera body: its lens, motion, size, tape, look, stamp, mic
//   Mods/<mod>/camcorders/<id>.lens.json     a lens for the front of any camera: a fisheye or a converter
//   Mods/<mod>/camcorders/<id>.grip.json     a way of holding the camera: its height, reach, shake
//   Mods/<mod>/camcorders/handling.json      how it all feels: the operator, the shake, tripods, controls
//
// The id is the file name's start ("vx2000.camera.json" is "vx2000"). A mod's profile with the id
// of a built-in one, or of one in a mod lower in the load order, replaces it. Every field is
// optional: what a file leaves out is the VX1000's (or the Century's, or the long grip's), so a
// camera can be a name and the two numbers that differ. handling.json files are layered: each
// changes only the numbers it gives, a higher mod's over a lower one's. Values out of range are brought into range, and what was
// changed or ignored is noted for `camcorder mods`. docs/MODDING.md has every field.
//
// Data only: nothing in a profile runs, as nothing in any ReSkate mod does.
#include "json.h"
#include "look.h"
#include "rigs.h"
#include "vx1000.h"
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dingosdk::camcorder {
struct CameraProfile {
    std::string id{"vx1000"};
    std::string name{"Sony DCR-VX1000"};
    LensSpec lens;                   // its own lens and sensor
    Motion motion{Motion::ntsc_60i};
    int lines{480};                  // its own recording height (the width is 4:3 of it)
    double tape_minutes{60};         // one cassette
    LookProfile look;
    StampProfile stamp;
    MicProfile mic;                  // its own microphone (`camcorder sound mic`)
    std::string source;              // the mod folder it came from; empty when built in
    bool operator==(const CameraProfile &) const = default;
};

// How the camcorder feels: the numbers a handling.json changes. The defaults are as built.
struct Handling {
    // The person filming.
    float reaction{1.6f};    // Hz: how fast a filmer swings onto the subject
    float look_speed{2.2f};  // radians a second at full stick, at wide
    float aim_height{0.8f};  // metres above a subject's feet to frame (handheld and tripods)
    float lead{0.12f};       // seconds: aim where the subject is going
    float steadiness{0.5f};  // 0 a nervous first-timer, 1 a seasoned filmer
    float max_nudge{25};     // degrees the stick moves the framing off a locked-on subject
    float raise_speed{0.8f}; // metres a second the handheld camera rises and drops
    float lowest{0.12f}, highest{2.3f}; // the handheld lens above the feet
    float height_step{0.15f}; // metres a `camcorder height up|down` moves it
    // The arms the camera hangs from (the body's pushes, stops, turns and landings swing it).
    float arm_sway{2.5f}, arm_bob{3.5f}; // Hz: side to side and front to back; up and down
    float arm_damping{0.45f};            // under 1 it swings past and back
    float arm_tilt{0.8f};                // radians the frame tips a metre of swing
    float arm_reach{0.3f};               // metres it can swing at most
    // The camera's weight in the hands.
    float aim_swing{3.0f};   // Hz the camera follows where the hands aim
    float aim_damping{0.7f}; // under 1 a flick carries on a little past
    float hunting{1};        // a person's small corrections chasing a subject (0: none)
    HandShake::Amounts shake;
    // Tripods.
    float tripod_height{1.1f};    // a new tripod's
    float tripod_drag{0.35f};     // seconds the fluid head takes to answer the handle
    float tripod_pan_speed{1.4f}; // radians a second at full handle, at wide
    float tripod_follow{1.1f};    // Hz: how fast a following head swings after the subject
    // Controls.
    float mouse{0.0025f};         // radians of aim a pixel of mouse movement
    float wheel_notch{0.15f};     // seconds of zoom rocker a mouse wheel notch gives
    bool operator==(const Handling &) const = default;
};
// The built-in camera: the Sony DCR-VX1000 (vx1000.h has where the numbers come from).
const CameraProfile &vx1000();

// What reading a profile found: the profile, or nothing and why; and notes on what was ignored or
// brought into range.
template <class Profile> struct Reading {
    std::optional<Profile> profile;
    std::string error;
    std::vector<std::string> notes;
};
Reading<CameraProfile> read_camera(std::string_view json_text, std::string id);
Reading<Adapter> read_adapter(std::string_view json_text, std::string id);
Reading<GripProfile> read_grip(std::string_view json_text, std::string id);
// The numbers in the file over `base` (a handling.json changes only what it gives).
Reading<Handling> read_handling(std::string_view json_text, const Handling &base);
// The same as JSON, every field written out: a starting point for a mod (`camcorder export`).
std::string write_camera(const CameraProfile &);
std::string write_adapter(const Adapter &);
std::string write_grip(const GripProfile &);
std::string write_handling(const Handling &);

enum class ProfileKind : std::uint8_t { camera, lens, grip, handling };
// "vx2000.camera.json" is camera "vx2000", "pole.grip.json" grip "pole", "handling.json" the
// handling; nothing for any other name. Ids are 1 to 32 of a-z, 0-9, '-' and '_' (lowercase, so
// they type easily in the console).
std::optional<std::pair<ProfileKind, std::string>> profile_file(std::string_view file_name);
bool valid_id(std::string_view id);

// The cameras and lenses to choose from: the built-in ones, then the mods'.
class Catalogue {
public:
    Catalogue(); // the VX1000, the Century fisheye, the three grips, the handling as built
    // Adds a profile, or replaces the one with its id.
    void add(CameraProfile profile);
    void add(Adapter profile);
    void add(GripProfile profile);
    const std::vector<CameraProfile> &cameras() const { return cameras_; }
    const std::vector<Adapter> &adapters() const { return adapters_; }
    const std::vector<GripProfile> &grips() const { return grips_; }
    const GripProfile *grip(std::string_view id) const;
    // The grip after `id` (the first after the last, or when `id` is unknown).
    const GripProfile &next_grip(std::string_view id) const;
    // The handling, with every mod's handling.json layered on in load order.
    Handling handling;
    std::vector<std::string> handling_sources; // the mods that changed it
    const CameraProfile *camera(std::string_view id) const;
    const Adapter *adapter(std::string_view id) const;
    // The camera after `id` (the first after the last, or when `id` is unknown).
    const CameraProfile &next_camera(std::string_view id) const;
    // The lens after `id` in the order stock (empty id), then each adapter: null for stock.
    const Adapter *next_adapter(std::string_view id) const;
    // The first fisheye, for `camcorder lens fisheye`.
    const Adapter *fisheye() const;

private:
    std::vector<CameraProfile> cameras_;
    std::vector<Adapter> adapters_;
    std::vector<GripProfile> grips_;
};
} // namespace dingosdk::camcorder
