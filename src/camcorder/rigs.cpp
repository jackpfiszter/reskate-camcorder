#include "rigs.h"

namespace dingosdk::camcorder {
namespace {
float clamp_unit(float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; }
float clamp_dt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }
Vec3 flat_forward(Vec3 forward) { return normalize({forward[0], 0, forward[2]}, {0, 0, 1}); }
// Screen right for a camera facing `forward` (camera_matrix's right row): facing +Z it is -X.
Vec3 flat_right(Vec3 forward) { return {-forward[2], 0, forward[0]}; }
constexpr float highest_pitch = 80 * degrees;

// A cheap repeatable hash, so a seed always shakes the same way.
float hash_phase(std::uint32_t seed, std::uint32_t i) {
    std::uint32_t h = seed * 0x9e3779b9u + i * 0x85ebca6bu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return static_cast<float>(h & 0xffff) / 65535.0f * 2 * pi;
}
} // namespace

HandShake::HandShake(std::uint32_t seed) noexcept {
    for (std::uint32_t i = 0; i < 12; ++i) phases_[i] = hash_phase(seed, i);
}

HandShake::Offset HandShake::step(float dt, float speed, bool on_board, float steadiness) noexcept {
    dt = clamp_dt(dt);
    time_ += dt;
    const float t = time_;
    const float calm = 1.0f - 0.7f * std::clamp(std::isfinite(steadiness) ? steadiness : 0.5f, 0.0f, 1.0f);
    speed = std::isfinite(speed) ? std::clamp(speed, 0.0f, 30.0f) : 0.0f;
    // Three incommensurate waves an axis: breathing, the arm, the wrist.
    const auto wave = [&](int axis, float a, float b, float c) {
        const float *p = phases_ + axis * 3;
        return a * std::sin(t * 2 * pi * 0.31f + p[0]) + b * std::sin(t * 2 * pi * 0.87f + p[1]) +
               c * std::sin(t * 2 * pi * 2.3f + p[2]);
    };
    Offset o;
    o.yaw = calm * wave(0, 0.006f, 0.004f, 0.0015f);
    o.pitch = calm * wave(1, 0.005f, 0.003f, 0.0015f);
    o.roll = calm * wave(2, 0.008f, 0.004f, 0.002f);
    if (on_board) {
        // Wheels on concrete: a fast buzz that grows with speed.
        const float buzz = std::min(speed / 8.0f, 1.0f) * 0.0025f * (0.5f + calm);
        o.pitch += buzz * std::sin(t * 2 * pi * 11.0f + phases_[9]);
        o.yaw += buzz * 0.6f * std::sin(t * 2 * pi * 13.7f + phases_[10]);
    } else if (speed > 0.3f) {
        // Footsteps: about two a second walking, three running; the camera dips with each.
        step_phase_ = std::fmod(step_phase_ + dt * std::clamp(1.2f + speed * 0.35f, 1.2f, 3.4f), 1.0f);
        const float bob = std::min(speed / 5.0f, 1.0f) * (0.5f + calm);
        o.position[1] = -0.025f * bob * std::abs(std::sin(step_phase_ * pi));
        o.roll += 0.01f * bob * std::sin(step_phase_ * 2 * pi + phases_[11]);
    }
    return o;
}

std::string_view grip_name(Grip grip) noexcept {
    switch (grip) {
    case Grip::long_grip: return "Long grip";
    case Grip::hip: return "Hip";
    case Grip::eye_level: return "Eye level";
    }
    return "Long grip";
}

Vec3 HandheldRig::grip_position(const Filmer &filmer) const {
    const auto forward = flat_forward(filmer.forward);
    const auto right = flat_right(forward);
    // Height above the feet, out to the right hand, ahead of the body. A filmer on a board
    // crouches: everything sits a little lower.
    float height = 0, side = 0, ahead = 0;
    switch (settings_.grip) {
    case Grip::long_grip: height = 0.45f; side = 0.35f; ahead = 0.55f; break;
    case Grip::hip: height = 1.0f; side = 0.3f; ahead = 0.3f; break;
    // Eye level sits clear in front of the face: the filmer is the player's own skater, whose head is
    // drawn (from inside it the view would be the inside of the head).
    case Grip::eye_level: height = 1.6f; side = 0.08f; ahead = 0.3f; break;
    }
    if (filmer.on_board) height *= 0.92f;
    return add(add(add(filmer.position, {0, height, 0}), mul(right, side)), mul(forward, ahead));
}

