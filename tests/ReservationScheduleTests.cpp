#include "../src/ReservationSchedule.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using ViewReservationSchedule::EpgSyncIntervalMs;
    assert(EpgSyncIntervalMs(0) == 5000);
    assert(EpgSyncIntervalMs(10ULL * 60) == 5000);
    assert(EpgSyncIntervalMs(10ULL * 60 + 1) == 15ULL * 1000);
    assert(EpgSyncIntervalMs(60ULL * 60) == 15ULL * 1000);
    assert(EpgSyncIntervalMs(60ULL * 60 + 1) == 60ULL * 1000);
    assert(EpgSyncIntervalMs(6ULL * 60 * 60) == 60ULL * 1000);
    assert(EpgSyncIntervalMs(6ULL * 60 * 60 + 1) == 10ULL * 60 * 1000);
    assert(EpgSyncIntervalMs(24ULL * 60 * 60) == 10ULL * 60 * 1000);
    assert(EpgSyncIntervalMs(24ULL * 60 * 60 + 1) == 30ULL * 60 * 1000);
    assert(EpgSyncIntervalMs((std::numeric_limits<std::uint64_t>::max)()) == 30ULL * 60 * 1000);
}
