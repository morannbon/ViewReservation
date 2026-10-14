#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <set>

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "ViewReservationPlugin.h"
#include "Version.h"

namespace {
// nullptr ガード付き wstring 変換（TVTest API の文字列ポインタ受け取り用）
std::wstring TrimText(const wchar_t *text) {
    if (text == nullptr) {
        return L"";
    }
    return text;
}

constexpr int kSettingsCheckPanel = 2101;
constexpr UINT kBaseDpi = 96;

UINT GetWindowDpiOrDefault(HWND hwnd) {
    using GetDpiForWindowFn = UINT (WINAPI *)(HWND);
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow != nullptr && hwnd != nullptr) {
        const UINT dpi = getDpiForWindow(hwnd);
        if (dpi != 0) {
            return dpi;
        }
    }

    HDC dc = GetDC(hwnd);
    if (dc != nullptr) {
        const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(hwnd, dc);
        if (dpi > 0) {
            return static_cast<UINT>(dpi);
        }
    }
    return kBaseDpi;
}

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), static_cast<int>(kBaseDpi));
}

COLORREF MixColor(COLORREF a, COLORREF b, int bPercent) {
    bPercent = (std::max)(0, (std::min)(100, bPercent));
    const int aPercent = 100 - bPercent;
    return RGB(
        (GetRValue(a) * aPercent + GetRValue(b) * bPercent) / 100,
        (GetGValue(a) * aPercent + GetGValue(b) * bPercent) / 100,
        (GetBValue(a) * aPercent + GetBValue(b) * bPercent) / 100);
}

struct PanelSemanticTextColors {
    COLORREF Station;
    COLORREF Time;
    COLORREF CurrentProgram;
    COLORREF FutureProgram;
};

// パネル文字色。時刻は番組名から独立させ、情報の階層を色でも判別できるようにする。
PanelSemanticTextColors GetPanelSemanticTextColors(bool darkSurface) {
    if (darkSurface) {
        return {
            RGB(112, 174, 218), // 局名
            RGB(184, 190, 198), // 時刻
            RGB(224, 187, 103), // 現在番組
            RGB(118, 184, 137)  // 未来番組
        };
    }
    return {
        RGB(39, 92, 139),   // 局名
        RGB(82, 88, 96),    // 時刻
        RGB(132, 91, 28),   // 現在番組
        RGB(53, 112, 70)    // 未来番組
    };
}

void FillSolid(HDC dc, const RECT &rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

void FillVerticalGradient(HDC dc, const RECT &rect, COLORREF top, COLORREF bottom) {
    constexpr int kSteps = 12;
    const int height = rect.bottom - rect.top;
    if (height <= 0) return;
    for (int i = 0; i < kSteps; ++i) {
        RECT band = rect;
        band.top = rect.top + height * i / kSteps;
        band.bottom = rect.top + height * (i + 1) / kSteps;
        const int mix = (i * 100) / (kSteps - 1);
        FillSolid(dc, band, MixColor(top, bottom, mix));
    }
}

// Owner-draw の1項目をメモリDCへ完成させてから一括転送する。
// 背景→文字→区切り線が画面へ段階的に見えるのを防ぎ、マーキー再描画時のちらつきを抑える。
RECT GetClippedPaintRect(HDC dc, const RECT &fallback) {
    RECT clip{};
    RECT result{};
    if (dc != nullptr && GetClipBox(dc, &clip) != ERROR && IntersectRect(&result, &fallback, &clip)) {
        return result;
    }
    return fallback;
}

class BufferedPaintSurface {
public:
    BufferedPaintSurface(HDC target, const RECT &rect)
        : m_target(target), m_rect(rect) {
        const int width = static_cast<int>(rect.right - rect.left);
        const int height = static_cast<int>(rect.bottom - rect.top);
        if (target == nullptr || width <= 0 || height <= 0) return;

        m_dc = CreateCompatibleDC(target);
        if (m_dc == nullptr) return;
        m_bitmap = CreateCompatibleBitmap(target, width, height);
        if (m_bitmap == nullptr) {
            DeleteDC(m_dc);
            m_dc = nullptr;
            return;
        }
        m_oldBitmap = SelectObject(m_dc, m_bitmap);
        SetViewportOrgEx(m_dc, -rect.left, -rect.top, nullptr);
    }

    ~BufferedPaintSurface() {
        if (m_dc == nullptr) return;
        const int width = static_cast<int>(m_rect.right - m_rect.left);
        const int height = static_cast<int>(m_rect.bottom - m_rect.top);
        BitBlt(m_target, m_rect.left, m_rect.top, width, height,
            m_dc, m_rect.left, m_rect.top, SRCCOPY);
        if (m_oldBitmap != nullptr) SelectObject(m_dc, m_oldBitmap);
        if (m_bitmap != nullptr) DeleteObject(m_bitmap);
        DeleteDC(m_dc);
    }

    HDC Get() const { return m_dc != nullptr ? m_dc : m_target; }

private:
    HDC m_target = nullptr;
    HDC m_dc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_oldBitmap = nullptr;
    RECT m_rect{};
};

HFONT CreateSettingsFont(UINT dpi) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);

    using SystemParametersInfoForDpiFn = BOOL (WINAPI *)(UINT, UINT, PVOID, UINT, UINT);
    static const auto systemParametersInfoForDpi = reinterpret_cast<SystemParametersInfoForDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));

    if (systemParametersInfoForDpi != nullptr &&
        systemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        metrics.lfMessageFont.lfHeight = MulDiv(
            metrics.lfMessageFont.lfHeight, static_cast<int>(dpi), static_cast<int>(kBaseDpi));
        metrics.lfMessageFont.lfWidth = MulDiv(
            metrics.lfMessageFont.lfWidth, static_cast<int>(dpi), static_cast<int>(kBaseDpi));
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    return nullptr;
}

struct SettingsDialogState {
    UINT Dpi = kBaseDpi;
    HFONT Font = nullptr;
    int TextHeight = 0;
    bool Accepted = false;
    bool Enabled = false;
};