void HandheldRig::face(YawPitch aim) {
    yaw_.reset(wrap_angle(aim.yaw));
    pitch_.reset(std::clamp(aim.pitch, -highest_pitch, highest_pitch));
    free_pitch_ = pitch_.value;
    offset_pending_ = true;
    nudge_yaw_ = nudge_pitch_ = 0;
    last_yaw_ = yaw_.value;
    started_ = true;
}

Shot HandheldRig::step(const Filmer &filmer, const Subject *subject, const OperatorInput &input, float dt,
                       float zoom_ratio) {
    dt = clamp_dt(dt);
    if (!filmer.valid || !finite(filmer.position)) return {};
    const auto eye = grip_position(filmer);
    const auto facing = flat_forward(filmer.forward);
    const float heading = std::atan2(facing[0], facing[2]);
    // Start looking the way the filmer faces, level.
    if (!started_) face({heading, settings_.grip == Grip::long_grip ? 5 * degrees : -3 * degrees});
    if (offset_pending_) {
        offset_yaw_ = wrap_angle(yaw_.value - heading);
        offset_pending_ = false;
    }
    const float zoom = std::isfinite(zoom_ratio) ? std::max(zoom_ratio, 1.0f) : 1.0f;
    const float rate = settings_.look_speed / std::sqrt(zoom);
    const float look_x = clamp_unit(input.look_x), look_y = clamp_unit(input.look_y);
    const bool tracking = settings_.lock_on && subject && subject->valid && finite(subject->position);
    float target_yaw = 0, target_pitch = 0;
    if (tracking) {
        // The stick shifts the framing; let go and the filmer drifts back to centring the subject.
        nudge_yaw_ = std::clamp(nudge_yaw_ - look_x * rate * dt, -settings_.max_nudge, settings_.max_nudge);
        nudge_pitch_ = std::clamp(nudge_pitch_ + look_y * rate * dt, -settings_.max_nudge, settings_.max_nudge);
        if (look_x == 0) nudge_yaw_ = damp(nudge_yaw_, 0, 1.5f, dt);
        if (look_y == 0) nudge_pitch_ = damp(nudge_pitch_, 0, 1.5f, dt);
        const auto ahead = add(subject->position, mul(subject->velocity, settings_.lead));
        const auto aim = aim_at(eye, add(ahead, {0, settings_.aim_height, 0}));
        target_yaw = aim.yaw + nudge_yaw_;
        target_pitch = aim.pitch + nudge_pitch_;
        // Losing the subject leaves the camera where it points, held at that angle to the body.
        offset_yaw_ = wrap_angle(yaw_.value - heading);
        free_pitch_ = pitch_.value;
    } else {
        offset_yaw_ = wrap_angle(offset_yaw_ - look_x * rate * dt);
        free_pitch_ = std::clamp(free_pitch_ + look_y * rate * dt, -highest_pitch, highest_pitch);
        target_yaw = wrap_angle(heading + offset_yaw_);
        target_pitch = free_pitch_;
        nudge_yaw_ = nudge_pitch_ = 0;
    }
    // A tracking camera swings after the subject like a person; a free one follows the stick and
    // the filmer's turns, the arm a moment behind the body.
    const float reaction = tracking ? settings_.operator_reaction : 5.0f;
    yaw_.step(wrap_angle(target_yaw), reaction, dt, true);
    pitch_.step(std::clamp(target_pitch, -highest_pitch, highest_pitch), reaction, dt);
    // Swinging the camera round tips it a little, the way an arm does.
    const float turn = dt > 0 ? wrap_angle(yaw_.value - last_yaw_) / dt : 0;
    last_yaw_ = yaw_.value;
    roll_ = damp(roll_, std::clamp(-turn * 0.06f, -8 * degrees, 8 * degrees), 0.25f, dt);

    const float speed = std::sqrt(filmer.velocity[0] * filmer.velocity[0] + filmer.velocity[2] * filmer.velocity[2]);
    const auto shake = shake_.step(dt, std::isfinite(speed) ? speed : 0, filmer.on_board, settings_.steadiness);
    Shot shot;
    shot.camera = camera_matrix(add(eye, shake.position), yaw_.value + shake.yaw, pitch_.value + shake.pitch,
                                roll_ + shake.roll);
    shot.valid = valid_camera_matrix(shot.camera);
    return shot;
}

