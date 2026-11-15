#include "../src/PanelGlyph.h"
#include "../src/PanelWaveBarLayout.h"

#include <cassert>
#include <cstddef>

int main() {
    using namespace ViewReservationUi;
    constexpr RgbColor dark{46, 56, 63};
    constexpr RgbColor light{223, 231, 236};
    constexpr RgbColor icon{205, 211, 216};
    static_assert(kPanelActionButtonSide == 24);
    static_assert(kPanelGlyphCanvasSide == 16);
    static_assert(sizeof(PanelGlyphPixel) == 4);
    static_assert(kPanelActionButtonSide - kPanelGlyphCanvasSide == 8);

    constexpr PanelGlyph glyphs[] = {
        PanelGlyph::Refresh, PanelGlyph::Power,
        PanelGlyph::ChevronDown, PanelGlyph::ChevronUp
    };
    for (const PanelGlyph glyph : glyphs) {
        const auto& alpha = PanelGlyphCoverage(glyph);
        const auto pixels = ComposePanelGlyph(glyph, icon, dark);
        const auto lightPixels = ComposePanelGlyph(glyph, dark, light);
        assert(alpha.size() == kPanelGlyphPixels);
        assert(pixels.size() == kPanelGlyphPixels);
        int xMin = kPanelGlyphCanvasSide;
        int yMin = kPanelGlyphCanvasSide;
        int xMax = -1;
        int yMax = -1;
        bool hasSoftEdge = false;
        for (int y = 0; y < kPanelGlyphCanvasSide; ++y) {
            for (int x = 0; x < kPanelGlyphCanvasSide; ++x) {
                const auto i = static_cast<std::size_t>(y * kPanelGlyphCanvasSide + x);
                if (alpha[i] > 0) {
                    if (x < xMin) xMin = x;
                    if (y < yMin) yMin = y;
                    if (x > xMax) xMax = x;
                    if (y > yMax) yMax = y;
                }
                if (alpha[i] > 0 && alpha[i] < 255) hasSoftEdge = true;
                if (alpha[i] == 0) {
                    assert(pixels[i].Red == dark.Red);
                    assert(pixels[i].Green == dark.Green);
                    assert(pixels[i].Blue == dark.Blue);
                    assert(lightPixels[i].Red == light.Red);
                }
                assert(pixels[i].Unused == 0);
                assert(lightPixels[i].Unused == 0);
                if (x == 0 || x == kPanelGlyphCanvasSide - 1 ||
                    y == 0 || y == kPanelGlyphCanvasSide - 1) {
                    assert(alpha[i] == 0);  // no drawing into neighbor or button border
                }
            }
        }
        assert(hasSoftEdge);
        assert(xMin > 0 && yMin > 0);
        assert(xMax < kPanelGlyphCanvasSide - 1);
        assert(yMax < kPanelGlyphCanvasSide - 1);
        const int width = xMax - xMin + 1;
        const int height = yMax - yMin + 1;
        if (glyph == PanelGlyph::ChevronUp || glyph == PanelGlyph::ChevronDown) {
            assert(width <= 10 && height <= 6);  // quieter than the old filled triangle
        } else {
            assert(width <= 13 && height <= 13);  // centered, never fills the 24px button
        }
    }

    assert(PanelGlyphCoverage(PanelGlyph::Refresh) != PanelGlyphCoverage(PanelGlyph::Power));
    assert(PanelGlyphCoverage(PanelGlyph::ChevronUp) != PanelGlyphCoverage(PanelGlyph::ChevronDown));
}
