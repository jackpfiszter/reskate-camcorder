#include "commands.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace dingosdk::camcorder {
namespace {
bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
// on/off/toggle (and 1/0), applied to `current`. Nothing for anything else.
std::optional<bool> switch_word(const std::vector<std::string> &words, std::size_t at, bool current) {
    if (words.size() <= at || same(words[at], "toggle")) return !current;
    if (same(words[at], "on") || words[at] == "1") return true;
    if (same(words[at], "off") || words[at] == "0") return false;
    return std::nullopt;
}
std::optional<int> slot_word(const std::string &word) {
    int value{};
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (error != std::errc{} || end != word.data() + word.size() || value < 1 || value > static_cast<int>(tripod_slots))
        return std::nullopt;
    return value - 1;
}
std::string_view head_word(TripodRig::Head head) {
    switch (head) {
    case TripodRig::Head::locked: return "locked";
    case TripodRig::Head::manual: return "manual";
    case TripodRig::Head::follow: return "follow";
    }
    return "follow";
}
std::string after(Camcorder &cam) { return describe(cam.status()); }
} // namespace

const std::vector<std::string_view> &command_verbs() {
    static const std::vector<std::string_view> verbs{"status", "power", "rec", "stop", "mode", "camera", "lens", "rig",
                                                     "grip", "lockon", "tripod", "head", "height", "tape"};
    return verbs;
}

std::string describe(const Status &s) {
    if (!s.powered) return "Camcorder off.";
    std::string line = s.transport == Transport::recording ? "REC " : "STBY ";
    line += format_counter(s.counter_seconds);
    line += s.mode == Mode::tripod ? ", tripod " + std::to_string(s.active_tripod + 1) + " (" +
                                         std::string(head_word(s.head)) + (s.tripod_setup ? ", setup" : "") + ")"
                                   : ", handheld (" + (s.grip_id == long_grip().id ? std::string() : s.grip_name + ", ") +
                                         (s.lock_on ? "lock on" : "free") + ")";
    line += ", " + s.camera_name + ", " + s.lens_name;
    char zoom[32];
    std::snprintf(zoom, sizeof(zoom), ", zoom %.1fx", static_cast<double>(s.zoom_ratio));
    line += zoom;
    // Rounded up, as the viewfinder shows it: a fresh 60-minute tape reads 60.
    line += ", " + std::to_string(static_cast<int>(std::ceil(s.tape_remaining / 60))) + " min of tape left";
    return line + ".";
}

