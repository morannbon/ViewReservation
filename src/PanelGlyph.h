#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PanelColor.h"

namespace ViewReservationUi {

// Every panel control glyph uses the same centered alpha footprint and
// palette-based compositor. Button sizing belongs to PanelWaveBarLayout.
inline constexpr int kPanelGlyphCanvasSide = 16;
inline constexpr std::size_t kPanelGlyphPixels =
    static_cast<std::size_t>(kPanelGlyphCanvasSide * kPanelGlyphCanvasSide);

enum class PanelGlyph { Refresh, Power, ChevronDown, ChevronUp };

inline constexpr std::array<std::uint8_t, kPanelGlyphPixels> kRefreshGlyphAlpha = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,  14, 105, 156, 159, 111,  33,   0,   0,   0,   0,   0,
      0,   0,   0,   0,  98, 240, 255, 228, 225, 255, 248, 130, 105,  25,   0,   0,
      0,   0,   0,  95, 255, 170,  37,   0,   0, 104, 244, 248, 255, 125,   0,   0,
      0,   0,  13, 242, 173,   0,   0,   0,   0,   0,  21,  14, 235, 133,   0,   0,
      0,   0, 109, 255,  25,   0,   0,   0,   0,   0,   0,   0, 255, 150,   0,   0,
      0,   0, 161, 226,   0,   0,   0,   0,   0,   0,   0,   0, 139,  74,   0,   0,
      0,   0, 162, 226,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0, 120, 254,  17,   0,   0,   0,   0,   0,   0,   0,  30,   0,   0,   0,
      0,   0,  22, 250, 159,   0,   0,   0,   0,   0,   0, 155, 241,   0,   0,   0,
      0,   0,   0, 107, 255, 147,  18,   0,   0,  14, 135, 255, 131,   0,   0,   0,
      0,   0,   0,   0, 109, 255, 254, 210, 210, 251, 255, 129,   0,   0,   0,   0,
      0,   0,   0,   0,   0,  34, 125, 179, 179, 127,  42,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

inline constexpr std::array<std::uint8_t, kPanelGlyphPixels> kPowerGlyphAlpha = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0, 147, 165,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,  16,   0,   0, 192, 233,   0,   0,  17,   0,   0,   0,   0,
      0,   0,   0,  56, 241,  86,   0, 181, 224,   0,  63, 244,  74,   0,   0,   0,
      0,   0,  13, 230, 220,  30,   0, 182, 225,   0,  21, 205, 242,  24,   0,   0,
      0,   0,  96, 255,  55,   0,   0, 179, 222,   0,   0,  36, 255, 119,   0,   0,
      0,   0, 170, 238,   0,   0,   0, 183, 227,   0,   0,   0, 221, 194,   0,   0,
      0,   0, 182, 223,   0,   0,   0, 179, 204,   0,   0,   0, 201, 205,   0,   0,
      0,   0, 144, 249,  10,   0,   0,  19,  21,   0,   0,   0, 239, 168,   0,   0,
      0,   0,  55, 255, 113,   0,   0,   0,   0,   0,   0,  91, 255,  74,   0,   0,
      0,   0,   0, 165, 255,  99,   0,   0,   0,   0,  85, 255, 184,   0,   0,   0,
      0,   0,   0,   9, 171, 255, 219, 161, 158, 213, 255, 186,  17,   0,   0,   0,
      0,   0,   0,   0,   0,  85, 196, 240, 241, 202,  96,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

inline constexpr std::array<std::uint8_t, kPanelGlyphPixels> kChevronDownAlpha = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,  24,  60,   0,   0,   0,   0,   0,   0,  56,  31,   0,   0,   0,
      0,   0,   0, 109, 255,  95,   0,   0,   0,   0,  78, 255, 133,   0,   0,   0,
      0,   0,   0,  12, 168, 255, 100,   0,   0,  84, 255, 185,  17,   0,   0,   0,
      0,   0,   0,   0,   0, 163, 255, 103,  87, 255, 179,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0, 153, 255, 255, 170,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0, 139, 150,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

inline constexpr std::array<std::uint8_t, kPanelGlyphPixels> kChevronUpAlpha = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0, 128, 139,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0, 134, 255, 255, 152,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0, 144, 255, 122, 105, 255, 161,   0,   0,   0,   0,   0,
      0,   0,   0,   0, 149, 255, 118,   0,   0, 102, 255, 167,   9,   0,   0,   0,
      0,   0,   0, 106, 255, 113,   0,   0,   0,   0,  95, 255, 130,   0,   0,   0,
      0,   0,   0,  33,  79,   0,   0,   0,   0,   0,   0,  75,  42,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

inline constexpr const std::array<std::uint8_t, kPanelGlyphPixels>&
PanelGlyphCoverage(PanelGlyph glyph) {
    switch (glyph) {
    case PanelGlyph::Refresh: return kRefreshGlyphAlpha;
    case PanelGlyph::Power: return kPowerGlyphAlpha;
    case PanelGlyph::ChevronDown: return kChevronDownAlpha;
    case PanelGlyph::ChevronUp: return kChevronUpAlpha;
    }
    return kRefreshGlyphAlpha;
}

struct PanelGlyphPixel {
    std::uint8_t Blue;
    std::uint8_t Green;
    std::uint8_t Red;
    std::uint8_t Unused;
};

inline std::array<PanelGlyphPixel, kPanelGlyphPixels> ComposePanelGlyph(
    PanelGlyph glyph, RgbColor foreground, RgbColor background) {
    const auto &coverage = PanelGlyphCoverage(glyph);
    std::array<PanelGlyphPixel, kPanelGlyphPixels> pixels{};
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const unsigned int alpha = coverage[i];
        const unsigned int inverse = 255 - alpha;
        const auto mix = [alpha, inverse](int fg, int bg) -> std::uint8_t {
            return static_cast<std::uint8_t>((fg * alpha + bg * inverse + 127) / 255);
        };
        pixels[i] = {
            mix(foreground.Blue, background.Blue),
            mix(foreground.Green, background.Green),
            mix(foreground.Red, background.Red),
            0
        };
    }
    return pixels;
}

} // namespace ViewReservationUi
