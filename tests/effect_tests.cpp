#include "camcorder/effects.h"
#include "camcorder/profiles.h"
#include "check.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace dingosdk::camcorder;

namespace {
bool mentions(const std::vector<std::string> &notes, const std::string &text) {
    for (const auto &n : notes)
        if (n.find(text) != std::string::npos) return true;
    return false;
}
DvLook all_on() {
    DvLook look;
    look.on = true;
    return look;
}
std::string identity_cube(int n) {
    std::string text = "TITLE \"identity\"\n# a comment\nLUT_3D_SIZE " + std::to_string(n) + "\n";
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r)
                text += std::to_string(r / double(n - 1)) + " " + std::to_string(g / double(n - 1)) + " " + std::to_string(b / double(n - 1)) + "\n";
    return text;
}
// The curve the shader draws (camcorder_shader.h, curve_at), from a curves pass's numbers.
float curve_at(const EffectPass &pass, float x) {
    const auto &v = pass.values;
    const int n = static_cast<int>(v[1]);
    if (x <= v[2]) return v[3] + (x - v[2]) * v[18];
    if (x > v[static_cast<std::size_t>(2 + 2 * (n - 1))])
        return v[static_cast<std::size_t>(3 + 2 * (n - 1))] + (x - v[static_cast<std::size_t>(2 + 2 * (n - 1))]) * v[static_cast<std::size_t>(18 + n - 1)];
    for (int k = 0; k + 1 < n; ++k) {
        const auto j = static_cast<std::size_t>(k);
        const float x0 = v[2 + 2 * j], x1 = v[4 + 2 * j];
        if (x > x0 && x <= x1) {
            const float h = x1 - x0, t = (x - x0) / h, t2 = t * t, t3 = t2 * t;
            return (2 * t3 - 3 * t2 + 1) * v[3 + 2 * j] + (t3 - 2 * t2 + t) * h * v[18 + j] + (-2 * t3 + 3 * t2) * v[5 + 2 * j] +
                   (t3 - t2) * h * v[19 + j];
        }
    }
    return 0;
}
} // namespace

TEST(the_effect_library_is_whole) {
    std::set<std::string_view> names;
    for (const auto &kind : effect_library()) {
        CHECK(names.insert(kind.name).second); // each name once
        CHECK(find_effect(kind.name) == &kind);
        CHECK(effect_kind(kind.type).name == kind.name);
        std::size_t used = 0;
        std::set<std::string_view> keys;
        for (const auto &p : kind.params) {
            CHECK(keys.insert(p.key).second);
            CHECK(p.key != "type" && p.key != "mix" && p.key != "part");
            used += p.count();
            if (p.kind == ParamKind::number || p.kind == ParamKind::pair || p.kind == ParamKind::triple)
                for (std::size_t i = 0; i < p.count(); ++i) CHECK(p.fallback[i] >= p.low && p.fallback[i] <= p.high);
            if (p.kind == ParamKind::word) CHECK(p.fallback[0] < static_cast<float>(p.words.size()));
        }
        // Their numbers fit below the 24 the plan keeps for what it works out each frame (curves
        // keeps its slopes from 18, the blurs their direction from 28).
        CHECK(used <= (kind.type == Effect::curves ? 18u : 24u));
    }
    CHECK(names.size() == static_cast<std::size_t>(effect_types));
    CHECK(!find_effect("sparkles"));
    const auto reference = effect_reference();
    for (const auto &kind : effect_library()) CHECK(reference.find("`" + std::string(kind.name) + "`") != std::string::npos);
}

