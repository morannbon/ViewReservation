#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <chrono>

#include "TVTestPlugin.h"

struct DriverChannelEntry {
    std::wstring DriverName;
    int Space = -1;
    int Channel = -1;
    WORD NetworkID = 0;
    WORD TransportStreamID = 0;
    WORD ServiceID = 0;
};

class DriverCache {
public:
    void Invalidate();
    void RebuildIfNeeded(TVTest::CTVTestApp *app, bool force);
    bool FindExact(WORD nid, WORD tsid, WORD sid, DriverChannelEntry *out) const;
    std::vector<DriverChannelEntry> FindByNetwork(WORD nid) const;
    std::vector<DriverChannelEntry> Snapshot() const;

private:
    std::vector<DriverChannelEntry> m_entries;
    std::chrono::steady_clock::time_point m_lastBuild{};
    bool m_valid = false;
};
