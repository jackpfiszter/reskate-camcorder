#include "camcorder/tape.h"
#include "check.h"

using namespace dingosdk::camcorder;

TEST(drop_frame_timecode_skips_two_numbers_a_minute) {
    CHECK(drop_frame_timecode(0) == (Timecode{0, 0, 0, 0}));
    CHECK(drop_frame_timecode(1799) == (Timecode{0, 0, 59, 29}));
    CHECK(drop_frame_timecode(1800) == (Timecode{0, 1, 0, 2}));
    CHECK(drop_frame_timecode(3597) == (Timecode{0, 1, 59, 29}));
    CHECK(drop_frame_timecode(3598) == (Timecode{0, 2, 0, 2}));
    // Every tenth minute keeps its ;00 and ;01.
    CHECK(drop_frame_timecode(17982) == (Timecode{0, 10, 0, 0}));
    CHECK(drop_frame_timecode(17983) == (Timecode{0, 10, 0, 1}));
    // An hour of 29.97 is 107892 frames, and the timecode agrees with the clock.
    CHECK(drop_frame_timecode(107892) == (Timecode{1, 0, 0, 0}));
    CHECK(drop_frame_timecode(-5) == (Timecode{0, 0, 0, 0}));
}

TEST(timecode_and_counter_text) {
    CHECK(format_timecode({0, 1, 0, 2}) == "00:01:00;02");
    CHECK(format_counter(0) == "0:00:00");
    CHECK(format_counter(3725.9) == "1:02:05");
    CHECK(format_counter(-3) == "0:00:00");
    CHECK(format_counter(NAN) == "0:00:00");
}

TEST(deck_records_clips_one_after_another) {
    Deck deck;
    CHECK(deck.record("first"));
    CHECK(deck.recording());
    for (int i = 0; i < 600; ++i) deck.tick(1.0 / 60, true);
    CHECK_NEAR(deck.tape_seconds(), 10.0, 0.05);
    CHECK_NEAR(deck.clips().back().seconds(), 10.0, 0.05);
    deck.stop();
    for (int i = 0; i < 600; ++i) deck.tick(1.0 / 60, true); // standby: the tape stands still
    CHECK_NEAR(deck.tape_seconds(), 10.0, 0.05);
    CHECK(deck.record("second"));
    deck.tick(1.0, true);
    deck.stop();
    CHECK(deck.clips().size() == 2);
    CHECK(deck.clips()[1].number == 2);
    CHECK(deck.clips()[1].start_frame == deck.clips()[0].end_frame);
    CHECK(deck.clips()[1].label == "second");
    deck.new_tape();
    CHECK(deck.clips().empty());
    CHECK(deck.frame() == 0);
}

TEST(deck_stops_at_the_end_of_the_tape) {
    Deck::Settings settings;
    settings.tape_seconds = 2;
    Deck deck(settings);
    CHECK(deck.record("x"));
    for (int i = 0; i < 300; ++i) deck.tick(1.0 / 60, true);
    CHECK(!deck.recording());
    CHECK(deck.stopped_because() == "Tape end.");
    CHECK(deck.tape_remaining() == 0);
    std::string reason;
    CHECK(!deck.record("y", &reason));
    CHECK(!reason.empty());
    settings.finite_tape = false;
    Deck endless(settings);
    CHECK(endless.record("x"));
    for (int i = 0; i < 300; ++i) endless.tick(1.0 / 60, true);
    CHECK(endless.recording());
}

TEST(deck_stops_when_the_battery_runs_flat_or_power_goes) {
    Deck::Settings settings;
    settings.finite_battery = true;
    settings.battery_seconds = 1;
    Deck deck(settings);
    CHECK(deck.record("x"));
    for (int i = 0; i < 120; ++i) deck.tick(1.0 / 60, true);
    CHECK(!deck.recording());
    CHECK(deck.stopped_because() == "Battery is flat.");
    CHECK(!deck.record("y"));
    deck.charge();
    CHECK(deck.record("y"));
    deck.tick(0.1, false);
    CHECK(!deck.recording());
    CHECK(deck.stopped_because() == "Powered off.");
    // The default battery never runs out.
    Deck plain;
    CHECK(plain.battery_fraction() == 1);
}
