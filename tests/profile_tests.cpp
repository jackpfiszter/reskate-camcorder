#include "camcorder/commands.h"
#include "camcorder/profiles.h"
#include "check.h"

using namespace dingosdk::camcorder;

namespace {
bool mentions(const std::vector<std::string> &notes, const std::string &text) {
    for (const auto &n : notes)
        if (n.find(text) != std::string::npos) return true;
    return false;
}
} // namespace

TEST(json_reads_what_profiles_use) {
    std::string error;
    const auto v = json::parse("\xEF\xBB\xBF{\"a\": [1, -2.5e1, true, null], \"b\": {\"c\": \"x\\\"y\\u00e9\"}}", error);
    CHECK(v && error.empty());
    CHECK(v->find("a")->items.size() == 4);
    CHECK_NEAR(v->find("a")->items[1].number, -25, 1e-9);
    CHECK(v->find("b")->find("c")->text == "x\"y\xC3\xA9");
    for (const char *broken : {"{", "{\"a\" 1}", "[1,]", "{\"a\": 1} x", "{\"a\": tru}", "\"line\nbreak\""}) {
        std::string why;
        CHECK(!json::parse(broken, why));
        CHECK(why.rfind("line ", 0) == 0);
    }
    std::string deep(100, '[');
    CHECK(!json::parse(deep, error));
}

TEST(file_names_give_kind_and_id) {
    const auto camera = profile_file("vx2000.camera.json");
    CHECK(camera && camera->first == ProfileKind::camera && camera->second == "vx2000");
    const auto lens = profile_file("xtreme-fisheye.lens.json");
    CHECK(lens && lens->first == ProfileKind::lens && lens->second == "xtreme-fisheye");
    CHECK(!profile_file("VX2000.camera.json")); // ids are lowercase
    CHECK(!profile_file(".camera.json"));
    CHECK(!profile_file("notes.json"));
    CHECK(!profile_file("my cam.camera.json"));
}

TEST(a_written_camera_reads_back_the_same) {
    auto camera = vx1000();
    camera.id = "test";
    camera.name = "Test \"Cam\"";
    camera.motion = Motion::pal_50i;
    camera.lines = 576;
    camera.look.grain = 0.25f;
    camera.stamp.corner = StampProfile::Corner::bottom_left;
    camera.stamp.colour = {255, 160, 40};
    const auto read = read_camera(write_camera(camera), "test");
    CHECK(read.profile.has_value());
    CHECK(read.notes.empty());
    CHECK(read.profile && *read.profile == camera);
    // The built-in camera survives the trip too: it is the template modders start from.
    const auto built_in = read_camera(write_camera(vx1000()), "vx1000");
    CHECK(built_in.profile && *built_in.profile == vx1000());
    const auto lens = read_adapter(write_adapter(century_fisheye()), "century");
    CHECK(lens.profile && *lens.profile == century_fisheye());
}

TEST(a_camera_needs_only_what_differs) {
    const auto read = read_camera(R"({"name": "Sony DCR-VX2000", "zoom_mm": [6, 72]})", "vx2000");
    CHECK(read.profile.has_value());
    CHECK(read.notes.empty());
    CHECK(read.profile->name == "Sony DCR-VX2000");
    CHECK_NEAR(read.profile->lens.tele_mm, 72, 1e-6);
    CHECK_NEAR(read.profile->lens.sensor_width_mm, vx1000().lens.sensor_width_mm, 1e-6); // the rest is the VX1000's
    CHECK(read.profile->look == vx1000().look);
    // With no name it is called by its id.
    CHECK(read_camera("{}", "plain").profile->name == "plain");
}

TEST(out_of_range_values_are_brought_in_and_noted) {
    const auto read = read_camera(R"({
        "zoom_mm": [5, 4000], "lines": 5000, "motion": "120p", "colour": 1,
        "look": {"grain": 7, "sharpness": 1},
        "stamp": {"colour": [300, 0, 0]}
    })", "wild");
    CHECK(read.profile.has_value());
    const auto &c = *read.profile;
    CHECK(c.lens.tele_mm <= c.lens.wide_mm * 50 + 1e-3f);
    CHECK(c.lines == 1080);
    CHECK(c.motion == Motion::ntsc_60i);
    CHECK_NEAR(c.look.grain, 1, 1e-6);
    CHECK(c.stamp.colour[0] == 255);
    CHECK(mentions(read.notes, "lines is 5000"));
    CHECK(mentions(read.notes, "motion \"120p\""));
    CHECK(mentions(read.notes, "look.sharpness is not a field"));
    CHECK(mentions(read.notes, "colour is not a field"));
    CHECK(mentions(read.notes, "look.grain is 7"));
}

TEST(broken_files_say_why) {
    CHECK(read_camera("[1, 2]", "x").error.find("one JSON object") != std::string::npos);
    CHECK(read_camera("{\"name\": }", "x").error.find("line 1") != std::string::npos);
    CHECK(!read_camera("{}", "Bad Id").profile);
    // A wrong type keeps the default and says so.
    const auto read = read_camera(R"({"lines": "480", "look": []})", "x");
    CHECK(read.profile && read.profile->lines == 480);
    CHECK(read.notes.size() == 2);
}

