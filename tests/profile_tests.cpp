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
    // Everything a mod can set goes out and comes back.
    camera.look.highlight_knee = 0.8f;
    camera.look.grain_size = 2.5f;
    camera.look.grain_colour = 0.6f;
    camera.look.sharpen_radius = 1.5f;
    camera.look.drift_seconds = {60, 5};
    camera.stamp.corner = StampProfile::Corner::top_left;
    camera.stamp.size = 1.5f;
    camera.stamp.margin = {0.1f, 0.05f};
    camera.stamp.outline = false;
    camera.stamp.outline_colour = {40, 0, 0};
    camera.light = {1.5f, 1.2f, 0.3f, {200, 220, 255}};
    camera.viewfinder.text = {120, 255, 120};
    camera.viewfinder.rec = {255, 255, 0};
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

TEST(a_camera_gives_its_pictures_shape) {
    const auto ratio = read_camera(R"({"aspect": [16, 9]})", "wide");
    CHECK(ratio.notes.empty());
    CHECK_NEAR(ratio.profile->lens.aspect(), 16.0 / 9.0, 1e-4);
    CHECK_NEAR(ratio.profile->lens.sensor_width_mm, 4.8, 1e-4); // the chip keeps its width
    const auto number = read_camera(R"({"sensor_mm": [5.79, 4.01], "aspect": 1})", "square");
    CHECK_NEAR(number.profile->lens.aspect(), 1, 1e-4);
    // Too thin is brought in; nonsense keeps the sensor's.
    CHECK_NEAR(read_camera(R"({"aspect": 9})", "thin").profile->lens.aspect(), 3, 1e-4);
    const auto bad = read_camera(R"({"aspect": "wide"})", "bad");
    CHECK(mentions(bad.notes, "aspect should be"));
    CHECK_NEAR(bad.profile->lens.aspect(), 4.0 / 3.0, 1e-4);
    // Written out and read back, the shape is the sensor's.
    CHECK_NEAR(read_camera(write_camera(*ratio.profile), "again").profile->lens.aspect(), 16.0 / 9.0, 1e-3);
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
        switch (kind->first) {
        case ProfileKind::camera: check_one(read_camera(text.str(), kind->second)); break;
        case ProfileKind::lens: check_one(read_adapter(text.str(), kind->second)); break;
        case ProfileKind::grip: check_one(read_grip(text.str(), kind->second)); break;
        case ProfileKind::handling: check_one(read_handling(text.str(), Handling{})); break;
        }
    }
    CHECK(profiles == 8);
    // Super 8 runs at film's 24 frames (as sound Super 8 did), with no fields to switch.
    std::ifstream in(std::filesystem::path(CAMCORDER_EXAMPLE_MODS) / "Camcorder Extras" / "camcorders" / "super8.camera.json");
    std::stringstream text;
    text << in.rdbuf();
    Finish finish;
    finish.dv.on = true;
    finish.motion = read_camera(text.str(), "super8").profile->motion;
    CHECK(!finish.fields());
    CHECK(clip_rate(finish).fps() > 23.9 && clip_rate(finish).fps() < 24.01);
}
#endif

TEST(grips_are_data) {
    const auto pole = read_grip(R"({"name": "Pole cam", "height": 2.6, "ahead": 1.2, "pitch": -25, "shake": 1.8})", "pole");
    CHECK(pole.profile && pole.notes.empty());
    CHECK(pole.profile->name == "Pole cam" && pole.profile->height == 2.6f);
    CHECK_NEAR(pole.profile->side, long_grip().side, 1e-6); // the rest is the long grip's
    for (const auto *grip : {&long_grip(), &hip_grip(), &eye_grip()}) {
        const auto again = read_grip(write_grip(*grip), grip->id);
        CHECK(again.profile && *again.profile == *grip);
    }
    CHECK(read_grip(R"({"height": 9})", "tall").profile->height == 3.0f);
    CHECK(profile_file("pole.grip.json")->first == ProfileKind::grip);
    CHECK(profile_file("handling.json")->first == ProfileKind::handling);
    // Chosen by id, with mods' rigs beside the built-in hand.
    Catalogue catalogue;
    catalogue.add(*pole.profile);
    Camcorder cam;
    CommandContext context;
    context.catalogue = &catalogue;
    CHECK(run_command(cam, "grip", {"pole"}, context) == "Pole cam.");
    CHECK(cam.status().grip_id == "pole");
    CHECK(run_command(cam, "rig", {"next"}, context) == "Handheld."); // round to the first
    CHECK(run_command(cam, "grip", {"list"}, context).find("pole  Pole cam") != std::string::npos);
}