LRESULT CALLBACK SettingsWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *state = reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        const auto *create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<SettingsDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        const UINT dpi = state != nullptr ? state->Dpi : kBaseDpi;
        auto sx = [dpi](int value) { return ScaleForDpi(value, dpi); };

        HFONT font = CreateSettingsFont(dpi);
        if (state != nullptr) {
            state->Font = font;
        }

        int textHeight = sx(16);
        if (font != nullptr) {
            HDC dc = GetDC(hwnd);
            if (dc != nullptr) {
                const HGDIOBJ oldFont = SelectObject(dc, font);
                TEXTMETRICW metrics{};
                if (GetTextMetricsW(dc, &metrics)) {
                    textHeight = metrics.tmHeight;
                }
                SelectObject(dc, oldFont);
                ReleaseDC(hwnd, dc);
            }
        }
        if (state != nullptr) {
            state->TextHeight = textHeight;
        }

        const int margin = sx(20);
        const int checkHeight = std::max(sx(28), textHeight + sx(10));
        const int noteHeight = std::max(sx(24), textHeight + sx(6));
        const int buttonHeight = std::max(sx(32), textHeight + sx(12));
        const int buttonWidth = sx(96);
        const int clientWidth = sx(430);
        const int checkTop = sx(18);
        const int noteTop = checkTop + checkHeight + sx(10);
        const int buttonTop = sx(105);

        HWND check = CreateWindowExW(0, L"BUTTON", L"パネルを使用する",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            margin, checkTop, clientWidth - margin * 2, checkHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsCheckPanel)), g_hinstDLL, nullptr);
        HWND note = CreateWindowExW(0, L"STATIC", L"変更はTVTest再起動後に反映されます。",
            WS_CHILD | WS_VISIBLE,
            margin, noteTop, clientWidth - margin * 2, noteHeight, hwnd, nullptr, g_hinstDLL, nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            clientWidth - margin - buttonWidth * 2 - sx(8), buttonTop, buttonWidth, buttonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), g_hinstDLL, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"キャンセル",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            clientWidth - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), g_hinstDLL, nullptr);

        for (HWND child : { check, note, ok, cancel }) {
            if (child != nullptr && font != nullptr) {
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
        }
        if (state != nullptr && state->Enabled && check != nullptr) {
            SendMessageW(check, BM_SETCHECK, BST_CHECKED, 0);
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            if (state != nullptr) {
                state->Enabled = SendDlgItemMessageW(hwnd, kSettingsCheckPanel, BM_GETCHECK, 0, 0) == BST_CHECKED;
                state->Accepted = true;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (state != nullptr && state->Font != nullptr) {
            DeleteObject(state->Font);
            state->Font = nullptr;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
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
    pInfo->Flags = static_cast<TVTest::PluginFlag>(TVTest::PLUGIN_FLAG_ENABLEDEFAULT | TVTest::PLUGIN_FLAG_HASSETTINGS);
    pInfo->pszPluginName = L"ViewReservation";
    pInfo->pszCopyright = L"";
    pInfo->pszDescription = L"視聴予約・録画予約 v" VIEWRESERVATION_VERSION_WSTRING;
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

    m_panelEnabled = LoadPanelEnabled();
    if (m_panelEnabled) {
        m_panelRegistered = RegisterPanelItem(); // 登録失敗でも予約本体は継続する
    }

    m_startupTick = GetTickMs();
    m_epgWaitLogged = false;
    m_sessionTriggeredKeys.clear();

    m_stop = false;
    m_worker = std::thread(&CViewReservationPlugin::WorkerLoop, this);

    return true;
}

bool CViewReservationPlugin::Finalize() {
    DestroyPanelWindow();

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


bool CViewReservationPlugin::LoadPanelEnabled() const {
    if (m_iniPath.empty()) {
        return false;
    }
    return GetPrivateProfileIntW(L"Settings", L"PanelEnabled", 0, m_iniPath.c_str()) != 0;
}

bool CViewReservationPlugin::SavePanelEnabled(bool enabled) const {
    if (m_iniPath.empty()) {
        return false;
    }
    return WritePrivateProfileStringW(
        L"Settings", L"PanelEnabled", enabled ? L"1" : L"0", m_iniPath.c_str()) != FALSE;
}

bool CViewReservationPlugin::ShowPanelSettingsDialog(HWND hwndOwner, bool *enabled) {
    if (enabled == nullptr) {
        return false;
    }

    static constexpr wchar_t kSettingsClassName[] = L"ViewReservation.SettingsWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsWindowProc;
    wc.hInstance = g_hinstDLL;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsClassName;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    SettingsDialogState state{};
    state.Enabled = *enabled;
    state.Dpi = GetWindowDpiOrDefault(hwndOwner);

    const int width = ScaleForDpi(430, state.Dpi);
    const int height = ScaleForDpi(180, state.Dpi);

    RECT ownerRect{};
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (hwndOwner != nullptr && GetWindowRect(hwndOwner, &ownerRect)) {
        x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClassName, L"ViewReservation 設定",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x, y, width, height, hwndOwner, nullptr, g_hinstDLL, &state);
    if (hwnd == nullptr) {
        return false;
    }

    if (hwndOwner != nullptr) EnableWindow(hwndOwner, FALSE);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg{};
    bool quitRequested = false;
    int quitCode = 0;
    while (IsWindow(hwnd)) {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) {
                quitRequested = true;
                quitCode = static_cast<int>(msg.wParam);
            }
            break;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hwndOwner != nullptr) EnableWindow(hwndOwner, TRUE);
    if (quitRequested) PostQuitMessage(quitCode);
    UnregisterClassW(kSettingsClassName, g_hinstDLL);

    if (!state.Accepted) {
        return false;
    }
    *enabled = state.Enabled;
    return true;
}

bool CViewReservationPlugin::OnPluginSettings(HWND hwndOwner) {
    bool enabled = LoadPanelEnabled();
    if (!ShowPanelSettingsDialog(hwndOwner, &enabled)) {
        return false;
    }
    if (!SavePanelEnabled(enabled)) {
        MessageBoxW(hwndOwner, L"設定を保存できませんでした。", L"ViewReservation", MB_OK | MB_ICONERROR);
        return false;
    }
    m_panelEnabled = enabled;
    return true;
}

bool CViewReservationPlugin::RegisterPanelItem() {
    TVTest::PanelItemInfo info{};
    info.Size = sizeof(info);
    info.Flags = TVTest::PANEL_ITEM_FLAG_NONE;
    info.Style = TVTest::PANEL_ITEM_STYLE_NONE;
    info.ID = kPanelItemId;
    info.pszIDText = L"ViewReservation";
    info.pszTitle = L"ViewReservation";
    info.hbmIcon = nullptr;
    if (!m_pApp->RegisterPanelItem(&info)) {
        return false;
    }

    TVTest::PanelItemSetInfo setInfo{};
    setInfo.Size = sizeof(setInfo);
    setInfo.Mask = TVTest::PANEL_ITEM_SET_INFO_MASK_STATE;
    setInfo.ID = kPanelItemId;
    setInfo.StateMask = TVTest::PANEL_ITEM_STATE_ENABLED;
    setInfo.State = TVTest::PANEL_ITEM_STATE_ENABLED;
    return m_pApp->SetPanelItem(&setInfo);
}

bool CViewReservationPlugin::OnPanelItemNotify(TVTest::PanelItemEventInfo *pInfo) {
    if (!m_panelRegistered || pInfo == nullptr || pInfo->ID != kPanelItemId) {
        return false;
    }

    switch (pInfo->Event) {
    case TVTest::PANEL_ITEM_EVENT_CREATE:
        return CreatePanelWindow(reinterpret_cast<TVTest::PanelItemCreateEventInfo*>(pInfo));

    case TVTest::PANEL_ITEM_EVENT_ACTIVATE:
        if (m_panelWindow != nullptr) {
            SetTimer(m_panelWindow, kPanelRefreshTimerId, kPanelRefreshIntervalMs, nullptr);
        }
        RefreshPanelRows(false);
        return true;

    case TVTest::PANEL_ITEM_EVENT_DEACTIVATE:
        m_panelHoverRow = -1;
        m_panelMarqueeActive = false;
        m_panelMouseTracking = false;
        if (m_panelWindow != nullptr) {
            KillTimer(m_panelWindow, kPanelRefreshTimerId);
            KillTimer(m_panelWindow, kPanelMarqueeTimerId);
            KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
        }
        return true;

    case TVTest::PANEL_ITEM_EVENT_DISABLE:
        m_panelHoverRow = -1;
        m_panelMarqueeActive = false;
        m_panelMouseTracking = false;
        if (m_panelWindow != nullptr) {
            KillTimer(m_panelWindow, kPanelRefreshTimerId);
            KillTimer(m_panelWindow, kPanelMarqueeTimerId);
            KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
        }
        return true;

    case TVTest::PANEL_ITEM_EVENT_ENABLE:
        return true;

    case TVTest::PANEL_ITEM_EVENT_STYLECHANGED:
    case TVTest::PANEL_ITEM_EVENT_FONTCHANGED:
        UpdatePanelFont();
        if (m_panelWindow != nullptr) {
            RECT rc{};
            GetClientRect(m_panelWindow, &rc);
            LayoutPanel(rc.right - rc.left, rc.bottom - rc.top);
            InvalidateRect(m_panelWindow, nullptr, FALSE);
        }
        return true;
    }

    return false;
}

bool CViewReservationPlugin::CreatePanelWindow(TVTest::PanelItemCreateEventInfo *pInfo) {
    if (pInfo == nullptr || pInfo->hwndParent == nullptr) {
        return false;
    }

    static constexpr wchar_t kPanelClassName[] = L"ViewReservation.PanelWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelWindowProc;
    wc.hInstance = g_hinstDLL;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kPanelClassName;

    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const int width = (std::max)(1L, pInfo->ItemRect.right - pInfo->ItemRect.left);
    const int height = (std::max)(1L, pInfo->ItemRect.bottom - pInfo->ItemRect.top);

    m_panelWindow = CreateWindowExW(
        0,
        kPanelClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        pInfo->ItemRect.left,
        pInfo->ItemRect.top,
        width,
        height,
        pInfo->hwndParent,
        nullptr,
        g_hinstDLL,
        this);

    if (m_panelWindow == nullptr) {
        return false;
    }

    pInfo->hwndItem = m_panelWindow;
    RefreshPanelRows(true);
    return true;
}

void CViewReservationPlugin::DestroyPanelWindow() {
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelRefreshTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
        DestroyWindow(m_panelWindow);
        m_panelWindow = nullptr;
    }
    m_panelList = nullptr;
    m_panelStatus = nullptr;
    m_panelListOriginalProc = nullptr;
    if (m_panelFont != nullptr) {
        DeleteObject(m_panelFont);
        m_panelFont = nullptr;
    }
    if (m_panelBackBrush != nullptr) {
        DeleteObject(m_panelBackBrush);
        m_panelBackBrush = nullptr;
        m_panelBackBrushColor = CLR_INVALID;
    }
    m_panelRows.clear();
    m_panelExpandedChannels.clear();
    m_panelWave = PanelWave::Terrestrial;
    m_panelCatvAvailable = false;
    UnregisterClassW(L"ViewReservation.PanelWindow", g_hinstDLL);
}

void CViewReservationPlugin::LayoutPanel(int width, int height) {
    if (m_panelWindow == nullptr) {
        return;
    }

    constexpr int margin = 4;
    constexpr int gap = 3;
    constexpr int waveHeight = 24;
    const int waveCount = m_panelCatvAvailable ? 4 : 3;
    const int totalGap = gap * (waveCount - 1);
    const int waveWidth = (std::max)(34, (width - margin * 2 - totalGap) / waveCount);
    const int listTop = margin + waveHeight + gap;
    const int listHeight = (std::max)(20, height - listTop - margin);

    HWND gr = GetDlgItem(m_panelWindow, kControlWaveGr);
    HWND bs = GetDlgItem(m_panelWindow, kControlWaveBs);
    HWND cs = GetDlgItem(m_panelWindow, kControlWaveCs);
    HWND catv = GetDlgItem(m_panelWindow, kControlWaveCatv);

    if (gr != nullptr) MoveWindow(gr, margin, margin, waveWidth, waveHeight, TRUE);
    if (bs != nullptr) MoveWindow(bs, margin + waveWidth + gap, margin, waveWidth, waveHeight, TRUE);
    if (cs != nullptr) MoveWindow(cs, margin + (waveWidth + gap) * 2, margin, waveWidth, waveHeight, TRUE);
    if (catv != nullptr) {
        if (m_panelCatvAvailable) {
            MoveWindow(catv, margin + (waveWidth + gap) * 3, margin, waveWidth, waveHeight, TRUE);
            ShowWindow(catv, SW_SHOWNOACTIVATE);
        } else {
            ShowWindow(catv, SW_HIDE);
        }
    }

    const int listWidth = (std::max)(20, width - margin * 2);
    if (m_panelList != nullptr) {
        ResetPanelMarqueeForLayoutChange();
        SetWindowPos(m_panelList, nullptr, margin, listTop, listWidth, listHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        RedrawWindow(m_panelList, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    if (m_panelStatus != nullptr) {
        const int statusHeight = (std::min)(48, (std::max)(28, listHeight / 5));
        MoveWindow(m_panelStatus, margin, listTop, listWidth, statusHeight, TRUE);
    }
}

void CViewReservationPlugin::UpdatePanelFont() {
    if (m_panelWindow == nullptr || m_pApp == nullptr) {
        return;
    }

    HFONT nextFont = nullptr;
    LOGFONTW logFont{};
    const int dpi = static_cast<int>(GetWindowDpiOrDefault(m_panelWindow));
    if (m_pApp->GetFont(L"PanelFont", &logFont, dpi)) {
        nextFont = CreateFontIndirectW(&logFont);
    }

    HFONT applyFont = nextFont != nullptr ? nextFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (const int id : { kControlWaveGr, kControlWaveBs, kControlWaveCs, kControlWaveCatv, kControlList, kControlStatus }) {
        HWND control = GetDlgItem(m_panelWindow, id);
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(applyFont), TRUE);
        }
    }

    if (m_panelFont != nullptr) {
        DeleteObject(m_panelFont);
    }
    m_panelFont = nextFont;
}

bool CViewReservationPlugin::BuildPanelPrograms(
    const DriverChannelEntry &channel,
    ReservationItem *current,
    std::vector<ReservationItem> *future) const {
    if (current == nullptr || future == nullptr || m_pApp == nullptr) {
        return false;
    }

    *current = ReservationItem{};
    future->clear();

    TVTest::EpgEventList list{};
    list.NetworkID = channel.NetworkID;
    list.TransportStreamID = channel.TransportStreamID;
    list.ServiceID = channel.ServiceID;
    if (!m_pApp->GetEpgEventList(&list)) {
        return false;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    const ULONGLONG nowValue = FileTimeOf(now);
    const ULONGLONG horizonValue = nowValue + kPanelFutureHorizonSec * 10000000ULL;
    bool foundCurrent = false;

    auto makeItem = [&](const TVTest::EpgEventInfo *ev) {
        ReservationItem item{};
        item.NetworkID = channel.NetworkID;
        item.TransportStreamID = channel.TransportStreamID;
        item.ServiceID = channel.ServiceID;
        item.EventID = ev->EventID;
        item.OriginalNetworkID = item.NetworkID;
        item.OriginalTransportStreamID = item.TransportStreamID;
        item.OriginalServiceID = item.ServiceID;
        item.OriginalEventID = item.EventID;
        item.StartTime = ev->StartTime;
        item.Duration = ev->Duration;
        item.EventName = TrimText(ev->pszEventName);
        item.LastDriverName = channel.DriverName;
        if (item.EventName.empty()) {
            item.EventName = L"(番組名なし)";
        }
        return item;
    };

    for (WORD i = 0; i < list.NumEvents; ++i) {
        const TVTest::EpgEventInfo *ev = list.EventList[i];
        if (ev == nullptr) {
            continue;
        }

        const ULONGLONG start = FileTimeOf(ev->StartTime);
        const ULONGLONG endTime = start + static_cast<ULONGLONG>(ev->Duration) * 10000000ULL;
        if (start <= nowValue && nowValue < endTime) {
            *current = makeItem(ev);
            foundCurrent = true;
            continue;
        }

        if (start > nowValue && start < horizonValue) {
            future->push_back(makeItem(ev));
        }
    }

    std::stable_sort(future->begin(), future->end(), [](const ReservationItem &a, const ReservationItem &b) {
        return FileTimeOf(a.StartTime) < FileTimeOf(b.StartTime);
    });

    m_pApp->FreeEpgEventList(&list);
    return foundCurrent;
}

void CViewReservationPlugin::RefreshPanelRows(bool forceDriverCache) {
    if (m_panelList == nullptr || m_pApp == nullptr) {
        return;
    }

    // 更新表示は初回の空一覧だけに限定する。定期更新で重ね直してちらつかせない。
    const bool showStatus = m_panelRows.empty() && m_panelStatus != nullptr;
    if (showStatus) {
        SetWindowTextW(m_panelStatus, L"番組情報を更新しています…");
        ShowWindow(m_panelStatus, SW_SHOWNOACTIVATE);
        SetWindowPos(m_panelStatus, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    const LRESULT oldSelection = SendMessageW(m_panelList, LB_GETCURSEL, 0, 0);
    ULONGLONG oldChannelKey = 0;
    WORD oldEventId = 0;
    if (oldSelection != LB_ERR && static_cast<size_t>(oldSelection) < m_panelRows.size()) {
        const auto &oldRow = m_panelRows[static_cast<size_t>(oldSelection)];
        oldChannelKey = oldRow.ChannelKey;
        oldEventId = oldRow.Program.EventID;
    }

    const LRESULT oldTopIndex = SendMessageW(m_panelList, LB_GETTOPINDEX, 0, 0);
    ULONGLONG oldTopChannelKey = 0;
    WORD oldTopEventId = 0;
    bool oldTopWasFuture = false;
    if (oldTopIndex != LB_ERR && oldTopIndex >= 0 && static_cast<size_t>(oldTopIndex) < m_panelRows.size()) {
        const auto &topRow = m_panelRows[static_cast<size_t>(oldTopIndex)];
        oldTopChannelKey = topRow.ChannelKey;
        oldTopEventId = topRow.Program.EventID;
        oldTopWasFuture = topRow.IsFuture;
    }

    m_driverCache.RebuildIfNeeded(m_pApp, forceDriverCache);
    auto channels = m_driverCache.Snapshot();

    auto normalizeSpace = [](const DriverChannelEntry &entry) -> PanelWave {
        switch (ResolveChannelDisplayWave(entry)) {
        case ChannelDisplayWave::Bs: return PanelWave::Bs;
        case ChannelDisplayWave::Cs: return PanelWave::Cs;
        case ChannelDisplayWave::Catv: return PanelWave::Catv;
        default: return PanelWave::Terrestrial;
        }
    };

    const bool catvAvailable = std::any_of(channels.begin(), channels.end(), [](const DriverChannelEntry &entry) {
        return ResolveChannelDisplayWave(entry) == ChannelDisplayWave::Catv;
    });
    if (catvAvailable != m_panelCatvAvailable) {
        m_panelCatvAvailable = catvAvailable;
        if (!m_panelCatvAvailable && m_panelWave == PanelWave::Catv) {
            m_panelWave = PanelWave::Terrestrial;
        }
        if (m_panelWindow != nullptr) {
            RECT panelRect{};
            GetClientRect(m_panelWindow, &panelRect);
            LayoutPanel(static_cast<int>(panelRect.right - panelRect.left),
                static_cast<int>(panelRect.bottom - panelRect.top));
        }
    }

    // Preserve the channel order returned by TVTest / the BonDriver tuning-space list.
    // The panel must not impose its own station-name or service-ID ordering.
    std::set<ULONGLONG> seen;
    const auto reservations = m_manager != nullptr ? m_manager->Snapshot() : std::vector<ReservationItem>{};
    auto reservationKindOf = [&](const ReservationItem &program) {
        if (program.EventID == 0) {
            return PanelReservationKind::None;
        }
        for (const auto &existing : reservations) {
            if (MatchesProgramIdentity(existing, program.NetworkID, program.TransportStreamID,
                    program.ServiceID, program.EventID)) {
                return existing.RecordOnTrigger ? PanelReservationKind::Record : PanelReservationKind::View;
            }
        }
        return PanelReservationKind::None;
    };

    std::vector<PanelRow> rows;
    for (auto channel : channels) {
        const PanelWave channelWave = normalizeSpace(channel);
        if (channelWave != m_panelWave) {
            continue;
        }

        const ULONGLONG key =
            (static_cast<ULONGLONG>(channel.NetworkID) << 32) |
            (static_cast<ULONGLONG>(channel.TransportStreamID) << 16) |
            static_cast<ULONGLONG>(channel.ServiceID);
        if (!seen.insert(key).second) {
            continue;
        }

        ReservationItem current{};
        std::vector<ReservationItem> future;
        const bool hasCurrent = BuildPanelPrograms(channel, &current, &future);
        if (!hasCurrent) {
            current.NetworkID = channel.NetworkID;
            current.TransportStreamID = channel.TransportStreamID;
            current.ServiceID = channel.ServiceID;
            current.OriginalNetworkID = channel.NetworkID;
            current.OriginalTransportStreamID = channel.TransportStreamID;
            current.OriginalServiceID = channel.ServiceID;
            current.EventName = L"番組情報なし";
            current.LastDriverName = channel.DriverName;
        }

        PanelRow header{};
        header.Channel = channel;
        header.Program = current;
        header.ChannelKey = key;
        header.IsFuture = false;
        header.HasProgram = hasCurrent;
        header.HasFuture = !future.empty();
        header.ReservationKind = reservationKindOf(header.Program);
        header.DisplayText = channel.ChannelName + L" " + current.EventName;
        rows.push_back(std::move(header));

        if (m_panelExpandedChannels.find(key) != m_panelExpandedChannels.end()) {
            for (auto &program : future) {
                PanelRow futureRow{};
                futureRow.Channel = channel;
                futureRow.Program = std::move(program);
                futureRow.ChannelKey = key;
                futureRow.IsFuture = true;
                futureRow.HasProgram = true;
                futureRow.HasFuture = false;
                futureRow.ReservationKind = reservationKindOf(futureRow.Program);
                futureRow.DisplayText = futureRow.Program.EventName;
                rows.push_back(std::move(futureRow));
            }
        }
    }

    auto sameRow = [&](const PanelRow &a, const PanelRow &b) {
        return a.ChannelKey == b.ChannelKey &&
            a.IsFuture == b.IsFuture &&
            a.HasProgram == b.HasProgram &&
            a.HasFuture == b.HasFuture &&
            a.ReservationKind == b.ReservationKind &&
            a.Channel.ChannelName == b.Channel.ChannelName &&
            a.Program.EventID == b.Program.EventID &&
            FileTimeOf(a.Program.StartTime) == FileTimeOf(b.Program.StartTime) &&
            a.Program.Duration == b.Program.Duration &&
            a.Program.EventName == b.Program.EventName;
    };
    bool rowsChanged = rows.size() != m_panelRows.size();
    if (!rowsChanged) {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (!sameRow(rows[i], m_panelRows[i])) {
                rowsChanged = true;
                break;
            }
        }
    }
    if (!rowsChanged) {
        if (showStatus) ShowWindow(m_panelStatus, SW_HIDE);
        return;
    }

    m_panelRows.swap(rows);
    m_panelHoverRow = -1;
    m_panelMarqueeActive = false;
    m_panelMouseTracking = false;
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
    }
    m_panelMarqueeEpochTick = GetTickMs();
    SendMessageW(m_panelList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(m_panelList, LB_RESETCONTENT, 0, 0);

    int selectIndex = -1;
    int topIndex = -1;
    for (size_t i = 0; i < m_panelRows.size(); ++i) {
        SendMessageW(m_panelList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m_panelRows[i].DisplayText.c_str()));
        if (selectIndex < 0 && oldSelection != LB_ERR && m_panelRows[i].ChannelKey == oldChannelKey &&
            (oldEventId == 0 || m_panelRows[i].Program.EventID == oldEventId)) {
            selectIndex = static_cast<int>(i);
        }
        if (topIndex < 0 && oldTopChannelKey != 0 && m_panelRows[i].ChannelKey == oldTopChannelKey &&
            m_panelRows[i].IsFuture == oldTopWasFuture &&
            (oldTopEventId == 0 || m_panelRows[i].Program.EventID == oldTopEventId)) {
            topIndex = static_cast<int>(i);
        }
    }

    if (topIndex >= 0) {
        SendMessageW(m_panelList, LB_SETTOPINDEX, static_cast<WPARAM>(topIndex), 0);
    }
    if (selectIndex >= 0) {
        SendMessageW(m_panelList, LB_SETCURSEL, static_cast<WPARAM>(selectIndex), 0);
    } else {
        SendMessageW(m_panelList, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    }

    SendMessageW(m_panelList, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(m_panelList, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    if (showStatus) {
        ShowWindow(m_panelStatus, SW_HIDE);
    }
}

void CViewReservationPlugin::SetPanelWave(PanelWave wave) {
    if (wave == m_panelWave) {
        return;
    }
    m_panelWave = wave;
    m_panelHoverRow = -1;
    m_panelMarqueeActive = false;
    m_panelMouseTracking = false;
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
    }
    m_panelMarqueeEpochTick = GetTickMs();
    for (const int id : { kControlWaveGr, kControlWaveBs, kControlWaveCs, kControlWaveCatv }) {
        HWND button = m_panelWindow != nullptr ? GetDlgItem(m_panelWindow, id) : nullptr;
        if (button != nullptr) InvalidateRect(button, nullptr, FALSE);
    }
    RefreshPanelRows(false);
}

void CViewReservationPlugin::TogglePanelChannelExpanded(size_t rowIndex) {
    if (rowIndex >= m_panelRows.size()) {
        return;
    }
    const PanelRow &row = m_panelRows[rowIndex];
    if (row.IsFuture || !row.HasFuture) {
        return;
    }

    const bool wasExpanded = m_panelExpandedChannels.find(row.ChannelKey) != m_panelExpandedChannels.end();
    m_panelExpandedChannels.clear();
    if (!wasExpanded) {
        m_panelExpandedChannels.insert(row.ChannelKey);
    }
    if (m_panelList != nullptr) {
        SendMessageW(m_panelList, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    }
    m_panelHoverRow = -1;
    m_panelMarqueeActive = false;
    m_panelMouseTracking = false;
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
    }
    m_panelMarqueeEpochTick = GetTickMs();
    RefreshPanelRows(false);
}

bool CViewReservationPlugin::ApplyPanelReservation(const ReservationItem &source, bool record) {
    if (m_manager == nullptr || !source.EventID || source.Duration == 0) {
        return false;
    }
    ReservationItem item = source;
    item.RecordOnTrigger = record;
    std::wstring message;
    if (!m_manager->AddOrUpdateReservation(item, &message)) {
        Log(message);
        ShowReservationError(message);
        return false;
    }
    Log(message);
    RefreshPanelRows(false);
    m_cv.notify_all();
    return true;
}

bool CViewReservationPlugin::RemovePanelReservation(const ReservationItem &item) {
    if (m_manager == nullptr || !item.EventID) {
        return false;
    }
    std::wstring ignored;
    if (!m_manager->RemoveReservation(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, &ignored)) {
        return false;
    }
    Log(L"予約を解除しました: " + item.EventName);
    RefreshPanelRows(false);
    m_cv.notify_all();
    return true;
}

void CViewReservationPlugin::ShowPanelReservationMenu(size_t rowIndex, POINT screenPoint) {
    if (rowIndex >= m_panelRows.size() || m_manager == nullptr) {
        return;
    }
    const PanelRow &row = m_panelRows[rowIndex];
    if (!row.HasProgram || row.Program.EventID == 0 || row.Program.Duration == 0) {
        return;
    }

    bool reserved = false;
    bool recordReserved = false;
    for (const auto &existing : m_manager->Snapshot()) {
        if (MatchesProgramIdentity(existing, row.Program.NetworkID, row.Program.TransportStreamID,
                row.Program.ServiceID, row.Program.EventID)) {
            reserved = true;
            recordReserved = existing.RecordOnTrigger;
            break;
        }
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    constexpr UINT kReserveView = 1;
    constexpr UINT kReserveRecord = 2;
    constexpr UINT kRemoveReservation = 3;
    if (!reserved) {
        AppendMenuW(menu, MF_STRING, kReserveView, L"視聴予約");
        AppendMenuW(menu, MF_STRING, kReserveRecord, L"録画予約");
    } else if (recordReserved) {
        AppendMenuW(menu, MF_STRING, kReserveView, L"視聴予約に変更");
        AppendMenuW(menu, MF_STRING, kRemoveReservation, L"録画予約を解除");
    } else {
        AppendMenuW(menu, MF_STRING, kReserveRecord, L"録画予約に変更");
        AppendMenuW(menu, MF_STRING, kRemoveReservation, L"視聴予約を解除");
    }

    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        screenPoint.x, screenPoint.y, 0, m_panelWindow, nullptr);
    DestroyMenu(menu);

    if (command == kReserveView) {
        ApplyPanelReservation(row.Program, false);
    } else if (command == kReserveRecord) {
        ApplyPanelReservation(row.Program, true);
    } else if (command == kRemoveReservation) {
        RemovePanelReservation(row.Program);
    }
    InvalidateRect(m_panelList, nullptr, FALSE);
}

void CViewReservationPlugin::ActivatePanelCurrentProgram(size_t rowIndex) {
    if (rowIndex >= m_panelRows.size()) {
        return;
    }

    const PanelRow &row = m_panelRows[rowIndex];
    if (row.IsFuture) {
        return;
    }

    ReservationItem item = row.Program;
    item.NetworkID = row.Channel.NetworkID;
    item.TransportStreamID = row.Channel.TransportStreamID;
    item.ServiceID = row.Channel.ServiceID;
    item.OriginalNetworkID = row.Channel.NetworkID;
    item.OriginalTransportStreamID = row.Channel.TransportStreamID;
    item.OriginalServiceID = row.Channel.ServiceID;
    item.RecordOnTrigger = false;
    item.LastDriverName = row.Channel.DriverName;
    if (item.EventName.empty()) {
        item.EventName = row.Channel.ChannelName;
    }

    TrySwitchToItem(item, L"視聴");
}

void CViewReservationPlugin::DrawPanelWaveButton(const DRAWITEMSTRUCT *draw) const {
    if (draw == nullptr) return;

    const RECT paintRect = GetClippedPaintRect(draw->hDC, draw->rcItem);
    BufferedPaintSurface surface(draw->hDC, paintRect);
    HDC dc = surface.Get();

    PanelWave wave = PanelWave::Terrestrial;
    const wchar_t *label = L"";
    if (draw->CtlID == kControlWaveGr) {
        wave = PanelWave::Terrestrial;
        label = L"地上波";
    } else if (draw->CtlID == kControlWaveBs) {
        wave = PanelWave::Bs;
        label = L"BS";
    } else if (draw->CtlID == kControlWaveCs) {
        wave = PanelWave::Cs;
        label = L"CS";
    } else if (draw->CtlID == kControlWaveCatv) {
        wave = PanelWave::Catv;
        label = L"CATV";
    } else {
        return;
    }

    auto themeColor = [&](LPCWSTR name, COLORREF fallback) {
        const COLORREF value = m_pApp != nullptr ? m_pApp->GetColor(name) : CLR_INVALID;
        return value == CLR_INVALID ? fallback : value;
    };
    const COLORREF panelBack = themeColor(L"PanelBack", GetSysColor(COLOR_BTNFACE));
    const COLORREF panelText = themeColor(L"PanelText", GetSysColor(COLOR_BTNTEXT));
    const COLORREF tab1 = themeColor(L"PanelTabBack", GetSysColor(COLOR_HIGHLIGHT));
    const COLORREF tab2 = themeColor(L"PanelTabBack2", tab1);
    const COLORREF tabText = themeColor(L"PanelTabText", panelText);
    const COLORREF border = themeColor(L"PanelTabBorder", panelText);
    const bool active = wave == m_panelWave;

    if (active) {
        FillVerticalGradient(dc, draw->rcItem, tab1, tab2);
    } else {
        FillSolid(dc, draw->rcItem, MixColor(panelBack, tab1, 8));
    }

    RECT edge = draw->rcItem;
    HBRUSH brush = CreateSolidBrush(MixColor(border, panelBack, 25));
    if (brush != nullptr) {
        FrameRect(dc, &edge, brush);
        DeleteObject(brush);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, active ? tabText : panelText);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    RECT text = draw->rcItem;
    DrawTextW(dc, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont != nullptr) SelectObject(dc, oldFont);
}

CViewReservationPlugin::PanelRowLayout CViewReservationPlugin::CalculatePanelRowLayout(
    const RECT &itemRect, HDC dc) const {
    PanelRowLayout layout{};

    // Win32 RECT は LONG、レイアウト定数は int なので、
    // 以後の計算は最初に int へ正規化して std::min/max の型を一本化する。
    const int itemLeft = static_cast<int>(itemRect.left);
    const int itemRight = static_cast<int>(itemRect.right);

    const int rcLeft = itemLeft + 5;
    const int rcRight = itemRight - 4;

    layout.Content.left = rcLeft;
    layout.Content.top = itemRect.top;
    layout.Content.right = (std::max)(
        rcLeft,
        rcRight - kPanelArrowVisualWidth - kPanelReservationVisualWidth);
    layout.Content.bottom = itemRect.bottom;

    layout.Arrow.left = (std::max)(
        rcLeft,
        rcRight - kPanelArrowVisualWidth + 1);
    layout.Arrow.top = itemRect.top;
    layout.Arrow.right = rcRight;
    layout.Arrow.bottom = itemRect.bottom;

    layout.Reservation.right = (std::max)(
        rcLeft,
        rcRight - kPanelArrowVisualWidth);
    layout.Reservation.left = (std::max)(
        rcLeft,
        static_cast<int>(layout.Reservation.right) - kPanelReservationVisualWidth);
    layout.Reservation.top = itemRect.top;
    layout.Reservation.bottom = itemRect.bottom;

    SIZE timeSize{};
    constexpr wchar_t kTimeSample[] = L"00:00-00:00";
    int measuredTimeWidth = 0;
    if (dc != nullptr &&
        GetTextExtentPoint32W(dc, kTimeSample, 11, &timeSize)) {
        measuredTimeWidth = static_cast<int>(timeSize.cx);
    }

    const int desiredTimeWidth = (std::max)(
        1,
        measuredTimeWidth + kPanelTimeColumnPadding);
    const int contentWidth = (std::max)(
        0,
        static_cast<int>(layout.Content.right) - static_cast<int>(layout.Content.left));
    layout.TimeWidth = (std::min)(desiredTimeWidth, contentWidth);
    layout.TitleLeft = (std::min)(
        static_cast<int>(layout.Content.right),
        static_cast<int>(layout.Content.left) + layout.TimeWidth + kPanelTitleGap);
    return layout;
}

void CViewReservationPlugin::ResetPanelMarqueeForLayoutChange() {
    m_panelHoverRow = -1;
    m_panelMarqueeActive = false;
    m_panelMouseTracking = false;
    m_panelMarqueeEpochTick = GetTickMs();
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
    }
}

void CViewReservationPlugin::DrawPanelRow(const DRAWITEMSTRUCT *draw) const {
    if (draw == nullptr || draw->itemID == static_cast<UINT>(-1) ||
        static_cast<size_t>(draw->itemID) >= m_panelRows.size()) {
        return;
    }

    const RECT paintRect = GetClippedPaintRect(draw->hDC, draw->rcItem);
    BufferedPaintSurface surface(draw->hDC, paintRect);
    HDC dc = surface.Get();

    const PanelRow &row = m_panelRows[draw->itemID];
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;

    auto themeColor = [&](LPCWSTR name, COLORREF fallback) {
        const COLORREF value = m_pApp != nullptr ? m_pApp->GetColor(name) : CLR_INVALID;
        return value == CLR_INVALID ? fallback : value;
    };
    const COLORREF panelBack = themeColor(L"PanelBack", GetSysColor(COLOR_WINDOW));
    const COLORREF panelText = themeColor(L"PanelText", GetSysColor(COLOR_WINDOWTEXT));
    const COLORREF accentTop = themeColor(L"PanelTabBack", GetSysColor(COLOR_HIGHLIGHT));
    const COLORREF accentBottom = themeColor(L"PanelTabBack2", accentTop);
    const COLORREF accentText = themeColor(L"PanelTabText", panelText);
    const COLORREF border = themeColor(L"PanelTabBorder", MixColor(panelText, panelBack, 55));

    COLORREF rowBack = panelBack;
    if (selected) {
        FillVerticalGradient(dc, draw->rcItem, accentTop, accentBottom);
        rowBack = MixColor(accentTop, accentBottom, 50);
    } else if (row.IsFuture) {
        rowBack = MixColor(panelBack, accentTop, 10);
        FillSolid(dc, draw->rcItem, rowBack);
    } else {
        FillSolid(dc, draw->rcItem, panelBack);
    }

    const bool darkSurface = m_pApp != nullptr
        ? m_pApp->IsDarkModeColor(rowBack)
        : (GetRValue(rowBack) + GetGValue(rowBack) + GetBValue(rowBack) < 384);
    const PanelSemanticTextColors semantic = GetPanelSemanticTextColors(darkSurface);
    const COLORREF programText = row.IsFuture ? semantic.FutureProgram : semantic.CurrentProgram;

    SetBkMode(dc, TRANSPARENT);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(m_panelList, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = nullptr;
    if (font != nullptr) {
        oldFont = SelectObject(dc, font);
    }

    // 右端の予約状態列・展開列・時刻列を全描画経路で同じ計算に統一する。
    const PanelRowLayout layout = CalculatePanelRowLayout(draw->rcItem, dc);
    RECT content = layout.Content;

    auto formatTime = [](const ReservationItem &item) {
        if (item.Duration == 0) return std::wstring{};
        FILETIME ft{};
        SYSTEMTIME end{};
        if (!SystemTimeToFileTime(&item.StartTime, &ft)) return std::wstring{};
        ULARGE_INTEGER value{};
        value.LowPart = ft.dwLowDateTime;
        value.HighPart = ft.dwHighDateTime;
        value.QuadPart += static_cast<ULONGLONG>(item.Duration) * 10000000ULL;
        ft.dwLowDateTime = value.LowPart;
        ft.dwHighDateTime = value.HighPart;
        if (!FileTimeToSystemTime(&ft, &end)) return std::wstring{};
        wchar_t text[24]{};
        swprintf_s(text, L"%02u:%02u-%02u:%02u", item.StartTime.wHour, item.StartTime.wMinute, end.wHour, end.wMinute);
        return std::wstring(text);
    };

    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    const int lineHeight = (std::max)(14, static_cast<int>(tm.tmHeight));
    const int timeWidth = layout.TimeWidth;
    const int titleStart = layout.TitleLeft;

    RECT titleRect{};
    if (!row.IsFuture) {
        RECT stationRect = content;
        stationRect.bottom = (std::min)(stationRect.bottom, stationRect.top + lineHeight + 3);
        const std::wstring station = row.Channel.ChannelName.empty() ? L"(局名未取得)" : row.Channel.ChannelName;
        SetTextColor(dc, semantic.Station);
        DrawTextW(dc, station.c_str(), -1, &stationRect,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT second = content;
        second.top = stationRect.bottom;
        const std::wstring time = formatTime(row.Program);
        RECT timeRect = second;
        timeRect.right = (std::min)(second.right, second.left + timeWidth);
        SetTextColor(dc, semantic.Time);
        DrawTextW(dc, time.c_str(), -1, &timeRect,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        titleRect = second;
        titleRect.left = titleStart;
    } else {
        // 未来番組は上下にわずかな余白を持たせ、展開時の詰まり感を抑える。
        content.top += 2;
        content.bottom -= 2;
        const std::wstring time = formatTime(row.Program);
        RECT timeRect = content;
        timeRect.right = (std::min)(content.right, timeRect.left + timeWidth);
        SetTextColor(dc, semantic.Time);
        DrawTextW(dc, time.c_str(), -1, &timeRect,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        titleRect = content;
        titleRect.left = titleStart;
    }

    const std::wstring &title = row.Program.EventName;
    row.NeedsMarquee = false;
    if (!title.empty() && titleRect.right > titleRect.left) {
        SetTextColor(dc, programText);
        SIZE size{};
        GetTextExtentPoint32W(dc, title.c_str(), static_cast<int>(title.size()), &size);
        const int titlePixelWidth = static_cast<int>(size.cx);
        const int available = static_cast<int>(titleRect.right - titleRect.left);
        row.NeedsMarquee = titlePixelWidth > available;
        const int y = static_cast<int>(titleRect.top)
            + (static_cast<int>(titleRect.bottom - titleRect.top) - lineHeight) / 2;
        RECT clip = titleRect;
        if (titlePixelWidth <= available) {
            ExtTextOutW(dc, titleRect.left, y, ETO_CLIPPED, &clip,
                title.c_str(), static_cast<UINT>(title.size()), nullptr);
        } else if (static_cast<int>(draw->itemID) == m_panelHoverRow && !m_panelMarqueeActive) {
            // ホバー待機中は通常位置の全文をそのままクリップ表示する。
            // 省略記号付き表示から移動描画へ直接切り替えず、開始位置を連続させる。
            ExtTextOutW(dc, titleRect.left, y, ETO_CLIPPED, &clip,
                title.c_str(), static_cast<UINT>(title.size()), nullptr);
        } else if (static_cast<int>(draw->itemID) == m_panelHoverRow && m_panelMarqueeActive) {
            // 見切れタイトルはホバー中の1行だけ、右から左へ滑らかに循環する。
            const int gap = (std::max)(28, available / 3);
            const int travel = (std::max)(1, titlePixelWidth + gap);
            const ULONGLONG nowTick = GetTickMs();
            const ULONGLONG elapsed = nowTick >= m_panelMarqueeEpochTick ? nowTick - m_panelMarqueeEpochTick : 0;
            const int phase = static_cast<int>(
                ((elapsed * static_cast<ULONGLONG>(kPanelMarqueePixelsPerSecond)) / 1000ULL)
                % static_cast<ULONGLONG>(travel));
            const int x1 = titleRect.left - phase;
            const int x2 = x1 + titlePixelWidth + gap;
            if (x1 < static_cast<int>(titleRect.right) && x1 + titlePixelWidth > static_cast<int>(titleRect.left)) {
                ExtTextOutW(dc, x1, y, ETO_CLIPPED, &clip,
                    title.c_str(), static_cast<UINT>(title.size()), nullptr);
            }
            if (x2 < static_cast<int>(titleRect.right) && x2 + titlePixelWidth > static_cast<int>(titleRect.left)) {
                ExtTextOutW(dc, x2, y, ETO_CLIPPED, &clip,
                    title.c_str(), static_cast<UINT>(title.size()), nullptr);
            }
        } else {
            DrawTextW(dc, title.c_str(), -1, &titleRect,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    if (row.ReservationKind != PanelReservationKind::None) {
        RECT reservationRect = layout.Reservation;
        const bool recordReserved = row.ReservationKind == PanelReservationKind::Record;
        const COLORREF reservationColor = darkSurface
            ? (recordReserved ? RGB(255, 105, 105) : RGB(105, 200, 255))
            : (recordReserved ? RGB(190, 45, 45) : RGB(0, 105, 180));
        SetTextColor(dc, reservationColor);
        const wchar_t *reservationMark = recordReserved ? L"●" : L"▶";
        DrawTextW(dc, reservationMark, -1, &reservationRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(dc, programText);
    }

    if (!row.IsFuture && row.HasFuture) {
        RECT arrowRect = layout.Arrow;
        const bool expanded = m_panelExpandedChannels.find(row.ChannelKey) != m_panelExpandedChannels.end();
        const COLORREF arrowBack = expanded ? MixColor(accentTop, accentBottom, 50) : MixColor(panelBack, accentTop, 18);
        FillSolid(dc, arrowRect, arrowBack);
        SetTextColor(dc, expanded ? accentText : panelText);
        const wchar_t *arrow = expanded ? L"△" : L"▽";
        DrawTextW(dc, arrow, -1, &arrowRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(dc, programText);
    }

    RECT separator = draw->rcItem;
    separator.top = separator.bottom - 1;
    const bool expandedHeader = !row.IsFuture
        && m_panelExpandedChannels.find(row.ChannelKey) != m_panelExpandedChannels.end();
    FillSolid(dc, separator, MixColor(border, panelBack, expandedHeader ? 68 : 35));

    if (oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }
}

LRESULT CALLBACK CViewReservationPlugin::PanelListProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<CViewReservationPlugin*>(GetPropW(hwnd, L"ViewReservation.PanelSelf"));
    if (self == nullptr || self->m_panelListOriginalProc == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    if (message == WM_ERASEBKGND) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        COLORREF back = self->m_pApp != nullptr ? self->m_pApp->GetColor(L"PanelBack") : CLR_INVALID;
        if (back == CLR_INVALID) back = GetSysColor(COLOR_WINDOW);
        FillSolid(dc, rc, back);
        return 1;
    }

    if (message == WM_MOUSEMOVE) {
        if (!self->m_panelMouseTracking) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            if (TrackMouseEvent(&track)) {
                self->m_panelMouseTracking = true;
            }
        }

        POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        int hover = -1;
        if (HIWORD(hit) == 0) {
            const int index = LOWORD(hit);
            if (index >= 0 && static_cast<size_t>(index) < self->m_panelRows.size()) {
                hover = index;
            }
        }

        if (hover != self->m_panelHoverRow) {
            const int oldHover = self->m_panelHoverRow;
            self->m_panelHoverRow = hover;
            self->m_panelMarqueeActive = false;
            self->m_panelMarqueeEpochTick = GetTickMs();
            if (self->m_panelWindow != nullptr) {
                KillTimer(self->m_panelWindow, kPanelMarqueeTimerId);
                KillTimer(self->m_panelWindow, kPanelMarqueeDelayTimerId);
            }

            auto invalidateRow = [&](int index) {
                if (index < 0 || static_cast<size_t>(index) >= self->m_panelRows.size()) return;
                RECT item{};
                if (SendMessageW(hwnd, LB_GETITEMRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&item)) != LB_ERR) {
                    InvalidateRect(hwnd, &item, FALSE);
                }
            };
            invalidateRow(oldHover);
            invalidateRow(hover);

            if (self->m_panelWindow != nullptr && hover >= 0 &&
                static_cast<size_t>(hover) < self->m_panelRows.size() &&
                self->m_panelRows[static_cast<size_t>(hover)].NeedsMarquee) {
                // マウスが横切っただけでは動かさず、一定時間留まった行だけマーキーを開始する。
                SetTimer(self->m_panelWindow, kPanelMarqueeDelayTimerId, kPanelMarqueeHoverDelayMs, nullptr);
            }
        }
        return CallWindowProcW(self->m_panelListOriginalProc, hwnd, message, wParam, lParam);
    }

    if (message == WM_MOUSELEAVE) {
        const int oldHover = self->m_panelHoverRow;
        self->m_panelMouseTracking = false;
        self->m_panelHoverRow = -1;
        self->m_panelMarqueeActive = false;
        if (self->m_panelWindow != nullptr) {
            KillTimer(self->m_panelWindow, kPanelMarqueeTimerId);
            KillTimer(self->m_panelWindow, kPanelMarqueeDelayTimerId);
        }
        if (oldHover >= 0 && static_cast<size_t>(oldHover) < self->m_panelRows.size()) {
            RECT item{};
            if (SendMessageW(hwnd, LB_GETITEMRECT, static_cast<WPARAM>(oldHover), reinterpret_cast<LPARAM>(&item)) != LB_ERR) {
                InvalidateRect(hwnd, &item, FALSE);
            }
        }
        return 0;
    }

    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_RBUTTONUP) {
        POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        const int index = LOWORD(hit);
        const bool outside = HIWORD(hit) != 0;
        if (!outside && index >= 0 && static_cast<size_t>(index) < self->m_panelRows.size()) {
            if (message == WM_LBUTTONDOWN) {
                RECT itemRect{};
                SendMessageW(hwnd, LB_GETITEMRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&itemRect));
                const auto &row = self->m_panelRows[static_cast<size_t>(index)];
                // 見た目の▽/△は細いまま、クリック領域だけ広く確保する。
                const int arrowLeft = static_cast<int>(itemRect.right) - 4 - kPanelArrowHitWidth;
                if (!row.IsFuture && row.HasFuture && pt.x >= arrowLeft) {
                    self->TogglePanelChannelExpanded(static_cast<size_t>(index));
                    return 0;
                }
                SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
            } else if (message == WM_LBUTTONDBLCLK) {
                const auto &row = self->m_panelRows[static_cast<size_t>(index)];
                if (!row.IsFuture) {
                    self->ActivatePanelCurrentProgram(static_cast<size_t>(index));
                    return 0;
                }
            } else {
                SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
                POINT screen = pt;
                ClientToScreen(hwnd, &screen);
                self->ShowPanelReservationMenu(static_cast<size_t>(index), screen);
                return 0;
            }
        }
    }

    return CallWindowProcW(self->m_panelListOriginalProc, hwnd, message, wParam, lParam);
}

LRESULT CALLBACK CViewReservationPlugin::PanelWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<CViewReservationPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<CViewReservationPlugin*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message) {
    case WM_CREATE: {
        const DWORD waveStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
        CreateWindowExW(0, L"BUTTON", L"地上波", waveStyle | WS_GROUP,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlWaveGr)), g_hinstDLL, nullptr);
        CreateWindowExW(0, L"BUTTON", L"BS", waveStyle,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlWaveBs)), g_hinstDLL, nullptr);
        CreateWindowExW(0, L"BUTTON", L"CS", waveStyle,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlWaveCs)), g_hinstDLL, nullptr);
        CreateWindowExW(0, L"BUTTON", L"CATV", waveStyle,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlWaveCatv)), g_hinstDLL, nullptr);
        HWND catvButton = GetDlgItem(hwnd, kControlWaveCatv);
        if (catvButton != nullptr) ShowWindow(catvButton, SW_HIDE);

        self->m_panelList = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
            LBS_DISABLENOSCROLL | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlList)), g_hinstDLL, nullptr);

        if (self->m_panelList != nullptr) {
            SetPropW(self->m_panelList, L"ViewReservation.PanelSelf", self);
            self->m_panelListOriginalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                self->m_panelList, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PanelListProc)));
        }

        self->m_panelStatus = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlStatus)), g_hinstDLL, nullptr);

        self->m_panelMarqueeEpochTick = GetTickMs();
        self->UpdatePanelFont();

        if (self->m_pApp != nullptr) {
            const COLORREF panelBack = self->m_pApp->GetColor(L"PanelBack");
            if (panelBack != CLR_INVALID) {
                const bool dark = self->m_pApp->IsDarkModeColor(panelBack);
                self->m_pApp->SetWindowDarkMode(hwnd, dark);
                if (self->m_panelList != nullptr) self->m_pApp->SetWindowDarkMode(self->m_panelList, dark);
                if (self->m_panelStatus != nullptr) self->m_pApp->SetWindowDarkMode(self->m_panelStatus, dark);
            }
        }
        return 0;
    }


    case WM_CTLCOLORLISTBOX: {
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == self->m_panelList) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            COLORREF back = self->m_pApp != nullptr ? self->m_pApp->GetColor(L"PanelBack") : CLR_INVALID;
            COLORREF text = self->m_pApp != nullptr ? self->m_pApp->GetColor(L"PanelText") : CLR_INVALID;
            if (back == CLR_INVALID) back = GetSysColor(COLOR_WINDOW);
            if (text == CLR_INVALID) text = GetSysColor(COLOR_WINDOWTEXT);
            SetBkColor(dc, back);
            SetTextColor(dc, text);
            if (self->m_panelBackBrush == nullptr || self->m_panelBackBrushColor != back) {
                if (self->m_panelBackBrush != nullptr) DeleteObject(self->m_panelBackBrush);
                self->m_panelBackBrush = CreateSolidBrush(back);
                self->m_panelBackBrushColor = back;
            }
            return reinterpret_cast<LRESULT>(self->m_panelBackBrush);
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == self->m_panelStatus) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            COLORREF back = self->m_pApp != nullptr ? self->m_pApp->GetColor(L"PanelBack") : CLR_INVALID;
            COLORREF text = self->m_pApp != nullptr ? self->m_pApp->GetColor(L"PanelText") : CLR_INVALID;
            if (back == CLR_INVALID) back = GetSysColor(COLOR_WINDOW);
            if (text == CLR_INVALID) text = GetSysColor(COLOR_WINDOWTEXT);
            SetBkColor(dc, back);
            SetTextColor(dc, text);
            if (self->m_panelBackBrush == nullptr || self->m_panelBackBrushColor != back) {
                if (self->m_panelBackBrush != nullptr) DeleteObject(self->m_panelBackBrush);
                self->m_panelBackBrush = CreateSolidBrush(back);
                self->m_panelBackBrushColor = back;
            }
            return reinterpret_cast<LRESULT>(self->m_panelBackBrush);
        }
        break;
    }

    case WM_MEASUREITEM: {
        auto *measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure != nullptr && measure->CtlID == kControlList) {
            // 現在番組はコンパクトさを維持し、未来番組だけ少し縦余白を広げる。
            if (measure->itemID != static_cast<UINT>(-1) &&
                static_cast<size_t>(measure->itemID) < self->m_panelRows.size() &&
                self->m_panelRows[measure->itemID].IsFuture) {
                measure->itemHeight = kPanelFutureRowHeight;
            } else {
                measure->itemHeight = kPanelCurrentRowHeight;
            }
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto *draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw != nullptr && draw->CtlID == kControlList) {
            self->DrawPanelRow(draw);
            return TRUE;
        }
        if (draw != nullptr && (draw->CtlID == kControlWaveGr || draw->CtlID == kControlWaveBs || draw->CtlID == kControlWaveCs || draw->CtlID == kControlWaveCatv)) {
            self->DrawPanelWaveButton(draw);
            return TRUE;
        }
        break;
    }

    case WM_SIZE:
        self->LayoutPanel(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_TIMER:
        if (wParam == kPanelRefreshTimerId) {
            self->RefreshPanelRows(false);
            return 0;
        }
        if (wParam == kPanelMarqueeDelayTimerId) {
            KillTimer(hwnd, kPanelMarqueeDelayTimerId);
            if (self->m_panelList != nullptr && self->m_panelHoverRow >= 0 &&
                static_cast<size_t>(self->m_panelHoverRow) < self->m_panelRows.size() &&
                self->m_panelRows[static_cast<size_t>(self->m_panelHoverRow)].NeedsMarquee) {
                self->m_panelMarqueeActive = true;
                self->m_panelMarqueeEpochTick = GetTickMs();
                RECT item{};
                if (SendMessageW(self->m_panelList, LB_GETITEMRECT,
                    static_cast<WPARAM>(self->m_panelHoverRow), reinterpret_cast<LPARAM>(&item)) != LB_ERR) {
                    InvalidateRect(self->m_panelList, &item, FALSE);
                }
                SetTimer(hwnd, kPanelMarqueeTimerId, kPanelMarqueeIntervalMs, nullptr);
            }
            return 0;
        }
        if (wParam == kPanelMarqueeTimerId) {
            if (self->m_panelMarqueeActive && self->m_panelList != nullptr && self->m_panelHoverRow >= 0 &&
                static_cast<size_t>(self->m_panelHoverRow) < self->m_panelRows.size() &&
                self->m_panelRows[static_cast<size_t>(self->m_panelHoverRow)].NeedsMarquee) {
                RECT item{};
                if (SendMessageW(self->m_panelList, LB_GETITEMRECT,
                    static_cast<WPARAM>(self->m_panelHoverRow), reinterpret_cast<LPARAM>(&item)) != LB_ERR) {
                    // ホバー中の1行について、通常描画と同じレイアウト計算からタイトル列だけを更新する。
                    HDC layoutDc = GetDC(self->m_panelList);
                    HFONT layoutFont = reinterpret_cast<HFONT>(SendMessageW(self->m_panelList, WM_GETFONT, 0, 0));
                    HGDIOBJ oldLayoutFont = nullptr;
                    if (layoutDc != nullptr && layoutFont != nullptr) {
                        oldLayoutFont = SelectObject(layoutDc, layoutFont);
                    }
                    const PanelRowLayout layout = self->CalculatePanelRowLayout(item, layoutDc);
                    if (layoutDc != nullptr && oldLayoutFont != nullptr) {
                        SelectObject(layoutDc, oldLayoutFont);
                    }
                    if (layoutDc != nullptr) {
                        ReleaseDC(self->m_panelList, layoutDc);
                    }

                    RECT dirty = layout.Content;
                    dirty.left = layout.TitleLeft;
                    if (dirty.right > dirty.left) {
                        InvalidateRect(self->m_panelList, &dirty, FALSE);
                    }
                }
            } else {
                self->m_panelMarqueeActive = false;
                KillTimer(hwnd, kPanelMarqueeTimerId);
            }
            return 0;
        }
        break;

    case WM_ERASEBKGND: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        COLORREF back = self->m_pApp != nullptr ? self->m_pApp->GetColor(L"PanelBack") : CLR_INVALID;
        if (back == CLR_INVALID) back = GetSysColor(COLOR_WINDOW);
        FillSolid(dc, rc, back);
        return 1;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notify = HIWORD(wParam);
        if (id == kControlWaveGr && notify == BN_CLICKED) {
            self->SetPanelWave(PanelWave::Terrestrial);
            return 0;
        }
        if (id == kControlWaveBs && notify == BN_CLICKED) {
            self->SetPanelWave(PanelWave::Bs);
            return 0;
        }
        if (id == kControlWaveCs && notify == BN_CLICKED) {
            self->SetPanelWave(PanelWave::Cs);
            return 0;
        }
        if (id == kControlWaveCatv && notify == BN_CLICKED && self->m_panelCatvAvailable) {
            self->SetPanelWave(PanelWave::Catv);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        KillTimer(hwnd, kPanelRefreshTimerId);
        KillTimer(hwnd, kPanelMarqueeTimerId);
        KillTimer(hwnd, kPanelMarqueeDelayTimerId);
        if (self->m_panelList != nullptr && self->m_panelListOriginalProc != nullptr) {
            SetWindowLongPtrW(self->m_panelList, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(self->m_panelListOriginalProc));
            RemovePropW(self->m_panelList, L"ViewReservation.PanelSelf");
        }
        if (self->m_panelFont != nullptr) {
            DeleteObject(self->m_panelFont);
            self->m_panelFont = nullptr;
        }
        if (self->m_panelBackBrush != nullptr) {
            DeleteObject(self->m_panelBackBrush);
            self->m_panelBackBrush = nullptr;
            self->m_panelBackBrushColor = CLR_INVALID;
        }
        if (self->m_panelWindow == hwnd) {
            self->m_panelWindow = nullptr;
            self->m_panelList = nullptr;
            self->m_panelStatus = nullptr;
            self->m_panelListOriginalProc = nullptr;
            self->m_panelRows.clear();
            self->m_panelExpandedChannels.clear();
        }
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void CViewReservationPlugin::Log(const std::wstring &text) {
    if (m_pApp != nullptr) {
        m_pApp->AddLog(text.c_str());
    }
}

void CViewReservationPlugin::ShowReservationError(const std::wstring &message) const {
    if (message.empty()) {
        return;
    }
    HWND owner = m_pApp != nullptr ? m_pApp->GetAppWindow() : nullptr;
    MessageBoxW(owner, message.c_str(), L"ViewReservation", MB_OK | MB_ICONWARNING);
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

    // 予約状態を取得（番組情報更新でIDが変わっている場合はOriginalIDでも照合）
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
    // EventIDは番組情報更新で変わる場合があるためOriginalIDも照合し、
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

    bool reservationChanged = false;
    if (foundInDb) {
        reservationChanged = m_manager->RemoveReservation(
            item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, &message);
    } else {
        reservationChanged = m_manager->AddOrUpdateReservation(item, &message);
    }

    if (!message.empty()) {
        Log(message);
    }
    if (!reservationChanged && !foundInDb) {
        ShowReservationError(message);
        return true;
    }

    if (m_manager->IsReserved(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID)) {
        const ULONGLONG secUntil = SecondsUntil(item.StartTime);
        if (secUntil == 0) {
            const std::wstring triggerKey = MakeTriggerKey(item);
            if (ClaimTriggerKey(triggerKey)) {
                const ReservationTriggerResult result = TrySwitchReservation(item, triggerKey);
                if (result == ReservationTriggerResult::Completed) {
                    m_manager->MarkTriggered(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID);
                } else if (result == ReservationTriggerResult::Failed ||
                           result == ReservationTriggerResult::WaitingForRecording) {
                    ReleaseTriggerKey(triggerKey);
                }
            }
        }
    }

    RefreshPanelRows(false);
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

        m_cv.wait_for(lock, std::chrono::milliseconds(waitMs));
    }
}

ULONGLONG CViewReservationPlugin::ComputeNextWakeDelayMs() const {
    // 録画要求のサービス到達待ちは短周期で確認する。固定待機はしない。
    if (HasPendingRecording()) {
        return kMinSleepMs;
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

    // Upcoming（次の番組）をActiveNow（現在放送中）より優先する。
    // 録画中は既存録画を停止せず、後続の録画要求は開始しない。
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

bool CViewReservationPlugin::ClaimTriggerKey(const std::wstring &key) {
    std::lock_guard<std::mutex> lock(m_sessionTriggeredMutex);
    return m_sessionTriggeredKeys.insert(key).second;
}

void CViewReservationPlugin::ReleaseTriggerKey(const std::wstring &key) {
    std::lock_guard<std::mutex> lock(m_sessionTriggeredMutex);
    m_sessionTriggeredKeys.erase(key);
}


void CViewReservationPlugin::RunMaintenanceTick() {
    if (m_manager == nullptr || m_pApp == nullptr) {
        return;
    }

    try {
        // チャンネル切替後は、目的サービスへ到達したことを確認してから録画する。
        TryCompletePendingRecording();

        m_manager->RemoveExpired();

        if (IsStartupEpgWaitActive()) {
            if (!m_epgWaitLogged) {
                m_epgWaitLogged = true;
            }
        } else {
            if (m_epgWaitLogged) {
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

        // INIに保存されたTriggeredフラグも確認（再起動後の再実行防止）
        if (item.Triggered)
            return;

        if (!ClaimTriggerKey(triggerKey))
            return;

        const ReservationTriggerResult result = TrySwitchReservation(item, triggerKey);
        if (result == ReservationTriggerResult::Completed) {
            m_manager->MarkTriggered(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID);
        } else if (result == ReservationTriggerResult::Failed ||
                   result == ReservationTriggerResult::WaitingForRecording) {
            ReleaseTriggerKey(triggerKey);
        }
    } catch (...) {
        Log(L"[視聴予約] 予約処理中にエラーが発生しました");
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
            continue;
        }

        if (secUntil <= 10 * 60) {
            if (m_manager->RemoveReservation(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID)) {
                Log(L"[視聴予約] 番組表で番組を確認できなかったため予約を解除しました: " + item.EventName);
            }
        } else {
        }
    }
}

bool CViewReservationPlugin::IsCurrentDriverPreferred(const ReservationItem &item) const {
    if (item.LastDriverName.empty() || m_pApp == nullptr) {
        return true;
    }

    wchar_t currentDriver[MAX_PATH]{};
    if (m_pApp->GetDriverName(currentDriver, MAX_PATH) <= 0) {
        return true;
    }
    return _wcsicmp(currentDriver, item.LastDriverName.c_str()) == 0;
}

bool CViewReservationPlugin::TrySelectOnCurrentDriver(const ReservationItem &item) {
    if (!IsCurrentDriverPreferred(item)) {
        return false;
    }

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
    auto findTarget = [&]() {
        if (!item.LastDriverName.empty() &&
            m_driverCache.FindExactOnDriver(
                item.NetworkID, item.TransportStreamID, item.ServiceID, item.LastDriverName, &found)) {
            return true;
        }
        return m_driverCache.FindExact(item.NetworkID, item.TransportStreamID, item.ServiceID, &found);
    };

    if (!findTarget()) {
        m_driverCache.RebuildIfNeeded(m_pApp, true);
        if (!findTarget()) {
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

CViewReservationPlugin::ReservationTriggerResult CViewReservationPlugin::TrySwitchReservation(
    ReservationItem &item,
    const std::wstring &triggerKey) {
    const wchar_t *actionLabel = item.RecordOnTrigger ? L"録画予約" : L"視聴予約";
    const std::wstring prefix = L"[" + std::wstring(actionLabel) + L"] ";

    // 自動予約では30秒前から準備するが、現在録画中はチャンネル切替を要求しない。
    // 録画終了後の次回監視で同じ予約を再評価し、そこから視聴/録画の切替を行う。
    // 手動の即時チャンネル切替は TrySwitchToItem() を直接使うため、この待機規則の対象外。
    if (IsRecordingActive()) {
        return ReservationTriggerResult::WaitingForRecording;
    }

    // 視聴予約はチャンネル切替成功を完了条件とする。
    if (!item.RecordOnTrigger) {
        return TrySwitchToItem(item, actionLabel)
            ? ReservationTriggerResult::Completed
            : ReservationTriggerResult::Failed;
    }

    // 録画予約は StartRecord 成功だけを完了条件とする。
    // 既存録画には触れず、実行済みにもしない。

    TVTest::ChannelInfo currentChannel{};
    currentChannel.Size = sizeof(currentChannel);
    const bool onSameService =
        m_pApp->GetCurrentChannelInfo(&currentChannel) &&
        IsCurrentDriverPreferred(item) &&
        currentChannel.NetworkID == item.NetworkID &&
        currentChannel.TransportStreamID == item.TransportStreamID &&
        currentChannel.ServiceID == item.ServiceID;

    if (onSameService) {
        return TryStartRecording(item, actionLabel)
            ? ReservationTriggerResult::Completed
            : ReservationTriggerResult::Failed;
    }

    if (!BeginPendingRecording(item, actionLabel, triggerKey)) {
        Log(prefix + L"別の録画予約を処理中のため開始できません: " + item.EventName);
        return ReservationTriggerResult::Failed;
    }

    if (TrySelectOnCurrentDriver(item)) {
        m_cv.notify_all();
        return ReservationTriggerResult::Pending;
    }

    if (TrySelectByEnumeratingDrivers(item)) {
        m_cv.notify_all();
        return ReservationTriggerResult::Pending;
    }

    ClearPendingRecording();
    Log(prefix + L"チャンネルを切り替えられませんでした: " + item.EventName);
    return ReservationTriggerResult::Failed;
}

bool CViewReservationPlugin::TrySwitchToItem(ReservationItem &item, const wchar_t *actionLabel) {
    const std::wstring prefix = L"[" + std::wstring(actionLabel != nullptr ? actionLabel : L"操作") + L"] ";

    // 現在局は NID / TSID / SID の3点で照合する。
    TVTest::ChannelInfo currentChannel{};
    currentChannel.Size = sizeof(currentChannel);
    const bool onSameService =
        m_pApp->GetCurrentChannelInfo(&currentChannel) &&
        IsCurrentDriverPreferred(item) &&
        currentChannel.NetworkID == item.NetworkID &&
        currentChannel.TransportStreamID == item.TransportStreamID &&
        currentChannel.ServiceID == item.ServiceID;

    if (onSameService) {
        return true;
    }

    if (TrySelectOnCurrentDriver(item)) {
        return true;
    }

    if (TrySelectByEnumeratingDrivers(item)) {
        return true;
    }

    Log(prefix + L"チャンネルを切り替えられませんでした: " + item.EventName);
    return false;
}

bool CViewReservationPlugin::IsRecordingActive() const {
    TVTest::RecordStatusInfo statusInfo{};
    statusInfo.Size = sizeof(statusInfo);
    return m_pApp != nullptr &&
           m_pApp->GetRecordStatus(&statusInfo) &&
           statusInfo.Status == TVTest::RECORD_STATUS_RECORDING;
}

bool CViewReservationPlugin::BeginPendingRecording(
    const ReservationItem &item,
    const wchar_t *actionLabel,
    const std::wstring &triggerKey) {
    std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
    if (m_pendingRecord.Active) {
        return false;
    }
    m_pendingRecord.Active = true;
    m_pendingRecord.StartedTick = GetTickMs();
    m_pendingRecord.Item = item;
    m_pendingRecord.Label = actionLabel != nullptr ? actionLabel : L"録画";
    m_pendingRecord.TriggerKey = triggerKey;
    m_cv.notify_all();
    return true;
}

void CViewReservationPlugin::ClearPendingRecording() {
    std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
    m_pendingRecord = PendingRecordRequest{};
}

bool CViewReservationPlugin::HasPendingRecording() const {
    std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
    return m_pendingRecord.Active;
}

void CViewReservationPlugin::TryCompletePendingRecording() {
    PendingRecordRequest snapshot{};
    {
        std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
        if (!m_pendingRecord.Active) {
            return;
        }
        snapshot = m_pendingRecord;
    }

    const ULONGLONG elapsed = GetTickMs() - snapshot.StartedTick;
    if (elapsed >= kRecordSwitchTimeoutMs) {
        bool expired = false;
        {
            std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
            if (m_pendingRecord.Active && m_pendingRecord.StartedTick == snapshot.StartedTick) {
                m_pendingRecord = PendingRecordRequest{};
                expired = true;
            }
        }
        if (expired) {
            Log(L"[" + snapshot.Label + L"] 予約先のチャンネルへ切り替えられなかったため録画を開始できません: " + snapshot.Item.EventName);
            ReleaseTriggerKey(snapshot.TriggerKey);
        }
        return;
    }

    TVTest::ChannelInfo current{};
    current.Size = sizeof(current);
    if (m_pApp == nullptr ||
        !m_pApp->GetCurrentChannelInfo(&current) ||
        current.NetworkID != snapshot.Item.NetworkID ||
        current.TransportStreamID != snapshot.Item.TransportStreamID ||
        current.ServiceID != snapshot.Item.ServiceID) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
        if (!m_pendingRecord.Active || m_pendingRecord.StartedTick != snapshot.StartedTick) {
            return;
        }
        m_pendingRecord = PendingRecordRequest{};
    }

    if (IsRecordingActive()) {
        Log(L"[" + snapshot.Label + L"] 録画中のため開始できません: " + snapshot.Item.EventName);
        ReleaseTriggerKey(snapshot.TriggerKey);
        return;
    }

    if (TryStartRecording(snapshot.Item, snapshot.Label.c_str())) {
        m_manager->MarkTriggered(
            snapshot.Item.NetworkID,
            snapshot.Item.TransportStreamID,
            snapshot.Item.ServiceID,
            snapshot.Item.EventID);
    } else {
        ReleaseTriggerKey(snapshot.TriggerKey);
    }
}

bool CViewReservationPlugin::OnChannelChange() {
    m_cv.notify_all();
    return true;
}

bool CViewReservationPlugin::OnServiceChange() {
    m_cv.notify_all();
    return true;
}

bool CViewReservationPlugin::TryStartRecording(const ReservationItem &item, const wchar_t *actionLabel) {
    const std::wstring prefix = L"[" + std::wstring(actionLabel != nullptr ? actionLabel : L"録画") + L"] ";
    // 既存録画は停止しない。開始直前にも再確認する。
    if (IsRecordingActive()) {
        Log(prefix + L"録画中のため開始できません: " + item.EventName);
        return false;
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
        Log(prefix + L"番組が終了しているため録画を開始できません: " + item.EventName);
        return false;
    }

    info.StopTimeSpec = TVTest::RECORD_STOP_DURATION;
    info.StopTime.Duration = msUntilEnd;

    if (m_pApp->StartRecord(&info)) {
        Log(prefix + L"録画開始: " + item.EventName);
        return true;
    }

    Log(prefix + L"録画を開始できませんでした: " + item.EventName);
    return false;
}

TVTest::CTVTestPlugin *CreatePluginClass() {
    return new CViewReservationPlugin();
}
