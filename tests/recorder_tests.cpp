#include "camcorder/recorder.h"
#include "check.h"
#include <cstring>

using namespace dingosdk::camcorder;

namespace {
Image solid(int w, int h, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    Image image;
    image.resize(w, h);
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        image.pixels[i] = b;
        image.pixels[i + 1] = g;
        image.pixels[i + 2] = r;
        image.pixels[i + 3] = 255;
    }
    return image;
}
const std::uint8_t *pixel(const Image &image, int x, int y) {
    return image.pixels.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
}
std::uint16_t half(float v) { // enough for the test values (normal, positive)
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    if (v == 0) return 0;
    return static_cast<std::uint16_t>((((bits >> 23) & 0xff) - 112) << 10 | ((bits >> 13) & 0x3ff));
}
} // namespace

TEST(capture_box_is_the_even_sized_four_by_three_picture) {
    CHECK(capture_box(1920, 1080) == (PixelBox{240, 0, 1440, 1080}));
    CHECK(capture_box(2560, 1440) == (PixelBox{320, 0, 1920, 1440}));
    CHECK(capture_box(1024, 768) == (PixelBox{0, 0, 1024, 768}));
    const auto odd = capture_box(1366, 767);
    CHECK(odd.width % 2 == 0 && odd.height % 2 == 0);
    CHECK(odd.x + odd.width <= 1366 && odd.y + odd.height <= 767);
    CHECK(capture_box(1, 1) == PixelBox{});
}

TEST(video_sizes_for_hd_and_dv) {
    CHECK(video_size(Quality::hd, {320, 0, 1920, 1440}) == (VideoSize{1440, 1080}));
    CHECK(video_size(Quality::hd, {240, 0, 1440, 1080}) == (VideoSize{1440, 1080}));
    CHECK(video_size(Quality::hd, {0, 0, 800, 600}) == (VideoSize{800, 600}));
    CHECK(video_size(Quality::dv, {240, 0, 1440, 1080}) == (VideoSize{640, 480}));
    CHECK(video_bitrate({1440, 1080}) > 8000000 && video_bitrate({1440, 1080}) < 20000000);
    CHECK(video_bitrate({640, 480}) < video_bitrate({1440, 1080}));
}

TEST(convert_reads_every_back_buffer_format) {
    // One pixel each, orange: R 255, G 128, B 0.
    Image out;
    const std::uint8_t rgba[4]{255, 128, 0, 7};
    convert_to_bgra(rgba, 4, 1, 1, SourceFormat::rgba8, out);
    CHECK(out.pixels == (std::vector<std::uint8_t>{0, 128, 255, 255}));
    const std::uint8_t bgra[4]{0, 128, 255, 7};
    convert_to_bgra(bgra, 4, 1, 1, SourceFormat::bgra8, out);
    CHECK(out.pixels == (std::vector<std::uint8_t>{0, 128, 255, 255}));
    const std::uint32_t ten = 1023u | (512u << 10) | (0u << 20);
    convert_to_bgra(reinterpret_cast<const std::uint8_t *>(&ten), 4, 1, 1, SourceFormat::rgb10a2, out);
    CHECK(out.pixels == (std::vector<std::uint8_t>{0, 128, 255, 255}));
    // scRGB: 1.0 is 80 nits; with white at 80 nits it is full white, 0.5 the sRGB of half.
    const std::uint16_t f16[8]{half(1.0f), half(0.5f), 0, half(1.0f), 0, 0, 0, 0};
    convert_to_bgra(reinterpret_cast<const std::uint8_t *>(f16), 8, 1, 1, SourceFormat::rgba16f, out, 80);
    CHECK(out.pixels[2] > 235);                        // red near white (the knee rolls off the top)
    CHECK(out.pixels[1] >= 180 && out.pixels[1] <= 192); // sRGB(0.5) is 188
    CHECK(out.pixels[0] == 0);
    // HDR10: PQ code 520 is about 100 nits; white at 100 nits makes it near full.
    const std::uint32_t pq = 520u | (520u << 10) | (520u << 20);
    convert_to_bgra(reinterpret_cast<const std::uint8_t *>(&pq), 4, 1, 1, SourceFormat::rgb10a2_pq, out, 100);
    CHECK(out.pixels[0] > 235 && out.pixels[1] > 235 && out.pixels[2] > 235);
    // Rows are read `pitch` apart, not packed.
    const std::uint8_t two_rows[16]{1, 2, 3, 4, 0, 0, 0, 0, 5, 6, 7, 8, 0, 0, 0, 0};
    convert_to_bgra(two_rows, 8, 1, 2, SourceFormat::bgra8, out);
    CHECK(out.pixels == (std::vector<std::uint8_t>{1, 2, 3, 255, 5, 6, 7, 255}));
}

