#pragma once
// Small vector and camera helpers for the camcorder. Header only, no game types, so the whole
// model builds and tests on any platform.
//
// Conventions follow ReSkate's camera code (Extension/Skater/free_flight.h,
// Extension/Multiplayer/Hud/follow_camera.cpp):
// - world Y is up;
// - a camera matrix is 16 floats in rows: right, up, back, position (the camera looks along
//   minus its back row); each row's fourth lane is 0, the position's 1;
// - yaw 0 looks along +Z and grows toward +X; positive pitch looks up.
#include <algorithm>
#include <array>
#include <cmath>

namespace dingosdk::camcorder {
using Vec3 = std::array<float, 3>;
using Matrix = std::array<float, 16>;

inline constexpr float pi = 3.14159265358979323846f;
inline constexpr float degrees = pi / 180.0f; // multiply degrees by this to get radians

inline Vec3 add(Vec3 a, Vec3 b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3 sub(Vec3 a, Vec3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 mul(Vec3 a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
inline float dot(Vec3 a, Vec3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a, Vec3 fallback) {
    const float l = length(a);
    return std::isfinite(l) && l > 1e-6f ? mul(a, 1.0f / l) : fallback;
}
inline bool finite(Vec3 a) { return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]); }

// An angle in (-pi, pi].
inline float wrap_angle(float a) {
    if (!std::isfinite(a)) return 0;
    a = std::fmod(a + pi, 2 * pi);
    if (a <= 0) a += 2 * pi;
    return a - pi;
}

// The look direction for a yaw and pitch.
inline Vec3 look_direction(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return {std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp};
}
struct YawPitch {
    float yaw{}, pitch{};
};
// The yaw and pitch that look from `from` at `to`; nothing sensible when they coincide.
inline YawPitch aim_at(Vec3 from, Vec3 to) {
    const auto d = sub(to, from);
    return {std::atan2(d[0], d[2]), std::atan2(d[1], std::sqrt(d[0] * d[0] + d[2] * d[2]))};
}

// The camera matrix at `eye` looking along yaw/pitch, rolled clockwise (as the viewer sees it)
// by `roll` radians about the look direction.
inline Matrix camera_matrix(Vec3 eye, float yaw, float pitch, float roll = 0) {
    pitch = std::clamp(pitch, -1.55f, 1.55f);
    const auto back = mul(look_direction(yaw, pitch), -1.0f);
    // right x up = back, so right = world_up x back and up = back x right.
    auto right = normalize(cross({0, 1, 0}, back), {1, 0, 0});
    auto up = cross(back, right);
    if (roll != 0) {
        const float c = std::cos(roll), s = std::sin(roll);
        const auto r = add(mul(right, c), mul(up, -s));
        const auto u = add(mul(up, c), mul(right, s));
        right = r;
        up = u;
    }
    return {right[0], right[1], right[2], 0, up[0], up[1], up[2], 0,
            back[0], back[1], back[2], 0, eye[0], eye[1], eye[2], 1};
}
inline Vec3 matrix_position(const Matrix &m) { return {m[12], m[13], m[14]}; }
inline Vec3 matrix_forward(const Matrix &m) { return {-m[8], -m[9], -m[10]}; }
inline YawPitch matrix_yaw_pitch(const Matrix &m) {
    const auto f = matrix_forward(m);
    return {std::atan2(f[0], f[2]), std::asin(std::clamp(f[1], -1.0f, 1.0f))};
}

// The same check ReSkate's free flight applies before it hands a matrix to the game
// (valid_flight_transform): finite, bounded, orthonormal rotation rows.
inline bool valid_camera_matrix(const Matrix &m) {
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t col = 0; col < 3; ++col)
            if (!std::isfinite(m[row * 4 + col]) || std::abs(m[row * 4 + col]) > 1000000) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3 a{m[i * 4], m[i * 4 + 1], m[i * 4 + 2]};
        if (std::abs(dot(a, a) - 1) > .05f) return false;
        for (std::size_t k = i + 1; k < 3; ++k)
            if (std::abs(dot(a, {m[k * 4], m[k * 4 + 1], m[k * 4 + 2]})) > .05f) return false;
    }
    return true;
}

// Exponential approach: after `time_constant` seconds about 63% of the way there.
inline float damp(float current, float target, float time_constant, float dt) {
    if (!(time_constant > 0) || !std::isfinite(current)) return target;
    return target + (current - target) * std::exp(-std::max(dt, 0.0f) / time_constant);
}
inline float damp_angle(float current, float target, float time_constant, float dt) {
    return wrap_angle(target + wrap_angle(current - target) * (time_constant > 0 ? std::exp(-std::max(dt, 0.0f) / time_constant) : 0.0f));
}

// A critically damped spring: follows its target with no overshoot, settling in about
// 1 / frequency seconds, the way a person swings a camera after something. Exact for any
// time step, so frame rate never changes how it feels.
struct Spring {
    float value{}, velocity{};
    void reset(float v) { value = v; velocity = 0; }
    void step(float target, float frequency_hz, float dt, bool angular = false) {
        if (!std::isfinite(value) || !std::isfinite(velocity)) reset(target);
        dt = std::clamp(dt, 0.0f, 0.25f);
        const float omega = 2 * pi * std::max(frequency_hz, 0.01f);
        const float x = angular ? wrap_angle(value - target) : value - target;
        const float e = std::exp(-omega * dt);
        const float j = (velocity + omega * x) * dt;
        const float next = (x + j) * e;
        velocity = (velocity - omega * j) * e;
        value = angular ? wrap_angle(target + next) : target + next;
    }
};
} // namespace dingosdk::camcorder
