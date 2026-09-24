#pragma once

#include <windows.h>
#include <atomic>
#include <unordered_set>
#include <thread>
#include <string>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <set>

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

    bool OnPanelItemNotify(TVTest::PanelItemEventInfo *pInfo) override;
    bool OnPluginSettings(HWND hwndOwner) override;
    bool OnChannelChange() override;
    bool OnServiceChange() override;

private:
    static constexpr UINT kMenuCommandReserve = 0;
    static constexpr UINT kMenuCommandRecord  = 1;
    static constexpr ULONGLONG kBaseCheckIntervalMs = 5000;
    static constexpr ULONGLONG kBeforeSwitchSec = 30;
    static constexpr ULONGLONG kIdleSleepMs = 5 * 60 * 1000ULL;
    static constexpr ULONGLONG kMaxSleepMs = 5 * 60 * 1000ULL;
    static constexpr ULONGLONG kMinSleepMs = 1000ULL;
    static constexpr ULONGLONG kRecordSwitchTimeoutMs = 15000ULL; // 録画要求のサービス到達確認タイムアウト(ms)

    static constexpr int kPanelItemId = 1;
    static constexpr UINT_PTR kPanelRefreshTimerId = 1;
    static constexpr UINT_PTR kPanelMarqueeTimerId = 2;
    static constexpr UINT_PTR kPanelMarqueeDelayTimerId = 3;
    static constexpr UINT kPanelRefreshIntervalMs = 60000;
    static constexpr UINT kPanelMarqueeIntervalMs = 16;
    static constexpr UINT kPanelMarqueeHoverDelayMs = 400;
    static constexpr int kPanelMarqueePixelsPerSecond = 54;
    static constexpr int kPanelCurrentRowHeight = 40;
    static constexpr int kPanelFutureRowHeight = 44;
    static constexpr int kPanelArrowVisualWidth = 24;
    static constexpr int kPanelReservationVisualWidth = 18;
    static constexpr int kPanelArrowHitWidth = 38;
    static constexpr int kPanelTimeColumnPadding = 6;
    static constexpr int kPanelTitleGap = 6;
    static constexpr ULONGLONG kPanelFutureHorizonSec = 6ULL * 60ULL * 60ULL;
    static constexpr int kControlWaveGr = 1001;
    static constexpr int kControlWaveBs = 1002;
    static constexpr int kControlWaveCs = 1003;
    static constexpr int kControlList = 1004;
    static constexpr int kControlStatus = 1005;

    std::atomic<bool> m_stop{ false };
    std::thread m_worker;
    std::condition_variable m_cv;
    std::mutex m_cvMutex;

    std::wstring m_iniPath;
    bool m_panelEnabled = false;
    bool m_panelRegistered = false;
    ReservationManager *m_manager = nullptr;
    DriverCache m_driverCache;
    ULONGLONG m_startupTick = 0;
    bool m_epgWaitLogged = false;
    mutable std::mutex m_sessionTriggeredMutex;
    std::unordered_set<std::wstring> m_sessionTriggeredKeys;

    struct PendingRecordRequest {
        bool Active = false;
        ULONGLONG StartedTick = 0;
        ReservationItem Item;
        std::wstring Label;
        std::wstring TriggerKey;
    };
    mutable std::mutex m_pendingRecordMutex;
    PendingRecordRequest m_pendingRecord;

    enum class PanelReservationKind { None, View, Record };

    struct PanelRow {
        DriverChannelEntry Channel;
        ReservationItem Program;
        ULONGLONG ChannelKey = 0;
        bool IsFuture = false;
        bool HasProgram = false;
        bool HasFuture = false;
        PanelReservationKind ReservationKind = PanelReservationKind::None;
        mutable bool NeedsMarquee = false;
        std::wstring DisplayText;
    };

    struct PanelRowLayout {
        RECT Content{};
        RECT Reservation{};
        RECT Arrow{};
        int TimeWidth = 0;
        int TitleLeft = 0;
    };

    HWND m_panelWindow = nullptr;
    HWND m_panelList = nullptr;
    HWND m_panelStatus = nullptr;
    HBRUSH m_panelBackBrush = nullptr;
    COLORREF m_panelBackBrushColor = CLR_INVALID;
    HFONT m_panelFont = nullptr;
    WNDPROC m_panelListOriginalProc = nullptr;
    TVTest::TuningSpaceType m_panelWave = TVTest::TUNINGSPACE_TERRESTRIAL;
    std::vector<PanelRow> m_panelRows;
    std::set<ULONGLONG> m_panelExpandedChannels;
    ULONGLONG m_panelMarqueeEpochTick = 0;
    int m_panelHoverRow = -1;
    bool m_panelMarqueeActive = false;
    bool m_panelMouseTracking = false;

    static LRESULT CALLBACK EventCallback(TVTest::EventCode Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);

    bool RegisterPanelItem();
    bool LoadPanelEnabled() const;
    bool SavePanelEnabled(bool enabled) const;
    bool ShowPanelSettingsDialog(HWND hwndOwner, bool *enabled);
    bool CreatePanelWindow(TVTest::PanelItemCreateEventInfo *pInfo);
    void DestroyPanelWindow();
    void LayoutPanel(int width, int height);
    void UpdatePanelFont();
    void RefreshPanelRows(bool forceDriverCache);
    bool BuildPanelPrograms(const DriverChannelEntry &channel, ReservationItem *current, std::vector<ReservationItem> *future) const;
    void SetPanelWave(TVTest::TuningSpaceType wave);
    void TogglePanelChannelExpanded(size_t rowIndex);
    void ShowPanelReservationMenu(size_t rowIndex, POINT screenPoint);
    void ActivatePanelCurrentProgram(size_t rowIndex);
    void DrawPanelWaveButton(const DRAWITEMSTRUCT *draw) const;
    PanelRowLayout CalculatePanelRowLayout(const RECT &itemRect, HDC dc) const;
    void ResetPanelMarqueeForLayoutChange();
    void DrawPanelRow(const DRAWITEMSTRUCT *draw) const;
    bool ApplyPanelReservation(const ReservationItem &item, bool record);
    bool RemovePanelReservation(const ReservationItem &item);
    static LRESULT CALLBACK PanelListProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PanelWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void WorkerLoop();
    void Log(const std::wstring &text);
    void ShowReservationError(const std::wstring &message) const;
    std::wstring GetModuleDirectory() const;
    ReservationItem MakeReservationItem(const TVTest::ProgramGuideProgramInfo &program) const;
    std::wstring QueryEventName(const TVTest::ProgramGuideProgramInfo &program) const;

    void RunMaintenanceTick();
    ULONGLONG ComputeNextWakeDelayMs() const;
    void ReconcileByEpg();
    bool TryDirectEpgSync(ReservationItem &item, std::wstring *message);
    bool TryRescueByEventList(const ReservationItem &item, ReservationItem *replacement);
    bool TryRescueInService(WORD nid, WORD tsid, WORD sid, const ReservationItem &item, ReservationItem *replacement, int *scoreOut);
    enum class ReservationTriggerResult { Failed, WaitingForRecording, Completed, Pending };
    ReservationTriggerResult TrySwitchReservation(ReservationItem &item, const std::wstring &triggerKey);
    bool TrySwitchToItem(ReservationItem &item, const wchar_t *actionLabel);
    bool TrySelectOnCurrentDriver(const ReservationItem &item);
    bool TrySelectByEnumeratingDrivers(ReservationItem &item);
    bool TryStartRecording(const ReservationItem &item, const wchar_t *actionLabel);
    bool IsRecordingActive() const;
    bool BeginPendingRecording(const ReservationItem &item, const wchar_t *actionLabel, const std::wstring &triggerKey);
    void ClearPendingRecording();
    bool HasPendingRecording() const;
    void TryCompletePendingRecording();

    static ULONGLONG GetTickMs();
    static ULONGLONG FileTimeOf(const SYSTEMTIME &st);
    static ULONGLONG SecondsUntil(const SYSTEMTIME &st);
    static int ComputeNameSimilarityScore(const std::wstring &a, const std::wstring &b);
    static int ComputeCandidateScore(const ReservationItem &base, const ReservationItem &candidate);
    static std::wstring ToLowerSimple(std::wstring text);
    static bool IsSyncDue(const ReservationItem &item);
    static bool IsRescueDue(const ReservationItem &item);
    static bool MatchesProgramIdentity(const ReservationItem &item, WORD nid, WORD tsid, WORD sid, WORD eid);
    bool IsStartupEpgWaitActive() const;
    bool IsSameLocalDate(const SYSTEMTIME &a, const SYSTEMTIME &b) const;
    bool IsReservationActiveNow(const ReservationItem &item) const;
    bool IsReservationTriggerWindow(const ReservationItem &item) const;
    int SelectSingleReservationIndexToTrigger(const std::vector<ReservationItem> &items) const;
    std::wstring MakeTriggerKey(const ReservationItem &item) const;
    bool ClaimTriggerKey(const std::wstring &key);
    void ReleaseTriggerKey(const std::wstring &key);
};