TEST(scale_keeps_colour_and_averages_detail) {
    const auto blue = solid(1920, 1440, 200, 10, 10);
    Image out;
    out.resize(1440, 1080);
    scale_bgra(blue, out);
    CHECK(pixel(out, 0, 0)[0] == 200 && pixel(out, 1439, 1079)[0] == 200 && pixel(out, 700, 500)[2] == 10);
    // A one-pixel checker shrunk 4x is an even grey, not moire.
    Image checker;
    checker.resize(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            for (int i = 0; i < 3; ++i) checker.pixels[(static_cast<std::size_t>(y) * 64 + x) * 4 + i] = (x + y) % 2 ? 255 : 0;
    Image small;
    small.resize(16, 16);
    scale_bgra(checker, small);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) CHECK_NEAR(pixel(small, x, y)[1], 128, 2);
}

TEST(vignette_blacks_out_the_corners_only) {
    auto image = solid(400, 300, 255, 255, 255);
    apply_vignette(image, 1.08f);
    CHECK(pixel(image, 200, 150)[0] == 255);  // centre
    CHECK(pixel(image, 0, 150)[0] == 255);    // the middle of the side edge is inside the circle
    CHECK(pixel(image, 0, 0)[0] == 0);        // corners black
    CHECK(pixel(image, 399, 299)[2] == 0);
    CHECK(pixel(image, 0, 0)[3] == 255);      // alpha untouched
    auto clear = solid(40, 30, 9, 9, 9);
    apply_vignette(clear, 0);
    CHECK(pixel(clear, 0, 0)[0] == 9);
}

TEST(nv12_is_bt709_limited_range) {
    std::vector<std::uint8_t> nv12;
    bgra_to_nv12(solid(4, 2, 255, 255, 255), nv12);
    CHECK(nv12.size() == 4 * 2 * 3 / 2);
    CHECK(nv12[0] == 235 && nv12[7] == 235);   // white luma
    CHECK_NEAR(nv12[8], 128, 1);               // no colour
    CHECK_NEAR(nv12[9], 128, 1);
    bgra_to_nv12(solid(2, 2, 0, 0, 0), nv12);
    CHECK(nv12[0] == 16);
    bgra_to_nv12(solid(2, 2, 0, 0, 255), nv12); // pure red
    CHECK_NEAR(nv12[0], 63, 1);
    CHECK_NEAR(nv12[4], 102, 2); // Cb
    CHECK_NEAR(nv12[5], 240, 1); // Cr
}

TEST(pacer_keeps_29_97_whatever_the_frame_rate) {
    for (const double hz : {144.0, 60.0, 30.0, 24.0}) {
        FramePacer pacer;
        std::int64_t total = 0;
        for (int i = 0; i <= static_cast<int>(hz * 10); ++i) total += pacer.due(100.0 + i / hz);
        CHECK_NEAR(static_cast<double>(total), 300, 2); // ten seconds
    }
    // A 2-second stall is not filled with a frozen frame: the clip skips ahead to keep the clock.
    FramePacer pacer;
    CHECK(pacer.due(0) == 1);
    CHECK(pacer.first() == 0);
    CHECK(pacer.due(1.0 / 29.97 + 1e-6) == 1);
    CHECK(pacer.first() == 1);
    CHECK(pacer.due(2.0) == 4);
    CHECK(pacer.first() == 60 - 4); // frames 56..59: 2 s in, at 29.97
    CHECK(pacer.due(2.0 + 1.0 / 30) == 1);
    CHECK(pacer.first() == 60);
    CHECK(pacer.due(NAN) == 0);
}

TEST(clip_file_names_sort_by_time) {
    std::tm t{};
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = 12;
    t.tm_hour = 14;
    t.tm_min = 3;
    t.tm_sec = 22;
    CHECK(clip_file_name(3, t) == "ReSkate 2026-10-12 14-03-22 clip 03.mp4");
    CHECK(photo_file_name(t) == "ReSkate 2026-10-12 14-03-22 still.jpg");
    CHECK(tape_file_name(2, 5, t) == "ReSkate 2026-10-12 14-03-22 tape 2 (5 clips).mp4");
    CHECK(tape_file_name(1, 1, t) == "ReSkate 2026-10-12 14-03-22 tape 1 (1 clip).mp4");
}

TEST(tape_log_lists_the_clips_of_the_tape_in_the_camcorder) {
    TapeLog log;
    log.saved(L"a.mp4");
    log.saved(L"b.mp4");
    log.tape(1); // the same tape
    CHECK(log.clips().size() == 2);
    log.tape(2); // a new tape
    CHECK(log.clips().empty());
    CHECK(log.number() == 2);
    log.saved(L"c.mp4");
    CHECK(log.clips().front() == L"c.mp4");
}
