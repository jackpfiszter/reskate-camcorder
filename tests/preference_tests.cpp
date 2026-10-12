#include "camcorder/preferences.h"
#include "check.h"

using namespace dingosdk::camcorder;

TEST(preferences_survive_a_round_trip) {
    const Preferences saved{{"lens", "fisheye"}, {"grip", "long"}, {"dv-chroma", "off"}, {"year", "1996"}};
    CHECK(read_preferences(write_preferences(saved)) == saved);
}

TEST(preferences_skip_what_they_cannot_read) {
    const auto read = read_preferences("# a comment\n\n  lens   stock  \r\nquality\nlens fisheye\n\t# another\n");
    CHECK(read.size() == 2);
    CHECK(read[0] == (Preference{"lens", "fisheye"})); // the later line wins
    CHECK(read[1] == (Preference{"quality", ""}));
    // Keys with spaces cannot be written.
    CHECK(read_preferences(write_preferences({{"two words", "x"}, {"ok", "y"}})).size() == 1);
}

TEST(a_preference_is_a_command) {
    const auto plain = preference_command({"lens", "fisheye"});
    CHECK(plain.first == "lens");
    CHECK(plain.second == std::vector<std::string>{"fisheye"});
    const auto part = preference_command({"dv-chroma", "off"});
    CHECK(part.first == "dv");
    CHECK((part.second == std::vector<std::string>{"chroma", "off"}));
    const auto year = preference_command({"date-year", " 1996 "});
    CHECK(year.first == "date");
    CHECK((year.second == std::vector<std::string>{"year", "1996"}));
}
