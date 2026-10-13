#include "camcorder/viewfinder.h"
#include "check.h"
#include <algorithm>

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
std::size_t marks(const Viewfinder &v, Mark::Kind kind) {
    return static_cast<std::size_t>(std::count_if(v.marks.begin(), v.marks.end(), [&](const Mark &m) { return m.kind == kind; }));
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
    CHECK(marks(v, Mark::Kind::dot) == 0);
    CHECK(marks(v, Mark::Kind::outline) == 1); // the battery
    CHECK(marks(v, Mark::Kind::box) == 5);     // its nub and four bars
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
    CHECK(marks(v, Mark::Kind::dot) == 1);
    CHECK(has_label(v, "REC"));
    for (const auto &l : v.labels)
        if (l.text == "REC") CHECK(l.colour == (Colour{235, 40, 40, 255}));
    CHECK(!has_label(v, "STBY"));
}

TEST(clean_feed_keeps_the_frame_and_lens_only) {
    ViewfinderOptions options;
    options.clean_feed = true;
    const auto v = layout_viewfinder(on_frame(), 1920, 1080, options);
    CHECK(v.visible);
    CHECK(v.labels.empty());
    CHECK(v.marks.empty());
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

TEST(the_vx1000_viewfinder_sits_where_it_did) {
    // 1440 x 1080 picture: a line is 2.25 pixels; readouts 30 lines in, 22 tall.
    const auto v = layout_viewfinder(on_frame(), 1920, 1080);
    for (const auto &l : v.labels) {
        if (l.text == "STBY") CHECK(std::abs(l.x - (240 + 67.5f)) < 0.01f && std::abs(l.y - 67.5f) < 0.01f && std::abs(l.size - 49.5f) < 0.01f);
        if (l.text == "0:00:00") CHECK(l.align == Align::right && std::abs(l.x - (1680 - 67.5f)) < 0.01f);
    }
}

TEST(a_camera_lays_out_its_own_viewfinder) {
    ViewfinderLayout layout;
    layout.text = {0, 255, 0};
    ViewfinderElement zoom;
    zoom.text = "{zoom}x {focal}mm";
    zoom.anchor = Anchor::bottom;
    zoom.offset = {0, 20};
    ViewfinderElement rec = zoom;
    rec.text = "{status} {counter}";
    rec.anchor = Anchor::top_right;
    rec.when = {{Condition::recording, true}};
    ViewfinderElement guides;
    guides.show = Show::grid;
    guides.size = 3;
    ViewfinderElement clock;
    clock.text = "{date} {time}";
    clock.anchor = Anchor::centre;
    clock.blink = 1;
    layout.elements = {zoom, rec, guides, clock};
    ViewfinderOptions options;
    options.layout = &layout;
    std::tm when{};
    when.tm_year = 96;
    when.tm_mon = 9;
    when.tm_mday = 12;
    when.tm_hour = 16;
    when.tm_min = 3;
    options.clock = when;
    auto v = layout_viewfinder(on_frame(), 1920, 1080, options);
    CHECK(has_label(v, "1.0x 5.9mm"));
    CHECK(!has_label(v, "STBY") && !has_label(v, "STBY 0:00:00")); // not recording; and not the VX1000's
    CHECK(marks(v, Mark::Kind::line) == 4);                       // thirds: two lines each way
    bool clock_found = false;
    for (const auto &l : v.labels) {
        if (l.text == "1.0x 5.9mm") {
            CHECK(l.align == Align::centre && std::abs(l.x - 960) < 0.01f);
            CHECK(std::abs(l.y + l.size - (1080 - 20 * 2.25f)) < 0.01f); // its bottom 20 lines up
            CHECK(l.colour == (Colour{0, 255, 0, 255}));
        }
        if (l.text.find("OCT 12 1996") != std::string::npos) clock_found = l.text.find("4:03") != std::string::npos;
    }
    CHECK(clock_found);
    // Blinking: off for the second half of each second.
    options.blink_time = 0.75f;
    v = layout_viewfinder(on_frame(), 1920, 1080, options);
    CHECK(!std::any_of(v.labels.begin(), v.labels.end(), [](const Label &l) { return l.text.find("1996") != std::string::npos; }));
    // Unknown fields stay as written.
    CHECK(viewfinder_text("{nope} {counter}", on_frame()) == "{nope} 0:00:00");
}
