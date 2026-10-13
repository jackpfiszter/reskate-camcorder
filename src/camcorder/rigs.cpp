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
// Smooth noise that never repeats: random values a step apart (one step a cycle of `rate`),
// eased from one to the next. A hand's wobble, a wheel's buzz: no rhythm to notice, as sines had.
float lattice(std::uint32_t seed, std::int64_t i) {
    std::uint32_t h = seed * 0x9e3779b9u ^ static_cast<std::uint32_t>(i) * 0x85ebca6bu ^ static_cast<std::uint32_t>(i >> 32) * 0xc2b2ae35u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffff) / 8388607.5f - 1.0f; // -1 to 1
}
float smooth_noise(std::uint32_t seed, double x) {
    const double cell = std::floor(x);
    const auto i = static_cast<std::int64_t>(cell);
    const auto f = static_cast<float>(x - cell);
    const float u = f * f * f * (f * (f * 6 - 15) + 10); // eased in and out: no kinks
    const float a = lattice(seed, i), b = lattice(seed, i + 1);
    return a + (b - a) * u;
}
} // namespace

HandShake::HandShake(std::uint32_t seed) noexcept : seed_(seed) {
    for (std::uint32_t i = 0; i < 12; ++i) phases_[i] = hash_phase(seed, i);
}

HandShake::Offset HandShake::step(float dt, float speed, bool on_board, float steadiness, const Amounts &amounts) noexcept {
    dt = clamp_dt(dt);
    time_ += dt;
    const float t = time_;
    const float calm = 1.0f - 0.7f * std::clamp(std::isfinite(steadiness) ? steadiness : 0.5f, 0.0f, 1.0f);
    speed = std::isfinite(speed) ? std::clamp(speed, 0.0f, 30.0f) : 0.0f;
    // Three bands of noise an axis, about as fast as breathing, the arm and the wrist (the noise
    // swings about half as far as a sine of the same size: hence the 1.6).
    const auto band = [&](int index, float rate) {
        const double pace = std::clamp(std::isfinite(amounts.speed) ? amounts.speed : 1.0f, 0.1f, 8.0f);
        return 1.6f * smooth_noise(seed_ * 16 + static_cast<std::uint32_t>(index), static_cast<double>(t) * rate * 2 * pace + phases_[index]);
    };
    const auto wave = [&](int axis, float a, float b, float c) {
        return a * band(axis * 3, 0.31f) + b * band(axis * 3 + 1, 0.87f) + c * band(axis * 3 + 2, 2.3f);
    };
    Offset o;
    const float hands = calm * std::max(amounts.hands, 0.0f);
    o.yaw = hands * wave(0, 0.006f, 0.004f, 0.0015f);
    o.pitch = hands * wave(1, 0.005f, 0.003f, 0.0015f);
    o.roll = hands * wave(2, 0.008f, 0.004f, 0.002f);
    if (on_board) {
        // Wheels on concrete: a fast buzz that grows with speed.
        const float buzz = std::min(speed / 8.0f, 1.0f) * 0.0025f * (0.5f + calm) * std::max(amounts.wheels, 0.0f);
        o.pitch += buzz * band(9, 11.0f);
        o.yaw += buzz * 0.6f * band(10, 13.7f);
    } else if (speed > 0.3f) {
        // Footsteps: about two a second walking, three running; the camera dips with each.
        step_phase_ = std::fmod(step_phase_ + dt * std::clamp(1.2f + speed * 0.35f, 1.2f, 3.4f), 1.0f);
        const float bob = std::min(speed / 5.0f, 1.0f) * (0.5f + calm) * std::max(amounts.footsteps, 0.0f);
        o.position[1] = -0.025f * bob * std::abs(std::sin(step_phase_ * pi));
        // Each step a little different: how hard the foot lands sways the roll.
        o.roll += 0.01f * bob * std::sin(step_phase_ * 2 * pi + phases_[11]) * (0.6f + 0.5f * band(11, 0.9f));
    }
    return o;
}

