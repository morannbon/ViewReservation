#pragma once

#include <algorithm>
#include <cmath>

#include "PanelColor.h"

namespace ViewReservationUi {

inline constexpr double kPanelIndicatorMinimumContrast = 4.5;

inline int ClampColorChannel(int value) {
    return (std::max)(0, (std::min)(255, value));
}

inline double LinearizeSrgbChannel(int value) {
    const double channel = static_cast<double>(ClampColorChannel(value)) / 255.0;
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
}

inline double RelativeLuminance(const RgbColor &color) {
    return 0.2126 * LinearizeSrgbChannel(color.Red)
        + 0.7152 * LinearizeSrgbChannel(color.Green)
        + 0.0722 * LinearizeSrgbChannel(color.Blue);
}

inline double ContrastRatio(const RgbColor &a, const RgbColor &b) {
    const double aLum = RelativeLuminance(a);
    const double bLum = RelativeLuminance(b);
    const double high = (std::max)(aLum, bLum);
    const double low = (std::min)(aLum, bLum);
    return (high + 0.05) / (low + 0.05);
}

inline RgbColor MixRgb(const RgbColor &background, const RgbColor &foreground, int foregroundPercent) {
    foregroundPercent = (std::max)(0, (std::min)(100, foregroundPercent));
    const int backgroundPercent = 100 - foregroundPercent;
    return {
        (background.Red * backgroundPercent + foreground.Red * foregroundPercent) / 100,
        (background.Green * backgroundPercent + foreground.Green * foregroundPercent) / 100,
        (background.Blue * backgroundPercent + foreground.Blue * foregroundPercent) / 100
    };
}

// 展開/収納インジケーターの色は描画先背景から一意に求める。
// 通常行・視聴中・ザッピング中で別の直値を持たず、描画先背景とのコントラストを確保する。
inline RgbColor CalculatePanelIndicatorColor(
    const RgbColor &background,
    const RgbColor &preferredText,
    double minimumContrast = kPanelIndicatorMinimumContrast) {

    constexpr RgbColor white{255, 255, 255};
    constexpr RgbColor black{0, 0, 0};

    RgbColor endpoint = preferredText;
    if (ContrastRatio(background, endpoint) < minimumContrast) {
        endpoint = ContrastRatio(background, white) >= ContrastRatio(background, black) ? white : black;
    }

    // 背景から順に混合率を上げ、最低コントラストを満たす最初の色を採用する。
    for (int percent = 20; percent <= 100; percent += 5) {
        const RgbColor candidate = MixRgb(background, endpoint, percent);
        if (ContrastRatio(background, candidate) >= minimumContrast) {
            return candidate;
        }
    }
    return endpoint;
}

} // namespace ViewReservationUi
