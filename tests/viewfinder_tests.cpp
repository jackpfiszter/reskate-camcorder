#include "camcorder/viewfinder.h"
#include "check.h"

using namespace dingosdk::camcorder;

namespace {
Frame on_frame(std::optional<Adapter> adapter = century_fisheye()) {
    Camcorder cam;
    cam.set_adapter(adapter);
    cam.power(true);
    TickInput in;
    in.dt = 1.0f / 60;
    in.filmer.valid = true;
    return cam.tick(in);
}
bool has_label(const Viewfinder &v, const std::string &text) {
    for (const auto &l : v.labels)
        if (l.text == text) return true;
    return false;
}
} // namespace

TEST(picture_is_four_by_three_in_any_window) {
    const auto wide = picture_rect(1920, 1080);
    CHECK_NEAR(wide.x, 240, 1e-3);
    CHECK_NEAR(wide.width, 1440, 1e-3);
    CHECK_NEAR(wide.height, 1080, 1e-3);
    const auto square = picture_rect(800, 600);
    CHECK_NEAR(square.x, 0, 1e-3);
    CHECK_NEAR(square.y, 0, 1e-3);
    const auto tall = picture_rect(600, 1000);
    CHECK_NEAR(tall.width, 600, 1e-3);
    CHECK_NEAR(tall.height, 450, 1e-3);
    CHECK_NEAR(tall.y, 275, 1e-3);
    CHECK_NEAR(picture_rect(0, 100).width, 0, 0);
    // Any other shape: a 16:9 camera fills a 16:9 screen, a square one is pillarboxed.
    const auto sixteen = picture_rect(1920, 1080, 16.0f / 9.0f);
    CHECK_NEAR(sixteen.x, 0, 1e-3);
    CHECK_NEAR(sixteen.width, 1920, 1e-3);
    CHECK_NEAR(picture_rect(1920, 1080, 1).width, 1080, 1e-3);
}

TEST(viewfinder_frames_the_cameras_shape) {
    auto frame = on_frame();
    frame.optics.aspect = 16.0f / 9.0f;
    const auto v = layout_viewfinder(frame, 1920, 1080);
    CHECK_NEAR(v.picture.width, 1920, 1e-3);
    CHECK(v.bars.empty());
}

TEST(viewfinder_shows_standby_counter_and_tape) {
    const auto v = layout_viewfinder(on_frame(), 1920, 1080);
    CHECK(v.visible);
    CHECK(v.bars.size() == 2);
    CHECK(has_label(v, "STBY"));
    CHECK(has_label(v, "0:00:00"));
    CHECK(has_label(v, "60min"));
    CHECK(!v.rec_dot);
    CHECK(v.battery.visible);
    CHECK(v.vignette.has_value()); // fisheye at full wide
    CHECK_NEAR(v.vignette->x, 960, 1e-3);
}

TEST(viewfinder_goes_red_when_recording) {
    Camcorder cam;
    cam.toggle_record();
    TickInput in;
    in.dt = 1.0f / 60;
    in.filmer.valid = true;
    const auto v = layout_viewfinder(cam.tick(in), 1920, 1080);
    CHECK(v.rec_dot);
    CHECK(has_label(v, "REC"));
    CHECK(!has_label(v, "STBY"));
}

TEST(clean_feed_keeps_the_frame_and_lens_only) {
    ViewfinderOptions options;
    options.clean_feed = true;
    const auto v = layout_viewfinder(on_frame(), 1920, 1080, options);
    CHECK(v.visible);
    CHECK(v.labels.empty());
    CHECK(!v.battery.visible);
    CHECK(v.bars.size() == 2);
    CHECK(v.vignette.has_value());
    const auto stock = layout_viewfinder(on_frame(std::nullopt), 1920, 1080, options);
    CHECK(!stock.vignette.has_value());
}

TEST(viewfinder_hides_when_the_camera_is_off) {
    Frame off;
    CHECK(!layout_viewfinder(off, 1920, 1080).visible);
}

TEST(viewfinder_shows_control_hints_but_not_in_the_clean_feed) {
    ViewfinderOptions options;
    options.hint = "Mouse: aim   Q / E: zoom";
    const auto v = layout_viewfinder(on_frame(), 1920, 1080, options);
    bool found = false;
    for (const auto &l : v.labels)
        if (l.text == options.hint) {
            found = true;
            CHECK(l.align == Align::centre);
            CHECK_NEAR(l.x, 960, 1e-3);
            CHECK(l.y > v.picture.y + v.picture.height * 0.85f && l.y < v.picture.y + v.picture.height);
        }
    CHECK(found);
    options.clean_feed = true;
    CHECK(layout_viewfinder(on_frame(), 1920, 1080, options).labels.empty());
}
