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
    bool IsCatv = false;
    std::wstring ChannelName;
};

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
