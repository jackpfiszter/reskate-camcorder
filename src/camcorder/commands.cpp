#include "commands.h"
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
    static const std::vector<std::string_view> verbs{"status", "power", "rec", "stop", "mode", "lens", "grip",
                                                     "lockon", "tripod", "head", "height", "tape"};
    return verbs;
}

std::string describe(const Status &s) {
    if (!s.powered) return "Camcorder off.";
    std::string line = s.transport == Transport::recording ? "REC " : "STBY ";
    line += format_counter(s.counter_seconds);
    line += s.mode == Mode::tripod ? ", tripod " + std::to_string(s.active_tripod + 1) + " (" +
                                         std::string(head_word(s.head)) + (s.tripod_setup ? ", setup" : "") + ")"
                                   : ", handheld (" + std::string(grip_name(s.grip)) + (s.lock_on ? ", lock on" : "") + ")";
    line += ", " + std::string(adapter_name(s.adapter));
    char zoom[32];
    std::snprintf(zoom, sizeof(zoom), ", zoom %.1fx", static_cast<double>(focal_length(s.zoom) / vx1000_lens.wide_mm));
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
    if (same(verb, "lens")) {
        Adapter adapter{};
        if (same(first, "fisheye")) adapter = Adapter::century_fisheye;
        else if (same(first, "stock")) adapter = Adapter::none;
        else if (first.empty() || same(first, "toggle"))
            adapter = cam.settings().adapter == Adapter::none ? Adapter::century_fisheye : Adapter::none;
        else return "Usage: camcorder lens fisheye|stock|toggle";
        cam.set_adapter(adapter);
        return std::string(adapter_name(adapter)) + ".";
    }
    if (same(verb, "grip")) {
        Grip grip{};
        if (same(first, "long")) grip = Grip::long_grip;
        else if (same(first, "hip")) grip = Grip::hip;
        else if (same(first, "eye")) grip = Grip::eye_level;
        else if (first.empty() || same(first, "next"))
            grip = static_cast<Grip>((static_cast<int>(cam.status().grip) + 1) % 3);
        else return "Usage: camcorder grip long|hip|eye|next";
        cam.set_grip(grip);
        return std::string(grip_name(grip)) + ".";
    }
    if (same(verb, "lockon")) {
        const auto on = switch_word(words, 0, cam.status().lock_on);
        if (!on) return "Usage: camcorder lockon [on|off|toggle]";
        cam.set_lock_on(*on);
        return *on ? "Lock on: the filmer keeps the subject framed." : "Lock on off: the stick aims.";
    }
    if (same(verb, "tripod")) {
        if (same(first, "place")) {
            const int slot = cam.place_tripod(context.filmer);
            return slot < 0 ? cam.status().message : cam.status().message + " Setting it up: move and aim it, then REC.";
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
        return "Usage: camcorder tripod place|next|setup [on|off]|remove [1-4]|<1-4>";
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
        float metres{};
        const auto [end, error] = std::from_chars(first.data(), first.data() + first.size(), metres);
        if (first.empty() || error != std::errc{} || end != first.data() + first.size() || !std::isfinite(metres))
            return "Usage: camcorder height <metres>";
        const int slot = cam.status().active_tripod;
        if (slot < 0 || !cam.tripod(slot)) return "No tripod set up.";
        cam.set_tripod_height(slot, metres);
        char text[48];
        std::snprintf(text, sizeof(text), "Tripod height %.2f m.", static_cast<double>(cam.tripod(slot)->settings().height));
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
