#include "camcorder/rigs.h"
#include "check.h"

using namespace dingosdk::camcorder;

namespace {
float angle_between(Vec3 a, Vec3 b) {
    return std::acos(std::clamp(dot(normalize(a, {0, 0, 1}), normalize(b, {0, 0, 1})), -1.0f, 1.0f)) / degrees;
}
Filmer standing(Vec3 at = {}, Vec3 facing = {0, 0, 1}) {
    Filmer f;
    f.valid = true;
    f.position = at;
    f.forward = facing;
    f.on_board = false;
    return f;
}
} // namespace

TEST(camera_matrix_follows_the_game_layout) {
    const auto m = camera_matrix({1, 2, 3}, 0, 0);
    CHECK(valid_camera_matrix(m));
    // Looking along +Z, screen right is -X, up is +Y, back is -Z.
    CHECK_NEAR(m[0], -1, 1e-6);
    CHECK_NEAR(m[5], 1, 1e-6);
    CHECK_NEAR(m[10], -1, 1e-6);
    CHECK_NEAR(m[12], 1, 1e-6);
    CHECK_NEAR(m[13], 2, 1e-6);
    CHECK_NEAR(m[14], 3, 1e-6);
    CHECK_NEAR(m[15], 1, 1e-6);
    // right x up = back, as ReSkate's follow camera builds it.
    const auto back = cross({m[0], m[1], m[2]}, {m[4], m[5], m[6]});
    CHECK_NEAR(back[2], m[10], 1e-6);
}

TEST(aim_and_matrix_agree) {
    const Vec3 eye{2, 1, -4}, target{-3, 5, 7};
    const auto aim = aim_at(eye, target);
    const auto m = camera_matrix(eye, aim.yaw, aim.pitch, 0.2f);
    CHECK(valid_camera_matrix(m));
    CHECK(angle_between(matrix_forward(m), sub(target, eye)) < 0.05f); // acos is coarse near 0
    const auto back = matrix_yaw_pitch(m);
    CHECK_NEAR(back.yaw, aim.yaw, 1e-4);
    CHECK_NEAR(back.pitch, aim.pitch, 1e-4);
}

TEST(spring_settles_without_overshoot_and_takes_the_short_way_round) {
    Spring s;
    s.reset(0);
    float last = 0;
    for (int i = 0; i < 240; ++i) {
        s.step(1, 1.5f, 1.0f / 60);
        CHECK(s.value <= 1.0f + 1e-5f);
        CHECK(s.value >= last - 1e-6f);
        last = s.value;
    }
    CHECK_NEAR(s.value, 1, 1e-3);
    Spring a;
    a.reset(170 * degrees);
    for (int i = 0; i < 240; ++i) {
        a.step(-170 * degrees, 1.5f, 1.0f / 60, true);
        CHECK(std::abs(a.value) > 160 * degrees); // never swings back through zero
    }
    CHECK_NEAR(a.value, -170 * degrees, 1e-3);
    // Huge and broken time steps are survived.
    a.step(0, 1.5f, 1e9f, true);
    a.step(0, 1.5f, NAN, true);
    CHECK(std::isfinite(a.value));
}

TEST(handheld_grips_hold_the_camera_at_their_heights) {
    HandheldRig rig;
    const auto f = standing({0, 10, 0});
    rig.settings().grip = long_grip();
    CHECK_NEAR(rig.grip_position(f)[1], 10.45, 1e-4);
    rig.settings().grip = eye_grip();
    CHECK_NEAR(rig.grip_position(f)[1], 11.6, 1e-4);
    // Clear of the filmer's own head (about 0.15 m from its centre to the face).
    CHECK(rig.grip_position(f)[2] >= 0.25f);
    // Held in the right hand: facing +Z, right is -X.
    rig.settings().grip = hip_grip();
    CHECK(rig.grip_position(f)[0] < 0);
}

TEST(handheld_lock_on_keeps_a_moving_skater_framed) {
    HandheldRig rig;
    Subject subject{true, {0, 0, 6}, {4, 0, 0}};
    Shot shot;
    for (int i = 0; i < 180; ++i) {
        subject.position = add(subject.position, mul(subject.velocity, 1.0f / 60));
        shot = rig.step(standing(), &subject, {}, 1.0f / 60, 1);
        CHECK(shot.valid);
    }
    const auto eye = matrix_position(shot.camera);
    const auto to_subject = sub(add(subject.position, {0, rig.settings().aim_height, 0}), eye);
    CHECK(angle_between(matrix_forward(shot.camera), to_subject) < 6);
}

