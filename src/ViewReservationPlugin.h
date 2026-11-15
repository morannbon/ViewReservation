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
#include "AutomationBarLayout.h"
#include "PanelStationPresentation.h"
#include "PanelViewportGeometry.h"
#include "PanelWaveBarLayout.h"
#include "ViewReservationSettings.h"

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
    bool OnDriverChange() override;

private:
    static constexpr UINT kMenuCommandReserve = 0;
    static constexpr UINT kMenuCommandRecord  = 1;
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
    static constexpr int kPanelAutomationFontPercent = 80;
    static constexpr ULONGLONG kPanelFutureHorizonSec = 6ULL * 60ULL * 60ULL;
    static constexpr int kControlWaveGr = 1001;
    static constexpr int kControlWaveBs = 1002;
    static constexpr int kControlWaveCs = 1003;
    static constexpr int kControlWaveCatv = 1004;
    static constexpr int kControlHeaderBar = 1009;
    static constexpr int kControlPower = 1007;
    static constexpr int kControlRefresh = 1008;
    static constexpr int kControlViewport = 1005;
    static constexpr int kControlStatus = 1006;
    static constexpr int kControlAutomationBar = 1013;
    static constexpr UINT kPanelUiStateMessage = WM_APP + 41;

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
    enum class PanelWave { Terrestrial, Bs, Cs, Catv };

    mutable std::mutex m_automationMutex;
    std::mutex m_zappingOperationMutex;
    bool m_zappingActive = false;
    bool m_zappingHasSwitched = false;
    PanelWave m_zappingWave = PanelWave::Terrestrial;
    int m_zappingIntervalSeconds = ViewReservationSettings::DefaultZappingIntervalSeconds;
    ULONGLONG m_zappingNextTick = 0;
    ULONGLONG m_zappingLastChannelKey = 0;
    ULONGLONG m_zappingGeneration = 0;
    bool m_exitTimerActive = false;
    int m_exitTimerHours = ViewReservationSettings::DefaultExitTimerHours;
    ULONGLONG m_exitDeadlineTick = 0;
    bool m_exitWaitingForRecording = false;
    ULONGLONG m_exitTimerGeneration = 0;

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

    struct PanelViewingProjection {
        bool HasCurrentService = false;
        ULONGLONG ChannelKey = 0;
        PanelWave Wave = PanelWave::Terrestrial;
        ViewReservationUi::PanelStationEmphasis Emphasis = ViewReservationUi::PanelStationEmphasis::None;
    };

    HWND m_panelWindow = nullptr;
    HWND m_panelAutomationBar = nullptr;
    HWND m_panelHeader = nullptr;
    ViewReservationUi::PanelWaveBarLayout::Item m_panelHeaderPressed = ViewReservationUi::PanelWaveBarLayout::Item::None;
    ViewReservationUi::PanelWaveBarLayout::Item m_panelHeaderFocused = ViewReservationUi::PanelWaveBarLayout::Item::Terrestrial;
    HWND m_panelViewport = nullptr;
    HWND m_panelStatus = nullptr;
    HBRUSH m_panelBackBrush = nullptr;
    COLORREF m_panelBackBrushColor = CLR_INVALID;
    HFONT m_panelFont = nullptr;
    HFONT m_panelAutomationFont = nullptr;
    int m_panelViewportScrollY = 0;
    int m_panelSelectedRow = -1;
    int m_panelWheelRemainder = 0;
    PanelWave m_panelWave = PanelWave::Terrestrial;
    PanelWave m_panelRowsWave = PanelWave::Terrestrial;
    bool m_panelCatvAvailable = false;
    std::vector<PanelRow> m_panelRows;
    std::set<ULONGLONG> m_panelExpandedChannels;
    ULONGLONG m_panelMarqueeEpochTick = 0;
    int m_panelHoverRow = -1;
    bool m_panelMarqueeActive = false;
    bool m_panelMouseTracking = false;
    PanelViewingProjection m_panelViewingProjection{};

    static LRESULT CALLBACK EventCallback(TVTest::EventCode Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);

    bool RegisterPanelItem();
    bool LoadPanelEnabled() const;
    bool SavePanelEnabled(bool enabled) const;
    int LoadZappingIntervalSeconds() const;
    bool SaveZappingIntervalSeconds(int seconds) const;
    int LoadExitTimerHours() const;
    bool SaveExitTimerHours(int hours) const;
    bool ShowPanelSettingsDialog(HWND hwndOwner, bool *enabled, int *zappingIntervalSeconds);
    bool CreatePanelWindow(TVTest::PanelItemCreateEventInfo *pInfo);
    void DestroyPanelWindow();
    void ReleasePanelWindowResources(HWND destroyedWindow);
    HBRUSH EnsurePanelBackBrush(COLORREF color);
    void LayoutPanel();
    ViewReservationUi::AutomationBarLayout CalculateAutomationBarLayout(HDC dc) const;
    void UpdatePanelFont();
    void RefreshPanelRows(bool forceDriverCache);
    void RefreshPanelDisplay(bool forceDriverCache);
    bool BuildPanelPrograms(const DriverChannelEntry &channel, ReservationItem *current, std::vector<ReservationItem> *future) const;
    void SetPanelWave(PanelWave wave);
    static PanelWave ResolvePanelWave(const DriverChannelEntry &entry);
    PanelViewingProjection CapturePanelViewingProjection() const;
    void UpdatePanelViewingProjection(bool centerCurrent);
    void CenterPanelViewingChannel(ULONGLONG channelKey);
    ViewReservationUi::PanelViewportGeometry GetPanelViewportGeometry() const;
    bool GetPanelViewportRowRect(int index, RECT *rect) const;
    void InvalidatePanelViewportRow(int index) const;
    void SetPanelViewportScrollY(int scrollY);
    void TogglePanelChannelExpanded(size_t rowIndex);
    void ShowPanelReservationMenu(size_t rowIndex, POINT screenPoint);
    void ActivatePanelCurrentProgram(size_t rowIndex);
    void DrawPanelHeader(HDC dc, const RECT &client) const;
    void DrawPanelHeaderItem(HDC dc, const RECT &rect, ViewReservationUi::PanelWaveBarLayout::Item item) const;
    ViewReservationUi::PanelWaveBarLayout GetPanelHeaderLayout() const;
    static int PanelHeaderCommand(ViewReservationUi::PanelWaveBarLayout::Item item);
    void DrawAutomationBar(HDC dc) const;
    PanelRowLayout CalculatePanelRowLayout(const RECT &itemRect, HDC dc) const;
    void ResetPanelMarqueeForLayoutChange();
    void DrawPanelRow(HDC dc, const RECT &rowRect,
        const ViewReservationUi::PanelViewportGeometry &geometry, size_t rowIndex) const;
    void UpdateAutomationControls();
    void ShowExitHoursMenu();
    void StartZapping();
    void StopZapping();
    void StartExitTimer();
    void StopExitTimer();
    void RunAutomationTick();
    void RunZappingTick(ULONGLONG nowTick);
    void RunExitTimerTick(ULONGLONG nowTick);
    void RequestTvTestClose();
    void PostPanelUiStateRefresh(bool centerCurrent = false) const;
    std::vector<DriverChannelEntry> GetDisplayChannelsForWave(PanelWave wave, bool forceDriverCache);
    bool ApplyPanelReservation(const ReservationItem &item, bool record);
    bool RemovePanelReservation(const ReservationItem &item);
    static LRESULT CALLBACK PanelHeaderProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PanelViewportProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AutomationBarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
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
    bool IsCurrentDriverPreferred(const ReservationItem &item) const;
    bool TrySelectOnCurrentDriver(const ReservationItem &item);
    bool TrySelectByEnumeratingDrivers(ReservationItem &item);
    bool TryStartRecording(const ReservationItem &item, const wchar_t *actionLabel);
    bool IsRecordingActive() const;
    bool BeginPendingRecording(const ReservationItem &item, const wchar_t *actionLabel, const std::wstring &triggerKey);
    void ClearPendingRecording();
    bool HasPendingRecording() const;
    void TryCompletePendingRecording();

    static ULONGLONG GetTickMs();
    static ULONGLONG SecondsUntil(const SYSTEMTIME &st);
    static int ComputeNameSimilarityScore(const std::wstring &a, const std::wstring &b);
    static int ComputeCandidateScore(const ReservationItem &base, const ReservationItem &candidate);
    static std::wstring ToLowerSimple(std::wstring text);
    static bool IsSyncDue(const ReservationItem &item);
    static bool IsRescueDue(const ReservationItem &item);
    static bool MatchesProgramIdentity(const ReservationItem &item, WORD nid, WORD tsid, WORD sid, WORD eid);
    static const ReservationItem *FindMatchingReservation(
        const std::vector<ReservationItem> &items, WORD nid, WORD tsid, WORD sid, WORD eid);
    bool IsStartupEpgWaitActive() const;
    bool IsSameLocalDate(const SYSTEMTIME &a, const SYSTEMTIME &b) const;
    bool IsReservationActiveNow(const ReservationItem &item) const;
    bool IsReservationTriggerWindow(const ReservationItem &item) const;
    int SelectSingleReservationIndexToTrigger(const std::vector<ReservationItem> &items) const;
    std::wstring MakeTriggerKey(const ReservationItem &item) const;
    bool ClaimTriggerKey(const std::wstring &key);
    void ReleaseTriggerKey(const std::wstring &key);
};
