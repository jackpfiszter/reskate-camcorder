#pragma once
// A camera's picture as a stack of effects: building blocks from a fixed library, in the order a
// camera file lists them, each with its own numbers. The blocks are the camcorder's own (drawn by
// its shader, camcorder_warp.cpp); a mod picks, orders and tunes them, as data: nothing in a mod
// runs.
//
//   "effects": [
//     {"type": "resolution", "lines": 480},
//     {"type": "blur", "channels": "chroma", "direction": "horizontal", "radius": 2},
//     {"type": "tone", "black_crush": 0.05, "gain": 1.15},
//     {"type": "grain", "amount": 0.04}
//   ]
//
// The picture runs through them top to bottom, after the lens and the camera light: each block
// takes the picture the one above it left. Sizes are in pixels of a 720-wide picture (so a block
// looks the same at any resolution), places in fractions of the picture (0 to 1, from the top
// left), colours [red, green, blue] from 0 to 255, times in seconds. Every block also takes
// "mix" (0 to 1: how much of its change is kept) and "part" (which of the player's look switches
// turns it off; each type has its own by default).
//
// A camera that gives no "effects" gets them from its look's numbers (stack_from_look): the
// VX1000's MiniDV picture is such a stack. docs/MODDING.md lists every block.
#include "look.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::camcorder {
// The library. The numbers are the shader's (camcorder_warp.cpp): keep them in step.
enum class Effect : std::uint8_t {
    // The picture's grain of detail: its raster, blur, edges, glow.
    resolution = 1, blur, sharpen, bloom, ghost, ccd_smear,
    // Tone and colour.
    tone, levels, curves, lift_gamma_gain, white_balance, hue_saturation, channel_mixer, monochrome, split_tone,
    posterize, invert, lut,
    // Over time.
    flicker, weave, line_wobble, tracking, head_switch, dropouts, scanlines, trails,
    // Texture and the frame.
    grain, dust, vignette, light_leak, gate, distort, chromatic_aberration, channel_shift,
};
inline constexpr int effect_types = static_cast<int>(Effect::channel_shift);

// Which of the player's look switches (DvLook) a block follows. `always`: only the whole look's.
enum class Part : std::uint8_t { always, tone, white_balance, sharpen, tape, grain };
bool part_on(Part, const DvLook &) noexcept;

// A 3D colour table from a .cube file (Resolve, Premiere, Photoshop and most graders write them).
struct Lut3d {
    int size{};                          // entries a side: 2 to 65
    std::array<float, 3> low{0, 0, 0}, high{1, 1, 1}; // DOMAIN_MIN and DOMAIN_MAX
    std::vector<float> rgb;              // size cubed entries of red, green, blue; red runs fastest
    std::uint64_t hash{};                // of the above, to tell tables apart
    std::string title;
};
// Reads a .cube's text. Nothing (and `error` says why) when it is not one.
std::optional<Lut3d> read_cube(std::string_view text, std::string &error);

inline constexpr std::size_t effect_values = 32;
struct EffectBlock {
    Effect type{Effect::tone};
    Part part{Part::always};
    float mix{1};
    // The type's parameters, in the library's order (effect_kind(type).params), each its own count
    // of values: a colour is 3, from 0 to 1.
    std::array<float, effect_values> values{};
    std::string file;                 // a lut's .cube, beside the camera's file
    std::shared_ptr<const Lut3d> lut; // its table, once read
    bool operator==(const EffectBlock &) const;
};
using EffectStack = std::vector<EffectBlock>;
inline constexpr std::size_t most_effects = 32;

// The library's description of a type, for reading, writing and the docs.
enum class ParamKind : std::uint8_t {
    number,
    pair,   // [a, b]
    triple, // [a, b, c]; one number sets all three
    colour, // [red, green, blue] from 0 to 255; kept as 0 to 1
    word,   // one of `words`; kept as its index
    flag,   // true or false; kept as 1 or 0
    points, // [[in, out], ...]: 2 to 8 points of a curve, each from 0 to 1; kept as their count, then pairs
    file,   // a file name beside the camera's file; kept in EffectBlock::file
};
struct EffectParam {
    std::string_view key;
    ParamKind kind{};
    float low{}, high{};
    std::array<float, 3> fallback{};
    std::span<const std::string_view> words{};
    std::string_view what;
    std::size_t count() const noexcept;
};
struct EffectKind {
    Effect type{};
    std::string_view name;
    Part part{};
    std::span<const EffectParam> params;
    std::string_view what;
};
std::span<const EffectKind> effect_library() noexcept;
const EffectKind &effect_kind(Effect) noexcept;
const EffectKind *find_effect(std::string_view name) noexcept;
std::string_view part_word(Part) noexcept;
bool part_from_word(std::string_view, Part &) noexcept;
// Where a parameter's values start in EffectBlock::values (none for a file).
std::optional<std::size_t> param_offset(const EffectKind &, std::string_view key) noexcept;
// A block of `type` with every parameter at its default.
EffectBlock effect_block(Effect type);
// A parameter's value (its first, or its `index`th of a pair, triple or colour); 0 when unknown.
float effect_value(const EffectBlock &, std::string_view key, std::size_t index = 0) noexcept;
void set_effect_value(EffectBlock &, std::string_view key, float value, std::size_t index = 0) noexcept;

// The stack a camera's look numbers make (the VX1000's MiniDV), with `lines` its own raster.
EffectStack stack_from_look(const LookProfile &, int lines);
// A stack's nearest classic look, for a recording made where the screen cannot show the stack (the
// recording's own look then does what it can: tone, colour, white balance, sharpening, grain, tape).
LookProfile classic_look(const EffectStack &, LookProfile base = {});
// The markdown table of every block and parameter, for docs/MODDING.md and `camcorder effects`.
std::string effect_reference();

// ---- One frame's drawing ----------------------------------------------------------------------
// The shader's textures a pass reads and writes: the picture so far (until a block's last pass
// writes the next one, it is also the picture as the block started), two spares for blocks drawn
// in several passes, the picture a moment ago (trails), and the next picture.
enum class Slot : std::uint8_t { current, scratch_a, scratch_b, history, next };
struct EffectPass {
    int program{};       // the shader's: 8 x the type + the pass's step; effect_blur_program for a plain blur
    Slot read{Slot::current}, other{Slot::current}, write{Slot::next};
    float mix{1};
    bool mix_with_other{}; // the block's own picture is `other` (true) or `read` (false)
    bool keep{};           // copy what this pass draws into the history
    std::array<float, effect_values> values{};
    std::shared_ptr<const Lut3d> lut;
};
inline constexpr int effect_blur_program = 4;
struct EffectTime {
    double seconds{};        // into the clip (or since the camera came on)
    std::int64_t frame{};    // pictures shown so far
    double frame_rate{30};   // pictures a second
    float seed{0.37f};       // the clip's own, 0 to 1
};
// The passes that draw `stack` this frame, with the player's switched-off parts left out and what
// moves over time worked out for this moment. Empty while the look is off.
std::vector<EffectPass> plan_effects(const EffectStack &, const DvLook &, const EffectTime &);
} // namespace dingosdk::camcorder
