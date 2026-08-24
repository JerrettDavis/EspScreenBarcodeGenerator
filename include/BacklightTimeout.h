#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// Preset ladder for the Settings > Power screen's backlight-timeout rows, shared by the draw
// code, the touch-cycling code, and native tests so there's one source of truth. 0 means
// "never dim" for that power state.
namespace esplink {

constexpr std::array<uint32_t, 8> kBacklightTimeoutPresetsSec = {0, 15, 30, 60, 120, 300, 600, 1800};

// Finds `current` in the ladder (falling back to the closest entry at or below it, so a value
// from an older/foreign config file still lands somewhere sane) and returns the next entry in
// `direction` (>=0 for right/increase, <0 for left/decrease). Saturates at the ends rather than
// wrapping -- unlike orientation cycling, "Never" and the longest timeout are real endpoints,
// not points on a loop.
inline uint32_t nextBacklightTimeoutPreset(uint32_t current, int direction) {
    std::size_t index = 0;
    for (std::size_t i = 0; i < kBacklightTimeoutPresetsSec.size(); ++i) {
        if (kBacklightTimeoutPresetsSec[i] <= current) index = i;
    }
    const int next = static_cast<int>(index) + (direction >= 0 ? 1 : -1);
    const int clamped = std::max(0, std::min(next, static_cast<int>(kBacklightTimeoutPresetsSec.size()) - 1));
    return kBacklightTimeoutPresetsSec[static_cast<std::size_t>(clamped)];
}

inline std::string formatBacklightTimeout(uint32_t seconds) {
    if (seconds == 0) return "Never";
    if (seconds < 60) return std::to_string(seconds) + "s";
    return std::to_string(seconds / 60) + "m";
}

}  // namespace esplink