TEST(handling_files_layer_over_each_other) {
    Handling built_in;
    const auto first = read_handling(R"({"filmer": {"look_speed": 3}, "shake": {"wheels": 2}})", built_in);
    CHECK(first.profile && first.notes.empty());
    const auto second = read_handling(R"({"filmer": {"lead": 0.3}, "controls": {"mouse": 0.004}})", *first.profile);
    CHECK(second.profile.has_value());
    const auto &h = *second.profile;
    CHECK(h.look_speed == 3.0f && h.shake.wheels == 2.0f && h.lead == 0.3f && h.mouse == 0.004f); // both kept
    CHECK(h.reaction == built_in.reaction);                                                      // untouched
    const auto again = read_handling(write_handling(h), Handling{});
    CHECK(again.profile && *again.profile == h);
    const auto odd = read_handling(R"({"filmer": {"lowest": 1.4, "highest": 0.6}, "turbo": 1})", built_in);
    CHECK(odd.profile && odd.profile->highest >= odd.profile->lowest);
    CHECK(odd.notes.size() >= 2);
}

TEST(handling_reaches_the_rigs_and_standing_tripods) {
    Camcorder cam;
    Filmer filmer;
    filmer.valid = true;
    cam.place_tripod(filmer);
    Handling h;
    h.tripod_drag = 1.5f;
    h.tripod_height = 0.4f;
    h.look_speed = 4;
    h.height_step = 0.5f;
    cam.set_handling(h);
    CHECK(cam.tripod(0)->settings().drag == 1.5f);           // a standing tripod takes the new feel
    CHECK(cam.tripod(0)->settings().height != 0.4f);         // but keeps its height
    CHECK(cam.handheld().settings().look_speed == 4.0f);
    cam.set_mode(Mode::handheld);
    CHECK(run_command(cam, "height", {"up"}).find("0.91") != std::string::npos); // 0.41 + a half-metre step
}

TEST(a_camera_has_its_own_mic) {
    const auto read = read_camera(R"({"mic": {"low_cut": 5000, "most_gain": 6}})", "lofi");
    CHECK(read.profile && read.notes.empty());
    CHECK(read.profile->mic.low_cut == 5000.0f && read.profile->mic.most_gain == 6.0f);
    CHECK(read.profile->mic.high_cut == MicProfile{}.high_cut);
}

TEST(a_camera_sets_how_its_look_light_and_stamp_behave) {
    const auto read = read_camera(R"({
        "look": {"grain_size": 3, "grain_colour": 1, "highlight_knee": 0.85, "drift_seconds": [5, 90]},
        "stamp": {"corner": "top-right", "size": 2, "outline": false},
        "light": {"strength": 4, "colour": [180, 200, 255]},
        "viewfinder": {"text": [255, 200, 0]}
    })", "led");
    CHECK(read.profile && read.notes.empty());
    const auto &c = *read.profile;
    CHECK(c.look.grain_size == 3 && c.look.grain_colour == 1);
    CHECK_NEAR(c.look.highlight_knee, 0.85, 1e-6);
    CHECK(c.look.drift_seconds[0] == 90 && c.look.drift_seconds[1] == 5); // the slow one first
    CHECK(c.stamp.corner == StampProfile::Corner::top_right && c.stamp.size == 2 && !c.stamp.outline);
    CHECK(c.light.strength == 4 && c.light.colour[2] == 255 && c.light.reach == LightProfile{}.reach);
    CHECK(c.viewfinder.text[1] == 200 && c.viewfinder.rec == vx1000().viewfinder.rec);
    const auto wrong = read_camera(R"({"light": {"colour": [1, 2]}, "stamp": {"size": 9}})", "x");
    CHECK(wrong.notes.size() == 2); // a colour needs three numbers; a size is kept to 4
}

