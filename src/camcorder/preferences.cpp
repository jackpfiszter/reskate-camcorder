#include "preferences.h"
#include <algorithm>
#include <cctype>

namespace dingosdk::camcorder {
namespace {
std::string_view trim(std::string_view s) {
    const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return s;
}
} // namespace

std::string write_preferences(const Preferences &preferences) {
    std::string text = "# ReSkate Camcorder settings, written by the game. Each line is a camcorder command.\n";
    for (const auto &p : preferences) {
        if (p.key.empty() || p.key.find_first_of(" \t\r\n#") != std::string::npos) continue;
        std::string value = p.value;
        std::replace(value.begin(), value.end(), '\n', ' ');
        std::replace(value.begin(), value.end(), '\r', ' ');
        text += p.key + " " + value + "\n";
    }
    return text;
}

Preferences read_preferences(std::string_view text) {
    Preferences out;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#') continue;
        const auto gap = line.find_first_of(" \t");
        const auto key = line.substr(0, gap);
        const auto value = gap == std::string_view::npos ? std::string_view{} : trim(line.substr(gap));
        const auto same = std::find_if(out.begin(), out.end(), [&](const Preference &p) { return p.key == key; });
        if (same != out.end()) same->value = std::string(value);
        else out.push_back({std::string(key), std::string(value)});
    }
    return out;
}

std::pair<std::string, std::vector<std::string>> preference_command(const Preference &p) {
    std::vector<std::string> words;
    std::string verb = p.key;
    if (const auto dash = verb.find('-'); dash != std::string::npos) {
        words.push_back(verb.substr(dash + 1));
        verb.resize(dash);
    }
    std::string_view rest = p.value;
    while (!rest.empty()) {
        rest = trim(rest);
        const auto gap = rest.find_first_of(" \t");
        if (!rest.empty()) words.emplace_back(rest.substr(0, gap));
        rest = gap == std::string_view::npos ? std::string_view{} : rest.substr(gap);
    }
    return {verb, words};
}
} // namespace dingosdk::camcorder