std::string run_command(Camcorder &cam, std::string_view verb, const std::vector<std::string> &words,
                        const CommandContext &context) {
    const auto first = words.empty() ? std::string_view{} : std::string_view{words[0]};
    if (verb.empty() || same(verb, "status")) return after(cam);
    if (same(verb, "power")) {
        const auto on = switch_word(words, 0, cam.powered());
        if (!on) return "Usage: camcorder power [on|off|toggle]";
        cam.power(*on);
        return after(cam);
    }
    if (same(verb, "rec")) {
        cam.toggle_record();
        return cam.status().message;
    }
    if (same(verb, "stop")) {
        if (cam.deck().recording()) cam.toggle_record();
        return after(cam);
    }
    if (same(verb, "mode")) {
        Mode mode{};
        if (same(first, "handheld")) mode = Mode::handheld;
        else if (same(first, "tripod")) mode = Mode::tripod;
        else if (first.empty() || same(first, "toggle"))
            mode = cam.status().mode == Mode::handheld ? Mode::tripod : Mode::handheld;
        else return "Usage: camcorder mode handheld|tripod|toggle";
        if (!cam.powered()) cam.power(true);
        cam.set_mode(mode);
        return cam.status().mode == mode ? after(cam) : cam.status().message;
    }
    static const Catalogue built_in;
    const auto &catalogue = context.catalogue ? *context.catalogue : built_in;
    if (same(verb, "camera")) {
        if (same(first, "list")) {
            std::string list;
            for (const auto &c : catalogue.cameras())
                list += (list.empty() ? "" : "\n") + c.id + "  " + c.name + (c.source.empty() ? "" : "  (" + c.source + ")") +
                        (c.id == cam.settings().camera.id ? "  <- this one" : "");
            return list;
        }
        const CameraProfile *camera = first.empty() || same(first, "next") ? &catalogue.next_camera(cam.settings().camera.id)
                                                                           : catalogue.camera(first);
        if (!camera) return "No camera called " + std::string(first) + " (camcorder camera list).";
        cam.set_camera(*camera);
        return camera->name + ".";
    }
    if (same(verb, "lens")) {
        if (same(first, "list")) {
            const auto &on = cam.settings().adapter;
            std::string list = std::string("stock  the camera's own lens") + (on ? "" : "  <- this one");
            for (const auto &a : catalogue.adapters())
                list += "\n" + a.id + "  " + a.name + (a.source.empty() ? "" : "  (" + a.source + ")") +
                        (on && on->id == a.id ? "  <- this one" : "");
            return list;
        }
        const Adapter *adapter = nullptr;
        const std::string current = cam.settings().adapter ? cam.settings().adapter->id : std::string();
        if (same(first, "stock") || same(first, "none")) adapter = nullptr;
        else if (same(first, "fisheye")) {
            adapter = catalogue.fisheye();
            if (!adapter) return "No fisheye lens to put on.";
        } else if (first.empty() || same(first, "next") || same(first, "toggle")) adapter = catalogue.next_adapter(current);
        else {
            adapter = catalogue.adapter(first);
            if (!adapter) return "No lens called " + std::string(first) + " (camcorder lens list).";
        }
        cam.set_adapter(adapter ? std::optional<Adapter>(*adapter) : std::nullopt);
        return adapter ? adapter->name + "." : std::string("The camera's own lens.");
    }
    // A height of the hand (`grip hip` before rigs: the settings files of older versions say it).
    const auto hand_height = [&](std::string_view word) -> const GripProfile * {
        return same(word, "low") ? &long_grip() : same(word, "hip") ? &hip_grip() : same(word, "eye") ? &eye_grip() : nullptr;
    };
    const auto hold_at = [&](const GripProfile &height) {
        const auto &rig = cam.handheld();
        const float base = rig.settings().grip.height_on(true);
        cam.set_handheld_lift(std::clamp(height.height_on(true), rig.settings().lowest, std::max(rig.settings().highest, rig.settings().lowest)) - base);
    };
    if (same(verb, "grip") || same(verb, "rig")) {
        const auto current = cam.handheld().settings().grip.id;
        if (same(first, "list")) {
            std::string list;
            for (const auto &g : catalogue.grips())
                list += (list.empty() ? "" : "\n") + g.id + "  " + g.name + (g.source.empty() ? "" : "  (" + g.source + ")") +
                        (g.id == current ? "  <- this one" : "");
            return list;
        }
        std::string id(first);
        for (auto &c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const GripProfile *grip = first.empty() || same(first, "next") ? &catalogue.next_grip(current) : catalogue.grip(id);
        if (!grip && hand_height(id)) {
            cam.set_grip(*catalogue.grip(long_grip().id));
            hold_at(*hand_height(id));
            return "Handheld, at " + std::string(same(id, "low") ? "a skate's height" : same(id, "hip") ? "the hip" : "eye level") + ".";
        }
        if (!grip) return "No rig called " + std::string(first) + " (camcorder rig list).";
        cam.set_grip(*grip);
        return grip->name + ".";
    }
    if (same(verb, "lockon")) {
        const auto on = switch_word(words, 0, cam.status().lock_on);
        if (!on) return "Usage: camcorder lockon [on|off|toggle]";
        cam.set_lock_on(*on);
        return *on ? "Lock on: the filmer keeps the subject framed." : "Lock on off: the stick aims.";
    }
    if (same(verb, "tripod")) {
        // The tripod key twice: set one up, then it is done (or REC: done, and rolling).
        if (same(first, "done") || (same(first, "place") && cam.status().tripod_setup)) {
            if (!cam.status().tripod_setup) return "No tripod being set up.";
            cam.set_tripod_setup(false);
            return "Tripod " + std::to_string(cam.status().active_tripod + 1) + " is set: REC rolls it, then skate.";
        }
        if (same(first, "place")) {
            const int slot = cam.place_tripod(context.filmer);
            return slot < 0 ? cam.status().message : cam.status().message + " Move and aim it, then press the tripod key again (or REC).";
        }
        if (first.empty() || same(first, "next")) {
            cam.next_tripod();
            return cam.status().message;
        }
        if (same(first, "setup")) {
            const auto on = switch_word(words, 1, cam.status().tripod_setup);
            if (!on) return "Usage: camcorder tripod setup [on|off|toggle]";
            cam.set_tripod_setup(*on);
            return cam.status().tripod_setup == *on ? after(cam) : cam.status().message;
        }
        if (same(first, "remove")) {
            const auto slot = words.size() > 1 ? slot_word(words[1]) : std::optional<int>(cam.status().active_tripod);
            if (!slot || *slot < 0 || !cam.tripod(*slot)) return "No tripod in that slot.";
            cam.remove_tripod(*slot);
            return cam.status().message;
        }
        if (const auto slot = slot_word(std::string(first))) {
            cam.select_tripod(*slot);
            return cam.status().message;
        }
        return "Usage: camcorder tripod place|done|next|setup [on|off]|remove [1-4]|<1-4>";
    }
    if (same(verb, "head")) {
        TripodRig::Head head{};
        if (same(first, "locked")) head = TripodRig::Head::locked;
        else if (same(first, "manual")) head = TripodRig::Head::manual;
        else if (same(first, "follow")) head = TripodRig::Head::follow;
        else if (first.empty() || same(first, "next"))
            head = static_cast<TripodRig::Head>((static_cast<int>(cam.status().head) + 1) % 3);
        else return "Usage: camcorder head locked|manual|follow|next";
        cam.set_head(head);
        return "Tripod head: " + std::string(head_word(head)) + ".";
    }
    if (same(verb, "height")) {
        // A step a press: bound to a key or a button, it moves the camera a hand's width.
        const float step = cam.handling().height_step;
        // The next of the three heights up from where the camera is now (round to low).
        std::string_view word = first;
        if (same(first, "next") && cam.status().mode != Mode::tripod) {
            // Compared with where each height really puts it (a mod's handling can cap them).
            const auto &held = cam.handheld().settings();
            const float lowest = held.lowest, highest = std::max(held.highest, held.lowest);
            const auto at = [&](const GripProfile &g) { return std::clamp(g.height_on(true), lowest, highest); };
            const float now = cam.handheld().lens_height(true);
            word = now < at(hip_grip()) - 0.05f ? "hip" : now < at(eye_grip()) - 0.05f ? "eye" : "low";
        }
        if (const auto *preset = hand_height(word); preset && cam.status().mode != Mode::tripod) {
            hold_at(*preset);
            char held[64];
            std::snprintf(held, sizeof(held), "Camera held %.2f m up.", static_cast<double>(cam.handheld().lens_height(true)));
            return held;
        }
        const bool up = same(first, "up"), down = same(first, "down"), reset = same(first, "reset");
        float metres{};
        const auto [end, error] = std::from_chars(first.data(), first.data() + first.size(), metres);
        const bool number = !first.empty() && error == std::errc{} && end == first.data() + first.size() && std::isfinite(metres);
        if (!up && !down && !reset && !number) return "Usage: camcorder height up|down|low|hip|eye|next|reset|<metres>";
        char text[64];
        const auto status = cam.status();
        if (status.mode == Mode::tripod) {
            const int slot = status.active_tripod;
            if (slot < 0 || !cam.tripod(slot)) return "No tripod set up.";
            const float now = cam.tripod(slot)->settings().height;
            cam.set_tripod_height(slot, up ? now + step : down ? now - step : reset ? cam.handling().tripod_height : metres);
            std::snprintf(text, sizeof(text), "Tripod height %.2f m.", static_cast<double>(cam.tripod(slot)->settings().height));
            return text;
        }
        // From where the lens really is (a tall rig can sit above the highest), a step at a time.
        const auto &rig = cam.handheld();
        const float base = rig.settings().grip.height_on(true), now = rig.lens_height(true);
        const float target = up ? now + step : down ? now - step : reset ? base : metres;
        const float low = rig.settings().lowest, high = std::max(rig.settings().highest, low);
        cam.set_handheld_lift(std::clamp(target, low, high) - base);
        std::snprintf(text, sizeof(text), "Camera held %.2f m up.", static_cast<double>(cam.handheld().lens_height(true)));
        return text;
    }
    if (same(verb, "tape")) {
        if (same(first, "new")) {
            cam.new_tape();
            return "New tape.";
        }
        if (first.empty() || same(first, "clips")) {
            const auto &clips = cam.deck().clips();
            if (clips.empty()) return "Nothing on this tape yet.";
            std::string list;
            for (const auto &clip : clips) {
                char line[160];
                std::snprintf(line, sizeof(line), "%s#%u %s  %.1f s  %s", list.empty() ? "" : "\n", clip.number,
                              format_timecode(drop_frame_timecode(clip.start_frame)).c_str(), clip.seconds(),
                              clip.label.c_str());
                list += line;
            }
            return list;
        }
        return "Usage: camcorder tape new|clips";
    }
    return "Unknown camcorder command: " + std::string(verb) + ".";
}
} // namespace dingosdk::camcorder
