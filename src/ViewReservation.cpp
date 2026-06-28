#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cwctype>

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "ViewReservationPlugin.h"

namespace {
// nullptr ガード付き wstring 変換（TVTest API の文字列ポインタ受け取り用）
std::wstring TrimText(const wchar_t *text) {
    if (text == nullptr) {
        return L"";
    }
    return text;
}
}

CViewReservationPlugin::CViewReservationPlugin() = default;

bool CViewReservationPlugin::MatchesProgramIdentity(const ReservationItem &item, WORD nid, WORD tsid, WORD sid, WORD eid) {
    const bool currentMatch =
        item.NetworkID == nid &&
        item.TransportStreamID == tsid &&
        item.ServiceID == sid &&
        item.EventID == eid;
    const bool originalMatch =
        item.OriginalNetworkID == nid &&
        item.OriginalTransportStreamID == tsid &&
        item.OriginalServiceID == sid &&
        item.OriginalEventID == eid;
    return currentMatch || originalMatch;
}

CViewReservationPlugin::~CViewReservationPlugin() {
    delete m_manager;
    m_manager = nullptr;
}

bool CViewReservationPlugin::GetPluginInfo(TVTest::PluginInfo *pInfo) {
    if (pInfo == nullptr) {
        return false;
    }

    pInfo->Type = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags = static_cast<TVTest::PluginFlag>(TVTest::PLUGIN_FLAG_ENABLEDEFAULT);
    pInfo->pszPluginName = L"ViewReservation";
    pInfo->pszCopyright = L"";
    pInfo->pszDescription = L"EPG視聴予約プラグイン";
    return true;
}

std::wstring CViewReservationPlugin::GetModuleDirectory() const {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_hinstDLL, path, MAX_PATH);

    std::wstring result(path);
    const auto pos = result.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        result.resize(pos + 1);
    }
    return result;
}

bool CViewReservationPlugin::Initialize() {
    m_iniPath = GetModuleDirectory() + L"ViewReservation.ini";
    m_manager = new ReservationManager(m_iniPath);
    m_manager->Load();

    if (!m_pApp->SetEventCallback(EventCallback, this)) {
        return false;
    }

    m_pApp->EnableProgramGuideEvent(static_cast<TVTest::ProgramGuideEventFlag>(
        TVTest::PROGRAMGUIDE_EVENT_PROGRAM));

    m_driverCache.RebuildIfNeeded(m_pApp, true);

    m_startupTick = GetTickMs();
    m_epgWaitLogged = false;
    m_sessionTriggeredKeys.clear();

    m_stop = false;
    m_worker = std::thread(&CViewReservationPlugin::WorkerLoop, this);

    return true;
}

bool CViewReservationPlugin::Finalize() {
    m_stop = true;
    m_cv.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    if (m_manager != nullptr) {
        m_manager->Save();
    }

    return true;
}

LRESULT CALLBACK CViewReservationPlugin::EventCallback(TVTest::EventCode Event, LPARAM lParam1, LPARAM lParam2, void *pClientData) {
    auto *self = static_cast<CViewReservationPlugin*>(pClientData);
    return self != nullptr ? self->HandleEvent(Event, lParam1, lParam2, pClientData) : 0;
}

void CViewReservationPlugin::Log(const std::wstring &text) {
    if (m_pApp != nullptr) {
        m_pApp->AddLog(text.c_str());
    }
}

std::wstring CViewReservationPlugin::QueryEventName(const TVTest::ProgramGuideProgramInfo &program) const {
    TVTest::EpgEventQueryInfo query{};
    query.NetworkID = program.NetworkID;
    query.TransportStreamID = program.TransportStreamID;
    query.ServiceID = program.ServiceID;
    query.Type = TVTest::EPG_EVENT_QUERY_EVENTID;
    query.Flags = TVTest::EPG_EVENT_QUERY_FLAG_NONE;
    query.EventID = program.EventID;

    TVTest::EpgEventInfo *eventInfo = m_pApp->GetEpgEventInfo(&query);
    if (eventInfo == nullptr) {
        return L"";
    }

    const std::wstring name = TrimText(eventInfo->pszEventName);
    m_pApp->FreeEpgEventInfo(eventInfo);
    return name;
}

