#pragma once
// "Capture the tape": every clip on the tape in the camcorder, one after another in one MP4, the way
// a MiniDV tape was captured into the computer for editing. Each clip is decoded and encoded again
// (clips encoded apart cannot simply be glued), on a thread of its own; clips of another size are
// scaled to the first's, and a clip without sound gets silence so the sound stays in step.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dingosdk::camcorder_tape {
struct Result {
    bool ok{};
    std::wstring path;
    std::size_t clips{};   // how many made it in
    std::string error;     // why not, when not ok
    std::string warning;   // clips that were left out, and why
};

class Capture {
public:
    Capture();
    ~Capture();
    Capture(const Capture &) = delete;
    Capture &operator=(const Capture &) = delete;
    // Starts joining `clips` into `path`. False while a capture is still running.
    bool start(std::vector<std::wstring> clips, std::wstring path);
    bool running() const;
    std::optional<Result> take_result();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dingosdk::camcorder_tape
