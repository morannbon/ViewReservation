#include <cassert>
#include <iostream>
#include "../src/PanelWaveBarLayout.h"

int main() {
    using Layout = ViewReservationUi::PanelWaveBarLayout;
    using Item = Layout::Item;
    for (const bool catv : {false, true}) {
        for (const int square : {16, 24, 32}) {
            for (int width = 0; width <= 1600; ++width) {
                for (const int margin : {0, 4}) {
                    constexpr int gap = 3;
                    const auto layout = Layout::Calculate(width, margin, gap, square, catv);
                    assert(layout.WaveCount == (catv ? 4 : 3));
                    assert(layout.Get(Item::Catv).Width == (catv ? layout.Waves[3].Width : 0));
                    assert(layout.Power.Width == (width - 2 * margin >= square ? square : 0));
                    assert(layout.Refresh.Width == (width - 2 * margin >= 2 * square + gap ? square : 0));

                    if (width - 2 * margin >= 2 * square + gap) {
                        assert(layout.Power.X + square == width - margin);
                        assert(layout.Refresh.X + square + gap == layout.Power.X);
                    }
                    if (layout.Refresh.Width != 0) {
                        for (int i = 0; i < layout.WaveCount; ++i) {
                            const auto w = layout.Waves[static_cast<size_t>(i)];
                            assert(w.X >= margin);
                            assert(w.Width >= 0);
                            if (w.Width > 0) assert(w.X + w.Width <= layout.Refresh.X - gap);
                        }
                    } else {
                        for (const auto wave : layout.Waves) assert(wave.Width == 0);
                    }
                    // No two visible segments may share a pixel. Rendering and
                    // input always refer to this exact shared geometry.
                    for (int x = 0; x < width; ++x) {
                        const auto hit = layout.HitTest(x);
                        int hits = 0;
                        Item expected = Item::None;
                        for (const auto item : Layout::Items) {
                            if (layout.Get(item).Contains(x)) {
                                ++hits;
                                expected = item;
                            }
                        }
                        assert(hits <= 1);
                        assert(hit == expected);
                    }
                    for (const auto item : Layout::Items) {
                        const auto segment = layout.Get(item);
                        if (segment.Width > 0 && segment.X >= 0 && segment.X + segment.Width <= width) {
                            assert(layout.HitTest(segment.X) == item);
                            assert(layout.HitTest(segment.X + segment.Width - 1) == item);
                        }
                    }
                    // When the pane is wide enough to show all waves, the
                    // remaining width is distributed almost equally.
                    if (width >= 2 * margin + 2 * square + 2 * gap +
                            (layout.WaveCount - 1) * gap + layout.WaveCount) {
                        for (int i = 0; i + 1 < layout.WaveCount; ++i) {
                            assert(layout.Waves[static_cast<size_t>(i)].Width >=
                                layout.Waves[static_cast<size_t>(i + 1)].Width);
                            assert(layout.Waves[static_cast<size_t>(i)].Width -
                                layout.Waves[static_cast<size_t>(i + 1)].Width <= 1);
                        }
                    }
                }
            }
        }
    }
    std::cout << "panel header resize, fixed squares and hit-test invariants passed\n";
}
