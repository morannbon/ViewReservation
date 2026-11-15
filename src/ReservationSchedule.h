#pragma once

#include <cstdint>

namespace ViewReservationSchedule {

inline constexpr std::uint64_t EpgSyncIntervalMs(std::uint64_t secondsUntil) {
    if (secondsUntil > 24ULL * 60 * 60) return 30ULL * 60 * 1000;
    if (secondsUntil > 6ULL * 60 * 60) return 10ULL * 60 * 1000;
    if (secondsUntil > 60ULL * 60) return 60ULL * 1000;
    if (secondsUntil > 10ULL * 60) return 15ULL * 1000;
    return 5000ULL;
}

} // namespace ViewReservationSchedule
