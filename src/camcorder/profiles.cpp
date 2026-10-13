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
    // Three numbers, [red, green, blue], each 0 to 255.
    void colour(std::string_view key, std::array<std::uint8_t, 3> &out) {
        const auto *v = get(key, Kind::array, "three numbers, [red, green, blue]");
        if (!v) return;
        if (v->items.size() != 3 || !std::all_of(v->items.begin(), v->items.end(), [](const Value &c) { return c.is(Kind::number); })) {
            notes.push_back(where + std::string(key) + " should be three numbers, [red, green, blue]; the default is kept.");
            return;
        }
        for (std::size_t i = 0; i < 3; ++i) out[i] = static_cast<std::uint8_t>(std::lround(clamp_note(key, v->items[i].number, 0, 255)));
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
const char *corner_word(StampProfile::Corner c) {
    switch (c) {
    case StampProfile::Corner::bottom_left: return "bottom-left";
    case StampProfile::Corner::top_right: return "top-right";
    case StampProfile::Corner::top_left: return "top-left";
    case StampProfile::Corner::bottom_right: break;
    }
    return "bottom-right";
}
const char *projection_word(Projection p) {
    switch (p) {
    case Projection::equisolid: return "equisolid";
    case Projection::stereographic: return "stereographic";
    case Projection::orthographic: return "orthographic";
    case Projection::equidistant: break;
    }
    return "equidistant";
}
std::string colour_text(const std::array<std::uint8_t, 3> &c) {
    return "[" + std::to_string(c[0]) + ", " + std::to_string(c[1]) + ", " + std::to_string(c[2]) + "]";
}
const char *lines_word(StampProfile::Lines l) {
    return l == StampProfile::Lines::date_only ? "date" : l == StampProfile::Lines::date_over_time ? "date-time" : "time-date";
}
const char *date_word(StampProfile::Date d) { return d == StampProfile::Date::day_month_year ? "day-month-year" : "month-day-year"; }

// A file a profile names beside itself: a plain name with that extension, no folders.
bool plain_file_name(std::string_view name, std::string_view extension) {
    if (name.size() <= extension.size() || name.size() > 64 || name.substr(name.size() - extension.size()) != extension) return false;
    if (name.front() == '.') return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ';
    });
}