ReservationItem CViewReservationPlugin::MakeReservationItem(const TVTest::ProgramGuideProgramInfo &program) const {
    ReservationItem item{};
    item.NetworkID = program.NetworkID;
    item.TransportStreamID = program.TransportStreamID;
    item.ServiceID = program.ServiceID;
    item.EventID = program.EventID;
    item.OriginalNetworkID = program.NetworkID;
    item.OriginalTransportStreamID = program.TransportStreamID;
    item.OriginalServiceID = program.ServiceID;
    item.OriginalEventID = program.EventID;
    item.StartTime = program.StartTime;
    item.Duration = program.Duration;
    item.EventName = QueryEventName(program);

    if (item.EventName.empty()) {
        item.EventName = L"(番組名未取得)";
    }

    return item;
}

int CViewReservationPlugin::OnProgramGuideProgramInitializeMenu(
    const TVTest::ProgramGuideProgramInfo *pProgramInfo,
    const TVTest::ProgramGuideProgramInitializeMenuInfo *pInfo) {
    if (pProgramInfo == nullptr || pInfo == nullptr || m_manager == nullptr) {
        return 0;
    }

    // 予約状態を取得（EPG救済でIDが変わっている場合はOriginalIDでも照合）
    bool isReserved = false;
    bool isRecord = false;
    {
        const auto list = m_manager->Snapshot();
        for (const auto &item : list) {
            if (MatchesProgramIdentity(
                    item,
                    pProgramInfo->NetworkID,
                    pProgramInfo->TransportStreamID,
                    pProgramInfo->ServiceID,
                    pProgramInfo->EventID)) {
                isReserved = true;
                isRecord = item.RecordOnTrigger;
                break;
            }
        }
    }

    int count = 0;

    if (!isReserved) {
        // 未予約：両方表示
        AppendMenuW(pInfo->hmenu, MF_STRING,
            pInfo->Command + kMenuCommandReserve, L"視聴予約");
        AppendMenuW(pInfo->hmenu, MF_STRING,
            pInfo->Command + kMenuCommandRecord, L"録画予約");
        count = 2;
    } else if (!isRecord) {
        // 視聴予約済み：Command+0で解除（kMenuCommandReserve=0）
        AppendMenuW(pInfo->hmenu, MF_STRING | MF_CHECKED,
            pInfo->Command + kMenuCommandReserve, L"視聴予約解除");
        count = 1;
    } else {
        // 録画予約済み：Command+0で解除（kMenuCommandRecordの代わりにCommand+0を使う）
        // TVTestはcount分だけCommand+0から順に割り当てるため、
        // 必ずCommand+0から追加しないとMenuSelectedが呼ばれない
        AppendMenuW(pInfo->hmenu, MF_STRING | MF_CHECKED,
            pInfo->Command + kMenuCommandReserve, L"録画予約解除");
        count = 1;
    }

    return count;
}

bool CViewReservationPlugin::OnProgramGuideProgramMenuSelected(
    const TVTest::ProgramGuideProgramInfo *pProgramInfo,
    UINT Command) {
    if (pProgramInfo == nullptr || m_manager == nullptr) {
        return false;
    }

    if (Command != kMenuCommandReserve && Command != kMenuCommandRecord) {
        return false;
    }

    // Commandは追加した項目の0始まりインデックス
    // 未予約時：0=視聴予約、1=録画予約
    // 予約済み時：0=解除（1項目のみ）
    // isRecord/isReservedはメニュー表示時と同じ判定で種別を確認する
    bool isReservedCmd = false;
    bool isRecordCmd = false;
    {
        const auto list = m_manager->Snapshot();
        for (const auto &existing : list) {
            if (MatchesProgramIdentity(
                    existing,
                    pProgramInfo->NetworkID,
                    pProgramInfo->TransportStreamID,
                    pProgramInfo->ServiceID,
                    pProgramInfo->EventID)) {
                isReservedCmd = true;
                isRecordCmd = existing.RecordOnTrigger;
                break;
            }
        }
    }

    ReservationItem item = MakeReservationItem(*pProgramInfo);

    if (isReservedCmd) {
        // 予約済み→解除（Command=0の1項目のみ、種別はDBから取得）
        item.RecordOnTrigger = isRecordCmd;
    } else {
        // 未予約→登録（Command=0:視聴予約、Command=1:録画予約）
        if (Command == kMenuCommandRecord) {
            item.RecordOnTrigger = true;
        }
    }

    std::wstring message;

    // 同じ種別の予約が既にあれば解除、なければ登録
    // EventIDはEPG救済で変わる場合があるためOriginalIDも照合し、
    // 削除時はDB上の実際のIDを使う
    bool foundInDb = false;
    {
        const auto list = m_manager->Snapshot();
        for (const auto &existing : list) {
            if (existing.RecordOnTrigger != item.RecordOnTrigger) continue;
            if (MatchesProgramIdentity(
                    existing,
                    item.NetworkID,
                    item.TransportStreamID,
                    item.ServiceID,
                    item.EventID)) {
                // DB上の実際のIDをitemに反映して削除に使う
                item.NetworkID = existing.NetworkID;
                item.TransportStreamID = existing.TransportStreamID;
                item.ServiceID = existing.ServiceID;
                item.EventID = existing.EventID;
                foundInDb = true;
                break;
            }
        }
    }

    if (foundInDb) {
        m_manager->RemoveReservation(
            item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, &message);
    } else {
        m_manager->AddOrUpdateReservation(item, &message);
    }

    if (!message.empty()) {
        Log(message);
    }

    if (m_manager->IsReserved(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID)) {
        const ULONGLONG secUntil = SecondsUntil(item.StartTime);
        if (secUntil == 0) {
            const std::wstring triggerKey = MakeTriggerKey(item);
            if (m_sessionTriggeredKeys.find(triggerKey) == m_sessionTriggeredKeys.end()) {
                if (TrySwitchReservation(item)) {
                    m_sessionTriggeredKeys.insert(triggerKey);
                    m_manager->MarkTriggered(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID);
                }
            }
        }
    }

    m_cv.notify_all();
    return true;
}