#ifdef CAMCORDER_SHADER
TEST(every_effect_has_its_program_in_the_shader) {
    std::ifstream in(CAMCORDER_SHADER);
    std::stringstream text;
    text << in.rdbuf();
    const auto shader = text.str();
    CHECK(!shader.empty());
    for (const auto &kind : effect_library()) {
        const auto program = 8 * static_cast<int>(kind.type);
        const auto line = "case " + std::to_string(program) + ": c = " + std::string(kind.name) +
                          (kind.type == Effect::bloom ? "_bright_program(p)" : "_program(p)");
        if (shader.find(line) == std::string::npos) std::printf("    missing: %s\n", line.c_str());
        CHECK(shader.find(line) != std::string::npos);
    }
    CHECK(shader.find("case 33: c = bloom_add_program(p)") != std::string::npos);
    CHECK(shader.find("case " + std::to_string(effect_blur_program) + ": c = plain_blur(p)") != std::string::npos);
}
#endif

TEST(a_camera_lists_its_effects) {
    const auto read = read_camera(R"({
      "effects": [
        {"type": "resolution", "lines": 240, "filter": "sharp"},
        {"type": "blur", "channels": "chroma", "direction": "horizontal", "radius": 3, "mix": 0.5},
        {"type": "lift_gamma_gain", "lift": 0.05, "gain": [1.1, 1, 0.9]},
        {"type": "monochrome", "colour": [255, 225, 180]},
        {"type": "curves", "channel": "luma", "points": [[1, 0.9], [0, 0.1], [0.5, 0.55]]},
        {"type": "grain", "amount": 0.2, "part": "always"}
      ]
    })", "test");
    CHECK(read.profile && read.notes.empty());
    const auto &stack = *read.profile->effects;
    CHECK(stack.size() == 6);
    CHECK(stack[0].type == Effect::resolution && effect_value(stack[0], "lines") == 240 && effect_value(stack[0], "filter") == 1);
    CHECK(effect_value(stack[0], "samples") == 0); // what a block leaves out is its default
    CHECK(stack[1].mix == 0.5f && effect_value(stack[1], "channels") == 2 && effect_value(stack[1], "direction") == 0);
    CHECK(effect_value(stack[2], "lift", 1) == 0.05f && effect_value(stack[2], "gain", 2) == 0.9f);
    CHECK_NEAR(effect_value(stack[3], "colour", 1), 225 / 255.0, 1e-6);
    // Points are kept in order, whatever order they were given in.
    CHECK(effect_value(stack[4], "points") == 3 && effect_value(stack[4], "points", 1) == 0 && effect_value(stack[4], "points", 5) == 1);
    CHECK(stack[5].part == Part::always && stack[4].part == Part::tone);
    // The camera's classic look follows them (for a recording made where the screen can't show them).
    CHECK(read.profile->look.saturation == 0 && read.profile->look.grain == 0.2f && read.profile->look.chroma_width == 6);
    // Written out and read back: the same.
    const auto again = read_camera(write_camera(*read.profile), "test");
    CHECK(again.notes.empty() && again.profile->effects == read.profile->effects);
    // A camera without effects gets its look's; with an empty list, none.
    CHECK(!read_camera("{}", "plain").profile->effects);
    CHECK(camera_effects(vx1000()) == stack_from_look(vx1000().look, vx1000().lines));
    CHECK(read_camera(R"({"effects": []})", "none").profile->effects->empty());
}

