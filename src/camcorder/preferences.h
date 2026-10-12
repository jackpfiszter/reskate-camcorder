#pragma once
// The camcorder's settings as a small text file, one "key value" a line, so they last from one
// game to the next. The keys are the console's own words (`lens fisheye`, `dv chroma off`): the
// ReSkate side writes what is set now and, at start, runs each line as a `camcorder` command.
// Lines it does not know are left out, so an old file never stops the camcorder starting.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dingosdk::camcorder {
struct Preference {
    std::string key, value;
    bool operator==(const Preference &) const = default;
};
using Preferences = std::vector<Preference>;

std::string write_preferences(const Preferences &);
// Blank lines and lines starting with '#' are skipped; keys and values are trimmed, and a key
// is one word. A later line for the same key wins.
Preferences read_preferences(std::string_view text);
// The command a preference becomes: "dv-chroma off" is `camcorder dv chroma off`. The verb, then
// its words.
std::pair<std::string, std::vector<std::string>> preference_command(const Preference &);
} // namespace dingosdk::camcorder