void CViewReservationPlugin::WorkerLoop() {
    std::unique_lock<std::mutex> lock(m_cvMutex);

    while (!m_stop.load()) {
        lock.unlock();
        RunMaintenanceTick();
        const ULONGLONG waitMs = ComputeNextWakeDelayMs();
        lock.lock();

        m_cv.wait_for(lock, std::chrono::milliseconds(waitMs), [&]() {
            return m_stop.load();
        });
    }
}

ULONGLONG CViewReservationPlugin::ComputeNextWakeDelayMs() const {
    // 録画開始待ちがある場合は残り待機時間を優先
    if (m_pendingRecord) {
        const ULONGLONG elapsed = GetTickMs() - m_pendingRecordTick;
        if (elapsed >= kRecordDelayMs) return kMinSleepMs;
        return kRecordDelayMs - elapsed;
    }

    if (m_manager == nullptr) {
        return kBaseCheckIntervalMs;
    }

    const auto list = m_manager->Snapshot();
    if (list.empty()) {
        return kIdleSleepMs;
    }

    const ULONGLONG nowTick = GetTickMs();
    ULONGLONG best = kMaxSleepMs;

    for (const auto &item : list) {
        const ULONGLONG secUntil = SecondsUntil(item.StartTime);

        if (!item.Triggered) {
            if (secUntil <= kBeforeSwitchSec) {
                return kMinSleepMs;
            }

            const ULONGLONG untilSwitchMs = (secUntil - kBeforeSwitchSec) * 1000ULL;
            if (untilSwitchMs < best) {
                best = untilSwitchMs;
            }
        }

        ULONGLONG syncIntervalMs = 5000ULL;
        if (secUntil > 24 * 60 * 60) {
            syncIntervalMs = 30 * 60 * 1000ULL;
        } else if (secUntil > 6 * 60 * 60) {
            syncIntervalMs = 10 * 60 * 1000ULL;
        } else if (secUntil > 60 * 60) {
            syncIntervalMs = 60 * 1000ULL;
        } else if (secUntil > 10 * 60) {
            syncIntervalMs = 15 * 1000ULL;
        }

        const ULONGLONG nextSyncMs =
            item.LastEpgSyncTick == 0 ? 0 :
            (item.LastEpgSyncTick + syncIntervalMs <= nowTick ? 0 : (item.LastEpgSyncTick + syncIntervalMs - nowTick));
        if (nextSyncMs < best) {
            best = nextSyncMs;
        }
    }

    if (best < kMinSleepMs) {
        best = kMinSleepMs;
    }
    if (best > kMaxSleepMs) {
        best = kMaxSleepMs;
    }
    return best;
}

bool CViewReservationPlugin::IsStartupEpgWaitActive() const {
    if (m_startupTick == 0) {
        return false;
    }
    const ULONGLONG elapsed = GetTickMs() - m_startupTick;
    return elapsed < 180ULL * 1000ULL;
}

bool CViewReservationPlugin::IsSameLocalDate(const SYSTEMTIME &a, const SYSTEMTIME &b) const {
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}


