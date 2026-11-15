#include "../src/AutomationBarLayout.h"
#include <cstdio>

using namespace ViewReservationUi;

static AutomationBarMetrics BaseMetrics(int width) {
    AutomationBarMetrics m{};
    m.ClientWidth = width;
    m.ClientHeight = 28;
    m.SidePadding = 8;
    m.ControlHeight = 22;
    m.LabelGap = 7;
    m.StatusGap = 9;
    m.ControlGap = 9;
    m.CenterGap = 8;
    m.RightGroupPadding = 9;
    m.SeparatorWidth = 1;
    m.ZappingLabelWidth = 52;
    m.ZappingStatusWidth = 48;
    m.ZappingToggleWidth = 44;
    m.ExitLabelWidth = 24;
    m.ExitHoursWidth = 45;
    m.ExitToggleWidth = 38;
    return m;
}

int main() {
    {
        const auto layout = CalculateAutomationBarLayout(BaseMetrics(450));
        if (layout.Mode != AutomationBarMode::Full) return 1;
        if (layout.ExitToggle.Right != 450 - 8) return 2;
        if (layout.ZappingToggle.Right + 8 > layout.ExitSeparator.Left) return 3;
        if (layout.ExitLabel.Empty() || layout.ZappingLabel.Empty()) return 4;
    }
    {
        const auto layout = CalculateAutomationBarLayout(BaseMetrics(260));
        if (layout.Mode != AutomationBarMode::Compact) return 5;
        if (!layout.ExitLabel.Empty() || !layout.ZappingLabel.Empty()) return 6;
        if (layout.ZappingStatus.Empty()) return 7;
        if (layout.ExitToggle.Right != 260 - 8) return 8;
        if (layout.ZappingToggle.Right + 8 > layout.ExitSeparator.Left) return 9;
    }
    {
        const auto layout = CalculateAutomationBarLayout(BaseMetrics(180));
        if (layout.Mode != AutomationBarMode::Essential) return 10;
        if (!layout.ZappingStatus.Empty()) return 11;
        if (layout.ZappingToggle.Empty()) return 12;
        if (layout.ExitToggle.Right != 180 - 8) return 13;
        if (layout.ZappingToggle.Right + 8 > layout.ExitSeparator.Left) return 14;
    }

    for (int width = 150; width <= 800; ++width) {
        const auto layout = CalculateAutomationBarLayout(BaseMetrics(width));
        const int rightEdge = width - 8;
        if (!layout.ExitToggle.Empty() && layout.ExitToggle.Right != rightEdge) return 20;
        if (layout.ExitHours.Left < 0 || layout.ExitToggle.Right > width) return 21;
        if (!layout.ZappingToggle.Empty() &&
            !layout.ExitSeparator.Empty() &&
            layout.ZappingToggle.Right + 8 > layout.ExitSeparator.Left) return 22;
        if (layout.Mode == AutomationBarMode::Full &&
            (layout.ZappingLabel.Empty() || layout.ExitLabel.Empty())) return 23;
        if (layout.Mode != AutomationBarMode::Full &&
            (!layout.ZappingLabel.Empty() || !layout.ExitLabel.Empty())) return 24;
    }

    std::printf("automation bar canonical layout tests passed\n");
    return 0;
}
