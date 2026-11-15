#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
#include "../src/PanelViewportGeometry.h"
#include "../src/PanelGlyph.h"

using ViewReservationUi::PanelViewportGeometry;
using Action = PanelViewportGeometry::ScrollAction;

static void CheckViewport(PanelViewportGeometry geometry) {
    const int total = geometry.TotalHeight();
    const int maxScroll = geometry.MaxScroll();
    assert(maxScroll >= 0 && maxScroll <= total);
    assert(geometry.ClampScroll(-100) == 0);
    assert(geometry.ClampScroll(static_cast<int64_t>(total) + 100) == maxScroll);

    const std::vector<int> positions = {0, 1, 2, maxScroll / 2, maxScroll - 1, maxScroll};
    for (int requested : positions) {
        geometry.ScrollY = geometry.ClampScroll(requested);
        const int scroll = geometry.ScrollY;
        assert(geometry.Scroll(Action::Top) == 0);
        assert(geometry.Scroll(Action::Bottom) == maxScroll);
        assert(geometry.Scroll(Action::Absolute, -5) == 0);
        assert(geometry.Scroll(Action::Absolute, total + (total < 1000 ? 100 : 0)) == maxScroll);
        if (geometry.LineStep() > 0) {
            assert(geometry.Scroll(Action::Line, -100000) == 0);
            assert(geometry.Scroll(Action::Line, 100000) == maxScroll);
        } else {
            assert(geometry.Scroll(Action::Line, -100000) == scroll);
            assert(geometry.Scroll(Action::Line, 100000) == scroll);
        }
        assert(geometry.Scroll(Action::Page, -1) <= scroll);
        assert(geometry.Scroll(Action::Page, 1) >= scroll);
        assert(geometry.Scroll(Action::Line, -1) <= scroll);
        assert(geometry.Scroll(Action::Line, 1) >= scroll);
        const int step = geometry.PageStep();
        if (step > 0 && scroll + static_cast<int64_t>(step) <= maxScroll) {
            geometry.ScrollY = geometry.Scroll(Action::Page, 1);
            assert(geometry.Scroll(Action::Page, -1) == scroll);
            geometry.ScrollY = scroll;
        }

        int previousEnd = 0;
        int expectedRow = -1;
        geometry.ForEachVisible([&](int index, int top, int rowHeight) {
            assert(index > expectedRow);
            expectedRow = index;
            assert(top < geometry.Height && static_cast<int64_t>(top) + rowHeight > 0);
            int actualTop = -1, actualBottom = -1;
            assert(geometry.RowRect(index, &actualTop, &actualBottom));
            assert(actualTop == top && actualBottom == top + rowHeight);
            if (previousEnd != 0) assert(top == previousEnd);
            previousEnd = top + rowHeight;
        });
        assert(geometry.HitTest(-1) == -1);
        assert(geometry.HitTest(geometry.Height) == -1);
        for (int y = 0; y < geometry.Height; ++y) {
            const int index = geometry.HitTest(y);
            if (static_cast<int64_t>(scroll) + y < total) {
                assert(index >= 0);
                int top = 0, bottom = 0;
                assert(geometry.RowRect(index, &top, &bottom));
                assert(top <= y && y < bottom);
            } else {
                assert(index == -1);
            }
        }
        for (int row = 0; row < geometry.RowCount(); ++row) {
            const int reveal = geometry.Scroll(Action::Reveal, row);
            const int center = geometry.Scroll(Action::Center, row);
            assert(0 <= reveal && reveal <= maxScroll);
            assert(0 <= center && center <= maxScroll);
        }

        const auto anchor = geometry.CaptureAnchor();
        for (int newHeight : {0, 1, 20, 40, 41, 100, 125, 350}) {
            auto resized = geometry;
            resized.Height = newHeight;
            const int restored = resized.RestoreAnchor(anchor, anchor.Row);
            assert(restored >= 0 && restored <= resized.MaxScroll());
            if (anchor.AtBottom) assert(restored == resized.MaxScroll());
            if (scroll == 0 && !anchor.AtBottom) assert(restored == 0);
            resized.ScrollY = restored;
            assert(resized.Scroll(Action::Top) == 0);
            assert(resized.Scroll(Action::Bottom) == resized.MaxScroll());
            if (resized.Height > 0 && resized.TotalHeight() >= resized.Height &&
                resized.ScrollY == resized.MaxScroll()) {
                // At the end there can be no blank area underneath content.
                assert(resized.HitTest(resized.Height - 1) >= 0);
            }
        }
    }
}