TEST(a_fisheye_bends_by_its_projection) {
    const auto read = read_adapter(R"({"fov": 150, "projection": "equisolid"})", "modern");
    CHECK(read.profile && read.notes.empty() && read.profile->projection == Projection::equisolid);
    CHECK(read_adapter(write_adapter(*read.profile), "modern").profile == read.profile);
    // Every projection meets the edge of the picture at the lens's half angle; between, they differ.
    const float half = 75 * degrees;
    for (const auto p : {Projection::equidistant, Projection::equisolid, Projection::stereographic})
        CHECK_NEAR(fisheye_angle(p, 1, half), half, 1e-4);
    // Squeezed edges leave the middle bigger (less angle there); rounder edges, the middle smaller.
    CHECK(fisheye_angle(Projection::equisolid, 0.5f, half) < fisheye_angle(Projection::equidistant, 0.5f, half));
    CHECK(fisheye_angle(Projection::orthographic, 0.5f, half) < fisheye_angle(Projection::equisolid, 0.5f, half));
    CHECK(fisheye_angle(Projection::stereographic, 0.5f, half) > fisheye_angle(Projection::equidistant, 0.5f, half));
    // The render still covers the picture: the warp's corners have picture to sample.
    Adapter lens = century_fisheye();
    lens.projection = Projection::orthographic;
    const auto o = optics(&lens, 0, FisheyeRender::warp);
    CHECK(o.warped && o.projection == Projection::orthographic && o.render_vertical_fov > o.vertical_fov);
    CHECK(read_adapter(R"({"projection": "bendy"})", "x").notes.size() == 1);
}

TEST(a_camera_brings_its_own_viewfinder) {
    const auto read = read_camera(R"({"viewfinder": {"text": [255, 255, 255], "warning": [255, 0, 0], "layout": [
        {"show": "text", "text": "{status}", "anchor": "top-right", "offset": [20, 20], "size": 30, "colour": "rec", "when": "recording"},
        {"text": "SP {remaining}", "anchor": "bottom-left", "when": ["!recording", "!low-tape"], "blink": 1},
        {"show": "box", "anchor": "top", "width": 120, "height": 30, "filled": true, "colour": [0, 0, 0], "opacity": 0.5},
        {"show": "corners", "offset": [24, 24]},
        {"show": "zoom-bar", "length": 0.5, "when": "zooming"},
        {"show": "sparkle"},
        {"anchor": "middle", "when": ["sometimes"], "glow": 1}
    ]}})", "own");
    CHECK(read.profile.has_value());
    const auto &v = read.profile->viewfinder;
    CHECK(v.warning == (std::array<std::uint8_t, 3>{255, 0, 0}));
    CHECK(v.elements.size() == 6);
    CHECK(v.elements[0].anchor == Anchor::top_right && v.elements[0].ink == Ink::rec && v.elements[0].when.size() == 1);
    CHECK(v.elements[1].when.size() == 2 && !v.elements[1].when[1].is && v.elements[1].when[1].what == Condition::low_tape);
    CHECK(v.elements[2].show == Show::box && v.elements[2].filled && v.elements[2].ink == Ink::own && v.elements[2].extent[0] == 120);
    CHECK(v.elements[3].show == Show::corners && v.elements[3].size == 40);
    CHECK(v.elements[4].extent[0] == 0.5f);
    CHECK(mentions(read.notes, "layout[5].show \"sparkle\""));
    CHECK(mentions(read.notes, "layout[6].anchor \"middle\""));
    CHECK(mentions(read.notes, "\"sometimes\" is not one of"));
    CHECK(mentions(read.notes, "layout[6].glow is not a field"));
    // Written out and read back: the same; the VX1000's own layout isn't written out at all.
    const auto again = read_camera(write_camera(*read.profile), "own");
    CHECK(again.notes.empty() && again.profile->viewfinder == v);
    CHECK(write_camera(vx1000()).find("layout") == std::string::npos);
    CHECK(read_camera("{}", "plain").profile->viewfinder == vx1000_viewfinder());
}
