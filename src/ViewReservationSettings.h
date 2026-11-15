#pragma once

#include <algorithm>
#include <iterator>

namespace ViewReservationSettings {

inline constexpr int ZappingIntervalsSeconds[] = {30, 60, 90, 120, 180};
inline constexpr int DefaultZappingIntervalSeconds = 60;
inline constexpr int ExitTimerMinHours = 1;
inline constexpr int ExitTimerMaxHours = 6;
inline constexpr int DefaultExitTimerHours = 1;

inline bool IsSupportedZappingInterval(int value) {
    return std::find(std::begin(ZappingIntervalsSeconds), std::end(ZappingIntervalsSeconds), value)
        != std::end(ZappingIntervalsSeconds);
}

inline int NormalizeZappingInterval(int value) {
    return IsSupportedZappingInterval(value) ? value : DefaultZappingIntervalSeconds;
}

inline bool IsSupportedExitTimerHours(int value) {
    return value >= ExitTimerMinHours && value <= ExitTimerMaxHours;
}

inline int NormalizeExitTimerHours(int value) {
    return IsSupportedExitTimerHours(value) ? value : DefaultExitTimerHours;
}

} // namespace ViewReservationSettings