TEST(handheld_lock_on_nudge_is_bounded_and_drifts_back) {
    HandheldRig rig;
    const Subject subject{true, {0, 0, 6}, {}};
    OperatorInput push;
    push.look_x = 1;
    Shot shot;
    for (int i = 0; i < 300; ++i) shot = rig.step(standing(), &subject, push, 1.0f / 60, 1);
    const auto to_subject = sub(add(subject.position, {0, 0.8f, 0}), matrix_position(shot.camera));
    const float off = angle_between(matrix_forward(shot.camera), to_subject);
    CHECK(off > 15 && off < 30);
    for (int i = 0; i < 600; ++i) shot = rig.step(standing(), &subject, {}, 1.0f / 60, 1);
    CHECK(angle_between(matrix_forward(shot.camera), sub(add(subject.position, {0, 0.8f, 0}), matrix_position(shot.camera))) < 3);
}

TEST(handheld_free_look_turns_the_way_the_stick_points) {
    HandheldRig rig;
    rig.settings().lock_on = false;
    OperatorInput right;
    right.look_x = 1;
    Shot shot;
    for (int i = 0; i < 30; ++i) shot = rig.step(standing(), nullptr, right, 1.0f / 60, 1);
    CHECK(matrix_forward(shot.camera)[0] < -0.2f); // facing +Z, turning right goes toward -X
    OperatorInput up;
    up.look_y = 1;
    for (int i = 0; i < 30; ++i) shot = rig.step(standing(), nullptr, up, 1.0f / 60, 1);
    CHECK(matrix_forward(shot.camera)[1] > 0.3f);
    // Slower at tele.
    HandheldRig wide, tele;
    wide.settings().lock_on = tele.settings().lock_on = false;
    Shot a, b;
    for (int i = 0; i < 10; ++i) {
        a = wide.step(standing(), nullptr, right, 1.0f / 60, 1);
        b = tele.step(standing(), nullptr, right, 1.0f / 60, 10);
    }
    CHECK(std::abs(matrix_forward(a.camera)[0]) > std::abs(matrix_forward(b.camera)[0]) * 2);
}

TEST(handheld_feel_does_not_depend_on_frame_rate) {
    const auto run = [](int hz) {
        HandheldRig rig;
        Subject subject{true, {3, 0, 6}, {-2, 0, 1}};
        Shot shot;
        for (int i = 0; i < hz; ++i) {
            subject.position = add(subject.position, mul(subject.velocity, 1.0f / hz));
            shot = rig.step(standing(), &subject, {}, 1.0f / hz, 1);
        }
        return matrix_forward(shot.camera);
    };
    CHECK(angle_between(run(30), run(144)) < 1.0f);
}

TEST(handheld_shake_stays_small) {
    HandShake shake(7);
    float most = 0;
    for (int i = 0; i < 600; ++i) {
        const auto o = shake.step(1.0f / 60, 12, true, 0);
        most = std::max({most, std::abs(o.yaw), std::abs(o.pitch), std::abs(o.roll)});
    }
    CHECK(most > 0.001f);        // there is some
    CHECK(most < 2.5f * degrees); // and it never ruins the shot
    HandShake a(3), b(3);
    CHECK_NEAR(a.step(0.1f, 0, false, 0.5f).yaw, b.step(0.1f, 0, false, 0.5f).yaw, 0); // repeatable
}

TEST(handheld_without_a_filmer_has_no_shot) {
    HandheldRig rig;
    CHECK(!rig.step(Filmer{}, nullptr, {}, 1.0f / 60, 1).valid);
}

TEST(tripod_follow_pans_after_the_subject) {
    TripodRig tripod;
    tripod.place({0, 0, 0}, {0, 0});
    Subject subject{true, {8, 0, 8}, {}};
    Shot shot;
    for (int i = 0; i < 300; ++i) shot = tripod.step(&subject, {}, 1.0f / 60, 1);
    CHECK(shot.valid);
    CHECK_NEAR(matrix_position(shot.camera)[1], tripod.settings().height, 1e-5);
    CHECK(angle_between(matrix_forward(shot.camera), sub(add(subject.position, {0, 0.8f, 0}), tripod.lens())) < 1);
}

