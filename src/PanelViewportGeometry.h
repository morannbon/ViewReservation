#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace ViewReservationUi {

// All panel navigation, painting and hit testing share one pixel coordinate space.
// The viewport may clip an edge row, but never discards a row to fill the viewport.
struct PanelViewportGeometry {
    std::vector<int> Heights;
    int ScrollY = 0;
    int Height = 0;

    struct Anchor {
        int Row = -1;
        int WithinRow = 0;
        bool AtBottom = false;
    };

    enum class ScrollAction {
        Line, Page, Top, Bottom, Absolute, Reveal, Center
    };

    static int Coordinate(int64_t value) {
        return static_cast<int>((std::max)(int64_t{0},
            (std::min)(value, static_cast<int64_t>((std::numeric_limits<int>::max)()))));
    }

    int RowCount() const { return static_cast<int>(Heights.size()); }
    int RowHeight(int index) const { return (std::max)(1, Heights[static_cast<size_t>(index)]); }

    int TotalHeight() const {
        int64_t total = 0;
        for (int i = 0; i < RowCount(); ++i) {
            total += RowHeight(i);
            if (total >= (std::numeric_limits<int>::max)())
                return (std::numeric_limits<int>::max)();
        }
        return static_cast<int>(total);
    }

    // Win32 SCROLLINFO treats nPage == 0 as a one-pixel scroll range.
    int MaxScroll() const { return (std::max)(0, TotalHeight() - (std::max)(1, Height)); }
    int ClampScroll(int64_t scroll) const {
        return static_cast<int>((std::max)(int64_t{0},
            (std::min)(scroll, static_cast<int64_t>(MaxScroll()))));
    }

    int RowStart(int index) const {
        int64_t y = 0;
        for (int i = 0; i < (std::max)(0, (std::min)(index, RowCount())); ++i)
            y += RowHeight(i);
        return Coordinate(y);
    }

    int IndexAtOffset(int offset) const {
        if (RowCount() == 0) return -1;
        int64_t end = 0;
        for (int i = 0; i < RowCount(); ++i) {
            end += RowHeight(i);
            if (static_cast<int64_t>(offset) < end) return i;
        }
        return RowCount() - 1;
    }

    template<class Visitor>
    void ForEachVisible(Visitor visitor) const {
        if (Height <= 0) return;
        int64_t y = -static_cast<int64_t>(ClampScroll(ScrollY));
        for (int i = 0; i < RowCount() && y < Height; ++i) {
            const int h = RowHeight(i);
            if (y + h > 0) visitor(i, static_cast<int>(y), h);
            y += h;
        }
    }

    // A row may cross either viewport edge, but foreground primitives are
    // atomic: never paint a clipped fragment of a text line or a symbol.
    // Backgrounds and separators still occupy the entire scrollable height.
    bool ContainsVerticalSpan(int64_t first, int64_t last) const {
        return Height > 0 && first >= 0 && last > first && last <= Height;
    }

    // Text, marquee and bitmap glyphs share the same centered foreground span.
    // A clipped foreground primitive is never partially painted.
    bool ContainsCenteredLine(int64_t first, int64_t last, int lineHeight) const {
        if (last <= first || lineHeight <= 0) return false;
        const int64_t top = first + (last - first - lineHeight) / 2;
        return ContainsVerticalSpan(top, top + lineHeight);
    }

    bool RowRect(int index, int *top, int *bottom) const {
        if (index < 0 || index >= RowCount() || Height <= 0) return false;
        const int64_t first = static_cast<int64_t>(RowStart(index)) - ClampScroll(ScrollY);
        const int64_t last = first + RowHeight(index);
        if (first >= Height || last <= 0) return false;
        if (top) *top = static_cast<int>((std::max)(static_cast<int64_t>((std::numeric_limits<int>::min)()), first));
        if (bottom) *bottom = static_cast<int>((std::min)(static_cast<int64_t>((std::numeric_limits<int>::max)()), last));
        return true;
    }

    int HitTest(int y) const {
        if (y < 0 || y >= Height) return -1;
        const int64_t offset = static_cast<int64_t>(ClampScroll(ScrollY)) + y;
        return offset < TotalHeight() ? IndexAtOffset(static_cast<int>(offset)) : -1;
    }

    int LineStep() const {
        if (Heights.empty() || Height <= 0) return 0;
        int minimum = (std::numeric_limits<int>::max)();
        for (int i = 0; i < RowCount(); ++i)
            minimum = (std::min)(minimum, RowHeight(i));
        return (std::min)(minimum, Height);
    }

    int PageStep() const {
        if (Height <= 0) return 0;
        return (std::max)(1, Height - (std::min)(LineStep(), Height - 1));
    }

    // The only location where a scroll command is converted into an absolute
    // pixel position. All callers share the same limits and movement rules.
    int Scroll(ScrollAction action, int value = 0) const {
        const int current = ClampScroll(ScrollY);
        switch (action) {
        case ScrollAction::Line:
            return ClampScroll(static_cast<int64_t>(current) + static_cast<int64_t>(LineStep()) * value);
        case ScrollAction::Page:
            return ClampScroll(static_cast<int64_t>(current) + static_cast<int64_t>(PageStep()) * value);
        case ScrollAction::Top:
            return 0;
        case ScrollAction::Bottom:
            return MaxScroll();
        case ScrollAction::Absolute:
            return ClampScroll(value);
        case ScrollAction::Reveal: {
            if (value < 0 || value >= RowCount() || Height <= 0) return current;
            const int64_t first = RowStart(value);
            const int64_t last = first + RowHeight(value);
            if (first < current) return ClampScroll(first);
            // Oversized rows cannot fit in full: show their beginning when
            // moving to them, rather than hiding their heading to show the end.
            if (RowHeight(value) > Height) {
                if (first >= static_cast<int64_t>(current) + Height) return ClampScroll(first);
                return current;
            }
            if (last > static_cast<int64_t>(current) + Height) return ClampScroll(last - Height);
            return current;
        }
        case ScrollAction::Center:
            if (value < 0 || value >= RowCount()) return current;
            return ClampScroll(static_cast<int64_t>(RowStart(value)) + RowHeight(value) / 2 - Height / 2);
        }
        return current;
    }

    Anchor CaptureAnchor() const {
        Anchor result{};
        const int scroll = ClampScroll(ScrollY);
        result.Row = IndexAtOffset(scroll);
        if (result.Row >= 0)
            result.WithinRow = (std::max)(0, scroll - RowStart(result.Row));
        result.AtBottom = MaxScroll() > 0 && scroll == MaxScroll();
        return result;
    }

    // Row identity mapping belongs to the caller. Within-row position is
    // bounded by the new row height; resizing preserves the bottom if pinned.
    int RestoreAnchor(const Anchor &anchor, int newRow) const {
        if (anchor.AtBottom) return MaxScroll();
        if (newRow < 0 || newRow >= RowCount()) return 0;
        return ClampScroll(static_cast<int64_t>(RowStart(newRow)) +
            (std::min)((std::max)(0, anchor.WithinRow), RowHeight(newRow) - 1));
    }
};

} // namespace ViewReservationUi