TEST(effects_that_are_wrong_say_so) {
    const auto read = read_camera(R"({
      "look": {"format": "Test", "grain": 0.5},
      "effects": [
        {"type": "sparkles"},
        {"lines": 3},
        12,
        {"type": "blur", "direction": "diagonal", "radius": 900, "wobble": 1},
        {"type": "levels", "output": 1},
        {"type": "curves", "points": [[0, 0]]},
        {"type": "vignette", "colour": [1, 2]},
        {"type": "grain", "part": "sometimes", "mix": 2}
      ]
    })", "test");
    const auto &notes = read.notes;
    CHECK(mentions(notes, "effects[0].type \"sparkles\" is not one of"));
    CHECK(mentions(notes, "effects[1].type is missing"));
    CHECK(mentions(notes, "effects[2] should be an object"));
    CHECK(mentions(notes, "effects[3].direction \"diagonal\" is not one of horizontal, vertical, both"));
    CHECK(mentions(notes, "effects[3].radius is 900"));
    CHECK(mentions(notes, "effects[3].wobble is not a field"));
    CHECK(mentions(notes, "effects[4].output should be two numbers"));
    CHECK(mentions(notes, "effects[5].points should be 2 to 8 points"));
    CHECK(mentions(notes, "effects[6].colour should be [red, green, blue]"));
    CHECK(mentions(notes, "effects[7].part \"sometimes\""));
    CHECK(mentions(notes, "effects[7].mix is 2"));
    CHECK(mentions(notes, "only look.format is used"));
    const auto &stack = *read.profile->effects;
    CHECK(stack.size() == 5); // the four broken ones left out
    CHECK(effect_value(stack[0], "radius") == 60);
    CHECK(read.profile->look.format == "Test");
    // At most 32.
    std::string many = R"({"effects": [)";
    for (int i = 0; i < 40; ++i) many += std::string(i ? "," : "") + R"({"type": "invert"})";
    const auto lots = read_camera(many + "]}", "lots");
    CHECK(lots.profile->effects->size() == most_effects && mentions(lots.notes, "more than 32 effects"));
}

TEST(a_lut_comes_from_a_cube_beside_the_camera) {
    std::string error;
    const auto lut = read_cube(identity_cube(3), error);
    CHECK(lut && lut->size == 3 && lut->rgb.size() == 81 && lut->title == "identity");
    CHECK_NEAR(lut->rgb[3], 0.5, 1e-5); // red runs fastest
    CHECK(!read_cube("LUT_3D_SIZE 2\n0 0 0\n", error) && error.find("1 colours where") != std::string::npos);
    CHECK(!read_cube("LUT_1D_SIZE 4\n", error) && error.find("1D") != std::string::npos);
    CHECK(!read_cube("0 0 0\n", error));
    CHECK(!read_cube("LUT_3D_SIZE 99\n", error));
    CHECK(read_cube("DOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n" + identity_cube(2), error)->high[1] == 2);

    const ProfileFiles files = [](std::string_view name) -> std::optional<std::string> {
        if (name == "grade.cube") return identity_cube(4);
        if (name == "broken.cube") return std::string("LUT_3D_SIZE 4\n1 2\n");
        return std::nullopt;
    };
    const auto read = read_camera(R"({"effects": [
        {"type": "lut", "file": "grade.cube", "mix": 0.7},
        {"type": "lut", "file": "../../secrets.cube"},
        {"type": "lut", "file": "missing.cube"},
        {"type": "lut", "file": "broken.cube"},
        {"type": "lut"}
    ]})", "graded", files);
    CHECK(read.profile->effects->size() == 1);
    const auto &block = read.profile->effects->front();
    CHECK(block.lut && block.lut->size == 4 && block.file == "grade.cube");
    CHECK(mentions(read.notes, "\"../../secrets.cube\" should be a .cube file's name"));
    CHECK(mentions(read.notes, "\"missing.cube\" could not be read"));
    CHECK(mentions(read.notes, "\"broken.cube\": line"));
    CHECK(mentions(read.notes, "effects[4].file is needed"));
    // The same table read twice is the same; another is not.
    const auto twice = read_camera(R"({"effects": [{"type": "lut", "file": "grade.cube", "mix": 0.7}]})", "graded", files);
    CHECK(twice.profile->effects == read.profile->effects);
    auto other = *read.profile->effects;
    other[0].lut = std::make_shared<const Lut3d>(*read_cube(identity_cube(3), error));
    CHECK(!(other == *read.profile->effects));
    // Its pass carries the table and its size.
    const auto passes = plan_effects(*read.profile->effects, all_on(), {});
    CHECK(passes.size() == 1 && passes[0].lut == block.lut && passes[0].values[0] == 4 && passes[0].values[4] == 1);
    // Without its table (never read) a lut draws nothing.
    CHECK(plan_effects({effect_block(Effect::lut)}, all_on(), {}).empty());
}

