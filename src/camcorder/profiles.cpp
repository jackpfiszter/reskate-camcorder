#include "profiles.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dingosdk::camcorder {
namespace {
using json::Value;
using Kind = Value::Kind;

// Reads one object's fields, noting what it cannot use. Each read takes the field's default and
// a range, and keeps the default when the field is missing.
struct Fields {
    const Value &object;
    std::string where; // "look." for nested objects, for the notes
    std::vector<std::string> &notes;
    std::vector<std::string> known{};

    const Value *get(std::string_view key, Kind kind, const char *expected) {
        known.emplace_back(key);
        const auto *v = object.find(key);
        if (!v) return nullptr;
        if (!v->is(kind)) {
            notes.push_back(where + std::string(key) + " should be " + expected + "; the default is kept.");
            return nullptr;
        }
        return v;
    }
    float clamp_note(std::string_view key, double value, double low, double high) {
        const double kept = std::clamp(value, low, high);
        if (kept != value) {
            char text[160];
            std::snprintf(text, sizeof(text), "%s%.*s is %g; it must be %g to %g, so it is %g.", where.c_str(),
                          static_cast<int>(key.size()), key.data(), value, low, high, kept);
            notes.emplace_back(text);
        }
        return static_cast<float>(kept);
    }
    void number(std::string_view key, float &out, double low, double high) {
        if (const auto *v = get(key, Kind::number, "a number")) out = clamp_note(key, v->number, low, high);
    }
    void number(std::string_view key, double &out, double low, double high) {
        if (const auto *v = get(key, Kind::number, "a number")) out = clamp_note(key, v->number, low, high);
    }
    void whole(std::string_view key, int &out, int low, int high) {
        if (const auto *v = get(key, Kind::number, "a number"))
            out = static_cast<int>(std::lround(clamp_note(key, v->number, low, high)));
    }
    void boolean(std::string_view key, bool &out) {
        if (const auto *v = get(key, Kind::boolean, "true or false")) out = v->boolean;
    }
    void text(std::string_view key, std::string &out, std::size_t longest) {
        if (const auto *v = get(key, Kind::string, "text")) {
            std::string kept;
            for (const unsigned char c : v->text)
                if (c >= 0x20 && c != 0x7f) kept += static_cast<char>(c); // no control characters
            if (kept.size() > longest) {
                notes.push_back(where + std::string(key) + " is cut to " + std::to_string(longest) + " characters.");
                kept.resize(longest);
            }
            if (!kept.empty()) out = kept;
        }
    }
    // Two numbers, [a, b], each in its range.
    bool pair(std::string_view key, float &a, float &b, double low_a, double high_a, double low_b, double high_b) {
        const auto *v = get(key, Kind::array, "two numbers, [a, b]");
        if (!v) return false;
        if (v->items.size() != 2 || !v->items[0].is(Kind::number) || !v->items[1].is(Kind::number)) {
            notes.push_back(where + std::string(key) + " should be two numbers, [a, b]; the default is kept.");
            return false;
        }
        a = clamp_note(std::string(key) + "[0]", v->items[0].number, low_a, high_a);
        b = clamp_note(std::string(key) + "[1]", v->items[1].number, low_b, high_b);
        return true;
    }
    // Words from a fixed list.
    template <class Enum> void word(std::string_view key, Enum &out, std::initializer_list<std::pair<const char *, Enum>> words) {
        const auto *v = get(key, Kind::string, "text");
        if (!v) return;
        std::string allowed;
        for (const auto &[name, value] : words) {
            if (v->text == name) {
                out = value;
                return;
            }
            allowed += (allowed.empty() ? "" : ", ") + std::string(name);
        }
        notes.push_back(where + std::string(key) + " \"" + v->text + "\" is not one of " + allowed + "; the default is kept.");
    }
    // Anything in the object not read is noted: most likely a misspelling.
    void unknown() {
        for (const auto &[name, value] : object.members)
            if (std::find(known.begin(), known.end(), name) == known.end())
                notes.push_back(where + name + " is not a field the camcorder knows; it is ignored.");
    }
};

std::string number_text(double v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", v);
    return text;
}
std::string quoted(const std::string &s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}
const char *corner_word(StampProfile::Corner c) { return c == StampProfile::Corner::bottom_left ? "bottom-left" : "bottom-right"; }
const char *lines_word(StampProfile::Lines l) {
    return l == StampProfile::Lines::date_only ? "date" : l == StampProfile::Lines::date_over_time ? "date-time" : "time-date";
}
const char *date_word(StampProfile::Date d) { return d == StampProfile::Date::day_month_year ? "day-month-year" : "month-day-year"; }

template <class Profile> std::optional<Value> root(std::string_view text, Reading<Profile> &reading) {
    std::string error;
    auto value = json::parse(text, error);
    if (!value) {
        reading.error = "Not valid JSON: " + error;
        return std::nullopt;
    }
    if (!value->is(Kind::object)) {
        reading.error = "The file should be one JSON object, { ... }.";
        return std::nullopt;
    }
    return value;
}
} // namespace