TEST(tripod_locked_off_never_moves) {
    TripodRig::Settings settings;
    settings.head = TripodRig::Head::locked;
    TripodRig tripod(settings);
    tripod.place({1, 0, 1}, {0.3f, -0.1f});
    const Subject subject{true, {-8, 0, 8}, {}};
    OperatorInput push;
    push.look_x = 1;
    const auto first = tripod.step(&subject, push, 1.0f / 60, 1);
    Shot last;
    for (int i = 0; i < 120; ++i) last = tripod.step(&subject, push, 1.0f / 60, 1);
    for (int i = 0; i < 16; ++i) CHECK_NEAR(first.camera[i], last.camera[i], 1e-6);
}

TEST(tripod_manual_head_drags) {
    TripodRig::Settings settings;
    settings.head = TripodRig::Head::manual;
    TripodRig tripod(settings);
    tripod.place({}, {0, 0});
    OperatorInput push;
    push.look_x = 1;
    tripod.step(nullptr, push, 0.05f, 1);
    const float early = std::abs(tripod.aim().yaw) / 0.05f; // radians a second, just after the push
    for (int i = 0; i < 60; ++i) tripod.step(nullptr, push, 1.0f / 60, 1);
    const float before = tripod.aim().yaw;
    tripod.step(nullptr, push, 0.05f, 1);
    const float late = std::abs(tripod.aim().yaw - before) / 0.05f;
    CHECK(early < late * 0.5f);
    CHECK(late <= settings.pan_speed * 1.01f);
    CHECK(tripod.aim().yaw < 0); // pushed right, panned right
}

TEST(tripod_setup_carries_it_over_the_ground) {
    TripodRig tripod;
    tripod.place({0, 0, 0}, {0, 0});
    OperatorInput walk;
    walk.move_y = 1;
    for (int i = 0; i < 60; ++i) tripod.setup(walk, 0.5f, 1.0f / 60, 1);
    CHECK_NEAR(tripod.feet()[2], 2.0, 0.05); // walking pace, the way it faces
    CHECK_NEAR(tripod.feet()[1], 0.5, 1e-6); // stood on the surface found
    tripod.setup(walk, std::nullopt, 1.0f / 60, 1);
    CHECK_NEAR(tripod.feet()[1], 0.5, 1e-6); // no surface found: kept its height
    OperatorInput lower;
    lower.raise = -1;
    for (int i = 0; i < 600; ++i) tripod.setup(lower, 0.5f, 1.0f / 60, 1);
    CHECK_NEAR(tripod.settings().height, TripodRig::lowest, 1e-6);
    OperatorInput strafe;
    strafe.move_x = 1;
    const auto before = tripod.feet();
    for (int i = 0; i < 30; ++i) tripod.setup(strafe, 0.5f, 1.0f / 60, 1);
    CHECK(tripod.feet()[0] < before[0]); // facing +Z, right is -X
    CHECK(!TripodRig().step(nullptr, {}, 1.0f / 60, 1).valid); // not placed: no shot
}

TEST(velocity_estimate_smooths_and_survives_teleports) {
    VelocityEstimate v;
    Vec3 at{};
    for (int i = 0; i < 120; ++i) {
        // 5 m/s along x, with a jittery sample every other tick.
        at[0] += 5.0f / 60;
        v.update({at[0] + (i % 2 ? 0.03f : -0.03f), 0, 0}, 1.0f / 60);
    }
    CHECK_NEAR(v.velocity[0], 5.0, 0.6); // raw differences swing by +-3.6 m/s here
    v.update({500, 0, 0}, 1.0f / 60);    // a teleport
    CHECK(length(v.velocity) == 0);
    v.update({500.1f, 0, 0}, 1.0f / 60);
    CHECK(v.velocity[0] > 0 && v.velocity[0] < 6);
    v.update({NAN, 0, 0}, 1.0f / 60);
    CHECK(finite(v.velocity));
}

