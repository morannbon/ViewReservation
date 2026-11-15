#include <cassert>
#include <iostream>
#include "../src/ViewReservationSettings.h"

int main() {
    using namespace ViewReservationSettings;

    assert(DefaultZappingIntervalSeconds == 60);
    assert(NormalizeZappingInterval(30) == 30);
    assert(NormalizeZappingInterval(180) == 180);
    assert(NormalizeZappingInterval(45) == DefaultZappingIntervalSeconds);

    assert(DefaultExitTimerHours == 1);
    assert(IsSupportedExitTimerHours(1));
    assert(IsSupportedExitTimerHours(6));
    assert(!IsSupportedExitTimerHours(0));
    assert(!IsSupportedExitTimerHours(7));
    assert(NormalizeExitTimerHours(ExitTimerMinHours) == ExitTimerMinHours);
    assert(NormalizeExitTimerHours(ExitTimerMaxHours) == ExitTimerMaxHours);
    assert(NormalizeExitTimerHours(0) == DefaultExitTimerHours);
    assert(NormalizeExitTimerHours(7) == DefaultExitTimerHours);

    std::cout << "view reservation settings tests passed\n";
}