const CameraProfile &vx1000() {
    static const CameraProfile camera{};
    return camera;
}

Reading<CameraProfile> read_camera(std::string_view text, std::string id) {
    Reading<CameraProfile> reading;
    if (!valid_id(id)) {
        reading.error = "\"" + id + "\" cannot be an id: use 1 to 32 of a-z, 0-9, - and _.";
        return reading;
    }
    const auto value = root(text, reading);
    if (!value) return reading;
    CameraProfile camera = vx1000();
    camera.id = std::move(id);
    camera.name = camera.id;
    Fields f{*value, "", reading.notes};
    f.text("name", camera.name, 48);
    f.pair("sensor_mm", camera.lens.sensor_width_mm, camera.lens.sensor_height_mm, 1, 40, 1, 40);
    if (f.pair("zoom_mm", camera.lens.wide_mm, camera.lens.tele_mm, 1, 200, 1, 2000) && camera.lens.tele_mm < camera.lens.wide_mm) {
        reading.notes.emplace_back("zoom_mm: tele is shorter than wide; the lens is fixed at wide.");
        camera.lens.tele_mm = camera.lens.wide_mm;
    }
    if (camera.lens.tele_mm > camera.lens.wide_mm * 50) {
        reading.notes.emplace_back("zoom_mm: more than a 50x zoom; tele is brought in to 50x.");
        camera.lens.tele_mm = camera.lens.wide_mm * 50;
    }
    if (f.pair("zoom_seconds", camera.lens.fastest_zoom_s, camera.lens.slowest_zoom_s, 0.5, 60, 0.5, 120) &&
        camera.lens.slowest_zoom_s < camera.lens.fastest_zoom_s)
        std::swap(camera.lens.fastest_zoom_s, camera.lens.slowest_zoom_s);
    f.word("motion", camera.motion, {{"60i", Motion::ntsc_60i}, {"50i", Motion::pal_50i}, {"30p", Motion::p30},
                                     {"25p", Motion::p25}, {"24p", Motion::p24}, {"18p", Motion::p18}});
    f.whole("lines", camera.lines, 120, 1080);
    camera.lines &= ~1;
    f.number("tape_minutes", camera.tape_minutes, 1, 600);
    if (const auto *look = f.get("look", Kind::object, "an object, { ... }")) {
        Fields l{*look, "look.", reading.notes};
        auto &p = camera.look;
        l.text("format", p.format, 24);
        l.number("black_crush", p.black_crush, 0, 0.3);
        l.number("highlight_gain", p.highlight_gain, 0.5, 2);
        l.number("saturation", p.saturation, 0, 2);
        l.number("white_balance_drift", p.white_balance_drift, 0, 0.2);
        l.number("sharpen", p.sharpen, 0, 2);
        l.number("grain", p.grain, 0, 1);
        l.number("chroma_width", p.chroma_width, 0, 16);
        l.number("softness", p.softness, 0, 8);
        l.unknown();
    }
    if (const auto *stamp = f.get("stamp", Kind::object, "an object, { ... }")) {
        Fields s{*stamp, "stamp.", reading.notes};
        auto &p = camera.stamp;
        s.word("corner", p.corner, {{"bottom-right", StampProfile::Corner::bottom_right}, {"bottom-left", StampProfile::Corner::bottom_left}});
        s.word("lines", p.lines, {{"time-date", StampProfile::Lines::time_over_date}, {"date", StampProfile::Lines::date_only},
                                  {"date-time", StampProfile::Lines::date_over_time}});
        s.word("date", p.date, {{"month-day-year", StampProfile::Date::month_day_year}, {"day-month-year", StampProfile::Date::day_month_year}});
        if (const auto *colour = s.get("colour", Kind::array, "three numbers, [red, green, blue]")) {
            if (colour->items.size() == 3 && std::all_of(colour->items.begin(), colour->items.end(), [](const Value &c) { return c.is(Kind::number); }))
                for (std::size_t i = 0; i < 3; ++i)
                    p.colour[i] = static_cast<std::uint8_t>(std::lround(s.clamp_note("colour", colour->items[i].number, 0, 255)));
            else
                reading.notes.emplace_back("stamp.colour should be three numbers, [red, green, blue]; the default is kept.");
        }
        s.unknown();
    }
    if (const auto *mic = f.get("mic", Kind::object, "an object, { ... }")) {
        Fields m{*mic, "mic.", reading.notes};
        auto &p = camera.mic;
        m.number("high_cut", p.high_cut, 10, 2000);
        m.number("low_cut", p.low_cut, 1000, 23000);
        m.number("target", p.target, 0.02, 0.9);
        m.number("most_gain", p.most_gain, 0, 10);
        m.number("attack", p.attack, 0.001, 0.5);
        m.number("release", p.release, 0.02, 5);
        m.number("drive", p.drive, 0.1, 5);
        m.unknown();
    }
    f.unknown();
    reading.profile = std::move(camera);
    return reading;
}