bool CViewReservationPlugin::IsReservationActiveNow(const ReservationItem &item) const {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const ULONGLONG nowFt = FileTimeOf(now);
    const ULONGLONG startFt = FileTimeOf(item.StartTime);
    const ULONGLONG endFt = startFt + static_cast<ULONGLONG>(item.Duration) * 10000000ULL;
    return nowFt >= startFt && nowFt < endFt;
}

bool CViewReservationPlugin::IsReservationTriggerWindow(const ReservationItem &item) const {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const ULONGLONG nowFt = FileTimeOf(now);
    const ULONGLONG startFt = FileTimeOf(item.StartTime);
    const ULONGLONG triggerFt = startFt - kBeforeSwitchSec * 10000000ULL;
    const ULONGLONG endFt = startFt + static_cast<ULONGLONG>(item.Duration) * 10000000ULL;
    return nowFt >= triggerFt && nowFt < endFt;
}

int CViewReservationPlugin::SelectSingleReservationIndexToTrigger(const std::vector<ReservationItem> &items) const {
    int bestActive = -1;
    int bestUpcoming = -1;

    for (size_t i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        if (!IsReservationTriggerWindow(item))
            continue;

        if (IsReservationActiveNow(item)) {
            if (bestActive < 0) {
                bestActive = static_cast<int>(i);
            } else {
                const ULONGLONG currentStart = FileTimeOf(item.StartTime);
                const ULONGLONG bestStart = FileTimeOf(items[bestActive].StartTime);
                if (currentStart > bestStart || (currentStart == bestStart && static_cast<int>(i) > bestActive)) {
                    bestActive = static_cast<int>(i);
                }
            }
        } else {
            if (bestUpcoming < 0) {
                bestUpcoming = static_cast<int>(i);
            } else {
                const ULONGLONG currentStart = FileTimeOf(item.StartTime);
                const ULONGLONG bestStart = FileTimeOf(items[bestUpcoming].StartTime);
                if (currentStart < bestStart || (currentStart == bestStart && static_cast<int>(i) > bestUpcoming)) {
                    bestUpcoming = static_cast<int>(i);
                }
            }
        }
    }

    // Upcoming（次の番組）をActiveNow（現在放送中）より優先する
    // 連続録画予約の場合、番組Bの30秒前になった時点で番組Aの録画を切り上げ番組Bへ移行する
    return bestUpcoming >= 0 ? bestUpcoming : bestActive;
}


std::wstring CViewReservationPlugin::MakeTriggerKey(const ReservationItem &item) const {
    return std::to_wstring(item.OriginalNetworkID ? item.OriginalNetworkID : item.NetworkID) + L":" +
           std::to_wstring(item.OriginalTransportStreamID ? item.OriginalTransportStreamID : item.TransportStreamID) + L":" +
           std::to_wstring(item.OriginalServiceID ? item.OriginalServiceID : item.ServiceID) + L":" +
           std::to_wstring(item.OriginalEventID ? item.OriginalEventID : item.EventID) + L":" +
           std::to_wstring(item.StartTime.wYear) + L"/" +
           std::to_wstring(item.StartTime.wMonth) + L"/" +
           std::to_wstring(item.StartTime.wDay) + L" " +
           std::to_wstring(item.StartTime.wHour) + L":" +
           std::to_wstring(item.StartTime.wMinute);
}

void CViewReservationPlugin::RunMaintenanceTick() {
    if (m_manager == nullptr || m_pApp == nullptr) {
        return;
    }

    try {
        // チャンネル切替後の録画開始待ち
        if (m_pendingRecord) {
            if (GetTickMs() - m_pendingRecordTick >= kRecordDelayMs) {
                m_pendingRecord = false;
                TryStartRecording(m_pendingRecordItem);
            }
        }

        m_manager->RemoveExpired();

        if (IsStartupEpgWaitActive()) {
            if (!m_epgWaitLogged) {
                Log(L"[視聴予約] EPGロード待機中");
                m_epgWaitLogged = true;
            }
        } else {
            if (m_epgWaitLogged) {
                Log(L"[視聴予約] EPGロード待機を終了し予約検証を開始します");
                m_epgWaitLogged = false;
            }
            ReconcileByEpg();
        }

        const auto list = m_manager->Snapshot();
        const int index = SelectSingleReservationIndexToTrigger(list);
        if (index < 0)
            return;

        ReservationItem item = list[static_cast<size_t>(index)];
        if (item.TransportStreamID == 0 && item.ServiceID == 0)
            return;

        const std::wstring triggerKey = MakeTriggerKey(item);
        if (m_sessionTriggeredKeys.find(triggerKey) != m_sessionTriggeredKeys.end())
            return;

        // INIに保存されたTriggeredフラグも確認（再起動後の再実行防止）
        if (item.Triggered)
            return;

        if (TrySwitchReservation(item)) {
            m_sessionTriggeredKeys.insert(triggerKey);
            m_manager->MarkTriggered(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID);
        }
    } catch (...) {
        Log(L"[視聴予約] 監視中に例外が発生しました");
    }
}