// The "effects" list (effects.h): each block's type, then its fields by the library's description.
EffectStack read_effects(const Value &list, std::vector<std::string> &notes, const ProfileFiles &files) {
    EffectStack stack;
    std::size_t index = 0;
    for (const auto &item : list.items) {
        const std::string where = "effects[" + std::to_string(index++) + "].";
        if (!item.is(Kind::object)) {
            notes.push_back(where.substr(0, where.size() - 1) + " should be an object, {\"type\": ...}; left out.");
            continue;
        }
        const auto *type = item.find("type");
        if (!type || !type->is(Kind::string)) {
            notes.push_back(where + "type is missing (\"blur\", \"grain\", ...); left out.");
            continue;
        }
        const auto *kind = find_effect(type->text);
        if (!kind) {
            notes.push_back(where + "type \"" + type->text + "\" is not one of the camcorder's effects (`camcorder effects` lists them); left out.");
            continue;
        }
        if (stack.size() >= most_effects) {
            notes.push_back(where.substr(0, where.size() - 1) + ": more than " + std::to_string(most_effects) + " effects; the rest are left out.");
            break;
        }
        auto block = effect_block(kind->type);
        Fields f{item, where, notes};
        f.known.emplace_back("type");
        f.number("mix", block.mix, 0, 1);
        if (const auto *part = f.get("part", Kind::string, "text")) {
            if (!part_from_word(part->text, block.part))
                notes.push_back(where + "part \"" + part->text + "\" is not one of always, tone, white_balance, sharpen, tape, grain; the type's own is kept.");
        }
        std::size_t at = 0;
        bool usable = true;
        for (const auto &p : kind->params) {
            float *values = block.values.data() + at;
            const auto key = p.key;
            at += p.count();
            switch (p.kind) {
            case ParamKind::number: f.number(key, values[0], p.low, p.high); break;
            case ParamKind::pair: f.pair(key, values[0], values[1], p.low, p.high, p.low, p.high); break;
            case ParamKind::flag: {
                bool on = values[0] > 0.5f;
                f.boolean(key, on);
                values[0] = on ? 1.0f : 0.0f;
                break;
            }
            case ParamKind::word: {
                const auto *v = f.get(key, Kind::string, "text");
                if (!v) break;
                const auto found = std::find(p.words.begin(), p.words.end(), v->text);
                if (found == p.words.end()) {
                    std::string allowed;
                    for (const auto w : p.words) allowed += (allowed.empty() ? "" : ", ") + std::string(w);
                    notes.push_back(where + std::string(key) + " \"" + v->text + "\" is not one of " + allowed + "; the default is kept.");
                } else {
                    values[0] = static_cast<float>(found - p.words.begin());
                }
                break;
            }
            case ParamKind::triple:
            case ParamKind::colour: {
                f.known.emplace_back(key);
                const auto *v = item.find(key);
                if (!v) break;
                const double scale = p.kind == ParamKind::colour ? 255 : 1;
                if (v->is(Kind::number) && p.kind == ParamKind::triple) {
                    const float one = f.clamp_note(key, v->number, p.low, p.high);
                    values[0] = values[1] = values[2] = one;
                } else if (v->is(Kind::array) && v->items.size() == 3 &&
                           std::all_of(v->items.begin(), v->items.end(), [](const Value &c) { return c.is(Kind::number); })) {
                    for (std::size_t i = 0; i < 3; ++i)
                        values[i] = static_cast<float>(f.clamp_note(key, v->items[i].number, p.low, p.high) / scale);
                } else {
                    notes.push_back(where + std::string(key) + (p.kind == ParamKind::colour ? " should be [red, green, blue], each 0 to 255"
                                                                                          : " should be [red, green, blue] or one number") +
                                    "; the default is kept.");
                }
                break;
            }
            case ParamKind::points: {
                const auto *v = f.get(key, Kind::array, "a list of points, [[in, out], ...]");
                if (!v) break;
                std::vector<std::pair<float, float>> points;
                for (const auto &point : v->items) {
                    if (!point.is(Kind::array) || point.items.size() != 2 || !point.items[0].is(Kind::number) || !point.items[1].is(Kind::number)) {
                        points.clear();
                        break;
                    }
                    points.emplace_back(f.clamp_note(key, point.items[0].number, 0, 1), f.clamp_note(key, point.items[1].number, 0, 1));
                }
                std::sort(points.begin(), points.end());
                // Two points at the same place in: the first is kept.
                points.erase(std::unique(points.begin(), points.end(), [](const auto &a, const auto &b) { return a.first == b.first; }), points.end());
                if (points.size() < 2 || points.size() > 8) {
                    notes.push_back(where + std::string(key) + " should be 2 to 8 points, [[in, out], ...], at different places in; the straight line is kept.");
                    break;
                }
                values[0] = static_cast<float>(points.size());
                for (std::size_t i = 0; i < points.size(); ++i) {
                    values[1 + 2 * i] = points[i].first;
                    values[2 + 2 * i] = points[i].second;
                }
                break;
            }
            case ParamKind::file: {
                const auto *v = f.get(key, Kind::string, "a file name");
                if (!v) {
                    notes.push_back(where + std::string(key) + " is needed: the .cube file beside the camera's; left out.");
                    usable = false;
                    break;
                }
                if (!plain_file_name(v->text, ".cube")) {
                    notes.push_back(where + std::string(key) + " \"" + v->text + "\" should be a .cube file's name, beside the camera's file; left out.");
                    usable = false;
                    break;
                }
                block.file = v->text;
                const auto text = files ? files(v->text) : std::nullopt;
                if (!text) {
                    notes.push_back(where + std::string(key) + " \"" + v->text + "\" could not be read; left out.");
                    usable = false;
                    break;
                }
                std::string error;
                if (auto lut = read_cube(*text, error)) {
                    block.lut = std::make_shared<const Lut3d>(std::move(*lut));
                } else {
                    notes.push_back(where + std::string(key) + " \"" + v->text + "\": " + error + " Left out.");
                    usable = false;
                }
                break;
            }
            }
        }
        f.unknown();
        if (usable) stack.push_back(std::move(block));
    }
    return stack;
}