Reading<GripProfile> read_grip(std::string_view text, std::string id) {
    Reading<GripProfile> reading;
    if (!valid_id(id)) {
        reading.error = "\"" + id + "\" cannot be an id: use 1 to 32 of a-z, 0-9, - and _.";
        return reading;
    }
    const auto value = root(text, reading);
    if (!value) return reading;
    GripProfile grip = long_grip();
    grip.id = std::move(id);
    grip.name = grip.id;
    Fields f{*value, "", reading.notes};
    f.text("name", grip.name, 48);
    f.number("height", grip.height, 0.05, 3);
    f.number("side", grip.side, -1, 1);
    f.number("ahead", grip.ahead, -1, 2);
    f.number("pitch", grip.pitch, -60, 60);
    f.number("crouch", grip.crouch, 0.5, 1.2);
    f.number("shake", grip.shake, 0, 4);
    f.unknown();
    reading.profile = std::move(grip);
    return reading;
}

Reading<Handling> read_handling(std::string_view text, const Handling &base) {
    Reading<Handling> reading;
    const auto value = root(text, reading);
    if (!value) return reading;
    Handling h = base;
    Fields f{*value, "", reading.notes};
    if (const auto *filmer = f.get("filmer", Kind::object, "an object, { ... }")) {
        Fields o{*filmer, "filmer.", reading.notes};
        o.number("reaction", h.reaction, 0.2, 10);
        o.number("look_speed", h.look_speed, 0.2, 10);
        o.number("aim_height", h.aim_height, 0, 2);
        o.number("lead", h.lead, 0, 1);
        o.number("steadiness", h.steadiness, 0, 1);
        o.number("max_nudge", h.max_nudge, 0, 60);
        o.number("raise_speed", h.raise_speed, 0.05, 5);
        o.number("lowest", h.lowest, 0.02, 1.5);
        o.number("highest", h.highest, 0.5, 4);
        o.number("height_step", h.height_step, 0.01, 1);
        o.unknown();
        if (h.highest < h.lowest) {
            reading.notes.emplace_back("filmer.highest is under filmer.lowest; it is raised to it.");
            h.highest = h.lowest;
        }
    }
    if (const auto *arms = f.get("arms", Kind::object, "an object, { ... }")) {
        Fields a{*arms, "arms.", reading.notes};
        a.number("sway", h.arm_sway, 0.3, 15);
        a.number("bob", h.arm_bob, 0.3, 15);
        a.number("damping", h.arm_damping, 0.1, 2);
        a.number("tilt", h.arm_tilt, 0, 4);
        a.number("reach", h.arm_reach, 0, 1);
        a.unknown();
    }
    if (const auto *aim = f.get("aim", Kind::object, "an object, { ... }")) {
        Fields a{*aim, "aim.", reading.notes};
        a.number("swing", h.aim_swing, 0.3, 15);
        a.number("damping", h.aim_damping, 0.1, 2);
        a.number("hunting", h.hunting, 0, 5);
        a.unknown();
    }
    if (const auto *shake = f.get("shake", Kind::object, "an object, { ... }")) {
        Fields s{*shake, "shake.", reading.notes};
        s.number("hands", h.shake.hands, 0, 5);
        s.number("wheels", h.shake.wheels, 0, 5);
        s.number("footsteps", h.shake.footsteps, 0, 5);
        s.unknown();
    }
    if (const auto *tripod = f.get("tripod", Kind::object, "an object, { ... }")) {
        Fields t{*tripod, "tripod.", reading.notes};
        t.number("height", h.tripod_height, TripodRig::lowest, TripodRig::highest);
        t.number("drag", h.tripod_drag, 0.02, 3);
        t.number("pan_speed", h.tripod_pan_speed, 0.1, 10);
        t.number("follow", h.tripod_follow, 0.1, 10);
        t.unknown();
    }
    if (const auto *controls = f.get("controls", Kind::object, "an object, { ... }")) {
        Fields c{*controls, "controls.", reading.notes};
        c.number("mouse", h.mouse, 0.0002, 0.02);
        c.number("wheel_notch", h.wheel_notch, 0.02, 1);
        c.unknown();
    }
    f.unknown();
    reading.profile = h;
    return reading;
}

