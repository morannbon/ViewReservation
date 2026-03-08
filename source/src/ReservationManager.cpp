#include "ReservationManager.h"

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace {
constexpr wchar_t kSectionSettings[] = L"Settings";
constexpr wchar_t kKeyCount[] = L"Count";

static int ReadInt(LPCWSTR section, LPCWSTR key, int defValue, const std::wstring &path) {
    return GetPrivateProfileIntW(section, key, defValue, path.c_str());
}
}

ReservationManager::ReservationManager(std::wstring iniPath)
    : m_iniPath(std::move(iniPath)) {
}

bool ReservationManager::SameIdentity(const ReservationItem &a, WORD nid, WORD tsid, WORD sid, WORD eid) {
    return a.NetworkID == nid &&
           a.TransportStreamID == tsid &&
           a.ServiceID == sid &&
           a.EventID == eid;
}

bool ReservationManager::SameStart(const ReservationItem &a, const ReservationItem &b) {
    return ToFileTimeValue(a.StartTime) == ToFileTimeValue(b.StartTime);
}

ULONGLONG ReservationManager::ToFileTimeValue(const SYSTEMTIME &st) {
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER ui{};
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return ui.QuadPart;
}

std::wstring ReservationManager::FormatDateTime(const SYSTEMTIME &st) {
    std::wostringstream oss;
    oss << std::setfill(L'0')
        << std::setw(4) << st.wYear << L"/"
        << std::setw(2) << st.wMonth << L"/"
        << std::setw(2) << st.wDay << L" "
        << std::setw(2) << st.wHour << L":"
        << std::setw(2) << st.wMinute << L":"
        << std::setw(2) << st.wSecond;
    return oss.str();
}

bool ReservationManager::ParseDateTime(const std::wstring &text, SYSTEMTIME *st) {
    if (st == nullptr) {
        return false;
    }

    SYSTEMTIME tmp{};
    if (swscanf_s(
            text.c_str(),
            L"%hu/%hu/%hu %hu:%hu:%hu",
            &tmp.wYear, &tmp.wMonth, &tmp.wDay,
            &tmp.wHour, &tmp.wMinute, &tmp.wSecond) != 6) {
        return false;
    }

    *st = tmp;
    return true;
}

void ReservationManager::Load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_items.clear();

    const UINT declaredCount = static_cast<UINT>(ReadInt(kSectionSettings, kKeyCount, 0, m_iniPath));
    const UINT scanCount = (std::max)(declaredCount, static_cast<UINT>(kMaxReservations));

    for (UINT i = 0; i < scanCount && i < kMaxReservations; ++i) {
        wchar_t section[32]{};
        wsprintfW(section, L"Reservation%u", i);

        wchar_t startText[512]{};
        GetPrivateProfileStringW(section, L"StartTime", L"", startText, static_cast<DWORD>(std::size(startText)), m_iniPath.c_str());
        if (startText[0] == L'\0') {
            continue;
        }

        ReservationItem item{};
        item.NetworkID = static_cast<WORD>(ReadInt(section, L"NetworkID", 0, m_iniPath));
        item.TransportStreamID = static_cast<WORD>(ReadInt(section, L"TransportStreamID", 0, m_iniPath));
        item.ServiceID = static_cast<WORD>(ReadInt(section, L"ServiceID", 0, m_iniPath));
        item.EventID = static_cast<WORD>(ReadInt(section, L"EventID", 0, m_iniPath));

        item.OriginalNetworkID = static_cast<WORD>(ReadInt(section, L"OriginalNetworkID", item.NetworkID, m_iniPath));
        item.OriginalTransportStreamID = static_cast<WORD>(ReadInt(section, L"OriginalTransportStreamID", item.TransportStreamID, m_iniPath));
        item.OriginalServiceID = static_cast<WORD>(ReadInt(section, L"OriginalServiceID", item.ServiceID, m_iniPath));
        item.OriginalEventID = static_cast<WORD>(ReadInt(section, L"OriginalEventID", item.EventID, m_iniPath));

        item.Duration = static_cast<DWORD>(ReadInt(section, L"Duration", 0, m_iniPath));
        item.RescueState = ReadInt(section, L"RescueState", 0, m_iniPath);

        if (!ParseDateTime(startText, &item.StartTime)) {
            continue;
        }

        wchar_t textBuf[512]{};
        GetPrivateProfileStringW(section, L"EventName", L"", textBuf, static_cast<DWORD>(std::size(textBuf)), m_iniPath.c_str());
        item.EventName = textBuf;

        GetPrivateProfileStringW(section, L"LastDriverName", L"", textBuf, static_cast<DWORD>(std::size(textBuf)), m_iniPath.c_str());
        item.LastDriverName = textBuf;

        item.Triggered = false;
        item.LastEpgSyncTick = 0;
        item.LastRescueTick = 0;

        const bool exactDuplicate = std::any_of(m_items.begin(), m_items.end(), [&](const ReservationItem &existing) {
            return SameIdentity(existing, item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID) &&
                   ToFileTimeValue(existing.StartTime) == ToFileTimeValue(item.StartTime);
        });
        if (exactDuplicate) {
            continue;
        }

        const auto sameStartIt = std::find_if(m_items.begin(), m_items.end(), [&](const ReservationItem &existing) {
            return ToFileTimeValue(existing.StartTime) == ToFileTimeValue(item.StartTime);
        });
        if (sameStartIt != m_items.end()) {
            *sameStartIt = item;
            continue;
        }

        if (m_items.size() < kMaxReservations) {
            m_items.push_back(item);
        }
    }

    RemoveExpiredUnlocked();
    SaveUnlocked();
}

