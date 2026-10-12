#include "camcorder/look.h"
#include "check.h"
#include <cstdlib>

using namespace dingosdk::camcorder;

namespace {
Image gradient(int w, int h) {
    Image image;
    image.resize(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto *p = image.pixels.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            p[0] = static_cast<std::uint8_t>(x * 255 / (w - 1));
            p[1] = static_cast<std::uint8_t>(y * 255 / (h - 1));
            p[2] = static_cast<std::uint8_t>((x + y) * 255 / (w + h - 2));
            p[3] = 255;
        }
    return image;
}
const std::uint8_t *pixel(const Image &image, int x, int y) {
    return image.pixels.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
}
std::tm moment(int year, int month, int day, int hour, int minute, int second) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return t;
}
} // namespace

TEST(clips_with_fields_run_at_59_94) {
    Finish finish;
    CHECK_NEAR(clip_frame_rate(finish), 30000.0 / 1001, 1e-9);
    finish.dv.on = true;
    CHECK_NEAR(clip_frame_rate(finish), 60000.0 / 1001, 1e-9);
    finish.dv.fields = false;
    CHECK_NEAR(clip_frame_rate(finish), 30000.0 / 1001, 1e-9);
}

TEST(dv_look_off_leaves_the_picture_alone) {
    auto image = gradient(64, 48);
    const auto before = image.pixels;
    DvLook look; // off
    apply_dv(image, look, LookProfile{}, {}, 3, 59.94);
    CHECK(image.pixels == before);
}

TEST(a_field_fills_the_other_lines_from_its_own) {
    DvLook look;
    look.on = true;
    look.highlights = look.white_balance = look.sharpen = look.chroma = look.grain = false;
    for (int parity = 0; parity < 2; ++parity) {
        auto image = gradient(32, 24);
        const auto before = image;
        apply_dv(image, look, LookProfile{}, {}, parity, 59.94);
        for (int y = 0; y < 24; ++y) {
            if (y % 2 == parity) {
                CHECK(std::equal(pixel(image, 0, y), pixel(image, 0, y) + 32 * 4, pixel(before, 0, y)));
            } else {
                const int above = y > 0 ? y - 1 : y + 1, below = y < 23 ? y + 1 : y - 1;
                for (int c = 0; c < 3; ++c)
                    CHECK(pixel(image, 5, y)[c] == (pixel(before, 5, above)[c] + pixel(before, 5, below)[c] + 1) / 2);
            }
        }
    }
}

TEST(highlights_clip_white_early_and_blacks_get_dense) {
    DvLook look;
    look.on = true;
    look.fields = look.white_balance = look.sharpen = look.chroma = look.grain = false;
    Image image;
    image.resize(3, 1);
    const std::uint8_t values[3][3] = {{8, 8, 8}, {240, 240, 240}, {120, 160, 250}};
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c) image.pixels[static_cast<std::size_t>(i * 4 + c)] = values[i][c];
    apply_dv(image, look, LookProfile{}, {}, 0, 29.97);
    CHECK(pixel(image, 0, 0)[0] < 8);   // dark goes darker
    CHECK(pixel(image, 1, 0)[0] == 255); // bright white clips
    // A blown channel pulls the others toward white.
    CHECK(pixel(image, 2, 0)[2] == 255);
    CHECK(pixel(image, 2, 0)[0] > 132);
}

TEST(white_balance_wanders_over_a_clip) {
    DvLook look;
    look.on = true;
    look.fields = look.highlights = look.sharpen = look.chroma = look.grain = false;
    Image grey;
    grey.resize(4, 4);
    std::fill(grey.pixels.begin(), grey.pixels.end(), std::uint8_t{128});
    int lowest = 255, highest = 0;
    for (int second = 0; second < 40; ++second) {
        auto image = grey;
        apply_dv(image, look, LookProfile{}, {0.3f}, static_cast<std::int64_t>(second * 30), 30.0);
        const int red_minus_blue = image.pixels[2] - image.pixels[0];
        lowest = std::min(lowest, red_minus_blue);
        highest = std::max(highest, red_minus_blue);
        CHECK(image.pixels[1] == 128); // green is the reference
    }
    CHECK(highest - lowest >= 8);  // it does drift
    CHECK(highest - lowest <= 40); // but stays a cast, not a colour change
}