ULONGLONG CViewReservationPlugin::GetTickMs() {
    return GetTickCount64();
}

ULONGLONG CViewReservationPlugin::FileTimeOf(const SYSTEMTIME &st) {
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER ui{};
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return ui.QuadPart;
}

ULONGLONG CViewReservationPlugin::SecondsUntil(const SYSTEMTIME &st) {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    const ULONGLONG a = FileTimeOf(now);
    const ULONGLONG b = FileTimeOf(st);

    if (b <= a) {
        return 0;
    }

    return (b - a) / 10000000ULL;
}

std::wstring CViewReservationPlugin::ToLowerSimple(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

int CViewReservationPlugin::ComputeNameSimilarityScore(const std::wstring &a, const std::wstring &b) {
    const std::wstring la = ToLowerSimple(a);
    const std::wstring lb = ToLowerSimple(b);

    if (la.empty() || lb.empty()) {
        return 0;
    }
    if (la == lb) {
        return 100;
    }
    if (la.find(lb) != std::wstring::npos || lb.find(la) != std::wstring::npos) {
        return 80;
    }

    int common = 0;
    for (wchar_t ch : la) {
        if (lb.find(ch) != std::wstring::npos) {
            ++common;
        }
    }

    const int maxLen = static_cast<int>((std::max)(la.size(), lb.size()));
    if (maxLen <= 0) {
        return 0;
    }

    return (common * 60) / maxLen;
}

int CViewReservationPlugin::ComputeCandidateScore(const ReservationItem &base, const ReservationItem &candidate) {
    int score = ComputeNameSimilarityScore(base.EventName, candidate.EventName);

    const ULONGLONG baseTime = FileTimeOf(base.StartTime);
    const ULONGLONG candTime = FileTimeOf(candidate.StartTime);
    const ULONGLONG diffSec = (baseTime > candTime ? (baseTime - candTime) : (candTime - baseTime)) / 10000000ULL;

    if (diffSec <= 60) {
        score += 60;
    } else if (diffSec <= 5 * 60) {
        score += 40;
    } else if (diffSec <= 30 * 60) {
        score += 20;
    } else if (diffSec <= 2 * 60 * 60) {
        score += 10;
    }

    const DWORD baseDur = base.Duration;
    const DWORD candDur = candidate.Duration;
    const DWORD durDiff = baseDur > candDur ? (baseDur - candDur) : (candDur - baseDur);

    if (durDiff == 0) {
        score += 30;
    } else if (durDiff <= 60) {
        score += 20;
    } else if (durDiff <= 10 * 60) {
        score += 10;
    }

    if (base.NetworkID == candidate.NetworkID) {
        score += 15;
    }
    if (base.TransportStreamID == candidate.TransportStreamID) {
        score += 10;
    }
    if (base.ServiceID == candidate.ServiceID) {
        score += 10;
    }

    return score;
}

bool CViewReservationPlugin::IsSyncDue(const ReservationItem &item) {
    const ULONGLONG now = GetTickMs();
    if (item.LastEpgSyncTick == 0) {
        return true;
    }

    const ULONGLONG secUntil = SecondsUntil(item.StartTime);
    ULONGLONG intervalMs = 5000ULL;

    if (secUntil > 24 * 60 * 60) {
        intervalMs = 30 * 60 * 1000ULL;
    } else if (secUntil > 6 * 60 * 60) {
        intervalMs = 10 * 60 * 1000ULL;
    } else if (secUntil > 60 * 60) {
        intervalMs = 60 * 1000ULL;
    } else if (secUntil > 10 * 60) {
        intervalMs = 15 * 1000ULL;
    } else {
        intervalMs = 5000ULL;
    }

    return (now - item.LastEpgSyncTick) >= intervalMs;
}

bool CViewReservationPlugin::IsRescueDue(const ReservationItem &item) {
    const ULONGLONG now = GetTickMs();
    if (item.LastRescueTick == 0) {
        return true;
    }

    ULONGLONG intervalMs = 30 * 1000ULL;
    const ULONGLONG secUntil = SecondsUntil(item.StartTime);

    if (secUntil > 6 * 60 * 60) {
        intervalMs = 10 * 60 * 1000ULL;
    } else if (secUntil > 60 * 60) {
        intervalMs = 5 * 60 * 1000ULL;
    } else if (secUntil > 10 * 60) {
        intervalMs = 60 * 1000ULL;
    }

    return (now - item.LastRescueTick) >= intervalMs;
}

bool CViewReservationPlugin::TryDirectEpgSync(ReservationItem &item, std::wstring *message) {
    TVTest::EpgEventQueryInfo query{};
    query.NetworkID = item.NetworkID;
    query.TransportStreamID = item.TransportStreamID;
    query.ServiceID = item.ServiceID;
    query.Type = TVTest::EPG_EVENT_QUERY_EVENTID;
    query.Flags = TVTest::EPG_EVENT_QUERY_FLAG_NONE;
    query.EventID = item.EventID;

    TVTest::EpgEventInfo *eventInfo = m_pApp->GetEpgEventInfo(&query);
    item.LastEpgSyncTick = GetTickMs();
    m_manager->TouchEpgSync(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, item.LastEpgSyncTick);

    if (eventInfo == nullptr) {
        return false;
    }

    const std::wstring name = TrimText(eventInfo->pszEventName);
    const bool changed = m_manager->UpdateTimingByIds(
        item.NetworkID,
        item.TransportStreamID,
        item.ServiceID,
        item.EventID,
        eventInfo->StartTime,
        eventInfo->Duration,
        &name,
        message);

    item.StartTime = eventInfo->StartTime;
    item.Duration = eventInfo->Duration;
    if (!name.empty()) {
        item.EventName = name;
    }
    item.RescueState = 0;

    m_pApp->FreeEpgEventInfo(eventInfo);
    return changed;
}

bool CViewReservationPlugin::TryRescueInService(WORD nid, WORD tsid, WORD sid, const ReservationItem &item, ReservationItem *replacement, int *scoreOut) {
    TVTest::EpgEventList list{};
    list.NetworkID = nid;
    list.TransportStreamID = tsid;
    list.ServiceID = sid;

    if (!m_pApp->GetEpgEventList(&list)) {
        return false;
    }

    bool found = false;
    ReservationItem best{};
    int bestScore = -1;

    for (WORD i = 0; i < list.NumEvents; ++i) {
        const TVTest::EpgEventInfo *ev = list.EventList[i];
        if (ev == nullptr) {
            continue;
        }

        ReservationItem candidate{};
        candidate.NetworkID = nid;
        candidate.TransportStreamID = tsid;
        candidate.ServiceID = sid;
        candidate.EventID = ev->EventID;
        candidate.OriginalNetworkID = item.OriginalNetworkID;
        candidate.OriginalTransportStreamID = item.OriginalTransportStreamID;
        candidate.OriginalServiceID = item.OriginalServiceID;
        candidate.OriginalEventID = item.OriginalEventID;
        candidate.StartTime = ev->StartTime;
        candidate.Duration = ev->Duration;
        candidate.EventName = TrimText(ev->pszEventName);
        candidate.LastDriverName = item.LastDriverName;

        const int score = ComputeCandidateScore(item, candidate);
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
            found = true;
        }
    }

    m_pApp->FreeEpgEventList(&list);

    if (!found || bestScore < 120) {
        return false;
    }

    if (replacement != nullptr) {
        *replacement = best;
    }
    if (scoreOut != nullptr) {
        *scoreOut = bestScore;
    }
    return true;
}