void ReservationManager::Save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    SaveUnlocked();
}

void ReservationManager::SaveUnlocked() const {
    const UINT oldCount = GetPrivateProfileIntW(kSectionSettings, kKeyCount, 0, m_iniPath.c_str());
    const UINT cleanupCount = (std::max)(oldCount, static_cast<UINT>(kMaxReservations));

    for (UINT i = 0; i < cleanupCount; ++i) {
        wchar_t section[32]{};
        wsprintfW(section, L"Reservation%u", i);
        WritePrivateProfileStringW(section, nullptr, nullptr, m_iniPath.c_str());
    }

    wchar_t countText[16]{};
    wsprintfW(countText, L"%u", static_cast<UINT>(m_items.size()));
    WritePrivateProfileStringW(kSectionSettings, kKeyCount, countText, m_iniPath.c_str());

    for (size_t i = 0; i < m_items.size(); ++i) {
        wchar_t section[32]{};
        wsprintfW(section, L"Reservation%u", static_cast<UINT>(i));

        const auto &item = m_items[i];
        wchar_t num[64]{};

        wsprintfW(num, L"%u", item.NetworkID);
        WritePrivateProfileStringW(section, L"NetworkID", num, m_iniPath.c_str());

        wsprintfW(num, L"%u", item.TransportStreamID);
        WritePrivateProfileStringW(section, L"TransportStreamID", num, m_iniPath.c_str());

        wsprintfW(num, L"%u", item.ServiceID);
        WritePrivateProfileStringW(section, L"ServiceID", num, m_iniPath.c_str());

        wsprintfW(num, L"%u", item.EventID);
        WritePrivateProfileStringW(section, L"EventID", num, m_iniPath.c_str());

        wsprintfW(num, L"%u", item.OriginalNetworkID);
        WritePrivateProfileStringW(section, L"OriginalNetworkID", num, m_iniPath.c_str());

        wsprintfW(num, L"%u", item.OriginalTransportStreamID);
        WritePrivateProfileStringW(section, L"OriginalTransportStreamID", num, m_iniPath.c_str());

        wsprintfW(num, L"%u", item.OriginalServiceID);
        WritePrivateProfileStringW(section, L"OriginalServiceID", num, m_iniPath.c_str());

        wsprintfW(num, L"%u", item.OriginalEventID);
        WritePrivateProfileStringW(section, L"OriginalEventID", num, m_iniPath.c_str());

        wsprintfW(num, L"%lu", static_cast<unsigned long>(item.Duration));
        WritePrivateProfileStringW(section, L"Duration", num, m_iniPath.c_str());

        wsprintfW(num, L"%d", item.RescueState);
        WritePrivateProfileStringW(section, L"RescueState", num, m_iniPath.c_str());

        WritePrivateProfileStringW(section, L"StartTime", FormatDateTime(item.StartTime).c_str(), m_iniPath.c_str());
        WritePrivateProfileStringW(section, L"EventName", item.EventName.c_str(), m_iniPath.c_str());
        WritePrivateProfileStringW(section, L"LastDriverName", item.LastDriverName.c_str(), m_iniPath.c_str());
    }
}

