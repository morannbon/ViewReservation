#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace ViewReservationUi {

inline constexpr int kPanelActionButtonSide = 24;

// The same segments determine painting, mouse hit-testing and keyboard navigation.
// The fixed action squares never participate in the wave-button width allocation.
struct PanelWaveBarLayout {
    enum class Item { None, Terrestrial, Bs, Cs, Catv, Refresh, Power };

    struct Segment {
        int X = 0;
        int Width = 0;

        bool Contains(int x) const { return Width > 0 && x >= X && x - X < Width; }
    };

    static constexpr std::array<Item, 6> Items = {
        Item::Terrestrial, Item::Bs, Item::Cs, Item::Catv, Item::Refresh, Item::Power
    };

    std::array<Segment, 4> Waves{};
    Segment Refresh{};
    Segment Power{};
    int WaveCount = 0;

    Segment Get(Item item) const {
        switch (item) {
        case Item::Terrestrial: return Waves[0];
        case Item::Bs: return Waves[1];
        case Item::Cs: return Waves[2];
        case Item::Catv: return Waves[3];
        case Item::Refresh: return Refresh;
        case Item::Power: return Power;
        default: return {};
        }
    }

    Item HitTest(int x) const {
        for (const Item item : Items) {
            if (Get(item).Contains(x)) return item;
        }
        return Item::None;
    }

    static PanelWaveBarLayout Calculate(int clientWidth, int margin, int gap, int buttonHeight, bool hasCatv) {
        PanelWaveBarLayout result;
        result.WaveCount = hasCatv ? 4 : 3;
        if (buttonHeight <= 0) return result;
        margin = (std::max)(0, margin);
        gap = (std::max)(0, gap);
        const int usable = (std::max)(0, clientWidth - 2 * margin);
        if (usable < buttonHeight) return result;

        // Even when a hosting panel becomes too narrow for the complete header,
        // never resize or overlay the action buttons. Prefer Power, then Refresh.
        result.Power = {margin + usable - buttonHeight, buttonHeight};
        if (usable < 2 * buttonHeight + gap) return result;
        result.Refresh = {result.Power.X - gap - buttonHeight, buttonHeight};

        const int waveSpace = (std::max)(0, result.Refresh.X - gap - margin);
        const int waveGap = waveSpace >= gap * (result.WaveCount - 1) ? gap : 0;
        const int widths = waveSpace - waveGap * (result.WaveCount - 1);
        const int each = widths / result.WaveCount;
        const int remainder = widths % result.WaveCount;
        int x = margin;
        for (int i = 0; i < result.WaveCount; ++i) {
            const int width = each + (i < remainder ? 1 : 0);
            result.Waves[static_cast<size_t>(i)] = {x, width};
            x += width + (i < result.WaveCount - 1 ? waveGap : 0);
        }
        return result;
    }
};

} // namespace ViewReservationUi
