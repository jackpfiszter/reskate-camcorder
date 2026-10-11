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
    rig.settings().grip = Grip::long_grip;
    CHECK_NEAR(rig.grip_position(f)[1], 10.45, 1e-4);
    rig.settings().grip = Grip::eye_level;
    CHECK_NEAR(rig.grip_position(f)[1], 11.62, 1e-4);
    // Held in the right hand: facing +Z, right is -X.
    rig.settings().grip = Grip::hip;
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
