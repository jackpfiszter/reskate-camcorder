#include "json.h"
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace dingosdk::camcorder::json {
namespace {
constexpr int deepest = 32; // nesting; a profile needs three or four

struct Reader {
    std::string_view text;
    std::size_t at{};
    std::string error;

    int line() const {
        int n = 1;
        for (std::size_t i = 0; i < at && i < text.size(); ++i) n += text[i] == '\n';
        return n;
    }
    bool fail(const std::string &what) {
        if (error.empty()) error = "line " + std::to_string(line()) + ": " + what;
        return false;
    }
    void space() {
        while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
    }
    bool literal(std::string_view word) {
        if (text.substr(at, word.size()) != word) return false;
        at += word.size();
        return true;
    }
    bool string(std::string &out) {
        if (at >= text.size() || text[at] != '"') return fail("expected a string");
        ++at;
        while (at < text.size()) {
            const char c = text[at++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("a line break inside a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at >= text.size()) break;
            const char e = text[at++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (at + 4 > text.size()) return fail("a short \\u escape");
                unsigned code{};
                const auto [end, err] = std::from_chars(text.data() + at, text.data() + at + 4, code, 16);
                if (err != std::errc{} || end != text.data() + at + 4) return fail("a bad \\u escape");
                at += 4;
                // UTF-8 for the basic plane (names are plain text; pairs are kept as two marks).
                if (code < 0x80) out += static_cast<char>(code);
                else if (code < 0x800) {
                    out += static_cast<char>(0xc0 | (code >> 6));
                    out += static_cast<char>(0x80 | (code & 0x3f));
                } else {
                    out += static_cast<char>(0xe0 | (code >> 12));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                    out += static_cast<char>(0x80 | (code & 0x3f));
                }
                break;
            }
            default: return fail(std::string("an unknown escape \\") + e);
            }
        }
        return fail("a string that never ends");
    }
    bool number(double &out) {
        const auto start = at;
        if (at < text.size() && text[at] == '-') ++at;
        while (at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '.' ||
                                    text[at] == 'e' || text[at] == 'E' || text[at] == '+' || text[at] == '-'))
            ++at;
        // from_chars for doubles is not everywhere yet; strtod on a copy is.
        const std::string digits(text.substr(start, at - start));
        char *end{};
        out = std::strtod(digits.c_str(), &end);
        if (digits.empty() || end != digits.c_str() + digits.size() || !std::isfinite(out)) {
            at = start;
            return fail("expected a value");
        }
        return true;
    }
    bool value(Value &out, int depth) {
        if (depth > deepest) return fail("nested too deep");
        space();
        if (at >= text.size()) return fail("expected a value");
        const char c = text[at];
        if (c == '{') {
            out.kind = Value::Kind::object;
            ++at;
            space();
            if (at < text.size() && text[at] == '}') {
                ++at;
                return true;
            }
            for (;;) {
                space();
                std::string key;
                if (!string(key)) return false;
                space();
                if (at >= text.size() || text[at] != ':') return fail("expected ':' after \"" + key + "\"");
                ++at;
                Value member;
                if (!value(member, depth + 1)) return false;
                out.members.push_back({std::move(key), std::move(member)});
                space();
                if (at < text.size() && text[at] == ',') { ++at; continue; }
                if (at < text.size() && text[at] == '}') {
                    ++at;
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            out.kind = Value::Kind::array;
            ++at;
            space();
            if (at < text.size() && text[at] == ']') {
                ++at;
                return true;
            }
            for (;;) {
                Value item;
                if (!value(item, depth + 1)) return false;
                out.items.push_back(std::move(item));
                space();
                if (at < text.size() && text[at] == ',') { ++at; continue; }
                if (at < text.size() && text[at] == ']') {
                    ++at;
                    return true;
                }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            out.kind = Value::Kind::string;
            return string(out.text);
        }
        if (literal("true")) { out.kind = Value::Kind::boolean; out.boolean = true; return true; }
        if (literal("false")) { out.kind = Value::Kind::boolean; out.boolean = false; return true; }
        if (literal("null")) { out.kind = Value::Kind::null; return true; }
        out.kind = Value::Kind::number;
        return number(out.number);
    }
};
} // namespace

const Value *Value::find(std::string_view key) const {
    for (const auto &[name, member] : members)
        if (name == key) return &member;
    return nullptr;
}

std::optional<Value> parse(std::string_view text, std::string &error) {
    Reader reader{text, 0, {}};
    // A UTF-8 byte order mark, as Notepad writes.
    if (text.substr(0, 3) == "\xEF\xBB\xBF") reader.at = 3;
    Value root;
    if (!reader.value(root, 0)) {
        error = reader.error;
        return std::nullopt;
    }
    reader.space();
    if (reader.at != text.size()) {
        reader.fail("something after the end of the document");
        error = reader.error;
        return std::nullopt;
    }
    return root;
}
} // namespace dingosdk::camcorder::json