Reading<Adapter> read_adapter(std::string_view text, std::string id) {
    Reading<Adapter> reading;
    if (!valid_id(id)) {
        reading.error = "\"" + id + "\" cannot be an id: use 1 to 32 of a-z, 0-9, - and _.";
        return reading;
    }
    const auto value = root(text, reading);
    if (!value) return reading;
    Adapter lens = century_fisheye();
    lens.id = std::move(id);
    lens.name = lens.id;
    Fields f{*value, "", reading.notes};
    f.text("name", lens.name, 48);
    f.boolean("fisheye", lens.fisheye);
    // A flat render cannot reach 90 degrees off-axis (vx1000.cpp), so 170 across is the most.
    f.number("fov", lens.fisheye_fov, 60, 170);
    f.number("magnification", lens.magnification, 0.25, 4);
    if (!f.pair("vignette", lens.vignette_radius, lens.vignette_growth, 0, 1.25, 0, 2) && !value->find("vignette") &&
        !lens.fisheye) {
        lens.vignette_radius = lens.vignette_growth = 0; // a flat converter casts no shadow unless it says so
    }
    f.unknown();
    reading.profile = std::move(lens);
    return reading;
}

std::string write_camera(const CameraProfile &c) {
    const auto &l = c.look;
    const auto &s = c.stamp;
    std::string out = "{\n";
    out += "  \"name\": " + quoted(c.name) + ",\n";
    out += "  \"sensor_mm\": [" + number_text(c.lens.sensor_width_mm) + ", " + number_text(c.lens.sensor_height_mm) + "],\n";
    out += "  \"zoom_mm\": [" + number_text(c.lens.wide_mm) + ", " + number_text(c.lens.tele_mm) + "],\n";
    out += "  \"zoom_seconds\": [" + number_text(c.lens.fastest_zoom_s) + ", " + number_text(c.lens.slowest_zoom_s) + "],\n";
    out += "  \"motion\": \"" + std::string(motion_word(c.motion)) + "\",\n";
    out += "  \"lines\": " + std::to_string(c.lines) + ",\n";
    out += "  \"tape_minutes\": " + number_text(c.tape_minutes) + ",\n";
    out += "  \"look\": {\n";
    out += "    \"format\": " + quoted(l.format) + ",\n";
    out += "    \"black_crush\": " + number_text(l.black_crush) + ",\n";
    out += "    \"highlight_gain\": " + number_text(l.highlight_gain) + ",\n";
    out += "    \"saturation\": " + number_text(l.saturation) + ",\n";
    out += "    \"white_balance_drift\": " + number_text(l.white_balance_drift) + ",\n";
    out += "    \"sharpen\": " + number_text(l.sharpen) + ",\n";
    out += "    \"grain\": " + number_text(l.grain) + ",\n";
    out += "    \"chroma_width\": " + number_text(l.chroma_width) + ",\n";
    out += "    \"softness\": " + number_text(l.softness) + "\n";
    out += "  },\n";
    out += "  \"stamp\": {\n";
    out += "    \"corner\": \"" + std::string(corner_word(s.corner)) + "\",\n";
    out += "    \"lines\": \"" + std::string(lines_word(s.lines)) + "\",\n";
    out += "    \"date\": \"" + std::string(date_word(s.date)) + "\",\n";
    out += "    \"colour\": [" + std::to_string(s.colour[0]) + ", " + std::to_string(s.colour[1]) + ", " + std::to_string(s.colour[2]) + "]\n";
    out += "  },\n";
    const auto &m = c.mic;
    out += "  \"mic\": {\n";
    out += "    \"high_cut\": " + number_text(m.high_cut) + ",\n";
    out += "    \"low_cut\": " + number_text(m.low_cut) + ",\n";
    out += "    \"target\": " + number_text(m.target) + ",\n";
    out += "    \"most_gain\": " + number_text(m.most_gain) + ",\n";
    out += "    \"attack\": " + number_text(m.attack) + ",\n";
    out += "    \"release\": " + number_text(m.release) + ",\n";
    out += "    \"drive\": " + number_text(m.drive) + "\n";
    out += "  }\n}\n";
    return out;
}

