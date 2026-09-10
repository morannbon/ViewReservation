#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>

struct ReservationItem {
    WORD NetworkID = 0;
    WORD TransportStreamID = 0;
    WORD ServiceID = 0;
    WORD EventID = 0;

    WORD OriginalNetworkID = 0;
    WORD OriginalTransportStreamID = 0;
    WORD OriginalServiceID = 0;
    WORD OriginalEventID = 0;

    SYSTEMTIME StartTime{};
    DWORD Duration = 0;
    std::wstring EventName;

    bool Triggered = false;
    bool RecordOnTrigger = false;   // true = 録画予約
    int RescueState = 0;
    ULONGLONG LastEpgSyncTick = 0;
    ULONGLONG LastRescueTick = 0;
    std::wstring LastDriverName;
};

class ReservationManager {
public:
    static constexpr size_t kMaxReservations = 10;

    explicit ReservationManager(std::wstring iniPath);

    void Load();
    void Save();
    void RemoveExpired();

    bool IsReserved(WORD nid, WORD tsid, WORD sid, WORD eid) const;
    bool AddOrUpdateReservation(const ReservationItem &item, std::wstring *message = nullptr);
    bool RemoveReservation(WORD nid, WORD tsid, WORD sid, WORD eid, std::wstring *message = nullptr);

    std::vector<ReservationItem> Snapshot() const;
    bool MarkTriggered(WORD nid, WORD tsid, WORD sid, WORD eid);
    bool UpdateTimingByIds(
        WORD nid, WORD tsid, WORD sid, WORD eid,
        const SYSTEMTIME &startTime, DWORD duration,
        const std::wstring *eventName,
        std::wstring *message = nullptr);
    bool ReplaceIdentity(
        WORD oldNid, WORD oldTsid, WORD oldSid, WORD oldEid,
        const ReservationItem &replacement,
        std::wstring *message = nullptr);
    void TouchEpgSync(WORD nid, WORD tsid, WORD sid, WORD eid, ULONGLONG tick);
    void TouchRescue(WORD nid, WORD tsid, WORD sid, WORD eid, ULONGLONG tick);

private:
    std::wstring m_iniPath;
    std::vector<ReservationItem> m_items;
    mutable std::mutex m_mutex;

    static bool SameIdentity(const ReservationItem &a, WORD nid, WORD tsid, WORD sid, WORD eid);
    static bool SameStart(const ReservationItem &a, const ReservationItem &b);
    static bool SameProgramSlot(const ReservationItem &a, const ReservationItem &b);
    static ULONGLONG ToFileTimeValue(const SYSTEMTIME &st);
    static std::wstring FormatDateTime(const SYSTEMTIME &st);
    static std::wstring FormatTimeRange(const ReservationItem &item);
    static bool TimeRangesOverlap(const ReservationItem &a, const ReservationItem &b);
    static bool ParseDateTime(const std::wstring &text, SYSTEMTIME *st);

    void SaveUnlocked() const;
    void RemoveExpiredUnlocked();
};
