#pragma once
// Letting the other players see who is filming: a REC light by a filmer's nametag. ReSkate's
// multiplayer packets cannot grow a field without shutting out every player on another build, so
// the camcorder's word travels where Throwdowns' does (PacketKind::throwdown: hosts and servers
// pass it on unread), with a first byte no Throwdowns message has. Builds without the camcorder
// drop it as an unreadable throwdown (a line in their log); builds with it show the light.
//
// It is said when it changes, and again every few seconds while the camera is on, so a player who
// joins later sees it; a player not heard from for a while is taken to have stopped.
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace dingosdk::camcorder {
enum class Presence : std::uint8_t {
    off = 0,       // no camcorder (or not heard from)
    standby = 1,   // the camera is on
    recording = 2, // REC
};
// Throwdowns' kinds are 1 to 15; this is not one of them.
inline constexpr std::uint8_t presence_tag = 0xC4, presence_version = 1;

std::vector<std::uint8_t> encode_presence(Presence);
// Nothing for anything that is not the camcorder's word (a throwdown, or garbage). A later
// version's longer message is read as far as this one understands it.
std::optional<Presence> decode_presence(std::span<const std::uint8_t> bytes) noexcept;

// When to tell the others: on a change, and every `heartbeat` seconds while not off.
class PresenceBeacon {
public:
    static constexpr double heartbeat = 10;
    std::optional<std::vector<std::uint8_t>> tick(Presence now, double seconds);

private:
    std::optional<Presence> said_;
    double said_at_{};
};

// What the others last said, each forgotten `forget` seconds after it was said.
class PresenceBook {
public:
    static constexpr double forget = 25;
    void heard(std::uint64_t player, Presence presence, double seconds);
    Presence of(std::uint64_t player, double seconds) const;

private:
    struct Entry {
        Presence presence{};
        double at{};
    };
    std::map<std::uint64_t, Entry> players_;
};
} // namespace dingosdk::camcorder
