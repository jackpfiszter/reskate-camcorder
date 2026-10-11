#pragma once
// A very small test harness: TEST(name) { CHECK(...); CHECK_NEAR(a, b, tolerance); }
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace check {
struct Case {
    const char *name;
    std::function<void()> run;
};
inline std::vector<Case> &cases() {
    static std::vector<Case> all;
    return all;
}
inline int &failures() {
    static int count = 0;
    return count;
}
struct Register {
    Register(const char *name, std::function<void()> run) { cases().push_back({name, std::move(run)}); }
};
inline void fail(const char *file, int line, const char *what) {
    ++failures();
    std::printf("  %s:%d: failed: %s\n", file, line, what);
}
} // namespace check

#define CHECK_JOIN2(a, b) a##b
#define CHECK_JOIN(a, b) CHECK_JOIN2(a, b)
#define TEST(name)                                                                                   \
    static void name();                                                                              \
    static check::Register CHECK_JOIN(register_, name)(#name, name);                                 \
    static void name()
#define CHECK(condition)                                                                             \
    do {                                                                                             \
        if (!(condition)) check::fail(__FILE__, __LINE__, #condition);                               \
    } while (0)
#define CHECK_NEAR(a, b, tolerance)                                                                  \
    do {                                                                                             \
        const double check_a = (a), check_b = (b);                                                   \
        if (!(std::abs(check_a - check_b) <= (tolerance))) {                                         \
            char check_text[256];                                                                    \
            std::snprintf(check_text, sizeof(check_text), "%s ~ %s (%g vs %g)", #a, #b, check_a, check_b); \
            check::fail(__FILE__, __LINE__, check_text);                                             \
        }                                                                                            \
    } while (0)