bool CViewReservationPlugin::TryRescueByEventList(const ReservationItem &item, ReservationItem *replacement) {
    ReservationItem best{};
    int bestScore = -1;

    int score = -1;
    if (TryRescueInService(item.NetworkID, item.TransportStreamID, item.ServiceID, item, &best, &score)) {
        bestScore = score;
    }

    m_driverCache.RebuildIfNeeded(m_pApp, false);

    std::vector<DriverChannelEntry> candidates = m_driverCache.FindByNetwork(item.NetworkID);
    if (candidates.empty()) {
        candidates = m_driverCache.Snapshot();
    }

    for (const auto &entry : candidates) {
        if (entry.NetworkID == item.NetworkID &&
            entry.TransportStreamID == item.TransportStreamID &&
            entry.ServiceID == item.ServiceID) {
            continue;
        }

        ReservationItem candidate{};
        int candidateScore = -1;
        if (TryRescueInService(entry.NetworkID, entry.TransportStreamID, entry.ServiceID, item, &candidate, &candidateScore)) {
            candidate.LastDriverName = entry.DriverName;
            if (candidateScore > bestScore) {
                best = candidate;
                bestScore = candidateScore;
            }
        }
    }

    if (bestScore < 120) {
        return false;
    }

    if (replacement != nullptr) {
        *replacement = best;
    }
    return true;
}