std::string write_grip(const GripProfile &g) {
    std::string out = "{\n";
    out += "  \"name\": " + quoted(g.name) + ",\n";
    out += "  \"height\": " + number_text(g.height) + ",\n";
    out += "  \"side\": " + number_text(g.side) + ",\n";
    out += "  \"ahead\": " + number_text(g.ahead) + ",\n";
    out += "  \"pitch\": " + number_text(g.pitch) + ",\n";
    out += "  \"crouch\": " + number_text(g.crouch) + ",\n";
    out += "  \"shake\": " + number_text(g.shake) + "\n}\n";
    return out;
}

std::string write_handling(const Handling &h) {
    const auto line = [](const char *key, double v, bool last = false) {
        return std::string("    \"") + key + "\": " + number_text(v) + (last ? "\n" : ",\n");
    };
    std::string out = "{\n  \"filmer\": {\n";
    out += line("reaction", h.reaction) + line("look_speed", h.look_speed) + line("aim_height", h.aim_height) +
           line("lead", h.lead) + line("steadiness", h.steadiness) + line("max_nudge", h.max_nudge) +
           line("raise_speed", h.raise_speed) + line("lowest", h.lowest) + line("highest", h.highest) +
           line("height_step", h.height_step, true);
    out += "  },\n  \"arms\": {\n";
    out += line("sway", h.arm_sway) + line("bob", h.arm_bob) + line("damping", h.arm_damping) + line("tilt", h.arm_tilt) +
           line("reach", h.arm_reach, true);
    out += "  },\n  \"aim\": {\n";
    out += line("swing", h.aim_swing) + line("damping", h.aim_damping) + line("hunting", h.hunting, true);
    out += "  },\n  \"shake\": {\n";
    out += line("hands", h.shake.hands) + line("wheels", h.shake.wheels) + line("footsteps", h.shake.footsteps, true);
    out += "  },\n  \"tripod\": {\n";
    out += line("height", h.tripod_height) + line("drag", h.tripod_drag) + line("pan_speed", h.tripod_pan_speed) +
           line("follow", h.tripod_follow, true);
    out += "  },\n  \"controls\": {\n";
    out += line("mouse", h.mouse) + line("wheel_notch", h.wheel_notch, true);
    out += "  }\n}\n";
    return out;
}

