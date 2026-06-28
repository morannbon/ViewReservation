#include "DriverCache.h"

#include <algorithm>

void DriverCache::Invalidate() {
    m_valid = false;
}

void DriverCache::RebuildIfNeeded(TVTest::CTVTestApp *app, bool force) {
    if (app == nullptr) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!force && m_valid && (now - m_lastBuild) < std::chrono::minutes(30)) {
        return;
    }

    std::vector<DriverChannelEntry> entries;
    wchar_t driverName[MAX_PATH]{};

    for (int driverIndex = 0;; ++driverIndex) {
        if (app->EnumDriver(driverIndex, driverName, MAX_PATH) <= 0) {
            break;
        }

        TVTest::DriverTuningSpaceList list{};
        list.Flags = TVTest::DRIVER_TUNING_SPACE_LIST_FLAG_NONE;

        if (!app->GetDriverTuningSpaceList(driverName, &list)) {
            continue;
        }

        for (DWORD s = 0; s < list.NumSpaces; ++s) {
            const TVTest::DriverTuningSpaceInfo *space = list.SpaceList[s];
            if (space == nullptr) {
                continue;
            }

            for (DWORD c = 0; c < space->NumChannels; ++c) {
                const TVTest::ChannelInfo *ch = space->ChannelList[c];
                if (ch == nullptr) {
                    continue;
                }

                DriverChannelEntry entry{};
                entry.DriverName = driverName;
                entry.Space = ch->Space;
                entry.Channel = ch->Channel;
                entry.NetworkID = ch->NetworkID;
                entry.TransportStreamID = ch->TransportStreamID;
                entry.ServiceID = ch->ServiceID;
                entries.push_back(entry);
            }
        }

        app->FreeDriverTuningSpaceList(&list);
    }

    m_entries.swap(entries);
    m_lastBuild = now;
    m_valid = true;
}

bool DriverCache::FindExact(WORD nid, WORD tsid, WORD sid, DriverChannelEntry *out) const {
    for (const auto &entry : m_entries) {
        if (entry.NetworkID == nid &&
            entry.TransportStreamID == tsid &&
            entry.ServiceID == sid) {
            if (out != nullptr) {
                *out = entry;
            }
            return true;
        }
    }
    return false;
}

std::vector<DriverChannelEntry> DriverCache::FindByNetwork(WORD nid) const {
    std::vector<DriverChannelEntry> result;
    for (const auto &entry : m_entries) {
        if (entry.NetworkID == nid) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<DriverChannelEntry> DriverCache::Snapshot() const {
    return m_entries;
}