TEST(handheld_free_aim_turns_with_the_filmer) {
    HandheldRig rig;
    rig.settings().lock_on = false;
    Shot shot;
    for (int i = 0; i < 60; ++i) shot = rig.step(standing({}, {0, 0, 1}), nullptr, {}, 1.0f / 60, 1);
    CHECK(matrix_forward(shot.camera)[2] > 0.95f);
    // The filmer turns to face +X: a held camera turns with them.
    for (int i = 0; i < 60; ++i) shot = rig.step(standing({}, {1, 0, 0}), nullptr, {}, 1.0f / 60, 1);
    CHECK(matrix_forward(shot.camera)[0] > 0.95f);
    // Aimed off to one side with the stick, it keeps that angle to the body through a turn.
    OperatorInput right;
    right.look_x = 1;
    for (int i = 0; i < 20; ++i) shot = rig.step(standing({}, {1, 0, 0}), nullptr, right, 1.0f / 60, 1);
    for (int i = 0; i < 60; ++i) shot = rig.step(standing({}, {1, 0, 0}), nullptr, {}, 1.0f / 60, 1); // settle
    const auto before = matrix_yaw_pitch(shot.camera).yaw - std::atan2(1.0f, 0.0f);
    for (int i = 0; i < 90; ++i) shot = rig.step(standing({}, {0, 0, -1}), nullptr, {}, 1.0f / 60, 1);
    const auto after = matrix_yaw_pitch(shot.camera).yaw - std::atan2(0.0f, -1.0f);
    CHECK(std::abs(wrap_angle(before - after)) < 2 * degrees);
}

TEST(handheld_face_sets_the_world_aim_then_follows_the_body) {
    HandheldRig rig;
    rig.settings().lock_on = false;
    rig.step(standing({}, {0, 0, 1}), nullptr, {}, 1.0f / 60, 1);
    rig.face({std::atan2(-1.0f, 0.0f), 0}); // look along -X (the game camera's way), body faces +Z
    Shot shot;
    for (int i = 0; i < 30; ++i) shot = rig.step(standing({}, {0, 0, 1}), nullptr, {}, 1.0f / 60, 1);
    CHECK(matrix_forward(shot.camera)[0] < -0.95f);
}

TEST(handheld_heights_are_from_the_feet_whatever_the_root) {
    // The game's root a metre above the feet (the skater's middle), the ground under them at 0.
    for (const float root : {0.0f, 1.0f}) {
        HandheldRig rig;
        auto f = standing({0, root, 0});
        f.on_board = false;
        f.ground = 0.0f;
        Shot shot;
        for (int i = 0; i < 30; ++i) shot = rig.step(f, nullptr, {}, 1.0f / 30, 1);
        CHECK_NEAR(shot.camera[13], 0.45, 0.06); // the long grip: low, whatever the root
        rig.settings().grip = eye_grip();
        shot = rig.step(f, nullptr, {}, 1.0f / 30, 1);
        CHECK_NEAR(shot.camera[13], 1.6, 0.08);
    }
}

TEST(a_jump_lifts_the_camera_with_the_filmer) {
    HandheldRig rig;
    auto f = standing({0, 1, 0});
    f.on_board = false;
    f.ground = 0.0f;
    for (int i = 0; i < 30; ++i) rig.step(f, nullptr, {}, 1.0f / 30, 1);
    f.position = {0, 1.6f, 0}; // an ollie: the root rises, the ground stays
    Shot shot;
    for (int i = 0; i < 10; ++i) shot = rig.step(f, nullptr, {}, 1.0f / 30, 1);
    CHECK(shot.camera[13] > 0.9f); // up with the filmer, not left on the ground
}

TEST(the_handheld_camera_raises_and_lowers) {
    HandheldRig rig;
    auto f = standing();
    f.on_board = false;
    f.ground = 0.0f;
    OperatorInput up;
    up.raise = 1;
    for (int i = 0; i < 30; ++i) rig.step(f, nullptr, up, 1.0f / 30, 1);
    CHECK_NEAR(rig.lens_height(false), 0.45 + 0.8, 0.05); // about 0.8 m in a second
    for (int i = 0; i < 300; ++i) rig.step(f, nullptr, up, 1.0f / 30, 1);
    CHECK_NEAR(rig.lens_height(false), rig.settings().highest, 1e-4); // held overhead, no higher
    OperatorInput down;
    down.raise = -1;
    for (int i = 0; i < 300; ++i) rig.step(f, nullptr, down, 1.0f / 30, 1);
    CHECK_NEAR(rig.lens_height(false), rig.settings().lowest, 1e-4); // a skate's height off the ground
}