void ReservationManager::RemoveExpired() {
    std::lock_guard<std::mutex> lock(m_mutex);
    RemoveExpiredUnlocked();
}

void ReservationManager::RemoveExpiredUnlocked() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const ULONGLONG nowValue = ToFileTimeValue(now);

    const auto oldSize = m_items.size();
    m_items.erase(
        std::remove_if(m_items.begin(), m_items.end(), [nowValue](const ReservationItem &item) {
            const ULONGLONG start = ToFileTimeValue(item.StartTime);
            const ULONGLONG end = start + (static_cast<ULONGLONG>(item.Duration) * 10000000ULL);
            return end <= nowValue;
        }),
        m_items.end());

    if (m_items.size() != oldSize) {
        SaveUnlocked();
    }
}

bool ReservationManager::IsReserved(WORD nid, WORD tsid, WORD sid, WORD eid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::any_of(m_items.begin(), m_items.end(), [&](const ReservationItem &item) {
        return SameIdentity(item, nid, tsid, sid, eid);
    });
}

bool ReservationManager::ToggleReservation(const ReservationItem &item, std::wstring *message) {
    if (IsReserved(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID)) {
        return RemoveReservation(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, message);
    }
    return AddOrUpdateReservation(item, message);
}

bool ReservationManager::AddOrUpdateReservation(const ReservationItem &item, std::wstring *message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto &existing : m_items) {
        if (SameIdentity(existing, item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID)) {
            const bool sameStart = ToFileTimeValue(existing.StartTime) == ToFileTimeValue(item.StartTime);
            const bool sameDuration = existing.Duration == item.Duration;
            const bool sameName = existing.EventName == item.EventName;

            ReservationItem updated = item;
            updated.OriginalNetworkID = existing.OriginalNetworkID;
            updated.OriginalTransportStreamID = existing.OriginalTransportStreamID;
            updated.OriginalServiceID = existing.OriginalServiceID;
            updated.OriginalEventID = existing.OriginalEventID;
            updated.LastDriverName = existing.LastDriverName;
            updated.Triggered = existing.Triggered;

            existing = updated;
            SaveUnlocked();

            if (message != nullptr) {
                if (sameStart && sameDuration && sameName) {
                    *message = L"[視聴予約] 既に登録済みです: " + item.EventName;
                } else {
                    *message = L"[視聴予約] 予約内容を更新しました: " + item.EventName;
                }
            }
            return true;
        }
    }

    for (auto &existing : m_items) {
        const bool sameStart = ToFileTimeValue(existing.StartTime) == ToFileTimeValue(item.StartTime);
        const bool sameName = !existing.EventName.empty() && existing.EventName == item.EventName;

        if (sameStart && sameName) {
            ReservationItem updated = item;
            updated.OriginalNetworkID = existing.OriginalNetworkID != 0 ? existing.OriginalNetworkID : item.NetworkID;
            updated.OriginalTransportStreamID = existing.OriginalTransportStreamID != 0 ? existing.OriginalTransportStreamID : item.TransportStreamID;
            updated.OriginalServiceID = existing.OriginalServiceID != 0 ? existing.OriginalServiceID : item.ServiceID;
            updated.OriginalEventID = existing.OriginalEventID != 0 ? existing.OriginalEventID : item.EventID;
            updated.LastDriverName = existing.LastDriverName;
            updated.Triggered = existing.Triggered;

            existing = updated;
            SaveUnlocked();
            if (message != nullptr) {
                *message = L"[視聴予約] 重複予約を統合しました: " + item.EventName;
            }
            return true;
        }
    }

    for (auto &existing : m_items) {
        if (SameStart(existing, item)) {
            ReservationItem updated = item;
            updated.OriginalNetworkID = item.NetworkID;
            updated.OriginalTransportStreamID = item.TransportStreamID;
            updated.OriginalServiceID = item.ServiceID;
            updated.OriginalEventID = item.EventID;
            updated.LastDriverName = existing.LastDriverName;
            updated.Triggered = existing.Triggered;
            existing = updated;
            SaveUnlocked();
            if (message != nullptr) {
                *message = L"[視聴予約] 同時刻予約を上書きしました: " + item.EventName;
            }
            return true;
        }
    }

    ReservationItem inserted = item;
    inserted.OriginalNetworkID = item.NetworkID;
    inserted.OriginalTransportStreamID = item.TransportStreamID;
    inserted.OriginalServiceID = item.ServiceID;
    inserted.OriginalEventID = item.EventID;

    if (m_items.size() >= kMaxReservations) {
        auto it = (std::max_element)(m_items.begin(), m_items.end(), [](const ReservationItem &a, const ReservationItem &b) {
            return ToFileTimeValue(a.StartTime) < ToFileTimeValue(b.StartTime);
        });

        if (it != m_items.end()) {
            if (message != nullptr) {
                *message = L"[視聴予約] 満杯のため最も遠い予約を上書きしました: " + it->EventName + L" -> " + inserted.EventName;
            }
            *it = inserted;
            SaveUnlocked();
            return true;
        }
    }

    m_items.push_back(inserted);
    SaveUnlocked();
    if (message != nullptr) {
        *message = L"[視聴予約] 「" + inserted.EventName + L"」を予約しました（" + FormatDateTime(inserted.StartTime) + L"～）";
    }
    return true;
}

