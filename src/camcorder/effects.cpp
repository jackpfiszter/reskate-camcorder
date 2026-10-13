#include "effects.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dingosdk::camcorder {
namespace {
constexpr double tau = 6.283185307179586;
constexpr float to_radians = 3.14159265f / 180;

// ---- The library --------------------------------------------------------------------------------
using P = EffectParam;
using K = ParamKind;
constexpr std::string_view direction_words[]{"horizontal", "vertical", "both"};
constexpr std::string_view blur_channels[]{"all", "luma", "chroma"};
constexpr std::string_view sharpen_channels[]{"luma", "all"};
constexpr std::string_view shape_words[]{"gaussian", "box"};
constexpr std::string_view filter_words[]{"smooth", "sharp"};
constexpr std::string_view curve_channels[]{"all", "luma", "red", "green", "blue"};
constexpr std::string_view trail_words[]{"blend", "bright"};
constexpr std::string_view dust_words[]{"dark", "light", "both"};
constexpr std::string_view edge_words[]{"any", "left", "right", "top", "bottom"};
constexpr std::array<float, 3> white{1, 1, 1}, black{0, 0, 0};
constexpr std::array<float, 3> number(float v) { return {v, 0, 0}; }

constexpr P resolution_params[]{
    {"lines", K::number, 0, 2160, number(480), {}, "Lines from top to bottom, each the average of its height. 0 keeps the screen's"},
    {"samples", K::number, 0, 3840, number(0), {}, "Samples across, each the average of its width. 0 keeps the screen's"},
    {"filter", K::word, 0, 0, number(0), filter_words, "`smooth` blends between neighbouring lines as a TV does; `sharp` shows each as a block"},
};
constexpr P blur_params[]{
    {"radius", K::number, 0, 60, number(1), {}, "How far it spreads, either side"},
    {"direction", K::word, 0, 0, number(2), direction_words, "`horizontal` (along the tape's lines), `vertical` or `both`"},
    {"channels", K::word, 0, 0, number(0), blur_channels, "`all`, `luma` (brightness only) or `chroma` (colour only: tape's colour smear)"},
    {"shape", K::word, 0, 0, number(0), shape_words, "`gaussian` (soft) or `box` (an even average)"},
    {"shift", K::number, -40, 40, number(0), {}, "Moves what it blurred sideways: colour lagging behind on tape"},
};
constexpr P sharpen_params[]{
    {"amount", K::number, 0, 5, number(0.6f), {}, "Edge enhancement: a bright and a dark halo along edges"},
    {"radius", K::number, 0.25f, 10, number(1), {}, "The halo's reach"},
    {"direction", K::word, 0, 0, number(0), direction_words, "`horizontal` (as video cameras did), `vertical` or `both`"},
    {"channels", K::word, 0, 0, number(0), sharpen_channels, "`luma` (brightness, as cameras did) or `all`"},
    {"threshold", K::number, 0, 1, number(0), {}, "Edges fainter than this are left alone (keeps noise from sharpening)"},
};
constexpr P bloom_params[]{
    {"threshold", K::number, 0, 1, number(0.75f), {}, "How bright a part must be to glow"},
    {"amount", K::number, 0, 5, number(0.5f), {}, "How strongly it glows"},
    {"radius", K::number, 1, 200, number(24), {}, "How far the glow spreads"},
    {"colour", K::colour, 0, 255, white, {}, "The glow's colour: white for bloom, red-orange for film's halation"},
};
constexpr P ghost_params[]{
    {"offset", K::pair, -200, 200, {12, 0, 0}, {}, "Where the echo sits from the picture, [right, down]"},
    {"amount", K::number, 0, 1, number(0.15f), {}, "How strong the echo is"},
};
constexpr P ccd_smear_params[]{
    {"threshold", K::number, 0, 1, number(0.95f), {}, "How bright a light must be to smear"},
    {"amount", K::number, 0, 4, number(0.6f), {}, "How bright the streak is"},
    {"length", K::number, 0.05f, 1, number(1), {}, "How far it runs up and down, of the picture's height (1: top to bottom, as on a CCD)"},
    {"colour", K::colour, 0, 255, white, {}, "The streak's tint"},
};
constexpr P tone_params[]{
    {"black_crush", K::number, 0, 0.5f, number(0.05f), {}, "The share of the darkest range pushed to black"},
    {"gain", K::number, 0.5f, 3, number(1.15f), {}, "Over 1, white arrives early (1.15 clips the top 13%)"},
    {"knee", K::number, 0.5f, 1, number(0.925f), {}, "Where a clipped colour starts going white (of white)"},
    {"spread", K::number, 0.005f, 1, number(0.075f), {}, "How far above the knee it is fully white"},
    {"bleed", K::flag, 0, 1, number(1), {}, "Blown highlights go white (a small CCD's), rather than staying their colour"},
};
constexpr P levels_params[]{
    {"black", K::number, -0.5f, 0.9f, number(0), {}, "What becomes black"},
    {"white", K::number, 0.1f, 2, number(1), {}, "What becomes white"},
    {"gamma", K::number, 0.1f, 5, number(1), {}, "Over 1 lifts the middle, under 1 sinks it"},
    {"output", K::pair, -0.5f, 2, {0, 1, 0}, {}, "[black, white] it all ends up between: [0.1, 0.9] for a faded print"},
    {"exposure", K::number, -5, 5, number(0), {}, "Stops brighter (or darker, under 0)"},
    {"contrast", K::number, 0, 4, number(1), {}, "About the pivot: over 1 more, under 1 less"},
    {"pivot", K::number, 0, 1, number(0.5f), {}, "What contrast leaves alone"},
};
constexpr P curves_params[]{
    {"channel", K::word, 0, 0, number(0), curve_channels, "`all` (each colour through the curve), `luma` (brightness), `red`, `green` or `blue`"},
    {"points", K::points, 0, 1, {}, {}, "[[in, out], ...]: 2 to 8 points the curve passes through, smoothly, from 0 to 1"},
};
constexpr P lift_gamma_gain_params[]{
    {"lift", K::triple, -1, 1, {0, 0, 0}, {}, "Raises (or sinks) the shadows, [red, green, blue] or one number"},
    {"gamma", K::triple, 0.1f, 5, {1, 1, 1}, {}, "The middle: over 1 brighter"},
    {"gain", K::triple, 0, 5, {1, 1, 1}, {}, "The highlights: a multiplier"},
};
constexpr P white_balance_params[]{
    {"gain", K::triple, 0, 4, {1, 1, 1}, {}, "[red, green, blue] multipliers"},
    {"temperature", K::number, -1, 1, number(0), {}, "Warmer (over 0) or cooler"},
    {"tint", K::number, -1, 1, number(0), {}, "Magenta (over 0) or green"},
    {"drift", K::number, 0, 0.5f, number(0), {}, "How far the auto white balance wanders warm and cool, as a gain (0.035 is 3.5%)"},
    {"drift_seconds", K::pair, 1, 600, {37, 11, 0}, {}, "Its two wanders' periods: a slow one and a quicker one"},
};
constexpr P hue_saturation_params[]{
    {"hue", K::number, -180, 180, number(0), {}, "Turns every colour round the colour wheel, degrees"},
    {"saturation", K::number, 0, 4, number(1), {}, "0 grey, 1 as it was"},
    {"vibrance", K::number, -1, 1, number(0), {}, "Saturation for the dull colours, leaving the strong ones"},
    {"lightness", K::number, -1, 1, number(0), {}, "Toward white (over 0) or black"},
};
constexpr P channel_mixer_params[]{
    {"red", K::triple, -2, 2, {1, 0, 0}, {}, "The new red, as [red, green, blue] of the old"},
    {"green", K::triple, -2, 2, {0, 1, 0}, {}, "The new green"},
    {"blue", K::triple, -2, 2, {0, 0, 1}, {}, "The new blue"},
    {"offset", K::triple, -1, 1, {0, 0, 0}, {}, "Added to each"},
};
constexpr P monochrome_params[]{
    {"weights", K::triple, 0, 2, {0.2126f, 0.7152f, 0.0722f}, {}, "How much of [red, green, blue] makes the grey: a red filter is [0.9, 0.3, 0]"},
    {"colour", K::colour, 0, 255, white, {}, "The tone it is printed in: sepia is [255, 225, 180]"},
};
constexpr P split_tone_params[]{
    {"shadows", K::colour, 0, 255, {90.0f / 255, 140.0f / 255, 160.0f / 255}, {}, "The shadows' tint (128 on each is none)"},
    {"highlights", K::colour, 0, 255, {1, 190.0f / 255, 120.0f / 255}, {}, "The highlights' tint"},
    {"balance", K::number, -1, 1, number(0), {}, "Where shadows end and highlights start: over 0 more highlights"},
    {"amount", K::number, 0, 1, number(0.5f), {}, "How strong"},
};
constexpr P posterize_params[]{
    {"levels", K::number, 2, 256, number(8), {}, "Steps of each colour"},
    {"dither", K::number, 0, 1, number(0), {}, "Noise that breaks up the steps"},
};
constexpr P lut_params[]{
    {"file", K::file, 0, 0, {}, {}, "A .cube file beside the camera's (3D, up to 65 a side; graders and editors export them)"},
};
constexpr P flicker_params[]{
    {"amount", K::number, 0, 1, number(0.05f), {}, "How far the brightness swings"},
    {"speed", K::number, 0.1f, 60, number(8), {}, "Swings a second"},
    {"smooth", K::number, 0, 1, number(0.5f), {}, "0 a new brightness each picture, 1 a smooth wander"},
};
constexpr P weave_params[]{
    {"amount", K::number, 0, 50, number(1.5f), {}, "How far the picture wanders in the gate"},
    {"rotation", K::number, 0, 5, number(0), {}, "How far it turns, degrees"},
    {"speed", K::number, 0.05f, 20, number(1.5f), {}, "Wanders a second"},
    {"jump", K::number, 0, 200, number(0), {}, "A sudden jolt's size (a splice, a bad frame)"},
    {"jump_rate", K::number, 0, 10, number(0), {}, "Jolts a second"},
};
constexpr P line_wobble_params[]{
    {"amount", K::number, 0, 40, number(1.5f), {}, "How far lines shift sideways (a tape's timing error)"},
    {"wavelength", K::number, 1, 960, number(60), {}, "The wave's length, in lines of a 480-line picture"},
    {"speed", K::number, -20, 20, number(1), {}, "Waves a second rolling through"},
    {"jitter", K::number, 0, 1, number(0.3f), {}, "Each line's own random shift, against the smooth wave"},
};
constexpr P tracking_params[]{
    {"amount", K::number, 0, 200, number(24), {}, "How far lines in the band are torn sideways"},
    {"position", K::number, 0, 1, number(0.85f), {}, "The band's middle, from the top"},
    {"height", K::number, 0.01f, 1, number(0.08f), {}, "The band's height, of the picture"},
    {"roll", K::number, -5, 5, number(0), {}, "Picture heights a second the band rolls (0: it stays)"},
    {"noise", K::number, 0, 1, number(0.5f), {}, "Snow in the band"},
    {"duty", K::number, 0, 1, number(1), {}, "The share of the time it shows: 1 always"},
    {"period", K::number, 0.5f, 60, number(6), {}, "Seconds between its comings and goings"},
};
constexpr P head_switch_params[]{
    {"height", K::number, 0, 0.2f, number(0.02f), {}, "The torn band at the bottom, of the picture's height"},
    {"amount", K::number, 0, 200, number(16), {}, "How far it tears sideways"},
    {"noise", K::number, 0, 1, number(0.3f), {}, "Snow in it"},
};
constexpr P dropouts_params[]{
    {"rate", K::number, 0, 200, number(2), {}, "Streaks a second"},
    {"length", K::number, 0.01f, 1, number(0.15f), {}, "A streak's length, of the picture's width"},
    {"thickness", K::number, 0.5f, 20, number(1), {}, "Lines of a 480-line picture"},
    {"colour", K::colour, 0, 255, white, {}, "The streaks' colour"},
};
constexpr P scanlines_params[]{
    {"amount", K::number, 0, 1, number(0.2f), {}, "How dark the gaps are"},
    {"lines", K::number, 16, 2160, number(240), {}, "Lines from top to bottom"},
    {"thickness", K::number, 0.05f, 0.95f, number(0.5f), {}, "The dark share of each line"},
    {"roll", K::number, -100, 100, number(0), {}, "Lines a second they creep"},
};
constexpr P trails_params[]{
    {"amount", K::number, 0, 0.98f, number(0.5f), {}, "How much of the last picture stays: a slow tube's or a CCD's lag"},
    {"mode", K::word, 0, 0, number(0), trail_words, "`blend` (everything lags) or `bright` (only lights leave trails)"},
};
constexpr P grain_params[]{
    {"amount", K::number, 0, 1, number(0.04f), {}, "0 none, 1 heavy"},
    {"size", K::number, 0.25f, 10, number(1), {}, "A grain's size (film: 2 or 3)"},
    {"colour", K::number, 0, 1, number(0), {}, "0 the same on every colour (a CCD's), 1 each colour its own (film, cheap tape)"},
    {"shadows", K::number, 0, 1, number(0), {}, "More of it in the dark parts, as video noise is"},
};
constexpr P dust_params[]{
    {"amount", K::number, 0, 50, number(3), {}, "Specks a picture"},
    {"size", K::number, 0.5f, 20, number(1.5f), {}, "A speck's size"},
    {"scratches", K::number, 0, 1, number(0.1f), {}, "The chance of a scratch down the picture"},
    {"tone", K::word, 0, 0, number(0), dust_words, "`dark` (dirt on the film), `light` (on a negative) or `both`"},
};
constexpr P vignette_params[]{
    {"amount", K::number, 0, 1, number(0.35f), {}, "How dark the edges go"},
    {"radius", K::number, 0.1f, 3, number(1), {}, "Where it starts, in half picture widths from the middle"},
    {"softness", K::number, 0.01f, 3, number(0.6f), {}, "How gradually"},
    {"roundness", K::number, 0, 1, number(1), {}, "1 a circle, 0 the picture's own shape"},
    {"colour", K::colour, 0, 255, black, {}, "What the edges go to"},
};
constexpr P light_leak_params[]{
    {"colour", K::colour, 0, 255, {1, 110.0f / 255, 40.0f / 255}, {}, "The leak's colour"},
    {"amount", K::number, 0, 3, number(0.5f), {}, "How bright"},
    {"size", K::number, 0.05f, 3, number(0.8f), {}, "How big, in half picture widths"},
    {"speed", K::number, 0, 5, number(0.1f), {}, "How fast it wanders and pulses"},
    {"edge", K::word, 0, 0, number(0), edge_words, "Where it comes in: `any`, `left`, `right`, `top` or `bottom`"},
};
constexpr P gate_params[]{
    {"radius", K::number, 0, 0.5f, number(0.06f), {}, "The corners' rounding, of the picture's height (a film gate's)"},
    {"softness", K::number, 0, 0.2f, number(0.01f), {}, "How soft its edge is"},
    {"inset", K::number, 0, 0.25f, number(0), {}, "How far in from the picture's edge"},
    {"colour", K::colour, 0, 255, black, {}, "Outside it"},
};
constexpr P distort_params[]{
    {"amount", K::number, -1, 1, number(0.1f), {}, "Barrel (over 0: the middle bulges) or pincushion (under 0)"},
    {"cubic", K::number, -1, 1, number(0), {}, "More of it toward the corners"},
    {"scale", K::number, 0.25f, 4, number(1), {}, "Zooms in (over 1) to hide the edges it pulls in"},
};
constexpr P chromatic_aberration_params[]{
    {"amount", K::number, -40, 40, number(2), {}, "How far red and blue part at the corners (a cheap lens's colour fringes)"},
};
constexpr P channel_shift_params[]{
    {"red", K::pair, -100, 100, {2, 0, 0}, {}, "Where red sits, [right, down]"},
    {"green", K::pair, -100, 100, {0, 0, 0}, {}, "Where green sits"},
    {"blue", K::pair, -100, 100, {-2, 0, 0}, {}, "Where blue sits"},
};

constexpr EffectKind library[]{
    {Effect::resolution, "resolution", Part::tape, resolution_params, "The format's raster: so many lines (and samples across), each an average"},
    {Effect::blur, "blur", Part::tape, blur_params, "Softens: everything, or brightness or colour alone"},
    {Effect::sharpen, "sharpen", Part::sharpen, sharpen_params, "Edge enhancement"},
    {Effect::bloom, "bloom", Part::tone, bloom_params, "Bright parts glow (or, tinted red, film's halation)"},
    {Effect::ghost, "ghost", Part::tape, ghost_params, "A faint echo of the picture beside it"},
    {Effect::ccd_smear, "ccd_smear", Part::tone, ccd_smear_params, "A bright light's streak up and down the picture, as on a CCD"},
    {Effect::tone, "tone", Part::tone, tone_params, "A small camera's tone: dense blacks, early white, blown highlights going white"},
    {Effect::levels, "levels", Part::tone, levels_params, "Black and white points, gamma, exposure and contrast"},
    {Effect::curves, "curves", Part::tone, curves_params, "A curve through up to 8 points"},
    {Effect::lift_gamma_gain, "lift_gamma_gain", Part::tone, lift_gamma_gain_params, "A colourist's shadows, middle and highlights, per colour"},
    {Effect::white_balance, "white_balance", Part::white_balance, white_balance_params, "The colour's balance, and an auto white balance that never settles"},
    {Effect::hue_saturation, "hue_saturation", Part::tone, hue_saturation_params, "Hue, saturation, vibrance and lightness"},
    {Effect::channel_mixer, "channel_mixer", Part::tone, channel_mixer_params, "Each colour made of the others: cross-processing, infrared, swaps"},
    {Effect::monochrome, "monochrome", Part::tone, monochrome_params, "Black and white (through a filter), or toned"},
    {Effect::split_tone, "split_tone", Part::tone, split_tone_params, "One tint in the shadows, another in the highlights"},
    {Effect::posterize, "posterize", Part::tone, posterize_params, "Colour in steps"},
    {Effect::invert, "invert", Part::tone, {}, "A negative"},
    {Effect::lut, "lut", Part::tone, lut_params, "A colour grade from a .cube file"},
    {Effect::flicker, "flicker", Part::always, flicker_params, "The brightness flickers, as a film projector's or a cheap auto exposure's"},
    {Effect::weave, "weave", Part::always, weave_params, "The picture wanders in the film gate, and jolts"},
    {Effect::line_wobble, "line_wobble", Part::tape, line_wobble_params, "Lines shift sideways: a worn tape's timing"},
    {Effect::tracking, "tracking", Part::tape, tracking_params, "A VHS tracking band: torn lines and snow"},
    {Effect::head_switch, "head_switch", Part::tape, head_switch_params, "The torn lines at the bottom of a VHS picture"},
    {Effect::dropouts, "dropouts", Part::tape, dropouts_params, "Short streaks where the tape lost its signal"},
    {Effect::scanlines, "scanlines", Part::tape, scanlines_params, "A TV's dark gaps between lines"},
    {Effect::trails, "trails", Part::always, trails_params, "Motion leaves trails"},
    {Effect::grain, "grain", Part::grain, grain_params, "Sensor noise or film grain"},
    {Effect::dust, "dust", Part::grain, dust_params, "Dust and scratches on film"},
    {Effect::vignette, "vignette", Part::always, vignette_params, "Darker edges"},
    {Effect::light_leak, "light_leak", Part::always, light_leak_params, "Light leaking into a film camera, a warm glow that wanders"},
    {Effect::gate, "gate", Part::always, gate_params, "A film gate's rounded corners"},
    {Effect::distort, "distort", Part::always, distort_params, "A cheap lens's barrel or pincushion"},
    {Effect::chromatic_aberration, "chromatic_aberration", Part::always, chromatic_aberration_params, "Colour fringes toward the corners"},
    {Effect::channel_shift, "channel_shift", Part::tape, channel_shift_params, "Red, green and blue out of register"},
};
static_assert(static_cast<int>(std::size(library)) == effect_types, "every type in the library, in order");
constexpr std::string_view part_words[]{"always", "tone", "white_balance", "sharpen", "tape", "grain"};

// ---- Noise over time ---------------------------------------------------------------------------
// -1 to 1, the same for the same numbers.
float lattice(std::int64_t i, std::uint32_t salt) {
    auto h = static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ull ^ (static_cast<std::uint64_t>(salt) + 1) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 29;
    return static_cast<float>(h >> 40) / static_cast<float>(1 << 23) - 1;
}
float unit_random(std::int64_t i, std::uint32_t salt) { return 0.5f * (lattice(i, salt) + 1); }
// Smooth value noise, -1 to 1, a new value each 1 of `t`.
float smooth_noise(double t, std::uint32_t salt) {
    const double whole = std::floor(t);
    const auto i = static_cast<std::int64_t>(whole);
    const auto f = static_cast<float>(t - whole);
    const float s = f * f * (3 - 2 * f);
    return lattice(i, salt) + (lattice(i + 1, salt) - lattice(i, salt)) * s;
}

std::uint64_t hash_floats(const std::vector<float> &values, std::uint64_t h = 1469598103934665603ull) {
    for (const float v : values) {
        std::uint32_t bits{};
        std::memcpy(&bits, &v, sizeof bits);
        for (int b = 0; b < 4; ++b) {
            h ^= (bits >> (8 * b)) & 0xFF;
            h *= 1099511628211ull;
        }
    }
    return h;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
// Up to `count` numbers from `line`; how many were read.
int read_numbers(std::string_view line, float *out, int count) {
    int n = 0;
    while (n < count) {
        line = trim(line);
        if (line.empty()) break;
        // from_chars takes no leading '+'.
        if (line.front() == '+') line.remove_prefix(1);
        double value{};
        const auto [end, error] = std::from_chars(line.data(), line.data() + line.size(), value);
        if (error != std::errc{} || !std::isfinite(value)) return -1;
        out[n++] = static_cast<float>(value);
        line.remove_prefix(static_cast<std::size_t>(end - line.data()));
    }
    return trim(line).empty() ? n : -1;
}

std::string number_text(float v) {
    char text[32];
    std::snprintf(text, sizeof text, "%g", static_cast<double>(v));
    return text;
}
std::string default_text(const EffectParam &p) {
    switch (p.kind) {
    case K::number: return number_text(p.fallback[0]);
    case K::pair: return "[" + number_text(p.fallback[0]) + ", " + number_text(p.fallback[1]) + "]";
    case K::triple: return "[" + number_text(p.fallback[0]) + ", " + number_text(p.fallback[1]) + ", " + number_text(p.fallback[2]) + "]";
    case K::colour: {
        std::string out = "[";
        for (int i = 0; i < 3; ++i) out += (i ? ", " : "") + std::to_string(static_cast<int>(std::lround(p.fallback[static_cast<std::size_t>(i)] * 255)));
        return out + "]";
    }
    case K::word: return "`\"" + std::string(p.words[static_cast<std::size_t>(p.fallback[0])]) + "\"`";
    case K::flag: return p.fallback[0] > 0.5f ? "`true`" : "`false`";
    case K::points: return "`[[0, 0], [1, 1]]`";
    case K::file: return "";
    }
    return "";
}
std::string range_text(const EffectParam &p) {
    switch (p.kind) {
    case K::number:
    case K::pair:
    case K::triple:
    case K::colour: return number_text(p.low) + " to " + number_text(p.high);
    case K::word: {
        std::string out;
        for (const auto w : p.words) out += (out.empty() ? "" : " ") + ("`" + std::string(w) + "`");
        return out;
    }
    case K::flag: return "`true` `false`";
    case K::points: return "2 to 8 points, 0 to 1";
    case K::file: return "a `.cube`";
    }
    return "";
}
} // namespace

std::size_t EffectParam::count() const noexcept {
    switch (kind) {
    case K::pair: return 2;
    case K::triple:
    case K::colour: return 3;
    case K::points: return 17;
    case K::file: return 0;
    case K::number:
    case K::word:
    case K::flag: break;
    }
    return 1;
}

bool EffectBlock::operator==(const EffectBlock &o) const {
    return type == o.type && part == o.part && mix == o.mix && values == o.values && file == o.file &&
           (lut ? lut->hash : 0) == (o.lut ? o.lut->hash : 0);
}

bool part_on(Part part, const DvLook &look) noexcept {
    switch (part) {
    case Part::tone: return look.highlights;
    case Part::white_balance: return look.white_balance;
    case Part::sharpen: return look.sharpen;
    case Part::tape: return look.chroma;
    case Part::grain: return look.grain;
    case Part::always: break;
    }
    return true;
}

std::span<const EffectKind> effect_library() noexcept { return library; }
const EffectKind &effect_kind(Effect type) noexcept {
    const int i = std::clamp(static_cast<int>(type), 1, effect_types) - 1;
    return library[i];
}
const EffectKind *find_effect(std::string_view name) noexcept {
    for (const auto &kind : library)
        if (kind.name == name) return &kind;
    return nullptr;
}
std::string_view part_word(Part part) noexcept { return part_words[static_cast<std::size_t>(part)]; }
bool part_from_word(std::string_view word, Part &out) noexcept {
    for (std::size_t i = 0; i < std::size(part_words); ++i)
        if (part_words[i] == word) {
            out = static_cast<Part>(i);
            return true;
        }
    return false;
}

std::optional<std::size_t> param_offset(const EffectKind &kind, std::string_view key) noexcept {
    std::size_t at = 0;
    for (const auto &p : kind.params) {
        if (p.key == key) return p.kind == K::file ? std::nullopt : std::optional<std::size_t>(at);
        at += p.count();
    }
    return std::nullopt;
}

EffectBlock effect_block(Effect type) {
    EffectBlock block;
    block.type = type;
    const auto &kind = effect_kind(type);
    block.part = kind.part;
    std::size_t at = 0;
    for (const auto &p : kind.params) {
        if (p.kind == K::points) {
            block.values[at] = 2; // the straight line
            block.values[at + 1] = 0;
            block.values[at + 2] = 0;
            block.values[at + 3] = 1;
            block.values[at + 4] = 1;
        } else {
            for (std::size_t i = 0; i < p.count(); ++i) block.values[at + i] = p.fallback[i];
        }
        at += p.count();
    }
    return block;
}

float effect_value(const EffectBlock &block, std::string_view key, std::size_t index) noexcept {
    const auto at = param_offset(effect_kind(block.type), key);
    return at && *at + index < effect_values ? block.values[*at + index] : 0.0f;
}
void set_effect_value(EffectBlock &block, std::string_view key, float value, std::size_t index) noexcept {
    const auto at = param_offset(effect_kind(block.type), key);
    if (at && *at + index < effect_values) block.values[*at + index] = value;
}

std::optional<Lut3d> read_cube(std::string_view text, std::string &error) {
    Lut3d lut;
    std::size_t expected = 0;
    lut.rgb.clear();
    int line_number = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = trim(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++line_number;
        if (line.empty() || line.front() == '#') continue;
        const auto word_end = line.find_first_of(" \t");
        const auto word = line.substr(0, word_end);
        const auto rest = word_end == std::string_view::npos ? std::string_view() : line.substr(word_end);
        float numbers[3]{};
        if (word == "TITLE") {
            auto title = trim(rest);
            if (title.size() >= 2 && title.front() == '"' && title.back() == '"') title = title.substr(1, title.size() - 2);
            lut.title = std::string(title.substr(0, 64));
        } else if (word == "LUT_3D_SIZE") {
            if (read_numbers(rest, numbers, 1) != 1 || numbers[0] < 2 || numbers[0] > 65 || numbers[0] != std::floor(numbers[0])) {
                error = "LUT_3D_SIZE should be a whole number from 2 to 65.";
                return std::nullopt;
            }
            lut.size = static_cast<int>(numbers[0]);
            const auto side = static_cast<std::size_t>(lut.size);
            expected = side * side * side;
            lut.rgb.reserve(expected * 3);
        } else if (word == "LUT_1D_SIZE") {
            error = "a 1D table; only 3D ones (LUT_3D_SIZE) are read.";
            return std::nullopt;
        } else if (word == "DOMAIN_MIN" || word == "DOMAIN_MAX") {
            if (read_numbers(rest, numbers, 3) != 3) {
                error = std::string(word) + " should be three numbers.";
                return std::nullopt;
            }
            auto &domain = word == "DOMAIN_MIN" ? lut.low : lut.high;
            domain = {numbers[0], numbers[1], numbers[2]};
        } else if (word == "LUT_3D_INPUT_RANGE") {
            if (read_numbers(rest, numbers, 2) != 2) {
                error = "LUT_3D_INPUT_RANGE should be two numbers.";
                return std::nullopt;
            }
            lut.low = {numbers[0], numbers[0], numbers[0]};
            lut.high = {numbers[1], numbers[1], numbers[1]};
        } else if ((word.front() >= '0' && word.front() <= '9') || word.front() == '-' || word.front() == '.' || word.front() == '+') {
            if (!lut.size) {
                error = "the colours start before LUT_3D_SIZE says how many there are.";
                return std::nullopt;
            }
            if (read_numbers(line, numbers, 3) != 3) {
                error = "line " + std::to_string(line_number) + " should be three numbers.";
                return std::nullopt;
            }
            if (lut.rgb.size() >= expected * 3) {
                error = "more colours than LUT_3D_SIZE says.";
                return std::nullopt;
            }
            lut.rgb.insert(lut.rgb.end(), numbers, numbers + 3);
        }
        // Other keywords (LUT_IN_VIDEO_RANGE and the like, a grader's own) are passed over.
    }
    if (!lut.size) {
        error = "no LUT_3D_SIZE: not a 3D .cube.";
        return std::nullopt;
    }
    if (lut.rgb.size() != expected * 3) {
        error = std::to_string(lut.rgb.size() / 3) + " colours where LUT_3D_SIZE " + std::to_string(lut.size) + " needs " +
                std::to_string(expected) + ".";
        return std::nullopt;
    }
    for (int i = 0; i < 3; ++i)
        if (!(lut.high[static_cast<std::size_t>(i)] > lut.low[static_cast<std::size_t>(i)])) {
            error = "DOMAIN_MAX should be over DOMAIN_MIN.";
            return std::nullopt;
        }
    std::vector<float> header{static_cast<float>(lut.size), lut.low[0], lut.low[1], lut.low[2], lut.high[0], lut.high[1], lut.high[2]};
    lut.hash = hash_floats(lut.rgb, hash_floats(header));
    return lut;
}

EffectStack stack_from_look(const LookProfile &look, int lines) {
    EffectStack stack;
    const auto add = [&](Effect type) -> EffectBlock & { return stack.emplace_back(effect_block(type)); };
    if (lines > 0) set_effect_value(add(Effect::resolution), "lines", static_cast<float>(std::clamp(lines, 120, 2160)));
    if (look.chroma_width > 1) {
        auto &chroma = add(Effect::blur);
        set_effect_value(chroma, "radius", std::clamp(look.chroma_width, 0.0f, 16.0f) * 0.5f);
        set_effect_value(chroma, "direction", 0);
        set_effect_value(chroma, "channels", 2);
        set_effect_value(chroma, "shape", 1);
    }
    if (look.softness > 0) {
        auto &soft = add(Effect::blur);
        set_effect_value(soft, "radius", std::clamp(look.softness, 0.0f, 8.0f));
        set_effect_value(soft, "direction", 0);
        set_effect_value(soft, "channels", 1);
        set_effect_value(soft, "shape", 1);
    }
    if (look.sharpen > 0) {
        auto &sharp = add(Effect::sharpen);
        set_effect_value(sharp, "amount", std::clamp(look.sharpen, 0.0f, 2.0f));
        set_effect_value(sharp, "radius", std::clamp(look.sharpen_radius, 0.25f, 8.0f));
    }
    if (look.white_balance_drift > 0) {
        auto &balance = add(Effect::white_balance);
        set_effect_value(balance, "drift", std::clamp(look.white_balance_drift, 0.0f, 0.5f));
        set_effect_value(balance, "drift_seconds", look.drift_seconds[0], 0);
        set_effect_value(balance, "drift_seconds", look.drift_seconds[1], 1);
    }
    auto &tone = add(Effect::tone);
    set_effect_value(tone, "black_crush", std::clamp(look.black_crush, 0.0f, 0.5f));
    set_effect_value(tone, "gain", std::clamp(look.highlight_gain, 0.5f, 2.0f));
    set_effect_value(tone, "knee", std::clamp(look.highlight_knee, 0.5f, 1.0f));
    set_effect_value(tone, "spread", std::clamp(look.highlight_spread, 0.005f, 0.5f));
    set_effect_value(tone, "bleed", look.highlight_gain > 1 ? 1.0f : 0.0f);
    if (look.saturation != 1) set_effect_value(add(Effect::hue_saturation), "saturation", std::clamp(look.saturation, 0.0f, 2.0f));
    if (look.grain > 0) {
        auto &grain = add(Effect::grain);
        set_effect_value(grain, "amount", std::clamp(look.grain, 0.0f, 1.0f));
        set_effect_value(grain, "size", std::clamp(look.grain_size, 0.25f, 8.0f));
        set_effect_value(grain, "colour", std::clamp(look.grain_colour, 0.0f, 1.0f));
    }
    return stack;
}

LookProfile classic_look(const EffectStack &stack, LookProfile base) {
    auto look = base;
    look.black_crush = 0;
    look.highlight_gain = 1;
    look.saturation = 1;
    look.white_balance_drift = 0;
    look.sharpen = 0;
    look.grain = 0;
    look.chroma_width = 0;
    look.softness = 0;
    for (const auto &b : stack) {
        const auto v = [&](std::string_view key, std::size_t i = 0) { return effect_value(b, key, i); };
        switch (b.type) {
        case Effect::tone:
            look.black_crush = v("black_crush");
            look.highlight_gain = v("gain");
            look.highlight_knee = v("knee");
            look.highlight_spread = v("spread");
            break;
        case Effect::hue_saturation: look.saturation *= v("saturation"); break;
        case Effect::monochrome: look.saturation = 0; break;
        case Effect::white_balance:
            look.white_balance_drift = v("drift");
            look.drift_seconds = {v("drift_seconds", 0), v("drift_seconds", 1)};
            break;
        case Effect::sharpen:
            look.sharpen = v("amount");
            look.sharpen_radius = v("radius");
            break;
        case Effect::grain:
            look.grain = v("amount");
            look.grain_size = v("size");
            look.grain_colour = v("colour");
            break;
        case Effect::blur:
            if (v("channels") == 2) look.chroma_width = v("radius") * 2;
            else look.softness = v("radius");
            break;
        default: break;
        }
    }
    return look;
}

std::string effect_reference() {
    std::string out;
    for (const auto &kind : library) {
        out += "#### `" + std::string(kind.name) + "`\n\n" + std::string(kind.what) + ". Part: `" + std::string(part_word(kind.part)) + "`.\n\n";
        if (kind.params.empty()) {
            out += "No numbers of its own (`mix` sets how much).\n\n";
            continue;
        }
        out += "| Field | Default | Range | What it is |\n|---|---|---|---|\n";
        for (const auto &p : kind.params)
            out += "| `" + std::string(p.key) + "` | " + default_text(p) + " | " + range_text(p) + " | " + std::string(p.what) + " |\n";
        out += "\n";
    }
    return out;
}

std::vector<EffectPass> plan_effects(const EffectStack &stack, const DvLook &look, const EffectTime &time) {
    std::vector<EffectPass> passes;
    if (!look.on) return passes;
    const double seconds = std::isfinite(time.seconds) ? time.seconds : 0;
    const double rate = time.frame_rate > 1 ? time.frame_rate : 30;
    const auto frame = static_cast<float>(time.frame % 65536);
    const float seed = std::clamp(time.seed, 0.0f, 1.0f);
    bool history_taken = false;
    std::uint32_t index = 0;
    for (const auto &block : stack) {
        ++index;
        if (!part_on(block.part, look) || !(block.mix > 0)) continue;
        if (block.type == Effect::lut && !block.lut) continue;
        const int program = 8 * static_cast<int>(block.type);
        EffectPass pass;
        pass.program = program;
        pass.mix = std::clamp(block.mix, 0.0f, 1.0f);
        pass.values = block.values;
        auto &v = pass.values;
        const auto salt = index * 977u;
        // A blur along one direction (the shader's plain blur, or a block's own first or last pass).
        const auto along = [](EffectPass &p, float dx, float dy, float radius, bool box) {
            p.values[28] = dx;
            p.values[29] = dy;
            p.values[30] = radius;
            p.values[31] = box ? 1.0f : 0.0f;
        };
        switch (block.type) {
        case Effect::blur: {
            // [0] radius, [1] direction, [2] channels, [3] shape, [4] shift; [28..31] this pass's direction and radius.
            const float radius = v[0];
            const bool box = v[3] > 0.5f;
            if (radius <= 0) continue;
            if (v[1] > 1.5f) { // both: across, then down (finishing the block)
                EffectPass first;
                first.program = effect_blur_program;
                first.write = Slot::scratch_a;
                along(first, 1, 0, radius, box);
                passes.push_back(first);
                pass.read = Slot::scratch_a;
                pass.other = Slot::current;
                pass.mix_with_other = true;
                along(pass, 0, 1, radius, box);
            } else {
                along(pass, v[1] < 0.5f ? 1.0f : 0.0f, v[1] < 0.5f ? 0.0f : 1.0f, radius, box);
            }
            break;
        }
        case Effect::bloom: {
            // Bright parts blurred twice over (a smoother glow than once, wider), then added: [0]
            // threshold, [1] amount, [2] radius, [3..5] colour.
            const float r = v[2] / std::sqrt(2.0f);
            EffectPass bright = pass;
            bright.write = Slot::scratch_a;
            along(bright, 1, 0, r, false);
            passes.push_back(bright);
            const Slot route[3][2]{{Slot::scratch_a, Slot::scratch_b}, {Slot::scratch_b, Slot::scratch_a}, {Slot::scratch_a, Slot::scratch_b}};
            for (int i = 0; i < 3; ++i) {
                EffectPass blur;
                blur.program = effect_blur_program;
                blur.read = route[i][0];
                blur.write = route[i][1];
                along(blur, i % 2 == 0 ? 0.0f : 1.0f, i % 2 == 0 ? 1.0f : 0.0f, r, false);
                passes.push_back(blur);
            }
            pass.program = program + 1;
            pass.other = Slot::scratch_b;
            break;
        }
        case Effect::curves: {
            // [0] channel, [1] points, [2..17] in, out pairs; [18..25] the slopes there (a smooth
            // curve that never overshoots between points: Fritsch-Carlson).
            const int n = std::clamp(static_cast<int>(v[1]), 2, 8);
            std::array<float, 8> x{}, y{}, d{}, m{};
            for (int i = 0; i < n; ++i) {
                x[static_cast<std::size_t>(i)] = v[static_cast<std::size_t>(2 + 2 * i)];
                y[static_cast<std::size_t>(i)] = v[static_cast<std::size_t>(3 + 2 * i)];
            }
            for (int i = 0; i + 1 < n; ++i) {
                const auto k = static_cast<std::size_t>(i);
                d[k] = x[k + 1] > x[k] ? (y[k + 1] - y[k]) / (x[k + 1] - x[k]) : 0.0f;
            }
            m[0] = d[0];
            m[static_cast<std::size_t>(n - 1)] = d[static_cast<std::size_t>(n - 2)];
            for (int i = 1; i + 1 < n; ++i) {
                const auto k = static_cast<std::size_t>(i);
                m[k] = d[k - 1] * d[k] <= 0 ? 0.0f : (d[k - 1] + d[k]) * 0.5f;
            }
            for (int i = 0; i + 1 < n; ++i) {
                const auto k = static_cast<std::size_t>(i);
                if (d[k] == 0) {
                    m[k] = m[k + 1] = 0;
                    continue;
                }
                const float a = m[k] / d[k], b = m[k + 1] / d[k], h = a * a + b * b;
                if (h > 9) {
                    const float t = 3 / std::sqrt(h);
                    m[k] = t * a * d[k];
                    m[k + 1] = t * b * d[k];
                }
            }
            v[1] = static_cast<float>(n);
            for (int i = 0; i < 8; ++i) v[static_cast<std::size_t>(18 + i)] = m[static_cast<std::size_t>(i)];
            break;
        }
        case Effect::white_balance: {
            // [24..26] this moment's gains: the balance, the temperature and tint, and the drift.
            const float drift = v[5];
            float cast = 0;
            if (drift > 0) {
                const double phase = seed * tau;
                const double slow = std::clamp(static_cast<double>(v[6]), 1.0, 3600.0), quick = std::clamp(static_cast<double>(v[7]), 1.0, 3600.0);
                cast = static_cast<float>(drift * std::sin(seconds * tau / slow + phase) + 0.57 * drift * std::sin(seconds * tau / quick + 2.1 * phase));
            }
            const float warm = v[3] * 0.25f, tint = v[4] * 0.2f;
            v[24] = v[0] * (1 + warm) * (1 + cast);
            v[25] = v[1] * (1 - tint);
            v[26] = v[2] * (1 - warm) * (1 - cast);
            break;
        }
        case Effect::lut: {
            // [0] entries a side, [1..3] the domain's low end, [4..6] its high end.
            v = {};
            v[0] = static_cast<float>(block.lut->size);
            for (int i = 0; i < 3; ++i) {
                v[static_cast<std::size_t>(1 + i)] = block.lut->low[static_cast<std::size_t>(i)];
                v[static_cast<std::size_t>(4 + i)] = block.lut->high[static_cast<std::size_t>(i)];
            }
            pass.lut = block.lut;
            break;
        }
        case Effect::flicker: {
            // [24] this moment's brightness.
            const double t = seconds * v[1];
            const float jumpy = lattice(time.frame, salt), smooth = smooth_noise(t, salt);
            v[24] = 1 + v[0] * (jumpy + (smooth - jumpy) * std::clamp(v[2], 0.0f, 1.0f));
            break;
        }
        case Effect::weave: {
            // [24, 25] this moment's offset, [26] its turn (radians).
            const double t = seconds * v[2];
            float dx = v[0] * smooth_noise(t, salt), dy = v[0] * 0.6f * smooth_noise(t * 0.8, salt + 1);
            if (v[4] > 0) {
                // A jolt: picked once a picture with the rate's chance, lasting that one picture.
                const float chance = static_cast<float>(v[4] / rate);
                if (unit_random(time.frame, salt + 2) < chance) {
                    dx += v[3] * lattice(time.frame, salt + 3) * 0.3f;
                    dy += v[3] * lattice(time.frame, salt + 4);
                }
            }
            v[24] = dx;
            v[25] = dy;
            v[26] = v[1] * to_radians * smooth_noise(t * 0.7, salt + 5);
            break;
        }
        case Effect::line_wobble:
            // [24] the wave's phase now, [25] the picture (for the jitter).
            v[24] = static_cast<float>(std::fmod(seconds * v[2], 1.0) * tau);
            v[25] = frame;
            break;
        case Effect::tracking: {
            // [24] the band's middle now, [25] how much of it shows now (0 to 1), [26] the picture.
            const double centre = v[1] + seconds * v[3];
            v[24] = static_cast<float>(centre - std::floor(centre));
            float shown = 1;
            if (v[5] < 1) {
                // On for `duty` of each period, easing in and out over a fifth of a second.
                const double period = std::max(0.5, static_cast<double>(v[6]));
                const double cycle = std::fmod(seconds + seed * period, period) / period;
                const double on = v[5], ease = std::min(0.5, 0.2 / period);
                shown = cycle < on ? static_cast<float>(std::clamp(std::min(cycle, on - cycle) / std::max(ease, 1e-3), 0.0, 1.0)) : 0.0f;
            }
            v[25] = shown;
            v[26] = frame;
            if (shown <= 0) continue;
            break;
        }
        case Effect::head_switch: v[24] = frame; break;
        case Effect::dropouts: {
            // [24] the chance a line band has a streak this picture, [25] the picture.
            const float bands = std::max(1.0f, 480 / std::max(v[2], 0.5f));
            v[24] = static_cast<float>(v[0] / rate) / bands;
            v[25] = frame;
            break;
        }
        case Effect::scanlines:
            // [24] how far they have crept, in lines.
            v[24] = static_cast<float>(std::fmod(seconds * v[3], 1.0));
            break;
        case Effect::posterize: v[24] = frame; break;
        case Effect::grain:
            // [24] the picture, [25] this block's own noise.
            v[24] = frame;
            v[25] = static_cast<float>(index);
            break;
        case Effect::dust: {
            // [24] the picture, [25, 26] this picture's scratches across (-1 none), [27] this block's own noise.
            v[24] = frame;
            for (int i = 0; i < 2; ++i) {
                const bool scratch = unit_random(time.frame, salt + 10 + static_cast<std::uint32_t>(i)) < v[2] * (i ? 0.4f : 1.0f);
                // A scratch stays put for a while (the film runs along it), wandering a little.
                const double slot = std::floor(seconds * 0.7 + i * 0.37);
                const float across = unit_random(static_cast<std::int64_t>(slot), salt + 20 + static_cast<std::uint32_t>(i)) * 0.9f + 0.05f +
                                     0.004f * lattice(time.frame, salt + 30);
                v[static_cast<std::size_t>(25 + i)] = scratch ? across : -1.0f;
            }
            v[27] = static_cast<float>(index);
            break;
        }
        case Effect::light_leak: {
            // [24, 25] its middle now (picture fractions, often outside), [26] its brightness now.
            // [0..2] colour, [3] amount, [4] size, [5] speed, [6] edge.
            const double t = seconds * std::max(v[5], 0.0f);
            const int edge = static_cast<int>(v[6]);
            const float wander = 0.5f + 0.45f * smooth_noise(t * 0.5, salt);
            const float out = 1.05f + 0.15f * smooth_noise(t * 0.3, salt + 1);
            const int side = edge > 0 ? edge : 1 + static_cast<int>(unit_random(static_cast<std::int64_t>(std::floor(t * 0.1)), salt + 2) * 3.999f);
            float cx = wander, cy = wander;
            if (side == 1) cx = 1 - out;      // left
            else if (side == 2) cx = out;     // right
            else if (side == 3) cy = 1 - out; // top
            else cy = out;                    // bottom
            v[24] = cx;
            v[25] = cy;
            v[26] = v[3] * std::clamp(0.55f + 0.45f * smooth_noise(t * 1.3, salt + 3), 0.0f, 1.0f);
            break;
        }
        case Effect::trails:
            pass.other = Slot::history;
            pass.keep = !history_taken;
            if (history_taken) continue; // one picture a moment ago: the first trails block's
            history_taken = true;
            break;
        default: break;
        }
        passes.push_back(pass);
    }
    return passes;
}
} // namespace dingosdk::camcorder
