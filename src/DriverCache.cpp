#include "DriverCache.h"

#include <algorithm>
#include <cwctype>

namespace {

std::wstring UpperAsciiLike(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return text;
}

bool IsCatvTuningSpaceName(const wchar_t *name) {
    if (name == nullptr || *name == L'\0') {
        return false;
    }

    const std::wstring upper = UpperAsciiLike(name);
    static constexpr const wchar_t *markers[] = {
        L"ISDB-C",
        L"ISDBC",
        L"CATV",
        L"CABLE",
        L"TRANSMODULATION",
        L"TRANSMOD",
        L"ケーブル"
    };
    for (const wchar_t *marker : markers) {
        if (upper.find(marker) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

void DriverCache::Invalidate() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_valid = false;
}

void DriverCache::RebuildIfNeeded(TVTest::CTVTestApp *app, bool force) {
    if (app == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

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
                entry.SpaceType = space->pInfo != nullptr ? space->pInfo->Space : TVTest::TUNINGSPACE_UNKNOWN;
                if (space->pInfo != nullptr) {
                    entry.SpaceName = space->pInfo->szName;
                    entry.IsCatv = IsCatvTuningSpaceName(space->pInfo->szName);
                }
                entry.ChannelName = ch->szChannelName;
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
    std::lock_guard<std::mutex> lock(m_mutex);
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

bool DriverCache::FindExactOnDriver(WORD nid, WORD tsid, WORD sid, const std::wstring &driverName, DriverChannelEntry *out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &entry : m_entries) {
        if (_wcsicmp(entry.DriverName.c_str(), driverName.c_str()) == 0 &&
            entry.NetworkID == nid &&
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
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DriverChannelEntry> result;
    for (const auto &entry : m_entries) {
        if (entry.NetworkID == nid) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<DriverChannelEntry> DriverCache::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}
