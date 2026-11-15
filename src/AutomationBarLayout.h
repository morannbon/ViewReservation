#pragma once

#include <algorithm>

namespace ViewReservationUi {

struct Rect {
    int Left = 0;
    int Top = 0;
    int Right = 0;
    int Bottom = 0;

    bool Empty() const noexcept {
        return Right <= Left || Bottom <= Top;
    }

    bool Contains(int x, int y) const noexcept {
        return !Empty() && x >= Left && x < Right && y >= Top && y < Bottom;
    }
};

enum class AutomationBarMode {
    Full,
    Compact,
    Essential
};

struct AutomationBarMetrics {
    int ClientWidth = 0;
    int ClientHeight = 0;
    int SidePadding = 0;
    int ControlHeight = 0;
    int LabelGap = 0;
    int StatusGap = 0;
    int ControlGap = 0;
    int CenterGap = 0;
    int RightGroupPadding = 0;
    int SeparatorWidth = 1;

    int ZappingLabelWidth = 0;
    int ZappingStatusWidth = 0;
    int ZappingToggleWidth = 0;
    int ExitLabelWidth = 0;
    int ExitHoursWidth = 0;
    int ExitToggleWidth = 0;
};

struct AutomationBarLayout {
    AutomationBarMode Mode = AutomationBarMode::Essential;
    Rect ZappingLabel;
    Rect ZappingStatus;
    Rect ZappingToggle;
    Rect ExitSeparator;
    Rect ExitLabel;
    Rect ExitHours;
    Rect ExitToggle;

    bool ShowLabels() const noexcept {
        return Mode == AutomationBarMode::Full;
    }

    bool ShowStatus() const noexcept {
        return Mode != AutomationBarMode::Essential;
    }
};

inline Rect MakeRect(int left, int top, int width, int height) noexcept {
    return { left, top, left + std::max(0, width), top + std::max(0, height) };
}

inline AutomationBarLayout CalculateAutomationBarLayout(const AutomationBarMetrics &m) noexcept {
    AutomationBarLayout out{};

    const int clientWidth = std::max(0, m.ClientWidth);
    const int clientHeight = std::max(0, m.ClientHeight);
    const int side = std::max(0, m.SidePadding);
    const int top = std::max(0, (clientHeight - std::max(0, m.ControlHeight)) / 2);
    const int controlHeight = std::max(0, std::min(m.ControlHeight, clientHeight));
    const int contentLeft = std::min(side, clientWidth);
    const int contentRight = std::max(contentLeft, clientWidth - side);

    const auto leftWidth = [&](AutomationBarMode mode) {
        if (mode == AutomationBarMode::Full) {
            return m.ZappingLabelWidth + m.LabelGap +
                m.ZappingStatusWidth + m.StatusGap + m.ZappingToggleWidth;
        }
        if (mode == AutomationBarMode::Compact) {
            return m.ZappingStatusWidth + m.StatusGap + m.ZappingToggleWidth;
        }
        return m.ZappingToggleWidth;
    };

    const auto rightContentWidth = [&](AutomationBarMode mode) {
        const int label = mode == AutomationBarMode::Full
            ? m.ExitLabelWidth + m.LabelGap
            : 0;
        return label + m.ExitHoursWidth + m.ControlGap + m.ExitToggleWidth;
    };

    const auto rightGroupWidth = [&](AutomationBarMode mode) {
        return std::max(0, m.SeparatorWidth) +
            std::max(0, m.RightGroupPadding) +
            rightContentWidth(mode);
    };

    auto fits = [&](AutomationBarMode mode) {
        const int required = leftWidth(mode) + std::max(0, m.CenterGap) + rightGroupWidth(mode);
        return required <= std::max(0, contentRight - contentLeft);
    };

    if (fits(AutomationBarMode::Full)) {
        out.Mode = AutomationBarMode::Full;
    } else if (fits(AutomationBarMode::Compact)) {
        out.Mode = AutomationBarMode::Compact;
    } else {
        out.Mode = AutomationBarMode::Essential;
    }

    const int rightWidth = rightGroupWidth(out.Mode);
    const int rightGroupLeft = std::max(contentLeft, contentRight - rightWidth);
    const int rightContentLeft = std::min(
        contentRight,
        rightGroupLeft + std::max(0, m.SeparatorWidth) + std::max(0, m.RightGroupPadding));

    if (m.SeparatorWidth > 0 && rightGroupLeft < contentRight) {
        out.ExitSeparator = MakeRect(
            rightGroupLeft,
            top,
            std::min(m.SeparatorWidth, contentRight - rightGroupLeft),
            controlHeight);
    }

    int leftX = contentLeft;
    if (out.ShowLabels()) {
        out.ZappingLabel = MakeRect(leftX, top, m.ZappingLabelWidth, controlHeight);
        leftX += m.ZappingLabelWidth + m.LabelGap;
    }
    if (out.ShowStatus()) {
        out.ZappingStatus = MakeRect(leftX, top, m.ZappingStatusWidth, controlHeight);
        leftX += m.ZappingStatusWidth + m.StatusGap;
    }

    const int leftLimit = std::max(contentLeft, rightGroupLeft - std::max(0, m.CenterGap));
    const int availableToggle = std::max(0, leftLimit - leftX);
    out.ZappingToggle = MakeRect(
        leftX,
        top,
        std::min(std::max(0, m.ZappingToggleWidth), availableToggle),
        controlHeight);

    int rightX = rightContentLeft;
    if (out.ShowLabels()) {
        out.ExitLabel = MakeRect(rightX, top, m.ExitLabelWidth, controlHeight);
        rightX += m.ExitLabelWidth + m.LabelGap;
    }
    out.ExitHours = MakeRect(rightX, top, m.ExitHoursWidth, controlHeight);
    rightX += m.ExitHoursWidth + m.ControlGap;
    out.ExitToggle = MakeRect(
        rightX,
        top,
        std::min(std::max(0, m.ExitToggleWidth), std::max(0, contentRight - rightX)),
        controlHeight);

    return out;
}

} // namespace ViewReservationUi
