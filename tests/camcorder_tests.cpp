#include "camcorder/camcorder.h"
#include "check.h"

using namespace dingosdk::camcorder;

namespace {
TickInput tick_at(Vec3 position = {}, float dt = 1.0f / 60) {
    TickInput in;
    in.dt = dt;
    in.filmer.valid = true;
    in.filmer.position = position;
    in.filmer.forward = {0, 0, 1};
    return in;
}
} // namespace

TEST(camcorder_off_gives_the_game_its_camera) {
    Camcorder cam;
    const auto frame = cam.tick(tick_at());
    CHECK(!frame.shot.valid);
    CHECK(!frame.status.powered);
}

TEST(camcorder_on_films_handheld_through_the_fisheye) {
    Camcorder cam;
    cam.power(true);
    const auto frame = cam.tick(tick_at());
    CHECK(frame.shot.valid);
    CHECK(frame.optics.fisheye);
    CHECK(frame.status.mode == Mode::handheld);
    cam.set_adapter(Adapter::none);
    CHECK(!cam.tick(tick_at()).optics.fisheye);
}

TEST(camcorder_records_and_labels_clips) {
    Camcorder cam;
    cam.toggle_record(); // powers on too
    CHECK(cam.powered());
    CHECK(cam.deck().recording());
    for (int i = 0; i < 60; ++i) cam.tick(tick_at());
    auto status = cam.status();
    CHECK(status.transport == Transport::recording);
    CHECK_NEAR(status.counter_seconds, 1.0, 0.05);
    CHECK(status.clip == 1);
    cam.toggle_record();
    CHECK(!cam.deck().recording());
    CHECK(cam.deck().clips()[0].label.find("Handheld") == 0);
    cam.toggle_record();
    cam.power(false);
    CHECK(!cam.deck().recording());
    CHECK(cam.deck().clips().size() == 2);
}

TEST(camcorder_zooms_with_the_rocker) {
    Camcorder cam;
    cam.power(true);
    auto in = tick_at();
    in.controls.zoom = 1;
    Frame frame;
    for (int i = 0; i < 120; ++i) frame = cam.tick(in);
    CHECK(frame.status.zoom > 0.3f);
    CHECK(frame.status.zoom_bar);
    CHECK(frame.optics.horizontal_fov < 125);
}

TEST(camcorder_tripods_set_up_cut_and_come_down) {
    Camcorder cam;
    CHECK(cam.place_tripod(tick_at().filmer) == 0);
    auto status = cam.status();
    CHECK(status.powered);
    CHECK(status.mode == Mode::tripod);
    CHECK(status.tripod_setup);
    // Setup moves the tripod; rolling ends setup.
    auto in = tick_at();
    in.controls.move_y = 1;
    for (int i = 0; i < 30; ++i) cam.tick(in);
    CHECK(cam.tripod(0)->feet()[2] > 0.8f);
    cam.toggle_record();
    CHECK(!cam.status().tripod_setup);
    const auto stood = cam.tripod(0)->feet();
    for (int i = 0; i < 30; ++i) cam.tick(in);
    CHECK(cam.tripod(0)->feet()[2] == stood[2]); // filming: the tripod stays put
    CHECK(cam.deck().clips()[0].label.find("Tripod 1") == 0);

    CHECK(cam.place_tripod(tick_at({5, 0, 0}).filmer) == 1);
    cam.next_tripod();
    CHECK(cam.status().active_tripod == 0);
    cam.next_tripod();
    CHECK(cam.status().active_tripod == 1);
    cam.remove_tripod(1);
    CHECK(cam.status().active_tripod == 0);
    CHECK(cam.status().mode == Mode::tripod);
    cam.remove_tripod(0);
    CHECK(cam.status().mode == Mode::handheld);
    CHECK(cam.tick(tick_at()).shot.valid);
}

TEST(camcorder_without_tripods_stays_handheld) {
    Camcorder cam;
    cam.power(true);
    cam.set_mode(Mode::tripod);
    CHECK(cam.status().mode == Mode::handheld);
    CHECK(cam.status().message == "No tripod set up.");
    cam.set_tripod_setup(true);
    CHECK(!cam.status().tripod_setup);
}

TEST(camcorder_reuses_the_active_slot_when_all_are_used) {
    Camcorder cam;
    for (int i = 0; i < 4; ++i) CHECK(cam.place_tripod(tick_at({float(i), 0, 0}).filmer) == i);
    cam.select_tripod(2);
    CHECK(cam.place_tripod(tick_at({9, 0, 0}).filmer) == 2);
    CHECK_NEAR(cam.tripod(2)->feet()[0], 9, 1e-5);
    CHECK(cam.place_tripod(Filmer{}) == -1);
}
