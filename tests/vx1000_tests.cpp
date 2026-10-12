#include "camcorder/vx1000.h"
#include "check.h"

using namespace dingosdk::camcorder;

TEST(focal_length_spans_the_ten_to_one_zoom) {
    CHECK_NEAR(focal_length(0), 5.9, 1e-4);
    CHECK_NEAR(focal_length(1), 59.0, 1e-3);
    CHECK_NEAR(focal_length(0.5f), std::sqrt(5.9 * 59.0), 1e-3); // equal steps, equal ratios
    CHECK_NEAR(focal_length(-3), 5.9, 1e-4);
    CHECK_NEAR(focal_length(NAN), 5.9, 1e-4);
}

TEST(stock_lens_angles_match_a_third_inch_sensor) {
    const auto wide = optics(Adapter::none, 0);
    CHECK_NEAR(wide.horizontal_fov, 44.27, 0.05);
    CHECK_NEAR(wide.vertical_fov, 33.96, 0.05);
    CHECK_NEAR(wide.render_vertical_fov, wide.vertical_fov, 1e-4);
    CHECK(!wide.fisheye);
    CHECK(wide.vignette_radius == 0);
    const auto tele = optics(Adapter::none, 1);
    CHECK_NEAR(tele.horizontal_fov, 4.66, 0.02);
    CHECK_NEAR(tele.zoom_ratio, 10.0, 1e-3);
}

TEST(century_fisheye_is_125_degrees_with_round_corners_at_wide) {
    const auto wide = optics(Adapter::century_fisheye, 0);
    CHECK(wide.fisheye);
    CHECK_NEAR(wide.horizontal_fov, 125.0, 1e-3);
    CHECK_NEAR(wide.vertical_fov, 93.75, 1e-3);
    CHECK(wide.vignette_radius > 1.0f && wide.vignette_radius < 1.25f); // cuts the corners only
    // The approximate render gives the flat camera the fisheye's height, within reason.
    CHECK(wide.render_vertical_fov <= 100.0f);
    CHECK_NEAR(wide.render_vertical_fov, 93.75, 1e-3);
}

TEST(fisheye_shadow_clears_after_a_touch_of_zoom) {
    // Zoom position for a 1.6x ratio.
    const float zoom = std::log(1.6f) / std::log(10.0f);
    const auto o = optics(Adapter::century_fisheye, zoom);
    CHECK_NEAR(o.zoom_ratio, 1.6, 1e-3);
    CHECK(o.vignette_radius == 0);
    const auto slight = optics(Adapter::century_fisheye, std::log(1.2f) / std::log(10.0f));
    CHECK(slight.vignette_radius > optics(Adapter::century_fisheye, 0).vignette_radius);
}

TEST(fisheye_source_is_equidistant) {
    const auto o = optics(Adapter::century_fisheye, 0);
    const auto centre = fisheye_source(o, 0, 0);
    CHECK_NEAR(centre.x, 0, 1e-6);
    CHECK_NEAR(centre.y, 0, 1e-6);
    const auto edge = fisheye_source(o, 1, 0);
    CHECK_NEAR(edge.x, std::tan(62.5 * 3.14159265 / 180), 1e-3);
    CHECK_NEAR(edge.y, 0, 1e-6);
    // Half way out is half the angle: a fisheye squeezes the edges.
    const auto half = fisheye_source(o, 0.5f, 0);
    CHECK_NEAR(half.x, std::tan(31.25 * 3.14159265 / 180), 1e-3);
}

