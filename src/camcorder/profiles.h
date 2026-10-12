#pragma once
// Cameras and lenses as data, so mods can add them. A mod is a folder in ReSkate's Mods, as any
// other mod (installed and shared from the mod browser); the camcorder reads
//
//   Mods/<mod>/camcorders/<id>.camera.json   a camera body: its lens, motion, size, tape, look, stamp
//   Mods/<mod>/camcorders/<id>.lens.json     a lens for the front of any camera: a fisheye or a converter
//
// The id is the file name's start ("vx2000.camera.json" is "vx2000"). A mod's profile with the id
// of a built-in one, or of one in a mod lower in the load order, replaces it. Every field is
// optional: what a file leaves out is the VX1000's (or the Century's), so a camera can be a
// name and the two numbers that differ. Values out of range are brought into range, and what was
// changed or ignored is noted for `camcorder mods`. docs/MODDING.md has every field.
//
// Data only: nothing in a profile runs, as nothing in any ReSkate mod does.
#include "json.h"
#include "look.h"
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
    std::string source;              // the mod folder it came from; empty when built in
    bool operator==(const CameraProfile &) const = default;
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
// The same as JSON, every field written out: a starting point for a mod (`camcorder export`).
std::string write_camera(const CameraProfile &);
std::string write_adapter(const Adapter &);

enum class ProfileKind : std::uint8_t { camera, lens };
// "vx2000.camera.json" is camera "vx2000"; nothing for any other name. Ids are 1 to 32 of a-z,
// 0-9, '-' and '_' (lowercase, so they type easily in the console).
std::optional<std::pair<ProfileKind, std::string>> profile_file(std::string_view file_name);
bool valid_id(std::string_view id);

// The cameras and lenses to choose from: the built-in ones, then the mods'.
class Catalogue {
public:
    Catalogue(); // the VX1000 and the Century fisheye
    // Adds a profile, or replaces the one with its id.
    void add(CameraProfile profile);
    void add(Adapter profile);
    const std::vector<CameraProfile> &cameras() const { return cameras_; }
    const std::vector<Adapter> &adapters() const { return adapters_; }
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
};
} // namespace dingosdk::camcorder