TEST(sharpening_rings_on_an_edge) {
    DvLook look;
    look.on = true;
    look.fields = look.highlights = look.white_balance = look.chroma = look.grain = false;
    Image image;
    image.resize(16, 1);
    for (int x = 0; x < 16; ++x) std::fill_n(image.pixels.begin() + x * 4, 3, static_cast<std::uint8_t>(x < 8 ? 60 : 180));
    apply_dv(image, look, LookProfile{}, {}, 0, 29.97);
    CHECK(pixel(image, 7, 0)[0] < 60);  // the dark side darker at the edge
    CHECK(pixel(image, 8, 0)[0] > 180); // the bright side brighter
    CHECK(pixel(image, 2, 0)[0] == 60); // flat areas untouched
}

TEST(chroma_smears_colour_but_keeps_brightness) {
    DvLook look;
    look.on = true;
    look.fields = look.highlights = look.white_balance = look.sharpen = look.grain = false;
    Image image;
    image.resize(40, 1);
    for (int x = 0; x < 40; ++x) {
        auto *p = image.pixels.data() + x * 4;
        // A red bar on grey of about the same brightness.
        if (x >= 18 && x < 22) { p[0] = 40; p[1] = 40; p[2] = 230; }
        else { p[0] = p[1] = p[2] = 80; }
    }
    const auto luma = [](const std::uint8_t *p) { return 0.2126 * p[2] + 0.7152 * p[1] + 0.0722 * p[0]; };
    std::vector<double> before;
    for (int x = 0; x < 40; ++x) before.push_back(luma(pixel(image, x, 0)));
    apply_dv(image, look, LookProfile{}, {}, 0, 29.97);
    CHECK(pixel(image, 16, 0)[2] > pixel(image, 16, 0)[0] + 10); // colour reaches past the bar
    CHECK(pixel(image, 19, 0)[2] < 230);                          // and is weaker in it
    for (int x = 0; x < 40; ++x) CHECK(std::abs(luma(pixel(image, x, 0)) - before[static_cast<std::size_t>(x)]) < 4);
    CHECK(pixel(image, 2, 0)[2] == 80); // far away nothing changes
}

TEST(date_stamp_reads_like_the_camera) {
    const auto lines = date_stamp_lines(moment(2026, 10, 12, 16, 3, 22), 0);
    CHECK(lines[0] == "4:03:22 PM");
    CHECK(lines[1] == "OCT 12 2026");
    CHECK(date_stamp_lines(moment(2026, 1, 5, 0, 7, 9), 1996)[0] == "12:07:09 AM");
    CHECK(date_stamp_lines(moment(2026, 1, 5, 0, 7, 9), 1996)[1] == "JAN 5 1996");
    CHECK(date_stamp_lines(moment(2026, 12, 31, 12, 0, 0), 0)[0] == "12:00:00 PM");
}

TEST(date_stamp_burns_into_the_bottom_right_only) {
    for (const auto size : {std::pair{640, 480}, std::pair{1440, 1080}}) {
        Image image;
        image.resize(size.first, size.second);
        std::fill(image.pixels.begin(), image.pixels.end(), std::uint8_t{100});
        burn_date(image, moment(1996, 7, 4, 15, 30, 0), 0);
        int changed = 0, white = 0;
        bool outside = false;
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x) {
                const auto *p = pixel(image, x, y);
                if (p[0] == 100) continue;
                ++changed;
                white += p[0] > 200;
                outside = outside || x < image.width / 2 || y < image.height * 2 / 3 || x >= image.width - 4 || y >= image.height - 4;
            }
        CHECK(white > 200);
        CHECK(changed > white);
        CHECK(!outside);
    }
}

TEST(a_finished_frame_has_the_stamp_and_the_look) {
    Finish finish;
    finish.date_stamp = true;
    auto plain = gradient(640, 480);
    auto stamped = plain;
    finish_frame(stamped, finish, {}, 0, 29.97, moment(1996, 7, 4, 15, 30, 0));
    CHECK(stamped.pixels != plain.pixels);
    finish.dv.on = true;
    auto both = plain;
    finish_frame(both, finish, {}, 0, 59.94, moment(1996, 7, 4, 15, 30, 0));
    CHECK(both.pixels != stamped.pixels);
}