void CViewReservationPlugin::ReconcileByEpg() {
    if (m_manager == nullptr) {
        return;
    }

    auto list = m_manager->Snapshot();
    for (auto &item : list) {
        if (!IsSyncDue(item)) {
            continue;
        }

        std::wstring updateMessage;
        if (TryDirectEpgSync(item, &updateMessage)) {
            if (!updateMessage.empty()) {
                Log(updateMessage);
            }
            continue;
        }

        if (!IsRescueDue(item)) {
            continue;
        }

        item.LastRescueTick = GetTickMs();
        m_manager->TouchRescue(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, item.LastRescueTick);

        ReservationItem replacement{};
        if (TryRescueByEventList(item, &replacement)) {
            std::wstring replaceMessage;
            if (m_manager->ReplaceIdentity(
                    item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID,
                    replacement, &replaceMessage)) {
                Log(replaceMessage);
            }
            continue;
        }

        const ULONGLONG secUntil = SecondsUntil(item.StartTime);
        SYSTEMTIME now{};
        GetLocalTime(&now);
        const bool isToday = IsSameLocalDate(now, item.StartTime);
        const bool protectNear = secUntil <= 30 * 60;

        if (IsStartupEpgWaitActive() || isToday || protectNear) {
            Log(L"[視聴予約] EPG再探索待ち: " + item.EventName);
            continue;
        }

        if (secUntil <= 10 * 60) {
            if (m_manager->RemoveReservation(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID)) {
                Log(L"[視聴予約] EPG上で見つからず、救済にも失敗したため解除: " + item.EventName);
            }
        } else {
            Log(L"[視聴予約] EPG再探索待ち: " + item.EventName);
        }
    }
}

bool CViewReservationPlugin::TrySelectOnCurrentDriver(const ReservationItem &item) {
    TVTest::ChannelSelectInfo info{};
    info.Size = sizeof(info);
    info.Flags = static_cast<TVTest::ChannelSelectFlag>(
        TVTest::CHANNEL_SELECT_FLAG_STRICTSERVICE |
        TVTest::CHANNEL_SELECT_FLAG_ALLOWDISABLED);
    info.pszTuner = nullptr;
    info.Space = -1;
    info.Channel = -1;
    info.NetworkID = item.NetworkID;
    info.TransportStreamID = item.TransportStreamID;
    info.ServiceID = item.ServiceID;
    return m_pApp->SelectChannel(&info);
}

bool CViewReservationPlugin::TrySelectByEnumeratingDrivers(ReservationItem &item) {
    m_driverCache.RebuildIfNeeded(m_pApp, false);

    DriverChannelEntry found{};
    if (!m_driverCache.FindExact(item.NetworkID, item.TransportStreamID, item.ServiceID, &found)) {
        m_driverCache.RebuildIfNeeded(m_pApp, true);
        if (!m_driverCache.FindExact(item.NetworkID, item.TransportStreamID, item.ServiceID, &found)) {
            return false;
        }
    }

    TVTest::ChannelSelectInfo info{};
    info.Size = sizeof(info);
    info.Flags = static_cast<TVTest::ChannelSelectFlag>(
        TVTest::CHANNEL_SELECT_FLAG_STRICTSERVICE |
        TVTest::CHANNEL_SELECT_FLAG_ALLOWDISABLED);
    info.pszTuner = found.DriverName.c_str();
    info.Space = found.Space;
    info.Channel = found.Channel;
    info.NetworkID = item.NetworkID;
    info.TransportStreamID = item.TransportStreamID;
    info.ServiceID = item.ServiceID;

    if (!m_pApp->SelectChannel(&info)) {
        m_driverCache.Invalidate();
        return false;
    }

    item.LastDriverName = found.DriverName;
    return true;
}