TEST(a_flat_converter_lens) {
    const auto read = read_adapter(R"({"name": "0.5x wide", "fisheye": false, "magnification": 0.5})", "half");
    CHECK(read.profile && !read.profile->fisheye);
    CHECK_NEAR(read.profile->vignette_radius, 0, 1e-6); // no shadow unless it says so
    const auto stock = optics(nullptr, 0), wide = optics(&*read.profile, 0);
    CHECK(wide.horizontal_fov > stock.horizontal_fov * 1.6f); // about twice as wide
    CHECK(!wide.fisheye);
    // A fisheye's fov is limited to what a flat render can feed.
    CHECK_NEAR(read_adapter(R"({"fov": 220})", "big").profile->fisheye_fov, 170, 1e-6);
}

TEST(the_catalogue_adds_replaces_and_cycles) {
    Catalogue catalogue;
    CHECK(catalogue.cameras().size() == 1 && catalogue.adapters().size() == 1);
    auto vx2000 = vx1000();
    vx2000.id = "vx2000";
    vx2000.name = "VX2000";
    catalogue.add(vx2000);
    auto better = vx1000();
    better.name = "VX1000 (tuned)";
    catalogue.add(better); // a mod's VX1000 replaces the built-in one
    CHECK(catalogue.cameras().size() == 2);
    CHECK(catalogue.camera("vx1000")->name == "VX1000 (tuned)");
    CHECK(catalogue.next_camera("vx1000").id == "vx2000");
    CHECK(catalogue.next_camera("vx2000").id == "vx1000");
    CHECK(catalogue.next_camera("gone").id == "vx1000");
    Adapter half;
    half.id = "half";
    catalogue.add(half);
    CHECK(catalogue.next_adapter("") == catalogue.adapter("century"));
    CHECK(catalogue.next_adapter("century") == catalogue.adapter("half"));
    CHECK(catalogue.next_adapter("half") == nullptr); // back to stock
    CHECK(catalogue.fisheye() == catalogue.adapter("century"));
}

TEST(commands_choose_cameras_and_lenses_from_the_catalogue) {
    Catalogue catalogue;
    auto hi8 = vx1000();
    hi8.id = "hi8";
    hi8.name = "Hi8 handycam";
    hi8.tape_minutes = 90;
    hi8.source = "Hi8 Mod";
    catalogue.add(hi8);
    Camcorder cam;
    CommandContext context;
    context.catalogue = &catalogue;
    CHECK(run_command(cam, "camera", {"hi8"}, context) == "Hi8 handycam.");
    CHECK(cam.status().camera_name == "Hi8 handycam");
    CHECK_NEAR(cam.deck().tape_remaining(), 90 * 60, 1); // its own cassette
    CHECK(run_command(cam, "camera", {"list"}, context).find("hi8  Hi8 handycam  (Hi8 Mod)  <- this one") != std::string::npos);
    CHECK(run_command(cam, "camera", {"nope"}, context).rfind("No camera called nope", 0) == 0);
    CHECK(run_command(cam, "camera", {}, context) == "Sony DCR-VX1000.");
    CHECK(run_command(cam, "lens", {"stock"}, context) == "The camera's own lens.");
    CHECK(run_command(cam, "lens", {"fisheye"}, context) == "Century Optics 0.3x fisheye.");
    CHECK(cam.status().fisheye && cam.status().lens_id == "century");
    CHECK(run_command(cam, "lens", {"next"}, context) == "The camera's own lens.");
    CHECK(run_command(cam, "lens", {"list"}, context).find("stock  the camera's own lens  <- this one") != std::string::npos);
}

#ifdef CAMCORDER_EXAMPLE_MODS
#include <filesystem>
#include <fstream>
#include <sstream>
TEST(the_example_mod_loads_without_a_note) {
    int profiles = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(CAMCORDER_EXAMPLE_MODS)) {
        const auto kind = profile_file(entry.path().filename().string());
        if (!kind) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        const auto check_one = [&](const auto &reading) {
            CHECK(reading.profile.has_value());
            CHECK(reading.notes.empty());
            for (const auto &note : reading.notes) std::printf("  %s: %s\n", entry.path().filename().string().c_str(), note.c_str());
            ++profiles;
        };
        if (kind->first == ProfileKind::camera) check_one(read_camera(text.str(), kind->second));
        else check_one(read_adapter(text.str(), kind->second));
    }
    CHECK(profiles == 4);
    // Super 8 runs at 18 frames, with no fields to switch.
    std::ifstream in(std::filesystem::path(CAMCORDER_EXAMPLE_MODS) / "Camcorder Extras" / "camcorders" / "super8.camera.json");
    std::stringstream text;
    text << in.rdbuf();
    Finish finish;
    finish.dv.on = true;
    finish.motion = read_camera(text.str(), "super8").profile->motion;
    CHECK(!finish.fields());
    CHECK(clip_rate(finish) == (FrameRate{18, 1}));
}
#endif
