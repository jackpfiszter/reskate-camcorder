#pragma once
// Just enough JSON to read camera and lens profiles (profiles.h): objects, arrays, numbers, strings,
// true, false and null. Strict about structure, so a modder hears where a file is broken.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::camcorder::json {
struct Member; // a key and its value, below (a Value holds them before it is complete)
struct Value {
    enum class Kind : unsigned char { null, boolean, number, string, array, object };
    Kind kind{Kind::null};
    bool boolean{};
    double number{};
    std::string text;
    std::vector<Value> items;                           // an array's
    std::vector<Member> members;                        // an object's, in file order

    bool is(Kind k) const { return kind == k; }
    // An object's member, or null when there is none (or this is not an object).
    const Value *find(std::string_view key) const;
};
struct Member {
    std::string key;
    Value value;
};

// The document, or nothing and `error` saying where ("line 4: expected ':'").
std::optional<Value> parse(std::string_view text, std::string &error);
} // namespace dingosdk::camcorder::json
