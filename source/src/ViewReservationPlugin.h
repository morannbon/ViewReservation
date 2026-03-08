#pragma once

#include <windows.h>
#include <atomic>
#include <unordered_set>
#include <thread>
#include <string>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "ReservationManager.h"
#include "DriverCache.h"
#include "TVTestPlugin.h"

class CViewReservationPlugin final
    : public TVTest::CTVTestPlugin
    , public TVTest::CTVTestEventHandler {
public:
    CViewReservationPlugin();
    ~CViewReservationPlugin() override;

    bool GetPluginInfo(TVTest::PluginInfo *pInfo) override;
    bool Initialize() override;
    bool Finalize() override;

    int OnProgramGuideProgramInitializeMenu(
        const TVTest::ProgramGuideProgramInfo *pProgramInfo,
        const TVTest::ProgramGuideProgramInitializeMenuInfo *pInfo) override;

    bool OnProgramGuideProgramMenuSelected(
        const TVTest::ProgramGuideProgramInfo *pProgramInfo,
        UINT Command) override;

private:
    static constexpr UINT kMenuCommandReserve = 0;
    static constexpr ULONGLONG kBaseCheckIntervalMs = 5000;
    static constexpr ULONGLONG kBeforeSwitchSec = 30;
    static constexpr ULONGLONG kIdleSleepMs = 5 * 60 * 1000ULL;
    static constexpr ULONGLONG kMaxSleepMs = 5 * 60 * 1000ULL;
    static constexpr ULONGLONG kMinSleepMs = 1000ULL;

    std::atomic<bool> m_stop{ false };
    std::thread m_worker;
    std::condition_variable m_cv;
    std::mutex m_cvMutex;

    std::wstring m_iniPath;
    ReservationManager *m_manager = nullptr;
    DriverCache m_driverCache;
    ULONGLONG m_startupTick = 0;
    bool m_epgWaitLogged = false;
    bool m_silentMode = true;
    std::unordered_set<std::wstring> m_sessionTriggeredKeys;

    static LRESULT CALLBACK EventCallback(TVTest::EventCode Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);

    void WorkerLoop();
    void Log(const std::wstring &text);
    std::wstring GetModuleDirectory() const;
    ReservationItem MakeReservationItem(const TVTest::ProgramGuideProgramInfo &program) const;
    std::wstring QueryEventName(const TVTest::ProgramGuideProgramInfo &program) const;

    void RunMaintenanceTick();
    ULONGLONG ComputeNextWakeDelayMs() const;
    void ReconcileByEpg();
    bool TryDirectEpgSync(ReservationItem &item, std::wstring *message);
    bool TryRescueByEventList(const ReservationItem &item, ReservationItem *replacement, std::wstring *message);
    bool TryRescueInService(WORD nid, WORD tsid, WORD sid, const ReservationItem &item, ReservationItem *replacement, int *scoreOut);
    bool TrySwitchReservation(ReservationItem &item);
    bool TrySelectOnCurrentDriver(const ReservationItem &item);
    bool TrySelectByEnumeratingDrivers(ReservationItem &item);

    static ULONGLONG GetTickMs();
    static ULONGLONG FileTimeOf(const SYSTEMTIME &st);
    static ULONGLONG SecondsUntil(const SYSTEMTIME &st);
    static int ComputeNameSimilarityScore(const std::wstring &a, const std::wstring &b);
    static int ComputeCandidateScore(const ReservationItem &base, const ReservationItem &candidate);
    static std::wstring ToLowerSimple(std::wstring text);
    static bool IsSyncDue(const ReservationItem &item);
    static bool IsRescueDue(const ReservationItem &item);
    bool IsStartupEpgWaitActive() const;
    bool IsSameLocalDate(const SYSTEMTIME &a, const SYSTEMTIME &b) const;
    bool IsReservationActiveNow(const ReservationItem &item) const;
    bool IsReservationTriggerWindow(const ReservationItem &item) const;
    int SelectSingleReservationIndexToTrigger(const std::vector<ReservationItem> &items) const;
    std::wstring MakeTriggerKey(const ReservationItem &item) const;
};
