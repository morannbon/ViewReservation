#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>

#include "TVTestPlugin.h"

struct DriverChannelEntry {
    std::wstring DriverName;
    int Space = -1;
    int Channel = -1;
    WORD NetworkID = 0;
    WORD TransportStreamID = 0;
    WORD ServiceID = 0;
    TVTest::TuningSpaceType SpaceType = TVTest::TUNINGSPACE_UNKNOWN;
    std::wstring SpaceName;
    std::wstring ChannelName;
};

enum class ChannelDisplayWave { Terrestrial, Bs, Cs, Catv };

// Display classification only; DriverName/Space/Channel remain the tuning route.
// Unknown nonzero networks use the CATV display bucket, not a claim that a
// particular driver or tuning space implements cable reception.
inline ChannelDisplayWave ResolveChannelDisplayWave(const DriverChannelEntry &entry) {
    if (entry.NetworkID == 4) return ChannelDisplayWave::Bs;
    if (entry.NetworkID == 6 || entry.NetworkID == 7 || entry.NetworkID == 10)
        return ChannelDisplayWave::Cs;
    if (entry.NetworkID >= 0x7880 && entry.NetworkID <= 0x7FE8)
        return ChannelDisplayWave::Terrestrial;
    if (entry.NetworkID != 0) return ChannelDisplayWave::Catv;

    // No network identity yet: retain TVTest's explicit legacy space type.
    if (entry.SpaceType == TVTest::TUNINGSPACE_BS) return ChannelDisplayWave::Bs;
    if (entry.SpaceType == TVTest::TUNINGSPACE_110CS) return ChannelDisplayWave::Cs;
    return ChannelDisplayWave::Terrestrial;
}

class DriverCache {
public:
    void Invalidate();
    void RebuildIfNeeded(TVTest::CTVTestApp *app, bool force);
    bool FindExact(WORD nid, WORD tsid, WORD sid, DriverChannelEntry *out) const;
    bool FindExactOnDriver(WORD nid, WORD tsid, WORD sid, const std::wstring &driverName, DriverChannelEntry *out) const;
    std::vector<DriverChannelEntry> FindByNetwork(WORD nid) const;
    std::vector<DriverChannelEntry> Snapshot() const;

private:
    mutable std::mutex m_mutex;
    std::vector<DriverChannelEntry> m_entries;
    std::chrono::steady_clock::time_point m_lastBuild{};
    bool m_valid = false;
};
