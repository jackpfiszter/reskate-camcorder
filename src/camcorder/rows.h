#pragma once
// Runs `work(first, last)` over [0, rows) split across a few threads: each pixel pass covers a whole
// frame, 30 or 60 times a second, beside a running game. Small jobs stay on the calling thread.
#include <algorithm>
#include <thread>
#include <vector>

namespace dingosdk::camcorder {
template <class Work> void for_rows(int rows, Work &&work) {
    static const int threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u));
    const int parts = rows >= 256 ? threads : 1;
    if (parts <= 1) {
        work(0, rows);
        return;
    }
    std::vector<std::thread> helpers;
    const int step = (rows + parts - 1) / parts;
    for (int first = step; first < rows; first += step)
        helpers.emplace_back([&work, first, last = std::min(rows, first + step)] { work(first, last); });
    work(0, std::min(rows, step));
    for (auto &helper : helpers) helper.join();
}
} // namespace dingosdk::camcorder