std::string effect_text(const EffectBlock &block) {
    const auto &kind = effect_kind(block.type);
    const auto fallback = effect_block(block.type);
    std::string out = "{\"type\": \"" + std::string(kind.name) + "\"";
    if (block.mix != 1) out += ", \"mix\": " + number_text(block.mix);
    if (block.part != kind.part) out += ", \"part\": \"" + std::string(part_word(block.part)) + "\"";
    std::size_t at = 0;
    for (const auto &p : kind.params) {
        const float *v = block.values.data() + at;
        const float *d = fallback.values.data() + at;
        const auto count = p.count();
        at += count;
        if (p.kind == ParamKind::file) {
            out += ", \"" + std::string(p.key) + "\": " + quoted(block.file);
            continue;
        }
        if (std::equal(v, v + count, d)) continue; // the default: left out
        out += ", \"" + std::string(p.key) + "\": ";
        switch (p.kind) {
        case ParamKind::number: out += number_text(v[0]); break;
        case ParamKind::flag: out += v[0] > 0.5f ? "true" : "false"; break;
        case ParamKind::word: out += "\"" + std::string(p.words[static_cast<std::size_t>(v[0])]) + "\""; break;
        case ParamKind::pair: out += "[" + number_text(v[0]) + ", " + number_text(v[1]) + "]"; break;
        case ParamKind::triple: out += "[" + number_text(v[0]) + ", " + number_text(v[1]) + ", " + number_text(v[2]) + "]"; break;
        case ParamKind::colour:
            out += "[" + std::to_string(std::lround(v[0] * 255)) + ", " + std::to_string(std::lround(v[1] * 255)) + ", " +
                   std::to_string(std::lround(v[2] * 255)) + "]";
            break;
        case ParamKind::points: {
            out += "[";
            for (int i = 0; i < static_cast<int>(v[0]); ++i)
                out += (i ? ", [" : "[") + number_text(v[1 + 2 * i]) + ", " + number_text(v[2 + 2 * i]) + "]";
            out += "]";
            break;
        }
        case ParamKind::file: break;
        }
    }
    return out + "}";
}

// A viewfinder's "layout" (viewfinder_layout.h): each element's fields.
std::vector<ViewfinderElement> read_layout(const Value &list, std::vector<std::string> &notes) {
    std::vector<ViewfinderElement> elements;
    std::size_t index = 0;
    for (const auto &item : list.items) {
        const std::string where = "viewfinder.layout[" + std::to_string(index++) + "].";
        if (!item.is(Kind::object)) {
            notes.push_back(where.substr(0, where.size() - 1) + " should be an object, {\"show\": ...}; left out.");
            continue;
        }
        if (elements.size() >= 48) {
            notes.push_back(where.substr(0, where.size() - 1) + ": more than 48 elements; the rest are left out.");
            break;
        }
        ViewfinderElement e;
        Fields f{item, where, notes};
        if (const auto *show = f.get("show", Kind::string, "text")) {
            if (const auto kind = show_from_word(show->text)) {
                e.show = *kind;
            } else {
                notes.push_back(where + "show \"" + show->text + "\" is not one of text, dot, battery, zoom-bar, box, centre, grid, corners; left out.");
                continue;
            }
        }
        if (e.show == Show::grid) e.size = 3;
        if (e.show == Show::battery) e.size = 16.5f;
        if (e.show == Show::corners) e.size = 40;
        if (const auto *anchor = f.get("anchor", Kind::string, "text")) {
            if (const auto a = anchor_from_word(anchor->text)) e.anchor = *a;
            else notes.push_back(where + "anchor \"" + anchor->text + "\" is not one of top-left, top, top-right, left, centre, right, bottom-left, bottom, bottom-right; top-left is kept.");
        }
        f.pair("offset", e.offset[0], e.offset[1], -480, 480, -480, 480);
        f.number("size", e.size, 0.5, 480);
        f.number("length", e.extent[0], 0.02, 1);
        f.number("width", e.extent[0], 0, 2000);
        f.number("height", e.extent[1], 0, 2000);
        f.text("text", e.text, 64);
        f.known.emplace_back("colour");
        if (const auto *colour = item.find("colour")) {
            if (colour->is(Kind::string)) {
                if (colour->text == "text") e.ink = Ink::text;
                else if (colour->text == "rec") e.ink = Ink::rec;
                else if (colour->text == "warning") e.ink = Ink::warning;
                else notes.push_back(where + "colour \"" + colour->text + "\" is not text, rec or warning; the text colour is kept.");
            } else {
                Fields c{item, where, notes};
                c.colour("colour", e.colour);
                e.ink = Ink::own;
            }
        }
        f.number("opacity", e.opacity, 0, 1);
        f.boolean("filled", e.filled);
        f.number("thickness", e.thickness, 0.25, 10);
        f.number("blink", e.blink, 0, 10);
        f.boolean("shadow", e.shadow);
        f.known.emplace_back("when");
        if (const auto *when = item.find("when")) {
            std::vector<const Value *> words;
            if (when->is(Kind::string)) words.push_back(when);
            else if (when->is(Kind::array))
                for (const auto &w : when->items) words.push_back(&w);
            if (words.empty() && !(when->is(Kind::array) && when->items.empty()))
                notes.push_back(where + "when should be a condition or a list of them (\"recording\", [\"tripod\", \"!setup\"]); always shown.");
            for (const auto *w : words) {
                const auto condition = w->is(Kind::string) ? when_from_word(w->text) : std::nullopt;
                if (condition) e.when.push_back(*condition);
                else notes.push_back(where + "when: " + (w->is(Kind::string) ? "\"" + w->text + "\"" : std::string("a non-text")) +
                                     " is not one of recording, zooming, tripod, setup, handheld, low-battery, low-tape, tape-end, fisheye (! for not); left out of it.");
            }
        }
        f.unknown();
        elements.push_back(std::move(e));
    }
    return elements;
}