TEST(the_screen_look_follows_the_camera_and_the_switches) {
    Finish finish;
    CHECK(!screen_look(finish, {}, 0).on); // off: nothing
    finish.dv.on = true;
    finish.look.grain = 0.3f;
    finish.look.softness = 1.5f;
    auto s = screen_look(finish, {0.2f}, 5);
    CHECK(s.on);
    CHECK_NEAR(s.black_crush, finish.look.black_crush, 1e-6);
    CHECK_NEAR(s.highlight_gain, 1.15, 1e-6);
    CHECK(s.highlight_bleed);
    CHECK_NEAR(s.sharpen, 0.6, 1e-6);
    CHECK_NEAR(s.grain, 0.3, 1e-6);
    CHECK_NEAR(s.chroma_width, 4, 1e-6);
    CHECK_NEAR(s.softness, 1.5, 1e-6);
    CHECK(s.red_gain != 1 && std::abs((s.red_gain - 1) + (s.blue_gain - 1)) < 1e-5); // a cast, red against blue
    // Each switch puts its part back to neutral.
    finish.dv.highlights = finish.dv.white_balance = finish.dv.sharpen = finish.dv.grain = finish.dv.chroma = false;
    s = screen_look(finish, {0.2f}, 5);
    CHECK(s.on && s.black_crush == 0 && s.highlight_gain == 1 && !s.highlight_bleed && s.saturation == 1);
    CHECK(s.red_gain == 1 && s.blue_gain == 1 && s.sharpen == 0 && s.grain == 0 && s.chroma_width == 0 && s.softness == 0);
}

TEST(film_cameras_move_at_their_own_rate_on_screen) {
    Finish finish;
    CHECK(screen_frame_rate(finish) == 0); // video: every game frame
    finish.motion = Motion::p18;
    CHECK_NEAR(screen_frame_rate(finish), 18, 1e-9);
    finish.motion = Motion::p24;
    CHECK_NEAR(screen_frame_rate(finish), 24000.0 / 1001, 1e-9);
}

TEST(a_recording_skips_what_the_screen_already_did) {
    Finish finish;
    finish.dv.on = true;
    finish.date_stamp = true;
    const auto rest = recorded_finish(finish, true, true);
    CHECK(rest.dv.on && rest.dv.fields); // the fields are the clip's own
    CHECK(!rest.dv.highlights && !rest.dv.white_balance && !rest.dv.sharpen && !rest.dv.chroma && !rest.dv.grain);
    CHECK(!rest.date_stamp);
    CHECK(recorded_finish(finish, false, false) == finish);
}

TEST(stamp_marks_are_what_burn_date_draws) {
    const auto when = moment(1996, 7, 4, 15, 30, 0);
    const auto marks = stamp_marks(640, 480, when, 0);
    CHECK(!marks.dots.empty() && marks.edges.size() == marks.dots.size());
    for (const auto &d : marks.dots) CHECK(d.x >= 320 && d.y >= 320 && d.x + d.width <= 640 && d.y + d.height <= 480);
    StampProfile left;
    left.corner = StampProfile::Corner::bottom_left;
    for (const auto &d : stamp_marks(640, 480, when, 0, left).dots) CHECK(d.x < 320);
    CHECK(stamp_marks(32, 24, when, 0).dots.empty());
}

TEST(the_camera_clock_can_be_set) {
    const std::time_t now = 1'700'000'000; // November 2023
    const auto offset = clock_offset("1996-10-12", "16:03", now);
    CHECK(offset.has_value());
    Finish finish;
    finish.stamp_offset = *offset;
    std::tm local{};
    const auto shown = stamp_clock(now, finish);
#ifdef _WIN32
    localtime_s(&local, &shown);
#else
    localtime_r(&shown, &local);
#endif
    CHECK(local.tm_year == 96 && local.tm_mon == 9 && local.tm_mday == 12 && local.tm_hour == 16 && local.tm_min == 3);
    // It runs on: a minute later, the stamp is a minute later.
    CHECK(stamp_clock(now + 60, finish) - shown == 60);
    CHECK(!clock_offset("1996-02-31", "", now)); // no such day
    CHECK(!clock_offset("12/10/1996", "", now));
    CHECK(!clock_offset("1996-10-12", "25:00", now));
    CHECK(clock_offset("1996-10-12", "", now).has_value()); // the time of day kept
}
