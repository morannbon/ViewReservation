#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <set>

#include "TVTestPlugin.h"

struct DriverChannelEntry {
    std::wstring DriverName;
    int Space = -1;
    int Channel = -1;
    WORD NetworkID = 0;
    WORD TransportStreamID = 0;
    WORD ServiceID = 0;
    TVTest::TuningSpaceType SpaceType = TVTest::TUNINGSPACE_UNKNOWN;
    TVTest::ChannelFlag Flags = TVTest::CHANNEL_FLAG_NONE;
    std::wstring SpaceName;
    std::wstring ChannelName;
};

enum class ChannelDisplayWave { Terrestrial, Bs, Cs, Catv };

inline ULONGLONG MakeChannelServiceKey(const DriverChannelEntry &entry) {
    return (static_cast<ULONGLONG>(entry.NetworkID) << 32) |
        (static_cast<ULONGLONG>(entry.TransportStreamID) << 16) |
        static_cast<ULONGLONG>(entry.ServiceID);
}

inline bool IsChannelEnabledForDisplay(const DriverChannelEntry &entry) {
    return (static_cast<DWORD>(entry.Flags) & static_cast<DWORD>(TVTest::CHANNEL_FLAG_DISABLED)) == 0;
}

inline std::vector<DriverChannelEntry> BuildEnabledDisplayServices(const std::vector<DriverChannelEntry> &entries) {
    std::vector<DriverChannelEntry> result;
    std::set<ULONGLONG> seen;
    result.reserve(entries.size());
    for (const auto &entry : entries) {
        if (!IsChannelEnabledForDisplay(entry)) {
            continue;
        }
        const ULONGLONG key = MakeChannelServiceKey(entry);
        if (!seen.insert(key).second) {
            continue;
        }
        result.push_back(entry);
    }
    return result;
}

// 表示分類と選局経路は分離する。DriverName / Space / Channel は選局経路として保持する。
inline ChannelDisplayWave ResolveChannelDisplayWave(const DriverChannelEntry &entry) {
    if (entry.NetworkID == 4) return ChannelDisplayWave::Bs;
    if (entry.NetworkID == 6 || entry.NetworkID == 7 || entry.NetworkID == 10)
        return ChannelDisplayWave::Cs;
    if (entry.NetworkID >= 0x7880 && entry.NetworkID <= 0x7FE8)
        return ChannelDisplayWave::Terrestrial;
    if (entry.NetworkID != 0) return ChannelDisplayWave::Catv;

    // NetworkID未確定時はTVTestが返したチューニング空間種別を使う。
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
    std::vector<DriverChannelEntry> SnapshotDisplayServices() const;

private:
    mutable std::mutex m_mutex;
    std::vector<DriverChannelEntry> m_entries;
    std::chrono::steady_clock::time_point m_lastBuild{};
    bool m_valid = false;
};