std::string element_text(const ViewfinderElement &e) {
    const ViewfinderElement plain;
    std::string out = "{\"show\": \"" + std::string(show_word(e.show)) + "\"";
    if (e.anchor != plain.anchor) out += ", \"anchor\": \"" + std::string(anchor_word(e.anchor)) + "\"";
    out += ", \"offset\": [" + number_text(e.offset[0]) + ", " + number_text(e.offset[1]) + "]";
    out += ", \"size\": " + number_text(e.size);
    if (e.show == Show::zoom_bar) out += ", \"length\": " + number_text(e.extent[0]);
    if (e.show == Show::box) out += ", \"width\": " + number_text(e.extent[0]) + ", \"height\": " + number_text(e.extent[1]);
    if (!e.text.empty()) out += ", \"text\": " + quoted(e.text);
    if (e.ink == Ink::own) out += ", \"colour\": " + colour_text(e.colour);
    else if (e.ink != Ink::text) out += std::string(", \"colour\": \"") + (e.ink == Ink::rec ? "rec" : "warning") + "\"";
    if (e.opacity != plain.opacity) out += ", \"opacity\": " + number_text(e.opacity);
    if (e.filled) out += ", \"filled\": true";
    if (e.thickness != plain.thickness) out += ", \"thickness\": " + number_text(e.thickness);
    if (e.blink != plain.blink) out += ", \"blink\": " + number_text(e.blink);
    if (!e.shadow) out += ", \"shadow\": false";
    if (!e.when.empty()) {
        out += ", \"when\": [";
        for (std::size_t i = 0; i < e.when.size(); ++i) out += (i ? ", \"" : "\"") + when_word(e.when[i]) + "\"";
        out += "]";
    }
    return out + "}";
}

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

EffectStack camera_effects(const CameraProfile &camera) {
    return camera.effects ? *camera.effects : stack_from_look(camera.look, camera.lines);
}

