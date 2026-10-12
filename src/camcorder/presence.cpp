#include "presence.h"

namespace dingosdk::camcorder {
std::vector<std::uint8_t> encode_presence(Presence presence) {
    return {presence_tag, presence_version, static_cast<std::uint8_t>(presence)};
}

std::optional<Presence> decode_presence(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < 3 || bytes[0] != presence_tag || bytes[1] < 1 || bytes[2] > 2) return std::nullopt;
    return static_cast<Presence>(bytes[2]);
}

std::optional<std::vector<std::uint8_t>> PresenceBeacon::tick(Presence now, double seconds) {
    const bool changed = !said_ || *said_ != now;
    // Off is said once (and only after something else was); the rest is said again now and then.
    const bool again = said_ && now != Presence::off && seconds - said_at_ >= heartbeat;
    if (!changed && !again) return std::nullopt;
    if (changed && !said_ && now == Presence::off) {
        said_ = now; // nothing was ever said: no need to say off
        said_at_ = seconds;
        return std::nullopt;
    }
    said_ = now;
    said_at_ = seconds;
    return encode_presence(now);
}

void PresenceBook::heard(std::uint64_t player, Presence presence, double seconds) {
    if (presence == Presence::off) players_.erase(player);
    else players_[player] = {presence, seconds};
}

Presence PresenceBook::of(std::uint64_t player, double seconds) const {
    const auto it = players_.find(player);
    if (it == players_.end() || seconds - it->second.at > forget) return Presence::off;
    return it->second.presence;
}
} // namespace dingosdk::camcorder
