#pragma once
// The camcorder's picture shader (camcorder_warp.cpp draws with it): the lens, the camera light, and
// every effect in the library (camcorder/effects.h), each a program of one pixel shader, drawn pass
// after pass. Compiled in the game by D3DCompile as Shader Model 5.0; CI compiles it with fxc.
//
// Programs: 0 the lens (the game's frame bent through the lens, into the look's values), 1 the
// camera light, 2 the picture back out to the screen, 4 a plain blur along one direction, and from
// 8 on the effects: 8 x the effect's number (effects.h, Effect) + the pass's step.
//
// The look's values are an SDR camera's: sRGB-encoded, white 1, black 0. Sizes in the numbers are in
// pixels of a 720-wide picture (`clock.z` screen pixels each), places in fractions of the picture.
//
// (MSVC keeps a string literal under 16 KB: the shader is in pieces, joined by the compiler. Names
// avoid what fxc reserves: no pass, line, point, sample, shared, linear, texture, sampler, step.)

namespace dingosdk::overlay {
inline constexpr char camcorder_picture_shader[] = R"hlsl(
cbuffer Constants : register(b0) {
    float4 rect;   // the picture in the buffer: left, top, width, height (pixels)
    float4 screen; // the buffer: width, height, 1 / width, 1 / height
    float4 clock;  // the picture's number, seconds, pixels per 720th, transfer (0 SDR, 1 scRGB, 2 HDR10)
    float4 job;    // program, mix, mix with `other` (1) or `current` (0), white (nits, HDR)
    float4 v[8];   // the program's numbers
};
Texture2D current : register(t0);
Texture2D other : register(t1);
Texture3D grade : register(t2);
SamplerState smooth_clamp : register(s0);
SamplerState nearest_clamp : register(s1);

float val(int i) { return v[i >> 2][i & 3]; }

float4 picture_vs(uint id : SV_VertexID) : SV_Position {
    // One triangle over the whole buffer; the scissor keeps it to the picture.
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

// ---- Places ----
float unit_px() { return max(clock.z, 1e-3); }
// Kept inside the picture: what lies outside it is not the picture.
float2 inside(float2 p) { return clamp(p, rect.xy + 0.5, rect.xy + rect.zw - 0.5); }
float3 at(float2 p) { return current.SampleLevel(smooth_clamp, inside(p) * screen.zw, 0).rgb; }
float3 at_other(float2 p) { return other.SampleLevel(smooth_clamp, inside(p) * screen.zw, 0).rgb; }
// 0 to 1 across the picture, from the top left.
float2 picture_uv(float2 p) { return (p - rect.xy) / rect.zw; }
// In half picture widths from the middle, y up.
float2 centred(float2 p) {
    return float2((p.x - rect.x) / rect.z * 2 - 1, (0.5 - (p.y - rect.y) / rect.w) * 2 * rect.w / rect.z);
}
float2 from_centred(float2 c) {
    return float2(rect.x + (c.x + 1) * 0.5 * rect.z, rect.y + rect.w * 0.5 - c.y * 0.5 * rect.z);
}
bool in_picture(float2 c) {
    float top = rect.w / rect.z;
    return abs(c.x) <= 1 && abs(c.y) <= top;
}

// ---- Values ----
float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
uint mix_bits(uint h) {
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
// 0 to 1, the same for the same three whole numbers.
float random3(float a, float b, float c) {
    uint h = (uint)(int)a * 0x85EBCA6Bu ^ (uint)(int)b * 0xC2B2AE35u ^ (uint)(int)c * 0x9E3779B9u;
    return (float)(mix_bits(h) & 0xFFFFFFu) / 16777216.0;
}

float3 srgb_encode(float3 l) {
    l = saturate(l);
    return lerp(12.92 * l, 1.055 * pow(l, 1 / 2.4) - 0.055, step(0.0031308, l));
}
float3 srgb_decode(float3 e) {
    e = saturate(e);
    return lerp(e / 12.92, pow((e + 0.055) / 1.055, 2.4), step(0.04045, e));
}
static const float pq_m1 = 0.1593017578125, pq_m2 = 78.84375, pq_c1 = 0.8359375, pq_c2 = 18.8515625, pq_c3 = 18.6875;
float3 pq_to_nits(float3 c) {
    float3 p = pow(saturate(c), 1 / pq_m2);
    return 10000 * pow(max(p - pq_c1, 0) / (pq_c2 - pq_c3 * p), 1 / pq_m1);
}
float3 nits_to_pq(float3 nits) {
    float3 y = pow(saturate(nits / 10000), pq_m1);
    return pow((pq_c1 + pq_c2 * y) / (1 + pq_c3 * y), pq_m2);
}
// The screen's values into the look's (an SDR camera's), and back.
float3 into_look(float3 c) {
    float3 result = c;
    if (clock.w > 1.5) {
        float3 wide = pq_to_nits(c) / job.w;
        float3 lin = float3(dot(wide, float3(1.6605, -0.5876, -0.0728)), dot(wide, float3(-0.1246, 1.1329, -0.0083)),
                            dot(wide, float3(-0.0182, -0.1006, 1.1187)));
        result = srgb_encode(lin);
    } else if (clock.w > 0.5) {
        result = srgb_encode(c * (80 / job.w)); // scRGB: 1 is 80 nits
    }
    return result;
}
float3 out_of_look(float3 e) {
    float3 result = e;
    if (clock.w > 1.5) {
        float3 lin = srgb_decode(e);
        float3 wide = float3(dot(lin, float3(0.6274, 0.3293, 0.0433)), dot(lin, float3(0.0691, 0.9195, 0.0114)),
                             dot(lin, float3(0.0164, 0.0880, 0.8956)));
        result = nits_to_pq(wide * job.w);
    } else if (clock.w > 0.5) {
        result = srgb_decode(e) * (job.w / 80);
    }
    return result;
}

// ---- 0: the lens ----
// v[0]: bent (0 / 1), half the picture's width (radians), tan(half the render's height), render aspect.
// v[1]: the projection (0 equidistant, 1 equisolid, 2 stereographic, 3 orthographic), into the look (0 / 1).
float3 lens_program(float2 p) {
    float2 uv = p * screen.zw;
    if (v[0].x > 0.5) {
        float2 c = centred(p);
        float rho = length(c), half_width = v[0].y;
        float angle = rho * half_width;
        if (v[1].x > 2.5) angle = asin(min(rho * sin(min(half_width, 1.5707963)), 1));
        else if (v[1].x > 1.5) angle = 2 * atan(rho * tan(half_width * 0.5));
        else if (v[1].x > 0.5) angle = 2 * asin(min(rho * sin(half_width * 0.5), 1));
        float2 t = rho > 1e-6 ? c * (tan(min(angle, 1.4835298)) / rho) : float2(0, 0);
        uv = float2(0.5 + 0.5 * t.x / (v[0].z * v[0].w), 0.5 - 0.5 * t.y / v[0].z);
    }
    float3 c = current.SampleLevel(smooth_clamp, uv, 0).rgb;
    return v[1].y > 0.5 ? into_look(c) : c;
}

// ---- 1: the camera light ----
// v[0]: strength, reach (half picture widths). v[1]: colour (red, green, blue), in the look (0 / 1).
// Without the scene's depth it can only scale the light already there, in linear light: the middle
// of a night shot comes up and its brightest parts burn out, as under a real camera light.
float3 light_program(float2 p) {
    float3 c = current.Load(int3((int2)p, 0)).rgb;
    float r = length(centred(p)) / max(v[0].y, 0.05);
    float3 gain = 1 + v[0].x * exp(-r * r * 1.6) * v[1].rgb;
    float3 result;
    if (v[1].w > 0.5 || clock.w < 0.5) result = srgb_encode(srgb_decode(c) * gain); // the look's values, or SDR
    else if (clock.w < 1.5) result = c * gain;                                     // scRGB is linear already
    else result = nits_to_pq(pq_to_nits(c) * gain);                                // HDR10
    return result;
}

// ---- 2: out to the screen ----
// v[0].x: from the look's values (1) or as they are (0).
float3 output_program(float2 p) {
    float3 c = current.Load(int3((int2)p, 0)).rgb;
    return v[0].x > 0.5 ? out_of_look(saturate(c)) : c;
}

// ---- Blurs along one direction: v[7] = direction x, y, radius (720ths), box (0 / 1) ----
float3 blur_along(float2 p, float2 direction, float radius, bool box) {
    float3 sum = 0;
    float weight = 0;
    [unroll] for (int i = -8; i <= 8; ++i) {
        float x = i / 8.0;
        float w = box ? 1 : exp(-x * x * 4.5); // gaussian: the radius is three sigma
        sum += w * at(p + direction * radius * x);
        weight += w;
    }
    return sum / weight;
}
float3 plain_blur(float2 p) {
    float radius = v[7].z * unit_px();
    return radius < 0.3 ? at(p) : blur_along(p, v[7].xy, radius, v[7].w > 0.5);
}
float3 by_channels(float3 original, float3 changed, float channels) {
    float3 result = changed;
    if (channels > 1.5) result = luma(original) + (changed - luma(changed));      // colour only
    else if (channels > 0.5) result = luma(changed) + (original - luma(original)); // brightness only
    return result;
}

// ---- resolution (8): [0] lines, [1] samples across, [2] filter (0 smooth, 1 sharp) ----
float3 cell_mean(float2 c, float2 cell) {
    float2 d = cell * 0.25;
    return 0.25 * (at(c + float2(-d.x, -d.y)) + at(c + float2(d.x, -d.y)) + at(c + float2(-d.x, d.y)) + at(c + d));
}
float3 resolution_program(float2 p) {
    float2 cell = float2(val(1) > 0 ? rect.z / val(1) : 0, val(0) > 0 ? rect.w / val(0) : 0);
    float2 q = p - rect.xy;
    float2 f = float2(cell.x > 0 ? q.x / cell.x - 0.5 : 0, cell.y > 0 ? q.y / cell.y - 0.5 : 0);
    float2 first = floor(f);
    float2 within = f - first;
    float2 t = val(2) > 0.5 ? step(0.5, within) : within;
    float2 c0 = rect.xy + (first + 0.5) * cell;
    c0 = float2(cell.x > 0 ? c0.x : p.x, cell.y > 0 ? c0.y : p.y);
    float2 c1 = c0 + cell;
    float3 a = lerp(cell_mean(c0, cell), cell_mean(float2(c1.x, c0.y), cell), t.x);
    float3 b = lerp(cell_mean(float2(c0.x, c1.y), cell), cell_mean(c1, cell), t.x);
    return lerp(a, b, t.y);
}

// ---- blur (16): [0] radius, [1] direction, [2] channels, [3] shape, [4] shift; v[7] this pass's ----
float3 blur_program(float2 p) {
    float2 from = p - float2(val(4) * unit_px(), 0);
    float radius = v[7].z * unit_px();
    float3 blurred = radius < 0.3 ? at(from) : blur_along(from, v[7].xy, radius, v[7].w > 0.5);
    float3 original = job.z > 0.5 ? at_other(p) : at(p);
    return by_channels(original, blurred, val(2));
}

// ---- sharpen (24): [0] amount, [1] radius, [2] direction, [3] channels (0 luma, 1 all), [4] threshold ----
float3 sharpen_program(float2 p) {
    float3 c = at(p);
    float r = max(val(1) * unit_px(), 0.5);
    float3 around;
    if (val(2) > 1.5) around = 0.25 * (at(p - float2(r, 0)) + at(p + float2(r, 0)) + at(p - float2(0, r)) + at(p + float2(0, r)));
    else if (val(2) > 0.5) around = 0.5 * (at(p - float2(0, r)) + at(p + float2(0, r)));
    else around = 0.5 * (at(p - float2(r, 0)) + at(p + float2(r, 0)));
    float3 edge = c - around;
    if (val(3) < 0.5) edge = luma(edge);
    float size = abs(luma(edge));
    float threshold = val(4);
    float keep = threshold > 0 ? smoothstep(threshold * 0.5, threshold, size) : 1;
    return c + edge * val(0) * keep;
}

// ---- bloom (32, 33): [0] threshold, [1] amount, [2] radius, [3..5] colour ----
// 32: the bright parts, blurred across (into a spare); 4 blurs them on; 33 adds the glow.
float3 bright_part(float3 c) {
    float y = luma(c), threshold = val(0);
    float over = max(y - threshold, 0) / max(1 - threshold, 1e-3);
    return c * (over / max(y, 1e-4));
}
float3 bloom_bright_program(float2 p) {
    float radius = v[7].z * unit_px();
    float3 sum = 0;
    float weight = 0;
    [unroll] for (int i = -8; i <= 8; ++i) {
        float x = i / 8.0;
        float w = exp(-x * x * 4.5);
        sum += w * bright_part(at(p + v[7].xy * radius * x));
        weight += w;
    }
    return sum / weight;
}
float3 bloom_add_program(float2 p) {
    return at(p) + at_other(p) * val(1) * float3(val(3), val(4), val(5));
}

// ---- ghost (40): [0, 1] offset, [2] amount ----
float3 ghost_program(float2 p) {
    float2 offset = float2(val(0), val(1)) * unit_px();
    return lerp(at(p), at(p - offset), val(2));
}

// ---- ccd_smear (48): [0] threshold, [1] amount, [2] length, [3..5] colour ----
float3 ccd_smear_program(float2 p) {
    float3 c = at(p);
    float reach = val(2) * rect.w;
    float threshold = val(0);
    float sum = 0;
    [loop] for (int i = 0; i < 48; ++i) {
        float y = p.y + ((i + 0.5) / 48.0 - 0.5) * reach * 2;
        if (y >= rect.y && y <= rect.y + rect.w) sum += max(luma(at(float2(p.x, y))) - threshold, 0);
    }
    float streak = sum / 48.0 * 12 / max(1 - threshold, 0.02);
    return c + streak * val(1) * float3(val(3), val(4), val(5));
}
)hlsl" R"hlsl(
// ---- tone (56): [0] black crush, [1] gain, [2] knee, [3] spread, [4] bleed ----
float3 tone_program(float2 p) {
    float3 c = at(p);
    float crush = val(0);
    c = max(c - crush, 0) / max(1 - crush, 1e-3) * val(1);
    float top = max(c.r, max(c.g, c.b));
    if (val(4) > 0.5 && top > val(2)) c += (1 - c) * saturate((top - val(2)) / max(val(3), 0.005));
    return c;
}

// ---- levels (64): [0] black, [1] white, [2] gamma, [3, 4] output, [5] exposure, [6] contrast, [7] pivot ----
float3 levels_program(float2 p) {
    float3 c = at(p);
    if (val(5) != 0) c = pow(pow(max(c, 0), 2.2) * exp2(val(5)), 1 / 2.2);
    c = (c - val(0)) / max(val(1) - val(0), 1e-3);
    c = pow(max(c, 0), 1 / max(val(2), 0.01));
    c = (c - val(7)) * val(6) + val(7);
    return val(3) + (val(4) - val(3)) * c;
}

// ---- curves (72): [0] channel, [1] points, [2..17] in, out pairs, [18..25] slopes ----
float curve_at(float x) {
    int n = (int)val(1);
    // Beyond the ends: straight on, at the end's slope.
    float result = val(3) + (x - val(2)) * val(18);
    [unroll] for (int k = 0; k < 8; ++k) {
        if (k == n - 1 && x > val(2 + 2 * k)) result = val(3 + 2 * k) + (x - val(2 + 2 * k)) * val(18 + k);
    }
    [unroll] for (int j = 0; j < 7; ++j) {
        float x0 = val(2 + 2 * j), x1 = val(4 + 2 * j);
        if (j + 1 < n && x > x0 && x <= x1) {
            float h = x1 - x0, t = (x - x0) / h;
            float t2 = t * t, t3 = t2 * t;
            result = (2 * t3 - 3 * t2 + 1) * val(3 + 2 * j) + (t3 - 2 * t2 + t) * h * val(18 + j) +
                     (-2 * t3 + 3 * t2) * val(5 + 2 * j) + (t3 - t2) * h * val(19 + j);
        }
    }
    return result;
}
float3 curves_program(float2 p) {
    float3 c = at(p);
    float channel = val(0);
    float3 result = c;
    if (channel < 0.5) result = float3(curve_at(c.r), curve_at(c.g), curve_at(c.b));
    else if (channel < 1.5) result = c + (curve_at(luma(c)) - luma(c));
    else if (channel < 2.5) result.r = curve_at(c.r);
    else if (channel < 3.5) result.g = curve_at(c.g);
    else result.b = curve_at(c.b);
    return result;
}

// ---- lift_gamma_gain (80): [0..2] lift, [3..5] gamma, [6..8] gain ----
float3 lift_gamma_gain_program(float2 p) {
    float3 c = at(p);
    float3 lift = float3(val(0), val(1), val(2)), gamma = float3(val(3), val(4), val(5)), gain = float3(val(6), val(7), val(8));
    c = gain * (c + lift * (1 - c));
    return pow(max(c, 0), 1 / max(gamma, 0.01));
}

// ---- white_balance (88): [24..26] this moment's gains ----
float3 white_balance_program(float2 p) { return at(p) * float3(val(24), val(25), val(26)); }

// ---- hue_saturation (96): [0] hue (degrees), [1] saturation, [2] vibrance, [3] lightness ----
float3 hue_saturation_program(float2 p) {
    float3 c = at(p);
    float angle = radians(val(0));
    if (angle != 0) {
        // Round the grey axis.
        float3 k = float3(0.57735, 0.57735, 0.57735);
        float s = sin(angle), co = cos(angle);
        c = c * co + cross(k, c) * s + k * dot(k, c) * (1 - co);
    }
    float y = luma(c);
    float strength = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    float amount = val(1) * (1 + val(2) * (1 - saturate(strength * 2)));
    c = y + (c - y) * amount;
    float light = val(3);
    return light > 0 ? lerp(c, 1, light) : c * (1 + light);
}

// ---- channel_mixer (104): [0..2] red, [3..5] green, [6..8] blue, [9..11] offset ----
float3 channel_mixer_program(float2 p) {
    float3 c = at(p);
    return float3(dot(c, float3(val(0), val(1), val(2))), dot(c, float3(val(3), val(4), val(5))),
                  dot(c, float3(val(6), val(7), val(8)))) + float3(val(9), val(10), val(11));
}

// ---- monochrome (112): [0..2] weights, [3..5] colour ----
float3 monochrome_program(float2 p) {
    return dot(at(p), float3(val(0), val(1), val(2))) * float3(val(3), val(4), val(5));
}

// ---- split_tone (120): [0..2] shadows, [3..5] highlights, [6] balance, [7] amount ----
float3 soft_light(float3 a, float3 b) { return (1 - 2 * b) * a * a + 2 * b * a; }
float3 split_tone_program(float2 p) {
    float3 c = saturate(at(p));
    float w = smoothstep(0, 1, saturate(luma(c) + val(6) * 0.5));
    float3 tint = lerp(float3(val(0), val(1), val(2)), float3(val(3), val(4), val(5)), w);
    return lerp(c, soft_light(c, tint), val(7));
}

// ---- posterize (128): [0] levels, [1] dither, [24] the picture ----
float3 posterize_program(float2 p) {
    float3 c = at(p);
    float n = max(val(0) - 1, 1);
    float d = (random3(p.x, p.y, val(24)) - 0.5) * val(1);
    return floor(saturate(c) * n + 0.5 + d) / n;
}

// ---- invert (136) ----
float3 invert_program(float2 p) { return 1 - at(p); }

// ---- lut (144): [0] entries a side, [1..3] the domain's low end, [4..6] its high end ----
float3 lut_program(float2 p) {
    float3 c = at(p);
    float n = max(val(0), 2);
    float3 x = saturate((c - float3(val(1), val(2), val(3))) / (float3(val(4), val(5), val(6)) - float3(val(1), val(2), val(3))));
    return grade.SampleLevel(smooth_clamp, (x * (n - 1) + 0.5) / n, 0).rgb;
}

// ---- flicker (152): [24] this moment's brightness ----
float3 flicker_program(float2 p) { return at(p) * val(24); }

// ---- weave (160): [24, 25] this moment's offset, [26] its turn (radians) ----
float3 weave_program(float2 p) {
    float2 middle = rect.xy + rect.zw * 0.5;
    float2 d = p - middle;
    float s = sin(-val(26)), co = cos(-val(26));
    d = float2(d.x * co - d.y * s, d.x * s + d.y * co);
    return at(middle + d - float2(val(24), val(25)) * unit_px());
}

// ---- line_wobble (168): [0] amount, [1] wavelength (480ths), [2] speed, [3] jitter, [24] phase, [25] picture ----
float3 line_wobble_program(float2 p) {
    float row = floor((p.y - rect.y) / rect.w * 480);
    float wave = sin(row / max(val(1), 1) * 6.2831853 + val(24));
    float jitter = random3(row, val(25), 17) * 2 - 1;
    float shift = val(0) * unit_px() * lerp(wave, jitter, val(3));
    return at(p - float2(shift, 0));
}
)hlsl" R"hlsl(
// ---- tracking (176): [0] amount, [2] height, [4] noise, [24] middle now, [25] how much shows, [26] picture ----
float3 tracking_program(float2 p) {
    float y = picture_uv(p).y;
    float d = abs(y - val(24));
    d = min(d, 1 - d); // it wraps round as it rolls
    float w = saturate(1 - d / max(val(2) * 0.5, 1e-3));
    w = w * w * (3 - 2 * w) * val(25);
    float3 result = at(p);
    if (w > 0) {
        float row = floor(y * 480);
        float tear = (random3(row, val(26), 31) * 0.7 + 0.3) * w * w;
        result = at(p - float2(val(0) * unit_px() * tear, 0));
        float snow = random3(floor((p.x - rect.x) / (unit_px() * 3)), row, val(26));
        result = lerp(result, snow, val(4) * w * step(0.55, random3(row, val(26), 7)));
    }
    return result;
}

// ---- head_switch (184): [0] height, [1] amount, [2] noise, [24] picture ----
float3 head_switch_program(float2 p) {
    float y = picture_uv(p).y;
    float band = val(0);
    float3 result = at(p);
    if (band > 0 && y > 1 - band) {
        float t = (y - (1 - band)) / band;
        float row = floor(y * 480);
        float shift = val(1) * unit_px() * t * t * (0.8 + 0.4 * random3(row, val(24), 3));
        result = at(p - float2(shift, 0));
        float snow = random3(floor((p.x - rect.x) / (unit_px() * 2)), row, val(24));
        result = lerp(result, snow, val(2) * t);
    }
    return result;
}

// ---- dropouts (192): [1] length, [2] thickness (480ths), [3..5] colour, [24] chance a band, [25] picture ----
float3 dropouts_program(float2 p) {
    float2 uv = picture_uv(p);
    float band = floor(uv.y * 480 / max(val(2), 0.5));
    float3 result = at(p);
    if (random3(band, val(25), 51) < val(24)) {
        float streak = val(1);
        float start = random3(band, val(25), 52) * (1 + streak) - streak;
        float along = (uv.x - start) / streak;
        if (along >= 0 && along <= 1) {
            float taper = smoothstep(0, 0.15, along) * smoothstep(1, 0.7, along);
            result = lerp(result, float3(val(3), val(4), val(5)), taper);
        }
    }
    return result;
}

// ---- scanlines (200): [0] amount, [1] lines, [2] thickness, [24] creep ----
float3 scanlines_program(float2 p) {
    float phase = frac(picture_uv(p).y * val(1) + val(24));
    float edge = 0.75 * val(1) / rect.w; // three quarters of a screen pixel, so they stay smooth
    float dark = smoothstep(1 - val(2) - edge, 1 - val(2) + edge, phase);
    return at(p) * (1 - val(0) * dark);
}

// ---- trails (208): [0] amount, [1] mode (0 blend, 1 bright); `other` the picture a moment ago ----
float3 trails_program(float2 p) {
    float3 c = at(p), before = at_other(p);
    return val(1) > 0.5 ? max(c, before * val(0)) : lerp(c, before, val(0));
}

// ---- grain (216): [0] amount, [1] size, [2] colour, [3] shadows, [24] picture, [25] block ----
float3 grain_program(float2 p) {
    float3 c = at(p);
    float2 cell = floor((p - rect.xy) / max(unit_px() * max(val(1), 0.25), 1e-3));
    float frame = val(24) + val(25) * 4099;
    // Two draws added, about -1 to 1, as the recording's grain.
    float same = random3(cell.x, cell.y, frame) + random3(cell.x, cell.y, frame + 7919) - 1;
    float3 own = float3(random3(cell.x, cell.y, frame + 1), random3(cell.x, cell.y, frame + 2), random3(cell.x, cell.y, frame + 3)) +
                 float3(random3(cell.x, cell.y, frame + 4), random3(cell.x, cell.y, frame + 5), random3(cell.x, cell.y, frame + 6)) - 1;
    float3 grain = lerp(float3(same, same, same), own, saturate(val(2)));
    float weight = lerp(1, 2 * (1 - saturate(luma(c))), val(3));
    return c + grain * val(0) * (56.0 / 255) * weight;
}

// ---- dust (224): [0] amount, [1] size, [3] tone (0 dark, 1 light, 2 both), [24] picture, [25, 26] scratches, [27] block ----
float3 dust_program(float2 p) {
    float3 c = at(p);
    float size = max(val(1), 0.5) * unit_px();
    float cell_size = size * 16;
    float2 q = p - rect.xy;
    float2 cell = floor(q / cell_size);
    float cells = (rect.z / cell_size) * (rect.w / cell_size);
    float frame = val(24) + val(27) * 8191;
    float mark = 0, light = 0;
    [unroll] for (int j = -1; j <= 1; ++j) {
        [unroll] for (int i = -1; i <= 1; ++i) {
            float2 k = cell + float2(i, j);
            if (random3(k.x, k.y, frame) < val(0) / max(cells, 1)) {
                float2 centre = (k + float2(random3(k.x, k.y, frame + 1), random3(k.x, k.y, frame + 2))) * cell_size;
                float r = size * (0.5 + random3(k.x, k.y, frame + 3));
                float2 d = q - centre;
                float wobble = 1 + 0.35 * sin(atan2(d.y, d.x) * 3 + random3(k.x, k.y, frame + 4) * 6.28);
                float speck = 1 - smoothstep(r * wobble * 0.7, r * wobble, length(d));
                mark = max(mark, speck);
                if (random3(k.x, k.y, frame + 5) < 0.5) light = max(light, speck);
            }
        }
    }
    [unroll] for (int s = 0; s < 2; ++s) {
        float across = val(25 + s);
        if (across >= 0) {
            float d = abs(picture_uv(p).x - across) * rect.z;
            float width = unit_px() * 0.6;
            float fade = 0.4 + 0.6 * random3(floor(q.y / (unit_px() * 6)), frame, 61 + s);
            mark = max(mark, (1 - smoothstep(width * 0.5, width, d)) * fade);
        }
    }
    float tone = val(3);
    float3 result = c * (1 - mark * 0.85);                              // dirt: dark
    if (tone > 0.5 && tone < 1.5) result = lerp(c, 1, mark * 0.85);       // on a negative: light
    else if (tone > 1.5) result = lerp(result, lerp(c, 1, light * 0.85), step(0.001, light));
    return result;
}

// ---- vignette (232): [0] amount, [1] radius, [2] softness, [3] roundness, [4..6] colour ----
float3 vignette_program(float2 p) {
    // Round: a circle in half picture widths. The frame's shape: the picture's edges all at 1.
    float2 round_place = centred(p);
    float2 frame_place = picture_uv(p) * 2 - 1;
    float d = length(lerp(frame_place, round_place, val(3)));
    float w = smoothstep(val(1), val(1) + val(2), d) * val(0);
    return lerp(at(p), float3(val(4), val(5), val(6)), w);
}

// ---- light_leak (240): [0..2] colour, [4] size, [24, 25] middle now, [26] brightness now ----
float3 light_leak_program(float2 p) {
    float3 c = at(p);
    float2 uv = picture_uv(p);
    float2 d = (uv - float2(val(24), val(25))) * float2(1, rect.w / rect.z) * 2;
    float glow = exp(-dot(d, d) / max(val(4) * val(4), 1e-3) * 2) * val(26);
    float3 colour = float3(val(0), val(1), val(2)) * glow;
    return 1 - (1 - saturate(c)) * (1 - saturate(colour)) + max(c - 1, 0); // screen: light only adds
}

// ---- gate (248): [0] corner radius, [1] softness, [2] inset (of the height), [3..5] colour ----
float3 gate_program(float2 p) {
    float2 middle = rect.xy + rect.zw * 0.5;
    float r = val(0) * rect.w, soft = max(val(1) * rect.w, 0.75);
    float2 half_size = rect.zw * 0.5 - val(2) * rect.w;
    float2 q = abs(p - middle) - half_size + r;
    float d = length(max(q, 0)) + min(max(q.x, q.y), 0) - r;
    return lerp(at(p), float3(val(3), val(4), val(5)), smoothstep(-soft, soft, d));
}

// ---- distort (256): [0] amount, [1] cubic, [2] scale ----
float3 distort_program(float2 p) {
    float2 c = centred(p) / max(val(2), 0.01);
    float r2 = dot(c, c);
    float2 from = c * (1 + val(0) * r2 + val(1) * r2 * r2);
    return in_picture(from) ? at(from_centred(from)) : float3(0, 0, 0);
}

// ---- chromatic_aberration (264): [0] amount at the corners ----
float3 chromatic_aberration_program(float2 p) {
    float2 middle = rect.xy + rect.zw * 0.5;
    float2 d = p - middle;
    float k = val(0) * unit_px() / max(length(rect.zw * 0.5), 1);
    return float3(at(p + d * k).r, at(p).g, at(p - d * k).b);
}

// ---- channel_shift (272): [0, 1] red, [2, 3] green, [4, 5] blue ----
float3 channel_shift_program(float2 p) {
    float u = unit_px();
    return float3(at(p - float2(val(0), val(1)) * u).r, at(p - float2(val(2), val(3)) * u).g, at(p - float2(val(4), val(5)) * u).b);
}

float3 run(int program, float2 p) {
    float3 c = 0;
    switch (program) {
    case 0: c = lens_program(p); break;
    case 1: c = light_program(p); break;
    case 2: c = output_program(p); break;
    case 4: c = plain_blur(p); break;
    case 8: c = resolution_program(p); break;
    case 16: c = blur_program(p); break;
    case 24: c = sharpen_program(p); break;
    case 32: c = bloom_bright_program(p); break;
    case 33: c = bloom_add_program(p); break;
    case 40: c = ghost_program(p); break;
    case 48: c = ccd_smear_program(p); break;
    case 56: c = tone_program(p); break;
    case 64: c = levels_program(p); break;
    case 72: c = curves_program(p); break;
    case 80: c = lift_gamma_gain_program(p); break;
    case 88: c = white_balance_program(p); break;
    case 96: c = hue_saturation_program(p); break;
    case 104: c = channel_mixer_program(p); break;
    case 112: c = monochrome_program(p); break;
    case 120: c = split_tone_program(p); break;
    case 128: c = posterize_program(p); break;
    case 136: c = invert_program(p); break;
    case 144: c = lut_program(p); break;
    case 152: c = flicker_program(p); break;
    case 160: c = weave_program(p); break;
    case 168: c = line_wobble_program(p); break;
    case 176: c = tracking_program(p); break;
    case 184: c = head_switch_program(p); break;
    case 192: c = dropouts_program(p); break;
    case 200: c = scanlines_program(p); break;
    case 208: c = trails_program(p); break;
    case 216: c = grain_program(p); break;
    case 224: c = dust_program(p); break;
    case 232: c = vignette_program(p); break;
    case 240: c = light_leak_program(p); break;
    case 248: c = gate_program(p); break;
    case 256: c = distort_program(p); break;
    case 264: c = chromatic_aberration_program(p); break;
    case 272: c = channel_shift_program(p); break;
    default: c = at(p); break;
    }
    return c;
}

float4 picture_ps(float4 position : SV_Position) : SV_Target {
    float2 p = position.xy;
    int program = (int)job.x;
    float3 c = run(program, p);
    // A block's mix: how much of its change is kept, over the picture as it came to it.
    if (program >= 8 && job.y < 0.999) c = lerp(job.z > 0.5 ? at_other(p) : at(p), c, job.y);
    return float4(c, 1);
}
)hlsl";
} // namespace dingosdk::overlay