TEST(warp_render_covers_every_visible_point) {
    for (const float aspect : {16.0f / 9.0f, 4.0f / 3.0f, 21.0f / 9.0f}) {
        for (const float zoom : {0.0f, 0.1f, 0.3f}) {
            const auto o = optics(Adapter::century_fisheye, zoom, FisheyeRender::warp, aspect);
            const double tan_v = std::tan(o.render_vertical_fov * 0.5 * 3.14159265 / 180);
            const double tan_h = tan_v * aspect;
            const float radius = o.vignette_radius > 0 ? o.vignette_radius : 1.25f;
            double closest = 1e9;
            for (int i = 0; i <= 80; ++i)
                for (int j = 0; j <= 60; ++j) {
                    const float x = -1 + 2.0f * i / 80, y = -0.75f + 1.5f * j / 60;
                    if (x * x + y * y > radius * radius) continue;
                    const auto t = fisheye_source(o, x, y);
                    CHECK(std::abs(t.x) <= tan_h * 1.001 + 1e-4);
                    CHECK(std::abs(t.y) <= tan_v * 1.001 + 1e-4);
                    closest = std::min({closest, tan_h - std::abs(t.x), tan_v - std::abs(t.y)});
                }
            // And not much wider than it needs to be (pixels spent off screen are pixels lost).
            CHECK(closest < 0.05 * tan_v);
        }
    }
}

TEST(zoom_servo_runs_end_to_end_in_the_spec_times) {
    const auto travel = [](float rocker) {
        ZoomServo servo;
        float t = 0;
        while (servo.position < 0.999f && t < 60) {
            servo.step(rocker, 1.0f / 60);
            t += 1.0f / 60;
        }
        return t;
    };
    const float fast = travel(1), slow = travel(0.0801f);
    CHECK(fast > 4.1f && fast < 4.4f);
    CHECK(slow > 19.5f && slow < 20.5f);
    ZoomServo servo;
    servo.position = 1;
    servo.step(-1, 0.1f);
    CHECK(servo.position < 1);
    CHECK(servo.shown_for > 0);
}

TEST(zoom_servo_stops_soon_after_release_and_hides_the_bar) {
    ZoomServo servo;
    for (int i = 0; i < 60; ++i) servo.step(1, 1.0f / 60);
    const float held = servo.position;
    for (int i = 0; i < 30; ++i) servo.step(0, 1.0f / 60);
    CHECK(servo.position - held < 0.03f); // coasts a little, then stops
    CHECK(std::abs(servo.speed) < 1e-3f);
    for (int i = 0; i < 180; ++i) servo.step(0, 1.0f / 60);
    CHECK(servo.shown_for == 0);
    servo.step(0.05f, 1.0f / 60); // inside the deadzone
    CHECK(servo.shown_for == 0);
}

TEST(warp_uv_maps_the_picture_into_the_flat_render) {
    const float aspect = 16.0f / 9.0f;
    const auto o = optics(Adapter::century_fisheye, 0.0f, FisheyeRender::warp, aspect);
    CHECK(o.warped);
    CHECK(!optics(Adapter::century_fisheye, 0.0f, FisheyeRender::approximate, aspect).warped);
    const auto centre = warp_uv(o, 0, 0, aspect);
    CHECK_NEAR(centre.u, 0.5, 1e-6);
    CHECK_NEAR(centre.v, 0.5, 1e-6);
    // Right is right, up is up (v grows down the texture).
    CHECK(warp_uv(o, 0.5f, 0, aspect).u > 0.5f);
    CHECK(warp_uv(o, 0, 0.5f, aspect).v < 0.5f);
    // Every point the housing leaves visible lands inside the render.
    const float radius = o.vignette_radius > 0 ? o.vignette_radius : 1.25f;
    for (int i = 0; i <= 40; ++i)
        for (int j = 0; j <= 30; ++j) {
            const float x = -1 + 2.0f * i / 40, y = -0.75f + 1.5f * j / 30;
            if (x * x + y * y > radius * radius) continue;
            const auto uv = warp_uv(o, x, y, aspect);
            CHECK(uv.u >= -1e-3f && uv.u <= 1.001f && uv.v >= -1e-3f && uv.v <= 1.001f);
        }
    // The fisheye squeezes the edges: equal steps out from the centre cover more and more render.
    const float a = warp_uv(o, 0.25f, 0, aspect).u - 0.5f, b = warp_uv(o, 0.5f, 0, aspect).u - 0.5f;
    CHECK(b > 2 * a);
}