std::string write_adapter(const Adapter &a) {
    std::string out = "{\n";
    out += "  \"name\": " + quoted(a.name) + ",\n";
    out += std::string("  \"fisheye\": ") + (a.fisheye ? "true" : "false") + ",\n";
    out += "  \"fov\": " + number_text(a.fisheye_fov) + ",\n";
    out += "  \"magnification\": " + number_text(a.magnification) + ",\n";
    out += "  \"vignette\": [" + number_text(a.vignette_radius) + ", " + number_text(a.vignette_growth) + "]\n}\n";
    return out;
}

bool valid_id(std::string_view id) {
    if (id.empty() || id.size() > 32) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

std::optional<std::pair<ProfileKind, std::string>> profile_file(std::string_view name) {
    if (name == "handling.json") return std::pair{ProfileKind::handling, std::string("handling")};
    for (const auto &[suffix, kind] : {std::pair{std::string_view(".camera.json"), ProfileKind::camera},
                                      std::pair{std::string_view(".lens.json"), ProfileKind::lens},
                                      std::pair{std::string_view(".grip.json"), ProfileKind::grip}}) {
        if (name.size() <= suffix.size() || name.substr(name.size() - suffix.size()) != suffix) continue;
        std::string id(name.substr(0, name.size() - suffix.size()));
        if (!valid_id(id)) return std::nullopt;
        return std::pair{kind, std::move(id)};
    }
    return std::nullopt;
}

Catalogue::Catalogue() : cameras_{vx1000()}, adapters_{century_fisheye()}, grips_{long_grip()} {}

void Catalogue::add(GripProfile profile) {
    for (auto &g : grips_)
        if (g.id == profile.id) {
            g = std::move(profile);
            return;
        }
    grips_.push_back(std::move(profile));
}

const GripProfile *Catalogue::grip(std::string_view id) const {
    for (const auto &g : grips_)
        if (g.id == id) return &g;
    return nullptr;
}

const GripProfile &Catalogue::next_grip(std::string_view id) const {
    for (std::size_t i = 0; i < grips_.size(); ++i)
        if (grips_[i].id == id) return grips_[(i + 1) % grips_.size()];
    return grips_.front();
}

void Catalogue::add(CameraProfile profile) {
    for (auto &c : cameras_)
        if (c.id == profile.id) {
            c = std::move(profile);
            return;
        }
    cameras_.push_back(std::move(profile));
}

void Catalogue::add(Adapter profile) {
    for (auto &a : adapters_)
        if (a.id == profile.id) {
            a = std::move(profile);
            return;
        }
    adapters_.push_back(std::move(profile));
}

const CameraProfile *Catalogue::camera(std::string_view id) const {
    for (const auto &c : cameras_)
        if (c.id == id) return &c;
    return nullptr;
}

const Adapter *Catalogue::adapter(std::string_view id) const {
    for (const auto &a : adapters_)
        if (a.id == id) return &a;
    return nullptr;
}

const CameraProfile &Catalogue::next_camera(std::string_view id) const {
    for (std::size_t i = 0; i < cameras_.size(); ++i)
        if (cameras_[i].id == id) return cameras_[(i + 1) % cameras_.size()];
    return cameras_.front();
}

const Adapter *Catalogue::next_adapter(std::string_view id) const {
    if (id.empty()) return adapters_.empty() ? nullptr : &adapters_.front();
    for (std::size_t i = 0; i < adapters_.size(); ++i)
        if (adapters_[i].id == id) return i + 1 < adapters_.size() ? &adapters_[i + 1] : nullptr;
    return nullptr;
}

const Adapter *Catalogue::fisheye() const {
    for (const auto &a : adapters_)
        if (a.fisheye) return &a;
    return nullptr;
}
} // namespace dingosdk::camcorder
