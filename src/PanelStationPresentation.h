#pragma once

#include "PanelColor.h"

namespace ViewReservationUi {

enum class PanelStationEmphasis {
    None,
    Viewing,
    Zapping
};

struct PanelPresentationColors {
    RgbColor StationText;
    RgbColor TimeText;
    RgbColor CurrentProgramText;
    RgbColor FutureProgramText;
    RgbColor ViewingBackground;
    RgbColor ZappingBackground;
    RgbColor ViewReservationMark;
    RgbColor RecordReservationMark;
};

inline constexpr PanelPresentationColors kPanelPresentationColors{
    {112, 174, 218},
    {184, 190, 198},
    {224, 187, 103},
    {118, 184, 137},
    {91, 75, 34},
    {90, 48, 48},
    {105, 200, 255},
    {255, 105, 105}
};

inline RgbColor ResolvePanelStationBackground(PanelStationEmphasis emphasis) {
    switch (emphasis) {
    case PanelStationEmphasis::Viewing:
        return kPanelPresentationColors.ViewingBackground;
    case PanelStationEmphasis::Zapping:
        return kPanelPresentationColors.ZappingBackground;
    default:
        return {};
    }
}

inline RgbColor ResolvePanelReservationMarkColor(bool record) {
    return record
        ? kPanelPresentationColors.RecordReservationMark
        : kPanelPresentationColors.ViewReservationMark;
}

} // namespace ViewReservationUi
