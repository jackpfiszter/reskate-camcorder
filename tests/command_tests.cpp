#include "camcorder/commands.h"
#include "check.h"

using namespace dingosdk::camcorder;

namespace {
CommandContext here() {
    CommandContext c;
    c.filmer.valid = true;
    c.filmer.forward = {0, 0, 1};
    return c;
}
bool starts(const std::string &text, std::string_view prefix) { return text.rfind(prefix, 0) == 0; }
} // namespace

TEST(commands_power_and_record) {
    Camcorder cam;
    CHECK(run_command(cam, "status", {}) == "Camcorder off.");
    CHECK(starts(run_command(cam, "power", {"on"}), "STBY 0:00:00, handheld (lock on)"));
    CHECK(run_command(cam, "status", {}).find("60 min of tape left") != std::string::npos);
    CHECK(run_command(cam, "rec", {}) == "REC");
    CHECK(cam.deck().recording());
    CHECK(run_command(cam, "rec", {}) == "STBY");
    run_command(cam, "rec", {});
    CHECK(starts(run_command(cam, "stop", {}), "STBY"));
    CHECK(run_command(cam, "power", {"toggle"}) == "Camcorder off.");
    CHECK(starts(run_command(cam, "power", {"sideways"}), "Usage"));
    CHECK(starts(run_command(cam, "POWER", {"ON"}), "STBY")); // any case
}

TEST(commands_change_the_lens_grip_and_lock_on) {
    Camcorder cam;
    CHECK(run_command(cam, "lens", {"stock"}) == "The camera's own lens.");
    CHECK(!cam.settings().adapter);
    CHECK(run_command(cam, "lens", {}) == "Century Optics 0.3x fisheye.");
    // One rig built in, the hand; older settings' grips are its heights.
    CHECK(run_command(cam, "grip", {"eye"}) == "Handheld, at eye level.");
    CHECK_NEAR(cam.status().handheld_height, eye_grip().height_on(true), 1e-5);
    CHECK(run_command(cam, "rig", {"next"}) == "Handheld.");
    CHECK_NEAR(cam.status().handheld_height, long_grip().height_on(true), 1e-5); // a rig starts at its own height
    CHECK(starts(run_command(cam, "lockon", {"off"}), "Lock on off"));
    CHECK(!cam.status().lock_on);
    CHECK(starts(run_command(cam, "lockon", {}), "Lock on:"));
}

TEST(commands_run_tripods) {
    Camcorder cam;
    CHECK(run_command(cam, "mode", {"tripod"}) == "No tripod set up.");
    CHECK(starts(run_command(cam, "tripod", {"place"}, here()), "Tripod 1 set up."));
    CHECK(cam.status().tripod_setup);
    CHECK(starts(run_command(cam, "tripod", {"setup", "off"}), "STBY"));
    CHECK(!cam.status().tripod_setup);
    CHECK(run_command(cam, "head", {"locked"}) == "Tripod head: locked.");
    CHECK(cam.tripod(0)->settings().head == TripodRig::Head::locked);
    CHECK(run_command(cam, "height", {"0.2"}) == "Tripod height 0.20 m.");
    CHECK(run_command(cam, "height", {"9"}) == "Tripod height 2.00 m.");
    CHECK(starts(run_command(cam, "height", {"tall"}), "Usage"));
    run_command(cam, "tripod", {"place"}, here());
    CHECK(run_command(cam, "tripod", {"1"}) == "Tripod 1.");
    CHECK(run_command(cam, "tripod", {"next"}) == "Tripod 2.");
    CHECK(run_command(cam, "tripod", {"9"}).find("Usage") == 0);
    CHECK(run_command(cam, "tripod", {"remove", "2"}) == "Tripod 2 taken down.");
    // The tripod key again while setting one up: it is done, not another tripod.
    run_command(cam, "tripod", {"place"}, here());
    CHECK(cam.status().tripod_setup);
    CHECK(starts(run_command(cam, "tripod", {"place"}, here()), "Tripod 2 is set"));
    CHECK(!cam.status().tripod_setup && cam.status().active_tripod == 1);
    CHECK(run_command(cam, "tripod", {"done"}) == "No tripod being set up.");
    run_command(cam, "tripod", {"remove", "2"});
    CHECK(run_command(cam, "tripod", {"remove", "2"}) == "No tripod in that slot.");
    CHECK(starts(run_command(cam, "mode", {"handheld"}), "STBY 0:00:00, handheld"));
    CHECK(starts(run_command(cam, "mode", {}), "STBY 0:00:00, tripod 1"));
}

TEST(commands_list_and_replace_the_tape) {
    Camcorder cam;
    CHECK(run_command(cam, "tape", {}) == "Nothing on this tape yet.");
    run_command(cam, "rec", {});
    TickInput in;
    in.dt = 0.1f;
    in.filmer.valid = true;
    for (int i = 0; i < 20; ++i) cam.tick(in);
    run_command(cam, "rec", {});
    const auto list = run_command(cam, "tape", {"clips"});
    CHECK(starts(list, "#1 00:00:00;00  2.0 s  Handheld, fisheye"));
    CHECK(run_command(cam, "tape", {"new"}) == "New tape.");
    CHECK(cam.deck().clips().empty());
    CHECK(starts(run_command(cam, "dance", {}), "Unknown camcorder command"));
}

TEST(height_steps_the_handheld_camera_and_a_grip_resets_it) {
    Camcorder cam;
    CHECK(starts(run_command(cam, "height", {"up"}), "Camera held 0.56 m up"));
    CHECK(starts(run_command(cam, "height", {"0.2"}), "Camera held 0.20 m up"));
    CHECK(starts(run_command(cam, "height", {"down"}), "Camera held 0.12 m up")); // the lowest
    run_command(cam, "grip", {"hip"});
    CHECK_NEAR(cam.status().handheld_height, hip_grip().height_on(true), 1e-5);
    CHECK(starts(run_command(cam, "height", {"eye"}), "Camera held 1.47 m up")); // 1.6 m, crouched on a board
    CHECK(starts(run_command(cam, "height", {"low"}), "Camera held 0.41 m up"));
    CHECK(starts(run_command(cam, "height", {"next"}), "Camera held 0.92 m up")); // the hip
    CHECK(starts(run_command(cam, "height", {"next"}), "Camera held 1.47 m up")); // eye level
    CHECK(starts(run_command(cam, "height", {"next"}), "Camera held 0.41 m up")); // round to low
    CHECK(starts(run_command(cam, "height", {"sideways"}), "Usage"));
}

TEST(height_next_goes_round_even_when_the_handling_caps_the_height) {
    Camcorder cam;
    auto handling = cam.handling();
    handling.highest = 1.0f;
    cam.set_handling(handling);
    std::string last;
    bool low_again = false;
    for (int i = 0; i < 6; ++i) {
        last = run_command(cam, "height", {"next"});
        if (i > 0 && cam.status().handheld_height < 0.5f) low_again = true;
    }
    CHECK(low_again);
}