TEST(a_swing_overshoots_and_a_spring_does_not) {
    Swing swing;
    Spring spring;
    float most_swing = 0, most_spring = 0;
    for (int i = 0; i < 240; ++i) {
        swing.step(1, 2, 0.4f, 1.0f / 60);
        spring.step(1, 2, 1.0f / 60);
        most_swing = std::max(most_swing, swing.value);
        most_spring = std::max(most_spring, spring.value);
    }
    CHECK(most_swing > 1.1f);       // past the target, then back
    CHECK(most_spring <= 1.0001f);  // never past
    CHECK_NEAR(swing.value, 1, 0.01);
}

TEST(the_camera_rides_steady_motion_without_lagging) {
    // Rolling at a steady 6 m/s: the arms hang straight, the camera where the grip holds it.
    HandheldRig rig;
    auto f = standing();
    f.on_board = true;
    f.ground = 0.0f;
    f.velocity = {0, 0, 6};
    Shot shot;
    for (int i = 0; i < 120; ++i) {
        f.position[2] = 6.0f * i / 60;
        shot = rig.step(f, nullptr, {}, 1.0f / 60, 1);
    }
    const auto grip = rig.grip_position(f);
    CHECK_NEAR(shot.camera[14], grip[2], 0.02);
}

TEST(a_landing_jolts_the_camera_and_it_settles) {
    HandheldRig rig;
    auto f = standing({0, 0, 0});
    f.on_board = true;
    f.ground = 0.0f;
    Shot shot;
    for (int i = 0; i < 60; ++i) rig.step(f, nullptr, {}, 1.0f / 60, 1);
    const float rest = rig.step(f, nullptr, {}, 1.0f / 60, 1).camera[13];
    // Dropping off a ledge at 4 m/s for a third of a second (the arms settle in the fall), then
    // down on the ground over three frames: the arms carry on a moment and the camera dips.
    f.velocity = {0, -4, 0};
    for (int i = 0; i < 20; ++i) rig.step(f, nullptr, {}, 1.0f / 60, 1);
    float lowest = rig.step(f, nullptr, {}, 1.0f / 60, 1).camera[13];
    const float falling = lowest;
    for (const float v : {-2.5f, -1.0f, 0.0f}) {
        f.velocity = {0, v, 0};
        lowest = std::min(lowest, rig.step(f, nullptr, {}, 1.0f / 60, 1).camera[13]);
    }
    for (int i = 0; i < 20; ++i) lowest = std::min(lowest, rig.step(f, nullptr, {}, 1.0f / 60, 1).camera[13]);
    CHECK(lowest < falling - 0.03f); // a visible dip below where it rode the fall
    for (int i = 0; i < 120; ++i) shot = rig.step(f, nullptr, {}, 1.0f / 60, 1);
    CHECK_NEAR(shot.camera[13], rest, 0.01); // and back
}

TEST(a_flick_of_the_aim_carries_on_a_little_and_settles) {
    HandheldRig rig;
    auto f = standing();
    rig.face({0, 0});
    rig.step(f, nullptr, {}, 1.0f / 60, 1);
    OperatorInput flick;
    flick.look_x = 1; // full right for a fifth of a second
    for (int i = 0; i < 12; ++i) rig.step(f, nullptr, flick, 1.0f / 60, 1);
    const float let_go = rig.aim().yaw;
    float furthest = let_go;
    for (int i = 0; i < 90; ++i) {
        rig.step(f, nullptr, {}, 1.0f / 60, 1);
        furthest = std::min(furthest, rig.aim().yaw); // right is negative yaw
    }
    const float settled = rig.aim().yaw;
    CHECK(furthest < settled - 0.01f); // it swung past where the hands stopped
    CHECK(std::abs(rig.aim().yaw - settled) < 1e-3f);
}

