#include <cassert>
#include <iostream>
#include "../src/PanelStationPresentation.h"

int main() {
    using namespace ViewReservationUi;

    const auto viewing = ResolvePanelStationBackground(PanelStationEmphasis::Viewing);
    const auto zapping = ResolvePanelStationBackground(PanelStationEmphasis::Zapping);
    const auto viewMark = ResolvePanelReservationMarkColor(false);
    const auto recordMark = ResolvePanelReservationMarkColor(true);

    assert(viewing.Red == 91 && viewing.Green == 75 && viewing.Blue == 34);
    assert(zapping.Red == 90 && zapping.Green == 48 && zapping.Blue == 48);
    assert(viewMark.Red == 105 && viewMark.Green == 200 && viewMark.Blue == 255);
    assert(recordMark.Red == 255 && recordMark.Green == 105 && recordMark.Blue == 105);
    assert(kPanelPresentationColors.StationText.Red == 112);
    assert(kPanelPresentationColors.TimeText.Green == 190);
    assert(kPanelPresentationColors.CurrentProgramText.Blue == 103);
    assert(kPanelPresentationColors.FutureProgramText.Green == 184);

    std::cout << "panel station presentation tests passed\n";
}