TEST(the_vx1000_look_is_a_stack) {
    const auto stack = stack_from_look(LookProfile{}, 480);
    std::vector<Effect> types;
    for (const auto &b : stack) types.push_back(b.type);
    CHECK((types == std::vector<Effect>{Effect::resolution, Effect::blur, Effect::sharpen, Effect::white_balance, Effect::tone, Effect::grain}));
    CHECK(effect_value(stack[0], "lines") == 480);
    CHECK(effect_value(stack[1], "radius") == 2 && stack[1].part == Part::tape);
    CHECK(effect_value(stack[4], "gain") == 1.15f && effect_value(stack[4], "bleed") == 1);
    // And back: the same look.
    const auto look = classic_look(stack);
    CHECK(look.black_crush == 0.05f && look.highlight_gain == 1.15f && look.sharpen == 0.6f && look.grain == 0.04f);
    CHECK(look.chroma_width == 4 && look.white_balance_drift == 0.035f && look.softness == 0);
    // Hi8's softness and saturation become blocks too.
    LookProfile hi8;
    hi8.softness = 1.2f;
    hi8.saturation = 0.85f;
    const auto soft = stack_from_look(hi8, 480);
    CHECK(std::count_if(soft.begin(), soft.end(), [](const EffectBlock &b) { return b.type == Effect::blur; }) == 2);
    CHECK(std::any_of(soft.begin(), soft.end(), [](const EffectBlock &b) { return b.type == Effect::hue_saturation; }));
}

TEST(the_plan_follows_the_switches_and_the_routes) {
    const auto stack = stack_from_look(LookProfile{}, 480);
    CHECK(plan_effects(stack, DvLook{}, {}).empty()); // the look off
    auto look = all_on();
    CHECK(plan_effects(stack, look, {}).size() == 6);
    look.chroma = false; // the tape: the raster and the colour smear
    look.grain = false;
    CHECK(plan_effects(stack, look, {}).size() == 3);
    // A blur both ways: across into a spare, then down, mixed against the picture it started from.
    auto blur = effect_block(Effect::blur);
    blur.mix = 0.5f;
    auto passes = plan_effects({blur}, all_on(), {});
    CHECK(passes.size() == 2);
    CHECK(passes[0].program == effect_blur_program && passes[0].write == Slot::scratch_a && passes[0].values[28] == 1);
    CHECK(passes[1].program == 16 && passes[1].read == Slot::scratch_a && passes[1].other == Slot::current && passes[1].mix_with_other);
    CHECK(passes[1].write == Slot::next && passes[1].values[29] == 1 && passes[1].mix == 0.5f);
    // One way: one pass.
    set_effect_value(blur, "direction", 1);
    passes = plan_effects({blur}, all_on(), {});
    CHECK(passes.size() == 1 && passes[0].values[29] == 1 && !passes[0].mix_with_other);
    // Bloom: the bright parts, three more blurs between the spares, then added to the picture.
    passes = plan_effects({effect_block(Effect::bloom)}, all_on(), {});
    CHECK(passes.size() == 5 && passes[0].program == 32 && passes[4].program == 33);
    CHECK(passes[3].write == Slot::scratch_b && passes[4].other == Slot::scratch_b && passes[4].read == Slot::current);
    // Trails keep their picture for the next frame; only the first trails block has one.
    passes = plan_effects({effect_block(Effect::trails), effect_block(Effect::trails)}, all_on(), {});
    CHECK(passes.size() == 1 && passes[0].other == Slot::history && passes[0].keep);
    // A block mixed to nothing is left out.
    auto none = effect_block(Effect::invert);
    none.mix = 0;
    CHECK(plan_effects({none}, all_on(), {}).empty());
}