// Foreground primitives are atomic at both edges even when rows are clipped.
// The background remains continuous because ForEachVisible still visits them.
static void CheckAtomicForeground() {
    using G = PanelViewportGeometry;
    G view{{40, 44, 40, 44, 40, 44}, 0, 43};
    assert(view.ContainsCenteredLine(0, 18, 14));
    assert(!view.ContainsCenteredLine(40, 58, 14));
    assert(!view.ContainsCenteredLine(40, 80, ViewReservationUi::kPanelGlyphCanvasSide));
    view.Height = 56;
    assert(view.ContainsCenteredLine(40, 58, 14));
    assert(!view.ContainsCenteredLine(40, 80, ViewReservationUi::kPanelGlyphCanvasSide));
    view.Height = 68;
    assert(view.ContainsCenteredLine(40, 80, ViewReservationUi::kPanelGlyphCanvasSide));
    view.Height = 30;
    assert(!view.ContainsCenteredLine(-5, 13, 14));
    assert(!view.ContainsCenteredLine(25, 43, 14));
    assert(view.ContainsCenteredLine(5, 23, 14));
    assert(!view.ContainsVerticalSpan(0, 0));
    assert(!view.ContainsCenteredLine(1, 5, 0));
    assert(!view.ContainsCenteredLine(10, 10, ViewReservationUi::kPanelGlyphCanvasSide));

    // Cross every scroll offset and panel height, including a viewport too
    // short to display even one complete text line.
    for (int height = 0; height <= 150; ++height) {
        view.Height = height;
        for (int scroll = 0; scroll <= view.MaxScroll(); scroll += 3) {
            view.ScrollY = scroll;
            int visible = 0;
            view.ForEachVisible([&](int, int rowTop, int rowHeight) {
                ++visible;
                const int rowBottom = rowTop + rowHeight;
                const int stationBottom = (std::min)(rowBottom, rowTop + 18);
                for (int fontLine : {11, 14, 18, 25}) {
                    const auto check = [&](int top, int bottom) {
                        const int64_t contentTop = top + (static_cast<int64_t>(bottom) - top - fontLine) / 2;
                        const bool expected = height > 0 && bottom > top &&
                            contentTop >= 0 && contentTop + fontLine <= height;
                        assert(view.ContainsCenteredLine(top, bottom, fontLine) == expected);
                    };
                    check(rowTop, stationBottom);
                    check(stationBottom, rowBottom);
                    check(rowTop + 2, rowBottom - 2);
                    check(rowTop, rowBottom);
                }
                const int iconSide = ViewReservationUi::kPanelGlyphCanvasSide;
                const int64_t iconTop = rowTop + (static_cast<int64_t>(rowBottom) - rowTop - iconSide) / 2;
                const bool iconExpected = height > 0 && rowBottom > rowTop
                    && iconTop >= 0 && iconTop + iconSide <= height;
                assert(view.ContainsCenteredLine(rowTop, rowBottom, iconSide) == iconExpected);
            });
            if (height > 0) {
                const int total = view.TotalHeight();
                const int effective = (std::min)(height, total - scroll);
                if (effective > 0) assert(visible > 0 && view.HitTest(effective - 1) >= 0);
            }
        }
    }

    // Continuous resize and scroll never leaves an edge fragment eligible.
    view = G{{40, 44, 40, 44}, 0, 125};
    for (int height : {15, 16, 25, 1, 87, 40, 2, 110, 0, 83, 125}) {
        const auto saved = view.CaptureAnchor();
        view.Height = height;
        view.ScrollY = view.RestoreAnchor(saved, saved.Row);
        for (int delta : {1, 2, -1, -2, 5, -10}) {
            view.ScrollY = view.Scroll(G::ScrollAction::Line, delta);
            view.ForEachVisible([&](int, int rowTop, int rowHeight) {
                const int iconSide = ViewReservationUi::kPanelGlyphCanvasSide;
                if (view.ContainsCenteredLine(rowTop, rowTop + rowHeight, iconSide)) {
                    const int64_t top = rowTop + (static_cast<int64_t>(rowHeight) - iconSide) / 2;
                    assert(top >= 0 && top + iconSide <= height);
                }
            });
        }
    }
}