bool CViewReservationPlugin::TrySwitchReservation(ReservationItem &item) {
    // 現在のサービスIDを取得して同じ局かどうか確認
    TVTest::ServiceInfo serviceInfo{};
    serviceInfo.Size = sizeof(serviceInfo);
    const bool onSameService =
        m_pApp->GetServiceInfo(0, &serviceInfo) &&
        serviceInfo.ServiceID == item.ServiceID;

    if (onSameService) {
        // 同じ局：チャンネル切替不要、録画のみ処理
        Log(L"[視聴予約] 同一サービスのためチャンネル切替をスキップ: " + item.EventName);
        if (item.RecordOnTrigger) {
            StopRecordIfRecording(item.EventName);
            m_pendingRecord = true;
            m_pendingRecordTick = GetTickMs();
            m_pendingRecordItem = item;
        }
        return true;
    }

    if (TrySelectOnCurrentDriver(item)) {
        Log(L"[視聴予約] チャンネル切り替え: " + item.EventName);
        if (item.RecordOnTrigger) {
            StopRecordIfRecording(item.EventName);
            m_pendingRecord = true;
            m_pendingRecordTick = GetTickMs();
            m_pendingRecordItem = item;
        }
        return true;
    }

    if (TrySelectByEnumeratingDrivers(item)) {
        Log(L"[視聴予約] BonDriverを跨いで切り替え: " + item.EventName);
        if (item.RecordOnTrigger) {
            StopRecordIfRecording(item.EventName);
            m_pendingRecord = true;
            m_pendingRecordTick = GetTickMs();
            m_pendingRecordItem = item;
        }
        return true;
    }

    Log(L"[視聴予約] 切り替え失敗: " + item.EventName);
    return false;
}

void CViewReservationPlugin::StopRecordIfRecording(const std::wstring &nextEventName) {
    TVTest::RecordStatusInfo statusInfo{};
    statusInfo.Size = sizeof(statusInfo);
    if (m_pApp->GetRecordStatus(&statusInfo) &&
        statusInfo.Status == TVTest::RECORD_STATUS_RECORDING) {
        m_pApp->StopRecord();
        Log(L"[録画予約] 前番組の録画を停止 → " + nextEventName + L" の録画待機開始");
    }
}

bool CViewReservationPlugin::TryStartRecording(const ReservationItem &item) {
    // 既に録画中なら停止済みのはず（TrySwitchReservationで処理）
    TVTest::RecordStatusInfo statusInfo{};
    statusInfo.Size = sizeof(statusInfo);
    if (m_pApp->GetRecordStatus(&statusInfo)) {
        if (statusInfo.Status == TVTest::RECORD_STATUS_RECORDING) {
            // 念のため停止
            m_pApp->StopRecord();
        }
    }

    // 終了時刻 = 開始時刻 + Duration (秒)
    TVTest::RecordInfo info{};
    info.Size = sizeof(info);

    // 開始: 即時
    info.Mask = TVTest::RECORD_MASK_STOPTIME;
    info.StartTimeSpec = TVTest::RECORD_START_NOTSPECIFIED;

    // 停止: 現在時刻から番組終了時刻までの残り時間（ms）
    const ULONGLONG msUntilEnd = [&]() -> ULONGLONG {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        FILETIME ftNow{}, ftStart{};
        SystemTimeToFileTime(&now, &ftNow);
        SystemTimeToFileTime(&item.StartTime, &ftStart);
        ULARGE_INTEGER uiStart{}, uiNow{};
        uiStart.LowPart = ftStart.dwLowDateTime; uiStart.HighPart = ftStart.dwHighDateTime;
        uiNow.LowPart   = ftNow.dwLowDateTime;   uiNow.HighPart   = ftNow.dwHighDateTime;
        const ULONGLONG endValue = uiStart.QuadPart + static_cast<ULONGLONG>(item.Duration) * 10000000ULL;
        if (endValue <= uiNow.QuadPart) return 0ULL;
        return (endValue - uiNow.QuadPart) / 10000ULL; // 100ns → ms
    }();

    if (msUntilEnd == 0) {
        Log(L"[録画予約] 番組終了済みのため録画開始をスキップ: " + item.EventName);
        return false;
    }

    info.StopTimeSpec = TVTest::RECORD_STOP_DURATION;
    info.StopTime.Duration = msUntilEnd;

    if (m_pApp->StartRecord(&info)) {
        Log(L"[録画予約] 録画開始: " + item.EventName);
        return true;
    }

    Log(L"[録画予約] 録画開始失敗: " + item.EventName);
    return false;
}

TVTest::CTVTestPlugin *CreatePluginClass() {
    return new CViewReservationPlugin();
}
