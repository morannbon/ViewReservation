#pragma once

#include <windows.h>

namespace ViewReservationTime {

inline constexpr ULONGLONG TicksPerSecond = 10000000ULL;

inline ULONGLONG FileTimeValue(const SYSTEMTIME &time) {
    FILETIME fileTime{};
    SystemTimeToFileTime(&time, &fileTime);
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return value.QuadPart;
}

} // namespace ViewReservationTime