Reading<CameraProfile> read_camera(std::string_view text, std::string id, const ProfileFiles &files) {
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
    // The picture's shape, a number (1.778) or [width, height] ([16, 9]): the sensor keeps its width
    // and is cut (or extended) to it, as a camera recording 16:9 from a 4:3 chip does.
    f.known.emplace_back("aspect");
    if (const auto *a = value->find("aspect")) {
        double ratio = 0;
        if (a->is(Kind::number)) ratio = a->number;
        else if (a->is(Kind::array) && a->items.size() == 2 && a->items[0].is(Kind::number) && a->items[1].is(Kind::number) &&
                 a->items[1].number > 0)
            ratio = a->items[0].number / a->items[1].number;
        if (!(ratio > 0) || !std::isfinite(ratio)) {
            reading.notes.emplace_back("aspect should be a number (1.778) or [width, height] ([16, 9]); the sensor's shape is kept.");
        } else {
            const float kept = f.clamp_note("aspect", ratio, 0.5, 3);
            camera.lens.sensor_height_mm = camera.lens.sensor_width_mm / kept;
        }
    }
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
        l.number("highlight_knee", p.highlight_knee, 0.5, 1);
        l.number("highlight_spread", p.highlight_spread, 0.005, 0.5);
        l.number("grain_size", p.grain_size, 0.25, 8);
        l.number("grain_colour", p.grain_colour, 0, 1);
        l.number("sharpen_radius", p.sharpen_radius, 0.25, 8);
        if (l.pair("drift_seconds", p.drift_seconds[0], p.drift_seconds[1], 1, 3600, 1, 3600) && p.drift_seconds[0] < p.drift_seconds[1])
            std::swap(p.drift_seconds[0], p.drift_seconds[1]); // the slow one first
        l.unknown();
    }
    if (const auto *effects = f.get("effects", Kind::array, "a list of effects, [{\"type\": ...}, ...]")) {
        camera.effects = read_effects(*effects, reading.notes, files);
        if (const auto *look = value->find("look"); look && look->is(Kind::object) &&
            std::any_of(look->members.begin(), look->members.end(), [](const json::Member &m) { return m.key != "format"; }))
            reading.notes.emplace_back("look: with effects given, only look.format is used (the effects are the picture).");
        // What a recording does where the screen cannot show the effects: their nearest classic look.
        camera.look = classic_look(*camera.effects, camera.look);
    }
    // "stamp": false: a camera with no date stamp (film had none).
    if (const auto *none = value->find("stamp"); none && none->is(Kind::boolean)) {
        f.known.emplace_back("stamp");
        camera.stamp.fitted = none->boolean;
    } else if (const auto *stamp = f.get("stamp", Kind::object, "an object, { ... }")) {
        Fields s{*stamp, "stamp.", reading.notes};
        auto &p = camera.stamp;
        s.word("corner", p.corner, {{"bottom-right", StampProfile::Corner::bottom_right}, {"bottom-left", StampProfile::Corner::bottom_left},
                                    {"top-right", StampProfile::Corner::top_right}, {"top-left", StampProfile::Corner::top_left}});
        s.word("lines", p.lines, {{"time-date", StampProfile::Lines::time_over_date}, {"date", StampProfile::Lines::date_only},
                                  {"date-time", StampProfile::Lines::date_over_time}});
        s.word("date", p.date, {{"month-day-year", StampProfile::Date::month_day_year}, {"day-month-year", StampProfile::Date::day_month_year}});
        s.colour("colour", p.colour);
        s.number("size", p.size, 0.25, 4);
        s.pair("margin", p.margin[0], p.margin[1], 0, 0.4, 0, 0.4);
        s.boolean("outline", p.outline);
        s.colour("outline_colour", p.outline_colour);
        s.unknown();
    }
    if (const auto *finder = f.get("viewfinder", Kind::object, "an object, { ... }")) {
        Fields v{*finder, "viewfinder.", reading.notes};
        v.colour("text", camera.viewfinder.text);
        v.colour("rec", camera.viewfinder.rec);
        v.colour("warning", camera.viewfinder.warning);
        if (const auto *layout = v.get("layout", Kind::array, "a list of elements, [{\"show\": ...}, ...]"))
            camera.viewfinder.elements = read_layout(*layout, reading.notes);
        v.unknown();
    }
    f.unknown();
    reading.profile = std::move(camera);
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
        s.number("speed", h.shake.speed, 0.1, 8);
        s.unknown();
    }
    if (const auto *sound = f.get("sound", Kind::object, "an object, { ... }")) {
        Fields s{*sound, "sound.", reading.notes};
        s.number("near", h.sound.nearby, 0.5, 100);
        s.number("floor", h.sound.floor, 0, 1);
        s.number("air", h.sound.air, 1, 1000);
        s.number("pan", h.sound.pan, 0, 1);
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
    f.word("projection", lens.projection, {{"equidistant", Projection::equidistant}, {"equisolid", Projection::equisolid},
                                           {"stereographic", Projection::stereographic}, {"orthographic", Projection::orthographic}});
    f.number("magnification", lens.magnification, 0.25, 4);
    if (!f.pair("vignette", lens.vignette_radius, lens.vignette_growth, 0, 2.5, 0, 2) && !value->find("vignette") &&
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
    // With effects, the effects are the picture: the look is only its name.
    if (c.effects) out += "    \"format\": " + quoted(l.format) + "\n  },\n";
    else {
        out += "    \"format\": " + quoted(l.format) + ",\n";
        out += "    \"black_crush\": " + number_text(l.black_crush) + ",\n";
        out += "    \"highlight_gain\": " + number_text(l.highlight_gain) + ",\n";
        out += "    \"saturation\": " + number_text(l.saturation) + ",\n";
        out += "    \"white_balance_drift\": " + number_text(l.white_balance_drift) + ",\n";
        out += "    \"sharpen\": " + number_text(l.sharpen) + ",\n";
        out += "    \"grain\": " + number_text(l.grain) + ",\n";
        out += "    \"chroma_width\": " + number_text(l.chroma_width) + ",\n";
        out += "    \"softness\": " + number_text(l.softness) + ",\n";
        out += "    \"highlight_knee\": " + number_text(l.highlight_knee) + ",\n";
        out += "    \"highlight_spread\": " + number_text(l.highlight_spread) + ",\n";
        out += "    \"grain_size\": " + number_text(l.grain_size) + ",\n";
        out += "    \"grain_colour\": " + number_text(l.grain_colour) + ",\n";
        out += "    \"sharpen_radius\": " + number_text(l.sharpen_radius) + ",\n";
        out += "    \"drift_seconds\": [" + number_text(l.drift_seconds[0]) + ", " + number_text(l.drift_seconds[1]) + "]\n";
        out += "  },\n";
    }
    if (c.effects) {
        out += "  \"effects\": [";
        for (std::size_t i = 0; i < c.effects->size(); ++i) out += std::string(i ? ",\n" : "\n") + "    " + effect_text((*c.effects)[i]);
        out += c.effects->empty() ? "],\n" : "\n  ],\n";
    }
    if (!s.fitted) out += "  \"stamp\": false,\n";
    else {
        out += "  \"stamp\": {\n";
        out += "    \"corner\": \"" + std::string(corner_word(s.corner)) + "\",\n";
        out += "    \"lines\": \"" + std::string(lines_word(s.lines)) + "\",\n";
        out += "    \"date\": \"" + std::string(date_word(s.date)) + "\",\n";
        out += "    \"colour\": " + colour_text(s.colour) + ",\n";
        out += "    \"size\": " + number_text(s.size) + ",\n";
        out += "    \"margin\": [" + number_text(s.margin[0]) + ", " + number_text(s.margin[1]) + "],\n";
        out += std::string("    \"outline\": ") + (s.outline ? "true" : "false") + ",\n";
        out += "    \"outline_colour\": " + colour_text(s.outline_colour) + "\n";
        out += "  },\n";
    }
    out += "  \"viewfinder\": {\n";
    out += "    \"text\": " + colour_text(c.viewfinder.text) + ",\n";
    out += "    \"rec\": " + colour_text(c.viewfinder.rec) + ",\n";
    out += "    \"warning\": " + colour_text(c.viewfinder.warning);
    // The layout when it is the camera's own (the VX1000's is the one built in).
    if (c.viewfinder.elements != vx1000_viewfinder().elements) {
        out += ",\n    \"layout\": [";
        for (std::size_t i = 0; i < c.viewfinder.elements.size(); ++i)
            out += std::string(i ? ",\n" : "\n") + "      " + element_text(c.viewfinder.elements[i]);
        out += c.viewfinder.elements.empty() ? "]" : "\n    ]";
    }
    out += "\n  }\n}\n";
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
    out += line("hands", h.shake.hands) + line("wheels", h.shake.wheels) + line("footsteps", h.shake.footsteps) +
           line("speed", h.shake.speed, true);
    out += "  },\n  \"sound\": {\n";
    out += line("near", h.sound.nearby) + line("floor", h.sound.floor) + line("air", h.sound.air) + line("pan", h.sound.pan, true);
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
    out += "  \"projection\": \"" + std::string(projection_word(a.projection)) + "\",\n";
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
                                      std::pair{std::string_view(".lens.json"), ProfileKind::lens}}) {
        if (name.size() <= suffix.size() || name.substr(name.size() - suffix.size()) != suffix) continue;
        std::string id(name.substr(0, name.size() - suffix.size()));
        if (!valid_id(id)) return std::nullopt;
        return std::pair{kind, std::move(id)};
    }
    return std::nullopt;
}

Catalogue::Catalogue() : cameras_{vx1000()}, adapters_{century_fisheye()} {}

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