void TripodRig::place(Vec3 feet, YawPitch aim) {
    if (!finite(feet)) return;
    feet_ = feet;
    yaw_.reset(wrap_angle(aim.yaw));
    pitch_.reset(std::clamp(aim.pitch, -highest_pitch, highest_pitch));
    yaw_rate_ = pitch_rate_ = 0;
    placed_ = true;
}

void TripodRig::setup(const OperatorInput &input, std::optional<float> ground, float dt, float zoom_ratio) {
    dt = clamp_dt(dt);
    if (!placed_) return;
    // Carried at walking pace, or jogging.
    const float pace = input.fast ? 6.0f : 2.0f;
    const auto forward = flat_forward(look_direction(yaw_.value, 0));
    const auto move = add(mul(flat_right(forward), clamp_unit(input.move_x)), mul(forward, clamp_unit(input.move_y)));
    feet_ = add(feet_, mul(move, pace * dt));
    if (ground && std::isfinite(*ground)) feet_[1] = *ground;
    settings_.height = std::clamp(settings_.height + clamp_unit(input.raise) * 0.8f * dt, lowest, highest);
    const float zoom = std::isfinite(zoom_ratio) ? std::max(zoom_ratio, 1.0f) : 1.0f;
    const float rate = 1.8f / std::sqrt(zoom);
    yaw_.reset(wrap_angle(yaw_.value - clamp_unit(input.look_x) * rate * dt));
    pitch_.reset(std::clamp(pitch_.value + clamp_unit(input.look_y) * rate * dt, -highest_pitch, highest_pitch));
    yaw_rate_ = pitch_rate_ = 0;
}

Shot TripodRig::step(const Subject *subject, const OperatorInput &input, float dt, float zoom_ratio) {
    dt = clamp_dt(dt);
    if (!placed_) return {};
    const float zoom = std::isfinite(zoom_ratio) ? std::max(zoom_ratio, 1.0f) : 1.0f;
    switch (settings_.head) {
    case Head::locked:
        yaw_rate_ = pitch_rate_ = 0;
        break;
    case Head::manual: {
        // The fluid head turns the handle's push into a speed, late and smoothly.
        const float top = settings_.pan_speed / zoom;
        yaw_rate_ = damp(yaw_rate_, -clamp_unit(input.look_x) * top, settings_.drag, dt);
        pitch_rate_ = damp(pitch_rate_, clamp_unit(input.look_y) * top, settings_.drag, dt);
        yaw_.reset(wrap_angle(yaw_.value + yaw_rate_ * dt));
        pitch_.reset(std::clamp(pitch_.value + pitch_rate_ * dt, -highest_pitch, highest_pitch));
        break;
    }
    case Head::follow:
        if (subject && subject->valid && finite(subject->position)) {
            const auto aim = aim_at(lens(), add(subject->position, {0, settings_.aim_height, 0}));
            yaw_.step(aim.yaw, settings_.follow_reaction, dt, true);
            pitch_.step(std::clamp(aim.pitch, -highest_pitch, highest_pitch), settings_.follow_reaction, dt);
        } else {
            // Lost them: the head coasts to a stop where it is.
            yaw_.step(yaw_.value, settings_.follow_reaction, dt, true);
            pitch_.step(pitch_.value, settings_.follow_reaction, dt);
        }
        break;
    }
    Shot shot;
    shot.camera = camera_matrix(lens(), yaw_.value, pitch_.value);
    shot.valid = valid_camera_matrix(shot.camera);
    return shot;
}
} // namespace dingosdk::camcorder