TEST(closest_skater_is_filmed_without_flicking_between_two) {
    const std::vector<Vec3> skaters{{10, 0, 0}, {4, 0, 0}, {-5.5f, 0, 0}};
    CHECK(closest_skater({0, 0, 0}, skaters, -1) == 1);
    CHECK(closest_skater({0, 0, 0}, {}, -1) == -1);
    // Filming the one at 5.5 m: the one at 4 m is not 2 m nearer, so the camera stays.
    CHECK(closest_skater({0, 0, 0}, skaters, 2) == 2);
    CHECK(closest_skater({0, 0, 0}, {{10, 0, 0}, {3, 0, 0}, {-6, 0, 0}}, 2) == 1); // 3 m clearly is
    CHECK(closest_skater({0, 0, 0}, skaters, 7) == 1);                             // gone: the nearest
}

TEST(handheld_shake_does_not_repeat) {
    // A sum of sines comes back to where it was; noise doesn't. Compare a second of shake with
    // every later second over two minutes: none matches it closely.
    HandShake shake(11);
    std::vector<float> yaw;
    for (int i = 0; i < 60 * 120; ++i) yaw.push_back(shake.step(1.0f / 60, 0, false, 0.5f).yaw);
    float closest = 1e9f;
    for (std::size_t start = 60; start + 60 <= yaw.size(); start += 7) {
        float difference = 0;
        for (std::size_t i = 0; i < 60; ++i) difference += std::abs(yaw[start + i] - yaw[i]);
        closest = std::min(closest, difference / 60);
    }
    float size = 0;
    for (std::size_t i = 0; i < 60; ++i) size += std::abs(yaw[i]);
    CHECK(closest > 0.15f * size / 60);
}

TEST(handheld_tilts_for_a_dutch_angle_and_comes_back_level) {
    HandheldRig rig;
    Filmer f;
    f.valid = true;
    f.position = {0, 1, 0};
    OperatorInput tilt;
    tilt.tilt = 1;
    const auto roll_of = [](const Shot &shot) { return std::atan2(shot.camera[1], shot.camera[5]); }; // right's up over up's up
    Shot shot;
    for (int i = 0; i < 120; ++i) shot = rig.step(f, nullptr, tilt, 1.0f / 60, 1);
    CHECK(std::abs(roll_of(shot)) > 20 * degrees);
    for (int i = 0; i < 240; ++i) shot = rig.step(f, nullptr, {}, 1.0f / 60, 1);
    CHECK(std::abs(roll_of(shot)) < 2 * degrees);
}

TEST(a_tick_with_no_time_keeps_the_velocity) {
    VelocityEstimate v;
    Vec3 at{};
    for (int i = 0; i < 60; ++i) {
        at[0] += 8.0f / 60;
        v.update(at, 1.0f / 60);
    }
    CHECK(v.velocity[0] > 7);
    v.update(at, 0); // a second call in the same instant
    CHECK(v.velocity[0] > 7);
}

TEST(framing_is_measured_from_the_feet) {
    // A root 0.9 m above the feet (around the hips, as the game's is): the whole skater is 0.8 m up
    // from the feet, not 0.8 m above the hips (their face).
    Subject s{true, {0, 10.9f, 0}, {2, 0, 0}, 0.9f};
    CHECK_NEAR(framing_point(s, 0.8f)[1], 10.8f, 1e-5);
    CHECK_NEAR(framing_point(s, 0.35f)[1], 10.35f, 1e-5); // the board
    CHECK_NEAR(framing_point(s, 0.8f, 0.5f)[0], 1.0f, 1e-5); // half a second ahead
    s.root_height.reset();
    CHECK_NEAR(framing_point(s, 0.8f)[1], 11.7f, 1e-5); // unknown: from the root, as before
    // The root's height follows a landing at once, and a jump hardly at all.
    RootHeight h;
    CHECK(!h.update(std::nullopt, 0.016f));
    CHECK_NEAR(*h.update(0.9f, 0.016f), 0.9, 1e-6);
    for (int i = 0; i < 30; ++i) h.update(1.9f, 0.016f); // half a second in the air, a metre up
    CHECK(*h.value < 1.05f);
    for (int i = 0; i < 20; ++i) h.update(0.6f, 0.016f); // crouching on landing
    CHECK(*h.value < 0.62f);
    CHECK_NEAR(*h.update(std::nullopt, 0.016f), *h.value, 0); // no ground found: kept
}
