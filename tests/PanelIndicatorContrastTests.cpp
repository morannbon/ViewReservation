#include "../src/PanelIndicatorContrast.h"
#include "../src/PanelStationPresentation.h"
#include <cstdio>

using namespace ViewReservationUi;

static bool HasMinimumContrast(const RgbColor &background, const RgbColor &foreground) {
    return ContrastRatio(background, foreground) >= kPanelIndicatorMinimumContrast;
}

int main() {
    const RgbColor panelBackground{48, 48, 48};
    const RgbColor panelText{220, 220, 220};
    const RgbColor viewingBackground = kPanelPresentationColors.ViewingBackground;
    const RgbColor zappingBackground = kPanelPresentationColors.ZappingBackground;

    const auto panelIndicator = CalculatePanelIndicatorColor(panelBackground, panelText);
    if (!HasMinimumContrast(panelBackground, panelIndicator)) return 1;

    const auto viewingIndicator = CalculatePanelIndicatorColor(viewingBackground, panelText);
    if (!HasMinimumContrast(viewingBackground, viewingIndicator)) return 2;

    const auto zappingIndicator = CalculatePanelIndicatorColor(zappingBackground, panelText);
    if (!HasMinimumContrast(zappingBackground, zappingIndicator)) return 3;

    const auto expanded = CalculatePanelIndicatorColor(viewingBackground, panelText);
    const auto collapsed = CalculatePanelIndicatorColor(viewingBackground, panelText);
    if (expanded.Red != collapsed.Red || expanded.Green != collapsed.Green || expanded.Blue != collapsed.Blue) return 4;

    std::printf("panel indicator contrast tests passed\n");
    return 0;
}
