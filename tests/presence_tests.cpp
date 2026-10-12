#include "camcorder/presence.h"
#include "check.h"

using namespace dingosdk::camcorder;

TEST(presence_is_not_a_throwdown) {
    const auto bytes = encode_presence(Presence::recording);
    CHECK(bytes.front() > 15); // Throwdowns' kinds are 1 to 15: they reject it, never misread it
    CHECK(decode_presence(bytes) == Presence::recording);
    CHECK(decode_presence(encode_presence(Presence::standby)) == Presence::standby);
    // Throwdowns' own messages, and garbage, are not the camcorder's.
    const std::vector<std::uint8_t> throwdown{15, 1, 2, 3}, short_one{presence_tag, 1}, odd{presence_tag, 1, 9};
    CHECK(!decode_presence(throwdown) && !decode_presence(short_one) && !decode_presence(odd));
    // A later version's longer message still reads.
    const std::vector<std::uint8_t> later{presence_tag, 2, 2, 7, 7};
    CHECK(decode_presence(later) == Presence::recording);
}

TEST(the_beacon_speaks_on_changes_and_now_and_then) {
    PresenceBeacon beacon;
    CHECK(!beacon.tick(Presence::off, 0)); // never on: nothing to say
    CHECK(beacon.tick(Presence::standby, 1).has_value());
    CHECK(!beacon.tick(Presence::standby, 2));
    CHECK(beacon.tick(Presence::recording, 3) == encode_presence(Presence::recording));
    CHECK(!beacon.tick(Presence::recording, 12));
    CHECK(beacon.tick(Presence::recording, 13.5).has_value()); // the heartbeat, for late joiners
    CHECK(beacon.tick(Presence::off, 14) == encode_presence(Presence::off));
    CHECK(!beacon.tick(Presence::off, 40)); // off is said once
}

TEST(the_book_forgets_the_quiet) {
    PresenceBook book;
    book.heard(7, Presence::recording, 100);
    CHECK(book.of(7, 110) == Presence::recording);
    CHECK(book.of(8, 110) == Presence::off);
    CHECK(book.of(7, 100 + PresenceBook::forget + 1) == Presence::off); // left, crashed, or stopped
    book.heard(7, Presence::off, 130);
    CHECK(book.of(7, 131) == Presence::off);
}