int main() {
    CheckAtomicForeground();
    PanelViewportGeometry viewport{{40, 44, 40, 44, 40, 44}, 0, 125};
    assert(viewport.TotalHeight() == 252);
    assert(viewport.MaxScroll() == 127);
    assert(viewport.LineStep() == 40);
    assert(viewport.PageStep() == 85);
    assert(viewport.HitTest(124) == 3);
    assert(viewport.HitTest(125) == -1);
    assert(viewport.Scroll(Action::Page, 1) == 85);
    assert(viewport.Scroll(Action::Reveal, 3) == 43);
    assert(viewport.Scroll(Action::Center, 5) == 127);
    int top = -1, bottom = -1;
    assert(viewport.RowRect(3, &top, &bottom) && top == 124 && bottom == 168);

    viewport.ScrollY = viewport.MaxScroll();
    assert(viewport.RowRect(5, &top, &bottom) && bottom == 125);
    assert(viewport.HitTest(0) == 3);
    assert(viewport.HitTest(124) == 5);
    assert(viewport.Scroll(Action::Reveal, 5) == viewport.MaxScroll());

    // Partial first-row display must return naturally to position zero.
    PanelViewportGeometry partial{{40, 44, 40, 44, 40, 44}, 19, 125};
    assert(partial.Scroll(Action::Line, -1) == 0);
    partial.ScrollY = 105;
    assert(partial.Scroll(Action::Line, -1) == 65);
    partial.ScrollY = 65;
    assert(partial.Scroll(Action::Line, -1) == 25);
    partial.ScrollY = 25;
    assert(partial.Scroll(Action::Line, -1) == 0);
    partial.ScrollY = partial.MaxScroll();
    for (int i = 0; i < 20; ++i) partial.ScrollY = partial.Scroll(Action::Line, -1);
    assert(partial.ScrollY == 0);

    // A row taller than the viewport remains scrollable by the wheel.
    PanelViewportGeometry shortView{{40}, 0, 20};
    assert(shortView.Scroll(Action::Line, 1) == shortView.MaxScroll());
    shortView.ScrollY = shortView.MaxScroll();
    assert(shortView.Scroll(Action::Line, -1) == 0);
    assert(shortView.HitTest(19) == 0);

    // The anchor tracks a row, not an independent hard-coded y offset.
    PanelViewportGeometry before{{40, 44, 40}, 61, 40};
    auto anchor = before.CaptureAnchor();
    assert(anchor.Row == 1 && anchor.WithinRow == 21 && !anchor.AtBottom);
    PanelViewportGeometry after{{40, 30, 44}, 0, 80};
    assert(after.RestoreAnchor(anchor, 1) == 34); // bounded by new MaxScroll
    after.Height = 40;
    assert(after.RestoreAnchor(anchor, 1) == 61);
    after.Heights = {40, 10, 44};
    assert(after.RestoreAnchor(anchor, 1) == 49);
    before.ScrollY = before.MaxScroll();
    anchor = before.CaptureAnchor();
    assert(anchor.AtBottom);
    after.Heights = {40, 44, 40};
    after.Height = 50;
    assert(after.RestoreAnchor(anchor, 1) == after.MaxScroll());
    after.Height = 200;
    assert(after.RestoreAnchor(anchor, 1) == 0);

    // Repeated resizing cannot cause stale scroll coordinates, including a
    // temporarily collapsed viewport and a return to its original height.
    PanelViewportGeometry resizedChain{{40, 44, 40, 44, 40, 44}, 127, 125};
    for (int newHeight : {80, 20, 0, 200, 125, 1, 252, 60, 125}) {
        const auto saved = resizedChain.CaptureAnchor();
        resizedChain.Height = newHeight;
        resizedChain.ScrollY = resizedChain.RestoreAnchor(saved, saved.Row);
        assert(resizedChain.ScrollY >= 0 && resizedChain.ScrollY <= resizedChain.MaxScroll());
        if (saved.AtBottom) assert(resizedChain.ScrollY == resizedChain.MaxScroll());
        if (newHeight > 0 && resizedChain.TotalHeight() >= newHeight &&
            resizedChain.ScrollY == resizedChain.MaxScroll())
            assert(resizedChain.HitTest(newHeight - 1) >= 0);
        assert(resizedChain.Scroll(Action::Top) == 0);
        assert(resizedChain.Scroll(Action::Bottom) == resizedChain.MaxScroll());
    }

    // Any display height, including zero and sub-row height, follows the
    // same painting, hit-testing, scrolling and end-clamping contract.
    for (int count = 0; count <= 15; ++count) {
        for (int height = 0; height <= 180; height += 7) {
            PanelViewportGeometry g;
            g.Height = height;
            for (int row = 0; row < count; ++row)
                g.Heights.push_back(row % 3 == 0 ? 44 : 40);
            CheckViewport(g);
        }
    }
    for (int height : {1, 19, 20, 39, 40, 41, 44, 45, 83, 84, 125, 252, 350}) {
        CheckViewport({{40, 44, 40, 44, 40, 44}, 0, height});
        CheckViewport({{40}, 0, height});
    }
    // Win32 SCROLLINFO is 32-bit: large row totals must not overflow.
    const int limit = (std::numeric_limits<int>::max)();
    PanelViewportGeometry large{{limit, limit}, 0, 50};
    assert(large.TotalHeight() == limit);
    assert(large.RowStart(2) == limit);
    assert(large.IndexAtOffset(limit - 1) == 0);
    assert(large.Scroll(Action::Line, 1) == 50);
    large.ScrollY = limit - 50;
    assert(large.HitTest(49) == 0);
    std::cout << "panel viewport geometry tests passed\n";
}