TEST(what_moves_moves_with_time) {
    // The white balance's drift is the classic look's.
    auto balance = effect_block(Effect::white_balance);
    set_effect_value(balance, "drift", 0.035f);
    for (const double t : {0.0, 3.0, 20.0}) {
        const auto pass = plan_effects({balance}, all_on(), {t, 0, 30, 0.2f})[0];
        const auto classic = white_balance(LookProfile{}, DvClip{0.2f}, t);
        CHECK_NEAR(pass.values[24], classic[0], 1e-5);
        CHECK_NEAR(pass.values[26], classic[1], 1e-5);
        CHECK(pass.values[25] == 1);
    }
    set_effect_value(balance, "temperature", 1);
    set_effect_value(balance, "drift", 0);
    const auto warm = plan_effects({balance}, all_on(), {})[0];
    CHECK(warm.values[24] > 1 && warm.values[26] < 1);
    // Flicker and weave wander, and are the same at the same moment.
    const auto flicker = effect_block(Effect::flicker), weave = effect_block(Effect::weave);
    const auto a = plan_effects({flicker, weave}, all_on(), {1.0, 30, 30, 0.37f});
    const auto b = plan_effects({flicker, weave}, all_on(), {1.0, 30, 30, 0.37f});
    const auto c = plan_effects({flicker, weave}, all_on(), {2.5, 75, 30, 0.37f});
    CHECK(a[0].values[24] == b[0].values[24] && a[1].values[24] == b[1].values[24]);
    CHECK(a[0].values[24] != c[0].values[24] && a[1].values[24] != c[1].values[24]);
    CHECK(std::abs(a[0].values[24] - 1) <= 0.05f + 1e-6f && std::abs(a[1].values[24]) <= 1.5f + 1e-6f);
    // Tracking that shows half the time is gone for the other half.
    auto tracking = effect_block(Effect::tracking);
    set_effect_value(tracking, "duty", 0.5f);
    set_effect_value(tracking, "period", 4);
    int shown = 0;
    for (int i = 0; i < 400; ++i) shown += plan_effects({tracking}, all_on(), {i * 0.01, i, 100, 0}).empty() ? 0 : 1;
    CHECK(shown > 150 && shown < 250);
    // A light leak comes in from the edge it is given.
    auto leak = effect_block(Effect::light_leak);
    set_effect_value(leak, "edge", 2); // right
    for (const double t : {0.0, 7.0, 31.0}) CHECK(plan_effects({leak}, all_on(), {t, 0, 30, 0})[0].values[24] > 1);
}

TEST(a_curve_passes_through_its_points_without_overshooting) {
    auto curve = effect_block(Effect::curves);
    // An S curve, and one with a flat stretch (where a plain spline would bulge).
    const std::vector<std::vector<std::pair<float, float>>> shapes{{{0, 0}, {0.25f, 0.15f}, {0.75f, 0.85f}, {1, 1}},
                                                                   {{0, 0}, {0.3f, 0.5f}, {0.6f, 0.5f}, {1, 1}}};
    for (const auto &points : shapes) {
        set_effect_value(curve, "points", static_cast<float>(points.size()), 0);
        for (std::size_t i = 0; i < points.size(); ++i) {
            set_effect_value(curve, "points", points[i].first, 1 + 2 * i);
            set_effect_value(curve, "points", points[i].second, 2 + 2 * i);
        }
        const auto pass = plan_effects({curve}, all_on(), {})[0];
        for (const auto &[x, y] : points) CHECK_NEAR(curve_at(pass, x), y, 1e-5);
        float last = -1;
        for (int i = 0; i <= 200; ++i) {
            const float y = curve_at(pass, i / 200.0f);
            CHECK(y >= last - 1e-5f); // never turns back
            last = y;
        }
        CHECK_NEAR(curve_at(pass, 0.45f), points[1].second == 0.5f ? 0.5 : curve_at(pass, 0.45f), 1e-5); // the flat stays flat
    }
}
