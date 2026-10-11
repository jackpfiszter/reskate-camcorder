#include "check.h"

int main() {
    int failed_cases = 0;
    for (const auto &c : check::cases()) {
        const int before = check::failures();
        c.run();
        const bool ok = check::failures() == before;
        if (!ok) ++failed_cases;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", c.name);
    }
    std::printf("%zu tests, %d failed\n", check::cases().size(), failed_cases);
    return failed_cases ? 1 : 0;
}