const GripProfile &long_grip() {
    static const GripProfile grip{};
    return grip;
}
const GripProfile &hip_grip() {
    static const GripProfile grip{"hip", "Hip", 1.0f, 0.3f, 0.3f, -3, 0.92f, 1, {}};
    return grip;
}
const GripProfile &eye_grip() {
    static const GripProfile grip{"eye", "Eye level", 1.6f, 0.08f, 0.3f, -3, 0.92f, 1, {}};
    return grip;
}

float HandheldRig::lens_height(bool on_board) const {
    const float low = std::max(settings_.lowest, 0.0f), high = std::max(settings_.highest, low);
    return std::clamp(settings_.grip.height_on(on_board) + lift_, low, high);
}

Vec3 HandheldRig::grip_position(const Filmer &filmer) const {
    const auto forward = flat_forward(filmer.forward);
    const auto right = flat_right(forward);
    // Out to the right hand and ahead of the body, as the grip holds it.
    const float side = settings_.grip.side, ahead = settings_.grip.ahead;
    // From the feet: the root less what it sits above them.
    const Vec3 feet{filmer.position[0], filmer.position[1] - root_above_feet_, filmer.position[2]};
    return add(add(add(feet, {0, lens_height(filmer.on_board), 0}), mul(right, side)), mul(forward, ahead));
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
    // Where the feet are under the root: from the ground when the game found it (a root more than
    // a quarter metre higher than usual is a jump, and the camera rises with the filmer).
    if (filmer.ground && std::isfinite(*filmer.ground)) {
        const float above = filmer.position[1] - *filmer.ground;
        if (above > -0.5f && above < 2.0f) {
            const float kept = std::clamp(above, 0.0f, 1.6f);
            if (!measured_) {
                root_above_feet_ = kept;
                measured_ = true;
            } else if (above < root_above_feet_ + 0.25f) {
                root_above_feet_ = damp(root_above_feet_, kept, 2.0f, dt);
            }
        }
    }
    // LB / RB (or R / F, or `camcorder height`) raise and lower it, about a metre in a second and a bit.
    if (const float raise = clamp_unit(input.raise); raise != 0) {
        const float now = lens_height(filmer.on_board);
        const float low = std::max(settings_.lowest, 0.0f), high = std::max(settings_.highest, low);
        set_lift(lift_ + std::clamp(now + raise * settings_.raise_speed * dt, low, high) - now);
    }
    const auto grip = grip_position(filmer);
    // The arms: the camera hangs from the body, so the body's pushes, stops, turns and landings
    // swing it before it settles; at a steady speed it rides along (the body's acceleration, not
    // its speed, moves it). A jump in place (a respawn) starts it again.
    if (!arm_started_ || length(sub(grip, last_eye_)) > 3) {
        for (auto &a : arm_) a.reset(0);
        last_velocity_ = filmer.velocity;
    }
    Vec3 push{};
    if (arm_started_ && dt > 1e-4f && finite(filmer.velocity))
        for (std::size_t i = 0; i < 3; ++i) push[i] = -(filmer.velocity[i] - last_velocity_[i]) / dt;
    if (const float g = length(push); g > 40) push = mul(push, 40 / g); // a hard landing, not a teleport
    for (std::size_t i = 0; i < 3; ++i) {
        auto &a = arm_[i];
        a.step(0, i == 1 ? settings_.arm_bob : settings_.arm_sway, settings_.arm_damping, dt, false, push[i]);
        if (std::abs(a.value) > settings_.arm_reach) {
            a.value = std::copysign(settings_.arm_reach, a.value);
            a.velocity = 0;
        }
    }
    if (finite(filmer.velocity)) last_velocity_ = filmer.velocity;
    last_eye_ = grip;
    arm_started_ = true;
    const Vec3 swing{arm_[0].value, arm_[1].value, arm_[2].value};
    const auto eye = add(grip, swing);
    const auto facing = flat_forward(filmer.forward);
    const float heading = std::atan2(facing[0], facing[2]);
    // Start looking the way the filmer faces, level.
    if (!started_) face({heading, settings_.grip.pitch * degrees});
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
        // A person chasing a subject is never exactly on it: small corrections, more of them the
        // faster the subject crosses the frame.
        if (dt > 1e-4f) target_rate_ = damp(target_rate_, std::abs(wrap_angle(aim.yaw - last_target_yaw_)) / dt, 0.3f, dt);
        last_target_yaw_ = aim.yaw;
        hunt_time_ += dt;
        const float hunt = std::max(settings_.hunting, 0.0f) * (0.004f + 0.05f * std::min(target_rate_, 2.0f));
        target_yaw = aim.yaw + nudge_yaw_ + hunt * (std::sin(hunt_time_ * 2 * pi * 0.37f) + 0.6f * std::sin(hunt_time_ * 2 * pi * 0.93f + 1.3f));
        target_pitch = aim.pitch + nudge_pitch_ + 0.6f * hunt * std::sin(hunt_time_ * 2 * pi * 0.51f + 0.7f);
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
    // With weight: a turn builds up and winds down, and swings a little past before settling.
    const float reaction = tracking ? settings_.operator_reaction : settings_.aim_swing;
    const float damping = tracking ? 0.75f : settings_.aim_damping;
    yaw_.step(wrap_angle(target_yaw), reaction, damping, dt, true);
    pitch_.step(std::clamp(target_pitch, -highest_pitch, highest_pitch), reaction, damping, dt);
    // Swinging the camera round tips it a little, the way an arm does.
    const float turn = dt > 0 ? wrap_angle(yaw_.value - last_yaw_) / dt : 0;
    last_yaw_ = yaw_.value;
    roll_ = damp(roll_, std::clamp(-turn * 0.06f, -8 * degrees, 8 * degrees), 0.25f, dt);
    // A dutch angle, held as long as the stick is: up to 25 degrees, eased in and back to level.
    tilt_ = damp(tilt_, clamp_unit(input.tilt) * 25 * degrees, 0.18f, dt);

    const float speed = std::sqrt(filmer.velocity[0] * filmer.velocity[0] + filmer.velocity[2] * filmer.velocity[2]);
    HandShake::Amounts amounts = settings_.shake;
    const float hold = std::max(settings_.grip.shake, 0.0f);
    amounts.hands *= hold;
    amounts.wheels *= hold;
    amounts.footsteps *= hold;
    const auto shake = shake_.step(dt, std::isfinite(speed) ? speed : 0, filmer.on_board, settings_.steadiness, amounts);
    Shot shot;
    // The arms' swing tips the frame too: a dip on landing nods it down, a swing out leans it.
    const float sideways = dot(swing, flat_right(facing));
    shot.camera = camera_matrix(add(eye, shake.position), yaw_.value + shake.yaw,
                                pitch_.value + shake.pitch + swing[1] * settings_.arm_tilt,
                                roll_ + tilt_ + shake.roll + sideways * settings_.arm_tilt * 0.5f);
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
int closest_skater(const Vec3 &camera, const std::vector<Vec3> &skaters, int current, float margin) {
    if (!finite(camera)) return current; // nowhere to measure from: no change
    int best = -1;
    float best_distance = 0;
    for (std::size_t i = 0; i < skaters.size(); ++i) {
        if (!finite(skaters[i])) continue;
        const float d = length(sub(skaters[i], camera));
        if (best < 0 || d < best_distance) {
            best = static_cast<int>(i);
            best_distance = d;
        }
    }
    if (current >= 0 && current < static_cast<int>(skaters.size()) && current != best && finite(skaters[static_cast<std::size_t>(current)]) &&
        length(sub(skaters[static_cast<std::size_t>(current)], camera)) < best_distance + std::max(margin, 0.0f))
        return current;
    return best;
}
} // namespace dingosdk::camcorder