bool ReservationManager::RemoveReservation(WORD nid, WORD tsid, WORD sid, WORD eid, std::wstring *message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::wstring name;
    const size_t before = m_items.size();
    m_items.erase(
        std::remove_if(m_items.begin(), m_items.end(), [&](const ReservationItem &item) {
            if (SameIdentity(item, nid, tsid, sid, eid)) {
                name = item.EventName;
                return true;
            }
            return false;
        }),
        m_items.end());

    if (m_items.size() == before) {
        return false;
    }

    SaveUnlocked();
    if (message != nullptr) {
        *message = L"[視聴予約] 予約を解除しました: " + name;
    }
    return true;
}

std::vector<ReservationItem> ReservationManager::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_items;
}

bool ReservationManager::MarkTriggered(WORD nid, WORD tsid, WORD sid, WORD eid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &item : m_items) {
        if (SameIdentity(item, nid, tsid, sid, eid)) {
            item.Triggered = true;
            return true;
        }
    }
    return false;
}

bool ReservationManager::UpdateTimingByIds(
    WORD nid, WORD tsid, WORD sid, WORD eid,
    const SYSTEMTIME &startTime, DWORD duration,
    const std::wstring *eventName,
    std::wstring *message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto &item : m_items) {
        if (!SameIdentity(item, nid, tsid, sid, eid)) {
            continue;
        }

        const bool changed =
            ToFileTimeValue(item.StartTime) != ToFileTimeValue(startTime) ||
            item.Duration != duration ||
            (eventName != nullptr && !eventName->empty() && item.EventName != *eventName);

        item.StartTime = startTime;
        item.Duration = duration;
        if (eventName != nullptr && !eventName->empty()) {
            item.EventName = *eventName;
        }
        item.RescueState = 0;

        if (changed) {
            SaveUnlocked();
            if (message != nullptr) {
                *message = L"[視聴予約] EPG更新を反映しました: " + item.EventName;
            }
        }
        return changed;
    }

    return false;
}

bool ReservationManager::ReplaceIdentity(
    WORD oldNid, WORD oldTsid, WORD oldSid, WORD oldEid,
    const ReservationItem &replacement,
    std::wstring *message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto &item : m_items) {
        if (!SameIdentity(item, oldNid, oldTsid, oldSid, oldEid)) {
            continue;
        }

        const WORD originalNid = item.OriginalNetworkID;
        const WORD originalTsid = item.OriginalTransportStreamID;
        const WORD originalSid = item.OriginalServiceID;
        const WORD originalEid = item.OriginalEventID;
        const bool triggered = item.Triggered;
        const std::wstring oldName = item.EventName;

        item = replacement;
        item.OriginalNetworkID = originalNid;
        item.OriginalTransportStreamID = originalTsid;
        item.OriginalServiceID = originalSid;
        item.OriginalEventID = originalEid;
        item.Triggered = triggered;
        item.RescueState = 0;

        SaveUnlocked();
        if (message != nullptr) {
            *message = L"[視聴予約] サービス再探索で予約先を更新しました: " + oldName + L" -> " + item.EventName;
        }
        return true;
    }

    return false;
}
